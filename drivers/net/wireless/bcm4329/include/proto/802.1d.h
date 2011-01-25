/*
 * Copyright (C) 1999-2010, Broadcom Corporation
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
 * Fundamental types and constants relating to 802.1D
 *
 * $Id: 802.1d.h,v 9.3 2007/04/10 21:33:06 Exp $
 */


#ifndef _802_1_D_
#define _802_1_D_


#define	PRIO_8021D_NONE		2	
#define	PRIO_8021D_BK		1	
#define	PRIO_8021D_BE		0	
#define	PRIO_8021D_EE		3	
#define	PRIO_8021D_CL		4	
#define	PRIO_8021D_VI		5	
#define	PRIO_8021D_VO		6	
#define	PRIO_8021D_NC		7	
#define	MAXPRIO			7	
#define NUMPRIO			(MAXPRIO + 1)

#define ALLPRIO		-1	


#define PRIO2PREC(prio) \
	(((prio) == PRIO_8021D_NONE || (prio) == PRIO_8021D_BE) ? ((prio^2)) : (prio))

#endif 
