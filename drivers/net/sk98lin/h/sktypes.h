/******************************************************************************
 *
 * Name:	sktypes.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.2 $
 * Date:	$Date: 2003/10/07 08:16:51 $
 * Purpose:	Define data types for Linux
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
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
 
/******************************************************************************
 *
 * Description:
 *
 * In this file, all data types that are needed by the common modules
 * are mapped to Linux data types.
 * 
 *
 * Include File Hierarchy:
 *
 *
 ******************************************************************************/

#ifndef __INC_SKTYPES_H
#define __INC_SKTYPES_H


/* defines *******************************************************************/

/*
 * Data types with a specific size. 'I' = signed, 'U' = unsigned.
 */
#define SK_I8	s8
#define SK_U8	u8
#define SK_I16	s16
#define SK_U16	u16
#define SK_I32	s32
#define SK_U32	u32
#define SK_I64	s64
#define SK_U64	u64

#define SK_UPTR	ulong		/* casting pointer <-> integral */

/*
* Boolean type.
*/
#define SK_BOOL		SK_U8
#define SK_FALSE	0
#define SK_TRUE		(!SK_FALSE)

/* typedefs *******************************************************************/

/* function prototypes ********************************************************/

#endif	/* __INC_SKTYPES_H */
