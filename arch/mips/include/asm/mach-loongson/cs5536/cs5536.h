/*
 * The header file of cs5536 south bridge.
 *
 * Copyright (C) 2007 Lemote, Inc.
 * Author : jlliu <liujl@lemote.com>
 */

#ifndef	_CS5536_H
#define	_CS5536_H

#include <linux/types.h>

extern void _rdmsr(u32 msr, u32 *hi, u32 *lo);
extern void _wrmsr(u32 msr, u32 hi, u32 lo);

/*
 * MSR module base
 */
#define	CS5536_SB_MSR_BASE	(0x00000000)
#define	CS5536_GLIU_MSR_BASE	(0x10000000)
#define	CS5536_ILLEGAL_MSR_BASE	(0x20000000)
#define	CS5536_USB_MSR_BASE	(0x40000000)
#define	CS5536_IDE_MSR_BASE	(0x60000000)
#define	CS5536_DIVIL_MSR_BASE	(0x80000000)
#define	CS5536_ACC_MSR_BASE	(0xa0000000)
#define	CS5536_UNUSED_MSR_BASE	(0xc0000000)
#define	CS5536_GLCP_MSR_BASE	(0xe0000000)

#define	SB_MSR_REG(offset)	(CS5536_SB_MSR_BASE	| (offset))
#define	GLIU_MSR_REG(offset)	(CS5536_GLIU_MSR_BASE	| (offset))
#define	ILLEGAL_MSR_REG(offset)	(CS5536_ILLEGAL_MSR_BASE | (offset))
#define	USB_MSR_REG(offset)	(CS5536_USB_MSR_BASE	| (offset))
#define	IDE_MSR_REG(offset)	(CS5536_IDE_MSR_BASE	| (offset))
#define	DIVIL_MSR_REG(offset)	(CS5536_DIVIL_MSR_BASE	| (offset))
#define	ACC_MSR_REG(offset)	(CS5536_ACC_MSR_BASE	| (offset))
#define	UNUSED_MSR_REG(offset)	(CS5536_UNUSED_MSR_BASE	| (offset))
#define	GLCP_MSR_REG(offset)	(CS5536_GLCP_MSR_BASE	| (offset))

/*
 * BAR SPACE OF VIRTUAL PCI :
 * range for pci probe use, length is the actual size.
 */
/* IO space for all DIVIL modules */
#define	CS5536_IRQ_RANGE	0xffffffe0 /* USERD FOR PCI PROBE */
#define	CS5536_IRQ_LENGTH	0x20	/* THE REGS ACTUAL LENGTH */
#define	CS5536_SMB_RANGE	0xfffffff8
#define	CS5536_SMB_LENGTH	0x08
#define	CS5536_GPIO_RANGE	0xffffff00
#define	CS5536_GPIO_LENGTH	0x100
#define	CS5536_MFGPT_RANGE	0xffffffc0
#define	CS5536_MFGPT_LENGTH	0x40
#define	CS5536_ACPI_RANGE	0xffffffe0
#define	CS5536_ACPI_LENGTH	0x20
#define	CS5536_PMS_RANGE	0xffffff80
#define	CS5536_PMS_LENGTH	0x80
/* IO space for IDE */
#define	CS5536_IDE_RANGE	0xfffffff0
#define	CS5536_IDE_LENGTH	0x10
/* IO space for ACC */
#define	CS5536_ACC_RANGE	0xffffff80
#define	CS5536_ACC_LENGTH	0x80
/* MEM space for ALL USB modules */
#define	CS5536_OHCI_RANGE	0xfffff000
#define	CS5536_OHCI_LENGTH	0x1000
#define	CS5536_EHCI_RANGE	0xfffff000
#define	CS5536_EHCI_LENGTH	0x1000

/*
 * PCI MSR ACCESS
 */
#define	PCI_MSR_CTRL		0xF0
#define	PCI_MSR_ADDR		0xF4
#define	PCI_MSR_DATA_LO		0xF8
#define	PCI_MSR_DATA_HI		0xFC

/**************** MSR *****************************/

/*
 * GLIU STANDARD MSR
 */
#define	GLIU_CAP		0x00
#define	GLIU_CONFIG		0x01
#define	GLIU_SMI		0x02
#define	GLIU_ERROR		0x03
#define	GLIU_PM			0x04
#define	GLIU_DIAG		0x05

/*
 * GLIU SPEC. MSR
 */
#define	GLIU_P2D_BM0		0x20
#define	GLIU_P2D_BM1		0x21
#define	GLIU_P2D_BM2		0x22
#define	GLIU_P2D_BMK0		0x23
#define	GLIU_P2D_BMK1		0x24
#define	GLIU_P2D_BM3		0x25
#define	GLIU_P2D_BM4		0x26
#define	GLIU_COH		0x80
#define	GLIU_PAE		0x81
#define	GLIU_ARB		0x82
#define	GLIU_ASMI		0x83
#define	GLIU_AERR		0x84
#define	GLIU_DEBUG		0x85
#define	GLIU_PHY_CAP		0x86
#define	GLIU_NOUT_RESP		0x87
#define	GLIU_NOUT_WDATA		0x88
#define	GLIU_WHOAMI		0x8B
#define	GLIU_SLV_DIS		0x8C
#define	GLIU_IOD_BM0		0xE0
#define	GLIU_IOD_BM1		0xE1
#define	GLIU_IOD_BM2		0xE2
#define	GLIU_IOD_BM3		0xE3
#define	GLIU_IOD_BM4		0xE4
#define	GLIU_IOD_BM5		0xE5
#define	GLIU_IOD_BM6		0xE6
#define	GLIU_IOD_BM7		0xE7
#define	GLIU_IOD_BM8		0xE8
#define	GLIU_IOD_BM9		0xE9
#define	GLIU_IOD_SC0		0xEA
#define	GLIU_IOD_SC1		0xEB
#define	GLIU_IOD_SC2		0xEC
#define	GLIU_IOD_SC3		0xED
#define	GLIU_IOD_SC4		0xEE
#define	GLIU_IOD_SC5		0xEF
#define	GLIU_IOD_SC6		0xF0
#define	GLIU_IOD_SC7		0xF1

/*
 * SB STANDARD
 */
#define	SB_CAP		0x00
#define	SB_CONFIG	0x01
#define	SB_SMI		0x02
#define	SB_ERROR	0x03
#define	SB_MAR_ERR_EN		0x00000001
#define	SB_TAR_ERR_EN		0x00000002
#define	SB_RSVD_BIT1		0x00000004
#define	SB_EXCEP_ERR_EN		0x00000008
#define	SB_SYSE_ERR_EN		0x00000010
#define	SB_PARE_ERR_EN		0x00000020
#define	SB_TAS_ERR_EN		0x00000040
#define	SB_MAR_ERR_FLAG		0x00010000
#define	SB_TAR_ERR_FLAG		0x00020000
#define	SB_RSVD_BIT2		0x00040000
#define	SB_EXCEP_ERR_FLAG	0x00080000
#define	SB_SYSE_ERR_FLAG	0x00100000
#define	SB_PARE_ERR_FLAG	0x00200000
#define	SB_TAS_ERR_FLAG		0x00400000
#define	SB_PM		0x04
#define	SB_DIAG		0x05

/*
 * SB SPEC.
 */
#define	SB_CTRL		0x10
#define	SB_R0		0x20
#define	SB_R1		0x21
#define	SB_R2		0x22
#define	SB_R3		0x23
#define	SB_R4		0x24
#define	SB_R5		0x25
#define	SB_R6		0x26
#define	SB_R7		0x27
#define	SB_R8		0x28
#define	SB_R9		0x29
#define	SB_R10		0x2A
#define	SB_R11		0x2B
#define	SB_R12		0x2C
#define	SB_R13		0x2D
#define	SB_R14		0x2E
#define	SB_R15		0x2F

/*
 * GLCP STANDARD
 */
#define	GLCP_CAP		0x00
#define	GLCP_CONFIG		0x01
#define	GLCP_SMI		0x02
#define	GLCP_ERROR		0x03
#define	GLCP_PM			0x04
#define	GLCP_DIAG		0x05

/*
 * GLCP SPEC.
 */
#define	GLCP_CLK_DIS_DELAY	0x08
#define	GLCP_PM_CLK_DISABLE	0x09
#define	GLCP_GLB_PM		0x0B
#define	GLCP_DBG_OUT		0x0C
#define	GLCP_RSVD1		0x0D
#define	GLCP_SOFT_COM		0x0E
#define	SOFT_BAR_SMB_FLAG	0x00000001
#define	SOFT_BAR_GPIO_FLAG	0x00000002
#define	SOFT_BAR_MFGPT_FLAG	0x00000004
#define	SOFT_BAR_IRQ_FLAG	0x00000008
#define	SOFT_BAR_PMS_FLAG	0x00000010
#define	SOFT_BAR_ACPI_FLAG	0x00000020
#define	SOFT_BAR_IDE_FLAG	0x00000400
#define	SOFT_BAR_ACC_FLAG	0x00000800
#define	SOFT_BAR_OHCI_FLAG	0x00001000
#define	SOFT_BAR_EHCI_FLAG	0x00002000
#define	GLCP_RSVD2		0x0F
#define	GLCP_CLK_OFF		0x10
#define	GLCP_CLK_ACTIVE		0x11
#define	GLCP_CLK_DISABLE	0x12
#define	GLCP_CLK4ACK		0x13
#define	GLCP_SYS_RST		0x14
#define	GLCP_RSVD3		0x15
#define	GLCP_DBG_CLK_CTRL	0x16
#define	GLCP_CHIP_REV_ID	0x17

/* PIC */
#define	PIC_YSEL_LOW		0x20
#define	PIC_YSEL_LOW_USB_SHIFT		8
#define	PIC_YSEL_LOW_ACC_SHIFT		16
#define	PIC_YSEL_LOW_FLASH_SHIFT	24
#define	PIC_YSEL_HIGH		0x21
#define	PIC_ZSEL_LOW		0x22
#define	PIC_ZSEL_HIGH		0x23
#define	PIC_IRQM_PRIM		0x24
#define	PIC_IRQM_LPC		0x25
#define	PIC_XIRR_STS_LOW	0x26
#define	PIC_XIRR_STS_HIGH	0x27
#define	PCI_SHDW		0x34

/*
 * DIVIL STANDARD
 */
#define	DIVIL_CAP		0x00
#define	DIVIL_CONFIG		0x01
#define	DIVIL_SMI		0x02
#define	DIVIL_ERROR		0x03
#define	DIVIL_PM		0x04
#define	DIVIL_DIAG		0x05

/*
 * DIVIL SPEC.
 */
#define	DIVIL_LBAR_IRQ		0x08
#define	DIVIL_LBAR_KEL		0x09
#define	DIVIL_LBAR_SMB		0x0B
#define	DIVIL_LBAR_GPIO		0x0C
#define	DIVIL_LBAR_MFGPT	0x0D
#define	DIVIL_LBAR_ACPI		0x0E
#define	DIVIL_LBAR_PMS		0x0F
#define	DIVIL_LEG_IO		0x14
#define	DIVIL_BALL_OPTS		0x15
#define	DIVIL_SOFT_IRQ		0x16
#define	DIVIL_SOFT_RESET	0x17

/* MFGPT */
#define MFGPT_IRQ	0x28

/*
 * IDE STANDARD
 */
#define	IDE_CAP		0x00
#define	IDE_CONFIG	0x01
#define	IDE_SMI		0x02
#define	IDE_ERROR	0x03
#define	IDE_PM		0x04
#define	IDE_DIAG	0x05

/*
 * IDE SPEC.
 */
#define	IDE_IO_BAR	0x08
#define	IDE_CFG		0x10
#define	IDE_DTC		0x12
#define	IDE_CAST	0x13
#define	IDE_ETC		0x14
#define	IDE_INTERNAL_PM	0x15

/*
 * ACC STANDARD
 */
#define	ACC_CAP		0x00
#define	ACC_CONFIG	0x01
#define	ACC_SMI		0x02
#define	ACC_ERROR	0x03
#define	ACC_PM		0x04
#define	ACC_DIAG	0x05

/*
 * USB STANDARD
 */
#define	USB_CAP		0x00
#define	USB_CONFIG	0x01
#define	USB_SMI		0x02
#define	USB_ERROR	0x03
#define	USB_PM		0x04
#define	USB_DIAG	0x05

/*
 * USB SPEC.
 */
#define	USB_OHCI	0x08
#define	USB_EHCI	0x09

/****************** NATIVE ***************************/
/* GPIO : I/O SPACE; REG : 32BITS */
#define	GPIOL_OUT_VAL		0x00
#define	GPIOL_OUT_EN		0x04

#endif				/* _CS5536_H */
