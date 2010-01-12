/*
 * Memory pre-allocations for Calliope boxes.
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
 * NON_DVR_CAPABLE CALLIOPE RESOURCES
 */
struct resource non_dvr_calliope_resources[] __initdata =
{
	/*
	 * VIDEO / LX1
	 */
	{
		.name   = "ST231aImage",     	/* Delta-Mu 1 image and ram */
		.start  = 0x24000000,
		.end    = 0x24200000 - 1,	/*2MiB */
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "ST231aMonitor",   /*8KiB block ST231a monitor */
		.start  = 0x24200000,
		.end    = 0x24202000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "MediaMemory1",
		.start  = 0x24202000,
		.end    = 0x26700000 - 1, /*~36.9MiB (32MiB - (2MiB + 8KiB)) */
		.flags  = IORESOURCE_MEM,
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
		.start  = 0x22000000,
		.end    = 0x22700000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * GHW HAL Driver
	 */
	{
		.name   = "GraphicsHeap",
		.start  = 0x22700000,
		.end    = 0x23500000 - 1,	/* 14 MB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * multi com buffer area
	 */
	{
		.name   = "MulticomSHM",
		.start  = 0x23700000,
		.end    = 0x23720000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * DMA Ring buffer (don't need recording buffers)
	 */
	{
		.name   = "BMM_Buffer",
		.start  = 0x00000000,
		.end    = 0x000AA000 - 1,
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
	 * Synopsys GMAC Memory Region
	 */
	{
		.name   = "GMAC",
		.start  = 0x00000000,
		.end    = 0x00010000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Add other resources here
	 *
	 */
	{ },
};

struct resource non_dvr_vz_calliope_resources[] __initdata =
{
	/*
	 * VIDEO / LX1
	 */
	{
		.name   = "ST231aImage",	/* Delta-Mu 1 image and ram */
		.start  = 0x24000000,
		.end    = 0x24200000 - 1, /*2 Meg */
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "ST231aMonitor",	/* 8k block ST231a monitor */
		.start  = 0x24200000,
		.end    = 0x24202000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "MediaMemory1",
		.start  = 0x22202000,
		.end    = 0x22C20B85 - 1,	/* 10.12 Meg */
		.flags  = IORESOURCE_MEM,
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
		.start  = 0x20300000,
		.end    = 0x20620000-1,  /*3.125 MB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * GHW HAL Driver
	 */
	{
		.name   = "GraphicsHeap",
		.start  = 0x20100000,
		.end    = 0x20300000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * multi com buffer area
	 */
	{
		.name   = "MulticomSHM",
		.start  = 0x23900000,
		.end    = 0x23920000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * DMA Ring buffer
	 */
	{
		.name   = "BMM_Buffer",
		.start  = 0x00000000,
		.end    = 0x000AA000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Display bins buffer for unit0
	 */
	{
		.name   = "DisplayBins0",
		.start  = 0x00000000,
		.end    = 0x00000FFF,
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
		.end    = NAND_FLASH_BASE+0x400 - 1,
		.flags  = IORESOURCE_IO,
	},
	/*
	 * Synopsys GMAC Memory Region
	 */
	{
		.name   = "GMAC",
		.start  = 0x00000000,
		.end    = 0x00010000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Add other resources here
	 */
	{ },
};

struct resource non_dvr_vze_calliope_resources[] __initdata =
{
	/*
	 * VIDEO / LX1
	 */
	{
		.name   = "ST231aImage",	/* Delta-Mu 1 image and ram */
		.start  = 0x22000000,
		.end    = 0x22200000 - 1,	/*2  Meg */
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "ST231aMonitor",	/* 8k block ST231a monitor */
		.start  = 0x22200000,
		.end    = 0x22202000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "MediaMemory1",
		.start  = 0x22202000,
		.end    = 0x22C20B85 - 1,	/* 10.12 Meg */
		.flags  = IORESOURCE_MEM,
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
		.start  = 0x20396000,
		.end    = 0x206B6000 - 1,		/* 3.125 MB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * GHW HAL Driver
	 */
	{
		.name   = "GraphicsHeap",
		.start  = 0x20100000,
		.end    = 0x20396000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * multi com buffer area
	 */
	{
		.name   = "MulticomSHM",
		.start  = 0x206B6000,
		.end    = 0x206D6000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * DMA Ring buffer
	 */
	{
		.name   = "BMM_Buffer",
		.start  = 0x00000000,
		.end    = 0x000AA000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Display bins buffer for unit0
	 */
	{
		.name   = "DisplayBins0",
		.start  = 0x00000000,
		.end    = 0x00000FFF,
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
		.end    = NAND_FLASH_BASE+0x400 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Synopsys GMAC Memory Region
	 */
	{
		.name   = "GMAC",
		.start  = 0x00000000,
		.end    = 0x00010000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Add other resources here
	 */
	{ },
};

struct resource non_dvr_vzf_calliope_resources[] __initdata =
{
	/*
	 * VIDEO / LX1
	 */
	{
		.name   = "ST231aImage",	/*Delta-Mu 1 image and ram */
		.start  = 0x24000000,
		.end    = 0x24200000 - 1,	/*2MiB */
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "ST231aMonitor",	/*8KiB block ST231a monitor */
		.start  = 0x24200000,
		.end    = 0x24202000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "MediaMemory1",
		.start  = 0x24202000,
		/* ~19.4 (21.5MiB - (2MiB + 8KiB)) */
		.end    = 0x25580000 - 1,
		.flags  = IORESOURCE_MEM,
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
		.end    = 0x00480000 - 1,  /* 4.5 MB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * GHW HAL Driver
	 */
	{
		.name   = "GraphicsHeap",
		.start  = 0x22700000,
		.end    = 0x23500000 - 1, /* 14 MB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * multi com buffer area
	 */
	{
		.name   = "MulticomSHM",
		.start  = 0x23700000,
		.end    = 0x23720000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * DMA Ring buffer (don't need recording buffers)
	 */
	{
		.name   = "BMM_Buffer",
		.start  = 0x00000000,
		.end    = 0x000AA000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Display bins buffer for unit0
	 */
	{
		.name   = "DisplayBins0",
		.start  = 0x00000000,
		.end    = 0x00000FFF,  /* 4 KB total */
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Display bins buffer for unit1
	 */
	{
		.name   = "DisplayBins1",
		.start  = 0x00000000,
		.end    = 0x00000FFF,  /* 4 KB total */
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
		.end    = 0x002c4c00 - 1,  /* 945K * 3 for playback */
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
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Synopsys GMAC Memory Region
	 */
	{
		.name   = "GMAC",
		.start  = 0x00000000,
		.end    = 0x00010000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	/*
	 * Add other resources here
	 */
	{ },
};
