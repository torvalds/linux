// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/Hal8188EPwrSeq.h"
#include "../include/rtl8188e_hal.h"

struct wl_pwr_cfg rtl8188E_power_on_flow[] = {
	{ 0x0006, PWR_CMD_POLLING, BIT(1), BIT(1) },
	{ 0x0002, PWR_CMD_WRITE, BIT(0) | BIT(1), 0 }, /* reset BB */
	{ 0x0026, PWR_CMD_WRITE, BIT(7), BIT(7) }, /* schmitt trigger */
	{ 0x0005, PWR_CMD_WRITE, BIT(7), 0 }, /* disable HWPDN (control by DRV)*/
	{ 0x0005, PWR_CMD_WRITE, BIT(4) | BIT(3), 0 }, /* disable WL suspend*/
	{ 0x0005, PWR_CMD_WRITE, BIT(0), BIT(0) },
	{ 0x0005, PWR_CMD_POLLING, BIT(0), 0 },
	{ 0x0023, PWR_CMD_WRITE, BIT(4), 0 },
	{ 0xFFFF, PWR_CMD_END, 0, 0 },
};

struct wl_pwr_cfg rtl8188E_card_disable_flow[] = {
	{ 0x001F, PWR_CMD_WRITE, 0xFF, 0 }, /* turn off RF */
	{ 0x0023, PWR_CMD_WRITE, BIT(4), BIT(4) }, /* LDO Sleep mode */
	{ 0x0005, PWR_CMD_WRITE, BIT(1), BIT(1) }, /* turn off MAC by HW state machine */
	{ 0x0005, PWR_CMD_POLLING, BIT(1), 0 },
	{ 0x0026, PWR_CMD_WRITE, BIT(7), BIT(7) }, /* schmitt trigger */
	{ 0x0005, PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) }, /* enable WL suspend */
	{ 0x0007, PWR_CMD_WRITE, 0xFF, 0 }, /* enable bandgap mbias in suspend */
	{ 0x0041, PWR_CMD_WRITE, BIT(4), 0 }, /* Clear SIC_EN register */
	{ 0xfe10, PWR_CMD_WRITE, BIT(4), BIT(4) }, /* Set USB suspend enable local register */
	{ 0xFFFF, PWR_CMD_END, 0, 0 },
};

/* This is used by driver for LPSRadioOff Procedure, not for FW LPS Step */
struct wl_pwr_cfg rtl8188E_enter_lps_flow[] = {
	{ 0x0522, PWR_CMD_WRITE, 0xFF, 0x7F },/* Tx Pause */
	{ 0x05F8, PWR_CMD_POLLING, 0xFF, 0 }, /* Should be zero if no packet is transmitted */
	{ 0x05F9, PWR_CMD_POLLING, 0xFF, 0 }, /* Should be zero if no packet is transmitted */
	{ 0x05FA, PWR_CMD_POLLING, 0xFF, 0 }, /* Should be zero if no packet is transmitted */
	{ 0x05FB, PWR_CMD_POLLING, 0xFF, 0 }, /* Should be zero if no packet is transmitted */
	{ 0x0002, PWR_CMD_WRITE, BIT(0), 0 }, /* CCK and OFDM are disabled, clocks are gated */
	{ 0x0002, PWR_CMD_DELAY, 0, PWRSEQ_DELAY_US },
	{ 0x0100, PWR_CMD_WRITE, 0xFF, 0x3F }, /* Reset MAC TRX */
	{ 0x0101, PWR_CMD_WRITE, BIT(1), 0 }, /* check if removed later */
	{ 0x0553, PWR_CMD_WRITE, BIT(5), BIT(5) }, /* Respond TxOK to scheduler */
	{ 0xFFFF, PWR_CMD_END, 0, 0 },
};
