/******************************************************************************
 *
 * Name:	version.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.5 $
 * Date:	$Date: 2003/10/07 08:16:51 $
 * Purpose:	SK specific Error log support
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

#ifdef	lint
static const char SysKonnectFileId[] = "@(#) (C) SysKonnect GmbH.";
static const char SysKonnectBuildNumber[] =
	"@(#)SK-BUILD: 6.23 PL: 01"; 
#endif	/* !defined(lint) */

#define BOOT_STRING	"sk98lin: Network Device Driver v6.23\n" \
			"(C)Copyright 1999-2004 Marvell(R)."

#define VER_STRING	"6.23"
#define DRIVER_FILE_NAME	"sk98lin"
#define DRIVER_REL_DATE		"Feb-13-2004"


