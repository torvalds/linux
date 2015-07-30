/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#include "rtl_core.h"
#include "rtl_eeprom.h"

static void _rtl92e_gpio_write_bit(struct net_device *dev, int no, bool val)
{
	u8 reg = rtl92e_readb(dev, EPROM_CMD);

	if (val)
		reg |= 1 << no;
	else
		reg &= ~(1 << no);

	rtl92e_writeb(dev, EPROM_CMD, reg);
	udelay(EPROM_DELAY);
}

static bool _rtl92e_gpio_get_bit(struct net_device *dev, int no)
{
	u8 reg = rtl92e_readb(dev, EPROM_CMD);

	return (reg >> no) & 0x1;
}

static void _rtl92e_eeprom_ck_cycle(struct net_device *dev)
{
	_rtl92e_gpio_write_bit(dev, EPROM_CK_BIT, 1);
	_rtl92e_gpio_write_bit(dev, EPROM_CK_BIT, 0);
}

static u16 _rtl92e_eeprom_xfer(struct net_device *dev, u16 data, int tx_len)
{
	u16 ret = 0;
	int rx_len = 16;

	_rtl92e_gpio_write_bit(dev, EPROM_CS_BIT, 1);
	_rtl92e_eeprom_ck_cycle(dev);

	while (tx_len--) {
		_rtl92e_gpio_write_bit(dev, EPROM_W_BIT,
				       (data >> tx_len) & 0x1);
		_rtl92e_eeprom_ck_cycle(dev);
	}

	_rtl92e_gpio_write_bit(dev, EPROM_W_BIT, 0);

	while (rx_len--) {
		_rtl92e_eeprom_ck_cycle(dev);
		ret |= _rtl92e_gpio_get_bit(dev, EPROM_R_BIT) << rx_len;
	}

	_rtl92e_gpio_write_bit(dev, EPROM_CS_BIT, 0);
	_rtl92e_eeprom_ck_cycle(dev);

	return ret;
}

u32 rtl92e_eeprom_read(struct net_device *dev, u32 addr)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 ret = 0;

	rtl92e_writeb(dev, EPROM_CMD,
		      (EPROM_CMD_PROGRAM << EPROM_CMD_OPERATING_MODE_SHIFT));
	udelay(EPROM_DELAY);

	/* EEPROM is configured as x16 */
	if (priv->epromtype == EEPROM_93C56)
		ret = _rtl92e_eeprom_xfer(dev, (addr & 0xFF) | (0x6 << 8), 11);
	else
		ret = _rtl92e_eeprom_xfer(dev, (addr & 0x3F) | (0x6 << 6), 9);

	rtl92e_writeb(dev, EPROM_CMD,
		      (EPROM_CMD_NORMAL<<EPROM_CMD_OPERATING_MODE_SHIFT));
	return ret;
}
