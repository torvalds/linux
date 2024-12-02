// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QLogic QLA3xxx NIC HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dmapool.h>
#include <linux/mempool.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/prefetch.h>

#include "qla3xxx.h"

#define DRV_NAME	"qla3xxx"
#define DRV_STRING	"QLogic ISP3XXX Network Driver"
#define DRV_VERSION	"v2.03.00-k5"

static const char ql3xxx_driver_name[] = DRV_NAME;
static const char ql3xxx_driver_version[] = DRV_VERSION;

#define TIMED_OUT_MSG							\
"Timed out waiting for management port to get free before issuing command\n"

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic ISP3XXX Network Driver " DRV_VERSION " ");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static const u32 default_msg
    = NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK
    | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN;

static int debug = -1;		/* defaults above */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static int msi;
module_param(msi, int, 0);
MODULE_PARM_DESC(msi, "Turn on Message Signaled Interrupts.");

static const struct pci_device_id ql3xxx_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, QL3022_DEVICE_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, QL3032_DEVICE_ID)},
	/* required last entry */
	{0,}
};

MODULE_DEVICE_TABLE(pci, ql3xxx_pci_tbl);

/*
 *  These are the known PHY's which are used
 */
enum PHY_DEVICE_TYPE {
   PHY_TYPE_UNKNOWN   = 0,
   PHY_VITESSE_VSC8211,
   PHY_AGERE_ET1011C,
   MAX_PHY_DEV_TYPES
};

struct PHY_DEVICE_INFO {
	const enum PHY_DEVICE_TYPE	phyDevice;
	const u32		phyIdOUI;
	const u16		phyIdModel;
	const char		*name;
};

static const struct PHY_DEVICE_INFO PHY_DEVICES[] = {
	{PHY_TYPE_UNKNOWN,    0x000000, 0x0, "PHY_TYPE_UNKNOWN"},
	{PHY_VITESSE_VSC8211, 0x0003f1, 0xb, "PHY_VITESSE_VSC8211"},
	{PHY_AGERE_ET1011C,   0x00a0bc, 0x1, "PHY_AGERE_ET1011C"},
};


/*
 * Caller must take hw_lock.
 */
static int ql_sem_spinlock(struct ql3_adapter *qdev,
			    u32 sem_mask, u32 sem_bits)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	u32 value;
	unsigned int seconds = 3;

	do {
		writel((sem_mask | sem_bits),
		       &port_regs->CommonRegs.semaphoreReg);
		value = readl(&port_regs->CommonRegs.semaphoreReg);
		if ((value & (sem_mask >> 16)) == sem_bits)
			return 0;
		mdelay(1000);
	} while (--seconds);
	return -1;
}

static void ql_sem_unlock(struct ql3_adapter *qdev, u32 sem_mask)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	writel(sem_mask, &port_regs->CommonRegs.semaphoreReg);
	readl(&port_regs->CommonRegs.semaphoreReg);
}

static int ql_sem_lock(struct ql3_adapter *qdev, u32 sem_mask, u32 sem_bits)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	u32 value;

	writel((sem_mask | sem_bits), &port_regs->CommonRegs.semaphoreReg);
	value = readl(&port_regs->CommonRegs.semaphoreReg);
	return ((value & (sem_mask >> 16)) == sem_bits);
}

/*
 * Caller holds hw_lock.
 */
static int ql_wait_for_drvr_lock(struct ql3_adapter *qdev)
{
	int i = 0;

	do {
		if (ql_sem_lock(qdev,
				QL_DRVR_SEM_MASK,
				(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index)
				 * 2) << 1)) {
			netdev_printk(KERN_DEBUG, qdev->ndev,
				      "driver lock acquired\n");
			return 1;
		}
		mdelay(1000);
	} while (++i < 10);

	netdev_err(qdev->ndev, "Timed out waiting for driver lock...\n");
	return 0;
}

static void ql_set_register_page(struct ql3_adapter *qdev, u32 page)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;

	writel(((ISP_CONTROL_NP_MASK << 16) | page),
			&port_regs->CommonRegs.ispControlStatus);
	readl(&port_regs->CommonRegs.ispControlStatus);
	qdev->current_page = page;
}

static u32 ql_read_common_reg_l(struct ql3_adapter *qdev, u32 __iomem *reg)
{
	u32 value;
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);
	value = readl(reg);
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

	return value;
}

static u32 ql_read_common_reg(struct ql3_adapter *qdev, u32 __iomem *reg)
{
	return readl(reg);
}

static u32 ql_read_page0_reg_l(struct ql3_adapter *qdev, u32 __iomem *reg)
{
	u32 value;
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	if (qdev->current_page != 0)
		ql_set_register_page(qdev, 0);
	value = readl(reg);

	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	return value;
}

static u32 ql_read_page0_reg(struct ql3_adapter *qdev, u32 __iomem *reg)
{
	if (qdev->current_page != 0)
		ql_set_register_page(qdev, 0);
	return readl(reg);
}

static void ql_write_common_reg_l(struct ql3_adapter *qdev,
				u32 __iomem *reg, u32 value)
{
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);
	writel(value, reg);
	readl(reg);
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
}

static void ql_write_common_reg(struct ql3_adapter *qdev,
				u32 __iomem *reg, u32 value)
{
	writel(value, reg);
	readl(reg);
}

static void ql_write_nvram_reg(struct ql3_adapter *qdev,
				u32 __iomem *reg, u32 value)
{
	writel(value, reg);
	readl(reg);
	udelay(1);
}

static void ql_write_page0_reg(struct ql3_adapter *qdev,
			       u32 __iomem *reg, u32 value)
{
	if (qdev->current_page != 0)
		ql_set_register_page(qdev, 0);
	writel(value, reg);
	readl(reg);
}

/*
 * Caller holds hw_lock. Only called during init.
 */
static void ql_write_page1_reg(struct ql3_adapter *qdev,
			       u32 __iomem *reg, u32 value)
{
	if (qdev->current_page != 1)
		ql_set_register_page(qdev, 1);
	writel(value, reg);
	readl(reg);
}

/*
 * Caller holds hw_lock. Only called during init.
 */
static void ql_write_page2_reg(struct ql3_adapter *qdev,
			       u32 __iomem *reg, u32 value)
{
	if (qdev->current_page != 2)
		ql_set_register_page(qdev, 2);
	writel(value, reg);
	readl(reg);
}

static void ql_disable_interrupts(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;

	ql_write_common_reg_l(qdev, &port_regs->CommonRegs.ispInterruptMaskReg,
			    (ISP_IMR_ENABLE_INT << 16));

}

static void ql_enable_interrupts(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;

	ql_write_common_reg_l(qdev, &port_regs->CommonRegs.ispInterruptMaskReg,
			    ((0xff << 16) | ISP_IMR_ENABLE_INT));

}

static void ql_release_to_lrg_buf_free_list(struct ql3_adapter *qdev,
					    struct ql_rcv_buf_cb *lrg_buf_cb)
{
	dma_addr_t map;
	int err;
	lrg_buf_cb->next = NULL;

	if (qdev->lrg_buf_free_tail == NULL) {	/* The list is empty  */
		qdev->lrg_buf_free_head = qdev->lrg_buf_free_tail = lrg_buf_cb;
	} else {
		qdev->lrg_buf_free_tail->next = lrg_buf_cb;
		qdev->lrg_buf_free_tail = lrg_buf_cb;
	}

	if (!lrg_buf_cb->skb) {
		lrg_buf_cb->skb = netdev_alloc_skb(qdev->ndev,
						   qdev->lrg_buffer_len);
		if (unlikely(!lrg_buf_cb->skb)) {
			qdev->lrg_buf_skb_check++;
		} else {
			/*
			 * We save some space to copy the ethhdr from first
			 * buffer
			 */
			skb_reserve(lrg_buf_cb->skb, QL_HEADER_SPACE);
			map = dma_map_single(&qdev->pdev->dev,
					     lrg_buf_cb->skb->data,
					     qdev->lrg_buffer_len - QL_HEADER_SPACE,
					     DMA_FROM_DEVICE);
			err = dma_mapping_error(&qdev->pdev->dev, map);
			if (err) {
				netdev_err(qdev->ndev,
					   "PCI mapping failed with error: %d\n",
					   err);
				dev_kfree_skb(lrg_buf_cb->skb);
				lrg_buf_cb->skb = NULL;

				qdev->lrg_buf_skb_check++;
				return;
			}

			lrg_buf_cb->buf_phy_addr_low =
			    cpu_to_le32(LS_64BITS(map));
			lrg_buf_cb->buf_phy_addr_high =
			    cpu_to_le32(MS_64BITS(map));
			dma_unmap_addr_set(lrg_buf_cb, mapaddr, map);
			dma_unmap_len_set(lrg_buf_cb, maplen,
					  qdev->lrg_buffer_len -
					  QL_HEADER_SPACE);
		}
	}

	qdev->lrg_buf_free_count++;
}

static struct ql_rcv_buf_cb *ql_get_from_lrg_buf_free_list(struct ql3_adapter
							   *qdev)
{
	struct ql_rcv_buf_cb *lrg_buf_cb = qdev->lrg_buf_free_head;

	if (lrg_buf_cb != NULL) {
		qdev->lrg_buf_free_head = lrg_buf_cb->next;
		if (qdev->lrg_buf_free_head == NULL)
			qdev->lrg_buf_free_tail = NULL;
		qdev->lrg_buf_free_count--;
	}

	return lrg_buf_cb;
}

static u32 addrBits = EEPROM_NO_ADDR_BITS;
static u32 dataBits = EEPROM_NO_DATA_BITS;

static void fm93c56a_deselect(struct ql3_adapter *qdev);
static void eeprom_readword(struct ql3_adapter *qdev, u32 eepromAddr,
			    unsigned short *value);

/*
 * Caller holds hw_lock.
 */
static void fm93c56a_select(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	__iomem u32 *spir = &port_regs->CommonRegs.serialPortInterfaceReg;

	qdev->eeprom_cmd_data = AUBURN_EEPROM_CS_1;
	ql_write_nvram_reg(qdev, spir, ISP_NVRAM_MASK | qdev->eeprom_cmd_data);
}

/*
 * Caller holds hw_lock.
 */
static void fm93c56a_cmd(struct ql3_adapter *qdev, u32 cmd, u32 eepromAddr)
{
	int i;
	u32 mask;
	u32 dataBit;
	u32 previousBit;
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	__iomem u32 *spir = &port_regs->CommonRegs.serialPortInterfaceReg;

	/* Clock in a zero, then do the start bit */
	ql_write_nvram_reg(qdev, spir,
			   (ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
			    AUBURN_EEPROM_DO_1));
	ql_write_nvram_reg(qdev, spir,
			   (ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
			    AUBURN_EEPROM_DO_1 | AUBURN_EEPROM_CLK_RISE));
	ql_write_nvram_reg(qdev, spir,
			   (ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
			    AUBURN_EEPROM_DO_1 | AUBURN_EEPROM_CLK_FALL));

	mask = 1 << (FM93C56A_CMD_BITS - 1);
	/* Force the previous data bit to be different */
	previousBit = 0xffff;
	for (i = 0; i < FM93C56A_CMD_BITS; i++) {
		dataBit = (cmd & mask)
			? AUBURN_EEPROM_DO_1
			: AUBURN_EEPROM_DO_0;
		if (previousBit != dataBit) {
			/* If the bit changed, change the DO state to match */
			ql_write_nvram_reg(qdev, spir,
					   (ISP_NVRAM_MASK |
					    qdev->eeprom_cmd_data | dataBit));
			previousBit = dataBit;
		}
		ql_write_nvram_reg(qdev, spir,
				   (ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
				    dataBit | AUBURN_EEPROM_CLK_RISE));
		ql_write_nvram_reg(qdev, spir,
				   (ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
				    dataBit | AUBURN_EEPROM_CLK_FALL));
		cmd = cmd << 1;
	}

	mask = 1 << (addrBits - 1);
	/* Force the previous data bit to be different */
	previousBit = 0xffff;
	for (i = 0; i < addrBits; i++) {
		dataBit = (eepromAddr & mask) ? AUBURN_EEPROM_DO_1
			: AUBURN_EEPROM_DO_0;
		if (previousBit != dataBit) {
			/*
			 * If the bit changed, then change the DO state to
			 * match
			 */
			ql_write_nvram_reg(qdev, spir,
					   (ISP_NVRAM_MASK |
					    qdev->eeprom_cmd_data | dataBit));
			previousBit = dataBit;
		}
		ql_write_nvram_reg(qdev, spir,
				   (ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
				    dataBit | AUBURN_EEPROM_CLK_RISE));
		ql_write_nvram_reg(qdev, spir,
				   (ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
				    dataBit | AUBURN_EEPROM_CLK_FALL));
		eepromAddr = eepromAddr << 1;
	}
}

/*
 * Caller holds hw_lock.
 */
static void fm93c56a_deselect(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	__iomem u32 *spir = &port_regs->CommonRegs.serialPortInterfaceReg;

	qdev->eeprom_cmd_data = AUBURN_EEPROM_CS_0;
	ql_write_nvram_reg(qdev, spir, ISP_NVRAM_MASK | qdev->eeprom_cmd_data);
}

/*
 * Caller holds hw_lock.
 */
static void fm93c56a_datain(struct ql3_adapter *qdev, unsigned short *value)
{
	int i;
	u32 data = 0;
	u32 dataBit;
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	__iomem u32 *spir = &port_regs->CommonRegs.serialPortInterfaceReg;

	/* Read the data bits */
	/* The first bit is a dummy.  Clock right over it. */
	for (i = 0; i < dataBits; i++) {
		ql_write_nvram_reg(qdev, spir,
				   ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
				   AUBURN_EEPROM_CLK_RISE);
		ql_write_nvram_reg(qdev, spir,
				   ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
				   AUBURN_EEPROM_CLK_FALL);
		dataBit = (ql_read_common_reg(qdev, spir) &
			   AUBURN_EEPROM_DI_1) ? 1 : 0;
		data = (data << 1) | dataBit;
	}
	*value = (u16)data;
}

/*
 * Caller holds hw_lock.
 */
static void eeprom_readword(struct ql3_adapter *qdev,
			    u32 eepromAddr, unsigned short *value)
{
	fm93c56a_select(qdev);
	fm93c56a_cmd(qdev, (int)FM93C56A_READ, eepromAddr);
	fm93c56a_datain(qdev, value);
	fm93c56a_deselect(qdev);
}

static void ql_set_mac_addr(struct net_device *ndev, u16 *addr)
{
	__le16 buf[ETH_ALEN / 2];

	buf[0] = cpu_to_le16(addr[0]);
	buf[1] = cpu_to_le16(addr[1]);
	buf[2] = cpu_to_le16(addr[2]);
	eth_hw_addr_set(ndev, (u8 *)buf);
}

static int ql_get_nvram_params(struct ql3_adapter *qdev)
{
	u16 *pEEPROMData;
	u16 checksum = 0;
	u32 index;
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	pEEPROMData = (u16 *)&qdev->nvram_data;
	qdev->eeprom_cmd_data = 0;
	if (ql_sem_spinlock(qdev, QL_NVRAM_SEM_MASK,
			(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 10)) {
		pr_err("%s: Failed ql_sem_spinlock()\n", __func__);
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
		return -1;
	}

	for (index = 0; index < EEPROM_SIZE; index++) {
		eeprom_readword(qdev, index, pEEPROMData);
		checksum += *pEEPROMData;
		pEEPROMData++;
	}
	ql_sem_unlock(qdev, QL_NVRAM_SEM_MASK);

	if (checksum != 0) {
		netdev_err(qdev->ndev, "checksum should be zero, is %x!!\n",
			   checksum);
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
		return -1;
	}

	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	return checksum;
}

static const u32 PHYAddr[2] = {
	PORT0_PHY_ADDRESS, PORT1_PHY_ADDRESS
};

static int ql_wait_for_mii_ready(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 temp;
	int count = 1000;

	while (count) {
		temp = ql_read_page0_reg(qdev, &port_regs->macMIIStatusReg);
		if (!(temp & MAC_MII_STATUS_BSY))
			return 0;
		udelay(10);
		count--;
	}
	return -1;
}

static void ql_mii_enable_scan_mode(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 scanControl;

	if (qdev->numPorts > 1) {
		/* Auto scan will cycle through multiple ports */
		scanControl = MAC_MII_CONTROL_AS | MAC_MII_CONTROL_SC;
	} else {
		scanControl = MAC_MII_CONTROL_SC;
	}

	/*
	 * Scan register 1 of PHY/PETBI,
	 * Set up to scan both devices
	 * The autoscan starts from the first register, completes
	 * the last one before rolling over to the first
	 */
	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   PHYAddr[0] | MII_SCAN_REGISTER);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   (scanControl) |
			   ((MAC_MII_CONTROL_SC | MAC_MII_CONTROL_AS) << 16));
}

static u8 ql_mii_disable_scan_mode(struct ql3_adapter *qdev)
{
	u8 ret;
	struct ql3xxx_port_registers __iomem *port_regs =
					qdev->mem_map_registers;

	/* See if scan mode is enabled before we turn it off */
	if (ql_read_page0_reg(qdev, &port_regs->macMIIMgmtControlReg) &
	    (MAC_MII_CONTROL_AS | MAC_MII_CONTROL_SC)) {
		/* Scan is enabled */
		ret = 1;
	} else {
		/* Scan is disabled */
		ret = 0;
	}

	/*
	 * When disabling scan mode you must first change the MII register
	 * address
	 */
	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   PHYAddr[0] | MII_SCAN_REGISTER);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   ((MAC_MII_CONTROL_SC | MAC_MII_CONTROL_AS |
			     MAC_MII_CONTROL_RC) << 16));

	return ret;
}

static int ql_mii_write_reg_ex(struct ql3_adapter *qdev,
			       u16 regAddr, u16 value, u32 phyAddr)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u8 scanWasEnabled;

	scanWasEnabled = ql_mii_disable_scan_mode(qdev);

	if (ql_wait_for_mii_ready(qdev)) {
		netif_warn(qdev, link, qdev->ndev, TIMED_OUT_MSG);
		return -1;
	}

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   phyAddr | regAddr);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtDataReg, value);

	/* Wait for write to complete 9/10/04 SJP */
	if (ql_wait_for_mii_ready(qdev)) {
		netif_warn(qdev, link, qdev->ndev, TIMED_OUT_MSG);
		return -1;
	}

	if (scanWasEnabled)
		ql_mii_enable_scan_mode(qdev);

	return 0;
}

static int ql_mii_read_reg_ex(struct ql3_adapter *qdev, u16 regAddr,
			      u16 *value, u32 phyAddr)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u8 scanWasEnabled;
	u32 temp;

	scanWasEnabled = ql_mii_disable_scan_mode(qdev);

	if (ql_wait_for_mii_ready(qdev)) {
		netif_warn(qdev, link, qdev->ndev, TIMED_OUT_MSG);
		return -1;
	}

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   phyAddr | regAddr);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   (MAC_MII_CONTROL_RC << 16));

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   (MAC_MII_CONTROL_RC << 16) | MAC_MII_CONTROL_RC);

	/* Wait for the read to complete */
	if (ql_wait_for_mii_ready(qdev)) {
		netif_warn(qdev, link, qdev->ndev, TIMED_OUT_MSG);
		return -1;
	}

	temp = ql_read_page0_reg(qdev, &port_regs->macMIIMgmtDataReg);
	*value = (u16) temp;

	if (scanWasEnabled)
		ql_mii_enable_scan_mode(qdev);

	return 0;
}

static int ql_mii_write_reg(struct ql3_adapter *qdev, u16 regAddr, u16 value)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;

	ql_mii_disable_scan_mode(qdev);

	if (ql_wait_for_mii_ready(qdev)) {
		netif_warn(qdev, link, qdev->ndev, TIMED_OUT_MSG);
		return -1;
	}

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   qdev->PHYAddr | regAddr);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtDataReg, value);

	/* Wait for write to complete. */
	if (ql_wait_for_mii_ready(qdev)) {
		netif_warn(qdev, link, qdev->ndev, TIMED_OUT_MSG);
		return -1;
	}

	ql_mii_enable_scan_mode(qdev);

	return 0;
}

static int ql_mii_read_reg(struct ql3_adapter *qdev, u16 regAddr, u16 *value)
{
	u32 temp;
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;

	ql_mii_disable_scan_mode(qdev);

	if (ql_wait_for_mii_ready(qdev)) {
		netif_warn(qdev, link, qdev->ndev, TIMED_OUT_MSG);
		return -1;
	}

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   qdev->PHYAddr | regAddr);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   (MAC_MII_CONTROL_RC << 16));

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   (MAC_MII_CONTROL_RC << 16) | MAC_MII_CONTROL_RC);

	/* Wait for the read to complete */
	if (ql_wait_for_mii_ready(qdev)) {
		netif_warn(qdev, link, qdev->ndev, TIMED_OUT_MSG);
		return -1;
	}

	temp = ql_read_page0_reg(qdev, &port_regs->macMIIMgmtDataReg);
	*value = (u16) temp;

	ql_mii_enable_scan_mode(qdev);

	return 0;
}

static void ql_petbi_reset(struct ql3_adapter *qdev)
{
	ql_mii_write_reg(qdev, PETBI_CONTROL_REG, PETBI_CTRL_SOFT_RESET);
}

static void ql_petbi_start_neg(struct ql3_adapter *qdev)
{
	u16 reg;

	/* Enable Auto-negotiation sense */
	ql_mii_read_reg(qdev, PETBI_TBI_CTRL, &reg);
	reg |= PETBI_TBI_AUTO_SENSE;
	ql_mii_write_reg(qdev, PETBI_TBI_CTRL, reg);

	ql_mii_write_reg(qdev, PETBI_NEG_ADVER,
			 PETBI_NEG_PAUSE | PETBI_NEG_DUPLEX);

	ql_mii_write_reg(qdev, PETBI_CONTROL_REG,
			 PETBI_CTRL_AUTO_NEG | PETBI_CTRL_RESTART_NEG |
			 PETBI_CTRL_FULL_DUPLEX | PETBI_CTRL_SPEED_1000);

}

static void ql_petbi_reset_ex(struct ql3_adapter *qdev)
{
	ql_mii_write_reg_ex(qdev, PETBI_CONTROL_REG, PETBI_CTRL_SOFT_RESET,
			    PHYAddr[qdev->mac_index]);
}

static void ql_petbi_start_neg_ex(struct ql3_adapter *qdev)
{
	u16 reg;

	/* Enable Auto-negotiation sense */
	ql_mii_read_reg_ex(qdev, PETBI_TBI_CTRL, &reg,
			   PHYAddr[qdev->mac_index]);
	reg |= PETBI_TBI_AUTO_SENSE;
	ql_mii_write_reg_ex(qdev, PETBI_TBI_CTRL, reg,
			    PHYAddr[qdev->mac_index]);

	ql_mii_write_reg_ex(qdev, PETBI_NEG_ADVER,
			    PETBI_NEG_PAUSE | PETBI_NEG_DUPLEX,
			    PHYAddr[qdev->mac_index]);

	ql_mii_write_reg_ex(qdev, PETBI_CONTROL_REG,
			    PETBI_CTRL_AUTO_NEG | PETBI_CTRL_RESTART_NEG |
			    PETBI_CTRL_FULL_DUPLEX | PETBI_CTRL_SPEED_1000,
			    PHYAddr[qdev->mac_index]);
}

static void ql_petbi_init(struct ql3_adapter *qdev)
{
	ql_petbi_reset(qdev);
	ql_petbi_start_neg(qdev);
}

static void ql_petbi_init_ex(struct ql3_adapter *qdev)
{
	ql_petbi_reset_ex(qdev);
	ql_petbi_start_neg_ex(qdev);
}

static int ql_is_petbi_neg_pause(struct ql3_adapter *qdev)
{
	u16 reg;

	if (ql_mii_read_reg(qdev, PETBI_NEG_PARTNER, &reg) < 0)
		return 0;

	return (reg & PETBI_NEG_PAUSE_MASK) == PETBI_NEG_PAUSE;
}

static void phyAgereSpecificInit(struct ql3_adapter *qdev, u32 miiAddr)
{
	netdev_info(qdev->ndev, "enabling Agere specific PHY\n");
	/* power down device bit 11 = 1 */
	ql_mii_write_reg_ex(qdev, 0x00, 0x1940, miiAddr);
	/* enable diagnostic mode bit 2 = 1 */
	ql_mii_write_reg_ex(qdev, 0x12, 0x840e, miiAddr);
	/* 1000MB amplitude adjust (see Agere errata) */
	ql_mii_write_reg_ex(qdev, 0x10, 0x8805, miiAddr);
	/* 1000MB amplitude adjust (see Agere errata) */
	ql_mii_write_reg_ex(qdev, 0x11, 0xf03e, miiAddr);
	/* 100MB amplitude adjust (see Agere errata) */
	ql_mii_write_reg_ex(qdev, 0x10, 0x8806, miiAddr);
	/* 100MB amplitude adjust (see Agere errata) */
	ql_mii_write_reg_ex(qdev, 0x11, 0x003e, miiAddr);
	/* 10MB amplitude adjust (see Agere errata) */
	ql_mii_write_reg_ex(qdev, 0x10, 0x8807, miiAddr);
	/* 10MB amplitude adjust (see Agere errata) */
	ql_mii_write_reg_ex(qdev, 0x11, 0x1f00, miiAddr);
	/* point to hidden reg 0x2806 */
	ql_mii_write_reg_ex(qdev, 0x10, 0x2806, miiAddr);
	/* Write new PHYAD w/bit 5 set */
	ql_mii_write_reg_ex(qdev, 0x11,
			    0x0020 | (PHYAddr[qdev->mac_index] >> 8), miiAddr);
	/*
	 * Disable diagnostic mode bit 2 = 0
	 * Power up device bit 11 = 0
	 * Link up (on) and activity (blink)
	 */
	ql_mii_write_reg(qdev, 0x12, 0x840a);
	ql_mii_write_reg(qdev, 0x00, 0x1140);
	ql_mii_write_reg(qdev, 0x1c, 0xfaf0);
}

static enum PHY_DEVICE_TYPE getPhyType(struct ql3_adapter *qdev,
				       u16 phyIdReg0, u16 phyIdReg1)
{
	enum PHY_DEVICE_TYPE result = PHY_TYPE_UNKNOWN;
	u32   oui;
	u16   model;
	int i;

	if (phyIdReg0 == 0xffff)
		return result;

	if (phyIdReg1 == 0xffff)
		return result;

	/* oui is split between two registers */
	oui = (phyIdReg0 << 6) | ((phyIdReg1 & PHY_OUI_1_MASK) >> 10);

	model = (phyIdReg1 & PHY_MODEL_MASK) >> 4;

	/* Scan table for this PHY */
	for (i = 0; i < MAX_PHY_DEV_TYPES; i++) {
		if ((oui == PHY_DEVICES[i].phyIdOUI) &&
		    (model == PHY_DEVICES[i].phyIdModel)) {
			netdev_info(qdev->ndev, "Phy: %s\n",
				    PHY_DEVICES[i].name);
			result = PHY_DEVICES[i].phyDevice;
			break;
		}
	}

	return result;
}

static int ql_phy_get_speed(struct ql3_adapter *qdev)
{
	u16 reg;

	switch (qdev->phyType) {
	case PHY_AGERE_ET1011C: {
		if (ql_mii_read_reg(qdev, 0x1A, &reg) < 0)
			return 0;

		reg = (reg >> 8) & 3;
		break;
	}
	default:
		if (ql_mii_read_reg(qdev, AUX_CONTROL_STATUS, &reg) < 0)
			return 0;

		reg = (((reg & 0x18) >> 3) & 3);
	}

	switch (reg) {
	case 2:
		return SPEED_1000;
	case 1:
		return SPEED_100;
	case 0:
		return SPEED_10;
	default:
		return -1;
	}
}

static int ql_is_full_dup(struct ql3_adapter *qdev)
{
	u16 reg;

	switch (qdev->phyType) {
	case PHY_AGERE_ET1011C: {
		if (ql_mii_read_reg(qdev, 0x1A, &reg))
			return 0;

		return ((reg & 0x0080) && (reg & 0x1000)) != 0;
	}
	case PHY_VITESSE_VSC8211:
	default: {
		if (ql_mii_read_reg(qdev, AUX_CONTROL_STATUS, &reg) < 0)
			return 0;
		return (reg & PHY_AUX_DUPLEX_STAT) != 0;
	}
	}
}

static int ql_is_phy_neg_pause(struct ql3_adapter *qdev)
{
	u16 reg;

	if (ql_mii_read_reg(qdev, PHY_NEG_PARTNER, &reg) < 0)
		return 0;

	return (reg & PHY_NEG_PAUSE) != 0;
}

static int PHY_Setup(struct ql3_adapter *qdev)
{
	u16   reg1;
	u16   reg2;
	bool  agereAddrChangeNeeded = false;
	u32 miiAddr = 0;
	int err;

	/*  Determine the PHY we are using by reading the ID's */
	err = ql_mii_read_reg(qdev, PHY_ID_0_REG, &reg1);
	if (err != 0) {
		netdev_err(qdev->ndev, "Could not read from reg PHY_ID_0_REG\n");
		return err;
	}

	err = ql_mii_read_reg(qdev, PHY_ID_1_REG, &reg2);
	if (err != 0) {
		netdev_err(qdev->ndev, "Could not read from reg PHY_ID_1_REG\n");
		return err;
	}

	/*  Check if we have a Agere PHY */
	if ((reg1 == 0xffff) || (reg2 == 0xffff)) {

		/* Determine which MII address we should be using
		   determined by the index of the card */
		if (qdev->mac_index == 0)
			miiAddr = MII_AGERE_ADDR_1;
		else
			miiAddr = MII_AGERE_ADDR_2;

		err = ql_mii_read_reg_ex(qdev, PHY_ID_0_REG, &reg1, miiAddr);
		if (err != 0) {
			netdev_err(qdev->ndev,
				   "Could not read from reg PHY_ID_0_REG after Agere detected\n");
			return err;
		}

		err = ql_mii_read_reg_ex(qdev, PHY_ID_1_REG, &reg2, miiAddr);
		if (err != 0) {
			netdev_err(qdev->ndev, "Could not read from reg PHY_ID_1_REG after Agere detected\n");
			return err;
		}

		/*  We need to remember to initialize the Agere PHY */
		agereAddrChangeNeeded = true;
	}

	/*  Determine the particular PHY we have on board to apply
	    PHY specific initializations */
	qdev->phyType = getPhyType(qdev, reg1, reg2);

	if ((qdev->phyType == PHY_AGERE_ET1011C) && agereAddrChangeNeeded) {
		/* need this here so address gets changed */
		phyAgereSpecificInit(qdev, miiAddr);
	} else if (qdev->phyType == PHY_TYPE_UNKNOWN) {
		netdev_err(qdev->ndev, "PHY is unknown\n");
		return -EIO;
	}

	return 0;
}

/*
 * Caller holds hw_lock.
 */
static void ql_mac_enable(struct ql3_adapter *qdev, u32 enable)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 value;

	if (enable)
		value = (MAC_CONFIG_REG_PE | (MAC_CONFIG_REG_PE << 16));
	else
		value = (MAC_CONFIG_REG_PE << 16);

	if (qdev->mac_index)
		ql_write_page0_reg(qdev, &port_regs->mac1ConfigReg, value);
	else
		ql_write_page0_reg(qdev, &port_regs->mac0ConfigReg, value);
}

/*
 * Caller holds hw_lock.
 */
static void ql_mac_cfg_soft_reset(struct ql3_adapter *qdev, u32 enable)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 value;

	if (enable)
		value = (MAC_CONFIG_REG_SR | (MAC_CONFIG_REG_SR << 16));
	else
		value = (MAC_CONFIG_REG_SR << 16);

	if (qdev->mac_index)
		ql_write_page0_reg(qdev, &port_regs->mac1ConfigReg, value);
	else
		ql_write_page0_reg(qdev, &port_regs->mac0ConfigReg, value);
}

/*
 * Caller holds hw_lock.
 */
static void ql_mac_cfg_gig(struct ql3_adapter *qdev, u32 enable)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 value;

	if (enable)
		value = (MAC_CONFIG_REG_GM | (MAC_CONFIG_REG_GM << 16));
	else
		value = (MAC_CONFIG_REG_GM << 16);

	if (qdev->mac_index)
		ql_write_page0_reg(qdev, &port_regs->mac1ConfigReg, value);
	else
		ql_write_page0_reg(qdev, &port_regs->mac0ConfigReg, value);
}

/*
 * Caller holds hw_lock.
 */
static void ql_mac_cfg_full_dup(struct ql3_adapter *qdev, u32 enable)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 value;

	if (enable)
		value = (MAC_CONFIG_REG_FD | (MAC_CONFIG_REG_FD << 16));
	else
		value = (MAC_CONFIG_REG_FD << 16);

	if (qdev->mac_index)
		ql_write_page0_reg(qdev, &port_regs->mac1ConfigReg, value);
	else
		ql_write_page0_reg(qdev, &port_regs->mac0ConfigReg, value);
}

/*
 * Caller holds hw_lock.
 */
static void ql_mac_cfg_pause(struct ql3_adapter *qdev, u32 enable)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 value;

	if (enable)
		value =
		    ((MAC_CONFIG_REG_TF | MAC_CONFIG_REG_RF) |
		     ((MAC_CONFIG_REG_TF | MAC_CONFIG_REG_RF) << 16));
	else
		value = ((MAC_CONFIG_REG_TF | MAC_CONFIG_REG_RF) << 16);

	if (qdev->mac_index)
		ql_write_page0_reg(qdev, &port_regs->mac1ConfigReg, value);
	else
		ql_write_page0_reg(qdev, &port_regs->mac0ConfigReg, value);
}

/*
 * Caller holds hw_lock.
 */
static int ql_is_fiber(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 bitToCheck = 0;
	u32 temp;

	switch (qdev->mac_index) {
	case 0:
		bitToCheck = PORT_STATUS_SM0;
		break;
	case 1:
		bitToCheck = PORT_STATUS_SM1;
		break;
	}

	temp = ql_read_page0_reg(qdev, &port_regs->portStatus);
	return (temp & bitToCheck) != 0;
}

static int ql_is_auto_cfg(struct ql3_adapter *qdev)
{
	u16 reg;
	ql_mii_read_reg(qdev, 0x00, &reg);
	return (reg & 0x1000) != 0;
}

/*
 * Caller holds hw_lock.
 */
static int ql_is_auto_neg_complete(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 bitToCheck = 0;
	u32 temp;

	switch (qdev->mac_index) {
	case 0:
		bitToCheck = PORT_STATUS_AC0;
		break;
	case 1:
		bitToCheck = PORT_STATUS_AC1;
		break;
	}

	temp = ql_read_page0_reg(qdev, &port_regs->portStatus);
	if (temp & bitToCheck) {
		netif_info(qdev, link, qdev->ndev, "Auto-Negotiate complete\n");
		return 1;
	}
	netif_info(qdev, link, qdev->ndev, "Auto-Negotiate incomplete\n");
	return 0;
}

/*
 *  ql_is_neg_pause() returns 1 if pause was negotiated to be on
 */
static int ql_is_neg_pause(struct ql3_adapter *qdev)
{
	if (ql_is_fiber(qdev))
		return ql_is_petbi_neg_pause(qdev);
	else
		return ql_is_phy_neg_pause(qdev);
}

static int ql_auto_neg_error(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 bitToCheck = 0;
	u32 temp;

	switch (qdev->mac_index) {
	case 0:
		bitToCheck = PORT_STATUS_AE0;
		break;
	case 1:
		bitToCheck = PORT_STATUS_AE1;
		break;
	}
	temp = ql_read_page0_reg(qdev, &port_regs->portStatus);
	return (temp & bitToCheck) != 0;
}

static u32 ql_get_link_speed(struct ql3_adapter *qdev)
{
	if (ql_is_fiber(qdev))
		return SPEED_1000;
	else
		return ql_phy_get_speed(qdev);
}

static int ql_is_link_full_dup(struct ql3_adapter *qdev)
{
	if (ql_is_fiber(qdev))
		return 1;
	else
		return ql_is_full_dup(qdev);
}

/*
 * Caller holds hw_lock.
 */
static int ql_link_down_detect(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 bitToCheck = 0;
	u32 temp;

	switch (qdev->mac_index) {
	case 0:
		bitToCheck = ISP_CONTROL_LINK_DN_0;
		break;
	case 1:
		bitToCheck = ISP_CONTROL_LINK_DN_1;
		break;
	}

	temp =
	    ql_read_common_reg(qdev, &port_regs->CommonRegs.ispControlStatus);
	return (temp & bitToCheck) != 0;
}

/*
 * Caller holds hw_lock.
 */
static int ql_link_down_detect_clear(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;

	switch (qdev->mac_index) {
	case 0:
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.ispControlStatus,
				    (ISP_CONTROL_LINK_DN_0) |
				    (ISP_CONTROL_LINK_DN_0 << 16));
		break;

	case 1:
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.ispControlStatus,
				    (ISP_CONTROL_LINK_DN_1) |
				    (ISP_CONTROL_LINK_DN_1 << 16));
		break;

	default:
		return 1;
	}

	return 0;
}

/*
 * Caller holds hw_lock.
 */
static int ql_this_adapter_controls_port(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 bitToCheck = 0;
	u32 temp;

	switch (qdev->mac_index) {
	case 0:
		bitToCheck = PORT_STATUS_F1_ENABLED;
		break;
	case 1:
		bitToCheck = PORT_STATUS_F3_ENABLED;
		break;
	default:
		break;
	}

	temp = ql_read_page0_reg(qdev, &port_regs->portStatus);
	if (temp & bitToCheck) {
		netif_printk(qdev, link, KERN_DEBUG, qdev->ndev,
			     "not link master\n");
		return 0;
	}

	netif_printk(qdev, link, KERN_DEBUG, qdev->ndev, "link master\n");
	return 1;
}

static void ql_phy_reset_ex(struct ql3_adapter *qdev)
{
	ql_mii_write_reg_ex(qdev, CONTROL_REG, PHY_CTRL_SOFT_RESET,
			    PHYAddr[qdev->mac_index]);
}

static void ql_phy_start_neg_ex(struct ql3_adapter *qdev)
{
	u16 reg;
	u16 portConfiguration;

	if (qdev->phyType == PHY_AGERE_ET1011C)
		ql_mii_write_reg(qdev, 0x13, 0x0000);
					/* turn off external loopback */

	if (qdev->mac_index == 0)
		portConfiguration =
			qdev->nvram_data.macCfg_port0.portConfiguration;
	else
		portConfiguration =
			qdev->nvram_data.macCfg_port1.portConfiguration;

	/*  Some HBA's in the field are set to 0 and they need to
	    be reinterpreted with a default value */
	if (portConfiguration == 0)
		portConfiguration = PORT_CONFIG_DEFAULT;

	/* Set the 1000 advertisements */
	ql_mii_read_reg_ex(qdev, PHY_GIG_CONTROL, &reg,
			   PHYAddr[qdev->mac_index]);
	reg &= ~PHY_GIG_ALL_PARAMS;

	if (portConfiguration & PORT_CONFIG_1000MB_SPEED) {
		if (portConfiguration & PORT_CONFIG_FULL_DUPLEX_ENABLED)
			reg |= PHY_GIG_ADV_1000F;
		else
			reg |= PHY_GIG_ADV_1000H;
	}

	ql_mii_write_reg_ex(qdev, PHY_GIG_CONTROL, reg,
			    PHYAddr[qdev->mac_index]);

	/* Set the 10/100 & pause negotiation advertisements */
	ql_mii_read_reg_ex(qdev, PHY_NEG_ADVER, &reg,
			   PHYAddr[qdev->mac_index]);
	reg &= ~PHY_NEG_ALL_PARAMS;

	if (portConfiguration & PORT_CONFIG_SYM_PAUSE_ENABLED)
		reg |= PHY_NEG_ASY_PAUSE | PHY_NEG_SYM_PAUSE;

	if (portConfiguration & PORT_CONFIG_FULL_DUPLEX_ENABLED) {
		if (portConfiguration & PORT_CONFIG_100MB_SPEED)
			reg |= PHY_NEG_ADV_100F;

		if (portConfiguration & PORT_CONFIG_10MB_SPEED)
			reg |= PHY_NEG_ADV_10F;
	}

	if (portConfiguration & PORT_CONFIG_HALF_DUPLEX_ENABLED) {
		if (portConfiguration & PORT_CONFIG_100MB_SPEED)
			reg |= PHY_NEG_ADV_100H;

		if (portConfiguration & PORT_CONFIG_10MB_SPEED)
			reg |= PHY_NEG_ADV_10H;
	}

	if (portConfiguration & PORT_CONFIG_1000MB_SPEED)
		reg |= 1;

	ql_mii_write_reg_ex(qdev, PHY_NEG_ADVER, reg,
			    PHYAddr[qdev->mac_index]);

	ql_mii_read_reg_ex(qdev, CONTROL_REG, &reg, PHYAddr[qdev->mac_index]);

	ql_mii_write_reg_ex(qdev, CONTROL_REG,
			    reg | PHY_CTRL_RESTART_NEG | PHY_CTRL_AUTO_NEG,
			    PHYAddr[qdev->mac_index]);
}

static void ql_phy_init_ex(struct ql3_adapter *qdev)
{
	ql_phy_reset_ex(qdev);
	PHY_Setup(qdev);
	ql_phy_start_neg_ex(qdev);
}

/*
 * Caller holds hw_lock.
 */
static u32 ql_get_link_state(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	u32 bitToCheck = 0;
	u32 temp, linkState;

	switch (qdev->mac_index) {
	case 0:
		bitToCheck = PORT_STATUS_UP0;
		break;
	case 1:
		bitToCheck = PORT_STATUS_UP1;
		break;
	}

	temp = ql_read_page0_reg(qdev, &port_regs->portStatus);
	if (temp & bitToCheck)
		linkState = LS_UP;
	else
		linkState = LS_DOWN;

	return linkState;
}

static int ql_port_start(struct ql3_adapter *qdev)
{
	if (ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
		(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7)) {
		netdev_err(qdev->ndev, "Could not get hw lock for GIO\n");
		return -1;
	}

	if (ql_is_fiber(qdev)) {
		ql_petbi_init(qdev);
	} else {
		/* Copper port */
		ql_phy_init_ex(qdev);
	}

	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	return 0;
}

static int ql_finish_auto_neg(struct ql3_adapter *qdev)
{

	if (ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
		(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7))
		return -1;

	if (!ql_auto_neg_error(qdev)) {
		if (test_bit(QL_LINK_MASTER, &qdev->flags)) {
			/* configure the MAC */
			netif_printk(qdev, link, KERN_DEBUG, qdev->ndev,
				     "Configuring link\n");
			ql_mac_cfg_soft_reset(qdev, 1);
			ql_mac_cfg_gig(qdev,
				       (ql_get_link_speed
					(qdev) ==
					SPEED_1000));
			ql_mac_cfg_full_dup(qdev,
					    ql_is_link_full_dup
					    (qdev));
			ql_mac_cfg_pause(qdev,
					 ql_is_neg_pause
					 (qdev));
			ql_mac_cfg_soft_reset(qdev, 0);

			/* enable the MAC */
			netif_printk(qdev, link, KERN_DEBUG, qdev->ndev,
				     "Enabling mac\n");
			ql_mac_enable(qdev, 1);
		}

		qdev->port_link_state = LS_UP;
		netif_start_queue(qdev->ndev);
		netif_carrier_on(qdev->ndev);
		netif_info(qdev, link, qdev->ndev,
			   "Link is up at %d Mbps, %s duplex\n",
			   ql_get_link_speed(qdev),
			   ql_is_link_full_dup(qdev) ? "full" : "half");

	} else {	/* Remote error detected */

		if (test_bit(QL_LINK_MASTER, &qdev->flags)) {
			netif_printk(qdev, link, KERN_DEBUG, qdev->ndev,
				     "Remote error detected. Calling ql_port_start()\n");
			/*
			 * ql_port_start() is shared code and needs
			 * to lock the PHY on it's own.
			 */
			ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
			if (ql_port_start(qdev))	/* Restart port */
				return -1;
			return 0;
		}
	}
	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	return 0;
}

static void ql_link_state_machine_work(struct work_struct *work)
{
	struct ql3_adapter *qdev =
		container_of(work, struct ql3_adapter, link_state_work.work);

	u32 curr_link_state;
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	curr_link_state = ql_get_link_state(qdev);

	if (test_bit(QL_RESET_ACTIVE, &qdev->flags)) {
		netif_info(qdev, link, qdev->ndev,
			   "Reset in progress, skip processing link state\n");

		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

		/* Restart timer on 2 second interval. */
		mod_timer(&qdev->adapter_timer, jiffies + HZ * 1);

		return;
	}

	switch (qdev->port_link_state) {
	default:
		if (test_bit(QL_LINK_MASTER, &qdev->flags))
			ql_port_start(qdev);
		qdev->port_link_state = LS_DOWN;
		fallthrough;

	case LS_DOWN:
		if (curr_link_state == LS_UP) {
			netif_info(qdev, link, qdev->ndev, "Link is up\n");
			if (ql_is_auto_neg_complete(qdev))
				ql_finish_auto_neg(qdev);

			if (qdev->port_link_state == LS_UP)
				ql_link_down_detect_clear(qdev);

			qdev->port_link_state = LS_UP;
		}
		break;

	case LS_UP:
		/*
		 * See if the link is currently down or went down and came
		 * back up
		 */
		if (curr_link_state == LS_DOWN) {
			netif_info(qdev, link, qdev->ndev, "Link is down\n");
			qdev->port_link_state = LS_DOWN;
		}
		if (ql_link_down_detect(qdev))
			qdev->port_link_state = LS_DOWN;
		break;
	}
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

	/* Restart timer on 2 second interval. */
	mod_timer(&qdev->adapter_timer, jiffies + HZ * 1);
}

/*
 * Caller must take hw_lock and QL_PHY_GIO_SEM.
 */
static void ql_get_phy_owner(struct ql3_adapter *qdev)
{
	if (ql_this_adapter_controls_port(qdev))
		set_bit(QL_LINK_MASTER, &qdev->flags);
	else
		clear_bit(QL_LINK_MASTER, &qdev->flags);
}

/*
 * Caller must take hw_lock and QL_PHY_GIO_SEM.
 */
static void ql_init_scan_mode(struct ql3_adapter *qdev)
{
	ql_mii_enable_scan_mode(qdev);

	if (test_bit(QL_LINK_OPTICAL, &qdev->flags)) {
		if (ql_this_adapter_controls_port(qdev))
			ql_petbi_init_ex(qdev);
	} else {
		if (ql_this_adapter_controls_port(qdev))
			ql_phy_init_ex(qdev);
	}
}

/*
 * MII_Setup needs to be called before taking the PHY out of reset
 * so that the management interface clock speed can be set properly.
 * It would be better if we had a way to disable MDC until after the
 * PHY is out of reset, but we don't have that capability.
 */
static int ql_mii_setup(struct ql3_adapter *qdev)
{
	u32 reg;
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;

	if (ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
			(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7))
		return -1;

	if (qdev->device_id == QL3032_DEVICE_ID)
		ql_write_page0_reg(qdev,
			&port_regs->macMIIMgmtControlReg, 0x0f00000);

	/* Divide 125MHz clock by 28 to meet PHY timing requirements */
	reg = MAC_MII_CONTROL_CLK_SEL_DIV28;

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   reg | ((MAC_MII_CONTROL_CLK_SEL_MASK) << 16));

	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	return 0;
}

#define SUPPORTED_OPTICAL_MODES	(SUPPORTED_1000baseT_Full |	\
				 SUPPORTED_FIBRE |		\
				 SUPPORTED_Autoneg)
#define SUPPORTED_TP_MODES	(SUPPORTED_10baseT_Half |	\
				 SUPPORTED_10baseT_Full |	\
				 SUPPORTED_100baseT_Half |	\
				 SUPPORTED_100baseT_Full |	\
				 SUPPORTED_1000baseT_Half |	\
				 SUPPORTED_1000baseT_Full |	\
				 SUPPORTED_Autoneg |		\
				 SUPPORTED_TP)			\

static u32 ql_supported_modes(struct ql3_adapter *qdev)
{
	if (test_bit(QL_LINK_OPTICAL, &qdev->flags))
		return SUPPORTED_OPTICAL_MODES;

	return SUPPORTED_TP_MODES;
}

static int ql_get_auto_cfg_status(struct ql3_adapter *qdev)
{
	int status;
	unsigned long hw_flags;
	spin_lock_irqsave(&qdev->hw_lock, hw_flags);
	if (ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
			    (QL_RESOURCE_BITS_BASE_CODE |
			     (qdev->mac_index) * 2) << 7)) {
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
		return 0;
	}
	status = ql_is_auto_cfg(qdev);
	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	return status;
}

static u32 ql_get_speed(struct ql3_adapter *qdev)
{
	u32 status;
	unsigned long hw_flags;
	spin_lock_irqsave(&qdev->hw_lock, hw_flags);
	if (ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
			    (QL_RESOURCE_BITS_BASE_CODE |
			     (qdev->mac_index) * 2) << 7)) {
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
		return 0;
	}
	status = ql_get_link_speed(qdev);
	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	return status;
}

static int ql_get_full_dup(struct ql3_adapter *qdev)
{
	int status;
	unsigned long hw_flags;
	spin_lock_irqsave(&qdev->hw_lock, hw_flags);
	if (ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
			    (QL_RESOURCE_BITS_BASE_CODE |
			     (qdev->mac_index) * 2) << 7)) {
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
		return 0;
	}
	status = ql_is_link_full_dup(qdev);
	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	return status;
}

static int ql_get_link_ksettings(struct net_device *ndev,
				 struct ethtool_link_ksettings *cmd)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	u32 supported, advertising;

	supported = ql_supported_modes(qdev);

	if (test_bit(QL_LINK_OPTICAL, &qdev->flags)) {
		cmd->base.port = PORT_FIBRE;
	} else {
		cmd->base.port = PORT_TP;
		cmd->base.phy_address = qdev->PHYAddr;
	}
	advertising = ql_supported_modes(qdev);
	cmd->base.autoneg = ql_get_auto_cfg_status(qdev);
	cmd->base.speed = ql_get_speed(qdev);
	cmd->base.duplex = ql_get_full_dup(qdev);

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static void ql_get_drvinfo(struct net_device *ndev,
			   struct ethtool_drvinfo *drvinfo)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	strscpy(drvinfo->driver, ql3xxx_driver_name, sizeof(drvinfo->driver));
	strscpy(drvinfo->version, ql3xxx_driver_version,
		sizeof(drvinfo->version));
	strscpy(drvinfo->bus_info, pci_name(qdev->pdev),
		sizeof(drvinfo->bus_info));
}

static u32 ql_get_msglevel(struct net_device *ndev)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	return qdev->msg_enable;
}

static void ql_set_msglevel(struct net_device *ndev, u32 value)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	qdev->msg_enable = value;
}

static void ql_get_pauseparam(struct net_device *ndev,
			      struct ethtool_pauseparam *pause)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;

	u32 reg;
	if (qdev->mac_index == 0)
		reg = ql_read_page0_reg(qdev, &port_regs->mac0ConfigReg);
	else
		reg = ql_read_page0_reg(qdev, &port_regs->mac1ConfigReg);

	pause->autoneg  = ql_get_auto_cfg_status(qdev);
	pause->rx_pause = (reg & MAC_CONFIG_REG_RF) >> 2;
	pause->tx_pause = (reg & MAC_CONFIG_REG_TF) >> 1;
}

static const struct ethtool_ops ql3xxx_ethtool_ops = {
	.get_drvinfo = ql_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_msglevel = ql_get_msglevel,
	.set_msglevel = ql_set_msglevel,
	.get_pauseparam = ql_get_pauseparam,
	.get_link_ksettings = ql_get_link_ksettings,
};

static int ql_populate_free_queue(struct ql3_adapter *qdev)
{
	struct ql_rcv_buf_cb *lrg_buf_cb = qdev->lrg_buf_free_head;
	dma_addr_t map;
	int err;

	while (lrg_buf_cb) {
		if (!lrg_buf_cb->skb) {
			lrg_buf_cb->skb =
				netdev_alloc_skb(qdev->ndev,
						 qdev->lrg_buffer_len);
			if (unlikely(!lrg_buf_cb->skb)) {
				netdev_printk(KERN_DEBUG, qdev->ndev,
					      "Failed netdev_alloc_skb()\n");
				break;
			} else {
				/*
				 * We save some space to copy the ethhdr from
				 * first buffer
				 */
				skb_reserve(lrg_buf_cb->skb, QL_HEADER_SPACE);
				map = dma_map_single(&qdev->pdev->dev,
						     lrg_buf_cb->skb->data,
						     qdev->lrg_buffer_len - QL_HEADER_SPACE,
						     DMA_FROM_DEVICE);

				err = dma_mapping_error(&qdev->pdev->dev, map);
				if (err) {
					netdev_err(qdev->ndev,
						   "PCI mapping failed with error: %d\n",
						   err);
					dev_kfree_skb(lrg_buf_cb->skb);
					lrg_buf_cb->skb = NULL;
					break;
				}


				lrg_buf_cb->buf_phy_addr_low =
					cpu_to_le32(LS_64BITS(map));
				lrg_buf_cb->buf_phy_addr_high =
					cpu_to_le32(MS_64BITS(map));
				dma_unmap_addr_set(lrg_buf_cb, mapaddr, map);
				dma_unmap_len_set(lrg_buf_cb, maplen,
						  qdev->lrg_buffer_len -
						  QL_HEADER_SPACE);
				--qdev->lrg_buf_skb_check;
				if (!qdev->lrg_buf_skb_check)
					return 1;
			}
		}
		lrg_buf_cb = lrg_buf_cb->next;
	}
	return 0;
}

/*
 * Caller holds hw_lock.
 */
static void ql_update_small_bufq_prod_index(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;

	if (qdev->small_buf_release_cnt >= 16) {
		while (qdev->small_buf_release_cnt >= 16) {
			qdev->small_buf_q_producer_index++;

			if (qdev->small_buf_q_producer_index ==
			    NUM_SBUFQ_ENTRIES)
				qdev->small_buf_q_producer_index = 0;
			qdev->small_buf_release_cnt -= 8;
		}
		wmb();
		writel_relaxed(qdev->small_buf_q_producer_index,
			       &port_regs->CommonRegs.rxSmallQProducerIndex);
	}
}

/*
 * Caller holds hw_lock.
 */
static void ql_update_lrg_bufq_prod_index(struct ql3_adapter *qdev)
{
	struct bufq_addr_element *lrg_buf_q_ele;
	int i;
	struct ql_rcv_buf_cb *lrg_buf_cb;
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;

	if ((qdev->lrg_buf_free_count >= 8) &&
	    (qdev->lrg_buf_release_cnt >= 16)) {

		if (qdev->lrg_buf_skb_check)
			if (!ql_populate_free_queue(qdev))
				return;

		lrg_buf_q_ele = qdev->lrg_buf_next_free;

		while ((qdev->lrg_buf_release_cnt >= 16) &&
		       (qdev->lrg_buf_free_count >= 8)) {

			for (i = 0; i < 8; i++) {
				lrg_buf_cb =
				    ql_get_from_lrg_buf_free_list(qdev);
				lrg_buf_q_ele->addr_high =
				    lrg_buf_cb->buf_phy_addr_high;
				lrg_buf_q_ele->addr_low =
				    lrg_buf_cb->buf_phy_addr_low;
				lrg_buf_q_ele++;

				qdev->lrg_buf_release_cnt--;
			}

			qdev->lrg_buf_q_producer_index++;

			if (qdev->lrg_buf_q_producer_index ==
			    qdev->num_lbufq_entries)
				qdev->lrg_buf_q_producer_index = 0;

			if (qdev->lrg_buf_q_producer_index ==
			    (qdev->num_lbufq_entries - 1)) {
				lrg_buf_q_ele = qdev->lrg_buf_q_virt_addr;
			}
		}
		wmb();
		qdev->lrg_buf_next_free = lrg_buf_q_ele;
		writel(qdev->lrg_buf_q_producer_index,
			&port_regs->CommonRegs.rxLargeQProducerIndex);
	}
}

static void ql_process_mac_tx_intr(struct ql3_adapter *qdev,
				   struct ob_mac_iocb_rsp *mac_rsp)
{
	struct ql_tx_buf_cb *tx_cb;
	int i;

	if (mac_rsp->flags & OB_MAC_IOCB_RSP_S) {
		netdev_warn(qdev->ndev,
			    "Frame too short but it was padded and sent\n");
	}

	tx_cb = &qdev->tx_buf[mac_rsp->transaction_id];

	/*  Check the transmit response flags for any errors */
	if (mac_rsp->flags & OB_MAC_IOCB_RSP_S) {
		netdev_err(qdev->ndev,
			   "Frame too short to be legal, frame not sent\n");

		qdev->ndev->stats.tx_errors++;
		goto frame_not_sent;
	}

	if (tx_cb->seg_count == 0) {
		netdev_err(qdev->ndev, "tx_cb->seg_count == 0: %d\n",
			   mac_rsp->transaction_id);

		qdev->ndev->stats.tx_errors++;
		goto invalid_seg_count;
	}

	dma_unmap_single(&qdev->pdev->dev,
			 dma_unmap_addr(&tx_cb->map[0], mapaddr),
			 dma_unmap_len(&tx_cb->map[0], maplen), DMA_TO_DEVICE);
	tx_cb->seg_count--;
	if (tx_cb->seg_count) {
		for (i = 1; i < tx_cb->seg_count; i++) {
			dma_unmap_page(&qdev->pdev->dev,
				       dma_unmap_addr(&tx_cb->map[i], mapaddr),
				       dma_unmap_len(&tx_cb->map[i], maplen),
				       DMA_TO_DEVICE);
		}
	}
	qdev->ndev->stats.tx_packets++;
	qdev->ndev->stats.tx_bytes += tx_cb->skb->len;

frame_not_sent:
	dev_kfree_skb_irq(tx_cb->skb);
	tx_cb->skb = NULL;

invalid_seg_count:
	atomic_inc(&qdev->tx_count);
}

static void ql_get_sbuf(struct ql3_adapter *qdev)
{
	if (++qdev->small_buf_index == NUM_SMALL_BUFFERS)
		qdev->small_buf_index = 0;
	qdev->small_buf_release_cnt++;
}

static struct ql_rcv_buf_cb *ql_get_lbuf(struct ql3_adapter *qdev)
{
	struct ql_rcv_buf_cb *lrg_buf_cb = NULL;
	lrg_buf_cb = &qdev->lrg_buf[qdev->lrg_buf_index];
	qdev->lrg_buf_release_cnt++;
	if (++qdev->lrg_buf_index == qdev->num_large_buffers)
		qdev->lrg_buf_index = 0;
	return lrg_buf_cb;
}

/*
 * The difference between 3022 and 3032 for inbound completions:
 * 3022 uses two buffers per completion.  The first buffer contains
 * (some) header info, the second the remainder of the headers plus
 * the data.  For this chip we reserve some space at the top of the
 * receive buffer so that the header info in buffer one can be
 * prepended to the buffer two.  Buffer two is the sent up while
 * buffer one is returned to the hardware to be reused.
 * 3032 receives all of it's data and headers in one buffer for a
 * simpler process.  3032 also supports checksum verification as
 * can be seen in ql_process_macip_rx_intr().
 */
static void ql_process_mac_rx_intr(struct ql3_adapter *qdev,
				   struct ib_mac_iocb_rsp *ib_mac_rsp_ptr)
{
	struct ql_rcv_buf_cb *lrg_buf_cb1 = NULL;
	struct ql_rcv_buf_cb *lrg_buf_cb2 = NULL;
	struct sk_buff *skb;
	u16 length = le16_to_cpu(ib_mac_rsp_ptr->length);

	/*
	 * Get the inbound address list (small buffer).
	 */
	ql_get_sbuf(qdev);

	if (qdev->device_id == QL3022_DEVICE_ID)
		lrg_buf_cb1 = ql_get_lbuf(qdev);

	/* start of second buffer */
	lrg_buf_cb2 = ql_get_lbuf(qdev);
	skb = lrg_buf_cb2->skb;

	qdev->ndev->stats.rx_packets++;
	qdev->ndev->stats.rx_bytes += length;

	skb_put(skb, length);
	dma_unmap_single(&qdev->pdev->dev,
			 dma_unmap_addr(lrg_buf_cb2, mapaddr),
			 dma_unmap_len(lrg_buf_cb2, maplen), DMA_FROM_DEVICE);
	prefetch(skb->data);
	skb_checksum_none_assert(skb);
	skb->protocol = eth_type_trans(skb, qdev->ndev);

	napi_gro_receive(&qdev->napi, skb);
	lrg_buf_cb2->skb = NULL;

	if (qdev->device_id == QL3022_DEVICE_ID)
		ql_release_to_lrg_buf_free_list(qdev, lrg_buf_cb1);
	ql_release_to_lrg_buf_free_list(qdev, lrg_buf_cb2);
}

static void ql_process_macip_rx_intr(struct ql3_adapter *qdev,
				     struct ib_ip_iocb_rsp *ib_ip_rsp_ptr)
{
	struct ql_rcv_buf_cb *lrg_buf_cb1 = NULL;
	struct ql_rcv_buf_cb *lrg_buf_cb2 = NULL;
	struct sk_buff *skb1 = NULL, *skb2;
	struct net_device *ndev = qdev->ndev;
	u16 length = le16_to_cpu(ib_ip_rsp_ptr->length);
	u16 size = 0;

	/*
	 * Get the inbound address list (small buffer).
	 */

	ql_get_sbuf(qdev);

	if (qdev->device_id == QL3022_DEVICE_ID) {
		/* start of first buffer on 3022 */
		lrg_buf_cb1 = ql_get_lbuf(qdev);
		skb1 = lrg_buf_cb1->skb;
		size = ETH_HLEN;
		if (*((u16 *) skb1->data) != 0xFFFF)
			size += VLAN_ETH_HLEN - ETH_HLEN;
	}

	/* start of second buffer */
	lrg_buf_cb2 = ql_get_lbuf(qdev);
	skb2 = lrg_buf_cb2->skb;

	skb_put(skb2, length);	/* Just the second buffer length here. */
	dma_unmap_single(&qdev->pdev->dev,
			 dma_unmap_addr(lrg_buf_cb2, mapaddr),
			 dma_unmap_len(lrg_buf_cb2, maplen), DMA_FROM_DEVICE);
	prefetch(skb2->data);

	skb_checksum_none_assert(skb2);
	if (qdev->device_id == QL3022_DEVICE_ID) {
		/*
		 * Copy the ethhdr from first buffer to second. This
		 * is necessary for 3022 IP completions.
		 */
		skb_copy_from_linear_data_offset(skb1, VLAN_ID_LEN,
						 skb_push(skb2, size), size);
	} else {
		u16 checksum = le16_to_cpu(ib_ip_rsp_ptr->checksum);
		if (checksum &
			(IB_IP_IOCB_RSP_3032_ICE |
			 IB_IP_IOCB_RSP_3032_CE)) {
			netdev_err(ndev,
				   "%s: Bad checksum for this %s packet, checksum = %x\n",
				   __func__,
				   ((checksum & IB_IP_IOCB_RSP_3032_TCP) ?
				    "TCP" : "UDP"), checksum);
		} else if ((checksum & IB_IP_IOCB_RSP_3032_TCP) ||
				(checksum & IB_IP_IOCB_RSP_3032_UDP &&
				!(checksum & IB_IP_IOCB_RSP_3032_NUC))) {
			skb2->ip_summed = CHECKSUM_UNNECESSARY;
		}
	}
	skb2->protocol = eth_type_trans(skb2, qdev->ndev);

	napi_gro_receive(&qdev->napi, skb2);
	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += length;
	lrg_buf_cb2->skb = NULL;

	if (qdev->device_id == QL3022_DEVICE_ID)
		ql_release_to_lrg_buf_free_list(qdev, lrg_buf_cb1);
	ql_release_to_lrg_buf_free_list(qdev, lrg_buf_cb2);
}

static int ql_tx_rx_clean(struct ql3_adapter *qdev, int budget)
{
	struct net_rsp_iocb *net_rsp;
	struct net_device *ndev = qdev->ndev;
	int work_done = 0;

	/* While there are entries in the completion queue. */
	while ((le32_to_cpu(*(qdev->prsp_producer_index)) !=
		qdev->rsp_consumer_index) && (work_done < budget)) {

		net_rsp = qdev->rsp_current;
		rmb();
		/*
		 * Fix 4032 chip's undocumented "feature" where bit-8 is set
		 * if the inbound completion is for a VLAN.
		 */
		if (qdev->device_id == QL3032_DEVICE_ID)
			net_rsp->opcode &= 0x7f;
		switch (net_rsp->opcode) {

		case OPCODE_OB_MAC_IOCB_FN0:
		case OPCODE_OB_MAC_IOCB_FN2:
			ql_process_mac_tx_intr(qdev, (struct ob_mac_iocb_rsp *)
					       net_rsp);
			break;

		case OPCODE_IB_MAC_IOCB:
		case OPCODE_IB_3032_MAC_IOCB:
			ql_process_mac_rx_intr(qdev, (struct ib_mac_iocb_rsp *)
					       net_rsp);
			work_done++;
			break;

		case OPCODE_IB_IP_IOCB:
		case OPCODE_IB_3032_IP_IOCB:
			ql_process_macip_rx_intr(qdev, (struct ib_ip_iocb_rsp *)
						 net_rsp);
			work_done++;
			break;
		default: {
			u32 *tmp = (u32 *)net_rsp;
			netdev_err(ndev,
				   "Hit default case, not handled!\n"
				   "	dropping the packet, opcode = %x\n"
				   "0x%08lx 0x%08lx 0x%08lx 0x%08lx\n",
				   net_rsp->opcode,
				   (unsigned long int)tmp[0],
				   (unsigned long int)tmp[1],
				   (unsigned long int)tmp[2],
				   (unsigned long int)tmp[3]);
		}
		}

		qdev->rsp_consumer_index++;

		if (qdev->rsp_consumer_index == NUM_RSP_Q_ENTRIES) {
			qdev->rsp_consumer_index = 0;
			qdev->rsp_current = qdev->rsp_q_virt_addr;
		} else {
			qdev->rsp_current++;
		}

	}

	return work_done;
}

static int ql_poll(struct napi_struct *napi, int budget)
{
	struct ql3_adapter *qdev = container_of(napi, struct ql3_adapter, napi);
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	int work_done;

	work_done = ql_tx_rx_clean(qdev, budget);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		unsigned long flags;

		spin_lock_irqsave(&qdev->hw_lock, flags);
		ql_update_small_bufq_prod_index(qdev);
		ql_update_lrg_bufq_prod_index(qdev);
		writel(qdev->rsp_consumer_index,
			    &port_regs->CommonRegs.rspQConsumerIndex);
		spin_unlock_irqrestore(&qdev->hw_lock, flags);

		ql_enable_interrupts(qdev);
	}
	return work_done;
}

static irqreturn_t ql3xxx_isr(int irq, void *dev_id)
{

	struct net_device *ndev = dev_id;
	struct ql3_adapter *qdev = netdev_priv(ndev);
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	u32 value;
	int handled = 1;
	u32 var;

	value = ql_read_common_reg_l(qdev,
				     &port_regs->CommonRegs.ispControlStatus);

	if (value & (ISP_CONTROL_FE | ISP_CONTROL_RI)) {
		spin_lock(&qdev->adapter_lock);
		netif_stop_queue(qdev->ndev);
		netif_carrier_off(qdev->ndev);
		ql_disable_interrupts(qdev);
		qdev->port_link_state = LS_DOWN;
		set_bit(QL_RESET_ACTIVE, &qdev->flags) ;

		if (value & ISP_CONTROL_FE) {
			/*
			 * Chip Fatal Error.
			 */
			var =
			    ql_read_page0_reg_l(qdev,
					      &port_regs->PortFatalErrStatus);
			netdev_warn(ndev,
				    "Resetting chip. PortFatalErrStatus register = 0x%x\n",
				    var);
			set_bit(QL_RESET_START, &qdev->flags) ;
		} else {
			/*
			 * Soft Reset Requested.
			 */
			set_bit(QL_RESET_PER_SCSI, &qdev->flags) ;
			netdev_err(ndev,
				   "Another function issued a reset to the chip. ISR value = %x\n",
				   value);
		}
		queue_delayed_work(qdev->workqueue, &qdev->reset_work, 0);
		spin_unlock(&qdev->adapter_lock);
	} else if (value & ISP_IMR_DISABLE_CMPL_INT) {
		ql_disable_interrupts(qdev);
		if (likely(napi_schedule_prep(&qdev->napi)))
			__napi_schedule(&qdev->napi);
	} else
		return IRQ_NONE;

	return IRQ_RETVAL(handled);
}

/*
 * Get the total number of segments needed for the given number of fragments.
 * This is necessary because outbound address lists (OAL) will be used when
 * more than two frags are given.  Each address list has 5 addr/len pairs.
 * The 5th pair in each OAL is used to  point to the next OAL if more frags
 * are coming.  That is why the frags:segment count ratio is not linear.
 */
static int ql_get_seg_count(struct ql3_adapter *qdev, unsigned short frags)
{
	if (qdev->device_id == QL3022_DEVICE_ID)
		return 1;

	if (frags <= 2)
		return frags + 1;
	else if (frags <= 6)
		return frags + 2;
	else if (frags <= 10)
		return frags + 3;
	else if (frags <= 14)
		return frags + 4;
	else if (frags <= 18)
		return frags + 5;
	return -1;
}

static void ql_hw_csum_setup(const struct sk_buff *skb,
			     struct ob_mac_iocb_req *mac_iocb_ptr)
{
	const struct iphdr *ip = ip_hdr(skb);

	mac_iocb_ptr->ip_hdr_off = skb_network_offset(skb);
	mac_iocb_ptr->ip_hdr_len = ip->ihl;

	if (ip->protocol == IPPROTO_TCP) {
		mac_iocb_ptr->flags1 |= OB_3032MAC_IOCB_REQ_TC |
			OB_3032MAC_IOCB_REQ_IC;
	} else {
		mac_iocb_ptr->flags1 |= OB_3032MAC_IOCB_REQ_UC |
			OB_3032MAC_IOCB_REQ_IC;
	}

}

/*
 * Map the buffers for this transmit.
 * This will return NETDEV_TX_BUSY or NETDEV_TX_OK based on success.
 */
static int ql_send_map(struct ql3_adapter *qdev,
				struct ob_mac_iocb_req *mac_iocb_ptr,
				struct ql_tx_buf_cb *tx_cb,
				struct sk_buff *skb)
{
	struct oal *oal;
	struct oal_entry *oal_entry;
	int len = skb_headlen(skb);
	dma_addr_t map;
	int err;
	int completed_segs, i;
	int seg_cnt, seg = 0;
	int frag_cnt = (int)skb_shinfo(skb)->nr_frags;

	seg_cnt = tx_cb->seg_count;
	/*
	 * Map the skb buffer first.
	 */
	map = dma_map_single(&qdev->pdev->dev, skb->data, len, DMA_TO_DEVICE);

	err = dma_mapping_error(&qdev->pdev->dev, map);
	if (err) {
		netdev_err(qdev->ndev, "PCI mapping failed with error: %d\n",
			   err);

		return NETDEV_TX_BUSY;
	}

	oal_entry = (struct oal_entry *)&mac_iocb_ptr->buf_addr0_low;
	oal_entry->dma_lo = cpu_to_le32(LS_64BITS(map));
	oal_entry->dma_hi = cpu_to_le32(MS_64BITS(map));
	oal_entry->len = cpu_to_le32(len);
	dma_unmap_addr_set(&tx_cb->map[seg], mapaddr, map);
	dma_unmap_len_set(&tx_cb->map[seg], maplen, len);
	seg++;

	if (seg_cnt == 1) {
		/* Terminate the last segment. */
		oal_entry->len |= cpu_to_le32(OAL_LAST_ENTRY);
		return NETDEV_TX_OK;
	}
	oal = tx_cb->oal;
	for (completed_segs = 0;
	     completed_segs < frag_cnt;
	     completed_segs++, seg++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[completed_segs];
		oal_entry++;
		/*
		 * Check for continuation requirements.
		 * It's strange but necessary.
		 * Continuation entry points to outbound address list.
		 */
		if ((seg == 2 && seg_cnt > 3) ||
		    (seg == 7 && seg_cnt > 8) ||
		    (seg == 12 && seg_cnt > 13) ||
		    (seg == 17 && seg_cnt > 18)) {
			map = dma_map_single(&qdev->pdev->dev, oal,
					     sizeof(struct oal),
					     DMA_TO_DEVICE);

			err = dma_mapping_error(&qdev->pdev->dev, map);
			if (err) {
				netdev_err(qdev->ndev,
					   "PCI mapping outbound address list with error: %d\n",
					   err);
				goto map_error;
			}

			oal_entry->dma_lo = cpu_to_le32(LS_64BITS(map));
			oal_entry->dma_hi = cpu_to_le32(MS_64BITS(map));
			oal_entry->len = cpu_to_le32(sizeof(struct oal) |
						     OAL_CONT_ENTRY);
			dma_unmap_addr_set(&tx_cb->map[seg], mapaddr, map);
			dma_unmap_len_set(&tx_cb->map[seg], maplen,
					  sizeof(struct oal));
			oal_entry = (struct oal_entry *)oal;
			oal++;
			seg++;
		}

		map = skb_frag_dma_map(&qdev->pdev->dev, frag, 0, skb_frag_size(frag),
				       DMA_TO_DEVICE);

		err = dma_mapping_error(&qdev->pdev->dev, map);
		if (err) {
			netdev_err(qdev->ndev,
				   "PCI mapping frags failed with error: %d\n",
				   err);
			goto map_error;
		}

		oal_entry->dma_lo = cpu_to_le32(LS_64BITS(map));
		oal_entry->dma_hi = cpu_to_le32(MS_64BITS(map));
		oal_entry->len = cpu_to_le32(skb_frag_size(frag));
		dma_unmap_addr_set(&tx_cb->map[seg], mapaddr, map);
		dma_unmap_len_set(&tx_cb->map[seg], maplen, skb_frag_size(frag));
		}
	/* Terminate the last segment. */
	oal_entry->len |= cpu_to_le32(OAL_LAST_ENTRY);
	return NETDEV_TX_OK;

map_error:
	/* A PCI mapping failed and now we will need to back out
	 * We need to traverse through the oal's and associated pages which
	 * have been mapped and now we must unmap them to clean up properly
	 */

	seg = 1;
	oal_entry = (struct oal_entry *)&mac_iocb_ptr->buf_addr0_low;
	oal = tx_cb->oal;
	for (i = 0; i < completed_segs; i++, seg++) {
		oal_entry++;

		/*
		 * Check for continuation requirements.
		 * It's strange but necessary.
		 */

		if ((seg == 2 && seg_cnt > 3) ||
		    (seg == 7 && seg_cnt > 8) ||
		    (seg == 12 && seg_cnt > 13) ||
		    (seg == 17 && seg_cnt > 18)) {
			dma_unmap_single(&qdev->pdev->dev,
					 dma_unmap_addr(&tx_cb->map[seg], mapaddr),
					 dma_unmap_len(&tx_cb->map[seg], maplen),
					 DMA_TO_DEVICE);
			oal++;
			seg++;
		}

		dma_unmap_page(&qdev->pdev->dev,
			       dma_unmap_addr(&tx_cb->map[seg], mapaddr),
			       dma_unmap_len(&tx_cb->map[seg], maplen),
			       DMA_TO_DEVICE);
	}

	dma_unmap_single(&qdev->pdev->dev,
			 dma_unmap_addr(&tx_cb->map[0], mapaddr),
			 dma_unmap_addr(&tx_cb->map[0], maplen),
			 DMA_TO_DEVICE);

	return NETDEV_TX_BUSY;

}

/*
 * The difference between 3022 and 3032 sends:
 * 3022 only supports a simple single segment transmission.
 * 3032 supports checksumming and scatter/gather lists (fragments).
 * The 3032 supports sglists by using the 3 addr/len pairs (ALP)
 * in the IOCB plus a chain of outbound address lists (OAL) that
 * each contain 5 ALPs.  The last ALP of the IOCB (3rd) or OAL (5th)
 * will be used to point to an OAL when more ALP entries are required.
 * The IOCB is always the top of the chain followed by one or more
 * OALs (when necessary).
 */
static netdev_tx_t ql3xxx_send(struct sk_buff *skb,
			       struct net_device *ndev)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	struct ql_tx_buf_cb *tx_cb;
	u32 tot_len = skb->len;
	struct ob_mac_iocb_req *mac_iocb_ptr;

	if (unlikely(atomic_read(&qdev->tx_count) < 2))
		return NETDEV_TX_BUSY;

	tx_cb = &qdev->tx_buf[qdev->req_producer_index];
	tx_cb->seg_count = ql_get_seg_count(qdev,
					     skb_shinfo(skb)->nr_frags);
	if (tx_cb->seg_count == -1) {
		netdev_err(ndev, "%s: invalid segment count!\n", __func__);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	mac_iocb_ptr = tx_cb->queue_entry;
	memset((void *)mac_iocb_ptr, 0, sizeof(struct ob_mac_iocb_req));
	mac_iocb_ptr->opcode = qdev->mac_ob_opcode;
	mac_iocb_ptr->flags = OB_MAC_IOCB_REQ_X;
	mac_iocb_ptr->flags |= qdev->mb_bit_mask;
	mac_iocb_ptr->transaction_id = qdev->req_producer_index;
	mac_iocb_ptr->data_len = cpu_to_le16((u16) tot_len);
	tx_cb->skb = skb;
	if (qdev->device_id == QL3032_DEVICE_ID &&
	    skb->ip_summed == CHECKSUM_PARTIAL)
		ql_hw_csum_setup(skb, mac_iocb_ptr);

	if (ql_send_map(qdev, mac_iocb_ptr, tx_cb, skb) != NETDEV_TX_OK) {
		netdev_err(ndev, "%s: Could not map the segments!\n", __func__);
		return NETDEV_TX_BUSY;
	}

	wmb();
	qdev->req_producer_index++;
	if (qdev->req_producer_index == NUM_REQ_Q_ENTRIES)
		qdev->req_producer_index = 0;
	wmb();
	ql_write_common_reg_l(qdev,
			    &port_regs->CommonRegs.reqQProducerIndex,
			    qdev->req_producer_index);

	netif_printk(qdev, tx_queued, KERN_DEBUG, ndev,
		     "tx queued, slot %d, len %d\n",
		     qdev->req_producer_index, skb->len);

	atomic_dec(&qdev->tx_count);
	return NETDEV_TX_OK;
}

static int ql_alloc_net_req_rsp_queues(struct ql3_adapter *qdev)
{
	qdev->req_q_size =
	    (u32) (NUM_REQ_Q_ENTRIES * sizeof(struct ob_mac_iocb_req));

	qdev->rsp_q_size = NUM_RSP_Q_ENTRIES * sizeof(struct net_rsp_iocb);

	/* The barrier is required to ensure request and response queue
	 * addr writes to the registers.
	 */
	wmb();

	qdev->req_q_virt_addr =
	    dma_alloc_coherent(&qdev->pdev->dev, (size_t)qdev->req_q_size,
			       &qdev->req_q_phy_addr, GFP_KERNEL);

	if ((qdev->req_q_virt_addr == NULL) ||
	    LS_64BITS(qdev->req_q_phy_addr) & (qdev->req_q_size - 1)) {
		netdev_err(qdev->ndev, "reqQ failed\n");
		return -ENOMEM;
	}

	qdev->rsp_q_virt_addr =
	    dma_alloc_coherent(&qdev->pdev->dev, (size_t)qdev->rsp_q_size,
			       &qdev->rsp_q_phy_addr, GFP_KERNEL);

	if ((qdev->rsp_q_virt_addr == NULL) ||
	    LS_64BITS(qdev->rsp_q_phy_addr) & (qdev->rsp_q_size - 1)) {
		netdev_err(qdev->ndev, "rspQ allocation failed\n");
		dma_free_coherent(&qdev->pdev->dev, (size_t)qdev->req_q_size,
				  qdev->req_q_virt_addr, qdev->req_q_phy_addr);
		return -ENOMEM;
	}

	set_bit(QL_ALLOC_REQ_RSP_Q_DONE, &qdev->flags);

	return 0;
}

static void ql_free_net_req_rsp_queues(struct ql3_adapter *qdev)
{
	if (!test_bit(QL_ALLOC_REQ_RSP_Q_DONE, &qdev->flags)) {
		netdev_info(qdev->ndev, "Already done\n");
		return;
	}

	dma_free_coherent(&qdev->pdev->dev, qdev->req_q_size,
			  qdev->req_q_virt_addr, qdev->req_q_phy_addr);

	qdev->req_q_virt_addr = NULL;

	dma_free_coherent(&qdev->pdev->dev, qdev->rsp_q_size,
			  qdev->rsp_q_virt_addr, qdev->rsp_q_phy_addr);

	qdev->rsp_q_virt_addr = NULL;

	clear_bit(QL_ALLOC_REQ_RSP_Q_DONE, &qdev->flags);
}

static int ql_alloc_buffer_queues(struct ql3_adapter *qdev)
{
	/* Create Large Buffer Queue */
	qdev->lrg_buf_q_size =
		qdev->num_lbufq_entries * sizeof(struct lrg_buf_q_entry);
	if (qdev->lrg_buf_q_size < PAGE_SIZE)
		qdev->lrg_buf_q_alloc_size = PAGE_SIZE;
	else
		qdev->lrg_buf_q_alloc_size = qdev->lrg_buf_q_size * 2;

	qdev->lrg_buf = kmalloc_array(qdev->num_large_buffers,
				      sizeof(struct ql_rcv_buf_cb),
				      GFP_KERNEL);
	if (qdev->lrg_buf == NULL)
		return -ENOMEM;

	qdev->lrg_buf_q_alloc_virt_addr =
		dma_alloc_coherent(&qdev->pdev->dev,
				   qdev->lrg_buf_q_alloc_size,
				   &qdev->lrg_buf_q_alloc_phy_addr, GFP_KERNEL);

	if (qdev->lrg_buf_q_alloc_virt_addr == NULL) {
		netdev_err(qdev->ndev, "lBufQ failed\n");
		kfree(qdev->lrg_buf);
		return -ENOMEM;
	}
	qdev->lrg_buf_q_virt_addr = qdev->lrg_buf_q_alloc_virt_addr;
	qdev->lrg_buf_q_phy_addr = qdev->lrg_buf_q_alloc_phy_addr;

	/* Create Small Buffer Queue */
	qdev->small_buf_q_size =
		NUM_SBUFQ_ENTRIES * sizeof(struct lrg_buf_q_entry);
	if (qdev->small_buf_q_size < PAGE_SIZE)
		qdev->small_buf_q_alloc_size = PAGE_SIZE;
	else
		qdev->small_buf_q_alloc_size = qdev->small_buf_q_size * 2;

	qdev->small_buf_q_alloc_virt_addr =
		dma_alloc_coherent(&qdev->pdev->dev,
				   qdev->small_buf_q_alloc_size,
				   &qdev->small_buf_q_alloc_phy_addr, GFP_KERNEL);

	if (qdev->small_buf_q_alloc_virt_addr == NULL) {
		netdev_err(qdev->ndev, "Small Buffer Queue allocation failed\n");
		dma_free_coherent(&qdev->pdev->dev,
				  qdev->lrg_buf_q_alloc_size,
				  qdev->lrg_buf_q_alloc_virt_addr,
				  qdev->lrg_buf_q_alloc_phy_addr);
		kfree(qdev->lrg_buf);
		return -ENOMEM;
	}

	qdev->small_buf_q_virt_addr = qdev->small_buf_q_alloc_virt_addr;
	qdev->small_buf_q_phy_addr = qdev->small_buf_q_alloc_phy_addr;
	set_bit(QL_ALLOC_BUFQS_DONE, &qdev->flags);
	return 0;
}

static void ql_free_buffer_queues(struct ql3_adapter *qdev)
{
	if (!test_bit(QL_ALLOC_BUFQS_DONE, &qdev->flags)) {
		netdev_info(qdev->ndev, "Already done\n");
		return;
	}
	kfree(qdev->lrg_buf);
	dma_free_coherent(&qdev->pdev->dev, qdev->lrg_buf_q_alloc_size,
			  qdev->lrg_buf_q_alloc_virt_addr,
			  qdev->lrg_buf_q_alloc_phy_addr);

	qdev->lrg_buf_q_virt_addr = NULL;

	dma_free_coherent(&qdev->pdev->dev, qdev->small_buf_q_alloc_size,
			  qdev->small_buf_q_alloc_virt_addr,
			  qdev->small_buf_q_alloc_phy_addr);

	qdev->small_buf_q_virt_addr = NULL;

	clear_bit(QL_ALLOC_BUFQS_DONE, &qdev->flags);
}

static int ql_alloc_small_buffers(struct ql3_adapter *qdev)
{
	int i;
	struct bufq_addr_element *small_buf_q_entry;

	/* Currently we allocate on one of memory and use it for smallbuffers */
	qdev->small_buf_total_size =
		(QL_ADDR_ELE_PER_BUFQ_ENTRY * NUM_SBUFQ_ENTRIES *
		 QL_SMALL_BUFFER_SIZE);

	qdev->small_buf_virt_addr =
		dma_alloc_coherent(&qdev->pdev->dev,
				   qdev->small_buf_total_size,
				   &qdev->small_buf_phy_addr, GFP_KERNEL);

	if (qdev->small_buf_virt_addr == NULL) {
		netdev_err(qdev->ndev, "Failed to get small buffer memory\n");
		return -ENOMEM;
	}

	qdev->small_buf_phy_addr_low = LS_64BITS(qdev->small_buf_phy_addr);
	qdev->small_buf_phy_addr_high = MS_64BITS(qdev->small_buf_phy_addr);

	small_buf_q_entry = qdev->small_buf_q_virt_addr;

	/* Initialize the small buffer queue. */
	for (i = 0; i < (QL_ADDR_ELE_PER_BUFQ_ENTRY * NUM_SBUFQ_ENTRIES); i++) {
		small_buf_q_entry->addr_high =
		    cpu_to_le32(qdev->small_buf_phy_addr_high);
		small_buf_q_entry->addr_low =
		    cpu_to_le32(qdev->small_buf_phy_addr_low +
				(i * QL_SMALL_BUFFER_SIZE));
		small_buf_q_entry++;
	}
	qdev->small_buf_index = 0;
	set_bit(QL_ALLOC_SMALL_BUF_DONE, &qdev->flags);
	return 0;
}

static void ql_free_small_buffers(struct ql3_adapter *qdev)
{
	if (!test_bit(QL_ALLOC_SMALL_BUF_DONE, &qdev->flags)) {
		netdev_info(qdev->ndev, "Already done\n");
		return;
	}
	if (qdev->small_buf_virt_addr != NULL) {
		dma_free_coherent(&qdev->pdev->dev,
				  qdev->small_buf_total_size,
				  qdev->small_buf_virt_addr,
				  qdev->small_buf_phy_addr);

		qdev->small_buf_virt_addr = NULL;
	}
}

static void ql_free_large_buffers(struct ql3_adapter *qdev)
{
	int i = 0;
	struct ql_rcv_buf_cb *lrg_buf_cb;

	for (i = 0; i < qdev->num_large_buffers; i++) {
		lrg_buf_cb = &qdev->lrg_buf[i];
		if (lrg_buf_cb->skb) {
			dev_kfree_skb(lrg_buf_cb->skb);
			dma_unmap_single(&qdev->pdev->dev,
					 dma_unmap_addr(lrg_buf_cb, mapaddr),
					 dma_unmap_len(lrg_buf_cb, maplen),
					 DMA_FROM_DEVICE);
			memset(lrg_buf_cb, 0, sizeof(struct ql_rcv_buf_cb));
		} else {
			break;
		}
	}
}

static void ql_init_large_buffers(struct ql3_adapter *qdev)
{
	int i;
	struct ql_rcv_buf_cb *lrg_buf_cb;
	struct bufq_addr_element *buf_addr_ele = qdev->lrg_buf_q_virt_addr;

	for (i = 0; i < qdev->num_large_buffers; i++) {
		lrg_buf_cb = &qdev->lrg_buf[i];
		buf_addr_ele->addr_high = lrg_buf_cb->buf_phy_addr_high;
		buf_addr_ele->addr_low = lrg_buf_cb->buf_phy_addr_low;
		buf_addr_ele++;
	}
	qdev->lrg_buf_index = 0;
	qdev->lrg_buf_skb_check = 0;
}

static int ql_alloc_large_buffers(struct ql3_adapter *qdev)
{
	int i;
	struct ql_rcv_buf_cb *lrg_buf_cb;
	struct sk_buff *skb;
	dma_addr_t map;
	int err;

	for (i = 0; i < qdev->num_large_buffers; i++) {
		lrg_buf_cb = &qdev->lrg_buf[i];
		memset(lrg_buf_cb, 0, sizeof(struct ql_rcv_buf_cb));

		skb = netdev_alloc_skb(qdev->ndev,
				       qdev->lrg_buffer_len);
		if (unlikely(!skb)) {
			/* Better luck next round */
			netdev_err(qdev->ndev,
				   "large buff alloc failed for %d bytes at index %d\n",
				   qdev->lrg_buffer_len * 2, i);
			ql_free_large_buffers(qdev);
			return -ENOMEM;
		} else {
			lrg_buf_cb->index = i;
			/*
			 * We save some space to copy the ethhdr from first
			 * buffer
			 */
			skb_reserve(skb, QL_HEADER_SPACE);
			map = dma_map_single(&qdev->pdev->dev, skb->data,
					     qdev->lrg_buffer_len - QL_HEADER_SPACE,
					     DMA_FROM_DEVICE);

			err = dma_mapping_error(&qdev->pdev->dev, map);
			if (err) {
				netdev_err(qdev->ndev,
					   "PCI mapping failed with error: %d\n",
					   err);
				dev_kfree_skb_irq(skb);
				ql_free_large_buffers(qdev);
				return -ENOMEM;
			}

			lrg_buf_cb->skb = skb;
			dma_unmap_addr_set(lrg_buf_cb, mapaddr, map);
			dma_unmap_len_set(lrg_buf_cb, maplen,
					  qdev->lrg_buffer_len -
					  QL_HEADER_SPACE);
			lrg_buf_cb->buf_phy_addr_low =
			    cpu_to_le32(LS_64BITS(map));
			lrg_buf_cb->buf_phy_addr_high =
			    cpu_to_le32(MS_64BITS(map));
		}
	}
	return 0;
}

static void ql_free_send_free_list(struct ql3_adapter *qdev)
{
	struct ql_tx_buf_cb *tx_cb;
	int i;

	tx_cb = &qdev->tx_buf[0];
	for (i = 0; i < NUM_REQ_Q_ENTRIES; i++) {
		kfree(tx_cb->oal);
		tx_cb->oal = NULL;
		tx_cb++;
	}
}

static int ql_create_send_free_list(struct ql3_adapter *qdev)
{
	struct ql_tx_buf_cb *tx_cb;
	int i;
	struct ob_mac_iocb_req *req_q_curr = qdev->req_q_virt_addr;

	/* Create free list of transmit buffers */
	for (i = 0; i < NUM_REQ_Q_ENTRIES; i++) {

		tx_cb = &qdev->tx_buf[i];
		tx_cb->skb = NULL;
		tx_cb->queue_entry = req_q_curr;
		req_q_curr++;
		tx_cb->oal = kmalloc(512, GFP_KERNEL);
		if (tx_cb->oal == NULL)
			return -ENOMEM;
	}
	return 0;
}

static int ql_alloc_mem_resources(struct ql3_adapter *qdev)
{
	if (qdev->ndev->mtu == NORMAL_MTU_SIZE) {
		qdev->num_lbufq_entries = NUM_LBUFQ_ENTRIES;
		qdev->lrg_buffer_len = NORMAL_MTU_SIZE;
	} else if (qdev->ndev->mtu == JUMBO_MTU_SIZE) {
		/*
		 * Bigger buffers, so less of them.
		 */
		qdev->num_lbufq_entries = JUMBO_NUM_LBUFQ_ENTRIES;
		qdev->lrg_buffer_len = JUMBO_MTU_SIZE;
	} else {
		netdev_err(qdev->ndev, "Invalid mtu size: %d.  Only %d and %d are accepted.\n",
			   qdev->ndev->mtu, NORMAL_MTU_SIZE, JUMBO_MTU_SIZE);
		return -ENOMEM;
	}
	qdev->num_large_buffers =
		qdev->num_lbufq_entries * QL_ADDR_ELE_PER_BUFQ_ENTRY;
	qdev->lrg_buffer_len += VLAN_ETH_HLEN + VLAN_ID_LEN + QL_HEADER_SPACE;
	qdev->max_frame_size =
		(qdev->lrg_buffer_len - QL_HEADER_SPACE) + ETHERNET_CRC_SIZE;

	/*
	 * First allocate a page of shared memory and use it for shadow
	 * locations of Network Request Queue Consumer Address Register and
	 * Network Completion Queue Producer Index Register
	 */
	qdev->shadow_reg_virt_addr =
		dma_alloc_coherent(&qdev->pdev->dev, PAGE_SIZE,
				   &qdev->shadow_reg_phy_addr, GFP_KERNEL);

	if (qdev->shadow_reg_virt_addr != NULL) {
		qdev->preq_consumer_index = qdev->shadow_reg_virt_addr;
		qdev->req_consumer_index_phy_addr_high =
			MS_64BITS(qdev->shadow_reg_phy_addr);
		qdev->req_consumer_index_phy_addr_low =
			LS_64BITS(qdev->shadow_reg_phy_addr);

		qdev->prsp_producer_index =
			(__le32 *) (((u8 *) qdev->preq_consumer_index) + 8);
		qdev->rsp_producer_index_phy_addr_high =
			qdev->req_consumer_index_phy_addr_high;
		qdev->rsp_producer_index_phy_addr_low =
			qdev->req_consumer_index_phy_addr_low + 8;
	} else {
		netdev_err(qdev->ndev, "shadowReg Alloc failed\n");
		return -ENOMEM;
	}

	if (ql_alloc_net_req_rsp_queues(qdev) != 0) {
		netdev_err(qdev->ndev, "ql_alloc_net_req_rsp_queues failed\n");
		goto err_req_rsp;
	}

	if (ql_alloc_buffer_queues(qdev) != 0) {
		netdev_err(qdev->ndev, "ql_alloc_buffer_queues failed\n");
		goto err_buffer_queues;
	}

	if (ql_alloc_small_buffers(qdev) != 0) {
		netdev_err(qdev->ndev, "ql_alloc_small_buffers failed\n");
		goto err_small_buffers;
	}

	if (ql_alloc_large_buffers(qdev) != 0) {
		netdev_err(qdev->ndev, "ql_alloc_large_buffers failed\n");
		goto err_small_buffers;
	}

	/* Initialize the large buffer queue. */
	ql_init_large_buffers(qdev);
	if (ql_create_send_free_list(qdev))
		goto err_free_list;

	qdev->rsp_current = qdev->rsp_q_virt_addr;

	return 0;
err_free_list:
	ql_free_send_free_list(qdev);
err_small_buffers:
	ql_free_buffer_queues(qdev);
err_buffer_queues:
	ql_free_net_req_rsp_queues(qdev);
err_req_rsp:
	dma_free_coherent(&qdev->pdev->dev, PAGE_SIZE,
			  qdev->shadow_reg_virt_addr,
			  qdev->shadow_reg_phy_addr);

	return -ENOMEM;
}

static void ql_free_mem_resources(struct ql3_adapter *qdev)
{
	ql_free_send_free_list(qdev);
	ql_free_large_buffers(qdev);
	ql_free_small_buffers(qdev);
	ql_free_buffer_queues(qdev);
	ql_free_net_req_rsp_queues(qdev);
	if (qdev->shadow_reg_virt_addr != NULL) {
		dma_free_coherent(&qdev->pdev->dev, PAGE_SIZE,
				  qdev->shadow_reg_virt_addr,
				  qdev->shadow_reg_phy_addr);
		qdev->shadow_reg_virt_addr = NULL;
	}
}

static int ql_init_misc_registers(struct ql3_adapter *qdev)
{
	struct ql3xxx_local_ram_registers __iomem *local_ram =
	    (void __iomem *)qdev->mem_map_registers;

	if (ql_sem_spinlock(qdev, QL_DDR_RAM_SEM_MASK,
			(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 4))
		return -1;

	ql_write_page2_reg(qdev,
			   &local_ram->bufletSize, qdev->nvram_data.bufletSize);

	ql_write_page2_reg(qdev,
			   &local_ram->maxBufletCount,
			   qdev->nvram_data.bufletCount);

	ql_write_page2_reg(qdev,
			   &local_ram->freeBufletThresholdLow,
			   (qdev->nvram_data.tcpWindowThreshold25 << 16) |
			   (qdev->nvram_data.tcpWindowThreshold0));

	ql_write_page2_reg(qdev,
			   &local_ram->freeBufletThresholdHigh,
			   qdev->nvram_data.tcpWindowThreshold50);

	ql_write_page2_reg(qdev,
			   &local_ram->ipHashTableBase,
			   (qdev->nvram_data.ipHashTableBaseHi << 16) |
			   qdev->nvram_data.ipHashTableBaseLo);
	ql_write_page2_reg(qdev,
			   &local_ram->ipHashTableCount,
			   qdev->nvram_data.ipHashTableSize);
	ql_write_page2_reg(qdev,
			   &local_ram->tcpHashTableBase,
			   (qdev->nvram_data.tcpHashTableBaseHi << 16) |
			   qdev->nvram_data.tcpHashTableBaseLo);
	ql_write_page2_reg(qdev,
			   &local_ram->tcpHashTableCount,
			   qdev->nvram_data.tcpHashTableSize);
	ql_write_page2_reg(qdev,
			   &local_ram->ncbBase,
			   (qdev->nvram_data.ncbTableBaseHi << 16) |
			   qdev->nvram_data.ncbTableBaseLo);
	ql_write_page2_reg(qdev,
			   &local_ram->maxNcbCount,
			   qdev->nvram_data.ncbTableSize);
	ql_write_page2_reg(qdev,
			   &local_ram->drbBase,
			   (qdev->nvram_data.drbTableBaseHi << 16) |
			   qdev->nvram_data.drbTableBaseLo);
	ql_write_page2_reg(qdev,
			   &local_ram->maxDrbCount,
			   qdev->nvram_data.drbTableSize);
	ql_sem_unlock(qdev, QL_DDR_RAM_SEM_MASK);
	return 0;
}

static int ql_adapter_initialize(struct ql3_adapter *qdev)
{
	u32 value;
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	__iomem u32 *spir = &port_regs->CommonRegs.serialPortInterfaceReg;
	struct ql3xxx_host_memory_registers __iomem *hmem_regs =
		(void __iomem *)port_regs;
	u32 delay = 10;
	int status = 0;

	if (ql_mii_setup(qdev))
		return -1;

	/* Bring out PHY out of reset */
	ql_write_common_reg(qdev, spir,
			    (ISP_SERIAL_PORT_IF_WE |
			     (ISP_SERIAL_PORT_IF_WE << 16)));
	/* Give the PHY time to come out of reset. */
	mdelay(100);
	qdev->port_link_state = LS_DOWN;
	netif_carrier_off(qdev->ndev);

	/* V2 chip fix for ARS-39168. */
	ql_write_common_reg(qdev, spir,
			    (ISP_SERIAL_PORT_IF_SDE |
			     (ISP_SERIAL_PORT_IF_SDE << 16)));

	/* Request Queue Registers */
	*((u32 *)(qdev->preq_consumer_index)) = 0;
	atomic_set(&qdev->tx_count, NUM_REQ_Q_ENTRIES);
	qdev->req_producer_index = 0;

	ql_write_page1_reg(qdev,
			   &hmem_regs->reqConsumerIndexAddrHigh,
			   qdev->req_consumer_index_phy_addr_high);
	ql_write_page1_reg(qdev,
			   &hmem_regs->reqConsumerIndexAddrLow,
			   qdev->req_consumer_index_phy_addr_low);

	ql_write_page1_reg(qdev,
			   &hmem_regs->reqBaseAddrHigh,
			   MS_64BITS(qdev->req_q_phy_addr));
	ql_write_page1_reg(qdev,
			   &hmem_regs->reqBaseAddrLow,
			   LS_64BITS(qdev->req_q_phy_addr));
	ql_write_page1_reg(qdev, &hmem_regs->reqLength, NUM_REQ_Q_ENTRIES);

	/* Response Queue Registers */
	*((__le16 *) (qdev->prsp_producer_index)) = 0;
	qdev->rsp_consumer_index = 0;
	qdev->rsp_current = qdev->rsp_q_virt_addr;

	ql_write_page1_reg(qdev,
			   &hmem_regs->rspProducerIndexAddrHigh,
			   qdev->rsp_producer_index_phy_addr_high);

	ql_write_page1_reg(qdev,
			   &hmem_regs->rspProducerIndexAddrLow,
			   qdev->rsp_producer_index_phy_addr_low);

	ql_write_page1_reg(qdev,
			   &hmem_regs->rspBaseAddrHigh,
			   MS_64BITS(qdev->rsp_q_phy_addr));

	ql_write_page1_reg(qdev,
			   &hmem_regs->rspBaseAddrLow,
			   LS_64BITS(qdev->rsp_q_phy_addr));

	ql_write_page1_reg(qdev, &hmem_regs->rspLength, NUM_RSP_Q_ENTRIES);

	/* Large Buffer Queue */
	ql_write_page1_reg(qdev,
			   &hmem_regs->rxLargeQBaseAddrHigh,
			   MS_64BITS(qdev->lrg_buf_q_phy_addr));

	ql_write_page1_reg(qdev,
			   &hmem_regs->rxLargeQBaseAddrLow,
			   LS_64BITS(qdev->lrg_buf_q_phy_addr));

	ql_write_page1_reg(qdev,
			   &hmem_regs->rxLargeQLength,
			   qdev->num_lbufq_entries);

	ql_write_page1_reg(qdev,
			   &hmem_regs->rxLargeBufferLength,
			   qdev->lrg_buffer_len);

	/* Small Buffer Queue */
	ql_write_page1_reg(qdev,
			   &hmem_regs->rxSmallQBaseAddrHigh,
			   MS_64BITS(qdev->small_buf_q_phy_addr));

	ql_write_page1_reg(qdev,
			   &hmem_regs->rxSmallQBaseAddrLow,
			   LS_64BITS(qdev->small_buf_q_phy_addr));

	ql_write_page1_reg(qdev, &hmem_regs->rxSmallQLength, NUM_SBUFQ_ENTRIES);
	ql_write_page1_reg(qdev,
			   &hmem_regs->rxSmallBufferLength,
			   QL_SMALL_BUFFER_SIZE);

	qdev->small_buf_q_producer_index = NUM_SBUFQ_ENTRIES - 1;
	qdev->small_buf_release_cnt = 8;
	qdev->lrg_buf_q_producer_index = qdev->num_lbufq_entries - 1;
	qdev->lrg_buf_release_cnt = 8;
	qdev->lrg_buf_next_free = qdev->lrg_buf_q_virt_addr;
	qdev->small_buf_index = 0;
	qdev->lrg_buf_index = 0;
	qdev->lrg_buf_free_count = 0;
	qdev->lrg_buf_free_head = NULL;
	qdev->lrg_buf_free_tail = NULL;

	ql_write_common_reg(qdev,
			    &port_regs->CommonRegs.
			    rxSmallQProducerIndex,
			    qdev->small_buf_q_producer_index);
	ql_write_common_reg(qdev,
			    &port_regs->CommonRegs.
			    rxLargeQProducerIndex,
			    qdev->lrg_buf_q_producer_index);

	/*
	 * Find out if the chip has already been initialized.  If it has, then
	 * we skip some of the initialization.
	 */
	clear_bit(QL_LINK_MASTER, &qdev->flags);
	value = ql_read_page0_reg(qdev, &port_regs->portStatus);
	if ((value & PORT_STATUS_IC) == 0) {

		/* Chip has not been configured yet, so let it rip. */
		if (ql_init_misc_registers(qdev)) {
			status = -1;
			goto out;
		}

		value = qdev->nvram_data.tcpMaxWindowSize;
		ql_write_page0_reg(qdev, &port_regs->tcpMaxWindow, value);

		value = (0xFFFF << 16) | qdev->nvram_data.extHwConfig;

		if (ql_sem_spinlock(qdev, QL_FLASH_SEM_MASK,
				(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index)
				 * 2) << 13)) {
			status = -1;
			goto out;
		}
		ql_write_page0_reg(qdev, &port_regs->ExternalHWConfig, value);
		ql_write_page0_reg(qdev, &port_regs->InternalChipConfig,
				   (((INTERNAL_CHIP_SD | INTERNAL_CHIP_WE) <<
				     16) | (INTERNAL_CHIP_SD |
					    INTERNAL_CHIP_WE)));
		ql_sem_unlock(qdev, QL_FLASH_SEM_MASK);
	}

	if (qdev->mac_index)
		ql_write_page0_reg(qdev,
				   &port_regs->mac1MaxFrameLengthReg,
				   qdev->max_frame_size);
	else
		ql_write_page0_reg(qdev,
					   &port_regs->mac0MaxFrameLengthReg,
					   qdev->max_frame_size);

	if (ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
			(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7)) {
		status = -1;
		goto out;
	}

	PHY_Setup(qdev);
	ql_init_scan_mode(qdev);
	ql_get_phy_owner(qdev);

	/* Load the MAC Configuration */

	/* Program lower 32 bits of the MAC address */
	ql_write_page0_reg(qdev, &port_regs->macAddrIndirectPtrReg,
			   (MAC_ADDR_INDIRECT_PTR_REG_RP_MASK << 16));
	ql_write_page0_reg(qdev, &port_regs->macAddrDataReg,
			   ((qdev->ndev->dev_addr[2] << 24)
			    | (qdev->ndev->dev_addr[3] << 16)
			    | (qdev->ndev->dev_addr[4] << 8)
			    | qdev->ndev->dev_addr[5]));

	/* Program top 16 bits of the MAC address */
	ql_write_page0_reg(qdev, &port_regs->macAddrIndirectPtrReg,
			   ((MAC_ADDR_INDIRECT_PTR_REG_RP_MASK << 16) | 1));
	ql_write_page0_reg(qdev, &port_regs->macAddrDataReg,
			   ((qdev->ndev->dev_addr[0] << 8)
			    | qdev->ndev->dev_addr[1]));

	/* Enable Primary MAC */
	ql_write_page0_reg(qdev, &port_regs->macAddrIndirectPtrReg,
			   ((MAC_ADDR_INDIRECT_PTR_REG_PE << 16) |
			    MAC_ADDR_INDIRECT_PTR_REG_PE));

	/* Clear Primary and Secondary IP addresses */
	ql_write_page0_reg(qdev, &port_regs->ipAddrIndexReg,
			   ((IP_ADDR_INDEX_REG_MASK << 16) |
			    (qdev->mac_index << 2)));
	ql_write_page0_reg(qdev, &port_regs->ipAddrDataReg, 0);

	ql_write_page0_reg(qdev, &port_regs->ipAddrIndexReg,
			   ((IP_ADDR_INDEX_REG_MASK << 16) |
			    ((qdev->mac_index << 2) + 1)));
	ql_write_page0_reg(qdev, &port_regs->ipAddrDataReg, 0);

	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);

	/* Indicate Configuration Complete */
	ql_write_page0_reg(qdev,
			   &port_regs->portControl,
			   ((PORT_CONTROL_CC << 16) | PORT_CONTROL_CC));

	do {
		value = ql_read_page0_reg(qdev, &port_regs->portStatus);
		if (value & PORT_STATUS_IC)
			break;
		spin_unlock_irq(&qdev->hw_lock);
		msleep(500);
		spin_lock_irq(&qdev->hw_lock);
	} while (--delay);

	if (delay == 0) {
		netdev_err(qdev->ndev, "Hw Initialization timeout\n");
		status = -1;
		goto out;
	}

	/* Enable Ethernet Function */
	if (qdev->device_id == QL3032_DEVICE_ID) {
		value =
		    (QL3032_PORT_CONTROL_EF | QL3032_PORT_CONTROL_KIE |
		     QL3032_PORT_CONTROL_EIv6 | QL3032_PORT_CONTROL_EIv4 |
			QL3032_PORT_CONTROL_ET);
		ql_write_page0_reg(qdev, &port_regs->functionControl,
				   ((value << 16) | value));
	} else {
		value =
		    (PORT_CONTROL_EF | PORT_CONTROL_ET | PORT_CONTROL_EI |
		     PORT_CONTROL_HH);
		ql_write_page0_reg(qdev, &port_regs->portControl,
				   ((value << 16) | value));
	}


out:
	return status;
}

/*
 * Caller holds hw_lock.
 */
static int ql_adapter_reset(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	int status = 0;
	u16 value;
	int max_wait_time;

	set_bit(QL_RESET_ACTIVE, &qdev->flags);
	clear_bit(QL_RESET_DONE, &qdev->flags);

	/*
	 * Issue soft reset to chip.
	 */
	netdev_printk(KERN_DEBUG, qdev->ndev, "Issue soft reset to chip\n");
	ql_write_common_reg(qdev,
			    &port_regs->CommonRegs.ispControlStatus,
			    ((ISP_CONTROL_SR << 16) | ISP_CONTROL_SR));

	/* Wait 3 seconds for reset to complete. */
	netdev_printk(KERN_DEBUG, qdev->ndev,
		      "Wait 10 milliseconds for reset to complete\n");

	/* Wait until the firmware tells us the Soft Reset is done */
	max_wait_time = 5;
	do {
		value =
		    ql_read_common_reg(qdev,
				       &port_regs->CommonRegs.ispControlStatus);
		if ((value & ISP_CONTROL_SR) == 0)
			break;

		mdelay(1000);
	} while ((--max_wait_time));

	/*
	 * Also, make sure that the Network Reset Interrupt bit has been
	 * cleared after the soft reset has taken place.
	 */
	value =
	    ql_read_common_reg(qdev, &port_regs->CommonRegs.ispControlStatus);
	if (value & ISP_CONTROL_RI) {
		netdev_printk(KERN_DEBUG, qdev->ndev,
			      "clearing RI after reset\n");
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    ispControlStatus,
				    ((ISP_CONTROL_RI << 16) | ISP_CONTROL_RI));
	}

	if (max_wait_time == 0) {
		/* Issue Force Soft Reset */
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    ispControlStatus,
				    ((ISP_CONTROL_FSR << 16) |
				     ISP_CONTROL_FSR));
		/*
		 * Wait until the firmware tells us the Force Soft Reset is
		 * done
		 */
		max_wait_time = 5;
		do {
			value = ql_read_common_reg(qdev,
						   &port_regs->CommonRegs.
						   ispControlStatus);
			if ((value & ISP_CONTROL_FSR) == 0)
				break;
			mdelay(1000);
		} while ((--max_wait_time));
	}
	if (max_wait_time == 0)
		status = 1;

	clear_bit(QL_RESET_ACTIVE, &qdev->flags);
	set_bit(QL_RESET_DONE, &qdev->flags);
	return status;
}

static void ql_set_mac_info(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	u32 value, port_status;
	u8 func_number;

	/* Get the function number */
	value =
	    ql_read_common_reg_l(qdev, &port_regs->CommonRegs.ispControlStatus);
	func_number = (u8) ((value >> 4) & OPCODE_FUNC_ID_MASK);
	port_status = ql_read_page0_reg(qdev, &port_regs->portStatus);
	switch (value & ISP_CONTROL_FN_MASK) {
	case ISP_CONTROL_FN0_NET:
		qdev->mac_index = 0;
		qdev->mac_ob_opcode = OUTBOUND_MAC_IOCB | func_number;
		qdev->mb_bit_mask = FN0_MA_BITS_MASK;
		qdev->PHYAddr = PORT0_PHY_ADDRESS;
		if (port_status & PORT_STATUS_SM0)
			set_bit(QL_LINK_OPTICAL, &qdev->flags);
		else
			clear_bit(QL_LINK_OPTICAL, &qdev->flags);
		break;

	case ISP_CONTROL_FN1_NET:
		qdev->mac_index = 1;
		qdev->mac_ob_opcode = OUTBOUND_MAC_IOCB | func_number;
		qdev->mb_bit_mask = FN1_MA_BITS_MASK;
		qdev->PHYAddr = PORT1_PHY_ADDRESS;
		if (port_status & PORT_STATUS_SM1)
			set_bit(QL_LINK_OPTICAL, &qdev->flags);
		else
			clear_bit(QL_LINK_OPTICAL, &qdev->flags);
		break;

	case ISP_CONTROL_FN0_SCSI:
	case ISP_CONTROL_FN1_SCSI:
	default:
		netdev_printk(KERN_DEBUG, qdev->ndev,
			      "Invalid function number, ispControlStatus = 0x%x\n",
			      value);
		break;
	}
	qdev->numPorts = qdev->nvram_data.version_and_numPorts >> 8;
}

static void ql_display_dev_info(struct net_device *ndev)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	struct pci_dev *pdev = qdev->pdev;

	netdev_info(ndev,
		    "%s Adapter %d RevisionID %d found %s on PCI slot %d\n",
		    DRV_NAME, qdev->index, qdev->chip_rev_id,
		    qdev->device_id == QL3032_DEVICE_ID ? "QLA3032" : "QLA3022",
		    qdev->pci_slot);
	netdev_info(ndev, "%s Interface\n",
		test_bit(QL_LINK_OPTICAL, &qdev->flags) ? "OPTICAL" : "COPPER");

	/*
	 * Print PCI bus width/type.
	 */
	netdev_info(ndev, "Bus interface is %s %s\n",
		    ((qdev->pci_width == 64) ? "64-bit" : "32-bit"),
		    ((qdev->pci_x) ? "PCI-X" : "PCI"));

	netdev_info(ndev, "mem  IO base address adjusted = 0x%p\n",
		    qdev->mem_map_registers);
	netdev_info(ndev, "Interrupt number = %d\n", pdev->irq);

	netif_info(qdev, probe, ndev, "MAC address %pM\n", ndev->dev_addr);
}

static int ql_adapter_down(struct ql3_adapter *qdev, int do_reset)
{
	struct net_device *ndev = qdev->ndev;
	int retval = 0;

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	clear_bit(QL_ADAPTER_UP, &qdev->flags);
	clear_bit(QL_LINK_MASTER, &qdev->flags);

	ql_disable_interrupts(qdev);

	free_irq(qdev->pdev->irq, ndev);

	if (qdev->msi && test_bit(QL_MSI_ENABLED, &qdev->flags)) {
		netdev_info(qdev->ndev, "calling pci_disable_msi()\n");
		clear_bit(QL_MSI_ENABLED, &qdev->flags);
		pci_disable_msi(qdev->pdev);
	}

	del_timer_sync(&qdev->adapter_timer);

	napi_disable(&qdev->napi);

	if (do_reset) {
		int soft_reset;
		unsigned long hw_flags;

		spin_lock_irqsave(&qdev->hw_lock, hw_flags);
		if (ql_wait_for_drvr_lock(qdev)) {
			soft_reset = ql_adapter_reset(qdev);
			if (soft_reset) {
				netdev_err(ndev, "ql_adapter_reset(%d) FAILED!\n",
					   qdev->index);
			}
			netdev_err(ndev,
				   "Releasing driver lock via chip reset\n");
		} else {
			netdev_err(ndev,
				   "Could not acquire driver lock to do reset!\n");
			retval = -1;
		}
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	}
	ql_free_mem_resources(qdev);
	return retval;
}

static int ql_adapter_up(struct ql3_adapter *qdev)
{
	struct net_device *ndev = qdev->ndev;
	int err;
	unsigned long irq_flags = IRQF_SHARED;
	unsigned long hw_flags;

	if (ql_alloc_mem_resources(qdev)) {
		netdev_err(ndev, "Unable to  allocate buffers\n");
		return -ENOMEM;
	}

	if (qdev->msi) {
		if (pci_enable_msi(qdev->pdev)) {
			netdev_err(ndev,
				   "User requested MSI, but MSI failed to initialize.  Continuing without MSI.\n");
			qdev->msi = 0;
		} else {
			netdev_info(ndev, "MSI Enabled...\n");
			set_bit(QL_MSI_ENABLED, &qdev->flags);
			irq_flags &= ~IRQF_SHARED;
		}
	}

	err = request_irq(qdev->pdev->irq, ql3xxx_isr,
			  irq_flags, ndev->name, ndev);
	if (err) {
		netdev_err(ndev,
			   "Failed to reserve interrupt %d - already in use\n",
			   qdev->pdev->irq);
		goto err_irq;
	}

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	if (!ql_wait_for_drvr_lock(qdev)) {
		netdev_err(ndev, "Could not acquire driver lock\n");
		err = -ENODEV;
		goto err_lock;
	}

	err = ql_adapter_initialize(qdev);
	if (err) {
		netdev_err(ndev, "Unable to initialize adapter\n");
		goto err_init;
	}
	ql_sem_unlock(qdev, QL_DRVR_SEM_MASK);

	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

	set_bit(QL_ADAPTER_UP, &qdev->flags);

	mod_timer(&qdev->adapter_timer, jiffies + HZ * 1);

	napi_enable(&qdev->napi);
	ql_enable_interrupts(qdev);
	return 0;

err_init:
	ql_sem_unlock(qdev, QL_DRVR_SEM_MASK);
err_lock:
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	free_irq(qdev->pdev->irq, ndev);
err_irq:
	if (qdev->msi && test_bit(QL_MSI_ENABLED, &qdev->flags)) {
		netdev_info(ndev, "calling pci_disable_msi()\n");
		clear_bit(QL_MSI_ENABLED, &qdev->flags);
		pci_disable_msi(qdev->pdev);
	}
	return err;
}

static int ql_cycle_adapter(struct ql3_adapter *qdev, int reset)
{
	if (ql_adapter_down(qdev, reset) || ql_adapter_up(qdev)) {
		netdev_err(qdev->ndev,
			   "Driver up/down cycle failed, closing device\n");
		rtnl_lock();
		dev_close(qdev->ndev);
		rtnl_unlock();
		return -1;
	}
	return 0;
}

static int ql3xxx_close(struct net_device *ndev)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);

	/*
	 * Wait for device to recover from a reset.
	 * (Rarely happens, but possible.)
	 */
	while (!test_bit(QL_ADAPTER_UP, &qdev->flags))
		msleep(50);

	ql_adapter_down(qdev, QL_DO_RESET);
	return 0;
}

static int ql3xxx_open(struct net_device *ndev)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	return ql_adapter_up(qdev);
}

static int ql3xxx_set_mac_address(struct net_device *ndev, void *p)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	struct ql3xxx_port_registers __iomem *port_regs =
			qdev->mem_map_registers;
	struct sockaddr *addr = p;
	unsigned long hw_flags;

	if (netif_running(ndev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(ndev, addr->sa_data);

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);
	/* Program lower 32 bits of the MAC address */
	ql_write_page0_reg(qdev, &port_regs->macAddrIndirectPtrReg,
			   (MAC_ADDR_INDIRECT_PTR_REG_RP_MASK << 16));
	ql_write_page0_reg(qdev, &port_regs->macAddrDataReg,
			   ((ndev->dev_addr[2] << 24) | (ndev->
							 dev_addr[3] << 16) |
			    (ndev->dev_addr[4] << 8) | ndev->dev_addr[5]));

	/* Program top 16 bits of the MAC address */
	ql_write_page0_reg(qdev, &port_regs->macAddrIndirectPtrReg,
			   ((MAC_ADDR_INDIRECT_PTR_REG_RP_MASK << 16) | 1));
	ql_write_page0_reg(qdev, &port_regs->macAddrDataReg,
			   ((ndev->dev_addr[0] << 8) | ndev->dev_addr[1]));
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

	return 0;
}

static void ql3xxx_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);

	netdev_err(ndev, "Resetting...\n");
	/*
	 * Stop the queues, we've got a problem.
	 */
	netif_stop_queue(ndev);

	/*
	 * Wake up the worker to process this event.
	 */
	queue_delayed_work(qdev->workqueue, &qdev->tx_timeout_work, 0);
}

static void ql_reset_work(struct work_struct *work)
{
	struct ql3_adapter *qdev =
		container_of(work, struct ql3_adapter, reset_work.work);
	struct net_device *ndev = qdev->ndev;
	u32 value;
	struct ql_tx_buf_cb *tx_cb;
	int max_wait_time, i;
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	unsigned long hw_flags;

	if (test_bit(QL_RESET_PER_SCSI, &qdev->flags) ||
	    test_bit(QL_RESET_START, &qdev->flags)) {
		clear_bit(QL_LINK_MASTER, &qdev->flags);

		/*
		 * Loop through the active list and return the skb.
		 */
		for (i = 0; i < NUM_REQ_Q_ENTRIES; i++) {
			int j;
			tx_cb = &qdev->tx_buf[i];
			if (tx_cb->skb) {
				netdev_printk(KERN_DEBUG, ndev,
					      "Freeing lost SKB\n");
				dma_unmap_single(&qdev->pdev->dev,
						 dma_unmap_addr(&tx_cb->map[0], mapaddr),
						 dma_unmap_len(&tx_cb->map[0], maplen),
						 DMA_TO_DEVICE);
				for (j = 1; j < tx_cb->seg_count; j++) {
					dma_unmap_page(&qdev->pdev->dev,
						       dma_unmap_addr(&tx_cb->map[j], mapaddr),
						       dma_unmap_len(&tx_cb->map[j], maplen),
						       DMA_TO_DEVICE);
				}
				dev_kfree_skb(tx_cb->skb);
				tx_cb->skb = NULL;
			}
		}

		netdev_err(ndev, "Clearing NRI after reset\n");
		spin_lock_irqsave(&qdev->hw_lock, hw_flags);
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    ispControlStatus,
				    ((ISP_CONTROL_RI << 16) | ISP_CONTROL_RI));
		/*
		 * Wait the for Soft Reset to Complete.
		 */
		max_wait_time = 10;
		do {
			value = ql_read_common_reg(qdev,
						   &port_regs->CommonRegs.

						   ispControlStatus);
			if ((value & ISP_CONTROL_SR) == 0) {
				netdev_printk(KERN_DEBUG, ndev,
					      "reset completed\n");
				break;
			}

			if (value & ISP_CONTROL_RI) {
				netdev_printk(KERN_DEBUG, ndev,
					      "clearing NRI after reset\n");
				ql_write_common_reg(qdev,
						    &port_regs->
						    CommonRegs.
						    ispControlStatus,
						    ((ISP_CONTROL_RI <<
						      16) | ISP_CONTROL_RI));
			}

			spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
			ssleep(1);
			spin_lock_irqsave(&qdev->hw_lock, hw_flags);
		} while (--max_wait_time);
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

		if (value & ISP_CONTROL_SR) {

			/*
			 * Set the reset flags and clear the board again.
			 * Nothing else to do...
			 */
			netdev_err(ndev,
				   "Timed out waiting for reset to complete\n");
			netdev_err(ndev, "Do a reset\n");
			clear_bit(QL_RESET_PER_SCSI, &qdev->flags);
			clear_bit(QL_RESET_START, &qdev->flags);
			ql_cycle_adapter(qdev, QL_DO_RESET);
			return;
		}

		clear_bit(QL_RESET_ACTIVE, &qdev->flags);
		clear_bit(QL_RESET_PER_SCSI, &qdev->flags);
		clear_bit(QL_RESET_START, &qdev->flags);
		ql_cycle_adapter(qdev, QL_NO_RESET);
	}
}

static void ql_tx_timeout_work(struct work_struct *work)
{
	struct ql3_adapter *qdev =
		container_of(work, struct ql3_adapter, tx_timeout_work.work);

	ql_cycle_adapter(qdev, QL_DO_RESET);
}

static void ql_get_board_info(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs =
		qdev->mem_map_registers;
	u32 value;

	value = ql_read_page0_reg_l(qdev, &port_regs->portStatus);

	qdev->chip_rev_id = ((value & PORT_STATUS_REV_ID_MASK) >> 12);
	if (value & PORT_STATUS_64)
		qdev->pci_width = 64;
	else
		qdev->pci_width = 32;
	if (value & PORT_STATUS_X)
		qdev->pci_x = 1;
	else
		qdev->pci_x = 0;
	qdev->pci_slot = (u8) PCI_SLOT(qdev->pdev->devfn);
}

static void ql3xxx_timer(struct timer_list *t)
{
	struct ql3_adapter *qdev = from_timer(qdev, t, adapter_timer);
	queue_delayed_work(qdev->workqueue, &qdev->link_state_work, 0);
}

static const struct net_device_ops ql3xxx_netdev_ops = {
	.ndo_open		= ql3xxx_open,
	.ndo_start_xmit		= ql3xxx_send,
	.ndo_stop		= ql3xxx_close,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= ql3xxx_set_mac_address,
	.ndo_tx_timeout		= ql3xxx_tx_timeout,
};

static int ql3xxx_probe(struct pci_dev *pdev,
			const struct pci_device_id *pci_entry)
{
	struct net_device *ndev = NULL;
	struct ql3_adapter *qdev = NULL;
	static int cards_found;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		pr_err("%s cannot enable PCI device\n", pci_name(pdev));
		goto err_out;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		pr_err("%s cannot obtain PCI resources\n", pci_name(pdev));
		goto err_out_disable_pdev;
	}

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		pr_err("%s no usable DMA configuration\n", pci_name(pdev));
		goto err_out_free_regions;
	}

	ndev = alloc_etherdev(sizeof(struct ql3_adapter));
	if (!ndev) {
		err = -ENOMEM;
		goto err_out_free_regions;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	pci_set_drvdata(pdev, ndev);

	qdev = netdev_priv(ndev);
	qdev->index = cards_found;
	qdev->ndev = ndev;
	qdev->pdev = pdev;
	qdev->device_id = pci_entry->device;
	qdev->port_link_state = LS_DOWN;
	if (msi)
		qdev->msi = 1;

	qdev->msg_enable = netif_msg_init(debug, default_msg);

	ndev->features |= NETIF_F_HIGHDMA;
	if (qdev->device_id == QL3032_DEVICE_ID)
		ndev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;

	qdev->mem_map_registers = pci_ioremap_bar(pdev, 1);
	if (!qdev->mem_map_registers) {
		pr_err("%s: cannot map device registers\n", pci_name(pdev));
		err = -EIO;
		goto err_out_free_ndev;
	}

	spin_lock_init(&qdev->adapter_lock);
	spin_lock_init(&qdev->hw_lock);

	/* Set driver entry points */
	ndev->netdev_ops = &ql3xxx_netdev_ops;
	ndev->ethtool_ops = &ql3xxx_ethtool_ops;
	ndev->watchdog_timeo = 5 * HZ;

	netif_napi_add(ndev, &qdev->napi, ql_poll);

	ndev->irq = pdev->irq;

	/* make sure the EEPROM is good */
	if (ql_get_nvram_params(qdev)) {
		pr_alert("%s: Adapter #%d, Invalid NVRAM parameters\n",
			 __func__, qdev->index);
		err = -EIO;
		goto err_out_iounmap;
	}

	ql_set_mac_info(qdev);

	/* Validate and set parameters */
	if (qdev->mac_index) {
		ndev->mtu = qdev->nvram_data.macCfg_port1.etherMtu_mac ;
		ql_set_mac_addr(ndev, qdev->nvram_data.funcCfg_fn2.macAddress);
	} else {
		ndev->mtu = qdev->nvram_data.macCfg_port0.etherMtu_mac ;
		ql_set_mac_addr(ndev, qdev->nvram_data.funcCfg_fn0.macAddress);
	}

	ndev->tx_queue_len = NUM_REQ_Q_ENTRIES;

	/* Record PCI bus information. */
	ql_get_board_info(qdev);

	/*
	 * Set the Maximum Memory Read Byte Count value. We do this to handle
	 * jumbo frames.
	 */
	if (qdev->pci_x)
		pci_write_config_word(pdev, (int)0x4e, (u16) 0x0036);

	err = register_netdev(ndev);
	if (err) {
		pr_err("%s: cannot register net device\n", pci_name(pdev));
		goto err_out_iounmap;
	}

	/* we're going to reset, so assume we have no link for now */

	netif_carrier_off(ndev);
	netif_stop_queue(ndev);

	qdev->workqueue = create_singlethread_workqueue(ndev->name);
	if (!qdev->workqueue) {
		unregister_netdev(ndev);
		err = -ENOMEM;
		goto err_out_iounmap;
	}

	INIT_DELAYED_WORK(&qdev->reset_work, ql_reset_work);
	INIT_DELAYED_WORK(&qdev->tx_timeout_work, ql_tx_timeout_work);
	INIT_DELAYED_WORK(&qdev->link_state_work, ql_link_state_machine_work);

	timer_setup(&qdev->adapter_timer, ql3xxx_timer, 0);
	qdev->adapter_timer.expires = jiffies + HZ * 2;	/* two second delay */

	if (!cards_found) {
		pr_alert("%s\n", DRV_STRING);
		pr_alert("Driver name: %s, Version: %s\n",
			 DRV_NAME, DRV_VERSION);
	}
	ql_display_dev_info(ndev);

	cards_found++;
	return 0;

err_out_iounmap:
	iounmap(qdev->mem_map_registers);
err_out_free_ndev:
	free_netdev(ndev);
err_out_free_regions:
	pci_release_regions(pdev);
err_out_disable_pdev:
	pci_disable_device(pdev);
err_out:
	return err;
}

static void ql3xxx_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	struct ql3_adapter *qdev = netdev_priv(ndev);

	unregister_netdev(ndev);

	ql_disable_interrupts(qdev);

	if (qdev->workqueue) {
		cancel_delayed_work(&qdev->reset_work);
		cancel_delayed_work(&qdev->tx_timeout_work);
		destroy_workqueue(qdev->workqueue);
		qdev->workqueue = NULL;
	}

	iounmap(qdev->mem_map_registers);
	pci_release_regions(pdev);
	free_netdev(ndev);
}

static struct pci_driver ql3xxx_driver = {

	.name = DRV_NAME,
	.id_table = ql3xxx_pci_tbl,
	.probe = ql3xxx_probe,
	.remove = ql3xxx_remove,
};

module_pci_driver(ql3xxx_driver);
