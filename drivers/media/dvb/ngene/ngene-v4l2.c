/*
 * ngene_v4l2.c: nGene PCIe bridge driver V4L2 support
 *
 * Copyright (C) 2005-2007 Micronas
 *
 * Based on the initial V4L2 support port by Thomas Eschbach.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/videodev2.h>
#include <linux/videodev.h>
#include <linux/version.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/kmap_types.h>
#include <linux/videodev.h>

#include <media/v4l2-dev.h>

#include "ngene.h"
#include "ngene-ioctls.h"

/****************************************************************************/

static unsigned int gbuffers = 8;
static unsigned int gbufsize = 0x208000;

enum km_type ngene_km_types[] = {
	KM_USER0,
	KM_USER1,
	KM_SOFTIRQ0,
	KM_SOFTIRQ1,
};

#define V4L2_STD_NTSC_M_KOREA          ((v4l2_std_id)0x00004000)
#define V4L2_STD_SECAM_L1              ((v4l2_std_id)0x00008000)

static inline void *my_video_get_drvdata(struct video_device *vd)
{
	return dev_get_drvdata(vd->dev);
}

static inline void my_video_set_drvdata(struct video_device *vd, void *d)
{
	dev_set_drvdata(vd->dev, d);
}

static struct ngene_tvnorm ngene_tvnorms_hd[] = {
	{
		.v4l2_id        = V4L2_STD_PAL_BG,
		.name           = "1080i50",
		.swidth         = 1920,
		.sheight        = 1080,
		.tuner_norm     = 1,
		.soundstd       = 1,
	}
};

static struct ngene_tvnorm ngene_tvnorms_sd[] = {
	/* PAL-BDGHI */
	/* max. active video is actually 922, but 924 is divisible by 4 & 3!*/
	/* actually, max active PAL with HSCALE=0 is 948, NTSC is 768 - nil */
	{
		.v4l2_id        = V4L2_STD_PAL_BG,
		.name           = "PAL-BG",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 1,
		.soundstd       = 1,
	}, {
		.v4l2_id        = V4L2_STD_PAL_DK,
		.name           = "PAL-DK",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 2,
		.soundstd       = 2,
	}, {
		.v4l2_id        = V4L2_STD_PAL_H,
		.name           = "PAL-H",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 0,
		.soundstd       = 1,
	}, {
		.v4l2_id        = V4L2_STD_PAL_I,
		.name           = "PAL-I",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 4,
		.soundstd       = 4,
	}, {
		.v4l2_id        = V4L2_STD_PAL_M,
		.name           = "PAL_M",
		.swidth         = 720,
		.sheight        = 5760,
		.tuner_norm     = 7,
		.soundstd       = 5,
	}, {
		.v4l2_id        = V4L2_STD_NTSC_M,
		.name           = "NTSC_M",
		.swidth         = 720,
		.sheight        = 480,
		.tuner_norm     = 7,
		.soundstd       = 5,
	}, {
		.v4l2_id        = V4L2_STD_NTSC_M_JP,
		.name           = "NTSC_M_JP",
		.swidth         = 720,
		.sheight        = 480,
		.tuner_norm     = 7,
		.soundstd       = 6,
	}, {
		.v4l2_id        = V4L2_STD_PAL_N,
		.name           = "PAL-N",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 7,
		.soundstd       = 5,
	}, {
		.v4l2_id        = V4L2_STD_SECAM_B,
		.name           = "SECAM_B",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 1,
		.soundstd       = 1,
	}, {
		.v4l2_id        = V4L2_STD_SECAM_D,
		.name           = "SECAM_D",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 2,
		.soundstd       = 2,
	}, {
		.v4l2_id        = V4L2_STD_SECAM_G,
		.name           = "SECAM_G",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 3,
		.soundstd       = 1,
	}, {
		.v4l2_id        = V4L2_STD_SECAM_H,
		.name           = "SECAM_H",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 3,
		.soundstd       = 1,
	}, {
		.v4l2_id        = V4L2_STD_SECAM_K,
		.name           = "SECAM_K",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 2,
		.soundstd       = 2,
	}, {
		.v4l2_id        = V4L2_STD_SECAM_K1,
		.name           = "SECAM_K1",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 2,
		.soundstd       = 2,
	}, {
		.v4l2_id        = V4L2_STD_SECAM_L,
		.name           = "SECAM_L",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 5,
		.soundstd       = 3,
	}, {
		.v4l2_id        = V4L2_STD_NTSC_M_KOREA,
		.name           = "NTSC_M_KOREA",
		.swidth         = 720,
		.sheight        = 480,
		.tuner_norm     = 7,
		.soundstd       = 7,
	}, {
		.v4l2_id        = V4L2_STD_SECAM_L1,
		.name           = "SECAM_L1",
		.swidth         = 720,
		.sheight        = 576,
		.tuner_norm     = 6,
		.soundstd       = 3,
	}

};

static const int NGENE_TVNORMS = ARRAY_SIZE(ngene_tvnorms_sd);

static u8 BlackLine[1440] = {
	/* 0x80, */ 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,

	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80, 0x10, 0x80, 0x10, 0x80, 0x10, 0x80, 0x10,
	0x80,
};

#define V4L2_CID_PRIVATE_SHARPNESS   (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PRIVATE_LASTP1      (V4L2_CID_PRIVATE_BASE + 1)

static const struct v4l2_queryctrl no_ctl = {
	.name  = "no_ctl",
	.flags = V4L2_CTRL_FLAG_DISABLED,
};

static const struct v4l2_queryctrl ngene_ctls[] = {
	/* --- video --- */
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.name          = "Brightness",
		.minimum       = -127,
		.maximum       = 127,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 63,
		.step          = 1,
		.default_value = 30,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id            = V4L2_CID_SATURATION,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 4094,
		.step          = 1,
		.default_value = 2000,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id            = V4L2_CID_HUE,
		.name          = "Hue",
		.minimum       = -2047,
		.maximum       = 2047,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},
	/* --- audio --- */
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	}, {
		.id            = V4L2_CID_PRIVATE_SHARPNESS,
		.name          = "sharpness",
		.minimum       = 0,
		.maximum       = 100,
		.step          = 1,
		.default_value = 50,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},

};

static const int NGENE_CTLS = ARRAY_SIZE(ngene_ctls);

static const struct ngene_format ngene_formats[] = {
	{
		.name     = "4:2:2, packed, YUYV",
		.palette  = -1,
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.format   = V4L2_PIX_FMT_YUYV,
		.palette  = VIDEO_PALETTE_YUYV,
		.depth    = 16,
		.flags    = 0x02,/* FORMAT_FLAGS_PACKED, */
	}
};

static const unsigned int NGENE_FORMATS = ARRAY_SIZE(ngene_formats);

/****************************************************************************/

static struct videobuf_queue *ngene_queue(struct ngene_vopen *vopen)
{
	struct videobuf_queue *q = NULL;

	switch (vopen->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		q = &vopen->vbuf_q;
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		q = &vopen->vbi;
		break;
	default:
		break;
	}
	return q;
}

static int ngene_resource(struct ngene_vopen *vopen)
{
	int res = 0;

	switch (vopen->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		res = RESOURCE_VIDEO;
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		res = RESOURCE_VBI;
		break;
	default:
		break;
	}
	return res;
}

static int ngene_try_fmt(struct ngene_vopen *vopen, struct ngene_channel *chan,
			 struct v4l2_format *f)
{
	switch (f->type) {

	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	{
		const struct ngene_format *fmt;
		enum v4l2_field field;
		unsigned int maxw, maxh;
		int maxLinesPerField;

		fmt = ngene_formats;
		if (NULL == fmt)
			return -EINVAL;

		/* fixup format */
		maxw  = chan->tvnorms[chan->tvnorm].swidth;
		maxLinesPerField = chan->tvnorms[chan->tvnorm].sheight;
		maxh  = maxLinesPerField;
		field = f->fmt.pix.field;

		if (V4L2_FIELD_ANY == field)
			field = (f->fmt.pix.height > maxh / 2)
				? V4L2_FIELD_INTERLACED : V4L2_FIELD_BOTTOM;

		if (V4L2_FIELD_SEQ_BT == field)
			field = V4L2_FIELD_SEQ_TB;

		/* update data for the application */
		f->fmt.pix.field = field;
		if (f->fmt.pix.width < 48)
			f->fmt.pix.width = 48;
		if (f->fmt.pix.height < 32)
			f->fmt.pix.height = 32;
		if (f->fmt.pix.width > maxw)
			f->fmt.pix.width = maxw;
		if (f->fmt.pix.height > maxh)
			f->fmt.pix.height = maxh;
		f->fmt.pix.width &= ~0x03;
		f->fmt.pix.bytesperline =
			(f->fmt.pix.width * fmt->depth) >> 3;
		f->fmt.pix.sizeimage =
			f->fmt.pix.height * f->fmt.pix.bytesperline;

		return 0;
	}

	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return -EINVAL;

	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return 0;

	default:
		return -EINVAL;
	}
}

/****************************************************************************/
/* Analog driver stuff ******************************************************/
/****************************************************************************/

static int check_alloc_res(struct ngene_channel *channel,
			   struct ngene_vopen *vopen, int bit)
{
	if (vopen->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	down(&channel->reslock);
	if (channel->resources & bit) {
		/* no, someone else uses it */
		up(&channel->reslock);
		return 0;
	}
	/* it's free, grab it */
	vopen->resources |= bit;
	channel->resources |= bit;
	up(&channel->reslock);
	return 1;
}

static int check_res(struct ngene_vopen *vopen, int bit)
{
	return vopen->resources & bit;
}

static int locked_res(struct ngene_channel *chan, int bit)
{
	return chan->resources & bit;
}

static void free_res(struct ngene_channel *channel,
		     struct ngene_vopen *vopen, int bits)
{
	down(&channel->reslock);
	vopen->resources &= ~bits;
	channel->resources &= ~bits;
	up(&channel->reslock);
}

/****************************************************************************/
/* MISC HELPERS *************************************************************/
/****************************************************************************/

static int ngene_g_fmt(struct ngene_vopen *vopen, struct v4l2_format *f)
{
	if (!vopen->fmt)
		vopen->fmt = ngene_formats;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(&f->fmt.pix, 0, sizeof(struct v4l2_pix_format));
		f->fmt.pix.width        = vopen->width;
		f->fmt.pix.height       = vopen->height;
		f->fmt.pix.field        = vopen->vbuf_q.field;
		f->fmt.pix.pixelformat  = vopen->fmt->fourcc;
		f->fmt.pix.bytesperline = (f->fmt.pix.width * 16) >> 3;
		f->fmt.pix.sizeimage    =
			f->fmt.pix.height * f->fmt.pix.bytesperline;
		return 0;

	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		memset(&f->fmt.win, 0, sizeof(struct v4l2_window));
		return 0;
		f->fmt.win.w     = vopen->ov.w;
		f->fmt.win.field = vopen->ov.field;
		return 0;

	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return -EINVAL;

	default:
		return -EINVAL;
	}
}

static int ngene_switch_type(struct ngene_vopen *vopen, enum v4l2_buf_type type)
{
	struct videobuf_queue *q = ngene_queue(vopen);
	int res = ngene_resource(vopen);

	if (check_res(vopen, res))
		return -EBUSY;
	if (videobuf_queue_is_busy(q))
		return -EBUSY;
	vopen->type = type;
	return 0;
}

static int ngene_s_fmt(struct ngene_vopen *vopen, struct ngene_channel *chan,
		       struct v4l2_format *f)
{
	int retval;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	{
		const struct ngene_format *fmt;
		retval = ngene_try_fmt(vopen, chan, f);
		if (0 != retval)
			return retval;

		retval = ngene_switch_type(vopen, f->type);
		if (0 != retval)
			return retval;
		fmt = ngene_formats;

		if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)
			return -EINVAL;
		/* update our state informations */
		mutex_lock(&vopen->vbuf_q.lock);
		vopen->fmt          = fmt;
		vopen->vbuf_q.field = f->fmt.pix.field;
		vopen->vbuf_q.last  = V4L2_FIELD_INTERLACED;
		vopen->width        = f->fmt.pix.width;
		vopen->height       = f->fmt.pix.height;
		chan->init.fmt      = fmt;
		chan->init.width    = f->fmt.pix.width;
		chan->init.height   = f->fmt.pix.height;
		mutex_unlock(&vopen->vbuf_q.lock);

		return 0;
	}
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return -EINVAL;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

/****************************************************************************/
/* SG support ***************************************************************/
/****************************************************************************/

static inline enum km_type ngene_kmap_type(int out)
{
	return ngene_km_types[(in_softirq() ? 2 : 0) + out];
}

static inline void *ngene_kmap(struct page *page, int out)
{
	return kmap_atomic(page, ngene_kmap_type(out));
}

static inline void ngene_kunmap(void *vaddr, int out)
{
	kunmap_atomic(vaddr, ngene_kmap_type(out));
}

struct scatter_walk {
	struct scatterlist *sg;
	struct page        *page;
	void               *data;
	unsigned int        len_this_page;
	unsigned int        len_this_segment;
	unsigned int        offset;
};

static inline struct scatterlist *sg_next(struct scatterlist *sg)
{
	return sg + 1;
}

void *scatterwalk_whichbuf(struct scatter_walk *walk, unsigned int nbytes)
{
	if (nbytes <= walk->len_this_page &&
	    (((unsigned long)walk->data) &
	     (PAGE_CACHE_SIZE - 1)) + nbytes <= PAGE_CACHE_SIZE)
		return walk->data;
	else
		return walk->data;
}

void scatterwalk_start(struct scatter_walk *walk, struct scatterlist *sg)
{
	unsigned int rest_of_page;

	walk->sg = sg;
	walk->page = sg->page;
	walk->len_this_segment = sg->length;
	rest_of_page = PAGE_CACHE_SIZE - (sg->offset & (PAGE_CACHE_SIZE - 1));
	walk->len_this_page = min(sg->length, rest_of_page);
	walk->offset = sg->offset;
}

void scatterwalk_map(struct scatter_walk *walk, int out)
{
	walk->data = ngene_kmap(walk->page, out) + walk->offset;
}

static void scatterwalk_pagedone(struct scatter_walk *walk, int out,
				 unsigned int more)
{
	/* walk->data may be pointing the first byte of the next page;
	   however, we know we transfered at least one byte.  So,
	   walk->data - 1 will be a virtual address in the mapped page. */

	if (out)
		flush_dcache_page(walk->page);

	if (more) {
		walk->len_this_segment -= walk->len_this_page;

		if (walk->len_this_segment) {
			walk->page++;
			walk->len_this_page = min(walk->len_this_segment,
						  (unsigned)PAGE_CACHE_SIZE);
			walk->offset = 0;
		} else {
			scatterwalk_start(walk, sg_next(walk->sg));
		}
	}
}

void scatterwalk_done(struct scatter_walk *walk, int out, int more)
{
	ngene_kunmap(walk->data, out);
	if (walk->len_this_page == 0 || !more)
		scatterwalk_pagedone(walk, out, more);
}

/*
 * Do not call this unless the total length of all of the fragments
 * has been verified as multiple of the block size.
 */
int scatterwalk_copychunks(struct scatter_walk *walk, size_t nbytes, int out)
{
	walk->offset += nbytes;
	walk->len_this_page -= nbytes;
	walk->len_this_segment -= nbytes;
	return 0;
}

static void *vid_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags)
{
	struct ngene_channel *chan = priv;
	struct ngene_buffer *item;
	int wstrich, hstrich;
	u8 *odd, *even;
	u32 bpl = chan->tvnorms[chan->tvnorm].swidth * 2;

	struct scatter_walk walk_out;
	const unsigned int bsize = PAGE_SIZE;
	unsigned int nbytes;
	int rest_of_buffer, ah, rwstrich;

	spin_lock(&chan->s_lock);

	if (list_empty(&chan->capture)) {
		chan->evenbuffer = NULL;
		goto out;
	}
	item = list_entry(chan->capture.next, struct ngene_buffer, vb.queue);

	if (chan->tvnorms[chan->tvnorm].sheight == 1080)
		buf += 3840;

	odd = buf;

	hstrich = item->vb.height;
	if (hstrich > chan->tvnorms[chan->tvnorm].sheight)
		hstrich = chan->tvnorms[chan->tvnorm].sheight;

	wstrich = item->vb.width;
	if (wstrich > chan->tvnorms[chan->tvnorm].swidth)
		wstrich = chan->tvnorms[chan->tvnorm].swidth;
	wstrich <<= 1;

	if (flags & BEF_EVEN_FIELD) {
		chan->evenbuffer = buf;
		if (chan->lastbufferflag) {
			chan->lastbufferflag = 0;
			if (chan->tvnorms[chan->tvnorm].sheight == 576) {
				memcpy(buf + 413280, BlackLine, 1440);
				memcpy(buf + 411840, BlackLine, 1440);
			}
			goto out;
		}
	}
	chan->lastbufferflag = 1;
	if (chan->evenbuffer)
		even = chan->evenbuffer;
	else
		even = odd;
	if (chan->tvnorms[chan->tvnorm].sheight == 576) {
		memcpy(odd + 413280, BlackLine, 1440);
		memcpy(odd + 411840, BlackLine, 1440);
	}
	nbytes = item->vb.dma.sglen * PAGE_SIZE;
	scatterwalk_start(&walk_out, item->vb.dma.sglist);
	ah = 0;
	rwstrich = wstrich;
	do {
		u8 *dst_p;

		rest_of_buffer = bsize;
		scatterwalk_map(&walk_out, 1);
		dst_p = scatterwalk_whichbuf(&walk_out, bsize);
		nbytes -= bsize;
		scatterwalk_copychunks(&walk_out, bsize, 1);

		while (rest_of_buffer > 0 && ah < hstrich) {
			if (rest_of_buffer >= rwstrich) {
				if (ah % 2 == 0) {
					memcpy(walk_out.data +
					       (bsize - rest_of_buffer),
					       odd, rwstrich);
					odd += bpl - (wstrich - rwstrich);
				} else {
					memcpy(walk_out.data +
					       (bsize - rest_of_buffer),
					       even, rwstrich);
					even += bpl - (wstrich - rwstrich);
				}
				rest_of_buffer -= rwstrich;
				ah++;
				rwstrich = wstrich;
			} else {
				if (ah % 2 == 0) {
					memcpy(walk_out.data +
					       (bsize - rest_of_buffer),
					       odd, rest_of_buffer);
					odd += rest_of_buffer;
				} else {
					memcpy(walk_out.data +
					       (bsize - rest_of_buffer),
					       even, rest_of_buffer);
					even += rest_of_buffer;
				}
				rwstrich -= rest_of_buffer;
				rest_of_buffer = 0;
			}
		}
		scatterwalk_done(&walk_out, 1, nbytes);
	} while (nbytes && ah < hstrich);

	{
		struct timeval ts;
		do_gettimeofday(&ts);
		list_del(&item->vb.queue);
		item->vb.state = STATE_DONE;
		item->vb.ts = ts;
		wake_up(&item->vb.done);
		chan->evenbuffer = NULL;
	}

out:
	spin_unlock(&chan->s_lock);
	return 0;
}

static void *snd_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags)
{
	struct ngene_channel *chan = priv;
	struct mychip *mychip = chan->mychip;

	if (chan->audiomute == 0)
		memcpy(chan->soundbuffer, (u8 *) buf, MAX_AUDIO_BUFFER_SIZE);
	else
		memset(chan->soundbuffer, 0, MAX_AUDIO_BUFFER_SIZE);

	if (mychip->substream != NULL) {
		if (chan->sndbuffflag == 0)
			chan->sndbuffflag = 1;
		else
			chan->sndbuffflag = 0;
		spin_unlock(&mychip->lock);
		snd_pcm_period_elapsed(mychip->substream);
		spin_lock(&mychip->lock);
	}
	return 0;
}

static void set_analog_transfer(struct ngene_channel *chan, int state)
{
	struct ngene_channel *ch;
	u8 flags = 0;

	ch = &chan->dev->channel[chan->number + 2];
	/* printk(KERN_INFO "set_analog_transfer %d\n", state); */

	if (1) { /* chan->tun_dec_rdy == 1){ */
		if (state) {

			chan->Capture1Length =
				chan->tvnorms[chan->tvnorm].swidth *
				chan->tvnorms[chan->tvnorm].sheight;
			if (chan->tvnorms[chan->tvnorm].sheight == 576)
				chan->nLines = 287;
			else if (chan->tvnorms[chan->tvnorm].sheight == 1080)
				chan->nLines = 541;
			else
				chan->nLines =
					chan->tvnorms[chan->tvnorm].sheight / 2;
			chan->nBytesPerLine =
				chan->tvnorms[chan->tvnorm].swidth * 2;
			if (chan->dev->card_info->io_type[chan->number] ==
			    NGENE_IO_HDTV) {
				chan->itumode = 2;
				flags = SFLAG_ORDER_LUMA_CHROMA;
			} else {
				chan->itumode = 0;
				flags = SFLAG_ORDER_LUMA_CHROMA;
			}
			chan->pBufferExchange = vid_exchange;
			ngene_command_stream_control(chan->dev, chan->number,
						     0x80,
						     SMODE_VIDEO_CAPTURE,
						     flags);

			ch->Capture1Length = MAX_AUDIO_BUFFER_SIZE;
			ch->pBufferExchange = snd_exchange;
			ngene_command_stream_control(ch->dev, ch->number,
						     0x80,
						     SMODE_AUDIO_CAPTURE, 0);
			ch->soundstreamon = 1;
		} else {
			ngene_command_stream_control(chan->dev, chan->number,
						     0, 0, 0);
			ngene_command_stream_control(ch->dev, ch->number,
						     0, 0, 0);
			ch->soundstreamon = 0;
		}
	}
}

static int ngene_analog_start_feed(struct ngene_channel *chan)
{
	int freerunmode = 1;
	struct i2c_adapter *adapter = &chan->i2c_adapter;

	if (chan->users == 0 && chan->number < 2) {
		chan->evenbuffer = NULL;
		chan->users = 1;
		i2c_clients_command(adapter, IOCTL_MIC_DEC_FREESYNC,
				    &freerunmode);
		msleep(25);
		set_analog_transfer(chan, 1);
		msleep(25);
		freerunmode = 0;
		i2c_clients_command(adapter, IOCTL_MIC_DEC_FREESYNC,
				    &freerunmode);
	}
	return chan->users;
}

static int ngene_analog_stop_feed(struct ngene_channel *chan)
{
	int freerunmode = 1;
	struct i2c_adapter *adapter = &chan->i2c_adapter;
	if (chan->users == 1 && chan->number < 2) {
		chan->users = 0;
		i2c_clients_command(adapter,
				    IOCTL_MIC_DEC_FREESYNC, &freerunmode);
		msleep(20);
		set_analog_transfer(chan, 0);
	}
	return 0;
}

/****************************************************************************/
/* V4L2 API interface *******************************************************/
/****************************************************************************/

void ngene_dma_free(struct videobuf_queue *q,
		    struct ngene_channel *chan, struct ngene_buffer *buf)
{
	videobuf_waiton(&buf->vb, 0, 0);
	videobuf_dma_unmap(q, &buf->vb.dma);
	videobuf_dma_free(&buf->vb.dma);
	buf->vb.state = STATE_NEEDS_INIT;
}

static int ngene_prepare_buffer(struct videobuf_queue *q,
				struct ngene_channel *chan,
				struct ngene_buffer *buf,
				const struct ngene_format *fmt,
				unsigned int width, unsigned int height,
				enum v4l2_field field)
{
	int rc = 0;
	/* check settings */
	if (NULL == fmt)
		return -EINVAL;

	if (width < 48 || height < 32)
		return -EINVAL;

	buf->vb.size = (width * height * 16 /* fmt->depth */) >> 3;
	if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	/* alloc + fill struct ngene_buffer (if changed) */
	if (buf->vb.width != width || buf->vb.height != height ||
	    buf->vb.field != field || buf->fmt != fmt ||
	    buf->tvnorm != chan->tvnorm) {

		buf->vb.width  = width;
		buf->vb.height = height;
		buf->vb.field  = field;
		buf->tvnorm    = chan->tvnorm;
		buf->fmt       = fmt;

		ngene_dma_free(q, chan, buf);
	}

	if (0 != buf->vb.baddr && buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->vb.field == 0)
		buf->vb.field = V4L2_FIELD_INTERLACED;

	if (STATE_NEEDS_INIT == buf->vb.state) {
		buf->vb.width  = width;
		buf->vb.height = height;
		buf->vb.field  = field;
		buf->tvnorm    = chan->tvnorm;
		buf->fmt       = fmt;

		rc = videobuf_iolock(q, &buf->vb, &chan->fbuf);
		if (0 != rc)
			goto fail;
	}
	if (!buf->vb.dma.bus_addr)
		videobuf_dma_sync(q, &buf->vb.dma);
	buf->vb.state = STATE_PREPARED;
	return 0;

fail:
	ngene_dma_free(q, chan, buf);
	return rc;

}

static int buffer_setup(struct videobuf_queue *q,
			unsigned int *count, unsigned int *size)
{
	struct ngene_vopen *vopen = q->priv_data;
	*size = 2 * vopen->width * vopen->height;
	if (0 == *count)
		*count = gbuffers;
	while (*size * *count > gbuffers * gbufsize)
		(*count)--;
	q->field = V4L2_FIELD_INTERLACED;
	q->last = V4L2_FIELD_INTERLACED;
	return 0;
}

static int buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	struct ngene_buffer *buf = container_of(vb, struct ngene_buffer, vb);
	struct ngene_vopen *vopen = q->priv_data;
	return ngene_prepare_buffer(q, vopen->ch, buf, vopen->fmt,
				    vopen->width, vopen->height, field);
}

static void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct ngene_buffer *buf = container_of(vb, struct ngene_buffer, vb);
	struct ngene_vopen *vopen = q->priv_data;
	ngene_dma_free(q, vopen->ch, buf);
}

static void buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct ngene_buffer *buf = container_of(vb, struct ngene_buffer, vb);
	struct ngene_vopen *vopen = q->priv_data;
	struct ngene_channel *chan = vopen->ch;

	buf->vb.state = STATE_QUEUED;
	list_add_tail(&buf->vb.queue, &chan->capture);
}

static struct videobuf_queue_ops ngene_video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

int video_open(struct inode *inode, struct file *flip)
{
	struct ngene_vopen *vopen = NULL;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	struct video_device *vd = video_devdata(flip);
	struct ngene_channel *chan = my_video_get_drvdata(vd);

	vopen = kmalloc(sizeof(*vopen), GFP_KERNEL);
	if (!vopen)
		return -ENOMEM;
	memset(vopen, 0, sizeof(*vopen));
	flip->private_data = vopen;
	v4l2_prio_open(&chan->prio, &vopen->prio);
	vopen->ch = chan;
	vopen->picxcount = 0;
	vopen->type = type;
	videobuf_queue_init(&vopen->vbuf_q, &ngene_video_qops,
			    chan->dev->pci_dev, &chan->s_lock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct ngene_buffer), vopen);

	vopen->ovfmt = ngene_formats;
	chan->videousers++;
	if (chan->dev->card_info->switch_ctrl)
		chan->dev->card_info->switch_ctrl(chan, 2, 1);
	return 0;
}

int video_close(struct inode *inode, struct file *filp)
{
	struct ngene_vopen *vopen = filp->private_data;
	struct ngene_channel *chan = vopen->ch;

	chan->videousers--;
	if (!chan->videousers) {
		if (chan->dev->card_info->switch_ctrl)
			chan->dev->card_info->switch_ctrl(chan, 2, 0);
		ngene_analog_stop_feed(chan);
	}
	videobuf_mmap_free(&vopen->vbuf_q);
	v4l2_prio_close(&chan->prio, &vopen->prio);
	filp->private_data = NULL;
	kfree(vopen);
	return 0;
}

/****************************************************************************/

static int vid_do_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, void *parg)
{
	struct ngene_vopen *vopen = file->private_data;
	struct ngene_channel *chan = vopen->ch;
	struct ngene *dev = chan->dev;
	struct i2c_adapter *adap = &chan->i2c_adapter;
	int retval = 0;
	int err = 0;

	switch (cmd) {

	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *c = parg;

		err = v4l2_prio_check(&chan->prio, &vopen->prio);
		if (err)
			return err;

		if (c->id == V4L2_CID_AUDIO_MUTE) {
			if (c->value)
				(dev->channel[chan->number + 2]).audiomute = 1;
			else
				(dev->channel[chan->number + 2]).audiomute = 0;
			return 0;
		}
		if (c->value != V4L2_CID_AUDIO_MUTE)
			ngene_analog_stop_feed(chan);
		i2c_clients_command(adap, cmd, parg);
		return 0;
	}

	case VIDIOC_S_TUNER:
	{
		err = v4l2_prio_check(&chan->prio, &vopen->prio);
		if (err != 0)
			return err;
		i2c_clients_command(adap, cmd, parg);
		return 0;
	}

	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = parg;
		u8 drxa = dev->card_info->demoda[chan->number];

		if (chan->fe && chan->fe->ops.tuner_ops.set_frequency)
			chan->fe->ops.tuner_ops.
				set_frequency(chan->fe, f->frequency * 62500);
		if (drxa)
			;
	}

	case VIDIOC_S_INPUT:
	{
		err = v4l2_prio_check(&chan->prio, &vopen->prio);
		if (err != 0)
			return err;
		i2c_clients_command(adap, cmd, parg);
		return 0;
	}

	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = parg;
		*id = chan->tvnorms[chan->tvnorm].v4l2_id;
		return 0;
	}

	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = parg;
		unsigned int i;

		err = v4l2_prio_check(&chan->prio, &vopen->prio);
		if (err != 0)
			return err;
		ngene_analog_stop_feed(chan);
		i2c_clients_command(adap, cmd, parg);
		for (i = 0; i < chan->tvnorm_num; i++)
			if (*id & chan->tvnorms[i].v4l2_id)
				break;
		if (i == chan->tvnorm_num)
			return -EINVAL;

		chan->tvnorm = i;
		mdelay(50);
		ngene_analog_start_feed(chan);
		return 0;
	}

	case VIDIOC_G_FREQUENCY:
	case VIDIOC_G_INPUT:
	case VIDIOC_S_AUDIO:
	case VIDIOC_G_AUDIO:
	case VIDIOC_ENUMAUDIO:
	case VIDIOC_S_MODULATOR:
	case VIDIOC_G_MODULATOR:
	case VIDIOC_G_CTRL:
	{
		i2c_clients_command(adap, cmd, parg);
		return 0;
	}

	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *tuner = parg;
		if (tuner->index != 0)
			return -EINVAL;
		i2c_clients_command(adap, cmd, parg);

		if (chan->fe && chan->fe->ops.tuner_ops.get_status) {
			u32 status;

			chan->fe->ops.tuner_ops.get_status(chan->fe, &status);
			tuner->signal = status;
		}
		return 0;
	}

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *c = parg;
		int i;

		if ((c->id <  V4L2_CID_BASE ||
		     c->id >= V4L2_CID_LASTP1) &&
		    (c->id <  V4L2_CID_PRIVATE_BASE ||
		     c->id >= V4L2_CID_PRIVATE_LASTP1))
			return -EINVAL;
		for (i = 0; i < NGENE_CTLS; i++)
			if (ngene_ctls[i].id == c->id)
				break;
		if (i == NGENE_CTLS) {
			*c = no_ctl;
			return 0;
		}
		*c = ngene_ctls[i];
		return 0;
	}

	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = parg;
		ngene_g_fmt(vopen, f);
	}

	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = parg;

		ngene_analog_stop_feed(chan);
		return ngene_s_fmt(vopen, chan, f);
	}

	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = parg;
		enum v4l2_buf_type type;
		unsigned int i;
		int index;

		type = f->type;
		if (V4L2_BUF_TYPE_VBI_CAPTURE == type) {
			/* vbi
			index = f->index;
			if (0 != index)
				return -EINVAL;
			memset(f, 0, sizeof(*f));
			f->index       = index;
			f->type        = type;
			f->pixelformat = V4L2_PIX_FMT_GREY;
			strcpy(f->description, "vbi data"); */
			return EINVAL;
		}

		/* video capture + overlay */
		index = -1;
		for (i = 0; i < NGENE_FORMATS; i++) {
			if (ngene_formats[i].fourcc != -1)
				index++;
			if ((unsigned int)index == f->index)
				break;
		}
		if (NGENE_FORMATS == i)
			return -EINVAL;

		switch (f->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			break;
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:
			/* dprintk(KERN_DEBUG
				   "Video Overlay not supported yet.\n"); */
			return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
		memset(f, 0, sizeof(*f));
		f->index       = index;
		f->type        = type;
		f->pixelformat = ngene_formats[i].fourcc;
		strlcpy(f->description,	ngene_formats[i].name,
			sizeof(f->description));
		return 0;

	}

	case VIDIOC_QUERYSTD:
	{
		v4l2_std_id *id = parg;
		*id = V4L2_STD_625_50 | V4L2_STD_525_60;
		return 0;
	}

	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *e = parg;
		unsigned int index = e->index;

		if (index >= chan->tvnorm_num)
			return -EINVAL;
		v4l2_video_std_construct(e, chan->tvnorms[e->index].v4l2_id,
					 chan->tvnorms[e->index].name);
		e->index = index;
		return 0;
	}

	case VIDIOC_QUERYCAP:
	{
		static char driver[] = {'n', 'G', 'e', 'n', 'e', '\0'};
		static char card[] = {'M', 'k', '4', 'x', 'x', '\0'};
		struct v4l2_capability *cap = parg;

		memset(cap, 0, sizeof(*cap));
		if (dev->nr == 0)
			card[3] = '0';
		else
			card[3] = '1';
		if (chan->number)
			card[4] = 'a';
		else
			card[4] = 'b';
		strlcpy(cap->driver, driver, sizeof(cap->driver));
		strlcpy(cap->card, card, sizeof(cap->card));
		cap->bus_info[0] = 0;
		cap->version = KERNEL_VERSION(0, 8, 1);
		cap->capabilities = V4L2_CAP_VIDEO_CAPTURE|
			V4L2_CAP_TUNER|V4L2_CAP_AUDIO|
			V4L2_CAP_READWRITE|V4L2_CAP_STREAMING;
		return 0;
	}

	case VIDIOC_ENUMINPUT:
	{
		static char *inputname[2] = {
			"AnalogTuner",
			"S-Video"
		};

		struct v4l2_input *i = parg;
		unsigned int index;
		index = i->index;

		if (index > 1)
			return -EINVAL;

		memset(i, 0, sizeof(*i));
		i->index = index;
		strlcpy(i->name, inputname[index], sizeof(i->name));

		i->type = index ? V4L2_INPUT_TYPE_CAMERA :
			V4L2_INPUT_TYPE_TUNER;
		i->audioset = 0;
		i->tuner = 0;
		i->std = V4L2_STD_PAL_BG | V4L2_STD_NTSC_M;
		i->status = 0;/* V4L2_IN_ST_NO_H_LOCK; */
		return 0;
	}

	case VIDIOC_G_PARM:
		return -EINVAL;

	case VIDIOC_S_PARM:
		return -EINVAL;

	case VIDIOC_G_PRIORITY:
	{
		enum v4l2_priority *prio = parg;
		*prio = v4l2_prio_max(&chan->prio);
		return 0;
	}

	case VIDIOC_S_PRIORITY:
	{
		enum v4l2_priority *prio = parg;
		return v4l2_prio_change(&chan->prio, &vopen->prio, *prio);
		return 0;
	}

	case VIDIOC_CROPCAP:
		return -EINVAL;

	case VIDIOC_G_CROP:
		return -EINVAL;

	case VIDIOC_S_CROP:
		return -EINVAL;

	case VIDIOC_G_FBUF:
	{
		struct v4l2_framebuffer *fb = parg;

		*fb = chan->fbuf;
		fb->capability = 0;
		if (vopen->ovfmt)
			fb->fmt.pixelformat = vopen->ovfmt->fourcc;
		return 0;
	}

	case VIDIOC_REQBUFS:
		return videobuf_reqbufs(ngene_queue(vopen), parg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(ngene_queue(vopen), parg);

	case VIDIOC_QBUF:
		return videobuf_qbuf(ngene_queue(vopen), parg);

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(ngene_queue(vopen), parg,
				      file->f_flags & O_NONBLOCK);

	case VIDIOC_S_FBUF:
	{
		/* ngene_analog_stop_feed(chan); */
		struct v4l2_framebuffer *fb = parg;
		const struct ngene_format *fmt;

		if (!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_RAWIO))
			return -EPERM;

		/* check args */
		fmt = ngene_formats; /*format_by_fourcc(fb->fmt.pixelformat);*/
		if (NULL == fmt)
			return -EINVAL;

		if (0 == (fmt->flags & 0x02 /*FORMAT_FLAGS_PACKED*/))
			return -EINVAL;

		mutex_lock(&vopen->vbuf_q.lock);
		retval = -EINVAL;

		if (fb->flags & V4L2_FBUF_FLAG_OVERLAY) {
			int maxLinesPerField;

			if (fb->fmt.width >
			    chan->tvnorms[chan->tvnorm].swidth)
				goto vopen_unlock_and_return;
			maxLinesPerField = chan->tvnorms[chan->tvnorm].sheight;
			if (fb->fmt.height > maxLinesPerField)
				goto vopen_unlock_and_return;
		}

		/* ok, accept it */
		chan->fbuf.base       = fb->base;
		chan->fbuf.fmt.width  = fb->fmt.width;
		chan->fbuf.fmt.height = fb->fmt.height;
		if (0 != fb->fmt.bytesperline)
			chan->fbuf.fmt.bytesperline = fb->fmt.bytesperline;
		else
			chan->fbuf.fmt.bytesperline =
				chan->fbuf.fmt.width * fmt->depth / 8;

		retval = 0;
		vopen->ovfmt = fmt;
		chan->init.ovfmt = fmt;

vopen_unlock_and_return:
		mutex_unlock(&vopen->vbuf_q.lock);
		return retval;

	}

	case VIDIOC_ENUMOUTPUT:
		return -EINVAL;

	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = parg;
		return ngene_try_fmt(vopen, chan, f);

	}

	case VIDIOC_STREAMON:
	{
		int res = ngene_resource(vopen);
		if (!check_alloc_res(chan, vopen, res))
			return -EBUSY;
		ngene_analog_start_feed(chan);
		return videobuf_streamon(ngene_queue(vopen));
	}

	case VIDIOC_STREAMOFF:
	{
		int res = ngene_resource(vopen);
		int retval = videobuf_streamoff(ngene_queue(vopen));
		ngene_analog_stop_feed(chan);
		if (retval < 0)
			return retval;

		free_res(chan, vopen, res);
		return 0;
	}

	case VIDIOC_OVERLAY:
		return -EINVAL;

	case VIDIOCGFBUF:
	{
		struct video_buffer *vb = parg;

		memset(vb, 0, sizeof(*vb));
		return 0;
	}

	default:
		err = -EINVAL;
		break;
	}
	return err;
}

/*
static int vid_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, vid_do_ioctl);
}
*/
static unsigned int video_fix_command(unsigned int cmd)
{
	switch (cmd) {
	}
	return cmd;
}

static int vid_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	void *parg = (void *)arg, *pbuf = NULL;
	char  buf[64];
	int   res = -EFAULT;
	cmd = video_fix_command(cmd);

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		parg = buf;
		if (_IOC_SIZE(cmd) > sizeof(buf)) {
			pbuf = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL);
			if (!pbuf)
				return -ENOMEM;
			parg = pbuf;
		}
		if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
			goto error;
	}
	res = vid_do_ioctl(inode, file, cmd, parg);
	if (res < 0)
		goto error;
	if (_IOC_DIR(cmd) & _IOC_READ)
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			res = -EFAULT;
error:
	kfree(pbuf);
	return res;
}

static int ngene_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ngene_vopen *vopen = file->private_data;
	return videobuf_mmap_mapper(ngene_queue(vopen), vma);
}

#define MAGIC_BUFFER 0x20040302
void *my_videobuf_alloc(unsigned int size)
{
	struct videobuf_buffer *vb;

	vb = kmalloc(size, GFP_KERNEL);
	if (NULL != vb) {
		memset(vb, 0, size);
		videobuf_dma_init(&vb->dma);
		init_waitqueue_head(&vb->done);
		vb->magic = MAGIC_BUFFER;
	}
	return vb;
}

static ssize_t driver_read(struct file *file, char *user,
			   size_t count, loff_t *offset)
{
	char __user *data = user;
	struct ngene_channel *chan;
	int retval = 0;
	struct videobuf_queue *q;
	struct ngene_vopen *vopen = file->private_data;
	int nonblocking = file->f_flags & O_NONBLOCK;
	enum v4l2_field field;
	unsigned long flags;
	unsigned size, nbufs, bytes;

	if (!vopen)
		return 0;

	chan = vopen->ch;
	q = &vopen->vbuf_q;

	mutex_lock(&q->lock);
	nbufs = 1;
	size = 0;
	q->ops->buf_setup(q, &nbufs, &size);

	if (NULL == q->read_buf) {
		/* need to capture a new frame */
		retval = -ENOMEM;
		q->read_buf = my_videobuf_alloc(q->msize);
		if (NULL == q->read_buf)
			goto done;

		q->read_buf->memory = V4L2_MEMORY_USERPTR;
		field = V4L2_FIELD_INTERLACED;
		retval = q->ops->buf_prepare(q, q->read_buf, field);
		if (0 != retval) {
			kfree(q->read_buf);
			q->read_buf = NULL;
			goto done;
		}

		spin_lock_irqsave(q->irqlock, flags);
		q->ops->buf_queue(q, q->read_buf);
		spin_unlock_irqrestore(q->irqlock, flags);
		q->read_off = 0;
	}

	ngene_analog_start_feed(chan);
	/* wait until capture is done */
	retval = videobuf_waiton(q->read_buf, nonblocking, 1);
	if (0 != retval)
		goto done;
	videobuf_dma_sync(q, &q->read_buf->dma);

	if (STATE_ERROR == q->read_buf->state) {
		/* catch I/O errors */
		q->ops->buf_release(q, q->read_buf);
		kfree(q->read_buf);
		q->read_buf = NULL;
		retval = -EIO;
		goto done;
	}

	/* copy to userspace */
	bytes = count;
	if (bytes > q->read_buf->size - q->read_off)
		bytes = q->read_buf->size - q->read_off;
	retval = -EFAULT;

	if (copy_to_user(data, q->read_buf->dma.vmalloc + q->read_off, bytes))
		goto done;

	retval = bytes;

	q->read_off += bytes;
	if (q->read_off == q->read_buf->size) {
		/* all data copied, cleanup */
		q->ops->buf_release(q, q->read_buf);
		kfree(q->read_buf);
		q->read_buf = NULL;
	}

done:
	mutex_unlock(&q->lock);

	ngene_analog_stop_feed(chan);

	return retval;
}

static unsigned int ngene_poll(struct file *file, poll_table *wait)
{
	struct ngene_vopen *vopen = file->private_data;
	struct ngene_buffer *buf;
	enum v4l2_field field;

	if (check_res(vopen, RESOURCE_VIDEO)) {
		/* streaming capture */
		if (list_empty(&vopen->vbuf_q.stream))
			return POLLERR;
		buf = list_entry(vopen->vbuf_q.stream.next,
				 struct ngene_buffer, vb.stream);
	} else {
		/* read() capture */
		mutex_lock(&vopen->vbuf_q.lock);
		if (NULL == vopen->vbuf_q.read_buf) {
			/* need to capture a new frame */
			if (locked_res(vopen->ch, RESOURCE_VIDEO)) {
				mutex_unlock(&vopen->vbuf_q.lock);
				return POLLERR;
			}
			vopen->vbuf_q.read_buf =
				videobuf_alloc(vopen->vbuf_q.msize);
			if (NULL == vopen->vbuf_q.read_buf) {
				mutex_unlock(&vopen->vbuf_q.lock);
				return POLLERR;
			}
			vopen->vbuf_q.read_buf->memory = V4L2_MEMORY_USERPTR;
			field = videobuf_next_field(&vopen->vbuf_q);
			if (0 !=
			    vopen->vbuf_q.ops->
			    buf_prepare(&vopen->vbuf_q,
					vopen->vbuf_q.read_buf, field)) {
				mutex_unlock(&vopen->vbuf_q.lock);
				return POLLERR;
			}
			vopen->vbuf_q.ops->buf_queue(&vopen->vbuf_q,
						     vopen->vbuf_q.read_buf);
			vopen->vbuf_q.read_off = 0;
		}
		mutex_unlock(&vopen->vbuf_q.lock);
		buf = (struct ngene_buffer *)vopen->vbuf_q.read_buf;
	}

	poll_wait(file, &buf->vb.done, wait);
	if (buf->vb.state == STATE_DONE || buf->vb.state == STATE_ERROR)
		return POLLIN | POLLRDNORM;
	return 0;
}

static const struct file_operations ngene_fops = {
	.owner    = THIS_MODULE,
	.read     = driver_read,
	.write    = 0,
	.open     = video_open,
	.release  = video_close,
	.ioctl    = vid_ioctl,
	.poll     = ngene_poll,
	.mmap     = ngene_mmap,
};

static struct video_device ngene_cinfo = {
	.name     = "analog_Ngene",
	.type     = VID_TYPE_CAPTURE | VID_TYPE_TUNER | VID_TYPE_SCALES,
	.fops     = &ngene_fops,
	.minor    = -1,
};

void ngene_v4l2_remove(struct ngene_channel *chan)
{
	video_unregister_device(chan->v4l_dev);
}

int ngene_v4l2_init(struct ngene_channel *chan)
{
	int ret = 0;
	struct video_device *v_dev;

	chan->evenbuffer = NULL;
	chan->dma_on = 0;

	v_dev = video_device_alloc();
	*v_dev = ngene_cinfo;
	/* v_dev->dev = &(chan->dev->pci_dev->dev); */
	v_dev->release = video_device_release;
	v_dev->minor = -1;
	video_register_device(v_dev, VFL_TYPE_GRABBER, -1);
	snprintf(v_dev->name, sizeof(v_dev->name), "AnalognGene%d",
		 v_dev->minor);
	chan->v4l_dev = v_dev;
	chan->minor = v_dev->minor;
	printk(KERN_INFO "nGene V4L2 device video%d registered.\n",
	       v_dev->minor);

	v_dev->dev = &chan->device;
	my_video_set_drvdata(chan->v4l_dev, chan);

	v4l2_prio_init(&chan->prio);

	if (chan->dev->card_info->io_type[chan->number] == NGENE_IO_HDTV) {
		chan->tvnorms = ngene_tvnorms_hd;
		chan->tvnorm_num = 1;
	} else {
		chan->tvnorms = ngene_tvnorms_sd;
		chan->tvnorm_num = NGENE_TVNORMS;
	}
	chan->tvnorm = 0;

	spin_lock_init(&chan->s_lock);
	init_MUTEX(&chan->reslock);
	INIT_LIST_HEAD(&chan->capture);
	chan->users            = 0;
	chan->videousers       = 0;
	chan->init.ov.w.width  = 384;
	chan->init.ov.w.height = 288;
	chan->init.fmt         = ngene_formats;
	chan->init.width       = 384;
	chan->init.height      = 288;
	chan->tun_rdy          = 0;
	chan->dec_rdy          = 0;
	chan->tun_dec_rdy      = 0;
	chan->lastbufferflag   = -1;

	if (chan->dev->card_info->avf[chan->number])
		avf4910a_attach(&chan->i2c_adapter,
				chan->dev->card_info->avf[chan->number]);

	return ret;
}
