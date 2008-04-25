/*
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 - 2007 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 Jay Cliburn <jcliburn@gmail.com>
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 *
 * Contact Information:
 * Xiong Huang <xiong_huang@attansic.com>
 * Attansic Technology Corp. 3F 147, Xianzheng 9th Road, Zhubei,
 * Xinzhu  302, TAIWAN, REPUBLIC OF CHINA
 *
 * Chris Snook <csnook@redhat.com>
 * Jay Cliburn <jcliburn@gmail.com>
 *
 * This version is adapted from the Attansic reference driver for
 * inclusion in the Linux kernel.  It is currently under heavy development.
 * A very incomplete list of things that need to be dealt with:
 *
 * TODO:
 * Wake on LAN.
 * Add more ethtool functions.
 * Fix abstruse irq enable/disable condition described here:
 *	http://marc.theaimsgroup.com/?l=linux-netdev&m=116398508500553&w=2
 *
 * NEEDS TESTING:
 * VLAN
 * multicast
 * promiscuous mode
 * interrupt coalescing
 * SMP torture testing
 */

#include <asm/atomic.h>
#include <asm/byteorder.h>

#include <linux/compiler.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/hardirq.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/irqflags.h>
#include <linux/irqreturn.h>
#include <linux/jiffies.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/pm.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/tcp.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <net/checksum.h>

#include "atl1.h"

/* Temporary hack for merging atl1 and atl2 */
#include "atlx.c"

/*
 * This is the only thing that needs to be changed to adjust the
 * maximum number of ports that the driver can manage.
 */
#define ATL1_MAX_NIC 4

#define OPTION_UNSET    -1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

#define ATL1_PARAM_INIT { [0 ... ATL1_MAX_NIC] = OPTION_UNSET }

/*
 * Interrupt Moderate Timer in units of 2 us
 *
 * Valid Range: 10-65535
 *
 * Default Value: 100 (200us)
 */
static int __devinitdata int_mod_timer[ATL1_MAX_NIC+1] = ATL1_PARAM_INIT;
static int num_int_mod_timer;
module_param_array_named(int_mod_timer, int_mod_timer, int,
	&num_int_mod_timer, 0);
MODULE_PARM_DESC(int_mod_timer, "Interrupt moderator timer");

#define DEFAULT_INT_MOD_CNT	100	/* 200us */
#define MAX_INT_MOD_CNT		65000
#define MIN_INT_MOD_CNT		50

struct atl1_option {
	enum { enable_option, range_option, list_option } type;
	char *name;
	char *err;
	int def;
	union {
		struct {	/* range_option info */
			int min;
			int max;
		} r;
		struct {	/* list_option info */
			int nr;
			struct atl1_opt_list {
				int i;
				char *str;
			} *p;
		} l;
	} arg;
};

static int __devinit atl1_validate_option(int *value, struct atl1_option *opt,
	struct pci_dev *pdev)
{
	if (*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			dev_info(&pdev->dev, "%s enabled\n", opt->name);
			return 0;
		case OPTION_DISABLED:
			dev_info(&pdev->dev, "%s disabled\n", opt->name);
			return 0;
		}
		break;
	case range_option:
		if (*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			dev_info(&pdev->dev, "%s set to %i\n", opt->name,
				*value);
			return 0;
		}
		break;
	case list_option:{
			int i;
			struct atl1_opt_list *ent;

			for (i = 0; i < opt->arg.l.nr; i++) {
				ent = &opt->arg.l.p[i];
				if (*value == ent->i) {
					if (ent->str[0] != '\0')
						dev_info(&pdev->dev, "%s\n",
							ent->str);
					return 0;
				}
			}
		}
		break;

	default:
		break;
	}

	dev_info(&pdev->dev, "invalid %s specified (%i) %s\n",
		opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

/*
 * atl1_check_options - Range Checking for Command Line Parameters
 * @adapter: board private structure
 *
 * This routine checks all command line parameters for valid user
 * input.  If an invalid value is given, or if no user specified
 * value exists, a default value is used.  The final value is stored
 * in a variable in the adapter structure.
 */
void __devinit atl1_check_options(struct atl1_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int bd = adapter->bd_number;
	if (bd >= ATL1_MAX_NIC) {
		dev_notice(&pdev->dev, "no configuration for board#%i\n", bd);
		dev_notice(&pdev->dev, "using defaults for all values\n");
	}
	{			/* Interrupt Moderate Timer */
		struct atl1_option opt = {
			.type = range_option,
			.name = "Interrupt Moderator Timer",
			.err = "using default of "
				__MODULE_STRING(DEFAULT_INT_MOD_CNT),
			.def = DEFAULT_INT_MOD_CNT,
			.arg = {.r = {.min = MIN_INT_MOD_CNT,
					.max = MAX_INT_MOD_CNT} }
		};
		int val;
		if (num_int_mod_timer > bd) {
			val = int_mod_timer[bd];
			atl1_validate_option(&val, &opt, pdev);
			adapter->imt = (u16) val;
		} else
			adapter->imt = (u16) (opt.def);
	}
}

/*
 * atl1_pci_tbl - PCI Device ID Table
 */
static const struct pci_device_id atl1_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATTANSIC_L1)},
	/* required last entry */
	{0,}
};
MODULE_DEVICE_TABLE(pci, atl1_pci_tbl);

static const u32 atl1_default_msg = NETIF_MSG_DRV | NETIF_MSG_PROBE |
	NETIF_MSG_LINK | NETIF_MSG_TIMER | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP;

static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Message level (0=none,...,16=all)");

/*
 * Reset the transmit and receive units; mask and clear all interrupts.
 * hw - Struct containing variables accessed by shared code
 * return : 0  or  idle status (if error)
 */
static s32 atl1_reset_hw(struct atl1_hw *hw)
{
	struct pci_dev *pdev = hw->back->pdev;
	struct atl1_adapter *adapter = hw->back;
	u32 icr;
	int i;

	/*
	 * Clear Interrupt mask to stop board from generating
	 * interrupts & Clear any pending interrupt events
	 */
	/*
	 * iowrite32(0, hw->hw_addr + REG_IMR);
	 * iowrite32(0xffffffff, hw->hw_addr + REG_ISR);
	 */

	/*
	 * Issue Soft Reset to the MAC.  This will reset the chip's
	 * transmit, receive, DMA.  It will not effect
	 * the current PCI configuration.  The global reset bit is self-
	 * clearing, and should clear within a microsecond.
	 */
	iowrite32(MASTER_CTRL_SOFT_RST, hw->hw_addr + REG_MASTER_CTRL);
	ioread32(hw->hw_addr + REG_MASTER_CTRL);

	iowrite16(1, hw->hw_addr + REG_PHY_ENABLE);
	ioread16(hw->hw_addr + REG_PHY_ENABLE);

	/* delay about 1ms */
	msleep(1);

	/* Wait at least 10ms for All module to be Idle */
	for (i = 0; i < 10; i++) {
		icr = ioread32(hw->hw_addr + REG_IDLE_STATUS);
		if (!icr)
			break;
		/* delay 1 ms */
		msleep(1);
		/* FIXME: still the right way to do this? */
		cpu_relax();
	}

	if (icr) {
		if (netif_msg_hw(adapter))
			dev_dbg(&pdev->dev, "ICR = 0x%x\n", icr);
		return icr;
	}

	return 0;
}

/* function about EEPROM
 *
 * check_eeprom_exist
 * return 0 if eeprom exist
 */
static int atl1_check_eeprom_exist(struct atl1_hw *hw)
{
	u32 value;
	value = ioread32(hw->hw_addr + REG_SPI_FLASH_CTRL);
	if (value & SPI_FLASH_CTRL_EN_VPD) {
		value &= ~SPI_FLASH_CTRL_EN_VPD;
		iowrite32(value, hw->hw_addr + REG_SPI_FLASH_CTRL);
	}

	value = ioread16(hw->hw_addr + REG_PCIE_CAP_LIST);
	return ((value & 0xFF00) == 0x6C00) ? 0 : 1;
}

static bool atl1_read_eeprom(struct atl1_hw *hw, u32 offset, u32 *p_value)
{
	int i;
	u32 control;

	if (offset & 3)
		/* address do not align */
		return false;

	iowrite32(0, hw->hw_addr + REG_VPD_DATA);
	control = (offset & VPD_CAP_VPD_ADDR_MASK) << VPD_CAP_VPD_ADDR_SHIFT;
	iowrite32(control, hw->hw_addr + REG_VPD_CAP);
	ioread32(hw->hw_addr + REG_VPD_CAP);

	for (i = 0; i < 10; i++) {
		msleep(2);
		control = ioread32(hw->hw_addr + REG_VPD_CAP);
		if (control & VPD_CAP_VPD_FLAG)
			break;
	}
	if (control & VPD_CAP_VPD_FLAG) {
		*p_value = ioread32(hw->hw_addr + REG_VPD_DATA);
		return true;
	}
	/* timeout */
	return false;
}

/*
 * Reads the value from a PHY register
 * hw - Struct containing variables accessed by shared code
 * reg_addr - address of the PHY register to read
 */
s32 atl1_read_phy_reg(struct atl1_hw *hw, u16 reg_addr, u16 *phy_data)
{
	u32 val;
	int i;

	val = ((u32) (reg_addr & MDIO_REG_ADDR_MASK)) << MDIO_REG_ADDR_SHIFT |
		MDIO_START | MDIO_SUP_PREAMBLE | MDIO_RW | MDIO_CLK_25_4 <<
		MDIO_CLK_SEL_SHIFT;
	iowrite32(val, hw->hw_addr + REG_MDIO_CTRL);
	ioread32(hw->hw_addr + REG_MDIO_CTRL);

	for (i = 0; i < MDIO_WAIT_TIMES; i++) {
		udelay(2);
		val = ioread32(hw->hw_addr + REG_MDIO_CTRL);
		if (!(val & (MDIO_START | MDIO_BUSY)))
			break;
	}
	if (!(val & (MDIO_START | MDIO_BUSY))) {
		*phy_data = (u16) val;
		return 0;
	}
	return ATLX_ERR_PHY;
}

#define CUSTOM_SPI_CS_SETUP	2
#define CUSTOM_SPI_CLK_HI	2
#define CUSTOM_SPI_CLK_LO	2
#define CUSTOM_SPI_CS_HOLD	2
#define CUSTOM_SPI_CS_HI	3

static bool atl1_spi_read(struct atl1_hw *hw, u32 addr, u32 *buf)
{
	int i;
	u32 value;

	iowrite32(0, hw->hw_addr + REG_SPI_DATA);
	iowrite32(addr, hw->hw_addr + REG_SPI_ADDR);

	value = SPI_FLASH_CTRL_WAIT_READY |
	    (CUSTOM_SPI_CS_SETUP & SPI_FLASH_CTRL_CS_SETUP_MASK) <<
	    SPI_FLASH_CTRL_CS_SETUP_SHIFT | (CUSTOM_SPI_CLK_HI &
					     SPI_FLASH_CTRL_CLK_HI_MASK) <<
	    SPI_FLASH_CTRL_CLK_HI_SHIFT | (CUSTOM_SPI_CLK_LO &
					   SPI_FLASH_CTRL_CLK_LO_MASK) <<
	    SPI_FLASH_CTRL_CLK_LO_SHIFT | (CUSTOM_SPI_CS_HOLD &
					   SPI_FLASH_CTRL_CS_HOLD_MASK) <<
	    SPI_FLASH_CTRL_CS_HOLD_SHIFT | (CUSTOM_SPI_CS_HI &
					    SPI_FLASH_CTRL_CS_HI_MASK) <<
	    SPI_FLASH_CTRL_CS_HI_SHIFT | (1 & SPI_FLASH_CTRL_INS_MASK) <<
	    SPI_FLASH_CTRL_INS_SHIFT;

	iowrite32(value, hw->hw_addr + REG_SPI_FLASH_CTRL);

	value |= SPI_FLASH_CTRL_START;
	iowrite32(value, hw->hw_addr + REG_SPI_FLASH_CTRL);
	ioread32(hw->hw_addr + REG_SPI_FLASH_CTRL);

	for (i = 0; i < 10; i++) {
		msleep(1);
		value = ioread32(hw->hw_addr + REG_SPI_FLASH_CTRL);
		if (!(value & SPI_FLASH_CTRL_START))
			break;
	}

	if (value & SPI_FLASH_CTRL_START)
		return false;

	*buf = ioread32(hw->hw_addr + REG_SPI_DATA);

	return true;
}

/*
 * get_permanent_address
 * return 0 if get valid mac address,
 */
static int atl1_get_permanent_address(struct atl1_hw *hw)
{
	u32 addr[2];
	u32 i, control;
	u16 reg;
	u8 eth_addr[ETH_ALEN];
	bool key_valid;

	if (is_valid_ether_addr(hw->perm_mac_addr))
		return 0;

	/* init */
	addr[0] = addr[1] = 0;

	if (!atl1_check_eeprom_exist(hw)) {
		reg = 0;
		key_valid = false;
		/* Read out all EEPROM content */
		i = 0;
		while (1) {
			if (atl1_read_eeprom(hw, i + 0x100, &control)) {
				if (key_valid) {
					if (reg == REG_MAC_STA_ADDR)
						addr[0] = control;
					else if (reg == (REG_MAC_STA_ADDR + 4))
						addr[1] = control;
					key_valid = false;
				} else if ((control & 0xff) == 0x5A) {
					key_valid = true;
					reg = (u16) (control >> 16);
				} else
					break;
			} else
				/* read error */
				break;
			i += 4;
		}

		*(u32 *) &eth_addr[2] = swab32(addr[0]);
		*(u16 *) &eth_addr[0] = swab16(*(u16 *) &addr[1]);
		if (is_valid_ether_addr(eth_addr)) {
			memcpy(hw->perm_mac_addr, eth_addr, ETH_ALEN);
			return 0;
		}
		return 1;
	}

	/* see if SPI FLAGS exist ? */
	addr[0] = addr[1] = 0;
	reg = 0;
	key_valid = false;
	i = 0;
	while (1) {
		if (atl1_spi_read(hw, i + 0x1f000, &control)) {
			if (key_valid) {
				if (reg == REG_MAC_STA_ADDR)
					addr[0] = control;
				else if (reg == (REG_MAC_STA_ADDR + 4))
					addr[1] = control;
				key_valid = false;
			} else if ((control & 0xff) == 0x5A) {
				key_valid = true;
				reg = (u16) (control >> 16);
			} else
				/* data end */
				break;
		} else
			/* read error */
			break;
		i += 4;
	}

	*(u32 *) &eth_addr[2] = swab32(addr[0]);
	*(u16 *) &eth_addr[0] = swab16(*(u16 *) &addr[1]);
	if (is_valid_ether_addr(eth_addr)) {
		memcpy(hw->perm_mac_addr, eth_addr, ETH_ALEN);
		return 0;
	}

	/*
	 * On some motherboards, the MAC address is written by the
	 * BIOS directly to the MAC register during POST, and is
	 * not stored in eeprom.  If all else thus far has failed
	 * to fetch the permanent MAC address, try reading it directly.
	 */
	addr[0] = ioread32(hw->hw_addr + REG_MAC_STA_ADDR);
	addr[1] = ioread16(hw->hw_addr + (REG_MAC_STA_ADDR + 4));
	*(u32 *) &eth_addr[2] = swab32(addr[0]);
	*(u16 *) &eth_addr[0] = swab16(*(u16 *) &addr[1]);
	if (is_valid_ether_addr(eth_addr)) {
		memcpy(hw->perm_mac_addr, eth_addr, ETH_ALEN);
		return 0;
	}

	return 1;
}

/*
 * Reads the adapter's MAC address from the EEPROM
 * hw - Struct containing variables accessed by shared code
 */
s32 atl1_read_mac_addr(struct atl1_hw *hw)
{
	u16 i;

	if (atl1_get_permanent_address(hw))
		random_ether_addr(hw->perm_mac_addr);

	for (i = 0; i < ETH_ALEN; i++)
		hw->mac_addr[i] = hw->perm_mac_addr[i];
	return 0;
}

/*
 * Hashes an address to determine its location in the multicast table
 * hw - Struct containing variables accessed by shared code
 * mc_addr - the multicast address to hash
 *
 * atl1_hash_mc_addr
 *  purpose
 *      set hash value for a multicast address
 *      hash calcu processing :
 *          1. calcu 32bit CRC for multicast address
 *          2. reverse crc with MSB to LSB
 */
u32 atl1_hash_mc_addr(struct atl1_hw *hw, u8 *mc_addr)
{
	u32 crc32, value = 0;
	int i;

	crc32 = ether_crc_le(6, mc_addr);
	for (i = 0; i < 32; i++)
		value |= (((crc32 >> i) & 1) << (31 - i));

	return value;
}

/*
 * Sets the bit in the multicast table corresponding to the hash value.
 * hw - Struct containing variables accessed by shared code
 * hash_value - Multicast address hash value
 */
void atl1_hash_set(struct atl1_hw *hw, u32 hash_value)
{
	u32 hash_bit, hash_reg;
	u32 mta;

	/*
	 * The HASH Table  is a register array of 2 32-bit registers.
	 * It is treated like an array of 64 bits.  We want to set
	 * bit BitArray[hash_value]. So we figure out what register
	 * the bit is in, read it, OR in the new bit, then write
	 * back the new value.  The register is determined by the
	 * upper 7 bits of the hash value and the bit within that
	 * register are determined by the lower 5 bits of the value.
	 */
	hash_reg = (hash_value >> 31) & 0x1;
	hash_bit = (hash_value >> 26) & 0x1F;
	mta = ioread32((hw->hw_addr + REG_RX_HASH_TABLE) + (hash_reg << 2));
	mta |= (1 << hash_bit);
	iowrite32(mta, (hw->hw_addr + REG_RX_HASH_TABLE) + (hash_reg << 2));
}

/*
 * Writes a value to a PHY register
 * hw - Struct containing variables accessed by shared code
 * reg_addr - address of the PHY register to write
 * data - data to write to the PHY
 */
static s32 atl1_write_phy_reg(struct atl1_hw *hw, u32 reg_addr, u16 phy_data)
{
	int i;
	u32 val;

	val = ((u32) (phy_data & MDIO_DATA_MASK)) << MDIO_DATA_SHIFT |
	    (reg_addr & MDIO_REG_ADDR_MASK) << MDIO_REG_ADDR_SHIFT |
	    MDIO_SUP_PREAMBLE |
	    MDIO_START | MDIO_CLK_25_4 << MDIO_CLK_SEL_SHIFT;
	iowrite32(val, hw->hw_addr + REG_MDIO_CTRL);
	ioread32(hw->hw_addr + REG_MDIO_CTRL);

	for (i = 0; i < MDIO_WAIT_TIMES; i++) {
		udelay(2);
		val = ioread32(hw->hw_addr + REG_MDIO_CTRL);
		if (!(val & (MDIO_START | MDIO_BUSY)))
			break;
	}

	if (!(val & (MDIO_START | MDIO_BUSY)))
		return 0;

	return ATLX_ERR_PHY;
}

/*
 * Make L001's PHY out of Power Saving State (bug)
 * hw - Struct containing variables accessed by shared code
 * when power on, L001's PHY always on Power saving State
 * (Gigabit Link forbidden)
 */
static s32 atl1_phy_leave_power_saving(struct atl1_hw *hw)
{
	s32 ret;
	ret = atl1_write_phy_reg(hw, 29, 0x0029);
	if (ret)
		return ret;
	return atl1_write_phy_reg(hw, 30, 0);
}

/*
 *TODO: do something or get rid of this
 */
#ifdef CONFIG_PM
static s32 atl1_phy_enter_power_saving(struct atl1_hw *hw)
{
/*    s32 ret_val;
 *    u16 phy_data;
 */

/*
    ret_val = atl1_write_phy_reg(hw, ...);
    ret_val = atl1_write_phy_reg(hw, ...);
    ....
*/
	return 0;
}
#endif

/*
 * Resets the PHY and make all config validate
 * hw - Struct containing variables accessed by shared code
 *
 * Sets bit 15 and 12 of the MII Control regiser (for F001 bug)
 */
static s32 atl1_phy_reset(struct atl1_hw *hw)
{
	struct pci_dev *pdev = hw->back->pdev;
	struct atl1_adapter *adapter = hw->back;
	s32 ret_val;
	u16 phy_data;

	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL)
		phy_data = MII_CR_RESET | MII_CR_AUTO_NEG_EN;
	else {
		switch (hw->media_type) {
		case MEDIA_TYPE_100M_FULL:
			phy_data =
			    MII_CR_FULL_DUPLEX | MII_CR_SPEED_100 |
			    MII_CR_RESET;
			break;
		case MEDIA_TYPE_100M_HALF:
			phy_data = MII_CR_SPEED_100 | MII_CR_RESET;
			break;
		case MEDIA_TYPE_10M_FULL:
			phy_data =
			    MII_CR_FULL_DUPLEX | MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		default:
			/* MEDIA_TYPE_10M_HALF: */
			phy_data = MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		}
	}

	ret_val = atl1_write_phy_reg(hw, MII_BMCR, phy_data);
	if (ret_val) {
		u32 val;
		int i;
		/* pcie serdes link may be down! */
		if (netif_msg_hw(adapter))
			dev_dbg(&pdev->dev, "pcie phy link down\n");

		for (i = 0; i < 25; i++) {
			msleep(1);
			val = ioread32(hw->hw_addr + REG_MDIO_CTRL);
			if (!(val & (MDIO_START | MDIO_BUSY)))
				break;
		}

		if ((val & (MDIO_START | MDIO_BUSY)) != 0) {
			if (netif_msg_hw(adapter))
				dev_warn(&pdev->dev,
					"pcie link down at least 25ms\n");
			return ret_val;
		}
	}
	return 0;
}

/*
 * Configures PHY autoneg and flow control advertisement settings
 * hw - Struct containing variables accessed by shared code
 */
static s32 atl1_phy_setup_autoneg_adv(struct atl1_hw *hw)
{
	s32 ret_val;
	s16 mii_autoneg_adv_reg;
	s16 mii_1000t_ctrl_reg;

	/* Read the MII Auto-Neg Advertisement Register (Address 4). */
	mii_autoneg_adv_reg = MII_AR_DEFAULT_CAP_MASK;

	/* Read the MII 1000Base-T Control Register (Address 9). */
	mii_1000t_ctrl_reg = MII_ATLX_CR_1000T_DEFAULT_CAP_MASK;

	/*
	 * First we clear all the 10/100 mb speed bits in the Auto-Neg
	 * Advertisement Register (Address 4) and the 1000 mb speed bits in
	 * the  1000Base-T Control Register (Address 9).
	 */
	mii_autoneg_adv_reg &= ~MII_AR_SPEED_MASK;
	mii_1000t_ctrl_reg &= ~MII_ATLX_CR_1000T_SPEED_MASK;

	/*
	 * Need to parse media_type  and set up
	 * the appropriate PHY registers.
	 */
	switch (hw->media_type) {
	case MEDIA_TYPE_AUTO_SENSOR:
		mii_autoneg_adv_reg |= (MII_AR_10T_HD_CAPS |
					MII_AR_10T_FD_CAPS |
					MII_AR_100TX_HD_CAPS |
					MII_AR_100TX_FD_CAPS);
		mii_1000t_ctrl_reg |= MII_ATLX_CR_1000T_FD_CAPS;
		break;

	case MEDIA_TYPE_1000M_FULL:
		mii_1000t_ctrl_reg |= MII_ATLX_CR_1000T_FD_CAPS;
		break;

	case MEDIA_TYPE_100M_FULL:
		mii_autoneg_adv_reg |= MII_AR_100TX_FD_CAPS;
		break;

	case MEDIA_TYPE_100M_HALF:
		mii_autoneg_adv_reg |= MII_AR_100TX_HD_CAPS;
		break;

	case MEDIA_TYPE_10M_FULL:
		mii_autoneg_adv_reg |= MII_AR_10T_FD_CAPS;
		break;

	default:
		mii_autoneg_adv_reg |= MII_AR_10T_HD_CAPS;
		break;
	}

	/* flow control fixed to enable all */
	mii_autoneg_adv_reg |= (MII_AR_ASM_DIR | MII_AR_PAUSE);

	hw->mii_autoneg_adv_reg = mii_autoneg_adv_reg;
	hw->mii_1000t_ctrl_reg = mii_1000t_ctrl_reg;

	ret_val = atl1_write_phy_reg(hw, MII_ADVERTISE, mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	ret_val = atl1_write_phy_reg(hw, MII_ATLX_CR, mii_1000t_ctrl_reg);
	if (ret_val)
		return ret_val;

	return 0;
}

/*
 * Configures link settings.
 * hw - Struct containing variables accessed by shared code
 * Assumes the hardware has previously been reset and the
 * transmitter and receiver are not enabled.
 */
static s32 atl1_setup_link(struct atl1_hw *hw)
{
	struct pci_dev *pdev = hw->back->pdev;
	struct atl1_adapter *adapter = hw->back;
	s32 ret_val;

	/*
	 * Options:
	 *  PHY will advertise value(s) parsed from
	 *  autoneg_advertised and fc
	 *  no matter what autoneg is , We will not wait link result.
	 */
	ret_val = atl1_phy_setup_autoneg_adv(hw);
	if (ret_val) {
		if (netif_msg_link(adapter))
			dev_dbg(&pdev->dev,
				"error setting up autonegotiation\n");
		return ret_val;
	}
	/* SW.Reset , En-Auto-Neg if needed */
	ret_val = atl1_phy_reset(hw);
	if (ret_val) {
		if (netif_msg_link(adapter))
			dev_dbg(&pdev->dev, "error resetting phy\n");
		return ret_val;
	}
	hw->phy_configured = true;
	return ret_val;
}

static void atl1_init_flash_opcode(struct atl1_hw *hw)
{
	if (hw->flash_vendor >= ARRAY_SIZE(flash_table))
		/* Atmel */
		hw->flash_vendor = 0;

	/* Init OP table */
	iowrite8(flash_table[hw->flash_vendor].cmd_program,
		hw->hw_addr + REG_SPI_FLASH_OP_PROGRAM);
	iowrite8(flash_table[hw->flash_vendor].cmd_sector_erase,
		hw->hw_addr + REG_SPI_FLASH_OP_SC_ERASE);
	iowrite8(flash_table[hw->flash_vendor].cmd_chip_erase,
		hw->hw_addr + REG_SPI_FLASH_OP_CHIP_ERASE);
	iowrite8(flash_table[hw->flash_vendor].cmd_rdid,
		hw->hw_addr + REG_SPI_FLASH_OP_RDID);
	iowrite8(flash_table[hw->flash_vendor].cmd_wren,
		hw->hw_addr + REG_SPI_FLASH_OP_WREN);
	iowrite8(flash_table[hw->flash_vendor].cmd_rdsr,
		hw->hw_addr + REG_SPI_FLASH_OP_RDSR);
	iowrite8(flash_table[hw->flash_vendor].cmd_wrsr,
		hw->hw_addr + REG_SPI_FLASH_OP_WRSR);
	iowrite8(flash_table[hw->flash_vendor].cmd_read,
		hw->hw_addr + REG_SPI_FLASH_OP_READ);
}

/*
 * Performs basic configuration of the adapter.
 * hw - Struct containing variables accessed by shared code
 * Assumes that the controller has previously been reset and is in a
 * post-reset uninitialized state. Initializes multicast table,
 * and  Calls routines to setup link
 * Leaves the transmit and receive units disabled and uninitialized.
 */
static s32 atl1_init_hw(struct atl1_hw *hw)
{
	u32 ret_val = 0;

	/* Zero out the Multicast HASH table */
	iowrite32(0, hw->hw_addr + REG_RX_HASH_TABLE);
	/* clear the old settings from the multicast hash table */
	iowrite32(0, (hw->hw_addr + REG_RX_HASH_TABLE) + (1 << 2));

	atl1_init_flash_opcode(hw);

	if (!hw->phy_configured) {
		/* enable GPHY LinkChange Interrrupt */
		ret_val = atl1_write_phy_reg(hw, 18, 0xC00);
		if (ret_val)
			return ret_val;
		/* make PHY out of power-saving state */
		ret_val = atl1_phy_leave_power_saving(hw);
		if (ret_val)
			return ret_val;
		/* Call a subroutine to configure the link */
		ret_val = atl1_setup_link(hw);
	}
	return ret_val;
}

/*
 * Detects the current speed and duplex settings of the hardware.
 * hw - Struct containing variables accessed by shared code
 * speed - Speed of the connection
 * duplex - Duplex setting of the connection
 */
static s32 atl1_get_speed_and_duplex(struct atl1_hw *hw, u16 *speed, u16 *duplex)
{
	struct pci_dev *pdev = hw->back->pdev;
	struct atl1_adapter *adapter = hw->back;
	s32 ret_val;
	u16 phy_data;

	/* ; --- Read   PHY Specific Status Register (17) */
	ret_val = atl1_read_phy_reg(hw, MII_ATLX_PSSR, &phy_data);
	if (ret_val)
		return ret_val;

	if (!(phy_data & MII_ATLX_PSSR_SPD_DPLX_RESOLVED))
		return ATLX_ERR_PHY_RES;

	switch (phy_data & MII_ATLX_PSSR_SPEED) {
	case MII_ATLX_PSSR_1000MBS:
		*speed = SPEED_1000;
		break;
	case MII_ATLX_PSSR_100MBS:
		*speed = SPEED_100;
		break;
	case MII_ATLX_PSSR_10MBS:
		*speed = SPEED_10;
		break;
	default:
		if (netif_msg_hw(adapter))
			dev_dbg(&pdev->dev, "error getting speed\n");
		return ATLX_ERR_PHY_SPEED;
		break;
	}
	if (phy_data & MII_ATLX_PSSR_DPLX)
		*duplex = FULL_DUPLEX;
	else
		*duplex = HALF_DUPLEX;

	return 0;
}

void atl1_set_mac_addr(struct atl1_hw *hw)
{
	u32 value;
	/*
	 * 00-0B-6A-F6-00-DC
	 * 0:  6AF600DC   1: 000B
	 * low dword
	 */
	value = (((u32) hw->mac_addr[2]) << 24) |
	    (((u32) hw->mac_addr[3]) << 16) |
	    (((u32) hw->mac_addr[4]) << 8) | (((u32) hw->mac_addr[5]));
	iowrite32(value, hw->hw_addr + REG_MAC_STA_ADDR);
	/* high dword */
	value = (((u32) hw->mac_addr[0]) << 8) | (((u32) hw->mac_addr[1]));
	iowrite32(value, (hw->hw_addr + REG_MAC_STA_ADDR) + (1 << 2));
}

/*
 * atl1_sw_init - Initialize general software structures (struct atl1_adapter)
 * @adapter: board private structure to initialize
 *
 * atl1_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 */
static int __devinit atl1_sw_init(struct atl1_adapter *adapter)
{
	struct atl1_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;

	hw->max_frame_size = netdev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	hw->min_frame_size = ETH_ZLEN + ETH_FCS_LEN;

	adapter->wol = 0;
	adapter->rx_buffer_len = (hw->max_frame_size + 7) & ~7;
	adapter->ict = 50000;		/* 100ms */
	adapter->link_speed = SPEED_0;	/* hardware init */
	adapter->link_duplex = FULL_DUPLEX;

	hw->phy_configured = false;
	hw->preamble_len = 7;
	hw->ipgt = 0x60;
	hw->min_ifg = 0x50;
	hw->ipgr1 = 0x40;
	hw->ipgr2 = 0x60;
	hw->max_retry = 0xf;
	hw->lcol = 0x37;
	hw->jam_ipg = 7;
	hw->rfd_burst = 8;
	hw->rrd_burst = 8;
	hw->rfd_fetch_gap = 1;
	hw->rx_jumbo_th = adapter->rx_buffer_len / 8;
	hw->rx_jumbo_lkah = 1;
	hw->rrd_ret_timer = 16;
	hw->tpd_burst = 4;
	hw->tpd_fetch_th = 16;
	hw->txf_burst = 0x100;
	hw->tx_jumbo_task_th = (hw->max_frame_size + 7) >> 3;
	hw->tpd_fetch_gap = 1;
	hw->rcb_value = atl1_rcb_64;
	hw->dma_ord = atl1_dma_ord_enh;
	hw->dmar_block = atl1_dma_req_256;
	hw->dmaw_block = atl1_dma_req_256;
	hw->cmb_rrd = 4;
	hw->cmb_tpd = 4;
	hw->cmb_rx_timer = 1;	/* about 2us */
	hw->cmb_tx_timer = 1;	/* about 2us */
	hw->smb_timer = 100000;	/* about 200ms */

	spin_lock_init(&adapter->lock);
	spin_lock_init(&adapter->mb_lock);

	return 0;
}

static int mdio_read(struct net_device *netdev, int phy_id, int reg_num)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	u16 result;

	atl1_read_phy_reg(&adapter->hw, reg_num & 0x1f, &result);

	return result;
}

static void mdio_write(struct net_device *netdev, int phy_id, int reg_num,
	int val)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);

	atl1_write_phy_reg(&adapter->hw, reg_num, val);
}

/*
 * atl1_mii_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 */
static int atl1_mii_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;
	int retval;

	if (!netif_running(netdev))
		return -EINVAL;

	spin_lock_irqsave(&adapter->lock, flags);
	retval = generic_mii_ioctl(&adapter->mii, if_mii(ifr), cmd, NULL);
	spin_unlock_irqrestore(&adapter->lock, flags);

	return retval;
}

/*
 * atl1_setup_mem_resources - allocate Tx / RX descriptor resources
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 */
static s32 atl1_setup_ring_resources(struct atl1_adapter *adapter)
{
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1_ring_header *ring_header = &adapter->ring_header;
	struct pci_dev *pdev = adapter->pdev;
	int size;
	u8 offset = 0;

	size = sizeof(struct atl1_buffer) * (tpd_ring->count + rfd_ring->count);
	tpd_ring->buffer_info = kzalloc(size, GFP_KERNEL);
	if (unlikely(!tpd_ring->buffer_info)) {
		if (netif_msg_drv(adapter))
			dev_err(&pdev->dev, "kzalloc failed , size = D%d\n",
				size);
		goto err_nomem;
	}
	rfd_ring->buffer_info =
		(struct atl1_buffer *)(tpd_ring->buffer_info + tpd_ring->count);

	/*
	 * real ring DMA buffer
	 * each ring/block may need up to 8 bytes for alignment, hence the
	 * additional 40 bytes tacked onto the end.
	 */
	ring_header->size = size =
		sizeof(struct tx_packet_desc) * tpd_ring->count
		+ sizeof(struct rx_free_desc) * rfd_ring->count
		+ sizeof(struct rx_return_desc) * rrd_ring->count
		+ sizeof(struct coals_msg_block)
		+ sizeof(struct stats_msg_block)
		+ 40;

	ring_header->desc = pci_alloc_consistent(pdev, ring_header->size,
		&ring_header->dma);
	if (unlikely(!ring_header->desc)) {
		if (netif_msg_drv(adapter))
			dev_err(&pdev->dev, "pci_alloc_consistent failed\n");
		goto err_nomem;
	}

	memset(ring_header->desc, 0, ring_header->size);

	/* init TPD ring */
	tpd_ring->dma = ring_header->dma;
	offset = (tpd_ring->dma & 0x7) ? (8 - (ring_header->dma & 0x7)) : 0;
	tpd_ring->dma += offset;
	tpd_ring->desc = (u8 *) ring_header->desc + offset;
	tpd_ring->size = sizeof(struct tx_packet_desc) * tpd_ring->count;

	/* init RFD ring */
	rfd_ring->dma = tpd_ring->dma + tpd_ring->size;
	offset = (rfd_ring->dma & 0x7) ? (8 - (rfd_ring->dma & 0x7)) : 0;
	rfd_ring->dma += offset;
	rfd_ring->desc = (u8 *) tpd_ring->desc + (tpd_ring->size + offset);
	rfd_ring->size = sizeof(struct rx_free_desc) * rfd_ring->count;


	/* init RRD ring */
	rrd_ring->dma = rfd_ring->dma + rfd_ring->size;
	offset = (rrd_ring->dma & 0x7) ? (8 - (rrd_ring->dma & 0x7)) : 0;
	rrd_ring->dma += offset;
	rrd_ring->desc = (u8 *) rfd_ring->desc + (rfd_ring->size + offset);
	rrd_ring->size = sizeof(struct rx_return_desc) * rrd_ring->count;


	/* init CMB */
	adapter->cmb.dma = rrd_ring->dma + rrd_ring->size;
	offset = (adapter->cmb.dma & 0x7) ? (8 - (adapter->cmb.dma & 0x7)) : 0;
	adapter->cmb.dma += offset;
	adapter->cmb.cmb = (struct coals_msg_block *)
		((u8 *) rrd_ring->desc + (rrd_ring->size + offset));

	/* init SMB */
	adapter->smb.dma = adapter->cmb.dma + sizeof(struct coals_msg_block);
	offset = (adapter->smb.dma & 0x7) ? (8 - (adapter->smb.dma & 0x7)) : 0;
	adapter->smb.dma += offset;
	adapter->smb.smb = (struct stats_msg_block *)
		((u8 *) adapter->cmb.cmb +
		(sizeof(struct coals_msg_block) + offset));

	return 0;

err_nomem:
	kfree(tpd_ring->buffer_info);
	return -ENOMEM;
}

static void atl1_init_ring_ptrs(struct atl1_adapter *adapter)
{
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;

	atomic_set(&tpd_ring->next_to_use, 0);
	atomic_set(&tpd_ring->next_to_clean, 0);

	rfd_ring->next_to_clean = 0;
	atomic_set(&rfd_ring->next_to_use, 0);

	rrd_ring->next_to_use = 0;
	atomic_set(&rrd_ring->next_to_clean, 0);
}

/*
 * atl1_clean_rx_ring - Free RFD Buffers
 * @adapter: board private structure
 */
static void atl1_clean_rx_ring(struct atl1_adapter *adapter)
{
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rfd_ring->count; i++) {
		buffer_info = &rfd_ring->buffer_info[i];
		if (buffer_info->dma) {
			pci_unmap_page(pdev, buffer_info->dma,
				buffer_info->length, PCI_DMA_FROMDEVICE);
			buffer_info->dma = 0;
		}
		if (buffer_info->skb) {
			dev_kfree_skb(buffer_info->skb);
			buffer_info->skb = NULL;
		}
	}

	size = sizeof(struct atl1_buffer) * rfd_ring->count;
	memset(rfd_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rfd_ring->desc, 0, rfd_ring->size);

	rfd_ring->next_to_clean = 0;
	atomic_set(&rfd_ring->next_to_use, 0);

	rrd_ring->next_to_use = 0;
	atomic_set(&rrd_ring->next_to_clean, 0);
}

/*
 * atl1_clean_tx_ring - Free Tx Buffers
 * @adapter: board private structure
 */
static void atl1_clean_tx_ring(struct atl1_adapter *adapter)
{
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i;

	/* Free all the Tx ring sk_buffs */
	for (i = 0; i < tpd_ring->count; i++) {
		buffer_info = &tpd_ring->buffer_info[i];
		if (buffer_info->dma) {
			pci_unmap_page(pdev, buffer_info->dma,
				buffer_info->length, PCI_DMA_TODEVICE);
			buffer_info->dma = 0;
		}
	}

	for (i = 0; i < tpd_ring->count; i++) {
		buffer_info = &tpd_ring->buffer_info[i];
		if (buffer_info->skb) {
			dev_kfree_skb_any(buffer_info->skb);
			buffer_info->skb = NULL;
		}
	}

	size = sizeof(struct atl1_buffer) * tpd_ring->count;
	memset(tpd_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(tpd_ring->desc, 0, tpd_ring->size);

	atomic_set(&tpd_ring->next_to_use, 0);
	atomic_set(&tpd_ring->next_to_clean, 0);
}

/*
 * atl1_free_ring_resources - Free Tx / RX descriptor Resources
 * @adapter: board private structure
 *
 * Free all transmit software resources
 */
static void atl1_free_ring_resources(struct atl1_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1_ring_header *ring_header = &adapter->ring_header;

	atl1_clean_tx_ring(adapter);
	atl1_clean_rx_ring(adapter);

	kfree(tpd_ring->buffer_info);
	pci_free_consistent(pdev, ring_header->size, ring_header->desc,
		ring_header->dma);

	tpd_ring->buffer_info = NULL;
	tpd_ring->desc = NULL;
	tpd_ring->dma = 0;

	rfd_ring->buffer_info = NULL;
	rfd_ring->desc = NULL;
	rfd_ring->dma = 0;

	rrd_ring->desc = NULL;
	rrd_ring->dma = 0;
}

static void atl1_setup_mac_ctrl(struct atl1_adapter *adapter)
{
	u32 value;
	struct atl1_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	/* Config MAC CTRL Register */
	value = MAC_CTRL_TX_EN | MAC_CTRL_RX_EN;
	/* duplex */
	if (FULL_DUPLEX == adapter->link_duplex)
		value |= MAC_CTRL_DUPLX;
	/* speed */
	value |= ((u32) ((SPEED_1000 == adapter->link_speed) ?
			 MAC_CTRL_SPEED_1000 : MAC_CTRL_SPEED_10_100) <<
		  MAC_CTRL_SPEED_SHIFT);
	/* flow control */
	value |= (MAC_CTRL_TX_FLOW | MAC_CTRL_RX_FLOW);
	/* PAD & CRC */
	value |= (MAC_CTRL_ADD_CRC | MAC_CTRL_PAD);
	/* preamble length */
	value |= (((u32) adapter->hw.preamble_len
		   & MAC_CTRL_PRMLEN_MASK) << MAC_CTRL_PRMLEN_SHIFT);
	/* vlan */
	if (adapter->vlgrp)
		value |= MAC_CTRL_RMV_VLAN;
	/* rx checksum
	   if (adapter->rx_csum)
	   value |= MAC_CTRL_RX_CHKSUM_EN;
	 */
	/* filter mode */
	value |= MAC_CTRL_BC_EN;
	if (netdev->flags & IFF_PROMISC)
		value |= MAC_CTRL_PROMIS_EN;
	else if (netdev->flags & IFF_ALLMULTI)
		value |= MAC_CTRL_MC_ALL_EN;
	/* value |= MAC_CTRL_LOOPBACK; */
	iowrite32(value, hw->hw_addr + REG_MAC_CTRL);
}

static u32 atl1_check_link(struct atl1_adapter *adapter)
{
	struct atl1_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 ret_val;
	u16 speed, duplex, phy_data;
	int reconfig = 0;

	/* MII_BMSR must read twice */
	atl1_read_phy_reg(hw, MII_BMSR, &phy_data);
	atl1_read_phy_reg(hw, MII_BMSR, &phy_data);
	if (!(phy_data & BMSR_LSTATUS)) {
		/* link down */
		if (netif_carrier_ok(netdev)) {
			/* old link state: Up */
			if (netif_msg_link(adapter))
				dev_info(&adapter->pdev->dev, "link is down\n");
			adapter->link_speed = SPEED_0;
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
		return 0;
	}

	/* Link Up */
	ret_val = atl1_get_speed_and_duplex(hw, &speed, &duplex);
	if (ret_val)
		return ret_val;

	switch (hw->media_type) {
	case MEDIA_TYPE_1000M_FULL:
		if (speed != SPEED_1000 || duplex != FULL_DUPLEX)
			reconfig = 1;
		break;
	case MEDIA_TYPE_100M_FULL:
		if (speed != SPEED_100 || duplex != FULL_DUPLEX)
			reconfig = 1;
		break;
	case MEDIA_TYPE_100M_HALF:
		if (speed != SPEED_100 || duplex != HALF_DUPLEX)
			reconfig = 1;
		break;
	case MEDIA_TYPE_10M_FULL:
		if (speed != SPEED_10 || duplex != FULL_DUPLEX)
			reconfig = 1;
		break;
	case MEDIA_TYPE_10M_HALF:
		if (speed != SPEED_10 || duplex != HALF_DUPLEX)
			reconfig = 1;
		break;
	}

	/* link result is our setting */
	if (!reconfig) {
		if (adapter->link_speed != speed
		    || adapter->link_duplex != duplex) {
			adapter->link_speed = speed;
			adapter->link_duplex = duplex;
			atl1_setup_mac_ctrl(adapter);
			if (netif_msg_link(adapter))
				dev_info(&adapter->pdev->dev,
					"%s link is up %d Mbps %s\n",
					netdev->name, adapter->link_speed,
					adapter->link_duplex == FULL_DUPLEX ?
					"full duplex" : "half duplex");
		}
		if (!netif_carrier_ok(netdev)) {
			/* Link down -> Up */
			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
		}
		return 0;
	}

	/* change original link status */
	if (netif_carrier_ok(netdev)) {
		adapter->link_speed = SPEED_0;
		netif_carrier_off(netdev);
		netif_stop_queue(netdev);
	}

	if (hw->media_type != MEDIA_TYPE_AUTO_SENSOR &&
	    hw->media_type != MEDIA_TYPE_1000M_FULL) {
		switch (hw->media_type) {
		case MEDIA_TYPE_100M_FULL:
			phy_data = MII_CR_FULL_DUPLEX | MII_CR_SPEED_100 |
			           MII_CR_RESET;
			break;
		case MEDIA_TYPE_100M_HALF:
			phy_data = MII_CR_SPEED_100 | MII_CR_RESET;
			break;
		case MEDIA_TYPE_10M_FULL:
			phy_data =
			    MII_CR_FULL_DUPLEX | MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		default:
			/* MEDIA_TYPE_10M_HALF: */
			phy_data = MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		}
		atl1_write_phy_reg(hw, MII_BMCR, phy_data);
		return 0;
	}

	/* auto-neg, insert timer to re-config phy */
	if (!adapter->phy_timer_pending) {
		adapter->phy_timer_pending = true;
		mod_timer(&adapter->phy_config_timer, jiffies + 3 * HZ);
	}

	return 0;
}

static void set_flow_ctrl_old(struct atl1_adapter *adapter)
{
	u32 hi, lo, value;

	/* RFD Flow Control */
	value = adapter->rfd_ring.count;
	hi = value / 16;
	if (hi < 2)
		hi = 2;
	lo = value * 7 / 8;

	value = ((hi & RXQ_RXF_PAUSE_TH_HI_MASK) << RXQ_RXF_PAUSE_TH_HI_SHIFT) |
		((lo & RXQ_RXF_PAUSE_TH_LO_MASK) << RXQ_RXF_PAUSE_TH_LO_SHIFT);
	iowrite32(value, adapter->hw.hw_addr + REG_RXQ_RXF_PAUSE_THRESH);

	/* RRD Flow Control */
	value = adapter->rrd_ring.count;
	lo = value / 16;
	hi = value * 7 / 8;
	if (lo < 2)
		lo = 2;
	value = ((hi & RXQ_RRD_PAUSE_TH_HI_MASK) << RXQ_RRD_PAUSE_TH_HI_SHIFT) |
		((lo & RXQ_RRD_PAUSE_TH_LO_MASK) << RXQ_RRD_PAUSE_TH_LO_SHIFT);
	iowrite32(value, adapter->hw.hw_addr + REG_RXQ_RRD_PAUSE_THRESH);
}

static void set_flow_ctrl_new(struct atl1_hw *hw)
{
	u32 hi, lo, value;

	/* RXF Flow Control */
	value = ioread32(hw->hw_addr + REG_SRAM_RXF_LEN);
	lo = value / 16;
	if (lo < 192)
		lo = 192;
	hi = value * 7 / 8;
	if (hi < lo)
		hi = lo + 16;
	value = ((hi & RXQ_RXF_PAUSE_TH_HI_MASK) << RXQ_RXF_PAUSE_TH_HI_SHIFT) |
		((lo & RXQ_RXF_PAUSE_TH_LO_MASK) << RXQ_RXF_PAUSE_TH_LO_SHIFT);
	iowrite32(value, hw->hw_addr + REG_RXQ_RXF_PAUSE_THRESH);

	/* RRD Flow Control */
	value = ioread32(hw->hw_addr + REG_SRAM_RRD_LEN);
	lo = value / 8;
	hi = value * 7 / 8;
	if (lo < 2)
		lo = 2;
	if (hi < lo)
		hi = lo + 3;
	value = ((hi & RXQ_RRD_PAUSE_TH_HI_MASK) << RXQ_RRD_PAUSE_TH_HI_SHIFT) |
		((lo & RXQ_RRD_PAUSE_TH_LO_MASK) << RXQ_RRD_PAUSE_TH_LO_SHIFT);
	iowrite32(value, hw->hw_addr + REG_RXQ_RRD_PAUSE_THRESH);
}

/*
 * atl1_configure - Configure Transmit&Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx /Rx unit of the MAC after a reset.
 */
static u32 atl1_configure(struct atl1_adapter *adapter)
{
	struct atl1_hw *hw = &adapter->hw;
	u32 value;

	/* clear interrupt status */
	iowrite32(0xffffffff, adapter->hw.hw_addr + REG_ISR);

	/* set MAC Address */
	value = (((u32) hw->mac_addr[2]) << 24) |
		(((u32) hw->mac_addr[3]) << 16) |
		(((u32) hw->mac_addr[4]) << 8) |
		(((u32) hw->mac_addr[5]));
	iowrite32(value, hw->hw_addr + REG_MAC_STA_ADDR);
	value = (((u32) hw->mac_addr[0]) << 8) | (((u32) hw->mac_addr[1]));
	iowrite32(value, hw->hw_addr + (REG_MAC_STA_ADDR + 4));

	/* tx / rx ring */

	/* HI base address */
	iowrite32((u32) ((adapter->tpd_ring.dma & 0xffffffff00000000ULL) >> 32),
		hw->hw_addr + REG_DESC_BASE_ADDR_HI);
	/* LO base address */
	iowrite32((u32) (adapter->rfd_ring.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_RFD_ADDR_LO);
	iowrite32((u32) (adapter->rrd_ring.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_RRD_ADDR_LO);
	iowrite32((u32) (adapter->tpd_ring.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_TPD_ADDR_LO);
	iowrite32((u32) (adapter->cmb.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_CMB_ADDR_LO);
	iowrite32((u32) (adapter->smb.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_SMB_ADDR_LO);

	/* element count */
	value = adapter->rrd_ring.count;
	value <<= 16;
	value += adapter->rfd_ring.count;
	iowrite32(value, hw->hw_addr + REG_DESC_RFD_RRD_RING_SIZE);
	iowrite32(adapter->tpd_ring.count, hw->hw_addr +
		REG_DESC_TPD_RING_SIZE);

	/* Load Ptr */
	iowrite32(1, hw->hw_addr + REG_LOAD_PTR);

	/* config Mailbox */
	value = ((atomic_read(&adapter->tpd_ring.next_to_use)
		  & MB_TPD_PROD_INDX_MASK) << MB_TPD_PROD_INDX_SHIFT) |
		((atomic_read(&adapter->rrd_ring.next_to_clean)
		& MB_RRD_CONS_INDX_MASK) << MB_RRD_CONS_INDX_SHIFT) |
		((atomic_read(&adapter->rfd_ring.next_to_use)
		& MB_RFD_PROD_INDX_MASK) << MB_RFD_PROD_INDX_SHIFT);
	iowrite32(value, hw->hw_addr + REG_MAILBOX);

	/* config IPG/IFG */
	value = (((u32) hw->ipgt & MAC_IPG_IFG_IPGT_MASK)
		 << MAC_IPG_IFG_IPGT_SHIFT) |
		(((u32) hw->min_ifg & MAC_IPG_IFG_MIFG_MASK)
		<< MAC_IPG_IFG_MIFG_SHIFT) |
		(((u32) hw->ipgr1 & MAC_IPG_IFG_IPGR1_MASK)
		<< MAC_IPG_IFG_IPGR1_SHIFT) |
		(((u32) hw->ipgr2 & MAC_IPG_IFG_IPGR2_MASK)
		<< MAC_IPG_IFG_IPGR2_SHIFT);
	iowrite32(value, hw->hw_addr + REG_MAC_IPG_IFG);

	/* config  Half-Duplex Control */
	value = ((u32) hw->lcol & MAC_HALF_DUPLX_CTRL_LCOL_MASK) |
		(((u32) hw->max_retry & MAC_HALF_DUPLX_CTRL_RETRY_MASK)
		<< MAC_HALF_DUPLX_CTRL_RETRY_SHIFT) |
		MAC_HALF_DUPLX_CTRL_EXC_DEF_EN |
		(0xa << MAC_HALF_DUPLX_CTRL_ABEBT_SHIFT) |
		(((u32) hw->jam_ipg & MAC_HALF_DUPLX_CTRL_JAMIPG_MASK)
		<< MAC_HALF_DUPLX_CTRL_JAMIPG_SHIFT);
	iowrite32(value, hw->hw_addr + REG_MAC_HALF_DUPLX_CTRL);

	/* set Interrupt Moderator Timer */
	iowrite16(adapter->imt, hw->hw_addr + REG_IRQ_MODU_TIMER_INIT);
	iowrite32(MASTER_CTRL_ITIMER_EN, hw->hw_addr + REG_MASTER_CTRL);

	/* set Interrupt Clear Timer */
	iowrite16(adapter->ict, hw->hw_addr + REG_CMBDISDMA_TIMER);

	/* set max frame size hw will accept */
	iowrite32(hw->max_frame_size, hw->hw_addr + REG_MTU);

	/* jumbo size & rrd retirement timer */
	value = (((u32) hw->rx_jumbo_th & RXQ_JMBOSZ_TH_MASK)
		 << RXQ_JMBOSZ_TH_SHIFT) |
		(((u32) hw->rx_jumbo_lkah & RXQ_JMBO_LKAH_MASK)
		<< RXQ_JMBO_LKAH_SHIFT) |
		(((u32) hw->rrd_ret_timer & RXQ_RRD_TIMER_MASK)
		<< RXQ_RRD_TIMER_SHIFT);
	iowrite32(value, hw->hw_addr + REG_RXQ_JMBOSZ_RRDTIM);

	/* Flow Control */
	switch (hw->dev_rev) {
	case 0x8001:
	case 0x9001:
	case 0x9002:
	case 0x9003:
		set_flow_ctrl_old(adapter);
		break;
	default:
		set_flow_ctrl_new(hw);
		break;
	}

	/* config TXQ */
	value = (((u32) hw->tpd_burst & TXQ_CTRL_TPD_BURST_NUM_MASK)
		 << TXQ_CTRL_TPD_BURST_NUM_SHIFT) |
		(((u32) hw->txf_burst & TXQ_CTRL_TXF_BURST_NUM_MASK)
		<< TXQ_CTRL_TXF_BURST_NUM_SHIFT) |
		(((u32) hw->tpd_fetch_th & TXQ_CTRL_TPD_FETCH_TH_MASK)
		<< TXQ_CTRL_TPD_FETCH_TH_SHIFT) | TXQ_CTRL_ENH_MODE |
		TXQ_CTRL_EN;
	iowrite32(value, hw->hw_addr + REG_TXQ_CTRL);

	/* min tpd fetch gap & tx jumbo packet size threshold for taskoffload */
	value = (((u32) hw->tx_jumbo_task_th & TX_JUMBO_TASK_TH_MASK)
		<< TX_JUMBO_TASK_TH_SHIFT) |
		(((u32) hw->tpd_fetch_gap & TX_TPD_MIN_IPG_MASK)
		<< TX_TPD_MIN_IPG_SHIFT);
	iowrite32(value, hw->hw_addr + REG_TX_JUMBO_TASK_TH_TPD_IPG);

	/* config RXQ */
	value = (((u32) hw->rfd_burst & RXQ_CTRL_RFD_BURST_NUM_MASK)
		<< RXQ_CTRL_RFD_BURST_NUM_SHIFT) |
		(((u32) hw->rrd_burst & RXQ_CTRL_RRD_BURST_THRESH_MASK)
		<< RXQ_CTRL_RRD_BURST_THRESH_SHIFT) |
		(((u32) hw->rfd_fetch_gap & RXQ_CTRL_RFD_PREF_MIN_IPG_MASK)
		<< RXQ_CTRL_RFD_PREF_MIN_IPG_SHIFT) | RXQ_CTRL_CUT_THRU_EN |
		RXQ_CTRL_EN;
	iowrite32(value, hw->hw_addr + REG_RXQ_CTRL);

	/* config DMA Engine */
	value = ((((u32) hw->dmar_block) & DMA_CTRL_DMAR_BURST_LEN_MASK)
		<< DMA_CTRL_DMAR_BURST_LEN_SHIFT) |
		((((u32) hw->dmaw_block) & DMA_CTRL_DMAW_BURST_LEN_MASK)
		<< DMA_CTRL_DMAW_BURST_LEN_SHIFT) | DMA_CTRL_DMAR_EN |
		DMA_CTRL_DMAW_EN;
	value |= (u32) hw->dma_ord;
	if (atl1_rcb_128 == hw->rcb_value)
		value |= DMA_CTRL_RCB_VALUE;
	iowrite32(value, hw->hw_addr + REG_DMA_CTRL);

	/* config CMB / SMB */
	value = (hw->cmb_tpd > adapter->tpd_ring.count) ?
		hw->cmb_tpd : adapter->tpd_ring.count;
	value <<= 16;
	value |= hw->cmb_rrd;
	iowrite32(value, hw->hw_addr + REG_CMB_WRITE_TH);
	value = hw->cmb_rx_timer | ((u32) hw->cmb_tx_timer << 16);
	iowrite32(value, hw->hw_addr + REG_CMB_WRITE_TIMER);
	iowrite32(hw->smb_timer, hw->hw_addr + REG_SMB_TIMER);

	/* --- enable CMB / SMB */
	value = CSMB_CTRL_CMB_EN | CSMB_CTRL_SMB_EN;
	iowrite32(value, hw->hw_addr + REG_CSMB_CTRL);

	value = ioread32(adapter->hw.hw_addr + REG_ISR);
	if (unlikely((value & ISR_PHY_LINKDOWN) != 0))
		value = 1;	/* config failed */
	else
		value = 0;

	/* clear all interrupt status */
	iowrite32(0x3fffffff, adapter->hw.hw_addr + REG_ISR);
	iowrite32(0, adapter->hw.hw_addr + REG_ISR);
	return value;
}

/*
 * atl1_pcie_patch - Patch for PCIE module
 */
static void atl1_pcie_patch(struct atl1_adapter *adapter)
{
	u32 value;

	/* much vendor magic here */
	value = 0x6500;
	iowrite32(value, adapter->hw.hw_addr + 0x12FC);
	/* pcie flow control mode change */
	value = ioread32(adapter->hw.hw_addr + 0x1008);
	value |= 0x8000;
	iowrite32(value, adapter->hw.hw_addr + 0x1008);
}

/*
 * When ACPI resume on some VIA MotherBoard, the Interrupt Disable bit/0x400
 * on PCI Command register is disable.
 * The function enable this bit.
 * Brackett, 2006/03/15
 */
static void atl1_via_workaround(struct atl1_adapter *adapter)
{
	unsigned long value;

	value = ioread16(adapter->hw.hw_addr + PCI_COMMAND);
	if (value & PCI_COMMAND_INTX_DISABLE)
		value &= ~PCI_COMMAND_INTX_DISABLE;
	iowrite32(value, adapter->hw.hw_addr + PCI_COMMAND);
}

static void atl1_inc_smb(struct atl1_adapter *adapter)
{
	struct stats_msg_block *smb = adapter->smb.smb;

	/* Fill out the OS statistics structure */
	adapter->soft_stats.rx_packets += smb->rx_ok;
	adapter->soft_stats.tx_packets += smb->tx_ok;
	adapter->soft_stats.rx_bytes += smb->rx_byte_cnt;
	adapter->soft_stats.tx_bytes += smb->tx_byte_cnt;
	adapter->soft_stats.multicast += smb->rx_mcast;
	adapter->soft_stats.collisions += (smb->tx_1_col + smb->tx_2_col * 2 +
		smb->tx_late_col + smb->tx_abort_col * adapter->hw.max_retry);

	/* Rx Errors */
	adapter->soft_stats.rx_errors += (smb->rx_frag + smb->rx_fcs_err +
		smb->rx_len_err + smb->rx_sz_ov + smb->rx_rxf_ov +
		smb->rx_rrd_ov + smb->rx_align_err);
	adapter->soft_stats.rx_fifo_errors += smb->rx_rxf_ov;
	adapter->soft_stats.rx_length_errors += smb->rx_len_err;
	adapter->soft_stats.rx_crc_errors += smb->rx_fcs_err;
	adapter->soft_stats.rx_frame_errors += smb->rx_align_err;
	adapter->soft_stats.rx_missed_errors += (smb->rx_rrd_ov +
		smb->rx_rxf_ov);

	adapter->soft_stats.rx_pause += smb->rx_pause;
	adapter->soft_stats.rx_rrd_ov += smb->rx_rrd_ov;
	adapter->soft_stats.rx_trunc += smb->rx_sz_ov;

	/* Tx Errors */
	adapter->soft_stats.tx_errors += (smb->tx_late_col +
		smb->tx_abort_col + smb->tx_underrun + smb->tx_trunc);
	adapter->soft_stats.tx_fifo_errors += smb->tx_underrun;
	adapter->soft_stats.tx_aborted_errors += smb->tx_abort_col;
	adapter->soft_stats.tx_window_errors += smb->tx_late_col;

	adapter->soft_stats.excecol += smb->tx_abort_col;
	adapter->soft_stats.deffer += smb->tx_defer;
	adapter->soft_stats.scc += smb->tx_1_col;
	adapter->soft_stats.mcc += smb->tx_2_col;
	adapter->soft_stats.latecol += smb->tx_late_col;
	adapter->soft_stats.tx_underun += smb->tx_underrun;
	adapter->soft_stats.tx_trunc += smb->tx_trunc;
	adapter->soft_stats.tx_pause += smb->tx_pause;

	adapter->net_stats.rx_packets = adapter->soft_stats.rx_packets;
	adapter->net_stats.tx_packets = adapter->soft_stats.tx_packets;
	adapter->net_stats.rx_bytes = adapter->soft_stats.rx_bytes;
	adapter->net_stats.tx_bytes = adapter->soft_stats.tx_bytes;
	adapter->net_stats.multicast = adapter->soft_stats.multicast;
	adapter->net_stats.collisions = adapter->soft_stats.collisions;
	adapter->net_stats.rx_errors = adapter->soft_stats.rx_errors;
	adapter->net_stats.rx_over_errors =
		adapter->soft_stats.rx_missed_errors;
	adapter->net_stats.rx_length_errors =
		adapter->soft_stats.rx_length_errors;
	adapter->net_stats.rx_crc_errors = adapter->soft_stats.rx_crc_errors;
	adapter->net_stats.rx_frame_errors =
		adapter->soft_stats.rx_frame_errors;
	adapter->net_stats.rx_fifo_errors = adapter->soft_stats.rx_fifo_errors;
	adapter->net_stats.rx_missed_errors =
		adapter->soft_stats.rx_missed_errors;
	adapter->net_stats.tx_errors = adapter->soft_stats.tx_errors;
	adapter->net_stats.tx_fifo_errors = adapter->soft_stats.tx_fifo_errors;
	adapter->net_stats.tx_aborted_errors =
		adapter->soft_stats.tx_aborted_errors;
	adapter->net_stats.tx_window_errors =
		adapter->soft_stats.tx_window_errors;
	adapter->net_stats.tx_carrier_errors =
		adapter->soft_stats.tx_carrier_errors;
}

static void atl1_update_mailbox(struct atl1_adapter *adapter)
{
	unsigned long flags;
	u32 tpd_next_to_use;
	u32 rfd_next_to_use;
	u32 rrd_next_to_clean;
	u32 value;

	spin_lock_irqsave(&adapter->mb_lock, flags);

	tpd_next_to_use = atomic_read(&adapter->tpd_ring.next_to_use);
	rfd_next_to_use = atomic_read(&adapter->rfd_ring.next_to_use);
	rrd_next_to_clean = atomic_read(&adapter->rrd_ring.next_to_clean);

	value = ((rfd_next_to_use & MB_RFD_PROD_INDX_MASK) <<
		MB_RFD_PROD_INDX_SHIFT) |
		((rrd_next_to_clean & MB_RRD_CONS_INDX_MASK) <<
		MB_RRD_CONS_INDX_SHIFT) |
		((tpd_next_to_use & MB_TPD_PROD_INDX_MASK) <<
		MB_TPD_PROD_INDX_SHIFT);
	iowrite32(value, adapter->hw.hw_addr + REG_MAILBOX);

	spin_unlock_irqrestore(&adapter->mb_lock, flags);
}

static void atl1_clean_alloc_flag(struct atl1_adapter *adapter,
	struct rx_return_desc *rrd, u16 offset)
{
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;

	while (rfd_ring->next_to_clean != (rrd->buf_indx + offset)) {
		rfd_ring->buffer_info[rfd_ring->next_to_clean].alloced = 0;
		if (++rfd_ring->next_to_clean == rfd_ring->count) {
			rfd_ring->next_to_clean = 0;
		}
	}
}

static void atl1_update_rfd_index(struct atl1_adapter *adapter,
	struct rx_return_desc *rrd)
{
	u16 num_buf;

	num_buf = (rrd->xsz.xsum_sz.pkt_size + adapter->rx_buffer_len - 1) /
		adapter->rx_buffer_len;
	if (rrd->num_buf == num_buf)
		/* clean alloc flag for bad rrd */
		atl1_clean_alloc_flag(adapter, rrd, num_buf);
}

static void atl1_rx_checksum(struct atl1_adapter *adapter,
	struct rx_return_desc *rrd, struct sk_buff *skb)
{
	struct pci_dev *pdev = adapter->pdev;

	skb->ip_summed = CHECKSUM_NONE;

	if (unlikely(rrd->pkt_flg & PACKET_FLAG_ERR)) {
		if (rrd->err_flg & (ERR_FLAG_CRC | ERR_FLAG_TRUNC |
					ERR_FLAG_CODE | ERR_FLAG_OV)) {
			adapter->hw_csum_err++;
			if (netif_msg_rx_err(adapter))
				dev_printk(KERN_DEBUG, &pdev->dev,
					"rx checksum error\n");
			return;
		}
	}

	/* not IPv4 */
	if (!(rrd->pkt_flg & PACKET_FLAG_IPV4))
		/* checksum is invalid, but it's not an IPv4 pkt, so ok */
		return;

	/* IPv4 packet */
	if (likely(!(rrd->err_flg &
		(ERR_FLAG_IP_CHKSUM | ERR_FLAG_L4_CHKSUM)))) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		adapter->hw_csum_good++;
		return;
	}

	/* IPv4, but hardware thinks its checksum is wrong */
	if (netif_msg_rx_err(adapter))
		dev_printk(KERN_DEBUG, &pdev->dev,
			"hw csum wrong, pkt_flag:%x, err_flag:%x\n",
			rrd->pkt_flg, rrd->err_flg);
	skb->ip_summed = CHECKSUM_COMPLETE;
	skb->csum = htons(rrd->xsz.xsum_sz.rx_chksum);
	adapter->hw_csum_err++;
	return;
}

/*
 * atl1_alloc_rx_buffers - Replace used receive buffers
 * @adapter: address of board private structure
 */
static u16 atl1_alloc_rx_buffers(struct atl1_adapter *adapter)
{
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct pci_dev *pdev = adapter->pdev;
	struct page *page;
	unsigned long offset;
	struct atl1_buffer *buffer_info, *next_info;
	struct sk_buff *skb;
	u16 num_alloc = 0;
	u16 rfd_next_to_use, next_next;
	struct rx_free_desc *rfd_desc;

	next_next = rfd_next_to_use = atomic_read(&rfd_ring->next_to_use);
	if (++next_next == rfd_ring->count)
		next_next = 0;
	buffer_info = &rfd_ring->buffer_info[rfd_next_to_use];
	next_info = &rfd_ring->buffer_info[next_next];

	while (!buffer_info->alloced && !next_info->alloced) {
		if (buffer_info->skb) {
			buffer_info->alloced = 1;
			goto next;
		}

		rfd_desc = ATL1_RFD_DESC(rfd_ring, rfd_next_to_use);

		skb = dev_alloc_skb(adapter->rx_buffer_len + NET_IP_ALIGN);
		if (unlikely(!skb)) {
			/* Better luck next round */
			adapter->net_stats.rx_dropped++;
			break;
		}

		/*
		 * Make buffer alignment 2 beyond a 16 byte boundary
		 * this will result in a 16 byte aligned IP header after
		 * the 14 byte MAC header is removed
		 */
		skb_reserve(skb, NET_IP_ALIGN);

		buffer_info->alloced = 1;
		buffer_info->skb = skb;
		buffer_info->length = (u16) adapter->rx_buffer_len;
		page = virt_to_page(skb->data);
		offset = (unsigned long)skb->data & ~PAGE_MASK;
		buffer_info->dma = pci_map_page(pdev, page, offset,
						adapter->rx_buffer_len,
						PCI_DMA_FROMDEVICE);
		rfd_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
		rfd_desc->buf_len = cpu_to_le16(adapter->rx_buffer_len);
		rfd_desc->coalese = 0;

next:
		rfd_next_to_use = next_next;
		if (unlikely(++next_next == rfd_ring->count))
			next_next = 0;

		buffer_info = &rfd_ring->buffer_info[rfd_next_to_use];
		next_info = &rfd_ring->buffer_info[next_next];
		num_alloc++;
	}

	if (num_alloc) {
		/*
		 * Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		atomic_set(&rfd_ring->next_to_use, (int)rfd_next_to_use);
	}
	return num_alloc;
}

static void atl1_intr_rx(struct atl1_adapter *adapter)
{
	int i, count;
	u16 length;
	u16 rrd_next_to_clean;
	u32 value;
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1_buffer *buffer_info;
	struct rx_return_desc *rrd;
	struct sk_buff *skb;

	count = 0;

	rrd_next_to_clean = atomic_read(&rrd_ring->next_to_clean);

	while (1) {
		rrd = ATL1_RRD_DESC(rrd_ring, rrd_next_to_clean);
		i = 1;
		if (likely(rrd->xsz.valid)) {	/* packet valid */
chk_rrd:
			/* check rrd status */
			if (likely(rrd->num_buf == 1))
				goto rrd_ok;
			else if (netif_msg_rx_err(adapter)) {
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"unexpected RRD buffer count\n");
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"rx_buf_len = %d\n",
					adapter->rx_buffer_len);
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"RRD num_buf = %d\n",
					rrd->num_buf);
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"RRD pkt_len = %d\n",
					rrd->xsz.xsum_sz.pkt_size);
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"RRD pkt_flg = 0x%08X\n",
					rrd->pkt_flg);
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"RRD err_flg = 0x%08X\n",
					rrd->err_flg);
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"RRD vlan_tag = 0x%08X\n",
					rrd->vlan_tag);
			}

			/* rrd seems to be bad */
			if (unlikely(i-- > 0)) {
				/* rrd may not be DMAed completely */
				udelay(1);
				goto chk_rrd;
			}
			/* bad rrd */
			if (netif_msg_rx_err(adapter))
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"bad RRD\n");
			/* see if update RFD index */
			if (rrd->num_buf > 1)
				atl1_update_rfd_index(adapter, rrd);

			/* update rrd */
			rrd->xsz.valid = 0;
			if (++rrd_next_to_clean == rrd_ring->count)
				rrd_next_to_clean = 0;
			count++;
			continue;
		} else {	/* current rrd still not be updated */

			break;
		}
rrd_ok:
		/* clean alloc flag for bad rrd */
		atl1_clean_alloc_flag(adapter, rrd, 0);

		buffer_info = &rfd_ring->buffer_info[rrd->buf_indx];
		if (++rfd_ring->next_to_clean == rfd_ring->count)
			rfd_ring->next_to_clean = 0;

		/* update rrd next to clean */
		if (++rrd_next_to_clean == rrd_ring->count)
			rrd_next_to_clean = 0;
		count++;

		if (unlikely(rrd->pkt_flg & PACKET_FLAG_ERR)) {
			if (!(rrd->err_flg &
				(ERR_FLAG_IP_CHKSUM | ERR_FLAG_L4_CHKSUM
				| ERR_FLAG_LEN))) {
				/* packet error, don't need upstream */
				buffer_info->alloced = 0;
				rrd->xsz.valid = 0;
				continue;
			}
		}

		/* Good Receive */
		pci_unmap_page(adapter->pdev, buffer_info->dma,
			       buffer_info->length, PCI_DMA_FROMDEVICE);
		skb = buffer_info->skb;
		length = le16_to_cpu(rrd->xsz.xsum_sz.pkt_size);

		skb_put(skb, length - ETH_FCS_LEN);

		/* Receive Checksum Offload */
		atl1_rx_checksum(adapter, rrd, skb);
		skb->protocol = eth_type_trans(skb, adapter->netdev);

		if (adapter->vlgrp && (rrd->pkt_flg & PACKET_FLAG_VLAN_INS)) {
			u16 vlan_tag = (rrd->vlan_tag >> 4) |
					((rrd->vlan_tag & 7) << 13) |
					((rrd->vlan_tag & 8) << 9);
			vlan_hwaccel_rx(skb, adapter->vlgrp, vlan_tag);
		} else
			netif_rx(skb);

		/* let protocol layer free skb */
		buffer_info->skb = NULL;
		buffer_info->alloced = 0;
		rrd->xsz.valid = 0;

		adapter->netdev->last_rx = jiffies;
	}

	atomic_set(&rrd_ring->next_to_clean, rrd_next_to_clean);

	atl1_alloc_rx_buffers(adapter);

	/* update mailbox ? */
	if (count) {
		u32 tpd_next_to_use;
		u32 rfd_next_to_use;

		spin_lock(&adapter->mb_lock);

		tpd_next_to_use = atomic_read(&adapter->tpd_ring.next_to_use);
		rfd_next_to_use =
		    atomic_read(&adapter->rfd_ring.next_to_use);
		rrd_next_to_clean =
		    atomic_read(&adapter->rrd_ring.next_to_clean);
		value = ((rfd_next_to_use & MB_RFD_PROD_INDX_MASK) <<
			MB_RFD_PROD_INDX_SHIFT) |
                        ((rrd_next_to_clean & MB_RRD_CONS_INDX_MASK) <<
			MB_RRD_CONS_INDX_SHIFT) |
                        ((tpd_next_to_use & MB_TPD_PROD_INDX_MASK) <<
			MB_TPD_PROD_INDX_SHIFT);
		iowrite32(value, adapter->hw.hw_addr + REG_MAILBOX);
		spin_unlock(&adapter->mb_lock);
	}
}

static void atl1_intr_tx(struct atl1_adapter *adapter)
{
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_buffer *buffer_info;
	u16 sw_tpd_next_to_clean;
	u16 cmb_tpd_next_to_clean;

	sw_tpd_next_to_clean = atomic_read(&tpd_ring->next_to_clean);
	cmb_tpd_next_to_clean = le16_to_cpu(adapter->cmb.cmb->tpd_cons_idx);

	while (cmb_tpd_next_to_clean != sw_tpd_next_to_clean) {
		struct tx_packet_desc *tpd;

		tpd = ATL1_TPD_DESC(tpd_ring, sw_tpd_next_to_clean);
		buffer_info = &tpd_ring->buffer_info[sw_tpd_next_to_clean];
		if (buffer_info->dma) {
			pci_unmap_page(adapter->pdev, buffer_info->dma,
				       buffer_info->length, PCI_DMA_TODEVICE);
			buffer_info->dma = 0;
		}

		if (buffer_info->skb) {
			dev_kfree_skb_irq(buffer_info->skb);
			buffer_info->skb = NULL;
		}

		if (++sw_tpd_next_to_clean == tpd_ring->count)
			sw_tpd_next_to_clean = 0;
	}
	atomic_set(&tpd_ring->next_to_clean, sw_tpd_next_to_clean);

	if (netif_queue_stopped(adapter->netdev)
	    && netif_carrier_ok(adapter->netdev))
		netif_wake_queue(adapter->netdev);
}

static u16 atl1_tpd_avail(struct atl1_tpd_ring *tpd_ring)
{
	u16 next_to_clean = atomic_read(&tpd_ring->next_to_clean);
	u16 next_to_use = atomic_read(&tpd_ring->next_to_use);
	return ((next_to_clean > next_to_use) ?
		next_to_clean - next_to_use - 1 :
		tpd_ring->count + next_to_clean - next_to_use - 1);
}

static int atl1_tso(struct atl1_adapter *adapter, struct sk_buff *skb,
	struct tx_packet_desc *ptpd)
{
	/* spinlock held */
	u8 hdr_len, ip_off;
	u32 real_len;
	int err;

	if (skb_shinfo(skb)->gso_size) {
		if (skb_header_cloned(skb)) {
			err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (unlikely(err))
				return -1;
		}

		if (skb->protocol == ntohs(ETH_P_IP)) {
			struct iphdr *iph = ip_hdr(skb);

			real_len = (((unsigned char *)iph - skb->data) +
				ntohs(iph->tot_len));
			if (real_len < skb->len)
				pskb_trim(skb, real_len);
			hdr_len = (skb_transport_offset(skb) + tcp_hdrlen(skb));
			if (skb->len == hdr_len) {
				iph->check = 0;
				tcp_hdr(skb)->check =
					~csum_tcpudp_magic(iph->saddr,
					iph->daddr, tcp_hdrlen(skb),
					IPPROTO_TCP, 0);
				ptpd->word3 |= (iph->ihl & TPD_IPHL_MASK) <<
					TPD_IPHL_SHIFT;
				ptpd->word3 |= ((tcp_hdrlen(skb) >> 2) &
					TPD_TCPHDRLEN_MASK) <<
					TPD_TCPHDRLEN_SHIFT;
				ptpd->word3 |= 1 << TPD_IP_CSUM_SHIFT;
				ptpd->word3 |= 1 << TPD_TCP_CSUM_SHIFT;
				return 1;
			}

			iph->check = 0;
			tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
					iph->daddr, 0, IPPROTO_TCP, 0);
			ip_off = (unsigned char *)iph -
				(unsigned char *) skb_network_header(skb);
			if (ip_off == 8) /* 802.3-SNAP frame */
				ptpd->word3 |= 1 << TPD_ETHTYPE_SHIFT;
			else if (ip_off != 0)
				return -2;

			ptpd->word3 |= (iph->ihl & TPD_IPHL_MASK) <<
				TPD_IPHL_SHIFT;
			ptpd->word3 |= ((tcp_hdrlen(skb) >> 2) &
				TPD_TCPHDRLEN_MASK) << TPD_TCPHDRLEN_SHIFT;
			ptpd->word3 |= (skb_shinfo(skb)->gso_size &
				TPD_MSS_MASK) << TPD_MSS_SHIFT;
			ptpd->word3 |= 1 << TPD_SEGMENT_EN_SHIFT;
			return 3;
		}
	}
	return false;
}

static int atl1_tx_csum(struct atl1_adapter *adapter, struct sk_buff *skb,
	struct tx_packet_desc *ptpd)
{
	u8 css, cso;

	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		css = (u8) (skb->csum_start - skb_headroom(skb));
		cso = css + (u8) skb->csum_offset;
		if (unlikely(css & 0x1)) {
			/* L1 hardware requires an even number here */
			if (netif_msg_tx_err(adapter))
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"payload offset not an even number\n");
			return -1;
		}
		ptpd->word3 |= (css & TPD_PLOADOFFSET_MASK) <<
			TPD_PLOADOFFSET_SHIFT;
		ptpd->word3 |= (cso & TPD_CCSUMOFFSET_MASK) <<
			TPD_CCSUMOFFSET_SHIFT;
		ptpd->word3 |= 1 << TPD_CUST_CSUM_EN_SHIFT;
		return true;
	}
	return 0;
}

static void atl1_tx_map(struct atl1_adapter *adapter, struct sk_buff *skb,
	struct tx_packet_desc *ptpd)
{
	/* spinlock held */
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_buffer *buffer_info;
	u16 buf_len = skb->len;
	struct page *page;
	unsigned long offset;
	unsigned int nr_frags;
	unsigned int f;
	int retval;
	u16 next_to_use;
	u16 data_len;
	u8 hdr_len;

	buf_len -= skb->data_len;
	nr_frags = skb_shinfo(skb)->nr_frags;
	next_to_use = atomic_read(&tpd_ring->next_to_use);
	buffer_info = &tpd_ring->buffer_info[next_to_use];
	if (unlikely(buffer_info->skb))
		BUG();
	/* put skb in last TPD */
	buffer_info->skb = NULL;

	retval = (ptpd->word3 >> TPD_SEGMENT_EN_SHIFT) & TPD_SEGMENT_EN_MASK;
	if (retval) {
		/* TSO */
		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		buffer_info->length = hdr_len;
		page = virt_to_page(skb->data);
		offset = (unsigned long)skb->data & ~PAGE_MASK;
		buffer_info->dma = pci_map_page(adapter->pdev, page,
						offset, hdr_len,
						PCI_DMA_TODEVICE);

		if (++next_to_use == tpd_ring->count)
			next_to_use = 0;

		if (buf_len > hdr_len) {
			int i, nseg;

			data_len = buf_len - hdr_len;
			nseg = (data_len + ATL1_MAX_TX_BUF_LEN - 1) /
				ATL1_MAX_TX_BUF_LEN;
			for (i = 0; i < nseg; i++) {
				buffer_info =
				    &tpd_ring->buffer_info[next_to_use];
				buffer_info->skb = NULL;
				buffer_info->length =
				    (ATL1_MAX_TX_BUF_LEN >=
				     data_len) ? ATL1_MAX_TX_BUF_LEN : data_len;
				data_len -= buffer_info->length;
				page = virt_to_page(skb->data +
					(hdr_len + i * ATL1_MAX_TX_BUF_LEN));
				offset = (unsigned long)(skb->data +
					(hdr_len + i * ATL1_MAX_TX_BUF_LEN)) &
					~PAGE_MASK;
				buffer_info->dma = pci_map_page(adapter->pdev,
					page, offset, buffer_info->length,
					PCI_DMA_TODEVICE);
				if (++next_to_use == tpd_ring->count)
					next_to_use = 0;
			}
		}
	} else {
		/* not TSO */
		buffer_info->length = buf_len;
		page = virt_to_page(skb->data);
		offset = (unsigned long)skb->data & ~PAGE_MASK;
		buffer_info->dma = pci_map_page(adapter->pdev, page,
			offset, buf_len, PCI_DMA_TODEVICE);
		if (++next_to_use == tpd_ring->count)
			next_to_use = 0;
	}

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;
		u16 i, nseg;

		frag = &skb_shinfo(skb)->frags[f];
		buf_len = frag->size;

		nseg = (buf_len + ATL1_MAX_TX_BUF_LEN - 1) /
			ATL1_MAX_TX_BUF_LEN;
		for (i = 0; i < nseg; i++) {
			buffer_info = &tpd_ring->buffer_info[next_to_use];
			if (unlikely(buffer_info->skb))
				BUG();
			buffer_info->skb = NULL;
			buffer_info->length = (buf_len > ATL1_MAX_TX_BUF_LEN) ?
				ATL1_MAX_TX_BUF_LEN : buf_len;
			buf_len -= buffer_info->length;
			buffer_info->dma = pci_map_page(adapter->pdev,
				frag->page,
				frag->page_offset + (i * ATL1_MAX_TX_BUF_LEN),
				buffer_info->length, PCI_DMA_TODEVICE);

			if (++next_to_use == tpd_ring->count)
				next_to_use = 0;
		}
	}

	/* last tpd's buffer-info */
	buffer_info->skb = skb;
}

static void atl1_tx_queue(struct atl1_adapter *adapter, u16 count,
       struct tx_packet_desc *ptpd)
{
	/* spinlock held */
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_buffer *buffer_info;
	struct tx_packet_desc *tpd;
	u16 j;
	u32 val;
	u16 next_to_use = (u16) atomic_read(&tpd_ring->next_to_use);

	for (j = 0; j < count; j++) {
		buffer_info = &tpd_ring->buffer_info[next_to_use];
		tpd = ATL1_TPD_DESC(&adapter->tpd_ring, next_to_use);
		if (tpd != ptpd)
			memcpy(tpd, ptpd, sizeof(struct tx_packet_desc));
		tpd->buffer_addr = cpu_to_le64(buffer_info->dma);
		tpd->word2 = (cpu_to_le16(buffer_info->length) &
			TPD_BUFLEN_MASK) << TPD_BUFLEN_SHIFT;

		/*
		 * if this is the first packet in a TSO chain, set
		 * TPD_HDRFLAG, otherwise, clear it.
		 */
		val = (tpd->word3 >> TPD_SEGMENT_EN_SHIFT) &
			TPD_SEGMENT_EN_MASK;
		if (val) {
			if (!j)
				tpd->word3 |= 1 << TPD_HDRFLAG_SHIFT;
			else
				tpd->word3 &= ~(1 << TPD_HDRFLAG_SHIFT);
		}

		if (j == (count - 1))
			tpd->word3 |= 1 << TPD_EOP_SHIFT;

		if (++next_to_use == tpd_ring->count)
			next_to_use = 0;
	}
	/*
	 * Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	atomic_set(&tpd_ring->next_to_use, next_to_use);
}

static int atl1_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	int len = skb->len;
	int tso;
	int count = 1;
	int ret_val;
	struct tx_packet_desc *ptpd;
	u16 frag_size;
	u16 vlan_tag;
	unsigned long flags;
	unsigned int nr_frags = 0;
	unsigned int mss = 0;
	unsigned int f;
	unsigned int proto_hdr_len;

	len -= skb->data_len;

	if (unlikely(skb->len <= 0)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	nr_frags = skb_shinfo(skb)->nr_frags;
	for (f = 0; f < nr_frags; f++) {
		frag_size = skb_shinfo(skb)->frags[f].size;
		if (frag_size)
			count += (frag_size + ATL1_MAX_TX_BUF_LEN - 1) /
				ATL1_MAX_TX_BUF_LEN;
	}

	mss = skb_shinfo(skb)->gso_size;
	if (mss) {
		if (skb->protocol == ntohs(ETH_P_IP)) {
			proto_hdr_len = (skb_transport_offset(skb) +
					 tcp_hdrlen(skb));
			if (unlikely(proto_hdr_len > len)) {
				dev_kfree_skb_any(skb);
				return NETDEV_TX_OK;
			}
			/* need additional TPD ? */
			if (proto_hdr_len != len)
				count += (len - proto_hdr_len +
					ATL1_MAX_TX_BUF_LEN - 1) /
					ATL1_MAX_TX_BUF_LEN;
		}
	}

	if (!spin_trylock_irqsave(&adapter->lock, flags)) {
		/* Can't get lock - tell upper layer to requeue */
		if (netif_msg_tx_queued(adapter))
			dev_printk(KERN_DEBUG, &adapter->pdev->dev,
				"tx locked\n");
		return NETDEV_TX_LOCKED;
	}

	if (atl1_tpd_avail(&adapter->tpd_ring) < count) {
		/* not enough descriptors */
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&adapter->lock, flags);
		if (netif_msg_tx_queued(adapter))
			dev_printk(KERN_DEBUG, &adapter->pdev->dev,
				"tx busy\n");
		return NETDEV_TX_BUSY;
	}

	ptpd = ATL1_TPD_DESC(tpd_ring,
		(u16) atomic_read(&tpd_ring->next_to_use));
	memset(ptpd, 0, sizeof(struct tx_packet_desc));

	if (adapter->vlgrp && vlan_tx_tag_present(skb)) {
		vlan_tag = vlan_tx_tag_get(skb);
		vlan_tag = (vlan_tag << 4) | (vlan_tag >> 13) |
			((vlan_tag >> 9) & 0x8);
		ptpd->word3 |= 1 << TPD_INS_VL_TAG_SHIFT;
		ptpd->word3 |= (vlan_tag & TPD_VL_TAGGED_MASK) <<
			TPD_VL_TAGGED_SHIFT;
	}

	tso = atl1_tso(adapter, skb, ptpd);
	if (tso < 0) {
		spin_unlock_irqrestore(&adapter->lock, flags);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (!tso) {
		ret_val = atl1_tx_csum(adapter, skb, ptpd);
		if (ret_val < 0) {
			spin_unlock_irqrestore(&adapter->lock, flags);
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	}

	atl1_tx_map(adapter, skb, ptpd);
	atl1_tx_queue(adapter, count, ptpd);
	atl1_update_mailbox(adapter);
	spin_unlock_irqrestore(&adapter->lock, flags);
	netdev->trans_start = jiffies;
	return NETDEV_TX_OK;
}

/*
 * atl1_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 * @pt_regs: CPU registers structure
 */
static irqreturn_t atl1_intr(int irq, void *data)
{
	struct atl1_adapter *adapter = netdev_priv(data);
	u32 status;
	int max_ints = 10;

	status = adapter->cmb.cmb->int_stats;
	if (!status)
		return IRQ_NONE;

	do {
		/* clear CMB interrupt status at once */
		adapter->cmb.cmb->int_stats = 0;

		if (status & ISR_GPHY)	/* clear phy status */
			atlx_clear_phy_int(adapter);

		/* clear ISR status, and Enable CMB DMA/Disable Interrupt */
		iowrite32(status | ISR_DIS_INT, adapter->hw.hw_addr + REG_ISR);

		/* check if SMB intr */
		if (status & ISR_SMB)
			atl1_inc_smb(adapter);

		/* check if PCIE PHY Link down */
		if (status & ISR_PHY_LINKDOWN) {
			if (netif_msg_intr(adapter))
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"pcie phy link down %x\n", status);
			if (netif_running(adapter->netdev)) {	/* reset MAC */
				iowrite32(0, adapter->hw.hw_addr + REG_IMR);
				schedule_work(&adapter->pcie_dma_to_rst_task);
				return IRQ_HANDLED;
			}
		}

		/* check if DMA read/write error ? */
		if (status & (ISR_DMAR_TO_RST | ISR_DMAW_TO_RST)) {
			if (netif_msg_intr(adapter))
				dev_printk(KERN_DEBUG, &adapter->pdev->dev,
					"pcie DMA r/w error (status = 0x%x)\n",
					status);
			iowrite32(0, adapter->hw.hw_addr + REG_IMR);
			schedule_work(&adapter->pcie_dma_to_rst_task);
			return IRQ_HANDLED;
		}

		/* link event */
		if (status & ISR_GPHY) {
			adapter->soft_stats.tx_carrier_errors++;
			atl1_check_for_link(adapter);
		}

		/* transmit event */
		if (status & ISR_CMB_TX)
			atl1_intr_tx(adapter);

		/* rx exception */
		if (unlikely(status & (ISR_RXF_OV | ISR_RFD_UNRUN |
			ISR_RRD_OV | ISR_HOST_RFD_UNRUN |
			ISR_HOST_RRD_OV | ISR_CMB_RX))) {
			if (status & (ISR_RXF_OV | ISR_RFD_UNRUN |
				ISR_RRD_OV | ISR_HOST_RFD_UNRUN |
				ISR_HOST_RRD_OV))
				if (netif_msg_intr(adapter))
					dev_printk(KERN_DEBUG,
						&adapter->pdev->dev,
						"rx exception, ISR = 0x%x\n",
						status);
			atl1_intr_rx(adapter);
		}

		if (--max_ints < 0)
			break;

	} while ((status = adapter->cmb.cmb->int_stats));

	/* re-enable Interrupt */
	iowrite32(ISR_DIS_SMB | ISR_DIS_DMA, adapter->hw.hw_addr + REG_ISR);
	return IRQ_HANDLED;
}

/*
 * atl1_watchdog - Timer Call-back
 * @data: pointer to netdev cast into an unsigned long
 */
static void atl1_watchdog(unsigned long data)
{
	struct atl1_adapter *adapter = (struct atl1_adapter *)data;

	/* Reset the timer */
	mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);
}

/*
 * atl1_phy_config - Timer Call-back
 * @data: pointer to netdev cast into an unsigned long
 */
static void atl1_phy_config(unsigned long data)
{
	struct atl1_adapter *adapter = (struct atl1_adapter *)data;
	struct atl1_hw *hw = &adapter->hw;
	unsigned long flags;

	spin_lock_irqsave(&adapter->lock, flags);
	adapter->phy_timer_pending = false;
	atl1_write_phy_reg(hw, MII_ADVERTISE, hw->mii_autoneg_adv_reg);
	atl1_write_phy_reg(hw, MII_ATLX_CR, hw->mii_1000t_ctrl_reg);
	atl1_write_phy_reg(hw, MII_BMCR, MII_CR_RESET | MII_CR_AUTO_NEG_EN);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

/*
 * Orphaned vendor comment left intact here:
 * <vendor comment>
 * If TPD Buffer size equal to 0, PCIE DMAR_TO_INT
 * will assert. We do soft reset <0x1400=1> according
 * with the SPEC. BUT, it seemes that PCIE or DMA
 * state-machine will not be reset. DMAR_TO_INT will
 * assert again and again.
 * </vendor comment>
 */

static int atl1_reset(struct atl1_adapter *adapter)
{
	int ret;
	ret = atl1_reset_hw(&adapter->hw);
	if (ret)
		return ret;
	return atl1_init_hw(&adapter->hw);
}

static s32 atl1_up(struct atl1_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;
	int irq_flags = IRQF_SAMPLE_RANDOM;

	/* hardware has been reset, we need to reload some things */
	atlx_set_multi(netdev);
	atl1_init_ring_ptrs(adapter);
	atlx_restore_vlan(adapter);
	err = atl1_alloc_rx_buffers(adapter);
	if (unlikely(!err))
		/* no RX BUFFER allocated */
		return -ENOMEM;

	if (unlikely(atl1_configure(adapter))) {
		err = -EIO;
		goto err_up;
	}

	err = pci_enable_msi(adapter->pdev);
	if (err) {
		if (netif_msg_ifup(adapter))
			dev_info(&adapter->pdev->dev,
				"Unable to enable MSI: %d\n", err);
		irq_flags |= IRQF_SHARED;
	}

	err = request_irq(adapter->pdev->irq, &atl1_intr, irq_flags,
			netdev->name, netdev);
	if (unlikely(err))
		goto err_up;

	mod_timer(&adapter->watchdog_timer, jiffies);
	atlx_irq_enable(adapter);
	atl1_check_link(adapter);
	return 0;

err_up:
	pci_disable_msi(adapter->pdev);
	/* free rx_buffers */
	atl1_clean_rx_ring(adapter);
	return err;
}

static void atl1_down(struct atl1_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_config_timer);
	adapter->phy_timer_pending = false;

	atlx_irq_disable(adapter);
	free_irq(adapter->pdev->irq, netdev);
	pci_disable_msi(adapter->pdev);
	atl1_reset_hw(&adapter->hw);
	adapter->cmb.cmb->int_stats = 0;

	adapter->link_speed = SPEED_0;
	adapter->link_duplex = -1;
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	atl1_clean_tx_ring(adapter);
	atl1_clean_rx_ring(adapter);
}

static void atl1_tx_timeout_task(struct work_struct *work)
{
	struct atl1_adapter *adapter =
		container_of(work, struct atl1_adapter, tx_timeout_task);
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);
	atl1_down(adapter);
	atl1_up(adapter);
	netif_device_attach(netdev);
}

/*
 * atl1_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 */
static int atl1_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	int old_mtu = netdev->mtu;
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

	if ((max_frame < ETH_ZLEN + ETH_FCS_LEN) ||
	    (max_frame > MAX_JUMBO_FRAME_SIZE)) {
		if (netif_msg_link(adapter))
			dev_warn(&adapter->pdev->dev, "invalid MTU setting\n");
		return -EINVAL;
	}

	adapter->hw.max_frame_size = max_frame;
	adapter->hw.tx_jumbo_task_th = (max_frame + 7) >> 3;
	adapter->rx_buffer_len = (max_frame + 7) & ~7;
	adapter->hw.rx_jumbo_th = adapter->rx_buffer_len / 8;

	netdev->mtu = new_mtu;
	if ((old_mtu != new_mtu) && netif_running(netdev)) {
		atl1_down(adapter);
		atl1_up(adapter);
	}

	return 0;
}

/*
 * atl1_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 */
static int atl1_open(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	int err;

	/* allocate transmit descriptors */
	err = atl1_setup_ring_resources(adapter);
	if (err)
		return err;

	err = atl1_up(adapter);
	if (err)
		goto err_up;

	return 0;

err_up:
	atl1_reset(adapter);
	return err;
}

/*
 * atl1_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 */
static int atl1_close(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	atl1_down(adapter);
	atl1_free_ring_resources(adapter);
	return 0;
}

#ifdef CONFIG_PM
static int atl1_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;
	u32 ctrl = 0;
	u32 wufc = adapter->wol;

	netif_device_detach(netdev);
	if (netif_running(netdev))
		atl1_down(adapter);

	atl1_read_phy_reg(hw, MII_BMSR, (u16 *) & ctrl);
	atl1_read_phy_reg(hw, MII_BMSR, (u16 *) & ctrl);
	if (ctrl & BMSR_LSTATUS)
		wufc &= ~ATLX_WUFC_LNKC;

	/* reduce speed to 10/100M */
	if (wufc) {
		atl1_phy_enter_power_saving(hw);
		/* if resume, let driver to re- setup link */
		hw->phy_configured = false;
		atl1_set_mac_addr(hw);
		atlx_set_multi(netdev);

		ctrl = 0;
		/* turn on magic packet wol */
		if (wufc & ATLX_WUFC_MAG)
			ctrl = WOL_MAGIC_EN | WOL_MAGIC_PME_EN;

		/* turn on Link change WOL */
		if (wufc & ATLX_WUFC_LNKC)
			ctrl |= (WOL_LINK_CHG_EN | WOL_LINK_CHG_PME_EN);
		iowrite32(ctrl, hw->hw_addr + REG_WOL_CTRL);

		/* turn on all-multi mode if wake on multicast is enabled */
		ctrl = ioread32(hw->hw_addr + REG_MAC_CTRL);
		ctrl &= ~MAC_CTRL_DBG;
		ctrl &= ~MAC_CTRL_PROMIS_EN;
		if (wufc & ATLX_WUFC_MC)
			ctrl |= MAC_CTRL_MC_ALL_EN;
		else
			ctrl &= ~MAC_CTRL_MC_ALL_EN;

		/* turn on broadcast mode if wake on-BC is enabled */
		if (wufc & ATLX_WUFC_BC)
			ctrl |= MAC_CTRL_BC_EN;
		else
			ctrl &= ~MAC_CTRL_BC_EN;

		/* enable RX */
		ctrl |= MAC_CTRL_RX_EN;
		iowrite32(ctrl, hw->hw_addr + REG_MAC_CTRL);
		pci_enable_wake(pdev, PCI_D3hot, 1);
		pci_enable_wake(pdev, PCI_D3cold, 1);
	} else {
		iowrite32(0, hw->hw_addr + REG_WOL_CTRL);
		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_enable_wake(pdev, PCI_D3cold, 0);
	}

	pci_save_state(pdev);
	pci_disable_device(pdev);

	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int atl1_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1_adapter *adapter = netdev_priv(netdev);
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	/* FIXME: check and handle */
	err = pci_enable_device(pdev);
	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	iowrite32(0, adapter->hw.hw_addr + REG_WOL_CTRL);
	atl1_reset(adapter);

	if (netif_running(netdev))
		atl1_up(adapter);
	netif_device_attach(netdev);

	atl1_via_workaround(adapter);

	return 0;
}
#else
#define atl1_suspend NULL
#define atl1_resume NULL
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
static void atl1_poll_controller(struct net_device *netdev)
{
	disable_irq(netdev->irq);
	atl1_intr(netdev->irq, netdev);
	enable_irq(netdev->irq);
}
#endif

/*
 * atl1_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in atl1_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * atl1_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 */
static int __devinit atl1_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct atl1_adapter *adapter;
	static int cards_found = 0;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	/*
	 * The atl1 chip can DMA to 64-bit addresses, but it uses a single
	 * shared register for the high 32 bits, so only a single, aligned,
	 * 4 GB physical address range can be used at a time.
	 *
	 * Supporting 64-bit DMA on this hardware is more trouble than it's
	 * worth.  It is far easier to limit to 32-bit DMA than update
	 * various kernel subsystems to support the mechanics required by a
	 * fixed-high-32-bit system.
	 */
	err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (err) {
		dev_err(&pdev->dev, "no usable DMA configuration\n");
		goto err_dma;
	}
	/*
	 * Mark all PCI regions associated with PCI device
	 * pdev as being reserved by owner atl1_driver_name
	 */
	err = pci_request_regions(pdev, ATLX_DRIVER_NAME);
	if (err)
		goto err_request_regions;

	/*
	 * Enables bus-mastering on the device and calls
	 * pcibios_set_master to do the needed arch specific settings
	 */
	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct atl1_adapter));
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->hw.back = adapter;
	adapter->msg_enable = netif_msg_init(debug, atl1_default_msg);

	adapter->hw.hw_addr = pci_iomap(pdev, 0, 0);
	if (!adapter->hw.hw_addr) {
		err = -EIO;
		goto err_pci_iomap;
	}
	/* get device revision number */
	adapter->hw.dev_rev = ioread16(adapter->hw.hw_addr +
		(REG_MASTER_CTRL + 2));
	if (netif_msg_probe(adapter))
		dev_info(&pdev->dev, "version %s\n", ATLX_DRIVER_VERSION);

	/* set default ring resource counts */
	adapter->rfd_ring.count = adapter->rrd_ring.count = ATL1_DEFAULT_RFD;
	adapter->tpd_ring.count = ATL1_DEFAULT_TPD;

	adapter->mii.dev = netdev;
	adapter->mii.mdio_read = mdio_read;
	adapter->mii.mdio_write = mdio_write;
	adapter->mii.phy_id_mask = 0x1f;
	adapter->mii.reg_num_mask = 0x1f;

	netdev->open = &atl1_open;
	netdev->stop = &atl1_close;
	netdev->hard_start_xmit = &atl1_xmit_frame;
	netdev->get_stats = &atlx_get_stats;
	netdev->set_multicast_list = &atlx_set_multi;
	netdev->set_mac_address = &atl1_set_mac;
	netdev->change_mtu = &atl1_change_mtu;
	netdev->do_ioctl = &atlx_ioctl;
	netdev->tx_timeout = &atlx_tx_timeout;
	netdev->watchdog_timeo = 5 * HZ;
#ifdef CONFIG_NET_POLL_CONTROLLER
	netdev->poll_controller = atl1_poll_controller;
#endif
	netdev->vlan_rx_register = atlx_vlan_rx_register;

	netdev->ethtool_ops = &atl1_ethtool_ops;
	adapter->bd_number = cards_found;

	/* setup the private structure */
	err = atl1_sw_init(adapter);
	if (err)
		goto err_common;

	netdev->features = NETIF_F_HW_CSUM;
	netdev->features |= NETIF_F_SG;
	netdev->features |= (NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX);
	netdev->features |= NETIF_F_TSO;
	netdev->features |= NETIF_F_LLTX;

	/*
	 * patch for some L1 of old version,
	 * the final version of L1 may not need these
	 * patches
	 */
	/* atl1_pcie_patch(adapter); */

	/* really reset GPHY core */
	iowrite16(0, adapter->hw.hw_addr + REG_PHY_ENABLE);

	/*
	 * reset the controller to
	 * put the device in a known good starting state
	 */
	if (atl1_reset_hw(&adapter->hw)) {
		err = -EIO;
		goto err_common;
	}

	/* copy the MAC address out of the EEPROM */
	atl1_read_mac_addr(&adapter->hw);
	memcpy(netdev->dev_addr, adapter->hw.mac_addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		err = -EIO;
		goto err_common;
	}

	atl1_check_options(adapter);

	/* pre-init the MAC, and setup link */
	err = atl1_init_hw(&adapter->hw);
	if (err) {
		err = -EIO;
		goto err_common;
	}

	atl1_pcie_patch(adapter);
	/* assume we have no link for now */
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &atl1_watchdog;
	adapter->watchdog_timer.data = (unsigned long)adapter;

	init_timer(&adapter->phy_config_timer);
	adapter->phy_config_timer.function = &atl1_phy_config;
	adapter->phy_config_timer.data = (unsigned long)adapter;
	adapter->phy_timer_pending = false;

	INIT_WORK(&adapter->tx_timeout_task, atl1_tx_timeout_task);

	INIT_WORK(&adapter->link_chg_task, atlx_link_chg_task);

	INIT_WORK(&adapter->pcie_dma_to_rst_task, atl1_tx_timeout_task);

	err = register_netdev(netdev);
	if (err)
		goto err_common;

	cards_found++;
	atl1_via_workaround(adapter);
	return 0;

err_common:
	pci_iounmap(pdev, adapter->hw.hw_addr);
err_pci_iomap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_regions(pdev);
err_dma:
err_request_regions:
	pci_disable_device(pdev);
	return err;
}

/*
 * atl1_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * atl1_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 */
static void __devexit atl1_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1_adapter *adapter;
	/* Device not available. Return. */
	if (!netdev)
		return;

	adapter = netdev_priv(netdev);

	/*
	 * Some atl1 boards lack persistent storage for their MAC, and get it
	 * from the BIOS during POST.  If we've been messing with the MAC
	 * address, we need to save the permanent one.
	 */
	if (memcmp(adapter->hw.mac_addr, adapter->hw.perm_mac_addr, ETH_ALEN)) {
		memcpy(adapter->hw.mac_addr, adapter->hw.perm_mac_addr,
			ETH_ALEN);
		atl1_set_mac_addr(&adapter->hw);
	}

	iowrite16(0, adapter->hw.hw_addr + REG_PHY_ENABLE);
	unregister_netdev(netdev);
	pci_iounmap(pdev, adapter->hw.hw_addr);
	pci_release_regions(pdev);
	free_netdev(netdev);
	pci_disable_device(pdev);
}

static struct pci_driver atl1_driver = {
	.name = ATLX_DRIVER_NAME,
	.id_table = atl1_pci_tbl,
	.probe = atl1_probe,
	.remove = __devexit_p(atl1_remove),
	.suspend = atl1_suspend,
	.resume = atl1_resume
};

/*
 * atl1_exit_module - Driver Exit Cleanup Routine
 *
 * atl1_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit atl1_exit_module(void)
{
	pci_unregister_driver(&atl1_driver);
}

/*
 * atl1_init_module - Driver Registration Routine
 *
 * atl1_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init atl1_init_module(void)
{
	return pci_register_driver(&atl1_driver);
}

module_init(atl1_init_module);
module_exit(atl1_exit_module);

struct atl1_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define ATL1_STAT(m) \
	sizeof(((struct atl1_adapter *)0)->m), offsetof(struct atl1_adapter, m)

static struct atl1_stats atl1_gstrings_stats[] = {
	{"rx_packets", ATL1_STAT(soft_stats.rx_packets)},
	{"tx_packets", ATL1_STAT(soft_stats.tx_packets)},
	{"rx_bytes", ATL1_STAT(soft_stats.rx_bytes)},
	{"tx_bytes", ATL1_STAT(soft_stats.tx_bytes)},
	{"rx_errors", ATL1_STAT(soft_stats.rx_errors)},
	{"tx_errors", ATL1_STAT(soft_stats.tx_errors)},
	{"rx_dropped", ATL1_STAT(net_stats.rx_dropped)},
	{"tx_dropped", ATL1_STAT(net_stats.tx_dropped)},
	{"multicast", ATL1_STAT(soft_stats.multicast)},
	{"collisions", ATL1_STAT(soft_stats.collisions)},
	{"rx_length_errors", ATL1_STAT(soft_stats.rx_length_errors)},
	{"rx_over_errors", ATL1_STAT(soft_stats.rx_missed_errors)},
	{"rx_crc_errors", ATL1_STAT(soft_stats.rx_crc_errors)},
	{"rx_frame_errors", ATL1_STAT(soft_stats.rx_frame_errors)},
	{"rx_fifo_errors", ATL1_STAT(soft_stats.rx_fifo_errors)},
	{"rx_missed_errors", ATL1_STAT(soft_stats.rx_missed_errors)},
	{"tx_aborted_errors", ATL1_STAT(soft_stats.tx_aborted_errors)},
	{"tx_carrier_errors", ATL1_STAT(soft_stats.tx_carrier_errors)},
	{"tx_fifo_errors", ATL1_STAT(soft_stats.tx_fifo_errors)},
	{"tx_window_errors", ATL1_STAT(soft_stats.tx_window_errors)},
	{"tx_abort_exce_coll", ATL1_STAT(soft_stats.excecol)},
	{"tx_abort_late_coll", ATL1_STAT(soft_stats.latecol)},
	{"tx_deferred_ok", ATL1_STAT(soft_stats.deffer)},
	{"tx_single_coll_ok", ATL1_STAT(soft_stats.scc)},
	{"tx_multi_coll_ok", ATL1_STAT(soft_stats.mcc)},
	{"tx_underun", ATL1_STAT(soft_stats.tx_underun)},
	{"tx_trunc", ATL1_STAT(soft_stats.tx_trunc)},
	{"tx_pause", ATL1_STAT(soft_stats.tx_pause)},
	{"rx_pause", ATL1_STAT(soft_stats.rx_pause)},
	{"rx_rrd_ov", ATL1_STAT(soft_stats.rx_rrd_ov)},
	{"rx_trunc", ATL1_STAT(soft_stats.rx_trunc)}
};

static void atl1_get_ethtool_stats(struct net_device *netdev,
	struct ethtool_stats *stats, u64 *data)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	int i;
	char *p;

	for (i = 0; i < ARRAY_SIZE(atl1_gstrings_stats); i++) {
		p = (char *)adapter+atl1_gstrings_stats[i].stat_offset;
		data[i] = (atl1_gstrings_stats[i].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

}

static int atl1_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(atl1_gstrings_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static int atl1_get_settings(struct net_device *netdev,
	struct ethtool_cmd *ecmd)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;

	ecmd->supported = (SUPPORTED_10baseT_Half |
			   SUPPORTED_10baseT_Full |
			   SUPPORTED_100baseT_Half |
			   SUPPORTED_100baseT_Full |
			   SUPPORTED_1000baseT_Full |
			   SUPPORTED_Autoneg | SUPPORTED_TP);
	ecmd->advertising = ADVERTISED_TP;
	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL) {
		ecmd->advertising |= ADVERTISED_Autoneg;
		if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR) {
			ecmd->advertising |= ADVERTISED_Autoneg;
			ecmd->advertising |=
			    (ADVERTISED_10baseT_Half |
			     ADVERTISED_10baseT_Full |
			     ADVERTISED_100baseT_Half |
			     ADVERTISED_100baseT_Full |
			     ADVERTISED_1000baseT_Full);
		} else
			ecmd->advertising |= (ADVERTISED_1000baseT_Full);
	}
	ecmd->port = PORT_TP;
	ecmd->phy_address = 0;
	ecmd->transceiver = XCVR_INTERNAL;

	if (netif_carrier_ok(adapter->netdev)) {
		u16 link_speed, link_duplex;
		atl1_get_speed_and_duplex(hw, &link_speed, &link_duplex);
		ecmd->speed = link_speed;
		if (link_duplex == FULL_DUPLEX)
			ecmd->duplex = DUPLEX_FULL;
		else
			ecmd->duplex = DUPLEX_HALF;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}
	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL)
		ecmd->autoneg = AUTONEG_ENABLE;
	else
		ecmd->autoneg = AUTONEG_DISABLE;

	return 0;
}

static int atl1_set_settings(struct net_device *netdev,
	struct ethtool_cmd *ecmd)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;
	u16 phy_data;
	int ret_val = 0;
	u16 old_media_type = hw->media_type;

	if (netif_running(adapter->netdev)) {
		if (netif_msg_link(adapter))
			dev_dbg(&adapter->pdev->dev,
				"ethtool shutting down adapter\n");
		atl1_down(adapter);
	}

	if (ecmd->autoneg == AUTONEG_ENABLE)
		hw->media_type = MEDIA_TYPE_AUTO_SENSOR;
	else {
		if (ecmd->speed == SPEED_1000) {
			if (ecmd->duplex != DUPLEX_FULL) {
				if (netif_msg_link(adapter))
					dev_warn(&adapter->pdev->dev,
						"1000M half is invalid\n");
				ret_val = -EINVAL;
				goto exit_sset;
			}
			hw->media_type = MEDIA_TYPE_1000M_FULL;
		} else if (ecmd->speed == SPEED_100) {
			if (ecmd->duplex == DUPLEX_FULL)
				hw->media_type = MEDIA_TYPE_100M_FULL;
			else
				hw->media_type = MEDIA_TYPE_100M_HALF;
		} else {
			if (ecmd->duplex == DUPLEX_FULL)
				hw->media_type = MEDIA_TYPE_10M_FULL;
			else
				hw->media_type = MEDIA_TYPE_10M_HALF;
		}
	}
	switch (hw->media_type) {
	case MEDIA_TYPE_AUTO_SENSOR:
		ecmd->advertising =
		    ADVERTISED_10baseT_Half |
		    ADVERTISED_10baseT_Full |
		    ADVERTISED_100baseT_Half |
		    ADVERTISED_100baseT_Full |
		    ADVERTISED_1000baseT_Full |
		    ADVERTISED_Autoneg | ADVERTISED_TP;
		break;
	case MEDIA_TYPE_1000M_FULL:
		ecmd->advertising =
		    ADVERTISED_1000baseT_Full |
		    ADVERTISED_Autoneg | ADVERTISED_TP;
		break;
	default:
		ecmd->advertising = 0;
		break;
	}
	if (atl1_phy_setup_autoneg_adv(hw)) {
		ret_val = -EINVAL;
		if (netif_msg_link(adapter))
			dev_warn(&adapter->pdev->dev,
				"invalid ethtool speed/duplex setting\n");
		goto exit_sset;
	}
	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL)
		phy_data = MII_CR_RESET | MII_CR_AUTO_NEG_EN;
	else {
		switch (hw->media_type) {
		case MEDIA_TYPE_100M_FULL:
			phy_data =
			    MII_CR_FULL_DUPLEX | MII_CR_SPEED_100 |
			    MII_CR_RESET;
			break;
		case MEDIA_TYPE_100M_HALF:
			phy_data = MII_CR_SPEED_100 | MII_CR_RESET;
			break;
		case MEDIA_TYPE_10M_FULL:
			phy_data =
			    MII_CR_FULL_DUPLEX | MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		default:
			/* MEDIA_TYPE_10M_HALF: */
			phy_data = MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		}
	}
	atl1_write_phy_reg(hw, MII_BMCR, phy_data);
exit_sset:
	if (ret_val)
		hw->media_type = old_media_type;

	if (netif_running(adapter->netdev)) {
		if (netif_msg_link(adapter))
			dev_dbg(&adapter->pdev->dev,
				"ethtool starting adapter\n");
		atl1_up(adapter);
	} else if (!ret_val) {
		if (netif_msg_link(adapter))
			dev_dbg(&adapter->pdev->dev,
				"ethtool resetting adapter\n");
		atl1_reset(adapter);
	}
	return ret_val;
}

static void atl1_get_drvinfo(struct net_device *netdev,
	struct ethtool_drvinfo *drvinfo)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);

	strncpy(drvinfo->driver, ATLX_DRIVER_NAME, sizeof(drvinfo->driver));
	strncpy(drvinfo->version, ATLX_DRIVER_VERSION,
		sizeof(drvinfo->version));
	strncpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strncpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->eedump_len = ATL1_EEDUMP_LEN;
}

static void atl1_get_wol(struct net_device *netdev,
	struct ethtool_wolinfo *wol)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);

	wol->supported = WAKE_UCAST | WAKE_MCAST | WAKE_BCAST | WAKE_MAGIC;
	wol->wolopts = 0;
	if (adapter->wol & ATLX_WUFC_EX)
		wol->wolopts |= WAKE_UCAST;
	if (adapter->wol & ATLX_WUFC_MC)
		wol->wolopts |= WAKE_MCAST;
	if (adapter->wol & ATLX_WUFC_BC)
		wol->wolopts |= WAKE_BCAST;
	if (adapter->wol & ATLX_WUFC_MAG)
		wol->wolopts |= WAKE_MAGIC;
	return;
}

static int atl1_set_wol(struct net_device *netdev,
	struct ethtool_wolinfo *wol)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_PHY | WAKE_ARP | WAKE_MAGICSECURE))
		return -EOPNOTSUPP;
	adapter->wol = 0;
	if (wol->wolopts & WAKE_UCAST)
		adapter->wol |= ATLX_WUFC_EX;
	if (wol->wolopts & WAKE_MCAST)
		adapter->wol |= ATLX_WUFC_MC;
	if (wol->wolopts & WAKE_BCAST)
		adapter->wol |= ATLX_WUFC_BC;
	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol |= ATLX_WUFC_MAG;
	return 0;
}

static u32 atl1_get_msglevel(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	return adapter->msg_enable;
}

static void atl1_set_msglevel(struct net_device *netdev, u32 value)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	adapter->msg_enable = value;
}

static int atl1_get_regs_len(struct net_device *netdev)
{
	return ATL1_REG_COUNT * sizeof(u32);
}

static void atl1_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
	void *p)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;
	unsigned int i;
	u32 *regbuf = p;

	for (i = 0; i < ATL1_REG_COUNT; i++) {
		/*
		 * This switch statement avoids reserved regions
		 * of register space.
		 */
		switch (i) {
		case 6 ... 9:
		case 14:
		case 29 ... 31:
		case 34 ... 63:
		case 75 ... 127:
		case 136 ... 1023:
		case 1027 ... 1087:
		case 1091 ... 1151:
		case 1194 ... 1195:
		case 1200 ... 1201:
		case 1206 ... 1213:
		case 1216 ... 1279:
		case 1290 ... 1311:
		case 1323 ... 1343:
		case 1358 ... 1359:
		case 1368 ... 1375:
		case 1378 ... 1383:
		case 1388 ... 1391:
		case 1393 ... 1395:
		case 1402 ... 1403:
		case 1410 ... 1471:
		case 1522 ... 1535:
			/* reserved region; don't read it */
			regbuf[i] = 0;
			break;
		default:
			/* unreserved region */
			regbuf[i] = ioread32(hw->hw_addr + (i * sizeof(u32)));
		}
	}
}

static void atl1_get_ringparam(struct net_device *netdev,
	struct ethtool_ringparam *ring)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_tpd_ring *txdr = &adapter->tpd_ring;
	struct atl1_rfd_ring *rxdr = &adapter->rfd_ring;

	ring->rx_max_pending = ATL1_MAX_RFD;
	ring->tx_max_pending = ATL1_MAX_TPD;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = rxdr->count;
	ring->tx_pending = txdr->count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int atl1_set_ringparam(struct net_device *netdev,
	struct ethtool_ringparam *ring)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_tpd_ring *tpdr = &adapter->tpd_ring;
	struct atl1_rrd_ring *rrdr = &adapter->rrd_ring;
	struct atl1_rfd_ring *rfdr = &adapter->rfd_ring;

	struct atl1_tpd_ring tpd_old, tpd_new;
	struct atl1_rfd_ring rfd_old, rfd_new;
	struct atl1_rrd_ring rrd_old, rrd_new;
	struct atl1_ring_header rhdr_old, rhdr_new;
	int err;

	tpd_old = adapter->tpd_ring;
	rfd_old = adapter->rfd_ring;
	rrd_old = adapter->rrd_ring;
	rhdr_old = adapter->ring_header;

	if (netif_running(adapter->netdev))
		atl1_down(adapter);

	rfdr->count = (u16) max(ring->rx_pending, (u32) ATL1_MIN_RFD);
	rfdr->count = rfdr->count > ATL1_MAX_RFD ? ATL1_MAX_RFD :
			rfdr->count;
	rfdr->count = (rfdr->count + 3) & ~3;
	rrdr->count = rfdr->count;

	tpdr->count = (u16) max(ring->tx_pending, (u32) ATL1_MIN_TPD);
	tpdr->count = tpdr->count > ATL1_MAX_TPD ? ATL1_MAX_TPD :
			tpdr->count;
	tpdr->count = (tpdr->count + 3) & ~3;

	if (netif_running(adapter->netdev)) {
		/* try to get new resources before deleting old */
		err = atl1_setup_ring_resources(adapter);
		if (err)
			goto err_setup_ring;

		/*
		 * save the new, restore the old in order to free it,
		 * then restore the new back again
		 */

		rfd_new = adapter->rfd_ring;
		rrd_new = adapter->rrd_ring;
		tpd_new = adapter->tpd_ring;
		rhdr_new = adapter->ring_header;
		adapter->rfd_ring = rfd_old;
		adapter->rrd_ring = rrd_old;
		adapter->tpd_ring = tpd_old;
		adapter->ring_header = rhdr_old;
		atl1_free_ring_resources(adapter);
		adapter->rfd_ring = rfd_new;
		adapter->rrd_ring = rrd_new;
		adapter->tpd_ring = tpd_new;
		adapter->ring_header = rhdr_new;

		err = atl1_up(adapter);
		if (err)
			return err;
	}
	return 0;

err_setup_ring:
	adapter->rfd_ring = rfd_old;
	adapter->rrd_ring = rrd_old;
	adapter->tpd_ring = tpd_old;
	adapter->ring_header = rhdr_old;
	atl1_up(adapter);
	return err;
}

static void atl1_get_pauseparam(struct net_device *netdev,
	struct ethtool_pauseparam *epause)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;

	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL) {
		epause->autoneg = AUTONEG_ENABLE;
	} else {
		epause->autoneg = AUTONEG_DISABLE;
	}
	epause->rx_pause = 1;
	epause->tx_pause = 1;
}

static int atl1_set_pauseparam(struct net_device *netdev,
	struct ethtool_pauseparam *epause)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;

	if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
	    hw->media_type == MEDIA_TYPE_1000M_FULL) {
		epause->autoneg = AUTONEG_ENABLE;
	} else {
		epause->autoneg = AUTONEG_DISABLE;
	}

	epause->rx_pause = 1;
	epause->tx_pause = 1;

	return 0;
}

/* FIXME: is this right? -- CHS */
static u32 atl1_get_rx_csum(struct net_device *netdev)
{
	return 1;
}

static void atl1_get_strings(struct net_device *netdev, u32 stringset,
	u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(atl1_gstrings_stats); i++) {
			memcpy(p, atl1_gstrings_stats[i].stat_string,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int atl1_nway_reset(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;

	if (netif_running(netdev)) {
		u16 phy_data;
		atl1_down(adapter);

		if (hw->media_type == MEDIA_TYPE_AUTO_SENSOR ||
			hw->media_type == MEDIA_TYPE_1000M_FULL) {
			phy_data = MII_CR_RESET | MII_CR_AUTO_NEG_EN;
		} else {
			switch (hw->media_type) {
			case MEDIA_TYPE_100M_FULL:
				phy_data = MII_CR_FULL_DUPLEX |
					MII_CR_SPEED_100 | MII_CR_RESET;
				break;
			case MEDIA_TYPE_100M_HALF:
				phy_data = MII_CR_SPEED_100 | MII_CR_RESET;
				break;
			case MEDIA_TYPE_10M_FULL:
				phy_data = MII_CR_FULL_DUPLEX |
					MII_CR_SPEED_10 | MII_CR_RESET;
				break;
			default:
				/* MEDIA_TYPE_10M_HALF */
				phy_data = MII_CR_SPEED_10 | MII_CR_RESET;
			}
		}
		atl1_write_phy_reg(hw, MII_BMCR, phy_data);
		atl1_up(adapter);
	}
	return 0;
}

const struct ethtool_ops atl1_ethtool_ops = {
	.get_settings		= atl1_get_settings,
	.set_settings		= atl1_set_settings,
	.get_drvinfo		= atl1_get_drvinfo,
	.get_wol		= atl1_get_wol,
	.set_wol		= atl1_set_wol,
	.get_msglevel		= atl1_get_msglevel,
	.set_msglevel		= atl1_set_msglevel,
	.get_regs_len		= atl1_get_regs_len,
	.get_regs		= atl1_get_regs,
	.get_ringparam		= atl1_get_ringparam,
	.set_ringparam		= atl1_set_ringparam,
	.get_pauseparam		= atl1_get_pauseparam,
	.set_pauseparam		= atl1_set_pauseparam,
	.get_rx_csum		= atl1_get_rx_csum,
	.set_tx_csum		= ethtool_op_set_tx_hw_csum,
	.get_link		= ethtool_op_get_link,
	.set_sg			= ethtool_op_set_sg,
	.get_strings		= atl1_get_strings,
	.nway_reset		= atl1_nway_reset,
	.get_ethtool_stats	= atl1_get_ethtool_stats,
	.get_sset_count		= atl1_get_sset_count,
	.set_tso		= ethtool_op_set_tso,
};
