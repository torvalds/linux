/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for the configuration register space at port I/O locations
 * 0x22 and 0x23 variously used by PC architectures, e.g. the MP Spec,
 * Cyrix CPUs, numerous chipsets.
 */
#ifndef _ASM_X86_PC_CONF_REG_H
#define _ASM_X86_PC_CONF_REG_H

#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define PC_CONF_INDEX		0x22
#define PC_CONF_DATA		0x23

#define PC_CONF_MPS_IMCR	0x70

extern raw_spinlock_t pc_conf_lock;

static inline u8 pc_conf_get(u8 reg)
{
	outb(reg, PC_CONF_INDEX);
	return inb(PC_CONF_DATA);
}

static inline void pc_conf_set(u8 reg, u8 data)
{
	outb(reg, PC_CONF_INDEX);
	outb(data, PC_CONF_DATA);
}

#endif /* _ASM_X86_PC_CONF_REG_H */
