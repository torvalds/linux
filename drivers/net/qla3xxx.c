/*
 * QLogic QLA3xxx NIC HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla3xxx for copyright and licensing details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
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
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>

#include "qla3xxx.h"

#define DRV_NAME  	"qla3xxx"
#define DRV_STRING 	"QLogic ISP3XXX Network Driver"
#define DRV_VERSION	"v2.02.00-k36"
#define PFX		DRV_NAME " "

static const char ql3xxx_driver_name[] = DRV_NAME;
static const char ql3xxx_driver_version[] = DRV_VERSION;

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

static struct pci_device_id ql3xxx_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, QL3022_DEVICE_ID)},
	/* required last entry */
	{0,}
};

MODULE_DEVICE_TABLE(pci, ql3xxx_pci_tbl);

/*
 * Caller must take hw_lock.
 */
static int ql_sem_spinlock(struct ql3_adapter *qdev,
			    u32 sem_mask, u32 sem_bits)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
	u32 value;
	unsigned int seconds = 3;

	do {
		writel((sem_mask | sem_bits),
		       &port_regs->CommonRegs.semaphoreReg);
		value = readl(&port_regs->CommonRegs.semaphoreReg);
		if ((value & (sem_mask >> 16)) == sem_bits)
			return 0;
		ssleep(1);
	} while(--seconds);
	return -1;
}

static void ql_sem_unlock(struct ql3_adapter *qdev, u32 sem_mask)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
	writel(sem_mask, &port_regs->CommonRegs.semaphoreReg);
	readl(&port_regs->CommonRegs.semaphoreReg);
}

static int ql_sem_lock(struct ql3_adapter *qdev, u32 sem_mask, u32 sem_bits)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
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

	while (1) {
		if (!ql_sem_lock(qdev,
				 QL_DRVR_SEM_MASK,
				 (QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index)
				  * 2) << 1)) {
			if (i < 10) {
				ssleep(1);
				i++;
			} else {
				printk(KERN_ERR PFX "%s: Timed out waiting for "
				       "driver lock...\n",
				       qdev->ndev->name);
				return 0;
			}
		} else {
			printk(KERN_DEBUG PFX
			       "%s: driver lock acquired.\n",
			       qdev->ndev->name);
			return 1;
		}
	}
}

static void ql_set_register_page(struct ql3_adapter *qdev, u32 page)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;

	writel(((ISP_CONTROL_NP_MASK << 16) | page),
			&port_regs->CommonRegs.ispControlStatus);
	readl(&port_regs->CommonRegs.ispControlStatus);
	qdev->current_page = page;
}

static u32 ql_read_common_reg_l(struct ql3_adapter *qdev,
			      u32 __iomem * reg)
{
	u32 value;
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);
	value = readl(reg);
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

	return value;
}

static u32 ql_read_common_reg(struct ql3_adapter *qdev,
			      u32 __iomem * reg)
{
	return readl(reg);
}

static u32 ql_read_page0_reg_l(struct ql3_adapter *qdev, u32 __iomem *reg)
{
	u32 value;
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	if (qdev->current_page != 0)
		ql_set_register_page(qdev,0);
	value = readl(reg);

	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	return value;
}

static u32 ql_read_page0_reg(struct ql3_adapter *qdev, u32 __iomem *reg)
{
	if (qdev->current_page != 0)
		ql_set_register_page(qdev,0);
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
	return;
}

static void ql_write_common_reg(struct ql3_adapter *qdev,
				u32 __iomem *reg, u32 value)
{
	writel(value, reg);
	readl(reg);
	return;
}

static void ql_write_page0_reg(struct ql3_adapter *qdev,
			       u32 __iomem *reg, u32 value)
{
	if (qdev->current_page != 0)
		ql_set_register_page(qdev,0);
	writel(value, reg);
	readl(reg);
	return;
}

/*
 * Caller holds hw_lock. Only called during init.
 */
static void ql_write_page1_reg(struct ql3_adapter *qdev,
			       u32 __iomem *reg, u32 value)
{
	if (qdev->current_page != 1)
		ql_set_register_page(qdev,1);
	writel(value, reg);
	readl(reg);
	return;
}

/*
 * Caller holds hw_lock. Only called during init.
 */
static void ql_write_page2_reg(struct ql3_adapter *qdev,
			       u32 __iomem *reg, u32 value)
{
	if (qdev->current_page != 2)
		ql_set_register_page(qdev,2);
	writel(value, reg);
	readl(reg);
	return;
}

static void ql_disable_interrupts(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;

	ql_write_common_reg_l(qdev, &port_regs->CommonRegs.ispInterruptMaskReg,
			    (ISP_IMR_ENABLE_INT << 16));

}

static void ql_enable_interrupts(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;

	ql_write_common_reg_l(qdev, &port_regs->CommonRegs.ispInterruptMaskReg,
			    ((0xff << 16) | ISP_IMR_ENABLE_INT));

}

static void ql_release_to_lrg_buf_free_list(struct ql3_adapter *qdev,
					    struct ql_rcv_buf_cb *lrg_buf_cb)
{
	u64 map;
	lrg_buf_cb->next = NULL;

	if (qdev->lrg_buf_free_tail == NULL) {	/* The list is empty  */
		qdev->lrg_buf_free_head = qdev->lrg_buf_free_tail = lrg_buf_cb;
	} else {
		qdev->lrg_buf_free_tail->next = lrg_buf_cb;
		qdev->lrg_buf_free_tail = lrg_buf_cb;
	}

	if (!lrg_buf_cb->skb) {
		lrg_buf_cb->skb = dev_alloc_skb(qdev->lrg_buffer_len);
		if (unlikely(!lrg_buf_cb->skb)) {
			printk(KERN_ERR PFX "%s: failed dev_alloc_skb().\n",
			       qdev->ndev->name);
			qdev->lrg_buf_skb_check++;
		} else {
			/*
			 * We save some space to copy the ethhdr from first
			 * buffer
			 */
			skb_reserve(lrg_buf_cb->skb, QL_HEADER_SPACE);
			map = pci_map_single(qdev->pdev,
					     lrg_buf_cb->skb->data,
					     qdev->lrg_buffer_len -
					     QL_HEADER_SPACE,
					     PCI_DMA_FROMDEVICE);
			lrg_buf_cb->buf_phy_addr_low =
			    cpu_to_le32(LS_64BITS(map));
			lrg_buf_cb->buf_phy_addr_high =
			    cpu_to_le32(MS_64BITS(map));
			pci_unmap_addr_set(lrg_buf_cb, mapaddr, map);
			pci_unmap_len_set(lrg_buf_cb, maplen,
					  qdev->lrg_buffer_len -
					  QL_HEADER_SPACE);
		}
	}

	qdev->lrg_buf_free_count++;
}

static struct ql_rcv_buf_cb *ql_get_from_lrg_buf_free_list(struct ql3_adapter
							   *qdev)
{
	struct ql_rcv_buf_cb *lrg_buf_cb;

	if ((lrg_buf_cb = qdev->lrg_buf_free_head) != NULL) {
		if ((qdev->lrg_buf_free_head = lrg_buf_cb->next) == NULL)
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

	qdev->eeprom_cmd_data = AUBURN_EEPROM_CS_1;
	ql_write_common_reg(qdev, &port_regs->CommonRegs.serialPortInterfaceReg,
			    ISP_NVRAM_MASK | qdev->eeprom_cmd_data);
	ql_write_common_reg(qdev, &port_regs->CommonRegs.serialPortInterfaceReg,
			    ((ISP_NVRAM_MASK << 16) | qdev->eeprom_cmd_data));
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

	/* Clock in a zero, then do the start bit */
	ql_write_common_reg(qdev, &port_regs->CommonRegs.serialPortInterfaceReg,
			    ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
			    AUBURN_EEPROM_DO_1);
	ql_write_common_reg(qdev, &port_regs->CommonRegs.serialPortInterfaceReg,
			    ISP_NVRAM_MASK | qdev->
			    eeprom_cmd_data | AUBURN_EEPROM_DO_1 |
			    AUBURN_EEPROM_CLK_RISE);
	ql_write_common_reg(qdev, &port_regs->CommonRegs.serialPortInterfaceReg,
			    ISP_NVRAM_MASK | qdev->
			    eeprom_cmd_data | AUBURN_EEPROM_DO_1 |
			    AUBURN_EEPROM_CLK_FALL);

	mask = 1 << (FM93C56A_CMD_BITS - 1);
	/* Force the previous data bit to be different */
	previousBit = 0xffff;
	for (i = 0; i < FM93C56A_CMD_BITS; i++) {
		dataBit =
		    (cmd & mask) ? AUBURN_EEPROM_DO_1 : AUBURN_EEPROM_DO_0;
		if (previousBit != dataBit) {
			/*
			 * If the bit changed, then change the DO state to
			 * match
			 */
			ql_write_common_reg(qdev,
					    &port_regs->CommonRegs.
					    serialPortInterfaceReg,
					    ISP_NVRAM_MASK | qdev->
					    eeprom_cmd_data | dataBit);
			previousBit = dataBit;
		}
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    serialPortInterfaceReg,
				    ISP_NVRAM_MASK | qdev->
				    eeprom_cmd_data | dataBit |
				    AUBURN_EEPROM_CLK_RISE);
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    serialPortInterfaceReg,
				    ISP_NVRAM_MASK | qdev->
				    eeprom_cmd_data | dataBit |
				    AUBURN_EEPROM_CLK_FALL);
		cmd = cmd << 1;
	}

	mask = 1 << (addrBits - 1);
	/* Force the previous data bit to be different */
	previousBit = 0xffff;
	for (i = 0; i < addrBits; i++) {
		dataBit =
		    (eepromAddr & mask) ? AUBURN_EEPROM_DO_1 :
		    AUBURN_EEPROM_DO_0;
		if (previousBit != dataBit) {
			/*
			 * If the bit changed, then change the DO state to
			 * match
			 */
			ql_write_common_reg(qdev,
					    &port_regs->CommonRegs.
					    serialPortInterfaceReg,
					    ISP_NVRAM_MASK | qdev->
					    eeprom_cmd_data | dataBit);
			previousBit = dataBit;
		}
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    serialPortInterfaceReg,
				    ISP_NVRAM_MASK | qdev->
				    eeprom_cmd_data | dataBit |
				    AUBURN_EEPROM_CLK_RISE);
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    serialPortInterfaceReg,
				    ISP_NVRAM_MASK | qdev->
				    eeprom_cmd_data | dataBit |
				    AUBURN_EEPROM_CLK_FALL);
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
	qdev->eeprom_cmd_data = AUBURN_EEPROM_CS_0;
	ql_write_common_reg(qdev, &port_regs->CommonRegs.serialPortInterfaceReg,
			    ISP_NVRAM_MASK | qdev->eeprom_cmd_data);
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

	/* Read the data bits */
	/* The first bit is a dummy.  Clock right over it. */
	for (i = 0; i < dataBits; i++) {
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    serialPortInterfaceReg,
				    ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
				    AUBURN_EEPROM_CLK_RISE);
		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    serialPortInterfaceReg,
				    ISP_NVRAM_MASK | qdev->eeprom_cmd_data |
				    AUBURN_EEPROM_CLK_FALL);
		dataBit =
		    (ql_read_common_reg
		     (qdev,
		      &port_regs->CommonRegs.
		      serialPortInterfaceReg) & AUBURN_EEPROM_DI_1) ? 1 : 0;
		data = (data << 1) | dataBit;
	}
	*value = (u16) data;
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

static void ql_swap_mac_addr(u8 * macAddress)
{
#ifdef __BIG_ENDIAN
	u8 temp;
	temp = macAddress[0];
	macAddress[0] = macAddress[1];
	macAddress[1] = temp;
	temp = macAddress[2];
	macAddress[2] = macAddress[3];
	macAddress[3] = temp;
	temp = macAddress[4];
	macAddress[4] = macAddress[5];
	macAddress[5] = temp;
#endif
}

static int ql_get_nvram_params(struct ql3_adapter *qdev)
{
	u16 *pEEPROMData;
	u16 checksum = 0;
	u32 index;
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	pEEPROMData = (u16 *) & qdev->nvram_data;
	qdev->eeprom_cmd_data = 0;
	if(ql_sem_spinlock(qdev, QL_NVRAM_SEM_MASK,
			(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 10)) {
		printk(KERN_ERR PFX"%s: Failed ql_sem_spinlock().\n",
			__func__);
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
		printk(KERN_ERR PFX "%s: checksum should be zero, is %x!!\n",
		       qdev->ndev->name, checksum);
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
		return -1;
	}

	/*
	 * We have a problem with endianness for the MAC addresses
	 * and the two 8-bit values version, and numPorts.  We
	 * have to swap them on big endian systems.
	 */
	ql_swap_mac_addr(qdev->nvram_data.funcCfg_fn0.macAddress);
	ql_swap_mac_addr(qdev->nvram_data.funcCfg_fn1.macAddress);
	ql_swap_mac_addr(qdev->nvram_data.funcCfg_fn2.macAddress);
	ql_swap_mac_addr(qdev->nvram_data.funcCfg_fn3.macAddress);
	pEEPROMData = (u16 *) & qdev->nvram_data.version;
	*pEEPROMData = le16_to_cpu(*pEEPROMData);

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
			       u16 regAddr, u16 value, u32 mac_index)
{
	struct ql3xxx_port_registers __iomem *port_regs =
	    		qdev->mem_map_registers;
	u8 scanWasEnabled;

	scanWasEnabled = ql_mii_disable_scan_mode(qdev);

	if (ql_wait_for_mii_ready(qdev)) {
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s Timed out waiting for management port to "
			       "get free before issuing command.\n",
			       qdev->ndev->name);
		return -1;
	}

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   PHYAddr[mac_index] | regAddr);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtDataReg, value);

	/* Wait for write to complete 9/10/04 SJP */
	if (ql_wait_for_mii_ready(qdev)) {
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Timed out waiting for management port to"
			       "get free before issuing command.\n",
			       qdev->ndev->name);
		return -1;
	}

	if (scanWasEnabled)
		ql_mii_enable_scan_mode(qdev);

	return 0;
}

static int ql_mii_read_reg_ex(struct ql3_adapter *qdev, u16 regAddr,
			      u16 * value, u32 mac_index)
{
	struct ql3xxx_port_registers __iomem *port_regs =
	    		qdev->mem_map_registers;
	u8 scanWasEnabled;
	u32 temp;

	scanWasEnabled = ql_mii_disable_scan_mode(qdev);

	if (ql_wait_for_mii_ready(qdev)) {
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Timed out waiting for management port to "
			       "get free before issuing command.\n",
			       qdev->ndev->name);
		return -1;
	}

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   PHYAddr[mac_index] | regAddr);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   (MAC_MII_CONTROL_RC << 16));

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   (MAC_MII_CONTROL_RC << 16) | MAC_MII_CONTROL_RC);

	/* Wait for the read to complete */
	if (ql_wait_for_mii_ready(qdev)) {
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Timed out waiting for management port to "
			       "get free after issuing command.\n",
			       qdev->ndev->name);
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
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Timed out waiting for management port to "
			       "get free before issuing command.\n",
			       qdev->ndev->name);
		return -1;
	}

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtAddrReg,
			   qdev->PHYAddr | regAddr);

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtDataReg, value);

	/* Wait for write to complete. */
	if (ql_wait_for_mii_ready(qdev)) {
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Timed out waiting for management port to "
			       "get free before issuing command.\n",
			       qdev->ndev->name);
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
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Timed out waiting for management port to "
			       "get free before issuing command.\n",
			       qdev->ndev->name);
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
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Timed out waiting for management port to "
			       "get free before issuing command.\n",
			       qdev->ndev->name);
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

static void ql_petbi_reset_ex(struct ql3_adapter *qdev, u32 mac_index)
{
	ql_mii_write_reg_ex(qdev, PETBI_CONTROL_REG, PETBI_CTRL_SOFT_RESET,
			    mac_index);
}

static void ql_petbi_start_neg_ex(struct ql3_adapter *qdev, u32 mac_index)
{
	u16 reg;

	/* Enable Auto-negotiation sense */
	ql_mii_read_reg_ex(qdev, PETBI_TBI_CTRL, &reg, mac_index);
	reg |= PETBI_TBI_AUTO_SENSE;
	ql_mii_write_reg_ex(qdev, PETBI_TBI_CTRL, reg, mac_index);

	ql_mii_write_reg_ex(qdev, PETBI_NEG_ADVER,
			    PETBI_NEG_PAUSE | PETBI_NEG_DUPLEX, mac_index);

	ql_mii_write_reg_ex(qdev, PETBI_CONTROL_REG,
			    PETBI_CTRL_AUTO_NEG | PETBI_CTRL_RESTART_NEG |
			    PETBI_CTRL_FULL_DUPLEX | PETBI_CTRL_SPEED_1000,
			    mac_index);
}

static void ql_petbi_init(struct ql3_adapter *qdev)
{
	ql_petbi_reset(qdev);
	ql_petbi_start_neg(qdev);
}

static void ql_petbi_init_ex(struct ql3_adapter *qdev, u32 mac_index)
{
	ql_petbi_reset_ex(qdev, mac_index);
	ql_petbi_start_neg_ex(qdev, mac_index);
}

static int ql_is_petbi_neg_pause(struct ql3_adapter *qdev)
{
	u16 reg;

	if (ql_mii_read_reg(qdev, PETBI_NEG_PARTNER, &reg) < 0)
		return 0;

	return (reg & PETBI_NEG_PAUSE_MASK) == PETBI_NEG_PAUSE;
}

static int ql_phy_get_speed(struct ql3_adapter *qdev)
{
	u16 reg;

	if (ql_mii_read_reg(qdev, AUX_CONTROL_STATUS, &reg) < 0)
		return 0;

	reg = (((reg & 0x18) >> 3) & 3);

	if (reg == 2)
		return SPEED_1000;
	else if (reg == 1)
		return SPEED_100;
	else if (reg == 0)
		return SPEED_10;
	else
		return -1;
}

static int ql_is_full_dup(struct ql3_adapter *qdev)
{
	u16 reg;

	if (ql_mii_read_reg(qdev, AUX_CONTROL_STATUS, &reg) < 0)
		return 0;

	return (reg & PHY_AUX_DUPLEX_STAT) != 0;
}

static int ql_is_phy_neg_pause(struct ql3_adapter *qdev)
{
	u16 reg;

	if (ql_mii_read_reg(qdev, PHY_NEG_PARTNER, &reg) < 0)
		return 0;

	return (reg & PHY_NEG_PAUSE) != 0;
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
		if (netif_msg_link(qdev))
			printk(KERN_INFO PFX
			       "%s: Auto-Negotiate complete.\n",
			       qdev->ndev->name);
		return 1;
	} else {
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Auto-Negotiate incomplete.\n",
			       qdev->ndev->name);
		return 0;
	}
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
static int ql_this_adapter_controls_port(struct ql3_adapter *qdev,
					 u32 mac_index)
{
	struct ql3xxx_port_registers __iomem *port_regs =
	    		qdev->mem_map_registers;
	u32 bitToCheck = 0;
	u32 temp;

	switch (mac_index) {
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
		if (netif_msg_link(qdev))
			printk(KERN_DEBUG PFX
			       "%s: is not link master.\n", qdev->ndev->name);
		return 0;
	} else {
		if (netif_msg_link(qdev))
			printk(KERN_DEBUG PFX
			       "%s: is link master.\n", qdev->ndev->name);
		return 1;
	}
}

static void ql_phy_reset_ex(struct ql3_adapter *qdev, u32 mac_index)
{
	ql_mii_write_reg_ex(qdev, CONTROL_REG, PHY_CTRL_SOFT_RESET, mac_index);
}

static void ql_phy_start_neg_ex(struct ql3_adapter *qdev, u32 mac_index)
{
	u16 reg;

	ql_mii_write_reg_ex(qdev, PHY_NEG_ADVER,
			    PHY_NEG_PAUSE | PHY_NEG_ADV_SPEED | 1, mac_index);

	ql_mii_read_reg_ex(qdev, CONTROL_REG, &reg, mac_index);
	ql_mii_write_reg_ex(qdev, CONTROL_REG, reg | PHY_CTRL_RESTART_NEG,
			    mac_index);
}

static void ql_phy_init_ex(struct ql3_adapter *qdev, u32 mac_index)
{
	ql_phy_reset_ex(qdev, mac_index);
	ql_phy_start_neg_ex(qdev, mac_index);
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
	if (temp & bitToCheck) {
		linkState = LS_UP;
	} else {
		linkState = LS_DOWN;
		if (netif_msg_link(qdev))
			printk(KERN_WARNING PFX
			       "%s: Link is down.\n", qdev->ndev->name);
	}
	return linkState;
}

static int ql_port_start(struct ql3_adapter *qdev)
{
	if(ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
		(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7))
		return -1;

	if (ql_is_fiber(qdev)) {
		ql_petbi_init(qdev);
	} else {
		/* Copper port */
		ql_phy_init_ex(qdev, qdev->mac_index);
	}

	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	return 0;
}

static int ql_finish_auto_neg(struct ql3_adapter *qdev)
{

	if(ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
		(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7))
		return -1;

	if (!ql_auto_neg_error(qdev)) {
		if (test_bit(QL_LINK_MASTER,&qdev->flags)) {
			/* configure the MAC */
			if (netif_msg_link(qdev))
				printk(KERN_DEBUG PFX
				       "%s: Configuring link.\n",
				       qdev->ndev->
				       name);
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
			if (netif_msg_link(qdev))
				printk(KERN_DEBUG PFX
				       "%s: Enabling mac.\n",
				       qdev->ndev->
					       name);
			ql_mac_enable(qdev, 1);
		}

		if (netif_msg_link(qdev))
			printk(KERN_DEBUG PFX
			       "%s: Change port_link_state LS_DOWN to LS_UP.\n",
			       qdev->ndev->name);
		qdev->port_link_state = LS_UP;
		netif_start_queue(qdev->ndev);
		netif_carrier_on(qdev->ndev);
		if (netif_msg_link(qdev))
			printk(KERN_INFO PFX
			       "%s: Link is up at %d Mbps, %s duplex.\n",
			       qdev->ndev->name,
			       ql_get_link_speed(qdev),
			       ql_is_link_full_dup(qdev)
			       ? "full" : "half");

	} else {	/* Remote error detected */

		if (test_bit(QL_LINK_MASTER,&qdev->flags)) {
			if (netif_msg_link(qdev))
				printk(KERN_DEBUG PFX
				       "%s: Remote error detected. "
				       "Calling ql_port_start().\n",
				       qdev->ndev->
				       name);
			/*
			 * ql_port_start() is shared code and needs
			 * to lock the PHY on it's own.
			 */
			ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
			if(ql_port_start(qdev))	{/* Restart port */
				return -1;
			} else
				return 0;
		}
	}
	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	return 0;
}

static void ql_link_state_machine(struct ql3_adapter *qdev)
{
	u32 curr_link_state;
	unsigned long hw_flags;

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	curr_link_state = ql_get_link_state(qdev);

	if (test_bit(QL_RESET_ACTIVE,&qdev->flags)) {
		if (netif_msg_link(qdev))
			printk(KERN_INFO PFX
			       "%s: Reset in progress, skip processing link "
			       "state.\n", qdev->ndev->name);
		return;
	}

	switch (qdev->port_link_state) {
	default:
		if (test_bit(QL_LINK_MASTER,&qdev->flags)) {
			ql_port_start(qdev);
		}
		qdev->port_link_state = LS_DOWN;
		/* Fall Through */

	case LS_DOWN:
		if (netif_msg_link(qdev))
			printk(KERN_DEBUG PFX
			       "%s: port_link_state = LS_DOWN.\n",
			       qdev->ndev->name);
		if (curr_link_state == LS_UP) {
			if (netif_msg_link(qdev))
				printk(KERN_DEBUG PFX
				       "%s: curr_link_state = LS_UP.\n",
				       qdev->ndev->name);
			if (ql_is_auto_neg_complete(qdev))
				ql_finish_auto_neg(qdev);

			if (qdev->port_link_state == LS_UP)
				ql_link_down_detect_clear(qdev);

		}
		break;

	case LS_UP:
		/*
		 * See if the link is currently down or went down and came
		 * back up
		 */
		if ((curr_link_state == LS_DOWN) || ql_link_down_detect(qdev)) {
			if (netif_msg_link(qdev))
				printk(KERN_INFO PFX "%s: Link is down.\n",
				       qdev->ndev->name);
			qdev->port_link_state = LS_DOWN;
		}
		break;
	}
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
}

/*
 * Caller must take hw_lock and QL_PHY_GIO_SEM.
 */
static void ql_get_phy_owner(struct ql3_adapter *qdev)
{
	if (ql_this_adapter_controls_port(qdev, qdev->mac_index))
		set_bit(QL_LINK_MASTER,&qdev->flags);
	else
		clear_bit(QL_LINK_MASTER,&qdev->flags);
}

/*
 * Caller must take hw_lock and QL_PHY_GIO_SEM.
 */
static void ql_init_scan_mode(struct ql3_adapter *qdev)
{
	ql_mii_enable_scan_mode(qdev);

	if (test_bit(QL_LINK_OPTICAL,&qdev->flags)) {
		if (ql_this_adapter_controls_port(qdev, qdev->mac_index))
			ql_petbi_init_ex(qdev, qdev->mac_index);
	} else {
		if (ql_this_adapter_controls_port(qdev, qdev->mac_index))
			ql_phy_init_ex(qdev, qdev->mac_index);
	}
}

/*
 * MII_Setup needs to be called before taking the PHY out of reset so that the
 * management interface clock speed can be set properly.  It would be better if
 * we had a way to disable MDC until after the PHY is out of reset, but we
 * don't have that capability.
 */
static int ql_mii_setup(struct ql3_adapter *qdev)
{
	u32 reg;
	struct ql3xxx_port_registers __iomem *port_regs =
	    		qdev->mem_map_registers;

	if(ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
			(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7))
		return -1;

	/* Divide 125MHz clock by 28 to meet PHY timing requirements */
	reg = MAC_MII_CONTROL_CLK_SEL_DIV28;

	ql_write_page0_reg(qdev, &port_regs->macMIIMgmtControlReg,
			   reg | ((MAC_MII_CONTROL_CLK_SEL_MASK) << 16));

	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	return 0;
}

static u32 ql_supported_modes(struct ql3_adapter *qdev)
{
	u32 supported;

	if (test_bit(QL_LINK_OPTICAL,&qdev->flags)) {
		supported = SUPPORTED_1000baseT_Full | SUPPORTED_FIBRE
		    | SUPPORTED_Autoneg;
	} else {
		supported = SUPPORTED_10baseT_Half
		    | SUPPORTED_10baseT_Full
		    | SUPPORTED_100baseT_Half
		    | SUPPORTED_100baseT_Full
		    | SUPPORTED_1000baseT_Half
		    | SUPPORTED_1000baseT_Full
		    | SUPPORTED_Autoneg | SUPPORTED_TP;
	}

	return supported;
}

static int ql_get_auto_cfg_status(struct ql3_adapter *qdev)
{
	int status;
	unsigned long hw_flags;
	spin_lock_irqsave(&qdev->hw_lock, hw_flags);
	if(ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
		(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7))
		return 0;
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
	if(ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
		(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7))
		return 0;
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
	if(ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
		(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7))
		return 0;
	status = ql_is_link_full_dup(qdev);
	ql_sem_unlock(qdev, QL_PHY_GIO_SEM_MASK);
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);
	return status;
}


static int ql_get_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);

	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->supported = ql_supported_modes(qdev);

	if (test_bit(QL_LINK_OPTICAL,&qdev->flags)) {
		ecmd->port = PORT_FIBRE;
	} else {
		ecmd->port = PORT_TP;
		ecmd->phy_address = qdev->PHYAddr;
	}
	ecmd->advertising = ql_supported_modes(qdev);
	ecmd->autoneg = ql_get_auto_cfg_status(qdev);
	ecmd->speed = ql_get_speed(qdev);
	ecmd->duplex = ql_get_full_dup(qdev);
	return 0;
}

static void ql_get_drvinfo(struct net_device *ndev,
			   struct ethtool_drvinfo *drvinfo)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	strncpy(drvinfo->driver, ql3xxx_driver_name, 32);
	strncpy(drvinfo->version, ql3xxx_driver_version, 32);
	strncpy(drvinfo->fw_version, "N/A", 32);
	strncpy(drvinfo->bus_info, pci_name(qdev->pdev), 32);
	drvinfo->n_stats = 0;
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
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

static const struct ethtool_ops ql3xxx_ethtool_ops = {
	.get_settings = ql_get_settings,
	.get_drvinfo = ql_get_drvinfo,
	.get_perm_addr = ethtool_op_get_perm_addr,
	.get_link = ethtool_op_get_link,
	.get_msglevel = ql_get_msglevel,
	.set_msglevel = ql_set_msglevel,
};

static int ql_populate_free_queue(struct ql3_adapter *qdev)
{
	struct ql_rcv_buf_cb *lrg_buf_cb = qdev->lrg_buf_free_head;
	u64 map;

	while (lrg_buf_cb) {
		if (!lrg_buf_cb->skb) {
			lrg_buf_cb->skb = dev_alloc_skb(qdev->lrg_buffer_len);
			if (unlikely(!lrg_buf_cb->skb)) {
				printk(KERN_DEBUG PFX
				       "%s: Failed dev_alloc_skb().\n",
				       qdev->ndev->name);
				break;
			} else {
				/*
				 * We save some space to copy the ethhdr from
				 * first buffer
				 */
				skb_reserve(lrg_buf_cb->skb, QL_HEADER_SPACE);
				map = pci_map_single(qdev->pdev,
						     lrg_buf_cb->skb->data,
						     qdev->lrg_buffer_len -
						     QL_HEADER_SPACE,
						     PCI_DMA_FROMDEVICE);
				lrg_buf_cb->buf_phy_addr_low =
				    cpu_to_le32(LS_64BITS(map));
				lrg_buf_cb->buf_phy_addr_high =
				    cpu_to_le32(MS_64BITS(map));
				pci_unmap_addr_set(lrg_buf_cb, mapaddr, map);
				pci_unmap_len_set(lrg_buf_cb, maplen,
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
static void ql_update_lrg_bufq_prod_index(struct ql3_adapter *qdev)
{
	struct bufq_addr_element *lrg_buf_q_ele;
	int i;
	struct ql_rcv_buf_cb *lrg_buf_cb;
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;

	if ((qdev->lrg_buf_free_count >= 8)
	    && (qdev->lrg_buf_release_cnt >= 16)) {

		if (qdev->lrg_buf_skb_check)
			if (!ql_populate_free_queue(qdev))
				return;

		lrg_buf_q_ele = qdev->lrg_buf_next_free;

		while ((qdev->lrg_buf_release_cnt >= 16)
		       && (qdev->lrg_buf_free_count >= 8)) {

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

			if (qdev->lrg_buf_q_producer_index == NUM_LBUFQ_ENTRIES)
				qdev->lrg_buf_q_producer_index = 0;

			if (qdev->lrg_buf_q_producer_index ==
			    (NUM_LBUFQ_ENTRIES - 1)) {
				lrg_buf_q_ele = qdev->lrg_buf_q_virt_addr;
			}
		}

		qdev->lrg_buf_next_free = lrg_buf_q_ele;

		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    rxLargeQProducerIndex,
				    qdev->lrg_buf_q_producer_index);
	}
}

static void ql_process_mac_tx_intr(struct ql3_adapter *qdev,
				   struct ob_mac_iocb_rsp *mac_rsp)
{
	struct ql_tx_buf_cb *tx_cb;

	tx_cb = &qdev->tx_buf[mac_rsp->transaction_id];
	pci_unmap_single(qdev->pdev,
			 pci_unmap_addr(tx_cb, mapaddr),
			 pci_unmap_len(tx_cb, maplen), PCI_DMA_TODEVICE);
	dev_kfree_skb_irq(tx_cb->skb);
	qdev->stats.tx_packets++;
	qdev->stats.tx_bytes += tx_cb->skb->len;
	tx_cb->skb = NULL;
	atomic_inc(&qdev->tx_count);
}

static void ql_process_mac_rx_intr(struct ql3_adapter *qdev,
				   struct ib_mac_iocb_rsp *ib_mac_rsp_ptr)
{
	long int offset;
	u32 lrg_buf_phy_addr_low = 0;
	struct ql_rcv_buf_cb *lrg_buf_cb1 = NULL;
	struct ql_rcv_buf_cb *lrg_buf_cb2 = NULL;
	u32 *curr_ial_ptr;
	struct sk_buff *skb;
	u16 length = le16_to_cpu(ib_mac_rsp_ptr->length);

	/*
	 * Get the inbound address list (small buffer).
	 */
	offset = qdev->small_buf_index * QL_SMALL_BUFFER_SIZE;
	if (++qdev->small_buf_index == NUM_SMALL_BUFFERS)
		qdev->small_buf_index = 0;

	curr_ial_ptr = (u32 *) (qdev->small_buf_virt_addr + offset);
	qdev->last_rsp_offset = qdev->small_buf_phy_addr_low + offset;
	qdev->small_buf_release_cnt++;

	/* start of first buffer */
	lrg_buf_phy_addr_low = le32_to_cpu(*curr_ial_ptr);
	lrg_buf_cb1 = &qdev->lrg_buf[qdev->lrg_buf_index];
	qdev->lrg_buf_release_cnt++;
	if (++qdev->lrg_buf_index == NUM_LARGE_BUFFERS)
		qdev->lrg_buf_index = 0;
	curr_ial_ptr++;		/* 64-bit pointers require two incs. */
	curr_ial_ptr++;

	/* start of second buffer */
	lrg_buf_phy_addr_low = le32_to_cpu(*curr_ial_ptr);
	lrg_buf_cb2 = &qdev->lrg_buf[qdev->lrg_buf_index];

	/*
	 * Second buffer gets sent up the stack.
	 */
	qdev->lrg_buf_release_cnt++;
	if (++qdev->lrg_buf_index == NUM_LARGE_BUFFERS)
		qdev->lrg_buf_index = 0;
	skb = lrg_buf_cb2->skb;

	qdev->stats.rx_packets++;
	qdev->stats.rx_bytes += length;

	skb_put(skb, length);
	pci_unmap_single(qdev->pdev,
			 pci_unmap_addr(lrg_buf_cb2, mapaddr),
			 pci_unmap_len(lrg_buf_cb2, maplen),
			 PCI_DMA_FROMDEVICE);
	prefetch(skb->data);
	skb->dev = qdev->ndev;
	skb->ip_summed = CHECKSUM_NONE;
	skb->protocol = eth_type_trans(skb, qdev->ndev);

	netif_receive_skb(skb);
	qdev->ndev->last_rx = jiffies;
	lrg_buf_cb2->skb = NULL;

	ql_release_to_lrg_buf_free_list(qdev, lrg_buf_cb1);
	ql_release_to_lrg_buf_free_list(qdev, lrg_buf_cb2);
}

static void ql_process_macip_rx_intr(struct ql3_adapter *qdev,
				     struct ib_ip_iocb_rsp *ib_ip_rsp_ptr)
{
	long int offset;
	u32 lrg_buf_phy_addr_low = 0;
	struct ql_rcv_buf_cb *lrg_buf_cb1 = NULL;
	struct ql_rcv_buf_cb *lrg_buf_cb2 = NULL;
	u32 *curr_ial_ptr;
	struct sk_buff *skb1, *skb2;
	struct net_device *ndev = qdev->ndev;
	u16 length = le16_to_cpu(ib_ip_rsp_ptr->length);
	u16 size = 0;

	/*
	 * Get the inbound address list (small buffer).
	 */

	offset = qdev->small_buf_index * QL_SMALL_BUFFER_SIZE;
	if (++qdev->small_buf_index == NUM_SMALL_BUFFERS)
		qdev->small_buf_index = 0;
	curr_ial_ptr = (u32 *) (qdev->small_buf_virt_addr + offset);
	qdev->last_rsp_offset = qdev->small_buf_phy_addr_low + offset;
	qdev->small_buf_release_cnt++;

	/* start of first buffer */
	lrg_buf_phy_addr_low = le32_to_cpu(*curr_ial_ptr);
	lrg_buf_cb1 = &qdev->lrg_buf[qdev->lrg_buf_index];

	qdev->lrg_buf_release_cnt++;
	if (++qdev->lrg_buf_index == NUM_LARGE_BUFFERS)
		qdev->lrg_buf_index = 0;
	skb1 = lrg_buf_cb1->skb;
	curr_ial_ptr++;		/* 64-bit pointers require two incs. */
	curr_ial_ptr++;

	/* start of second buffer */
	lrg_buf_phy_addr_low = le32_to_cpu(*curr_ial_ptr);
	lrg_buf_cb2 = &qdev->lrg_buf[qdev->lrg_buf_index];
	skb2 = lrg_buf_cb2->skb;
	qdev->lrg_buf_release_cnt++;
	if (++qdev->lrg_buf_index == NUM_LARGE_BUFFERS)
		qdev->lrg_buf_index = 0;

	qdev->stats.rx_packets++;
	qdev->stats.rx_bytes += length;

	/*
	 * Copy the ethhdr from first buffer to second. This
	 * is necessary for IP completions.
	 */
	if (*((u16 *) skb1->data) != 0xFFFF)
		size = VLAN_ETH_HLEN;
	else
		size = ETH_HLEN;

	skb_put(skb2, length);	/* Just the second buffer length here. */
	pci_unmap_single(qdev->pdev,
			 pci_unmap_addr(lrg_buf_cb2, mapaddr),
			 pci_unmap_len(lrg_buf_cb2, maplen),
			 PCI_DMA_FROMDEVICE);
	prefetch(skb2->data);

	memcpy(skb_push(skb2, size), skb1->data + VLAN_ID_LEN, size);
	skb2->dev = qdev->ndev;
	skb2->ip_summed = CHECKSUM_NONE;
	skb2->protocol = eth_type_trans(skb2, qdev->ndev);

	netif_receive_skb(skb2);
	ndev->last_rx = jiffies;
	lrg_buf_cb2->skb = NULL;

	ql_release_to_lrg_buf_free_list(qdev, lrg_buf_cb1);
	ql_release_to_lrg_buf_free_list(qdev, lrg_buf_cb2);
}

static int ql_tx_rx_clean(struct ql3_adapter *qdev,
			  int *tx_cleaned, int *rx_cleaned, int work_to_do)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
	struct net_rsp_iocb *net_rsp;
	struct net_device *ndev = qdev->ndev;
	unsigned long hw_flags;

	/* While there are entries in the completion queue. */
	while ((cpu_to_le32(*(qdev->prsp_producer_index)) !=
		qdev->rsp_consumer_index) && (*rx_cleaned < work_to_do)) {

		net_rsp = qdev->rsp_current;
		switch (net_rsp->opcode) {

		case OPCODE_OB_MAC_IOCB_FN0:
		case OPCODE_OB_MAC_IOCB_FN2:
			ql_process_mac_tx_intr(qdev, (struct ob_mac_iocb_rsp *)
					       net_rsp);
			(*tx_cleaned)++;
			break;

		case OPCODE_IB_MAC_IOCB:
			ql_process_mac_rx_intr(qdev, (struct ib_mac_iocb_rsp *)
					       net_rsp);
			(*rx_cleaned)++;
			break;

		case OPCODE_IB_IP_IOCB:
			ql_process_macip_rx_intr(qdev, (struct ib_ip_iocb_rsp *)
						 net_rsp);
			(*rx_cleaned)++;
			break;
		default:
			{
				u32 *tmp = (u32 *) net_rsp;
				printk(KERN_ERR PFX
				       "%s: Hit default case, not "
				       "handled!\n"
				       "	dropping the packet, opcode = "
				       "%x.\n",
				       ndev->name, net_rsp->opcode);
				printk(KERN_ERR PFX
				       "0x%08lx 0x%08lx 0x%08lx 0x%08lx \n",
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

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	ql_update_lrg_bufq_prod_index(qdev);

	if (qdev->small_buf_release_cnt >= 16) {
		while (qdev->small_buf_release_cnt >= 16) {
			qdev->small_buf_q_producer_index++;

			if (qdev->small_buf_q_producer_index ==
			    NUM_SBUFQ_ENTRIES)
				qdev->small_buf_q_producer_index = 0;
			qdev->small_buf_release_cnt -= 8;
		}

		ql_write_common_reg(qdev,
				    &port_regs->CommonRegs.
				    rxSmallQProducerIndex,
				    qdev->small_buf_q_producer_index);
	}

	ql_write_common_reg(qdev,
			    &port_regs->CommonRegs.rspQConsumerIndex,
			    qdev->rsp_consumer_index);
	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

	if (unlikely(netif_queue_stopped(qdev->ndev))) {
		if (netif_queue_stopped(qdev->ndev) &&
		    (atomic_read(&qdev->tx_count) > (NUM_REQ_Q_ENTRIES / 4)))
			netif_wake_queue(qdev->ndev);
	}

	return *tx_cleaned + *rx_cleaned;
}

static int ql_poll(struct net_device *ndev, int *budget)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	int work_to_do = min(*budget, ndev->quota);
	int rx_cleaned = 0, tx_cleaned = 0;

	if (!netif_carrier_ok(ndev))
		goto quit_polling;

	ql_tx_rx_clean(qdev, &tx_cleaned, &rx_cleaned, work_to_do);
	*budget -= rx_cleaned;
	ndev->quota -= rx_cleaned;

	if ((!tx_cleaned && !rx_cleaned) || !netif_running(ndev)) {
quit_polling:
		netif_rx_complete(ndev);
		ql_enable_interrupts(qdev);
		return 0;
	}
	return 1;
}

static irqreturn_t ql3xxx_isr(int irq, void *dev_id)
{

	struct net_device *ndev = dev_id;
	struct ql3_adapter *qdev = netdev_priv(ndev);
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
	u32 value;
	int handled = 1;
	u32 var;

	port_regs = qdev->mem_map_registers;

	value =
	    ql_read_common_reg_l(qdev, &port_regs->CommonRegs.ispControlStatus);

	if (value & (ISP_CONTROL_FE | ISP_CONTROL_RI)) {
		spin_lock(&qdev->adapter_lock);
		netif_stop_queue(qdev->ndev);
		netif_carrier_off(qdev->ndev);
		ql_disable_interrupts(qdev);
		qdev->port_link_state = LS_DOWN;
		set_bit(QL_RESET_ACTIVE,&qdev->flags) ;

		if (value & ISP_CONTROL_FE) {
			/*
			 * Chip Fatal Error.
			 */
			var =
			    ql_read_page0_reg_l(qdev,
					      &port_regs->PortFatalErrStatus);
			printk(KERN_WARNING PFX
			       "%s: Resetting chip. PortFatalErrStatus "
			       "register = 0x%x\n", ndev->name, var);
			set_bit(QL_RESET_START,&qdev->flags) ;
		} else {
			/*
			 * Soft Reset Requested.
			 */
			set_bit(QL_RESET_PER_SCSI,&qdev->flags) ;
			printk(KERN_ERR PFX
			       "%s: Another function issued a reset to the "
			       "chip. ISR value = %x.\n", ndev->name, value);
		}
		queue_work(qdev->workqueue, &qdev->reset_work);
		spin_unlock(&qdev->adapter_lock);
	} else if (value & ISP_IMR_DISABLE_CMPL_INT) {
		ql_disable_interrupts(qdev);
		if (likely(netif_rx_schedule_prep(ndev)))
			__netif_rx_schedule(ndev);
		else
			ql_enable_interrupts(qdev);
	} else {
		return IRQ_NONE;
	}

	return IRQ_RETVAL(handled);
}

static int ql3xxx_send(struct sk_buff *skb, struct net_device *ndev)
{
	struct ql3_adapter *qdev = (struct ql3_adapter *)netdev_priv(ndev);
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
	struct ql_tx_buf_cb *tx_cb;
	struct ob_mac_iocb_req *mac_iocb_ptr;
	u64 map;

	if (unlikely(atomic_read(&qdev->tx_count) < 2)) {
		if (!netif_queue_stopped(ndev))
			netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}
	tx_cb = &qdev->tx_buf[qdev->req_producer_index] ;
	mac_iocb_ptr = tx_cb->queue_entry;
	memset((void *)mac_iocb_ptr, 0, sizeof(struct ob_mac_iocb_req));
	mac_iocb_ptr->opcode = qdev->mac_ob_opcode;
	mac_iocb_ptr->flags |= qdev->mb_bit_mask;
	mac_iocb_ptr->transaction_id = qdev->req_producer_index;
	mac_iocb_ptr->data_len = cpu_to_le16((u16) skb->len);
	tx_cb->skb = skb;
	map = pci_map_single(qdev->pdev, skb->data, skb->len, PCI_DMA_TODEVICE);
	mac_iocb_ptr->buf_addr0_low = cpu_to_le32(LS_64BITS(map));
	mac_iocb_ptr->buf_addr0_high = cpu_to_le32(MS_64BITS(map));
	mac_iocb_ptr->buf_0_len = cpu_to_le32(skb->len | OB_MAC_IOCB_REQ_E);
	pci_unmap_addr_set(tx_cb, mapaddr, map);
	pci_unmap_len_set(tx_cb, maplen, skb->len);
	atomic_dec(&qdev->tx_count);

	qdev->req_producer_index++;
	if (qdev->req_producer_index == NUM_REQ_Q_ENTRIES)
		qdev->req_producer_index = 0;
	wmb();
	ql_write_common_reg_l(qdev,
			    &port_regs->CommonRegs.reqQProducerIndex,
			    qdev->req_producer_index);

	ndev->trans_start = jiffies;
	if (netif_msg_tx_queued(qdev))
		printk(KERN_DEBUG PFX "%s: tx queued, slot %d, len %d\n",
		       ndev->name, qdev->req_producer_index, skb->len);

	return NETDEV_TX_OK;
}
static int ql_alloc_net_req_rsp_queues(struct ql3_adapter *qdev)
{
	qdev->req_q_size =
	    (u32) (NUM_REQ_Q_ENTRIES * sizeof(struct ob_mac_iocb_req));

	qdev->req_q_virt_addr =
	    pci_alloc_consistent(qdev->pdev,
				 (size_t) qdev->req_q_size,
				 &qdev->req_q_phy_addr);

	if ((qdev->req_q_virt_addr == NULL) ||
	    LS_64BITS(qdev->req_q_phy_addr) & (qdev->req_q_size - 1)) {
		printk(KERN_ERR PFX "%s: reqQ failed.\n",
		       qdev->ndev->name);
		return -ENOMEM;
	}

	qdev->rsp_q_size = NUM_RSP_Q_ENTRIES * sizeof(struct net_rsp_iocb);

	qdev->rsp_q_virt_addr =
	    pci_alloc_consistent(qdev->pdev,
				 (size_t) qdev->rsp_q_size,
				 &qdev->rsp_q_phy_addr);

	if ((qdev->rsp_q_virt_addr == NULL) ||
	    LS_64BITS(qdev->rsp_q_phy_addr) & (qdev->rsp_q_size - 1)) {
		printk(KERN_ERR PFX
		       "%s: rspQ allocation failed\n",
		       qdev->ndev->name);
		pci_free_consistent(qdev->pdev, (size_t) qdev->req_q_size,
				    qdev->req_q_virt_addr,
				    qdev->req_q_phy_addr);
		return -ENOMEM;
	}

	set_bit(QL_ALLOC_REQ_RSP_Q_DONE,&qdev->flags);

	return 0;
}

static void ql_free_net_req_rsp_queues(struct ql3_adapter *qdev)
{
	if (!test_bit(QL_ALLOC_REQ_RSP_Q_DONE,&qdev->flags)) {
		printk(KERN_INFO PFX
		       "%s: Already done.\n", qdev->ndev->name);
		return;
	}

	pci_free_consistent(qdev->pdev,
			    qdev->req_q_size,
			    qdev->req_q_virt_addr, qdev->req_q_phy_addr);

	qdev->req_q_virt_addr = NULL;

	pci_free_consistent(qdev->pdev,
			    qdev->rsp_q_size,
			    qdev->rsp_q_virt_addr, qdev->rsp_q_phy_addr);

	qdev->rsp_q_virt_addr = NULL;

	clear_bit(QL_ALLOC_REQ_RSP_Q_DONE,&qdev->flags);
}

static int ql_alloc_buffer_queues(struct ql3_adapter *qdev)
{
	/* Create Large Buffer Queue */
	qdev->lrg_buf_q_size =
	    NUM_LBUFQ_ENTRIES * sizeof(struct lrg_buf_q_entry);
	if (qdev->lrg_buf_q_size < PAGE_SIZE)
		qdev->lrg_buf_q_alloc_size = PAGE_SIZE;
	else
		qdev->lrg_buf_q_alloc_size = qdev->lrg_buf_q_size * 2;

	qdev->lrg_buf_q_alloc_virt_addr =
	    pci_alloc_consistent(qdev->pdev,
				 qdev->lrg_buf_q_alloc_size,
				 &qdev->lrg_buf_q_alloc_phy_addr);

	if (qdev->lrg_buf_q_alloc_virt_addr == NULL) {
		printk(KERN_ERR PFX
		       "%s: lBufQ failed\n", qdev->ndev->name);
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
	    pci_alloc_consistent(qdev->pdev,
				 qdev->small_buf_q_alloc_size,
				 &qdev->small_buf_q_alloc_phy_addr);

	if (qdev->small_buf_q_alloc_virt_addr == NULL) {
		printk(KERN_ERR PFX
		       "%s: Small Buffer Queue allocation failed.\n",
		       qdev->ndev->name);
		pci_free_consistent(qdev->pdev, qdev->lrg_buf_q_alloc_size,
				    qdev->lrg_buf_q_alloc_virt_addr,
				    qdev->lrg_buf_q_alloc_phy_addr);
		return -ENOMEM;
	}

	qdev->small_buf_q_virt_addr = qdev->small_buf_q_alloc_virt_addr;
	qdev->small_buf_q_phy_addr = qdev->small_buf_q_alloc_phy_addr;
	set_bit(QL_ALLOC_BUFQS_DONE,&qdev->flags);
	return 0;
}

static void ql_free_buffer_queues(struct ql3_adapter *qdev)
{
	if (!test_bit(QL_ALLOC_BUFQS_DONE,&qdev->flags)) {
		printk(KERN_INFO PFX
		       "%s: Already done.\n", qdev->ndev->name);
		return;
	}

	pci_free_consistent(qdev->pdev,
			    qdev->lrg_buf_q_alloc_size,
			    qdev->lrg_buf_q_alloc_virt_addr,
			    qdev->lrg_buf_q_alloc_phy_addr);

	qdev->lrg_buf_q_virt_addr = NULL;

	pci_free_consistent(qdev->pdev,
			    qdev->small_buf_q_alloc_size,
			    qdev->small_buf_q_alloc_virt_addr,
			    qdev->small_buf_q_alloc_phy_addr);

	qdev->small_buf_q_virt_addr = NULL;

	clear_bit(QL_ALLOC_BUFQS_DONE,&qdev->flags);
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
	    pci_alloc_consistent(qdev->pdev,
				 qdev->small_buf_total_size,
				 &qdev->small_buf_phy_addr);

	if (qdev->small_buf_virt_addr == NULL) {
		printk(KERN_ERR PFX
		       "%s: Failed to get small buffer memory.\n",
		       qdev->ndev->name);
		return -ENOMEM;
	}

	qdev->small_buf_phy_addr_low = LS_64BITS(qdev->small_buf_phy_addr);
	qdev->small_buf_phy_addr_high = MS_64BITS(qdev->small_buf_phy_addr);

	small_buf_q_entry = qdev->small_buf_q_virt_addr;

	qdev->last_rsp_offset = qdev->small_buf_phy_addr_low;

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
	set_bit(QL_ALLOC_SMALL_BUF_DONE,&qdev->flags);
	return 0;
}

static void ql_free_small_buffers(struct ql3_adapter *qdev)
{
	if (!test_bit(QL_ALLOC_SMALL_BUF_DONE,&qdev->flags)) {
		printk(KERN_INFO PFX
		       "%s: Already done.\n", qdev->ndev->name);
		return;
	}
	if (qdev->small_buf_virt_addr != NULL) {
		pci_free_consistent(qdev->pdev,
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

	for (i = 0; i < NUM_LARGE_BUFFERS; i++) {
		lrg_buf_cb = &qdev->lrg_buf[i];
		if (lrg_buf_cb->skb) {
			dev_kfree_skb(lrg_buf_cb->skb);
			pci_unmap_single(qdev->pdev,
					 pci_unmap_addr(lrg_buf_cb, mapaddr),
					 pci_unmap_len(lrg_buf_cb, maplen),
					 PCI_DMA_FROMDEVICE);
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

	for (i = 0; i < NUM_LARGE_BUFFERS; i++) {
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
	u64 map;

	for (i = 0; i < NUM_LARGE_BUFFERS; i++) {
		skb = dev_alloc_skb(qdev->lrg_buffer_len);
		if (unlikely(!skb)) {
			/* Better luck next round */
			printk(KERN_ERR PFX
			       "%s: large buff alloc failed, "
			       "for %d bytes at index %d.\n",
			       qdev->ndev->name,
			       qdev->lrg_buffer_len * 2, i);
			ql_free_large_buffers(qdev);
			return -ENOMEM;
		} else {

			lrg_buf_cb = &qdev->lrg_buf[i];
			memset(lrg_buf_cb, 0, sizeof(struct ql_rcv_buf_cb));
			lrg_buf_cb->index = i;
			lrg_buf_cb->skb = skb;
			/*
			 * We save some space to copy the ethhdr from first
			 * buffer
			 */
			skb_reserve(skb, QL_HEADER_SPACE);
			map = pci_map_single(qdev->pdev,
					     skb->data,
					     qdev->lrg_buffer_len -
					     QL_HEADER_SPACE,
					     PCI_DMA_FROMDEVICE);
			pci_unmap_addr_set(lrg_buf_cb, mapaddr, map);
			pci_unmap_len_set(lrg_buf_cb, maplen,
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

static void ql_create_send_free_list(struct ql3_adapter *qdev)
{
	struct ql_tx_buf_cb *tx_cb;
	int i;
	struct ob_mac_iocb_req *req_q_curr =
					qdev->req_q_virt_addr;

	/* Create free list of transmit buffers */
	for (i = 0; i < NUM_REQ_Q_ENTRIES; i++) {
		tx_cb = &qdev->tx_buf[i];
		tx_cb->skb = NULL;
		tx_cb->queue_entry = req_q_curr;
		req_q_curr++;
	}
}

static int ql_alloc_mem_resources(struct ql3_adapter *qdev)
{
	if (qdev->ndev->mtu == NORMAL_MTU_SIZE)
		qdev->lrg_buffer_len = NORMAL_MTU_SIZE;
	else if (qdev->ndev->mtu == JUMBO_MTU_SIZE) {
		qdev->lrg_buffer_len = JUMBO_MTU_SIZE;
	} else {
		printk(KERN_ERR PFX
		       "%s: Invalid mtu size.  Only 1500 and 9000 are accepted.\n",
		       qdev->ndev->name);
		return -ENOMEM;
	}
	qdev->lrg_buffer_len += VLAN_ETH_HLEN + VLAN_ID_LEN + QL_HEADER_SPACE;
	qdev->max_frame_size =
	    (qdev->lrg_buffer_len - QL_HEADER_SPACE) + ETHERNET_CRC_SIZE;

	/*
	 * First allocate a page of shared memory and use it for shadow
	 * locations of Network Request Queue Consumer Address Register and
	 * Network Completion Queue Producer Index Register
	 */
	qdev->shadow_reg_virt_addr =
	    pci_alloc_consistent(qdev->pdev,
				 PAGE_SIZE, &qdev->shadow_reg_phy_addr);

	if (qdev->shadow_reg_virt_addr != NULL) {
		qdev->preq_consumer_index = (u16 *) qdev->shadow_reg_virt_addr;
		qdev->req_consumer_index_phy_addr_high =
		    MS_64BITS(qdev->shadow_reg_phy_addr);
		qdev->req_consumer_index_phy_addr_low =
		    LS_64BITS(qdev->shadow_reg_phy_addr);

		qdev->prsp_producer_index =
		    (u32 *) (((u8 *) qdev->preq_consumer_index) + 8);
		qdev->rsp_producer_index_phy_addr_high =
		    qdev->req_consumer_index_phy_addr_high;
		qdev->rsp_producer_index_phy_addr_low =
		    qdev->req_consumer_index_phy_addr_low + 8;
	} else {
		printk(KERN_ERR PFX
		       "%s: shadowReg Alloc failed.\n", qdev->ndev->name);
		return -ENOMEM;
	}

	if (ql_alloc_net_req_rsp_queues(qdev) != 0) {
		printk(KERN_ERR PFX
		       "%s: ql_alloc_net_req_rsp_queues failed.\n",
		       qdev->ndev->name);
		goto err_req_rsp;
	}

	if (ql_alloc_buffer_queues(qdev) != 0) {
		printk(KERN_ERR PFX
		       "%s: ql_alloc_buffer_queues failed.\n",
		       qdev->ndev->name);
		goto err_buffer_queues;
	}

	if (ql_alloc_small_buffers(qdev) != 0) {
		printk(KERN_ERR PFX
		       "%s: ql_alloc_small_buffers failed\n", qdev->ndev->name);
		goto err_small_buffers;
	}

	if (ql_alloc_large_buffers(qdev) != 0) {
		printk(KERN_ERR PFX
		       "%s: ql_alloc_large_buffers failed\n", qdev->ndev->name);
		goto err_small_buffers;
	}

	/* Initialize the large buffer queue. */
	ql_init_large_buffers(qdev);
	ql_create_send_free_list(qdev);

	qdev->rsp_current = qdev->rsp_q_virt_addr;

	return 0;

err_small_buffers:
	ql_free_buffer_queues(qdev);
err_buffer_queues:
	ql_free_net_req_rsp_queues(qdev);
err_req_rsp:
	pci_free_consistent(qdev->pdev,
			    PAGE_SIZE,
			    qdev->shadow_reg_virt_addr,
			    qdev->shadow_reg_phy_addr);

	return -ENOMEM;
}

static void ql_free_mem_resources(struct ql3_adapter *qdev)
{
	ql_free_large_buffers(qdev);
	ql_free_small_buffers(qdev);
	ql_free_buffer_queues(qdev);
	ql_free_net_req_rsp_queues(qdev);
	if (qdev->shadow_reg_virt_addr != NULL) {
		pci_free_consistent(qdev->pdev,
				    PAGE_SIZE,
				    qdev->shadow_reg_virt_addr,
				    qdev->shadow_reg_phy_addr);
		qdev->shadow_reg_virt_addr = NULL;
	}
}

static int ql_init_misc_registers(struct ql3_adapter *qdev)
{
	struct ql3xxx_local_ram_registers __iomem *local_ram =
	    (void __iomem *)qdev->mem_map_registers;

	if(ql_sem_spinlock(qdev, QL_DDR_RAM_SEM_MASK,
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
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
	struct ql3xxx_host_memory_registers __iomem *hmem_regs =
						(void __iomem *)port_regs;
	u32 delay = 10;
	int status = 0;

	if(ql_mii_setup(qdev))
		return -1;

	/* Bring out PHY out of reset */
	ql_write_common_reg(qdev, &port_regs->CommonRegs.serialPortInterfaceReg,
			    (ISP_SERIAL_PORT_IF_WE |
			     (ISP_SERIAL_PORT_IF_WE << 16)));

	qdev->port_link_state = LS_DOWN;
	netif_carrier_off(qdev->ndev);

	/* V2 chip fix for ARS-39168. */
	ql_write_common_reg(qdev, &port_regs->CommonRegs.serialPortInterfaceReg,
			    (ISP_SERIAL_PORT_IF_SDE |
			     (ISP_SERIAL_PORT_IF_SDE << 16)));

	/* Request Queue Registers */
	*((u32 *) (qdev->preq_consumer_index)) = 0;
	atomic_set(&qdev->tx_count,NUM_REQ_Q_ENTRIES);
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
	*((u16 *) (qdev->prsp_producer_index)) = 0;
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

	ql_write_page1_reg(qdev, &hmem_regs->rxLargeQLength, NUM_LBUFQ_ENTRIES);

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
	qdev->lrg_buf_q_producer_index = NUM_LBUFQ_ENTRIES - 1;
	qdev->lrg_buf_release_cnt = 8;
	qdev->lrg_buf_next_free =
	    (struct bufq_addr_element *)qdev->lrg_buf_q_virt_addr;
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
		if(ql_init_misc_registers(qdev)) {
			status = -1;
			goto out;
		}

		if (qdev->mac_index)
			ql_write_page0_reg(qdev,
					   &port_regs->mac1MaxFrameLengthReg,
					   qdev->max_frame_size);
		else
			ql_write_page0_reg(qdev,
					   &port_regs->mac0MaxFrameLengthReg,
					   qdev->max_frame_size);

		value = qdev->nvram_data.tcpMaxWindowSize;
		ql_write_page0_reg(qdev, &port_regs->tcpMaxWindow, value);

		value = (0xFFFF << 16) | qdev->nvram_data.extHwConfig;

		if(ql_sem_spinlock(qdev, QL_FLASH_SEM_MASK,
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


	if(ql_sem_spinlock(qdev, QL_PHY_GIO_SEM_MASK,
			(QL_RESOURCE_BITS_BASE_CODE | (qdev->mac_index) *
			 2) << 7)) {
		status = -1;
		goto out;
	}

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
		msleep(500);
	} while (--delay);

	if (delay == 0) {
		printk(KERN_ERR PFX
		       "%s: Hw Initialization timeout.\n", qdev->ndev->name);
		status = -1;
		goto out;
	}

	/* Enable Ethernet Function */
	value =
	    (PORT_CONTROL_EF | PORT_CONTROL_ET | PORT_CONTROL_EI |
	     PORT_CONTROL_HH);
	ql_write_page0_reg(qdev, &port_regs->portControl,
			   ((value << 16) | value));

out:
	return status;
}

/*
 * Caller holds hw_lock.
 */
static int ql_adapter_reset(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
	int status = 0;
	u16 value;
	int max_wait_time;

	set_bit(QL_RESET_ACTIVE, &qdev->flags);
	clear_bit(QL_RESET_DONE, &qdev->flags);

	/*
	 * Issue soft reset to chip.
	 */
	printk(KERN_DEBUG PFX
	       "%s: Issue soft reset to chip.\n",
	       qdev->ndev->name);
	ql_write_common_reg(qdev,
			    &port_regs->CommonRegs.ispControlStatus,
			    ((ISP_CONTROL_SR << 16) | ISP_CONTROL_SR));

	/* Wait 3 seconds for reset to complete. */
	printk(KERN_DEBUG PFX
	       "%s: Wait 10 milliseconds for reset to complete.\n",
	       qdev->ndev->name);

	/* Wait until the firmware tells us the Soft Reset is done */
	max_wait_time = 5;
	do {
		value =
		    ql_read_common_reg(qdev,
				       &port_regs->CommonRegs.ispControlStatus);
		if ((value & ISP_CONTROL_SR) == 0)
			break;

		ssleep(1);
	} while ((--max_wait_time));

	/*
	 * Also, make sure that the Network Reset Interrupt bit has been
	 * cleared after the soft reset has taken place.
	 */
	value =
	    ql_read_common_reg(qdev, &port_regs->CommonRegs.ispControlStatus);
	if (value & ISP_CONTROL_RI) {
		printk(KERN_DEBUG PFX
		       "ql_adapter_reset: clearing RI after reset.\n");
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
			value =
			    ql_read_common_reg(qdev,
					       &port_regs->CommonRegs.
					       ispControlStatus);
			if ((value & ISP_CONTROL_FSR) == 0) {
				break;
			}
			ssleep(1);
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
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
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
		qdev->tcp_ob_opcode = OUTBOUND_TCP_IOCB | func_number;
		qdev->update_ob_opcode = UPDATE_NCB_IOCB | func_number;
		qdev->mb_bit_mask = FN0_MA_BITS_MASK;
		qdev->PHYAddr = PORT0_PHY_ADDRESS;
		if (port_status & PORT_STATUS_SM0)
			set_bit(QL_LINK_OPTICAL,&qdev->flags);
		else
			clear_bit(QL_LINK_OPTICAL,&qdev->flags);
		break;

	case ISP_CONTROL_FN1_NET:
		qdev->mac_index = 1;
		qdev->mac_ob_opcode = OUTBOUND_MAC_IOCB | func_number;
		qdev->tcp_ob_opcode = OUTBOUND_TCP_IOCB | func_number;
		qdev->update_ob_opcode = UPDATE_NCB_IOCB | func_number;
		qdev->mb_bit_mask = FN1_MA_BITS_MASK;
		qdev->PHYAddr = PORT1_PHY_ADDRESS;
		if (port_status & PORT_STATUS_SM1)
			set_bit(QL_LINK_OPTICAL,&qdev->flags);
		else
			clear_bit(QL_LINK_OPTICAL,&qdev->flags);
		break;

	case ISP_CONTROL_FN0_SCSI:
	case ISP_CONTROL_FN1_SCSI:
	default:
		printk(KERN_DEBUG PFX
		       "%s: Invalid function number, ispControlStatus = 0x%x\n",
		       qdev->ndev->name,value);
		break;
	}
	qdev->numPorts = qdev->nvram_data.numPorts;
}

static void ql_display_dev_info(struct net_device *ndev)
{
	struct ql3_adapter *qdev = (struct ql3_adapter *)netdev_priv(ndev);
	struct pci_dev *pdev = qdev->pdev;

	printk(KERN_INFO PFX
	       "\n%s Adapter %d RevisionID %d found on PCI slot %d.\n",
	       DRV_NAME, qdev->index, qdev->chip_rev_id, qdev->pci_slot);
	printk(KERN_INFO PFX
	       "%s Interface.\n",
	       test_bit(QL_LINK_OPTICAL,&qdev->flags) ? "OPTICAL" : "COPPER");

	/*
	 * Print PCI bus width/type.
	 */
	printk(KERN_INFO PFX
	       "Bus interface is %s %s.\n",
	       ((qdev->pci_width == 64) ? "64-bit" : "32-bit"),
	       ((qdev->pci_x) ? "PCI-X" : "PCI"));

	printk(KERN_INFO PFX
	       "mem  IO base address adjusted = 0x%p\n",
	       qdev->mem_map_registers);
	printk(KERN_INFO PFX "Interrupt number = %d\n", pdev->irq);

	if (netif_msg_probe(qdev))
		printk(KERN_INFO PFX
		       "%s: MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
		       ndev->name, ndev->dev_addr[0], ndev->dev_addr[1],
		       ndev->dev_addr[2], ndev->dev_addr[3], ndev->dev_addr[4],
		       ndev->dev_addr[5]);
}

static int ql_adapter_down(struct ql3_adapter *qdev, int do_reset)
{
	struct net_device *ndev = qdev->ndev;
	int retval = 0;

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	clear_bit(QL_ADAPTER_UP,&qdev->flags);
	clear_bit(QL_LINK_MASTER,&qdev->flags);

	ql_disable_interrupts(qdev);

	free_irq(qdev->pdev->irq, ndev);

	if (qdev->msi && test_bit(QL_MSI_ENABLED,&qdev->flags)) {
		printk(KERN_INFO PFX
		       "%s: calling pci_disable_msi().\n", qdev->ndev->name);
		clear_bit(QL_MSI_ENABLED,&qdev->flags);
		pci_disable_msi(qdev->pdev);
	}

	del_timer_sync(&qdev->adapter_timer);

	netif_poll_disable(ndev);

	if (do_reset) {
		int soft_reset;
		unsigned long hw_flags;

		spin_lock_irqsave(&qdev->hw_lock, hw_flags);
		if (ql_wait_for_drvr_lock(qdev)) {
			if ((soft_reset = ql_adapter_reset(qdev))) {
				printk(KERN_ERR PFX
				       "%s: ql_adapter_reset(%d) FAILED!\n",
				       ndev->name, qdev->index);
			}
			printk(KERN_ERR PFX
				"%s: Releaseing driver lock via chip reset.\n",ndev->name);
		} else {
			printk(KERN_ERR PFX
			       "%s: Could not acquire driver lock to do "
			       "reset!\n", ndev->name);
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
	unsigned long irq_flags = SA_SAMPLE_RANDOM | SA_SHIRQ;
	unsigned long hw_flags;

	if (ql_alloc_mem_resources(qdev)) {
		printk(KERN_ERR PFX
		       "%s Unable to  allocate buffers.\n", ndev->name);
		return -ENOMEM;
	}

	if (qdev->msi) {
		if (pci_enable_msi(qdev->pdev)) {
			printk(KERN_ERR PFX
			       "%s: User requested MSI, but MSI failed to "
			       "initialize.  Continuing without MSI.\n",
			       qdev->ndev->name);
			qdev->msi = 0;
		} else {
			printk(KERN_INFO PFX "%s: MSI Enabled...\n", qdev->ndev->name);
			set_bit(QL_MSI_ENABLED,&qdev->flags);
			irq_flags &= ~SA_SHIRQ;
		}
	}

	if ((err = request_irq(qdev->pdev->irq,
			       ql3xxx_isr,
			       irq_flags, ndev->name, ndev))) {
		printk(KERN_ERR PFX
		       "%s: Failed to reserve interrupt %d already in use.\n",
		       ndev->name, qdev->pdev->irq);
		goto err_irq;
	}

	spin_lock_irqsave(&qdev->hw_lock, hw_flags);

	if ((err = ql_wait_for_drvr_lock(qdev))) {
		if ((err = ql_adapter_initialize(qdev))) {
			printk(KERN_ERR PFX
			       "%s: Unable to initialize adapter.\n",
			       ndev->name);
			goto err_init;
		}
		printk(KERN_ERR PFX
				"%s: Releaseing driver lock.\n",ndev->name);
		ql_sem_unlock(qdev, QL_DRVR_SEM_MASK);
	} else {
		printk(KERN_ERR PFX
		       "%s: Could not aquire driver lock.\n",
		       ndev->name);
		goto err_lock;
	}

	spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

	set_bit(QL_ADAPTER_UP,&qdev->flags);

	mod_timer(&qdev->adapter_timer, jiffies + HZ * 1);

	netif_poll_enable(ndev);
	ql_enable_interrupts(qdev);
	return 0;

err_init:
	ql_sem_unlock(qdev, QL_DRVR_SEM_MASK);
err_lock:
	free_irq(qdev->pdev->irq, ndev);
err_irq:
	if (qdev->msi && test_bit(QL_MSI_ENABLED,&qdev->flags)) {
		printk(KERN_INFO PFX
		       "%s: calling pci_disable_msi().\n",
		       qdev->ndev->name);
		clear_bit(QL_MSI_ENABLED,&qdev->flags);
		pci_disable_msi(qdev->pdev);
	}
	return err;
}

static int ql_cycle_adapter(struct ql3_adapter *qdev, int reset)
{
	if( ql_adapter_down(qdev,reset) || ql_adapter_up(qdev)) {
		printk(KERN_ERR PFX
				"%s: Driver up/down cycle failed, "
				"closing device\n",qdev->ndev->name);
		dev_close(qdev->ndev);
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
	while (!test_bit(QL_ADAPTER_UP,&qdev->flags))
		msleep(50);

	ql_adapter_down(qdev,QL_DO_RESET);
	return 0;
}

static int ql3xxx_open(struct net_device *ndev)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	return (ql_adapter_up(qdev));
}

static struct net_device_stats *ql3xxx_get_stats(struct net_device *dev)
{
	struct ql3_adapter *qdev = (struct ql3_adapter *)dev->priv;
	return &qdev->stats;
}

static int ql3xxx_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct ql3_adapter *qdev = netdev_priv(ndev);
	printk(KERN_ERR PFX "%s:  new mtu size = %d.\n", ndev->name, new_mtu);
	if (new_mtu != NORMAL_MTU_SIZE && new_mtu != JUMBO_MTU_SIZE) {
		printk(KERN_ERR PFX
		       "%s: mtu size of %d is not valid.  Use exactly %d or "
		       "%d.\n", ndev->name, new_mtu, NORMAL_MTU_SIZE,
		       JUMBO_MTU_SIZE);
		return -EINVAL;
	}

	if (!netif_running(ndev)) {
		ndev->mtu = new_mtu;
		return 0;
	}

	ndev->mtu = new_mtu;
	return ql_cycle_adapter(qdev,QL_DO_RESET);
}

static void ql3xxx_set_multicast_list(struct net_device *ndev)
{
	/*
	 * We are manually parsing the list in the net_device structure.
	 */
	return;
}

static int ql3xxx_set_mac_address(struct net_device *ndev, void *p)
{
	struct ql3_adapter *qdev = (struct ql3_adapter *)netdev_priv(ndev);
	struct ql3xxx_port_registers __iomem *port_regs =
	    		qdev->mem_map_registers;
	struct sockaddr *addr = p;
	unsigned long hw_flags;

	if (netif_running(ndev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(ndev->dev_addr, addr->sa_data, ndev->addr_len);

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

static void ql3xxx_tx_timeout(struct net_device *ndev)
{
	struct ql3_adapter *qdev = (struct ql3_adapter *)netdev_priv(ndev);

	printk(KERN_ERR PFX "%s: Resetting...\n", ndev->name);
	/*
	 * Stop the queues, we've got a problem.
	 */
	netif_stop_queue(ndev);

	/*
	 * Wake up the worker to process this event.
	 */
	queue_work(qdev->workqueue, &qdev->tx_timeout_work);
}

static void ql_reset_work(struct ql3_adapter *qdev)
{
	struct net_device *ndev = qdev->ndev;
	u32 value;
	struct ql_tx_buf_cb *tx_cb;
	int max_wait_time, i;
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
	unsigned long hw_flags;

	if (test_bit((QL_RESET_PER_SCSI | QL_RESET_START),&qdev->flags)) {
		clear_bit(QL_LINK_MASTER,&qdev->flags);

		/*
		 * Loop through the active list and return the skb.
		 */
		for (i = 0; i < NUM_REQ_Q_ENTRIES; i++) {
			tx_cb = &qdev->tx_buf[i];
			if (tx_cb->skb) {

				printk(KERN_DEBUG PFX
				       "%s: Freeing lost SKB.\n",
				       qdev->ndev->name);
				pci_unmap_single(qdev->pdev,
					pci_unmap_addr(tx_cb, mapaddr),
					pci_unmap_len(tx_cb, maplen), PCI_DMA_TODEVICE);
				dev_kfree_skb(tx_cb->skb);
				tx_cb->skb = NULL;
			}
		}

		printk(KERN_ERR PFX
		       "%s: Clearing NRI after reset.\n", qdev->ndev->name);
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
				printk(KERN_DEBUG PFX
				       "%s: reset completed.\n",
				       qdev->ndev->name);
				break;
			}

			if (value & ISP_CONTROL_RI) {
				printk(KERN_DEBUG PFX
				       "%s: clearing NRI after reset.\n",
				       qdev->ndev->name);
				ql_write_common_reg(qdev,
						    &port_regs->
						    CommonRegs.
						    ispControlStatus,
						    ((ISP_CONTROL_RI <<
						      16) | ISP_CONTROL_RI));
			}

			ssleep(1);
		} while (--max_wait_time);
		spin_unlock_irqrestore(&qdev->hw_lock, hw_flags);

		if (value & ISP_CONTROL_SR) {

			/*
			 * Set the reset flags and clear the board again.
			 * Nothing else to do...
			 */
			printk(KERN_ERR PFX
			       "%s: Timed out waiting for reset to "
			       "complete.\n", ndev->name);
			printk(KERN_ERR PFX
			       "%s: Do a reset.\n", ndev->name);
			clear_bit(QL_RESET_PER_SCSI,&qdev->flags);
			clear_bit(QL_RESET_START,&qdev->flags);
			ql_cycle_adapter(qdev,QL_DO_RESET);
			return;
		}

		clear_bit(QL_RESET_ACTIVE,&qdev->flags);
		clear_bit(QL_RESET_PER_SCSI,&qdev->flags);
		clear_bit(QL_RESET_START,&qdev->flags);
		ql_cycle_adapter(qdev,QL_NO_RESET);
	}
}

static void ql_tx_timeout_work(struct ql3_adapter *qdev)
{
	ql_cycle_adapter(qdev,QL_DO_RESET);
}

static void ql_get_board_info(struct ql3_adapter *qdev)
{
	struct ql3xxx_port_registers __iomem *port_regs = qdev->mem_map_registers;
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

static void ql3xxx_timer(unsigned long ptr)
{
	struct ql3_adapter *qdev = (struct ql3_adapter *)ptr;

	if (test_bit(QL_RESET_ACTIVE,&qdev->flags)) {
		printk(KERN_DEBUG PFX
		       "%s: Reset in progress.\n",
		       qdev->ndev->name);
		goto end;
	}

	ql_link_state_machine(qdev);

	/* Restart timer on 2 second interval. */
end:
	mod_timer(&qdev->adapter_timer, jiffies + HZ * 1);
}

static int __devinit ql3xxx_probe(struct pci_dev *pdev,
				  const struct pci_device_id *pci_entry)
{
	struct net_device *ndev = NULL;
	struct ql3_adapter *qdev = NULL;
	static int cards_found = 0;
	int pci_using_dac, err;

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX "%s cannot enable PCI device\n",
		       pci_name(pdev));
		goto err_out;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		printk(KERN_ERR PFX "%s cannot obtain PCI resources\n",
		       pci_name(pdev));
		goto err_out_disable_pdev;
	}

	pci_set_master(pdev);

	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK)) {
		pci_using_dac = 1;
		err = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
	} else if (!(err = pci_set_dma_mask(pdev, DMA_32BIT_MASK))) {
		pci_using_dac = 0;
		err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	}

	if (err) {
		printk(KERN_ERR PFX "%s no usable DMA configuration\n",
		       pci_name(pdev));
		goto err_out_free_regions;
	}

	ndev = alloc_etherdev(sizeof(struct ql3_adapter));
	if (!ndev)
		goto err_out_free_regions;

	SET_MODULE_OWNER(ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	ndev->features = NETIF_F_LLTX;
	if (pci_using_dac)
		ndev->features |= NETIF_F_HIGHDMA;

	pci_set_drvdata(pdev, ndev);

	qdev = netdev_priv(ndev);
	qdev->index = cards_found;
	qdev->ndev = ndev;
	qdev->pdev = pdev;
	qdev->port_link_state = LS_DOWN;
	if (msi)
		qdev->msi = 1;

	qdev->msg_enable = netif_msg_init(debug, default_msg);

	qdev->mem_map_registers =
	    ioremap_nocache(pci_resource_start(pdev, 1),
			    pci_resource_len(qdev->pdev, 1));
	if (!qdev->mem_map_registers) {
		printk(KERN_ERR PFX "%s: cannot map device registers\n",
		       pci_name(pdev));
		goto err_out_free_ndev;
	}

	spin_lock_init(&qdev->adapter_lock);
	spin_lock_init(&qdev->hw_lock);

	/* Set driver entry points */
	ndev->open = ql3xxx_open;
	ndev->hard_start_xmit = ql3xxx_send;
	ndev->stop = ql3xxx_close;
	ndev->get_stats = ql3xxx_get_stats;
	ndev->change_mtu = ql3xxx_change_mtu;
	ndev->set_multicast_list = ql3xxx_set_multicast_list;
	SET_ETHTOOL_OPS(ndev, &ql3xxx_ethtool_ops);
	ndev->set_mac_address = ql3xxx_set_mac_address;
	ndev->tx_timeout = ql3xxx_tx_timeout;
	ndev->watchdog_timeo = 5 * HZ;

	ndev->poll = &ql_poll;
	ndev->weight = 64;

	ndev->irq = pdev->irq;

	/* make sure the EEPROM is good */
	if (ql_get_nvram_params(qdev)) {
		printk(KERN_ALERT PFX
		       "ql3xxx_probe: Adapter #%d, Invalid NVRAM parameters.\n",
		       qdev->index);
		goto err_out_iounmap;
	}

	ql_set_mac_info(qdev);

	/* Validate and set parameters */
	if (qdev->mac_index) {
		memcpy(ndev->dev_addr, &qdev->nvram_data.funcCfg_fn2.macAddress,
		       ETH_ALEN);
	} else {
		memcpy(ndev->dev_addr, &qdev->nvram_data.funcCfg_fn0.macAddress,
		       ETH_ALEN);
	}
	memcpy(ndev->perm_addr, ndev->dev_addr, ndev->addr_len);

	ndev->tx_queue_len = NUM_REQ_Q_ENTRIES;

	/* Turn off support for multicasting */
	ndev->flags &= ~IFF_MULTICAST;

	/* Record PCI bus information. */
	ql_get_board_info(qdev);

	/*
	 * Set the Maximum Memory Read Byte Count value. We do this to handle
	 * jumbo frames.
	 */
	if (qdev->pci_x) {
		pci_write_config_word(pdev, (int)0x4e, (u16) 0x0036);
	}

	err = register_netdev(ndev);
	if (err) {
		printk(KERN_ERR PFX "%s: cannot register net device\n",
		       pci_name(pdev));
		goto err_out_iounmap;
	}

	/* we're going to reset, so assume we have no link for now */

	netif_carrier_off(ndev);
	netif_stop_queue(ndev);

	qdev->workqueue = create_singlethread_workqueue(ndev->name);
	INIT_WORK(&qdev->reset_work, (void (*)(void *))ql_reset_work, qdev);
	INIT_WORK(&qdev->tx_timeout_work,
		  (void (*)(void *))ql_tx_timeout_work, qdev);

	init_timer(&qdev->adapter_timer);
	qdev->adapter_timer.function = ql3xxx_timer;
	qdev->adapter_timer.expires = jiffies + HZ * 2;	/* two second delay */
	qdev->adapter_timer.data = (unsigned long)qdev;

	if(!cards_found) {
		printk(KERN_ALERT PFX "%s\n", DRV_STRING);
		printk(KERN_ALERT PFX "Driver name: %s, Version: %s.\n",
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
	pci_set_drvdata(pdev, NULL);
err_out:
	return err;
}

static void __devexit ql3xxx_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	struct ql3_adapter *qdev = netdev_priv(ndev);

	unregister_netdev(ndev);
	qdev = netdev_priv(ndev);

	ql_disable_interrupts(qdev);

	if (qdev->workqueue) {
		cancel_delayed_work(&qdev->reset_work);
		cancel_delayed_work(&qdev->tx_timeout_work);
		destroy_workqueue(qdev->workqueue);
		qdev->workqueue = NULL;
	}

	iounmap(qdev->mem_map_registers);
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	free_netdev(ndev);
}

static struct pci_driver ql3xxx_driver = {

	.name = DRV_NAME,
	.id_table = ql3xxx_pci_tbl,
	.probe = ql3xxx_probe,
	.remove = __devexit_p(ql3xxx_remove),
};

static int __init ql3xxx_init_module(void)
{
	return pci_register_driver(&ql3xxx_driver);
}

static void __exit ql3xxx_exit(void)
{
	pci_unregister_driver(&ql3xxx_driver);
}

module_init(ql3xxx_init_module);
module_exit(ql3xxx_exit);
