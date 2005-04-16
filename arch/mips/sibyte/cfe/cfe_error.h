/*
 * Copyright (C) 2000, 2001, 2002 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*  *********************************************************************
    *  
    *  Broadcom Common Firmware Environment (CFE)
    *  
    *  Error codes				File: cfe_error.h
    *  
    *  CFE's global error code list is here.
    *  
    *  Author:  Mitch Lichtenberg
    *  
    ********************************************************************* */


#define CFE_OK			 0
#define CFE_ERR                 -1	/* generic error */
#define CFE_ERR_INV_COMMAND	-2
#define CFE_ERR_EOF		-3
#define CFE_ERR_IOERR		-4
#define CFE_ERR_NOMEM		-5
#define CFE_ERR_DEVNOTFOUND	-6
#define CFE_ERR_DEVOPEN		-7
#define CFE_ERR_INV_PARAM	-8
#define CFE_ERR_ENVNOTFOUND	-9
#define CFE_ERR_ENVREADONLY	-10

#define CFE_ERR_NOTELF		-11
#define CFE_ERR_NOT32BIT 	-12
#define CFE_ERR_WRONGENDIAN 	-13
#define CFE_ERR_BADELFVERS 	-14
#define CFE_ERR_NOTMIPS 	-15
#define CFE_ERR_BADELFFMT 	-16
#define CFE_ERR_BADADDR 	-17

#define CFE_ERR_FILENOTFOUND	-18
#define CFE_ERR_UNSUPPORTED	-19

#define CFE_ERR_HOSTUNKNOWN	-20

#define CFE_ERR_TIMEOUT		-21

#define CFE_ERR_PROTOCOLERR	-22

#define CFE_ERR_NETDOWN		-23
#define CFE_ERR_NONAMESERVER	-24

#define CFE_ERR_NOHANDLES	-25
#define CFE_ERR_ALREADYBOUND	-26

#define CFE_ERR_CANNOTSET	-27
#define CFE_ERR_NOMORE		-28
#define CFE_ERR_BADFILESYS	-29
#define CFE_ERR_FSNOTAVAIL	-30

#define CFE_ERR_INVBOOTBLOCK	-31
#define CFE_ERR_WRONGDEVTYPE	-32
#define CFE_ERR_BBCHECKSUM	-33
#define CFE_ERR_BOOTPROGCHKSUM	-34

#define CFE_ERR_LDRNOTAVAIL	-35

#define CFE_ERR_NOTREADY	-36

#define CFE_ERR_GETMEM          -37
#define CFE_ERR_SETMEM          -38

#define CFE_ERR_NOTCONN		-39
#define CFE_ERR_ADDRINUSE	-40
