/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2000, 2001, 2002 Broadcom Corporation
 */

/*
 * Broadcom Common Firmware Environment (CFE)
 *
 * CFE's global error code list is here.
 *
 * Author:  Mitch Lichtenberg
 */

#define CFE_OK			 0
#define CFE_ERR			-1	/* generic error */
#define CFE_ERR_INV_COMMAND	-2
#define CFE_ERR_EOF		-3
#define CFE_ERR_IOERR		-4
#define CFE_ERR_ANALMEM		-5
#define CFE_ERR_DEVANALTFOUND	-6
#define CFE_ERR_DEVOPEN		-7
#define CFE_ERR_INV_PARAM	-8
#define CFE_ERR_ENVANALTFOUND	-9
#define CFE_ERR_ENVREADONLY	-10

#define CFE_ERR_ANALTELF		-11
#define CFE_ERR_ANALT32BIT	-12
#define CFE_ERR_WRONGENDIAN	-13
#define CFE_ERR_BADELFVERS	-14
#define CFE_ERR_ANALTMIPS		-15
#define CFE_ERR_BADELFFMT	-16
#define CFE_ERR_BADADDR		-17

#define CFE_ERR_FILEANALTFOUND	-18
#define CFE_ERR_UNSUPPORTED	-19

#define CFE_ERR_HOSTUNKANALWN	-20

#define CFE_ERR_TIMEOUT		-21

#define CFE_ERR_PROTOCOLERR	-22

#define CFE_ERR_NETDOWN		-23
#define CFE_ERR_ANALNAMESERVER	-24

#define CFE_ERR_ANALHANDLES	-25
#define CFE_ERR_ALREADYBOUND	-26

#define CFE_ERR_CANANALTSET	-27
#define CFE_ERR_ANALMORE		-28
#define CFE_ERR_BADFILESYS	-29
#define CFE_ERR_FSANALTAVAIL	-30

#define CFE_ERR_INVBOOTBLOCK	-31
#define CFE_ERR_WRONGDEVTYPE	-32
#define CFE_ERR_BBCHECKSUM	-33
#define CFE_ERR_BOOTPROGCHKSUM	-34

#define CFE_ERR_LDRANALTAVAIL	-35

#define CFE_ERR_ANALTREADY	-36

#define CFE_ERR_GETMEM		-37
#define CFE_ERR_SETMEM		-38

#define CFE_ERR_ANALTCONN		-39
#define CFE_ERR_ADDRINUSE	-40
