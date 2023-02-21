// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <linux/pci.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "ngbe_type.h"
#include "ngbe_hw.h"
#include "ngbe.h"

int ngbe_eeprom_chksum_hostif(struct ngbe_hw *hw)
{
	struct wx_hic_read_shadow_ram buffer;
	struct wx_hw *wxhw = &hw->wxhw;
	int status;
	int tmp;

	buffer.hdr.req.cmd = NGBE_FW_EEPROM_CHECKSUM_CMD;
	buffer.hdr.req.buf_lenh = 0;
	buffer.hdr.req.buf_lenl = 0;
	buffer.hdr.req.checksum = NGBE_FW_CMD_DEFAULT_CHECKSUM;
	/* convert offset from words to bytes */
	buffer.address = 0;
	/* one word */
	buffer.length = 0;

	status = wx_host_interface_command(wxhw, (u32 *)&buffer, sizeof(buffer),
					   WX_HI_COMMAND_TIMEOUT, false);

	if (status < 0)
		return status;
	tmp = rd32a(wxhw, WX_MNG_MBOX, 1);
	if (tmp == NGBE_FW_CMD_ST_PASS)
		return 0;
	return -EIO;
}

static int ngbe_reset_misc(struct ngbe_hw *hw)
{
	struct wx_hw *wxhw = &hw->wxhw;

	wx_reset_misc(wxhw);
	if (hw->mac_type == ngbe_mac_type_rgmii)
		wr32(wxhw, NGBE_MDIO_CLAUSE_SELECT, 0xF);
	if (hw->gpio_ctrl) {
		/* gpio0 is used to power on/off control*/
		wr32(wxhw, NGBE_GPIO_DDR, 0x1);
		wr32(wxhw, NGBE_GPIO_DR, NGBE_GPIO_DR_0);
	}
	return 0;
}

/**
 *  ngbe_reset_hw - Perform hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 **/
int ngbe_reset_hw(struct ngbe_hw *hw)
{
	struct wx_hw *wxhw = &hw->wxhw;
	int status = 0;
	u32 reset = 0;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	status = wx_stop_adapter(wxhw);
	if (status != 0)
		return status;
	reset = WX_MIS_RST_LAN_RST(wxhw->bus.func);
	wr32(wxhw, WX_MIS_RST, reset | rd32(wxhw, WX_MIS_RST));
	ngbe_reset_misc(hw);

	/* Store the permanent mac address */
	wx_get_mac_addr(wxhw, wxhw->mac.perm_addr);

	/* reset num_rar_entries to 128 */
	wxhw->mac.num_rar_entries = NGBE_RAR_ENTRIES;
	wx_init_rx_addrs(wxhw);
	pci_set_master(wxhw->pdev);

	return 0;
}
