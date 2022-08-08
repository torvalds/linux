// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_EEPROM_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

void up_clk(_adapter *padapter,	 u16 *x)
{
_func_enter_;
	*x = *x | _EESK;
	rtw_write8(padapter, EE_9346CR, (u8)*x);
	udelay(CLOCK_RATE);

_func_exit_;

}

void down_clk(_adapter *padapter, u16 *x)
{
_func_enter_;
	*x = *x & ~_EESK;
	rtw_write8(padapter, EE_9346CR, (u8)*x);
	udelay(CLOCK_RATE);
_func_exit_;
}

void shift_out_bits(_adapter *padapter, u16 data, u16 count)
{
	u16 x, mask;
_func_enter_;

	if (padapter->bSurpriseRemoved)
		goto out;

	mask = 0x01 << (count - 1);
	x = rtw_read8(padapter, EE_9346CR);

	x &= ~(_EEDO | _EEDI);

	do {
		x &= ~_EEDI;
		if (data & mask)
			x |= _EEDI;
		if (padapter->bSurpriseRemoved)
			goto out;

		rtw_write8(padapter, EE_9346CR, (u8)x);
		udelay(CLOCK_RATE);
		up_clk(padapter, &x);
		down_clk(padapter, &x);
		mask = mask >> 1;
	} while (mask);
	if (padapter->bSurpriseRemoved)
		goto out;

	x &= ~_EEDI;
	rtw_write8(padapter, EE_9346CR, (u8)x);
out:
_func_exit_;
}

u16 shift_in_bits(_adapter *padapter)
{
	u16 x, d = 0, i;
_func_enter_;
	if (padapter->bSurpriseRemoved)
		goto out;

	x = rtw_read8(padapter, EE_9346CR);

	x &= ~(_EEDO | _EEDI);
	d = 0;

	for (i = 0; i < 16; i++) {
		d = d << 1;
		up_clk(padapter, &x);
	if (padapter->bSurpriseRemoved)
		goto out;

		x = rtw_read8(padapter, EE_9346CR);

		x &= ~(_EEDI);
		if (x & _EEDO)
		d |= 1;

		down_clk(padapter, &x);
	}
out:
_func_exit_;

	return d;
}

void standby(_adapter *padapter)
{
	u8   x;
_func_enter_;
	x = rtw_read8(padapter, EE_9346CR);

	x &= ~(_EECS | _EESK);
	rtw_write8(padapter, EE_9346CR, x);

	udelay(CLOCK_RATE);
	x |= _EECS;
	rtw_write8(padapter, EE_9346CR, x);
	udelay(CLOCK_RATE);
_func_exit_;
}

void eeprom_clean(_adapter *padapter)
{
	u16 x;
_func_enter_;
	if (padapter->bSurpriseRemoved)
		goto out;

	x = rtw_read8(padapter, EE_9346CR);
	if (padapter->bSurpriseRemoved)
		goto out;

	x &= ~(_EECS | _EEDI);
	rtw_write8(padapter, EE_9346CR, (u8)x);
	if (padapter->bSurpriseRemoved)
		goto out;

	up_clk(padapter, &x);
	if (padapter->bSurpriseRemoved)
		goto out;

	down_clk(padapter, &x);
out:
_func_exit_;
}

u16 eeprom_read16(_adapter *padapter, u16 reg) /*ReadEEprom*/
{

	u16 x;
	u16 data = 0;

_func_enter_;

	if (padapter->bSurpriseRemoved)
		goto out;

	/* select EEPROM, reset bits, set _EECS*/
	x = rtw_read8(padapter, EE_9346CR);

	if (padapter->bSurpriseRemoved)
		goto out;

	x &= ~(_EEDI | _EEDO | _EESK | _EEM0);
	x |= _EEM1 | _EECS;
	rtw_write8(padapter, EE_9346CR, (unsigned char)x);

	/* write the read opcode and register number in that order*/
	/* The opcode is 3bits in length, reg is 6 bits long*/
	shift_out_bits(padapter, EEPROM_READ_OPCODE, 3);
	shift_out_bits(padapter, reg, padapter->EepromAddressSize);

	/* Now read the data (16 bits) in from the selected EEPROM word*/
	data = shift_in_bits(padapter);

	eeprom_clean(padapter);
out:
_func_exit_;
	return data;


}

/*addr_off : address offset of the entry in eeprom (not the tuple number of eeprom (reg); that is addr_off !=reg)*/
u8 eeprom_read(_adapter *padapter, u32 addr_off, u8 sz, u8 *rbuf)
{
	u8 quotient, remainder, addr_2align_odd;
	u16 reg, stmp, i = 0, idx = 0;
_func_enter_;
	reg = (u16)(addr_off >> 1);
	addr_2align_odd = (u8)(addr_off & 0x1);

	/*read that start at high part: e.g  1,3,5,7,9,...*/
	if (addr_2align_odd) {
		stmp = eeprom_read16(padapter, reg);
		rbuf[idx++] = (u8) ((stmp>>8)&0xff); /*return hogh-part of the short*/
		reg++; sz--;
	}

	quotient = sz >> 1;
	remainder = sz & 0x1;

	for (i = 0; i < quotient; i++) {
		stmp = eeprom_read16(padapter, reg+i);
		rbuf[idx++] = (u8) (stmp&0xff);
		rbuf[idx++] = (u8) ((stmp>>8)&0xff);
	}

	reg = reg+i;
	if (remainder) { /*end of read at lower part of short : 0,2,4,6,...*/
		stmp = eeprom_read16(padapter, reg);
		rbuf[idx] = (u8)(stmp & 0xff);
	}
_func_exit_;
	return true;
}
