/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <sys/mman.h>

int switcheroo(int fd, int prot, void *from, void *to, int size)
{
	if(munmap(to, size) < 0){
		return(-1);
	}
	if(mmap(to, size, prot,	MAP_SHARED | MAP_FIXED, fd, 0) != to){
		return(-1);
	}
	if(munmap(from, size) < 0){
		return(-1);
	}
	return(0);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
