// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Video capture interface for Linux version 2
 *
 * A generic framework to process V4L2 ioctl commands.
 *
 * Authors:	Alan Cox, <alan@lxorguk.ukuu.org.uk> (version 1)
 *              Mauro Carvalho Chehab <mchehab@kernel.org> (version 2)
 */

#include <linux/mm.h>
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
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-mem2mem.h>

#include <trace/events/v4l2.h>

/* Zero out the end of the struct pointed to by p.  Everything after, but
 * not including, the specified field is cleared. */
#define CLEAR_AFTER_FIELD(p, field) \
	memset((u8 *)(p) + offsetof(typeof(*(p)), field) + sizeof((p)->field), \
	0, sizeof(*(p)) - offsetof(typeof(*(p)), field) - sizeof((p)->field))

#define is_valid_ioctl(vfd, cmd) test_bit(_IOC_NR(cmd), (vfd)->valid_ioctls)

struct std_descr {
	v4l2_std_id std;
	const char *descr;
};

static const struct std_descr standards[] = {
	{ V4L2_STD_NTSC,	"NTSC"      },
	{ V4L2_STD_NTSC_M,	"NTSC-M"    },
	{ V4L2_STD_NTSC_M_JP,	"NTSC-M-JP" },
	{ V4L2_STD_NTSC_M_KR,	"NTSC-M-KR" },
	{ V4L2_STD_NTSC_443,	"NTSC-443"  },
	{ V4L2_STD_PAL,		"PAL"       },
	{ V4L2_STD_PAL_BG,	"PAL-BG"    },
	{ V4L2_STD_PAL_B,	"PAL-B"     },
	{ V4L2_STD_PAL_B1,	"PAL-B1"    },
	{ V4L2_STD_PAL_G,	"PAL-G"     },
	{ V4L2_STD_PAL_H,	"PAL-H"     },
	{ V4L2_STD_PAL_I,	"PAL-I"     },
	{ V4L2_STD_PAL_DK,	"PAL-DK"    },
	{ V4L2_STD_PAL_D,	"PAL-D"     },
	{ V4L2_STD_PAL_D1,	"PAL-D1"    },
	{ V4L2_STD_PAL_K,	"PAL-K"     },
	{ V4L2_STD_PAL_M,	"PAL-M"     },
	{ V4L2_STD_PAL_N,	"PAL-N"     },
	{ V4L2_STD_PAL_Nc,	"PAL-Nc"    },
	{ V4L2_STD_PAL_60,	"PAL-60"    },
	{ V4L2_STD_SECAM,	"SECAM"     },
	{ V4L2_STD_SECAM_B,	"SECAM-B"   },
	{ V4L2_STD_SECAM_G,	"SECAM-G"   },
	{ V4L2_STD_SECAM_H,	"SECAM-H"   },
	{ V4L2_STD_SECAM_DK,	"SECAM-DK"  },
	{ V4L2_STD_SECAM_D,	"SECAM-D"   },
	{ V4L2_STD_SECAM_K,	"SECAM-K"   },
	{ V4L2_STD_SECAM_K1,	"SECAM-K1"  },
	{ V4L2_STD_SECAM_L,	"SECAM-L"   },
	{ V4L2_STD_SECAM_LC,	"SECAM-Lc"  },
	{ 0,			"Unknown"   }
};

/* video4linux standard ID conversion to standard name
 */
const char *v4l2_norm_to_name(v4l2_std_id id)
{
	u32 myid = id;
	int i;

	/* HACK: ppc32 architecture doesn't have __ucmpdi2 function to handle
	   64 bit comparisons. So, on that architecture, with some gcc
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
	strscpy(vs->name, name, sizeof(vs->name));
	return 0;
}
EXPORT_SYMBOL(v4l2_video_std_construct);

/* Fill in the fields of a v4l2_standard structure according to the
 * 'id' and 'vs->index' parameters. Returns negative on error. */
int v4l_video_std_enumstd(struct v4l2_standard *vs, v4l2_std_id id)
{
	v4l2_std_id curr_id = 0;
	unsigned int index = vs->index, i, j = 0;
	const char *descr = "";

	/* Return -ENODATA if the id for the current input
	   or output is 0, meaning that it doesn't support this API. */
	if (id == 0)
		return -ENODATA;

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

	v4l2_video_std_construct(vs, curr_id, descr);
	return 0;
}

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
	[0]				   = "0",
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
	[V4L2_BUF_TYPE_SDR_CAPTURE]        = "sdr-cap",
	[V4L2_BUF_TYPE_SDR_OUTPUT]         = "sdr-out",
	[V4L2_BUF_TYPE_META_CAPTURE]       = "meta-cap",
	[V4L2_BUF_TYPE_META_OUTPUT]	   = "meta-out",
};
EXPORT_SYMBOL(v4l2_type_names);

static const char *v4l2_memory_names[] = {
	[V4L2_MEMORY_MMAP]    = "mmap",
	[V4L2_MEMORY_USERPTR] = "userptr",
	[V4L2_MEMORY_OVERLAY] = "overlay",
	[V4L2_MEMORY_DMABUF] = "dmabuf",
};

#define prt_names(a, arr) (((unsigned)(a)) < ARRAY_SIZE(arr) ? arr[a] : "unknown")

/* ------------------------------------------------------------------ */
/* debug help functions                                               */

static void v4l_print_querycap(const void *arg, bool write_only)
{
	const struct v4l2_capability *p = arg;

	pr_cont("driver=%.*s, card=%.*s, bus=%.*s, version=0x%08x, capabilities=0x%08x, device_caps=0x%08x\n",
		(int)sizeof(p->driver), p->driver,
		(int)sizeof(p->card), p->card,
		(int)sizeof(p->bus_info), p->bus_info,
		p->version, p->capabilities, p->device_caps);
}

static void v4l_print_enuminput(const void *arg, bool write_only)
{
	const struct v4l2_input *p = arg;

	pr_cont("index=%u, name=%.*s, type=%u, audioset=0x%x, tuner=%u, std=0x%08Lx, status=0x%x, capabilities=0x%x\n",
		p->index, (int)sizeof(p->name), p->name, p->type, p->audioset,
		p->tuner, (unsigned long long)p->std, p->status,
		p->capabilities);
}

static void v4l_print_enumoutput(const void *arg, bool write_only)
{
	const struct v4l2_output *p = arg;

	pr_cont("index=%u, name=%.*s, type=%u, audioset=0x%x, modulator=%u, std=0x%08Lx, capabilities=0x%x\n",
		p->index, (int)sizeof(p->name), p->name, p->type, p->audioset,
		p->modulator, (unsigned long long)p->std, p->capabilities);
}

static void v4l_print_audio(const void *arg, bool write_only)
{
	const struct v4l2_audio *p = arg;

	if (write_only)
		pr_cont("index=%u, mode=0x%x\n", p->index, p->mode);
	else
		pr_cont("index=%u, name=%.*s, capability=0x%x, mode=0x%x\n",
			p->index, (int)sizeof(p->name), p->name,
			p->capability, p->mode);
}

static void v4l_print_audioout(const void *arg, bool write_only)
{
	const struct v4l2_audioout *p = arg;

	if (write_only)
		pr_cont("index=%u\n", p->index);
	else
		pr_cont("index=%u, name=%.*s, capability=0x%x, mode=0x%x\n",
			p->index, (int)sizeof(p->name), p->name,
			p->capability, p->mode);
}

static void v4l_print_fmtdesc(const void *arg, bool write_only)
{
	const struct v4l2_fmtdesc *p = arg;

	pr_cont("index=%u, type=%s, flags=0x%x, pixelformat=%c%c%c%c, description='%.*s'\n",
		p->index, prt_names(p->type, v4l2_type_names),
		p->flags, (p->pixelformat & 0xff),
		(p->pixelformat >>  8) & 0xff,
		(p->pixelformat >> 16) & 0xff,
		(p->pixelformat >> 24) & 0xff,
		(int)sizeof(p->description), p->description);
}

static void v4l_print_format(const void *arg, bool write_only)
{
	const struct v4l2_format *p = arg;
	const struct v4l2_pix_format *pix;
	const struct v4l2_pix_format_mplane *mp;
	const struct v4l2_vbi_format *vbi;
	const struct v4l2_sliced_vbi_format *sliced;
	const struct v4l2_window *win;
	const struct v4l2_sdr_format *sdr;
	const struct v4l2_meta_format *meta;
	u32 planes;
	unsigned i;

	pr_cont("type=%s", prt_names(p->type, v4l2_type_names));
	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		pix = &p->fmt.pix;
		pr_cont(", width=%u, height=%u, pixelformat=%c%c%c%c, field=%s, bytesperline=%u, sizeimage=%u, colorspace=%d, flags=0x%x, ycbcr_enc=%u, quantization=%u, xfer_func=%u\n",
			pix->width, pix->height,
			(pix->pixelformat & 0xff),
			(pix->pixelformat >>  8) & 0xff,
			(pix->pixelformat >> 16) & 0xff,
			(pix->pixelformat >> 24) & 0xff,
			prt_names(pix->field, v4l2_field_names),
			pix->bytesperline, pix->sizeimage,
			pix->colorspace, pix->flags, pix->ycbcr_enc,
			pix->quantization, pix->xfer_func);
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		mp = &p->fmt.pix_mp;
		pr_cont(", width=%u, height=%u, format=%c%c%c%c, field=%s, colorspace=%d, num_planes=%u, flags=0x%x, ycbcr_enc=%u, quantization=%u, xfer_func=%u\n",
			mp->width, mp->height,
			(mp->pixelformat & 0xff),
			(mp->pixelformat >>  8) & 0xff,
			(mp->pixelformat >> 16) & 0xff,
			(mp->pixelformat >> 24) & 0xff,
			prt_names(mp->field, v4l2_field_names),
			mp->colorspace, mp->num_planes, mp->flags,
			mp->ycbcr_enc, mp->quantization, mp->xfer_func);
		planes = min_t(u32, mp->num_planes, VIDEO_MAX_PLANES);
		for (i = 0; i < planes; i++)
			printk(KERN_DEBUG "plane %u: bytesperline=%u sizeimage=%u\n", i,
					mp->plane_fmt[i].bytesperline,
					mp->plane_fmt[i].sizeimage);
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		win = &p->fmt.win;
		/* Note: we can't print the clip list here since the clips
		 * pointer is a userspace pointer, not a kernelspace
		 * pointer. */
		pr_cont(", wxh=%dx%d, x,y=%d,%d, field=%s, chromakey=0x%08x, clipcount=%u, clips=%p, bitmap=%p, global_alpha=0x%02x\n",
			win->w.width, win->w.height, win->w.left, win->w.top,
			prt_names(win->field, v4l2_field_names),
			win->chromakey, win->clipcount, win->clips,
			win->bitmap, win->global_alpha);
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		vbi = &p->fmt.vbi;
		pr_cont(", sampling_rate=%u, offset=%u, samples_per_line=%u, sample_format=%c%c%c%c, start=%u,%u, count=%u,%u\n",
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
	case V4L2_BUF_TYPE_SDR_CAPTURE:
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		sdr = &p->fmt.sdr;
		pr_cont(", pixelformat=%c%c%c%c\n",
			(sdr->pixelformat >>  0) & 0xff,
			(sdr->pixelformat >>  8) & 0xff,
			(sdr->pixelformat >> 16) & 0xff,
			(sdr->pixelformat >> 24) & 0xff);
		break;
	case V4L2_BUF_TYPE_META_CAPTURE:
	case V4L2_BUF_TYPE_META_OUTPUT:
		meta = &p->fmt.meta;
		pr_cont(", dataformat=%c%c%c%c, buffersize=%u\n",
			(meta->dataformat >>  0) & 0xff,
			(meta->dataformat >>  8) & 0xff,
			(meta->dataformat >> 16) & 0xff,
			(meta->dataformat >> 24) & 0xff,
			meta->buffersize);
		break;
	}
}

static void v4l_print_framebuffer(const void *arg, bool write_only)
{
	const struct v4l2_framebuffer *p = arg;

	pr_cont("capability=0x%x, flags=0x%x, base=0x%p, width=%u, height=%u, pixelformat=%c%c%c%c, bytesperline=%u, sizeimage=%u, colorspace=%d\n",
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
		pr_cont("index=%u, txsubchans=0x%x\n", p->index, p->txsubchans);
	else
		pr_cont("index=%u, name=%.*s, capability=0x%x, rangelow=%u, rangehigh=%u, txsubchans=0x%x\n",
			p->index, (int)sizeof(p->name), p->name, p->capability,
			p->rangelow, p->rangehigh, p->txsubchans);
}

static void v4l_print_tuner(const void *arg, bool write_only)
{
	const struct v4l2_tuner *p = arg;

	if (write_only)
		pr_cont("index=%u, audmode=%u\n", p->index, p->audmode);
	else
		pr_cont("index=%u, name=%.*s, type=%u, capability=0x%x, rangelow=%u, rangehigh=%u, signal=%u, afc=%d, rxsubchans=0x%x, audmode=%u\n",
			p->index, (int)sizeof(p->name), p->name, p->type,
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

	pr_cont("index=%u, id=0x%Lx, name=%.*s, fps=%u/%u, framelines=%u\n",
		p->index,
		(unsigned long long)p->id, (int)sizeof(p->name), p->name,
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

	pr_cont("tuner=%u, type=%u, seek_upward=%u, wrap_around=%u, spacing=%u, rangelow=%u, rangehigh=%u\n",
		p->tuner, p->type, p->seek_upward, p->wrap_around, p->spacing,
		p->rangelow, p->rangehigh);
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

	pr_cont("%02ld:%02d:%02d.%08ld index=%d, type=%s, request_fd=%d, flags=0x%08x, field=%s, sequence=%d, memory=%s",
			p->timestamp.tv_sec / 3600,
			(int)(p->timestamp.tv_sec / 60) % 60,
			(int)(p->timestamp.tv_sec % 60),
			(long)p->timestamp.tv_usec,
			p->index,
			prt_names(p->type, v4l2_type_names), p->request_fd,
			p->flags, prt_names(p->field, v4l2_field_names),
			p->sequence, prt_names(p->memory, v4l2_memory_names));

	if (V4L2_TYPE_IS_MULTIPLANAR(p->type) && p->m.planes) {
		pr_cont("\n");
		for (i = 0; i < p->length; ++i) {
			plane = &p->m.planes[i];
			printk(KERN_DEBUG
				"plane %d: bytesused=%d, data_offset=0x%08x, offset/userptr=0x%lx, length=%d\n",
				i, plane->bytesused, plane->data_offset,
				plane->m.userptr, plane->length);
		}
	} else {
		pr_cont(", bytesused=%d, offset/userptr=0x%lx, length=%d\n",
			p->bytesused, p->m.userptr, p->length);
	}

	printk(KERN_DEBUG "timecode=%02d:%02d:%02d type=%d, flags=0x%08x, frames=%d, userbits=0x%08x\n",
			tc->hours, tc->minutes, tc->seconds,
			tc->type, tc->flags, tc->frames, *(__u32 *)tc->userbits);
}

static void v4l_print_exportbuffer(const void *arg, bool write_only)
{
	const struct v4l2_exportbuffer *p = arg;

	pr_cont("fd=%d, type=%s, index=%u, plane=%u, flags=0x%08x\n",
		p->fd, prt_names(p->type, v4l2_type_names),
		p->index, p->plane, p->flags);
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

		pr_cont(", capability=0x%x, capturemode=0x%x, timeperframe=%d/%d, extendedmode=%d, readbuffers=%d\n",
			c->capability, c->capturemode,
			c->timeperframe.numerator, c->timeperframe.denominator,
			c->extendedmode, c->readbuffers);
	} else if (p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT ||
		   p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		const struct v4l2_outputparm *c = &p->parm.output;

		pr_cont(", capability=0x%x, outputmode=0x%x, timeperframe=%d/%d, extendedmode=%d, writebuffers=%d\n",
			c->capability, c->outputmode,
			c->timeperframe.numerator, c->timeperframe.denominator,
			c->extendedmode, c->writebuffers);
	} else {
		pr_cont("\n");
	}
}

static void v4l_print_queryctrl(const void *arg, bool write_only)
{
	const struct v4l2_queryctrl *p = arg;

	pr_cont("id=0x%x, type=%d, name=%.*s, min/max=%d/%d, step=%d, default=%d, flags=0x%08x\n",
			p->id, p->type, (int)sizeof(p->name), p->name,
			p->minimum, p->maximum,
			p->step, p->default_value, p->flags);
}

static void v4l_print_query_ext_ctrl(const void *arg, bool write_only)
{
	const struct v4l2_query_ext_ctrl *p = arg;

	pr_cont("id=0x%x, type=%d, name=%.*s, min/max=%lld/%lld, step=%lld, default=%lld, flags=0x%08x, elem_size=%u, elems=%u, nr_of_dims=%u, dims=%u,%u,%u,%u\n",
			p->id, p->type, (int)sizeof(p->name), p->name,
			p->minimum, p->maximum,
			p->step, p->default_value, p->flags,
			p->elem_size, p->elems, p->nr_of_dims,
			p->dims[0], p->dims[1], p->dims[2], p->dims[3]);
}

static void v4l_print_querymenu(const void *arg, bool write_only)
{
	const struct v4l2_querymenu *p = arg;

	pr_cont("id=0x%x, index=%d\n", p->id, p->index);
}

static void v4l_print_control(const void *arg, bool write_only)
{
	const struct v4l2_control *p = arg;

	pr_cont("id=0x%x, value=%d\n", p->id, p->value);
}

static void v4l_print_ext_controls(const void *arg, bool write_only)
{
	const struct v4l2_ext_controls *p = arg;
	int i;

	pr_cont("which=0x%x, count=%d, error_idx=%d, request_fd=%d",
			p->which, p->count, p->error_idx, p->request_fd);
	for (i = 0; i < p->count; i++) {
		if (!p->controls[i].size)
			pr_cont(", id/val=0x%x/0x%x",
				p->controls[i].id, p->controls[i].value);
		else
			pr_cont(", id/size=0x%x/%u",
				p->controls[i].id, p->controls[i].size);
	}
	pr_cont("\n");
}

static void v4l_print_cropcap(const void *arg, bool write_only)
{
	const struct v4l2_cropcap *p = arg;

	pr_cont("type=%s, bounds wxh=%dx%d, x,y=%d,%d, defrect wxh=%dx%d, x,y=%d,%d, pixelaspect %d/%d\n",
		prt_names(p->type, v4l2_type_names),
		p->bounds.width, p->bounds.height,
		p->bounds.left, p->bounds.top,
		p->defrect.width, p->defrect.height,
		p->defrect.left, p->defrect.top,
		p->pixelaspect.numerator, p->pixelaspect.denominator);
}

static void v4l_print_crop(const void *arg, bool write_only)
{
	const struct v4l2_crop *p = arg;

	pr_cont("type=%s, wxh=%dx%d, x,y=%d,%d\n",
		prt_names(p->type, v4l2_type_names),
		p->c.width, p->c.height,
		p->c.left, p->c.top);
}

static void v4l_print_selection(const void *arg, bool write_only)
{
	const struct v4l2_selection *p = arg;

	pr_cont("type=%s, target=%d, flags=0x%x, wxh=%dx%d, x,y=%d,%d\n",
		prt_names(p->type, v4l2_type_names),
		p->target, p->flags,
		p->r.width, p->r.height, p->r.left, p->r.top);
}

static void v4l_print_jpegcompression(const void *arg, bool write_only)
{
	const struct v4l2_jpegcompression *p = arg;

	pr_cont("quality=%d, APPn=%d, APP_len=%d, COM_len=%d, jpeg_markers=0x%x\n",
		p->quality, p->APPn, p->APP_len,
		p->COM_len, p->jpeg_markers);
}

static void v4l_print_enc_idx(const void *arg, bool write_only)
{
	const struct v4l2_enc_idx *p = arg;

	pr_cont("entries=%d, entries_cap=%d\n",
			p->entries, p->entries_cap);
}

static void v4l_print_encoder_cmd(const void *arg, bool write_only)
{
	const struct v4l2_encoder_cmd *p = arg;

	pr_cont("cmd=%d, flags=0x%x\n",
			p->cmd, p->flags);
}

static void v4l_print_decoder_cmd(const void *arg, bool write_only)
{
	const struct v4l2_decoder_cmd *p = arg;

	pr_cont("cmd=%d, flags=0x%x\n", p->cmd, p->flags);

	if (p->cmd == V4L2_DEC_CMD_START)
		pr_info("speed=%d, format=%u\n",
				p->start.speed, p->start.format);
	else if (p->cmd == V4L2_DEC_CMD_STOP)
		pr_info("pts=%llu\n", p->stop.pts);
}

static void v4l_print_dbg_chip_info(const void *arg, bool write_only)
{
	const struct v4l2_dbg_chip_info *p = arg;

	pr_cont("type=%u, ", p->match.type);
	if (p->match.type == V4L2_CHIP_MATCH_I2C_DRIVER)
		pr_cont("name=%.*s, ",
				(int)sizeof(p->match.name), p->match.name);
	else
		pr_cont("addr=%u, ", p->match.addr);
	pr_cont("name=%.*s\n", (int)sizeof(p->name), p->name);
}

static void v4l_print_dbg_register(const void *arg, bool write_only)
{
	const struct v4l2_dbg_register *p = arg;

	pr_cont("type=%u, ", p->match.type);
	if (p->match.type == V4L2_CHIP_MATCH_I2C_DRIVER)
		pr_cont("name=%.*s, ",
				(int)sizeof(p->match.name), p->match.name);
	else
		pr_cont("addr=%u, ", p->match.addr);
	pr_cont("reg=0x%llx, val=0x%llx\n",
			p->reg, p->val);
}

static void v4l_print_dv_timings(const void *arg, bool write_only)
{
	const struct v4l2_dv_timings *p = arg;

	switch (p->type) {
	case V4L2_DV_BT_656_1120:
		pr_cont("type=bt-656/1120, interlaced=%u, pixelclock=%llu, width=%u, height=%u, polarities=0x%x, hfrontporch=%u, hsync=%u, hbackporch=%u, vfrontporch=%u, vsync=%u, vbackporch=%u, il_vfrontporch=%u, il_vsync=%u, il_vbackporch=%u, standards=0x%x, flags=0x%x\n",
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
		pr_cont("type=%d\n", p->type);
		break;
	}
}

static void v4l_print_enum_dv_timings(const void *arg, bool write_only)
{
	const struct v4l2_enum_dv_timings *p = arg;

	pr_cont("index=%u, ", p->index);
	v4l_print_dv_timings(&p->timings, write_only);
}

static void v4l_print_dv_timings_cap(const void *arg, bool write_only)
{
	const struct v4l2_dv_timings_cap *p = arg;

	switch (p->type) {
	case V4L2_DV_BT_656_1120:
		pr_cont("type=bt-656/1120, width=%u-%u, height=%u-%u, pixelclock=%llu-%llu, standards=0x%x, capabilities=0x%x\n",
			p->bt.min_width, p->bt.max_width,
			p->bt.min_height, p->bt.max_height,
			p->bt.min_pixelclock, p->bt.max_pixelclock,
			p->bt.standards, p->bt.capabilities);
		break;
	default:
		pr_cont("type=%u\n", p->type);
		break;
	}
}

static void v4l_print_frmsizeenum(const void *arg, bool write_only)
{
	const struct v4l2_frmsizeenum *p = arg;

	pr_cont("index=%u, pixelformat=%c%c%c%c, type=%u",
			p->index,
			(p->pixel_format & 0xff),
			(p->pixel_format >>  8) & 0xff,
			(p->pixel_format >> 16) & 0xff,
			(p->pixel_format >> 24) & 0xff,
			p->type);
	switch (p->type) {
	case V4L2_FRMSIZE_TYPE_DISCRETE:
		pr_cont(", wxh=%ux%u\n",
			p->discrete.width, p->discrete.height);
		break;
	case V4L2_FRMSIZE_TYPE_STEPWISE:
		pr_cont(", min=%ux%u, max=%ux%u, step=%ux%u\n",
				p->stepwise.min_width,
				p->stepwise.min_height,
				p->stepwise.max_width,
				p->stepwise.max_height,
				p->stepwise.step_width,
				p->stepwise.step_height);
		break;
	case V4L2_FRMSIZE_TYPE_CONTINUOUS:
		/* fall through */
	default:
		pr_cont("\n");
		break;
	}
}

static void v4l_print_frmivalenum(const void *arg, bool write_only)
{
	const struct v4l2_frmivalenum *p = arg;

	pr_cont("index=%u, pixelformat=%c%c%c%c, wxh=%ux%u, type=%u",
			p->index,
			(p->pixel_format & 0xff),
			(p->pixel_format >>  8) & 0xff,
			(p->pixel_format >> 16) & 0xff,
			(p->pixel_format >> 24) & 0xff,
			p->width, p->height, p->type);
	switch (p->type) {
	case V4L2_FRMIVAL_TYPE_DISCRETE:
		pr_cont(", fps=%d/%d\n",
				p->discrete.numerator,
				p->discrete.denominator);
		break;
	case V4L2_FRMIVAL_TYPE_STEPWISE:
		pr_cont(", min=%d/%d, max=%d/%d, step=%d/%d\n",
				p->stepwise.min.numerator,
				p->stepwise.min.denominator,
				p->stepwise.max.numerator,
				p->stepwise.max.denominator,
				p->stepwise.step.numerator,
				p->stepwise.step.denominator);
		break;
	case V4L2_FRMIVAL_TYPE_CONTINUOUS:
		/* fall through */
	default:
		pr_cont("\n");
		break;
	}
}

static void v4l_print_event(const void *arg, bool write_only)
{
	const struct v4l2_event *p = arg;
	const struct v4l2_event_ctrl *c;

	pr_cont("type=0x%x, pending=%u, sequence=%u, id=%u, timestamp=%lu.%9.9lu\n",
			p->type, p->pending, p->sequence, p->id,
			p->timestamp.tv_sec, p->timestamp.tv_nsec);
	switch (p->type) {
	case V4L2_EVENT_VSYNC:
		printk(KERN_DEBUG "field=%s\n",
			prt_names(p->u.vsync.field, v4l2_field_names));
		break;
	case V4L2_EVENT_CTRL:
		c = &p->u.ctrl;
		printk(KERN_DEBUG "changes=0x%x, type=%u, ",
			c->changes, c->type);
		if (c->type == V4L2_CTRL_TYPE_INTEGER64)
			pr_cont("value64=%lld, ", c->value64);
		else
			pr_cont("value=%d, ", c->value);
		pr_cont("flags=0x%x, minimum=%d, maximum=%d, step=%d, default_value=%d\n",
			c->flags, c->minimum, c->maximum,
			c->step, c->default_value);
		break;
	case V4L2_EVENT_FRAME_SYNC:
		pr_cont("frame_sequence=%u\n",
			p->u.frame_sync.frame_sequence);
		break;
	}
}

static void v4l_print_event_subscription(const void *arg, bool write_only)
{
	const struct v4l2_event_subscription *p = arg;

	pr_cont("type=0x%x, id=0x%x, flags=0x%x\n",
			p->type, p->id, p->flags);
}

static void v4l_print_sliced_vbi_cap(const void *arg, bool write_only)
{
	const struct v4l2_sliced_vbi_cap *p = arg;
	int i;

	pr_cont("type=%s, service_set=0x%08x\n",
			prt_names(p->type, v4l2_type_names), p->service_set);
	for (i = 0; i < 24; i++)
		printk(KERN_DEBUG "line[%02u]=0x%04x, 0x%04x\n", i,
				p->service_lines[0][i],
				p->service_lines[1][i]);
}

static void v4l_print_freq_band(const void *arg, bool write_only)
{
	const struct v4l2_frequency_band *p = arg;

	pr_cont("tuner=%u, type=%u, index=%u, capability=0x%x, rangelow=%u, rangehigh=%u, modulation=0x%x\n",
			p->tuner, p->type, p->index,
			p->capability, p->rangelow,
			p->rangehigh, p->modulation);
}

static void v4l_print_edid(const void *arg, bool write_only)
{
	const struct v4l2_edid *p = arg;

	pr_cont("pad=%u, start_block=%u, blocks=%u\n",
		p->pad, p->start_block, p->blocks);
}

static void v4l_print_u32(const void *arg, bool write_only)
{
	pr_cont("value=%u\n", *(const u32 *)arg);
}

static void v4l_print_newline(const void *arg, bool write_only)
{
	pr_cont("\n");
}

static void v4l_print_default(const void *arg, bool write_only)
{
	pr_cont("driver-specific ioctl\n");
}

static int check_ext_ctrls(struct v4l2_ext_controls *c, int allow_priv)
{
	__u32 i;

	/* zero the reserved fields */
	c->reserved[0] = 0;
	for (i = 0; i < c->count; i++)
		c->controls[i].reserved2[0] = 0;

	/* V4L2_CID_PRIVATE_BASE cannot be used as control class
	   when using extended controls.
	   Only when passed in through VIDIOC_G_CTRL and VIDIOC_S_CTRL
	   is it allowed for backwards compatibility.
	 */
	if (!allow_priv && c->which == V4L2_CID_PRIVATE_BASE)
		return 0;
	if (!c->which)
		return 1;
	/* Check that all controls are from the same control class. */
	for (i = 0; i < c->count; i++) {
		if (V4L2_CTRL_ID2WHICH(c->controls[i].id) != c->which) {
			c->error_idx = i;
			return 0;
		}
	}
	return 1;
}

static int check_fmt(struct file *file, enum v4l2_buf_type type)
{
	struct video_device *vfd = video_devdata(file);
	const struct v4l2_ioctl_ops *ops = vfd->ioctl_ops;
	bool is_vid = vfd->vfl_type == VFL_TYPE_GRABBER;
	bool is_vbi = vfd->vfl_type == VFL_TYPE_VBI;
	bool is_sdr = vfd->vfl_type == VFL_TYPE_SDR;
	bool is_tch = vfd->vfl_type == VFL_TYPE_TOUCH;
	bool is_rx = vfd->vfl_dir != VFL_DIR_TX;
	bool is_tx = vfd->vfl_dir != VFL_DIR_RX;

	if (ops == NULL)
		return -EINVAL;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if ((is_vid || is_tch) && is_rx &&
		    (ops->vidioc_g_fmt_vid_cap || ops->vidioc_g_fmt_vid_cap_mplane))
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (is_vid && is_rx && ops->vidioc_g_fmt_vid_cap_mplane)
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (is_vid && is_rx && ops->vidioc_g_fmt_vid_overlay)
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (is_vid && is_tx &&
		    (ops->vidioc_g_fmt_vid_out || ops->vidioc_g_fmt_vid_out_mplane))
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (is_vid && is_tx && ops->vidioc_g_fmt_vid_out_mplane)
			return 0;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		if (is_vid && is_tx && ops->vidioc_g_fmt_vid_out_overlay)
			return 0;
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (is_vbi && is_rx && ops->vidioc_g_fmt_vbi_cap)
			return 0;
		break;
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		if (is_vbi && is_tx && ops->vidioc_g_fmt_vbi_out)
			return 0;
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		if (is_vbi && is_rx && ops->vidioc_g_fmt_sliced_vbi_cap)
			return 0;
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		if (is_vbi && is_tx && ops->vidioc_g_fmt_sliced_vbi_out)
			return 0;
		break;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		if (is_sdr && is_rx && ops->vidioc_g_fmt_sdr_cap)
			return 0;
		break;
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		if (is_sdr && is_tx && ops->vidioc_g_fmt_sdr_out)
			return 0;
		break;
	case V4L2_BUF_TYPE_META_CAPTURE:
		if (is_vid && is_rx && ops->vidioc_g_fmt_meta_cap)
			return 0;
		break;
	case V4L2_BUF_TYPE_META_OUTPUT:
		if (is_vid && is_tx && ops->vidioc_g_fmt_meta_out)
			return 0;
		break;
	default:
		break;
	}
	return -EINVAL;
}

static void v4l_sanitize_format(struct v4l2_format *fmt)
{
	unsigned int offset;

	/* Make sure num_planes is not bogus */
	if (fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ||
	    fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		fmt->fmt.pix_mp.num_planes = min_t(u32, fmt->fmt.pix_mp.num_planes,
					       VIDEO_MAX_PLANES);

	/*
	 * The v4l2_pix_format structure has been extended with fields that were
	 * not previously required to be set to zero by applications. The priv
	 * field, when set to a magic value, indicates the the extended fields
	 * are valid. Otherwise they will contain undefined values. To simplify
	 * the API towards drivers zero the extended fields and set the priv
	 * field to the magic value when the extended pixel format structure
	 * isn't used by applications.
	 */

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    fmt->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return;

	if (fmt->fmt.pix.priv == V4L2_PIX_FMT_PRIV_MAGIC)
		return;

	fmt->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;

	offset = offsetof(struct v4l2_pix_format, priv)
	       + sizeof(fmt->fmt.pix.priv);
	memset(((void *)&fmt->fmt.pix) + offset, 0,
	       sizeof(fmt->fmt.pix) - offset);
}

static int v4l_querycap(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_capability *cap = (struct v4l2_capability *)arg;
	struct video_device *vfd = video_devdata(file);
	int ret;

	cap->version = LINUX_VERSION_CODE;
	cap->device_caps = vfd->device_caps;
	cap->capabilities = vfd->device_caps | V4L2_CAP_DEVICE_CAPS;

	ret = ops->vidioc_querycap(file, fh, cap);

	/*
	 * Drivers must not change device_caps, so check for this and
	 * warn if this happened.
	 */
	WARN_ON(cap->device_caps != vfd->device_caps);
	/*
	 * Check that capabilities is a superset of
	 * vfd->device_caps | V4L2_CAP_DEVICE_CAPS
	 */
	WARN_ON((cap->capabilities &
		 (vfd->device_caps | V4L2_CAP_DEVICE_CAPS)) !=
		(vfd->device_caps | V4L2_CAP_DEVICE_CAPS));
	cap->capabilities |= V4L2_CAP_EXT_PIX_FORMAT;
	cap->device_caps |= V4L2_CAP_EXT_PIX_FORMAT;

	return ret;
}

static int v4l_s_input(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	int ret;

	ret = v4l_enable_media_source(vfd);
	if (ret)
		return ret;
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

	vfd = video_devdata(file);
	*p = v4l2_prio_max(vfd->prio);
	return 0;
}

static int v4l_s_priority(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd;
	struct v4l2_fh *vfh;
	u32 *p = arg;

	vfd = video_devdata(file);
	if (!test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags))
		return -ENOTTY;
	vfh = file->private_data;
	return v4l2_prio_change(vfd->prio, &vfh->prio, *p);
}

static int v4l_enuminput(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_input *p = arg;

	/*
	 * We set the flags for CAP_DV_TIMINGS &
	 * CAP_STD here based on ioctl handler provided by the
	 * driver. If the driver doesn't support these
	 * for a specific input, it must override these flags.
	 */
	if (is_valid_ioctl(vfd, VIDIOC_S_STD))
		p->capabilities |= V4L2_IN_CAP_STD;

	return ops->vidioc_enum_input(file, fh, p);
}

static int v4l_enumoutput(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_output *p = arg;

	/*
	 * We set the flags for CAP_DV_TIMINGS &
	 * CAP_STD here based on ioctl handler provided by the
	 * driver. If the driver doesn't support these
	 * for a specific output, it must override these flags.
	 */
	if (is_valid_ioctl(vfd, VIDIOC_S_STD))
		p->capabilities |= V4L2_OUT_CAP_STD;

	return ops->vidioc_enum_output(file, fh, p);
}

static void v4l_fill_fmtdesc(struct v4l2_fmtdesc *fmt)
{
	const unsigned sz = sizeof(fmt->description);
	const char *descr = NULL;
	u32 flags = 0;

	/*
	 * We depart from the normal coding style here since the descriptions
	 * should be aligned so it is easy to see which descriptions will be
	 * longer than 31 characters (the max length for a description).
	 * And frankly, this is easier to read anyway.
	 *
	 * Note that gcc will use O(log N) comparisons to find the right case.
	 */
	switch (fmt->pixelformat) {
	/* Max description length mask:	descr = "0123456789012345678901234567890" */
	case V4L2_PIX_FMT_RGB332:	descr = "8-bit RGB 3-3-2"; break;
	case V4L2_PIX_FMT_RGB444:	descr = "16-bit A/XRGB 4-4-4-4"; break;
	case V4L2_PIX_FMT_ARGB444:	descr = "16-bit ARGB 4-4-4-4"; break;
	case V4L2_PIX_FMT_XRGB444:	descr = "16-bit XRGB 4-4-4-4"; break;
	case V4L2_PIX_FMT_RGBA444:	descr = "16-bit RGBA 4-4-4-4"; break;
	case V4L2_PIX_FMT_RGBX444:	descr = "16-bit RGBX 4-4-4-4"; break;
	case V4L2_PIX_FMT_ABGR444:	descr = "16-bit ABGR 4-4-4-4"; break;
	case V4L2_PIX_FMT_XBGR444:	descr = "16-bit XBGR 4-4-4-4"; break;
	case V4L2_PIX_FMT_BGRA444:	descr = "16-bit BGRA 4-4-4-4"; break;
	case V4L2_PIX_FMT_BGRX444:	descr = "16-bit BGRX 4-4-4-4"; break;
	case V4L2_PIX_FMT_RGB555:	descr = "16-bit A/XRGB 1-5-5-5"; break;
	case V4L2_PIX_FMT_ARGB555:	descr = "16-bit ARGB 1-5-5-5"; break;
	case V4L2_PIX_FMT_XRGB555:	descr = "16-bit XRGB 1-5-5-5"; break;
	case V4L2_PIX_FMT_ABGR555:	descr = "16-bit ABGR 1-5-5-5"; break;
	case V4L2_PIX_FMT_XBGR555:	descr = "16-bit XBGR 1-5-5-5"; break;
	case V4L2_PIX_FMT_RGBA555:	descr = "16-bit RGBA 5-5-5-1"; break;
	case V4L2_PIX_FMT_RGBX555:	descr = "16-bit RGBX 5-5-5-1"; break;
	case V4L2_PIX_FMT_BGRA555:	descr = "16-bit BGRA 5-5-5-1"; break;
	case V4L2_PIX_FMT_BGRX555:	descr = "16-bit BGRX 5-5-5-1"; break;
	case V4L2_PIX_FMT_RGB565:	descr = "16-bit RGB 5-6-5"; break;
	case V4L2_PIX_FMT_RGB555X:	descr = "16-bit A/XRGB 1-5-5-5 BE"; break;
	case V4L2_PIX_FMT_ARGB555X:	descr = "16-bit ARGB 1-5-5-5 BE"; break;
	case V4L2_PIX_FMT_XRGB555X:	descr = "16-bit XRGB 1-5-5-5 BE"; break;
	case V4L2_PIX_FMT_RGB565X:	descr = "16-bit RGB 5-6-5 BE"; break;
	case V4L2_PIX_FMT_BGR666:	descr = "18-bit BGRX 6-6-6-14"; break;
	case V4L2_PIX_FMT_BGR24:	descr = "24-bit BGR 8-8-8"; break;
	case V4L2_PIX_FMT_RGB24:	descr = "24-bit RGB 8-8-8"; break;
	case V4L2_PIX_FMT_BGR32:	descr = "32-bit BGRA/X 8-8-8-8"; break;
	case V4L2_PIX_FMT_ABGR32:	descr = "32-bit BGRA 8-8-8-8"; break;
	case V4L2_PIX_FMT_XBGR32:	descr = "32-bit BGRX 8-8-8-8"; break;
	case V4L2_PIX_FMT_RGB32:	descr = "32-bit A/XRGB 8-8-8-8"; break;
	case V4L2_PIX_FMT_ARGB32:	descr = "32-bit ARGB 8-8-8-8"; break;
	case V4L2_PIX_FMT_XRGB32:	descr = "32-bit XRGB 8-8-8-8"; break;
	case V4L2_PIX_FMT_BGRA32:	descr = "32-bit ABGR 8-8-8-8"; break;
	case V4L2_PIX_FMT_BGRX32:	descr = "32-bit XBGR 8-8-8-8"; break;
	case V4L2_PIX_FMT_RGBA32:	descr = "32-bit RGBA 8-8-8-8"; break;
	case V4L2_PIX_FMT_RGBX32:	descr = "32-bit RGBX 8-8-8-8"; break;
	case V4L2_PIX_FMT_GREY:		descr = "8-bit Greyscale"; break;
	case V4L2_PIX_FMT_Y4:		descr = "4-bit Greyscale"; break;
	case V4L2_PIX_FMT_Y6:		descr = "6-bit Greyscale"; break;
	case V4L2_PIX_FMT_Y10:		descr = "10-bit Greyscale"; break;
	case V4L2_PIX_FMT_Y12:		descr = "12-bit Greyscale"; break;
	case V4L2_PIX_FMT_Y16:		descr = "16-bit Greyscale"; break;
	case V4L2_PIX_FMT_Y16_BE:	descr = "16-bit Greyscale BE"; break;
	case V4L2_PIX_FMT_Y10BPACK:	descr = "10-bit Greyscale (Packed)"; break;
	case V4L2_PIX_FMT_Y10P:		descr = "10-bit Greyscale (MIPI Packed)"; break;
	case V4L2_PIX_FMT_Y8I:		descr = "Interleaved 8-bit Greyscale"; break;
	case V4L2_PIX_FMT_Y12I:		descr = "Interleaved 12-bit Greyscale"; break;
	case V4L2_PIX_FMT_Z16:		descr = "16-bit Depth"; break;
	case V4L2_PIX_FMT_INZI:		descr = "Planar 10:16 Greyscale Depth"; break;
	case V4L2_PIX_FMT_CNF4:		descr = "4-bit Depth Confidence (Packed)"; break;
	case V4L2_PIX_FMT_PAL8:		descr = "8-bit Palette"; break;
	case V4L2_PIX_FMT_UV8:		descr = "8-bit Chrominance UV 4-4"; break;
	case V4L2_PIX_FMT_YVU410:	descr = "Planar YVU 4:1:0"; break;
	case V4L2_PIX_FMT_YVU420:	descr = "Planar YVU 4:2:0"; break;
	case V4L2_PIX_FMT_YUYV:		descr = "YUYV 4:2:2"; break;
	case V4L2_PIX_FMT_YYUV:		descr = "YYUV 4:2:2"; break;
	case V4L2_PIX_FMT_YVYU:		descr = "YVYU 4:2:2"; break;
	case V4L2_PIX_FMT_UYVY:		descr = "UYVY 4:2:2"; break;
	case V4L2_PIX_FMT_VYUY:		descr = "VYUY 4:2:2"; break;
	case V4L2_PIX_FMT_YUV422P:	descr = "Planar YUV 4:2:2"; break;
	case V4L2_PIX_FMT_YUV411P:	descr = "Planar YUV 4:1:1"; break;
	case V4L2_PIX_FMT_Y41P:		descr = "YUV 4:1:1 (Packed)"; break;
	case V4L2_PIX_FMT_YUV444:	descr = "16-bit A/XYUV 4-4-4-4"; break;
	case V4L2_PIX_FMT_YUV555:	descr = "16-bit A/XYUV 1-5-5-5"; break;
	case V4L2_PIX_FMT_YUV565:	descr = "16-bit YUV 5-6-5"; break;
	case V4L2_PIX_FMT_YUV32:	descr = "32-bit A/XYUV 8-8-8-8"; break;
	case V4L2_PIX_FMT_AYUV32:	descr = "32-bit AYUV 8-8-8-8"; break;
	case V4L2_PIX_FMT_XYUV32:	descr = "32-bit XYUV 8-8-8-8"; break;
	case V4L2_PIX_FMT_VUYA32:	descr = "32-bit VUYA 8-8-8-8"; break;
	case V4L2_PIX_FMT_VUYX32:	descr = "32-bit VUYX 8-8-8-8"; break;
	case V4L2_PIX_FMT_YUV410:	descr = "Planar YUV 4:1:0"; break;
	case V4L2_PIX_FMT_YUV420:	descr = "Planar YUV 4:2:0"; break;
	case V4L2_PIX_FMT_HI240:	descr = "8-bit Dithered RGB (BTTV)"; break;
	case V4L2_PIX_FMT_HM12:		descr = "YUV 4:2:0 (16x16 Macroblocks)"; break;
	case V4L2_PIX_FMT_M420:		descr = "YUV 4:2:0 (M420)"; break;
	case V4L2_PIX_FMT_NV12:		descr = "Y/CbCr 4:2:0"; break;
	case V4L2_PIX_FMT_NV21:		descr = "Y/CrCb 4:2:0"; break;
	case V4L2_PIX_FMT_NV16:		descr = "Y/CbCr 4:2:2"; break;
	case V4L2_PIX_FMT_NV61:		descr = "Y/CrCb 4:2:2"; break;
	case V4L2_PIX_FMT_NV24:		descr = "Y/CbCr 4:4:4"; break;
	case V4L2_PIX_FMT_NV42:		descr = "Y/CrCb 4:4:4"; break;
	case V4L2_PIX_FMT_NV12M:	descr = "Y/CbCr 4:2:0 (N-C)"; break;
	case V4L2_PIX_FMT_NV21M:	descr = "Y/CrCb 4:2:0 (N-C)"; break;
	case V4L2_PIX_FMT_NV16M:	descr = "Y/CbCr 4:2:2 (N-C)"; break;
	case V4L2_PIX_FMT_NV61M:	descr = "Y/CrCb 4:2:2 (N-C)"; break;
	case V4L2_PIX_FMT_NV12MT:	descr = "Y/CbCr 4:2:0 (64x32 MB, N-C)"; break;
	case V4L2_PIX_FMT_NV12MT_16X16:	descr = "Y/CbCr 4:2:0 (16x16 MB, N-C)"; break;
	case V4L2_PIX_FMT_YUV420M:	descr = "Planar YUV 4:2:0 (N-C)"; break;
	case V4L2_PIX_FMT_YVU420M:	descr = "Planar YVU 4:2:0 (N-C)"; break;
	case V4L2_PIX_FMT_YUV422M:	descr = "Planar YUV 4:2:2 (N-C)"; break;
	case V4L2_PIX_FMT_YVU422M:	descr = "Planar YVU 4:2:2 (N-C)"; break;
	case V4L2_PIX_FMT_YUV444M:	descr = "Planar YUV 4:4:4 (N-C)"; break;
	case V4L2_PIX_FMT_YVU444M:	descr = "Planar YVU 4:4:4 (N-C)"; break;
	case V4L2_PIX_FMT_SBGGR8:	descr = "8-bit Bayer BGBG/GRGR"; break;
	case V4L2_PIX_FMT_SGBRG8:	descr = "8-bit Bayer GBGB/RGRG"; break;
	case V4L2_PIX_FMT_SGRBG8:	descr = "8-bit Bayer GRGR/BGBG"; break;
	case V4L2_PIX_FMT_SRGGB8:	descr = "8-bit Bayer RGRG/GBGB"; break;
	case V4L2_PIX_FMT_SBGGR10:	descr = "10-bit Bayer BGBG/GRGR"; break;
	case V4L2_PIX_FMT_SGBRG10:	descr = "10-bit Bayer GBGB/RGRG"; break;
	case V4L2_PIX_FMT_SGRBG10:	descr = "10-bit Bayer GRGR/BGBG"; break;
	case V4L2_PIX_FMT_SRGGB10:	descr = "10-bit Bayer RGRG/GBGB"; break;
	case V4L2_PIX_FMT_SBGGR10P:	descr = "10-bit Bayer BGBG/GRGR Packed"; break;
	case V4L2_PIX_FMT_SGBRG10P:	descr = "10-bit Bayer GBGB/RGRG Packed"; break;
	case V4L2_PIX_FMT_SGRBG10P:	descr = "10-bit Bayer GRGR/BGBG Packed"; break;
	case V4L2_PIX_FMT_SRGGB10P:	descr = "10-bit Bayer RGRG/GBGB Packed"; break;
	case V4L2_PIX_FMT_IPU3_SBGGR10: descr = "10-bit bayer BGGR IPU3 Packed"; break;
	case V4L2_PIX_FMT_IPU3_SGBRG10: descr = "10-bit bayer GBRG IPU3 Packed"; break;
	case V4L2_PIX_FMT_IPU3_SGRBG10: descr = "10-bit bayer GRBG IPU3 Packed"; break;
	case V4L2_PIX_FMT_IPU3_SRGGB10: descr = "10-bit bayer RGGB IPU3 Packed"; break;
	case V4L2_PIX_FMT_SBGGR10ALAW8:	descr = "8-bit Bayer BGBG/GRGR (A-law)"; break;
	case V4L2_PIX_FMT_SGBRG10ALAW8:	descr = "8-bit Bayer GBGB/RGRG (A-law)"; break;
	case V4L2_PIX_FMT_SGRBG10ALAW8:	descr = "8-bit Bayer GRGR/BGBG (A-law)"; break;
	case V4L2_PIX_FMT_SRGGB10ALAW8:	descr = "8-bit Bayer RGRG/GBGB (A-law)"; break;
	case V4L2_PIX_FMT_SBGGR10DPCM8:	descr = "8-bit Bayer BGBG/GRGR (DPCM)"; break;
	case V4L2_PIX_FMT_SGBRG10DPCM8:	descr = "8-bit Bayer GBGB/RGRG (DPCM)"; break;
	case V4L2_PIX_FMT_SGRBG10DPCM8:	descr = "8-bit Bayer GRGR/BGBG (DPCM)"; break;
	case V4L2_PIX_FMT_SRGGB10DPCM8:	descr = "8-bit Bayer RGRG/GBGB (DPCM)"; break;
	case V4L2_PIX_FMT_SBGGR12:	descr = "12-bit Bayer BGBG/GRGR"; break;
	case V4L2_PIX_FMT_SGBRG12:	descr = "12-bit Bayer GBGB/RGRG"; break;
	case V4L2_PIX_FMT_SGRBG12:	descr = "12-bit Bayer GRGR/BGBG"; break;
	case V4L2_PIX_FMT_SRGGB12:	descr = "12-bit Bayer RGRG/GBGB"; break;
	case V4L2_PIX_FMT_SBGGR12P:	descr = "12-bit Bayer BGBG/GRGR Packed"; break;
	case V4L2_PIX_FMT_SGBRG12P:	descr = "12-bit Bayer GBGB/RGRG Packed"; break;
	case V4L2_PIX_FMT_SGRBG12P:	descr = "12-bit Bayer GRGR/BGBG Packed"; break;
	case V4L2_PIX_FMT_SRGGB12P:	descr = "12-bit Bayer RGRG/GBGB Packed"; break;
	case V4L2_PIX_FMT_SBGGR14P:	descr = "14-bit Bayer BGBG/GRGR Packed"; break;
	case V4L2_PIX_FMT_SGBRG14P:	descr = "14-bit Bayer GBGB/RGRG Packed"; break;
	case V4L2_PIX_FMT_SGRBG14P:	descr = "14-bit Bayer GRGR/BGBG Packed"; break;
	case V4L2_PIX_FMT_SRGGB14P:	descr = "14-bit Bayer RGRG/GBGB Packed"; break;
	case V4L2_PIX_FMT_SBGGR16:	descr = "16-bit Bayer BGBG/GRGR"; break;
	case V4L2_PIX_FMT_SGBRG16:	descr = "16-bit Bayer GBGB/RGRG"; break;
	case V4L2_PIX_FMT_SGRBG16:	descr = "16-bit Bayer GRGR/BGBG"; break;
	case V4L2_PIX_FMT_SRGGB16:	descr = "16-bit Bayer RGRG/GBGB"; break;
	case V4L2_PIX_FMT_SN9C20X_I420:	descr = "GSPCA SN9C20X I420"; break;
	case V4L2_PIX_FMT_SPCA501:	descr = "GSPCA SPCA501"; break;
	case V4L2_PIX_FMT_SPCA505:	descr = "GSPCA SPCA505"; break;
	case V4L2_PIX_FMT_SPCA508:	descr = "GSPCA SPCA508"; break;
	case V4L2_PIX_FMT_STV0680:	descr = "GSPCA STV0680"; break;
	case V4L2_PIX_FMT_TM6000:	descr = "A/V + VBI Mux Packet"; break;
	case V4L2_PIX_FMT_CIT_YYVYUY:	descr = "GSPCA CIT YYVYUY"; break;
	case V4L2_PIX_FMT_KONICA420:	descr = "GSPCA KONICA420"; break;
	case V4L2_PIX_FMT_HSV24:	descr = "24-bit HSV 8-8-8"; break;
	case V4L2_PIX_FMT_HSV32:	descr = "32-bit XHSV 8-8-8-8"; break;
	case V4L2_SDR_FMT_CU8:		descr = "Complex U8"; break;
	case V4L2_SDR_FMT_CU16LE:	descr = "Complex U16LE"; break;
	case V4L2_SDR_FMT_CS8:		descr = "Complex S8"; break;
	case V4L2_SDR_FMT_CS14LE:	descr = "Complex S14LE"; break;
	case V4L2_SDR_FMT_RU12LE:	descr = "Real U12LE"; break;
	case V4L2_SDR_FMT_PCU16BE:	descr = "Planar Complex U16BE"; break;
	case V4L2_SDR_FMT_PCU18BE:	descr = "Planar Complex U18BE"; break;
	case V4L2_SDR_FMT_PCU20BE:	descr = "Planar Complex U20BE"; break;
	case V4L2_TCH_FMT_DELTA_TD16:	descr = "16-bit Signed Deltas"; break;
	case V4L2_TCH_FMT_DELTA_TD08:	descr = "8-bit Signed Deltas"; break;
	case V4L2_TCH_FMT_TU16:		descr = "16-bit Unsigned Touch Data"; break;
	case V4L2_TCH_FMT_TU08:		descr = "8-bit Unsigned Touch Data"; break;
	case V4L2_META_FMT_VSP1_HGO:	descr = "R-Car VSP1 1-D Histogram"; break;
	case V4L2_META_FMT_VSP1_HGT:	descr = "R-Car VSP1 2-D Histogram"; break;
	case V4L2_META_FMT_UVC:		descr = "UVC Payload Header Metadata"; break;
	case V4L2_META_FMT_D4XX:	descr = "Intel D4xx UVC Metadata"; break;

	default:
		/* Compressed formats */
		flags = V4L2_FMT_FLAG_COMPRESSED;
		switch (fmt->pixelformat) {
		/* Max description length mask:	descr = "0123456789012345678901234567890" */
		case V4L2_PIX_FMT_MJPEG:	descr = "Motion-JPEG"; break;
		case V4L2_PIX_FMT_JPEG:		descr = "JFIF JPEG"; break;
		case V4L2_PIX_FMT_DV:		descr = "1394"; break;
		case V4L2_PIX_FMT_MPEG:		descr = "MPEG-1/2/4"; break;
		case V4L2_PIX_FMT_H264:		descr = "H.264"; break;
		case V4L2_PIX_FMT_H264_NO_SC:	descr = "H.264 (No Start Codes)"; break;
		case V4L2_PIX_FMT_H264_MVC:	descr = "H.264 MVC"; break;
		case V4L2_PIX_FMT_H264_SLICE_RAW:	descr = "H.264 Parsed Slice Data"; break;
		case V4L2_PIX_FMT_H263:		descr = "H.263"; break;
		case V4L2_PIX_FMT_MPEG1:	descr = "MPEG-1 ES"; break;
		case V4L2_PIX_FMT_MPEG2:	descr = "MPEG-2 ES"; break;
		case V4L2_PIX_FMT_MPEG2_SLICE:	descr = "MPEG-2 Parsed Slice Data"; break;
		case V4L2_PIX_FMT_MPEG4:	descr = "MPEG-4 Part 2 ES"; break;
		case V4L2_PIX_FMT_XVID:		descr = "Xvid"; break;
		case V4L2_PIX_FMT_VC1_ANNEX_G:	descr = "VC-1 (SMPTE 412M Annex G)"; break;
		case V4L2_PIX_FMT_VC1_ANNEX_L:	descr = "VC-1 (SMPTE 412M Annex L)"; break;
		case V4L2_PIX_FMT_VP8:		descr = "VP8"; break;
		case V4L2_PIX_FMT_VP8_FRAME:    descr = "VP8 Frame"; break;
		case V4L2_PIX_FMT_VP9:		descr = "VP9"; break;
		case V4L2_PIX_FMT_HEVC:		descr = "HEVC"; break; /* aka H.265 */
		case V4L2_PIX_FMT_FWHT:		descr = "FWHT"; break; /* used in vicodec */
		case V4L2_PIX_FMT_FWHT_STATELESS:	descr = "FWHT Stateless"; break; /* used in vicodec */
		case V4L2_PIX_FMT_CPIA1:	descr = "GSPCA CPiA YUV"; break;
		case V4L2_PIX_FMT_WNVA:		descr = "WNVA"; break;
		case V4L2_PIX_FMT_SN9C10X:	descr = "GSPCA SN9C10X"; break;
		case V4L2_PIX_FMT_PWC1:		descr = "Raw Philips Webcam Type (Old)"; break;
		case V4L2_PIX_FMT_PWC2:		descr = "Raw Philips Webcam Type (New)"; break;
		case V4L2_PIX_FMT_ET61X251:	descr = "GSPCA ET61X251"; break;
		case V4L2_PIX_FMT_SPCA561:	descr = "GSPCA SPCA561"; break;
		case V4L2_PIX_FMT_PAC207:	descr = "GSPCA PAC207"; break;
		case V4L2_PIX_FMT_MR97310A:	descr = "GSPCA MR97310A"; break;
		case V4L2_PIX_FMT_JL2005BCD:	descr = "GSPCA JL2005BCD"; break;
		case V4L2_PIX_FMT_SN9C2028:	descr = "GSPCA SN9C2028"; break;
		case V4L2_PIX_FMT_SQ905C:	descr = "GSPCA SQ905C"; break;
		case V4L2_PIX_FMT_PJPG:		descr = "GSPCA PJPG"; break;
		case V4L2_PIX_FMT_OV511:	descr = "GSPCA OV511"; break;
		case V4L2_PIX_FMT_OV518:	descr = "GSPCA OV518"; break;
		case V4L2_PIX_FMT_JPGL:		descr = "JPEG Lite"; break;
		case V4L2_PIX_FMT_SE401:	descr = "GSPCA SE401"; break;
		case V4L2_PIX_FMT_S5C_UYVY_JPG:	descr = "S5C73MX interleaved UYVY/JPEG"; break;
		case V4L2_PIX_FMT_MT21C:	descr = "Mediatek Compressed Format"; break;
		case V4L2_PIX_FMT_SUNXI_TILED_NV12: descr = "Sunxi Tiled NV12 Format"; break;
		default:
			if (fmt->description[0])
				return;
			WARN(1, "Unknown pixelformat 0x%08x\n", fmt->pixelformat);
			flags = 0;
			snprintf(fmt->description, sz, "%c%c%c%c%s",
					(char)(fmt->pixelformat & 0x7f),
					(char)((fmt->pixelformat >> 8) & 0x7f),
					(char)((fmt->pixelformat >> 16) & 0x7f),
					(char)((fmt->pixelformat >> 24) & 0x7f),
					(fmt->pixelformat & (1 << 31)) ? "-BE" : "");
			break;
		}
	}

	if (descr)
		WARN_ON(strscpy(fmt->description, descr, sz) < 0);
	fmt->flags = flags;
}

static int v4l_enum_fmt(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_fmtdesc *p = arg;
	int ret = check_fmt(file, p->type);
	u32 cap_mask;

	if (ret)
		return ret;
	ret = -EINVAL;

	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		cap_mask = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
			   V4L2_CAP_VIDEO_M2M_MPLANE;
		if (!!(vdev->device_caps & cap_mask) !=
		    (p->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE))
			break;

		if (unlikely(!ops->vidioc_enum_fmt_vid_cap))
			break;
		ret = ops->vidioc_enum_fmt_vid_cap(file, fh, arg);
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (unlikely(!ops->vidioc_enum_fmt_vid_overlay))
			break;
		ret = ops->vidioc_enum_fmt_vid_overlay(file, fh, arg);
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		cap_mask = V4L2_CAP_VIDEO_OUTPUT_MPLANE |
			   V4L2_CAP_VIDEO_M2M_MPLANE;
		if (!!(vdev->device_caps & cap_mask) !=
		    (p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE))
			break;

		if (unlikely(!ops->vidioc_enum_fmt_vid_out))
			break;
		ret = ops->vidioc_enum_fmt_vid_out(file, fh, arg);
		break;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		if (unlikely(!ops->vidioc_enum_fmt_sdr_cap))
			break;
		ret = ops->vidioc_enum_fmt_sdr_cap(file, fh, arg);
		break;
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		if (unlikely(!ops->vidioc_enum_fmt_sdr_out))
			break;
		ret = ops->vidioc_enum_fmt_sdr_out(file, fh, arg);
		break;
	case V4L2_BUF_TYPE_META_CAPTURE:
		if (unlikely(!ops->vidioc_enum_fmt_meta_cap))
			break;
		ret = ops->vidioc_enum_fmt_meta_cap(file, fh, arg);
		break;
	case V4L2_BUF_TYPE_META_OUTPUT:
		if (unlikely(!ops->vidioc_enum_fmt_meta_out))
			break;
		ret = ops->vidioc_enum_fmt_meta_out(file, fh, arg);
		break;
	}
	if (ret == 0)
		v4l_fill_fmtdesc(p);
	return ret;
}

static int v4l_g_fmt(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_format *p = arg;
	int ret = check_fmt(file, p->type);

	if (ret)
		return ret;

	/*
	 * fmt can't be cleared for these overlay types due to the 'clips'
	 * 'clipcount' and 'bitmap' pointers in struct v4l2_window.
	 * Those are provided by the user. So handle these two overlay types
	 * first, and then just do a simple memset for the other types.
	 */
	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY: {
		struct v4l2_clip __user *clips = p->fmt.win.clips;
		u32 clipcount = p->fmt.win.clipcount;
		void __user *bitmap = p->fmt.win.bitmap;

		memset(&p->fmt, 0, sizeof(p->fmt));
		p->fmt.win.clips = clips;
		p->fmt.win.clipcount = clipcount;
		p->fmt.win.bitmap = bitmap;
		break;
	}
	default:
		memset(&p->fmt, 0, sizeof(p->fmt));
		break;
	}

	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (unlikely(!ops->vidioc_g_fmt_vid_cap))
			break;
		p->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
		ret = ops->vidioc_g_fmt_vid_cap(file, fh, arg);
		/* just in case the driver zeroed it again */
		p->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
		return ret;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return ops->vidioc_g_fmt_vid_cap_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return ops->vidioc_g_fmt_vid_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return ops->vidioc_g_fmt_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		return ops->vidioc_g_fmt_sliced_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (unlikely(!ops->vidioc_g_fmt_vid_out))
			break;
		p->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
		ret = ops->vidioc_g_fmt_vid_out(file, fh, arg);
		/* just in case the driver zeroed it again */
		p->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
		return ret;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return ops->vidioc_g_fmt_vid_out_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return ops->vidioc_g_fmt_vid_out_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return ops->vidioc_g_fmt_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return ops->vidioc_g_fmt_sliced_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		return ops->vidioc_g_fmt_sdr_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		return ops->vidioc_g_fmt_sdr_out(file, fh, arg);
	case V4L2_BUF_TYPE_META_CAPTURE:
		return ops->vidioc_g_fmt_meta_cap(file, fh, arg);
	case V4L2_BUF_TYPE_META_OUTPUT:
		return ops->vidioc_g_fmt_meta_out(file, fh, arg);
	}
	return -EINVAL;
}

static void v4l_pix_format_touch(struct v4l2_pix_format *p)
{
	/*
	 * The v4l2_pix_format structure contains fields that make no sense for
	 * touch. Set them to default values in this case.
	 */

	p->field = V4L2_FIELD_NONE;
	p->colorspace = V4L2_COLORSPACE_RAW;
	p->flags = 0;
	p->ycbcr_enc = 0;
	p->quantization = 0;
	p->xfer_func = 0;
}

static int v4l_s_fmt(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_format *p = arg;
	struct video_device *vfd = video_devdata(file);
	int ret = check_fmt(file, p->type);
	unsigned int i;

	if (ret)
		return ret;

	ret = v4l_enable_media_source(vfd);
	if (ret)
		return ret;
	v4l_sanitize_format(p);

	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (unlikely(!ops->vidioc_s_fmt_vid_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix);
		ret = ops->vidioc_s_fmt_vid_cap(file, fh, arg);
		/* just in case the driver zeroed it again */
		p->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
		if (vfd->vfl_type == VFL_TYPE_TOUCH)
			v4l_pix_format_touch(&p->fmt.pix);
		return ret;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (unlikely(!ops->vidioc_s_fmt_vid_cap_mplane))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix_mp.xfer_func);
		for (i = 0; i < p->fmt.pix_mp.num_planes; i++)
			CLEAR_AFTER_FIELD(&p->fmt.pix_mp.plane_fmt[i],
					  bytesperline);
		return ops->vidioc_s_fmt_vid_cap_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (unlikely(!ops->vidioc_s_fmt_vid_overlay))
			break;
		CLEAR_AFTER_FIELD(p, fmt.win);
		return ops->vidioc_s_fmt_vid_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_s_fmt_vbi_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.vbi);
		return ops->vidioc_s_fmt_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_s_fmt_sliced_vbi_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sliced);
		return ops->vidioc_s_fmt_sliced_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (unlikely(!ops->vidioc_s_fmt_vid_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix);
		ret = ops->vidioc_s_fmt_vid_out(file, fh, arg);
		/* just in case the driver zeroed it again */
		p->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
		return ret;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (unlikely(!ops->vidioc_s_fmt_vid_out_mplane))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix_mp.xfer_func);
		for (i = 0; i < p->fmt.pix_mp.num_planes; i++)
			CLEAR_AFTER_FIELD(&p->fmt.pix_mp.plane_fmt[i],
					  bytesperline);
		return ops->vidioc_s_fmt_vid_out_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		if (unlikely(!ops->vidioc_s_fmt_vid_out_overlay))
			break;
		CLEAR_AFTER_FIELD(p, fmt.win);
		return ops->vidioc_s_fmt_vid_out_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_s_fmt_vbi_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.vbi);
		return ops->vidioc_s_fmt_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_s_fmt_sliced_vbi_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sliced);
		return ops->vidioc_s_fmt_sliced_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		if (unlikely(!ops->vidioc_s_fmt_sdr_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sdr);
		return ops->vidioc_s_fmt_sdr_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		if (unlikely(!ops->vidioc_s_fmt_sdr_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sdr);
		return ops->vidioc_s_fmt_sdr_out(file, fh, arg);
	case V4L2_BUF_TYPE_META_CAPTURE:
		if (unlikely(!ops->vidioc_s_fmt_meta_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.meta);
		return ops->vidioc_s_fmt_meta_cap(file, fh, arg);
	case V4L2_BUF_TYPE_META_OUTPUT:
		if (unlikely(!ops->vidioc_s_fmt_meta_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.meta);
		return ops->vidioc_s_fmt_meta_out(file, fh, arg);
	}
	return -EINVAL;
}

static int v4l_try_fmt(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_format *p = arg;
	struct video_device *vfd = video_devdata(file);
	int ret = check_fmt(file, p->type);
	unsigned int i;

	if (ret)
		return ret;

	v4l_sanitize_format(p);

	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (unlikely(!ops->vidioc_try_fmt_vid_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix);
		ret = ops->vidioc_try_fmt_vid_cap(file, fh, arg);
		/* just in case the driver zeroed it again */
		p->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
		if (vfd->vfl_type == VFL_TYPE_TOUCH)
			v4l_pix_format_touch(&p->fmt.pix);
		return ret;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (unlikely(!ops->vidioc_try_fmt_vid_cap_mplane))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix_mp.xfer_func);
		for (i = 0; i < p->fmt.pix_mp.num_planes; i++)
			CLEAR_AFTER_FIELD(&p->fmt.pix_mp.plane_fmt[i],
					  bytesperline);
		return ops->vidioc_try_fmt_vid_cap_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (unlikely(!ops->vidioc_try_fmt_vid_overlay))
			break;
		CLEAR_AFTER_FIELD(p, fmt.win);
		return ops->vidioc_try_fmt_vid_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_try_fmt_vbi_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.vbi);
		return ops->vidioc_try_fmt_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		if (unlikely(!ops->vidioc_try_fmt_sliced_vbi_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sliced);
		return ops->vidioc_try_fmt_sliced_vbi_cap(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (unlikely(!ops->vidioc_try_fmt_vid_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix);
		ret = ops->vidioc_try_fmt_vid_out(file, fh, arg);
		/* just in case the driver zeroed it again */
		p->fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
		return ret;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (unlikely(!ops->vidioc_try_fmt_vid_out_mplane))
			break;
		CLEAR_AFTER_FIELD(p, fmt.pix_mp.xfer_func);
		for (i = 0; i < p->fmt.pix_mp.num_planes; i++)
			CLEAR_AFTER_FIELD(&p->fmt.pix_mp.plane_fmt[i],
					  bytesperline);
		return ops->vidioc_try_fmt_vid_out_mplane(file, fh, arg);
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		if (unlikely(!ops->vidioc_try_fmt_vid_out_overlay))
			break;
		CLEAR_AFTER_FIELD(p, fmt.win);
		return ops->vidioc_try_fmt_vid_out_overlay(file, fh, arg);
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_try_fmt_vbi_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.vbi);
		return ops->vidioc_try_fmt_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		if (unlikely(!ops->vidioc_try_fmt_sliced_vbi_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sliced);
		return ops->vidioc_try_fmt_sliced_vbi_out(file, fh, arg);
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		if (unlikely(!ops->vidioc_try_fmt_sdr_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sdr);
		return ops->vidioc_try_fmt_sdr_cap(file, fh, arg);
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		if (unlikely(!ops->vidioc_try_fmt_sdr_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.sdr);
		return ops->vidioc_try_fmt_sdr_out(file, fh, arg);
	case V4L2_BUF_TYPE_META_CAPTURE:
		if (unlikely(!ops->vidioc_try_fmt_meta_cap))
			break;
		CLEAR_AFTER_FIELD(p, fmt.meta);
		return ops->vidioc_try_fmt_meta_cap(file, fh, arg);
	case V4L2_BUF_TYPE_META_OUTPUT:
		if (unlikely(!ops->vidioc_try_fmt_meta_out))
			break;
		CLEAR_AFTER_FIELD(p, fmt.meta);
		return ops->vidioc_try_fmt_meta_out(file, fh, arg);
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
	int err;

	p->type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
			V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	err = ops->vidioc_g_tuner(file, fh, p);
	if (!err)
		p->capability |= V4L2_TUNER_CAP_FREQ_BANDS;
	return err;
}

static int v4l_s_tuner(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_tuner *p = arg;
	int ret;

	ret = v4l_enable_media_source(vfd);
	if (ret)
		return ret;
	p->type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
			V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	return ops->vidioc_s_tuner(file, fh, p);
}

static int v4l_g_modulator(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_modulator *p = arg;
	int err;

	if (vfd->vfl_type == VFL_TYPE_RADIO)
		p->type = V4L2_TUNER_RADIO;

	err = ops->vidioc_g_modulator(file, fh, p);
	if (!err)
		p->capability |= V4L2_TUNER_CAP_FREQ_BANDS;
	return err;
}

static int v4l_s_modulator(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_modulator *p = arg;

	if (vfd->vfl_type == VFL_TYPE_RADIO)
		p->type = V4L2_TUNER_RADIO;

	return ops->vidioc_s_modulator(file, fh, p);
}

static int v4l_g_frequency(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_frequency *p = arg;

	if (vfd->vfl_type == VFL_TYPE_SDR)
		p->type = V4L2_TUNER_SDR;
	else
		p->type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
				V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	return ops->vidioc_g_frequency(file, fh, p);
}

static int v4l_s_frequency(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	const struct v4l2_frequency *p = arg;
	enum v4l2_tuner_type type;
	int ret;

	ret = v4l_enable_media_source(vfd);
	if (ret)
		return ret;
	if (vfd->vfl_type == VFL_TYPE_SDR) {
		if (p->type != V4L2_TUNER_SDR && p->type != V4L2_TUNER_RF)
			return -EINVAL;
	} else {
		type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
				V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
		if (type != p->type)
			return -EINVAL;
	}
	return ops->vidioc_s_frequency(file, fh, p);
}

static int v4l_enumstd(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_standard *p = arg;

	return v4l_video_std_enumstd(p, vfd->tvnorms);
}

static int v4l_s_std(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	v4l2_std_id id = *(v4l2_std_id *)arg, norm;
	int ret;

	ret = v4l_enable_media_source(vfd);
	if (ret)
		return ret;
	norm = id & vfd->tvnorms;
	if (vfd->tvnorms && !norm)	/* Check if std is supported */
		return -EINVAL;

	/* Calls the specific handler */
	return ops->vidioc_s_std(file, fh, norm);
}

static int v4l_querystd(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	v4l2_std_id *p = arg;
	int ret;

	ret = v4l_enable_media_source(vfd);
	if (ret)
		return ret;
	/*
	 * If no signal is detected, then the driver should return
	 * V4L2_STD_UNKNOWN. Otherwise it should return tvnorms with
	 * any standards that do not apply removed.
	 *
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
	int ret;

	ret = v4l_enable_media_source(vfd);
	if (ret)
		return ret;
	/* s_hw_freq_seek is not supported for SDR for now */
	if (vfd->vfl_type == VFL_TYPE_SDR)
		return -EINVAL;

	type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
		V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	if (p->type != type)
		return -EINVAL;
	return ops->vidioc_s_hw_freq_seek(file, fh, p);
}

static int v4l_overlay(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	return ops->vidioc_overlay(file, fh, *(unsigned int *)arg);
}

static int v4l_reqbufs(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_requestbuffers *p = arg;
	int ret = check_fmt(file, p->type);

	if (ret)
		return ret;

	CLEAR_AFTER_FIELD(p, capabilities);

	return ops->vidioc_reqbufs(file, fh, p);
}

static int v4l_querybuf(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_buffer *p = arg;
	int ret = check_fmt(file, p->type);

	return ret ? ret : ops->vidioc_querybuf(file, fh, p);
}

static int v4l_qbuf(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_buffer *p = arg;
	int ret = check_fmt(file, p->type);

	return ret ? ret : ops->vidioc_qbuf(file, fh, p);
}

static int v4l_dqbuf(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_buffer *p = arg;
	int ret = check_fmt(file, p->type);

	return ret ? ret : ops->vidioc_dqbuf(file, fh, p);
}

static int v4l_create_bufs(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_create_buffers *create = arg;
	int ret = check_fmt(file, create->format.type);

	if (ret)
		return ret;

	CLEAR_AFTER_FIELD(create, capabilities);

	v4l_sanitize_format(&create->format);

	ret = ops->vidioc_create_bufs(file, fh, create);

	if (create->format.type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    create->format.type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		create->format.fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;

	return ret;
}

static int v4l_prepare_buf(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_buffer *b = arg;
	int ret = check_fmt(file, b->type);

	return ret ? ret : ops->vidioc_prepare_buf(file, fh, b);
}

static int v4l_g_parm(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_streamparm *p = arg;
	v4l2_std_id std;
	int ret = check_fmt(file, p->type);

	if (ret)
		return ret;
	if (ops->vidioc_g_parm)
		return ops->vidioc_g_parm(file, fh, p);
	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;
	p->parm.capture.readbuffers = 2;
	ret = ops->vidioc_g_std(file, fh, &std);
	if (ret == 0)
		v4l2_video_std_frame_period(std, &p->parm.capture.timeperframe);
	return ret;
}

static int v4l_s_parm(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_streamparm *p = arg;
	int ret = check_fmt(file, p->type);

	if (ret)
		return ret;

	/* Note: extendedmode is never used in drivers */
	if (V4L2_TYPE_IS_OUTPUT(p->type)) {
		memset(p->parm.output.reserved, 0,
		       sizeof(p->parm.output.reserved));
		p->parm.output.extendedmode = 0;
		p->parm.output.outputmode &= V4L2_MODE_HIGHQUALITY;
	} else {
		memset(p->parm.capture.reserved, 0,
		       sizeof(p->parm.capture.reserved));
		p->parm.capture.extendedmode = 0;
		p->parm.capture.capturemode &= V4L2_MODE_HIGHQUALITY;
	}
	return ops->vidioc_s_parm(file, fh, p);
}

static int v4l_queryctrl(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_queryctrl *p = arg;
	struct v4l2_fh *vfh =
		test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) ? fh : NULL;

	if (vfh && vfh->ctrl_handler)
		return v4l2_queryctrl(vfh->ctrl_handler, p);
	if (vfd->ctrl_handler)
		return v4l2_queryctrl(vfd->ctrl_handler, p);
	if (ops->vidioc_queryctrl)
		return ops->vidioc_queryctrl(file, fh, p);
	return -ENOTTY;
}

static int v4l_query_ext_ctrl(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_query_ext_ctrl *p = arg;
	struct v4l2_fh *vfh =
		test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) ? fh : NULL;

	if (vfh && vfh->ctrl_handler)
		return v4l2_query_ext_ctrl(vfh->ctrl_handler, p);
	if (vfd->ctrl_handler)
		return v4l2_query_ext_ctrl(vfd->ctrl_handler, p);
	if (ops->vidioc_query_ext_ctrl)
		return ops->vidioc_query_ext_ctrl(file, fh, p);
	return -ENOTTY;
}

static int v4l_querymenu(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_querymenu *p = arg;
	struct v4l2_fh *vfh =
		test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) ? fh : NULL;

	if (vfh && vfh->ctrl_handler)
		return v4l2_querymenu(vfh->ctrl_handler, p);
	if (vfd->ctrl_handler)
		return v4l2_querymenu(vfd->ctrl_handler, p);
	if (ops->vidioc_querymenu)
		return ops->vidioc_querymenu(file, fh, p);
	return -ENOTTY;
}

static int v4l_g_ctrl(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_control *p = arg;
	struct v4l2_fh *vfh =
		test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) ? fh : NULL;
	struct v4l2_ext_controls ctrls;
	struct v4l2_ext_control ctrl;

	if (vfh && vfh->ctrl_handler)
		return v4l2_g_ctrl(vfh->ctrl_handler, p);
	if (vfd->ctrl_handler)
		return v4l2_g_ctrl(vfd->ctrl_handler, p);
	if (ops->vidioc_g_ctrl)
		return ops->vidioc_g_ctrl(file, fh, p);
	if (ops->vidioc_g_ext_ctrls == NULL)
		return -ENOTTY;

	ctrls.which = V4L2_CTRL_ID2WHICH(p->id);
	ctrls.count = 1;
	ctrls.controls = &ctrl;
	ctrl.id = p->id;
	ctrl.value = p->value;
	if (check_ext_ctrls(&ctrls, 1)) {
		int ret = ops->vidioc_g_ext_ctrls(file, fh, &ctrls);

		if (ret == 0)
			p->value = ctrl.value;
		return ret;
	}
	return -EINVAL;
}

static int v4l_s_ctrl(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_control *p = arg;
	struct v4l2_fh *vfh =
		test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) ? fh : NULL;
	struct v4l2_ext_controls ctrls;
	struct v4l2_ext_control ctrl;

	if (vfh && vfh->ctrl_handler)
		return v4l2_s_ctrl(vfh, vfh->ctrl_handler, p);
	if (vfd->ctrl_handler)
		return v4l2_s_ctrl(NULL, vfd->ctrl_handler, p);
	if (ops->vidioc_s_ctrl)
		return ops->vidioc_s_ctrl(file, fh, p);
	if (ops->vidioc_s_ext_ctrls == NULL)
		return -ENOTTY;

	ctrls.which = V4L2_CTRL_ID2WHICH(p->id);
	ctrls.count = 1;
	ctrls.controls = &ctrl;
	ctrl.id = p->id;
	ctrl.value = p->value;
	if (check_ext_ctrls(&ctrls, 1))
		return ops->vidioc_s_ext_ctrls(file, fh, &ctrls);
	return -EINVAL;
}

static int v4l_g_ext_ctrls(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_ext_controls *p = arg;
	struct v4l2_fh *vfh =
		test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) ? fh : NULL;

	p->error_idx = p->count;
	if (vfh && vfh->ctrl_handler)
		return v4l2_g_ext_ctrls(vfh->ctrl_handler,
					vfd, vfd->v4l2_dev->mdev, p);
	if (vfd->ctrl_handler)
		return v4l2_g_ext_ctrls(vfd->ctrl_handler,
					vfd, vfd->v4l2_dev->mdev, p);
	if (ops->vidioc_g_ext_ctrls == NULL)
		return -ENOTTY;
	return check_ext_ctrls(p, 0) ? ops->vidioc_g_ext_ctrls(file, fh, p) :
					-EINVAL;
}

static int v4l_s_ext_ctrls(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_ext_controls *p = arg;
	struct v4l2_fh *vfh =
		test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) ? fh : NULL;

	p->error_idx = p->count;
	if (vfh && vfh->ctrl_handler)
		return v4l2_s_ext_ctrls(vfh, vfh->ctrl_handler,
					vfd, vfd->v4l2_dev->mdev, p);
	if (vfd->ctrl_handler)
		return v4l2_s_ext_ctrls(NULL, vfd->ctrl_handler,
					vfd, vfd->v4l2_dev->mdev, p);
	if (ops->vidioc_s_ext_ctrls == NULL)
		return -ENOTTY;
	return check_ext_ctrls(p, 0) ? ops->vidioc_s_ext_ctrls(file, fh, p) :
					-EINVAL;
}

static int v4l_try_ext_ctrls(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_ext_controls *p = arg;
	struct v4l2_fh *vfh =
		test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags) ? fh : NULL;

	p->error_idx = p->count;
	if (vfh && vfh->ctrl_handler)
		return v4l2_try_ext_ctrls(vfh->ctrl_handler,
					  vfd, vfd->v4l2_dev->mdev, p);
	if (vfd->ctrl_handler)
		return v4l2_try_ext_ctrls(vfd->ctrl_handler,
					  vfd, vfd->v4l2_dev->mdev, p);
	if (ops->vidioc_try_ext_ctrls == NULL)
		return -ENOTTY;
	return check_ext_ctrls(p, 0) ? ops->vidioc_try_ext_ctrls(file, fh, p) :
					-EINVAL;
}

/*
 * The selection API specified originally that the _MPLANE buffer types
 * shouldn't be used. The reasons for this are lost in the mists of time
 * (or just really crappy memories). Regardless, this is really annoying
 * for userspace. So to keep things simple we map _MPLANE buffer types
 * to their 'regular' counterparts before calling the driver. And we
 * restore it afterwards. This way applications can use either buffer
 * type and drivers don't need to check for both.
 */
static int v4l_g_selection(const struct v4l2_ioctl_ops *ops,
			   struct file *file, void *fh, void *arg)
{
	struct v4l2_selection *p = arg;
	u32 old_type = p->type;
	int ret;

	if (p->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		p->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		p->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ops->vidioc_g_selection(file, fh, p);
	p->type = old_type;
	return ret;
}

static int v4l_s_selection(const struct v4l2_ioctl_ops *ops,
			   struct file *file, void *fh, void *arg)
{
	struct v4l2_selection *p = arg;
	u32 old_type = p->type;
	int ret;

	if (p->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		p->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (p->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		p->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ops->vidioc_s_selection(file, fh, p);
	p->type = old_type;
	return ret;
}

static int v4l_g_crop(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_crop *p = arg;
	struct v4l2_selection s = {
		.type = p->type,
	};
	int ret;

	/* simulate capture crop using selection api */

	/* crop means compose for output devices */
	if (V4L2_TYPE_IS_OUTPUT(p->type))
		s.target = V4L2_SEL_TGT_COMPOSE;
	else
		s.target = V4L2_SEL_TGT_CROP;

	if (test_bit(V4L2_FL_QUIRK_INVERTED_CROP, &vfd->flags))
		s.target = s.target == V4L2_SEL_TGT_COMPOSE ?
			V4L2_SEL_TGT_CROP : V4L2_SEL_TGT_COMPOSE;

	ret = v4l_g_selection(ops, file, fh, &s);

	/* copying results to old structure on success */
	if (!ret)
		p->c = s.r;
	return ret;
}

static int v4l_s_crop(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_crop *p = arg;
	struct v4l2_selection s = {
		.type = p->type,
		.r = p->c,
	};

	/* simulate capture crop using selection api */

	/* crop means compose for output devices */
	if (V4L2_TYPE_IS_OUTPUT(p->type))
		s.target = V4L2_SEL_TGT_COMPOSE;
	else
		s.target = V4L2_SEL_TGT_CROP;

	if (test_bit(V4L2_FL_QUIRK_INVERTED_CROP, &vfd->flags))
		s.target = s.target == V4L2_SEL_TGT_COMPOSE ?
			V4L2_SEL_TGT_CROP : V4L2_SEL_TGT_COMPOSE;

	return v4l_s_selection(ops, file, fh, &s);
}

static int v4l_cropcap(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_cropcap *p = arg;
	struct v4l2_selection s = { .type = p->type };
	int ret = 0;

	/* setting trivial pixelaspect */
	p->pixelaspect.numerator = 1;
	p->pixelaspect.denominator = 1;

	if (s.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		s.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	else if (s.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		s.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	/*
	 * The determine_valid_ioctls() call already should ensure
	 * that this can never happen, but just in case...
	 */
	if (WARN_ON(!ops->vidioc_g_selection))
		return -ENOTTY;

	if (ops->vidioc_g_pixelaspect)
		ret = ops->vidioc_g_pixelaspect(file, fh, s.type,
						&p->pixelaspect);

	/*
	 * Ignore ENOTTY or ENOIOCTLCMD error returns, just use the
	 * square pixel aspect ratio in that case.
	 */
	if (ret && ret != -ENOTTY && ret != -ENOIOCTLCMD)
		return ret;

	/* Use g_selection() to fill in the bounds and defrect rectangles */

	/* obtaining bounds */
	if (V4L2_TYPE_IS_OUTPUT(p->type))
		s.target = V4L2_SEL_TGT_COMPOSE_BOUNDS;
	else
		s.target = V4L2_SEL_TGT_CROP_BOUNDS;

	if (test_bit(V4L2_FL_QUIRK_INVERTED_CROP, &vfd->flags))
		s.target = s.target == V4L2_SEL_TGT_COMPOSE_BOUNDS ?
			V4L2_SEL_TGT_CROP_BOUNDS : V4L2_SEL_TGT_COMPOSE_BOUNDS;

	ret = v4l_g_selection(ops, file, fh, &s);
	if (ret)
		return ret;
	p->bounds = s.r;

	/* obtaining defrect */
	if (s.target == V4L2_SEL_TGT_COMPOSE_BOUNDS)
		s.target = V4L2_SEL_TGT_COMPOSE_DEFAULT;
	else
		s.target = V4L2_SEL_TGT_CROP_DEFAULT;

	ret = v4l_g_selection(ops, file, fh, &s);
	if (ret)
		return ret;
	p->defrect = s.r;

	return 0;
}

static int v4l_log_status(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	int ret;

	if (vfd->v4l2_dev)
		pr_info("%s: =================  START STATUS  =================\n",
			vfd->v4l2_dev->name);
	ret = ops->vidioc_log_status(file, fh);
	if (vfd->v4l2_dev)
		pr_info("%s: ==================  END STATUS  ==================\n",
			vfd->v4l2_dev->name);
	return ret;
}

static int v4l_dbg_g_register(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
#ifdef CONFIG_VIDEO_ADV_DEBUG
	struct v4l2_dbg_register *p = arg;
	struct video_device *vfd = video_devdata(file);
	struct v4l2_subdev *sd;
	int idx = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (p->match.type == V4L2_CHIP_MATCH_SUBDEV) {
		if (vfd->v4l2_dev == NULL)
			return -EINVAL;
		v4l2_device_for_each_subdev(sd, vfd->v4l2_dev)
			if (p->match.addr == idx++)
				return v4l2_subdev_call(sd, core, g_register, p);
		return -EINVAL;
	}
	if (ops->vidioc_g_register && p->match.type == V4L2_CHIP_MATCH_BRIDGE &&
	    (ops->vidioc_g_chip_info || p->match.addr == 0))
		return ops->vidioc_g_register(file, fh, p);
	return -EINVAL;
#else
	return -ENOTTY;
#endif
}

static int v4l_dbg_s_register(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
#ifdef CONFIG_VIDEO_ADV_DEBUG
	const struct v4l2_dbg_register *p = arg;
	struct video_device *vfd = video_devdata(file);
	struct v4l2_subdev *sd;
	int idx = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (p->match.type == V4L2_CHIP_MATCH_SUBDEV) {
		if (vfd->v4l2_dev == NULL)
			return -EINVAL;
		v4l2_device_for_each_subdev(sd, vfd->v4l2_dev)
			if (p->match.addr == idx++)
				return v4l2_subdev_call(sd, core, s_register, p);
		return -EINVAL;
	}
	if (ops->vidioc_s_register && p->match.type == V4L2_CHIP_MATCH_BRIDGE &&
	    (ops->vidioc_g_chip_info || p->match.addr == 0))
		return ops->vidioc_s_register(file, fh, p);
	return -EINVAL;
#else
	return -ENOTTY;
#endif
}

static int v4l_dbg_g_chip_info(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
#ifdef CONFIG_VIDEO_ADV_DEBUG
	struct video_device *vfd = video_devdata(file);
	struct v4l2_dbg_chip_info *p = arg;
	struct v4l2_subdev *sd;
	int idx = 0;

	switch (p->match.type) {
	case V4L2_CHIP_MATCH_BRIDGE:
		if (ops->vidioc_s_register)
			p->flags |= V4L2_CHIP_FL_WRITABLE;
		if (ops->vidioc_g_register)
			p->flags |= V4L2_CHIP_FL_READABLE;
		strscpy(p->name, vfd->v4l2_dev->name, sizeof(p->name));
		if (ops->vidioc_g_chip_info)
			return ops->vidioc_g_chip_info(file, fh, arg);
		if (p->match.addr)
			return -EINVAL;
		return 0;

	case V4L2_CHIP_MATCH_SUBDEV:
		if (vfd->v4l2_dev == NULL)
			break;
		v4l2_device_for_each_subdev(sd, vfd->v4l2_dev) {
			if (p->match.addr != idx++)
				continue;
			if (sd->ops->core && sd->ops->core->s_register)
				p->flags |= V4L2_CHIP_FL_WRITABLE;
			if (sd->ops->core && sd->ops->core->g_register)
				p->flags |= V4L2_CHIP_FL_READABLE;
			strscpy(p->name, sd->name, sizeof(p->name));
			return 0;
		}
		break;
	}
	return -EINVAL;
#else
	return -ENOTTY;
#endif
}

static int v4l_dqevent(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	return v4l2_event_dequeue(fh, arg, file->f_flags & O_NONBLOCK);
}

static int v4l_subscribe_event(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	return ops->vidioc_subscribe_event(fh, arg);
}

static int v4l_unsubscribe_event(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	return ops->vidioc_unsubscribe_event(fh, arg);
}

static int v4l_g_sliced_vbi_cap(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct v4l2_sliced_vbi_cap *p = arg;
	int ret = check_fmt(file, p->type);

	if (ret)
		return ret;

	/* Clear up to type, everything after type is zeroed already */
	memset(p, 0, offsetof(struct v4l2_sliced_vbi_cap, type));

	return ops->vidioc_g_sliced_vbi_cap(file, fh, p);
}

static int v4l_enum_freq_bands(const struct v4l2_ioctl_ops *ops,
				struct file *file, void *fh, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_frequency_band *p = arg;
	enum v4l2_tuner_type type;
	int err;

	if (vfd->vfl_type == VFL_TYPE_SDR) {
		if (p->type != V4L2_TUNER_SDR && p->type != V4L2_TUNER_RF)
			return -EINVAL;
		type = p->type;
	} else {
		type = (vfd->vfl_type == VFL_TYPE_RADIO) ?
				V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
		if (type != p->type)
			return -EINVAL;
	}
	if (ops->vidioc_enum_freq_bands) {
		err = ops->vidioc_enum_freq_bands(file, fh, p);
		if (err != -ENOTTY)
			return err;
	}
	if (is_valid_ioctl(vfd, VIDIOC_G_TUNER)) {
		struct v4l2_tuner t = {
			.index = p->tuner,
			.type = type,
		};

		if (p->index)
			return -EINVAL;
		err = ops->vidioc_g_tuner(file, fh, &t);
		if (err)
			return err;
		p->capability = t.capability | V4L2_TUNER_CAP_FREQ_BANDS;
		p->rangelow = t.rangelow;
		p->rangehigh = t.rangehigh;
		p->modulation = (type == V4L2_TUNER_RADIO) ?
			V4L2_BAND_MODULATION_FM : V4L2_BAND_MODULATION_VSB;
		return 0;
	}
	if (is_valid_ioctl(vfd, VIDIOC_G_MODULATOR)) {
		struct v4l2_modulator m = {
			.index = p->tuner,
		};

		if (type != V4L2_TUNER_RADIO)
			return -EINVAL;
		if (p->index)
			return -EINVAL;
		err = ops->vidioc_g_modulator(file, fh, &m);
		if (err)
			return err;
		p->capability = m.capability | V4L2_TUNER_CAP_FREQ_BANDS;
		p->rangelow = m.rangelow;
		p->rangehigh = m.rangehigh;
		p->modulation = (type == V4L2_TUNER_RADIO) ?
			V4L2_BAND_MODULATION_FM : V4L2_BAND_MODULATION_VSB;
		return 0;
	}
	return -ENOTTY;
}

struct v4l2_ioctl_info {
	unsigned int ioctl;
	u32 flags;
	const char * const name;
	int (*func)(const struct v4l2_ioctl_ops *ops, struct file *file,
		    void *fh, void *p);
	void (*debug)(const void *arg, bool write_only);
};

/* This control needs a priority check */
#define INFO_FL_PRIO		(1 << 0)
/* This control can be valid if the filehandle passes a control handler. */
#define INFO_FL_CTRL		(1 << 1)
/* Queuing ioctl */
#define INFO_FL_QUEUE		(1 << 2)
/* Always copy back result, even on error */
#define INFO_FL_ALWAYS_COPY	(1 << 3)
/* Zero struct from after the field to the end */
#define INFO_FL_CLEAR(v4l2_struct, field)			\
	((offsetof(struct v4l2_struct, field) +			\
	  sizeof(((struct v4l2_struct *)0)->field)) << 16)
#define INFO_FL_CLEAR_MASK	(_IOC_SIZEMASK << 16)

#define DEFINE_V4L_STUB_FUNC(_vidioc)				\
	static int v4l_stub_ ## _vidioc(			\
			const struct v4l2_ioctl_ops *ops,	\
			struct file *file, void *fh, void *p)	\
	{							\
		return ops->vidioc_ ## _vidioc(file, fh, p);	\
	}

#define IOCTL_INFO(_ioctl, _func, _debug, _flags)		\
	[_IOC_NR(_ioctl)] = {					\
		.ioctl = _ioctl,				\
		.flags = _flags,				\
		.name = #_ioctl,				\
		.func = _func,					\
		.debug = _debug,				\
	}

DEFINE_V4L_STUB_FUNC(g_fbuf)
DEFINE_V4L_STUB_FUNC(s_fbuf)
DEFINE_V4L_STUB_FUNC(expbuf)
DEFINE_V4L_STUB_FUNC(g_std)
DEFINE_V4L_STUB_FUNC(g_audio)
DEFINE_V4L_STUB_FUNC(s_audio)
DEFINE_V4L_STUB_FUNC(g_input)
DEFINE_V4L_STUB_FUNC(g_edid)
DEFINE_V4L_STUB_FUNC(s_edid)
DEFINE_V4L_STUB_FUNC(g_output)
DEFINE_V4L_STUB_FUNC(g_audout)
DEFINE_V4L_STUB_FUNC(s_audout)
DEFINE_V4L_STUB_FUNC(g_jpegcomp)
DEFINE_V4L_STUB_FUNC(s_jpegcomp)
DEFINE_V4L_STUB_FUNC(enumaudio)
DEFINE_V4L_STUB_FUNC(enumaudout)
DEFINE_V4L_STUB_FUNC(enum_framesizes)
DEFINE_V4L_STUB_FUNC(enum_frameintervals)
DEFINE_V4L_STUB_FUNC(g_enc_index)
DEFINE_V4L_STUB_FUNC(encoder_cmd)
DEFINE_V4L_STUB_FUNC(try_encoder_cmd)
DEFINE_V4L_STUB_FUNC(decoder_cmd)
DEFINE_V4L_STUB_FUNC(try_decoder_cmd)
DEFINE_V4L_STUB_FUNC(s_dv_timings)
DEFINE_V4L_STUB_FUNC(g_dv_timings)
DEFINE_V4L_STUB_FUNC(enum_dv_timings)
DEFINE_V4L_STUB_FUNC(query_dv_timings)
DEFINE_V4L_STUB_FUNC(dv_timings_cap)

static const struct v4l2_ioctl_info v4l2_ioctls[] = {
	IOCTL_INFO(VIDIOC_QUERYCAP, v4l_querycap, v4l_print_querycap, 0),
	IOCTL_INFO(VIDIOC_ENUM_FMT, v4l_enum_fmt, v4l_print_fmtdesc, INFO_FL_CLEAR(v4l2_fmtdesc, type)),
	IOCTL_INFO(VIDIOC_G_FMT, v4l_g_fmt, v4l_print_format, 0),
	IOCTL_INFO(VIDIOC_S_FMT, v4l_s_fmt, v4l_print_format, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_REQBUFS, v4l_reqbufs, v4l_print_requestbuffers, INFO_FL_PRIO | INFO_FL_QUEUE),
	IOCTL_INFO(VIDIOC_QUERYBUF, v4l_querybuf, v4l_print_buffer, INFO_FL_QUEUE | INFO_FL_CLEAR(v4l2_buffer, length)),
	IOCTL_INFO(VIDIOC_G_FBUF, v4l_stub_g_fbuf, v4l_print_framebuffer, 0),
	IOCTL_INFO(VIDIOC_S_FBUF, v4l_stub_s_fbuf, v4l_print_framebuffer, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_OVERLAY, v4l_overlay, v4l_print_u32, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_QBUF, v4l_qbuf, v4l_print_buffer, INFO_FL_QUEUE),
	IOCTL_INFO(VIDIOC_EXPBUF, v4l_stub_expbuf, v4l_print_exportbuffer, INFO_FL_QUEUE | INFO_FL_CLEAR(v4l2_exportbuffer, flags)),
	IOCTL_INFO(VIDIOC_DQBUF, v4l_dqbuf, v4l_print_buffer, INFO_FL_QUEUE),
	IOCTL_INFO(VIDIOC_STREAMON, v4l_streamon, v4l_print_buftype, INFO_FL_PRIO | INFO_FL_QUEUE),
	IOCTL_INFO(VIDIOC_STREAMOFF, v4l_streamoff, v4l_print_buftype, INFO_FL_PRIO | INFO_FL_QUEUE),
	IOCTL_INFO(VIDIOC_G_PARM, v4l_g_parm, v4l_print_streamparm, INFO_FL_CLEAR(v4l2_streamparm, type)),
	IOCTL_INFO(VIDIOC_S_PARM, v4l_s_parm, v4l_print_streamparm, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_STD, v4l_stub_g_std, v4l_print_std, 0),
	IOCTL_INFO(VIDIOC_S_STD, v4l_s_std, v4l_print_std, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_ENUMSTD, v4l_enumstd, v4l_print_standard, INFO_FL_CLEAR(v4l2_standard, index)),
	IOCTL_INFO(VIDIOC_ENUMINPUT, v4l_enuminput, v4l_print_enuminput, INFO_FL_CLEAR(v4l2_input, index)),
	IOCTL_INFO(VIDIOC_G_CTRL, v4l_g_ctrl, v4l_print_control, INFO_FL_CTRL | INFO_FL_CLEAR(v4l2_control, id)),
	IOCTL_INFO(VIDIOC_S_CTRL, v4l_s_ctrl, v4l_print_control, INFO_FL_PRIO | INFO_FL_CTRL),
	IOCTL_INFO(VIDIOC_G_TUNER, v4l_g_tuner, v4l_print_tuner, INFO_FL_CLEAR(v4l2_tuner, index)),
	IOCTL_INFO(VIDIOC_S_TUNER, v4l_s_tuner, v4l_print_tuner, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_AUDIO, v4l_stub_g_audio, v4l_print_audio, 0),
	IOCTL_INFO(VIDIOC_S_AUDIO, v4l_stub_s_audio, v4l_print_audio, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_QUERYCTRL, v4l_queryctrl, v4l_print_queryctrl, INFO_FL_CTRL | INFO_FL_CLEAR(v4l2_queryctrl, id)),
	IOCTL_INFO(VIDIOC_QUERYMENU, v4l_querymenu, v4l_print_querymenu, INFO_FL_CTRL | INFO_FL_CLEAR(v4l2_querymenu, index)),
	IOCTL_INFO(VIDIOC_G_INPUT, v4l_stub_g_input, v4l_print_u32, 0),
	IOCTL_INFO(VIDIOC_S_INPUT, v4l_s_input, v4l_print_u32, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_EDID, v4l_stub_g_edid, v4l_print_edid, INFO_FL_ALWAYS_COPY),
	IOCTL_INFO(VIDIOC_S_EDID, v4l_stub_s_edid, v4l_print_edid, INFO_FL_PRIO | INFO_FL_ALWAYS_COPY),
	IOCTL_INFO(VIDIOC_G_OUTPUT, v4l_stub_g_output, v4l_print_u32, 0),
	IOCTL_INFO(VIDIOC_S_OUTPUT, v4l_s_output, v4l_print_u32, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_ENUMOUTPUT, v4l_enumoutput, v4l_print_enumoutput, INFO_FL_CLEAR(v4l2_output, index)),
	IOCTL_INFO(VIDIOC_G_AUDOUT, v4l_stub_g_audout, v4l_print_audioout, 0),
	IOCTL_INFO(VIDIOC_S_AUDOUT, v4l_stub_s_audout, v4l_print_audioout, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_MODULATOR, v4l_g_modulator, v4l_print_modulator, INFO_FL_CLEAR(v4l2_modulator, index)),
	IOCTL_INFO(VIDIOC_S_MODULATOR, v4l_s_modulator, v4l_print_modulator, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_FREQUENCY, v4l_g_frequency, v4l_print_frequency, INFO_FL_CLEAR(v4l2_frequency, tuner)),
	IOCTL_INFO(VIDIOC_S_FREQUENCY, v4l_s_frequency, v4l_print_frequency, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_CROPCAP, v4l_cropcap, v4l_print_cropcap, INFO_FL_CLEAR(v4l2_cropcap, type)),
	IOCTL_INFO(VIDIOC_G_CROP, v4l_g_crop, v4l_print_crop, INFO_FL_CLEAR(v4l2_crop, type)),
	IOCTL_INFO(VIDIOC_S_CROP, v4l_s_crop, v4l_print_crop, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_SELECTION, v4l_g_selection, v4l_print_selection, INFO_FL_CLEAR(v4l2_selection, r)),
	IOCTL_INFO(VIDIOC_S_SELECTION, v4l_s_selection, v4l_print_selection, INFO_FL_PRIO | INFO_FL_CLEAR(v4l2_selection, r)),
	IOCTL_INFO(VIDIOC_G_JPEGCOMP, v4l_stub_g_jpegcomp, v4l_print_jpegcompression, 0),
	IOCTL_INFO(VIDIOC_S_JPEGCOMP, v4l_stub_s_jpegcomp, v4l_print_jpegcompression, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_QUERYSTD, v4l_querystd, v4l_print_std, 0),
	IOCTL_INFO(VIDIOC_TRY_FMT, v4l_try_fmt, v4l_print_format, 0),
	IOCTL_INFO(VIDIOC_ENUMAUDIO, v4l_stub_enumaudio, v4l_print_audio, INFO_FL_CLEAR(v4l2_audio, index)),
	IOCTL_INFO(VIDIOC_ENUMAUDOUT, v4l_stub_enumaudout, v4l_print_audioout, INFO_FL_CLEAR(v4l2_audioout, index)),
	IOCTL_INFO(VIDIOC_G_PRIORITY, v4l_g_priority, v4l_print_u32, 0),
	IOCTL_INFO(VIDIOC_S_PRIORITY, v4l_s_priority, v4l_print_u32, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_G_SLICED_VBI_CAP, v4l_g_sliced_vbi_cap, v4l_print_sliced_vbi_cap, INFO_FL_CLEAR(v4l2_sliced_vbi_cap, type)),
	IOCTL_INFO(VIDIOC_LOG_STATUS, v4l_log_status, v4l_print_newline, 0),
	IOCTL_INFO(VIDIOC_G_EXT_CTRLS, v4l_g_ext_ctrls, v4l_print_ext_controls, INFO_FL_CTRL),
	IOCTL_INFO(VIDIOC_S_EXT_CTRLS, v4l_s_ext_ctrls, v4l_print_ext_controls, INFO_FL_PRIO | INFO_FL_CTRL),
	IOCTL_INFO(VIDIOC_TRY_EXT_CTRLS, v4l_try_ext_ctrls, v4l_print_ext_controls, INFO_FL_CTRL),
	IOCTL_INFO(VIDIOC_ENUM_FRAMESIZES, v4l_stub_enum_framesizes, v4l_print_frmsizeenum, INFO_FL_CLEAR(v4l2_frmsizeenum, pixel_format)),
	IOCTL_INFO(VIDIOC_ENUM_FRAMEINTERVALS, v4l_stub_enum_frameintervals, v4l_print_frmivalenum, INFO_FL_CLEAR(v4l2_frmivalenum, height)),
	IOCTL_INFO(VIDIOC_G_ENC_INDEX, v4l_stub_g_enc_index, v4l_print_enc_idx, 0),
	IOCTL_INFO(VIDIOC_ENCODER_CMD, v4l_stub_encoder_cmd, v4l_print_encoder_cmd, INFO_FL_PRIO | INFO_FL_CLEAR(v4l2_encoder_cmd, flags)),
	IOCTL_INFO(VIDIOC_TRY_ENCODER_CMD, v4l_stub_try_encoder_cmd, v4l_print_encoder_cmd, INFO_FL_CLEAR(v4l2_encoder_cmd, flags)),
	IOCTL_INFO(VIDIOC_DECODER_CMD, v4l_stub_decoder_cmd, v4l_print_decoder_cmd, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_TRY_DECODER_CMD, v4l_stub_try_decoder_cmd, v4l_print_decoder_cmd, 0),
	IOCTL_INFO(VIDIOC_DBG_S_REGISTER, v4l_dbg_s_register, v4l_print_dbg_register, 0),
	IOCTL_INFO(VIDIOC_DBG_G_REGISTER, v4l_dbg_g_register, v4l_print_dbg_register, 0),
	IOCTL_INFO(VIDIOC_S_HW_FREQ_SEEK, v4l_s_hw_freq_seek, v4l_print_hw_freq_seek, INFO_FL_PRIO),
	IOCTL_INFO(VIDIOC_S_DV_TIMINGS, v4l_stub_s_dv_timings, v4l_print_dv_timings, INFO_FL_PRIO | INFO_FL_CLEAR(v4l2_dv_timings, bt.flags)),
	IOCTL_INFO(VIDIOC_G_DV_TIMINGS, v4l_stub_g_dv_timings, v4l_print_dv_timings, 0),
	IOCTL_INFO(VIDIOC_DQEVENT, v4l_dqevent, v4l_print_event, 0),
	IOCTL_INFO(VIDIOC_SUBSCRIBE_EVENT, v4l_subscribe_event, v4l_print_event_subscription, 0),
	IOCTL_INFO(VIDIOC_UNSUBSCRIBE_EVENT, v4l_unsubscribe_event, v4l_print_event_subscription, 0),
	IOCTL_INFO(VIDIOC_CREATE_BUFS, v4l_create_bufs, v4l_print_create_buffers, INFO_FL_PRIO | INFO_FL_QUEUE),
	IOCTL_INFO(VIDIOC_PREPARE_BUF, v4l_prepare_buf, v4l_print_buffer, INFO_FL_QUEUE),
	IOCTL_INFO(VIDIOC_ENUM_DV_TIMINGS, v4l_stub_enum_dv_timings, v4l_print_enum_dv_timings, INFO_FL_CLEAR(v4l2_enum_dv_timings, pad)),
	IOCTL_INFO(VIDIOC_QUERY_DV_TIMINGS, v4l_stub_query_dv_timings, v4l_print_dv_timings, INFO_FL_ALWAYS_COPY),
	IOCTL_INFO(VIDIOC_DV_TIMINGS_CAP, v4l_stub_dv_timings_cap, v4l_print_dv_timings_cap, INFO_FL_CLEAR(v4l2_dv_timings_cap, pad)),
	IOCTL_INFO(VIDIOC_ENUM_FREQ_BANDS, v4l_enum_freq_bands, v4l_print_freq_band, 0),
	IOCTL_INFO(VIDIOC_DBG_G_CHIP_INFO, v4l_dbg_g_chip_info, v4l_print_dbg_chip_info, INFO_FL_CLEAR(v4l2_dbg_chip_info, match)),
	IOCTL_INFO(VIDIOC_QUERY_EXT_CTRL, v4l_query_ext_ctrl, v4l_print_query_ext_ctrl, INFO_FL_CTRL | INFO_FL_CLEAR(v4l2_query_ext_ctrl, id)),
};
#define V4L2_IOCTLS ARRAY_SIZE(v4l2_ioctls)

static bool v4l2_is_known_ioctl(unsigned int cmd)
{
	if (_IOC_NR(cmd) >= V4L2_IOCTLS)
		return false;
	return v4l2_ioctls[_IOC_NR(cmd)].ioctl == cmd;
}

static struct mutex *v4l2_ioctl_get_lock(struct video_device *vdev,
					 struct v4l2_fh *vfh, unsigned int cmd,
					 void *arg)
{
	if (_IOC_NR(cmd) >= V4L2_IOCTLS)
		return vdev->lock;
#if IS_ENABLED(CONFIG_V4L2_MEM2MEM_DEV)
	if (vfh && vfh->m2m_ctx &&
	    (v4l2_ioctls[_IOC_NR(cmd)].flags & INFO_FL_QUEUE)) {
		if (vfh->m2m_ctx->q_lock)
			return vfh->m2m_ctx->q_lock;
	}
#endif
	if (vdev->queue && vdev->queue->lock &&
			(v4l2_ioctls[_IOC_NR(cmd)].flags & INFO_FL_QUEUE))
		return vdev->queue->lock;
	return vdev->lock;
}

/* Common ioctl debug function. This function can be used by
   external ioctl messages as well as internal V4L ioctl */
void v4l_printk_ioctl(const char *prefix, unsigned int cmd)
{
	const char *dir, *type;

	if (prefix)
		printk(KERN_DEBUG "%s: ", prefix);

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
	struct mutex *req_queue_lock = NULL;
	struct mutex *lock; /* ioctl serialization mutex */
	const struct v4l2_ioctl_ops *ops = vfd->ioctl_ops;
	bool write_only = false;
	struct v4l2_ioctl_info default_info;
	const struct v4l2_ioctl_info *info;
	void *fh = file->private_data;
	struct v4l2_fh *vfh = NULL;
	int dev_debug = vfd->dev_debug;
	long ret = -ENOTTY;

	if (ops == NULL) {
		pr_warn("%s: has no ioctl_ops.\n",
				video_device_node_name(vfd));
		return ret;
	}

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags))
		vfh = file->private_data;

	/*
	 * We need to serialize streamon/off with queueing new requests.
	 * These ioctls may trigger the cancellation of a streaming
	 * operation, and that should not be mixed with queueing a new
	 * request at the same time.
	 */
	if (v4l2_device_supports_requests(vfd->v4l2_dev) &&
	    (cmd == VIDIOC_STREAMON || cmd == VIDIOC_STREAMOFF)) {
		req_queue_lock = &vfd->v4l2_dev->mdev->req_queue_mutex;

		if (mutex_lock_interruptible(req_queue_lock))
			return -ERESTARTSYS;
	}

	lock = v4l2_ioctl_get_lock(vfd, vfh, cmd, arg);

	if (lock && mutex_lock_interruptible(lock)) {
		if (req_queue_lock)
			mutex_unlock(req_queue_lock);
		return -ERESTARTSYS;
	}

	if (!video_is_registered(vfd)) {
		ret = -ENODEV;
		goto unlock;
	}

	if (v4l2_is_known_ioctl(cmd)) {
		info = &v4l2_ioctls[_IOC_NR(cmd)];

		if (!test_bit(_IOC_NR(cmd), vfd->valid_ioctls) &&
		    !((info->flags & INFO_FL_CTRL) && vfh && vfh->ctrl_handler))
			goto done;

		if (vfh && (info->flags & INFO_FL_PRIO)) {
			ret = v4l2_prio_check(vfd->prio, vfh->prio);
			if (ret)
				goto done;
		}
	} else {
		default_info.ioctl = cmd;
		default_info.flags = 0;
		default_info.debug = v4l_print_default;
		info = &default_info;
	}

	write_only = _IOC_DIR(cmd) == _IOC_WRITE;
	if (info != &default_info) {
		ret = info->func(ops, file, fh, arg);
	} else if (!ops->vidioc_default) {
		ret = -ENOTTY;
	} else {
		ret = ops->vidioc_default(file, fh,
			vfh ? v4l2_prio_check(vfd->prio, vfh->prio) >= 0 : 0,
			cmd, arg);
	}

done:
	if (dev_debug & (V4L2_DEV_DEBUG_IOCTL | V4L2_DEV_DEBUG_IOCTL_ARG)) {
		if (!(dev_debug & V4L2_DEV_DEBUG_STREAMING) &&
		    (cmd == VIDIOC_QBUF || cmd == VIDIOC_DQBUF))
			goto unlock;

		v4l_printk_ioctl(video_device_node_name(vfd), cmd);
		if (ret < 0)
			pr_cont(": error %ld", ret);
		if (!(dev_debug & V4L2_DEV_DEBUG_IOCTL_ARG))
			pr_cont("\n");
		else if (_IOC_DIR(cmd) == _IOC_NONE)
			info->debug(arg, write_only);
		else {
			pr_cont(": ");
			info->debug(arg, write_only);
		}
	}

unlock:
	if (lock)
		mutex_unlock(lock);
	if (req_queue_lock)
		mutex_unlock(req_queue_lock);
	return ret;
}

static int check_array_args(unsigned int cmd, void *parg, size_t *array_size,
			    void __user **user_ptr, void ***kernel_ptr)
{
	int ret = 0;

	switch (cmd) {
	case VIDIOC_PREPARE_BUF:
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
			*kernel_ptr = (void **)&buf->m.planes;
			*array_size = sizeof(struct v4l2_plane) * buf->length;
			ret = 1;
		}
		break;
	}

	case VIDIOC_G_EDID:
	case VIDIOC_S_EDID: {
		struct v4l2_edid *edid = parg;

		if (edid->blocks) {
			if (edid->blocks > 256) {
				ret = -EINVAL;
				break;
			}
			*user_ptr = (void __user *)edid->edid;
			*kernel_ptr = (void **)&edid->edid;
			*array_size = edid->blocks * 128;
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
			*kernel_ptr = (void **)&ctrls->controls;
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
	bool	always_copy = false;
	size_t  array_size = 0;
	void __user *user_ptr = NULL;
	void	**kernel_ptr = NULL;
	const size_t ioc_size = _IOC_SIZE(cmd);

	/*  Copy arguments into temp kernel buffer  */
	if (_IOC_DIR(cmd) != _IOC_NONE) {
		if (ioc_size <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kvmalloc(ioc_size, GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (_IOC_DIR(cmd) & _IOC_WRITE) {
			unsigned int n = ioc_size;

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
				always_copy = flags & INFO_FL_ALWAYS_COPY;
			}

			if (copy_from_user(parg, (void __user *)arg, n))
				goto out;

			/* zero out anything we don't copy from userspace */
			if (n < ioc_size)
				memset((u8 *)parg + n, 0, ioc_size - n);
		} else {
			/* read-only ioctl */
			memset(parg, 0, ioc_size);
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
		mbuf = kvmalloc(array_size, GFP_KERNEL);
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
	if (err == -ENOTTY || err == -ENOIOCTLCMD) {
		err = -ENOTTY;
		goto out;
	}

	if (err == 0) {
		if (cmd == VIDIOC_DQBUF)
			trace_v4l2_dqbuf(video_devdata(file)->minor, parg);
		else if (cmd == VIDIOC_QBUF)
			trace_v4l2_qbuf(video_devdata(file)->minor, parg);
	}

	if (has_array_args) {
		*kernel_ptr = (void __force *)user_ptr;
		if (copy_to_user(user_ptr, mbuf, array_size))
			err = -EFAULT;
		goto out_array_args;
	}
	/*
	 * Some ioctls can return an error, but still have valid
	 * results that must be returned.
	 */
	if (err < 0 && !always_copy)
		goto out;

out_array_args:
	/*  Copy results into user buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_READ:
	case (_IOC_WRITE | _IOC_READ):
		if (copy_to_user((void __user *)arg, parg, ioc_size))
			err = -EFAULT;
		break;
	}

out:
	kvfree(mbuf);
	return err;
}

long video_ioctl2(struct file *file,
	       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(file, cmd, arg, __video_do_ioctl);
}
EXPORT_SYMBOL(video_ioctl2);
