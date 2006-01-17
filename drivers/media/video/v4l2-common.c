/*
 *	Video for Linux Two
 *
 *	A generic video device interface for the LINUX operating system
 *	using a set of device structures/vectors for low level operations.
 *
 *	This file replaces the videodev.c file that comes with the
 *	regular kernel distribution.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Author:	Bill Dirks <bdirks@pacbell.net>
 *		based on code by Alan Cox, <alan@cymru.net>
 *
 */

/*
 * Video capture interface for Linux
 *
 *	A generic video device interface for the LINUX operating system
 *	using a set of device structures/vectors for low level operations.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Alan Cox, <alan@redhat.com>
 *
 * Fixes:
 */

/*
 * Video4linux 1/2 integration by Justin Schoeman
 * <justin@suntiger.ee.up.ac.za>
 * 2.4 PROCFS support ported from 2.4 kernels by
 *  Iñaki García Etxebarria <garetxe@euskalnet.net>
 * Makefile fix by "W. Michael Petullo" <mike@flyn.org>
 * 2.4 devfs support ported from 2.4 kernels by
 *  Dan Merillat <dan@merillat.org>
 * Added Gerd Knorrs v4l1 enhancements (Justin Schoeman)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/div64.h>
#include <linux/video_decoder.h>
#include <media/v4l2-common.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#if defined(CONFIG_UST) || defined(CONFIG_UST_MODULE)
#include <linux/ust.h>
#endif


#include <linux/videodev.h>

MODULE_AUTHOR("Bill Dirks, Justin Schoeman, Gerd Knorr");
MODULE_DESCRIPTION("misc helper functions for v4l2 device drivers");
MODULE_LICENSE("GPL");

/*
 *
 *	V 4 L 2   D R I V E R   H E L P E R   A P I
 *
 */

/*
 *  Video Standard Operations (contributed by Michael Schimek)
 */


/* Fill in the fields of a v4l2_standard structure according to the
   'id' and 'transmission' parameters.  Returns negative on error.  */
int v4l2_video_std_construct(struct v4l2_standard *vs,
			     int id, char *name)
{
	u32 index = vs->index;

	memset(vs, 0, sizeof(struct v4l2_standard));
	vs->index = index;
	vs->id    = id;
	if (id & (V4L2_STD_NTSC | V4L2_STD_PAL_M)) {
		vs->frameperiod.numerator = 1001;
		vs->frameperiod.denominator = 30000;
		vs->framelines = 525;
	} else {
		vs->frameperiod.numerator = 1;
		vs->frameperiod.denominator = 25;
		vs->framelines = 625;
	}
	strlcpy(vs->name,name,sizeof(vs->name));
	return 0;
}


/* ----------------------------------------------------------------- */
/* priority handling                                                 */

#define V4L2_PRIO_VALID(val) (val == V4L2_PRIORITY_BACKGROUND   || \
			      val == V4L2_PRIORITY_INTERACTIVE  || \
			      val == V4L2_PRIORITY_RECORD)

int v4l2_prio_init(struct v4l2_prio_state *global)
{
	memset(global,0,sizeof(*global));
	return 0;
}

int v4l2_prio_change(struct v4l2_prio_state *global, enum v4l2_priority *local,
		     enum v4l2_priority new)
{
	if (!V4L2_PRIO_VALID(new))
		return -EINVAL;
	if (*local == new)
		return 0;

	atomic_inc(&global->prios[new]);
	if (V4L2_PRIO_VALID(*local))
		atomic_dec(&global->prios[*local]);
	*local = new;
	return 0;
}

int v4l2_prio_open(struct v4l2_prio_state *global, enum v4l2_priority *local)
{
	return v4l2_prio_change(global,local,V4L2_PRIORITY_DEFAULT);
}

int v4l2_prio_close(struct v4l2_prio_state *global, enum v4l2_priority *local)
{
	if (V4L2_PRIO_VALID(*local))
		atomic_dec(&global->prios[*local]);
	return 0;
}

enum v4l2_priority v4l2_prio_max(struct v4l2_prio_state *global)
{
	if (atomic_read(&global->prios[V4L2_PRIORITY_RECORD]) > 0)
		return V4L2_PRIORITY_RECORD;
	if (atomic_read(&global->prios[V4L2_PRIORITY_INTERACTIVE]) > 0)
		return V4L2_PRIORITY_INTERACTIVE;
	if (atomic_read(&global->prios[V4L2_PRIORITY_BACKGROUND]) > 0)
		return V4L2_PRIORITY_BACKGROUND;
	return V4L2_PRIORITY_UNSET;
}

int v4l2_prio_check(struct v4l2_prio_state *global, enum v4l2_priority *local)
{
	if (*local < v4l2_prio_max(global))
		return -EBUSY;
	return 0;
}


/* ----------------------------------------------------------------- */
/* some arrays for pretty-printing debug messages                    */

char *v4l2_field_names[] = {
	[V4L2_FIELD_ANY]        = "any",
	[V4L2_FIELD_NONE]       = "none",
	[V4L2_FIELD_TOP]        = "top",
	[V4L2_FIELD_BOTTOM]     = "bottom",
	[V4L2_FIELD_INTERLACED] = "interlaced",
	[V4L2_FIELD_SEQ_TB]     = "seq-tb",
	[V4L2_FIELD_SEQ_BT]     = "seq-bt",
	[V4L2_FIELD_ALTERNATE]  = "alternate",
};

char *v4l2_type_names[] = {
	[V4L2_BUF_TYPE_VIDEO_CAPTURE] = "video-cap",
	[V4L2_BUF_TYPE_VIDEO_OVERLAY] = "video-over",
	[V4L2_BUF_TYPE_VIDEO_OUTPUT]  = "video-out",
	[V4L2_BUF_TYPE_VBI_CAPTURE]   = "vbi-cap",
	[V4L2_BUF_TYPE_VBI_OUTPUT]    = "vbi-out",
};

/* ------------------------------------------------------------------ */
/* debug help functions                                               */

#ifdef HAVE_V4L1
static const char *v4l1_ioctls[] = {
	[_IOC_NR(VIDIOCGCAP)]       = "VIDIOCGCAP",
	[_IOC_NR(VIDIOCGCHAN)]      = "VIDIOCGCHAN",
	[_IOC_NR(VIDIOCSCHAN)]      = "VIDIOCSCHAN",
	[_IOC_NR(VIDIOCGTUNER)]     = "VIDIOCGTUNER",
	[_IOC_NR(VIDIOCSTUNER)]     = "VIDIOCSTUNER",
	[_IOC_NR(VIDIOCGPICT)]      = "VIDIOCGPICT",
	[_IOC_NR(VIDIOCSPICT)]      = "VIDIOCSPICT",
	[_IOC_NR(VIDIOCCAPTURE)]    = "VIDIOCCAPTURE",
	[_IOC_NR(VIDIOCGWIN)]       = "VIDIOCGWIN",
	[_IOC_NR(VIDIOCSWIN)]       = "VIDIOCSWIN",
	[_IOC_NR(VIDIOCGFBUF)]      = "VIDIOCGFBUF",
	[_IOC_NR(VIDIOCSFBUF)]      = "VIDIOCSFBUF",
	[_IOC_NR(VIDIOCKEY)]        = "VIDIOCKEY",
	[_IOC_NR(VIDIOCGFREQ)]      = "VIDIOCGFREQ",
	[_IOC_NR(VIDIOCSFREQ)]      = "VIDIOCSFREQ",
	[_IOC_NR(VIDIOCGAUDIO)]     = "VIDIOCGAUDIO",
	[_IOC_NR(VIDIOCSAUDIO)]     = "VIDIOCSAUDIO",
	[_IOC_NR(VIDIOCSYNC)]       = "VIDIOCSYNC",
	[_IOC_NR(VIDIOCMCAPTURE)]   = "VIDIOCMCAPTURE",
	[_IOC_NR(VIDIOCGMBUF)]      = "VIDIOCGMBUF",
	[_IOC_NR(VIDIOCGUNIT)]      = "VIDIOCGUNIT",
	[_IOC_NR(VIDIOCGCAPTURE)]   = "VIDIOCGCAPTURE",
	[_IOC_NR(VIDIOCSCAPTURE)]   = "VIDIOCSCAPTURE",
	[_IOC_NR(VIDIOCSPLAYMODE)]  = "VIDIOCSPLAYMODE",
	[_IOC_NR(VIDIOCSWRITEMODE)] = "VIDIOCSWRITEMODE",
	[_IOC_NR(VIDIOCGPLAYINFO)]  = "VIDIOCGPLAYINFO",
	[_IOC_NR(VIDIOCSMICROCODE)] = "VIDIOCSMICROCODE",
	[_IOC_NR(VIDIOCGVBIFMT)]    = "VIDIOCGVBIFMT",
	[_IOC_NR(VIDIOCSVBIFMT)]    = "VIDIOCSVBIFMT"
};
#define V4L1_IOCTLS ARRAY_SIZE(v4l1_ioctls)
#endif

static const char *v4l2_ioctls[] = {
	[_IOC_NR(VIDIOC_QUERYCAP)]         = "VIDIOC_QUERYCAP",
	[_IOC_NR(VIDIOC_RESERVED)]         = "VIDIOC_RESERVED",
	[_IOC_NR(VIDIOC_ENUM_FMT)]         = "VIDIOC_ENUM_FMT",
	[_IOC_NR(VIDIOC_G_FMT)]            = "VIDIOC_G_FMT",
	[_IOC_NR(VIDIOC_S_FMT)]            = "VIDIOC_S_FMT",
	[_IOC_NR(VIDIOC_G_MPEGCOMP)]       = "VIDIOC_G_MPEGCOMP",
	[_IOC_NR(VIDIOC_S_MPEGCOMP)]       = "VIDIOC_S_MPEGCOMP",
	[_IOC_NR(VIDIOC_REQBUFS)]          = "VIDIOC_REQBUFS",
	[_IOC_NR(VIDIOC_QUERYBUF)]         = "VIDIOC_QUERYBUF",
	[_IOC_NR(VIDIOC_G_FBUF)]           = "VIDIOC_G_FBUF",
	[_IOC_NR(VIDIOC_S_FBUF)]           = "VIDIOC_S_FBUF",
	[_IOC_NR(VIDIOC_OVERLAY)]          = "VIDIOC_OVERLAY",
	[_IOC_NR(VIDIOC_QBUF)]             = "VIDIOC_QBUF",
	[_IOC_NR(VIDIOC_DQBUF)]            = "VIDIOC_DQBUF",
	[_IOC_NR(VIDIOC_STREAMON)]         = "VIDIOC_STREAMON",
	[_IOC_NR(VIDIOC_STREAMOFF)]        = "VIDIOC_STREAMOFF",
	[_IOC_NR(VIDIOC_G_PARM)]           = "VIDIOC_G_PARM",
	[_IOC_NR(VIDIOC_S_PARM)]           = "VIDIOC_S_PARM",
	[_IOC_NR(VIDIOC_G_STD)]            = "VIDIOC_G_STD",
	[_IOC_NR(VIDIOC_S_STD)]            = "VIDIOC_S_STD",
	[_IOC_NR(VIDIOC_ENUMSTD)]          = "VIDIOC_ENUMSTD",
	[_IOC_NR(VIDIOC_ENUMINPUT)]        = "VIDIOC_ENUMINPUT",
	[_IOC_NR(VIDIOC_G_CTRL)]           = "VIDIOC_G_CTRL",
	[_IOC_NR(VIDIOC_S_CTRL)]           = "VIDIOC_S_CTRL",
	[_IOC_NR(VIDIOC_G_TUNER)]          = "VIDIOC_G_TUNER",
	[_IOC_NR(VIDIOC_S_TUNER)]          = "VIDIOC_S_TUNER",
	[_IOC_NR(VIDIOC_G_AUDIO)]          = "VIDIOC_G_AUDIO",
	[_IOC_NR(VIDIOC_S_AUDIO)]          = "VIDIOC_S_AUDIO",
	[_IOC_NR(VIDIOC_QUERYCTRL)]        = "VIDIOC_QUERYCTRL",
	[_IOC_NR(VIDIOC_QUERYMENU)]        = "VIDIOC_QUERYMENU",
	[_IOC_NR(VIDIOC_G_INPUT)]          = "VIDIOC_G_INPUT",
	[_IOC_NR(VIDIOC_S_INPUT)]          = "VIDIOC_S_INPUT",
	[_IOC_NR(VIDIOC_G_OUTPUT)]         = "VIDIOC_G_OUTPUT",
	[_IOC_NR(VIDIOC_S_OUTPUT)]         = "VIDIOC_S_OUTPUT",
	[_IOC_NR(VIDIOC_ENUMOUTPUT)]       = "VIDIOC_ENUMOUTPUT",
	[_IOC_NR(VIDIOC_G_AUDOUT)]         = "VIDIOC_G_AUDOUT",
	[_IOC_NR(VIDIOC_S_AUDOUT)]         = "VIDIOC_S_AUDOUT",
	[_IOC_NR(VIDIOC_G_MODULATOR)]      = "VIDIOC_G_MODULATOR",
	[_IOC_NR(VIDIOC_S_MODULATOR)]      = "VIDIOC_S_MODULATOR",
	[_IOC_NR(VIDIOC_G_FREQUENCY)]      = "VIDIOC_G_FREQUENCY",
	[_IOC_NR(VIDIOC_S_FREQUENCY)]      = "VIDIOC_S_FREQUENCY",
	[_IOC_NR(VIDIOC_CROPCAP)]          = "VIDIOC_CROPCAP",
	[_IOC_NR(VIDIOC_G_CROP)]           = "VIDIOC_G_CROP",
	[_IOC_NR(VIDIOC_S_CROP)]           = "VIDIOC_S_CROP",
	[_IOC_NR(VIDIOC_G_JPEGCOMP)]       = "VIDIOC_G_JPEGCOMP",
	[_IOC_NR(VIDIOC_S_JPEGCOMP)]       = "VIDIOC_S_JPEGCOMP",
	[_IOC_NR(VIDIOC_QUERYSTD)]         = "VIDIOC_QUERYSTD",
	[_IOC_NR(VIDIOC_TRY_FMT)]          = "VIDIOC_TRY_FMT",
	[_IOC_NR(VIDIOC_ENUMAUDIO)]        = "VIDIOC_ENUMAUDIO",
	[_IOC_NR(VIDIOC_ENUMAUDOUT)]       = "VIDIOC_ENUMAUDOUT",
	[_IOC_NR(VIDIOC_G_PRIORITY)]       = "VIDIOC_G_PRIORITY",
	[_IOC_NR(VIDIOC_S_PRIORITY)]       = "VIDIOC_S_PRIORITY",
#if 1
	[_IOC_NR(VIDIOC_G_SLICED_VBI_CAP)] = "VIDIOC_G_SLICED_VBI_CAP",
#endif
	[_IOC_NR(VIDIOC_LOG_STATUS)]       = "VIDIOC_LOG_STATUS"
};
#define V4L2_IOCTLS ARRAY_SIZE(v4l2_ioctls)

static const char *v4l2_int_ioctls[] = {
#ifdef HAVE_VIDEO_DECODER
	[_IOC_NR(DECODER_GET_CAPABILITIES)]    = "DECODER_GET_CAPABILITIES",
	[_IOC_NR(DECODER_GET_STATUS)]          = "DECODER_GET_STATUS",
	[_IOC_NR(DECODER_SET_NORM)]            = "DECODER_SET_NORM",
	[_IOC_NR(DECODER_SET_INPUT)]           = "DECODER_SET_INPUT",
	[_IOC_NR(DECODER_SET_OUTPUT)]          = "DECODER_SET_OUTPUT",
	[_IOC_NR(DECODER_ENABLE_OUTPUT)]       = "DECODER_ENABLE_OUTPUT",
	[_IOC_NR(DECODER_SET_PICTURE)]         = "DECODER_SET_PICTURE",
	[_IOC_NR(DECODER_SET_GPIO)]            = "DECODER_SET_GPIO",
	[_IOC_NR(DECODER_INIT)]                = "DECODER_INIT",
	[_IOC_NR(DECODER_SET_VBI_BYPASS)]      = "DECODER_SET_VBI_BYPASS",
	[_IOC_NR(DECODER_DUMP)]                = "DECODER_DUMP",
#endif
	[_IOC_NR(AUDC_SET_RADIO)]              = "AUDC_SET_RADIO",
	[_IOC_NR(AUDC_SET_INPUT)]              = "AUDC_SET_INPUT",
	[_IOC_NR(MSP_SET_MATRIX)]              = "MSP_SET_MATRIX",

	[_IOC_NR(TUNER_SET_TYPE_ADDR)]         = "TUNER_SET_TYPE_ADDR",
	[_IOC_NR(TUNER_SET_STANDBY)]           = "TUNER_SET_STANDBY",
	[_IOC_NR(TDA9887_SET_CONFIG)]          = "TDA9887_SET_CONFIG",

	[_IOC_NR(VIDIOC_INT_S_REGISTER)]       = "VIDIOC_INT_S_REGISTER",
	[_IOC_NR(VIDIOC_INT_G_REGISTER)]       = "VIDIOC_INT_G_REGISTER",
	[_IOC_NR(VIDIOC_INT_RESET)]            = "VIDIOC_INT_RESET",
	[_IOC_NR(VIDIOC_INT_AUDIO_CLOCK_FREQ)] = "VIDIOC_INT_AUDIO_CLOCK_FREQ",
	[_IOC_NR(VIDIOC_INT_DECODE_VBI_LINE)]  = "VIDIOC_INT_DECODE_VBI_LINE",
	[_IOC_NR(VIDIOC_INT_S_VBI_DATA)]       = "VIDIOC_INT_S_VBI_DATA",
	[_IOC_NR(VIDIOC_INT_G_VBI_DATA)]       = "VIDIOC_INT_G_VBI_DATA",
	[_IOC_NR(VIDIOC_INT_G_CHIP_IDENT)]     = "VIDIOC_INT_G_CHIP_IDENT",
	[_IOC_NR(VIDIOC_INT_I2S_CLOCK_FREQ)]   = "VIDIOC_INT_I2S_CLOCK_FREQ"
};
#define V4L2_INT_IOCTLS ARRAY_SIZE(v4l2_int_ioctls)

/* Common ioctl debug function. This function can be used by
   external ioctl messages as well as internal V4L ioctl */
void v4l_printk_ioctl(unsigned int cmd)
{
	char *dir;

	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:              dir = "--"; break;
	case _IOC_READ:              dir = "r-"; break;
	case _IOC_WRITE:             dir = "-w"; break;
	case _IOC_READ | _IOC_WRITE: dir = "rw"; break;
	default:                     dir = "*ERR*"; break;
	}
	switch (_IOC_TYPE(cmd)) {
	case 'd':
		printk("v4l2_int ioctl %s, dir=%s (0x%08x)\n",
		       (_IOC_NR(cmd) < V4L2_INT_IOCTLS) ?
		       v4l2_int_ioctls[_IOC_NR(cmd)] : "UNKNOWN", dir, cmd);
		break;
#ifdef HAVE_V4L1
	case 'v':
		printk("v4l1 ioctl %s, dir=%s (0x%08x)\n",
		       (_IOC_NR(cmd) < V4L1_IOCTLS) ?
		       v4l1_ioctls[_IOC_NR(cmd)] : "UNKNOWN", dir, cmd);
		break;
#endif
	case 'V':
		printk("v4l2 ioctl %s, dir=%s (0x%08x)\n",
		       (_IOC_NR(cmd) < V4L2_IOCTLS) ?
		       v4l2_ioctls[_IOC_NR(cmd)] : "UNKNOWN", dir, cmd);
		break;

	default:
		printk("unknown ioctl '%c', dir=%s, #%d (0x%08x)\n",
		       _IOC_TYPE(cmd), dir, _IOC_NR(cmd), cmd);
	}
}

/* ----------------------------------------------------------------- */

EXPORT_SYMBOL(v4l2_video_std_construct);

EXPORT_SYMBOL(v4l2_prio_init);
EXPORT_SYMBOL(v4l2_prio_change);
EXPORT_SYMBOL(v4l2_prio_open);
EXPORT_SYMBOL(v4l2_prio_close);
EXPORT_SYMBOL(v4l2_prio_max);
EXPORT_SYMBOL(v4l2_prio_check);

EXPORT_SYMBOL(v4l2_field_names);
EXPORT_SYMBOL(v4l2_type_names);
EXPORT_SYMBOL(v4l_printk_ioctl);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
