/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * Changes for BUZ by Wolfgang Scherr <scherr@net4you.net>
 *
 * Changes for DC10/DC30 by Laurent Pinchart <laurent.pinchart@skynet.be>
 *
 * Changes for LML33R10 by Maxim Yevtyushkin <max@linuxmedialabs.com>
 *
 * Changes for videodev2/v4l2 by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * Based on
 *
 * Miro DC10 driver
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 *
 * Iomega Buz driver version 1.0
 * Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 *
 * buz.0.0.3
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * bttv - Bt848 frame grabber driver
 * Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
 *                        & Marcus Metzler (mocm@thp.uni-koeln.de)
 *
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

#include <linux/config.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/byteorder/generic.h>

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include <linux/spinlock.h>
#define     MAP_NR(x)       virt_to_page(x)
#define     ZORAN_HARDWARE  VID_HARDWARE_ZR36067
#define     ZORAN_VID_TYPE  ( \
				VID_TYPE_CAPTURE | \
				VID_TYPE_OVERLAY | \
				VID_TYPE_CLIPPING | \
				VID_TYPE_FRAMERAM | \
				VID_TYPE_SCALES | \
				VID_TYPE_MJPEG_DECODER | \
				VID_TYPE_MJPEG_ENCODER \
			     )

#include <linux/videodev.h>
#include "videocodec.h"

#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

#include <linux/video_decoder.h>
#include <linux/video_encoder.h>
#include "zoran.h"
#include "zoran_device.h"
#include "zoran_card.h"

#ifdef HAVE_V4L2
	/* we declare some card type definitions here, they mean
	 * the same as the v4l1 ZORAN_VID_TYPE above, except it's v4l2 */
#define ZORAN_V4L2_VID_FLAGS ( \
				V4L2_CAP_STREAMING |\
				V4L2_CAP_VIDEO_CAPTURE |\
				V4L2_CAP_VIDEO_OUTPUT |\
				V4L2_CAP_VIDEO_OVERLAY \
                              )
#endif

#include <asm/byteorder.h>

const struct zoran_format zoran_formats[] = {
	{
		.name = "15-bit RGB",
		.palette = VIDEO_PALETTE_RGB555,
#ifdef HAVE_V4L2
#ifdef __LITTLE_ENDIAN
		.fourcc = V4L2_PIX_FMT_RGB555,
#else
		.fourcc = V4L2_PIX_FMT_RGB555X,
#endif
		.colorspace = V4L2_COLORSPACE_SRGB,
#endif
		.depth = 15,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
	}, {
		.name = "16-bit RGB",
		.palette = VIDEO_PALETTE_RGB565,
#ifdef HAVE_V4L2
#ifdef __LITTLE_ENDIAN
		.fourcc = V4L2_PIX_FMT_RGB565,
#else
		.fourcc = V4L2_PIX_FMT_RGB565X,
#endif
		.colorspace = V4L2_COLORSPACE_SRGB,
#endif
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
	}, {
		.name = "24-bit RGB",
		.palette = VIDEO_PALETTE_RGB24,
#ifdef HAVE_V4L2
#ifdef __LITTLE_ENDIAN
		.fourcc = V4L2_PIX_FMT_BGR24,
#else
		.fourcc = V4L2_PIX_FMT_RGB24,
#endif
		.colorspace = V4L2_COLORSPACE_SRGB,
#endif
		.depth = 24,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
	}, {
		.name = "32-bit RGB",
		.palette = VIDEO_PALETTE_RGB32,
#ifdef HAVE_V4L2
#ifdef __LITTLE_ENDIAN
		.fourcc = V4L2_PIX_FMT_BGR32,
#else
		.fourcc = V4L2_PIX_FMT_RGB32,
#endif
		.colorspace = V4L2_COLORSPACE_SRGB,
#endif
		.depth = 32,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
	}, {
		.name = "4:2:2, packed, YUYV",
		.palette = VIDEO_PALETTE_YUV422,
#ifdef HAVE_V4L2
		.fourcc = V4L2_PIX_FMT_YUYV,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
#endif
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE |
		         ZORAN_FORMAT_OVERLAY,
	}, {
		.name = "Hardware-encoded Motion-JPEG",
		.palette = -1,
#ifdef HAVE_V4L2
		.fourcc = V4L2_PIX_FMT_MJPEG,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
#endif
		.depth = 0,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_PLAYBACK |
			 ZORAN_FORMAT_COMPRESSED,
	}
};
static const int zoran_num_formats =
    (sizeof(zoran_formats) / sizeof(struct zoran_format));

// RJ: Test only - want to test BUZ_USE_HIMEM even when CONFIG_BIGPHYS_AREA is defined
#if !defined(CONFIG_BIGPHYS_AREA)
//#undef CONFIG_BIGPHYS_AREA
#define BUZ_USE_HIMEM
#endif

#if defined(CONFIG_BIGPHYS_AREA)
#   include <linux/bigphysarea.h>
#endif

extern int *zr_debug;

#define dprintk(num, format, args...) \
	do { \
		if (*zr_debug >= num) \
			printk(format, ##args);	\
	} while (0)

extern int v4l_nbufs;
extern int v4l_bufsize;
extern int jpg_nbufs;
extern int jpg_bufsize;
extern int pass_through;

static int lock_norm = 0;	/* 1=Don't change TV standard (norm) */
module_param(lock_norm, int, 0);
MODULE_PARM_DESC(lock_norm, "Users can't change norm");

#ifdef HAVE_V4L2
	/* small helper function for calculating buffersizes for v4l2
	 * we calculate the nearest higher power-of-two, which
	 * will be the recommended buffersize */
static __u32
zoran_v4l2_calc_bufsize (struct zoran_jpg_settings *settings)
{
	__u8 div = settings->VerDcm * settings->HorDcm * settings->TmpDcm;
	__u32 num = (1024 * 512) / (div);
	__u32 result = 2;

	num--;
	while (num) {
		num >>= 1;
		result <<= 1;
	}

	if (result > jpg_bufsize)
		return jpg_bufsize;
	if (result < 8192)
		return 8192;
	return result;
}
#endif

/* forward references */
static void v4l_fbuffer_free(struct file *file);
static void jpg_fbuffer_free(struct file *file);

/*
 *   Allocate the V4L grab buffers
 *
 *   These have to be pysically contiguous.
 *   If v4l_bufsize <= MAX_KMALLOC_MEM we use kmalloc
 *   else we try to allocate them with bigphysarea_alloc_pages
 *   if the bigphysarea patch is present in the kernel,
 *   else we try to use high memory (if the user has bootet
 *   Linux with the necessary memory left over).
 */

#if defined(BUZ_USE_HIMEM) && !defined(CONFIG_BIGPHYS_AREA)
static unsigned long
get_high_mem (unsigned long size)
{
/*
 * Check if there is usable memory at the end of Linux memory
 * of at least size. Return the physical address of this memory,
 * return 0 on failure.
 *
 * The idea is from Alexandro Rubini's book "Linux device drivers".
 * The driver from him which is downloadable from O'Reilly's
 * web site misses the "virt_to_phys(high_memory)" part
 * (and therefore doesn't work at all - at least with 2.2.x kernels).
 *
 * It should be unnecessary to mention that THIS IS DANGEROUS,
 * if more than one driver at a time has the idea to use this memory!!!!
 */

	volatile unsigned char __iomem *mem;
	unsigned char c;
	unsigned long hi_mem_ph;
	unsigned long i;

	/* Map the high memory to user space */

	hi_mem_ph = virt_to_phys(high_memory);

	mem = ioremap(hi_mem_ph, size);
	if (!mem) {
		dprintk(1,
			KERN_ERR "%s: get_high_mem() - ioremap failed\n",
			ZORAN_NAME);
		return 0;
	}

	for (i = 0; i < size; i++) {
		/* Check if it is memory */
		c = i & 0xff;
		writeb(c, mem + i);
		if (readb(mem + i) != c)
			break;
		c = 255 - c;
		writeb(c, mem + i);
		if (readb(mem + i) != c)
			break;
		writeb(0, mem + i);	/* zero out memory */

		/* give the kernel air to breath */
		if ((i & 0x3ffff) == 0x3ffff)
			schedule();
	}

	iounmap(mem);

	if (i != size) {
		dprintk(1,
			KERN_ERR
			"%s: get_high_mem() - requested %lu, avail %lu\n",
			ZORAN_NAME, size, i);
		return 0;
	}

	return hi_mem_ph;
}
#endif

static int
v4l_fbuffer_alloc (struct file *file)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i, off;
	unsigned char *mem;
#if defined(BUZ_USE_HIMEM) && !defined(CONFIG_BIGPHYS_AREA)
	unsigned long pmem = 0;
#endif

	/* we might have old buffers lying around... */
	if (fh->v4l_buffers.ready_to_be_freed) {
		v4l_fbuffer_free(file);
	}

	for (i = 0; i < fh->v4l_buffers.num_buffers; i++) {
		if (fh->v4l_buffers.buffer[i].fbuffer)
			dprintk(2,
				KERN_WARNING
				"%s: v4l_fbuffer_alloc() - buffer %d allready allocated!?\n",
				ZR_DEVNAME(zr), i);

		//udelay(20);
		if (fh->v4l_buffers.buffer_size <= MAX_KMALLOC_MEM) {
			/* Use kmalloc */

			mem =
			    (unsigned char *) kmalloc(fh->v4l_buffers.
						      buffer_size,
						      GFP_KERNEL);
			if (mem == 0) {
				dprintk(1,
					KERN_ERR
					"%s: v4l_fbuffer_alloc() - kmalloc for V4L buf %d failed\n",
					ZR_DEVNAME(zr), i);
				v4l_fbuffer_free(file);
				return -ENOBUFS;
			}
			fh->v4l_buffers.buffer[i].fbuffer = mem;
			fh->v4l_buffers.buffer[i].fbuffer_phys =
			    virt_to_phys(mem);
			fh->v4l_buffers.buffer[i].fbuffer_bus =
			    virt_to_bus(mem);
			for (off = 0; off < fh->v4l_buffers.buffer_size;
			     off += PAGE_SIZE)
				SetPageReserved(MAP_NR(mem + off));
			dprintk(4,
				KERN_INFO
				"%s: v4l_fbuffer_alloc() - V4L frame %d mem 0x%lx (bus: 0x%lx)\n",
				ZR_DEVNAME(zr), i, (unsigned long) mem,
				virt_to_bus(mem));
		} else {
#if defined(CONFIG_BIGPHYS_AREA)
			/* Use bigphysarea_alloc_pages */

			int n =
			    (fh->v4l_buffers.buffer_size + PAGE_SIZE -
			     1) / PAGE_SIZE;

			mem =
			    (unsigned char *) bigphysarea_alloc_pages(n, 0,
								      GFP_KERNEL);
			if (mem == 0) {
				dprintk(1,
					KERN_ERR
					"%s: v4l_fbuffer_alloc() - bigphysarea_alloc_pages for V4L buf %d failed\n",
					ZR_DEVNAME(zr), i);
				v4l_fbuffer_free(file);
				return -ENOBUFS;
			}
			fh->v4l_buffers.buffer[i].fbuffer = mem;
			fh->v4l_buffers.buffer[i].fbuffer_phys =
			    virt_to_phys(mem);
			fh->v4l_buffers.buffer[i].fbuffer_bus =
			    virt_to_bus(mem);
			dprintk(4,
				KERN_INFO
				"%s: Bigphysarea frame %d mem 0x%x (bus: 0x%x)\n",
				ZR_DEVNAME(zr), i, (unsigned) mem,
				(unsigned) virt_to_bus(mem));

			/* Zero out the allocated memory */
			memset(fh->v4l_buffers.buffer[i].fbuffer, 0,
			       fh->v4l_buffers.buffer_size);
#elif defined(BUZ_USE_HIMEM)

			/* Use high memory which has been left at boot time */

			/* Ok., Ok. this is an evil hack - we make
			 * the assumption that physical addresses are
			 * the same as bus addresses (true at least
			 * for Intel processors). The whole method of
			 * obtaining and using this memory is not very
			 * nice - but I hope it saves some poor users
			 * from kernel hacking, which might have even
			 * more evil results */

			if (i == 0) {
				int size =
				    fh->v4l_buffers.num_buffers *
				    fh->v4l_buffers.buffer_size;

				pmem = get_high_mem(size);
				if (pmem == 0) {
					dprintk(1,
						KERN_ERR
						"%s: v4l_fbuffer_alloc() - get_high_mem (size = %d KB) for V4L bufs failed\n",
						ZR_DEVNAME(zr), size >> 10);
					return -ENOBUFS;
				}
				fh->v4l_buffers.buffer[0].fbuffer = NULL;
				fh->v4l_buffers.buffer[0].fbuffer_phys = pmem;
				fh->v4l_buffers.buffer[0].fbuffer_bus = pmem;
				dprintk(4,
					KERN_INFO
					"%s: v4l_fbuffer_alloc() - using %d KB high memory\n",
					ZR_DEVNAME(zr), size >> 10);
			} else {
				fh->v4l_buffers.buffer[i].fbuffer = NULL;
				fh->v4l_buffers.buffer[i].fbuffer_phys =
				    pmem + i * fh->v4l_buffers.buffer_size;
				fh->v4l_buffers.buffer[i].fbuffer_bus =
				    pmem + i * fh->v4l_buffers.buffer_size;
			}
#else
			/* No bigphysarea present, usage of high memory disabled,
			 * but user wants buffers of more than MAX_KMALLOC_MEM */
			dprintk(1,
				KERN_ERR
				"%s: v4l_fbuffer_alloc() - no bigphysarea_patch present, usage of high memory disabled,\n",
				ZR_DEVNAME(zr));
			dprintk(1,
				KERN_ERR
				"%s: v4l_fbuffer_alloc() - sorry, could not allocate %d V4L buffers of size %d KB.\n",
				ZR_DEVNAME(zr), fh->v4l_buffers.num_buffers,
				fh->v4l_buffers.buffer_size >> 10);
			return -ENOBUFS;
#endif
		}
	}

	fh->v4l_buffers.allocated = 1;

	return 0;
}

/* free the V4L grab buffers */
static void
v4l_fbuffer_free (struct file *file)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i, off;
	unsigned char *mem;

	dprintk(4, KERN_INFO "%s: v4l_fbuffer_free()\n", ZR_DEVNAME(zr));

	for (i = 0; i < fh->v4l_buffers.num_buffers; i++) {
		if (!fh->v4l_buffers.buffer[i].fbuffer)
			continue;

		if (fh->v4l_buffers.buffer_size <= MAX_KMALLOC_MEM) {
			mem = fh->v4l_buffers.buffer[i].fbuffer;
			for (off = 0; off < fh->v4l_buffers.buffer_size;
			     off += PAGE_SIZE)
				ClearPageReserved(MAP_NR(mem + off));
			kfree((void *) fh->v4l_buffers.buffer[i].fbuffer);
		}
#if defined(CONFIG_BIGPHYS_AREA)
		else
			bigphysarea_free_pages((void *) fh->v4l_buffers.
					       buffer[i].fbuffer);
#endif
		fh->v4l_buffers.buffer[i].fbuffer = NULL;
	}

	fh->v4l_buffers.allocated = 0;
	fh->v4l_buffers.ready_to_be_freed = 0;
}

/*
 *   Allocate the MJPEG grab buffers.
 *
 *   If the requested buffer size is smaller than MAX_KMALLOC_MEM,
 *   kmalloc is used to request a physically contiguous area,
 *   else we allocate the memory in framgents with get_zeroed_page.
 *
 *   If a Natoma chipset is present and this is a revision 1 zr36057,
 *   each MJPEG buffer needs to be physically contiguous.
 *   (RJ: This statement is from Dave Perks' original driver,
 *   I could never check it because I have a zr36067)
 *   The driver cares about this because it reduces the buffer
 *   size to MAX_KMALLOC_MEM in that case (which forces contiguous allocation).
 *
 *   RJ: The contents grab buffers needs never be accessed in the driver.
 *       Therefore there is no need to allocate them with vmalloc in order
 *       to get a contiguous virtual memory space.
 *       I don't understand why many other drivers first allocate them with
 *       vmalloc (which uses internally also get_zeroed_page, but delivers you
 *       virtual addresses) and then again have to make a lot of efforts
 *       to get the physical address.
 *
 *   Ben Capper:
 *       On big-endian architectures (such as ppc) some extra steps
 *       are needed. When reading and writing to the stat_com array
 *       and fragment buffers, the device expects to see little-
 *       endian values. The use of cpu_to_le32() and le32_to_cpu()
 *       in this function (and one or two others in zoran_device.c)
 *       ensure that these values are always stored in little-endian
 *       form, regardless of architecture. The zr36057 does Very Bad
 *       Things on big endian architectures if the stat_com array
 *       and fragment buffers are not little-endian.
 */

static int
jpg_fbuffer_alloc (struct file *file)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i, j, off;
	unsigned long mem;

	/* we might have old buffers lying around */
	if (fh->jpg_buffers.ready_to_be_freed) {
		jpg_fbuffer_free(file);
	}

	for (i = 0; i < fh->jpg_buffers.num_buffers; i++) {
		if (fh->jpg_buffers.buffer[i].frag_tab)
			dprintk(2,
				KERN_WARNING
				"%s: jpg_fbuffer_alloc() - buffer %d allready allocated!?\n",
				ZR_DEVNAME(zr), i);

		/* Allocate fragment table for this buffer */

		mem = get_zeroed_page(GFP_KERNEL);
		if (mem == 0) {
			dprintk(1,
				KERN_ERR
				"%s: jpg_fbuffer_alloc() - get_zeroed_page (frag_tab) failed for buffer %d\n",
				ZR_DEVNAME(zr), i);
			jpg_fbuffer_free(file);
			return -ENOBUFS;
		}
		memset((void *) mem, 0, PAGE_SIZE);
		fh->jpg_buffers.buffer[i].frag_tab = (u32 *) mem;
		fh->jpg_buffers.buffer[i].frag_tab_bus =
		    virt_to_bus((void *) mem);

		//if (alloc_contig) {
		if (fh->jpg_buffers.need_contiguous) {
			mem =
			    (unsigned long) kmalloc(fh->jpg_buffers.
						    buffer_size,
						    GFP_KERNEL);
			if (mem == 0) {
				dprintk(1,
					KERN_ERR
					"%s: jpg_fbuffer_alloc() - kmalloc failed for buffer %d\n",
					ZR_DEVNAME(zr), i);
				jpg_fbuffer_free(file);
				return -ENOBUFS;
			}
			fh->jpg_buffers.buffer[i].frag_tab[0] =
			    cpu_to_le32(virt_to_bus((void *) mem));
			fh->jpg_buffers.buffer[i].frag_tab[1] =
			    cpu_to_le32(((fh->jpg_buffers.buffer_size / 4) << 1) | 1);
			for (off = 0; off < fh->jpg_buffers.buffer_size;
			     off += PAGE_SIZE)
				SetPageReserved(MAP_NR(mem + off));
		} else {
			/* jpg_bufsize is allreay page aligned */
			for (j = 0;
			     j < fh->jpg_buffers.buffer_size / PAGE_SIZE;
			     j++) {
				mem = get_zeroed_page(GFP_KERNEL);
				if (mem == 0) {
					dprintk(1,
						KERN_ERR
						"%s: jpg_fbuffer_alloc() - get_zeroed_page failed for buffer %d\n",
						ZR_DEVNAME(zr), i);
					jpg_fbuffer_free(file);
					return -ENOBUFS;
				}

				fh->jpg_buffers.buffer[i].frag_tab[2 * j] =
				    cpu_to_le32(virt_to_bus((void *) mem));
				fh->jpg_buffers.buffer[i].frag_tab[2 * j +
								   1] =
				    cpu_to_le32((PAGE_SIZE / 4) << 1);
				SetPageReserved(MAP_NR(mem));
			}

			fh->jpg_buffers.buffer[i].frag_tab[2 * j - 1] |= cpu_to_le32(1);
		}
	}

	dprintk(4,
		KERN_DEBUG "%s: jpg_fbuffer_alloc() - %d KB allocated\n",
		ZR_DEVNAME(zr),
		(fh->jpg_buffers.num_buffers *
		 fh->jpg_buffers.buffer_size) >> 10);

	fh->jpg_buffers.allocated = 1;

	return 0;
}

/* free the MJPEG grab buffers */
static void
jpg_fbuffer_free (struct file *file)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i, j, off;
	unsigned char *mem;

	dprintk(4, KERN_DEBUG "%s: jpg_fbuffer_free()\n", ZR_DEVNAME(zr));

	for (i = 0; i < fh->jpg_buffers.num_buffers; i++) {
		if (!fh->jpg_buffers.buffer[i].frag_tab)
			continue;

		//if (alloc_contig) {
		if (fh->jpg_buffers.need_contiguous) {
			if (fh->jpg_buffers.buffer[i].frag_tab[0]) {
				mem = (unsigned char *) bus_to_virt(le32_to_cpu(
					fh->jpg_buffers.buffer[i].frag_tab[0]));
				for (off = 0;
				     off < fh->jpg_buffers.buffer_size;
				     off += PAGE_SIZE)
					ClearPageReserved(MAP_NR
							  (mem + off));
				kfree(mem);
				fh->jpg_buffers.buffer[i].frag_tab[0] = 0;
				fh->jpg_buffers.buffer[i].frag_tab[1] = 0;
			}
		} else {
			for (j = 0;
			     j < fh->jpg_buffers.buffer_size / PAGE_SIZE;
			     j++) {
				if (!fh->jpg_buffers.buffer[i].
				    frag_tab[2 * j])
					break;
				ClearPageReserved(MAP_NR
						  (bus_to_virt
						   (le32_to_cpu
						    (fh->jpg_buffers.
						     buffer[i].frag_tab[2 *
								       j]))));
				free_page((unsigned long)
					  bus_to_virt
					          (le32_to_cpu
						   (fh->jpg_buffers.
						      buffer[i].
						      frag_tab[2 * j])));
				fh->jpg_buffers.buffer[i].frag_tab[2 * j] =
				    0;
				fh->jpg_buffers.buffer[i].frag_tab[2 * j +
								   1] = 0;
			}
		}

		free_page((unsigned long) fh->jpg_buffers.buffer[i].
			  frag_tab);
		fh->jpg_buffers.buffer[i].frag_tab = NULL;
	}

	fh->jpg_buffers.allocated = 0;
	fh->jpg_buffers.ready_to_be_freed = 0;
}

/*
 *   V4L Buffer grabbing
 */

static int
zoran_v4l_set_format (struct file               *file,
		      int                        width,
		      int                        height,
		      const struct zoran_format *format)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int bpp;

	/* Check size and format of the grab wanted */

	if (height < BUZ_MIN_HEIGHT || width < BUZ_MIN_WIDTH ||
	    height > BUZ_MAX_HEIGHT || width > BUZ_MAX_WIDTH) {
		dprintk(1,
			KERN_ERR
			"%s: v4l_set_format() - wrong frame size (%dx%d)\n",
			ZR_DEVNAME(zr), width, height);
		return -EINVAL;
	}

	bpp = (format->depth + 7) / 8;

	/* Check against available buffer size */
	if (height * width * bpp > fh->v4l_buffers.buffer_size) {
		dprintk(1,
			KERN_ERR
			"%s: v4l_set_format() - video buffer size (%d kB) is too small\n",
			ZR_DEVNAME(zr), fh->v4l_buffers.buffer_size >> 10);
		return -EINVAL;
	}

	/* The video front end needs 4-byte alinged line sizes */

	if ((bpp == 2 && (width & 1)) || (bpp == 3 && (width & 3))) {
		dprintk(1,
			KERN_ERR
			"%s: v4l_set_format() - wrong frame alingment\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	fh->v4l_settings.width = width;
	fh->v4l_settings.height = height;
	fh->v4l_settings.format = format;
	fh->v4l_settings.bytesperline = bpp * fh->v4l_settings.width;

	return 0;
}

static int
zoran_v4l_queue_frame (struct file *file,
		       int          num)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int res = 0;

	if (!fh->v4l_buffers.allocated) {
		dprintk(1,
			KERN_ERR
			"%s: v4l_queue_frame() - buffers not yet allocated\n",
			ZR_DEVNAME(zr));
		res = -ENOMEM;
	}

	/* No grabbing outside the buffer range! */
	if (num >= fh->v4l_buffers.num_buffers || num < 0) {
		dprintk(1,
			KERN_ERR
			"%s: v4l_queue_frame() - buffer %d is out of range\n",
			ZR_DEVNAME(zr), num);
		res = -EINVAL;
	}

	spin_lock_irqsave(&zr->spinlock, flags);

	if (fh->v4l_buffers.active == ZORAN_FREE) {
		if (zr->v4l_buffers.active == ZORAN_FREE) {
			zr->v4l_buffers = fh->v4l_buffers;
			fh->v4l_buffers.active = ZORAN_ACTIVE;
		} else {
			dprintk(1,
				KERN_ERR
				"%s: v4l_queue_frame() - another session is already capturing\n",
				ZR_DEVNAME(zr));
			res = -EBUSY;
		}
	}

	/* make sure a grab isn't going on currently with this buffer */
	if (!res) {
		switch (zr->v4l_buffers.buffer[num].state) {
		default:
		case BUZ_STATE_PEND:
			if (zr->v4l_buffers.active == ZORAN_FREE) {
				fh->v4l_buffers.active = ZORAN_FREE;
				zr->v4l_buffers.allocated = 0;
			}
			res = -EBUSY;	/* what are you doing? */
			break;
		case BUZ_STATE_DONE:
			dprintk(2,
				KERN_WARNING
				"%s: v4l_queue_frame() - queueing buffer %d in state DONE!?\n",
				ZR_DEVNAME(zr), num);
		case BUZ_STATE_USER:
			/* since there is at least one unused buffer there's room for at least
			 * one more pend[] entry */
			zr->v4l_pend[zr->v4l_pend_head++ &
					V4L_MASK_FRAME] = num;
			zr->v4l_buffers.buffer[num].state = BUZ_STATE_PEND;
			zr->v4l_buffers.buffer[num].bs.length =
			    fh->v4l_settings.bytesperline *
			    zr->v4l_settings.height;
			fh->v4l_buffers.buffer[num] =
			    zr->v4l_buffers.buffer[num];
			break;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	if (!res && zr->v4l_buffers.active == ZORAN_FREE)
		zr->v4l_buffers.active = fh->v4l_buffers.active;

	return res;
}

static int
v4l_grab (struct file       *file,
	  struct video_mmap *mp)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int res = 0, i;

	for (i = 0; i < zoran_num_formats; i++) {
		if (zoran_formats[i].palette == mp->format &&
		    zoran_formats[i].flags & ZORAN_FORMAT_CAPTURE &&
		    !(zoran_formats[i].flags & ZORAN_FORMAT_COMPRESSED))
			break;
	}
	if (i == zoran_num_formats || zoran_formats[i].depth == 0) {
		dprintk(1,
			KERN_ERR
			"%s: v4l_grab() - wrong bytes-per-pixel format\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	/*
	 * To minimize the time spent in the IRQ routine, we avoid setting up
	 * the video front end there.
	 * If this grab has different parameters from a running streaming capture
	 * we stop the streaming capture and start it over again.
	 */
	if (zr->v4l_memgrab_active &&
	    (zr->v4l_settings.width != mp->width ||
	     zr->v4l_settings.height != mp->height ||
	     zr->v4l_settings.format->palette != mp->format)) {
		res = wait_grab_pending(zr);
		if (res)
			return res;
	}
	if ((res = zoran_v4l_set_format(file,
					mp->width,
					mp->height,
					&zoran_formats[i])))
		return res;
	zr->v4l_settings = fh->v4l_settings;

	/* queue the frame in the pending queue */
	if ((res = zoran_v4l_queue_frame(file, mp->frame))) {
		fh->v4l_buffers.active = ZORAN_FREE;
		return res;
	}

	/* put the 36057 into frame grabbing mode */
	if (!res && !zr->v4l_memgrab_active)
		zr36057_set_memgrab(zr, 1);

	//dprintk(4, KERN_INFO "%s: Frame grab 3...\n", ZR_DEVNAME(zr));

	return res;
}

/*
 * Sync on a V4L buffer
 */

static int
v4l_sync (struct file *file,
	  int          frame)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long flags;

	if (fh->v4l_buffers.active == ZORAN_FREE) {
		dprintk(1,
			KERN_ERR
			"%s: v4l_sync() - no grab active for this session\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	/* check passed-in frame number */
	if (frame >= fh->v4l_buffers.num_buffers || frame < 0) {
		dprintk(1,
			KERN_ERR "%s: v4l_sync() - frame %d is invalid\n",
			ZR_DEVNAME(zr), frame);
		return -EINVAL;
	}

	/* Check if is buffer was queued at all */
	if (zr->v4l_buffers.buffer[frame].state == BUZ_STATE_USER) {
		dprintk(1,
			KERN_ERR
			"%s: v4l_sync() - attempt to sync on a buffer which was not queued?\n",
			ZR_DEVNAME(zr));
		return -EPROTO;
	}

	/* wait on this buffer to get ready */
	if (!wait_event_interruptible_timeout(zr->v4l_capq,
				(zr->v4l_buffers.buffer[frame].state != BUZ_STATE_PEND),
				10*HZ))
		return -ETIME;
	if (signal_pending(current))
		return -ERESTARTSYS;

	/* buffer should now be in BUZ_STATE_DONE */
	if (zr->v4l_buffers.buffer[frame].state != BUZ_STATE_DONE)
		dprintk(2,
			KERN_ERR "%s: v4l_sync() - internal state error\n",
			ZR_DEVNAME(zr));

	zr->v4l_buffers.buffer[frame].state = BUZ_STATE_USER;
	fh->v4l_buffers.buffer[frame] = zr->v4l_buffers.buffer[frame];

	spin_lock_irqsave(&zr->spinlock, flags);

	/* Check if streaming capture has finished */
	if (zr->v4l_pend_tail == zr->v4l_pend_head) {
		zr36057_set_memgrab(zr, 0);
		if (zr->v4l_buffers.active == ZORAN_ACTIVE) {
			fh->v4l_buffers.active = zr->v4l_buffers.active =
			    ZORAN_FREE;
			zr->v4l_buffers.allocated = 0;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	return 0;
}

/*
 *   Queue a MJPEG buffer for capture/playback
 */

static int
zoran_jpg_queue_frame (struct file          *file,
		       int                   num,
		       enum zoran_codec_mode mode)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int res = 0;

	/* Check if buffers are allocated */
	if (!fh->jpg_buffers.allocated) {
		dprintk(1,
			KERN_ERR
			"%s: jpg_queue_frame() - buffers not yet allocated\n",
			ZR_DEVNAME(zr));
		return -ENOMEM;
	}

	/* No grabbing outside the buffer range! */
	if (num >= fh->jpg_buffers.num_buffers || num < 0) {
		dprintk(1,
			KERN_ERR
			"%s: jpg_queue_frame() - buffer %d out of range\n",
			ZR_DEVNAME(zr), num);
		return -EINVAL;
	}

	/* what is the codec mode right now? */
	if (zr->codec_mode == BUZ_MODE_IDLE) {
		zr->jpg_settings = fh->jpg_settings;
	} else if (zr->codec_mode != mode) {
		/* wrong codec mode active - invalid */
		dprintk(1,
			KERN_ERR
			"%s: jpg_queue_frame() - codec in wrong mode\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	spin_lock_irqsave(&zr->spinlock, flags);

	if (fh->jpg_buffers.active == ZORAN_FREE) {
		if (zr->jpg_buffers.active == ZORAN_FREE) {
			zr->jpg_buffers = fh->jpg_buffers;
			fh->jpg_buffers.active = ZORAN_ACTIVE;
		} else {
			dprintk(1,
				KERN_ERR
				"%s: jpg_queue_frame() - another session is already capturing\n",
				ZR_DEVNAME(zr));
			res = -EBUSY;
		}
	}

	if (!res && zr->codec_mode == BUZ_MODE_IDLE) {
		/* Ok load up the jpeg codec */
		zr36057_enable_jpg(zr, mode);
	}

	if (!res) {
		switch (zr->jpg_buffers.buffer[num].state) {
		case BUZ_STATE_DONE:
			dprintk(2,
				KERN_WARNING
				"%s: jpg_queue_frame() - queing frame in BUZ_STATE_DONE state!?\n",
				ZR_DEVNAME(zr));
		case BUZ_STATE_USER:
			/* since there is at least one unused buffer there's room for at
			 *least one more pend[] entry */
			zr->jpg_pend[zr->jpg_que_head++ & BUZ_MASK_FRAME] =
			    num;
			zr->jpg_buffers.buffer[num].state = BUZ_STATE_PEND;
			fh->jpg_buffers.buffer[num] =
			    zr->jpg_buffers.buffer[num];
			zoran_feed_stat_com(zr);
			break;
		default:
		case BUZ_STATE_DMA:
		case BUZ_STATE_PEND:
			if (zr->jpg_buffers.active == ZORAN_FREE) {
				fh->jpg_buffers.active = ZORAN_FREE;
				zr->jpg_buffers.allocated = 0;
			}
			res = -EBUSY;	/* what are you doing? */
			break;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	if (!res && zr->jpg_buffers.active == ZORAN_FREE) {
		zr->jpg_buffers.active = fh->jpg_buffers.active;
	}

	return res;
}

static int
jpg_qbuf (struct file          *file,
	  int                   frame,
	  enum zoran_codec_mode mode)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int res = 0;

	/* Does the user want to stop streaming? */
	if (frame < 0) {
		if (zr->codec_mode == mode) {
			if (fh->jpg_buffers.active == ZORAN_FREE) {
				dprintk(1,
					KERN_ERR
					"%s: jpg_qbuf(-1) - session not active\n",
					ZR_DEVNAME(zr));
				return -EINVAL;
			}
			fh->jpg_buffers.active = zr->jpg_buffers.active =
			    ZORAN_FREE;
			zr->jpg_buffers.allocated = 0;
			zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
			return 0;
		} else {
			dprintk(1,
				KERN_ERR
				"%s: jpg_qbuf() - stop streaming but not in streaming mode\n",
				ZR_DEVNAME(zr));
			return -EINVAL;
		}
	}

	if ((res = zoran_jpg_queue_frame(file, frame, mode)))
		return res;

	/* Start the jpeg codec when the first frame is queued  */
	if (!res && zr->jpg_que_head == 1)
		jpeg_start(zr);

	return res;
}

/*
 *   Sync on a MJPEG buffer
 */

static int
jpg_sync (struct file       *file,
	  struct zoran_sync *bs)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int frame;

	if (fh->jpg_buffers.active == ZORAN_FREE) {
		dprintk(1,
			KERN_ERR
			"%s: jpg_sync() - capture is not currently active\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}
	if (zr->codec_mode != BUZ_MODE_MOTION_DECOMPRESS &&
	    zr->codec_mode != BUZ_MODE_MOTION_COMPRESS) {
		dprintk(1,
			KERN_ERR
			"%s: jpg_sync() - codec not in streaming mode\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}
	if (!wait_event_interruptible_timeout(zr->jpg_capq,
			(zr->jpg_que_tail != zr->jpg_dma_tail ||
			 zr->jpg_dma_tail == zr->jpg_dma_head),
			10*HZ)) {
		int isr;

		btand(~ZR36057_JMC_Go_en, ZR36057_JMC);
		udelay(1);
		zr->codec->control(zr->codec, CODEC_G_STATUS,
					   sizeof(isr), &isr);
		dprintk(1,
			KERN_ERR
			"%s: jpg_sync() - timeout: codec isr=0x%02x\n",
			ZR_DEVNAME(zr), isr);

		return -ETIME;

	}
	if (signal_pending(current))
		return -ERESTARTSYS;

	spin_lock_irqsave(&zr->spinlock, flags);

	if (zr->jpg_dma_tail != zr->jpg_dma_head)
		frame = zr->jpg_pend[zr->jpg_que_tail++ & BUZ_MASK_FRAME];
	else
		frame = zr->jpg_pend[zr->jpg_que_tail & BUZ_MASK_FRAME];

	/* buffer should now be in BUZ_STATE_DONE */
	if (*zr_debug > 0)
		if (zr->jpg_buffers.buffer[frame].state != BUZ_STATE_DONE)
			dprintk(2,
				KERN_ERR
				"%s: jpg_sync() - internal state error\n",
				ZR_DEVNAME(zr));

	*bs = zr->jpg_buffers.buffer[frame].bs;
	bs->frame = frame;
	zr->jpg_buffers.buffer[frame].state = BUZ_STATE_USER;
	fh->jpg_buffers.buffer[frame] = zr->jpg_buffers.buffer[frame];

	spin_unlock_irqrestore(&zr->spinlock, flags);

	return 0;
}

static void
zoran_open_init_session (struct file *file)
{
	int i;
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	/* Per default, map the V4L Buffers */
	fh->map_mode = ZORAN_MAP_MODE_RAW;

	/* take over the card's current settings */
	fh->overlay_settings = zr->overlay_settings;
	fh->overlay_settings.is_set = 0;
	fh->overlay_settings.format = zr->overlay_settings.format;
	fh->overlay_active = ZORAN_FREE;

	/* v4l settings */
	fh->v4l_settings = zr->v4l_settings;

	/* v4l_buffers */
	memset(&fh->v4l_buffers, 0, sizeof(struct zoran_v4l_struct));
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		fh->v4l_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
		fh->v4l_buffers.buffer[i].bs.frame = i;
	}
	fh->v4l_buffers.allocated = 0;
	fh->v4l_buffers.ready_to_be_freed = 0;
	fh->v4l_buffers.active = ZORAN_FREE;
	fh->v4l_buffers.buffer_size = v4l_bufsize;
	fh->v4l_buffers.num_buffers = v4l_nbufs;

	/* jpg settings */
	fh->jpg_settings = zr->jpg_settings;

	/* jpg_buffers */
	memset(&fh->jpg_buffers, 0, sizeof(struct zoran_jpg_struct));
	for (i = 0; i < BUZ_MAX_FRAME; i++) {
		fh->jpg_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
		fh->jpg_buffers.buffer[i].bs.frame = i;
	}
	fh->jpg_buffers.need_contiguous = zr->jpg_buffers.need_contiguous;
	fh->jpg_buffers.allocated = 0;
	fh->jpg_buffers.ready_to_be_freed = 0;
	fh->jpg_buffers.active = ZORAN_FREE;
	fh->jpg_buffers.buffer_size = jpg_bufsize;
	fh->jpg_buffers.num_buffers = jpg_nbufs;
}

static void
zoran_close_end_session (struct file *file)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	/* overlay */
	if (fh->overlay_active != ZORAN_FREE) {
		fh->overlay_active = zr->overlay_active = ZORAN_FREE;
		zr->v4l_overlay_active = 0;
		if (!zr->v4l_memgrab_active)
			zr36057_overlay(zr, 0);
		zr->overlay_mask = NULL;
	}

	/* v4l capture */
	if (fh->v4l_buffers.active != ZORAN_FREE) {
		zr36057_set_memgrab(zr, 0);
		zr->v4l_buffers.allocated = 0;
		zr->v4l_buffers.active = fh->v4l_buffers.active =
		    ZORAN_FREE;
	}

	/* v4l buffers */
	if (fh->v4l_buffers.allocated ||
	    fh->v4l_buffers.ready_to_be_freed) {
		v4l_fbuffer_free(file);
	}

	/* jpg capture */
	if (fh->jpg_buffers.active != ZORAN_FREE) {
		zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
		zr->jpg_buffers.allocated = 0;
		zr->jpg_buffers.active = fh->jpg_buffers.active =
		    ZORAN_FREE;
	}

	/* jpg buffers */
	if (fh->jpg_buffers.allocated ||
	    fh->jpg_buffers.ready_to_be_freed) {
		jpg_fbuffer_free(file);
	}
}

/*
 *   Open a zoran card. Right now the flags stuff is just playing
 */

static int
zoran_open (struct inode *inode,
	    struct file  *file)
{
	unsigned int minor = iminor(inode);
	struct zoran *zr = NULL;
	struct zoran_fh *fh;
	int i, res, first_open = 0, have_module_locks = 0;

	/* find the device */
	for (i = 0; i < zoran_num; i++) {
		if (zoran[i].video_dev->minor == minor) {
			zr = &zoran[i];
			break;
		}
	}

	if (!zr) {
		dprintk(1, KERN_ERR "%s: device not found!\n", ZORAN_NAME);
		res = -ENODEV;
		goto open_unlock_and_return;
	}

	/* see fs/device.c - the kernel already locks during open(),
	 * so locking ourselves only causes deadlocks */
	/*down(&zr->resource_lock);*/

	if (!zr->decoder) {
		dprintk(1,
			KERN_ERR "%s: no TV decoder loaded for device!\n",
			ZR_DEVNAME(zr));
		res = -EIO;
		goto open_unlock_and_return;
	}

	/* try to grab a module lock */
	if (!try_module_get(THIS_MODULE)) {
		dprintk(1,
			KERN_ERR
			"%s: failed to acquire my own lock! PANIC!\n",
			ZR_DEVNAME(zr));
		res = -ENODEV;
		goto open_unlock_and_return;
	}
	if (!try_module_get(zr->decoder->driver->owner)) {
		dprintk(1,
			KERN_ERR
			"%s: failed to grab ownership of i2c decoder\n",
			ZR_DEVNAME(zr));
		res = -EIO;
		module_put(THIS_MODULE);
		goto open_unlock_and_return;
	}
	if (zr->encoder &&
	    !try_module_get(zr->encoder->driver->owner)) {
		dprintk(1,
			KERN_ERR
			"%s: failed to grab ownership of i2c encoder\n",
			ZR_DEVNAME(zr));
		res = -EIO;
		module_put(zr->decoder->driver->owner);
		module_put(THIS_MODULE);
		goto open_unlock_and_return;
	}

	have_module_locks = 1;

	if (zr->user >= 2048) {
		dprintk(1, KERN_ERR "%s: too many users (%d) on device\n",
			ZR_DEVNAME(zr), zr->user);
		res = -EBUSY;
		goto open_unlock_and_return;
	}

	dprintk(1, KERN_INFO "%s: zoran_open(%s, pid=[%d]), users(-)=%d\n",
		ZR_DEVNAME(zr), current->comm, current->pid, zr->user);

	/* now, create the open()-specific file_ops struct */
	fh = kmalloc(sizeof(struct zoran_fh), GFP_KERNEL);
	if (!fh) {
		dprintk(1,
			KERN_ERR
			"%s: zoran_open() - allocation of zoran_fh failed\n",
			ZR_DEVNAME(zr));
		res = -ENOMEM;
		goto open_unlock_and_return;
	}
	memset(fh, 0, sizeof(struct zoran_fh));
	/* used to be BUZ_MAX_WIDTH/HEIGHT, but that gives overflows
	 * on norm-change! */
	fh->overlay_mask =
	    kmalloc(((768 + 31) / 32) * 576 * 4, GFP_KERNEL);
	if (!fh->overlay_mask) {
		dprintk(1,
			KERN_ERR
			"%s: zoran_open() - allocation of overlay_mask failed\n",
			ZR_DEVNAME(zr));
		kfree(fh);
		res = -ENOMEM;
		goto open_unlock_and_return;
	}

	if (zr->user++ == 0)
		first_open = 1;

	/*up(&zr->resource_lock);*/

	/* default setup - TODO: look at flags */
	if (first_open) {	/* First device open */
		zr36057_restart(zr);
		zoran_open_init_params(zr);
		zoran_init_hardware(zr);

		btor(ZR36057_ICR_IntPinEn, ZR36057_ICR);
	}

	/* set file_ops stuff */
	file->private_data = fh;
	fh->zr = zr;
	zoran_open_init_session(file);

	return 0;

open_unlock_and_return:
	/* if we grabbed locks, release them accordingly */
	if (have_module_locks) {
		module_put(zr->decoder->driver->owner);
		if (zr->encoder) {
			module_put(zr->encoder->driver->owner);
		}
		module_put(THIS_MODULE);
	}

	/* if there's no device found, we didn't obtain the lock either */
	if (zr) {
		/*up(&zr->resource_lock);*/
	}

	return res;
}

static int
zoran_close (struct inode *inode,
	     struct file  *file)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	dprintk(1, KERN_INFO "%s: zoran_close(%s, pid=[%d]), users(+)=%d\n",
		ZR_DEVNAME(zr), current->comm, current->pid, zr->user);

	/* kernel locks (fs/device.c), so don't do that ourselves
	 * (prevents deadlocks) */
	/*down(&zr->resource_lock);*/

	zoran_close_end_session(file);

	if (zr->user-- == 1) {	/* Last process */
		/* Clean up JPEG process */
		wake_up_interruptible(&zr->jpg_capq);
		zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
		zr->jpg_buffers.allocated = 0;
		zr->jpg_buffers.active = ZORAN_FREE;

		/* disable interrupts */
		btand(~ZR36057_ICR_IntPinEn, ZR36057_ICR);

		if (*zr_debug > 1)
			print_interrupts(zr);

		/* Overlay off */
		zr->v4l_overlay_active = 0;
		zr36057_overlay(zr, 0);
		zr->overlay_mask = NULL;

		/* capture off */
		wake_up_interruptible(&zr->v4l_capq);
		zr36057_set_memgrab(zr, 0);
		zr->v4l_buffers.allocated = 0;
		zr->v4l_buffers.active = ZORAN_FREE;
		zoran_set_pci_master(zr, 0);

		if (!pass_through) {	/* Switch to color bar */
			int zero = 0, two = 2;
			decoder_command(zr, DECODER_ENABLE_OUTPUT, &zero);
			encoder_command(zr, ENCODER_SET_INPUT, &two);
		}
	}

	file->private_data = NULL;
	kfree(fh->overlay_mask);
	kfree(fh);

	/* release locks on the i2c modules */
	module_put(zr->decoder->driver->owner);
	if (zr->encoder) {
		 module_put(zr->encoder->driver->owner);
	}
	module_put(THIS_MODULE);

	/*up(&zr->resource_lock);*/

	dprintk(4, KERN_INFO "%s: zoran_close() done\n", ZR_DEVNAME(zr));

	return 0;
}


static ssize_t
zoran_read (struct file *file,
	    char        __user *data,
	    size_t       count,
	    loff_t      *ppos)
{
	/* we simply don't support read() (yet)... */

	return -EINVAL;
}

static ssize_t
zoran_write (struct file *file,
	     const char  __user *data,
	     size_t       count,
	     loff_t      *ppos)
{
	/* ...and the same goes for write() */

	return -EINVAL;
}

static int
setup_fbuffer (struct file               *file,
	       void                      *base,
	       const struct zoran_format *fmt,
	       int                        width,
	       int                        height,
	       int                        bytesperline)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	/* (Ronald) v4l/v4l2 guidelines */
	if (!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/* we need a bytesperline value, even if not given */
	if (!bytesperline)
		bytesperline = width * ((fmt->depth + 7) & ~7) / 8;

#if 0
	if (zr->overlay_active) {
		/* dzjee... stupid users... don't even bother to turn off
		 * overlay before changing the memory location...
		 * normally, we would return errors here. However, one of
		 * the tools that does this is... xawtv! and since xawtv
		 * is used by +/- 99% of the users, we'd rather be user-
		 * friendly and silently do as if nothing went wrong */
		dprintk(3,
			KERN_ERR
			"%s: setup_fbuffer() - forced overlay turnoff because framebuffer changed\n",
			ZR_DEVNAME(zr));
		zr36057_overlay(zr, 0);
	}
#endif

	if (!(fmt->flags & ZORAN_FORMAT_OVERLAY)) {
		dprintk(1,
			KERN_ERR
			"%s: setup_fbuffer() - no valid overlay format given\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}
	if (height <= 0 || width <= 0 || bytesperline <= 0) {
		dprintk(1,
			KERN_ERR
			"%s: setup_fbuffer() - invalid height/width/bpl value (%d|%d|%d)\n",
			ZR_DEVNAME(zr), width, height, bytesperline);
		return -EINVAL;
	}
	if (bytesperline & 3) {
		dprintk(1,
			KERN_ERR
			"%s: setup_fbuffer() - bytesperline (%d) must be 4-byte aligned\n",
			ZR_DEVNAME(zr), bytesperline);
		return -EINVAL;
	}

	zr->buffer.base = (void *) ((unsigned long) base & ~3);
	zr->buffer.height = height;
	zr->buffer.width = width;
	zr->buffer.depth = fmt->depth;
	zr->overlay_settings.format = fmt;
	zr->buffer.bytesperline = bytesperline;

	/* The user should set new window parameters */
	zr->overlay_settings.is_set = 0;

	return 0;
}


static int
setup_window (struct file       *file,
	      int                x,
	      int                y,
	      int                width,
	      int                height,
	      struct video_clip __user *clips,
	      int                clipcount,
	      void              __user *bitmap)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	struct video_clip *vcp = NULL;
	int on, end;


	if (!zr->buffer.base) {
		dprintk(1,
			KERN_ERR
			"%s: setup_window() - frame buffer has to be set first\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	if (!fh->overlay_settings.format) {
		dprintk(1,
			KERN_ERR
			"%s: setup_window() - no overlay format set\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	/*
	 * The video front end needs 4-byte alinged line sizes, we correct that
	 * silently here if necessary
	 */
	if (zr->buffer.depth == 15 || zr->buffer.depth == 16) {
		end = (x + width) & ~1;	/* round down */
		x = (x + 1) & ~1;	/* round up */
		width = end - x;
	}

	if (zr->buffer.depth == 24) {
		end = (x + width) & ~3;	/* round down */
		x = (x + 3) & ~3;	/* round up */
		width = end - x;
	}

	if (width > BUZ_MAX_WIDTH)
		width = BUZ_MAX_WIDTH;
	if (height > BUZ_MAX_HEIGHT)
		height = BUZ_MAX_HEIGHT;

	/* Check for vaild parameters */
	if (width < BUZ_MIN_WIDTH || height < BUZ_MIN_HEIGHT ||
	    width > BUZ_MAX_WIDTH || height > BUZ_MAX_HEIGHT) {
		dprintk(1,
			KERN_ERR
			"%s: setup_window() - width = %d or height = %d invalid\n",
			ZR_DEVNAME(zr), width, height);
		return -EINVAL;
	}

	fh->overlay_settings.x = x;
	fh->overlay_settings.y = y;
	fh->overlay_settings.width = width;
	fh->overlay_settings.height = height;
	fh->overlay_settings.clipcount = clipcount;

	/*
	 * If an overlay is running, we have to switch it off
	 * and switch it on again in order to get the new settings in effect.
	 *
	 * We also want to avoid that the overlay mask is written
	 * when an overlay is running.
	 */

	on = zr->v4l_overlay_active && !zr->v4l_memgrab_active &&
	    zr->overlay_active != ZORAN_FREE &&
	    fh->overlay_active != ZORAN_FREE;
	if (on)
		zr36057_overlay(zr, 0);

	/*
	 *   Write the overlay mask if clips are wanted.
	 *   We prefer a bitmap.
	 */
	if (bitmap) {
		/* fake value - it just means we want clips */
		fh->overlay_settings.clipcount = 1;

		if (copy_from_user(fh->overlay_mask, bitmap,
				   (width * height + 7) / 8)) {
			return -EFAULT;
		}
	} else if (clipcount > 0) {
		/* write our own bitmap from the clips */
		vcp = vmalloc(sizeof(struct video_clip) * (clipcount + 4));
		if (vcp == NULL) {
			dprintk(1,
				KERN_ERR
				"%s: setup_window() - Alloc of clip mask failed\n",
				ZR_DEVNAME(zr));
			return -ENOMEM;
		}
		if (copy_from_user
		    (vcp, clips, sizeof(struct video_clip) * clipcount)) {
			vfree(vcp);
			return -EFAULT;
		}
		write_overlay_mask(file, vcp, clipcount);
		vfree(vcp);
	}

	fh->overlay_settings.is_set = 1;
	if (fh->overlay_active != ZORAN_FREE &&
	    zr->overlay_active != ZORAN_FREE)
		zr->overlay_settings = fh->overlay_settings;

	if (on)
		zr36057_overlay(zr, 1);

	/* Make sure the changes come into effect */
	return wait_grab_pending(zr);
}

static int
setup_overlay (struct file *file,
	       int          on)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	/* If there is nothing to do, return immediatly */
	if ((on && fh->overlay_active != ZORAN_FREE) ||
	    (!on && fh->overlay_active == ZORAN_FREE))
		return 0;

	/* check whether we're touching someone else's overlay */
	if (on && zr->overlay_active != ZORAN_FREE &&
	    fh->overlay_active == ZORAN_FREE) {
		dprintk(1,
			KERN_ERR
			"%s: setup_overlay() - overlay is already active for another session\n",
			ZR_DEVNAME(zr));
		return -EBUSY;
	}
	if (!on && zr->overlay_active != ZORAN_FREE &&
	    fh->overlay_active == ZORAN_FREE) {
		dprintk(1,
			KERN_ERR
			"%s: setup_overlay() - you cannot cancel someone else's session\n",
			ZR_DEVNAME(zr));
		return -EPERM;
	}

	if (on == 0) {
		zr->overlay_active = fh->overlay_active = ZORAN_FREE;
		zr->v4l_overlay_active = 0;
		/* When a grab is running, the video simply
		 * won't be switched on any more */
		if (!zr->v4l_memgrab_active)
			zr36057_overlay(zr, 0);
		zr->overlay_mask = NULL;
	} else {
		if (!zr->buffer.base || !fh->overlay_settings.is_set) {
			dprintk(1,
				KERN_ERR
				"%s: setup_overlay() - buffer or window not set\n",
				ZR_DEVNAME(zr));
			return -EINVAL;
		}
		if (!fh->overlay_settings.format) {
			dprintk(1,
				KERN_ERR
				"%s: setup_overlay() - no overlay format set\n",
				ZR_DEVNAME(zr));
			return -EINVAL;
		}
		zr->overlay_active = fh->overlay_active = ZORAN_LOCKED;
		zr->v4l_overlay_active = 1;
		zr->overlay_mask = fh->overlay_mask;
		zr->overlay_settings = fh->overlay_settings;
		if (!zr->v4l_memgrab_active)
			zr36057_overlay(zr, 1);
		/* When a grab is running, the video will be
		 * switched on when grab is finished */
	}

	/* Make sure the changes come into effect */
	return wait_grab_pending(zr);
}

#ifdef HAVE_V4L2
	/* get the status of a buffer in the clients buffer queue */
static int
zoran_v4l2_buffer_status (struct file        *file,
			  struct v4l2_buffer *buf,
			  int                 num)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	buf->flags = V4L2_BUF_FLAG_MAPPED;

	switch (fh->map_mode) {
	case ZORAN_MAP_MODE_RAW:

		/* check range */
		if (num < 0 || num >= fh->v4l_buffers.num_buffers ||
		    !fh->v4l_buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: v4l2_buffer_status() - wrong number or buffers not allocated\n",
				ZR_DEVNAME(zr));
			return -EINVAL;
		}

		buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf->length = fh->v4l_buffers.buffer_size;

		/* get buffer */
		buf->bytesused = fh->v4l_buffers.buffer[num].bs.length;
		if (fh->v4l_buffers.buffer[num].state == BUZ_STATE_DONE ||
		    fh->v4l_buffers.buffer[num].state == BUZ_STATE_USER) {
			buf->sequence = fh->v4l_buffers.buffer[num].bs.seq;
			buf->flags |= V4L2_BUF_FLAG_DONE;
			buf->timestamp =
			    fh->v4l_buffers.buffer[num].bs.timestamp;
		} else {
			buf->flags |= V4L2_BUF_FLAG_QUEUED;
		}

		if (fh->v4l_settings.height <= BUZ_MAX_HEIGHT / 2)
			buf->field = V4L2_FIELD_TOP;
		else
			buf->field = V4L2_FIELD_INTERLACED;

		break;

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:

		/* check range */
		if (num < 0 || num >= fh->jpg_buffers.num_buffers ||
		    !fh->jpg_buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: v4l2_buffer_status() - wrong number or buffers not allocated\n",
				ZR_DEVNAME(zr));
			return -EINVAL;
		}

		buf->type = (fh->map_mode == ZORAN_MAP_MODE_JPG_REC) ?
			      V4L2_BUF_TYPE_VIDEO_CAPTURE :
			      V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf->length = fh->jpg_buffers.buffer_size;

		/* these variables are only written after frame has been captured */
		if (fh->jpg_buffers.buffer[num].state == BUZ_STATE_DONE ||
		    fh->jpg_buffers.buffer[num].state == BUZ_STATE_USER) {
			buf->sequence = fh->jpg_buffers.buffer[num].bs.seq;
			buf->timestamp =
			    fh->jpg_buffers.buffer[num].bs.timestamp;
			buf->bytesused =
			    fh->jpg_buffers.buffer[num].bs.length;
			buf->flags |= V4L2_BUF_FLAG_DONE;
		} else {
			buf->flags |= V4L2_BUF_FLAG_QUEUED;
		}

		/* which fields are these? */
		if (fh->jpg_settings.TmpDcm != 1)
			buf->field =
			    fh->jpg_settings.
			    odd_even ? V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM;
		else
			buf->field =
			    fh->jpg_settings.
			    odd_even ? V4L2_FIELD_SEQ_TB :
			    V4L2_FIELD_SEQ_BT;

		break;

	default:

		dprintk(5,
			KERN_ERR
			"%s: v4l2_buffer_status() - invalid buffer type|map_mode (%d|%d)\n",
			ZR_DEVNAME(zr), buf->type, fh->map_mode);
		return -EINVAL;
	}

	buf->memory = V4L2_MEMORY_MMAP;
	buf->index = num;
	buf->m.offset = buf->length * num;

	return 0;
}
#endif

static int
zoran_set_norm (struct zoran *zr,
	        int           norm) /* VIDEO_MODE_* */
{
	int norm_encoder, on;

	if (zr->v4l_buffers.active != ZORAN_FREE ||
	    zr->jpg_buffers.active != ZORAN_FREE) {
		dprintk(1,
			KERN_WARNING
			"%s: set_norm() called while in playback/capture mode\n",
			ZR_DEVNAME(zr));
		return -EBUSY;
	}

	if (lock_norm && norm != zr->norm) {
		if (lock_norm > 1) {
			dprintk(1,
				KERN_WARNING
				"%s: set_norm() - TV standard is locked, can not switch norm\n",
				ZR_DEVNAME(zr));
			return -EPERM;
		} else {
			dprintk(1,
				KERN_WARNING
				"%s: set_norm() - TV standard is locked, norm was not changed\n",
				ZR_DEVNAME(zr));
			norm = zr->norm;
		}
	}

	if (norm != VIDEO_MODE_AUTO &&
	    (norm < 0 || norm >= zr->card.norms ||
	     !zr->card.tvn[norm])) {
		dprintk(1,
			KERN_ERR "%s: set_norm() - unsupported norm %d\n",
			ZR_DEVNAME(zr), norm);
		return -EINVAL;
	}

	if (norm == VIDEO_MODE_AUTO) {
		int status;

		/* if we have autodetect, ... */
		struct video_decoder_capability caps;
		decoder_command(zr, DECODER_GET_CAPABILITIES, &caps);
		if (!(caps.flags & VIDEO_DECODER_AUTO)) {
			dprintk(1, KERN_ERR "%s: norm=auto unsupported\n",
				ZR_DEVNAME(zr));
			return -EINVAL;
		}

		decoder_command(zr, DECODER_SET_NORM, &norm);

		/* let changes come into effect */
		ssleep(2);

		decoder_command(zr, DECODER_GET_STATUS, &status);
		if (!(status & DECODER_STATUS_GOOD)) {
			dprintk(1,
				KERN_ERR
				"%s: set_norm() - no norm detected\n",
				ZR_DEVNAME(zr));
			/* reset norm */
			decoder_command(zr, DECODER_SET_NORM, &zr->norm);
			return -EIO;
		}

		if (status & DECODER_STATUS_NTSC)
			norm = VIDEO_MODE_NTSC;
		else if (status & DECODER_STATUS_SECAM)
			norm = VIDEO_MODE_SECAM;
		else
			norm = VIDEO_MODE_PAL;
	}
	zr->timing = zr->card.tvn[norm];
	norm_encoder = norm;

	/* We switch overlay off and on since a change in the
	 * norm needs different VFE settings */
	on = zr->overlay_active && !zr->v4l_memgrab_active;
	if (on)
		zr36057_overlay(zr, 0);

	decoder_command(zr, DECODER_SET_NORM, &norm);
	encoder_command(zr, ENCODER_SET_NORM, &norm_encoder);

	if (on)
		zr36057_overlay(zr, 1);

	/* Make sure the changes come into effect */
	zr->norm = norm;

	return 0;
}

static int
zoran_set_input (struct zoran *zr,
		 int           input)
{
	int realinput;

	if (input == zr->input) {
		return 0;
	}

	if (zr->v4l_buffers.active != ZORAN_FREE ||
	    zr->jpg_buffers.active != ZORAN_FREE) {
		dprintk(1,
			KERN_WARNING
			"%s: set_input() called while in playback/capture mode\n",
			ZR_DEVNAME(zr));
		return -EBUSY;
	}

	if (input < 0 || input >= zr->card.inputs) {
		dprintk(1,
			KERN_ERR
			"%s: set_input() - unnsupported input %d\n",
			ZR_DEVNAME(zr), input);
		return -EINVAL;
	}

	realinput = zr->card.input[input].muxsel;
	zr->input = input;

	decoder_command(zr, DECODER_SET_INPUT, &realinput);

	return 0;
}

/*
 *   ioctl routine
 */

static int
zoran_do_ioctl (struct inode *inode,
	        struct file  *file,
	        unsigned int  cmd,
	        void         *arg)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	/* CAREFUL: used in multiple places here */
	struct zoran_jpg_settings settings;

	/* we might have older buffers lying around... We don't want
	 * to wait, but we do want to try cleaning them up ASAP. So
	 * we try to obtain the lock and free them. If that fails, we
	 * don't do anything and wait for the next turn. In the end,
	 * zoran_close() or a new allocation will still free them...
	 * This is just a 'the sooner the better' extra 'feature'
	 *
	 * We don't free the buffers right on munmap() because that
	 * causes oopses (kfree() inside munmap() oopses for no
	 * apparent reason - it's also not reproduceable in any way,
	 * but moving the free code outside the munmap() handler fixes
	 * all this... If someone knows why, please explain me (Ronald)
	 */
	if (!down_trylock(&zr->resource_lock)) {
		/* we obtained it! Let's try to free some things */
		if (fh->jpg_buffers.ready_to_be_freed)
			jpg_fbuffer_free(file);
		if (fh->v4l_buffers.ready_to_be_freed)
			v4l_fbuffer_free(file);

		up(&zr->resource_lock);
	}

	switch (cmd) {

	case VIDIOCGCAP:
	{
		struct video_capability *vcap = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOCGCAP\n", ZR_DEVNAME(zr));

		memset(vcap, 0, sizeof(struct video_capability));
		strncpy(vcap->name, ZR_DEVNAME(zr), sizeof(vcap->name));
		vcap->type = ZORAN_VID_TYPE;

		vcap->channels = zr->card.inputs;
		vcap->audios = 0;
		down(&zr->resource_lock);
		vcap->maxwidth = BUZ_MAX_WIDTH;
		vcap->maxheight = BUZ_MAX_HEIGHT;
		vcap->minwidth = BUZ_MIN_WIDTH;
		vcap->minheight = BUZ_MIN_HEIGHT;
		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOCGCHAN:
	{
		struct video_channel *vchan = arg;
		int channel = vchan->channel;

		dprintk(3, KERN_DEBUG "%s: VIDIOCGCHAN - channel=%d\n",
			ZR_DEVNAME(zr), vchan->channel);

		memset(vchan, 0, sizeof(struct video_channel));
		if (channel > zr->card.inputs || channel < 0) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOCGCHAN on not existing channel %d\n",
				ZR_DEVNAME(zr), channel);
			return -EINVAL;
		}

		strcpy(vchan->name, zr->card.input[channel].name);

		vchan->tuners = 0;
		vchan->flags = 0;
		vchan->type = VIDEO_TYPE_CAMERA;
		down(&zr->resource_lock);
		vchan->norm = zr->norm;
		up(&zr->resource_lock);
		vchan->channel = channel;

		return 0;
	}
		break;

		/* RJ: the documentation at http://roadrunner.swansea.linux.org.uk/v4lapi.shtml says:
		 * 
		 * * "The VIDIOCSCHAN ioctl takes an integer argument and switches the capture to this input."
		 * *                                 ^^^^^^^
		 * * The famos BTTV driver has it implemented with a struct video_channel argument
		 * * and we follow it for compatibility reasons
		 * *
		 * * BTW: this is the only way the user can set the norm!
		 */

	case VIDIOCSCHAN:
	{
		struct video_channel *vchan = arg;
		int res;

		dprintk(3,
			KERN_DEBUG
			"%s: VIDIOCSCHAN - channel=%d, norm=%d\n",
			ZR_DEVNAME(zr), vchan->channel, vchan->norm);

		down(&zr->resource_lock);
		if ((res = zoran_set_input(zr, vchan->channel)))
			goto schan_unlock_and_return;
		if ((res = zoran_set_norm(zr, vchan->norm)))
			goto schan_unlock_and_return;

		/* Make sure the changes come into effect */
		res = wait_grab_pending(zr);
	schan_unlock_and_return:
		up(&zr->resource_lock);
		return res;
	}
		break;

	case VIDIOCGPICT:
	{
		struct video_picture *vpict = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOCGPICT\n", ZR_DEVNAME(zr));

		memset(vpict, 0, sizeof(struct video_picture));
		down(&zr->resource_lock);
		vpict->hue = zr->hue;
		vpict->brightness = zr->brightness;
		vpict->contrast = zr->contrast;
		vpict->colour = zr->saturation;
		if (fh->overlay_settings.format) {
			vpict->depth = fh->overlay_settings.format->depth;
			vpict->palette = fh->overlay_settings.format->palette;
		} else {
			vpict->depth = 0;
		}
		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOCSPICT:
	{
		struct video_picture *vpict = arg;
		int i;

		dprintk(3,
			KERN_DEBUG
			"%s: VIDIOCSPICT - bri=%d, hue=%d, col=%d, con=%d, dep=%d, pal=%d\n",
			ZR_DEVNAME(zr), vpict->brightness, vpict->hue,
			vpict->colour, vpict->contrast, vpict->depth,
			vpict->palette);

		for (i = 0; i < zoran_num_formats; i++) {
			const struct zoran_format *fmt = &zoran_formats[i];

			if (fmt->palette != -1 &&
			    fmt->flags & ZORAN_FORMAT_OVERLAY &&
			    fmt->palette == vpict->palette &&
			    fmt->depth == vpict->depth)
				break;
		}
		if (i == zoran_num_formats) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOCSPICT - Invalid palette %d\n",
				ZR_DEVNAME(zr), vpict->palette);
			return -EINVAL;
		}

		down(&zr->resource_lock);

		decoder_command(zr, DECODER_SET_PICTURE, vpict);

		zr->hue = vpict->hue;
		zr->contrast = vpict->contrast;
		zr->saturation = vpict->colour;
		zr->brightness = vpict->brightness;

		fh->overlay_settings.format = &zoran_formats[i];

		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOCCAPTURE:
	{
		int *on = arg, res;

		dprintk(3, KERN_DEBUG "%s: VIDIOCCAPTURE - on=%d\n",
			ZR_DEVNAME(zr), *on);

		down(&zr->resource_lock);
		res = setup_overlay(file, *on);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOCGWIN:
	{
		struct video_window *vwin = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOCGWIN\n", ZR_DEVNAME(zr));

		memset(vwin, 0, sizeof(struct video_window));
		down(&zr->resource_lock);
		vwin->x = fh->overlay_settings.x;
		vwin->y = fh->overlay_settings.y;
		vwin->width = fh->overlay_settings.width;
		vwin->height = fh->overlay_settings.height;
		up(&zr->resource_lock);
		vwin->clipcount = 0;
		return 0;
	}
		break;

	case VIDIOCSWIN:
	{
		struct video_window *vwin = arg;
		int res;

		dprintk(3,
			KERN_DEBUG
			"%s: VIDIOCSWIN - x=%d, y=%d, w=%d, h=%d, clipcount=%d\n",
			ZR_DEVNAME(zr), vwin->x, vwin->y, vwin->width,
			vwin->height, vwin->clipcount);

		down(&zr->resource_lock);
		res =
		    setup_window(file, vwin->x, vwin->y, vwin->width,
				 vwin->height, vwin->clips,
				 vwin->clipcount, NULL);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOCGFBUF:
	{
		struct video_buffer *vbuf = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOCGFBUF\n", ZR_DEVNAME(zr));

		down(&zr->resource_lock);
		*vbuf = zr->buffer;
		up(&zr->resource_lock);
		return 0;
	}
		break;

	case VIDIOCSFBUF:
	{
		struct video_buffer *vbuf = arg;
		int i, res = 0;

		dprintk(3,
			KERN_DEBUG
			"%s: VIDIOCSFBUF - base=%p, w=%d, h=%d, depth=%d, bpl=%d\n",
			ZR_DEVNAME(zr), vbuf->base, vbuf->width,
			vbuf->height, vbuf->depth, vbuf->bytesperline);

		for (i = 0; i < zoran_num_formats; i++)
			if (zoran_formats[i].depth == vbuf->depth)
				break;
		if (i == zoran_num_formats) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOCSFBUF - invalid fbuf depth %d\n",
				ZR_DEVNAME(zr), vbuf->depth);
			return -EINVAL;
		}

		down(&zr->resource_lock);
		res =
		    setup_fbuffer(file, vbuf->base, &zoran_formats[i],
				  vbuf->width, vbuf->height,
				  vbuf->bytesperline);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOCSYNC:
	{
		int *frame = arg, res;

		dprintk(3, KERN_DEBUG "%s: VIDIOCSYNC - frame=%d\n",
			ZR_DEVNAME(zr), *frame);

		down(&zr->resource_lock);
		res = v4l_sync(file, *frame);
		up(&zr->resource_lock);
		if (!res)
			zr->v4l_sync_tail++;
		return res;
	}
		break;

	case VIDIOCMCAPTURE:
	{
		struct video_mmap *vmap = arg;
		int res;

		dprintk(3,
			KERN_DEBUG
			"%s: VIDIOCMCAPTURE - frame=%d, geom=%dx%d, fmt=%d\n",
			ZR_DEVNAME(zr), vmap->frame, vmap->width, vmap->height,
			vmap->format);

		down(&zr->resource_lock);
		res = v4l_grab(file, vmap);
		up(&zr->resource_lock);
		return res;
	}
		break;

	case VIDIOCGMBUF:
	{
		struct video_mbuf *vmbuf = arg;
		int i, res = 0;

		dprintk(3, KERN_DEBUG "%s: VIDIOCGMBUF\n", ZR_DEVNAME(zr));

		vmbuf->size =
		    fh->v4l_buffers.num_buffers *
		    fh->v4l_buffers.buffer_size;
		vmbuf->frames = fh->v4l_buffers.num_buffers;
		for (i = 0; i < vmbuf->frames; i++) {
			vmbuf->offsets[i] =
			    i * fh->v4l_buffers.buffer_size;
		}

		down(&zr->resource_lock);

		if (fh->jpg_buffers.allocated || fh->v4l_buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOCGMBUF - buffers already allocated\n",
				ZR_DEVNAME(zr));
			res = -EINVAL;
			goto v4l1reqbuf_unlock_and_return;
		}

		if (v4l_fbuffer_alloc(file)) {
			res = -ENOMEM;
			goto v4l1reqbuf_unlock_and_return;
		}

		/* The next mmap will map the V4L buffers */
		fh->map_mode = ZORAN_MAP_MODE_RAW;
	v4l1reqbuf_unlock_and_return:
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOCGUNIT:
	{
		struct video_unit *vunit = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOCGUNIT\n", ZR_DEVNAME(zr));

		vunit->video = zr->video_dev->minor;
		vunit->vbi = VIDEO_NO_UNIT;
		vunit->radio = VIDEO_NO_UNIT;
		vunit->audio = VIDEO_NO_UNIT;
		vunit->teletext = VIDEO_NO_UNIT;

		return 0;
	}
		break;

		/*
		 * RJ: In principal we could support subcaptures for V4L grabbing.
		 *     Not even the famous BTTV driver has them, however.
		 *     If there should be a strong demand, one could consider
		 *     to implement them.
		 */
	case VIDIOCGCAPTURE:
	{
		dprintk(3, KERN_ERR "%s: VIDIOCGCAPTURE not supported\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}
		break;

	case VIDIOCSCAPTURE:
	{
		dprintk(3, KERN_ERR "%s: VIDIOCSCAPTURE not supported\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}
		break;

	case BUZIOC_G_PARAMS:
	{
		struct zoran_params *bparams = arg;

		dprintk(3, KERN_DEBUG "%s: BUZIOC_G_PARAMS\n", ZR_DEVNAME(zr));

		memset(bparams, 0, sizeof(struct zoran_params));
		bparams->major_version = MAJOR_VERSION;
		bparams->minor_version = MINOR_VERSION;

		down(&zr->resource_lock);

		bparams->norm = zr->norm;
		bparams->input = zr->input;

		bparams->decimation = fh->jpg_settings.decimation;
		bparams->HorDcm = fh->jpg_settings.HorDcm;
		bparams->VerDcm = fh->jpg_settings.VerDcm;
		bparams->TmpDcm = fh->jpg_settings.TmpDcm;
		bparams->field_per_buff = fh->jpg_settings.field_per_buff;
		bparams->img_x = fh->jpg_settings.img_x;
		bparams->img_y = fh->jpg_settings.img_y;
		bparams->img_width = fh->jpg_settings.img_width;
		bparams->img_height = fh->jpg_settings.img_height;
		bparams->odd_even = fh->jpg_settings.odd_even;

		bparams->quality = fh->jpg_settings.jpg_comp.quality;
		bparams->APPn = fh->jpg_settings.jpg_comp.APPn;
		bparams->APP_len = fh->jpg_settings.jpg_comp.APP_len;
		memcpy(bparams->APP_data,
		       fh->jpg_settings.jpg_comp.APP_data,
		       sizeof(bparams->APP_data));
		bparams->COM_len = zr->jpg_settings.jpg_comp.COM_len;
		memcpy(bparams->COM_data,
		       fh->jpg_settings.jpg_comp.COM_data,
		       sizeof(bparams->COM_data));
		bparams->jpeg_markers =
		    fh->jpg_settings.jpg_comp.jpeg_markers;

		up(&zr->resource_lock);

		bparams->VFIFO_FB = 0;

		return 0;
	}
		break;

	case BUZIOC_S_PARAMS:
	{
		struct zoran_params *bparams = arg;
		int res = 0;

		dprintk(3, KERN_DEBUG "%s: BUZIOC_S_PARAMS\n", ZR_DEVNAME(zr));

		settings.decimation = bparams->decimation;
		settings.HorDcm = bparams->HorDcm;
		settings.VerDcm = bparams->VerDcm;
		settings.TmpDcm = bparams->TmpDcm;
		settings.field_per_buff = bparams->field_per_buff;
		settings.img_x = bparams->img_x;
		settings.img_y = bparams->img_y;
		settings.img_width = bparams->img_width;
		settings.img_height = bparams->img_height;
		settings.odd_even = bparams->odd_even;

		settings.jpg_comp.quality = bparams->quality;
		settings.jpg_comp.APPn = bparams->APPn;
		settings.jpg_comp.APP_len = bparams->APP_len;
		memcpy(settings.jpg_comp.APP_data, bparams->APP_data,
		       sizeof(bparams->APP_data));
		settings.jpg_comp.COM_len = bparams->COM_len;
		memcpy(settings.jpg_comp.COM_data, bparams->COM_data,
		       sizeof(bparams->COM_data));
		settings.jpg_comp.jpeg_markers = bparams->jpeg_markers;

		down(&zr->resource_lock);

		if (zr->codec_mode != BUZ_MODE_IDLE) {
			dprintk(1,
				KERN_ERR
				"%s: BUZIOC_S_PARAMS called, but Buz in capture/playback mode\n",
				ZR_DEVNAME(zr));
			res = -EINVAL;
			goto sparams_unlock_and_return;
		}

		/* Check the params first before overwriting our
		 * nternal values */
		if (zoran_check_jpg_settings(zr, &settings)) {
			res = -EINVAL;
			goto sparams_unlock_and_return;
		}

		fh->jpg_settings = settings;
	sparams_unlock_and_return:
		up(&zr->resource_lock);

		return res;
	}
		break;

	case BUZIOC_REQBUFS:
	{
		struct zoran_requestbuffers *breq = arg;
		int res = 0;

		dprintk(3,
			KERN_DEBUG
			"%s: BUZIOC_REQBUFS - count=%lu, size=%lu\n",
			ZR_DEVNAME(zr), breq->count, breq->size);

		/* Enforce reasonable lower and upper limits */
		if (breq->count < 4)
			breq->count = 4;	/* Could be choosen smaller */
		if (breq->count > jpg_nbufs)
			breq->count = jpg_nbufs;
		breq->size = PAGE_ALIGN(breq->size);
		if (breq->size < 8192)
			breq->size = 8192;	/* Arbitrary */
		/* breq->size is limited by 1 page for the stat_com
		 * tables to a Maximum of 2 MB */
		if (breq->size > jpg_bufsize)
			breq->size = jpg_bufsize;
		if (fh->jpg_buffers.need_contiguous &&
		    breq->size > MAX_KMALLOC_MEM)
			breq->size = MAX_KMALLOC_MEM;

		down(&zr->resource_lock);

		if (fh->jpg_buffers.allocated || fh->v4l_buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: BUZIOC_REQBUFS - buffers allready allocated\n",
				ZR_DEVNAME(zr));
			res = -EBUSY;
			goto jpgreqbuf_unlock_and_return;
		}

		fh->jpg_buffers.num_buffers = breq->count;
		fh->jpg_buffers.buffer_size = breq->size;

		if (jpg_fbuffer_alloc(file)) {
			res = -ENOMEM;
			goto jpgreqbuf_unlock_and_return;
		}

		/* The next mmap will map the MJPEG buffers - could
		 * also be *_PLAY, but it doesn't matter here */
		fh->map_mode = ZORAN_MAP_MODE_JPG_REC;
	jpgreqbuf_unlock_and_return:
		up(&zr->resource_lock);

		return res;
	}
		break;

	case BUZIOC_QBUF_CAPT:
	{
		int *frame = arg, res;

		dprintk(3, KERN_DEBUG "%s: BUZIOC_QBUF_CAPT - frame=%d\n",
			ZR_DEVNAME(zr), *frame);

		down(&zr->resource_lock);
		res = jpg_qbuf(file, *frame, BUZ_MODE_MOTION_COMPRESS);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case BUZIOC_QBUF_PLAY:
	{
		int *frame = arg, res;

		dprintk(3, KERN_DEBUG "%s: BUZIOC_QBUF_PLAY - frame=%d\n",
			ZR_DEVNAME(zr), *frame);

		down(&zr->resource_lock);
		res = jpg_qbuf(file, *frame, BUZ_MODE_MOTION_DECOMPRESS);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case BUZIOC_SYNC:
	{
		struct zoran_sync *bsync = arg;
		int res;

		dprintk(3, KERN_DEBUG "%s: BUZIOC_SYNC\n", ZR_DEVNAME(zr));

		down(&zr->resource_lock);
		res = jpg_sync(file, bsync);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case BUZIOC_G_STATUS:
	{
		struct zoran_status *bstat = arg;
		int norm, input, status, res = 0;

		dprintk(3, KERN_DEBUG "%s: BUZIOC_G_STATUS\n", ZR_DEVNAME(zr));

		if (zr->codec_mode != BUZ_MODE_IDLE) {
			dprintk(1,
				KERN_ERR
				"%s: BUZIOC_G_STATUS called but Buz in capture/playback mode\n",
				ZR_DEVNAME(zr));
			return -EINVAL;
		}

		input = zr->card.input[bstat->input].muxsel;
		norm = VIDEO_MODE_AUTO;

		down(&zr->resource_lock);

		if (zr->codec_mode != BUZ_MODE_IDLE) {
			dprintk(1,
				KERN_ERR
				"%s: BUZIOC_G_STATUS called, but Buz in capture/playback mode\n",
				ZR_DEVNAME(zr));
			res = -EINVAL;
			goto gstat_unlock_and_return;
		}

		decoder_command(zr, DECODER_SET_INPUT, &input);
		decoder_command(zr, DECODER_SET_NORM, &norm);

		/* sleep 1 second */
		ssleep(1);

		/* Get status of video decoder */
		decoder_command(zr, DECODER_GET_STATUS, &status);

		/* restore previous input and norm */
		input = zr->card.input[zr->input].muxsel;
		decoder_command(zr, DECODER_SET_INPUT, &input);
		decoder_command(zr, DECODER_SET_NORM, &zr->norm);
	gstat_unlock_and_return:
		up(&zr->resource_lock);

		if (!res) {
			bstat->signal =
			    (status & DECODER_STATUS_GOOD) ? 1 : 0;
			if (status & DECODER_STATUS_NTSC)
				bstat->norm = VIDEO_MODE_NTSC;
			else if (status & DECODER_STATUS_SECAM)
				bstat->norm = VIDEO_MODE_SECAM;
			else
				bstat->norm = VIDEO_MODE_PAL;

			bstat->color =
			    (status & DECODER_STATUS_COLOR) ? 1 : 0;
		}

		return res;
	}
		break;

#ifdef HAVE_V4L2

		/* The new video4linux2 capture interface - much nicer than video4linux1, since
		 * it allows for integrating the JPEG capturing calls inside standard v4l2
		 */

	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_QUERYCAP\n", ZR_DEVNAME(zr));

		memset(cap, 0, sizeof(*cap));
		strncpy(cap->card, ZR_DEVNAME(zr), sizeof(cap->card));
		strncpy(cap->driver, "zoran", sizeof(cap->driver));
		snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
			 pci_name(zr->pci_dev));
		cap->version =
		    KERNEL_VERSION(MAJOR_VERSION, MINOR_VERSION,
				   RELEASE_VERSION);
		cap->capabilities = ZORAN_V4L2_VID_FLAGS;

		return 0;
	}
		break;

	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *fmt = arg;
		int index = fmt->index, num = -1, i, flag = 0, type =
		    fmt->type;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_ENUM_FMT - index=%d\n",
			ZR_DEVNAME(zr), fmt->index);

		switch (fmt->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			flag = ZORAN_FORMAT_CAPTURE;
			break;
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			flag = ZORAN_FORMAT_PLAYBACK;
			break;
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:
			flag = ZORAN_FORMAT_OVERLAY;
			break;
		default:
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_ENUM_FMT - unknown type %d\n",
				ZR_DEVNAME(zr), fmt->type);
			return -EINVAL;
		}

		for (i = 0; i < zoran_num_formats; i++) {
			if (zoran_formats[i].flags & flag)
				num++;
			if (num == fmt->index)
				break;
		}
		if (fmt->index < 0 /* late, but not too late */  ||
		    i == zoran_num_formats)
			return -EINVAL;

		memset(fmt, 0, sizeof(*fmt));
		fmt->index = index;
		fmt->type = type;
		strncpy(fmt->description, zoran_formats[i].name, 31);
		fmt->pixelformat = zoran_formats[i].fourcc;
		if (zoran_formats[i].flags & ZORAN_FORMAT_COMPRESSED)
			fmt->flags |= V4L2_FMT_FLAG_COMPRESSED;

		return 0;
	}
		break;

	case VIDIOC_G_FMT:
	{
		struct v4l2_format *fmt = arg;
		int type = fmt->type;

		dprintk(5, KERN_DEBUG "%s: VIDIOC_G_FMT\n", ZR_DEVNAME(zr));

		memset(fmt, 0, sizeof(*fmt));
		fmt->type = type;

		switch (fmt->type) {
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:

			down(&zr->resource_lock);

			fmt->fmt.win.w.left = fh->overlay_settings.x;
			fmt->fmt.win.w.top = fh->overlay_settings.y;
			fmt->fmt.win.w.width = fh->overlay_settings.width;
			fmt->fmt.win.w.height =
			    fh->overlay_settings.height;
			if (fh->overlay_settings.width * 2 >
			    BUZ_MAX_HEIGHT)
				fmt->fmt.win.field = V4L2_FIELD_INTERLACED;
			else
				fmt->fmt.win.field = V4L2_FIELD_TOP;

			up(&zr->resource_lock);

			break;

		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:

			down(&zr->resource_lock);

			if (fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			    fh->map_mode == ZORAN_MAP_MODE_RAW) {

				fmt->fmt.pix.width =
				    fh->v4l_settings.width;
				fmt->fmt.pix.height =
				    fh->v4l_settings.height;
				fmt->fmt.pix.sizeimage =
				    fh->v4l_buffers.buffer_size;
				fmt->fmt.pix.pixelformat =
				    fh->v4l_settings.format->fourcc;
				fmt->fmt.pix.colorspace =
				    fh->v4l_settings.format->colorspace;
				fmt->fmt.pix.bytesperline = 0;
				if (BUZ_MAX_HEIGHT <
				    (fh->v4l_settings.height * 2))
					fmt->fmt.pix.field =
					    V4L2_FIELD_INTERLACED;
				else
					fmt->fmt.pix.field =
					    V4L2_FIELD_TOP;

			} else {

				fmt->fmt.pix.width =
				    fh->jpg_settings.img_width /
				    fh->jpg_settings.HorDcm;
				fmt->fmt.pix.height =
				    fh->jpg_settings.img_height /
				    (fh->jpg_settings.VerDcm *
				     fh->jpg_settings.TmpDcm);
				fmt->fmt.pix.sizeimage =
				    zoran_v4l2_calc_bufsize(&fh->
							    jpg_settings);
				fmt->fmt.pix.pixelformat =
				    V4L2_PIX_FMT_MJPEG;
				if (fh->jpg_settings.TmpDcm == 1)
					fmt->fmt.pix.field =
					    (fh->jpg_settings.
					     odd_even ? V4L2_FIELD_SEQ_BT :
					     V4L2_FIELD_SEQ_BT);
				else
					fmt->fmt.pix.field =
					    (fh->jpg_settings.
					     odd_even ? V4L2_FIELD_TOP :
					     V4L2_FIELD_BOTTOM);

				fmt->fmt.pix.bytesperline = 0;
				fmt->fmt.pix.colorspace =
				    V4L2_COLORSPACE_SMPTE170M;
			}

			up(&zr->resource_lock);

			break;

		default:
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_G_FMT - unsupported type %d\n",
				ZR_DEVNAME(zr), fmt->type);
			return -EINVAL;
		}
		return 0;
	}
		break;

	case VIDIOC_S_FMT:
	{
		struct v4l2_format *fmt = arg;
		int i, res = 0;
		__u32 printformat;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_S_FMT - type=%d, ",
			ZR_DEVNAME(zr), fmt->type);

		switch (fmt->type) {
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:

			dprintk(3, "x=%d, y=%d, w=%d, h=%d, cnt=%d, map=0x%p\n",
				fmt->fmt.win.w.left, fmt->fmt.win.w.top,
				fmt->fmt.win.w.width,
				fmt->fmt.win.w.height,
				fmt->fmt.win.clipcount,
				fmt->fmt.win.bitmap);
			down(&zr->resource_lock);
			res =
			    setup_window(file, fmt->fmt.win.w.left,
					 fmt->fmt.win.w.top,
					 fmt->fmt.win.w.width,
					 fmt->fmt.win.w.height,
					 (struct video_clip __user *)
					   fmt->fmt.win.clips,
					 fmt->fmt.win.clipcount,
					 fmt->fmt.win.bitmap);
			up(&zr->resource_lock);
			return res;
			break;

		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:

			printformat =
			    __cpu_to_le32(fmt->fmt.pix.pixelformat);
			dprintk(3, "size=%dx%d, fmt=0x%x (%4.4s)\n",
				fmt->fmt.pix.width, fmt->fmt.pix.height,
				fmt->fmt.pix.pixelformat,
				(char *) &printformat);

			if (fmt->fmt.pix.bytesperline > 0) {
				dprintk(5,
					KERN_ERR "%s: bpl not supported\n",
					ZR_DEVNAME(zr));
				return -EINVAL;
			}

			/* we can be requested to do JPEG/raw playback/capture */
			if (!
			    (fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
			     (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
			      fmt->fmt.pix.pixelformat ==
			      V4L2_PIX_FMT_MJPEG))) {
				dprintk(1,
					KERN_ERR
					"%s: VIDIOC_S_FMT - unknown type %d/0x%x(%4.4s) combination\n",
					ZR_DEVNAME(zr), fmt->type,
					fmt->fmt.pix.pixelformat,
					(char *) &printformat);
				return -EINVAL;
			}

			if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
				down(&zr->resource_lock);

				settings = fh->jpg_settings;

				if (fh->v4l_buffers.allocated ||
				    fh->jpg_buffers.allocated) {
					dprintk(1,
						KERN_ERR
						"%s: VIDIOC_S_FMT - cannot change capture mode\n",
						ZR_DEVNAME(zr));
					res = -EBUSY;
					goto sfmtjpg_unlock_and_return;
				}

				/* we actually need to set 'real' parameters now */
				if ((fmt->fmt.pix.height * 2) >
				    BUZ_MAX_HEIGHT)
					settings.TmpDcm = 1;
				else
					settings.TmpDcm = 2;
				settings.decimation = 0;
				if (fmt->fmt.pix.height <=
				    fh->jpg_settings.img_height / 2)
					settings.VerDcm = 2;
				else
					settings.VerDcm = 1;
				if (fmt->fmt.pix.width <=
				    fh->jpg_settings.img_width / 4)
					settings.HorDcm = 4;
				else if (fmt->fmt.pix.width <=
					 fh->jpg_settings.img_width / 2)
					settings.HorDcm = 2;
				else
					settings.HorDcm = 1;
				if (settings.TmpDcm == 1)
					settings.field_per_buff = 2;
				else
					settings.field_per_buff = 1;

				/* check */
				if ((res =
				     zoran_check_jpg_settings(zr,
							      &settings)))
					goto sfmtjpg_unlock_and_return;

				/* it's ok, so set them */
				fh->jpg_settings = settings;

				/* tell the user what we actually did */
				fmt->fmt.pix.width =
				    settings.img_width / settings.HorDcm;
				fmt->fmt.pix.height =
				    settings.img_height * 2 /
				    (settings.TmpDcm * settings.VerDcm);
				if (settings.TmpDcm == 1)
					fmt->fmt.pix.field =
					    (fh->jpg_settings.
					     odd_even ? V4L2_FIELD_SEQ_TB :
					     V4L2_FIELD_SEQ_BT);
				else
					fmt->fmt.pix.field =
					    (fh->jpg_settings.
					     odd_even ? V4L2_FIELD_TOP :
					     V4L2_FIELD_BOTTOM);
				fh->jpg_buffers.buffer_size =
				    zoran_v4l2_calc_bufsize(&fh->
							    jpg_settings);
				fmt->fmt.pix.sizeimage =
				    fh->jpg_buffers.buffer_size;

				/* we hereby abuse this variable to show that
				 * we're gonna do mjpeg capture */
				fh->map_mode =
				    (fmt->type ==
				     V4L2_BUF_TYPE_VIDEO_CAPTURE) ?
				    ZORAN_MAP_MODE_JPG_REC :
				    ZORAN_MAP_MODE_JPG_PLAY;
			sfmtjpg_unlock_and_return:
				up(&zr->resource_lock);
			} else {
				for (i = 0; i < zoran_num_formats; i++)
					if (fmt->fmt.pix.pixelformat ==
					    zoran_formats[i].fourcc)
						break;
				if (i == zoran_num_formats) {
					dprintk(1,
						KERN_ERR
						"%s: VIDIOC_S_FMT - unknown/unsupported format 0x%x (%4.4s)\n",
						ZR_DEVNAME(zr),
						fmt->fmt.pix.pixelformat,
						(char *) &printformat);
					return -EINVAL;
				}
				down(&zr->resource_lock);
				if (fh->jpg_buffers.allocated ||
				    (fh->v4l_buffers.allocated &&
				     fh->v4l_buffers.active !=
				     ZORAN_FREE)) {
					dprintk(1,
						KERN_ERR
						"%s: VIDIOC_S_FMT - cannot change capture mode\n",
						ZR_DEVNAME(zr));
					res = -EBUSY;
					goto sfmtv4l_unlock_and_return;
				}
				if (fmt->fmt.pix.height > BUZ_MAX_HEIGHT)
					fmt->fmt.pix.height =
					    BUZ_MAX_HEIGHT;
				if (fmt->fmt.pix.width > BUZ_MAX_WIDTH)
					fmt->fmt.pix.width = BUZ_MAX_WIDTH;

				if ((res =
				     zoran_v4l_set_format(file,
							  fmt->fmt.pix.
							  width,
							  fmt->fmt.pix.
							  height,
							  &zoran_formats
							  [i])))
					goto sfmtv4l_unlock_and_return;

				/* tell the user the
				 * results/missing stuff */
				fmt->fmt.pix.sizeimage = fh->v4l_buffers.buffer_size	/*zr->gbpl * zr->gheight */
				    ;
				if (BUZ_MAX_HEIGHT <
				    (fh->v4l_settings.height * 2))
					fmt->fmt.pix.field =
					    V4L2_FIELD_INTERLACED;
				else
					fmt->fmt.pix.field =
					    V4L2_FIELD_TOP;

				fh->map_mode = ZORAN_MAP_MODE_RAW;
			sfmtv4l_unlock_and_return:
				up(&zr->resource_lock);
			}

			break;

		default:
			dprintk(3, "unsupported\n");
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_S_FMT - unsupported type %d\n",
				ZR_DEVNAME(zr), fmt->type);
			return -EINVAL;
		}

		return res;
	}
		break;

	case VIDIOC_G_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_G_FBUF\n", ZR_DEVNAME(zr));

		memset(fb, 0, sizeof(*fb));
		down(&zr->resource_lock);
		fb->base = zr->buffer.base;
		fb->fmt.width = zr->buffer.width;
		fb->fmt.height = zr->buffer.height;
		if (zr->overlay_settings.format) {
			fb->fmt.pixelformat =
				fh->overlay_settings.format->fourcc;
		}
		fb->fmt.bytesperline = zr->buffer.bytesperline;
		up(&zr->resource_lock);
		fb->fmt.colorspace = V4L2_COLORSPACE_SRGB;
		fb->fmt.field = V4L2_FIELD_INTERLACED;
		fb->flags = V4L2_FBUF_FLAG_OVERLAY;
		fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;

		return 0;
	}
		break;

	case VIDIOC_S_FBUF:
	{
		int i, res = 0;
		struct v4l2_framebuffer *fb = arg;
		__u32 printformat = __cpu_to_le32(fb->fmt.pixelformat);

		dprintk(3,
			KERN_DEBUG
			"%s: VIDIOC_S_FBUF - base=0x%p, size=%dx%d, bpl=%d, fmt=0x%x (%4.4s)\n",
			ZR_DEVNAME(zr), fb->base, fb->fmt.width, fb->fmt.height,
			fb->fmt.bytesperline, fb->fmt.pixelformat,
			(char *) &printformat);

		for (i = 0; i < zoran_num_formats; i++)
			if (zoran_formats[i].fourcc == fb->fmt.pixelformat)
				break;
		if (i == zoran_num_formats) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_S_FBUF - format=0x%x (%4.4s) not allowed\n",
				ZR_DEVNAME(zr), fb->fmt.pixelformat,
				(char *) &printformat);
			return -EINVAL;
		}

		down(&zr->resource_lock);
		res =
		    setup_fbuffer(file, fb->base, &zoran_formats[i],
				  fb->fmt.width, fb->fmt.height,
				  fb->fmt.bytesperline);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOC_OVERLAY:
	{
		int *on = arg, res;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_PREVIEW - on=%d\n",
			ZR_DEVNAME(zr), *on);

		down(&zr->resource_lock);
		res = setup_overlay(file, *on);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOC_REQBUFS:
	{
		struct v4l2_requestbuffers *req = arg;
		int res = 0;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_REQBUFS - type=%d\n",
			ZR_DEVNAME(zr), req->type);

		if (req->memory != V4L2_MEMORY_MMAP) {
			dprintk(1,
				KERN_ERR
				"%s: only MEMORY_MMAP capture is supported, not %d\n",
				ZR_DEVNAME(zr), req->memory);
			return -EINVAL;
		}

		down(&zr->resource_lock);

		if (fh->v4l_buffers.allocated || fh->jpg_buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_REQBUFS - buffers allready allocated\n",
				ZR_DEVNAME(zr));
			res = -EBUSY;
			goto v4l2reqbuf_unlock_and_return;
		}

		if (fh->map_mode == ZORAN_MAP_MODE_RAW &&
		    req->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {

			/* control user input */
			if (req->count < 2)
				req->count = 2;
			if (req->count > v4l_nbufs)
				req->count = v4l_nbufs;
			fh->v4l_buffers.num_buffers = req->count;

			if (v4l_fbuffer_alloc(file)) {
				res = -ENOMEM;
				goto v4l2reqbuf_unlock_and_return;
			}

			/* The next mmap will map the V4L buffers */
			fh->map_mode = ZORAN_MAP_MODE_RAW;

		} else if (fh->map_mode == ZORAN_MAP_MODE_JPG_REC ||
			   fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY) {

			/* we need to calculate size ourselves now */
			if (req->count < 4)
				req->count = 4;
			if (req->count > jpg_nbufs)
				req->count = jpg_nbufs;
			fh->jpg_buffers.num_buffers = req->count;
			fh->jpg_buffers.buffer_size =
			    zoran_v4l2_calc_bufsize(&fh->jpg_settings);

			if (jpg_fbuffer_alloc(file)) {
				res = -ENOMEM;
				goto v4l2reqbuf_unlock_and_return;
			}

			/* The next mmap will map the MJPEG buffers */
			if (req->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
				fh->map_mode = ZORAN_MAP_MODE_JPG_REC;
			else
				fh->map_mode = ZORAN_MAP_MODE_JPG_PLAY;

		} else {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_REQBUFS - unknown type %d\n",
				ZR_DEVNAME(zr), req->type);
			res = -EINVAL;
			goto v4l2reqbuf_unlock_and_return;
		}
	v4l2reqbuf_unlock_and_return:
		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOC_QUERYBUF:
	{
		struct v4l2_buffer *buf = arg;
		__u32 type = buf->type;
		int index = buf->index, res;

		dprintk(3,
			KERN_DEBUG
			"%s: VIDIOC_QUERYBUF - index=%d, type=%d\n",
			ZR_DEVNAME(zr), buf->index, buf->type);

		memset(buf, 0, sizeof(buf));
		buf->type = type;
		buf->index = index;

		down(&zr->resource_lock);
		res = zoran_v4l2_buffer_status(file, buf, buf->index);
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOC_QBUF:
	{
		struct v4l2_buffer *buf = arg;
		int res = 0, codec_mode, buf_type;

		dprintk(3,
			KERN_DEBUG "%s: VIDIOC_QBUF - type=%d, index=%d\n",
			ZR_DEVNAME(zr), buf->type, buf->index);

		down(&zr->resource_lock);

		switch (fh->map_mode) {
		case ZORAN_MAP_MODE_RAW:
			if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
				dprintk(1,
					KERN_ERR
					"%s: VIDIOC_QBUF - invalid buf->type=%d for map_mode=%d\n",
					ZR_DEVNAME(zr), buf->type, fh->map_mode);
				res = -EINVAL;
				goto qbuf_unlock_and_return;
			}

			res = zoran_v4l_queue_frame(file, buf->index);
			if (res)
				goto qbuf_unlock_and_return;
			if (!zr->v4l_memgrab_active &&
			    fh->v4l_buffers.active == ZORAN_LOCKED)
				zr36057_set_memgrab(zr, 1);
			break;

		case ZORAN_MAP_MODE_JPG_REC:
		case ZORAN_MAP_MODE_JPG_PLAY:
			if (fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY) {
				buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
				codec_mode = BUZ_MODE_MOTION_DECOMPRESS;
			} else {
				buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				codec_mode = BUZ_MODE_MOTION_COMPRESS;
			}

			if (buf->type != buf_type) {
				dprintk(1,
					KERN_ERR
					"%s: VIDIOC_QBUF - invalid buf->type=%d for map_mode=%d\n",
					ZR_DEVNAME(zr), buf->type, fh->map_mode);
				res = -EINVAL;
				goto qbuf_unlock_and_return;
			}

			res =
			    zoran_jpg_queue_frame(file, buf->index,
						  codec_mode);
			if (res != 0)
				goto qbuf_unlock_and_return;
			if (zr->codec_mode == BUZ_MODE_IDLE &&
			    fh->jpg_buffers.active == ZORAN_LOCKED) {
				zr36057_enable_jpg(zr, codec_mode);
			}
			break;

		default:
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_QBUF - unsupported type %d\n",
				ZR_DEVNAME(zr), buf->type);
			res = -EINVAL;
			goto qbuf_unlock_and_return;
		}
	qbuf_unlock_and_return:
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOC_DQBUF:
	{
		struct v4l2_buffer *buf = arg;
		int res = 0, buf_type, num = -1;	/* compiler borks here (?) */

		dprintk(3, KERN_DEBUG "%s: VIDIOC_DQBUF - type=%d\n",
			ZR_DEVNAME(zr), buf->type);

		down(&zr->resource_lock);

		switch (fh->map_mode) {
		case ZORAN_MAP_MODE_RAW:
			if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
				dprintk(1,
					KERN_ERR
					"%s: VIDIOC_QBUF - invalid buf->type=%d for map_mode=%d\n",
					ZR_DEVNAME(zr), buf->type, fh->map_mode);
				res = -EINVAL;
				goto dqbuf_unlock_and_return;
			}

			num = zr->v4l_pend[zr->v4l_sync_tail & V4L_MASK_FRAME];
			if (file->f_flags & O_NONBLOCK &&
			    zr->v4l_buffers.buffer[num].state !=
			    BUZ_STATE_DONE) {
				res = -EAGAIN;
				goto dqbuf_unlock_and_return;
			}
			res = v4l_sync(file, num);
			if (res)
				goto dqbuf_unlock_and_return;
			else
				zr->v4l_sync_tail++;
			res = zoran_v4l2_buffer_status(file, buf, num);
			break;

		case ZORAN_MAP_MODE_JPG_REC:
		case ZORAN_MAP_MODE_JPG_PLAY:
		{
			struct zoran_sync bs;

			if (fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY)
				buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
			else
				buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

			if (buf->type != buf_type) {
				dprintk(1,
					KERN_ERR
					"%s: VIDIOC_QBUF - invalid buf->type=%d for map_mode=%d\n",
					ZR_DEVNAME(zr), buf->type, fh->map_mode);
				res = -EINVAL;
				goto dqbuf_unlock_and_return;
			}

			num =
			    zr->jpg_pend[zr->
					 jpg_que_tail & BUZ_MASK_FRAME];

			if (file->f_flags & O_NONBLOCK &&
			    zr->jpg_buffers.buffer[num].state !=
			    BUZ_STATE_DONE) {
				res = -EAGAIN;
				goto dqbuf_unlock_and_return;
			}
			res = jpg_sync(file, &bs);
			if (res)
				goto dqbuf_unlock_and_return;
			res =
			    zoran_v4l2_buffer_status(file, buf, bs.frame);
			break;
		}

		default:
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_DQBUF - unsupported type %d\n",
				ZR_DEVNAME(zr), buf->type);
			res = -EINVAL;
			goto dqbuf_unlock_and_return;
		}
	dqbuf_unlock_and_return:
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOC_STREAMON:
	{
		int res = 0;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_STREAMON\n", ZR_DEVNAME(zr));

		down(&zr->resource_lock);

		switch (fh->map_mode) {
		case ZORAN_MAP_MODE_RAW:	/* raw capture */
			if (zr->v4l_buffers.active != ZORAN_ACTIVE ||
			    fh->v4l_buffers.active != ZORAN_ACTIVE) {
				res = -EBUSY;
				goto strmon_unlock_and_return;
			}

			zr->v4l_buffers.active = fh->v4l_buffers.active =
			    ZORAN_LOCKED;
			zr->v4l_settings = fh->v4l_settings;

			zr->v4l_sync_tail = zr->v4l_pend_tail;
			if (!zr->v4l_memgrab_active &&
			    zr->v4l_pend_head != zr->v4l_pend_tail) {
				zr36057_set_memgrab(zr, 1);
			}
			break;

		case ZORAN_MAP_MODE_JPG_REC:
		case ZORAN_MAP_MODE_JPG_PLAY:
			/* what is the codec mode right now? */
			if (zr->jpg_buffers.active != ZORAN_ACTIVE ||
			    fh->jpg_buffers.active != ZORAN_ACTIVE) {
				res = -EBUSY;
				goto strmon_unlock_and_return;
			}

			zr->jpg_buffers.active = fh->jpg_buffers.active =
			    ZORAN_LOCKED;

			if (zr->jpg_que_head != zr->jpg_que_tail) {
				/* Start the jpeg codec when the first frame is queued  */
				jpeg_start(zr);
			}

			break;
		default:
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_STREAMON - invalid map mode %d\n",
				ZR_DEVNAME(zr), fh->map_mode);
			res = -EINVAL;
			goto strmon_unlock_and_return;
		}
	strmon_unlock_and_return:
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOC_STREAMOFF:
	{
		int i, res = 0;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_STREAMOFF\n", ZR_DEVNAME(zr));

		down(&zr->resource_lock);

		switch (fh->map_mode) {
		case ZORAN_MAP_MODE_RAW:	/* raw capture */
			if (fh->v4l_buffers.active == ZORAN_FREE &&
			    zr->v4l_buffers.active != ZORAN_FREE) {
				res = -EPERM;	/* stay off other's settings! */
				goto strmoff_unlock_and_return;
			}
			if (zr->v4l_buffers.active == ZORAN_FREE)
				goto strmoff_unlock_and_return;

			/* unload capture */
			if (zr->v4l_memgrab_active)
				zr36057_set_memgrab(zr, 0);

			for (i = 0; i < fh->v4l_buffers.num_buffers; i++)
				zr->v4l_buffers.buffer[i].state =
				    BUZ_STATE_USER;
			fh->v4l_buffers = zr->v4l_buffers;

			zr->v4l_buffers.active = fh->v4l_buffers.active =
			    ZORAN_FREE;

			zr->v4l_grab_seq = 0;
			zr->v4l_pend_head = zr->v4l_pend_tail = 0;
			zr->v4l_sync_tail = 0;

			break;

		case ZORAN_MAP_MODE_JPG_REC:
		case ZORAN_MAP_MODE_JPG_PLAY:
			if (fh->jpg_buffers.active == ZORAN_FREE &&
			    zr->jpg_buffers.active != ZORAN_FREE) {
				res = -EPERM;	/* stay off other's settings! */
				goto strmoff_unlock_and_return;
			}
			if (zr->jpg_buffers.active == ZORAN_FREE)
				goto strmoff_unlock_and_return;

			res =
			    jpg_qbuf(file, -1,
				     (fh->map_mode ==
				      ZORAN_MAP_MODE_JPG_REC) ?
				     BUZ_MODE_MOTION_COMPRESS :
				     BUZ_MODE_MOTION_DECOMPRESS);
			if (res)
				goto strmoff_unlock_and_return;
			break;
		default:
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_STREAMOFF - invalid map mode %d\n",
				ZR_DEVNAME(zr), fh->map_mode);
			res = -EINVAL;
			goto strmoff_unlock_and_return;
		}
	strmoff_unlock_and_return:
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *ctrl = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_QUERYCTRL - id=%d\n",
			ZR_DEVNAME(zr), ctrl->id);

		/* we only support hue/saturation/contrast/brightness */
		if (ctrl->id < V4L2_CID_BRIGHTNESS ||
		    ctrl->id > V4L2_CID_HUE)
			return -EINVAL;
		else {
			int id = ctrl->id;
			memset(ctrl, 0, sizeof(*ctrl));
			ctrl->id = id;
		}

		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			strncpy(ctrl->name, "Brightness", 31);
			break;
		case V4L2_CID_CONTRAST:
			strncpy(ctrl->name, "Contrast", 31);
			break;
		case V4L2_CID_SATURATION:
			strncpy(ctrl->name, "Saturation", 31);
			break;
		case V4L2_CID_HUE:
			strncpy(ctrl->name, "Hue", 31);
			break;
		}

		ctrl->minimum = 0;
		ctrl->maximum = 65535;
		ctrl->step = 1;
		ctrl->default_value = 32768;
		ctrl->type = V4L2_CTRL_TYPE_INTEGER;

		return 0;
	}
		break;

	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *ctrl = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_G_CTRL - id=%d\n",
			ZR_DEVNAME(zr), ctrl->id);

		/* we only support hue/saturation/contrast/brightness */
		if (ctrl->id < V4L2_CID_BRIGHTNESS ||
		    ctrl->id > V4L2_CID_HUE)
			return -EINVAL;

		down(&zr->resource_lock);
		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			ctrl->value = zr->brightness;
			break;
		case V4L2_CID_CONTRAST:
			ctrl->value = zr->contrast;
			break;
		case V4L2_CID_SATURATION:
			ctrl->value = zr->saturation;
			break;
		case V4L2_CID_HUE:
			ctrl->value = zr->hue;
			break;
		}
		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *ctrl = arg;
		struct video_picture pict;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_S_CTRL - id=%d\n",
			ZR_DEVNAME(zr), ctrl->id);

		/* we only support hue/saturation/contrast/brightness */
		if (ctrl->id < V4L2_CID_BRIGHTNESS ||
		    ctrl->id > V4L2_CID_HUE)
			return -EINVAL;

		if (ctrl->value < 0 || ctrl->value > 65535) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_S_CTRL - invalid value %d for id=%d\n",
				ZR_DEVNAME(zr), ctrl->value, ctrl->id);
			return -EINVAL;
		}

		down(&zr->resource_lock);
		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			zr->brightness = ctrl->value;
			break;
		case V4L2_CID_CONTRAST:
			zr->contrast = ctrl->value;
			break;
		case V4L2_CID_SATURATION:
			zr->saturation = ctrl->value;
			break;
		case V4L2_CID_HUE:
			zr->hue = ctrl->value;
			break;
		}
		pict.brightness = zr->brightness;
		pict.contrast = zr->contrast;
		pict.colour = zr->saturation;
		pict.hue = zr->hue;

		decoder_command(zr, DECODER_SET_PICTURE, &pict);

		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *std = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_ENUMSTD - index=%d\n",
			ZR_DEVNAME(zr), std->index);

		if (std->index < 0 || std->index >= (zr->card.norms + 1))
			return -EINVAL;
		else {
			int id = std->index;
			memset(std, 0, sizeof(*std));
			std->index = id;
		}

		if (std->index == zr->card.norms) {
			/* if we have autodetect, ... */
			struct video_decoder_capability caps;
			decoder_command(zr, DECODER_GET_CAPABILITIES,
					&caps);
			if (caps.flags & VIDEO_DECODER_AUTO) {
				std->id = V4L2_STD_ALL;
				strncpy(std->name, "Autodetect", 31);
				return 0;
			} else
				return -EINVAL;
		}
		switch (std->index) {
		case 0:
			std->id = V4L2_STD_PAL;
			strncpy(std->name, "PAL", 31);
			std->frameperiod.numerator = 1;
			std->frameperiod.denominator = 25;
			std->framelines = zr->card.tvn[0]->Ht;
			break;
		case 1:
			std->id = V4L2_STD_NTSC;
			strncpy(std->name, "NTSC", 31);
			std->frameperiod.numerator = 1001;
			std->frameperiod.denominator = 30000;
			std->framelines = zr->card.tvn[1]->Ht;
			break;
		case 2:
			std->id = V4L2_STD_SECAM;
			strncpy(std->name, "SECAM", 31);
			std->frameperiod.numerator = 1;
			std->frameperiod.denominator = 25;
			std->framelines = zr->card.tvn[2]->Ht;
			break;
		}

		return 0;
	}
		break;

	case VIDIOC_G_STD:
	{
		v4l2_std_id *std = arg;
		int norm;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_G_STD\n", ZR_DEVNAME(zr));

		down(&zr->resource_lock);
		norm = zr->norm;
		up(&zr->resource_lock);

		switch (norm) {
		case VIDEO_MODE_PAL:
			*std = V4L2_STD_PAL;
			break;
		case VIDEO_MODE_NTSC:
			*std = V4L2_STD_NTSC;
			break;
		case VIDEO_MODE_SECAM:
			*std = V4L2_STD_SECAM;
			break;
		}

		return 0;
	}
		break;

	case VIDIOC_S_STD:
	{
		int norm = -1, res = 0;
		v4l2_std_id *std = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_S_STD - norm=0x%llx\n",
			ZR_DEVNAME(zr), (unsigned long long)*std);

		if (*std == V4L2_STD_PAL)
			norm = VIDEO_MODE_PAL;
		else if (*std == V4L2_STD_NTSC)
			norm = VIDEO_MODE_NTSC;
		else if (*std == V4L2_STD_SECAM)
			norm = VIDEO_MODE_SECAM;
		else if (*std == V4L2_STD_ALL)
			norm = VIDEO_MODE_AUTO;
		else {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_S_STD - invalid norm 0x%llx\n",
				ZR_DEVNAME(zr), (unsigned long long)*std);
			return -EINVAL;
		}

		down(&zr->resource_lock);
		if ((res = zoran_set_norm(zr, norm)))
			goto sstd_unlock_and_return;

		res = wait_grab_pending(zr);
	sstd_unlock_and_return:
		up(&zr->resource_lock);
		return res;
	}
		break;

	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *inp = arg;
		int status;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_ENUMINPUT - index=%d\n",
			ZR_DEVNAME(zr), inp->index);

		if (inp->index < 0 || inp->index >= zr->card.inputs)
			return -EINVAL;
		else {
			int id = inp->index;
			memset(inp, 0, sizeof(*inp));
			inp->index = id;
		}

		strncpy(inp->name, zr->card.input[inp->index].name,
			sizeof(inp->name) - 1);
		inp->type = V4L2_INPUT_TYPE_CAMERA;
		inp->std = V4L2_STD_ALL;

		/* Get status of video decoder */
		down(&zr->resource_lock);
		decoder_command(zr, DECODER_GET_STATUS, &status);
		up(&zr->resource_lock);

		if (!(status & DECODER_STATUS_GOOD)) {
			inp->status |= V4L2_IN_ST_NO_POWER;
			inp->status |= V4L2_IN_ST_NO_SIGNAL;
		}
		if (!(status & DECODER_STATUS_COLOR))
			inp->status |= V4L2_IN_ST_NO_COLOR;

		return 0;
	}
		break;

	case VIDIOC_G_INPUT:
	{
		int *input = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_G_INPUT\n", ZR_DEVNAME(zr));

		down(&zr->resource_lock);
		*input = zr->input;
		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOC_S_INPUT:
	{
		int *input = arg, res = 0;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_S_INPUT - input=%d\n",
			ZR_DEVNAME(zr), *input);

		down(&zr->resource_lock);
		if ((res = zoran_set_input(zr, *input)))
			goto sinput_unlock_and_return;

		/* Make sure the changes come into effect */
		res = wait_grab_pending(zr);
	sinput_unlock_and_return:
		up(&zr->resource_lock);
		return res;
	}
		break;

	case VIDIOC_ENUMOUTPUT:
	{
		struct v4l2_output *outp = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_ENUMOUTPUT - index=%d\n",
			ZR_DEVNAME(zr), outp->index);

		if (outp->index != 0)
			return -EINVAL;

		memset(outp, 0, sizeof(*outp));
		outp->index = 0;
		outp->type = V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY;
		strncpy(outp->name, "Autodetect", 31);

		return 0;
	}
		break;

	case VIDIOC_G_OUTPUT:
	{
		int *output = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_G_OUTPUT\n", ZR_DEVNAME(zr));

		*output = 0;

		return 0;
	}
		break;

	case VIDIOC_S_OUTPUT:
	{
		int *output = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_S_OUTPUT - output=%d\n",
			ZR_DEVNAME(zr), *output);

		if (*output != 0)
			return -EINVAL;

		return 0;
	}
		break;

		/* cropping (sub-frame capture) */
	case VIDIOC_CROPCAP:
	{
		struct v4l2_cropcap *cropcap = arg;
		int type = cropcap->type, res = 0;

		dprintk(3, KERN_ERR "%s: VIDIOC_CROPCAP - type=%d\n",
			ZR_DEVNAME(zr), cropcap->type);

		memset(cropcap, 0, sizeof(*cropcap));
		cropcap->type = type;

		down(&zr->resource_lock);

		if (cropcap->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
		    (cropcap->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
		     fh->map_mode == ZORAN_MAP_MODE_RAW)) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_CROPCAP - subcapture only supported for compressed capture\n",
				ZR_DEVNAME(zr));
			res = -EINVAL;
			goto cropcap_unlock_and_return;
		}

		cropcap->bounds.top = cropcap->bounds.left = 0;
		cropcap->bounds.width = BUZ_MAX_WIDTH;
		cropcap->bounds.height = BUZ_MAX_HEIGHT;
		cropcap->defrect.top = cropcap->defrect.left = 0;
		cropcap->defrect.width = BUZ_MIN_WIDTH;
		cropcap->defrect.height = BUZ_MIN_HEIGHT;
	cropcap_unlock_and_return:
		up(&zr->resource_lock);
		return res;
	}
		break;

	case VIDIOC_G_CROP:
	{
		struct v4l2_crop *crop = arg;
		int type = crop->type, res = 0;

		dprintk(3, KERN_ERR "%s: VIDIOC_G_CROP - type=%d\n",
			ZR_DEVNAME(zr), crop->type);

		memset(crop, 0, sizeof(*crop));
		crop->type = type;

		down(&zr->resource_lock);

		if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
		    (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
		     fh->map_mode == ZORAN_MAP_MODE_RAW)) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_G_CROP - subcapture only supported for compressed capture\n",
				ZR_DEVNAME(zr));
			res = -EINVAL;
			goto gcrop_unlock_and_return;
		}

		crop->c.top = fh->jpg_settings.img_y;
		crop->c.left = fh->jpg_settings.img_x;
		crop->c.width = fh->jpg_settings.img_width;
		crop->c.height = fh->jpg_settings.img_height;

	gcrop_unlock_and_return:
		up(&zr->resource_lock);

		return res;
	}
		break;

	case VIDIOC_S_CROP:
	{
		struct v4l2_crop *crop = arg;
		int res = 0;

		settings = fh->jpg_settings;

		dprintk(3,
			KERN_ERR
			"%s: VIDIOC_S_CROP - type=%d, x=%d,y=%d,w=%d,h=%d\n",
			ZR_DEVNAME(zr), crop->type, crop->c.left, crop->c.top,
			crop->c.width, crop->c.height);

		down(&zr->resource_lock);

		if (fh->jpg_buffers.allocated || fh->v4l_buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_S_CROP - cannot change settings while active\n",
				ZR_DEVNAME(zr));
			res = -EBUSY;
			goto scrop_unlock_and_return;
		}

		if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
		    (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
		     fh->map_mode == ZORAN_MAP_MODE_RAW)) {
			dprintk(1,
				KERN_ERR
				"%s: VIDIOC_G_CROP - subcapture only supported for compressed capture\n",
				ZR_DEVNAME(zr));
			res = -EINVAL;
			goto scrop_unlock_and_return;
		}

		/* move into a form that we understand */
		settings.img_x = crop->c.left;
		settings.img_y = crop->c.top;
		settings.img_width = crop->c.width;
		settings.img_height = crop->c.height;

		/* check validity */
		if ((res = zoran_check_jpg_settings(zr, &settings)))
			goto scrop_unlock_and_return;

		/* accept */
		fh->jpg_settings = settings;

	scrop_unlock_and_return:
		up(&zr->resource_lock);
		return res;
	}
		break;

	case VIDIOC_G_JPEGCOMP:
	{
		struct v4l2_jpegcompression *params = arg;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_G_JPEGCOMP\n",
			ZR_DEVNAME(zr));

		memset(params, 0, sizeof(*params));

		down(&zr->resource_lock);

		params->quality = fh->jpg_settings.jpg_comp.quality;
		params->APPn = fh->jpg_settings.jpg_comp.APPn;
		memcpy(params->APP_data,
		       fh->jpg_settings.jpg_comp.APP_data,
		       fh->jpg_settings.jpg_comp.APP_len);
		params->APP_len = fh->jpg_settings.jpg_comp.APP_len;
		memcpy(params->COM_data,
		       fh->jpg_settings.jpg_comp.COM_data,
		       fh->jpg_settings.jpg_comp.COM_len);
		params->COM_len = fh->jpg_settings.jpg_comp.COM_len;
		params->jpeg_markers =
		    fh->jpg_settings.jpg_comp.jpeg_markers;

		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOC_S_JPEGCOMP:
	{
		struct v4l2_jpegcompression *params = arg;
		int res = 0;

		settings = fh->jpg_settings;

		dprintk(3,
			KERN_DEBUG
			"%s: VIDIOC_S_JPEGCOMP - quality=%d, APPN=%d, APP_len=%d, COM_len=%d\n",
			ZR_DEVNAME(zr), params->quality, params->APPn,
			params->APP_len, params->COM_len);

		settings.jpg_comp = *params;

		down(&zr->resource_lock);

		if (fh->v4l_buffers.active != ZORAN_FREE ||
		    fh->jpg_buffers.active != ZORAN_FREE) {
			dprintk(1,
				KERN_WARNING
				"%s: VIDIOC_S_JPEGCOMP called while in playback/capture mode\n",
				ZR_DEVNAME(zr));
			res = -EBUSY;
			goto sjpegc_unlock_and_return;
		}

		if ((res = zoran_check_jpg_settings(zr, &settings)))
			goto sjpegc_unlock_and_return;
		if (!fh->jpg_buffers.allocated)
			fh->jpg_buffers.buffer_size =
			    zoran_v4l2_calc_bufsize(&fh->jpg_settings);
		fh->jpg_settings.jpg_comp = *params = settings.jpg_comp;
	sjpegc_unlock_and_return:
		up(&zr->resource_lock);

		return 0;
	}
		break;

	case VIDIOC_QUERYSTD:	/* why is this useful? */
	{
		v4l2_std_id *std = arg;

		dprintk(3,
			KERN_DEBUG "%s: VIDIOC_QUERY_STD - std=0x%llx\n",
			ZR_DEVNAME(zr), (unsigned long long)*std);

		if (*std == V4L2_STD_ALL || *std == V4L2_STD_NTSC ||
		    *std == V4L2_STD_PAL || (*std == V4L2_STD_SECAM &&
					     zr->card.norms == 3)) {
			return 0;
		}

		return -EINVAL;
	}
		break;

	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *fmt = arg;
		int res = 0;

		dprintk(3, KERN_DEBUG "%s: VIDIOC_TRY_FMT - type=%d\n",
			ZR_DEVNAME(zr), fmt->type);

		switch (fmt->type) {
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:
			down(&zr->resource_lock);

			if (fmt->fmt.win.w.width > BUZ_MAX_WIDTH)
				fmt->fmt.win.w.width = BUZ_MAX_WIDTH;
			if (fmt->fmt.win.w.width < BUZ_MIN_WIDTH)
				fmt->fmt.win.w.width = BUZ_MIN_WIDTH;
			if (fmt->fmt.win.w.height > BUZ_MAX_HEIGHT)
				fmt->fmt.win.w.height = BUZ_MAX_HEIGHT;
			if (fmt->fmt.win.w.height < BUZ_MIN_HEIGHT)
				fmt->fmt.win.w.height = BUZ_MIN_HEIGHT;

			up(&zr->resource_lock);
			break;

		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			if (fmt->fmt.pix.bytesperline > 0)
				return -EINVAL;

			down(&zr->resource_lock);

			if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
				settings = fh->jpg_settings;

				/* we actually need to set 'real' parameters now */
				if ((fmt->fmt.pix.height * 2) >
				    BUZ_MAX_HEIGHT)
					settings.TmpDcm = 1;
				else
					settings.TmpDcm = 2;
				settings.decimation = 0;
				if (fmt->fmt.pix.height <=
				    fh->jpg_settings.img_height / 2)
					settings.VerDcm = 2;
				else
					settings.VerDcm = 1;
				if (fmt->fmt.pix.width <=
				    fh->jpg_settings.img_width / 4)
					settings.HorDcm = 4;
				else if (fmt->fmt.pix.width <=
					 fh->jpg_settings.img_width / 2)
					settings.HorDcm = 2;
				else
					settings.HorDcm = 1;
				if (settings.TmpDcm == 1)
					settings.field_per_buff = 2;
				else
					settings.field_per_buff = 1;

				/* check */
				if ((res =
				     zoran_check_jpg_settings(zr,
							      &settings)))
					goto tryfmt_unlock_and_return;

				/* tell the user what we actually did */
				fmt->fmt.pix.width =
				    settings.img_width / settings.HorDcm;
				fmt->fmt.pix.height =
				    settings.img_height * 2 /
				    (settings.TmpDcm * settings.VerDcm);
				if (settings.TmpDcm == 1)
					fmt->fmt.pix.field =
					    (fh->jpg_settings.
					     odd_even ? V4L2_FIELD_SEQ_TB :
					     V4L2_FIELD_SEQ_BT);
				else
					fmt->fmt.pix.field =
					    (fh->jpg_settings.
					     odd_even ? V4L2_FIELD_TOP :
					     V4L2_FIELD_BOTTOM);

				fmt->fmt.pix.sizeimage =
				    zoran_v4l2_calc_bufsize(&settings);
			} else if (fmt->type ==
				   V4L2_BUF_TYPE_VIDEO_CAPTURE) {
				int i;

				for (i = 0; i < zoran_num_formats; i++)
					if (zoran_formats[i].fourcc ==
					    fmt->fmt.pix.pixelformat)
						break;
				if (i == zoran_num_formats) {
					res = -EINVAL;
					goto tryfmt_unlock_and_return;
				}

				if (fmt->fmt.pix.width > BUZ_MAX_WIDTH)
					fmt->fmt.pix.width = BUZ_MAX_WIDTH;
				if (fmt->fmt.pix.width < BUZ_MIN_WIDTH)
					fmt->fmt.pix.width = BUZ_MIN_WIDTH;
				if (fmt->fmt.pix.height > BUZ_MAX_HEIGHT)
					fmt->fmt.pix.height =
					    BUZ_MAX_HEIGHT;
				if (fmt->fmt.pix.height < BUZ_MIN_HEIGHT)
					fmt->fmt.pix.height =
					    BUZ_MIN_HEIGHT;
			} else {
				res = -EINVAL;
				goto tryfmt_unlock_and_return;
			}
		tryfmt_unlock_and_return:
			up(&zr->resource_lock);

			return res;
			break;

		default:
			return -EINVAL;
		}

		return 0;
	}
		break;
#endif

	default:
		dprintk(1, KERN_DEBUG "%s: UNKNOWN ioctl cmd: 0x%x\n",
			ZR_DEVNAME(zr), cmd);
		return -ENOIOCTLCMD;
		break;

	}
	return 0;
}


static int
zoran_ioctl (struct inode *inode,
	     struct file  *file,
	     unsigned int  cmd,
	     unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, zoran_do_ioctl);
}

static unsigned int
zoran_poll (struct file *file,
	    poll_table  *wait)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	wait_queue_head_t *queue = NULL;
	int res = 0, frame;

	/* we should check whether buffers are ready to be synced on
	 * (w/o waits - O_NONBLOCK) here
	 * if ready for read (sync), return POLLIN|POLLRDNORM,
	 * if ready for write (sync), return POLLOUT|POLLWRNORM,
	 * if error, return POLLERR,
	 * if no buffers queued or so, return POLLNVAL
	 */

	down(&zr->resource_lock);

	switch (fh->map_mode) {
	case ZORAN_MAP_MODE_RAW:
		if (fh->v4l_buffers.active == ZORAN_FREE ||
		    zr->v4l_pend_head == zr->v4l_pend_tail) {
			dprintk(1,
				"%s: zoran_poll() - no buffers queued\n",
				ZR_DEVNAME(zr));
			res = POLLNVAL;
			goto poll_unlock_and_return;
		}
		queue = &zr->v4l_capq;
		frame = zr->v4l_pend[zr->v4l_pend_tail & V4L_MASK_FRAME];
		poll_wait(file, queue, wait);
		if (fh->v4l_buffers.buffer[frame].state == BUZ_STATE_DONE)
			res = POLLIN | POLLRDNORM;
		break;

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:
		if (fh->jpg_buffers.active == ZORAN_FREE ||
		    zr->jpg_que_head == zr->jpg_que_tail) {
			dprintk(1,
				"%s: zoran_poll() - no buffers queued\n",
				ZR_DEVNAME(zr));
			res = POLLNVAL;
			goto poll_unlock_and_return;
		}
		queue = &zr->jpg_capq;
		frame = zr->jpg_pend[zr->jpg_que_tail & BUZ_MASK_FRAME];
		poll_wait(file, queue, wait);
		if (fh->jpg_buffers.buffer[frame].state == BUZ_STATE_DONE) {
			if (fh->map_mode == ZORAN_MAP_MODE_JPG_REC)
				res = POLLIN | POLLRDNORM;
			else
				res = POLLOUT | POLLWRNORM;
		}
		break;

	default:
		dprintk(1,
			"%s: zoran_poll() - internal error, unknown map_mode=%d\n",
			ZR_DEVNAME(zr), fh->map_mode);
		res = POLLNVAL;
		goto poll_unlock_and_return;
	}

poll_unlock_and_return:
	up(&zr->resource_lock);

	return res;
}


/*
 * This maps the buffers to user space.
 *
 * Depending on the state of fh->map_mode
 * the V4L or the MJPEG buffers are mapped
 * per buffer or all together
 *
 * Note that we need to connect to some
 * unmap signal event to unmap the de-allocate
 * the buffer accordingly (zoran_vm_close())
 */

static void
zoran_vm_open (struct vm_area_struct *vma)
{
	struct zoran_mapping *map = vma->vm_private_data;

	map->count++;
}

static void
zoran_vm_close (struct vm_area_struct *vma)
{
	struct zoran_mapping *map = vma->vm_private_data;
	struct file *file = map->file;
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i;

	map->count--;
	if (map->count == 0) {
		switch (fh->map_mode) {
		case ZORAN_MAP_MODE_JPG_REC:
		case ZORAN_MAP_MODE_JPG_PLAY:

			dprintk(3, KERN_INFO "%s: munmap(MJPEG)\n",
				ZR_DEVNAME(zr));

			for (i = 0; i < fh->jpg_buffers.num_buffers; i++) {
				if (fh->jpg_buffers.buffer[i].map == map) {
					fh->jpg_buffers.buffer[i].map =
					    NULL;
				}
			}
			kfree(map);

			for (i = 0; i < fh->jpg_buffers.num_buffers; i++)
				if (fh->jpg_buffers.buffer[i].map)
					break;
			if (i == fh->jpg_buffers.num_buffers) {
				down(&zr->resource_lock);

				if (fh->jpg_buffers.active != ZORAN_FREE) {
					jpg_qbuf(file, -1, zr->codec_mode);
					zr->jpg_buffers.allocated = 0;
					zr->jpg_buffers.active =
					    fh->jpg_buffers.active =
					    ZORAN_FREE;
				}
				//jpg_fbuffer_free(file);
				fh->jpg_buffers.allocated = 0;
				fh->jpg_buffers.ready_to_be_freed = 1;

				up(&zr->resource_lock);
			}

			break;

		case ZORAN_MAP_MODE_RAW:

			dprintk(3, KERN_INFO "%s: munmap(V4L)\n",
				ZR_DEVNAME(zr));

			for (i = 0; i < fh->v4l_buffers.num_buffers; i++) {
				if (fh->v4l_buffers.buffer[i].map == map) {
					/* unqueue/unmap */
					fh->v4l_buffers.buffer[i].map =
					    NULL;
				}
			}
			kfree(map);

			for (i = 0; i < fh->v4l_buffers.num_buffers; i++)
				if (fh->v4l_buffers.buffer[i].map)
					break;
			if (i == fh->v4l_buffers.num_buffers) {
				down(&zr->resource_lock);

				if (fh->v4l_buffers.active != ZORAN_FREE) {
					zr36057_set_memgrab(zr, 0);
					zr->v4l_buffers.allocated = 0;
					zr->v4l_buffers.active =
					    fh->v4l_buffers.active =
					    ZORAN_FREE;
				}
				//v4l_fbuffer_free(file);
				fh->v4l_buffers.allocated = 0;
				fh->v4l_buffers.ready_to_be_freed = 1;

				up(&zr->resource_lock);
			}

			break;

		default:
			printk(KERN_ERR
			       "%s: munmap() - internal error - unknown map mode %d\n",
			       ZR_DEVNAME(zr), fh->map_mode);
			break;

		}
	}
}

static struct vm_operations_struct zoran_vm_ops = {
	.open = zoran_vm_open,
	.close = zoran_vm_close,
};

static int
zoran_mmap (struct file           *file,
	    struct vm_area_struct *vma)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long size = (vma->vm_end - vma->vm_start);
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int i, j;
	unsigned long page, start = vma->vm_start, todo, pos, fraglen;
	int first, last;
	struct zoran_mapping *map;
	int res = 0;

	dprintk(3,
		KERN_INFO "%s: mmap(%s) of 0x%08lx-0x%08lx (size=%lu)\n",
		ZR_DEVNAME(zr),
		fh->map_mode == ZORAN_MAP_MODE_RAW ? "V4L" : "MJPEG",
		vma->vm_start, vma->vm_end, size);

	if (!(vma->vm_flags & VM_SHARED) || !(vma->vm_flags & VM_READ) ||
	    !(vma->vm_flags & VM_WRITE)) {
		dprintk(1,
			KERN_ERR
			"%s: mmap() - no MAP_SHARED/PROT_{READ,WRITE} given\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	switch (fh->map_mode) {

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:

		/* lock */
		down(&zr->resource_lock);

		/* Map the MJPEG buffers */
		if (!fh->jpg_buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: zoran_mmap(MJPEG) - buffers not yet allocated\n",
				ZR_DEVNAME(zr));
			res = -ENOMEM;
			goto jpg_mmap_unlock_and_return;
		}

		first = offset / fh->jpg_buffers.buffer_size;
		last = first - 1 + size / fh->jpg_buffers.buffer_size;
		if (offset % fh->jpg_buffers.buffer_size != 0 ||
		    size % fh->jpg_buffers.buffer_size != 0 || first < 0 ||
		    last < 0 || first >= fh->jpg_buffers.num_buffers ||
		    last >= fh->jpg_buffers.num_buffers) {
			dprintk(1,
				KERN_ERR
				"%s: mmap(MJPEG) - offset=%lu or size=%lu invalid for bufsize=%d and numbufs=%d\n",
				ZR_DEVNAME(zr), offset, size,
				fh->jpg_buffers.buffer_size,
				fh->jpg_buffers.num_buffers);
			res = -EINVAL;
			goto jpg_mmap_unlock_and_return;
		}
		for (i = first; i <= last; i++) {
			if (fh->jpg_buffers.buffer[i].map) {
				dprintk(1,
					KERN_ERR
					"%s: mmap(MJPEG) - buffer %d already mapped\n",
					ZR_DEVNAME(zr), i);
				res = -EBUSY;
				goto jpg_mmap_unlock_and_return;
			}
		}

		/* map these buffers (v4l_buffers[i]) */
		map = kmalloc(sizeof(struct zoran_mapping), GFP_KERNEL);
		if (!map) {
			res = -ENOMEM;
			goto jpg_mmap_unlock_and_return;
		}
		map->file = file;
		map->count = 1;

		vma->vm_ops = &zoran_vm_ops;
		vma->vm_flags |= VM_DONTEXPAND;
		vma->vm_private_data = map;

		for (i = first; i <= last; i++) {
			for (j = 0;
			     j < fh->jpg_buffers.buffer_size / PAGE_SIZE;
			     j++) {
				fraglen =
				    (le32_to_cpu(fh->jpg_buffers.buffer[i].
				     frag_tab[2 * j + 1]) & ~1) << 1;
				todo = size;
				if (todo > fraglen)
					todo = fraglen;
				pos =
				    le32_to_cpu((unsigned long) fh->jpg_buffers.
				    buffer[i].frag_tab[2 * j]);
				/* should just be pos on i386 */
				page = virt_to_phys(bus_to_virt(pos))
								>> PAGE_SHIFT;
				if (remap_pfn_range(vma, start, page,
							todo, PAGE_SHARED)) {
					dprintk(1,
						KERN_ERR
						"%s: zoran_mmap(V4L) - remap_pfn_range failed\n",
						ZR_DEVNAME(zr));
					res = -EAGAIN;
					goto jpg_mmap_unlock_and_return;
				}
				size -= todo;
				start += todo;
				if (size == 0)
					break;
				if (le32_to_cpu(fh->jpg_buffers.buffer[i].
				    frag_tab[2 * j + 1]) & 1)
					break;	/* was last fragment */
			}
			fh->jpg_buffers.buffer[i].map = map;
			if (size == 0)
				break;

		}
	jpg_mmap_unlock_and_return:
		up(&zr->resource_lock);

		break;

	case ZORAN_MAP_MODE_RAW:

		down(&zr->resource_lock);

		/* Map the V4L buffers */
		if (!fh->v4l_buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: zoran_mmap(V4L) - buffers not yet allocated\n",
				ZR_DEVNAME(zr));
			res = -ENOMEM;
			goto v4l_mmap_unlock_and_return;
		}

		first = offset / fh->v4l_buffers.buffer_size;
		last = first - 1 + size / fh->v4l_buffers.buffer_size;
		if (offset % fh->v4l_buffers.buffer_size != 0 ||
		    size % fh->v4l_buffers.buffer_size != 0 || first < 0 ||
		    last < 0 || first >= fh->v4l_buffers.num_buffers ||
		    last >= fh->v4l_buffers.buffer_size) {
			dprintk(1,
				KERN_ERR
				"%s: mmap(V4L) - offset=%lu or size=%lu invalid for bufsize=%d and numbufs=%d\n",
				ZR_DEVNAME(zr), offset, size,
				fh->v4l_buffers.buffer_size,
				fh->v4l_buffers.num_buffers);
			res = -EINVAL;
			goto v4l_mmap_unlock_and_return;
		}
		for (i = first; i <= last; i++) {
			if (fh->v4l_buffers.buffer[i].map) {
				dprintk(1,
					KERN_ERR
					"%s: mmap(V4L) - buffer %d already mapped\n",
					ZR_DEVNAME(zr), i);
				res = -EBUSY;
				goto v4l_mmap_unlock_and_return;
			}
		}

		/* map these buffers (v4l_buffers[i]) */
		map = kmalloc(sizeof(struct zoran_mapping), GFP_KERNEL);
		if (!map) {
			res = -ENOMEM;
			goto v4l_mmap_unlock_and_return;
		}
		map->file = file;
		map->count = 1;

		vma->vm_ops = &zoran_vm_ops;
		vma->vm_flags |= VM_DONTEXPAND;
		vma->vm_private_data = map;

		for (i = first; i <= last; i++) {
			todo = size;
			if (todo > fh->v4l_buffers.buffer_size)
				todo = fh->v4l_buffers.buffer_size;
			page = fh->v4l_buffers.buffer[i].fbuffer_phys;
			if (remap_pfn_range(vma, start, page >> PAGE_SHIFT,
							todo, PAGE_SHARED)) {
				dprintk(1,
					KERN_ERR
					"%s: zoran_mmap(V4L)i - remap_pfn_range failed\n",
					ZR_DEVNAME(zr));
				res = -EAGAIN;
				goto v4l_mmap_unlock_and_return;
			}
			size -= todo;
			start += todo;
			fh->v4l_buffers.buffer[i].map = map;
			if (size == 0)
				break;
		}
	v4l_mmap_unlock_and_return:
		up(&zr->resource_lock);

		break;

	default:
		dprintk(1,
			KERN_ERR
			"%s: zoran_mmap() - internal error - unknown map mode %d\n",
			ZR_DEVNAME(zr), fh->map_mode);
		break;
	}

	return 0;
}

static struct file_operations zoran_fops = {
	.owner = THIS_MODULE,
	.open = zoran_open,
	.release = zoran_close,
	.ioctl = zoran_ioctl,
	.llseek = no_llseek,
	.read = zoran_read,
	.write = zoran_write,
	.mmap = zoran_mmap,
	.poll = zoran_poll,
};

struct video_device zoran_template __devinitdata = {
	.name = ZORAN_NAME,
	.type = ZORAN_VID_TYPE,
#ifdef HAVE_V4L2
	.type2 = ZORAN_V4L2_VID_FLAGS,
#endif
	.hardware = ZORAN_HARDWARE,
	.fops = &zoran_fops,
	.release = &zoran_vdev_release,
	.minor = -1
};

