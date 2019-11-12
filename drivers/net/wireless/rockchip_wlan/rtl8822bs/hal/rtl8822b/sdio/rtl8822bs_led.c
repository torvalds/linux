/******************************************************************************
 *
 * Copyright(c) 2015 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#define _RTL8822BS_LED_C_

#include <drv_types.h>		/* PADAPTER */
#include <hal_data.h>		/* PHAL_DATA_TYPE */
#include <hal_com_led.h>	/* PLED_SDIO */
#ifdef CONFIG_RTW_SW_LED

/*
 * =============================================================================
 * LED object.
 * =============================================================================
 */


/*
 * =============================================================================
 *	Prototype of protected function.
 * =============================================================================
 */

/*
 * =============================================================================
 * LED routines.
 * =============================================================================
 */

/*
 * Description:
 *	Turn on LED according to LedPin specified.
 */
void swledon(PADAPTER adapter, PLED_SDIO pLed)
{
	u8 LedCfg;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (RTW_CANNOT_RUN(adapter))
		return;

	pLed->bLedOn = _TRUE;
}


/*
 * Description:
 *	Turn off LED according to LedPin specified.
 */
void swledoff(PADAPTER adapter, PLED_SDIO pLed)
{
	u8 LedCfg;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (RTW_CANNOT_RUN(adapter))
		goto exit;

exit:
	pLed->bLedOn = _FALSE;
}

/*
 * =============================================================================
 * Interface to manipulate LED objects.
 * =============================================================================
 */

/*
 * =============================================================================
 * Default LED behavior.
 * =============================================================================
 */

/*
 * Description:
 *	Initialize all LED_871x objects.
 */
void rtl8822bs_initswleds(PADAPTER adapter)
{
}

/*
 * Description:
 *	DeInitialize all LED_819xUsb objects.
 */
void rtl8822bs_deinitswleds(PADAPTER adapter)
{
}
#endif
