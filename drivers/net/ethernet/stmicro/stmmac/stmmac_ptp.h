/******************************************************************************
  PTP Header file

  Copyright (C) 2013  Vayavya Labs Pvt Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Rayagond Kokatanur <rayagond@vayavyalabs.com>
******************************************************************************/

#ifndef __STMMAC_PTP_H__
#define __STMMAC_PTP_H__

#define STMMAC_SYSCLOCK 62500000

/* IEEE 1588 PTP register offsets */
#define PTP_TCR		0x0700	/* Timestamp Control Reg */
#define PTP_SSIR	0x0704	/* Sub-Second Increment Reg */
#define PTP_STSR	0x0708	/* System Time – Seconds Regr */
#define PTP_STNSR	0x070C	/* System Time – Nanoseconds Reg */
#define PTP_STSUR	0x0710	/* System Time – Seconds Update Reg */
#define PTP_STNSUR	0x0714	/* System Time – Nanoseconds Update Reg */
#define PTP_TAR		0x0718	/* Timestamp Addend Reg */
#define PTP_TTSR	0x071C	/* Target Time Seconds Reg */
#define PTP_TTNSR	0x0720	/* Target Time Nanoseconds Reg */
#define	PTP_STHWSR	0x0724	/* System Time - Higher Word Seconds Reg */
#define PTP_TSR		0x0728	/* Timestamp Status */

#define PTP_STNSUR_ADDSUB_SHIFT 31

/* PTP TCR defines */
#define PTP_TCR_TSENA		0x00000001 /* Timestamp Enable */
#define PTP_TCR_TSCFUPDT	0x00000002 /* Timestamp Fine/Coarse Update */
#define PTP_TCR_TSINIT		0x00000004 /* Timestamp Initialize */
#define PTP_TCR_TSUPDT		0x00000008 /* Timestamp Update */
/* Timestamp Interrupt Trigger Enable */
#define PTP_TCR_TSTRIG		0x00000010
#define PTP_TCR_TSADDREG	0x00000020 /* Addend Reg Update */
#define PTP_TCR_TSENALL		0x00000100 /* Enable Timestamp for All Frames */
/* Timestamp Digital or Binary Rollover Control */
#define PTP_TCR_TSCTRLSSR	0x00000200

/* Enable PTP packet Processing for Version 2 Format */
#define PTP_TCR_TSVER2ENA	0x00000400
/* Enable Processing of PTP over Ethernet Frames */
#define PTP_TCR_TSIPENA		0x00000800
/* Enable Processing of PTP Frames Sent over IPv6-UDP */
#define PTP_TCR_TSIPV6ENA	0x00001000
/* Enable Processing of PTP Frames Sent over IPv4-UDP */
#define PTP_TCR_TSIPV4ENA	0x00002000
/* Enable Timestamp Snapshot for Event Messages */
#define PTP_TCR_TSEVNTENA	0x00004000
/* Enable Snapshot for Messages Relevant to Master */
#define PTP_TCR_TSMSTRENA	0x00008000
/* Select PTP packets for Taking Snapshots */
#define PTP_TCR_SNAPTYPSEL_1	0x00010000
/* Enable MAC address for PTP Frame Filtering */
#define PTP_TCR_TSENMACADDR	0x00040000

#endif /* __STMMAC_PTP_H__ */
