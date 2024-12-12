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

/* Diag ioctl definitions */
#define DIAG324_GET_PIBBUF	_IOWR(DIAG_MAGIC_STR, 0x77, struct diag324_pib)
#define DIAG324_GET_PIBLEN	_IOR(DIAG_MAGIC_STR, 0x78, size_t)

#endif /* __S390_UAPI_ASM_DIAG_H */
