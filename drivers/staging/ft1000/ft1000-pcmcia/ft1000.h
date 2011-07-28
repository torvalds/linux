/*---------------------------------------------------------------------------
   FT1000 driver for Flarion Flash OFDM NIC Device

   Copyright (C) 2002 Flarion Technologies, All rights reserved.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option) any
   later version. This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details. You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place -
   Suite 330, Boston, MA 02111-1307, USA.
---------------------------------------------------------------------------
   Description:    Common structures and defines
---------------------------------------------------------------------------*/
#ifndef _FT1000H_
#define _FT1000H_

#include "../ft1000.h"

#define FT1000_DRV_VER 0x01010300

#define FT1000_DPRAM_BASE	0x0000	/* Dual Port RAM starting offset */

/* Maximum number of occurrence of pseudo header errors before resetting PC Card. */
#define MAX_PH_ERR	300

#define SUCCESS	0x00
#define FAILURE	0x01

struct ft1000_info {
	struct net_device_stats stats;
	u16 DrvErrNum;
	u16 AsicID;
	int PktIntfErr;
	int CardReady;
	int registered;
	int mediastate;
	u16 packetseqnum;
	u8 squeseqnum;			/* sequence number on slow queue */
	spinlock_t dpram_lock;
	u16 fifo_cnt;
	u8 DspVer[DSPVERSZ];		/* DSP version number */
	u8 HwSerNum[HWSERNUMSZ];	/* Hardware Serial Number */
	u8 Sku[SKUSZ];			/* SKU */
	u8 eui64[EUISZ];		/* EUI64 */
	time_t ConTm;			/* Connection Time */
	u16 LedStat;
	u16 ConStat;
	u16 ProgConStat;
	u8 ProductMode[MODESZ];
	u8 RfCalVer[CALVERSZ];
	u8 RfCalDate[CALDATESZ];
	u16 DSP_TIME[4];
	struct list_head prov_list;
	u16 DSPInfoBlklen;
	int (*ft1000_reset)(void *);
	void *link;
	u16 DSPInfoBlk[MAX_DSP_SESS_REC];
	union {
		u16 Rec[MAX_DSP_SESS_REC];
		u32 MagRec[MAX_DSP_SESS_REC/2];
	} DSPSess;
	struct proc_dir_entry *proc_ft1000;
	char netdevname[IFNAMSIZ];
};

extern u16 ft1000_read_dpram(struct net_device *dev, int offset);
extern void card_bootload(struct net_device *dev);
extern u16 ft1000_read_dpram_mag_16(struct net_device *dev, int offset, int Index);
extern u32 ft1000_read_dpram_mag_32(struct net_device *dev, int offset);
void ft1000_write_dpram_mag_32(struct net_device *dev, int offset, u32 value);

/* Read the value of a given ASIC register. */
static inline u16 ft1000_read_reg(struct net_device *dev, u16 offset)
{
	return inw(dev->base_addr + offset);
}

/* Set the value of a given ASIC register. */
static inline void ft1000_write_reg(struct net_device *dev, u16 offset, u16 value)
{
	outw(value, dev->base_addr + offset);
}

#endif
