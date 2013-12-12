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

/* Per-CPU flags for PMU states */
#define PMU_F_RESERVED			0x1000
#define PMU_F_ENABLED			0x2000
#define PMU_F_IN_USE			0x4000
#define PMU_F_ERR_IBE			0x0100
#define PMU_F_ERR_LSDA			0x0200
#define PMU_F_ERR_MASK			(PMU_F_ERR_IBE|PMU_F_ERR_LSDA)

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

/* Perf pt_regs extension for sample-data-entry indicators */
struct perf_sf_sde_regs {
	unsigned char in_guest:1;	  /* guest sample */
	unsigned long reserved:63;	  /* reserved */
};

/* Perf PMU definitions for the counter facility */
#define PERF_CPUM_CF_MAX_CTR		256

/* Perf PMU definitions for the sampling facility */
#define PERF_CPUM_SF_MAX_CTR		1
#define PERF_EVENT_CPUM_SF		0xB0000UL	/* Raw event ID */

#define REG_NONE		0
#define REG_OVERFLOW		1
#define OVERFLOW_REG(hwc)	((hwc)->extra_reg.config)
#define SFB_ALLOC_REG(hwc)	((hwc)->extra_reg.alloc)
#define TEAR_REG(hwc)		((hwc)->last_tag)
#define SAMPL_RATE(hwc)		((hwc)->event_base)

/* Perf hardware reserve and release functions */
int perf_reserve_sampling(void);
void perf_release_sampling(void);

#endif /* CONFIG_64BIT */
#endif /* _ASM_S390_PERF_EVENT_H */
