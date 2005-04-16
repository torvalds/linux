/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <sys/mman.h>
#include "mem_user.h"
#include "mem.h"
#include "user.h"
#include "os.h"
#include "proc_mm.h"

void map(int fd, unsigned long virt, unsigned long len, int r, int w,
	 int x, int phys_fd, unsigned long long offset)
{
	struct proc_mm_op map;
	int prot, n;

	prot = (r ? PROT_READ : 0) | (w ? PROT_WRITE : 0) | 
		(x ? PROT_EXEC : 0);

	map = ((struct proc_mm_op) { .op 	= MM_MMAP,
				     .u 	= 
				     { .mmap	= 
				       { .addr 		= virt,
					 .len		= len,
					 .prot		= prot,
					 .flags		= MAP_SHARED | 
					                  MAP_FIXED,
					 .fd		= phys_fd,
					 .offset	= offset
				       } } } );
	n = os_write_file(fd, &map, sizeof(map));
	if(n != sizeof(map)) 
		printk("map : /proc/mm map failed, err = %d\n", -n);
}

int unmap(int fd, void *addr, unsigned long len)
{
	struct proc_mm_op unmap;
	int n;

	unmap = ((struct proc_mm_op) { .op 	= MM_MUNMAP,
				       .u 	= 
				       { .munmap	= 
					 { .addr 	= (unsigned long) addr,
					   .len		= len } } } );
	n = os_write_file(fd, &unmap, sizeof(unmap));
	if(n != sizeof(unmap)) {
		if(n < 0)
			return(n);
		else if(n > 0)
			return(-EIO);
	}

	return(0);
}

int protect(int fd, unsigned long addr, unsigned long len, int r, int w, 
	    int x, int must_succeed)
{
	struct proc_mm_op protect;
	int prot, n;

	prot = (r ? PROT_READ : 0) | (w ? PROT_WRITE : 0) | 
		(x ? PROT_EXEC : 0);

	protect = ((struct proc_mm_op) { .op 	= MM_MPROTECT,
				       .u 	= 
				       { .mprotect	= 
					 { .addr 	= (unsigned long) addr,
					   .len		= len,
					   .prot	= prot } } } );

	n = os_write_file(fd, &protect, sizeof(protect));
	if(n != sizeof(protect)) {
		if(n == 0) return(0);

		if(must_succeed)
			panic("protect failed, err = %d", -n);

		return(-EIO);
	}

	return(0);
}

void before_mem_skas(unsigned long unused)
{
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
