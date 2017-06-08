/*
 * Copyright (c) 2006 - 2011 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>

#include "nes.h"

static u16 nes_read16_eeprom(void __iomem *addr, u16 offset);

u32 mh_detected;
u32 mh_pauses_sent;

static u32 nes_set_pau(struct nes_device *nesdev)
{
	u32 ret = 0;
	u32 counter;

	nes_write_indexed(nesdev, NES_IDX_GPR2, NES_ENABLE_PAU);
	nes_write_indexed(nesdev, NES_IDX_GPR_TRIGGER, 1);

	for (counter = 0; counter < NES_PAU_COUNTER; counter++) {
		udelay(30);
		if (!nes_read_indexed(nesdev, NES_IDX_GPR2)) {
			printk(KERN_INFO PFX "PAU is supported.\n");
			break;
		}
		nes_write_indexed(nesdev, NES_IDX_GPR_TRIGGER, 1);
	}
	if (counter == NES_PAU_COUNTER) {
		printk(KERN_INFO PFX "PAU is not supported.\n");
		return -EPERM;
	}
	return ret;
}

/**
 * nes_read_eeprom_values -
 */
int nes_read_eeprom_values(struct nes_device *nesdev, struct nes_adapter *nesadapter)
{
	u32 mac_addr_low;
	u16 mac_addr_high;
	u16 eeprom_data;
	u16 eeprom_offset;
	u16 next_section_address;
	u16 sw_section_ver;
	u8  major_ver = 0;
	u8  minor_ver = 0;

	/* TODO: deal with EEPROM endian issues */
	if (nesadapter->firmware_eeprom_offset == 0) {
		/* Read the EEPROM Parameters */
		eeprom_data = nes_read16_eeprom(nesdev->regs, 0);
		nes_debug(NES_DBG_HW, "EEPROM Offset 0  = 0x%04X\n", eeprom_data);
		eeprom_offset = 2 + (((eeprom_data & 0x007f) << 3) <<
				((eeprom_data & 0x0080) >> 7));
		nes_debug(NES_DBG_HW, "Firmware Offset = 0x%04X\n", eeprom_offset);
		nesadapter->firmware_eeprom_offset = eeprom_offset;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 4);
		if (eeprom_data != 0x5746) {
			nes_debug(NES_DBG_HW, "Not a valid Firmware Image = 0x%04X\n", eeprom_data);
			return -1;
		}

		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 2);
		nes_debug(NES_DBG_HW, "EEPROM Offset %u  = 0x%04X\n",
				eeprom_offset + 2, eeprom_data);
		eeprom_offset += ((eeprom_data & 0x00ff) << 3) << ((eeprom_data & 0x0100) >> 8);
		nes_debug(NES_DBG_HW, "Software Offset = 0x%04X\n", eeprom_offset);
		nesadapter->software_eeprom_offset = eeprom_offset;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 4);
		if (eeprom_data != 0x5753) {
			printk("Not a valid Software Image = 0x%04X\n", eeprom_data);
			return -1;
		}
		sw_section_ver = nes_read16_eeprom(nesdev->regs, nesadapter->software_eeprom_offset  + 6);
		nes_debug(NES_DBG_HW, "Software section version number = 0x%04X\n",
				sw_section_ver);

		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 2);
		nes_debug(NES_DBG_HW, "EEPROM Offset %u (next section)  = 0x%04X\n",
				eeprom_offset + 2, eeprom_data);
		next_section_address = eeprom_offset + (((eeprom_data & 0x00ff) << 3) <<
				((eeprom_data & 0x0100) >> 8));
		eeprom_data = nes_read16_eeprom(nesdev->regs, next_section_address + 4);
		if (eeprom_data != 0x414d) {
			nes_debug(NES_DBG_HW, "EEPROM Changed offset should be 0x414d but was 0x%04X\n",
					eeprom_data);
			goto no_fw_rev;
		}
		eeprom_offset = next_section_address;

		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 2);
		nes_debug(NES_DBG_HW, "EEPROM Offset %u (next section)  = 0x%04X\n",
				eeprom_offset + 2, eeprom_data);
		next_section_address = eeprom_offset + (((eeprom_data & 0x00ff) << 3) <<
				((eeprom_data & 0x0100) >> 8));
		eeprom_data = nes_read16_eeprom(nesdev->regs, next_section_address + 4);
		if (eeprom_data != 0x4f52) {
			nes_debug(NES_DBG_HW, "EEPROM Changed offset should be 0x4f52 but was 0x%04X\n",
					eeprom_data);
			goto no_fw_rev;
		}
		eeprom_offset = next_section_address;

		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 2);
		nes_debug(NES_DBG_HW, "EEPROM Offset %u (next section)  = 0x%04X\n",
				eeprom_offset + 2, eeprom_data);
		next_section_address = eeprom_offset + ((eeprom_data & 0x00ff) << 3);
		eeprom_data = nes_read16_eeprom(nesdev->regs, next_section_address + 4);
		if (eeprom_data != 0x5746) {
			nes_debug(NES_DBG_HW, "EEPROM Changed offset should be 0x5746 but was 0x%04X\n",
					eeprom_data);
			goto no_fw_rev;
		}
		eeprom_offset = next_section_address;

		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 2);
		nes_debug(NES_DBG_HW, "EEPROM Offset %u (next section)  = 0x%04X\n",
				eeprom_offset + 2, eeprom_data);
		next_section_address = eeprom_offset + ((eeprom_data & 0x00ff) << 3);
		eeprom_data = nes_read16_eeprom(nesdev->regs, next_section_address + 4);
		if (eeprom_data != 0x5753) {
			nes_debug(NES_DBG_HW, "EEPROM Changed offset should be 0x5753 but was 0x%04X\n",
					eeprom_data);
			goto no_fw_rev;
		}
		eeprom_offset = next_section_address;

		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 2);
		nes_debug(NES_DBG_HW, "EEPROM Offset %u (next section)  = 0x%04X\n",
				eeprom_offset + 2, eeprom_data);
		next_section_address = eeprom_offset + ((eeprom_data & 0x00ff) << 3);
		eeprom_data = nes_read16_eeprom(nesdev->regs, next_section_address + 4);
		if (eeprom_data != 0x414d) {
			nes_debug(NES_DBG_HW, "EEPROM Changed offset should be 0x414d but was 0x%04X\n",
					eeprom_data);
			goto no_fw_rev;
		}
		eeprom_offset = next_section_address;

		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset + 2);
		nes_debug(NES_DBG_HW, "EEPROM Offset %u (next section)  = 0x%04X\n",
				eeprom_offset + 2, eeprom_data);
		next_section_address = eeprom_offset + ((eeprom_data & 0x00ff) << 3);
		eeprom_data = nes_read16_eeprom(nesdev->regs, next_section_address + 4);
		if (eeprom_data != 0x464e) {
			nes_debug(NES_DBG_HW, "EEPROM Changed offset should be 0x464e but was 0x%04X\n",
					eeprom_data);
			goto no_fw_rev;
		}
		eeprom_data = nes_read16_eeprom(nesdev->regs, next_section_address + 8);
		printk(PFX "Firmware version %u.%u\n", (u8)(eeprom_data>>8), (u8)eeprom_data);
		major_ver = (u8)(eeprom_data >> 8);
		minor_ver = (u8)(eeprom_data);

		if (nes_drv_opt & NES_DRV_OPT_DISABLE_VIRT_WQ) {
			nes_debug(NES_DBG_HW, "Virtual WQs have been disabled\n");
		} else if (((major_ver == 2) && (minor_ver > 21)) || ((major_ver > 2) && (major_ver != 255))) {
			nesadapter->virtwq = 1;
		}
		if (((major_ver == 3) && (minor_ver >= 16)) || (major_ver > 3))
			nesadapter->send_term_ok = 1;

		if (nes_drv_opt & NES_DRV_OPT_ENABLE_PAU) {
			if (!nes_set_pau(nesdev))
				nesadapter->allow_unaligned_fpdus = 1;
		}

		nesadapter->firmware_version = (((u32)(u8)(eeprom_data>>8))  <<  16) +
				(u32)((u8)eeprom_data);

		eeprom_data = nes_read16_eeprom(nesdev->regs, next_section_address + 10);
		printk(PFX "EEPROM version %u.%u\n", (u8)(eeprom_data>>8), (u8)eeprom_data);
		nesadapter->eeprom_version = (((u32)(u8)(eeprom_data>>8)) << 16) +
				(u32)((u8)eeprom_data);

no_fw_rev:
		/* eeprom is valid */
		eeprom_offset = nesadapter->software_eeprom_offset;
		eeprom_offset += 8;
		nesadapter->netdev_max = (u8)nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		mac_addr_high = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		mac_addr_low = (u32)nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		mac_addr_low <<= 16;
		mac_addr_low += (u32)nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "Base MAC Address = 0x%04X%08X\n",
				mac_addr_high, mac_addr_low);
		nes_debug(NES_DBG_HW, "MAC Address count = %u\n", nesadapter->netdev_max);

		nesadapter->mac_addr_low = mac_addr_low;
		nesadapter->mac_addr_high = mac_addr_high;

		/* Read the Phy Type array */
		eeprom_offset += 10;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->phy_type[0] = (u8)(eeprom_data >> 8);
		nesadapter->phy_type[1] = (u8)eeprom_data;

		/* Read the port array */
		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->phy_type[2] = (u8)(eeprom_data >> 8);
		nesadapter->phy_type[3] = (u8)eeprom_data;
		/* port_count is set by soft reset reg */
		nes_debug(NES_DBG_HW, "port_count = %u, port 0 -> %u, port 1 -> %u,"
				" port 2 -> %u, port 3 -> %u\n",
				nesadapter->port_count,
				nesadapter->phy_type[0], nesadapter->phy_type[1],
				nesadapter->phy_type[2], nesadapter->phy_type[3]);

		/* Read PD config array */
		eeprom_offset += 10;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->pd_config_size[0] = eeprom_data;
		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->pd_config_base[0] = eeprom_data;
		nes_debug(NES_DBG_HW, "PD0 config, size=0x%04x, base=0x%04x\n",
				nesadapter->pd_config_size[0], nesadapter->pd_config_base[0]);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->pd_config_size[1] = eeprom_data;
		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->pd_config_base[1] = eeprom_data;
		nes_debug(NES_DBG_HW, "PD1 config, size=0x%04x, base=0x%04x\n",
				nesadapter->pd_config_size[1], nesadapter->pd_config_base[1]);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->pd_config_size[2] = eeprom_data;
		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->pd_config_base[2] = eeprom_data;
		nes_debug(NES_DBG_HW, "PD2 config, size=0x%04x, base=0x%04x\n",
				nesadapter->pd_config_size[2], nesadapter->pd_config_base[2]);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->pd_config_size[3] = eeprom_data;
		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nesadapter->pd_config_base[3] = eeprom_data;
		nes_debug(NES_DBG_HW, "PD3 config, size=0x%04x, base=0x%04x\n",
				nesadapter->pd_config_size[3], nesadapter->pd_config_base[3]);

		/* Read Rx Pool Size */
		eeprom_offset += 22;   /* 46 */
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->rx_pool_size = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "rx_pool_size = 0x%08X\n", nesadapter->rx_pool_size);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->tx_pool_size = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "tx_pool_size = 0x%08X\n", nesadapter->tx_pool_size);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->rx_threshold = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "rx_threshold = 0x%08X\n", nesadapter->rx_threshold);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->tcp_timer_core_clk_divisor = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "tcp_timer_core_clk_divisor = 0x%08X\n",
				nesadapter->tcp_timer_core_clk_divisor);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->iwarp_config = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "iwarp_config = 0x%08X\n", nesadapter->iwarp_config);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->cm_config = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "cm_config = 0x%08X\n", nesadapter->cm_config);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->sws_timer_config = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "sws_timer_config = 0x%08X\n", nesadapter->sws_timer_config);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->tcp_config1 = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "tcp_config1 = 0x%08X\n", nesadapter->tcp_config1);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->wqm_wat = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "wqm_wat = 0x%08X\n", nesadapter->wqm_wat);

		eeprom_offset += 2;
		eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
		eeprom_offset += 2;
		nesadapter->core_clock = (((u32)eeprom_data) << 16) +
				nes_read16_eeprom(nesdev->regs, eeprom_offset);
		nes_debug(NES_DBG_HW, "core_clock = 0x%08X\n", nesadapter->core_clock);

		if ((sw_section_ver) && (nesadapter->hw_rev != NE020_REV)) {
			eeprom_offset += 2;
			eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
			nesadapter->phy_index[0] = (eeprom_data & 0xff00)>>8;
			nesadapter->phy_index[1] = eeprom_data & 0x00ff;
			eeprom_offset += 2;
			eeprom_data = nes_read16_eeprom(nesdev->regs, eeprom_offset);
			nesadapter->phy_index[2] = (eeprom_data & 0xff00)>>8;
			nesadapter->phy_index[3] = eeprom_data & 0x00ff;
		} else {
			nesadapter->phy_index[0] = 4;
			nesadapter->phy_index[1] = 5;
			nesadapter->phy_index[2] = 6;
			nesadapter->phy_index[3] = 7;
		}
		nes_debug(NES_DBG_HW, "Phy address map = 0 > %u,  1 > %u, 2 > %u, 3 > %u\n",
			   nesadapter->phy_index[0],nesadapter->phy_index[1],
			   nesadapter->phy_index[2],nesadapter->phy_index[3]);
	}

	return 0;
}


/**
 * nes_read16_eeprom
 */
static u16 nes_read16_eeprom(void __iomem *addr, u16 offset)
{
	writel(NES_EEPROM_READ_REQUEST + (offset >> 1),
			(void __iomem *)addr + NES_EEPROM_COMMAND);

	do {
	} while (readl((void __iomem *)addr + NES_EEPROM_COMMAND) &
			NES_EEPROM_READ_REQUEST);

	return readw((void __iomem *)addr + NES_EEPROM_DATA);
}


/**
 * nes_write_1G_phy_reg
 */
void nes_write_1G_phy_reg(struct nes_device *nesdev, u8 phy_reg, u8 phy_addr, u16 data)
{
	u32 u32temp;
	u32 counter;

	nes_write_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL,
			0x50020000 | data | ((u32)phy_reg << 18) | ((u32)phy_addr << 23));
	for (counter = 0; counter < 100 ; counter++) {
		udelay(30);
		u32temp = nes_read_indexed(nesdev, NES_IDX_MAC_INT_STATUS);
		if (u32temp & 1) {
			/* nes_debug(NES_DBG_PHY, "Phy interrupt status = 0x%X.\n", u32temp); */
			nes_write_indexed(nesdev, NES_IDX_MAC_INT_STATUS, 1);
			break;
		}
	}
	if (!(u32temp & 1))
		nes_debug(NES_DBG_PHY, "Phy is not responding. interrupt status = 0x%X.\n",
				u32temp);
}


/**
 * nes_read_1G_phy_reg
 * This routine only issues the read, the data must be read
 * separately.
 */
void nes_read_1G_phy_reg(struct nes_device *nesdev, u8 phy_reg, u8 phy_addr, u16 *data)
{
	u32 u32temp;
	u32 counter;

	/* nes_debug(NES_DBG_PHY, "phy addr = %d, mac_index = %d\n",
			phy_addr, nesdev->mac_index); */

	nes_write_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL,
			0x60020000 | ((u32)phy_reg << 18) | ((u32)phy_addr << 23));
	for (counter = 0; counter < 100 ; counter++) {
		udelay(30);
		u32temp = nes_read_indexed(nesdev, NES_IDX_MAC_INT_STATUS);
		if (u32temp & 1) {
			/* nes_debug(NES_DBG_PHY, "Phy interrupt status = 0x%X.\n", u32temp); */
			nes_write_indexed(nesdev, NES_IDX_MAC_INT_STATUS, 1);
			break;
		}
	}
	if (!(u32temp & 1)) {
		nes_debug(NES_DBG_PHY, "Phy is not responding. interrupt status = 0x%X.\n",
				u32temp);
		*data = 0xffff;
	} else {
		*data = (u16)nes_read_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL);
	}
}


/**
 * nes_write_10G_phy_reg
 */
void nes_write_10G_phy_reg(struct nes_device *nesdev, u16 phy_addr, u8 dev_addr, u16 phy_reg,
		u16 data)
{
	u32 port_addr;
	u32 u32temp;
	u32 counter;

	port_addr = phy_addr;

	/* set address */
	nes_write_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL,
			0x00020000 | (u32)phy_reg | (((u32)dev_addr) << 18) | (((u32)port_addr) << 23));
	for (counter = 0; counter < 100 ; counter++) {
		udelay(30);
		u32temp = nes_read_indexed(nesdev, NES_IDX_MAC_INT_STATUS);
		if (u32temp & 1) {
			nes_write_indexed(nesdev, NES_IDX_MAC_INT_STATUS, 1);
			break;
		}
	}
	if (!(u32temp & 1))
		nes_debug(NES_DBG_PHY, "Phy is not responding. interrupt status = 0x%X.\n",
				u32temp);

	/* set data */
	nes_write_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL,
			0x10020000 | (u32)data | (((u32)dev_addr) << 18) | (((u32)port_addr) << 23));
	for (counter = 0; counter < 100 ; counter++) {
		udelay(30);
		u32temp = nes_read_indexed(nesdev, NES_IDX_MAC_INT_STATUS);
		if (u32temp & 1) {
			nes_write_indexed(nesdev, NES_IDX_MAC_INT_STATUS, 1);
			break;
		}
	}
	if (!(u32temp & 1))
		nes_debug(NES_DBG_PHY, "Phy is not responding. interrupt status = 0x%X.\n",
				u32temp);
}


/**
 * nes_read_10G_phy_reg
 * This routine only issues the read, the data must be read
 * separately.
 */
void nes_read_10G_phy_reg(struct nes_device *nesdev, u8 phy_addr, u8 dev_addr, u16 phy_reg)
{
	u32 port_addr;
	u32 u32temp;
	u32 counter;

	port_addr = phy_addr;

	/* set address */
	nes_write_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL,
			0x00020000 | (u32)phy_reg | (((u32)dev_addr) << 18) | (((u32)port_addr) << 23));
	for (counter = 0; counter < 100 ; counter++) {
		udelay(30);
		u32temp = nes_read_indexed(nesdev, NES_IDX_MAC_INT_STATUS);
		if (u32temp & 1) {
			nes_write_indexed(nesdev, NES_IDX_MAC_INT_STATUS, 1);
			break;
		}
	}
	if (!(u32temp & 1))
		nes_debug(NES_DBG_PHY, "Phy is not responding. interrupt status = 0x%X.\n",
				u32temp);

	/* issue read */
	nes_write_indexed(nesdev, NES_IDX_MAC_MDIO_CONTROL,
			0x30020000 | (((u32)dev_addr) << 18) | (((u32)port_addr) << 23));
	for (counter = 0; counter < 100 ; counter++) {
		udelay(30);
		u32temp = nes_read_indexed(nesdev, NES_IDX_MAC_INT_STATUS);
		if (u32temp & 1) {
			nes_write_indexed(nesdev, NES_IDX_MAC_INT_STATUS, 1);
			break;
		}
	}
	if (!(u32temp & 1))
		nes_debug(NES_DBG_PHY, "Phy is not responding. interrupt status = 0x%X.\n",
				u32temp);
}


/**
 * nes_get_cqp_request
 */
struct nes_cqp_request *nes_get_cqp_request(struct nes_device *nesdev)
{
	unsigned long flags;
	struct nes_cqp_request *cqp_request = NULL;

	if (!list_empty(&nesdev->cqp_avail_reqs)) {
		spin_lock_irqsave(&nesdev->cqp.lock, flags);
		if (!list_empty(&nesdev->cqp_avail_reqs)) {
			cqp_request = list_entry(nesdev->cqp_avail_reqs.next,
				struct nes_cqp_request, list);
			list_del_init(&cqp_request->list);
		}
		spin_unlock_irqrestore(&nesdev->cqp.lock, flags);
	}
	if (cqp_request == NULL) {
		cqp_request = kzalloc(sizeof(struct nes_cqp_request), GFP_ATOMIC);
		if (cqp_request) {
			cqp_request->dynamic = 1;
			INIT_LIST_HEAD(&cqp_request->list);
		}
	}

	if (cqp_request) {
		init_waitqueue_head(&cqp_request->waitq);
		cqp_request->waiting = 0;
		cqp_request->request_done = 0;
		cqp_request->callback = 0;
		init_waitqueue_head(&cqp_request->waitq);
		nes_debug(NES_DBG_CQP, "Got cqp request %p from the available list \n",
				cqp_request);
	} else
		printk(KERN_ERR PFX "%s: Could not allocated a CQP request.\n",
			   __func__);

	return cqp_request;
}

void nes_free_cqp_request(struct nes_device *nesdev,
			  struct nes_cqp_request *cqp_request)
{
	unsigned long flags;

	nes_debug(NES_DBG_CQP, "CQP request %p (opcode 0x%02X) freed.\n",
		  cqp_request,
		  le32_to_cpu(cqp_request->cqp_wqe.wqe_words[NES_CQP_WQE_OPCODE_IDX]) & 0x3f);

	if (cqp_request->dynamic) {
		kfree(cqp_request);
	} else {
		spin_lock_irqsave(&nesdev->cqp.lock, flags);
		list_add_tail(&cqp_request->list, &nesdev->cqp_avail_reqs);
		spin_unlock_irqrestore(&nesdev->cqp.lock, flags);
	}
}

void nes_put_cqp_request(struct nes_device *nesdev,
			 struct nes_cqp_request *cqp_request)
{
	if (atomic_dec_and_test(&cqp_request->refcount))
		nes_free_cqp_request(nesdev, cqp_request);
}


/**
 * nes_post_cqp_request
 */
void nes_post_cqp_request(struct nes_device *nesdev,
			  struct nes_cqp_request *cqp_request)
{
	struct nes_hw_cqp_wqe *cqp_wqe;
	unsigned long flags;
	u32 cqp_head;
	u64 u64temp;
	u32 opcode;
	int ctx_index = NES_CQP_WQE_COMP_CTX_LOW_IDX;

	spin_lock_irqsave(&nesdev->cqp.lock, flags);

	if (((((nesdev->cqp.sq_tail+(nesdev->cqp.sq_size*2))-nesdev->cqp.sq_head) &
			(nesdev->cqp.sq_size - 1)) != 1)
			&& (list_empty(&nesdev->cqp_pending_reqs))) {
		cqp_head = nesdev->cqp.sq_head++;
		nesdev->cqp.sq_head &= nesdev->cqp.sq_size-1;
		cqp_wqe = &nesdev->cqp.sq_vbase[cqp_head];
		memcpy(cqp_wqe, &cqp_request->cqp_wqe, sizeof(*cqp_wqe));
		opcode = le32_to_cpu(cqp_wqe->wqe_words[NES_CQP_WQE_OPCODE_IDX]);
		if ((opcode & NES_CQP_OPCODE_MASK) == NES_CQP_DOWNLOAD_SEGMENT)
			ctx_index = NES_CQP_WQE_DL_COMP_CTX_LOW_IDX;
		barrier();
		u64temp = (unsigned long)cqp_request;
		set_wqe_64bit_value(cqp_wqe->wqe_words, ctx_index, u64temp);
		nes_debug(NES_DBG_CQP, "CQP request (opcode 0x%02X), line 1 = 0x%08X put on CQPs SQ,"
			" request = %p, cqp_head = %u, cqp_tail = %u, cqp_size = %u,"
			" waiting = %d, refcount = %d.\n",
			opcode & NES_CQP_OPCODE_MASK,
			le32_to_cpu(cqp_wqe->wqe_words[NES_CQP_WQE_ID_IDX]), cqp_request,
			nesdev->cqp.sq_head, nesdev->cqp.sq_tail, nesdev->cqp.sq_size,
			cqp_request->waiting, atomic_read(&cqp_request->refcount));

		barrier();

		/* Ring doorbell (1 WQEs) */
		nes_write32(nesdev->regs+NES_WQE_ALLOC, 0x01800000 | nesdev->cqp.qp_id);

		barrier();
	} else {
		nes_debug(NES_DBG_CQP, "CQP request %p (opcode 0x%02X), line 1 = 0x%08X"
				" put on the pending queue.\n",
				cqp_request,
				le32_to_cpu(cqp_request->cqp_wqe.wqe_words[NES_CQP_WQE_OPCODE_IDX])&0x3f,
				le32_to_cpu(cqp_request->cqp_wqe.wqe_words[NES_CQP_WQE_ID_IDX]));
		list_add_tail(&cqp_request->list, &nesdev->cqp_pending_reqs);
	}

	spin_unlock_irqrestore(&nesdev->cqp.lock, flags);

	return;
}

/**
 * nes_arp_table
 */
int nes_arp_table(struct nes_device *nesdev, u32 ip_addr, u8 *mac_addr, u32 action)
{
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	int arp_index;
	int err = 0;
	__be32 tmp_addr;

	for (arp_index = 0; (u32) arp_index < nesadapter->arp_table_size; arp_index++) {
		if (nesadapter->arp_table[arp_index].ip_addr == ip_addr)
			break;
	}

	if (action == NES_ARP_ADD) {
		if (arp_index != nesadapter->arp_table_size) {
			return -1;
		}

		arp_index = 0;
		err = nes_alloc_resource(nesadapter, nesadapter->allocated_arps,
				nesadapter->arp_table_size, (u32 *)&arp_index, &nesadapter->next_arp_index, NES_RESOURCE_ARP);
		if (err) {
			nes_debug(NES_DBG_NETDEV, "nes_alloc_resource returned error = %u\n", err);
			return err;
		}
		nes_debug(NES_DBG_NETDEV, "ADD, arp_index=%d\n", arp_index);

		nesadapter->arp_table[arp_index].ip_addr = ip_addr;
		memcpy(nesadapter->arp_table[arp_index].mac_addr, mac_addr, ETH_ALEN);
		return arp_index;
	}

	/* DELETE or RESOLVE */
	if (arp_index == nesadapter->arp_table_size) {
		tmp_addr = cpu_to_be32(ip_addr);
		nes_debug(NES_DBG_NETDEV, "MAC for %pI4 not in ARP table - cannot %s\n",
			  &tmp_addr, action == NES_ARP_RESOLVE ? "resolve" : "delete");
		return -1;
	}

	if (action == NES_ARP_RESOLVE) {
		nes_debug(NES_DBG_NETDEV, "RESOLVE, arp_index=%d\n", arp_index);
		return arp_index;
	}

	if (action == NES_ARP_DELETE) {
		nes_debug(NES_DBG_NETDEV, "DELETE, arp_index=%d\n", arp_index);
		nesadapter->arp_table[arp_index].ip_addr = 0;
		eth_zero_addr(nesadapter->arp_table[arp_index].mac_addr);
		nes_free_resource(nesadapter, nesadapter->allocated_arps, arp_index);
		return arp_index;
	}

	return -1;
}


/**
 * nes_mh_fix
 */
void nes_mh_fix(unsigned long parm)
{
	unsigned long flags;
	struct nes_device *nesdev = (struct nes_device *)parm;
	struct nes_adapter *nesadapter = nesdev->nesadapter;
	struct nes_vnic *nesvnic;
	u32 used_chunks_tx;
	u32 temp_used_chunks_tx;
	u32 temp_last_used_chunks_tx;
	u32 used_chunks_mask;
	u32 mac_tx_frames_low;
	u32 mac_tx_frames_high;
	u32 mac_tx_pauses;
	u32 serdes_status;
	u32 reset_value;
	u32 tx_control;
	u32 tx_config;
	u32 tx_pause_quanta;
	u32 rx_control;
	u32 rx_config;
	u32 mac_exact_match;
	u32 mpp_debug;
	u32 i=0;
	u32 chunks_tx_progress = 0;

	spin_lock_irqsave(&nesadapter->phy_lock, flags);
	if ((nesadapter->mac_sw_state[0] != NES_MAC_SW_IDLE) || (nesadapter->mac_link_down[0])) {
		spin_unlock_irqrestore(&nesadapter->phy_lock, flags);
		goto no_mh_work;
	}
	nesadapter->mac_sw_state[0] = NES_MAC_SW_MH;
	spin_unlock_irqrestore(&nesadapter->phy_lock, flags);
	do {
		mac_tx_frames_low = nes_read_indexed(nesdev, NES_IDX_MAC_TX_FRAMES_LOW);
		mac_tx_frames_high = nes_read_indexed(nesdev, NES_IDX_MAC_TX_FRAMES_HIGH);
		mac_tx_pauses = nes_read_indexed(nesdev, NES_IDX_MAC_TX_PAUSE_FRAMES);
		used_chunks_tx = nes_read_indexed(nesdev, NES_IDX_USED_CHUNKS_TX);
		nesdev->mac_pause_frames_sent += mac_tx_pauses;
		used_chunks_mask = 0;
		temp_used_chunks_tx = used_chunks_tx;
		temp_last_used_chunks_tx = nesdev->last_used_chunks_tx;

		if (nesdev->netdev[0]) {
			nesvnic = netdev_priv(nesdev->netdev[0]);
		} else {
			break;
		}

		for (i=0; i<4; i++) {
			used_chunks_mask <<= 8;
			if (nesvnic->qp_nic_index[i] != 0xff) {
				used_chunks_mask |= 0xff;
				if ((temp_used_chunks_tx&0xff)<(temp_last_used_chunks_tx&0xff)) {
					chunks_tx_progress = 1;
				}
			}
			temp_used_chunks_tx >>= 8;
			temp_last_used_chunks_tx >>= 8;
		}
		if ((mac_tx_frames_low) || (mac_tx_frames_high) ||
			(!(used_chunks_tx&used_chunks_mask)) ||
			(!(nesdev->last_used_chunks_tx&used_chunks_mask)) ||
			(chunks_tx_progress) ) {
			nesdev->last_used_chunks_tx = used_chunks_tx;
			break;
		}
		nesdev->last_used_chunks_tx = used_chunks_tx;
		barrier();

		nes_write_indexed(nesdev, NES_IDX_MAC_TX_CONTROL, 0x00000005);
		mh_pauses_sent++;
		mac_tx_pauses = nes_read_indexed(nesdev, NES_IDX_MAC_TX_PAUSE_FRAMES);
		if (mac_tx_pauses) {
			nesdev->mac_pause_frames_sent += mac_tx_pauses;
			break;
		}

		tx_control = nes_read_indexed(nesdev, NES_IDX_MAC_TX_CONTROL);
		tx_config = nes_read_indexed(nesdev, NES_IDX_MAC_TX_CONFIG);
		tx_pause_quanta = nes_read_indexed(nesdev, NES_IDX_MAC_TX_PAUSE_QUANTA);
		rx_control = nes_read_indexed(nesdev, NES_IDX_MAC_RX_CONTROL);
		rx_config = nes_read_indexed(nesdev, NES_IDX_MAC_RX_CONFIG);
		mac_exact_match = nes_read_indexed(nesdev, NES_IDX_MAC_EXACT_MATCH_BOTTOM);
		mpp_debug = nes_read_indexed(nesdev, NES_IDX_MPP_DEBUG);

		/* one last ditch effort to avoid a false positive */
		mac_tx_pauses = nes_read_indexed(nesdev, NES_IDX_MAC_TX_PAUSE_FRAMES);
		if (mac_tx_pauses) {
			nesdev->last_mac_tx_pauses = nesdev->mac_pause_frames_sent;
			nes_debug(NES_DBG_HW, "failsafe caught slow outbound pause\n");
			break;
		}
		mh_detected++;

		nes_write_indexed(nesdev, NES_IDX_MAC_TX_CONTROL, 0x00000000);
		nes_write_indexed(nesdev, NES_IDX_MAC_TX_CONFIG, 0x00000000);
		reset_value = nes_read32(nesdev->regs+NES_SOFTWARE_RESET);

		nes_write32(nesdev->regs+NES_SOFTWARE_RESET, reset_value | 0x0000001d);

		while (((nes_read32(nesdev->regs+NES_SOFTWARE_RESET)
				& 0x00000040) != 0x00000040) && (i++ < 5000)) {
			/* mdelay(1); */
		}

		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_CONTROL0, 0x00000008);
		serdes_status = nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_COMMON_STATUS0);

		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_TX_EMP0, 0x000bdef7);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_TX_DRIVE0, 0x9ce73000);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_MODE0, 0x0ff00000);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_SIGDET0, 0x00000000);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_BYPASS0, 0x00000000);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_LOOPBACK_CONTROL0, 0x00000000);
		if (nesadapter->OneG_Mode) {
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_EQ_CONTROL0, 0xf0182222);
		} else {
			nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_RX_EQ_CONTROL0, 0xf0042222);
		}
		serdes_status = nes_read_indexed(nesdev, NES_IDX_ETH_SERDES_RX_EQ_STATUS0);
		nes_write_indexed(nesdev, NES_IDX_ETH_SERDES_CDR_CONTROL0, 0x000000ff);

		nes_write_indexed(nesdev, NES_IDX_MAC_TX_CONTROL, tx_control);
		nes_write_indexed(nesdev, NES_IDX_MAC_TX_CONFIG, tx_config);
		nes_write_indexed(nesdev, NES_IDX_MAC_TX_PAUSE_QUANTA, tx_pause_quanta);
		nes_write_indexed(nesdev, NES_IDX_MAC_RX_CONTROL, rx_control);
		nes_write_indexed(nesdev, NES_IDX_MAC_RX_CONFIG, rx_config);
		nes_write_indexed(nesdev, NES_IDX_MAC_EXACT_MATCH_BOTTOM, mac_exact_match);
		nes_write_indexed(nesdev, NES_IDX_MPP_DEBUG, mpp_debug);

	} while (0);

	nesadapter->mac_sw_state[0] = NES_MAC_SW_IDLE;
no_mh_work:
	nesdev->nesadapter->mh_timer.expires = jiffies + (HZ/5);
	add_timer(&nesdev->nesadapter->mh_timer);
}

/**
 * nes_clc
 */
void nes_clc(unsigned long parm)
{
	unsigned long flags;
	struct nes_device *nesdev = (struct nes_device *)parm;
	struct nes_adapter *nesadapter = nesdev->nesadapter;

	spin_lock_irqsave(&nesadapter->phy_lock, flags);
    nesadapter->link_interrupt_count[0] = 0;
    nesadapter->link_interrupt_count[1] = 0;
    nesadapter->link_interrupt_count[2] = 0;
    nesadapter->link_interrupt_count[3] = 0;
	spin_unlock_irqrestore(&nesadapter->phy_lock, flags);

	nesadapter->lc_timer.expires = jiffies + 3600 * HZ;  /* 1 hour */
	add_timer(&nesadapter->lc_timer);
}


/**
 * nes_dump_mem
 */
void nes_dump_mem(unsigned int dump_debug_level, void *addr, int length)
{
	if (!(nes_debug_level & dump_debug_level)) {
		return;
	}

	if (length > 0x100) {
		nes_debug(dump_debug_level, "Length truncated from %x to %x\n", length, 0x100);
		length = 0x100;
	}
	nes_debug(dump_debug_level, "Address=0x%p, length=0x%x (%d)\n", addr, length, length);

	print_hex_dump(KERN_ERR, PFX, DUMP_PREFIX_NONE, 16, 1, addr, length, true);
}
