// SPDX-License-Identifier: GPL-2.0
/*
 * Vidtv serves as a reference DVB driver and helps validate the existing APIs
 * in the media subsystem. It can also aid developers working on userspace
 * applications.
 *
 * This file contains the code for an AES3 (also known as AES/EBU) encoder.
 * It is based on EBU Tech 3250 and SMPTE 302M technical documents.
 *
 * This encoder currently supports 16bit AES3 subframes using 16bit signed
 * integers.
 *
 * Note: AU stands for Access Unit, and AAU stands for Audio Access Unit
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s, %d: " fmt, __func__, __LINE__

#include <linux/bug.h>
#include <linux/crc32.h>
#include <linux/fixp-arith.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include "vidtv_common.h"
#include "vidtv_encoder.h"
#include "vidtv_s302m.h"

#define S302M_SAMPLING_RATE_HZ 48000
#define PES_PRIVATE_STREAM_1 0xbd  /* PES: private_stream_1 */
#define S302M_BLOCK_SZ 192
#define S302M_SIN_LUT_NUM_ELEM 1024

/* these are retrieved empirically from ffmpeg/libavcodec */
#define FF_S302M_DEFAULT_NUM_FRAMES 1115
#define FF_S302M_DEFAULT_PTS_INCREMENT 2090
#define FF_S302M_DEFAULT_PTS_OFFSET 100000

/* Used by the tone generator: number of samples for PI */
#define PI		180

static const u8 reverse[256] = {
	/* from ffmpeg */
	0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0,
	0x30, 0xB0, 0x70, 0xF0, 0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
	0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 0x04, 0x84, 0x44, 0xC4,
	0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC,
	0x3C, 0xBC, 0x7C, 0xFC, 0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
	0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 0x0A, 0x8A, 0x4A, 0xCA,
	0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6,
	0x36, 0xB6, 0x76, 0xF6, 0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
	0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE, 0x01, 0x81, 0x41, 0xC1,
	0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
	0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9,
	0x39, 0xB9, 0x79, 0xF9, 0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
	0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5, 0x0D, 0x8D, 0x4D, 0xCD,
	0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
	0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3,
	0x33, 0xB3, 0x73, 0xF3, 0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
	0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB, 0x07, 0x87, 0x47, 0xC7,
	0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
	0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF,
	0x3F, 0xBF, 0x7F, 0xFF,
};

struct tone_duration {
	enum musical_notes note;
	int duration;
};

#define COMPASS 100 /* beats per minute */
static const struct tone_duration beethoven_fur_elise[] = {
	{ NOTE_SILENT, 512},
	{ NOTE_E_6, 128},  { NOTE_DS_6, 128}, { NOTE_E_6, 128},
	{ NOTE_DS_6, 128}, { NOTE_E_6, 128},  { NOTE_B_5, 128},
	{ NOTE_D_6, 128},  { NOTE_C_6, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_C_5, 128},
	{ NOTE_E_5, 128},  { NOTE_A_5, 128},  { NOTE_E_3, 128},
	{ NOTE_E_4, 128},  { NOTE_GS_4, 128}, { NOTE_E_5, 128},
	{ NOTE_GS_5, 128}, { NOTE_B_5, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_E_5, 128},
	{ NOTE_E_6, 128},  { NOTE_DS_6, 128}, { NOTE_E_6, 128},
	{ NOTE_DS_6, 128}, { NOTE_E_6, 128},  { NOTE_B_5, 128},
	{ NOTE_D_6, 128},  { NOTE_C_6, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_C_5, 128},
	{ NOTE_E_5, 128},  { NOTE_A_5, 128},  { NOTE_E_3, 128},
	{ NOTE_E_4, 128},  { NOTE_GS_4, 128}, { NOTE_E_5, 128},
	{ NOTE_C_6, 128},  { NOTE_B_5, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_SILENT, 128},

	{ NOTE_E_6, 128},  { NOTE_DS_6, 128}, { NOTE_E_6, 128},
	{ NOTE_DS_6, 128}, { NOTE_E_6, 128},  { NOTE_B_5, 128},
	{ NOTE_D_6, 128},  { NOTE_C_6, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_C_5, 128},
	{ NOTE_E_5, 128},  { NOTE_A_5, 128},  { NOTE_E_3, 128},
	{ NOTE_E_4, 128},  { NOTE_GS_4, 128}, { NOTE_E_5, 128},
	{ NOTE_GS_5, 128}, { NOTE_B_5, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_E_5, 128},
	{ NOTE_E_6, 128},  { NOTE_DS_6, 128}, { NOTE_E_6, 128},
	{ NOTE_DS_6, 128}, { NOTE_E_6, 128},  { NOTE_B_5, 128},
	{ NOTE_D_6, 128},  { NOTE_C_6, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_C_5, 128},
	{ NOTE_E_5, 128},  { NOTE_A_5, 128},  { NOTE_E_3, 128},
	{ NOTE_E_4, 128},  { NOTE_GS_4, 128}, { NOTE_E_5, 128},
	{ NOTE_C_6, 128},  { NOTE_B_5, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_B_4, 128},
	{ NOTE_C_5, 128},  { NOTE_D_5, 128},  { NOTE_C_4, 128},
	{ NOTE_G_4, 128},  { NOTE_C_5, 128},  { NOTE_G_4, 128},
	{ NOTE_F_5, 128},  { NOTE_E_5, 128},  { NOTE_G_3, 128},
	{ NOTE_G_4, 128},  { NOTE_B_3, 128},  { NOTE_F_4, 128},
	{ NOTE_E_5, 128},  { NOTE_D_5, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_E_4, 128},
	{ NOTE_D_5, 128},  { NOTE_C_5, 128},  { NOTE_E_3, 128},
	{ NOTE_E_4, 128},  { NOTE_E_5, 128},  { NOTE_E_5, 128},
	{ NOTE_E_6, 128},  { NOTE_E_5, 128},  { NOTE_E_6, 128},
	{ NOTE_E_5, 128},  { NOTE_E_5, 128},  { NOTE_DS_5, 128},
	{ NOTE_E_5, 128},  { NOTE_DS_6, 128}, { NOTE_E_6, 128},
	{ NOTE_DS_5, 128}, { NOTE_E_5, 128},  { NOTE_DS_6, 128},
	{ NOTE_E_6, 128},  { NOTE_DS_6, 128}, { NOTE_E_6, 128},
	{ NOTE_DS_6, 128}, { NOTE_E_6, 128},  { NOTE_B_5, 128},
	{ NOTE_D_6, 128},  { NOTE_C_6, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_C_5, 128},
	{ NOTE_E_5, 128},  { NOTE_A_5, 128},  { NOTE_E_3, 128},
	{ NOTE_E_4, 128},  { NOTE_GS_4, 128}, { NOTE_E_5, 128},
	{ NOTE_GS_5, 128}, { NOTE_B_5, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_E_5, 128},
	{ NOTE_E_6, 128},  { NOTE_DS_6, 128}, { NOTE_E_6, 128},
	{ NOTE_DS_6, 128}, { NOTE_E_6, 128},  { NOTE_B_5, 128},
	{ NOTE_D_6, 128},  { NOTE_C_6, 128},  { NOTE_A_3, 128},
	{ NOTE_E_4, 128},  { NOTE_A_4, 128},  { NOTE_C_5, 128},
	{ NOTE_E_5, 128},  { NOTE_A_5, 128},  { NOTE_E_3, 128},
	{ NOTE_E_4, 128},  { NOTE_GS_4, 128}, { NOTE_E_5, 128},
	{ NOTE_C_6, 128},  { NOTE_B_5, 128},  { NOTE_A_5, 512},
	{ NOTE_SILENT, 256},
};

static struct vidtv_access_unit *vidtv_s302m_access_unit_init(struct vidtv_access_unit *head)
{
	struct vidtv_access_unit *au;

	au = kzalloc(sizeof(*au), GFP_KERNEL);
	if (!au)
		return NULL;

	if (head) {
		while (head->next)
			head = head->next;

		head->next = au;
	}

	return au;
}

static void vidtv_s302m_access_unit_destroy(struct vidtv_encoder *e)
{
	struct vidtv_access_unit *head = e->access_units;
	struct vidtv_access_unit *tmp = NULL;

	while (head) {
		tmp = head;
		head = head->next;
		kfree(tmp);
	}

	e->access_units = NULL;
}

static void vidtv_s302m_alloc_au(struct vidtv_encoder *e)
{
	struct vidtv_access_unit *sync_au = NULL;
	struct vidtv_access_unit *temp = NULL;

	if (e->sync && e->sync->is_video_encoder) {
		sync_au = e->sync->access_units;

		while (sync_au) {
			temp = vidtv_s302m_access_unit_init(e->access_units);
			if (!e->access_units)
				e->access_units = temp;

			sync_au = sync_au->next;
		}

		return;
	}

	e->access_units = vidtv_s302m_access_unit_init(NULL);
}

static void
vidtv_s302m_compute_sample_count_from_video(struct vidtv_encoder *e)
{
	struct vidtv_access_unit *sync_au = e->sync->access_units;
	struct vidtv_access_unit *au = e->access_units;
	u32 sample_duration_usecs;
	u32 vau_duration_usecs;
	u32 s;

	vau_duration_usecs    = USEC_PER_SEC / e->sync->sampling_rate_hz;
	sample_duration_usecs = USEC_PER_SEC / e->sampling_rate_hz;

	while (au && sync_au) {
		s = DIV_ROUND_UP(vau_duration_usecs, sample_duration_usecs);
		au->num_samples = s;
		au = au->next;
		sync_au = sync_au->next;
	}
}

static void vidtv_s302m_compute_pts_from_video(struct vidtv_encoder *e)
{
	struct vidtv_access_unit *au = e->access_units;
	struct vidtv_access_unit *sync_au = e->sync->access_units;

	/* use the same pts from the video access unit*/
	while (au && sync_au) {
		au->pts = sync_au->pts;
		au = au->next;
		sync_au = sync_au->next;
	}
}

static u16 vidtv_s302m_get_sample(struct vidtv_encoder *e)
{
	u16 sample;
	int pos;
	struct vidtv_s302m_ctx *ctx = e->ctx;

	if (!e->src_buf) {
		/*
		 * Simple tone generator: play the tones at the
		 * beethoven_fur_elise array.
		 */
		if (ctx->last_duration <= 0) {
			if (e->src_buf_offset >= ARRAY_SIZE(beethoven_fur_elise))
				e->src_buf_offset = 0;

			ctx->last_tone = beethoven_fur_elise[e->src_buf_offset].note;
			ctx->last_duration = beethoven_fur_elise[e->src_buf_offset].duration *
					     S302M_SAMPLING_RATE_HZ / COMPASS / 5;
			e->src_buf_offset++;
			ctx->note_offset = 0;
		} else {
			ctx->last_duration--;
		}

		/* Handle pause notes */
		if (!ctx->last_tone)
			return 0x8000;

		pos = (2 * PI * ctx->note_offset * ctx->last_tone) / S302M_SAMPLING_RATE_HZ;
		ctx->note_offset++;

		return (fixp_sin32(pos % (2 * PI)) >> 16) + 0x8000;
	}

	/* bug somewhere */
	if (e->src_buf_offset > e->src_buf_sz) {
		pr_err_ratelimited("overflow detected: %d > %d, wrapping.\n",
				   e->src_buf_offset,
				   e->src_buf_sz);

		e->src_buf_offset = 0;
	}

	if (e->src_buf_offset >= e->src_buf_sz) {
		/* let the source know we are out of data */
		if (e->last_sample_cb)
			e->last_sample_cb(e->sample_count);

		e->src_buf_offset = 0;
	}

	sample = *(u16 *)(e->src_buf + e->src_buf_offset);

	return sample;
}

static u32 vidtv_s302m_write_frame(struct vidtv_encoder *e,
				   u16 sample)
{
	struct vidtv_s302m_ctx *ctx = e->ctx;
	struct vidtv_s302m_frame_16 f = {};
	u32 nbytes = 0;

	/* from ffmpeg: see s302enc.c */

	u8 vucf = ctx->frame_index == 0 ? 0x10 : 0;

	f.data[0] = sample & 0xFF;
	f.data[1] = (sample & 0xFF00) >>  8;
	f.data[2] = ((sample & 0x0F)  <<  4) | vucf;
	f.data[3] = (sample & 0x0FF0) >>  4;
	f.data[4] = (sample & 0xF000) >> 12;

	f.data[0] = reverse[f.data[0]];
	f.data[1] = reverse[f.data[1]];
	f.data[2] = reverse[f.data[2]];
	f.data[3] = reverse[f.data[3]];
	f.data[4] = reverse[f.data[4]];

	nbytes += vidtv_memcpy(e->encoder_buf,
			       e->encoder_buf_offset,
			       VIDTV_S302M_BUF_SZ,
			       &f,
			       sizeof(f));

	e->encoder_buf_offset += nbytes;

	ctx->frame_index++;
	if (ctx->frame_index >= S302M_BLOCK_SZ)
		ctx->frame_index = 0;

	return nbytes;
}

static u32 vidtv_s302m_write_h(struct vidtv_encoder *e, u32 p_sz)
{
	struct vidtv_smpte_s302m_es h = {};
	u32 nbytes = 0;

	/* 2 channels, ident: 0, 16 bits per sample */
	h.bitfield = cpu_to_be32((p_sz << 16));

	nbytes += vidtv_memcpy(e->encoder_buf,
			       e->encoder_buf_offset,
			       e->encoder_buf_sz,
			       &h,
			       sizeof(h));

	e->encoder_buf_offset += nbytes;
	return nbytes;
}

static void vidtv_s302m_write_frames(struct vidtv_encoder *e)
{
	struct vidtv_access_unit *au = e->access_units;
	struct vidtv_s302m_ctx *ctx = e->ctx;
	u32 nbytes_per_unit = 0;
	u32 nbytes = 0;
	u32 au_sz = 0;
	u16 sample;
	u32 j;

	while (au) {
		au_sz = au->num_samples *
			sizeof(struct vidtv_s302m_frame_16);

		nbytes_per_unit = vidtv_s302m_write_h(e, au_sz);

		for (j = 0; j < au->num_samples; ++j) {
			sample = vidtv_s302m_get_sample(e);
			nbytes_per_unit += vidtv_s302m_write_frame(e, sample);

			if (e->src_buf)
				e->src_buf_offset += sizeof(u16);

			e->sample_count++;
		}

		au->nbytes = nbytes_per_unit;

		if (au_sz + sizeof(struct vidtv_smpte_s302m_es) != nbytes_per_unit) {
			pr_warn_ratelimited("write size was %u, expected %zu\n",
					    nbytes_per_unit,
					    au_sz + sizeof(struct vidtv_smpte_s302m_es));
		}

		nbytes += nbytes_per_unit;
		au->offset = nbytes - nbytes_per_unit;

		nbytes_per_unit = 0;
		ctx->au_count++;

		au = au->next;
	}
}

static void *vidtv_s302m_encode(struct vidtv_encoder *e)
{
	struct vidtv_s302m_ctx *ctx = e->ctx;

	/*
	 * According to SMPTE 302M, an audio access unit is specified as those
	 * AES3 words that are associated with a corresponding video frame.
	 * Therefore, there is one audio access unit for every video access unit
	 * in the corresponding video encoder ('sync'), using the same values
	 * for PTS as used by the video encoder.
	 *
	 * Assuming that it is also possible to send audio without any
	 * associated video, as in a radio-like service, a single audio access unit
	 * is created with values for 'num_samples' and 'pts' taken empirically from
	 * ffmpeg
	 */

	vidtv_s302m_access_unit_destroy(e);
	vidtv_s302m_alloc_au(e);

	if (e->sync && e->sync->is_video_encoder) {
		vidtv_s302m_compute_sample_count_from_video(e);
		vidtv_s302m_compute_pts_from_video(e);
	} else {
		e->access_units->num_samples = FF_S302M_DEFAULT_NUM_FRAMES;
		e->access_units->pts = (ctx->au_count * FF_S302M_DEFAULT_PTS_INCREMENT) +
				       FF_S302M_DEFAULT_PTS_OFFSET;
	}

	vidtv_s302m_write_frames(e);

	return e->encoder_buf;
}

static u32 vidtv_s302m_clear(struct vidtv_encoder *e)
{
	struct vidtv_access_unit *au = e->access_units;
	u32 count = 0;

	while (au) {
		count++;
		au = au->next;
	}

	vidtv_s302m_access_unit_destroy(e);
	memset(e->encoder_buf, 0, VIDTV_S302M_BUF_SZ);
	e->encoder_buf_offset = 0;

	return count;
}

struct vidtv_encoder
*vidtv_s302m_encoder_init(struct vidtv_s302m_encoder_init_args args)
{
	u32 priv_sz = sizeof(struct vidtv_s302m_ctx);
	struct vidtv_s302m_ctx *ctx;
	struct vidtv_encoder *e;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return NULL;

	e->id = S302M;

	if (args.name)
		e->name = kstrdup(args.name, GFP_KERNEL);

	e->encoder_buf = vzalloc(VIDTV_S302M_BUF_SZ);
	e->encoder_buf_sz = VIDTV_S302M_BUF_SZ;
	e->encoder_buf_offset = 0;

	e->sample_count = 0;

	e->src_buf = (args.src_buf) ? args.src_buf : NULL;
	e->src_buf_sz = (args.src_buf) ? args.src_buf_sz : 0;
	e->src_buf_offset = 0;

	e->is_video_encoder = false;

	ctx = kzalloc(priv_sz, GFP_KERNEL);
	if (!ctx) {
		kfree(e);
		return NULL;
	}

	e->ctx = ctx;
	ctx->last_duration = 0;

	e->encode = vidtv_s302m_encode;
	e->clear = vidtv_s302m_clear;

	e->es_pid = cpu_to_be16(args.es_pid);
	e->stream_id = cpu_to_be16(PES_PRIVATE_STREAM_1);

	e->sync = args.sync;
	e->sampling_rate_hz = S302M_SAMPLING_RATE_HZ;

	e->last_sample_cb = args.last_sample_cb;

	e->destroy = vidtv_s302m_encoder_destroy;

	if (args.head) {
		while (args.head->next)
			args.head = args.head->next;

		args.head->next = e;
	}

	e->next = NULL;

	return e;
}

void vidtv_s302m_encoder_destroy(struct vidtv_encoder *e)
{
	if (e->id != S302M) {
		pr_err_ratelimited("Encoder type mismatch, skipping.\n");
		return;
	}

	vidtv_s302m_access_unit_destroy(e);
	kfree(e->name);
	vfree(e->encoder_buf);
	kfree(e->ctx);
	kfree(e);
}
