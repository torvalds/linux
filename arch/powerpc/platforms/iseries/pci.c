/*
 * Copyright (C) 2001 Allan Trautman, IBM Corporation
 *
 * iSeries specific routines for PCI.
 *
 * Based on code from pci.c and iSeries_pci.c 32bit
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/abs_addr.h>
#include <asm/firmware.h>

#include <asm/iseries/hv_call_xm.h>
#include <asm/iseries/mf.h>
#include <asm/iseries/iommu.h>

#include <asm/ppc-pci.h>

#include "irq.h"
#include "pci.h"
#include "call_pci.h"

/*
 * Forward declares of prototypes.
 */
static struct device_node *find_Device_Node(int bus, int devfn);

static int Pci_Retry_Max = 3;	/* Only retry 3 times  */
static int Pci_Error_Flag = 1;	/* Set Retry Error on. */

static struct pci_ops iSeries_pci_ops;

/*
 * Table defines
 * Each Entry size is 4 MB * 1024 Entries = 4GB I/O address space.
 */
#define IOMM_TABLE_MAX_ENTRIES	1024
#define IOMM_TABLE_ENTRY_SIZE	0x0000000000400000UL
#define BASE_IO_MEMORY		0xE000000000000000UL

static unsigned long max_io_memory = BASE_IO_MEMORY;
static long current_iomm_table_entry;

/*
 * Lookup Tables.
 */
static struct device_node *iomm_table[IOMM_TABLE_MAX_ENTRIES];
static u8 iobar_table[IOMM_TABLE_MAX_ENTRIES];

static const char pci_io_text[] = "iSeries PCI I/O";
static DEFINE_SPINLOCK(iomm_table_lock);

/*
 * iomm_table_allocate_entry
 *
 * Adds pci_dev entry in address translation table
 *
 * - Allocates the number of entries required in table base on BAR
 *   size.
 * - Allocates starting at BASE_IO_MEMORY and increases.
 * - The size is round up to be a multiple of entry size.
 * - CurrentIndex is incremented to keep track of the last entry.
 * - Builds the resource entry for allocated BARs.
 */
static void iomm_table_allocate_entry(struct pci_dev *dev, int bar_num)
{
	struct resource *bar_res = &dev->resource[bar_num];
	long bar_size = pci_resource_len(dev, bar_num);

	/*
	 * No space to allocate, quick exit, skip Allocation.
	 */
	if (bar_size == 0)
		return;
	/*
	 * Set Resource values.
	 */
	spin_lock(&iomm_table_lock);
	bar_res->name = pci_io_text;
	bar_res->start = BASE_IO_MEMORY +
		IOMM_TABLE_ENTRY_SIZE * current_iomm_table_entry;
	bar_res->end = bar_res->start + bar_size - 1;
	/*
	 * Allocate the number of table entries needed for BAR.
	 */
	while (bar_size > 0 ) {
		iomm_table[current_iomm_table_entry] = dev->sysdata;
		iobar_table[current_iomm_table_entry] = bar_num;
		bar_size -= IOMM_TABLE_ENTRY_SIZE;
		++current_iomm_table_entry;
	}
	max_io_memory = BASE_IO_MEMORY +
		IOMM_TABLE_ENTRY_SIZE * current_iomm_table_entry;
	spin_unlock(&iomm_table_lock);
}

/*
 * allocate_device_bars
 *
 * - Allocates ALL pci_dev BAR's and updates the resources with the
 *   BAR value.  BARS with zero length will have the resources
 *   The HvCallPci_getBarParms is used to get the size of the BAR
 *   space.  It calls iomm_table_allocate_entry to allocate
 *   each entry.
 * - Loops through The Bar resources(0 - 5) including the ROM
 *   is resource(6).
 */
static void allocate_device_bars(struct pci_dev *dev)
{
	int bar_num;

	for (bar_num = 0; bar_num <= PCI_ROM_RESOURCE; ++bar_num)
		iomm_table_allocate_entry(dev, bar_num);
}

/*
 * Log error information to system console.
 * Filter out the device not there errors.
 * PCI: EADs Connect Failed 0x18.58.10 Rc: 0x00xx
 * PCI: Read Vendor Failed 0x18.58.10 Rc: 0x00xx
 * PCI: Connect Bus Unit Failed 0x18.58.10 Rc: 0x00xx
 */
static void pci_Log_Error(char *Error_Text, int Bus, int SubBus,
		int AgentId, int HvRc)
{
	if (HvRc == 0x0302)
		return;
	printk(KERN_ERR "PCI: %s Failed: 0x%02X.%02X.%02X Rc: 0x%04X",
	       Error_Text, Bus, SubBus, AgentId, HvRc);
}

/*
 * iSeries_pci_final_fixup(void)
 */
void __init iSeries_pci_final_fixup(void)
{
	struct pci_dev *pdev = NULL;
	struct device_node *node;
	int DeviceCount = 0;

	/* Fix up at the device node and pci_dev relationship */
	mf_display_src(0xC9000100);

	printk("pcibios_final_fixup\n");
	for_each_pci_dev(pdev) {
		node = find_Device_Node(pdev->bus->number, pdev->devfn);
		printk("pci dev %p (%x.%x), node %p\n", pdev,
		       pdev->bus->number, pdev->devfn, node);

		if (node != NULL) {
			struct pci_dn *pdn = PCI_DN(node);
			const u32 *agent;

			agent = of_get_property(node, "linux,agent-id", NULL);
			if ((pdn != NULL) && (agent != NULL)) {
				u8 irq = iSeries_allocate_IRQ(pdn->busno, 0,
						pdn->bussubno);
				int err;

				err = HvCallXm_connectBusUnit(pdn->busno, pdn->bussubno,
						*agent, irq);
				if (err)
					pci_Log_Error("Connect Bus Unit",
						pdn->busno, pdn->bussubno, *agent, err);
				else {
					err = HvCallPci_configStore8(pdn->busno, pdn->bussubno,
							*agent,
							PCI_INTERRUPT_LINE,
							irq);
					if (err)
						pci_Log_Error("PciCfgStore Irq Failed!",
							pdn->busno, pdn->bussubno, *agent, err);
				}
				if (!err)
					pdev->irq = irq;
			}

			++DeviceCount;
			pdev->sysdata = (void *)node;
			PCI_DN(node)->pcidev = pdev;
			allocate_device_bars(pdev);
			iSeries_Device_Information(pdev, DeviceCount);
			iommu_devnode_init_iSeries(pdev, node);
		} else
			printk("PCI: Device Tree not found for 0x%016lX\n",
					(unsigned long)pdev);
	}
	iSeries_activate_IRQs();
	mf_display_src(0xC9000200);
}

/*
 * Look down the chain to find the matching Device Device
 */
static struct device_node *find_Device_Node(int bus, int devfn)
{
	struct device_node *node;

	for (node = NULL; (node = of_find_all_nodes(node)); ) {
		struct pci_dn *pdn = PCI_DN(node);

		if (pdn && (bus == pdn->busno) && (devfn == pdn->devfn))
			return node;
	}
	return NULL;
}

#if 0
/*
 * Returns the device node for the passed pci_dev
 * Sanity Check Node PciDev to passed pci_dev
 * If none is found, returns a NULL which the client must handle.
 */
static struct device_node *get_Device_Node(struct pci_dev *pdev)
{
	struct device_node *node;

	node = pdev->sysdata;
	if (node == NULL || PCI_DN(node)->pcidev != pdev)
		node = find_Device_Node(pdev->bus->number, pdev->devfn);
	return node;
}
#endif

/*
 * Config space read and write functions.
 * For now at least, we look for the device node for the bus and devfn
 * that we are asked to access.  It may be possible to translate the devfn
 * to a subbus and deviceid more directly.
 */
static u64 hv_cfg_read_func[4]  = {
	HvCallPciConfigLoad8, HvCallPciConfigLoad16,
	HvCallPciConfigLoad32, HvCallPciConfigLoad32
};

static u64 hv_cfg_write_func[4] = {
	HvCallPciConfigStore8, HvCallPciConfigStore16,
	HvCallPciConfigStore32, HvCallPciConfigStore32
};

/*
 * Read PCI config space
 */
static int iSeries_pci_read_config(struct pci_bus *bus, unsigned int devfn,
		int offset, int size, u32 *val)
{
	struct device_node *node = find_Device_Node(bus->number, devfn);
	u64 fn;
	struct HvCallPci_LoadReturn ret;

	if (node == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset > 255) {
		*val = ~0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	fn = hv_cfg_read_func[(size - 1) & 3];
	HvCall3Ret16(fn, &ret, iseries_ds_addr(node), offset, 0);

	if (ret.rc != 0) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;	/* or something */
	}

	*val = ret.value;
	return 0;
}

/*
 * Write PCI config space
 */

static int iSeries_pci_write_config(struct pci_bus *bus, unsigned int devfn,
		int offset, int size, u32 val)
{
	struct device_node *node = find_Device_Node(bus->number, devfn);
	u64 fn;
	u64 ret;

	if (node == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset > 255)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	fn = hv_cfg_write_func[(size - 1) & 3];
	ret = HvCall4(fn, iseries_ds_addr(node), offset, val, 0);

	if (ret != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return 0;
}

static struct pci_ops iSeries_pci_ops = {
	.read = iSeries_pci_read_config,
	.write = iSeries_pci_write_config
};

/*
 * Check Return Code
 * -> On Failure, print and log information.
 *    Increment Retry Count, if exceeds max, panic partition.
 *
 * PCI: Device 23.90 ReadL I/O Error( 0): 0x1234
 * PCI: Device 23.90 ReadL Retry( 1)
 * PCI: Device 23.90 ReadL Retry Successful(1)
 */
static int CheckReturnCode(char *TextHdr, struct device_node *DevNode,
		int *retry, u64 ret)
{
	if (ret != 0)  {
		struct pci_dn *pdn = PCI_DN(DevNode);

		(*retry)++;
		printk("PCI: %s: Device 0x%04X:%02X  I/O Error(%2d): 0x%04X\n",
				TextHdr, pdn->busno, pdn->devfn,
				*retry, (int)ret);
		/*
		 * Bump the retry and check for retry count exceeded.
		 * If, Exceeded, panic the system.
		 */
		if (((*retry) > Pci_Retry_Max) &&
				(Pci_Error_Flag > 0)) {
			mf_display_src(0xB6000103);
			panic_timeout = 0;
			panic("PCI: Hardware I/O Error, SRC B6000103, "
					"Automatic Reboot Disabled.\n");
		}
		return -1;	/* Retry Try */
	}
	return 0;
}

/*
 * Translate the I/O Address into a device node, bar, and bar offset.
 * Note: Make sure the passed variable end up on the stack to avoid
 * the exposure of being device global.
 */
static inline struct device_node *xlate_iomm_address(
		const volatile void __iomem *IoAddress,
		u64 *dsaptr, u64 *BarOffsetPtr)
{
	unsigned long OrigIoAddr;
	unsigned long BaseIoAddr;
	unsigned long TableIndex;
	struct device_node *DevNode;

	OrigIoAddr = (unsigned long __force)IoAddress;
	if ((OrigIoAddr < BASE_IO_MEMORY) || (OrigIoAddr >= max_io_memory))
		return NULL;
	BaseIoAddr = OrigIoAddr - BASE_IO_MEMORY;
	TableIndex = BaseIoAddr / IOMM_TABLE_ENTRY_SIZE;
	DevNode = iomm_table[TableIndex];

	if (DevNode != NULL) {
		int barnum = iobar_table[TableIndex];
		*dsaptr = iseries_ds_addr(DevNode) | (barnum << 24);
		*BarOffsetPtr = BaseIoAddr % IOMM_TABLE_ENTRY_SIZE;
	} else
		panic("PCI: Invalid PCI IoAddress detected!\n");
	return DevNode;
}

/*
 * Read MM I/O Instructions for the iSeries
 * On MM I/O error, all ones are returned and iSeries_pci_IoError is cal
 * else, data is returned in Big Endian format.
 */
static u8 iSeries_Read_Byte(const volatile void __iomem *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	int retry = 0;
	struct HvCallPci_LoadReturn ret;
	struct device_node *DevNode =
		xlate_iomm_address(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Read_Byte: invalid access at IO address %p\n",
			       IoAddress);
		return 0xff;
	}
	do {
		HvCall3Ret16(HvCallPciBarLoad8, &ret, dsa, BarOffset, 0);
	} while (CheckReturnCode("RDB", DevNode, &retry, ret.rc) != 0);

	return ret.value;
}

static u16 iSeries_Read_Word(const volatile void __iomem *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	int retry = 0;
	struct HvCallPci_LoadReturn ret;
	struct device_node *DevNode =
		xlate_iomm_address(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Read_Word: invalid access at IO address %p\n",
			       IoAddress);
		return 0xffff;
	}
	do {
		HvCall3Ret16(HvCallPciBarLoad16, &ret, dsa,
				BarOffset, 0);
	} while (CheckReturnCode("RDW", DevNode, &retry, ret.rc) != 0);

	return ret.value;
}

static u32 iSeries_Read_Long(const volatile void __iomem *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	int retry = 0;
	struct HvCallPci_LoadReturn ret;
	struct device_node *DevNode =
		xlate_iomm_address(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Read_Long: invalid access at IO address %p\n",
			       IoAddress);
		return 0xffffffff;
	}
	do {
		HvCall3Ret16(HvCallPciBarLoad32, &ret, dsa,
				BarOffset, 0);
	} while (CheckReturnCode("RDL", DevNode, &retry, ret.rc) != 0);

	return ret.value;
}

/*
 * Write MM I/O Instructions for the iSeries
 *
 */
static void iSeries_Write_Byte(u8 data, volatile void __iomem *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	int retry = 0;
	u64 rc;
	struct device_node *DevNode =
		xlate_iomm_address(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Write_Byte: invalid access at IO address %p\n", IoAddress);
		return;
	}
	do {
		rc = HvCall4(HvCallPciBarStore8, dsa, BarOffset, data, 0);
	} while (CheckReturnCode("WWB", DevNode, &retry, rc) != 0);
}

static void iSeries_Write_Word(u16 data, volatile void __iomem *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	int retry = 0;
	u64 rc;
	struct device_node *DevNode =
		xlate_iomm_address(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Write_Word: invalid access at IO address %p\n",
			       IoAddress);
		return;
	}
	do {
		rc = HvCall4(HvCallPciBarStore16, dsa, BarOffset, data, 0);
	} while (CheckReturnCode("WWW", DevNode, &retry, rc) != 0);
}

static void iSeries_Write_Long(u32 data, volatile void __iomem *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	int retry = 0;
	u64 rc;
	struct device_node *DevNode =
		xlate_iomm_address(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Write_Long: invalid access at IO address %p\n",
			       IoAddress);
		return;
	}
	do {
		rc = HvCall4(HvCallPciBarStore32, dsa, BarOffset, data, 0);
	} while (CheckReturnCode("WWL", DevNode, &retry, rc) != 0);
}

static u8 iseries_readb(const volatile void __iomem *addr)
{
	return iSeries_Read_Byte(addr);
}

static u16 iseries_readw(const volatile void __iomem *addr)
{
	return le16_to_cpu(iSeries_Read_Word(addr));
}

static u32 iseries_readl(const volatile void __iomem *addr)
{
	return le32_to_cpu(iSeries_Read_Long(addr));
}

static u16 iseries_readw_be(const volatile void __iomem *addr)
{
	return iSeries_Read_Word(addr);
}

static u32 iseries_readl_be(const volatile void __iomem *addr)
{
	return iSeries_Read_Long(addr);
}

static void iseries_writeb(u8 data, volatile void __iomem *addr)
{
	iSeries_Write_Byte(data, addr);
}

static void iseries_writew(u16 data, volatile void __iomem *addr)
{
	iSeries_Write_Word(cpu_to_le16(data), addr);
}

static void iseries_writel(u32 data, volatile void __iomem *addr)
{
	iSeries_Write_Long(cpu_to_le32(data), addr);
}

static void iseries_writew_be(u16 data, volatile void __iomem *addr)
{
	iSeries_Write_Word(data, addr);
}

static void iseries_writel_be(u32 data, volatile void __iomem *addr)
{
	iSeries_Write_Long(data, addr);
}

static void iseries_readsb(const volatile void __iomem *addr, void *buf,
			   unsigned long count)
{
	u8 *dst = buf;
	while(count-- > 0)
		*(dst++) = iSeries_Read_Byte(addr);
}

static void iseries_readsw(const volatile void __iomem *addr, void *buf,
			   unsigned long count)
{
	u16 *dst = buf;
	while(count-- > 0)
		*(dst++) = iSeries_Read_Word(addr);
}

static void iseries_readsl(const volatile void __iomem *addr, void *buf,
			   unsigned long count)
{
	u32 *dst = buf;
	while(count-- > 0)
		*(dst++) = iSeries_Read_Long(addr);
}

static void iseries_writesb(volatile void __iomem *addr, const void *buf,
			    unsigned long count)
{
	const u8 *src = buf;
	while(count-- > 0)
		iSeries_Write_Byte(*(src++), addr);
}

static void iseries_writesw(volatile void __iomem *addr, const void *buf,
			    unsigned long count)
{
	const u16 *src = buf;
	while(count-- > 0)
		iSeries_Write_Word(*(src++), addr);
}

static void iseries_writesl(volatile void __iomem *addr, const void *buf,
			    unsigned long count)
{
	const u32 *src = buf;
	while(count-- > 0)
		iSeries_Write_Long(*(src++), addr);
}

static void iseries_memset_io(volatile void __iomem *addr, int c,
			      unsigned long n)
{
	volatile char __iomem *d = addr;

	while (n-- > 0)
		iSeries_Write_Byte(c, d++);
}

static void iseries_memcpy_fromio(void *dest, const volatile void __iomem *src,
				  unsigned long n)
{
	char *d = dest;
	const volatile char __iomem *s = src;

	while (n-- > 0)
		*d++ = iSeries_Read_Byte(s++);
}

static void iseries_memcpy_toio(volatile void __iomem *dest, const void *src,
				unsigned long n)
{
	const char *s = src;
	volatile char __iomem *d = dest;

	while (n-- > 0)
		iSeries_Write_Byte(*s++, d++);
}

/* We only set MMIO ops. The default PIO ops will be default
 * to the MMIO ops + pci_io_base which is 0 on iSeries as
 * expected so both should work.
 *
 * Note that we don't implement the readq/writeq versions as
 * I don't know of an HV call for doing so. Thus, the default
 * operation will be used instead, which will fault a the value
 * return by iSeries for MMIO addresses always hits a non mapped
 * area. This is as good as the BUG() we used to have there.
 */
static struct ppc_pci_io __initdata iseries_pci_io = {
	.readb = iseries_readb,
	.readw = iseries_readw,
	.readl = iseries_readl,
	.readw_be = iseries_readw_be,
	.readl_be = iseries_readl_be,
	.writeb = iseries_writeb,
	.writew = iseries_writew,
	.writel = iseries_writel,
	.writew_be = iseries_writew_be,
	.writel_be = iseries_writel_be,
	.readsb = iseries_readsb,
	.readsw = iseries_readsw,
	.readsl = iseries_readsl,
	.writesb = iseries_writesb,
	.writesw = iseries_writesw,
	.writesl = iseries_writesl,
	.memset_io = iseries_memset_io,
	.memcpy_fromio = iseries_memcpy_fromio,
	.memcpy_toio = iseries_memcpy_toio,
};

/*
 * iSeries_pcibios_init
 *
 * Description:
 *   This function checks for all possible system PCI host bridges that connect
 *   PCI buses.  The system hypervisor is queried as to the guest partition
 *   ownership status.  A pci_controller is built for any bus which is partially
 *   owned or fully owned by this guest partition.
 */
void __init iSeries_pcibios_init(void)
{
	struct pci_controller *phb;
	struct device_node *root = of_find_node_by_path("/");
	struct device_node *node = NULL;

	/* Install IO hooks */
	ppc_pci_io = iseries_pci_io;

	if (root == NULL) {
		printk(KERN_CRIT "iSeries_pcibios_init: can't find root "
				"of device tree\n");
		return;
	}
	while ((node = of_get_next_child(root, node)) != NULL) {
		HvBusNumber bus;
		const u32 *busp;

		if ((node->type == NULL) || (strcmp(node->type, "pci") != 0))
			continue;

		busp = of_get_property(node, "bus-range", NULL);
		if (busp == NULL)
			continue;
		bus = *busp;
		printk("bus %d appears to exist\n", bus);
		phb = pcibios_alloc_controller(node);
		if (phb == NULL)
			continue;

		phb->pci_mem_offset = phb->local_number = bus;
		phb->first_busno = bus;
		phb->last_busno = bus;
		phb->ops = &iSeries_pci_ops;
	}

	of_node_put(root);

	pci_devs_phb_init();
}

