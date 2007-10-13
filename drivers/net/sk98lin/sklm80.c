/******************************************************************************
 *
 * Name:	sklm80.c
 * Project:	Gigabit Ethernet Adapters, TWSI-Module
 * Version:	$Revision: 1.22 $
 * Date:	$Date: 2003/10/20 09:08:21 $
 * Purpose:	Functions to access Voltage and Temperature Sensor (LM80)
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
	LM80 functions
*/
#if (defined(DEBUG) || ((!defined(LINT)) && (!defined(SK_SLIM))))
static const char SysKonnectFileId[] =
	"@(#) $Id: sklm80.c,v 1.22 2003/10/20 09:08:21 rschmidt Exp $ (C) Marvell. ";
#endif

#include "h/skdrv1st.h"		/* Driver Specific Definitions */
#include "h/lm80.h"
#include "h/skdrv2nd.h"		/* Adapter Control- and Driver specific Def. */

#define	BREAK_OR_WAIT(pAC,IoC,Event)	break

/*
 * read a sensors value (LM80 specific)
 *
 * This function reads a sensors value from the I2C sensor chip LM80.
 * The sensor is defined by its index into the sensors database in the struct
 * pAC points to.
 *
 * Returns	1 if the read is completed
 *		0 if the read must be continued (I2C Bus still allocated)
 */
int SkLm80ReadSensor(
SK_AC		*pAC,	/* Adapter Context */
SK_IOC		IoC,	/* I/O Context needed in level 1 and 2 */
SK_SENSOR	*pSen)	/* Sensor to be read */
{
	SK_I32		Value;

	switch (pSen->SenState) {
	case SK_SEN_IDLE:
		/* Send address to ADDR register */
		SK_I2C_CTL(IoC, I2C_READ, pSen->SenDev, I2C_025K_DEV, pSen->SenReg, 0);

		pSen->SenState = SK_SEN_VALUE ;
		BREAK_OR_WAIT(pAC, IoC, I2C_READ);
	
	case SK_SEN_VALUE:
		/* Read value from data register */
		SK_IN32(IoC, B2_I2C_DATA, ((SK_U32 *)&Value));
		
		Value &= 0xff; /* only least significant byte is valid */

		/* Do NOT check the Value against the thresholds */
		/* Checking is done in the calling instance */

		if (pSen->SenType == SK_SEN_VOLT) {
			/* Voltage sensor */
			pSen->SenValue = Value * SK_LM80_VT_LSB;
			pSen->SenState = SK_SEN_IDLE ;
			return(1);
		}

		if (pSen->SenType == SK_SEN_FAN) {
			if (Value != 0 && Value != 0xff) {
				/* Fan speed counter */
				pSen->SenValue = SK_LM80_FAN_FAKTOR/Value;
			}
			else {
				/* Indicate Fan error */
				pSen->SenValue = 0;
			}
			pSen->SenState = SK_SEN_IDLE ;
			return(1);
		}

		/* First: correct the value: it might be negative */
		if ((Value & 0x80) != 0) {
			/* Value is negative */
			Value = Value - 256;
		}

		/* We have a temperature sensor and need to get the signed extension.
		 * For now we get the extension from the last reading, so in the normal
		 * case we won't see flickering temperatures.
		 */
		pSen->SenValue = (Value * SK_LM80_TEMP_LSB) +
			(pSen->SenValue % SK_LM80_TEMP_LSB);

		/* Send address to ADDR register */
		SK_I2C_CTL(IoC, I2C_READ, pSen->SenDev, I2C_025K_DEV, LM80_TEMP_CTRL, 0);

		pSen->SenState = SK_SEN_VALEXT ;
		BREAK_OR_WAIT(pAC, IoC, I2C_READ);
	
	case SK_SEN_VALEXT:
		/* Read value from data register */
		SK_IN32(IoC, B2_I2C_DATA, ((SK_U32 *)&Value));
		Value &= LM80_TEMP_LSB_9; /* only bit 7 is valid */

		/* cut the LSB bit */
		pSen->SenValue = ((pSen->SenValue / SK_LM80_TEMP_LSB) *
			SK_LM80_TEMP_LSB);

		if (pSen->SenValue < 0) {
			/* Value negative: The bit value must be subtracted */
			pSen->SenValue -= ((Value >> 7) * SK_LM80_TEMPEXT_LSB);
		}
		else {
			/* Value positive: The bit value must be added */
			pSen->SenValue += ((Value >> 7) * SK_LM80_TEMPEXT_LSB);
		}

		pSen->SenState = SK_SEN_IDLE ;
		return(1);
	
	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_I2C_E007, SKERR_I2C_E007MSG);
		return(1);
	}

	/* Not completed */
	return(0);
}

