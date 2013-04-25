/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include	"rt_config.h"



/* IRQL = PASSIVE_LEVEL*/
static inline VOID RaiseClock(
    IN	PRTMP_ADAPTER	pAd,
    IN  UINT32 *x)
{
	*x = *x | EESK;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, *x);
	RTMPusecDelay(1);				/* Max frequency = 1MHz in Spec. definition */
}

/* IRQL = PASSIVE_LEVEL*/
static inline VOID LowerClock(
    IN	PRTMP_ADAPTER	pAd,
    IN  UINT32 *x)
{
	*x = *x & ~EESK;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, *x);
	RTMPusecDelay(1);
}

/* IRQL = PASSIVE_LEVEL*/
static inline USHORT ShiftInBits(
	IN PRTMP_ADAPTER	pAd)
{
	UINT32		x,i;
	USHORT      data=0;

	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

	x &= ~( EEDO | EEDI);

	for(i=0; i<16; i++)
	{
		data = data << 1;
		RaiseClock(pAd, &x);

		RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
		LowerClock(pAd, &x); /*prevent read failed*/
		
		x &= ~(EEDI);
		if(x & EEDO)
		    data |= 1;
	}

	return data;
}


/* IRQL = PASSIVE_LEVEL*/
static inline VOID ShiftOutBits(
	IN PRTMP_ADAPTER	pAd,
	IN USHORT			data,
	IN USHORT			count)
{
	UINT32       x,mask;

	mask = 0x01 << (count - 1);
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

	x &= ~(EEDO | EEDI);

	do
	{
	    x &= ~EEDI;
	    if(data & mask)		x |= EEDI;

	    RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	    RaiseClock(pAd, &x);
	    LowerClock(pAd, &x);

	    mask = mask >> 1;
	} while(mask);

	x &= ~EEDI;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);
}


/* IRQL = PASSIVE_LEVEL*/
static inline VOID EEpromCleanup(
	IN PRTMP_ADAPTER	pAd)
{
	UINT32 x;

	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

	x &= ~(EECS | EEDI);
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);	
}


static inline VOID EWEN(
	IN PRTMP_ADAPTER	pAd)
{
	UINT32	x;

	/* reset bits and set EECS*/
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
	x &= ~(EEDI | EEDO | EESK);
	x |= EECS;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	/* kick a pulse*/
	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);

	/* output the read_opcode and six pulse in that order    */
	ShiftOutBits(pAd, EEPROM_EWEN_OPCODE, 5);
	ShiftOutBits(pAd, 0, 6);

	EEpromCleanup(pAd);    
}


static inline VOID EWDS(
	IN PRTMP_ADAPTER	pAd)
{
	UINT32	x;

	/* reset bits and set EECS*/
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
	x &= ~(EEDI | EEDO | EESK);
	x |= EECS;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	/* kick a pulse*/
	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);

	/* output the read_opcode and six pulse in that order    */
	ShiftOutBits(pAd, EEPROM_EWDS_OPCODE, 5);
	ShiftOutBits(pAd, 0, 6);

	EEpromCleanup(pAd);    
}


/* IRQL = PASSIVE_LEVEL*/
int rtmp_ee_prom_read16(
	IN PRTMP_ADAPTER	pAd,
	IN USHORT			Offset,
	OUT USHORT			*pValue)
{
	UINT32		x;
	USHORT		data;

#ifdef RT30xx
#endif /* RT30xx */

	Offset /= 2;
	/* reset bits and set EECS*/
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
	x &= ~(EEDI | EEDO | EESK);
	x |= EECS;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	/* patch can not access e-Fuse issue*/
	if (!(IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd) || IS_RT3593(pAd)|| IS_RT5390(pAd)))
	{
		/* kick a pulse*/
		RaiseClock(pAd, &x);
		LowerClock(pAd, &x);
	}

	/* output the read_opcode and register number in that order    */
	ShiftOutBits(pAd, EEPROM_READ_OPCODE, 3);
	ShiftOutBits(pAd, Offset, pAd->EEPROMAddressNum);

	/* Now read the data (16 bits) in from the selected EEPROM word*/
	data = ShiftInBits(pAd);

	EEpromCleanup(pAd);

#ifdef RT30xx
#endif /* RT30xx */

	*pValue = data;

	return NDIS_STATUS_SUCCESS;
}


int rtmp_ee_prom_write16(
    IN  PRTMP_ADAPTER	pAd,
    IN  USHORT Offset,
    IN  USHORT Data)
{
	UINT32 x;

#ifdef RT30xx
#endif /* RT30xx */

	Offset /= 2;

	EWEN(pAd);

	/* reset bits and set EECS*/
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
	x &= ~(EEDI | EEDO | EESK);
	x |= EECS;
	RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	/* patch can not access e-Fuse issue*/
	if (!(IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd) || IS_RT3593(pAd) || IS_RT5390(pAd)))
	{
		/* kick a pulse*/
		RaiseClock(pAd, &x);
		LowerClock(pAd, &x);
	}
	
	/* output the read_opcode ,register number and data in that order    */
	ShiftOutBits(pAd, EEPROM_WRITE_OPCODE, 3);
	ShiftOutBits(pAd, Offset, pAd->EEPROMAddressNum);
	ShiftOutBits(pAd, Data, 16);		/* 16-bit access*/

	/* read DO status*/
	RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

	EEpromCleanup(pAd);

	RTMPusecDelay(10000);	/*delay for twp(MAX)=10ms*/

	EWDS(pAd);

	EEpromCleanup(pAd);

#ifdef RT30xx
#endif /* RT30xx */

	return NDIS_STATUS_SUCCESS;
	
}


