/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Intel Corporation */

#ifndef IPU7_PLATFORM_REGS_H
#define IPU7_PLATFORM_REGS_H

#include <linux/bits.h>

#define IPU7_IS_UC_CTRL_BASE                   0x230000
#define IPU7_ISYS_DMEM_OFFSET                  0x200000
#define IPU7_PS_UC_CTRL_BASE                   0x130000
#define IPU7_PSYS_DMEM_OFFSET                  0x100000

#define IPU7_IS_IO_BASE				0x280000
#define IPU7_IS_IO_CSI2_GPREGS_BASE		(IPU7_IS_IO_BASE + 0x53400)

#define IPU7_IS_IO_CSI2_LEGACY_IRQ_CTRL_BASE	(IPU7_IS_IO_BASE + 0x49000)
#define IPU7_IRQ_CTL_EDGE			0x0
#define IPU7_IRQ_CTL_MASK			0x4
#define IPU7_IRQ_CTL_STATUS			0x8
#define IPU7_IRQ_CTL_CLEAR			0xc
#define IPU7_IRQ_CTL_ENABLE			0x10
#define IPU7_CSI_RX_LEGACY_IRQ_MASK		0x1ff

#define IPU7_TO_SW_IRQ_CNTL_EDGE		0x4000
#define IPU7_TO_SW_IRQ_CNTL_MASK_N		0x4004
#define IPU7_TO_SW_IRQ_CNTL_STATUS		0x4008
#define IPU7_TO_SW_IRQ_CNTL_CLEAR		0x400c
#define IPU7_TO_SW_IRQ_CNTL_ENABLE		0x4010
#define IPU7_IS_UC_TO_SW_IRQ_MASK		0xf
#define IPU7_TO_SW_IRQ_FW			BIT(0)
#define IPU7_REG_PRINTF_AXI_CNTL		0x301c

/* psys subdomain power request positions in PS_WORKPOINT_DOMAIN_REQ */
enum ipu7_psys_subdomain_pos {
	IPU7_PSYS_SUBDOMAIN_LB		= 0,
	IPU7_PSYS_SUBDOMAIN_BB		= 1,
};

#define IPU7_PSYS_DOMAIN_POWER_MASK		(BIT(IPU7_PSYS_SUBDOMAIN_LB) | \
						 BIT(IPU7_PSYS_SUBDOMAIN_BB))
#define IPU7_PSYS_DOMAIN_POWER_IN_PROGRESS	BIT(31)

#endif
