/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007-2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Tony Li <tony.li@freescale.com>
 *	   Jason Jin <Jason.jin@freescale.com>
 */
#ifndef _POWERPC_SYSDEV_FSL_MSI_H
#define _POWERPC_SYSDEV_FSL_MSI_H

#include <linux/of.h>
#include <asm/msi_bitmap.h>

#define NR_MSI_REG_MSIIR	8  /* MSIIR can index 8 MSI registers */
#define NR_MSI_REG_MSIIR1	16 /* MSIIR1 can index 16 MSI registers */
#define NR_MSI_REG_MAX		NR_MSI_REG_MSIIR1
#define IRQS_PER_MSI_REG	32
#define NR_MSI_IRQS_MAX	(NR_MSI_REG_MAX * IRQS_PER_MSI_REG)

#define FSL_PIC_IP_MASK   0x0000000F
#define FSL_PIC_IP_MPIC   0x00000001
#define FSL_PIC_IP_IPIC   0x00000002
#define FSL_PIC_IP_VMPIC  0x00000003

#define MSI_HW_ERRATA_ENDIAN 0x00000010

struct fsl_msi_cascade_data;

struct fsl_msi {
	struct irq_domain *irqhost;

	unsigned long cascade_irq;

	u32 msiir_offset; /* Offset of MSIIR, relative to start of CCSR */
	u32 ibs_shift; /* Shift of interrupt bit select */
	u32 srs_shift; /* Shift of the shared interrupt register select */
	void __iomem *msi_regs;
	u32 feature;
	struct fsl_msi_cascade_data *cascade_array[NR_MSI_REG_MAX];

	struct msi_bitmap bitmap;

	struct list_head list;          /* support multiple MSI banks */

	phandle phandle;
};

#endif /* _POWERPC_SYSDEV_FSL_MSI_H */

