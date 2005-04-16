/* $XFree86$ */
/* $XdotOrg$ */
/*
 * General type definitions for universal mode switching modules
 *
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License as published by
 * * the Free Software Foundation; either version 2 of the named License,
 * * or any later version.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) The name of the author may not be used to endorse or promote products
 * *    derived from this software without specific prior written permission.
 * *
 * * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

#ifndef _VGATYPES_
#define _VGATYPES_

#ifdef LINUX_KERNEL  /* We don't want the X driver to depend on kernel source */
#include <linux/ioctl.h>
#include <linux/version.h>
#endif

#ifndef FALSE
#define FALSE   0
#endif

#ifndef TRUE
#define TRUE    1
#endif

#ifndef NULL
#define NULL    0
#endif

#ifndef CHAR
typedef char CHAR;
#endif

#ifndef SHORT
typedef short SHORT;
#endif

#ifndef LONG
typedef long  LONG;
#endif

#ifndef UCHAR
typedef unsigned char UCHAR;
#endif

#ifndef USHORT
typedef unsigned short USHORT;
#endif

#ifndef ULONG
typedef unsigned long ULONG;
#endif

#ifndef BOOLEAN
typedef unsigned char BOOLEAN;
#endif

#define SISIOMEMTYPE

#ifdef LINUX_KERNEL
typedef unsigned long SISIOADDRESS;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
#include <linux/types.h>  /* Need __iomem */
#undef SISIOMEMTYPE
#define SISIOMEMTYPE __iomem
#endif
#endif

#ifdef LINUX_XF86
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,2,0,0,0)
typedef unsigned long IOADDRESS;
typedef unsigned long SISIOADDRESS;
#else
typedef IOADDRESS SISIOADDRESS;
#endif
#endif

enum _SIS_CHIP_TYPE {
    SIS_VGALegacy = 0,
    SIS_530,
    SIS_OLD,
    SIS_300,
    SIS_630,
    SIS_730,
    SIS_540,
    SIS_315H,   /* SiS 310 */
    SIS_315,
    SIS_315PRO,
    SIS_550,
    SIS_650,
    SIS_740,
    SIS_330,
    SIS_661,
    SIS_741,
    SIS_660,
    SIS_760,
    SIS_761,
    SIS_340,
    MAX_SIS_CHIP
};

#ifndef SIS_HW_INFO
typedef struct _SIS_HW_INFO  SIS_HW_INFO, *PSIS_HW_INFO;

struct _SIS_HW_INFO
{
#ifdef LINUX_XF86
    PCITAG PciTag;		 /* PCI Tag */
#endif

    UCHAR *pjVirtualRomBase;	 /* ROM image */

    BOOLEAN UseROM;		 /* Use the ROM image if provided */

#ifdef LINUX_KERNEL
    UCHAR SISIOMEMTYPE *pjVideoMemoryAddress;
    				 /* base virtual memory address */
                                 /* of Linear VGA memory */

    ULONG  ulVideoMemorySize;    /* size, in bytes, of the memory on the board */
#endif

    SISIOADDRESS ulIOAddress;    /* base I/O address of VGA ports (0x3B0; relocated) */

    UCHAR  jChipType;            /* Used to Identify SiS Graphics Chip */
                                 /* defined in the enum "SIS_CHIP_TYPE" (above or sisfb.h) */

    UCHAR  jChipRevision;        /* Used to Identify SiS Graphics Chip Revision */

    BOOLEAN bIntegratedMMEnabled;/* supporting integration MM enable */
};
#endif

/* Addtional IOCTLs for communication sisfb <> X driver        */
/* If changing this, sisfb.h must also be changed (for sisfb) */

#ifdef LINUX_XF86  /* We don't want the X driver to depend on the kernel source */

/* ioctl for identifying and giving some info (esp. memory heap start) */
#define SISFB_GET_INFO_SIZE	0x8004f300
#define SISFB_GET_INFO		0x8000f301  /* Must be patched with result from ..._SIZE at D[29:16] */
/* deprecated ioctl number (for older versions of sisfb) */
#define SISFB_GET_INFO_OLD    	0x80046ef8

/* ioctls for tv parameters (position) */
#define SISFB_SET_TVPOSOFFSET   0x4004f304

/* lock sisfb from register access */
#define SISFB_SET_LOCK		0x4004f306

/* Structure argument for SISFB_GET_INFO ioctl  */
typedef struct _SISFB_INFO sisfb_info, *psisfb_info;

struct _SISFB_INFO {
	CARD32 	sisfb_id;         	/* for identifying sisfb */
#ifndef SISFB_ID
#define SISFB_ID	  0x53495346    /* Identify myself with 'SISF' */
#endif
 	CARD32 	chip_id;		/* PCI ID of detected chip */
	CARD32	memory;			/* video memory in KB which sisfb manages */
	CARD32	heapstart;             	/* heap start (= sisfb "mem" argument) in KB */
	CARD8 	fbvidmode;		/* current sisfb mode */

	CARD8 	sisfb_version;
	CARD8	sisfb_revision;
	CARD8 	sisfb_patchlevel;

	CARD8 	sisfb_caps;		/* sisfb's capabilities */

	CARD32 	sisfb_tqlen;		/* turbo queue length (in KB) */

	CARD32 	sisfb_pcibus;      	/* The card's PCI ID */
	CARD32 	sisfb_pcislot;
	CARD32 	sisfb_pcifunc;

	CARD8 	sisfb_lcdpdc;

	CARD8	sisfb_lcda;

	CARD32	sisfb_vbflags;
	CARD32	sisfb_currentvbflags;

	CARD32 	sisfb_scalelcd;
	CARD32 	sisfb_specialtiming;

	CARD8 	sisfb_haveemi;
	CARD8 	sisfb_emi30,sisfb_emi31,sisfb_emi32,sisfb_emi33;
	CARD8 	sisfb_haveemilcd;

	CARD8 	sisfb_lcdpdca;

	CARD16  sisfb_tvxpos, sisfb_tvypos;  	/* Warning: Values + 32 ! */

	CARD8 reserved[208]; 			/* for future use */
};
#endif

#endif

