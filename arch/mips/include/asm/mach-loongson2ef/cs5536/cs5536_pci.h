/* SPDX-License-Identifier: GPL-2.0 */
/*
 * the definition file of cs5536 Virtual Support Module(VSM).
 * pci configuration space can be accessed through the VSM, so
 * there is no need of the MSR read/write now, except the spec.
 * MSR registers which are not implemented yet.
 *
 * Copyright (C) 2007 Lemote Inc.
 * Author : jlliu, liujl@lemote.com
 */

#ifndef _CS5536_PCI_H
#define _CS5536_PCI_H

#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci_regs.h>

extern void cs5536_pci_conf_write4(int function, int reg, u32 value);
extern u32 cs5536_pci_conf_read4(int function, int reg);

extern void pci_ehci_write_reg(int reg, u32 value);
extern u32 pci_ehci_read_reg(int reg);

extern void pci_ide_write_reg(int reg, u32 value);
extern u32 pci_ide_read_reg(int reg);

extern void pci_acc_write_reg(int reg, u32 value);
extern u32 pci_acc_read_reg(int reg);

extern void pci_ohci_write_reg(int reg, u32 value);
extern u32 pci_ohci_read_reg(int reg);

extern void pci_isa_write_bar(int n, u32 value);
extern u32 pci_isa_read_bar(int n);
extern void pci_isa_write_reg(int reg, u32 value);
extern u32 pci_isa_read_reg(int reg);

extern int __init init_mfgpt_clocksource(void);

#define CS5536_ACC_INTR		9
#define CS5536_IDE_INTR		14
#define CS5536_USB_INTR		11
#define CS5536_MFGPT_INTR	5
#define CS5536_UART1_INTR	4
#define CS5536_UART2_INTR	3

/************** PCI BUS DEVICE FUNCTION ***************/

/*
 * PCI bus device function
 */
#define PCI_BUS_CS5536		0
#define PCI_IDSEL_CS5536	14

/********** STANDARD PCI-2.2 EXPANSION ****************/

/*
 * PCI configuration space
 * we have to virtualize the PCI configure space head, so we should
 * define the necessary IDs and some others.
 */

/* CONFIG of PCI VENDOR ID*/
#define CFG_PCI_VENDOR_ID(mod_dev_id, sys_vendor_id) \
	(((mod_dev_id) << 16) | (sys_vendor_id))

/* VENDOR ID */
#define CS5536_VENDOR_ID	0x1022

/* DEVICE ID */
#define CS5536_ISA_DEVICE_ID		0x2090
#define CS5536_IDE_DEVICE_ID		0x209a
#define CS5536_ACC_DEVICE_ID		0x2093
#define CS5536_OHCI_DEVICE_ID		0x2094
#define CS5536_EHCI_DEVICE_ID		0x2095

/* CLASS CODE : CLASS SUB-CLASS INTERFACE */
#define CS5536_ISA_CLASS_CODE		0x060100
#define CS5536_IDE_CLASS_CODE		0x010180
#define CS5536_ACC_CLASS_CODE		0x040100
#define CS5536_OHCI_CLASS_CODE		0x0C0310
#define CS5536_EHCI_CLASS_CODE		0x0C0320

/* BHLC : BIST HEADER-TYPE LATENCY-TIMER CACHE-LINE-SIZE */

#define CFG_PCI_CACHE_LINE_SIZE(header_type, latency_timer)	\
	((PCI_NONE_BIST << 24) | ((header_type) << 16) \
		| ((latency_timer) << 8) | PCI_NORMAL_CACHE_LINE_SIZE);

#define PCI_NONE_BIST			0x00	/* RO not implemented yet. */
#define PCI_BRIDGE_HEADER_TYPE		0x80	/* RO */
#define PCI_NORMAL_HEADER_TYPE		0x00
#define PCI_NORMAL_LATENCY_TIMER	0x00
#define PCI_NORMAL_CACHE_LINE_SIZE	0x08	/* RW */

/* BAR */
#define PCI_BAR0_REG			0x10
#define PCI_BAR1_REG			0x14
#define PCI_BAR2_REG			0x18
#define PCI_BAR3_REG			0x1c
#define PCI_BAR4_REG			0x20
#define PCI_BAR5_REG			0x24
#define PCI_BAR_RANGE_MASK		0xFFFFFFFF

/* CARDBUS CIS POINTER */
#define PCI_CARDBUS_CIS_POINTER		0x00000000

/* SUBSYSTEM VENDOR ID	*/
#define CS5536_SUB_VENDOR_ID		CS5536_VENDOR_ID

/* SUBSYSTEM ID */
#define CS5536_ISA_SUB_ID		CS5536_ISA_DEVICE_ID
#define CS5536_IDE_SUB_ID		CS5536_IDE_DEVICE_ID
#define CS5536_ACC_SUB_ID		CS5536_ACC_DEVICE_ID
#define CS5536_OHCI_SUB_ID		CS5536_OHCI_DEVICE_ID
#define CS5536_EHCI_SUB_ID		CS5536_EHCI_DEVICE_ID

/* EXPANSION ROM BAR */
#define PCI_EXPANSION_ROM_BAR		0x00000000

/* CAPABILITIES POINTER */
#define PCI_CAPLIST_POINTER		0x00000000
#define PCI_CAPLIST_USB_POINTER		0x40
/* INTERRUPT */

#define CFG_PCI_INTERRUPT_LINE(pin, mod_intr) \
	((PCI_MAX_LATENCY << 24) | (PCI_MIN_GRANT << 16) | \
		((pin) << 8) | (mod_intr))

#define PCI_MAX_LATENCY			0x40
#define PCI_MIN_GRANT			0x00
#define PCI_DEFAULT_PIN			0x01

/*********** EXPANSION PCI REG ************************/

/*
 * ISA EXPANSION
 */
#define PCI_UART1_INT_REG	0x50
#define PCI_UART2_INT_REG	0x54
#define PCI_ISA_FIXUP_REG	0x58

/*
 * IDE EXPANSION
 */
#define PCI_IDE_CFG_REG		0x40
#define CS5536_IDE_FLASH_SIGNATURE	0xDEADBEEF
#define PCI_IDE_DTC_REG		0x48
#define PCI_IDE_CAST_REG	0x4C
#define PCI_IDE_ETC_REG		0x50
#define PCI_IDE_PM_REG		0x54
#define PCI_IDE_INT_REG		0x60

/*
 * ACC EXPANSION
 */
#define PCI_ACC_INT_REG		0x50

/*
 * OHCI EXPANSION : INTTERUPT IS IMPLEMENTED BY THE OHCI
 */
#define PCI_OHCI_PM_REG		0x40
#define PCI_OHCI_INT_REG	0x50

/*
 * EHCI EXPANSION
 */
#define PCI_EHCI_LEGSMIEN_REG	0x50
#define PCI_EHCI_LEGSMISTS_REG	0x54
#define PCI_EHCI_FLADJ_REG	0x60

#endif				/* _CS5536_PCI_H_ */
