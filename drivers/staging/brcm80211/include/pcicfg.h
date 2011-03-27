/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_h_pcicfg_
#define	_h_pcicfg_

/* The actual config space */

#define	PCI_BAR_MAX		6

#define	PCR_RSVDA_MAX		2

/* pci config status reg has a bit to indicate that capability ptr is present */

#define PCI_CAPPTR_PRESENT	0x0010

typedef struct _pci_config_regs {
	u16 vendor;
	u16 device;
	u16 command;
	u16 status;
	u8 rev_id;
	u8 prog_if;
	u8 sub_class;
	u8 base_class;
	u8 cache_line_size;
	u8 latency_timer;
	u8 header_type;
	u8 bist;
	u32 base[PCI_BAR_MAX];
	u32 cardbus_cis;
	u16 subsys_vendor;
	u16 subsys_id;
	u32 baserom;
	u32 rsvd_a[PCR_RSVDA_MAX];
	u8 int_line;
	u8 int_pin;
	u8 min_gnt;
	u8 max_lat;
	u8 dev_dep[192];
} pci_config_regs;

#define	SZPCR		(sizeof (pci_config_regs))
#define	MINSZPCR	64	/* offsetof (dev_dep[0] */

/* A structure for the config registers is nice, but in most
 * systems the config space is not memory mapped, so we need
 * field offsetts. :-(
 */
#define	PCI_CFG_VID		0
#define	PCI_CFG_DID		2
#define	PCI_CFG_CMD		4
#define	PCI_CFG_STAT		6
#define	PCI_CFG_REV		8
#define	PCI_CFG_PROGIF		9
#define	PCI_CFG_SUBCL		0xa
#define	PCI_CFG_BASECL		0xb
#define	PCI_CFG_CLSZ		0xc
#define	PCI_CFG_LATTIM		0xd
#define	PCI_CFG_HDR		0xe
#define	PCI_CFG_BIST		0xf
#define	PCI_CFG_BAR0		0x10
#define	PCI_CFG_BAR1		0x14
#define	PCI_CFG_BAR2		0x18
#define	PCI_CFG_BAR3		0x1c
#define	PCI_CFG_BAR4		0x20
#define	PCI_CFG_BAR5		0x24
#define	PCI_CFG_CIS		0x28
#define	PCI_CFG_SVID		0x2c
#define	PCI_CFG_SSID		0x2e
#define	PCI_CFG_ROMBAR		0x30
#define PCI_CFG_CAPPTR		0x34
#define	PCI_CFG_INT		0x3c
#define	PCI_CFG_PIN		0x3d
#define	PCI_CFG_MINGNT		0x3e
#define	PCI_CFG_MAXLAT		0x3f

/* Classes and subclasses */

/* Header types */
typedef enum {
	PCI_HEADER_NORMAL
} pci_header_types;

/* Overlay for a PCI-to-PCI bridge */

#define	PPB_RSVDA_MAX		2
#define	PPB_RSVDD_MAX		8

typedef struct _ppb_config_regs {
	u16 vendor;
	u16 device;
	u16 command;
	u16 status;
	u8 rev_id;
	u8 prog_if;
	u8 sub_class;
	u8 base_class;
	u8 cache_line_size;
	u8 latency_timer;
	u8 header_type;
	u8 bist;
	u32 rsvd_a[PPB_RSVDA_MAX];
	u8 prim_bus;
	u8 sec_bus;
	u8 sub_bus;
	u8 sec_lat;
	u8 io_base;
	u8 io_lim;
	u16 sec_status;
	u16 mem_base;
	u16 mem_lim;
	u16 pf_mem_base;
	u16 pf_mem_lim;
	u32 pf_mem_base_hi;
	u32 pf_mem_lim_hi;
	u16 io_base_hi;
	u16 io_lim_hi;
	u16 subsys_vendor;
	u16 subsys_id;
	u32 rsvd_b;
	u8 rsvd_c;
	u8 int_pin;
	u16 bridge_ctrl;
	u8 chip_ctrl;
	u8 diag_ctrl;
	u16 arb_ctrl;
	u32 rsvd_d[PPB_RSVDD_MAX];
	u8 dev_dep[192];
} ppb_config_regs;

/* PCI CAPABILITY DEFINES */
#define PCI_CAP_POWERMGMTCAP_ID		0x01
#define PCI_CAP_PCIECAP_ID		0x10

#define PME_CAP_PM_STATES (0x1f << 27)	/* Bits 31:27 states that can generate PME */
#define PME_CSR_OFFSET	    0x4	/* 4-bytes offset */
#define PME_CSR_PME_EN	  (1 << 8)	/* Bit 8 Enable generating of PME */
#define PME_CSR_PME_STAT  (1 << 15)	/* Bit 15 PME got asserted */

/* Everything below is BRCM HND proprietary */

/* Brcm PCI configuration registers */
#define	PCI_BAR0_WIN		0x80	/* backplane address space accessed by BAR0 */
#define	PCI_SPROM_CONTROL	0x88	/* sprom property control */
#define	PCI_INT_MASK		0x94	/* mask of PCI and other cores interrupts */
#define	PCI_BAR0_WIN2		0xac	/* backplane address space accessed by second 4KB of BAR0 */
#define	PCI_GPIO_IN		0xb0	/* pci config space gpio input (>=rev3) */
#define	PCI_GPIO_OUT		0xb4	/* pci config space gpio output (>=rev3) */
#define	PCI_GPIO_OUTEN		0xb8	/* pci config space gpio output enable (>=rev3) */

#define	PCI_BAR0_SPROM_OFFSET	(4 * 1024)	/* bar0 + 4K accesses external sprom */
#define	PCI_BAR0_PCIREGS_OFFSET	(6 * 1024)	/* bar0 + 6K accesses pci core registers */
#define	PCI_BAR0_PCISBR_OFFSET	(4 * 1024)	/* pci core SB registers are at the end of the
						 * 8KB window, so their address is the "regular"
						 * address plus 4K
						 */
#define PCI_BAR0_WINSZ		(16 * 1024)	/* bar0 window size Match with corerev 13 */
/* On pci corerev >= 13 and all pcie, the bar0 is now 16KB and it maps: */
#define	PCI_16KB0_PCIREGS_OFFSET (8 * 1024)	/* bar0 + 8K accesses pci/pcie core registers */
#define	PCI_16KB0_CCREGS_OFFSET	(12 * 1024)	/* bar0 + 12K accesses chipc core registers */

#define	PCI_SBIM_STATUS_SERR	0x4	/* backplane SBErr interrupt status */

/* PCI_INT_MASK */
#define	PCI_SBIM_SHIFT		8	/* backplane core interrupt mask bits offset */

#endif				/* _h_pcicfg_ */
