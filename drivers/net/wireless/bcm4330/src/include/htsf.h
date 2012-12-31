/*
 * Time stamps for latency measurements 
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
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
 * $Id: htsf.h,v 1.1.2.4 2011-01-21 08:27:03 Exp $
 */
#ifndef _HTSF_H_
#define _HTSF_H_

#define HTSFMAGIC       	0xCDCDABAB  /* in network order for tcpdump  */
#define HTSFENDMAGIC    	0xEFEFABAB  /* to distinguish from RT2 magic */
#define HTSF_HOSTOFFSET		102
#define HTSF_DNGLOFFSET		HTSF_HOSTOFFSET	- 4
#define HTSF_DNGLOFFSET2	HTSF_HOSTOFFSET	+ 106
#define HTSF_MIN_PKTLEN 	200
#define ETHER_TYPE_BRCM_PKTDLYSTATS     0x886d

typedef enum htsfts_type {
	T10,
	T20,
	T30,
	T40,
	T50,
	T60,
	T70,
	T80,
	T90,
	TA0,
	TE0
} htsf_timestamp_t;

typedef struct {
	uint32 magic;
	uint32 prio;
	uint32 seqnum;
	uint32 misc;
	uint32 c10;
	uint32 t10;
	uint32 c20;
	uint32 t20;
	uint32 t30;
	uint32 t40;
	uint32 t50;
	uint32 t60;
	uint32 t70;
	uint32 t80;
	uint32 t90;
	uint32 cA0;
	uint32 tA0;
	uint32 cE0;
	uint32 tE0;
	uint32 endmagic;
} htsfts_t;

#endif /* _HTSF_H_ */
