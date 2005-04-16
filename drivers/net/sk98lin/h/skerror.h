/******************************************************************************
 *
 * Name:	skerror.h
 * Project:	Gigabit Ethernet Adapters, Common Modules
 * Version:	$Revision: 1.7 $
 * Date:	$Date: 2003/05/13 17:25:13 $
 * Purpose:	SK specific Error log support
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

#ifndef _INC_SKERROR_H_
#define _INC_SKERROR_H_

/*
 * Define Error Classes
 */
#define	SK_ERRCL_OTHER		(0)		/* Other error */
#define	SK_ERRCL_CONFIG		(1L<<0)	/* Configuration error */
#define	SK_ERRCL_INIT		(1L<<1)	/* Initialization error */
#define	SK_ERRCL_NORES		(1L<<2)	/* Out of Resources error */
#define	SK_ERRCL_SW			(1L<<3)	/* Internal Software error */
#define	SK_ERRCL_HW			(1L<<4)	/* Hardware Failure */
#define	SK_ERRCL_COMM		(1L<<5)	/* Communication error */


/*
 * Define Error Code Bases
 */
#define	SK_ERRBASE_RLMT		 100	/* Base Error number for RLMT */
#define	SK_ERRBASE_HWINIT	 200	/* Base Error number for HWInit */
#define	SK_ERRBASE_VPD		 300	/* Base Error number for VPD */
#define	SK_ERRBASE_PNMI		 400	/* Base Error number for PNMI */
#define	SK_ERRBASE_CSUM		 500	/* Base Error number for Checksum */
#define	SK_ERRBASE_SIRQ		 600	/* Base Error number for Special IRQ */
#define	SK_ERRBASE_I2C		 700	/* Base Error number for I2C module */
#define	SK_ERRBASE_QUEUE	 800	/* Base Error number for Scheduler */
#define	SK_ERRBASE_ADDR		 900	/* Base Error number for Address module */
#define SK_ERRBASE_PECP		1000    /* Base Error number for PECP */
#define	SK_ERRBASE_DRV		1100	/* Base Error number for Driver */

#endif	/* _INC_SKERROR_H_ */
