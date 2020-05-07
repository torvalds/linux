/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __DRV_TYPES_PCI_H__
#define __DRV_TYPES_PCI_H__

#ifdef PLATFORM_LINUX
	#include <linux/pci.h>
#endif

#define	INTEL_VENDOR_ID				0x8086
#define	SIS_VENDOR_ID					0x1039
#define	ATI_VENDOR_ID					0x1002
#define	ATI_DEVICE_ID					0x7914
#define	AMD_VENDOR_ID					0x1022

#define PCI_VENDER_ID_REALTEK		0x10ec

enum aspm_mode {
	ASPM_MODE_UND,
	ASPM_MODE_PERF,
	ASPM_MODE_PS,
	ASPM_MODE_DEF,
};

struct pci_priv {
	BOOLEAN		pci_clk_req;

	u8	pciehdr_offset;

	u8	linkctrl_reg;
	u8	pcibridge_linkctrlreg;

	u8	amd_l1_patch;

#ifdef CONFIG_PCI_DYNAMIC_ASPM
	u8	aspm_mode;
#endif
};

typedef struct _RT_ISR_CONTENT {
	union {
		u32			IntArray[2];
		u32			IntReg4Byte;
		u16			IntReg2Byte;
	};
} RT_ISR_CONTENT, *PRT_ISR_CONTENT;

#endif
