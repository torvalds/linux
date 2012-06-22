/*
 * Video capture interface for Linux version 2
 *
 * A generic framework to process V4L2 ioctl commands.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Authors:	Alan Cox, <alan@lxorguk.ukuu.org.uk> (version 1)
 *              Mauro Carvalho Chehab <mchehab@infradead.org> (version 2)
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>

#include <linux/videodev2.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>

#define dbgarg(cmd, fmt, arg...) \
		do {							\
		    if (vfd->debug & V4L2_DEBUG_IOCTL_ARG) {		\
			printk(KERN_DEBUG "%s: ",  vfd->name);		\
			v4l_printk_ioctl(cmd);				\
			printk(" " fmt,  ## arg);			\
		    }							\
		} while (0)

#define dbgarg2(fmt, arg...) \
		do {							\
		    if (vfd->debug & V4L2_DEBUG_IOCTL_ARG)		\
			printk(KERN_DEBUG "%s: " fmt, vfd->name, ## arg);\
		} while (0)

#define dbgarg3(fmt, arg...) \
		do {							\
		    if (vfd->debug & V4L2_DEBUG_IOCTL_ARG)		\
			printk(KERN_CONT "%s: " fmt, vfd->name, ## arg);\
		} while (0)

/* Zero out the end of the struct pointed to by p.  Everything after, but
 * not including, the specified field is cleared. */
#define CLEAR_AFTER_FIELD(p, field) \
	memset((u8 *)(p) + offsetof(typeof(*(p)), field) + sizeof((p)->field), \
	0, sizeof(*(p)) - offsetof(typeof(*(p)), field) - sizeof((p)->field))

struct std_descr {
	v4l2_std_id std;
	const char *descr;
};

static const struct std_descr standards[] = {
	{ V4L2_STD_NTSC, 	"NTSC"      },
	{ V4L2_STD_NTSC_M, 	"NTSC-M"    },
	{ V4L2_STD_NTSC_M_JP, 	"NTSC-M-JP" },
	{ V4L2_STD_NTSC_M_KR,	"NTSC-M-KR" },
	{ V4L2_STD_NTSC_443, 	"NTSC-443"  },
	{ V4L2_STD_PAL, 	"PAL"       },
	{ V4L2_STD_PAL_BG, 	"PAL-BG"    },
	{ V4L2_STD_PAL_B, 	"PAL-B"     },
	{ V4L2_STD_PAL_B1, 	"PAL-B1"    },
	{ V4L2_STD_PAL_G, 	"PAL-G"     },
	{ V4L2_STD_PAL_H, 	"PAL-H"     },
	{ V4L2_STD_PAL_I, 	"PAL-I"     },
	{ V4L2_STD_PAL_DK, 	"PAL-DK"    },
	{ V4L2_STD_PAL_D, 	"PAL-D"     },
	{ V4L2_STD_PAL_D1, 	"PAL-D1"    },
	{ V4L2_STD_PAL_K, 	"PAL-K"     },
	{ V4L2_STD_PAL_M, 	"PAL-M"     },
	{ V4L2_STD_PAL_N, 	"PAL-N"     },
	{ V4L2_STD_PAL_Nc, 	"PAL-Nc"    },
	{ V4L2_STD_PAL_60, 	"PAL-60"    },
	{ V4L2_STD_SECAM, 	"SECAM"     },
	{ V4L2_STD_SECAM_B, 	"SECAM-B"   },
	{ V4L2_STD_SECAM_G, 	"SECAM-G"   },
	{ V4L2_STD_SECAM_H, 	"SECAM-H"   },
	{ V4L2_STD_SECAM_DK, 	"SECAM-DK"  },
	{ V4L2_STD_SECAM_D, 	"SECAM-D"   },
	{ V4L2_STD_SECAM_K, 	"SECAM-K"   },
	{ V4L2_STD_SECAM_K1, 	"SECAM-K1"  },
	{ V4L2_STD_SECAM_L, 	"SECAM-L"   },
	{ V4L2_STD_SECAM_LC, 	"SECAM-Lc"  },
	{ 0, 			"Unknown"   }
};

/* video4linux standard ID conversion to standard name
 */
const char *v4l2_norm_to_name(v4l2_std_id id)
{
	u32 myid = id;
	int i;

	/* HACK: ppc32 architecture doesn't have __ucmpdi2 function to handle
	   64 bit comparations. So, on that architecture, with some gcc
	   variants, compilation fails. Currently, the max value is 30bit wide.
	 */
	BUG_ON(myid != id);

	for (i = 0; standards[i].std; i++)
		if (myid == standards[i].std)
			break;
	return standards[i].descr;
}
EXPORT_SYMBOL(v4l2_norm_to_name);

/* Returns frame period for the given standard */
void v4l2_video_std_frame_period(int id, struct v4l2_fract *frameperiod)
{
	if (id & V4L2_STD_525_60) {
		frameperiod->numerator = 1001;
		frameperiod->denominator = 30000;
	} else {
		frameperiod->numerator = 1;
		frameperiod->denominator = 25;
	}
}
EXPORT_SYMBOL(v4l2_video_std_frame_period);

/* Fill in the fields of a v4l2_standard structure according to the
   'id' and 'transmission' parameters.  Returns negative on error.  */
int v4l2_video_std_construct(struct v4l2_standard *vs,
			     int id, const char *name)
{
	vs->id = id;
	v4l2_video_std_frame_period(id, &vs->frameperiod);
	vs->framelines = (id & V4L2_STD_525_60) ? 525 : 625;
	strlcpy(vs->name, name, sizeof(vs->name));
	return 0;
}
EXPORT_SYMBOL(v4l2_video_std_construct);

/* ----------------------------------------------------------------- */
/* some arrays for pretty-printing debug messages of enum types      */

const char *v4l2_field_names[] = {
	[V4L2_FIELD_ANY]        = "any",
	[V4L2_FIELD_NONE]       = "none",
	[V4L2_FIELD_TOP]        = "top",
	[V4L2_FIELD_BOTTOM]     = "bottom",
	[V4L2_FIELD_INTERLACED] = "interlaced",
	[V4L2_FIELD_SEQ_TB]     = "seq-tb",
	[V4L2_FIELD_SEQ_BT]     = "seq-bt",
	[V4L2_FIELD_ALTERNATE]  = "alternate",
	[V4L2_FIELD_INTERLACED_TB] = "interlaced-tb",
	[V4L2_FIELD_INTERLACED_BT] = "interlaced-bt",
};
EXPORT_SYMBOL(v4l2_field_names);

const char *v4l2_type_names[] = {
	[V4L2_BUF_TYPE_VIDEO_CAPTURE]      = "vid-cap",
	[V4L2_BUF_TYPE_VIDEO_OVERLAY]      = "vid-overlay",
	[V4L2_BUF_TYPE_VIDEO_OUTPUT]       = "vid-out",
	[V4L2_BUF_TYPE_VBI_CAPTURE]        = "vbi-cap",
	[V4L2_BUF_TYPE_VBI_OUTPUT]         = "vbi-out",
	[V4L2_BUF_TYPE_SLICED_VBI_CAPTURE] = "sliced-vbi-cap",
	[V4L2_BUF_TYPE_SLICED_VBI_OUTPUT]  = "sliced-vbi-out",
	[V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY] = "vid-out-overlay",
	[V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE] = "vid-cap-mplane",
	[V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE] = "vid-out-mplane",
};
EXPORT_SYMBOL(v4l2_type_names);

static const char *v4l2_memory_names[] = {
	[V4L2_MEMORY_MMAP]    = "mmap",
	[V4L2_MEMORY_USERPTR] = "userptr",
	[V4L2_MEMORY_OVERLAY] = "overlay",
};

#define prt_names(a, arr) ((((a) >= 0) && ((a) < ARRAY_SIZE(arr))) ? \
			   arr[a] : "unknown")

/* ------------------------------------------------------------------ */
/* debug help functions                                               */

static void v4l_print_querycap(const void *arg, bool write_only)
{
	const struct v4l2_capability *p = arg;

	pr_cont("driver=%s, card=%s, bus=%s, version=0x%08x, "
		"capabilities=0x%08x, device_caps=0x%08x\n",
		p->driver, p->card, p->bus_info,
		p->version, p->capabilities, p->device_caps);
}

static void v4l_print_enuminput(const void *arg, bool write_only)
{
	const struct v4l2_input *p = arg;

	pr_cont("index=%u, name=%s, type=%u, audioset=0x%x, tuner=%u, "
		"std=0x%08Lx, status=0x%x, capabilities=0x%x\n",
		p->index, p->name, p->type, p->audioset, p->tuner,
		(unsigned long long)p->std, p->status, p->capabilities);
}

static void v4l_print_enumoutput(const void *arg, bool write_only)
{
	const struct v4l2_output *p = arg;

	pr_cont("index=%u, name=%s, type=%u, audioset=0x%x, "
		"modulator=%u, std=0x%08Lx, capabilities=0x%x\n",
		p->index, p->name, p->type, p->audioset, p->modulator,
		(unsigned long long)p->std, p->capabilities);
}

static void v4l_print_audio(const void *arg, bool write_only)
{
	const struct v4l2_audio *p = arg;

	if (write_only)
		pr_cont("index=%u, mode=0x%x\n", p->index, p->mode);
	else
		pr_cont("index=%u, name=%s, capability=0x%x, mode=0x%x\n",
			p->index, p->name, p->capability, p->mode);
}

static void v4l_print_audioout(const void *arg, bool write_only)
{
	const struct v4l2_audioout *p = arg;

	if (write_only)
		pr_cont("index=%u\n", p->index);
	else
		pr_cont("index=%u, name=%s, capability=0x%x, mode=0x%x\n",
			p->index, p->name, p->capability, p->mode);
}

static void v4l_print_fmtdesc(const void *arg, bool write_only)
{
	const struct v4l2_fmtdesc *p = arg;

	pr_cont("index=%u, type=%s, flags=0x%x, pixelformat=%c%c%c%c, description='%s'\n",
		p->index, prt_names(p->type, v4l2_type_names),
		p->flags, (p->pixelformat & 0xff),
		(p->pixelformat >>  8) & 0xff,
		(p->pixelformat >> 16) & 0xff,
		(p->pixelformat >> 24) & 0xff,
		p->description);
}

static void v4l_print_format(const void *arg, bool write_only)
{
	const struct v4l2_format *p = arg;
	const struct v4l2_pix_format *pix;
	const struct v4l2_pix_format_mplane *mp;
	const struct v4l2_vbi_format *vbi;
	const struct v4l2_sliced_vbi_format *sliced;
	const struct v4l2_window *win;
	const struct v4l2_clip *clip;
	unsigned i;

	pr_cont("type=%s", prt_names(p->type, v4l2_type_names));
	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		pix = &p->fmt.pix;
		pr_cont(", width=%u, height=%u, "
			"pixelformat=%c%c%c%c, field=%s, "
			"bytesperline=%u sizeimage=%u, colorspace=%d\n",
			pix->width, pix->height,
			(pix->pixelformat & 0xff),
			(pix->pixelformat >>  8) & 0xff,
			(pix->pixelformat >> 16) & 0xff,
			(pix->pixelformat >> 24) & 0xff,
			prt_names(pix->field, v4l2_field_names),
			pix->bytesperline, pix->sizeimage,
			pix->colorspace);
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		mp = &p->fmt.pix_mp;
		pr_cont(", width=%u, height=%u, "
			"format=%c%c%c%c, field=%s, "
			"colorspace=%d, num_planes=%u\n",
			mp->width, mp->height,
			(mp->pixelformat & 0xff),
			(mp->pixelformat >>  8) & 0xff,
			(mp->pixelformat >> 16) & 0xff,
			(mp->pixelformat >> 24) & 0xff,
			prt_names(mp->field, v4l2_field_names),
			mp->colorspace, mp->num_planes);
		for (i = 0; i < mp->num_planes; i++)
			printk(KERN_DEBUG "plane %u: bytesperline=%u sizeimage=%u\n", i,
					mp->plane_fmt[i].bytesperline,
					mp->plane_fmt[i].sizeimage);
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		win = &p->fmt.win;
		pr_cont(", wxh=%dx%d, x,y=%d,%d, field=%s, "
			"chromakey=0x%08x, bitmap=%p, "
			"global_alpha=0x%02x\n",
			win->w.width, win->w.height,
			win->w.left, win->w.top,
			prt_names(win->field, v4l2_field_names),
			win->chromakey, win->bitmap, win->global_alpha);
		clip = win->clips;
		for (i = 0; i < win->clipcount; i++) {
			printk(KERN_DEBUG "clip %u: wxh=%dx%d, x,y=%d,%d\n",
					i, clip->c.width, clip->c.height,
					clip->c.left, clip->c.top);
			clip = clip->next;
		}
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		vbi = &p->fmt.vbi;
		pr_cont(", sampling_rate=%u, offset=%u, samples_per_line=%u, "
			"sample_format=%c%c%c%c, start=%u,%u, count=%u,%u\n",
			vbi->sampling_rate, vbi->offset,
			vbi->samples_per_line,
			(vbi->sample_format & 0xff),
			(vbi->sample_format >>  8) & 0xff,
			(vbi->sample_format >> 16) & 0xff,
			(vbi->sample_format >> 24) & 0xff,
			vbi->start[0], vbi->start[1],
			vbi->count[0], vbi->count[1]);
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		sliced = &p->fmt.sliced;
		pr_cont(", service_set=0x%08x, io_size=%d\n",
				sliced->service_set, sliced->io_size);
		for (i = 0; i < 24; i++)
			printk(KERN_DEBUG "line[%02u]=0x%04x, 0x%04x\n", i,
				sliced->service_lines[0][i],
				sliced->service_lines[1][i]);
		break;
	case V4L2_BUF_TYPE_PRIVATE:
		pr_cont("\n");
		break;
	}
}

static void v4l_print_framebuffer(const void *arg, bool write_only)
{
	const struct v4l2_framebuffer *p = arg;

	pr_cont("capability=0x%x, flags=0x%x, base=0x%p, width=%u, "
		"height=%u, pixelformat=%c%c%c%c, "
		"bytesperline=%u sizeimage=%u, colorspace=%d\n",
			p->capability, p->flags, p->base,
			p->fmt.width, p->fmt.height,
			(p->fmt.pixelformat & 0xff),
			(p->fmt.pixelformat >>  8) & 0xff,
			(p->fmt.pixelformat >> 16) & 0xff,
			(p->fmt.pixelformat >> 24) & 0xff,
			p->fmt.bytesperline, p->fmt.sizeimage,
			p->fmt.colorspace);
}

static void v4l_print_buftype(const void *arg, bool write_only)
{
	pr_cont("type=%s\n", prt_names(*(u32 *)arg, v4l2_type_names));
}

static void v4l_print_modulator(const void *arg, bool write_only)
{
	const struct v4l2_modulator *p = arg;

	if (write_only)
		pr_cont("index=%u, txsubchans=0x%x", p->index, p->txsubchans);
	else
		pr_cont("index=%u, name=%s, capability=0x%x, "
			"rangelow=%u, rangehigh=%u, txsubchans=0x%x\n",
			p->index, p->name, p->capability,
			p->rangelow, p->rangehigh, p->txsubchans);
}

static void v4l_print_tuner(const void *arg, bool write_only)
{
	const struct v4l2_tuner *p = arg;

	if (write_only)
		pr_cont("index=%u, audmode=%u\n", p->index, p->audmode);
	else
		pr_cont("index=%u, name=%s, type=%u, capability=0x%x, "
			"rangelow=%u, rangehigh=%u, signal=%u, afc=%d, "
			"rxsubchans=0x%x, audmode=%u\n",
			p->index, p->name, p->type,
			p->capability, p->rangelow,
			p->rangehigh, p->signal, p->afc,
			p->rxsubchans, p->audmode);
}

static void v4l_print_frequency(const void *arg, bool write_only)
{
	const struct v4l2_frequency *p = arg;

	pr_cont("tuner=%u, type=%u, frequency=%u\n",
				p->tuner, p->type, p->frequency);
}

static void v4l_print_standard(const void *arg, bool write_only)
{
	const struct v4l2_standard *p = arg;

	pr_cont("index=%u, id=0x%Lx, name=%s, fps=%u/%u, "
		"framelines=%u\n", p->index,
		(unsigned long long)p->id, p->name,
		p->frameperiod.numerator,
		p->frameperiod.denominator,
		p->framelines);
}

static void v4l_print_std(const void *arg, bool write_only)
{
	pr_cont("std=0x%08Lx\n", *(const long long unsigned *)arg);
}

static void v4l_print_hw_freq_seek(const void *arg, bool write_only)
{
	const struct v4l2_hw_freq_seek *p = arg;

	pr_cont("tuner=%u, type=%u, seek_upward=%u, wrap_around=%u, spacing=%u\n",
		p->tuner, p->type, p->seek_upward, p->wrap_around, p->spacing);
}

static void v4l_print_requestbuffers(const void *arg, bool write_only)
{
	const struct v4l2_requestbuffers *p = arg;

	pr_cont("count=%d, type=%s, memory=%s\n",
		p->count,
		prt_names(p->type, v4l2_type_names),
		prt_names(p->memory, v4l2_memory_names));
}

static void v4l_print_buffer(const void *arg, bool write_only)
{
	const struct v4l2_buffer *p = arg;
	const struct v4l2_timecode *tc = &p->timecode;
	const struct v4l2_plane *plane;
	int i;

	pr_cont("%02ld:%02d:%02d.%08ld index=%d, type=%s, "
		"flags=0x%08x, field=%s, sequence=%d, memory=%s",
			p->timestamp.tv_sec / 3600,
			(int)(p->timestamp.tv_sec / 60) % 60,
			(int)(p->timestamp.tv_sec % 60),
			(long)p->timestamp.tv_usec,
			p->index,
			prt_names(p->type, v4l2_type_names),
			p->flags, prt_names(p->field, v4l2_field_names),
			p->sequence, prt_names(p->memory, v4l2_memory_names));

	if (V4L2_TYPE_IS_MULTIPLANAR(p->type) && p->m.planes) {
		pr_cont("\n");
		for (i = 0; i < p->length; ++i) {
			plane = &p->m.planes[i];
			printk(KERN_DEBUG
				"plane %d: bytesused=%d, data_offset=0x%08x "
				"offset/userptr=0x%lx, length=%d\n",
				i, plane->bytesused, plane->data_offset,
				plane->m.userptr, plane->length);
		}
	} else {
		pr_cont("bytesused=%d, offset/userptr=0x%lx, length=%d\n",
			p->bytesused, p->m.userptr, p->length);
	}

	printk(KERN_DEBUG "timecode=%02d:%02d:%02d type=%d, "
		"flags=0x%08x, frames=%d, userbits=0x%08x\n",
			tc->hours, tc->minutes, tc->seconds,
			tc->type, tc->flags, tc->frames, *(__u32 *)tc->userbits);
}

static void v4l_print_create_buffers(const void *arg, bool write_only)
{
	const struct v4l2_create_buffers *p = arg;

	pr_cont("index=%d, count=%d, memory=%s, ",
			p->index, p->count,
			prt_names(p->memory, v4l2_memory_names));
	v4l_print_format(&p->format, write_only);
}

static void v4l_print_streamparm(const void *arg, bool write_only)
{
	const struct v4l2_streamparm *p = arg;

	pr_cont("type=%s", prt_names(p->type, v4l2_type_names));

	if (p->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    p->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		const struct v4l2_captureparm *c = &p->parm.capture;

		pr_cont(", capability=0x%x, capturemode=0x%x, timeperframe=%d/%d, "
			"extendedmode=%d, readbuffers=%d\n",
			c->capability, c->capturemode,
			c->timeperframe.numerator, c->timeperframe.denominator,
			c->extendedmode, c->readbuffers);
	} else if (p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT ||
		   p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		const struct v4l2_outputparm *c = &p->parm.output;

		pr_cont(", capability=0x%x, outputmode=0x%x, timeperframe=%d/%d, "
			"extendedmode=%d, writebuffers=%d\n",
			c->capability, c->outputmode,
			c->timeperframe.numerator, c->timeperframe.denominator,
			c->extendedmode, c->writebuffers);
	}
}

static void v4l_print_u32(const void *arg, bool write_only)
{
	pr_cont("value=%u\n", *(const u32 *)arg);
}

static inline void dbgrect(struct video_device *vfd, char *s,
							struct v4l2_rect *r)
{
	dbgarg2("%sRect start at %dx%d, size=%dx%d\n", s, r->left, r->top,
						r->width, r->height);
};

static void dbgtimings(struct video_device *vfd,
			const struct v4l2_dv_timings *p)
{
	switch (p->type) {
	case V4L2_DV_BT_656_1120:
		dbgarg2("bt-656/1120:interlaced=%d,"
				" pixelclock=%lld,"
				" width=%d, height=%d, polarities=%x,"
				" hfrontporch=%d, hsync=%d,"
				" hbackporch=%d, vfrontporch=%d,"
				" vsync=%d, vbackporch=%d,"
				" il_vfrontporch=%d, il_vsync=%d,"
				" il_vbackporch=%d, standards=%x, flags=%x\n",
				p->bt.interlaced, p->bt.pixelclock,
				p->bt.width, p->bt.height,
				p->bt.polarities, p->bt.hfrontporch,
				p->bt.hsync, p->bt.hbackporch,
				p->bt.vfrontporch, p->bt.vsync,
				p->bt.vbackporch, p->bt.il_vfrontporch,
				p->bt.il_vsync, p->bt.il_vbackporch,
				p->bt.standards, p->bt.flags);
		break;
	default:
		dbgarg2("Unknown type %d!\n", p->type);
		break;
	}
}

static inline void v4l_print_ext_ctrls(unsigned int cmd,
	struct video_device *vfd, struct v4l2_ext_controls *c, int show_vals)
{
	__u32 i;

	if (!(vfd->debug & V4L2_DEBUG_IOCTL_ARG))
		return;
	dbgarg(cmd, "");
	printk(KERN_CONT "class=0x%x", c->ctrl_class);
	for (i = 0; i < c->count; i++) {
		if (show_vals && !c->controls[i].size)
			printk(KERN_CONT " id/val=0x%x/0x%x",
				c->controls[i].id, c->controls[i].value);
		else
			printk(KERN_CONT " id=0x%x,size=%u",
				c->controls[i].id, c->controls[i].size);
	}
	printk(KERN_CONT "\n");
};

static inline int check_ext_ctrls(struct v4l2_ext_controls *c, int allow_priv)
{
	__u32 i;

	/* zero the reserved fields */
	c->reserved[0] = c->reserved[1] = 0;
	for (i = 0; i < c->count; i++)
		c->controls[i].reserved2[0] = 0;

	/* V4L2_CID_PRIVATE_BASE cannot be used as control class
	   when using extended controls.
	   Only when passed in through VIDIOC_G_CTRL and VIDIOC_S_CTRL
	   is it allowed for backwards compatibility.
	 */
	if (!allow_priv && c->ctrl_class == V4L2_CID_PRIVATE_BASE)
		return 0;
	/* Check that all controls are from the same control class. */
	for (i = 0; i < c->count; i++) {
		if (V4L2_CTRL_ID2CLASS(c->controls[i].id) != c->ctrl_class) {
			c->error_idx = i;
			return 0;
		}
	}
	return 1;
}

static int check_fmt(const struct v4l2_ioctl_ops *ops, enum v4l2_buf_type type)
{
	if (ops == NULL)
		return -EINVAL;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (ops->vidioc_g_fmt_vid_cap ||
				ops->vidioc_g_fmt_vid_cap_mplane)
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (ops->vidioc_g_fmt_vid_cap_mplane)
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (ops->vidioc_g_fmt_vid_overlay)
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (ops->vidioc_g_fmt_vid_out ||
				ops->vidioc_g_fmt_vid_out_mplane)
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (ops->vidioc_g_fmt_vid_out_mplane)
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		if (ops->vidioc_g_fmt_vid_out_overlay)
			return 0;
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (ops->vidioc_g_fmt_vbi_cap)
			return 0;
		break;
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		if (ops->vidioc_g_fmt_vbi_out)
			return 0;
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		if (ops->vidioc_g_fmt_sliced_vbi_cap)
			return 0;
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		if (ops->vidioc_g_fmt_sliced_vbi_out)
			return 0;
		break;
	case V4L2_BUF_TYPE_PRIVATE:
		if (ops->vidioc_g_fmt_type_private)
			return 0;
		break;
	}
	return -EINVAL;
}

static int v4l_querycap(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_capability *cap = (struct v4l2_capability *)arg;

	cap->version = LINUX_VERSION_CODE;
	return ops->vidioc_querycap(file, fh, cap);
}

static int v4l_s_input(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	return ops->vidioc_s_input(file, fh, *(unsigned int *)arg);
}

static int v4l_s_output(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	return ops->vidioc_s_output(file, fh, *(unsigned int *)arg);
}

static int v4l_g_priority(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd;
	u32 *p = arg;

	if (ops->vidioc_g_priority)
		return ops->vidioc_g_priority(file, fh, arg);
	vfd = video_devdata(file);
	*p = v4l2_prio_max(&vfd->v4l2_dev->prio);
	return 0;
}

static int v4l_s_priority(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd;
	struct v4l2_fh *vfh;
	u32 *p = arg;

	if (ops->vidioc_s_priority)
		return ops->vidioc_s_priority(file, fh, *p);
	vfd = video_devdata(file);
	vfh = file->private_data;
	return v4l2_prio_change(&vfd->v4l2_dev->prio, &vfh->prio, *p);
}

static int v4l_enuminput(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_input *p = arg;

	/*
	 * We set the flags for CAP_PRESETS, CAP_CUSTOM_TIMINGS &
	 * CAP_STD here based on ioctl handler provided by the
	 * driver. If the driver doesn't support these
	 * for a specific input, it must override these flags.
	 */
	if (ops->vidioc_s_std)
		p->capabilities |= V4L2_IN_CAP_STD;
	if (ops->vidioc_s_dv_preset)
		p->capabilities |= V4L2_IN_CAP_PRESETS;
	if (ops->vidioc_s_dv_timings)
		p->capabilities |= V4L2_IN_CAP_CUSTOM_TIMINGS;

	return ops->vidioc_enum_input(file, fh, p);
}

static int v4l_enumoutput(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_output *p = arg;

	/*
	 * We set the flags for CAP_PRESETS, CAP_CUSTOM_TIMINGS &
	 * CAP_STD here based on ioctl handler provided by the
	 * driver. If the driver doesn't support these
	 * for a specific output, it must override these flags.
	 */
	if (ops->vidioc_s_std)
		p->capabilities |= V4L2_OUT_CAP_STD;
	if (ops->vidioc_s_dv_preset)
		p->capabilities |= V4L2_OUT_CAP_PRESETS;
	if (ops->vidioc_s_dv_timings)
		p->capabilities |= V4L2_OUT_CAP_CUSTOM_TIMINGS;

	return ops->vidioc_enum_output(file, fh, p);
}

static int v4l_enum_fmt(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_fmtdesc *p = arg;

	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (unlikely(!ops->vidioc_enum_fmt_vid_cap))
			break;
		return ops->vidioc_enum_fmt_vid_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (unlikely(!ops->vidioc_enum_fmt_vid_cap_mplane))
			break;
		return ops->vidioc_enum_fmt_vid_cap_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (unlikely(!ops->vidioc_enum_fmt_vid_overlay))
			break;
		return ops->vidioc_enum_fmt_vid_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (unlikely(!ops->vidioc_enum_fmt_vid_out))
			break;
		return ops->vidioc_enum_fmt_vid_out(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (unlikely(!ops->vidioc_enum_fmt_vid_out_mplane))
			break;
		return ops->vidioc_enum_fmt_vid_out_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_PRIVATE:
		if (unlikely(!ops->vidioc_enum_fmt_type_private))
			break;
		return ops->vidioc_enum_fmt_type_private(file, fh, arg);
	}
	return -EINVAL;
}

static int v4l_g_fmt(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_format *p = arg;

	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (unlikely(!ops->vidioc_g_fmt_vid_cap))
			break;
		return ops->vidioc_g_fmt_vid_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (unlikely(!ops->vidioc_g_fmt_vid_cap_mplane))
			break;
		return ops->vidioc_g_fmt_vid_cap_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (unlikely(!ops->vidioc_g_fmt_vid_overlay))
			break;
		return ops->vidioc_g_fmt_vid_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (unlikely(!ops->vidioc_g_fmt_vid_out))
			break;
		return ops->vidioc_g_fmt_vid_out(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (unlikely(!ops->vidioc_g_fmt_vid_out_mplane))
			break;
		return ops->vidioc_g_fmt_vid_out_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		if (unlikely(!ops->vidioc_g_fmt_vid_out_overlay))
			break;
		return ops->vidioc_g_fmt_vid_out_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_g_fmt_vbi_cap))
			break;
		return ops->vidioc_g_fmt_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_g_fmt_vbi_out))
			break;
		return ops->vidioc_g_fmt_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_g_fmt_sliced_vbi_cap))
			break;
		return ops->vidioc_g_fmt_sliced_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_g_fmt_sliced_vbi_out))
			break;
		return ops->vidioc_g_fmt_sliced_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_PRIVATE:
		if (unlikely(!ops->vidioc_g_fmt_type_private))
			break;
		return ops->vidioc_g_fmt_type_private(file, fh, arg);
	}
	return -EINVAL;
}

static int v4l_s_fmt(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_format *p = arg;

	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (unlikely(!ops->vidioc_s_fmt_vid_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix);
		return ops->vidioc_s_fmt_vid_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (unlikely(!ops->vidioc_s_fmt_vid_cap_mplane))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix_mp);
		return ops->vidioc_s_fmt_vid_cap_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (unlikely(!ops->vidioc_s_fmt_vid_overlay))
			break;
		CLEAR_AFTER_FIELD(p, fmt.win);
		return ops->vidioc_s_fmt_vid_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (unlikely(!ops->vidioc_s_fmt_vid_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix);
		return ops->vidioc_s_fmt_vid_out(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (unlikely(!ops->vidioc_s_fmt_vid_out_mplane))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix_mp);
		return ops->vidioc_s_fmt_vid_out_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		if (unlikely(!ops->vidioc_s_fmt_vid_out_overlay))
			break;
		CLEAR_AFTER_FIELD(p, fmt.win);
		return ops->vidioc_s_fmt_vid_out_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_s_fmt_vbi_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.vbi);
		return ops->vidioc_s_fmt_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_s_fmt_vbi_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.vbi);
		return ops->vidioc_s_fmt_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_s_fmt_sliced_vbi_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sliced);
		return ops->vidioc_s_fmt_sliced_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_s_fmt_sliced_vbi_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sliced);
		return ops->vidioc_s_fmt_sliced_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_PRIVATE:
		if (unlikely(!ops->vidioc_s_fmt_type_private))
			break;
		return ops->vidioc_s_fmt_type_private(file, fh, arg);
	}
	return -EINVAL;
}

static int v4l_try_fmt(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_format *p = arg;

	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (unlikely(!ops->vidioc_try_fmt_vid_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix);
		return ops->vidioc_try_fmt_vid_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (unlikely(!ops->vidioc_try_fmt_vid_cap_mplane))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix_mp);
		return ops->vidioc_try_fmt_vid_cap_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (unlikely(!ops->vidioc_try_fmt_vid_overlay))
			break;
		CLEAR_AFTER_FIELD(p, fmt.win);
		return ops->vidioc_try_fmt_vid_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (unlikely(!ops->vidioc_try_fmt_vid_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix);
		return ops->vidioc_try_fmt_vid_out(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (unlikely(!ops->vidioc_try_fmt_vid_out_mplane))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix_mp);
		return ops->vidioc_try_fmt_vid_out_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		if (unlikely(!ops->vidioc_try_fmt_vid_out_overlay))
			break;
		CLEAR_AFTER_FIELD(p, fmt.win);
		return ops->vidioc_try_fmt_vid_out_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_try_fmt_vbi_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.vbi);
		return ops->vidioc_try_fmt_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_try_fmt_vbi_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.vbi);
		return ops->vidioc_try_fmt_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_try_fmt_sliced_vbi_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sliced);
		return ops->vidioc_try_fmt_sliced_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_try_fmt_sliced_vbi_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sliced);
		return ops->vidioc_try_fmt_sliced_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_PRIVATE:
		if (unlikely(!ops->vidioc_try_fmt_type_private))
			break;
		return ops->vidioc_try_fmt_type_private(file, fh, arg);
	}
	return -EINVAL;
}

static int v4l_streamon(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	return ops->vidioc_streamon(file, fh, *(unsigned int *)arg);
}

static int v4l_streamoff(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	return ops->vidioc_streamoff(file, fh, *(unsigned int *)arg);
}

static int v4l_g_tuner(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_tuner *p = arg;

	p->type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
			V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	return ops->vidioc_g_tuner(file, fh, p);
}

static int v4l_s_tuner(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_tuner *p = arg;

	p->type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
			V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	return ops->vidioc_s_tuner(file, fh, p);
}

static int v4l_g_frequency(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_frequency *p = arg;

	p->type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
			V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	return ops->vidioc_g_frequency(file, fh, p);
}

static int v4l_s_frequency(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_frequency *p = arg;
	enum v4l2_tuner_type type;

	type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
			V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	if (p->type != type)
		return -EINVAL;
	return ops->vidioc_s_frequency(file, fh, p);
}

static int v4l_enumstd(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_standard *p = arg;
	v4l2_std_id id = vfd->tvnorms, curr_id = 0;
	unsigned int index = p->index, i, j = 0;
	const char *descr = "";

	/* Return norm array in a canonical way */
	for (i = 0; i <= index && id; i++) {
		/* last std value in the standards array is 0, so this
		   while always ends there since (id & 0) == 0. */
		while ((id & standards[j].std) != standards[j].std)
			j++;
		curr_id = standards[j].std;
		descr = standards[j].descr;
		j++;
		if (curr_id == 0)
			break;
		if (curr_id != V4L2_STD_PAL &&
				curr_id != V4L2_STD_SECAM &&
				curr_id != V4L2_STD_NTSC)
			id &= ~curr_id;
	}
	if (i <= index)
		return -EINVAL;

	v4l2_video_std_construct(p, curr_id, descr);
	return 0;
}

static int v4l_g_std(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	v4l2_std_id *id = arg;

	/* Calls the specific handler */
	if (ops->vidioc_g_std)
		return ops->vidioc_g_std(file, fh, arg);
	if (vfd->current_norm) {
		*id = vfd->current_norm;
		return 0;
	}
	return -ENOTTY;
}

static int v4l_s_std(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	v4l2_std_id *id = arg, norm;
	int ret;

	norm = (*id) & vfd->tvnorms;
	if (vfd->tvnorms && !norm)	/* Check if std is supported */
		return -EINVAL;

	/* Calls the specific handler */
	ret = ops->vidioc_s_std(file, fh, &norm);

	/* Updates standard information */
	if (ret >= 0)
		vfd->current_norm = norm;
	return ret;
}

static int v4l_querystd(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	v4l2_std_id *p = arg;

	/*
	 * If nothing detected, it should return all supported
	 * standard.
	 * Drivers just need to mask the std argument, in order
	 * to remove the standards that don't apply from the mask.
	 * This means that tuners, audio and video decoders can join
	 * their efforts to improve the standards detection.
	 */
	*p = vfd->tvnorms;
	return ops->vidioc_querystd(file, fh, arg);
}

static int v4l_s_hw_freq_seek(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_hw_freq_seek *p = arg;
	enum v4l2_tuner_type type;

	type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
		V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	if (p->type != type)
		return -EINVAL;
	return ops->vidioc_s_hw_freq_seek(file, fh, p);
}

static int v4l_reqbufs(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_requestbuffers *p = arg;
	int ret = check_fmt(ops, p->type);

	if (ret)
		return ret;

	if (p->type < V4L2_BUF_TYPE_PRIVATE)
		CLEAR_AFTER_FIELD(p, memory);

	return ops->vidioc_reqbufs(file, fh, p);
}

static int v4l_querybuf(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_buffer *p = arg;
	int ret = check_fmt(ops, p->type);

	return ret ? ret : ops->vidioc_querybuf(file, fh, p);
}

static int v4l_qbuf(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_buffer *p = arg;
	int ret = check_fmt(ops, p->type);

	return ret ? ret : ops->vidioc_qbuf(file, fh, p);
}

static int v4l_dqbuf(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_buffer *p = arg;
	int ret = check_fmt(ops, p->type);

	return ret ? ret : ops->vidioc_dqbuf(file, fh, p);
}

static int v4l_create_bufs(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_create_buffers *create = arg;
	int ret = check_fmt(ops, create->format.type);

	return ret ? ret : ops->vidioc_create_bufs(file, fh, create);
}

static int v4l_prepare_buf(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_buffer *b = arg;
	int ret = check_fmt(ops, b->type);

	return ret ? ret : ops->vidioc_prepare_buf(file, fh, b);
}

static int v4l_g_parm(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_streamparm *p = arg;
	v4l2_std_id std;
	int ret = check_fmt(ops, p->type);

	if (ret)
		return ret;
	if (ops->vidioc_g_parm)
		return ops->vidioc_g_parm(file, fh, p);
	std = vfd->current_norm;
	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;
	p->parm.capture.readbuffers = 2;
	if (ops->vidioc_g_std)
		ret = ops->vidioc_g_std(file, fh, &std);
	if (ret == 0)
		v4l2_video_std_frame_period(std,
			    &p->parm.capture.timeperframe);
	return ret;
}

static int v4l_s_parm(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_streamparm *p = arg;
	int ret = check_fmt(ops, p->type);

	return ret ? ret : ops->vidioc_s_parm(file, fh, p);
}

struct v4l2_ioctl_info {
	unsigned int ioctl;
	u32 flags;
	const char * const name;
	union {
		u32 offset;
		int (*func)(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *p);
	};
	void (*debug)(const void *arg, bool write_only);
};

/* This control needs a priority check */
#define INFO_FL_PRIO	(1 << 0)
/* This control can be valid if the filehandle passes a control handler. */
#define INFO_FL_CTRL	(1 << 1)
/* This is a standard ioctl, no need for special code */
#define INFO_FL_STD	(1 << 2)
/* This is ioctl has its own function */
#define INFO_FL_FUNC	(1 << 3)
/* Zero struct from after the field to the end */
#define INFO_FL_CLEAR(v4l2_struct, field)			\
	((offsetof(struct v4l2_struct, field) +			\
	  sizeof(((struct v4l2_struct *)0)->field)) << 16)
#define INFO_FL_CLEAR_MASK (_IOC_SIZEMASK << 16)

#define IOCTL_INFO(_ioctl, _flags) [_IOC_NR(_ioctl)] = {	\
	.ioctl = _ioctl,					\
	.flags = _flags,					\
	.name = #_ioctl,					\
}

#define IOCTL_INFO_STD(_ioctl, _vidioc, _debug, _flags)			\
	[_IOC_NR(_ioctl)] = {						\
		.ioctl = _ioctl,					\
		.flags = _flags | INFO_FL_STD,				\
		.name = #_ioctl,					\
		.offset = offsetof(struct v4l2_ioctl_ops, _vidioc),	\
		.debug = _debug,					\
	}

#define IOCTL_INFO_FNC(_ioctl, _func, _debug, _flags)			\
	[_IOC_NR(_ioctl)] = {						\
		.ioctl = _ioctl,					\
		.flags = _flags | INFO_FL_FUNC,				\
		.name = #_ioctl,					\
		.func = _func,						\
		.debug = _debug,					\
	}

static struct v4l2_ioctl_info v4l2_ioctls[] = {
	IOCTL_INFO_FNC(VIDIOC_QUERYCAP, v4l_querycap, v4l_print_querycap, 0),
	IOCTL_INFO_FNC(VIDIOC_ENUM_FMT, v4l_enum_fmt, v4l_print_fmtdesc, INFO_FL_CLEAR(v4l2_fmtdesc, type)),
	IOCTL_INFO_FNC(VIDIOC_G_FMT, v4l_g_fmt, v4l_print_format, INFO_FL_CLEAR(v4l2_format, type)),
	IOCTL_INFO_FNC(VIDIOC_S_FMT, v4l_s_fmt, v4l_print_format, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_REQBUFS, v4l_reqbufs, v4l_print_requestbuffers, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_QUERYBUF, v4l_querybuf, v4l_print_buffer, INFO_FL_CLEAR(v4l2_buffer, length)),
	IOCTL_INFO_STD(VIDIOC_G_FBUF, vidioc_g_fbuf, v4l_print_framebuffer, 0),
	IOCTL_INFO_STD(VIDIOC_S_FBUF, vidioc_s_fbuf, v4l_print_framebuffer, INFO_FL_PRIO),
	IOCTL_INFO_STD(VIDIOC_OVERLAY, vidioc_overlay, v4l_print_u32, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_QBUF, v4l_qbuf, v4l_print_buffer, 0),
	IOCTL_INFO_FNC(VIDIOC_DQBUF, v4l_dqbuf, v4l_print_buffer, 0),
	IOCTL_INFO_FNC(VIDIOC_STREAMON, v4l_streamon, v4l_print_buftype, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_STREAMOFF, v4l_streamoff, v4l_print_buftype, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_G_PARM, v4l_g_parm, v4l_print_streamparm, INFO_FL_CLEAR(v4l2_streamparm, type)),
	IOCTL_INFO_FNC(VIDIOC_S_PARM, v4l_s_parm, v4l_print_streamparm, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_G_STD, v4l_g_std, v4l_print_std, 0),
	IOCTL_INFO_FNC(VIDIOC_S_STD, v4l_s_std, v4l_print_std, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_ENUMSTD, v4l_enumstd, v4l_print_standard, INFO_FL_CLEAR(v4l2_standard, index)),
	IOCTL_INFO_FNC(VIDIOC_ENUMINPUT, v4l_enuminput, v4l_print_enuminput, INFO_FL_CLEAR(v4l2_input, index)),
	IOCTL_INFO(VIDIOC_G_CTRL, INFO_FL_CTRL),
	IOCTL_INFO(VIDIOC_S_CTRL, INFO_FL_PRIO | INFO_FL_CTRL),
	IOCTL_INFO_FNC(VIDIOC_G_TUNER, v4l_g_tuner, v4l_print_tuner, INFO_FL_CLEAR(v4l2_tuner, index)),
	IOCTL_INFO_FNC(VIDIOC_S_TUNER, v4l_s_tuner, v4l_print_tuner, INFO_FL_PRIO),
	IOCTL_INFO_STD(VIDIOC_G_AUDIO, vidioc_g_audio, v4l_print_audio, 0),
	IOCTL_INFO_STD(VIDIOC_S_AUDIO, vidioc_s_audio, v4l_print_audio, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_QUERYCTRL, INFO_FL_CTRL | INFO_FL_CLEAR(v4l2_queryctrl, id)),
	IOCTL_INFO(VIDIOC_QUERYMENU, INFO_FL_CTRL | INFO_FL_CLEAR(v4l2_querymenu, index)),
	IOCTL_INFO_STD(VIDIOC_G_INPUT, vidioc_g_input, v4l_print_u32, 0),
	IOCTL_INFO_FNC(VIDIOC_S_INPUT, v4l_s_input, v4l_print_u32, INFO_FL_PRIO),
	IOCTL_INFO_STD(VIDIOC_G_OUTPUT, vidioc_g_output, v4l_print_u32, 0),
	IOCTL_INFO_FNC(VIDIOC_S_OUTPUT, v4l_s_output, v4l_print_u32, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_ENUMOUTPUT, v4l_enumoutput, v4l_print_enumoutput, INFO_FL_CLEAR(v4l2_output, index)),
	IOCTL_INFO_STD(VIDIOC_G_AUDOUT, vidioc_g_audout, v4l_print_audioout, 0),
	IOCTL_INFO_STD(VIDIOC_S_AUDOUT, vidioc_s_audout, v4l_print_audioout, INFO_FL_PRIO),
	IOCTL_INFO_STD(VIDIOC_G_MODULATOR, vidioc_g_modulator, v4l_print_modulator, INFO_FL_CLEAR(v4l2_modulator, index)),
	IOCTL_INFO_STD(VIDIOC_S_MODULATOR, vidioc_s_modulator, v4l_print_modulator, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_G_FREQUENCY, v4l_g_frequency, v4l_print_frequency, INFO_FL_CLEAR(v4l2_frequency, tuner)),
	IOCTL_INFO_FNC(VIDIOC_S_FREQUENCY, v4l_s_frequency, v4l_print_frequency, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_CROPCAP, INFO_FL_CLEAR(v4l2_cropcap, type)),
	IOCTL_INFO(VIDIOC_G_CROP, INFO_FL_CLEAR(v4l2_crop, type)),
	IOCTL_INFO(VIDIOC_S_CROP, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_SELECTION, 0),
	IOCTL_INFO(VIDIOC_S_SELECTION, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_JPEGCOMP, 0),
	IOCTL_INFO(VIDIOC_S_JPEGCOMP, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_QUERYSTD, v4l_querystd, v4l_print_std, 0),
	IOCTL_INFO_FNC(VIDIOC_TRY_FMT, v4l_try_fmt, v4l_print_format, 0),
	IOCTL_INFO_STD(VIDIOC_ENUMAUDIO, vidioc_enumaudio, v4l_print_audio, INFO_FL_CLEAR(v4l2_audio, index)),
	IOCTL_INFO_STD(VIDIOC_ENUMAUDOUT, vidioc_enumaudout, v4l_print_audioout, INFO_FL_CLEAR(v4l2_audioout, index)),
	IOCTL_INFO_FNC(VIDIOC_G_PRIORITY, v4l_g_priority, v4l_print_u32, 0),
	IOCTL_INFO_FNC(VIDIOC_S_PRIORITY, v4l_s_priority, v4l_print_u32, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_SLICED_VBI_CAP, INFO_FL_CLEAR(v4l2_sliced_vbi_cap, type)),
	IOCTL_INFO(VIDIOC_LOG_STATUS, 0),
	IOCTL_INFO(VIDIOC_G_EXT_CTRLS, INFO_FL_CTRL),
	IOCTL_INFO(VIDIOC_S_EXT_CTRLS, INFO_FL_PRIO | INFO_FL_CTRL),
	IOCTL_INFO(VIDIOC_TRY_EXT_CTRLS, 0),
	IOCTL_INFO(VIDIOC_ENUM_FRAMESIZES, INFO_FL_CLEAR(v4l2_frmsizeenum, pixel_format)),
	IOCTL_INFO(VIDIOC_ENUM_FRAMEINTERVALS, INFO_FL_CLEAR(v4l2_frmivalenum, height)),
	IOCTL_INFO(VIDIOC_G_ENC_INDEX, 0),
	IOCTL_INFO(VIDIOC_ENCODER_CMD, INFO_FL_PRIO | INFO_FL_CLEAR(v4l2_encoder_cmd, flags)),
	IOCTL_INFO(VIDIOC_TRY_ENCODER_CMD, INFO_FL_CLEAR(v4l2_encoder_cmd, flags)),
	IOCTL_INFO(VIDIOC_DECODER_CMD, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_TRY_DECODER_CMD, 0),
	IOCTL_INFO(VIDIOC_DBG_S_REGISTER, 0),
	IOCTL_INFO(VIDIOC_DBG_G_REGISTER, 0),
	IOCTL_INFO(VIDIOC_DBG_G_CHIP_IDENT, 0),
	IOCTL_INFO_FNC(VIDIOC_S_HW_FREQ_SEEK, v4l_s_hw_freq_seek, v4l_print_hw_freq_seek, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_ENUM_DV_PRESETS, 0),
	IOCTL_INFO(VIDIOC_S_DV_PRESET, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_DV_PRESET, 0),
	IOCTL_INFO(VIDIOC_QUERY_DV_PRESET, 0),
	IOCTL_INFO(VIDIOC_S_DV_TIMINGS, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_DV_TIMINGS, 0),
	IOCTL_INFO(VIDIOC_DQEVENT, 0),
	IOCTL_INFO(VIDIOC_SUBSCRIBE_EVENT, 0),
	IOCTL_INFO(VIDIOC_UNSUBSCRIBE_EVENT, 0),
	IOCTL_INFO_FNC(VIDIOC_CREATE_BUFS, v4l_create_bufs, v4l_print_create_buffers, INFO_FL_PRIO),
	IOCTL_INFO_FNC(VIDIOC_PREPARE_BUF, v4l_prepare_buf, v4l_print_buffer, 0),
	IOCTL_INFO(VIDIOC_ENUM_DV_TIMINGS, 0),
	IOCTL_INFO(VIDIOC_QUERY_DV_TIMINGS, 0),
	IOCTL_INFO(VIDIOC_DV_TIMINGS_CAP, 0),
};
#define V4L2_IOCTLS ARRAY_SIZE(v4l2_ioctls)

bool v4l2_is_known_ioctl(unsigned int cmd)
{
	if (_IOC_NR(cmd) >= V4L2_IOCTLS)
		return false;
	return v4l2_ioctls[_IOC_NR(cmd)].ioctl == cmd;
}

/* Common ioctl debug function. This function can be used by
   external ioctl messages as well as internal V4L ioctl */
void v4l_printk_ioctl(unsigned int cmd)
{
	const char *dir, *type;

	switch (_IOC_TYPE(cmd)) {
	case 'd':
		type = "v4l2_int";
		break;
	case 'V':
		if (_IOC_NR(cmd) >= V4L2_IOCTLS) {
			type = "v4l2";
			break;
		}
		pr_cont("%s", v4l2_ioctls[_IOC_NR(cmd)].name);
		return;
	default:
		type = "unknown";
		break;
	}

	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:              dir = "--"; break;
	case _IOC_READ:              dir = "r-"; break;
	case _IOC_WRITE:             dir = "-w"; break;
	case _IOC_READ | _IOC_WRITE: dir = "rw"; break;
	default:                     dir = "*ERR*"; break;
	}
	pr_cont("%s ioctl '%c', dir=%s, #%d (0x%08x)",
		type, _IOC_TYPE(cmd), dir, _IOC_NR(cmd), cmd);
}
EXPORT_SYMBOL(v4l_printk_ioctl);

static long __video_do_ioctl(struct file *file,
		unsigned int cmd, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	const struct v4l2_ioctl_ops *ops = vfd->ioctl_ops;
	bool write_only = false;
	struct v4l2_ioctl_info default_info;
	const struct v4l2_ioctl_info *info;
	void *fh = file->private_data;
	struct v4l2_fh *vfh = NULL;
	int use_fh_prio = 0;
	long ret = -ENOTTY;

	if (ops == NULL) {
		printk(KERN_WARNING "videodev: \"%s\" has no ioctl_ops.\n",
				vfd->name);
		return ret;
	}

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags)) {
		vfh = file->private_data;
		use_fh_prio = test_bit(V4L2_FL_USE_FH_PRIO, &vfd->flags);
	}

	if (v4l2_is_known_ioctl(cmd)) {
		info = &v4l2_ioctls[_IOC_NR(cmd)];

	        if (!test_bit(_IOC_NR(cmd), vfd->valid_ioctls) &&
		    !((info->flags & INFO_FL_CTRL) && vfh && vfh->ctrl_handler))
			goto done;

		if (use_fh_prio && (info->flags & INFO_FL_PRIO)) {
			ret = v4l2_prio_check(vfd->prio, vfh->prio);
			if (ret)
				goto done;
		}
	} else {
		default_info.ioctl = cmd;
		default_info.flags = 0;
		default_info.debug = NULL;
		info = &default_info;
	}

	write_only = _IOC_DIR(cmd) == _IOC_WRITE;
	if (info->debug && write_only && vfd->debug > V4L2_DEBUG_IOCTL) {
		v4l_print_ioctl(vfd->name, cmd);
		pr_cont(": ");
		info->debug(arg, write_only);
	}
	if (info->flags & INFO_FL_STD) {
		typedef int (*vidioc_op)(struct file *file, void *fh, void *p);
		const void *p = vfd->ioctl_ops;
		const vidioc_op *vidioc = p + info->offset;

		ret = (*vidioc)(file, fh, arg);
		goto done;
	} else if (info->flags & INFO_FL_FUNC) {
		ret = info->func(ops, file, fh, arg);
		goto done;
	}

	switch (cmd) {
	/* --- controls ---------------------------------------------- */
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *p = arg;

		if (vfh && vfh->ctrl_handler)
			ret = v4l2_queryctrl(vfh->ctrl_handler, p);
		else if (vfd->ctrl_handler)
			ret = v4l2_queryctrl(vfd->ctrl_handler, p);
		else if (ops->vidioc_queryctrl)
			ret = ops->vidioc_queryctrl(file, fh, p);
		else
			break;
		if (!ret)
			dbgarg(cmd, "id=0x%x, type=%d, name=%s, min/max=%d/%d, "
					"step=%d, default=%d, flags=0x%08x\n",
					p->id, p->type, p->name,
					p->minimum, p->maximum,
					p->step, p->default_value, p->flags);
		else
			dbgarg(cmd, "id=0x%x\n", p->id);
		break;
	}
	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *p = arg;

		if (vfh && vfh->ctrl_handler)
			ret = v4l2_g_ctrl(vfh->ctrl_handler, p);
		else if (vfd->ctrl_handler)
			ret = v4l2_g_ctrl(vfd->ctrl_handler, p);
		else if (ops->vidioc_g_ctrl)
			ret = ops->vidioc_g_ctrl(file, fh, p);
		else if (ops->vidioc_g_ext_ctrls) {
			struct v4l2_ext_controls ctrls;
			struct v4l2_ext_control ctrl;

			ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(p->id);
			ctrls.count = 1;
			ctrls.controls = &ctrl;
			ctrl.id = p->id;
			ctrl.value = p->value;
			if (check_ext_ctrls(&ctrls, 1)) {
				ret = ops->vidioc_g_ext_ctrls(file, fh, &ctrls);
				if (ret == 0)
					p->value = ctrl.value;
			}
		} else
			break;
		if (!ret)
			dbgarg(cmd, "id=0x%x, value=%d\n", p->id, p->value);
		else
			dbgarg(cmd, "id=0x%x\n", p->id);
		break;
	}
	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *p = arg;
		struct v4l2_ext_controls ctrls;
		struct v4l2_ext_control ctrl;

		if (!(vfh && vfh->ctrl_handler) && !vfd->ctrl_handler &&
			!ops->vidioc_s_ctrl && !ops->vidioc_s_ext_ctrls)
			break;

		dbgarg(cmd, "id=0x%x, value=%d\n", p->id, p->value);

		if (vfh && vfh->ctrl_handler) {
			ret = v4l2_s_ctrl(vfh, vfh->ctrl_handler, p);
			break;
		}
		if (vfd->ctrl_handler) {
			ret = v4l2_s_ctrl(NULL, vfd->ctrl_handler, p);
			break;
		}
		if (ops->vidioc_s_ctrl) {
			ret = ops->vidioc_s_ctrl(file, fh, p);
			break;
		}
		if (!ops->vidioc_s_ext_ctrls)
			break;

		ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(p->id);
		ctrls.count = 1;
		ctrls.controls = &ctrl;
		ctrl.id = p->id;
		ctrl.value = p->value;
		if (check_ext_ctrls(&ctrls, 1))
			ret = ops->vidioc_s_ext_ctrls(file, fh, &ctrls);
		else
			ret = -EINVAL;
		break;
	}
	case VIDIOC_G_EXT_CTRLS:
	{
		struct v4l2_ext_controls *p = arg;

		p->error_idx = p->count;
		if (vfh && vfh->ctrl_handler)
			ret = v4l2_g_ext_ctrls(vfh->ctrl_handler, p);
		else if (vfd->ctrl_handler)
			ret = v4l2_g_ext_ctrls(vfd->ctrl_handler, p);
		else if (ops->vidioc_g_ext_ctrls)
			ret = check_ext_ctrls(p, 0) ?
				ops->vidioc_g_ext_ctrls(file, fh, p) :
				-EINVAL;
		else
			break;
		v4l_print_ext_ctrls(cmd, vfd, p, !ret);
		break;
	}
	case VIDIOC_S_EXT_CTRLS:
	{
		struct v4l2_ext_controls *p = arg;

		p->error_idx = p->count;
		if (!(vfh && vfh->ctrl_handler) && !vfd->ctrl_handler &&
				!ops->vidioc_s_ext_ctrls)
			break;
		v4l_print_ext_ctrls(cmd, vfd, p, 1);
		if (vfh && vfh->ctrl_handler)
			ret = v4l2_s_ext_ctrls(vfh, vfh->ctrl_handler, p);
		else if (vfd->ctrl_handler)
			ret = v4l2_s_ext_ctrls(NULL, vfd->ctrl_handler, p);
		else if (check_ext_ctrls(p, 0))
			ret = ops->vidioc_s_ext_ctrls(file, fh, p);
		else
			ret = -EINVAL;
		break;
	}
	case VIDIOC_TRY_EXT_CTRLS:
	{
		struct v4l2_ext_controls *p = arg;

		p->error_idx = p->count;
		if (!(vfh && vfh->ctrl_handler) && !vfd->ctrl_handler &&
				!ops->vidioc_try_ext_ctrls)
			break;
		v4l_print_ext_ctrls(cmd, vfd, p, 1);
		if (vfh && vfh->ctrl_handler)
			ret = v4l2_try_ext_ctrls(vfh->ctrl_handler, p);
		else if (vfd->ctrl_handler)
			ret = v4l2_try_ext_ctrls(vfd->ctrl_handler, p);
		else if (check_ext_ctrls(p, 0))
			ret = ops->vidioc_try_ext_ctrls(file, fh, p);
		else
			ret = -EINVAL;
		break;
	}
	case VIDIOC_QUERYMENU:
	{
		struct v4l2_querymenu *p = arg;

		if (vfh && vfh->ctrl_handler)
			ret = v4l2_querymenu(vfh->ctrl_handler, p);
		else if (vfd->ctrl_handler)
			ret = v4l2_querymenu(vfd->ctrl_handler, p);
		else if (ops->vidioc_querymenu)
			ret = ops->vidioc_querymenu(file, fh, p);
		else
			break;
		if (!ret)
			dbgarg(cmd, "id=0x%x, index=%d, name=%s\n",
				p->id, p->index, p->name);
		else
			dbgarg(cmd, "id=0x%x, index=%d\n",
				p->id, p->index);
		break;
	}
	case VIDIOC_G_CROP:
	{
		struct v4l2_crop *p = arg;

		dbgarg(cmd, "type=%s\n", prt_names(p->type, v4l2_type_names));

		if (ops->vidioc_g_crop) {
			ret = ops->vidioc_g_crop(file, fh, p);
		} else {
			/* simulate capture crop using selection api */
			struct v4l2_selection s = {
				.type = p->type,
			};

			/* crop means compose for output devices */
			if (V4L2_TYPE_IS_OUTPUT(p->type))
				s.target = V4L2_SEL_TGT_COMPOSE_ACTIVE;
			else
				s.target = V4L2_SEL_TGT_CROP_ACTIVE;

			ret = ops->vidioc_g_selection(file, fh, &s);

			/* copying results to old structure on success */
			if (!ret)
				p->c = s.r;
		}

		if (!ret)
			dbgrect(vfd, "", &p->c);
		break;
	}
	case VIDIOC_S_CROP:
	{
		struct v4l2_crop *p = arg;

		dbgarg(cmd, "type=%s\n", prt_names(p->type, v4l2_type_names));
		dbgrect(vfd, "", &p->c);

		if (ops->vidioc_s_crop) {
			ret = ops->vidioc_s_crop(file, fh, p);
		} else {
			/* simulate capture crop using selection api */
			struct v4l2_selection s = {
				.type = p->type,
				.r = p->c,
			};

			/* crop means compose for output devices */
			if (V4L2_TYPE_IS_OUTPUT(p->type))
				s.target = V4L2_SEL_TGT_COMPOSE_ACTIVE;
			else
				s.target = V4L2_SEL_TGT_CROP_ACTIVE;

			ret = ops->vidioc_s_selection(file, fh, &s);
		}
		break;
	}
	case VIDIOC_G_SELECTION:
	{
		struct v4l2_selection *p = arg;

		dbgarg(cmd, "type=%s\n", prt_names(p->type, v4l2_type_names));

		ret = ops->vidioc_g_selection(file, fh, p);
		if (!ret)
			dbgrect(vfd, "", &p->r);
		break;
	}
	case VIDIOC_S_SELECTION:
	{
		struct v4l2_selection *p = arg;


		dbgarg(cmd, "type=%s\n", prt_names(p->type, v4l2_type_names));
		dbgrect(vfd, "", &p->r);

		ret = ops->vidioc_s_selection(file, fh, p);
		break;
	}
	case VIDIOC_CROPCAP:
	{
		struct v4l2_cropcap *p = arg;

		/*FIXME: Should also show v4l2_fract pixelaspect */
		dbgarg(cmd, "type=%s\n", prt_names(p->type, v4l2_type_names));
		if (ops->vidioc_cropcap) {
			ret = ops->vidioc_cropcap(file, fh, p);
		} else {
			struct v4l2_selection s = { .type = p->type };

			/* obtaining bounds */
			if (V4L2_TYPE_IS_OUTPUT(p->type))
				s.target = V4L2_SEL_TGT_COMPOSE_BOUNDS;
			else
				s.target = V4L2_SEL_TGT_CROP_BOUNDS;

			ret = ops->vidioc_g_selection(file, fh, &s);
			if (ret)
				break;
			p->bounds = s.r;

			/* obtaining defrect */
			if (V4L2_TYPE_IS_OUTPUT(p->type))
				s.target = V4L2_SEL_TGT_COMPOSE_DEFAULT;
			else
				s.target = V4L2_SEL_TGT_CROP_DEFAULT;

			ret = ops->vidioc_g_selection(file, fh, &s);
			if (ret)
				break;
			p->defrect = s.r;

			/* setting trivial pixelaspect */
			p->pixelaspect.numerator = 1;
			p->pixelaspect.denominator = 1;
		}

		if (!ret) {
			dbgrect(vfd, "bounds ", &p->bounds);
			dbgrect(vfd, "defrect ", &p->defrect);
		}
		break;
	}
	case VIDIOC_G_JPEGCOMP:
	{
		struct v4l2_jpegcompression *p = arg;

		ret = ops->vidioc_g_jpegcomp(file, fh, p);
		if (!ret)
			dbgarg(cmd, "quality=%d, APPn=%d, "
					"APP_len=%d, COM_len=%d, "
					"jpeg_markers=%d\n",
					p->quality, p->APPn, p->APP_len,
					p->COM_len, p->jpeg_markers);
		break;
	}
	case VIDIOC_S_JPEGCOMP:
	{
		struct v4l2_jpegcompression *p = arg;

		dbgarg(cmd, "quality=%d, APPn=%d, APP_len=%d, "
					"COM_len=%d, jpeg_markers=%d\n",
					p->quality, p->APPn, p->APP_len,
					p->COM_len, p->jpeg_markers);
		ret = ops->vidioc_s_jpegcomp(file, fh, p);
		break;
	}
	case VIDIOC_G_ENC_INDEX:
	{
		struct v4l2_enc_idx *p = arg;

		ret = ops->vidioc_g_enc_index(file, fh, p);
		if (!ret)
			dbgarg(cmd, "entries=%d, entries_cap=%d\n",
					p->entries, p->entries_cap);
		break;
	}
	case VIDIOC_ENCODER_CMD:
	{
		struct v4l2_encoder_cmd *p = arg;

		ret = ops->vidioc_encoder_cmd(file, fh, p);
		if (!ret)
			dbgarg(cmd, "cmd=%d, flags=%x\n", p->cmd, p->flags);
		break;
	}
	case VIDIOC_TRY_ENCODER_CMD:
	{
		struct v4l2_encoder_cmd *p = arg;

		ret = ops->vidioc_try_encoder_cmd(file, fh, p);
		if (!ret)
			dbgarg(cmd, "cmd=%d, flags=%x\n", p->cmd, p->flags);
		break;
	}
	case VIDIOC_DECODER_CMD:
	{
		struct v4l2_decoder_cmd *p = arg;

		ret = ops->vidioc_decoder_cmd(file, fh, p);
		if (!ret)
			dbgarg(cmd, "cmd=%d, flags=%x\n", p->cmd, p->flags);
		break;
	}
	case VIDIOC_TRY_DECODER_CMD:
	{
		struct v4l2_decoder_cmd *p = arg;

		ret = ops->vidioc_try_decoder_cmd(file, fh, p);
		if (!ret)
			dbgarg(cmd, "cmd=%d, flags=%x\n", p->cmd, p->flags);
		break;
	}
	case VIDIOC_G_SLICED_VBI_CAP:
	{
		struct v4l2_sliced_vbi_cap *p = arg;

		/* Clear up to type, everything after type is zerod already */
		memset(p, 0, offsetof(struct v4l2_sliced_vbi_cap, type));

		dbgarg(cmd, "type=%s\n", prt_names(p->type, v4l2_type_names));
		ret = ops->vidioc_g_sliced_vbi_cap(file, fh, p);
		if (!ret)
			dbgarg2("service_set=%d\n", p->service_set);
		break;
	}
	case VIDIOC_LOG_STATUS:
	{
		if (vfd->v4l2_dev)
			pr_info("%s: =================  START STATUS  =================\n",
				vfd->v4l2_dev->name);
		ret = ops->vidioc_log_status(file, fh);
		if (vfd->v4l2_dev)
			pr_info("%s: ==================  END STATUS  ==================\n",
				vfd->v4l2_dev->name);
		break;
	}
	case VIDIOC_DBG_G_REGISTER:
	{
#ifdef CONFIG_VIDEO_ADV_DEBUG
		struct v4l2_dbg_register *p = arg;

		if (!capable(CAP_SYS_ADMIN))
			ret = -EPERM;
		else
			ret = ops->vidioc_g_register(file, fh, p);
#endif
		break;
	}
	case VIDIOC_DBG_S_REGISTER:
	{
#ifdef CONFIG_VIDEO_ADV_DEBUG
		struct v4l2_dbg_register *p = arg;

		if (!capable(CAP_SYS_ADMIN))
			ret = -EPERM;
		else
			ret = ops->vidioc_s_register(file, fh, p);
#endif
		break;
	}
	case VIDIOC_DBG_G_CHIP_IDENT:
	{
		struct v4l2_dbg_chip_ident *p = arg;

		p->ident = V4L2_IDENT_NONE;
		p->revision = 0;
		ret = ops->vidioc_g_chip_ident(file, fh, p);
		if (!ret)
			dbgarg(cmd, "chip_ident=%u, revision=0x%x\n", p->ident, p->revision);
		break;
	}
	case VIDIOC_ENUM_FRAMESIZES:
	{
		struct v4l2_frmsizeenum *p = arg;

		ret = ops->vidioc_enum_framesizes(file, fh, p);
		dbgarg(cmd,
			"index=%d, pixelformat=%c%c%c%c, type=%d ",
			p->index,
			(p->pixel_format & 0xff),
			(p->pixel_format >>  8) & 0xff,
			(p->pixel_format >> 16) & 0xff,
			(p->pixel_format >> 24) & 0xff,
			p->type);
		switch (p->type) {
		case V4L2_FRMSIZE_TYPE_DISCRETE:
			dbgarg3("width = %d, height=%d\n",
				p->discrete.width, p->discrete.height);
			break;
		case V4L2_FRMSIZE_TYPE_STEPWISE:
			dbgarg3("min %dx%d, max %dx%d, step %dx%d\n",
				p->stepwise.min_width,  p->stepwise.min_height,
				p->stepwise.step_width, p->stepwise.step_height,
				p->stepwise.max_width,  p->stepwise.max_height);
			break;
		case V4L2_FRMSIZE_TYPE_CONTINUOUS:
			dbgarg3("continuous\n");
			break;
		default:
			dbgarg3("- Unknown type!\n");
		}

		break;
	}
	case VIDIOC_ENUM_FRAMEINTERVALS:
	{
		struct v4l2_frmivalenum *p = arg;

		ret = ops->vidioc_enum_frameintervals(file, fh, p);
		dbgarg(cmd,
			"index=%d, pixelformat=%d, width=%d, height=%d, type=%d ",
			p->index, p->pixel_format,
			p->width, p->height, p->type);
		switch (p->type) {
		case V4L2_FRMIVAL_TYPE_DISCRETE:
			dbgarg2("fps=%d/%d\n",
				p->discrete.numerator,
				p->discrete.denominator);
			break;
		case V4L2_FRMIVAL_TYPE_STEPWISE:
			dbgarg2("min=%d/%d, max=%d/%d, step=%d/%d\n",
				p->stepwise.min.numerator,
				p->stepwise.min.denominator,
				p->stepwise.max.numerator,
				p->stepwise.max.denominator,
				p->stepwise.step.numerator,
				p->stepwise.step.denominator);
			break;
		case V4L2_FRMIVAL_TYPE_CONTINUOUS:
			dbgarg2("continuous\n");
			break;
		default:
			dbgarg2("- Unknown type!\n");
		}
		break;
	}
	case VIDIOC_ENUM_DV_PRESETS:
	{
		struct v4l2_dv_enum_preset *p = arg;

		ret = ops->vidioc_enum_dv_presets(file, fh, p);
		if (!ret)
			dbgarg(cmd,
				"index=%d, preset=%d, name=%s, width=%d,"
				" height=%d ",
				p->index, p->preset, p->name, p->width,
				p->height);
		break;
	}
	case VIDIOC_S_DV_PRESET:
	{
		struct v4l2_dv_preset *p = arg;

		dbgarg(cmd, "preset=%d\n", p->preset);
		ret = ops->vidioc_s_dv_preset(file, fh, p);
		break;
	}
	case VIDIOC_G_DV_PRESET:
	{
		struct v4l2_dv_preset *p = arg;

		ret = ops->vidioc_g_dv_preset(file, fh, p);
		if (!ret)
			dbgarg(cmd, "preset=%d\n", p->preset);
		break;
	}
	case VIDIOC_QUERY_DV_PRESET:
	{
		struct v4l2_dv_preset *p = arg;

		ret = ops->vidioc_query_dv_preset(file, fh, p);
		if (!ret)
			dbgarg(cmd, "preset=%d\n", p->preset);
		break;
	}
	case VIDIOC_S_DV_TIMINGS:
	{
		struct v4l2_dv_timings *p = arg;

		dbgtimings(vfd, p);
		switch (p->type) {
		case V4L2_DV_BT_656_1120:
			ret = ops->vidioc_s_dv_timings(file, fh, p);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	}
	case VIDIOC_G_DV_TIMINGS:
	{
		struct v4l2_dv_timings *p = arg;

		ret = ops->vidioc_g_dv_timings(file, fh, p);
		if (!ret)
			dbgtimings(vfd, p);
		break;
	}
	case VIDIOC_ENUM_DV_TIMINGS:
	{
		struct v4l2_enum_dv_timings *p = arg;

		if (!ops->vidioc_enum_dv_timings)
			break;

		ret = ops->vidioc_enum_dv_timings(file, fh, p);
		if (!ret) {
			dbgarg(cmd, "index=%d: ", p->index);
			dbgtimings(vfd, &p->timings);
		}
		break;
	}
	case VIDIOC_QUERY_DV_TIMINGS:
	{
		struct v4l2_dv_timings *p = arg;

		if (!ops->vidioc_query_dv_timings)
			break;

		ret = ops->vidioc_query_dv_timings(file, fh, p);
		if (!ret)
			dbgtimings(vfd, p);
		break;
	}
	case VIDIOC_DV_TIMINGS_CAP:
	{
		struct v4l2_dv_timings_cap *p = arg;

		if (!ops->vidioc_dv_timings_cap)
			break;

		ret = ops->vidioc_dv_timings_cap(file, fh, p);
		if (ret)
			break;
		switch (p->type) {
		case V4L2_DV_BT_656_1120:
			dbgarg(cmd,
			       "type=%d, width=%u-%u, height=%u-%u, "
			       "pixelclock=%llu-%llu, standards=%x, capabilities=%x ",
			       p->type,
			       p->bt.min_width, p->bt.max_width,
			       p->bt.min_height, p->bt.max_height,
			       p->bt.min_pixelclock, p->bt.max_pixelclock,
			       p->bt.standards, p->bt.capabilities);
			break;
		default:
			dbgarg(cmd, "unknown type ");
			break;
		}
		break;
	}
	case VIDIOC_DQEVENT:
	{
		struct v4l2_event *ev = arg;

		ret = v4l2_event_dequeue(fh, ev, file->f_flags & O_NONBLOCK);
		if (ret < 0) {
			dbgarg(cmd, "no pending events?");
			break;
		}
		dbgarg(cmd,
		       "pending=%d, type=0x%8.8x, sequence=%d, "
		       "timestamp=%lu.%9.9lu ",
		       ev->pending, ev->type, ev->sequence,
		       ev->timestamp.tv_sec, ev->timestamp.tv_nsec);
		break;
	}
	case VIDIOC_SUBSCRIBE_EVENT:
	{
		struct v4l2_event_subscription *sub = arg;

		ret = ops->vidioc_subscribe_event(fh, sub);
		if (ret < 0) {
			dbgarg(cmd, "failed, ret=%ld", ret);
			break;
		}
		dbgarg(cmd, "type=0x%8.8x", sub->type);
		break;
	}
	case VIDIOC_UNSUBSCRIBE_EVENT:
	{
		struct v4l2_event_subscription *sub = arg;

		ret = ops->vidioc_unsubscribe_event(fh, sub);
		if (ret < 0) {
			dbgarg(cmd, "failed, ret=%ld", ret);
			break;
		}
		dbgarg(cmd, "type=0x%8.8x", sub->type);
		break;
	}
	default:
		if (!ops->vidioc_default)
			break;
		ret = ops->vidioc_default(file, fh, use_fh_prio ?
				v4l2_prio_check(vfd->prio, vfh->prio) >= 0 : 0,
				cmd, arg);
		break;
	} /* switch */

done:
	if (vfd->debug) {
		if (write_only && vfd->debug > V4L2_DEBUG_IOCTL) {
			if (ret < 0)
				printk(KERN_DEBUG "%s: error %ld\n",
					video_device_node_name(vfd), ret);
			return ret;
		}
		v4l_print_ioctl(vfd->name, cmd);
		if (ret < 0)
			pr_cont(": error %ld\n", ret);
		else if (vfd->debug == V4L2_DEBUG_IOCTL)
			pr_cont("\n");
		else if (!info->debug)
			return ret;
		else if (_IOC_DIR(cmd) == _IOC_NONE)
			info->debug(arg, write_only);
		else {
			pr_cont(": ");
			info->debug(arg, write_only);
		}
	}

	return ret;
}

static int check_array_args(unsigned int cmd, void *parg, size_t *array_size,
			    void * __user *user_ptr, void ***kernel_ptr)
{
	int ret = 0;

	switch (cmd) {
	case VIDIOC_QUERYBUF:
	case VIDIOC_QBUF:
	case VIDIOC_DQBUF: {
		struct v4l2_buffer *buf = parg;

		if (V4L2_TYPE_IS_MULTIPLANAR(buf->type) && buf->length > 0) {
			if (buf->length > VIDEO_MAX_PLANES) {
				ret = -EINVAL;
				break;
			}
			*user_ptr = (void __user *)buf->m.planes;
			*kernel_ptr = (void *)&buf->m.planes;
			*array_size = sizeof(struct v4l2_plane) * buf->length;
			ret = 1;
		}
		break;
	}

	case VIDIOC_S_EXT_CTRLS:
	case VIDIOC_G_EXT_CTRLS:
	case VIDIOC_TRY_EXT_CTRLS: {
		struct v4l2_ext_controls *ctrls = parg;

		if (ctrls->count != 0) {
			if (ctrls->count > V4L2_CID_MAX_CTRLS) {
				ret = -EINVAL;
				break;
			}
			*user_ptr = (void __user *)ctrls->controls;
			*kernel_ptr = (void *)&ctrls->controls;
			*array_size = sizeof(struct v4l2_ext_control)
				    * ctrls->count;
			ret = 1;
		}
		break;
	}
	}

	return ret;
}

long
video_usercopy(struct file *file, unsigned int cmd, unsigned long arg,
	       v4l2_kioctl func)
{
	char	sbuf[128];
	void    *mbuf = NULL;
	void	*parg = (void *)arg;
	long	err  = -EINVAL;
	bool	has_array_args;
	size_t  array_size = 0;
	void __user *user_ptr = NULL;
	void	**kernel_ptr = NULL;

	/*  Copy arguments into temp kernel buffer  */
	if (_IOC_DIR(cmd) != _IOC_NONE) {
		if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (_IOC_DIR(cmd) & _IOC_WRITE) {
			unsigned int n = _IOC_SIZE(cmd);

			/*
			 * In some cases, only a few fields are used as input,
			 * i.e. when the app sets "index" and then the driver
			 * fills in the rest of the structure for the thing
			 * with that index.  We only need to copy up the first
			 * non-input field.
			 */
			if (v4l2_is_known_ioctl(cmd)) {
				u32 flags = v4l2_ioctls[_IOC_NR(cmd)].flags;
				if (flags & INFO_FL_CLEAR_MASK)
					n = (flags & INFO_FL_CLEAR_MASK) >> 16;
			}

			if (copy_from_user(parg, (void __user *)arg, n))
				goto out;

			/* zero out anything we don't copy from userspace */
			if (n < _IOC_SIZE(cmd))
				memset((u8 *)parg + n, 0, _IOC_SIZE(cmd) - n);
		} else {
			/* read-only ioctl */
			memset(parg, 0, _IOC_SIZE(cmd));
		}
	}

	err = check_array_args(cmd, parg, &array_size, &user_ptr, &kernel_ptr);
	if (err < 0)
		goto out;
	has_array_args = err;

	if (has_array_args) {
		/*
		 * When adding new types of array args, make sure that the
		 * parent argument to ioctl (which contains the pointer to the
		 * array) fits into sbuf (so that mbuf will still remain
		 * unused up to here).
		 */
		mbuf = kmalloc(array_size, GFP_KERNEL);
		err = -ENOMEM;
		if (NULL == mbuf)
			goto out_array_args;
		err = -EFAULT;
		if (copy_from_user(mbuf, user_ptr, array_size))
			goto out_array_args;
		*kernel_ptr = mbuf;
	}

	/* Handles IOCTL */
	err = func(file, cmd, parg);
	if (err == -ENOIOCTLCMD)
		err = -ENOTTY;

	if (has_array_args) {
		*kernel_ptr = user_ptr;
		if (copy_to_user(user_ptr, mbuf, array_size))
			err = -EFAULT;
		goto out_array_args;
	}
	/* VIDIOC_QUERY_DV_TIMINGS can return an error, but still have valid
	   results that must be returned. */
	if (err < 0 && cmd != VIDIOC_QUERY_DV_TIMINGS)
		goto out;

out_array_args:
	/*  Copy results into user buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_READ:
	case (_IOC_WRITE | _IOC_READ):
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			err = -EFAULT;
		break;
	}

out:
	kfree(mbuf);
	return err;
}
EXPORT_SYMBOL(video_usercopy);

long video_ioctl2(struct file *file,
	       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(file, cmd, arg, __video_do_ioctl);
}
EXPORT_SYMBOL(video_ioctl2);
