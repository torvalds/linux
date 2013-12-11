/*
 * Performance event support - s390 specific definitions.
 *
 * Copyright IBM Corp. 2009, 2013
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef _ASM_S390_PERF_EVENT_H
#define _ASM_S390_PERF_EVENT_H

#ifdef CONFIG_64BIT

#include <linux/perf_event.h>
#include <linux/device.h>
#include <asm/cpu_mf.h>

/* CPU-measurement counter facility */
#define PERF_CPUM_CF_MAX_CTR		256

/* Per-CPU flags for PMU states */
#define PMU_F_RESERVED			0x1000
#define PMU_F_ENABLED			0x2000

/* Perf defintions for PMU event attributes in sysfs */
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
extern unsigned long perf_instruction_pointer(struct pt_regs *regs);
extern unsigned long perf_misc_flags(struct pt_regs *regs);
#define perf_misc_flags(regs) perf_misc_flags(regs)

#endif /* CONFIG_64BIT */
#endif /* _ASM_S390_PERF_EVENT_H */
