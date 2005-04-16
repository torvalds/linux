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

#if 0 /* seems to have no users */
/* This is the recommended method to deal with the framerate fields. More
   sophisticated drivers will access the fields directly. */
unsigned int
v4l2_video_std_fps(struct v4l2_standard *vs)
{
	if (vs->frameperiod.numerator > 0)
		return (((vs->frameperiod.denominator << 8) /
			 vs->frameperiod.numerator) +
			(1 << 7)) / (1 << 8);
	return 0;
}
EXPORT_SYMBOL(v4l2_video_std_fps);
#endif

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

char *v4l2_ioctl_names[256] = {
#if __GNUC__ >= 3
	[0 ... 255]                      = "UNKNOWN",
#endif
	[_IOC_NR(VIDIOC_QUERYCAP)]       = "VIDIOC_QUERYCAP",
	[_IOC_NR(VIDIOC_RESERVED)]       = "VIDIOC_RESERVED",
	[_IOC_NR(VIDIOC_ENUM_FMT)]       = "VIDIOC_ENUM_FMT",
	[_IOC_NR(VIDIOC_G_FMT)]          = "VIDIOC_G_FMT",
	[_IOC_NR(VIDIOC_S_FMT)]          = "VIDIOC_S_FMT",
#if 0
	[_IOC_NR(VIDIOC_G_COMP)]         = "VIDIOC_G_COMP",
	[_IOC_NR(VIDIOC_S_COMP)]         = "VIDIOC_S_COMP",
#endif
	[_IOC_NR(VIDIOC_REQBUFS)]        = "VIDIOC_REQBUFS",
	[_IOC_NR(VIDIOC_QUERYBUF)]       = "VIDIOC_QUERYBUF",
	[_IOC_NR(VIDIOC_G_FBUF)]         = "VIDIOC_G_FBUF",
	[_IOC_NR(VIDIOC_S_FBUF)]         = "VIDIOC_S_FBUF",
	[_IOC_NR(VIDIOC_OVERLAY)]        = "VIDIOC_OVERLAY",
	[_IOC_NR(VIDIOC_QBUF)]           = "VIDIOC_QBUF",
	[_IOC_NR(VIDIOC_DQBUF)]          = "VIDIOC_DQBUF",
	[_IOC_NR(VIDIOC_STREAMON)]       = "VIDIOC_STREAMON",
	[_IOC_NR(VIDIOC_STREAMOFF)]      = "VIDIOC_STREAMOFF",
	[_IOC_NR(VIDIOC_G_PARM)]         = "VIDIOC_G_PARM",
	[_IOC_NR(VIDIOC_S_PARM)]         = "VIDIOC_S_PARM",
	[_IOC_NR(VIDIOC_G_STD)]          = "VIDIOC_G_STD",
	[_IOC_NR(VIDIOC_S_STD)]          = "VIDIOC_S_STD",
	[_IOC_NR(VIDIOC_ENUMSTD)]        = "VIDIOC_ENUMSTD",
	[_IOC_NR(VIDIOC_ENUMINPUT)]      = "VIDIOC_ENUMINPUT",
	[_IOC_NR(VIDIOC_G_CTRL)]         = "VIDIOC_G_CTRL",
	[_IOC_NR(VIDIOC_S_CTRL)]         = "VIDIOC_S_CTRL",
	[_IOC_NR(VIDIOC_G_TUNER)]        = "VIDIOC_G_TUNER",
	[_IOC_NR(VIDIOC_S_TUNER)]        = "VIDIOC_S_TUNER",
	[_IOC_NR(VIDIOC_G_AUDIO)]        = "VIDIOC_G_AUDIO",
	[_IOC_NR(VIDIOC_S_AUDIO)]        = "VIDIOC_S_AUDIO",
	[_IOC_NR(VIDIOC_QUERYCTRL)]      = "VIDIOC_QUERYCTRL",
	[_IOC_NR(VIDIOC_QUERYMENU)]      = "VIDIOC_QUERYMENU",
	[_IOC_NR(VIDIOC_G_INPUT)]        = "VIDIOC_G_INPUT",
	[_IOC_NR(VIDIOC_S_INPUT)]        = "VIDIOC_S_INPUT",
	[_IOC_NR(VIDIOC_G_OUTPUT)]       = "VIDIOC_G_OUTPUT",
	[_IOC_NR(VIDIOC_S_OUTPUT)]       = "VIDIOC_S_OUTPUT",
	[_IOC_NR(VIDIOC_ENUMOUTPUT)]     = "VIDIOC_ENUMOUTPUT",
	[_IOC_NR(VIDIOC_G_AUDOUT)]       = "VIDIOC_G_AUDOUT",
	[_IOC_NR(VIDIOC_S_AUDOUT)]       = "VIDIOC_S_AUDOUT",
	[_IOC_NR(VIDIOC_G_MODULATOR)]    = "VIDIOC_G_MODULATOR",
	[_IOC_NR(VIDIOC_S_MODULATOR)]    = "VIDIOC_S_MODULATOR",
	[_IOC_NR(VIDIOC_G_FREQUENCY)]    = "VIDIOC_G_FREQUENCY",
	[_IOC_NR(VIDIOC_S_FREQUENCY)]    = "VIDIOC_S_FREQUENCY",
	[_IOC_NR(VIDIOC_CROPCAP)]        = "VIDIOC_CROPCAP",
	[_IOC_NR(VIDIOC_G_CROP)]         = "VIDIOC_G_CROP",
	[_IOC_NR(VIDIOC_S_CROP)]         = "VIDIOC_S_CROP",
	[_IOC_NR(VIDIOC_G_JPEGCOMP)]     = "VIDIOC_G_JPEGCOMP",
	[_IOC_NR(VIDIOC_S_JPEGCOMP)]     = "VIDIOC_S_JPEGCOMP",
	[_IOC_NR(VIDIOC_QUERYSTD)]       = "VIDIOC_QUERYSTD",
	[_IOC_NR(VIDIOC_TRY_FMT)]        = "VIDIOC_TRY_FMT",
};

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
EXPORT_SYMBOL(v4l2_ioctl_names);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
