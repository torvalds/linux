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
 * Author:	 Ken Eppinett
 *		 David Schleef <ds@schleef.org>
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/mach-powertv/asic.h>
#include "prealloc.h"

/*
 * NON_DVR_CAPABLE CALLIOPE RESOURCES
 */
struct resource non_dvr_calliope_resources[] __initdata =
{
	/*
	 * VIDEO / LX1
	 */
	/* Delta-Mu 1 image (2MiB) */
	PREALLOC_NORMAL("ST231aImage", 0x24000000, 0x24200000-1,
		IORESOURCE_MEM)
	/* Delta-Mu 1 monitor (8KiB) */
	PREALLOC_NORMAL("ST231aMonitor", 0x24200000, 0x24202000-1,
		IORESOURCE_MEM)
	/* Delta-Mu 1 RAM (~36.9MiB (32MiB - (2MiB + 8KiB))) */
	PREALLOC_NORMAL("MediaMemory1", 0x24202000, 0x26700000-1,
		IORESOURCE_MEM)

	/*
	 * Sysaudio Driver
	 */
	/* DSP code and data images (1MiB) */
	PREALLOC_NORMAL("DSP_Image_Buff", 0x00000000, 0x00100000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC CPU PCM buffer (40KiB) */
	PREALLOC_NORMAL("ADSC_CPU_PCM_Buff", 0x00000000, 0x0000A000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC AUX buffer (128KiB) */
	PREALLOC_NORMAL("ADSC_AUX_Buff", 0x00000000, 0x00020000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC Main buffer (128KiB) */
	PREALLOC_NORMAL("ADSC_Main_Buff", 0x00000000, 0x00020000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * STAVEM driver/STAPI
	 */
	/* 6MiB */
	PREALLOC_NORMAL("AVMEMPartition0", 0x00000000, 0x00600000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * DOCSIS Subsystem
	 */
	/* 7MiB */
	PREALLOC_DOCSIS("Docsis", 0x27500000, 0x27c00000-1, IORESOURCE_MEM)

	/*
	 * GHW HAL Driver
	 */
	/* PowerTV Graphics Heap (14MiB) */
	PREALLOC_NORMAL("GraphicsHeap", 0x26700000, 0x26700000+(14*1048576)-1,
		IORESOURCE_MEM)

	/*
	 * multi com buffer area
	 */
	/* 128KiB */
	PREALLOC_NORMAL("MulticomSHM", 0x23700000, 0x23720000-1,
		IORESOURCE_MEM)

	/*
	 * DMA Ring buffer (don't need recording buffers)
	 */
	/* 680KiB */
	PREALLOC_NORMAL("BMM_Buffer", 0x00000000, 0x000AA000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Display bins buffer for unit0
	 */
	/* 4KiB */
	PREALLOC_NORMAL("DisplayBins0", 0x00000000, 0x00001000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * AVFS: player HAL memory
	 */
	/* 945K * 3 for playback */
	PREALLOC_NORMAL("AvfsDmaMem", 0x00000000, 0x002c4c00-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * PMEM
	 */
	/* Persistent memory for diagnostics (64KiB) */
	PREALLOC_PMEM("DiagPersistentMemory", 0x00000000, 0x10000-1,
	     (IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Smartcard
	 */
	/* Read and write buffers for Internal/External cards (10KiB) */
	PREALLOC_NORMAL("SmartCardInfo", 0x00000000, 0x2800-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * NAND Flash
	 */
	/* 10KiB */
	PREALLOC_NORMAL("NandFlash", NAND_FLASH_BASE, NAND_FLASH_BASE+0x400-1,
		IORESOURCE_MEM)

	/*
	 * Synopsys GMAC Memory Region
	 */
	/* 64KiB */
	PREALLOC_NORMAL("GMAC", 0x00000000, 0x00010000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * TFTPBuffer
	 *
	 *  This buffer is used in some minimal configurations (e.g. two-way
	 *  loader) for storing software images
	 */
	PREALLOC_TFTP("TFTPBuffer", 0x00000000, MEBIBYTE(80)-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Add other resources here
	 */

	/*
	 * End of Resource marker
	 */
	{
		.flags	= 0,
	},
};


struct resource non_dvr_vze_calliope_resources[] __initdata =
{
	/*
	 * VIDEO / LX1
	 */
	/* Delta-Mu 1 image (2MiB) */
	PREALLOC_NORMAL("ST231aImage", 0x22000000, 0x22200000-1,
		IORESOURCE_MEM)
	/* Delta-Mu 1 monitor (8KiB) */
	PREALLOC_NORMAL("ST231aMonitor", 0x22200000, 0x22202000-1,
		IORESOURCE_MEM)
	/* Delta-Mu 1 RAM (10.12MiB) */
	PREALLOC_NORMAL("MediaMemory1", 0x22202000, 0x22C20B85-1,
		IORESOURCE_MEM)

	/*
	 * Sysaudio Driver
	 */
	/* DSP code and data images (1MiB) */
	PREALLOC_NORMAL("DSP_Image_Buff", 0x00000000, 0x00100000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC CPU PCM buffer (40KiB) */
	PREALLOC_NORMAL("ADSC_CPU_PCM_Buff", 0x00000000, 0x0000A000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC AUX buffer (16KiB) */
	PREALLOC_NORMAL("ADSC_AUX_Buff", 0x00000000, 0x00004000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC Main buffer (16KiB) */
	PREALLOC_NORMAL("ADSC_Main_Buff", 0x00000000, 0x00004000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * STAVEM driver/STAPI
	 */
	/* 3.125MiB */
	PREALLOC_NORMAL("AVMEMPartition0", 0x20396000, 0x206B6000-1,
		IORESOURCE_MEM)

	/*
	 * GHW HAL Driver
	 */
	/* PowerTV Graphics Heap (2.59MiB) */
	PREALLOC_NORMAL("GraphicsHeap", 0x20100000, 0x20396000-1,
		IORESOURCE_MEM)

	/*
	 * multi com buffer area
	 */
	/* 128KiB */
	PREALLOC_NORMAL("MulticomSHM", 0x206B6000, 0x206D6000-1,
		IORESOURCE_MEM)

	/*
	 * DMA Ring buffer (don't need recording buffers)
	 */
	/* 680KiB */
	PREALLOC_NORMAL("BMM_Buffer", 0x00000000, 0x000AA000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Display bins buffer for unit0
	 */
	/* 4KiB */
	PREALLOC_NORMAL("DisplayBins0", 0x00000000, 0x00001000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * PMEM
	 */
	/* Persistent memory for diagnostics (64KiB) */
	PREALLOC_PMEM("DiagPersistentMemory", 0x00000000, 0x10000-1,
	     (IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Smartcard
	 */
	/* Read and write buffers for Internal/External cards (10KiB) */
	PREALLOC_NORMAL("SmartCardInfo", 0x00000000, 0x2800-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * NAND Flash
	 */
	/* 10KiB */
	PREALLOC_NORMAL("NandFlash", NAND_FLASH_BASE, NAND_FLASH_BASE+0x400-1,
		IORESOURCE_MEM)

	/*
	 * Synopsys GMAC Memory Region
	 */
	/* 64KiB */
	PREALLOC_NORMAL("GMAC", 0x00000000, 0x00010000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Add other resources here
	 */

	/*
	 * End of Resource marker
	 */
	{
		.flags	= 0,
	},
};

struct resource non_dvr_vzf_calliope_resources[] __initdata =
{
	/*
	 * VIDEO / LX1
	 */
	/* Delta-Mu 1 image (2MiB) */
	PREALLOC_NORMAL("ST231aImage", 0x24000000, 0x24200000-1,
		IORESOURCE_MEM)
	/* Delta-Mu 1 monitor (8KiB) */
	PREALLOC_NORMAL("ST231aMonitor", 0x24200000, 0x24202000-1,
		IORESOURCE_MEM)
	/* Delta-Mu 1 RAM (~19.4 (21.5MiB - (2MiB + 8KiB))) */
	PREALLOC_NORMAL("MediaMemory1", 0x24202000, 0x25580000-1,
		IORESOURCE_MEM)

	/*
	 * Sysaudio Driver
	 */
	/* DSP code and data images (1MiB) */
	PREALLOC_NORMAL("DSP_Image_Buff", 0x00000000, 0x00100000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC CPU PCM buffer (40KiB) */
	PREALLOC_NORMAL("ADSC_CPU_PCM_Buff", 0x00000000, 0x0000A000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC AUX buffer (128KiB) */
	PREALLOC_NORMAL("ADSC_AUX_Buff", 0x00000000, 0x00020000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))
	/* ADSC Main buffer (128KiB) */
	PREALLOC_NORMAL("ADSC_Main_Buff", 0x00000000, 0x00020000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * STAVEM driver/STAPI
	 */
	/* 4.5MiB */
	PREALLOC_NORMAL("AVMEMPartition0", 0x00000000, 0x00480000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * GHW HAL Driver
	 */
	/* PowerTV Graphics Heap (14MiB) */
	PREALLOC_NORMAL("GraphicsHeap", 0x25600000, 0x25600000+(14*1048576)-1,
		IORESOURCE_MEM)

	/*
	 * multi com buffer area
	 */
	/* 128KiB */
	PREALLOC_NORMAL("MulticomSHM", 0x23700000, 0x23720000-1,
		IORESOURCE_MEM)

	/*
	 * DMA Ring buffer (don't need recording buffers)
	 */
	/* 680KiB */
	PREALLOC_NORMAL("BMM_Buffer", 0x00000000, 0x000AA000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Display bins buffer for unit0
	 */
	/* 4KiB */
	PREALLOC_NORMAL("DisplayBins0", 0x00000000, 0x00001000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Display bins buffer for unit1
	 */
	/* 4KiB */
	PREALLOC_NORMAL("DisplayBins1", 0x00000000, 0x00001000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * AVFS: player HAL memory
	 */
	/* 945K * 3 for playback */
	PREALLOC_NORMAL("AvfsDmaMem", 0x00000000, 0x002c4c00-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * PMEM
	 */
	/* Persistent memory for diagnostics (64KiB) */
	PREALLOC_PMEM("DiagPersistentMemory", 0x00000000, 0x10000-1,
	     (IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Smartcard
	 */
	/* Read and write buffers for Internal/External cards (10KiB) */
	PREALLOC_NORMAL("SmartCardInfo", 0x00000000, 0x2800-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * NAND Flash
	 */
	/* 10KiB */
	PREALLOC_NORMAL("NandFlash", NAND_FLASH_BASE, NAND_FLASH_BASE+0x400-1,
		IORESOURCE_MEM)

	/*
	 * Synopsys GMAC Memory Region
	 */
	/* 64KiB */
	PREALLOC_NORMAL("GMAC", 0x00000000, 0x00010000-1,
		(IORESOURCE_MEM|IORESOURCE_PTV_RES_LOEXT))

	/*
	 * Add other resources here
	 */

	/*
	 * End of Resource marker
	 */
	{
		.flags	= 0,
	},
};
