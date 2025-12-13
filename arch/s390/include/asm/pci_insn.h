/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_PCI_INSN_H
#define _ASM_S390_PCI_INSN_H

#include <linux/jump_label.h>

/* Load/Store status codes */
#define ZPCI_PCI_ST_FUNC_NOT_ENABLED		4
#define ZPCI_PCI_ST_FUNC_IN_ERR			8
#define ZPCI_PCI_ST_BLOCKED			12
#define ZPCI_PCI_ST_INSUF_RES			16
#define ZPCI_PCI_ST_INVAL_AS			20
#define ZPCI_PCI_ST_FUNC_ALREADY_ENABLED	24
#define ZPCI_PCI_ST_DMA_AS_NOT_ENABLED		28
#define ZPCI_PCI_ST_2ND_OP_IN_INV_AS		36
#define ZPCI_PCI_ST_FUNC_NOT_AVAIL		40
#define ZPCI_PCI_ST_ALREADY_IN_RQ_STATE		44

/* PCI instruction condition codes */
#define ZPCI_CC_OK				0
#define ZPCI_CC_ERR				1
#define ZPCI_CC_BUSY				2
#define ZPCI_CC_INVAL_HANDLE			3

/* Load/Store address space identifiers */
#define ZPCI_PCIAS_MEMIO_0			0
#define ZPCI_PCIAS_MEMIO_1			1
#define ZPCI_PCIAS_MEMIO_2			2
#define ZPCI_PCIAS_MEMIO_3			3
#define ZPCI_PCIAS_MEMIO_4			4
#define ZPCI_PCIAS_MEMIO_5			5
#define ZPCI_PCIAS_CFGSPC			15

/* Modify PCI Function Controls */
#define ZPCI_MOD_FC_REG_INT	2
#define ZPCI_MOD_FC_DEREG_INT	3
#define ZPCI_MOD_FC_REG_IOAT	4
#define ZPCI_MOD_FC_DEREG_IOAT	5
#define ZPCI_MOD_FC_REREG_IOAT	6
#define ZPCI_MOD_FC_RESET_ERROR	7
#define ZPCI_MOD_FC_RESET_BLOCK	9
#define ZPCI_MOD_FC_SET_MEASURE	10
#define ZPCI_MOD_FC_REG_INT_D	16
#define ZPCI_MOD_FC_DEREG_INT_D	17

/* FIB function controls */
#define ZPCI_FIB_FC_ENABLED	0x80
#define ZPCI_FIB_FC_ERROR	0x40
#define ZPCI_FIB_FC_LS_BLOCKED	0x20
#define ZPCI_FIB_FC_DMAAS_REG	0x10

/* FIB function controls */
#define ZPCI_FIB_FC_ENABLED	0x80
#define ZPCI_FIB_FC_ERROR	0x40
#define ZPCI_FIB_FC_LS_BLOCKED	0x20
#define ZPCI_FIB_FC_DMAAS_REG	0x10

struct zpci_fib_fmt0 {
	u32		:  1;
	u32 isc		:  3;	/* Interrupt subclass */
	u32 noi		: 12;	/* Number of interrupts */
	u32		:  2;
	u32 aibvo	:  6;	/* Adapter interrupt bit vector offset */
	u32 sum		:  1;	/* Adapter int summary bit enabled */
	u32		:  1;
	u32 aisbo	:  6;	/* Adapter int summary bit offset */
	u32		: 32;
	u64 aibv;		/* Adapter int bit vector address */
	u64 aisb;		/* Adapter int summary bit address */
};

struct zpci_fib_fmt1 {
	u32		:  4;
	u32 noi		: 12;
	u32		: 16;
	u32 dibvo	: 16;
	u32		: 16;
	u64		: 64;
	u64		: 64;
};

/* Function Information Block */
struct zpci_fib {
	u32 fmt		:  8;	/* format */
	u32		: 24;
	u32		: 32;
	u8 fc;			/* function controls */
	u64		: 56;
	u64 pba;		/* PCI base address */
	u64 pal;		/* PCI address limit */
	u64 iota;		/* I/O Translation Anchor */
	union {
		struct zpci_fib_fmt0 fmt0;
		struct zpci_fib_fmt1 fmt1;
	};
	u64 fmb_addr;		/* Function measurement block address and key */
	u32		: 32;
	u32 gd;
} __packed __aligned(8);

/* Set Interruption Controls Operation Controls  */
#define	SIC_IRQ_MODE_ALL		0
#define	SIC_IRQ_MODE_SINGLE		1
#define	SIC_SET_AENI_CONTROLS		2
#define	SIC_IRQ_MODE_DIRECT		4
#define	SIC_IRQ_MODE_D_ALL		16
#define	SIC_IRQ_MODE_D_SINGLE		17
#define	SIC_IRQ_MODE_SET_CPU		18

/* directed interruption information block */
struct zpci_diib {
	u32 : 1;
	u32 isc : 3;
	u32 : 28;
	u16 : 16;
	u16 nr_cpus;
	u64 disb_addr;
	u64 : 64;
	u64 : 64;
} __packed __aligned(8);

/* cpu directed interruption information block */
struct zpci_cdiib {
	u64 : 64;
	u64 dibv_addr;
	u64 : 64;
	u64 : 64;
	u64 : 64;
} __packed __aligned(8);

/* adapter interruption parameters block */
struct zpci_aipb {
	u64 faisb;
	u64 gait;
	u16 : 13;
	u16 afi : 3;
	u32 : 32;
	u16 faal;
} __packed __aligned(8);

union zpci_sic_iib {
	struct zpci_diib diib;
	struct zpci_cdiib cdiib;
	struct zpci_aipb aipb;
};

DECLARE_STATIC_KEY_FALSE(have_mio);

u8 zpci_mod_fc(u64 req, struct zpci_fib *fib, u8 *status);
int zpci_refresh_trans(u64 fn, u64 addr, u64 range);
int __zpci_load(u64 *data, u64 req, u64 offset);
int zpci_load(u64 *data, const volatile void __iomem *addr, unsigned long len);
int __zpci_store(u64 data, u64 req, u64 offset);
int zpci_store(const volatile void __iomem *addr, u64 data, unsigned long len);
int __zpci_store_block(const u64 *data, u64 req, u64 offset);
void zpci_barrier(void);
int zpci_set_irq_ctrl(u16 ctl, u8 isc, union zpci_sic_iib *iib);

#endif
