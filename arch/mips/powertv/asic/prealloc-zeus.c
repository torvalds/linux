/*
 * Memory pre-allocations for Zeus boxes.
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
 * DVR_CAPABLE RESOURCES
 */
struct resource dvr_zeus_resources[] __initdata =
{
	/*
	 *
	 * VIDEO1 / LX1
	 *
	 */
	{
		.name   = "ST231aImage",	/* Delta-Mu 1 image and ram */
		.start  = 0x20000000,
		.end    = 0x201FFFFF,		/* 2MiB */
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "ST231aMonitor",	/* 8KiB block ST231a monitor */
		.start  = 0x20200000,
		.end    = 0x20201FFF,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "MediaMemory1",
		.start  = 0x20202000,
		.end    = 0x21FFFFFF, /*~29.9MiB (32MiB - (2MiB + 8KiB)) */
		.flags  = IORESOURCE_IO,
	},
	/*
	 *
	 * VIDEO2 / LX2
	 *
	 */
	{
		.name   = "ST231bImage",	/* Delta-Mu 2 image and ram */
		.start  = 0x30000000,
		.end    = 0x301FFFFF,		/* 2MiB */
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "ST231bMonitor",	/* 8KiB block ST231b monitor */
		.start  = 0x30200000,
		.end    = 0x30201FFF,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "MediaMemory2",
		.start  = 0x30202000,
		.end    = 0x31FFFFFF, /*~29.9MiB (32MiB - (2MiB + 8KiB)) */
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
		.start  = 0x00000000,
		.end    = 0x00c00000 - 1,	/* 12 MB total */
		.flags  = IORESOURCE_MEM,
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
		.start  = 0x40100000,
		.end    = 0x407fffff,
		.flags  = IORESOURCE_MEM,
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
		.start  = 0x46900000,
		.end    = 0x47700000 - 1,	/* 14 MB total */
		.flags  = IORESOURCE_MEM,
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
		.start  = 0x47900000,
		.end    = 0x47920000 - 1,
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
		.end    = 0x00280000 - 1,
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
		.end    = 0x00000FFF,	/* 4 KB total */
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
		.start  = 0x00000000,
		.end    = 0x00000FFF,	/* 4 KB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 *
	 * ITFS
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Docsis -
	 *
	 */
	{
		.name   = "ITFS",
		.start  = 0x00000000,
		/* 815,104 bytes each for 2 ITFS partitions. */
		.end    = 0x0018DFFF,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 *
	 * AVFS
	 *
	 * This driver requires:
	 *
	 * Arbitrary Based Buffers:
	 *  Docsis -
	 *
	 */
	{
		.name   = "AvfsDmaMem",
		.start  = 0x00000000,
		/* (945K * 8) = (128K * 3) 5 playbacks / 3 server */
		.end    = 0x007c2000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "AvfsFileSys",
		.start  = 0x00000000,
		.end    = 0x00001000 - 1,  /* 4K */
		.flags  = IORESOURCE_MEM,
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
		.start  = 0x00000000,
		.end    = 0x2800 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Add other resources here
	 */
	{ },
};

/*
 * NON_DVR_CAPABLE ZEUS RESOURCES
 */
struct resource non_dvr_zeus_resources[] __initdata =
{
	/*
	 * VIDEO1 / LX1
	 */
	{
		.name   = "ST231aImage",	/* Delta-Mu 1 image and ram */
		.start  = 0x20000000,
		.end    = 0x201FFFFF,		/* 2MiB */
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "ST231aMonitor",	/* 8KiB block ST231a monitor */
		.start  = 0x20200000,
		.end    = 0x20201FFF,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "MediaMemory1",
		.start  = 0x20202000,
		.end    = 0x21FFFFFF, /*~29.9MiB (32MiB - (2MiB + 8KiB)) */
		.flags  = IORESOURCE_IO,
	},
	/*
	 * Sysaudio Driver
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
	 * STAVEM driver/STAPI
	 */
	{
		.name   = "AVMEMPartition0",
		.start  = 0x00000000,
		.end    = 0x00600000 - 1,	/* 6 MB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * DOCSIS Subsystem
	 */
	{
		.name   = "Docsis",
		.start  = 0x40100000,
		.end    = 0x407fffff,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * GHW HAL Driver
	 */
	{
		.name   = "GraphicsHeap",
		.start  = 0x46900000,
		.end    = 0x47700000 - 1,	/* 14 MB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * multi com buffer area
	 */
	{
		.name   = "MulticomSHM",
		.start  = 0x47900000,
		.end    = 0x47920000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * DMA Ring buffer
	 */
	{
		.name   = "BMM_Buffer",
		.start  = 0x00000000,
		.end    = 0x00280000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Display bins buffer for unit0
	 */
	{
		.name   = "DisplayBins0",
		.start  = 0x00000000,
		.end    = 0x00000FFF,		/* 4 KB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 *
	 * AVFS: player HAL memory
	 *
	 *
	 */
	{
		.name   = "AvfsDmaMem",
		.start  = 0x00000000,
		.end    = 0x002c4c00 - 1,	/* 945K * 3 for playback */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * PMEM
	 */
	{
		.name   = "DiagPersistentMemory",
		.start  = 0x00000000,
		.end    = 0x10000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Smartcard
	 */
	{
		.name   = "SmartCardInfo",
		.start  = 0x00000000,
		.end    = 0x2800 - 1,
		.flags  = IORESOURCE_MEM,
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
