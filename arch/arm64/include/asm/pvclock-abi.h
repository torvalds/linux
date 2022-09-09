/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Arm Ltd. */

#ifndef __ASM_PVCLOCK_ABI_H
#define __ASM_PVCLOCK_ABI_H

/* The below structure is defined in ARM DEN0057A */

struct pvclock_vcpu_stolen_time {
	__le32 revision;
	__le32 attributes;
	__le64 stolen_time;
	/* Structure must be 64 byte aligned, pad to that size */
	u8 padding[48];
} __packed;

#endif
