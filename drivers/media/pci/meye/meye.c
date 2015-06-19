/*
 * Motion Eye video4linux driver for Sony Vaio PictureBook
 *
 * Copyright (C) 2001-2004 Stelian Pop <stelian@popies.net>
 *
 * Copyright (C) 2001-2002 Alcôve <www.alcove.com>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
 *
 * Some parts borrowed from various video4linux drivers, especially
 * bttv-driver.c and zoran.c, see original files for credits.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>

#include "meye.h"
#include <linux/meye.h>

MODULE_AUTHOR("Stelian Pop <stelian@popies.net>");
MODULE_DESCRIPTION("v4l2 driver for the MotionEye camera");
MODULE_LICENSE("GPL");
MODULE_VERSION(MEYE_DRIVER_VERSION);

/* number of grab buffers */
static unsigned int gbuffers = 2;
module_param(gbuffers, int, 0444);
MODULE_PARM_DESC(gbuffers, "number of capture buffers, default is 2 (32 max)");

/* size of a grab buffer */
static unsigned int gbufsize = MEYE_MAX_BUFSIZE;
module_param(gbufsize, int, 0444);
MODULE_PARM_DESC(gbufsize, "size of the capture buffers, default is 614400"
		 " (will be rounded up to a page multiple)");

/* /dev/videoX registration number */
static int video_nr = -1;
module_param(video_nr, int, 0444);
MODULE_PARM_DESC(video_nr, "video device to register (0=/dev/video0, etc)");

/* driver structure - only one possible */
static struct meye meye;

/****************************************************************************/
/* Memory allocation routines (stolen from bttv-driver.c)                   */
/****************************************************************************/
static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (mem) {
		memset(mem, 0, size);
		adr = (unsigned long) mem;
		while (size > 0) {
			SetPageReserved(vmalloc_to_page((void *)adr));
			adr += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
	}
	return mem;
}

static void rvfree(void * mem, unsigned long size)
{
	unsigned long adr;

	if (mem) {
		adr = (unsigned long) mem;
		while ((long) size > 0) {
			ClearPageReserved(vmalloc_to_page((void *)adr));
			adr += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
		vfree(mem);
	}
}

/*
 * return a page table pointing to N pages of locked memory
 *
 * NOTE: The meye device expects DMA addresses on 32 bits, we build
 * a table of 1024 entries = 4 bytes * 1024 = 4096 bytes.
 */
static int ptable_alloc(void)
{
	u32 *pt;
	int i;

	memset(meye.mchip_ptable, 0, sizeof(meye.mchip_ptable));

	/* give only 32 bit DMA addresses */
	if (dma_set_mask(&meye.mchip_dev->dev, DMA_BIT_MASK(32)))
		return -1;

	meye.mchip_ptable_toc = dma_alloc_coherent(&meye.mchip_dev->dev,
						   PAGE_SIZE,
						   &meye.mchip_dmahandle,
						   GFP_KERNEL);
	if (!meye.mchip_ptable_toc) {
		meye.mchip_dmahandle = 0;
		return -1;
	}

	pt = meye.mchip_ptable_toc;
	for (i = 0; i < MCHIP_NB_PAGES; i++) {
		dma_addr_t dma;
		meye.mchip_ptable[i] = dma_alloc_coherent(&meye.mchip_dev->dev,
							  PAGE_SIZE,
							  &dma,
							  GFP_KERNEL);
		if (!meye.mchip_ptable[i]) {
			int j;
			pt = meye.mchip_ptable_toc;
			for (j = 0; j < i; ++j) {
				dma = (dma_addr_t) *pt;
				dma_free_coherent(&meye.mchip_dev->dev,
						  PAGE_SIZE,
						  meye.mchip_ptable[j], dma);
				pt++;
			}
			dma_free_coherent(&meye.mchip_dev->dev,
					  PAGE_SIZE,
					  meye.mchip_ptable_toc,
					  meye.mchip_dmahandle);
			meye.mchip_ptable_toc = NULL;
			meye.mchip_dmahandle = 0;
			return -1;
		}
		*pt = (u32) dma;
		pt++;
	}
	return 0;
}

static void ptable_free(void)
{
	u32 *pt;
	int i;

	pt = meye.mchip_ptable_toc;
	for (i = 0; i < MCHIP_NB_PAGES; i++) {
		dma_addr_t dma = (dma_addr_t) *pt;
		if (meye.mchip_ptable[i])
			dma_free_coherent(&meye.mchip_dev->dev,
					  PAGE_SIZE,
					  meye.mchip_ptable[i], dma);
		pt++;
	}

	if (meye.mchip_ptable_toc)
		dma_free_coherent(&meye.mchip_dev->dev,
				  PAGE_SIZE,
				  meye.mchip_ptable_toc,
				  meye.mchip_dmahandle);

	memset(meye.mchip_ptable, 0, sizeof(meye.mchip_ptable));
	meye.mchip_ptable_toc = NULL;
	meye.mchip_dmahandle = 0;
}

/* copy data from ptable into buf */
static void ptable_copy(u8 *buf, int start, int size, int pt_pages)
{
	int i;

	for (i = 0; i < (size / PAGE_SIZE) * PAGE_SIZE; i += PAGE_SIZE) {
		memcpy(buf + i, meye.mchip_ptable[start++], PAGE_SIZE);
		if (start >= pt_pages)
			start = 0;
	}
	memcpy(buf + i, meye.mchip_ptable[start], size % PAGE_SIZE);
}

/****************************************************************************/
/* JPEG tables at different qualities to load into the VRJ chip             */
/****************************************************************************/

/* return a set of quantisation tables based on a quality from 1 to 10 */
static u16 *jpeg_quantisation_tables(int *length, int quality)
{
	static u16 jpeg_tables[][70] = { {
		0xdbff, 0x4300, 0xff00, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff,
		0xdbff, 0x4300, 0xff01, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff,
	},
	{
		0xdbff, 0x4300, 0x5000, 0x3c37, 0x3c46, 0x5032, 0x4146, 0x5a46,
		0x5055, 0x785f, 0x82c8, 0x6e78, 0x786e, 0xaff5, 0x91b9, 0xffc8,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff,
		0xdbff, 0x4300, 0x5501, 0x5a5a, 0x6978, 0xeb78, 0x8282, 0xffeb,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0xffff, 0xffff, 0xffff,
	},
	{
		0xdbff, 0x4300, 0x2800, 0x1e1c, 0x1e23, 0x2819, 0x2123, 0x2d23,
		0x282b, 0x3c30, 0x4164, 0x373c, 0x3c37, 0x587b, 0x495d, 0x9164,
		0x9980, 0x8f96, 0x8c80, 0xa08a, 0xe6b4, 0xa0c3, 0xdaaa, 0x8aad,
		0xc88c, 0xcbff, 0xeeda, 0xfff5, 0xffff, 0xc19b, 0xffff, 0xfaff,
		0xe6ff, 0xfffd, 0xfff8,
		0xdbff, 0x4300, 0x2b01, 0x2d2d, 0x353c, 0x763c, 0x4141, 0xf876,
		0x8ca5, 0xf8a5, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8,
		0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8,
		0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8, 0xf8f8,
		0xf8f8, 0xf8f8, 0xfff8,
	},
	{
		0xdbff, 0x4300, 0x1b00, 0x1412, 0x1417, 0x1b11, 0x1617, 0x1e17,
		0x1b1c, 0x2820, 0x2b42, 0x2528, 0x2825, 0x3a51, 0x303d, 0x6042,
		0x6555, 0x5f64, 0x5d55, 0x6a5b, 0x9978, 0x6a81, 0x9071, 0x5b73,
		0x855d, 0x86b5, 0x9e90, 0xaba3, 0xabad, 0x8067, 0xc9bc, 0xa6ba,
		0x99c7, 0xaba8, 0xffa4,
		0xdbff, 0x4300, 0x1c01, 0x1e1e, 0x2328, 0x4e28, 0x2b2b, 0xa44e,
		0x5d6e, 0xa46e, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4,
		0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4,
		0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4, 0xa4a4,
		0xa4a4, 0xa4a4, 0xffa4,
	},
	{
		0xdbff, 0x4300, 0x1400, 0x0f0e, 0x0f12, 0x140d, 0x1012, 0x1712,
		0x1415, 0x1e18, 0x2132, 0x1c1e, 0x1e1c, 0x2c3d, 0x242e, 0x4932,
		0x4c40, 0x474b, 0x4640, 0x5045, 0x735a, 0x5062, 0x6d55, 0x4556,
		0x6446, 0x6588, 0x776d, 0x817b, 0x8182, 0x604e, 0x978d, 0x7d8c,
		0x7396, 0x817e, 0xff7c,
		0xdbff, 0x4300, 0x1501, 0x1717, 0x1a1e, 0x3b1e, 0x2121, 0x7c3b,
		0x4653, 0x7c53, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c,
		0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c,
		0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c, 0x7c7c,
		0x7c7c, 0x7c7c, 0xff7c,
	},
	{
		0xdbff, 0x4300, 0x1000, 0x0c0b, 0x0c0e, 0x100a, 0x0d0e, 0x120e,
		0x1011, 0x1813, 0x1a28, 0x1618, 0x1816, 0x2331, 0x1d25, 0x3a28,
		0x3d33, 0x393c, 0x3833, 0x4037, 0x5c48, 0x404e, 0x5744, 0x3745,
		0x5038, 0x516d, 0x5f57, 0x6762, 0x6768, 0x4d3e, 0x7971, 0x6470,
		0x5c78, 0x6765, 0xff63,
		0xdbff, 0x4300, 0x1101, 0x1212, 0x1518, 0x2f18, 0x1a1a, 0x632f,
		0x3842, 0x6342, 0x6363, 0x6363, 0x6363, 0x6363, 0x6363, 0x6363,
		0x6363, 0x6363, 0x6363, 0x6363, 0x6363, 0x6363, 0x6363, 0x6363,
		0x6363, 0x6363, 0x6363, 0x6363, 0x6363, 0x6363, 0x6363, 0x6363,
		0x6363, 0x6363, 0xff63,
	},
	{
		0xdbff, 0x4300, 0x0d00, 0x0a09, 0x0a0b, 0x0d08, 0x0a0b, 0x0e0b,
		0x0d0e, 0x130f, 0x1520, 0x1213, 0x1312, 0x1c27, 0x171e, 0x2e20,
		0x3129, 0x2e30, 0x2d29, 0x332c, 0x4a3a, 0x333e, 0x4636, 0x2c37,
		0x402d, 0x4157, 0x4c46, 0x524e, 0x5253, 0x3e32, 0x615a, 0x505a,
		0x4a60, 0x5251, 0xff4f,
		0xdbff, 0x4300, 0x0e01, 0x0e0e, 0x1113, 0x2613, 0x1515, 0x4f26,
		0x2d35, 0x4f35, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f,
		0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f,
		0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f, 0x4f4f,
		0x4f4f, 0x4f4f, 0xff4f,
	},
	{
		0xdbff, 0x4300, 0x0a00, 0x0707, 0x0708, 0x0a06, 0x0808, 0x0b08,
		0x0a0a, 0x0e0b, 0x1018, 0x0d0e, 0x0e0d, 0x151d, 0x1116, 0x2318,
		0x251f, 0x2224, 0x221f, 0x2621, 0x372b, 0x262f, 0x3429, 0x2129,
		0x3022, 0x3141, 0x3934, 0x3e3b, 0x3e3e, 0x2e25, 0x4944, 0x3c43,
		0x3748, 0x3e3d, 0xff3b,
		0xdbff, 0x4300, 0x0a01, 0x0b0b, 0x0d0e, 0x1c0e, 0x1010, 0x3b1c,
		0x2228, 0x3b28, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b,
		0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b,
		0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b, 0x3b3b,
		0x3b3b, 0x3b3b, 0xff3b,
	},
	{
		0xdbff, 0x4300, 0x0600, 0x0504, 0x0506, 0x0604, 0x0506, 0x0706,
		0x0607, 0x0a08, 0x0a10, 0x090a, 0x0a09, 0x0e14, 0x0c0f, 0x1710,
		0x1814, 0x1718, 0x1614, 0x1a16, 0x251d, 0x1a1f, 0x231b, 0x161c,
		0x2016, 0x202c, 0x2623, 0x2927, 0x292a, 0x1f19, 0x302d, 0x282d,
		0x2530, 0x2928, 0xff28,
		0xdbff, 0x4300, 0x0701, 0x0707, 0x080a, 0x130a, 0x0a0a, 0x2813,
		0x161a, 0x281a, 0x2828, 0x2828, 0x2828, 0x2828, 0x2828, 0x2828,
		0x2828, 0x2828, 0x2828, 0x2828, 0x2828, 0x2828, 0x2828, 0x2828,
		0x2828, 0x2828, 0x2828, 0x2828, 0x2828, 0x2828, 0x2828, 0x2828,
		0x2828, 0x2828, 0xff28,
	},
	{
		0xdbff, 0x4300, 0x0300, 0x0202, 0x0203, 0x0302, 0x0303, 0x0403,
		0x0303, 0x0504, 0x0508, 0x0405, 0x0504, 0x070a, 0x0607, 0x0c08,
		0x0c0a, 0x0b0c, 0x0b0a, 0x0d0b, 0x120e, 0x0d10, 0x110e, 0x0b0e,
		0x100b, 0x1016, 0x1311, 0x1514, 0x1515, 0x0f0c, 0x1817, 0x1416,
		0x1218, 0x1514, 0xff14,
		0xdbff, 0x4300, 0x0301, 0x0404, 0x0405, 0x0905, 0x0505, 0x1409,
		0x0b0d, 0x140d, 0x1414, 0x1414, 0x1414, 0x1414, 0x1414, 0x1414,
		0x1414, 0x1414, 0x1414, 0x1414, 0x1414, 0x1414, 0x1414, 0x1414,
		0x1414, 0x1414, 0x1414, 0x1414, 0x1414, 0x1414, 0x1414, 0x1414,
		0x1414, 0x1414, 0xff14,
	},
	{
		0xdbff, 0x4300, 0x0100, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0101, 0x0101, 0xff01,
		0xdbff, 0x4300, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0101, 0x0101, 0xff01,
	} };

	if (quality < 0 || quality > 10) {
		printk(KERN_WARNING
		       "meye: invalid quality level %d - using 8\n", quality);
		quality = 8;
	}

	*length = ARRAY_SIZE(jpeg_tables[quality]);
	return jpeg_tables[quality];
}

/* return a generic set of huffman tables */
static u16 *jpeg_huffman_tables(int *length)
{
	static u16 tables[] = {
		0xC4FF, 0xB500, 0x0010, 0x0102, 0x0303, 0x0402, 0x0503, 0x0405,
		0x0004, 0x0100, 0x017D, 0x0302, 0x0400, 0x0511, 0x2112, 0x4131,
		0x1306, 0x6151, 0x2207, 0x1471, 0x8132, 0xA191, 0x2308, 0xB142,
		0x15C1, 0xD152, 0x24F0, 0x6233, 0x8272, 0x0A09, 0x1716, 0x1918,
		0x251A, 0x2726, 0x2928, 0x342A, 0x3635, 0x3837, 0x3A39, 0x4443,
		0x4645, 0x4847, 0x4A49, 0x5453, 0x5655, 0x5857, 0x5A59, 0x6463,
		0x6665, 0x6867, 0x6A69, 0x7473, 0x7675, 0x7877, 0x7A79, 0x8483,
		0x8685, 0x8887, 0x8A89, 0x9392, 0x9594, 0x9796, 0x9998, 0xA29A,
		0xA4A3, 0xA6A5, 0xA8A7, 0xAAA9, 0xB3B2, 0xB5B4, 0xB7B6, 0xB9B8,
		0xC2BA, 0xC4C3, 0xC6C5, 0xC8C7, 0xCAC9, 0xD3D2, 0xD5D4, 0xD7D6,
		0xD9D8, 0xE1DA, 0xE3E2, 0xE5E4, 0xE7E6, 0xE9E8, 0xF1EA, 0xF3F2,
		0xF5F4, 0xF7F6, 0xF9F8, 0xFFFA,
		0xC4FF, 0xB500, 0x0011, 0x0102, 0x0402, 0x0304, 0x0704, 0x0405,
		0x0004, 0x0201, 0x0077, 0x0201, 0x1103, 0x0504, 0x3121, 0x1206,
		0x5141, 0x6107, 0x1371, 0x3222, 0x0881, 0x4214, 0xA191, 0xC1B1,
		0x2309, 0x5233, 0x15F0, 0x7262, 0x0AD1, 0x2416, 0xE134, 0xF125,
		0x1817, 0x1A19, 0x2726, 0x2928, 0x352A, 0x3736, 0x3938, 0x433A,
		0x4544, 0x4746, 0x4948, 0x534A, 0x5554, 0x5756, 0x5958, 0x635A,
		0x6564, 0x6766, 0x6968, 0x736A, 0x7574, 0x7776, 0x7978, 0x827A,
		0x8483, 0x8685, 0x8887, 0x8A89, 0x9392, 0x9594, 0x9796, 0x9998,
		0xA29A, 0xA4A3, 0xA6A5, 0xA8A7, 0xAAA9, 0xB3B2, 0xB5B4, 0xB7B6,
		0xB9B8, 0xC2BA, 0xC4C3, 0xC6C5, 0xC8C7, 0xCAC9, 0xD3D2, 0xD5D4,
		0xD7D6, 0xD9D8, 0xE2DA, 0xE4E3, 0xE6E5, 0xE8E7, 0xEAE9, 0xF3F2,
		0xF5F4, 0xF7F6, 0xF9F8, 0xFFFA,
		0xC4FF, 0x1F00, 0x0000, 0x0501, 0x0101, 0x0101, 0x0101, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0201, 0x0403, 0x0605, 0x0807, 0x0A09,
		0xFF0B,
		0xC4FF, 0x1F00, 0x0001, 0x0103, 0x0101, 0x0101, 0x0101, 0x0101,
		0x0000, 0x0000, 0x0000, 0x0201, 0x0403, 0x0605, 0x0807, 0x0A09,
		0xFF0B
	};

	*length = ARRAY_SIZE(tables);
	return tables;
}

/****************************************************************************/
/* MCHIP low-level functions                                                */
/****************************************************************************/

/* returns the horizontal capture size */
static inline int mchip_hsize(void)
{
	return meye.params.subsample ? 320 : 640;
}

/* returns the vertical capture size */
static inline int mchip_vsize(void)
{
	return meye.params.subsample ? 240 : 480;
}

/* waits for a register to be available */
static void mchip_sync(int reg)
{
	u32 status;
	int i;

	if (reg == MCHIP_MM_FIFO_DATA) {
		for (i = 0; i < MCHIP_REG_TIMEOUT; i++) {
			status = readl(meye.mchip_mmregs +
				       MCHIP_MM_FIFO_STATUS);
			if (!(status & MCHIP_MM_FIFO_WAIT)) {
				printk(KERN_WARNING "meye: fifo not ready\n");
				return;
			}
			if (status & MCHIP_MM_FIFO_READY)
				return;
			udelay(1);
		}
	} else if (reg > 0x80) {
		u32 mask = (reg < 0x100) ? MCHIP_HIC_STATUS_MCC_RDY
					 : MCHIP_HIC_STATUS_VRJ_RDY;
		for (i = 0; i < MCHIP_REG_TIMEOUT; i++) {
			status = readl(meye.mchip_mmregs + MCHIP_HIC_STATUS);
			if (status & mask)
				return;
			udelay(1);
		}
	} else
		return;
	printk(KERN_WARNING
	       "meye: mchip_sync() timeout on reg 0x%x status=0x%x\n",
	       reg, status);
}

/* sets a value into the register */
static inline void mchip_set(int reg, u32 v)
{
	mchip_sync(reg);
	writel(v, meye.mchip_mmregs + reg);
}

/* get the register value */
static inline u32 mchip_read(int reg)
{
	mchip_sync(reg);
	return readl(meye.mchip_mmregs + reg);
}

/* wait for a register to become a particular value */
static inline int mchip_delay(u32 reg, u32 v)
{
	int n = 10;
	while (--n && mchip_read(reg) != v)
		udelay(1);
	return n;
}

/* setup subsampling */
static void mchip_subsample(void)
{
	mchip_set(MCHIP_MCC_R_SAMPLING, meye.params.subsample);
	mchip_set(MCHIP_MCC_R_XRANGE, mchip_hsize());
	mchip_set(MCHIP_MCC_R_YRANGE, mchip_vsize());
	mchip_set(MCHIP_MCC_B_XRANGE, mchip_hsize());
	mchip_set(MCHIP_MCC_B_YRANGE, mchip_vsize());
	mchip_delay(MCHIP_HIC_STATUS, MCHIP_HIC_STATUS_IDLE);
}

/* set the framerate into the mchip */
static void mchip_set_framerate(void)
{
	mchip_set(MCHIP_HIC_S_RATE, meye.params.framerate);
}

/* load some huffman and quantisation tables into the VRJ chip ready
   for JPEG compression */
static void mchip_load_tables(void)
{
	int i;
	int length;
	u16 *tables;

	tables = jpeg_huffman_tables(&length);
	for (i = 0; i < length; i++)
		writel(tables[i], meye.mchip_mmregs + MCHIP_VRJ_TABLE_DATA);

	tables = jpeg_quantisation_tables(&length, meye.params.quality);
	for (i = 0; i < length; i++)
		writel(tables[i], meye.mchip_mmregs + MCHIP_VRJ_TABLE_DATA);
}

/* setup the VRJ parameters in the chip */
static void mchip_vrj_setup(u8 mode)
{
	mchip_set(MCHIP_VRJ_BUS_MODE, 5);
	mchip_set(MCHIP_VRJ_SIGNAL_ACTIVE_LEVEL, 0x1f);
	mchip_set(MCHIP_VRJ_PDAT_USE, 1);
	mchip_set(MCHIP_VRJ_IRQ_FLAG, 0xa0);
	mchip_set(MCHIP_VRJ_MODE_SPECIFY, mode);
	mchip_set(MCHIP_VRJ_NUM_LINES, mchip_vsize());
	mchip_set(MCHIP_VRJ_NUM_PIXELS, mchip_hsize());
	mchip_set(MCHIP_VRJ_NUM_COMPONENTS, 0x1b);
	mchip_set(MCHIP_VRJ_LIMIT_COMPRESSED_LO, 0xFFFF);
	mchip_set(MCHIP_VRJ_LIMIT_COMPRESSED_HI, 0xFFFF);
	mchip_set(MCHIP_VRJ_COMP_DATA_FORMAT, 0xC);
	mchip_set(MCHIP_VRJ_RESTART_INTERVAL, 0);
	mchip_set(MCHIP_VRJ_SOF1, 0x601);
	mchip_set(MCHIP_VRJ_SOF2, 0x1502);
	mchip_set(MCHIP_VRJ_SOF3, 0x1503);
	mchip_set(MCHIP_VRJ_SOF4, 0x1596);
	mchip_set(MCHIP_VRJ_SOS, 0x0ed0);

	mchip_load_tables();
}

/* sets the DMA parameters into the chip */
static void mchip_dma_setup(dma_addr_t dma_addr)
{
	int i;

	mchip_set(MCHIP_MM_PT_ADDR, (u32)dma_addr);
	for (i = 0; i < 4; i++)
		mchip_set(MCHIP_MM_FIR(i), 0);
	meye.mchip_fnum = 0;
}

/* setup for DMA transfers - also zeros the framebuffer */
static int mchip_dma_alloc(void)
{
	if (!meye.mchip_dmahandle)
		if (ptable_alloc())
			return -1;
	return 0;
}

/* frees the DMA buffer */
static void mchip_dma_free(void)
{
	if (meye.mchip_dmahandle) {
		mchip_dma_setup(0);
		ptable_free();
	}
}

/* stop any existing HIC action and wait for any dma to complete then
   reset the dma engine */
static void mchip_hic_stop(void)
{
	int i, j;

	meye.mchip_mode = MCHIP_HIC_MODE_NOOP;
	if (!(mchip_read(MCHIP_HIC_STATUS) & MCHIP_HIC_STATUS_BUSY))
		return;
	for (i = 0; i < 20; ++i) {
		mchip_set(MCHIP_HIC_CMD, MCHIP_HIC_CMD_STOP);
		mchip_delay(MCHIP_HIC_CMD, 0);
		for (j = 0; j < 100; ++j) {
			if (mchip_delay(MCHIP_HIC_STATUS,
					MCHIP_HIC_STATUS_IDLE))
				return;
			msleep(1);
		}
		printk(KERN_ERR "meye: need to reset HIC!\n");

		mchip_set(MCHIP_HIC_CTL, MCHIP_HIC_CTL_SOFT_RESET);
		msleep(250);
	}
	printk(KERN_ERR "meye: resetting HIC hanged!\n");
}

/****************************************************************************/
/* MCHIP frame processing functions                                         */
/****************************************************************************/

/* get the next ready frame from the dma engine */
static u32 mchip_get_frame(void)
{
	u32 v;

	v = mchip_read(MCHIP_MM_FIR(meye.mchip_fnum));
	return v;
}

/* frees the current frame from the dma engine */
static void mchip_free_frame(void)
{
	mchip_set(MCHIP_MM_FIR(meye.mchip_fnum), 0);
	meye.mchip_fnum++;
	meye.mchip_fnum %= 4;
}

/* read one frame from the framebuffer assuming it was captured using
   a uncompressed transfer */
static void mchip_cont_read_frame(u32 v, u8 *buf, int size)
{
	int pt_id;

	pt_id = (v >> 17) & 0x3FF;

	ptable_copy(buf, pt_id, size, MCHIP_NB_PAGES);
}

/* read a compressed frame from the framebuffer */
static int mchip_comp_read_frame(u32 v, u8 *buf, int size)
{
	int pt_start, pt_end, trailer;
	int fsize;
	int i;

	pt_start = (v >> 19) & 0xFF;
	pt_end = (v >> 11) & 0xFF;
	trailer = (v >> 1) & 0x3FF;

	if (pt_end < pt_start)
		fsize = (MCHIP_NB_PAGES_MJPEG - pt_start) * PAGE_SIZE +
			pt_end * PAGE_SIZE + trailer * 4;
	else
		fsize = (pt_end - pt_start) * PAGE_SIZE + trailer * 4;

	if (fsize > size) {
		printk(KERN_WARNING "meye: oversized compressed frame %d\n",
		       fsize);
		return -1;
	}

	ptable_copy(buf, pt_start, fsize, MCHIP_NB_PAGES_MJPEG);

#ifdef MEYE_JPEG_CORRECTION

	/* Some mchip generated jpeg frames are incorrect. In most
	 * (all ?) of those cases, the final EOI (0xff 0xd9) marker
	 * is not present at the end of the frame.
	 *
	 * Since adding the final marker is not enough to restore
	 * the jpeg integrity, we drop the frame.
	 */

	for (i = fsize - 1; i > 0 && buf[i] == 0xff; i--) ;

	if (i < 2 || buf[i - 1] != 0xff || buf[i] != 0xd9)
		return -1;

#endif

	return fsize;
}

/* take a picture into SDRAM */
static void mchip_take_picture(void)
{
	int i;

	mchip_hic_stop();
	mchip_subsample();
	mchip_dma_setup(meye.mchip_dmahandle);

	mchip_set(MCHIP_HIC_MODE, MCHIP_HIC_MODE_STILL_CAP);
	mchip_set(MCHIP_HIC_CMD, MCHIP_HIC_CMD_START);

	mchip_delay(MCHIP_HIC_CMD, 0);

	for (i = 0; i < 100; ++i) {
		if (mchip_delay(MCHIP_HIC_STATUS, MCHIP_HIC_STATUS_IDLE))
			break;
		msleep(1);
	}
}

/* dma a previously taken picture into a buffer */
static void mchip_get_picture(u8 *buf, int bufsize)
{
	u32 v;
	int i;

	mchip_set(MCHIP_HIC_MODE, MCHIP_HIC_MODE_STILL_OUT);
	mchip_set(MCHIP_HIC_CMD, MCHIP_HIC_CMD_START);

	mchip_delay(MCHIP_HIC_CMD, 0);
	for (i = 0; i < 100; ++i) {
		if (mchip_delay(MCHIP_HIC_STATUS, MCHIP_HIC_STATUS_IDLE))
			break;
		msleep(1);
	}
	for (i = 0; i < 4; ++i) {
		v = mchip_get_frame();
		if (v & MCHIP_MM_FIR_RDY) {
			mchip_cont_read_frame(v, buf, bufsize);
			break;
		}
		mchip_free_frame();
	}
}

/* start continuous dma capture */
static void mchip_continuous_start(void)
{
	mchip_hic_stop();
	mchip_subsample();
	mchip_set_framerate();
	mchip_dma_setup(meye.mchip_dmahandle);

	meye.mchip_mode = MCHIP_HIC_MODE_CONT_OUT;

	mchip_set(MCHIP_HIC_MODE, MCHIP_HIC_MODE_CONT_OUT);
	mchip_set(MCHIP_HIC_CMD, MCHIP_HIC_CMD_START);

	mchip_delay(MCHIP_HIC_CMD, 0);
}

/* compress one frame into a buffer */
static int mchip_compress_frame(u8 *buf, int bufsize)
{
	u32 v;
	int len = -1, i;

	mchip_vrj_setup(0x3f);
	udelay(50);

	mchip_set(MCHIP_HIC_MODE, MCHIP_HIC_MODE_STILL_COMP);
	mchip_set(MCHIP_HIC_CMD, MCHIP_HIC_CMD_START);

	mchip_delay(MCHIP_HIC_CMD, 0);
	for (i = 0; i < 100; ++i) {
		if (mchip_delay(MCHIP_HIC_STATUS, MCHIP_HIC_STATUS_IDLE))
			break;
		msleep(1);
	}

	for (i = 0; i < 4; ++i) {
		v = mchip_get_frame();
		if (v & MCHIP_MM_FIR_RDY) {
			len = mchip_comp_read_frame(v, buf, bufsize);
			break;
		}
		mchip_free_frame();
	}
	return len;
}

#if 0
/* uncompress one image into a buffer */
static int mchip_uncompress_frame(u8 *img, int imgsize, u8 *buf, int bufsize)
{
	mchip_vrj_setup(0x3f);
	udelay(50);

	mchip_set(MCHIP_HIC_MODE, MCHIP_HIC_MODE_STILL_DECOMP);
	mchip_set(MCHIP_HIC_CMD, MCHIP_HIC_CMD_START);

	mchip_delay(MCHIP_HIC_CMD, 0);

	return mchip_comp_read_frame(buf, bufsize);
}
#endif

/* start continuous compressed capture */
static void mchip_cont_compression_start(void)
{
	mchip_hic_stop();
	mchip_vrj_setup(0x3f);
	mchip_subsample();
	mchip_set_framerate();
	mchip_dma_setup(meye.mchip_dmahandle);

	meye.mchip_mode = MCHIP_HIC_MODE_CONT_COMP;

	mchip_set(MCHIP_HIC_MODE, MCHIP_HIC_MODE_CONT_COMP);
	mchip_set(MCHIP_HIC_CMD, MCHIP_HIC_CMD_START);

	mchip_delay(MCHIP_HIC_CMD, 0);
}

/****************************************************************************/
/* Interrupt handling                                                       */
/****************************************************************************/

static irqreturn_t meye_irq(int irq, void *dev_id)
{
	u32 v;
	int reqnr;
	static int sequence;

	v = mchip_read(MCHIP_MM_INTA);

	if (meye.mchip_mode != MCHIP_HIC_MODE_CONT_OUT &&
	    meye.mchip_mode != MCHIP_HIC_MODE_CONT_COMP)
		return IRQ_NONE;

again:
	v = mchip_get_frame();
	if (!(v & MCHIP_MM_FIR_RDY))
		return IRQ_HANDLED;

	if (meye.mchip_mode == MCHIP_HIC_MODE_CONT_OUT) {
		if (kfifo_out_locked(&meye.grabq, (unsigned char *)&reqnr,
			      sizeof(int), &meye.grabq_lock) != sizeof(int)) {
			mchip_free_frame();
			return IRQ_HANDLED;
		}
		mchip_cont_read_frame(v, meye.grab_fbuffer + gbufsize * reqnr,
				      mchip_hsize() * mchip_vsize() * 2);
		meye.grab_buffer[reqnr].size = mchip_hsize() * mchip_vsize() * 2;
		meye.grab_buffer[reqnr].state = MEYE_BUF_DONE;
		v4l2_get_timestamp(&meye.grab_buffer[reqnr].timestamp);
		meye.grab_buffer[reqnr].sequence = sequence++;
		kfifo_in_locked(&meye.doneq, (unsigned char *)&reqnr,
				sizeof(int), &meye.doneq_lock);
		wake_up_interruptible(&meye.proc_list);
	} else {
		int size;
		size = mchip_comp_read_frame(v, meye.grab_temp, gbufsize);
		if (size == -1) {
			mchip_free_frame();
			goto again;
		}
		if (kfifo_out_locked(&meye.grabq, (unsigned char *)&reqnr,
			      sizeof(int), &meye.grabq_lock) != sizeof(int)) {
			mchip_free_frame();
			goto again;
		}
		memcpy(meye.grab_fbuffer + gbufsize * reqnr, meye.grab_temp,
		       size);
		meye.grab_buffer[reqnr].size = size;
		meye.grab_buffer[reqnr].state = MEYE_BUF_DONE;
		v4l2_get_timestamp(&meye.grab_buffer[reqnr].timestamp);
		meye.grab_buffer[reqnr].sequence = sequence++;
		kfifo_in_locked(&meye.doneq, (unsigned char *)&reqnr,
				sizeof(int), &meye.doneq_lock);
		wake_up_interruptible(&meye.proc_list);
	}
	mchip_free_frame();
	goto again;
}

/****************************************************************************/
/* video4linux integration                                                  */
/****************************************************************************/

static int meye_open(struct file *file)
{
	int i;

	if (test_and_set_bit(0, &meye.in_use))
		return -EBUSY;

	mchip_hic_stop();

	if (mchip_dma_alloc()) {
		printk(KERN_ERR "meye: mchip framebuffer allocation failed\n");
		clear_bit(0, &meye.in_use);
		return -ENOBUFS;
	}

	for (i = 0; i < MEYE_MAX_BUFNBRS; i++)
		meye.grab_buffer[i].state = MEYE_BUF_UNUSED;
	kfifo_reset(&meye.grabq);
	kfifo_reset(&meye.doneq);
	return v4l2_fh_open(file);
}

static int meye_release(struct file *file)
{
	mchip_hic_stop();
	mchip_dma_free();
	clear_bit(0, &meye.in_use);
	return v4l2_fh_release(file);
}

static int meyeioc_g_params(struct meye_params *p)
{
	*p = meye.params;
	return 0;
}

static int meyeioc_s_params(struct meye_params *jp)
{
	if (jp->subsample > 1)
		return -EINVAL;

	if (jp->quality > 10)
		return -EINVAL;

	if (jp->sharpness > 63 || jp->agc > 63 || jp->picture > 63)
		return -EINVAL;

	if (jp->framerate > 31)
		return -EINVAL;

	mutex_lock(&meye.lock);

	if (meye.params.subsample != jp->subsample ||
	    meye.params.quality != jp->quality)
		mchip_hic_stop();	/* need restart */

	meye.params = *jp;
	sony_pic_camera_command(SONY_PIC_COMMAND_SETCAMERASHARPNESS,
			      meye.params.sharpness);
	sony_pic_camera_command(SONY_PIC_COMMAND_SETCAMERAAGC,
			      meye.params.agc);
	sony_pic_camera_command(SONY_PIC_COMMAND_SETCAMERAPICTURE,
			      meye.params.picture);
	mutex_unlock(&meye.lock);

	return 0;
}

static int meyeioc_qbuf_capt(int *nb)
{
	if (!meye.grab_fbuffer)
		return -EINVAL;

	if (*nb >= gbuffers)
		return -EINVAL;

	if (*nb < 0) {
		/* stop capture */
		mchip_hic_stop();
		return 0;
	}

	if (meye.grab_buffer[*nb].state != MEYE_BUF_UNUSED)
		return -EBUSY;

	mutex_lock(&meye.lock);

	if (meye.mchip_mode != MCHIP_HIC_MODE_CONT_COMP)
		mchip_cont_compression_start();

	meye.grab_buffer[*nb].state = MEYE_BUF_USING;
	kfifo_in_locked(&meye.grabq, (unsigned char *)nb, sizeof(int),
			 &meye.grabq_lock);
	mutex_unlock(&meye.lock);

	return 0;
}

static int meyeioc_sync(struct file *file, void *fh, int *i)
{
	int unused;

	if (*i < 0 || *i >= gbuffers)
		return -EINVAL;

	mutex_lock(&meye.lock);
	switch (meye.grab_buffer[*i].state) {

	case MEYE_BUF_UNUSED:
		mutex_unlock(&meye.lock);
		return -EINVAL;
	case MEYE_BUF_USING:
		if (file->f_flags & O_NONBLOCK) {
			mutex_unlock(&meye.lock);
			return -EAGAIN;
		}
		if (wait_event_interruptible(meye.proc_list,
			(meye.grab_buffer[*i].state != MEYE_BUF_USING))) {
			mutex_unlock(&meye.lock);
			return -EINTR;
		}
		/* fall through */
	case MEYE_BUF_DONE:
		meye.grab_buffer[*i].state = MEYE_BUF_UNUSED;
		if (kfifo_out_locked(&meye.doneq, (unsigned char *)&unused,
				sizeof(int), &meye.doneq_lock) != sizeof(int))
					break;
	}
	*i = meye.grab_buffer[*i].size;
	mutex_unlock(&meye.lock);
	return 0;
}

static int meyeioc_stillcapt(void)
{
	if (!meye.grab_fbuffer)
		return -EINVAL;

	if (meye.grab_buffer[0].state != MEYE_BUF_UNUSED)
		return -EBUSY;

	mutex_lock(&meye.lock);
	meye.grab_buffer[0].state = MEYE_BUF_USING;
	mchip_take_picture();

	mchip_get_picture(meye.grab_fbuffer,
			mchip_hsize() * mchip_vsize() * 2);

	meye.grab_buffer[0].state = MEYE_BUF_DONE;
	mutex_unlock(&meye.lock);

	return 0;
}

static int meyeioc_stilljcapt(int *len)
{
	if (!meye.grab_fbuffer)
		return -EINVAL;

	if (meye.grab_buffer[0].state != MEYE_BUF_UNUSED)
		return -EBUSY;

	mutex_lock(&meye.lock);
	meye.grab_buffer[0].state = MEYE_BUF_USING;
	*len = -1;

	while (*len == -1) {
		mchip_take_picture();
		*len = mchip_compress_frame(meye.grab_fbuffer, gbufsize);
	}

	meye.grab_buffer[0].state = MEYE_BUF_DONE;
	mutex_unlock(&meye.lock);
	return 0;
}

static int vidioc_querycap(struct file *file, void *fh,
				struct v4l2_capability *cap)
{
	strcpy(cap->driver, "meye");
	strcpy(cap->card, "meye");
	sprintf(cap->bus_info, "PCI:%s", pci_name(meye.mchip_dev));

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			    V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_enum_input(struct file *file, void *fh, struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;

	strcpy(i->name, "Camera");
	i->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	if (i != 0)
		return -EINVAL;

	return 0;
}

static int meye_s_ctrl(struct v4l2_ctrl *ctrl)
{
	mutex_lock(&meye.lock);
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		sony_pic_camera_command(
			SONY_PIC_COMMAND_SETCAMERABRIGHTNESS, ctrl->val);
		meye.brightness = ctrl->val << 10;
		break;
	case V4L2_CID_HUE:
		sony_pic_camera_command(
			SONY_PIC_COMMAND_SETCAMERAHUE, ctrl->val);
		meye.hue = ctrl->val << 10;
		break;
	case V4L2_CID_CONTRAST:
		sony_pic_camera_command(
			SONY_PIC_COMMAND_SETCAMERACONTRAST, ctrl->val);
		meye.contrast = ctrl->val << 10;
		break;
	case V4L2_CID_SATURATION:
		sony_pic_camera_command(
			SONY_PIC_COMMAND_SETCAMERACOLOR, ctrl->val);
		meye.colour = ctrl->val << 10;
		break;
	case V4L2_CID_MEYE_AGC:
		sony_pic_camera_command(
			SONY_PIC_COMMAND_SETCAMERAAGC, ctrl->val);
		meye.params.agc = ctrl->val;
		break;
	case V4L2_CID_SHARPNESS:
		sony_pic_camera_command(
			SONY_PIC_COMMAND_SETCAMERASHARPNESS, ctrl->val);
		meye.params.sharpness = ctrl->val;
		break;
	case V4L2_CID_MEYE_PICTURE:
		sony_pic_camera_command(
			SONY_PIC_COMMAND_SETCAMERAPICTURE, ctrl->val);
		meye.params.picture = ctrl->val;
		break;
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		meye.params.quality = ctrl->val;
		break;
	case V4L2_CID_MEYE_FRAMERATE:
		meye.params.framerate = ctrl->val;
		break;
	default:
		mutex_unlock(&meye.lock);
		return -EINVAL;
	}
	mutex_unlock(&meye.lock);

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_fmtdesc *f)
{
	if (f->index > 1)
		return -EINVAL;

	if (f->index == 0) {
		/* standard YUV 422 capture */
		f->flags = 0;
		strcpy(f->description, "YUV422");
		f->pixelformat = V4L2_PIX_FMT_YUYV;
	} else {
		/* compressed MJPEG capture */
		f->flags = V4L2_FMT_FLAG_COMPRESSED;
		strcpy(f->description, "MJPEG");
		f->pixelformat = V4L2_PIX_FMT_MJPEG;
	}

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV &&
	    f->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	if (f->fmt.pix.field != V4L2_FIELD_ANY &&
	    f->fmt.pix.field != V4L2_FIELD_NONE)
		return -EINVAL;

	f->fmt.pix.field = V4L2_FIELD_NONE;

	if (f->fmt.pix.width <= 320) {
		f->fmt.pix.width = 320;
		f->fmt.pix.height = 240;
	} else {
		f->fmt.pix.width = 640;
		f->fmt.pix.height = 480;
	}

	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height *
			       f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = 0;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *fh,
				    struct v4l2_format *f)
{
	switch (meye.mchip_mode) {
	case MCHIP_HIC_MODE_CONT_OUT:
	default:
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	case MCHIP_HIC_MODE_CONT_COMP:
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
		break;
	}

	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.width = mchip_hsize();
	f->fmt.pix.height = mchip_vsize();
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height *
			       f->fmt.pix.bytesperline;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *fh,
				    struct v4l2_format *f)
{
	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV &&
	    f->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	if (f->fmt.pix.field != V4L2_FIELD_ANY &&
	    f->fmt.pix.field != V4L2_FIELD_NONE)
		return -EINVAL;

	f->fmt.pix.field = V4L2_FIELD_NONE;
	mutex_lock(&meye.lock);

	if (f->fmt.pix.width <= 320) {
		f->fmt.pix.width = 320;
		f->fmt.pix.height = 240;
		meye.params.subsample = 1;
	} else {
		f->fmt.pix.width = 640;
		f->fmt.pix.height = 480;
		meye.params.subsample = 0;
	}

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		meye.mchip_mode = MCHIP_HIC_MODE_CONT_OUT;
		break;
	case V4L2_PIX_FMT_MJPEG:
		meye.mchip_mode = MCHIP_HIC_MODE_CONT_COMP;
		break;
	}

	mutex_unlock(&meye.lock);
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.height *
			       f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = 0;

	return 0;
}

static int vidioc_reqbufs(struct file *file, void *fh,
				struct v4l2_requestbuffers *req)
{
	int i;

	if (req->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	if (meye.grab_fbuffer && req->count == gbuffers) {
		/* already allocated, no modifications */
		return 0;
	}

	mutex_lock(&meye.lock);
	if (meye.grab_fbuffer) {
		for (i = 0; i < gbuffers; i++)
			if (meye.vma_use_count[i]) {
				mutex_unlock(&meye.lock);
				return -EINVAL;
			}
		rvfree(meye.grab_fbuffer, gbuffers * gbufsize);
		meye.grab_fbuffer = NULL;
	}

	gbuffers = max(2, min((int)req->count, MEYE_MAX_BUFNBRS));
	req->count = gbuffers;
	meye.grab_fbuffer = rvmalloc(gbuffers * gbufsize);

	if (!meye.grab_fbuffer) {
		printk(KERN_ERR "meye: v4l framebuffer allocation"
				" failed\n");
		mutex_unlock(&meye.lock);
		return -ENOMEM;
	}

	for (i = 0; i < gbuffers; i++)
		meye.vma_use_count[i] = 0;

	mutex_unlock(&meye.lock);

	return 0;
}

static int vidioc_querybuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	unsigned int index = buf->index;

	if (index >= gbuffers)
		return -EINVAL;

	buf->bytesused = meye.grab_buffer[index].size;
	buf->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	if (meye.grab_buffer[index].state == MEYE_BUF_USING)
		buf->flags |= V4L2_BUF_FLAG_QUEUED;

	if (meye.grab_buffer[index].state == MEYE_BUF_DONE)
		buf->flags |= V4L2_BUF_FLAG_DONE;

	buf->field = V4L2_FIELD_NONE;
	buf->timestamp = meye.grab_buffer[index].timestamp;
	buf->sequence = meye.grab_buffer[index].sequence;
	buf->memory = V4L2_MEMORY_MMAP;
	buf->m.offset = index * gbufsize;
	buf->length = gbufsize;

	return 0;
}

static int vidioc_qbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	if (buf->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	if (buf->index >= gbuffers)
		return -EINVAL;

	if (meye.grab_buffer[buf->index].state != MEYE_BUF_UNUSED)
		return -EINVAL;

	mutex_lock(&meye.lock);
	buf->flags |= V4L2_BUF_FLAG_QUEUED;
	buf->flags &= ~V4L2_BUF_FLAG_DONE;
	meye.grab_buffer[buf->index].state = MEYE_BUF_USING;
	kfifo_in_locked(&meye.grabq, (unsigned char *)&buf->index,
			sizeof(int), &meye.grabq_lock);
	mutex_unlock(&meye.lock);

	return 0;
}

static int vidioc_dqbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	int reqnr;

	if (buf->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	mutex_lock(&meye.lock);

	if (kfifo_len(&meye.doneq) == 0 && file->f_flags & O_NONBLOCK) {
		mutex_unlock(&meye.lock);
		return -EAGAIN;
	}

	if (wait_event_interruptible(meye.proc_list,
				     kfifo_len(&meye.doneq) != 0) < 0) {
		mutex_unlock(&meye.lock);
		return -EINTR;
	}

	if (!kfifo_out_locked(&meye.doneq, (unsigned char *)&reqnr,
		       sizeof(int), &meye.doneq_lock)) {
		mutex_unlock(&meye.lock);
		return -EBUSY;
	}

	if (meye.grab_buffer[reqnr].state != MEYE_BUF_DONE) {
		mutex_unlock(&meye.lock);
		return -EINVAL;
	}

	buf->index = reqnr;
	buf->bytesused = meye.grab_buffer[reqnr].size;
	buf->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	buf->field = V4L2_FIELD_NONE;
	buf->timestamp = meye.grab_buffer[reqnr].timestamp;
	buf->sequence = meye.grab_buffer[reqnr].sequence;
	buf->memory = V4L2_MEMORY_MMAP;
	buf->m.offset = reqnr * gbufsize;
	buf->length = gbufsize;
	meye.grab_buffer[reqnr].state = MEYE_BUF_UNUSED;
	mutex_unlock(&meye.lock);

	return 0;
}

static int vidioc_streamon(struct file *file, void *fh, enum v4l2_buf_type i)
{
	mutex_lock(&meye.lock);

	switch (meye.mchip_mode) {
	case MCHIP_HIC_MODE_CONT_OUT:
		mchip_continuous_start();
		break;
	case MCHIP_HIC_MODE_CONT_COMP:
		mchip_cont_compression_start();
		break;
	default:
		mutex_unlock(&meye.lock);
		return -EINVAL;
	}

	mutex_unlock(&meye.lock);

	return 0;
}

static int vidioc_streamoff(struct file *file, void *fh, enum v4l2_buf_type i)
{
	mutex_lock(&meye.lock);
	mchip_hic_stop();
	kfifo_reset(&meye.grabq);
	kfifo_reset(&meye.doneq);

	for (i = 0; i < MEYE_MAX_BUFNBRS; i++)
		meye.grab_buffer[i].state = MEYE_BUF_UNUSED;

	mutex_unlock(&meye.lock);
	return 0;
}

static long vidioc_default(struct file *file, void *fh, bool valid_prio,
			   unsigned int cmd, void *arg)
{
	switch (cmd) {
	case MEYEIOC_G_PARAMS:
		return meyeioc_g_params((struct meye_params *) arg);

	case MEYEIOC_S_PARAMS:
		return meyeioc_s_params((struct meye_params *) arg);

	case MEYEIOC_QBUF_CAPT:
		return meyeioc_qbuf_capt((int *) arg);

	case MEYEIOC_SYNC:
		return meyeioc_sync(file, fh, (int *) arg);

	case MEYEIOC_STILLCAPT:
		return meyeioc_stillcapt();

	case MEYEIOC_STILLJCAPT:
		return meyeioc_stilljcapt((int *) arg);

	default:
		return -ENOTTY;
	}

}

static unsigned int meye_poll(struct file *file, poll_table *wait)
{
	unsigned int res = v4l2_ctrl_poll(file, wait);

	mutex_lock(&meye.lock);
	poll_wait(file, &meye.proc_list, wait);
	if (kfifo_len(&meye.doneq))
		res |= POLLIN | POLLRDNORM;
	mutex_unlock(&meye.lock);
	return res;
}

static void meye_vm_open(struct vm_area_struct *vma)
{
	long idx = (long)vma->vm_private_data;
	meye.vma_use_count[idx]++;
}

static void meye_vm_close(struct vm_area_struct *vma)
{
	long idx = (long)vma->vm_private_data;
	meye.vma_use_count[idx]--;
}

static const struct vm_operations_struct meye_vm_ops = {
	.open		= meye_vm_open,
	.close		= meye_vm_close,
};

static int meye_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;

	mutex_lock(&meye.lock);
	if (size > gbuffers * gbufsize) {
		mutex_unlock(&meye.lock);
		return -EINVAL;
	}
	if (!meye.grab_fbuffer) {
		int i;

		/* lazy allocation */
		meye.grab_fbuffer = rvmalloc(gbuffers*gbufsize);
		if (!meye.grab_fbuffer) {
			printk(KERN_ERR "meye: v4l framebuffer allocation failed\n");
			mutex_unlock(&meye.lock);
			return -ENOMEM;
		}
		for (i = 0; i < gbuffers; i++)
			meye.vma_use_count[i] = 0;
	}
	pos = (unsigned long)meye.grab_fbuffer + offset;

	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
			mutex_unlock(&meye.lock);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	vma->vm_ops = &meye_vm_ops;
	vma->vm_flags &= ~VM_IO;	/* not I/O memory */
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_private_data = (void *) (offset / gbufsize);
	meye_vm_open(vma);

	mutex_unlock(&meye.lock);
	return 0;
}

static const struct v4l2_file_operations meye_fops = {
	.owner		= THIS_MODULE,
	.open		= meye_open,
	.release	= meye_release,
	.mmap		= meye_mmap,
	.unlocked_ioctl	= video_ioctl2,
	.poll		= meye_poll,
};

static const struct v4l2_ioctl_ops meye_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,
	.vidioc_enum_input	= vidioc_enum_input,
	.vidioc_g_input		= vidioc_g_input,
	.vidioc_s_input		= vidioc_s_input,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs		= vidioc_reqbufs,
	.vidioc_querybuf	= vidioc_querybuf,
	.vidioc_qbuf		= vidioc_qbuf,
	.vidioc_dqbuf		= vidioc_dqbuf,
	.vidioc_streamon	= vidioc_streamon,
	.vidioc_streamoff	= vidioc_streamoff,
	.vidioc_log_status	= v4l2_ctrl_log_status,
	.vidioc_subscribe_event	= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_default		= vidioc_default,
};

static struct video_device meye_template = {
	.name		= "meye",
	.fops		= &meye_fops,
	.ioctl_ops 	= &meye_ioctl_ops,
	.release	= video_device_release_empty,
};

static const struct v4l2_ctrl_ops meye_ctrl_ops = {
	.s_ctrl = meye_s_ctrl,
};

#ifdef CONFIG_PM
static int meye_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state(pdev);
	meye.pm_mchip_mode = meye.mchip_mode;
	mchip_hic_stop();
	mchip_set(MCHIP_MM_INTA, 0x0);
	return 0;
}

static int meye_resume(struct pci_dev *pdev)
{
	pci_restore_state(pdev);
	pci_write_config_word(meye.mchip_dev, MCHIP_PCI_SOFTRESET_SET, 1);

	mchip_delay(MCHIP_HIC_CMD, 0);
	mchip_delay(MCHIP_HIC_STATUS, MCHIP_HIC_STATUS_IDLE);
	msleep(1);
	mchip_set(MCHIP_VRJ_SOFT_RESET, 1);
	msleep(1);
	mchip_set(MCHIP_MM_PCI_MODE, 5);
	msleep(1);
	mchip_set(MCHIP_MM_INTA, MCHIP_MM_INTA_HIC_1_MASK);

	switch (meye.pm_mchip_mode) {
	case MCHIP_HIC_MODE_CONT_OUT:
		mchip_continuous_start();
		break;
	case MCHIP_HIC_MODE_CONT_COMP:
		mchip_cont_compression_start();
		break;
	}
	return 0;
}
#endif

static int meye_probe(struct pci_dev *pcidev, const struct pci_device_id *ent)
{
	static const struct v4l2_ctrl_config ctrl_agc = {
		.id = V4L2_CID_MEYE_AGC,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.ops = &meye_ctrl_ops,
		.name = "AGC",
		.max = 63,
		.step = 1,
		.def = 48,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	};
	static const struct v4l2_ctrl_config ctrl_picture = {
		.id = V4L2_CID_MEYE_PICTURE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.ops = &meye_ctrl_ops,
		.name = "Picture",
		.max = 63,
		.step = 1,
	};
	static const struct v4l2_ctrl_config ctrl_framerate = {
		.id = V4L2_CID_MEYE_FRAMERATE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.ops = &meye_ctrl_ops,
		.name = "Framerate",
		.max = 31,
		.step = 1,
	};
	struct v4l2_device *v4l2_dev = &meye.v4l2_dev;
	int ret = -EBUSY;
	unsigned long mchip_adr;

	if (meye.mchip_dev != NULL) {
		printk(KERN_ERR "meye: only one device allowed!\n");
		return ret;
	}

	ret = v4l2_device_register(&pcidev->dev, v4l2_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		return ret;
	}
	ret = -ENOMEM;
	meye.mchip_dev = pcidev;

	meye.grab_temp = vmalloc(MCHIP_NB_PAGES_MJPEG * PAGE_SIZE);
	if (!meye.grab_temp) {
		v4l2_err(v4l2_dev, "grab buffer allocation failed\n");
		goto outvmalloc;
	}

	spin_lock_init(&meye.grabq_lock);
	if (kfifo_alloc(&meye.grabq, sizeof(int) * MEYE_MAX_BUFNBRS,
				GFP_KERNEL)) {
		v4l2_err(v4l2_dev, "fifo allocation failed\n");
		goto outkfifoalloc1;
	}
	spin_lock_init(&meye.doneq_lock);
	if (kfifo_alloc(&meye.doneq, sizeof(int) * MEYE_MAX_BUFNBRS,
				GFP_KERNEL)) {
		v4l2_err(v4l2_dev, "fifo allocation failed\n");
		goto outkfifoalloc2;
	}

	meye.vdev = meye_template;
	meye.vdev.v4l2_dev = &meye.v4l2_dev;

	ret = -EIO;
	if ((ret = sony_pic_camera_command(SONY_PIC_COMMAND_SETCAMERA, 1))) {
		v4l2_err(v4l2_dev, "meye: unable to power on the camera\n");
		v4l2_err(v4l2_dev, "meye: did you enable the camera in "
				"sonypi using the module options ?\n");
		goto outsonypienable;
	}

	if ((ret = pci_enable_device(meye.mchip_dev))) {
		v4l2_err(v4l2_dev, "meye: pci_enable_device failed\n");
		goto outenabledev;
	}

	mchip_adr = pci_resource_start(meye.mchip_dev,0);
	if (!mchip_adr) {
		v4l2_err(v4l2_dev, "meye: mchip has no device base address\n");
		goto outregions;
	}
	if (!request_mem_region(pci_resource_start(meye.mchip_dev, 0),
				pci_resource_len(meye.mchip_dev, 0),
				"meye")) {
		v4l2_err(v4l2_dev, "meye: request_mem_region failed\n");
		goto outregions;
	}
	meye.mchip_mmregs = ioremap(mchip_adr, MCHIP_MM_REGS);
	if (!meye.mchip_mmregs) {
		v4l2_err(v4l2_dev, "meye: ioremap failed\n");
		goto outremap;
	}

	meye.mchip_irq = pcidev->irq;
	if (request_irq(meye.mchip_irq, meye_irq,
			IRQF_SHARED, "meye", meye_irq)) {
		v4l2_err(v4l2_dev, "request_irq failed\n");
		goto outreqirq;
	}

	pci_write_config_byte(meye.mchip_dev, PCI_CACHE_LINE_SIZE, 8);
	pci_write_config_byte(meye.mchip_dev, PCI_LATENCY_TIMER, 64);

	pci_set_master(meye.mchip_dev);

	/* Ask the camera to perform a soft reset. */
	pci_write_config_word(meye.mchip_dev, MCHIP_PCI_SOFTRESET_SET, 1);

	mchip_delay(MCHIP_HIC_CMD, 0);
	mchip_delay(MCHIP_HIC_STATUS, MCHIP_HIC_STATUS_IDLE);

	msleep(1);
	mchip_set(MCHIP_VRJ_SOFT_RESET, 1);

	msleep(1);
	mchip_set(MCHIP_MM_PCI_MODE, 5);

	msleep(1);
	mchip_set(MCHIP_MM_INTA, MCHIP_MM_INTA_HIC_1_MASK);

	mutex_init(&meye.lock);
	init_waitqueue_head(&meye.proc_list);

	v4l2_ctrl_handler_init(&meye.hdl, 3);
	v4l2_ctrl_new_std(&meye.hdl, &meye_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 63, 1, 32);
	v4l2_ctrl_new_std(&meye.hdl, &meye_ctrl_ops,
			  V4L2_CID_HUE, 0, 63, 1, 32);
	v4l2_ctrl_new_std(&meye.hdl, &meye_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 63, 1, 32);
	v4l2_ctrl_new_std(&meye.hdl, &meye_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 63, 1, 32);
	v4l2_ctrl_new_custom(&meye.hdl, &ctrl_agc, NULL);
	v4l2_ctrl_new_std(&meye.hdl, &meye_ctrl_ops,
			  V4L2_CID_SHARPNESS, 0, 63, 1, 32);
	v4l2_ctrl_new_custom(&meye.hdl, &ctrl_picture, NULL);
	v4l2_ctrl_new_std(&meye.hdl, &meye_ctrl_ops,
			  V4L2_CID_JPEG_COMPRESSION_QUALITY, 0, 10, 1, 8);
	v4l2_ctrl_new_custom(&meye.hdl, &ctrl_framerate, NULL);
	if (meye.hdl.error) {
		v4l2_err(v4l2_dev, "couldn't register controls\n");
		goto outvideoreg;
	}

	v4l2_ctrl_handler_setup(&meye.hdl);
	meye.vdev.ctrl_handler = &meye.hdl;

	if (video_register_device(&meye.vdev, VFL_TYPE_GRABBER,
				  video_nr) < 0) {
		v4l2_err(v4l2_dev, "video_register_device failed\n");
		goto outvideoreg;
	}

	v4l2_info(v4l2_dev, "Motion Eye Camera Driver v%s.\n",
	       MEYE_DRIVER_VERSION);
	v4l2_info(v4l2_dev, "mchip KL5A72002 rev. %d, base %lx, irq %d\n",
	       meye.mchip_dev->revision, mchip_adr, meye.mchip_irq);

	return 0;

outvideoreg:
	v4l2_ctrl_handler_free(&meye.hdl);
	free_irq(meye.mchip_irq, meye_irq);
outreqirq:
	iounmap(meye.mchip_mmregs);
outremap:
	release_mem_region(pci_resource_start(meye.mchip_dev, 0),
			   pci_resource_len(meye.mchip_dev, 0));
outregions:
	pci_disable_device(meye.mchip_dev);
outenabledev:
	sony_pic_camera_command(SONY_PIC_COMMAND_SETCAMERA, 0);
outsonypienable:
	kfifo_free(&meye.doneq);
outkfifoalloc2:
	kfifo_free(&meye.grabq);
outkfifoalloc1:
	vfree(meye.grab_temp);
outvmalloc:
	return ret;
}

static void meye_remove(struct pci_dev *pcidev)
{
	video_unregister_device(&meye.vdev);

	mchip_hic_stop();

	mchip_dma_free();

	/* disable interrupts */
	mchip_set(MCHIP_MM_INTA, 0x0);

	free_irq(meye.mchip_irq, meye_irq);

	iounmap(meye.mchip_mmregs);

	release_mem_region(pci_resource_start(meye.mchip_dev, 0),
			   pci_resource_len(meye.mchip_dev, 0));

	pci_disable_device(meye.mchip_dev);

	sony_pic_camera_command(SONY_PIC_COMMAND_SETCAMERA, 0);

	kfifo_free(&meye.doneq);
	kfifo_free(&meye.grabq);

	vfree(meye.grab_temp);

	if (meye.grab_fbuffer) {
		rvfree(meye.grab_fbuffer, gbuffers*gbufsize);
		meye.grab_fbuffer = NULL;
	}

	printk(KERN_INFO "meye: removed\n");
}

static struct pci_device_id meye_pci_tbl[] = {
	{ PCI_VDEVICE(KAWASAKI, PCI_DEVICE_ID_MCHIP_KL5A72002), 0 },
	{ }
};

MODULE_DEVICE_TABLE(pci, meye_pci_tbl);

static struct pci_driver meye_driver = {
	.name		= "meye",
	.id_table	= meye_pci_tbl,
	.probe		= meye_probe,
	.remove		= meye_remove,
#ifdef CONFIG_PM
	.suspend	= meye_suspend,
	.resume		= meye_resume,
#endif
};

static int __init meye_init(void)
{
	gbuffers = max(2, min((int)gbuffers, MEYE_MAX_BUFNBRS));
	if (gbufsize > MEYE_MAX_BUFSIZE)
		gbufsize = MEYE_MAX_BUFSIZE;
	gbufsize = PAGE_ALIGN(gbufsize);
	printk(KERN_INFO "meye: using %d buffers with %dk (%dk total) "
			 "for capture\n",
			 gbuffers,
			 gbufsize / 1024, gbuffers * gbufsize / 1024);
	return pci_register_driver(&meye_driver);
}

static void __exit meye_exit(void)
{
	pci_unregister_driver(&meye_driver);
}

module_init(meye_init);
module_exit(meye_exit);
