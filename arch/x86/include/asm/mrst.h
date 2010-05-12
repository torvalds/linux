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

#define SFI_MTMR_MAX_NUM 8
#define SFI_MRTC_MAX	8

#endif /* _ASM_X86_MRST_H */
