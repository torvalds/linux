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
	if (id & V4L2_STD_525_60) {
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
/* some arrays for pretty-printing debug messages of enum types      */

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

static char *v4l2_memory_names[] = {
	[V4L2_MEMORY_MMAP]    = "mmap",
	[V4L2_MEMORY_USERPTR] = "userptr",
	[V4L2_MEMORY_OVERLAY] = "overlay",
};

#define prt_names(a,arr) (((a)>=0)&&((a)<ARRAY_SIZE(arr)))?arr[a]:"unknown"

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

static void v4l_print_pix_fmt (char *s, struct v4l2_pix_format *fmt)
{
	printk ("%s: width=%d, height=%d, format=%d, field=%s, "
		"bytesperline=%d sizeimage=%d, colorspace=%d\n", s,
		fmt->width,fmt->height,fmt->pixelformat,
		prt_names(fmt->field,v4l2_field_names),
		fmt->bytesperline,fmt->sizeimage,fmt->colorspace);
};

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

/* Common ioctl debug function. This function can be used by
   external ioctl messages as well as internal V4L ioctl and its
   arguments */
void v4l_printk_ioctl_arg(char *s,unsigned int cmd, void *arg)
{
	printk(s);
	printk(": ");
	v4l_printk_ioctl(cmd);
	switch (cmd) {
	case VIDIOC_INT_G_CHIP_IDENT:
	{
		enum v4l2_chip_ident  *p=arg;
		printk ("%s: chip ident=%d\n", s, *p);
		break;
	}
	case VIDIOC_G_PRIORITY:
	case VIDIOC_S_PRIORITY:
	{
		enum v4l2_priority *p=arg;
		printk ("%s: priority=%d\n", s, *p);
		break;
	}
	case VIDIOC_INT_S_TUNER_MODE:
	{
		enum v4l2_tuner_type *p=arg;
		printk ("%s: tuner type=%d\n", s, *p);
		break;
	}
	case DECODER_SET_VBI_BYPASS:
	case DECODER_ENABLE_OUTPUT:
	case DECODER_GET_STATUS:
	case DECODER_SET_OUTPUT:
	case DECODER_SET_INPUT:
	case DECODER_SET_GPIO:
	case DECODER_SET_NORM:
	case VIDIOCCAPTURE:
	case VIDIOCSYNC:
	case VIDIOCSWRITEMODE:
	case TUNER_SET_TYPE_ADDR:
	case TUNER_SET_STANDBY:
	case TDA9887_SET_CONFIG:
	case AUDC_SET_INPUT:
	case VIDIOC_OVERLAY_OLD:
	case VIDIOC_STREAMOFF:
	case VIDIOC_G_OUTPUT:
	case VIDIOC_S_OUTPUT:
	case VIDIOC_STREAMON:
	case VIDIOC_G_INPUT:
	case VIDIOC_OVERLAY:
	case VIDIOC_S_INPUT:
	{
		int *p=arg;
		printk ("%s: value=%d\n", s, *p);
		break;
	}
	case MSP_SET_MATRIX:
	{
		struct msp_matrix *p=arg;
		printk ("%s: input=%d, output=%d\n", s, p->input, p->output);
		break;
	}
	case VIDIOC_G_AUDIO:
	case VIDIOC_S_AUDIO:
	case VIDIOC_ENUMAUDIO:
	case VIDIOC_G_AUDIO_OLD:
	{
		struct v4l2_audio *p=arg;

		printk ("%s: index=%d, name=%s, capability=%d, mode=%d\n",
			s,p->index, p->name,p->capability, p->mode);
		break;
	}
	case VIDIOC_G_AUDOUT:
	case VIDIOC_S_AUDOUT:
	case VIDIOC_ENUMAUDOUT:
	case VIDIOC_G_AUDOUT_OLD:
	{
		struct v4l2_audioout *p=arg;
		printk ("%s: index=%d, name=%s, capability=%d, mode=%d\n", s,
				p->index, p->name, p->capability,p->mode);
		break;
	}
	case VIDIOC_QBUF:
	case VIDIOC_DQBUF:
	case VIDIOC_QUERYBUF:
	{
		struct v4l2_buffer *p=arg;
		struct v4l2_timecode *tc=&p->timecode;
		printk ("%s: %02ld:%02d:%02d.%08ld index=%d, type=%s, "
			"bytesused=%d, flags=0x%08d, "
			"field=%0d, sequence=%d, memory=%s, offset/userptr=0x%08lx\n",
				s,
				(p->timestamp.tv_sec/3600),
				(int)(p->timestamp.tv_sec/60)%60,
				(int)(p->timestamp.tv_sec%60),
				p->timestamp.tv_usec,
				p->index,
				prt_names(p->type,v4l2_type_names),
				p->bytesused,p->flags,
				p->field,p->sequence,
				prt_names(p->memory,v4l2_memory_names),
				p->m.userptr);
		printk ("%s: timecode= %02d:%02d:%02d type=%d, "
			"flags=0x%08d, frames=%d, userbits=0x%08x",
				s,tc->hours,tc->minutes,tc->seconds,
				tc->type, tc->flags, tc->frames, (__u32) tc->userbits);
		break;
	}
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *p=arg;
		printk ("%s: driver=%s, card=%s, bus=%s, version=%d, "
			"capabilities=%d\n", s,
				p->driver,p->card,p->bus_info,
				p->version,
				p->capabilities);
		break;
	}
	case VIDIOC_G_CTRL:
	case VIDIOC_S_CTRL:
	case VIDIOC_S_CTRL_OLD:
	{
		struct v4l2_control *p=arg;
		printk ("%s: id=%d, value=%d\n", s, p->id, p->value);
		break;
	}
	case VIDIOC_G_CROP:
	case VIDIOC_S_CROP:
	{
		struct v4l2_crop *p=arg;
		/*FIXME: Should also show rect structs */
		printk ("%s: type=%d\n", s, p->type);
		break;
	}
	case VIDIOC_CROPCAP:
	case VIDIOC_CROPCAP_OLD:
	{
		struct v4l2_cropcap *p=arg;
		/*FIXME: Should also show rect structs */
		printk ("%s: type=%d\n", s, p->type);
		break;
	}
	case VIDIOC_INT_DECODE_VBI_LINE:
	{
		struct v4l2_decode_vbi_line *p=arg;
		printk ("%s: is_second_field=%d, ptr=0x%08lx, line=%d, "
			"type=%d\n", s,
				p->is_second_field,(unsigned long)p->p,p->line,p->type);
		break;
	}
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *p=arg;
		printk ("%s: index=%d, type=%d, flags=%d, description=%s,"
			" pixelformat=%d\n", s,
				p->index, p->type, p->flags,p->description,
				p->pixelformat);

		break;
	}
	case VIDIOC_G_FMT:
	case VIDIOC_S_FMT:
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *p=arg;
		printk ("%s: type=%s\n", s,
				prt_names(p->type,v4l2_type_names));
		switch (p->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			v4l_print_pix_fmt (s, &p->fmt.pix);
			break;
		default:
			break;
		}
	}
	case VIDIOC_G_FBUF:
	case VIDIOC_S_FBUF:
	{
		struct v4l2_framebuffer *p=arg;
		printk ("%s: capability=%d, flags=%d, base=0x%08lx\n", s,
				p->capability,p->flags, (unsigned long)p->base);
		v4l_print_pix_fmt (s, &p->fmt);
		break;
	}
	case VIDIOC_G_FREQUENCY:
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *p=arg;
		printk ("%s: tuner=%d, type=%d, frequency=%d\n", s,
				p->tuner,p->type,p->frequency);
		break;
	}
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *p=arg;
		printk ("%s: index=%d, name=%s, type=%d, audioset=%d, "
			"tuner=%d, std=%lld, status=%d\n", s,
				p->index,p->name,p->type,p->audioset,
				p->tuner,p->std,
				p->status);
		break;
	}
	case VIDIOC_G_JPEGCOMP:
	case VIDIOC_S_JPEGCOMP:
	{
		struct v4l2_jpegcompression *p=arg;
		printk ("%s: quality=%d, APPn=%d, APP_len=%d, COM_len=%d,"
			" jpeg_markers=%d\n", s,
				p->quality,p->APPn,p->APP_len,
				p->COM_len,p->jpeg_markers);
		break;
	}
	case VIDIOC_G_MODULATOR:
	case VIDIOC_S_MODULATOR:
	{
		struct v4l2_modulator *p=arg;
		printk ("%s: index=%d, name=%s, capability=%d, rangelow=%d,"
			" rangehigh=%d, txsubchans=%d\n", s,
				p->index, p->name,p->capability,p->rangelow,
				p->rangehigh,p->txsubchans);
		break;
	}
	case VIDIOC_G_MPEGCOMP:
	case VIDIOC_S_MPEGCOMP:
	{
		struct v4l2_mpeg_compression *p=arg;
		/*FIXME: Several fields not shown */
		printk ("%s: ts_pid_pmt=%d, ts_pid_audio=%d, ts_pid_video=%d, "
			"ts_pid_pcr=%d, ps_size=%d, au_sample_rate=%d, "
			"au_pesid=%c, vi_frame_rate=%d, vi_frames_per_gop=%d, "
			"vi_bframes_count=%d, vi_pesid=%c\n", s,
				p->ts_pid_pmt,p->ts_pid_audio, p->ts_pid_video,
				p->ts_pid_pcr, p->ps_size, p->au_sample_rate,
				p->au_pesid, p->vi_frame_rate,
				p->vi_frames_per_gop, p->vi_bframes_count,
				p->vi_pesid);
		break;
	}
	case VIDIOC_ENUMOUTPUT:
	{
		struct v4l2_output *p=arg;
		printk ("%s: index=%d, name=%s,type=%d, audioset=%d, "
			"modulator=%d, std=%lld\n",
				s,p->index,p->name,p->type,p->audioset,
				p->modulator,p->std);
		break;
	}
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *p=arg;
		printk ("%s: id=%d, type=%d, name=%s, min/max=%d/%d,"
			" step=%d, default=%d, flags=0x%08x\n", s,
				p->id,p->type,p->name,p->minimum,p->maximum,
				p->step,p->default_value,p->flags);
		break;
	}
	case VIDIOC_QUERYMENU:
	{
		struct v4l2_querymenu *p=arg;
		printk ("%s: id=%d, index=%d, name=%s\n", s,
				p->id,p->index,p->name);
		break;
	}
	case VIDIOC_INT_G_REGISTER:
	case VIDIOC_INT_S_REGISTER:
	{
		struct v4l2_register *p=arg;
		printk ("%s: i2c_id=%d, reg=%lu, val=%d\n", s,
				p->i2c_id,p->reg,p->val);

		break;
	}
	case VIDIOC_REQBUFS:
	{
		struct v4l2_requestbuffers *p=arg;
		printk ("%s: count=%d, type=%s, memory=%s\n", s,
				p->count,
				prt_names(p->type,v4l2_type_names),
				prt_names(p->memory,v4l2_memory_names));
		break;
	}
	case VIDIOC_INT_S_AUDIO_ROUTING:
	case VIDIOC_INT_S_VIDEO_ROUTING:
	case VIDIOC_INT_G_AUDIO_ROUTING:
	case VIDIOC_INT_G_VIDEO_ROUTING:
	{
		struct v4l2_routing  *p=arg;
		printk ("%s: input=%d, output=%d\n", s, p->input, p->output);
		break;
	}
	case VIDIOC_G_SLICED_VBI_CAP:
	{
		struct v4l2_sliced_vbi_cap *p=arg;
		printk ("%s: service_set=%d\n", s,
				p->service_set);
		break;
	}
	case VIDIOC_INT_S_VBI_DATA:
	case VIDIOC_INT_G_VBI_DATA:
	{
		struct v4l2_sliced_vbi_data  *p=arg;
		printk ("%s: id=%d, field=%d, line=%d\n", s,
				p->id, p->field, p->line);
		break;
	}
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *p=arg;
		printk ("%s: index=%d, id=%lld, name=%s, fps=%d/%d, framelines=%d\n", s,
				p->index, p->id, p->name,
				p->frameperiod.numerator,
				p->frameperiod.denominator,
				p->framelines);

		break;
	}
	case VIDIOC_G_PARM:
	case VIDIOC_S_PARM:
	case VIDIOC_S_PARM_OLD:
	{
		struct v4l2_streamparm *p=arg;
		printk ("%s: type=%d\n", s, p->type);

		break;
	}
	case VIDIOC_G_TUNER:
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *p=arg;
		printk ("%s: index=%d, name=%s, type=%d, capability=%d, "
			"rangelow=%d, rangehigh=%d, signal=%d, afc=%d, "
			"rxsubchans=%d, audmode=%d\n", s,
				p->index, p->name, p->type,
				p->capability, p->rangelow,p->rangehigh,
				p->rxsubchans, p->audmode, p->signal,
				p->afc);
		break;
	}
	case VIDIOCGVBIFMT:
	case VIDIOCSVBIFMT:
	{
		struct vbi_format *p=arg;
		printk ("%s: sampling_rate=%d, samples_per_line=%d, "
			"sample_format=%d, start=%d/%d, count=%d/%d, flags=%d\n", s,
				p->sampling_rate,p->samples_per_line,
				p->sample_format,p->start[0],p->start[1],
				p->count[0],p->count[1],p->flags);
		break;
	}
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
	{
		struct video_audio *p=arg;
		printk ("%s: audio=%d, volume=%d, bass=%d, treble=%d, "
			"flags=%d, name=%s, mode=%d, balance=%d, step=%d\n",
				s,p->audio,p->volume,p->bass, p->treble,
				p->flags,p->name,p->mode,p->balance,p->step);
		break;
	}
	case VIDIOCGFBUF:
	case VIDIOCSFBUF:
	{
		struct video_buffer *p=arg;
		printk ("%s: base=%08lx, height=%d, width=%d, depth=%d, "
			"bytesperline=%d\n", s,
				(unsigned long) p->base, p->height, p->width,
				p->depth,p->bytesperline);
		break;
	}
	case VIDIOCGCAP:
	{
		struct video_capability *p=arg;
		printk ("%s: name=%s, type=%d, channels=%d, audios=%d, "
			"maxwidth=%d, maxheight=%d, minwidth=%d, minheight=%d\n",
				s,p->name,p->type,p->channels,p->audios,
				p->maxwidth,p->maxheight,p->minwidth,
				p->minheight);

		break;
	}
	case VIDIOCGCAPTURE:
	case VIDIOCSCAPTURE:
	{
		struct video_capture *p=arg;
		printk ("%s: x=%d, y=%d, width=%d, height=%d, decimation=%d,"
			" flags=%d\n", s,
				p->x, p->y,p->width, p->height,
				p->decimation,p->flags);
		break;
	}
	case VIDIOCGCHAN:
	case VIDIOCSCHAN:
	{
		struct video_channel *p=arg;
		printk ("%s: channel=%d, name=%s, tuners=%d, flags=%d, "
			"type=%d, norm=%d\n", s,
				p->channel,p->name,p->tuners,
				p->flags,p->type,p->norm);

		break;
	}
	case VIDIOCSMICROCODE:
	{
		struct video_code *p=arg;
		printk ("%s: loadwhat=%s, datasize=%d\n", s,
				p->loadwhat,p->datasize);
		break;
	}
	case DECODER_GET_CAPABILITIES:
	{
		struct video_decoder_capability *p=arg;
		printk ("%s: flags=%d, inputs=%d, outputs=%d\n", s,
				p->flags,p->inputs,p->outputs);
		break;
	}
	case DECODER_INIT:
	{
		struct video_decoder_init *p=arg;
		printk ("%s: len=%c\n", s, p->len);
		break;
	}
	case VIDIOCGPLAYINFO:
	{
		struct video_info *p=arg;
		printk ("%s: frame_count=%d, h_size=%d, v_size=%d, "
			"smpte_timecode=%d, picture_type=%d, "
			"temporal_reference=%d, user_data=%s\n", s,
				p->frame_count, p->h_size,
				p->v_size, p->smpte_timecode,
				p->picture_type, p->temporal_reference,
				p->user_data);
		break;
	}
	case VIDIOCKEY:
	{
		struct video_key *p=arg;
		printk ("%s: key=%s, flags=%d\n", s,
				p->key, p->flags);
		break;
	}
	case VIDIOCGMBUF:
	{
		struct video_mbuf *p=arg;
		printk ("%s: size=%d, frames=%d, offsets=0x%08lx\n", s,
				p->size,
				p->frames,
				(unsigned long)p->offsets);
		break;
	}
	case VIDIOCMCAPTURE:
	{
		struct video_mmap *p=arg;
		printk ("%s: frame=%d, height=%d, width=%d, format=%d\n", s,
				p->frame,
				p->height, p->width,
				p->format);
		break;
	}
	case VIDIOCGPICT:
	case VIDIOCSPICT:
	case DECODER_SET_PICTURE:
	{
		struct video_picture *p=arg;

		printk ("%s: brightness=%d, hue=%d, colour=%d, contrast=%d,"
			" whiteness=%d, depth=%d, palette=%d\n", s,
				p->brightness, p->hue, p->colour,
				p->contrast, p->whiteness, p->depth,
				p->palette);
		break;
	}
	case VIDIOCSPLAYMODE:
	{
		struct video_play_mode *p=arg;
		printk ("%s: mode=%d, p1=%d, p2=%d\n", s,
				p->mode,p->p1,p->p2);
		break;
	}
	case VIDIOCGTUNER:
	case VIDIOCSTUNER:
	{
		struct video_tuner *p=arg;
		printk ("%s: tuner=%d, name=%s, rangelow=%ld, rangehigh=%ld, "
			"flags=%d, mode=%d, signal=%d\n", s,
				p->tuner, p->name,p->rangelow, p->rangehigh,
				p->flags,p->mode, p->signal);
		break;
	}
	case VIDIOCGUNIT:
	{
		struct video_unit *p=arg;
		printk ("%s: video=%d, vbi=%d, radio=%d, audio=%d, "
			"teletext=%d\n", s,
				p->video,p->vbi,p->radio,p->audio,p->teletext);
		break;
	}
	case VIDIOCGWIN:
	case VIDIOCSWIN:
	{
		struct video_window *p=arg;
		printk ("%s: x=%d, y=%d, width=%d, height=%d, chromakey=%d,"
			" flags=%d, clipcount=%d\n", s,
				p->x, p->y,p->width, p->height,
				p->chromakey,p->flags,
				p->clipcount);
		break;
	}
	case VIDIOC_INT_AUDIO_CLOCK_FREQ:
	case VIDIOC_INT_I2S_CLOCK_FREQ:
	case VIDIOC_INT_S_STANDBY:
	{
		u32 *p=arg;

		printk ("%s: value=%d\n", s, *p);
		break;
	}
	case VIDIOCGFREQ:
	case VIDIOCSFREQ:
	{
		unsigned long *p=arg;
		printk ("%s: value=%lu\n", s, *p);
		break;
	}
	case VIDIOC_G_STD:
	case VIDIOC_S_STD:
	case VIDIOC_QUERYSTD:
	{
		v4l2_std_id *p=arg;

		printk ("%s: value=%llu\n", s, *p);
		break;
	}
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
EXPORT_SYMBOL(v4l_printk_ioctl_arg);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
