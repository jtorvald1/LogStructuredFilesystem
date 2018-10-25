//INCLUDE

#include <fuse.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h>
#include <time.h>
#include <utime.h>

//DEFINE

#define SEGMENT_SIZE 262144             //segment is 1/4 of 1MB. 256 blocks per segment.
#define NUMBER_OF_SEGMENTS 4            //4 segments makes 1MB
#define BLOCK_SIZE 1024                 //each block is 1kb
#define NUMBER_OF_INODES 256            //we can use 32 blocks of each segment to hold inodes.
#define MAX_LENGTH 56                   //max length to limit size of inodes to 128 bytes
#define NUMBER_OF_DATAPOINTERS 8        
#define NUMBER_OF_INDIRECTPOINTERS 32   
#define HARDDISK "/home/tjohn16/dm510/project4/harddisk.txt"

//STRUCT INODE

typedef struct inode {
  int ID;                                   //ID number, also the number in the inode array
  int type;                                 //0 = directory, 1 = file
  char name[MAX_LENGTH];                    //directory or file name, max length is 56
  int size;                                 //filesize
  time_t modify;                            //modification time stamp
  time_t access;                            //access time stamp
  int datapointer[NUMBER_OF_DATAPOINTERS];  //8 datapointers
  int indirectDataPointer;                  //indirect data pointer
} inode;                                    //inode size is 128 bytes

//DEFINE METHODS

int lfs_init(void);
int lfs_getattr( const char *, struct stat * );
int lfs_readdir( const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info * );
int lfs_mknod(const char *, mode_t, dev_t);
int lfs_mkdir(const char *, mode_t);
int lfs_unlink(const char *);
int lfs_rmdir(const char *);
int lfs_rename(const char* from, const char* to);
int lfs_truncate(const char *path, off_t size);
int lfs_open( const char *, struct fuse_file_info * );
int lfs_read( const char *, char *, size_t, off_t, struct fuse_file_info * );
int lfs_write (const char *, const char *, size_t, off_t, struct fuse_file_info *);
int lfs_release(const char *path, struct fuse_file_info *fi);
int lfs_findInodeID(char *);
int lfs_createInode(char *, int);
int lfs_removeInode(char *);
int lfs_insertData(char *, int);
int lfs_cleaner(void);
int lfs_write_segment(int, const char *);
int lfs_utime(const char *, struct utimbuf *);

//STRUCT FUSE OPERATIONS

static struct fuse_operations lfs_oper = {
	.getattr	= lfs_getattr,          //get attribute
	.readdir	= lfs_readdir,          //read directory
	.mknod = lfs_mknod,               //make file
	.mkdir = lfs_mkdir,               //make directory
	.unlink = lfs_unlink,             //remove file
	.rmdir = lfs_rmdir,               //remove directory
	.rename = lfs_rename,             //change filename
	.truncate = lfs_truncate,         //change filesize
	.open	= lfs_open,                 //open file
	.read	= lfs_read,                 //read file
	.write = lfs_write,               //write file
	.release = lfs_release,           //release file
	.utime = lfs_utime                //change access time
};

//GLOBAL VARIABLES

int *lfs_inodeArray;
void *lfs_disk_in_memory;
int lfs_segment;
int lfs_block;

//INIT METHOD

int lfs_init(void){
  printf("///  INITIALIZING FILE SYSTEM  ///\n");
  
  
  //allocate memory for the array of inodes
  lfs_inodeArray = malloc(NUMBER_OF_INODES * sizeof(inode));
  //allocate 1mb of memory for the disk
  lfs_disk_in_memory = malloc(NUMBER_OF_SEGMENTS * SEGMENT_SIZE);
  
  //create harddisk
  char harddisk[NUMBER_OF_SEGMENTS*SEGMENT_SIZE];
  int h;
  for (h=0; h<(NUMBER_OF_SEGMENTS*SEGMENT_SIZE); h++) {
    harddisk[h] = '0';
  }
  
  int file;
  file = creat(HARDDISK, 0777);
  write(file, harddisk, (NUMBER_OF_SEGMENTS*SEGMENT_SIZE));
  
  //set current block and segment 
  lfs_segment = 0;
  lfs_block = 32;
  
  //initialize array of inodes
  int i;
  for(i=0; i<NUMBER_OF_INODES; i++) {
    lfs_inodeArray[i]=-1;
  }
  
  //create root inode 
  inode *lfs_root;
  lfs_root = malloc(sizeof(inode));
  
  lfs_root->ID = 0;
  lfs_root->type = 0;
  memset(lfs_root->name, 0, 2);
  memcpy(lfs_root->name, "/", 2);
  lfs_root->size = 0;
  lfs_root->modify = time(NULL);
  lfs_root->access = time(NULL);
  
  int j;
  for(j=0; j<NUMBER_OF_DATAPOINTERS; j++) {
    lfs_root->datapointer[j] = -1;
  }
  
  lfs_root->indirectDataPointer = -1;
  
  //insert root inode into the disk in memory
  memset(lfs_disk_in_memory + (lfs_block*BLOCK_SIZE), 0, sizeof(inode));
  memcpy(lfs_disk_in_memory + (lfs_block*BLOCK_SIZE), (char *) lfs_root, sizeof(inode));
  lfs_inodeArray[0] = lfs_block;
  
  //go to next block 
  lfs_block++;
  
  printf("FILE SYSTEM INITIALIZED\n");
  
  return 0;
}

//GET ATTRIBUTE METHOD

int lfs_getattr( const char *path, struct stat *stbuf ) {
	printf("getattr method called\n");

	memset(stbuf, 0, sizeof(struct stat));
	
	//create an inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));

  //find the inode
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(path);
  
  //if there is no inode
  if(lfs_inodeID == -1) {
    free(lfs_inode);
    
    return -ENOENT;
  } else {
    //get the inode
    memset(lfs_inode, 0, sizeof(inode));
    memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));
    
    //if the inode is a directory
    if(lfs_inode->type == 0) {
      stbuf->st_mode = S_IFDIR | 0777;
      stbuf->st_atime = lfs_inode->access;
  		stbuf->st_mtime = lfs_inode->modify;
		  
		  return 0;
  	} else if (lfs_inode->type == 1){
  	  stbuf->st_mode = S_IFREG | 0777;
  	  stbuf->st_atime = lfs_inode->access;
  		stbuf->st_mtime = lfs_inode->modify;
		  stbuf->st_size = lfs_inode->size;
		  
		  free(lfs_inode);
		  
		  return 0;
  	}
  }
  return 0;
}

//READ DIRECTORY METHOD

int lfs_readdir( const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi ) {
	printf("readdir method called\n");

  filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(path);
  
  //get the inode
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  memset(lfs_inode, 0, sizeof(inode));
  memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));
  
  //read through the inode's datapointers
  int i;
  for(i=0; i<NUMBER_OF_DATAPOINTERS; i++) {
    if(lfs_inode->datapointer[i] != -1) {
      filler(buf, ((inode*)(lfs_disk_in_memory + (lfs_inodeArray[lfs_inode->datapointer[i]] * BLOCK_SIZE)))->name, NULL, 0);
    }
  }
  
  //read through the inode's indirectDataPointers
  if(lfs_inode->indirectDataPointer != -1) {
    int *lfs_indirectPointersArray;
    lfs_indirectPointersArray = malloc(NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
    
    //get the array of indirectDataPointers from the disk in memory
    memset(lfs_indirectPointersArray, 0, NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
    memcpy(lfs_indirectPointersArray, lfs_disk_in_memory + (lfs_inode->indirectDataPointer * BLOCK_SIZE), NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
    
    //checking each indirectDataPointer
    int j;
    for(j=0; j<NUMBER_OF_INDIRECTPOINTERS; j++) {
      if(lfs_indirectPointersArray[j] != -1) {
        filler(buf, ((inode*)(lfs_disk_in_memory + (lfs_inodeArray[lfs_indirectPointersArray[j]] * BLOCK_SIZE)))->name, NULL, 0);
      }
    }
    free(lfs_indirectPointersArray);
  }
  free(lfs_inode);

	return 0;
}

//MKNOD METHOD

int lfs_mknod(const char * path, mode_t mode, dev_t dev) {
  int res;
  printf("mknod method called\n");
  
  //create the inode
  res = lfs_createInode(path, 1);
  
  return res;
}

//MKDIR METHOD

int lfs_mkdir(const char * path, mode_t mode) {
  int res;
  printf("mkdir method called\n");
  
  //create the inode
  res = lfs_createInode(path, 0);
  
  return res;
}

//UNLINK METHOD

int lfs_unlink(const char * path) {
  int res;
  printf("unlink method called\n");

  //remove the inode
  res = lfs_removeInode(path);
  
  return res;
}

//RMDIR METHOD

int lfs_rmdir(const char * path) {
  int res;
  printf("rmdir method called\n");

  //remove the inode
  res = lfs_removeInode(path);

  return res;
}

//RENAME METHOD

int lfs_rename(const char* from, const char* to) {
  printf("rename method called\n");
  
  //create an inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  
  //find the inode
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(from);
  
  //get the inode
  memset(lfs_inode, 0, sizeof(inode));
  memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));

  //set the inode name
  if(strlen(basename(to)) < MAX_LENGTH) {
    memset(lfs_inode->name, 0, strlen(basename(from)));
    memcpy(lfs_inode->name, basename(to), strlen(basename(to)));
  }
  else {
    memset(lfs_inode->name, 0, strlen(basename(from)));
    memcpy(lfs_inode->name, basename(to), MAX_LENGTH);
  }
  
  //insert the inode into the inode array
  lfs_inodeArray[lfs_inodeID] = lfs_insertData((char *) lfs_inode, sizeof(inode));
  
  free(lfs_inode);
  return 0;
}

//TRUNCATE METHOD

int lfs_truncate(const char *path, off_t size) {
  int res;
  printf("truncate method called\n");
  
  //create an inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  
  //find the inode
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(path);
  
  //get the inode
  memset(lfs_inode, 0, sizeof(inode));
  memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));
    
  //set new size
  lfs_inode->size = size;
  lfs_inodeArray[lfs_inodeID] = lfs_insertData((char *) lfs_inode, sizeof(inode));
  
  free(lfs_inode);
  
  return 0;
}

//OPEN METHOD

int lfs_open( const char *path, struct fuse_file_info *fi ) {
  int res;
  printf("open method called\n");

  //make sure inode exists
  res = lfs_findInodeID(path);
  if(res == -1) {
    return -ENOENT;
  }
  
	return 0;
}

//READ METHOD

int lfs_read( const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi ) {
  int remain;
  printf("read method called\n");
  
  //create an inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  
  //find the inode
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(path);
  
  //get the inode
  memset(lfs_inode, 0, sizeof(inode));
  memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));
  
  remain = lfs_inode->size;
  
  //read data
  int i=0;
  while(remain > BLOCK_SIZE && i<NUMBER_OF_DATAPOINTERS) {
    memset(buf + (i*BLOCK_SIZE), 0, BLOCK_SIZE);
    memcpy(buf + (i*BLOCK_SIZE), lfs_disk_in_memory + (lfs_inode->datapointer[i] * BLOCK_SIZE), BLOCK_SIZE);
    
    remain -= BLOCK_SIZE;
    i++;
  }
  
  if( i < NUMBER_OF_DATAPOINTERS) {
    //remaining data is within datapointers
    memset(buf + (i*BLOCK_SIZE), 0, remain);
    memcpy(buf + (i*BLOCK_SIZE), lfs_disk_in_memory + (lfs_inode->datapointer[i] * BLOCK_SIZE), remain);
    
    remain = lfs_inode->size;
    
    free(lfs_inode);
    
    return remain;
  }
  
  int *lfs_indirectPointersArray;
  lfs_indirectPointersArray = (int *)(lfs_disk_in_memory + (lfs_inode->indirectDataPointer * BLOCK_SIZE));
  
  int l = 0;
  while(remain > BLOCK_SIZE) {
    memset(buf + (i*BLOCK_SIZE), 0, BLOCK_SIZE);
    memcpy(buf + (i*BLOCK_SIZE), lfs_disk_in_memory + (lfs_indirectPointersArray[l] * BLOCK_SIZE), BLOCK_SIZE);
    remain -= BLOCK_SIZE;
    
    i++;
    l++;
  }
  
  memset(buf + (i*BLOCK_SIZE), 0, remain);
  memcpy(buf + (i*BLOCK_SIZE), lfs_disk_in_memory + (lfs_indirectPointersArray[l] * BLOCK_SIZE), remain);
  
  remain = lfs_inode->size;
	
	free(lfs_inode);
	
	return remain;
}

//WRITE METHOD

int lfs_write(const char * path, const char * buf, size_t size, off_t offset, struct fuse_file_info * fi) {
  int remain;
  printf("write method called\n");
  
  //create an inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  
  //find the inode
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(path);
  
  //get the inode
  memset(lfs_inode, 0, sizeof(inode));
  memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));
  
  remain = size;
  int i = 0;
  while(remain > BLOCK_SIZE && i < NUMBER_OF_DATAPOINTERS) {
    lfs_inode->datapointer[i] = lfs_insertData((char *) buf + (i*BLOCK_SIZE), BLOCK_SIZE);
    remain -= BLOCK_SIZE;
    i++;
    }
    
    if(i < NUMBER_OF_DATAPOINTERS) {
      //write remaining data to datapointer
      lfs_inode-> datapointer[i] = lfs_insertData((char *) buf + (i*BLOCK_SIZE), remain);
    } else {
      //more than one block of data remaining, write to indirectDataPointers
      int *lfs_indirectPointersArray = malloc(NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
      
      int l;
      for(l=0; l<NUMBER_OF_INDIRECTPOINTERS; l++) {
        lfs_indirectPointersArray[l] = -1;
      }
      int k = 0;
      while (remain > BLOCK_SIZE && k < NUMBER_OF_INDIRECTPOINTERS) {
        lfs_indirectPointersArray[k] = lfs_insertData((char *) buf + (i*BLOCK_SIZE), BLOCK_SIZE);
        remain -= BLOCK_SIZE;
        i++;
        k++;
      }
      if(k < NUMBER_OF_INDIRECTPOINTERS) {
        lfs_indirectPointersArray[k] = lfs_insertData((char *) buf + (i*BLOCK_SIZE), remain);
        lfs_inode->indirectDataPointer = lfs_insertData((char *) lfs_indirectPointersArray, NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
      }
      free(lfs_indirectPointersArray);
    }
    
  lfs_inode->size = size;
  lfs_inodeArray[lfs_inodeID] = lfs_insertData((char *) lfs_inode, sizeof(inode));
  
  free(lfs_inode);
  
  return size;
}


//RELEASE METHOD

int lfs_release(const char *path, struct fuse_file_info *fi) {
  int res;
	printf("release method called\n");
  
  //make sure inode exists
  res = lfs_findInodeID(path);
  if(res == -1) {
    return -ENOENT;
  }
	
	return 0;
}

//FIND INODE ID METHOD

int lfs_findInodeID(char *path) {
  int res = 0;
  printf("findInodeID method called\n");
  
  //check if the given path is root
  if(strcmp("/", path) == 0) {
    //the root will always be at index 0
    return 0;
  }
  
  //create inode pointer 
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  
  //if the given path is not root, we will recursively find the parent
  char *lfs_path;
  lfs_path = malloc(strlen(path)+1);
  memset(lfs_path, 0, strlen(path)+1);
  memcpy(lfs_path, path, strlen(path)+1);
  
  //find the parent inode
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(dirname(lfs_path));
  
  //get the parent inode
  memset(lfs_inode, 0, sizeof(inode));
  memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));
  
  //set the path back to the inode we're searching for
  memset(lfs_path, 0, strlen(path)+1);
  memcpy(lfs_path, path, strlen(path)+1);
  
  //checking the parent inode's datapointers 
  int i;
  for (i=0; i<NUMBER_OF_DATAPOINTERS; i++) {
    if(lfs_inode->datapointer[i] != -1) {
      //set the path back to the inode we're searching for
      memset(lfs_path, 0, strlen(path)+1);
      memcpy(lfs_path, path, strlen(path)+1);
      
      if(!strcmp(basename(lfs_path), ((inode *) (lfs_disk_in_memory + (lfs_inodeArray[lfs_inode->datapointer[i]]) * BLOCK_SIZE))->name)) {
        res = ((inode*) (lfs_disk_in_memory + (lfs_inodeArray[lfs_inode->datapointer[i]] * BLOCK_SIZE)))->ID;
        free(lfs_inode);
        free(lfs_path);
        
        //return the inode
        return res;
      }
    }
  }
  //checking the parent inode's indirectDataPointers
  if (lfs_inode->indirectDataPointer != -1) {
    int *lfs_indirectPointersArray;
    lfs_indirectPointersArray = (int *)(lfs_disk_in_memory + (lfs_inode->indirectDataPointer * BLOCK_SIZE));
    
    int j;
    for(j=0; j<NUMBER_OF_INDIRECTPOINTERS; j++) {
      if(lfs_indirectPointersArray[i] != -1) {
        memset(lfs_path, 0, strlen(path)+1);
        memcpy(lfs_path, path, strlen(path)+1);
        if(!strcmp(basename(lfs_path),((inode *)(lfs_disk_in_memory + (lfs_inodeArray[lfs_indirectPointersArray[i]])*BLOCK_SIZE))->name)) {
          res = ((inode *)(lfs_disk_in_memory + (lfs_inodeArray[lfs_indirectPointersArray[i]]*BLOCK_SIZE)))->ID;
          
          free(lfs_inode);
          free(lfs_path);
          return res;
        }
      }
    }
  }
  
  //no inode found
  free(lfs_inode);
  free(lfs_path);
  return -1;
}

//CREATE INODE METHOD

int lfs_createInode(char * path, int type) {
  printf("createInode method called\n");
  
  //create inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  memset(lfs_inode, 0, sizeof(inode));
  
  //copy the path
  char *lfs_path;
  lfs_path = malloc(strlen(path)+1);
  memset(lfs_path, 0, strlen(path)+1);
  memcpy(lfs_path, path, strlen(path)+1);
  
  //set the inode type
  lfs_inode->type = type;

  //set the inode name
  if(strlen(basename(lfs_path)) < MAX_LENGTH) {
    memset(lfs_inode->name, 0, strlen(basename(lfs_path)));
    memcpy(lfs_inode->name, basename(lfs_path), strlen(basename(lfs_path)));
  }
  else {
    memset(lfs_inode->name, 0, strlen(basename(lfs_path)));
    memcpy(lfs_inode->name, basename(lfs_path), MAX_LENGTH);
  }
  
  //set the modify and access stamps
  lfs_inode->modify = time(NULL);
  lfs_inode->access = time(NULL);
  
  //set the size
  lfs_inode->size = 0;
  
  int j;
  for(j=0; j<NUMBER_OF_DATAPOINTERS; j++) {
    lfs_inode->datapointer[j] = -1;
  }
  
  lfs_inode->indirectDataPointer = -1;
  
  //find a free space in the array of inodes
  int i;
  for(i=0; i<NUMBER_OF_INODES; i++) {
    if(lfs_inodeArray[i] == -1) {
      lfs_inode->ID = i;
      //insert the new inode into the array
      lfs_inodeArray[i] = lfs_insertData((char *) lfs_inode, sizeof(inode));
      
      //set the path back to the inode we're creating
      memset(lfs_path, 0, strlen(path)+1);
      memcpy(lfs_path, path, strlen(path)+1);
      
      //get the parent inode
      int lfs_inodeID;
      lfs_inodeID = lfs_findInodeID(dirname(lfs_path));
      
      memset(lfs_inode, 0, sizeof(inode));
      memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));
      
      //set the parent datapointer
      int j;
      for(j=0; j<NUMBER_OF_DATAPOINTERS; j++) {
        if(lfs_inode->datapointer[j] == -1) {
          lfs_inode->datapointer[j] = i;
          
          //reinsert the parent with the new inode set to its datapointer
          lfs_inodeArray[lfs_inodeID] = lfs_insertData((char *) lfs_inode, sizeof(inode));
          
          free(lfs_inode);
          free(lfs_path);
          
          return 0;
        }
      }
      
      //set the parent indirectDataPointer
      if(lfs_inode->indirectDataPointer == -1) {
        //no indirectDataPointers, need to create array
        int *lfs_arrayIndirectPointers;
        lfs_arrayIndirectPointers = malloc(NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
        int l;
        for(l=0; l<NUMBER_OF_INDIRECTPOINTERS; l++) {
          lfs_arrayIndirectPointers[l] = -1;
        }
        //set empty array to inode
        lfs_inode->indirectDataPointer = lfs_insertData((char *) lfs_arrayIndirectPointers, NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
        
        free(lfs_arrayIndirectPointers);
      }
      //get the parent indirectDataPointers array
      int *lfs_indirectPointersArray;
      lfs_indirectPointersArray = lfs_disk_in_memory + (lfs_inode->indirectDataPointer * BLOCK_SIZE);
      
      int m;
      for(m = 0; m<NUMBER_OF_INDIRECTPOINTERS; m++) {
        if (lfs_indirectPointersArray[m] == -1) {
          lfs_indirectPointersArray[m] = i;
          //set the updated indirectDataPointers array
          lfs_inode->indirectDataPointer = lfs_insertData((char *) lfs_indirectPointersArray, NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
          //insert the updated parent inode back into the array
          lfs_inodeArray[lfs_inodeID] = lfs_insertData((char *) lfs_inode, sizeof(inode));
          
          free(lfs_indirectPointersArray);
          free(lfs_inode);
          free(lfs_path);
          
          return 0;
        }
      }
    } /*No space for a new inode*/ else {
      if (i == NUMBER_OF_INODES-1) {
        free(lfs_inode);
        free(lfs_path);
        
        return -ENOMEM;
      }
    }
  }
  
  free(lfs_inode);
  free(lfs_path);
  
  return 0;
}

//REMOVE INODE METHOD

int lfs_removeInode(char * path) {
  printf("remove inode method called\n");
  
  //find the inode
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(path);
  
  //find the parent inode
  int lfs_parentInodeID;
  lfs_parentInodeID = lfs_findInodeID(dirname(path));
  
  //create an inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  
  //get the parent inode
  memset(lfs_inode, 0, sizeof(inode));
  memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_parentInodeID] * BLOCK_SIZE), sizeof(inode));
  
  //remove the inode datapointer from the parent inode
  int i;
  for (i=0; i<NUMBER_OF_DATAPOINTERS; i++) {
    if(lfs_inode->datapointer[i] == lfs_inodeID) {
      lfs_inode->datapointer[i] = -1;
    }
  }
  
  //check indirectDataPointers
  if(lfs_inode->indirectDataPointer != -1) {
    int *lfs_indirectPointersArray;
    lfs_indirectPointersArray = lfs_disk_in_memory + (lfs_inode->indirectDataPointer * BLOCK_SIZE);
    
    int j;
    for(j=0; j<NUMBER_OF_INDIRECTPOINTERS; j++) {
      if(lfs_indirectPointersArray[j] == lfs_inodeID) {
        lfs_indirectPointersArray[j] = -1;
        lfs_inode->indirectDataPointer = lfs_insertData((char *) lfs_indirectPointersArray, NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
      }
    }
  }
  
  //insert the updated parent inode into array of inodes, and remove the original inode from the array
  lfs_inodeArray[lfs_parentInodeID] = lfs_insertData((char *) lfs_inode, sizeof(inode));
  lfs_inodeArray[lfs_inodeID] = -1;

  free(lfs_inode);
  
  return 0;
}

//INSERT DATA METHOD

int lfs_insertData(char * data, int size) {
  int block;
  printf("insertData method called\n");
  
  block = lfs_block;
  //insert the data into the disk in memory
  memset(lfs_disk_in_memory + (lfs_block * BLOCK_SIZE), 0, size);
  memcpy(lfs_disk_in_memory + (lfs_block * BLOCK_SIZE), data, size);
  
  //incriment block
  lfs_block++;
   
  if(lfs_block % (SEGMENT_SIZE / BLOCK_SIZE) == 0) {
    //have to begin a new segment, write the inode array at the beginning of the segment 
    memset(lfs_disk_in_memory + (lfs_segment * SEGMENT_SIZE), 0, 32 * BLOCK_SIZE);
    memcpy(lfs_disk_in_memory + (lfs_segment * SEGMENT_SIZE), lfs_inodeArray, 32 * BLOCK_SIZE);
    
    //write segment to file
    lfs_write_segment(lfs_segment, ((char *)(lfs_disk_in_memory + (lfs_segment * SEGMENT_SIZE))));

    //incriment block by 32 to save space for inode array
    lfs_block += 32;
    //incriment segment
    lfs_segment++;
    
    if(lfs_segment == NUMBER_OF_SEGMENTS){
      //file full, time to loop to beginning
      lfs_block = 32;
      lfs_segment = 0;
    }
    //clean the old inode array
    lfs_cleaner();
    
    return block;
  }
  return block;
}

//CLEANER METHOD
int lfs_cleaner(void) {
  printf("cleaner method called\n");
  
  //create an inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  
  //create an empty new array of inodes
  int *lfs_newinodeArray;
  lfs_newinodeArray = malloc(NUMBER_OF_INODES * sizeof(inode));
  
  //initialize new array of inodes
  int m;
  for(m=0; m<NUMBER_OF_INODES; m++) {
    lfs_inodeArray[m]=-1;
  }
  
  //check each inode in the old inode array
  int i;
  for (i=0; i<NUMBER_OF_INODES; i++) {
    if(lfs_inodeArray[i] != -1) {
      //found an inode. Get the inode
      memset(lfs_inode, 0, sizeof(inode));
      memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[i] * BLOCK_SIZE), sizeof(inode));
      
      if(lfs_inode->type == 0) {
        //the inode is a directory, copy it over.
        lfs_newinodeArray[i] = lfs_insertData((char *) lfs_inode, sizeof(inode));
      } else {
        //The inode is a file, check its datapointers.
        int j;
        for(j=0; j<NUMBER_OF_DATAPOINTERS; j++) {
          if(lfs_inode->datapointer[j] != -1) {
            //found data
            lfs_inode->datapointer[j] = lfs_insertData(lfs_disk_in_memory + (lfs_inode->datapointer[j] * BLOCK_SIZE), BLOCK_SIZE);
          }
        }
        //check indirectDataPointers
        if(lfs_inode->indirectDataPointer != -1) {
          //get the indirectDataPointers array
          int *lfs_indirectPointersArray;
          lfs_indirectPointersArray = lfs_disk_in_memory + (lfs_inode->indirectDataPointer * BLOCK_SIZE);
          
          int k;
          for(k = 0; k<NUMBER_OF_INDIRECTPOINTERS; k++) {
            if (lfs_indirectPointersArray[k] != -1) {
              //found data
              lfs_indirectPointersArray[k] = lfs_insertData(lfs_disk_in_memory + (lfs_indirectPointersArray[k] * BLOCK_SIZE), BLOCK_SIZE);
            }
            
          }
        //set the updated indirectDataPointers array
        lfs_inode->indirectDataPointer = lfs_insertData((char *) lfs_indirectPointersArray, NUMBER_OF_INDIRECTPOINTERS * sizeof(int));
        
        free(lfs_indirectPointersArray);
        }
        //insert the updated inode into the new inode array
        lfs_newinodeArray[i] = lfs_insertData((char *) lfs_inode, sizeof(inode));
      }
    }
  }
  //overrwite the old inode array with the new updated inode array
  memset(lfs_inodeArray, 0, NUMBER_OF_INODES);
  memcpy(lfs_inodeArray, lfs_newinodeArray, NUMBER_OF_INODES);
  
  free(lfs_newinodeArray);
  free(lfs_inode);
  
  return 0;
}

//WRITE SEGMENT METHOD

int lfs_write_segment(int segment, const char * data) {
  printf("writeSegment method called\n");
  FILE *file;
  file = fopen(HARDDISK,"w+");
  if(file == NULL) {
    perror("Write Segment: Error opening harrdisk file\n");
    return 1;
  }
  
  char * lfs_segment_data;
  lfs_segment_data = malloc(SEGMENT_SIZE);

  memset(lfs_segment_data, 0, SEGMENT_SIZE);
  memcpy(lfs_segment_data, data, SEGMENT_SIZE);
  
  if(fseek(file, (segment * SEGMENT_SIZE), SEEK_SET) != 0) {
    perror("Write Segment: Could not set stream position in harddisk");
    return 1;
  }
  fputs(lfs_segment_data, file);
  fclose(file);
  return 0;
}

//UTIME METHOD

int lfs_utime(const char * path, struct utimbuf * utime) {
  printf("utime method called\n");
  
  //create an inode pointer
  inode *lfs_inode;
  lfs_inode = malloc(sizeof(inode));
  
  //find the inode
  int lfs_inodeID;
  lfs_inodeID = lfs_findInodeID(path);
  
  //get the inode
  memset(lfs_inode, 0, sizeof(inode));
  memcpy(lfs_inode, lfs_disk_in_memory + (lfs_inodeArray[lfs_inodeID] * BLOCK_SIZE), sizeof(inode));
  
  //set the access time
  if(utime != NULL) {
    lfs_inode->modify = time(NULL);
    lfs_inode->access = utime->actime;
  } else {
    lfs_inode->modify = time(NULL);
    lfs_inode->access = time(NULL);
  }
  
  //insert the updated inode into the array of inodes
  lfs_inodeArray[lfs_inodeID] = lfs_insertData((char *) lfs_inode, sizeof(inode));
  
  free(lfs_inode);
  
  return 0;
}

int main( int argc, char *argv[] ) {
  lfs_init();
	fuse_main( argc, argv, &lfs_oper );

	return 0;
}