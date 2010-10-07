/*
 * mrst.h: Intel Moorestown platform specific setup code
 *
 * (C) Copyright 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _ASM_X86_MRST_H
#define _ASM_X86_MRST_H
extern int pci_mrst_init(void);
int __init sfi_parse_mrtc(struct sfi_table_header *table);

/*
 * Medfield is the follow-up of Moorestown, it combines two chip solution into
 * one. Other than that it also added always-on and constant tsc and lapic
 * timers. Medfield is the platform name, and the chip name is called Penwell
 * we treat Medfield/Penwell as a variant of Moorestown. Penwell can be
 * identified via MSRs.
 */
enum mrst_cpu_type {
	MRST_CPU_CHIP_LINCROFT = 1,
	MRST_CPU_CHIP_PENWELL,
};

extern enum mrst_cpu_type __mrst_cpu_chip;
static inline enum mrst_cpu_type mrst_identify_cpu(void)
{
	return __mrst_cpu_chip;
}

enum mrst_timer_options {
	MRST_TIMER_DEFAULT,
	MRST_TIMER_APBT_ONLY,
	MRST_TIMER_LAPIC_APBT,
};

extern enum mrst_timer_options mrst_timer_options;

#define SFI_MTMR_MAX_NUM 8
#define SFI_MRTC_MAX	8

#endif /* _ASM_X86_MRST_H */
