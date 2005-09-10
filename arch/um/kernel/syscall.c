/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "kern_util.h"
#include "syscall.h"
#include "os.h"

struct {
	int syscall;
	int pid;
	long result;
	unsigned long long start;
	unsigned long long end;
} syscall_record[1024];

int record_syscall_start(int syscall)
{
	int max, index;

	max = sizeof(syscall_record)/sizeof(syscall_record[0]);
	index = next_syscall_index(max);

	syscall_record[index].syscall = syscall;
	syscall_record[index].pid = current_pid();
	syscall_record[index].result = 0xdeadbeef;
	syscall_record[index].start = os_usecs();
	return(index);
}

void record_syscall_end(int index, long result)
{
	syscall_record[index].result = result;
	syscall_record[index].end = os_usecs();
}
