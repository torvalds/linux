/*****************************************************************************
* Copyright 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/
/****************************************************************************/
/**
*  @file    chipcHw_str.c
*
*  @brief   Contains strings which are useful to linux and csp
*
*  @note
*/
/****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */

#include <mach/csp/chipcHw_inline.h>

/* ---- Private Constants and Types --------------------------------------- */

static const char *gMuxStr[] = {
	"GPIO",			/* 0 */
	"KeyPad",		/* 1 */
	"I2C-Host",		/* 2 */
	"SPI",			/* 3 */
	"Uart",			/* 4 */
	"LED-Mtx-P",		/* 5 */
	"LED-Mtx-S",		/* 6 */
	"SDIO-0",		/* 7 */
	"SDIO-1",		/* 8 */
	"PCM",			/* 9 */
	"I2S",			/* 10 */
	"ETM",			/* 11 */
	"Debug",		/* 12 */
	"Misc",			/* 13 */
	"0xE",			/* 14 */
	"0xF",			/* 15 */
};

/****************************************************************************/
/**
*  @brief   Retrieves a string representation of the mux setting for a pin.
*
*  @return  Pointer to a character string.
*/
/****************************************************************************/

const char *chipcHw_getGpioPinFunctionStr(int pin)
{
	if ((pin < 0) || (pin >= chipcHw_GPIO_COUNT)) {
		return "";
	}

	return gMuxStr[chipcHw_getGpioPinFunction(pin)];
}
