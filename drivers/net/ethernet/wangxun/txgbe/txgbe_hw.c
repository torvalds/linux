// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/string.h>
#include <linux/iopoll.h>
#include <linux/types.h>
#include <linux/pci.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "txgbe_type.h"
#include "txgbe_hw.h"
#include "txgbe.h"

/**
 *  txgbe_init_thermal_sensor_thresh - Inits thermal sensor thresholds
 *  @hw: pointer to hardware structure
 *
 *  Inits the thermal sensor thresholds according to the NVM map
 *  and save off the threshold and location values into mac.thermal_sensor_data
 **/
static void txgbe_init_thermal_sensor_thresh(struct txgbe_hw *hw)
{
	struct wx_hw *wxhw = &hw->wxhw;
	struct wx_thermal_sensor_data *data = &wxhw->mac.sensor;

	memset(data, 0, sizeof(struct wx_thermal_sensor_data));

	/* Only support thermal sensors attached to SP physical port 0 */
	if (wxhw->bus.func)
		return;

	wr32(wxhw, TXGBE_TS_CTL, TXGBE_TS_CTL_EVAL_MD);

	wr32(wxhw, WX_TS_INT_EN,
	     WX_TS_INT_EN_ALARM_INT_EN | WX_TS_INT_EN_DALARM_INT_EN);
	wr32(wxhw, WX_TS_EN, WX_TS_EN_ENA);

	data->alarm_thresh = 100;
	wr32(wxhw, WX_TS_ALARM_THRE, 677);
	data->dalarm_thresh = 90;
	wr32(wxhw, WX_TS_DALARM_THRE, 614);
}

static void txgbe_reset_misc(struct txgbe_hw *hw)
{
	struct wx_hw *wxhw = &hw->wxhw;

	wx_reset_misc(wxhw);
	txgbe_init_thermal_sensor_thresh(hw);
}

/**
 *  txgbe_reset_hw - Perform hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 **/
int txgbe_reset_hw(struct txgbe_hw *hw)
{
	struct wx_hw *wxhw = &hw->wxhw;
	u32 reset = 0;
	int status;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	status = wx_stop_adapter(wxhw);
	if (status != 0)
		return status;

	reset = WX_MIS_RST_LAN_RST(wxhw->bus.func);
	wr32(wxhw, WX_MIS_RST, reset | rd32(wxhw, WX_MIS_RST));

	WX_WRITE_FLUSH(wxhw);
	usleep_range(10, 100);

	status = wx_check_flash_load(wxhw, TXGBE_SPI_ILDR_STATUS_LAN_SW_RST(wxhw->bus.func));
	if (status != 0)
		return status;

	txgbe_reset_misc(hw);

	/* Store the permanent mac address */
	wx_get_mac_addr(wxhw, wxhw->mac.perm_addr);

	/* Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	wxhw->mac.num_rar_entries = TXGBE_SP_RAR_ENTRIES;
	wx_init_rx_addrs(wxhw);

	pci_set_master(wxhw->pdev);

	return 0;
}
