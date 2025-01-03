/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Performance event support - s390 specific definitions.
 *
 * Copyright IBM Corp. 2009, 2017
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef _ASM_S390_PERF_EVENT_H
#define _ASM_S390_PERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/device.h>
#include <asm/stacktrace.h>

/* Per-CPU flags for PMU states */
#define PMU_F_RESERVED			0x1000
#define PMU_F_ENABLED			0x2000
#define PMU_F_IN_USE			0x4000
#define PMU_F_ERR_IBE			0x0100
#define PMU_F_ERR_LSDA			0x0200
#define PMU_F_ERR_MASK			(PMU_F_ERR_IBE|PMU_F_ERR_LSDA)

/* Perf definitions for PMU event attributes in sysfs */
extern __init const struct attribute_group **cpumf_cf_event_group(void);
extern ssize_t cpumf_events_sysfs_show(struct device *dev,
				       struct device_attribute *attr,
				       char *page);
#define EVENT_VAR(_cat, _name)		event_attr_##_cat##_##_name
#define EVENT_PTR(_cat, _name)		(&EVENT_VAR(_cat, _name).attr.attr)

#define CPUMF_EVENT_ATTR(cat, name, id)			\
	PMU_EVENT_ATTR(name, EVENT_VAR(cat, name), id, cpumf_events_sysfs_show)
#define CPUMF_EVENT_PTR(cat, name)	EVENT_PTR(cat, name)


/* Perf callbacks */
struct pt_regs;
extern unsigned long perf_arch_instruction_pointer(struct pt_regs *regs);
extern unsigned long perf_arch_misc_flags(struct pt_regs *regs);
#define perf_arch_misc_flags(regs) perf_arch_misc_flags(regs)
#define perf_arch_bpf_user_pt_regs(regs) &regs->user_regs

/* Perf pt_regs extension for sample-data-entry indicators */
struct perf_sf_sde_regs {
	unsigned char in_guest:1;	  /* guest sample */
	unsigned long reserved:63;	  /* reserved */
};

#define perf_arch_fetch_caller_regs(regs, __ip) do {			\
	(regs)->psw.mask = 0;						\
	(regs)->psw.addr = (__ip);					\
	(regs)->gprs[15] = (unsigned long)__builtin_frame_address(0) -	\
		offsetof(struct stack_frame, back_chain);		\
} while (0)

#endif /* _ASM_S390_PERF_EVENT_H */
