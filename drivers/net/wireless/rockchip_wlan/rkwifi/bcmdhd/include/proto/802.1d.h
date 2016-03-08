/*
 * Fundamental types and constants relating to 802.1D
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: 802.1d.h 518342 2014-12-01 23:21:41Z $
 */

#ifndef _802_1_D_
#define _802_1_D_

/* 802.1D priority defines */
#define	PRIO_8021D_NONE		2	/* None = - */
#define	PRIO_8021D_BK		1	/* BK - Background */
#define	PRIO_8021D_BE		0	/* BE - Best-effort */
#define	PRIO_8021D_EE		3	/* EE - Excellent-effort */
#define	PRIO_8021D_CL		4	/* CL - Controlled Load */
#define	PRIO_8021D_VI		5	/* Vi - Video */
#define	PRIO_8021D_VO		6	/* Vo - Voice */
#define	PRIO_8021D_NC		7	/* NC - Network Control */
#define	MAXPRIO			7	/* 0-7 */
#define NUMPRIO			(MAXPRIO + 1)

#define ALLPRIO		-1	/* All prioirty */

/* Converts prio to precedence since the numerical value of
 * PRIO_8021D_BE and PRIO_8021D_NONE are swapped.
 */
#define PRIO2PREC(prio) \
	(((prio) == PRIO_8021D_NONE || (prio) == PRIO_8021D_BE) ? ((prio^2)) : (prio))

#endif /* _802_1_D__ */
