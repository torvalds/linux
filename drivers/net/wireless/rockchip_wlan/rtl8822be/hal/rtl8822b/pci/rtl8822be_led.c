/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include <drv_types.h>		/* PADAPTER */
#include <hal_data.h>		/* PHAL_DATA_TYPE */
#include <hal_com_led.h>	/* PLED_PCIE */


/*
 *==============================================================================
 *	Prototype of protected function.
 *==============================================================================
 */

/*
 *==============================================================================
 * LED_819xUsb routines.
 *==============================================================================
 */

/*
 * Description:
 *	Turn on LED according to LedPin specified.
 */
static void SwLedOn_8822be(PADAPTER adapter, PLED_PCIE pLed)
{
#if 0
	u16 LedReg = REG_LEDCFG0;
	u8 LedCfg = 0;
	struct led_priv	*ledpriv = &(adapter->ledpriv);

	if (RTW_CANNOT_RUN(adapter))
		return;

	switch (pLed->LedPin) {
	case LED_PIN_LED0:
		if (ledpriv->LedStrategy == SW_LED_MODE10)
			LedReg = REG_LEDCFG0;
		else
			LedReg = REG_LEDCFG1;
		break;

	case LED_PIN_LED1:
		LedReg = REG_LEDCFG2;
		break;

	case LED_PIN_GPIO0:
	default:
		break;
	}

	LedCfg = rtw_read8(adapter, LedReg);
	LedCfg |= BIT5; /* Set 0x4c[21] */

	/* Clear 0x4c[23:22] and 0x4c[19:16] */
	LedCfg &= ~(BIT7 | BIT6 | BIT3 | BIT2 | BIT1 | BIT0);

	/* SW control led0 on. */
	rtw_write8(adapter, LedReg, LedCfg);
	pLed->bLedOn = _TRUE;
#else
	RTW_INFO("%s(%d)TODO LED\n", __func__, __LINE__);
#endif
}


/*
 * Description:
 *	Turn off LED according to LedPin specified.
 */
static void SwLedOff_8822be(PADAPTER adapter, PLED_PCIE pLed)
{
#if 0
	u16 LedReg = REG_LEDCFG0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct led_priv	*ledpriv = &adapter->ledpriv;

	if (RTW_CANNOT_RUN(adapter))
		return;

	switch (pLed->LedPin) {
	case LED_PIN_LED0:
		if (ledpriv->LedStrategy == SW_LED_MODE10)
			LedReg = REG_LEDCFG0;
		else
			LedReg = REG_LEDCFG1;
		break;

	case LED_PIN_LED1:
		LedReg = REG_LEDCFG2;
		break;

	case LED_PIN_GPIO0:
	default:
		break;
	}

	/* Open-drain arrangement for controlling the LED */
	if (hal->bLedOpenDrain == _TRUE) {
		u8 LedCfg = rtw_read8(adapter, LedReg);

		LedCfg &= 0xd0; /* Set to software control. */
		rtw_write8(adapter, LedReg, (LedCfg | BIT3));

		/* Open-drain arrangement */
		LedCfg = rtw_read8(adapter, REG_MAC_PINMUX_CFG);
		LedCfg &= 0xFE;/* Set GPIO[8] to input mode */
		rtw_write8(adapter, REG_MAC_PINMUX_CFG, LedCfg);

	} else
		rtw_write8(adapter, LedReg, 0x28);

	pLed->bLedOn = _FALSE;
#else
	RTW_INFO("%s(%d)TODO LED\n", __func__, __LINE__);
#endif
}

/*
 * Description:
 *	Initialize all LED_871x objects.
 */
void rtl8822be_InitSwLeds(PADAPTER adapter)
{
	struct led_priv *pledpriv = &adapter->ledpriv;

	pledpriv->LedControlHandler = LedControlPCIE;

	pledpriv->SwLedOn = SwLedOn_8822be;
	pledpriv->SwLedOff = SwLedOff_8822be;

	InitLed(adapter, &pledpriv->SwLed0, LED_PIN_LED0);
	InitLed(adapter, &pledpriv->SwLed1, LED_PIN_LED1);
}


/*
 * Description:
 *	DeInitialize all LED_819xUsb objects.
 */
void rtl8822be_DeInitSwLeds(PADAPTER adapter)
{
	struct led_priv	*ledpriv = &adapter->ledpriv;

	DeInitLed(&ledpriv->SwLed0);
	DeInitLed(&ledpriv->SwLed1);
}
