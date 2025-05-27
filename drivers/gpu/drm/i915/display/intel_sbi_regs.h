/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_SBI_REGS_H__
#define __INTEL_SBI_REGS_H__

#include "i915_reg_defs.h"

/*
 * Sideband Interface (SBI) is programmed indirectly, via SBI_ADDR, which
 * contains the register offset; and SBI_DATA, which contains the payload.
 */
#define SBI_ADDR			_MMIO(0xC6000)
#define SBI_DATA			_MMIO(0xC6004)
#define SBI_CTL_STAT			_MMIO(0xC6008)
#define  SBI_CTL_DEST_ICLK		(0x0 << 16)
#define  SBI_CTL_DEST_MPHY		(0x1 << 16)
#define  SBI_CTL_OP_IORD		(0x2 << 8)
#define  SBI_CTL_OP_IOWR		(0x3 << 8)
#define  SBI_CTL_OP_CRRD		(0x6 << 8)
#define  SBI_CTL_OP_CRWR		(0x7 << 8)
#define  SBI_RESPONSE_FAIL		(0x1 << 1)
#define  SBI_RESPONSE_SUCCESS		(0x0 << 1)
#define  SBI_BUSY			(0x1 << 0)
#define  SBI_READY			(0x0 << 0)

/* SBI offsets */
#define  SBI_SSCDIVINTPHASE			0x0200
#define  SBI_SSCDIVINTPHASE6			0x0600
#define   SBI_SSCDIVINTPHASE_DIVSEL_SHIFT	1
#define   SBI_SSCDIVINTPHASE_DIVSEL_MASK	(0x7f << 1)
#define   SBI_SSCDIVINTPHASE_DIVSEL(x)		((x) << 1)
#define   SBI_SSCDIVINTPHASE_INCVAL_SHIFT	8
#define   SBI_SSCDIVINTPHASE_INCVAL_MASK	(0x7f << 8)
#define   SBI_SSCDIVINTPHASE_INCVAL(x)		((x) << 8)
#define   SBI_SSCDIVINTPHASE_DIR(x)		((x) << 15)
#define   SBI_SSCDIVINTPHASE_PROPAGATE		(1 << 0)
#define  SBI_SSCDITHPHASE			0x0204
#define  SBI_SSCCTL				0x020c
#define  SBI_SSCCTL6				0x060C
#define   SBI_SSCCTL_PATHALT			(1 << 3)
#define   SBI_SSCCTL_DISABLE			(1 << 0)
#define  SBI_SSCAUXDIV6				0x0610
#define   SBI_SSCAUXDIV_FINALDIV2SEL_SHIFT	4
#define   SBI_SSCAUXDIV_FINALDIV2SEL_MASK	(1 << 4)
#define   SBI_SSCAUXDIV_FINALDIV2SEL(x)		((x) << 4)
#define  SBI_DBUFF0				0x2a00
#define  SBI_GEN0				0x1f00
#define   SBI_GEN0_CFG_BUFFENABLE_DISABLE	(1 << 0)

#endif /* __INTEL_SBI_REGS_H__ */
