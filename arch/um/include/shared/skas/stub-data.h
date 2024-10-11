/* SPDX-License-Identifier: GPL-2.0 */
/*

 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2005 Jeff Dike (jdike@karaya.com)
 */

#ifndef __STUB_DATA_H
#define __STUB_DATA_H

#include <linux/compiler_types.h>
#include <as-layout.h>
#include <sysdep/tls.h>

struct stub_init_data {
	unsigned long stub_start;

	int stub_code_fd;
	unsigned long stub_code_offset;
	int stub_data_fd;
	unsigned long stub_data_offset;

	unsigned long segv_handler;
};

#define STUB_NEXT_SYSCALL(s) \
	((struct stub_syscall *) (((unsigned long) s) + (s)->cmd_len))

enum stub_syscall_type {
	STUB_SYSCALL_UNSET = 0,
	STUB_SYSCALL_MMAP,
	STUB_SYSCALL_MUNMAP,
};

struct stub_syscall {
	struct {
		unsigned long addr;
		unsigned long length;
		unsigned long offset;
		int fd;
		int prot;
	} mem;

	enum stub_syscall_type syscall;
};

struct stub_data {
	unsigned long offset;
	long err, child_err;

	int syscall_data_len;
	/* 128 leaves enough room for additional fields in the struct */
	struct stub_syscall syscall_data[(UM_KERN_PAGE_SIZE - 128) / sizeof(struct stub_syscall)] __aligned(16);

	/* Stack for our signal handlers and for calling into . */
	unsigned char sigstack[UM_KERN_PAGE_SIZE] __aligned(UM_KERN_PAGE_SIZE);
};

#endif
