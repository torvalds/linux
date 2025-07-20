/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Diag ioctls and its associated structures definitions.
 *
 * Copyright IBM Corp. 2024
 */

#ifndef __S390_UAPI_ASM_DIAG_H
#define __S390_UAPI_ASM_DIAG_H

#include <linux/types.h>

#define DIAG_MAGIC_STR 'D'

struct diag324_pib {
	__u64 address;
	__u64 sequence;
};

struct diag310_memtop {
	__u64 address;
	__u64 nesting_lvl;
};

/* Diag ioctl definitions */
#define DIAG324_GET_PIBBUF	_IOWR(DIAG_MAGIC_STR, 0x77, struct diag324_pib)
#define DIAG324_GET_PIBLEN	_IOR(DIAG_MAGIC_STR, 0x78, size_t)
#define DIAG310_GET_STRIDE	_IOR(DIAG_MAGIC_STR, 0x79, size_t)
#define DIAG310_GET_MEMTOPLEN	_IOWR(DIAG_MAGIC_STR, 0x7a, size_t)
#define DIAG310_GET_MEMTOPBUF	_IOWR(DIAG_MAGIC_STR, 0x7b, struct diag310_memtop)

#endif /* __S390_UAPI_ASM_DIAG_H */
