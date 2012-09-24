/*
 * Copyright (C) 1999-2012, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: epivers.h.in,v 13.33 2010-09-08 22:08:53 $
 *
*/

#ifndef _epivers_h_
#define _epivers_h_

#define	EPI_MAJOR_VERSION	1

#define	EPI_MINOR_VERSION	28

#define	EPI_RC_NUMBER		13

#define	EPI_INCREMENTAL_NUMBER	1

#define	EPI_BUILD_NUMBER	0

#define	EPI_VERSION		1, 28, 13, 1

#define	EPI_VERSION_NUM		0x011c0d01

#define EPI_VERSION_DEV		1.28.13

/* Driver Version String, ASCII, 32 chars max */
#ifdef BCMINTERNAL
#define	EPI_VERSION_STR		"1.28.13.1 (r BCMINT)"
#else
#ifdef WLTEST
#define	EPI_VERSION_STR		"1.28.13.1 (r WLTEST)"
#else
#define	EPI_VERSION_STR		"1.28.13.1 (r)"
#endif
#endif /* BCMINTERNAL */

#endif /* _epivers_h_ */
