/*
 * IOCTL interface for hypfs
 *
 * Copyright IBM Corp. 2013
 *
 * Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _ASM_HYPFS_CTL_H
#define _ASM_HYPFS_CTL_H

#include <linux/types.h>

struct hypfs_diag304 {
	__u32	args[2];
	__u64	data;
	__u64	rc;
} __attribute__((packed));

#define HYPFS_IOCTL_MAGIC 0x10

#define HYPFS_DIAG304 \
	_IOWR(HYPFS_IOCTL_MAGIC, 0x20, struct hypfs_diag304)

#endif
