/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include "tt.h"
#include "mem_user.h"
#include "user_util.h"
#include "os.h"

void remap_data(void *segment_start, void *segment_end, int w)
{
	void *addr;
	unsigned long size;
	int data, prot;

	if(w) prot = PROT_WRITE;
	else prot = 0;
	prot |= PROT_READ | PROT_EXEC;
	size = (unsigned long) segment_end - 
		(unsigned long) segment_start;
	data = create_mem_file(size);
	addr = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, data, 0);
	if(addr == MAP_FAILED){
		perror("mapping new data segment");
		exit(1);
	}
	memcpy(addr, segment_start, size);
	if(switcheroo(data, prot, addr, segment_start, size) < 0){
		printf("switcheroo failed\n");
		exit(1);
	}
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
