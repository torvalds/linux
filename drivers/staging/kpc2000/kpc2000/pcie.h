/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef KP2000_PCIE_H
#define KP2000_PCIE_H
#include <linux/types.h>
#include <linux/pci.h>
#include "../kpc.h"
#include "dma_common_defs.h"

/*      System Register Map (BAR 1, Start Addr 0)
 *
 *  BAR Size:
 *  1048576 (0x100000) bytes = 131072 (0x20000) registers = 256 pages (4K)
 *
 *             6         5         4         3         2         1         0
 *          3210987654321098765432109876543210987654321098765432109876543210
 *      0   <--------------------------- MAGIC ---------------------------->
 *      1   <----------- Card ID ---------><----------- Revision ---------->
 *      2   <--------- Date Stamp --------><--------- Time Stamp ---------->
 *      3   <-------- Core Tbl Len -------><-------- Core Tbl Offset ------>
 *      4   <---------------------------- SSID ---------------------------->
 *      5                                                           < HWID >
 *      6   <------------------------- FPGA DDNA -------------------------->
 *      7   <------------------------ CPLD Config ------------------------->
 *      8   <----------------------- IRQ Mask Flags ----------------------->
 *      9   <---------------------- IRQ Active Flags ---------------------->
 */

#define REG_WIDTH			8
#define REG_MAGIC_NUMBER		(0 * REG_WIDTH)
#define REG_CARD_ID_AND_BUILD		(1 * REG_WIDTH)
#define REG_DATE_AND_TIME_STAMPS	(2 * REG_WIDTH)
#define REG_CORE_TABLE_OFFSET		(3 * REG_WIDTH)
#define REG_FPGA_SSID			(4 * REG_WIDTH)
#define REG_FPGA_HW_ID			(5 * REG_WIDTH)
#define REG_FPGA_DDNA			(6 * REG_WIDTH)
#define REG_CPLD_CONFIG			(7 * REG_WIDTH)
#define REG_INTERRUPT_MASK		(8 * REG_WIDTH)
#define REG_INTERRUPT_ACTIVE		(9 * REG_WIDTH)
#define REG_PCIE_ERROR_COUNT		(10 * REG_WIDTH)

#define KP2000_MAGIC_VALUE		0x196C61482231894DULL

#define PCI_VENDOR_ID_DAKTRONICS	0x1c33
#define PCI_DEVICE_ID_DAKTRONICS	0x6021

#define DMA_BAR				0
#define REG_BAR				1

struct kp2000_device {
	struct pci_dev		*pdev;
	char			name[16];

	unsigned int		card_num;
	struct mutex		sem;

	void __iomem		*sysinfo_regs_base;
	void __iomem		*regs_bar_base;
	struct resource		regs_base_resource;
	void __iomem		*dma_bar_base;
	void __iomem		*dma_common_regs;
	struct resource		dma_base_resource;

	// "System Registers"
	u32			card_id;
	u32			build_version;
	u32			build_datestamp;
	u32			build_timestamp;
	u32			core_table_offset;
	u32			core_table_length;
	u8			core_table_rev;
	u8			hardware_revision;
	u64			ssid;
	u64			ddna;

	// IRQ stuff
	unsigned int		irq;

	struct list_head	uio_devices_list;
};

extern struct class *kpc_uio_class;
extern struct attribute *kpc_uio_class_attrs[];

int kp2000_probe_cores(struct kp2000_device *pcard);
void kp2000_remove_cores(struct kp2000_device *pcard);

// Define this quick little macro because the expression is used frequently
#define PCARD_TO_DEV(pcard)	(&(pcard->pdev->dev))

#endif /* KP2000_PCIE_H */
