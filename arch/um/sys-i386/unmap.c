/*
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <linux/mman.h>
#include <asm/unistd.h>

static int errno;

static inline _syscall2(int,munmap,void *,start,size_t,len)
static inline _syscall6(void *,mmap2,void *,addr,size_t,len,int,prot,int,flags,int,fd,off_t,offset)
int switcheroo(int fd, int prot, void *from, void *to, int size)
{
	if(munmap(to, size) < 0){
		return(-1);
	}
	if(mmap2(to, size, prot, MAP_SHARED | MAP_FIXED, fd, 0) == (void*) -1 ){
		return(-1);
	}
	if(munmap(from, size) < 0){
		return(-1);
	}
	return(0);
}
