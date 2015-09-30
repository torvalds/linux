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

/*
 * Maximum number of occurrence of pseudo header errors before resetting PC
 * Card.
 */
#define MAX_PH_ERR	300

#define SUCCESS	0x00
#define FAILURE	0x01

struct ft1000_pcmcia {
	int PktIntfErr;
	u16 packetseqnum;
	void *link;
};

struct pcmcia_device;
struct net_device;
struct net_device *init_ft1000_card(struct pcmcia_device *link,
				    void *ft1000_reset);
void stop_ft1000_card(struct net_device *dev);
int card_download(struct net_device *dev, const u8 *pFileStart,
		  size_t FileLength);

u16 ft1000_read_dpram(struct net_device *dev, int offset);
void card_bootload(struct net_device *dev);
u16 ft1000_read_dpram_mag_16(struct net_device *dev, int offset, int Index);
u32 ft1000_read_dpram_mag_32(struct net_device *dev, int offset);
void ft1000_write_dpram_mag_32(struct net_device *dev, int offset, u32 value);

/* Read the value of a given ASIC register. */
static inline u16 ft1000_read_reg(struct net_device *dev, u16 offset)
{
	return inw(dev->base_addr + offset);
}

/* Set the value of a given ASIC register. */
static inline void ft1000_write_reg(struct net_device *dev, u16 offset,
				    u16 value)
{
	outw(value, dev->base_addr + offset);
}

#endif
