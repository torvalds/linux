/*
 * Copyright (C) 2010 OKI SEMICONDUCTOR CO., LTD.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/if_ether.h>
#include <linux/ctype.h>
#include <linux/dmi.h>

#define PHUB_STATUS 0x00		/* Status Register offset */
#define PHUB_CONTROL 0x04		/* Control Register offset */
#define PHUB_TIMEOUT 0x05		/* Time out value for Status Register */
#define PCH_PHUB_ROM_WRITE_ENABLE 0x01	/* Enabling for writing ROM */
#define PCH_PHUB_ROM_WRITE_DISABLE 0x00	/* Disabling for writing ROM */
#define PCH_PHUB_MAC_START_ADDR_EG20T 0x14  /* MAC data area start address
					       offset */
#define PCH_PHUB_MAC_START_ADDR_ML7223 0x20C  /* MAC data area start address
						 offset */
#define PCH_PHUB_ROM_START_ADDR_EG20T 0x80 /* ROM data area start address offset
					      (Intel EG20T PCH)*/
#define PCH_PHUB_ROM_START_ADDR_ML7213 0x400 /* ROM data area start address
						offset(OKI SEMICONDUCTOR ML7213)
					      */
#define PCH_PHUB_ROM_START_ADDR_ML7223 0x400 /* ROM data area start address
						offset(OKI SEMICONDUCTOR ML7223)
					      */

/* MAX number of INT_REDUCE_CONTROL registers */
#define MAX_NUM_INT_REDUCE_CONTROL_REG 128
#define PCI_DEVICE_ID_PCH1_PHUB 0x8801
#define PCH_MINOR_NOS 1
#define CLKCFG_CAN_50MHZ 0x12000000
#define CLKCFG_CANCLK_MASK 0xFF000000
#define CLKCFG_UART_MASK			0xFFFFFF

/* CM-iTC */
#define CLKCFG_UART_48MHZ			(1 << 16)
#define CLKCFG_BAUDDIV				(2 << 20)
#define CLKCFG_PLL2VCO				(8 << 9)
#define CLKCFG_UARTCLKSEL			(1 << 18)

/* Macros for ML7213 */
#define PCI_VENDOR_ID_ROHM			0x10db
#define PCI_DEVICE_ID_ROHM_ML7213_PHUB		0x801A

/* Macros for ML7213 */
#define PCI_VENDOR_ID_ROHM			0x10db
#define PCI_DEVICE_ID_ROHM_ML7213_PHUB		0x801A

/* Macros for ML7223 */
#define PCI_DEVICE_ID_ROHM_ML7223_mPHUB	0x8012 /* for Bus-m */
#define PCI_DEVICE_ID_ROHM_ML7223_nPHUB	0x8002 /* for Bus-n */

/* SROM ACCESS Macro */
#define PCH_WORD_ADDR_MASK (~((1 << 2) - 1))

/* Registers address offset */
#define PCH_PHUB_ID_REG				0x0000
#define PCH_PHUB_QUEUE_PRI_VAL_REG		0x0004
#define PCH_PHUB_RC_QUEUE_MAXSIZE_REG		0x0008
#define PCH_PHUB_BRI_QUEUE_MAXSIZE_REG		0x000C
#define PCH_PHUB_COMP_RESP_TIMEOUT_REG		0x0010
#define PCH_PHUB_BUS_SLAVE_CONTROL_REG		0x0014
#define PCH_PHUB_DEADLOCK_AVOID_TYPE_REG	0x0018
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG0	0x0020
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG1	0x0024
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG2	0x0028
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG3	0x002C
#define PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE	0x0040
#define CLKCFG_REG_OFFSET			0x500
#define FUNCSEL_REG_OFFSET			0x508

#define PCH_PHUB_OROM_SIZE 15360

/**
 * struct pch_phub_reg - PHUB register structure
 * @phub_id_reg:			PHUB_ID register val
 * @q_pri_val_reg:			QUEUE_PRI_VAL register val
 * @rc_q_maxsize_reg:			RC_QUEUE_MAXSIZE register val
 * @bri_q_maxsize_reg:			BRI_QUEUE_MAXSIZE register val
 * @comp_resp_timeout_reg:		COMP_RESP_TIMEOUT register val
 * @bus_slave_control_reg:		BUS_SLAVE_CONTROL_REG register val
 * @deadlock_avoid_type_reg:		DEADLOCK_AVOID_TYPE register val
 * @intpin_reg_wpermit_reg0:		INTPIN_REG_WPERMIT register 0 val
 * @intpin_reg_wpermit_reg1:		INTPIN_REG_WPERMIT register 1 val
 * @intpin_reg_wpermit_reg2:		INTPIN_REG_WPERMIT register 2 val
 * @intpin_reg_wpermit_reg3:		INTPIN_REG_WPERMIT register 3 val
 * @int_reduce_control_reg:		INT_REDUCE_CONTROL registers val
 * @clkcfg_reg:				CLK CFG register val
 * @funcsel_reg:			Function select register value
 * @pch_phub_base_address:		Register base address
 * @pch_phub_extrom_base_address:	external rom base address
 * @pch_mac_start_address:		MAC address area start address
 * @pch_opt_rom_start_address:		Option ROM start address
 * @ioh_type:				Save IOH type
 */
struct pch_phub_reg {
	u32 phub_id_reg;
	u32 q_pri_val_reg;
	u32 rc_q_maxsize_reg;
	u32 bri_q_maxsize_reg;
	u32 comp_resp_timeout_reg;
	u32 bus_slave_control_reg;
	u32 deadlock_avoid_type_reg;
	u32 intpin_reg_wpermit_reg0;
	u32 intpin_reg_wpermit_reg1;
	u32 intpin_reg_wpermit_reg2;
	u32 intpin_reg_wpermit_reg3;
	u32 int_reduce_control_reg[MAX_NUM_INT_REDUCE_CONTROL_REG];
	u32 clkcfg_reg;
	u32 funcsel_reg;
	void __iomem *pch_phub_base_address;
	void __iomem *pch_phub_extrom_base_address;
	u32 pch_mac_start_address;
	u32 pch_opt_rom_start_address;
	int ioh_type;
};

/* SROM SPEC for MAC address assignment offset */
static const int pch_phub_mac_offset[ETH_ALEN] = {0x3, 0x2, 0x1, 0x0, 0xb, 0xa};

static DEFINE_MUTEX(pch_phub_mutex);

/**
 * pch_phub_read_modify_write_reg() - Reading modifying and writing register
 * @reg_addr_offset:	Register offset address value.
 * @data:		Writing value.
 * @mask:		Mask value.
 */
static void pch_phub_read_modify_write_reg(struct pch_phub_reg *chip,
					   unsigned int reg_addr_offset,
					   unsigned int data, unsigned int mask)
{
	void __iomem *reg_addr = chip->pch_phub_base_address + reg_addr_offset;
	iowrite32(((ioread32(reg_addr) & ~mask)) | data, reg_addr);
}

/* pch_phub_save_reg_conf - saves register configuration */
static void pch_phub_save_reg_conf(struct pci_dev *pdev)
{
	unsigned int i;
	struct pch_phub_reg *chip = pci_get_drvdata(pdev);

	void __iomem *p = chip->pch_phub_base_address;

	chip->phub_id_reg = ioread32(p + PCH_PHUB_ID_REG);
	chip->q_pri_val_reg = ioread32(p + PCH_PHUB_QUEUE_PRI_VAL_REG);
	chip->rc_q_maxsize_reg = ioread32(p + PCH_PHUB_RC_QUEUE_MAXSIZE_REG);
	chip->bri_q_maxsize_reg = ioread32(p + PCH_PHUB_BRI_QUEUE_MAXSIZE_REG);
	chip->comp_resp_timeout_reg =
				ioread32(p + PCH_PHUB_COMP_RESP_TIMEOUT_REG);
	chip->bus_slave_control_reg =
				ioread32(p + PCH_PHUB_BUS_SLAVE_CONTROL_REG);
	chip->deadlock_avoid_type_reg =
				ioread32(p + PCH_PHUB_DEADLOCK_AVOID_TYPE_REG);
	chip->intpin_reg_wpermit_reg0 =
				ioread32(p + PCH_PHUB_INTPIN_REG_WPERMIT_REG0);
	chip->intpin_reg_wpermit_reg1 =
				ioread32(p + PCH_PHUB_INTPIN_REG_WPERMIT_REG1);
	chip->intpin_reg_wpermit_reg2 =
				ioread32(p + PCH_PHUB_INTPIN_REG_WPERMIT_REG2);
	chip->intpin_reg_wpermit_reg3 =
				ioread32(p + PCH_PHUB_INTPIN_REG_WPERMIT_REG3);
	dev_dbg(&pdev->dev, "%s : "
		"chip->phub_id_reg=%x, "
		"chip->q_pri_val_reg=%x, "
		"chip->rc_q_maxsize_reg=%x, "
		"chip->bri_q_maxsize_reg=%x, "
		"chip->comp_resp_timeout_reg=%x, "
		"chip->bus_slave_control_reg=%x, "
		"chip->deadlock_avoid_type_reg=%x, "
		"chip->intpin_reg_wpermit_reg0=%x, "
		"chip->intpin_reg_wpermit_reg1=%x, "
		"chip->intpin_reg_wpermit_reg2=%x, "
		"chip->intpin_reg_wpermit_reg3=%x\n", __func__,
		chip->phub_id_reg,
		chip->q_pri_val_reg,
		chip->rc_q_maxsize_reg,
		chip->bri_q_maxsize_reg,
		chip->comp_resp_timeout_reg,
		chip->bus_slave_control_reg,
		chip->deadlock_avoid_type_reg,
		chip->intpin_reg_wpermit_reg0,
		chip->intpin_reg_wpermit_reg1,
		chip->intpin_reg_wpermit_reg2,
		chip->intpin_reg_wpermit_reg3);
	for (i = 0; i < MAX_NUM_INT_REDUCE_CONTROL_REG; i++) {
		chip->int_reduce_control_reg[i] =
		    ioread32(p + PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE + 4 * i);
		dev_dbg(&pdev->dev, "%s : "
			"chip->int_reduce_control_reg[%d]=%x\n",
			__func__, i, chip->int_reduce_control_reg[i]);
	}
	chip->clkcfg_reg = ioread32(p + CLKCFG_REG_OFFSET);
	if ((chip->ioh_type == 2) || (chip->ioh_type == 4))
		chip->funcsel_reg = ioread32(p + FUNCSEL_REG_OFFSET);
}

/* pch_phub_restore_reg_conf - restore register configuration */
static void pch_phub_restore_reg_conf(struct pci_dev *pdev)
{
	unsigned int i;
	struct pch_phub_reg *chip = pci_get_drvdata(pdev);
	void __iomem *p;
	p = chip->pch_phub_base_address;

	iowrite32(chip->phub_id_reg, p + PCH_PHUB_ID_REG);
	iowrite32(chip->q_pri_val_reg, p + PCH_PHUB_QUEUE_PRI_VAL_REG);
	iowrite32(chip->rc_q_maxsize_reg, p + PCH_PHUB_RC_QUEUE_MAXSIZE_REG);
	iowrite32(chip->bri_q_maxsize_reg, p + PCH_PHUB_BRI_QUEUE_MAXSIZE_REG);
	iowrite32(chip->comp_resp_timeout_reg,
					p + PCH_PHUB_COMP_RESP_TIMEOUT_REG);
	iowrite32(chip->bus_slave_control_reg,
					p + PCH_PHUB_BUS_SLAVE_CONTROL_REG);
	iowrite32(chip->deadlock_avoid_type_reg,
					p + PCH_PHUB_DEADLOCK_AVOID_TYPE_REG);
	iowrite32(chip->intpin_reg_wpermit_reg0,
					p + PCH_PHUB_INTPIN_REG_WPERMIT_REG0);
	iowrite32(chip->intpin_reg_wpermit_reg1,
					p + PCH_PHUB_INTPIN_REG_WPERMIT_REG1);
	iowrite32(chip->intpin_reg_wpermit_reg2,
					p + PCH_PHUB_INTPIN_REG_WPERMIT_REG2);
	iowrite32(chip->intpin_reg_wpermit_reg3,
					p + PCH_PHUB_INTPIN_REG_WPERMIT_REG3);
	dev_dbg(&pdev->dev, "%s : "
		"chip->phub_id_reg=%x, "
		"chip->q_pri_val_reg=%x, "
		"chip->rc_q_maxsize_reg=%x, "
		"chip->bri_q_maxsize_reg=%x, "
		"chip->comp_resp_timeout_reg=%x, "
		"chip->bus_slave_control_reg=%x, "
		"chip->deadlock_avoid_type_reg=%x, "
		"chip->intpin_reg_wpermit_reg0=%x, "
		"chip->intpin_reg_wpermit_reg1=%x, "
		"chip->intpin_reg_wpermit_reg2=%x, "
		"chip->intpin_reg_wpermit_reg3=%x\n", __func__,
		chip->phub_id_reg,
		chip->q_pri_val_reg,
		chip->rc_q_maxsize_reg,
		chip->bri_q_maxsize_reg,
		chip->comp_resp_timeout_reg,
		chip->bus_slave_control_reg,
		chip->deadlock_avoid_type_reg,
		chip->intpin_reg_wpermit_reg0,
		chip->intpin_reg_wpermit_reg1,
		chip->intpin_reg_wpermit_reg2,
		chip->intpin_reg_wpermit_reg3);
	for (i = 0; i < MAX_NUM_INT_REDUCE_CONTROL_REG; i++) {
		iowrite32(chip->int_reduce_control_reg[i],
			p + PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE + 4 * i);
		dev_dbg(&pdev->dev, "%s : "
			"chip->int_reduce_control_reg[%d]=%x\n",
			__func__, i, chip->int_reduce_control_reg[i]);
	}

	iowrite32(chip->clkcfg_reg, p + CLKCFG_REG_OFFSET);
	if ((chip->ioh_type == 2) || (chip->ioh_type == 4))
		iowrite32(chip->funcsel_reg, p + FUNCSEL_REG_OFFSET);
}

/**
 * pch_phub_read_serial_rom() - Reading Serial ROM
 * @offset_address:	Serial ROM offset address to read.
 * @data:		Read buffer for specified Serial ROM value.
 */
static void pch_phub_read_serial_rom(struct pch_phub_reg *chip,
				     unsigned int offset_address, u8 *data)
{
	void __iomem *mem_addr = chip->pch_phub_extrom_base_address +
								offset_address;

	*data = ioread8(mem_addr);
}

/**
 * pch_phub_write_serial_rom() - Writing Serial ROM
 * @offset_address:	Serial ROM offset address.
 * @data:		Serial ROM value to write.
 */
static int pch_phub_write_serial_rom(struct pch_phub_reg *chip,
				     unsigned int offset_address, u8 data)
{
	void __iomem *mem_addr = chip->pch_phub_extrom_base_address +
					(offset_address & PCH_WORD_ADDR_MASK);
	int i;
	unsigned int word_data;
	unsigned int pos;
	unsigned int mask;
	pos = (offset_address % 4) * 8;
	mask = ~(0xFF << pos);

	iowrite32(PCH_PHUB_ROM_WRITE_ENABLE,
			chip->pch_phub_extrom_base_address + PHUB_CONTROL);

	word_data = ioread32(mem_addr);
	iowrite32((word_data & mask) | (u32)data << pos, mem_addr);

	i = 0;
	while (ioread8(chip->pch_phub_extrom_base_address +
						PHUB_STATUS) != 0x00) {
		msleep(1);
		if (i == PHUB_TIMEOUT)
			return -ETIMEDOUT;
		i++;
	}

	iowrite32(PCH_PHUB_ROM_WRITE_DISABLE,
			chip->pch_phub_extrom_base_address + PHUB_CONTROL);

	return 0;
}

/**
 * pch_phub_read_serial_rom_val() - Read Serial ROM value
 * @offset_address:	Serial ROM address offset value.
 * @data:		Serial ROM value to read.
 */
static void pch_phub_read_serial_rom_val(struct pch_phub_reg *chip,
					 unsigned int offset_address, u8 *data)
{
	unsigned int mem_addr;

	mem_addr = chip->pch_mac_start_address +
			pch_phub_mac_offset[offset_address];

	pch_phub_read_serial_rom(chip, mem_addr, data);
}

/**
 * pch_phub_write_serial_rom_val() - writing Serial ROM value
 * @offset_address:	Serial ROM address offset value.
 * @data:		Serial ROM value.
 */
static int pch_phub_write_serial_rom_val(struct pch_phub_reg *chip,
					 unsigned int offset_address, u8 data)
{
	int retval;
	unsigned int mem_addr;

	mem_addr = chip->pch_mac_start_address +
			pch_phub_mac_offset[offset_address];

	retval = pch_phub_write_serial_rom(chip, mem_addr, data);

	return retval;
}

/* pch_phub_gbe_serial_rom_conf - makes Serial ROM header format configuration
 * for Gigabit Ethernet MAC address
 */
static int pch_phub_gbe_serial_rom_conf(struct pch_phub_reg *chip)
{
	int retval;

	retval = pch_phub_write_serial_rom(chip, 0x0b, 0xbc);
	retval |= pch_phub_write_serial_rom(chip, 0x0a, 0x10);
	retval |= pch_phub_write_serial_rom(chip, 0x09, 0x01);
	retval |= pch_phub_write_serial_rom(chip, 0x08, 0x02);

	retval |= pch_phub_write_serial_rom(chip, 0x0f, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x0e, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x0d, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x0c, 0x80);

	retval |= pch_phub_write_serial_rom(chip, 0x13, 0xbc);
	retval |= pch_phub_write_serial_rom(chip, 0x12, 0x10);
	retval |= pch_phub_write_serial_rom(chip, 0x11, 0x01);
	retval |= pch_phub_write_serial_rom(chip, 0x10, 0x18);

	retval |= pch_phub_write_serial_rom(chip, 0x1b, 0xbc);
	retval |= pch_phub_write_serial_rom(chip, 0x1a, 0x10);
	retval |= pch_phub_write_serial_rom(chip, 0x19, 0x01);
	retval |= pch_phub_write_serial_rom(chip, 0x18, 0x19);

	retval |= pch_phub_write_serial_rom(chip, 0x23, 0xbc);
	retval |= pch_phub_write_serial_rom(chip, 0x22, 0x10);
	retval |= pch_phub_write_serial_rom(chip, 0x21, 0x01);
	retval |= pch_phub_write_serial_rom(chip, 0x20, 0x3a);

	retval |= pch_phub_write_serial_rom(chip, 0x27, 0x01);
	retval |= pch_phub_write_serial_rom(chip, 0x26, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x25, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x24, 0x00);

	return retval;
}

/* pch_phub_gbe_serial_rom_conf_mp - makes SerialROM header format configuration
 * for Gigabit Ethernet MAC address
 */
static int pch_phub_gbe_serial_rom_conf_mp(struct pch_phub_reg *chip)
{
	int retval;
	u32 offset_addr;

	offset_addr = 0x200;
	retval = pch_phub_write_serial_rom(chip, 0x03 + offset_addr, 0xbc);
	retval |= pch_phub_write_serial_rom(chip, 0x02 + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x01 + offset_addr, 0x40);
	retval |= pch_phub_write_serial_rom(chip, 0x00 + offset_addr, 0x02);

	retval |= pch_phub_write_serial_rom(chip, 0x07 + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x06 + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x05 + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x04 + offset_addr, 0x80);

	retval |= pch_phub_write_serial_rom(chip, 0x0b + offset_addr, 0xbc);
	retval |= pch_phub_write_serial_rom(chip, 0x0a + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x09 + offset_addr, 0x40);
	retval |= pch_phub_write_serial_rom(chip, 0x08 + offset_addr, 0x18);

	retval |= pch_phub_write_serial_rom(chip, 0x13 + offset_addr, 0xbc);
	retval |= pch_phub_write_serial_rom(chip, 0x12 + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x11 + offset_addr, 0x40);
	retval |= pch_phub_write_serial_rom(chip, 0x10 + offset_addr, 0x19);

	retval |= pch_phub_write_serial_rom(chip, 0x1b + offset_addr, 0xbc);
	retval |= pch_phub_write_serial_rom(chip, 0x1a + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x19 + offset_addr, 0x40);
	retval |= pch_phub_write_serial_rom(chip, 0x18 + offset_addr, 0x3a);

	retval |= pch_phub_write_serial_rom(chip, 0x1f + offset_addr, 0x01);
	retval |= pch_phub_write_serial_rom(chip, 0x1e + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x1d + offset_addr, 0x00);
	retval |= pch_phub_write_serial_rom(chip, 0x1c + offset_addr, 0x00);

	return retval;
}

/**
 * pch_phub_read_gbe_mac_addr() - Read Gigabit Ethernet MAC address
 * @offset_address:	Gigabit Ethernet MAC address offset value.
 * @data:		Buffer of the Gigabit Ethernet MAC address value.
 */
static void pch_phub_read_gbe_mac_addr(struct pch_phub_reg *chip, u8 *data)
{
	int i;
	for (i = 0; i < ETH_ALEN; i++)
		pch_phub_read_serial_rom_val(chip, i, &data[i]);
}

/**
 * pch_phub_write_gbe_mac_addr() - Write MAC address
 * @offset_address:	Gigabit Ethernet MAC address offset value.
 * @data:		Gigabit Ethernet MAC address value.
 */
static int pch_phub_write_gbe_mac_addr(struct pch_phub_reg *chip, u8 *data)
{
	int retval;
	int i;

	if (chip->ioh_type == 1) /* EG20T */
		retval = pch_phub_gbe_serial_rom_conf(chip);
	else	/* ML7223 */
		retval = pch_phub_gbe_serial_rom_conf_mp(chip);
	if (retval)
		return retval;

	for (i = 0; i < ETH_ALEN; i++) {
		retval = pch_phub_write_serial_rom_val(chip, i, data[i]);
		if (retval)
			return retval;
	}

	return retval;
}

static ssize_t pch_phub_bin_read(struct file *filp, struct kobject *kobj,
				 struct bin_attribute *attr, char *buf,
				 loff_t off, size_t count)
{
	unsigned int rom_signature;
	unsigned char rom_length;
	unsigned int tmp;
	unsigned int addr_offset;
	unsigned int orom_size;
	int ret;
	int err;

	struct pch_phub_reg *chip =
		dev_get_drvdata(container_of(kobj, struct device, kobj));

	ret = mutex_lock_interruptible(&pch_phub_mutex);
	if (ret) {
		err = -ERESTARTSYS;
		goto return_err_nomutex;
	}

	/* Get Rom signature */
	pch_phub_read_serial_rom(chip, chip->pch_opt_rom_start_address,
				(unsigned char *)&rom_signature);
	rom_signature &= 0xff;
	pch_phub_read_serial_rom(chip, chip->pch_opt_rom_start_address + 1,
				(unsigned char *)&tmp);
	rom_signature |= (tmp & 0xff) << 8;
	if (rom_signature == 0xAA55) {
		pch_phub_read_serial_rom(chip,
					 chip->pch_opt_rom_start_address + 2,
					 &rom_length);
		orom_size = rom_length * 512;
		if (orom_size < off) {
			addr_offset = 0;
			goto return_ok;
		}
		if (orom_size < count) {
			addr_offset = 0;
			goto return_ok;
		}

		for (addr_offset = 0; addr_offset < count; addr_offset++) {
			pch_phub_read_serial_rom(chip,
			    chip->pch_opt_rom_start_address + addr_offset + off,
			    &buf[addr_offset]);
		}
	} else {
		err = -ENODATA;
		goto return_err;
	}
return_ok:
	mutex_unlock(&pch_phub_mutex);
	return addr_offset;

return_err:
	mutex_unlock(&pch_phub_mutex);
return_err_nomutex:
	return err;
}

static ssize_t pch_phub_bin_write(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	int err;
	unsigned int addr_offset;
	int ret;
	struct pch_phub_reg *chip =
		dev_get_drvdata(container_of(kobj, struct device, kobj));

	ret = mutex_lock_interruptible(&pch_phub_mutex);
	if (ret)
		return -ERESTARTSYS;

	if (off > PCH_PHUB_OROM_SIZE) {
		addr_offset = 0;
		goto return_ok;
	}
	if (count > PCH_PHUB_OROM_SIZE) {
		addr_offset = 0;
		goto return_ok;
	}

	for (addr_offset = 0; addr_offset < count; addr_offset++) {
		if (PCH_PHUB_OROM_SIZE < off + addr_offset)
			goto return_ok;

		ret = pch_phub_write_serial_rom(chip,
			    chip->pch_opt_rom_start_address + addr_offset + off,
			    buf[addr_offset]);
		if (ret) {
			err = ret;
			goto return_err;
		}
	}

return_ok:
	mutex_unlock(&pch_phub_mutex);
	return addr_offset;

return_err:
	mutex_unlock(&pch_phub_mutex);
	return err;
}

static ssize_t show_pch_mac(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	u8 mac[8];
	struct pch_phub_reg *chip = dev_get_drvdata(dev);

	pch_phub_read_gbe_mac_addr(chip, mac);

	return sprintf(buf, "%pM\n", mac);
}

static ssize_t store_pch_mac(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	u8 mac[6];
	struct pch_phub_reg *chip = dev_get_drvdata(dev);

	if (count != 18)
		return -EINVAL;

	sscanf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
		(u32 *)&mac[0], (u32 *)&mac[1], (u32 *)&mac[2], (u32 *)&mac[3],
		(u32 *)&mac[4], (u32 *)&mac[5]);

	pch_phub_write_gbe_mac_addr(chip, mac);

	return count;
}

static DEVICE_ATTR(pch_mac, S_IRUGO | S_IWUSR, show_pch_mac, store_pch_mac);

static struct bin_attribute pch_bin_attr = {
	.attr = {
		.name = "pch_firmware",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = PCH_PHUB_OROM_SIZE + 1,
	.read = pch_phub_bin_read,
	.write = pch_phub_bin_write,
};

static int __devinit pch_phub_probe(struct pci_dev *pdev,
				    const struct pci_device_id *id)
{
	int retval;

	int ret;
	ssize_t rom_size;
	struct pch_phub_reg *chip;

	chip = kzalloc(sizeof(struct pch_phub_reg), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev,
		"%s : pci_enable_device FAILED(ret=%d)", __func__, ret);
		goto err_pci_enable_dev;
	}
	dev_dbg(&pdev->dev, "%s : pci_enable_device returns %d\n", __func__,
		ret);

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev,
		"%s : pci_request_regions FAILED(ret=%d)", __func__, ret);
		goto err_req_regions;
	}
	dev_dbg(&pdev->dev, "%s : "
		"pci_request_regions returns %d\n", __func__, ret);

	chip->pch_phub_base_address = pci_iomap(pdev, 1, 0);


	if (chip->pch_phub_base_address == 0) {
		dev_err(&pdev->dev, "%s : pci_iomap FAILED", __func__);
		ret = -ENOMEM;
		goto err_pci_iomap;
	}
	dev_dbg(&pdev->dev, "%s : pci_iomap SUCCESS and value "
		"in pch_phub_base_address variable is %p\n", __func__,
		chip->pch_phub_base_address);

	if (id->driver_data != 3) {
		chip->pch_phub_extrom_base_address =\
						   pci_map_rom(pdev, &rom_size);
		if (chip->pch_phub_extrom_base_address == 0) {
			dev_err(&pdev->dev, "%s: pci_map_rom FAILED", __func__);
			ret = -ENOMEM;
			goto err_pci_map;
		}
		dev_dbg(&pdev->dev, "%s : "
			"pci_map_rom SUCCESS and value in "
			"pch_phub_extrom_base_address variable is %p\n",
			__func__, chip->pch_phub_extrom_base_address);
	}

	if (id->driver_data == 1) { /* EG20T PCH */
		const char *board_name;

		retval = sysfs_create_file(&pdev->dev.kobj,
					   &dev_attr_pch_mac.attr);
		if (retval)
			goto err_sysfs_create;

		retval = sysfs_create_bin_file(&pdev->dev.kobj, &pch_bin_attr);
		if (retval)
			goto exit_bin_attr;

		pch_phub_read_modify_write_reg(chip,
					       (unsigned int)CLKCFG_REG_OFFSET,
					       CLKCFG_CAN_50MHZ,
					       CLKCFG_CANCLK_MASK);

		/* quirk for CM-iTC board */
		board_name = dmi_get_system_info(DMI_BOARD_NAME);
		if (board_name && strstr(board_name, "CM-iTC"))
			pch_phub_read_modify_write_reg(chip,
						(unsigned int)CLKCFG_REG_OFFSET,
						CLKCFG_UART_48MHZ | CLKCFG_BAUDDIV |
						CLKCFG_PLL2VCO | CLKCFG_UARTCLKSEL,
						CLKCFG_UART_MASK);

		/* set the prefech value */
		iowrite32(0x000affaa, chip->pch_phub_base_address + 0x14);
		/* set the interrupt delay value */
		iowrite32(0x25, chip->pch_phub_base_address + 0x44);
		chip->pch_opt_rom_start_address = PCH_PHUB_ROM_START_ADDR_EG20T;
		chip->pch_mac_start_address = PCH_PHUB_MAC_START_ADDR_EG20T;
	} else if (id->driver_data == 2) { /* ML7213 IOH */
		retval = sysfs_create_bin_file(&pdev->dev.kobj, &pch_bin_attr);
		if (retval)
			goto err_sysfs_create;
		/* set the prefech value
		 * Device2(USB OHCI #1/ USB EHCI #1/ USB Device):a
		 * Device4(SDIO #0,1,2):f
		 * Device6(SATA 2):f
		 * Device8(USB OHCI #0/ USB EHCI #0):a
		 */
		iowrite32(0x000affa0, chip->pch_phub_base_address + 0x14);
		chip->pch_opt_rom_start_address =\
						 PCH_PHUB_ROM_START_ADDR_ML7213;
	} else if (id->driver_data == 3) { /* ML7223 IOH Bus-m*/
		/* set the prefech value
		 * Device8(GbE)
		 */
		iowrite32(0x000a0000, chip->pch_phub_base_address + 0x14);
		/* set the interrupt delay value */
		iowrite32(0x25, chip->pch_phub_base_address + 0x140);
		chip->pch_opt_rom_start_address =\
						 PCH_PHUB_ROM_START_ADDR_ML7223;
		chip->pch_mac_start_address = PCH_PHUB_MAC_START_ADDR_ML7223;
	} else if (id->driver_data == 4) { /* ML7223 IOH Bus-n*/
		retval = sysfs_create_file(&pdev->dev.kobj,
					   &dev_attr_pch_mac.attr);
		if (retval)
			goto err_sysfs_create;
		retval = sysfs_create_bin_file(&pdev->dev.kobj, &pch_bin_attr);
		if (retval)
			goto exit_bin_attr;
		/* set the prefech value
		 * Device2(USB OHCI #0,1,2,3/ USB EHCI #0):a
		 * Device4(SDIO #0,1):f
		 * Device6(SATA 2):f
		 */
		iowrite32(0x0000ffa0, chip->pch_phub_base_address + 0x14);
		chip->pch_opt_rom_start_address =\
						 PCH_PHUB_ROM_START_ADDR_ML7223;
		chip->pch_mac_start_address = PCH_PHUB_MAC_START_ADDR_ML7223;
	}

	chip->ioh_type = id->driver_data;
	pci_set_drvdata(pdev, chip);

	return 0;
exit_bin_attr:
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_pch_mac.attr);

err_sysfs_create:
	pci_unmap_rom(pdev, chip->pch_phub_extrom_base_address);
err_pci_map:
	pci_iounmap(pdev, chip->pch_phub_base_address);
err_pci_iomap:
	pci_release_regions(pdev);
err_req_regions:
	pci_disable_device(pdev);
err_pci_enable_dev:
	kfree(chip);
	dev_err(&pdev->dev, "%s returns %d\n", __func__, ret);
	return ret;
}

static void __devexit pch_phub_remove(struct pci_dev *pdev)
{
	struct pch_phub_reg *chip = pci_get_drvdata(pdev);

	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_pch_mac.attr);
	sysfs_remove_bin_file(&pdev->dev.kobj, &pch_bin_attr);
	pci_unmap_rom(pdev, chip->pch_phub_extrom_base_address);
	pci_iounmap(pdev, chip->pch_phub_base_address);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(chip);
}

#ifdef CONFIG_PM

static int pch_phub_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int ret;

	pch_phub_save_reg_conf(pdev);
	ret = pci_save_state(pdev);
	if (ret) {
		dev_err(&pdev->dev,
			" %s -pci_save_state returns %d\n", __func__, ret);
		return ret;
	}
	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int pch_phub_resume(struct pci_dev *pdev)
{
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev,
		"%s-pci_enable_device failed(ret=%d) ", __func__, ret);
		return ret;
	}

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pch_phub_restore_reg_conf(pdev);

	return 0;
}
#else
#define pch_phub_suspend NULL
#define pch_phub_resume NULL
#endif /* CONFIG_PM */

static struct pci_device_id pch_phub_pcidev_id[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_PCH1_PHUB),       1,  },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ROHM_ML7213_PHUB), 2,  },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ROHM_ML7223_mPHUB), 3,  },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ROHM_ML7223_nPHUB), 4,  },
	{ }
};
MODULE_DEVICE_TABLE(pci, pch_phub_pcidev_id);

static struct pci_driver pch_phub_driver = {
	.name = "pch_phub",
	.id_table = pch_phub_pcidev_id,
	.probe = pch_phub_probe,
	.remove = __devexit_p(pch_phub_remove),
	.suspend = pch_phub_suspend,
	.resume = pch_phub_resume
};

static int __init pch_phub_pci_init(void)
{
	return pci_register_driver(&pch_phub_driver);
}

static void __exit pch_phub_pci_exit(void)
{
	pci_unregister_driver(&pch_phub_driver);
}

module_init(pch_phub_pci_init);
module_exit(pch_phub_pci_exit);

MODULE_DESCRIPTION("Intel EG20T PCH/OKI SEMICONDUCTOR IOH(ML7213/ML7223) PHUB");
MODULE_LICENSE("GPL");
