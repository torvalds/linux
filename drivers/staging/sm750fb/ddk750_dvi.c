// SPDX-License-Identifier: GPL-2.0
#define USE_DVICHIP
#ifdef USE_DVICHIP
#include "ddk750_chip.h"
#include "ddk750_reg.h"
#include "ddk750_dvi.h"
#include "ddk750_sii164.h"

/*
 * This global variable contains all the supported driver and its corresponding
 * function API. Please set the function pointer to NULL whenever the function
 * is not supported.
 */
static struct dvi_ctrl_device dcft_supported_dvi_controller[] = {
#ifdef DVI_CTRL_SII164
	{
		.init = sii164InitChip,
		.get_vendor_id = sii164GetVendorID,
		.get_device_id = sii164GetDeviceID,
#ifdef SII164_FULL_FUNCTIONS
		.reset_chip = sii164ResetChip,
		.get_chip_string = sii164GetChipString,
		.set_power = sii164SetPower,
		.enable_hot_plug_detection = sii164EnableHotPlugDetection,
		.is_connected = sii164IsConnected,
		.check_interrupt = sii164CheckInterrupt,
		.clear_interrupt = sii164ClearInterrupt,
#endif
	},
#endif
};

int dvi_init(unsigned char edge_select,
	     unsigned char bus_select,
	     unsigned char dual_edge_clk_select,
	     unsigned char hsync_enable,
	     unsigned char vsync_enable,
	     unsigned char deskew_enable,
	     unsigned char deskew_setting,
	     unsigned char continuous_sync_enable,
	     unsigned char pll_filter_enable,
	     unsigned char pll_filter_value)
{
	struct dvi_ctrl_device *current_dvi_ctrl;

	current_dvi_ctrl = dcft_supported_dvi_controller;
	if (current_dvi_ctrl->init) {
		return current_dvi_ctrl->init(edge_select,
					      bus_select,
					      dual_edge_clk_select,
					      hsync_enable,
					      vsync_enable,
					      deskew_enable,
					      deskew_setting,
					      continuous_sync_enable,
					      pll_filter_enable,
					      pll_filter_value);
	}
	return -1; /* error */
}

#endif
