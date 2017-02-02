/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Hugues Fruchet <hugues.fruchet@st.com>
 *          Fabrice Lecoultre <fabrice.lecoultre@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include "delta.h"
#include "delta-debug.h"

char *delta_streaminfo_str(struct delta_streaminfo *s, char *str,
			   unsigned int len)
{
	if (!s)
		return NULL;

	snprintf(str, len,
		 "%4.4s %dx%d %s %s dpb=%d %s %s %s%dx%d@(%d,%d) %s%d/%d",
		 (char *)&s->streamformat, s->width, s->height,
		 s->profile, s->level, s->dpb,
		 (s->field == V4L2_FIELD_NONE) ? "progressive" : "interlaced",
		 s->other,
		 s->flags & DELTA_STREAMINFO_FLAG_CROP ? "crop=" : "",
		 s->crop.width, s->crop.height,
		 s->crop.left, s->crop.top,
		 s->flags & DELTA_STREAMINFO_FLAG_PIXELASPECT ? "par=" : "",
		 s->pixelaspect.numerator,
		 s->pixelaspect.denominator);

	return str;
}

char *delta_frameinfo_str(struct delta_frameinfo *f, char *str,
			  unsigned int len)
{
	if (!f)
		return NULL;

	snprintf(str, len,
		 "%4.4s %dx%d aligned %dx%d %s %s%dx%d@(%d,%d) %s%d/%d",
		 (char *)&f->pixelformat, f->width, f->height,
		 f->aligned_width, f->aligned_height,
		 (f->field == V4L2_FIELD_NONE) ? "progressive" : "interlaced",
		 f->flags & DELTA_STREAMINFO_FLAG_CROP ? "crop=" : "",
		 f->crop.width, f->crop.height,
		 f->crop.left, f->crop.top,
		 f->flags & DELTA_STREAMINFO_FLAG_PIXELASPECT ? "par=" : "",
		 f->pixelaspect.numerator,
		 f->pixelaspect.denominator);

	return str;
}

void delta_trace_summary(struct delta_ctx *ctx)
{
	struct delta_dev *delta = ctx->dev;
	struct delta_streaminfo *s = &ctx->streaminfo;
	unsigned char str[100] = "";

	if (!(ctx->flags & DELTA_FLAG_STREAMINFO))
		return;

	dev_dbg(delta->dev, "%s %s, %d frames decoded, %d frames output, %d frames dropped, %d stream errors, %d decode errors",
		ctx->name,
		delta_streaminfo_str(s, str, sizeof(str)),
		ctx->decoded_frames,
		ctx->output_frames,
		ctx->dropped_frames,
		ctx->stream_errors,
		ctx->decode_errors);
}
