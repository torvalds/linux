/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * IPIC external definitions and structure.
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005 Freescale Semiconductor, Inc
 */
#ifdef __KERNEL__
#ifndef __ASM_IPIC_H__
#define __ASM_IPIC_H__

#include <linux/irq.h>

/* Flags when we init the IPIC */
#define IPIC_SPREADMODE_GRP_A	0x00000001
#define IPIC_SPREADMODE_GRP_B	0x00000002
#define IPIC_SPREADMODE_GRP_C	0x00000004
#define IPIC_SPREADMODE_GRP_D	0x00000008
#define IPIC_SPREADMODE_MIX_A	0x00000010
#define IPIC_SPREADMODE_MIX_B	0x00000020
#define IPIC_DISABLE_MCP_OUT	0x00000040
#define IPIC_IRQ0_MCP		0x00000080

/* IPIC registers offsets */
#define IPIC_SICFR	0x00	/* System Global Interrupt Configuration Register */
#define IPIC_SIVCR	0x04	/* System Global Interrupt Vector Register */
#define IPIC_SIPNR_H	0x08	/* System Internal Interrupt Pending Register (HIGH) */
#define IPIC_SIPNR_L	0x0C	/* System Internal Interrupt Pending Register (LOW) */
#define IPIC_SIPRR_A	0x10	/* System Internal Interrupt group A Priority Register */
#define IPIC_SIPRR_B	0x14	/* System Internal Interrupt group B Priority Register */
#define IPIC_SIPRR_C	0x18	/* System Internal Interrupt group C Priority Register */
#define IPIC_SIPRR_D	0x1C	/* System Internal Interrupt group D Priority Register */
#define IPIC_SIMSR_H	0x20	/* System Internal Interrupt Mask Register (HIGH) */
#define IPIC_SIMSR_L	0x24	/* System Internal Interrupt Mask Register (LOW) */
#define IPIC_SICNR	0x28	/* System Internal Interrupt Control Register */
#define IPIC_SEPNR	0x2C	/* System External Interrupt Pending Register */
#define IPIC_SMPRR_A	0x30	/* System Mixed Interrupt group A Priority Register */
#define IPIC_SMPRR_B	0x34	/* System Mixed Interrupt group B Priority Register */
#define IPIC_SEMSR	0x38	/* System External Interrupt Mask Register */
#define IPIC_SECNR	0x3C	/* System External Interrupt Control Register */
#define IPIC_SERSR	0x40	/* System Error Status Register */
#define IPIC_SERMR	0x44	/* System Error Mask Register */
#define IPIC_SERCR	0x48	/* System Error Control Register */
#define IPIC_SIFCR_H	0x50	/* System Internal Interrupt Force Register (HIGH) */
#define IPIC_SIFCR_L	0x54	/* System Internal Interrupt Force Register (LOW) */
#define IPIC_SEFCR	0x58	/* System External Interrupt Force Register */
#define IPIC_SERFR	0x5C	/* System Error Force Register */
#define IPIC_SCVCR	0x60	/* System Critical Interrupt Vector Register */
#define IPIC_SMVCR	0x64	/* System Management Interrupt Vector Register */

enum ipic_prio_grp {
	IPIC_INT_GRP_A = IPIC_SIPRR_A,
	IPIC_INT_GRP_D = IPIC_SIPRR_D,
	IPIC_MIX_GRP_A = IPIC_SMPRR_A,
	IPIC_MIX_GRP_B = IPIC_SMPRR_B,
};

enum ipic_mcp_irq {
	IPIC_MCP_IRQ0 = 0,
	IPIC_MCP_WDT  = 1,
	IPIC_MCP_SBA  = 2,
	IPIC_MCP_PCI1 = 5,
	IPIC_MCP_PCI2 = 6,
	IPIC_MCP_MU   = 7,
};

void __init ipic_set_default_priority(void);
extern u32 ipic_get_mcp_status(void);
extern void ipic_clear_mcp_status(u32 mask);

extern struct ipic * ipic_init(struct device_node *node, unsigned int flags);
extern unsigned int ipic_get_irq(void);

#endif /* __ASM_IPIC_H__ */
#endif /* __KERNEL__ */
