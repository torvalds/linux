// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <linux/pci.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "ngbe_type.h"
#include "ngbe_hw.h"

int ngbe_eeprom_chksum_hostif(struct wx *wx)
{
	struct wx_hic_read_shadow_ram buffer;
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

	status = wx_host_interface_command(wx, (u32 *)&buffer, sizeof(buffer),
					   WX_HI_COMMAND_TIMEOUT, false);

	if (status < 0)
		return status;
	tmp = rd32a(wx, WX_MNG_MBOX, 1);
	if (tmp == NGBE_FW_CMD_ST_PASS)
		return 0;
	return -EIO;
}

static int ngbe_reset_misc(struct wx *wx)
{
	wx_reset_misc(wx);
	if (wx->gpio_ctrl) {
		/* gpio0 is used to power on/off control*/
		wr32(wx, NGBE_GPIO_DDR, 0x1);
		ngbe_sfp_modules_txrx_powerctl(wx, false);
	}
	return 0;
}

void ngbe_sfp_modules_txrx_powerctl(struct wx *wx, bool swi)
{
	/* gpio0 is used to power on control . 0 is on */
	wr32(wx, NGBE_GPIO_DR, swi ? 0 : NGBE_GPIO_DR_0);
}

/**
 *  ngbe_reset_hw - Perform hardware reset
 *  @wx: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 **/
int ngbe_reset_hw(struct wx *wx)
{
	u32 val = 0;
	int ret = 0;

	/* Call wx stop to disable tx/rx and clear interrupts */
	ret = wx_stop_adapter(wx);
	if (ret != 0)
		return ret;

	if (wx->mac_type != em_mac_type_mdi) {
		val = WX_MIS_RST_LAN_RST(wx->bus.func);
		wr32(wx, WX_MIS_RST, val | rd32(wx, WX_MIS_RST));

		ret = read_poll_timeout(rd32, val,
					!(val & (BIT(9) << wx->bus.func)), 1000,
					100000, false, wx, 0x10028);
		if (ret) {
			wx_err(wx, "Lan reset exceed s maximum times.\n");
			return ret;
		}
	}
	ngbe_reset_misc(wx);

	/* Store the permanent mac address */
	wx_get_mac_addr(wx, wx->mac.perm_addr);

	/* reset num_rar_entries to 128 */
	wx->mac.num_rar_entries = NGBE_RAR_ENTRIES;
	wx_init_rx_addrs(wx);
	pci_set_master(wx->pdev);

	return 0;
}
