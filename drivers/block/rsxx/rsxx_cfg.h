/*
* Filename: rsXX_cfg.h
*
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software Foundation,
* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __RSXX_CFG_H__
#define __RSXX_CFG_H__

/* NOTE: Config values will be saved in network byte order (i.e. Big endian) */
#include <linux/types.h>

/*
 * The card config version must match the driver's expected version. If it does
 * not, the DMA interfaces will not be attached and the user will need to
 * initialize/upgrade the card configuration using the card config utility.
 */
#define RSXX_CFG_VERSION	4

struct card_cfg_hdr {
	__u32	version;
	__u32	crc;
};

struct card_cfg_data {
	__u32	block_size;
	__u32	stripe_size;
	__u32	vendor_id;
	__u32	cache_order;
	struct {
		__u32	mode;	/* Disabled, manual, auto-tune... */
		__u32	count;	/* Number of intr to coalesce     */
		__u32	latency;/* Max wait time (in ns)          */
	} intr_coal;
};

struct rsxx_card_cfg {
	struct card_cfg_hdr	hdr;
	struct card_cfg_data	data;
};

/* Vendor ID Values */
#define RSXX_VENDOR_ID_IBM		0
#define RSXX_VENDOR_ID_DSI		1
#define RSXX_VENDOR_COUNT		2

/* Interrupt Coalescing Values */
#define RSXX_INTR_COAL_DISABLED           0
#define RSXX_INTR_COAL_EXPLICIT           1
#define RSXX_INTR_COAL_AUTO_TUNE          2


#endif /* __RSXX_CFG_H__ */

