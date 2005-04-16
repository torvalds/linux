/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <sys/time.h>
#include "kern_util.h"
#include "syscall_user.h"

struct {
	int syscall;
	int pid;
	long result;
	struct timeval start;
	struct timeval end;
} syscall_record[1024];

int record_syscall_start(int syscall)
{
	int max, index;

	max = sizeof(syscall_record)/sizeof(syscall_record[0]);
	index = next_syscall_index(max);

	syscall_record[index].syscall = syscall;
	syscall_record[index].pid = current_pid();
	syscall_record[index].result = 0xdeadbeef;
	gettimeofday(&syscall_record[index].start, NULL);
	return(index);
}

void record_syscall_end(int index, long result)
{
	syscall_record[index].result = result;
	gettimeofday(&syscall_record[index].end, NULL);
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
