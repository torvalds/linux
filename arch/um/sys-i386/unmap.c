/*
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <linux/mman.h>
#include <asm/unistd.h>
#include <sys/syscall.h>

int switcheroo(int fd, int prot, void *from, void *to, int size)
{
	if (syscall(__NR_munmap, to, size) < 0){
		return(-1);
	}
	if (syscall(__NR_mmap2, to, size, prot, MAP_SHARED | MAP_FIXED, fd, 0) == (void*) -1 ){
		return(-1);
	}
	if (syscall(__NR_munmap, from, size) < 0){
		return(-1);
	}
	return(0);
}
