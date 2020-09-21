// SPDX-License-Identifier: GPL-2.0
/*
 * Vidtv serves as a reference DVB driver and helps validate the existing APIs
 * in the media subsystem. It can also aid developers working on userspace
 * applications.
 *
 * This file contains the logic to translate the ES data for one access unit
 * from an encoder into MPEG TS packets. It does so by first encapsulating it
 * with a PES header and then splitting it into TS packets.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s, %d: " fmt, __func__, __LINE__

#include <linux/types.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <asm/byteorder.h>

#include "vidtv_pes.h"
#include "vidtv_common.h"
#include "vidtv_encoder.h"
#include "vidtv_ts.h"

#define PRIVATE_STREAM_1_ID 0xbd /* private_stream_1. See SMPTE 302M-2007 p.6 */
#define PES_HEADER_MAX_STUFFING_BYTES 32
#define PES_TS_HEADER_MAX_STUFFING_BYTES 182

static u32 vidtv_pes_op_get_len(bool send_pts, bool send_dts)
{
	u32 len = 0;

	/* the flags must always be sent */
	len += sizeof(struct vidtv_pes_optional);

	/* From all optionals, we might send these for now */
	if (send_pts && send_dts)
		len += sizeof(struct vidtv_pes_optional_pts_dts);
	else if (send_pts)
		len += sizeof(struct vidtv_pes_optional_pts);

	return len;
}

static u32 vidtv_pes_h_get_len(bool send_pts, bool send_dts)
{
	/* PES header length notwithstanding stuffing bytes */
	u32 len = 0;

	len += sizeof(struct vidtv_mpeg_pes);
	len += vidtv_pes_op_get_len(send_pts, send_dts);

	return len;
}

static u32 vidtv_pes_write_header_stuffing(struct pes_header_write_args args)
{
	/*
	 * This is a fixed 8-bit value equal to '0xFF' that can be inserted
	 * by the encoder, for example to meet the requirements of the channel.
	 * It is discarded by the decoder. No more than 32 stuffing bytes shall
	 * be present in one PES packet header.
	 */
	if (args.n_pes_h_s_bytes > PES_HEADER_MAX_STUFFING_BYTES) {
		pr_warn_ratelimited("More than %d stuffing bytes in PES packet header\n",
				    PES_HEADER_MAX_STUFFING_BYTES);
		args.n_pes_h_s_bytes = PES_HEADER_MAX_STUFFING_BYTES;
	}

	return vidtv_memset(args.dest_buf,
			    args.dest_offset,
			    args.dest_buf_sz,
			    TS_FILL_BYTE,
			    args.n_pes_h_s_bytes);
}

static u32 vidtv_pes_write_pts_dts(struct pes_header_write_args args)
{
	u32 nbytes = 0;  /* the number of bytes written by this function */

	struct vidtv_pes_optional_pts pts = {};
	struct vidtv_pes_optional_pts_dts pts_dts = {};
	void *op = NULL;
	size_t op_sz = 0;
	u64 mask1;
	u64 mask2;
	u64 mask3;

	if (!args.send_pts && args.send_dts)
		return 0;

	mask1 = GENMASK_ULL(32, 30);
	mask2 = GENMASK_ULL(29, 15);
	mask3 = GENMASK_ULL(14, 0);

	/* see ISO/IEC 13818-1 : 2000 p. 32 */
	if (args.send_pts && args.send_dts) {
		pts_dts.pts1 = (0x3 << 4) | ((args.pts & mask1) >> 29) | 0x1;
		pts_dts.pts2 = cpu_to_be16(((args.pts & mask2) >> 14) | 0x1);
		pts_dts.pts3 = cpu_to_be16(((args.pts & mask3) << 1) | 0x1);

		pts_dts.dts1 = (0x1 << 4) | ((args.dts & mask1) >> 29) | 0x1;
		pts_dts.dts2 = cpu_to_be16(((args.dts & mask2) >> 14) | 0x1);
		pts_dts.dts3 = cpu_to_be16(((args.dts & mask3) << 1) | 0x1);

		op = &pts_dts;
		op_sz = sizeof(pts_dts);

	} else if (args.send_pts) {
		pts.pts1 = (0x1 << 5) | ((args.pts & mask1) >> 29) | 0x1;
		pts.pts2 = cpu_to_be16(((args.pts & mask2) >> 14) | 0x1);
		pts.pts3 = cpu_to_be16(((args.pts & mask3) << 1) | 0x1);

		op = &pts;
		op_sz = sizeof(pts);
	}

	/* copy PTS/DTS optional */
	nbytes += vidtv_memcpy(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.dest_buf_sz,
			       op,
			       op_sz);

	return nbytes;
}

static u32 vidtv_pes_write_h(struct pes_header_write_args args)
{
	u32 nbytes = 0;  /* the number of bytes written by this function */

	struct vidtv_mpeg_pes pes_header          = {};
	struct vidtv_pes_optional pes_optional    = {};
	struct pes_header_write_args pts_dts_args = args;
	u32 stream_id = (args.encoder_id == S302M) ? PRIVATE_STREAM_1_ID : args.stream_id;
	u16 pes_opt_bitfield = 0x2 << 13;

	pes_header.bitfield = cpu_to_be32((PES_START_CODE_PREFIX << 8) | stream_id);

	pes_header.length = cpu_to_be16(vidtv_pes_op_get_len(args.send_pts,
							     args.send_dts) +
							     args.access_unit_len);

	if (args.send_pts && args.send_dts)
		pes_opt_bitfield |= (0x3 << 6);
	else if (args.send_pts)
		pes_opt_bitfield |= (0x1 << 7);

	pes_optional.bitfield = cpu_to_be16(pes_opt_bitfield);
	pes_optional.length = vidtv_pes_op_get_len(args.send_pts, args.send_dts) +
			      args.n_pes_h_s_bytes -
			      sizeof(struct vidtv_pes_optional);

	/* copy header */
	nbytes += vidtv_memcpy(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.dest_buf_sz,
			       &pes_header,
			       sizeof(pes_header));

	/* copy optional header bits */
	nbytes += vidtv_memcpy(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.dest_buf_sz,
			       &pes_optional,
			       sizeof(pes_optional));

	/* copy the timing information */
	pts_dts_args.dest_offset = args.dest_offset + nbytes;
	nbytes += vidtv_pes_write_pts_dts(pts_dts_args);

	/* write any PES header stuffing */
	nbytes += vidtv_pes_write_header_stuffing(args);

	return nbytes;
}

static u32 vidtv_pes_write_stuffing(struct pes_ts_header_write_args *args,
				    u32 dest_offset)
{
	u32 nbytes = 0;
	struct vidtv_mpeg_ts_adaption ts_adap = {};
	u32 stuff_nbytes;

	if (!args->n_stuffing_bytes)
		goto out;

	if (args->n_stuffing_bytes > PES_TS_HEADER_MAX_STUFFING_BYTES) {
		pr_warn_ratelimited("More than %d stuffing bytes for a PES packet!\n",
				    PES_TS_HEADER_MAX_STUFFING_BYTES);

		args->n_stuffing_bytes = PES_TS_HEADER_MAX_STUFFING_BYTES;
	}

	/* the AF will only be its 'length' field with a value of zero */
	if (args->n_stuffing_bytes == 1) {
		nbytes += vidtv_memset(args->dest_buf,
				       dest_offset + nbytes,
				       args->dest_buf_sz,
				       0,
				       args->n_stuffing_bytes);
		goto out;
	}

	stuff_nbytes = args->n_stuffing_bytes - sizeof(ts_adap);

	/* length _immediately_ following 'adaptation_field_length' */
	ts_adap.length = sizeof(ts_adap) -
			 sizeof(ts_adap.length) +
			 stuff_nbytes;

	/* write the adap after the TS header */
	nbytes += vidtv_memcpy(args->dest_buf,
			       dest_offset + nbytes,
			       args->dest_buf_sz,
			       &ts_adap,
			       sizeof(ts_adap));

	/* write the stuffing bytes */
	nbytes += vidtv_memset(args->dest_buf,
			       dest_offset + nbytes,
			       args->dest_buf_sz,
			       TS_FILL_BYTE,
			       stuff_nbytes);

out:
	if (nbytes != args->n_stuffing_bytes)
		pr_warn_ratelimited("write size was %d, expected %d\n",
				    nbytes,
				    args->n_stuffing_bytes);

	return nbytes;
}

static u32 vidtv_pes_write_ts_h(struct pes_ts_header_write_args args)
{
	/* number of bytes written by this function */
	u32 nbytes = 0;
	struct vidtv_mpeg_ts ts_header = {};
	u16 payload_start = !args.wrote_pes_header;

	ts_header.sync_byte        = TS_SYNC_BYTE;
	ts_header.bitfield         = cpu_to_be16((payload_start << 14) | args.pid);
	ts_header.scrambling       = 0;
	ts_header.adaptation_field = (args.n_stuffing_bytes) > 0;
	ts_header.payload          = (args.n_stuffing_bytes) < PES_TS_HEADER_MAX_STUFFING_BYTES;

	ts_header.continuity_counter = *args.continuity_counter;

	vidtv_ts_inc_cc(args.continuity_counter);

	/* write the TS header */
	nbytes += vidtv_memcpy(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.dest_buf_sz,
			       &ts_header,
			       sizeof(ts_header));

	/* write stuffing, if any */
	nbytes += vidtv_pes_write_stuffing(&args, args.dest_offset + nbytes);

	return nbytes;
}

u32 vidtv_pes_write_into(struct pes_write_args args)
{
	u32 nbytes_past_boundary = (args.dest_offset % TS_PACKET_LEN);
	bool aligned = (nbytes_past_boundary == 0);

	struct pes_ts_header_write_args ts_header_args = {};
	struct pes_header_write_args pes_header_args   = {};

	/* number of bytes written by this function */
	u32 nbytes        = 0;
	u32 remaining_len = args.access_unit_len;

	bool wrote_pes_header = false;

	/* whether we need to stuff the TS packet to align the buffer */
	bool should_insert_stuffing_bytes = false;

	u32 available_space    = 0;
	u32 payload_write_len  = 0;
	u32 num_stuffing_bytes = 0;

	if (!aligned) {
		pr_warn_ratelimited("Cannot start a PES packet in a misaligned buffer\n");

		/* forcibly align and hope for the best */
		nbytes += vidtv_memset(args.dest_buf,
				       args.dest_offset + nbytes,
				       args.dest_buf_sz,
				       TS_FILL_BYTE,
				       TS_PACKET_LEN - nbytes_past_boundary);

		aligned = true;
	}

	if (args.send_dts && !args.send_pts) {
		pr_warn_ratelimited("forbidden value '01' for PTS_DTS flags\n");
		args.send_pts = true;
		args.pts      = args.dts;
	}

	/* see SMPTE 302M clause 6.4 */
	if (args.encoder_id == S302M) {
		args.send_dts = false;
		args.send_pts = true;
	}

	while (remaining_len) {
		/*
		 * The amount of space initially available in the TS packet.
		 * if this is the beginning of the PES packet, take into account
		 * the space needed for the TS header _and_ for the PES header
		 */
		available_space = (!wrote_pes_header) ?
				  TS_PAYLOAD_LEN -
				  vidtv_pes_h_get_len(args.send_pts, args.send_dts) :
				  TS_PAYLOAD_LEN;

		/* if the encoder has inserted stuffing bytes in the PES
		 * header, account for them.
		 */
		available_space -= args.n_pes_h_s_bytes;

		/* whether stuffing bytes are needed to align the buffer */
		should_insert_stuffing_bytes = remaining_len < available_space;

		/*
		 * how much of the _actual_ payload should be written in this
		 * packet.
		 */
		payload_write_len = (should_insert_stuffing_bytes) ?
				    remaining_len :
				    available_space;

		num_stuffing_bytes = available_space - payload_write_len;

		/* write ts header */
		ts_header_args.dest_buf           = args.dest_buf;
		ts_header_args.dest_offset        = args.dest_offset + nbytes;
		ts_header_args.dest_buf_sz        = args.dest_buf_sz;
		ts_header_args.pid                = args.pid;
		ts_header_args.continuity_counter = args.continuity_counter;
		ts_header_args.wrote_pes_header   = wrote_pes_header;
		ts_header_args.n_stuffing_bytes   = num_stuffing_bytes;

		nbytes += vidtv_pes_write_ts_h(ts_header_args);

		if (!wrote_pes_header) {
			/* write the PES header only once */
			pes_header_args.dest_buf        = args.dest_buf;

			pes_header_args.dest_offset     = args.dest_offset +
							  nbytes;

			pes_header_args.dest_buf_sz     = args.dest_buf_sz;
			pes_header_args.encoder_id      = args.encoder_id;
			pes_header_args.send_pts        = args.send_pts;
			pes_header_args.pts             = args.pts;
			pes_header_args.send_dts        = args.send_dts;
			pes_header_args.dts             = args.dts;
			pes_header_args.stream_id       = args.stream_id;
			pes_header_args.n_pes_h_s_bytes = args.n_pes_h_s_bytes;
			pes_header_args.access_unit_len = args.access_unit_len;

			nbytes           += vidtv_pes_write_h(pes_header_args);
			wrote_pes_header  = true;
		}

		/* write as much of the payload as we possibly can */
		nbytes += vidtv_memcpy(args.dest_buf,
				       args.dest_offset + nbytes,
				       args.dest_buf_sz,
				       args.from,
				       payload_write_len);

		args.from += payload_write_len;

		remaining_len -= payload_write_len;
	}

	return nbytes;
}
