/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#define _RTW_EEPROM_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

void up_clk(_adapter	*padapter,	 u16 *x)
{
	_func_enter_;
	*x = *x | _EESK;
	rtw_write8(padapter, EE_9346CR, (u8)*x);
	rtw_udelay_os(CLOCK_RATE);

	_func_exit_;

}

void down_clk(_adapter	*padapter, u16 *x)
{
	_func_enter_;
	*x = *x & ~_EESK;
	rtw_write8(padapter, EE_9346CR, (u8)*x);
	rtw_udelay_os(CLOCK_RATE);
	_func_exit_;
}

void shift_out_bits(_adapter *padapter, u16 data, u16 count)
{
	u16 x, mask;
	_func_enter_;

	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	mask = 0x01 << (count - 1);
	x = rtw_read8(padapter, EE_9346CR);

	x &= ~(_EEDO | _EEDI);

	do {
		x &= ~_EEDI;
		if (data & mask)
			x |= _EEDI;
		if (rtw_is_surprise_removed(padapter)) {
			RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
			goto out;
		}
		rtw_write8(padapter, EE_9346CR, (u8)x);
		rtw_udelay_os(CLOCK_RATE);
		up_clk(padapter, &x);
		down_clk(padapter, &x);
		mask = mask >> 1;
	} while (mask);
	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	x &= ~_EEDI;
	rtw_write8(padapter, EE_9346CR, (u8)x);
out:
	_func_exit_;
}

u16 shift_in_bits(_adapter *padapter)
{
	u16 x, d = 0, i;
	_func_enter_;
	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	x = rtw_read8(padapter, EE_9346CR);

	x &= ~(_EEDO | _EEDI);
	d = 0;

	for (i = 0; i < 16; i++) {
		d = d << 1;
		up_clk(padapter, &x);
		if (rtw_is_surprise_removed(padapter)) {
			RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
			goto out;
		}
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

void standby(_adapter	*padapter)
{
	u8   x;
	_func_enter_;
	x = rtw_read8(padapter, EE_9346CR);

	x &= ~(_EECS | _EESK);
	rtw_write8(padapter, EE_9346CR, x);

	rtw_udelay_os(CLOCK_RATE);
	x |= _EECS;
	rtw_write8(padapter, EE_9346CR, x);
	rtw_udelay_os(CLOCK_RATE);
	_func_exit_;
}

u16 wait_eeprom_cmd_done(_adapter *padapter)
{
	u8	x;
	u16	i, res = _FALSE;
	_func_enter_;
	standby(padapter);
	for (i = 0; i < 200; i++) {
		x = rtw_read8(padapter, EE_9346CR);
		if (x & _EEDO) {
			res = _TRUE;
			goto exit;
		}
		rtw_udelay_os(CLOCK_RATE);
	}
exit:
	_func_exit_;
	return res;
}

void eeprom_clean(_adapter *padapter)
{
	u16 x;
	_func_enter_;
	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	x = rtw_read8(padapter, EE_9346CR);
	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	x &= ~(_EECS | _EEDI);
	rtw_write8(padapter, EE_9346CR, (u8)x);
	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	up_clk(padapter, &x);
	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	down_clk(padapter, &x);
out:
	_func_exit_;
}

void eeprom_write16(_adapter *padapter, u16 reg, u16 data)
{
	u8 x;
#ifdef CONFIG_RTL8712
	u8	tmp8_ori, tmp8_new, tmp8_clk_ori, tmp8_clk_new;
	tmp8_ori = rtw_read8(padapter, 0x102502f1);
	tmp8_new = tmp8_ori & 0xf7;
	if (tmp8_ori != tmp8_new) {
		rtw_write8(padapter, 0x102502f1, tmp8_new);
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("====write 0x102502f1=====\n"));
	}
	tmp8_clk_ori = rtw_read8(padapter, 0x10250003);
	tmp8_clk_new = tmp8_clk_ori | 0x20;
	if (tmp8_clk_new != tmp8_clk_ori) {
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("====write 0x10250003=====\n"));
		rtw_write8(padapter, 0x10250003, tmp8_clk_new);
	}
#endif
	_func_enter_;

	x = rtw_read8(padapter, EE_9346CR);

	x &= ~(_EEDI | _EEDO | _EESK | _EEM0);
	x |= _EEM1 | _EECS;
	rtw_write8(padapter, EE_9346CR, x);

	shift_out_bits(padapter, EEPROM_EWEN_OPCODE, 5);

	if (padapter->EepromAddressSize == 8)	/* CF+ and SDIO */
		shift_out_bits(padapter, 0, 6);
	else									/* USB */
		shift_out_bits(padapter, 0, 4);

	standby(padapter);

	/* Commented out by rcnjko, 2004.0
	 * 	  Erase this particular word.  Write the erase opcode and register
	 *    number in that order. The opcode is 3bits in length; reg is 6 bits long. */
/*	shift_out_bits(Adapter, EEPROM_ERASE_OPCODE, 3);
 *	shift_out_bits(Adapter, reg, Adapter->EepromAddressSize);
 *
 *	if (wait_eeprom_cmd_done(Adapter ) == FALSE)
 *	{
 *		return;
 *	} */


	standby(padapter);

	/* write the new word to the EEPROM */

	/* send the write opcode the EEPORM */
	shift_out_bits(padapter, EEPROM_WRITE_OPCODE, 3);

	/* select which word in the EEPROM that we are writing to. */
	shift_out_bits(padapter, reg, padapter->EepromAddressSize);

	/* write the data to the selected EEPROM word. */
	shift_out_bits(padapter, data, 16);

	if (wait_eeprom_cmd_done(padapter) == _FALSE)

		goto exit;

	standby(padapter);

	shift_out_bits(padapter, EEPROM_EWDS_OPCODE, 5);
	shift_out_bits(padapter, reg, 4);

	eeprom_clean(padapter);
exit:
#ifdef CONFIG_RTL8712
	if (tmp8_clk_new != tmp8_clk_ori)
		rtw_write8(padapter, 0x10250003, tmp8_clk_ori);
	if (tmp8_new != tmp8_ori)
		rtw_write8(padapter, 0x102502f1, tmp8_ori);

#endif
	_func_exit_;
	return;
}

u16 eeprom_read16(_adapter *padapter, u16 reg)  /* ReadEEprom */
{

	u16 x;
	u16 data = 0;
#ifdef CONFIG_RTL8712
	u8	tmp8_ori, tmp8_new, tmp8_clk_ori, tmp8_clk_new;
	tmp8_ori = rtw_read8(padapter, 0x102502f1);
	tmp8_new = tmp8_ori & 0xf7;
	if (tmp8_ori != tmp8_new) {
		rtw_write8(padapter, 0x102502f1, tmp8_new);
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("====write 0x102502f1=====\n"));
	}
	tmp8_clk_ori = rtw_read8(padapter, 0x10250003);
	tmp8_clk_new = tmp8_clk_ori | 0x20;
	if (tmp8_clk_new != tmp8_clk_ori) {
		RT_TRACE(_module_rtl871x_mp_ioctl_c_, _drv_err_, ("====write 0x10250003=====\n"));
		rtw_write8(padapter, 0x10250003, tmp8_clk_new);
	}
#endif
	_func_enter_;

	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	/* select EEPROM, reset bits, set _EECS */
	x = rtw_read8(padapter, EE_9346CR);

	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}

	x &= ~(_EEDI | _EEDO | _EESK | _EEM0);
	x |= _EEM1 | _EECS;
	rtw_write8(padapter, EE_9346CR, (unsigned char)x);

	/* write the read opcode and register number in that order */
	/* The opcode is 3bits in length, reg is 6 bits long */
	shift_out_bits(padapter, EEPROM_READ_OPCODE, 3);
	shift_out_bits(padapter, reg, padapter->EepromAddressSize);

	/* Now read the data (16 bits) in from the selected EEPROM word */
	data = shift_in_bits(padapter);

	eeprom_clean(padapter);
out:
#ifdef CONFIG_RTL8712
	if (tmp8_clk_new != tmp8_clk_ori)
		rtw_write8(padapter, 0x10250003, tmp8_clk_ori);
	if (tmp8_new != tmp8_ori)
		rtw_write8(padapter, 0x102502f1, tmp8_ori);

#endif
	_func_exit_;
	return data;


}




/* From even offset */
void eeprom_read_sz(_adapter *padapter, u16 reg, u8 *data, u32 sz)
{

	u16 x, data16;
	u32 i;
	_func_enter_;
	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}
	/* select EEPROM, reset bits, set _EECS */
	x = rtw_read8(padapter, EE_9346CR);

	if (rtw_is_surprise_removed(padapter)) {
		RT_TRACE(_module_rtl871x_eeprom_c_, _drv_err_, ("padapter->bSurpriseRemoved==_TRUE"));
		goto out;
	}

	x &= ~(_EEDI | _EEDO | _EESK | _EEM0);
	x |= _EEM1 | _EECS;
	rtw_write8(padapter, EE_9346CR, (unsigned char)x);

	/* write the read opcode and register number in that order */
	/* The opcode is 3bits in length, reg is 6 bits long */
	shift_out_bits(padapter, EEPROM_READ_OPCODE, 3);
	shift_out_bits(padapter, reg, padapter->EepromAddressSize);


	for (i = 0; i < sz; i += 2) {
		data16 = shift_in_bits(padapter);
		data[i] = data16 & 0xff;
		data[i + 1] = data16 >> 8;
	}

	eeprom_clean(padapter);
out:
	_func_exit_;



}


/* addr_off : address offset of the entry in eeprom (not the tuple number of eeprom (reg); that is addr_off !=reg) */
u8 eeprom_read(_adapter *padapter, u32 addr_off, u8 sz, u8 *rbuf)
{
	u8 quotient, remainder, addr_2align_odd;
	u16 reg, stmp , i = 0, idx = 0;
	_func_enter_;
	reg = (u16)(addr_off >> 1);
	addr_2align_odd = (u8)(addr_off & 0x1);

	if (addr_2align_odd) { /* read that start at high part: e.g  1,3,5,7,9,... */
		stmp = eeprom_read16(padapter, reg);
		rbuf[idx++] = (u8)((stmp >> 8) & 0xff); /* return hogh-part of the short */
		reg++;
		sz--;
	}

	quotient = sz >> 1;
	remainder = sz & 0x1;

	for (i = 0 ; i < quotient; i++) {
		stmp = eeprom_read16(padapter, reg + i);
		rbuf[idx++] = (u8)(stmp & 0xff);
		rbuf[idx++] = (u8)((stmp >> 8) & 0xff);
	}

	reg = reg + i;
	if (remainder) { /* end of read at lower part of short : 0,2,4,6,... */
		stmp = eeprom_read16(padapter, reg);
		rbuf[idx] = (u8)(stmp & 0xff);
	}
	_func_exit_;
	return _TRUE;
}



VOID read_eeprom_content(_adapter	*padapter)
{

	_func_enter_;


	_func_exit_;
}
