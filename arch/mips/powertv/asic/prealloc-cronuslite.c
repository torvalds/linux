/*
 * Memory pre-allocations for Cronus Lite boxes.
 *
 * Copyright (C) 2005-2009 Scientific-Atlanta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author:       Ken Eppinett
 *               David Schleef <ds@schleef.org>
 */

#include <linux/init.h>
#include <asm/mach-powertv/asic.h>

/*
 * NON_DVR_CAPABLE CRONUSLITE RESOURCES
 */
struct resource non_dvr_cronuslite_resources[] __initdata =
{
	/*
	 *
	 * VIDEO2 / LX2
	 *
	 */
	{
		.name   = "ST231aImage",	/* Delta-Mu 2 image and ram */
		.start  = 0x60000000,
		.end    = 0x601FFFFF,		/* 2MiB */
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "ST231aMonitor",	/* 8KiB block ST231b monitor */
		.start  = 0x60200000,
		.end    = 0x60201FFF,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "MediaMemory1",
		.start  = 0x60202000,
		.end    = 0x61FFFFFF, /*~29.9MiB (32MiB - (2MiB + 8KiB)) */
		.flags  = IORESOURCE_IO,
	},
	/*
	 *
	 * Sysaudio Driver
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  DSP_Image_Buff - DSP code and data images (1MB)
	 *  ADSC_CPU_PCM_Buff - ADSC CPU PCM buffer (40KB)
	 *  ADSC_AUX_Buff - ADSC AUX buffer (16KB)
	 *  ADSC_Main_Buff - ADSC Main buffer (16KB)
	 *
	 */
	{
		.name   = "DSP_Image_Buff",
		.start  = 0x00000000,
		.end    = 0x000FFFFF,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "ADSC_CPU_PCM_Buff",
		.start  = 0x00000000,
		.end    = 0x00009FFF,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "ADSC_AUX_Buff",
		.start  = 0x00000000,
		.end    = 0x00003FFF,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "ADSC_Main_Buff",
		.start  = 0x00000000,
		.end    = 0x00003FFF,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 *
	 * STAVEM driver/STAPI
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  This memory area is used for allocating buffers for Video decoding
	 *  purposes.  Allocation/De-allocation within this buffer is managed
	 *  by the STAVMEM driver of the STAPI.  They could be Decimated
	 *  Picture Buffers, Intermediate Buffers, as deemed necessary for
	 *  video decoding purposes, for any video decoders on Zeus.
	 *
	 */
	{
		.name   = "AVMEMPartition0",
		.start  = 0x63580000,
		.end    = 0x63B80000 - 1,  /* 6 MB total */
		.flags  = IORESOURCE_IO,
	},
	/*
	 *
	 * DOCSIS Subsystem
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Docsis -
	 *
	 */
	{
		.name   = "Docsis",
		.start  = 0x62000000,
		.end    = 0x62700000 - 1,	/* 7 MB total */
		.flags  = IORESOURCE_IO,
	},
	/*
	 *
	 * GHW HAL Driver
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  GraphicsHeap - PowerTV Graphics Heap
	 *
	 */
	{
		.name   = "GraphicsHeap",
		.start  = 0x62700000,
		.end    = 0x63500000 - 1,	/* 14 MB total */
		.flags  = IORESOURCE_IO,
	},
	/*
	 *
	 * multi com buffer area
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Docsis -
	 *
	 */
	{
		.name   = "MulticomSHM",
		.start  = 0x26000000,
		.end    = 0x26020000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 *
	 * DMA Ring buffer
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Docsis -
	 *
	 */
	{
		.name   = "BMM_Buffer",
		.start  = 0x00000000,
		.end    = 0x000AA000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 *
	 * Display bins buffer for unit0
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Display Bins for unit0
	 *
	 */
	{
		.name   = "DisplayBins0",
		.start  = 0x00000000,
		.end    = 0x00000FFF,		/* 4 KB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 *
	 * Display bins buffer
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Display Bins for unit1
	 *
	 */
	{
		.name   = "DisplayBins1",
		.start  = 0x63B83000,
		.end    = 0x63B84000 - 1,  /* 4 KB total */
		.flags  = IORESOURCE_IO,
	},
	/*
	 *
	 * AVFS: player HAL memory
	 *
	 *
	 */
	{
		.name   = "AvfsDmaMem",
		.start  = 0x63B84000,
		.end    = 0x63E48C00 - 1,  /* 945K * 3 for playback */
		.flags  = IORESOURCE_IO,
	},
	/*
	 *
	 * PMEM
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Persistent memory for diagnostics.
	 *
	 */
	{
		.name   = "DiagPersistentMemory",
		.start  = 0x00000000,
		.end    = 0x10000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 *
	 * Smartcard
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Read and write buffers for Internal/External cards
	 *
	 */
	{
		.name   = "SmartCardInfo",
		.start  = 0x63B80000,
		.end    = 0x63B82800 - 1,
		.flags  = IORESOURCE_IO,
	},
	/*
	 *
	 * KAVNET
	 *    NP Reset Vector - must be of the form xxCxxxxx
	 *	   NP Image - must be video bank 1
	 *	   NP IPC - must be video bank 2
	 */
	{
		.name   = "NP_Reset_Vector",
		.start  = 0x27c00000,
		.end    = 0x27c01000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "NP_Image",
		.start  = 0x27020000,
		.end    = 0x27060000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "NP_IPC",
		.start  = 0x63500000,
		.end    = 0x63580000 - 1,
		.flags  = IORESOURCE_IO,
	},
	/*
	 * NAND Flash
	 */
	{
		.name   = "NandFlash",
		.start  = NAND_FLASH_BASE,
		.end    = NAND_FLASH_BASE + 0x400 - 1,
		.flags  = IORESOURCE_IO,
	},
	/*
	 * Add other resources here
	 */
	{ },
};
