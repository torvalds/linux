/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _RTW_EVENT_H_
#define _RTW_EVENT_H_
#include <drv_conf.h>
#include <osdep_service.h>

#ifndef CONFIG_RTL8711FW
#ifdef PLATFORM_LINUX
#include <wlan_bssdef.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#include <linux/sem.h>
#endif
#else
#include <wlan_bssdef.h>
#endif//CONFIG_RTL8711FW



#ifdef CONFIG_H2CLBK
#include <h2clbk.h>
#endif

/*
Used to report a bss has been scanned

*/
struct survey_event	{
	WLAN_BSSID_EX bss;
};

/*
Used to report that the requested site survey has been done.

bss_cnt indicates the number of bss that has been reported.


*/
struct surveydone_event {
	unsigned int	bss_cnt;	
	
};

/*
Used to report the link result of joinning the given bss


join_res:
-1: authentication fail
-2: association fail
> 0: TID

*/
struct joinbss_event {
	struct	wlan_network	network;
};

/*
Used to report a given STA has joinned the created BSS.
It is used in AP/Ad-HoC(M) mode.


*/
struct stassoc_event {
	unsigned char macaddr[6];
	unsigned char rsvd[2];
	int    cam_id;
	
};

struct stadel_event {
 unsigned char macaddr[6];
 unsigned char rsvd[2]; 
};

struct addba_event
{
 	unsigned int tid;
};


#ifdef CONFIG_H2CLBK
struct c2hlbk_event{
	unsigned char mac[6];
	unsigned short	s0;
	unsigned short	s1;
	unsigned int	w0;
	unsigned char	b0;
	unsigned short  s2;
	unsigned char	b1;
	unsigned int	w1;	
};
#endif//CONFIG_H2CLBK

#define GEN_EVT_CODE(event)	event ## _EVT_



struct fwevent {
	u32	parmsize;
	void (*event_callback)(_adapter *dev, u8 *pbuf);
};


#define C2HEVENT_SZ			32	

struct event_node{
	unsigned char *node;
	unsigned char evt_code;
	unsigned short evt_sz;
	volatile int	*caller_ff_tail;
	int	caller_ff_sz;
};

struct c2hevent_queue {
	volatile int	head;
	volatile int	tail;
	struct	event_node	nodes[C2HEVENT_SZ];
	unsigned char	seq;
};

#define NETWORK_QUEUE_SZ	4

struct network_queue {
	volatile int	head;
	volatile int	tail;
	WLAN_BSSID_EX networks[NETWORK_QUEUE_SZ];	
};


#endif // _WLANEVENT_H_

