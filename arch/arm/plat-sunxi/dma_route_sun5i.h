/*
 * sun5i accepted dma routes
 * dma private header
 * (C) Copyright 2013
 * Alexsey Shestacov <wingrimen@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#ifndef __DMA_ROUTES__
#define __DMA_ROUTES__
#include <plat/dma_defs.h>


static struct sw_dma_map __initdata sw_dma_mappings[DMACH_MAX] = {
	[DMACH_NSPI0] = {
		.name		= "spi0",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NSPI1] = {
		.name		= "spi1",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NSPI2] = {
		.name		= "spi2",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUART0] = {
		.name		= "uart0",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUART1] = {
		.name		= "uart1",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUART2] = {
		.name		= "uart2",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
			0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUART3] = {
		.name		= "uart3",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
			0,0,0,0,0,0,0,0,},
	},
	[DMACH_NSRAM] = {
		.name		= "nsram",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,0,0,0,0,0,0,0,0},
	},
	[DMACH_NSDRAM] = {
		.name		= "nsdram",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,0,0,0,0,0,0,0,0},
	},
	[DMACH_NTPAD] = {
		.name		= "tpadc",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,0,0,0,0,0,0,0,0},
	},
	[DMACH_NADDA_PLAY] = {
		.name		= "adda_play",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NADDA_CAPTURE] = {
		.name		= "adda_capture",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NIIS] = {
		.name		= "iis",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NIIS_CAPTURE] = {
		.name		= "iis_capture",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NIR] = {
		.name		= "ir",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NSPDIF] = {
		.name		= "spdif",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NHDMI] = {
		.name		= "hdmi",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUSBEP1] = {
		.name		= "usbep1",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUSBEP2] = {
		.name		= "usbep2",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUSBEP3] = {
		.name		= "usbep3",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUSBEP4] = {
		.name		= "usbep4",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUSBEP5] = {
		.name		= "usbep5",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			     0,0,0,0,0,0,0,0,},
	},
	[DMACH_DSRAM] = {
		.name		= "dsram",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DSDRAM] = {
		.name		= "dsdram",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DNAND] = {
		.name		= "dnand",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DUSB0] = {
		.name		= "usb0",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DEMACR] = {
		.name		= "EMACRX_DMA",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DEMACT] = {
		.name		= "EMACTX_DMA",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DSPI1] = {
		.name		= "dspi1",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DSSR] = {
		.name		= "dssr",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DSST] = {
		.name		= "dsst",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_TCON0] = {
		.name		= "tcon0",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_HDMIAUDIO] = {
		.name		= "hdmiaudio",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DMS] = {
		.name		= "dms",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DSPI0] = {
		.name		= "dspi0",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DSPI2] = {
		.name		= "dspi2",
		.channels = {0,0,0,0,0,0,0,0,
			     DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
};


unsigned long xfer_arr[DMAXFER_MAX]={
	/*des:X_SIGLE  src:X_SIGLE*/
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_WORD << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_WORD << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_WORD << 9),

	/*des:X_SIGLE   src:X_BURST*/
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_WORD << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_WORD << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_WORD << 9),

	/*des:X_SIGLE   src:X_TIPPL*/
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_WORD << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_WORD << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_SIGLE << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_WORD << 9),

	/*des:X_BURST  src:X_BURST*/
	(X_BURST << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_WORD << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_WORD << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_WORD << 9),

	/*des:X_BURST   src:X_SIGLE*/
	(X_BURST << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_WORD << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_WORD << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_WORD << 9),

	/*des:X_BURST   src:X_TIPPL*/
	(X_BURST << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_WORD << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_WORD << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_BURST << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_WORD << 9),

	/*des:X_TIPPL   src:X_TIPPL*/
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_TIPPL <<7) | (X_WORD << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_TIPPL <<7) | (X_WORD << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_TIPPL <<7) | (X_WORD << 9),

	/*des:X_TIPPL   src:X_SIGLE*/
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_SIGLE <<7) | (X_WORD << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_SIGLE <<7) | (X_WORD << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_SIGLE <<7) | (X_WORD << 9),

	/*des:X_TIPPL   src:X_BURST*/
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_BYTE << 25) | (X_BURST <<7) | (X_WORD << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_HALF << 25) | (X_BURST <<7) | (X_WORD << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_BYTE << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_HALF << 9),
	(X_TIPPL << 23) | (X_WORD << 25) | (X_BURST <<7) | (X_WORD << 9),
};

unsigned long addrtype_arr[DMAADDRT_MAX]={
	(A_INC << 21) | (A_INC << 5),
	(A_INC << 21) | (A_FIX << 5),
	(A_FIX << 21) | (A_INC << 5),
	(A_FIX << 21) | (A_FIX << 5),

	(A_LN  << 21) | (A_LN  << 5),
	(A_LN  << 21) | (A_IO  << 5),
	(A_LN  << 21) | (A_PH  << 5),
	(A_LN  << 21) | (A_PV  << 5),

	(A_IO  << 21) | (A_LN  << 5),
	(A_IO  << 21) | (A_IO  << 5),
	(A_IO  << 21) | (A_PH  << 5),
	(A_IO  << 21) | (A_PV  << 5),

	(A_PH  << 21) | (A_LN  << 5),
	(A_PH  << 21) | (A_IO  << 5),
	(A_PH  << 21) | (A_PH  << 5),
	(A_PH  << 21) | (A_PV  << 5),

	(A_PV  << 21) | (A_LN  << 5),
	(A_PV  << 21) | (A_IO  << 5),
	(A_PV  << 21) | (A_PH  << 5),
	(A_PV  << 21) | (A_PV  << 5),
};

unsigned long n_drqsrc_arr[DRQ_TYPE_MAX]={
	N_DRQSRC_SRAM,       		//DRQ_TYPE_SRAM
	N_DRQSRC_SDRAM,      		//DRQ_TYPE_SDRAM
	DRQ_INVALID,         		//DRQ_TYPE_NAND
	DRQ_INVALID,         		//DRQ_TYPE_USB0
	DRQ_INVALID,         		//DRQ_TYPE_EMAC
	N_DRQSRC_SPI1RX,         	//DRQ_TYPE_SPI1
	DRQ_INVALID,         		//DRQ_TYPE_SS
	DRQ_INVALID,         		//DRQ_TYPE_MS
	N_DRQSRC_SPI0RX,       		//DRQ_TYPE_SPI0
	N_DRQSRC_SPI2RX,       		//DRQ_TYPE_SPI2
	DRQ_INVALID,				//DRQ_TYPE_TCON0
	DRQ_INVALID,				//DRQ_TYPE_HDMIAUDIO
	N_DRQSRC_HDMIDDCRX,			//DRQ_TYPE_HDMI

	N_DRQSRC_IRRX,       		//DRQ_TYPE_IR
	N_DRQSRC_SPDIFRX,      		//DRQ_TYPE_SPDIF
	N_DRQSRC_IISRX,     		//DRQ_TYPE_IIS
	N_DRQSRC_UART0RX,     		//DRQ_TYPE_UART0
	N_DRQSRC_UART1RX,			//DRQ_TYPE_UART1
	N_DRQSRC_UART2RX,    		//DRQ_TYPE_UART2
	N_DRQSRC_UART3RX,    		//DRQ_TYPE_UART3
	N_DRQSRC_AUDIOCDAD,    		//DRQ_TYPE_AUDIO
	N_DRQSRC_TPAD,	    		//DRQ_TYPE_TPAD
	N_DRQSRC_USBEP1,			//DRQ_TYPE_USBEP1
	N_DRQSRC_USBEP2,			//DRQ_TYPE_USBEP2
	N_DRQSRC_USBEP3,			//DRQ_TYPE_USBEP3
	N_DRQSRC_USBEP4,			//DRQ_TYPE_USBEP4
	N_DRQSRC_USBEP5,			//DRQ_TYPE_USBEP5
};

unsigned long n_drqdst_arr[DRQ_TYPE_MAX]={
	N_DRQDST_SRAM,       	//DRQ_TYPE_SRAM
	N_DRQDST_SDRAM,      	//DRQ_TYPE_SDRAM
	DRQ_INVALID,         	//DRQ_TYPE_NAND
	DRQ_INVALID,         	//DRQ_TYPE_USB0
	DRQ_INVALID,         	//DRQ_TYPE_EMAC
	N_DRQDST_SPI1TX,       	//DRQ_TYPE_SPI1
	DRQ_INVALID,         	//DRQ_TYPE_SS
	DRQ_INVALID,         	//DRQ_TYPE_MS
	N_DRQDST_SPI0TX,       	//DRQ_TYPE_SPI0
	N_DRQDST_SPI2TX,       	//DRQ_TYPE_SPI2
	DRQ_INVALID,			//DRQ_TYPE_TCON0
	DRQ_INVALID,			//DRQ_TYPE_HDMIAUDIO
	N_DRQDST_HDMIDDCTX,		//DRQ_TYPE_HDMI

	N_DRQDST_IRTX,       	//DRQ_TYPE_IR
	N_DRQDST_SPDIFTX,      	//DRQ_TYPE_SPDIF
	N_DRQDST_IISTX,     	//DRQ_TYPE_IIS
	N_DRQDST_UART0TX,     	//DRQ_TYPE_UART0
	N_DRQDST_UART1TX,		//DRQ_TYPE_UART1
	N_DRQDST_UART2TX,    	//DRQ_TYPE_UART2
	N_DRQDST_UART3TX,    	//DRQ_TYPE_UART3
	N_DRQDST_AUDIOCDAD,    	//DRQ_TYPE_AUDIO
	DRQ_INVALID,	    	//DRQ_TYPE_TPAD
	N_DRQDST_USBEP1,		//DRQ_TYPE_USBEP1
	N_DRQDST_USBEP2,		//DRQ_TYPE_USBEP2
	N_DRQDST_USBEP3,		//DRQ_TYPE_USBEP3
	N_DRQDST_USBEP4,		//DRQ_TYPE_USBEP4
	N_DRQDST_USBEP5,		//DRQ_TYPE_USBEP5
};

unsigned long d_drqsrc_arr[DRQ_TYPE_MAX]={
	D_DRQSRC_SRAM,       	//DRQ_TYPE_SRAM
	D_DRQSRC_SDRAM,      	//DRQ_TYPE_SDRAM
	D_DRQSRC_NAND,         	//DRQ_TYPE_NAND
	D_DRQSRC_USB0,         	//DRQ_TYPE_USB0
	D_DRQSRC_EMACRX,       	//DRQ_TYPE_EMAC
	D_DRQSRC_SPI1RX,       	//DRQ_TYPE_SPI1
	D_DRQSRC_SECRX,       	//DRQ_TYPE_SS
	D_DRQSRC_MS,         	//DRQ_TYPE_MS
	D_DRQSRC_SPI0RX,       	//DRQ_TYPE_SPI0
	D_DRQSRC_SPI2RX,       	//DRQ_TYPE_SPI2
	DRQ_INVALID,			//DRQ_TYPE_TCON0
	DRQ_INVALID,			//DRQ_TYPE_HDMIAUDIO
	DRQ_INVALID,			//DRQ_TYPE_HDMI

	DRQ_INVALID,       		//DRQ_TYPE_IR
	DRQ_INVALID,      		//DRQ_TYPE_SPDIF
	DRQ_INVALID,     		//DRQ_TYPE_IIS
	DRQ_INVALID,     		//DRQ_TYPE_UART0
	DRQ_INVALID,			//DRQ_TYPE_UART1
	DRQ_INVALID,    		//DRQ_TYPE_UART2
	DRQ_INVALID,    		//DRQ_TYPE_UART3
	DRQ_INVALID,    		//DRQ_TYPE_AUDIO
	DRQ_INVALID,	    	//DRQ_TYPE_TPAD
	DRQ_INVALID,			//DRQ_TYPE_USBEP1
	DRQ_INVALID,			//DRQ_TYPE_USBEP2
	DRQ_INVALID,			//DRQ_TYPE_USBEP3
	DRQ_INVALID,			//DRQ_TYPE_USBEP4
	DRQ_INVALID,			//DRQ_TYPE_USBEP5
};
unsigned long d_drqdst_arr[DRQ_TYPE_MAX]={
	D_DRQDST_SRAM,       	//DRQ_TYPE_SRAM
	D_DRQDST_SDRAM,      	//DRQ_TYPE_SDRAM
	D_DRQDST_NAND,         	//DRQ_TYPE_NAND
	D_DRQDST_USB0,         	//DRQ_TYPE_USB0
	D_DRQDST_EMACTX,       	//DRQ_TYPE_EMAC
	D_DRQDST_SPI1TX,       	//DRQ_TYPE_SPI1
	D_DRQDST_SECTX,       	//DRQ_TYPE_SS
	D_DRQDST_MS,         	//DRQ_TYPE_MS
	D_DRQDST_SPI0TX,       	//DRQ_TYPE_SPI0
	D_DRQDST_SPI2TX,       	//DRQ_TYPE_SPI2
	DRQ_INVALID,			//DRQ_TYPE_TCON0
	D_DRQDST_HDMIAUDIO,		//DRQ_TYPE_HDMIAUDIO
	DRQ_INVALID,			//DRQ_TYPE_HDMI

	DRQ_INVALID,       		//DRQ_TYPE_IR
	DRQ_INVALID,      		//DRQ_TYPE_SPDIF
	DRQ_INVALID,     		//DRQ_TYPE_IIS
	DRQ_INVALID,     		//DRQ_TYPE_UART0
	DRQ_INVALID,			//DRQ_TYPE_UART1
	DRQ_INVALID,    		//DRQ_TYPE_UART2
	DRQ_INVALID,    		//DRQ_TYPE_UART3
	DRQ_INVALID,    		//DRQ_TYPE_AUDIO
	DRQ_INVALID,	    	//DRQ_TYPE_TPAD
	DRQ_INVALID,			//DRQ_TYPE_USBEP1
	DRQ_INVALID,			//DRQ_TYPE_USBEP2
	DRQ_INVALID,			//DRQ_TYPE_USBEP3
	DRQ_INVALID,			//DRQ_TYPE_USBEP4
	DRQ_INVALID,			//DRQ_TYPE_USBEP5
};

#endif
