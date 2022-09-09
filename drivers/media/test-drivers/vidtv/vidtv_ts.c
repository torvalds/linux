// SPDX-License-Identifier: GPL-2.0
/*
 * The Virtual DVB test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s, %d: " fmt, __func__, __LINE__

#include <linux/math64.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/types.h>

#include "vidtv_common.h"
#include "vidtv_ts.h"

static u32 vidtv_ts_write_pcr_bits(u8 *to, u32 to_offset, u64 pcr)
{
	/* Exact same from ffmpeg. PCR is a counter driven by a 27Mhz clock */
	u64 div;
	u64 rem;
	u8 *buf = to + to_offset;
	u64 pcr_low;
	u64 pcr_high;

	div = div64_u64_rem(pcr, 300, &rem);

	pcr_low = rem; /* pcr_low = pcr % 300 */
	pcr_high = div; /* pcr_high = pcr / 300 */

	*buf++ = pcr_high >> 25;
	*buf++ = pcr_high >> 17;
	*buf++ = pcr_high >>  9;
	*buf++ = pcr_high >>  1;
	*buf++ = pcr_high <<  7 | pcr_low >> 8 | 0x7e;
	*buf++ = pcr_low;

	return 6;
}

void vidtv_ts_inc_cc(u8 *continuity_counter)
{
	++*continuity_counter;
	if (*continuity_counter > TS_CC_MAX_VAL)
		*continuity_counter = 0;
}

u32 vidtv_ts_null_write_into(struct null_packet_write_args args)
{
	u32 nbytes = 0;
	struct vidtv_mpeg_ts ts_header = {};

	ts_header.sync_byte          = TS_SYNC_BYTE;
	ts_header.bitfield           = cpu_to_be16(TS_NULL_PACKET_PID);
	ts_header.payload            = 1;
	ts_header.continuity_counter = *args.continuity_counter;

	/* copy TS header */
	nbytes += vidtv_memcpy(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.buf_sz,
			       &ts_header,
			       sizeof(ts_header));

	vidtv_ts_inc_cc(args.continuity_counter);

	/* fill the rest with empty data */
	nbytes += vidtv_memset(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.buf_sz,
			       TS_FILL_BYTE,
			       TS_PACKET_LEN - nbytes);

	/* we should have written exactly _one_ 188byte packet */
	if (nbytes != TS_PACKET_LEN)
		pr_warn_ratelimited("Expected exactly %d bytes, got %d\n",
				    TS_PACKET_LEN,
				    nbytes);

	return nbytes;
}

u32 vidtv_ts_pcr_write_into(struct pcr_write_args args)
{
	u32 nbytes = 0;
	struct vidtv_mpeg_ts ts_header = {};
	struct vidtv_mpeg_ts_adaption ts_adap = {};

	ts_header.sync_byte     = TS_SYNC_BYTE;
	ts_header.bitfield      = cpu_to_be16(args.pid);
	ts_header.scrambling    = 0;
	/* cc is not incremented, but it is needed. see 13818-1 clause 2.4.3.3 */
	ts_header.continuity_counter = *args.continuity_counter;
	ts_header.payload            = 0;
	ts_header.adaptation_field   = 1;

	/* 13818-1 clause 2.4.3.5 */
	ts_adap.length = 183;
	ts_adap.PCR    = 1;

	/* copy TS header */
	nbytes += vidtv_memcpy(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.buf_sz,
			       &ts_header,
			       sizeof(ts_header));

	/* write the adap after the TS header */
	nbytes += vidtv_memcpy(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.buf_sz,
			       &ts_adap,
			       sizeof(ts_adap));

	/* write the PCR optional */
	nbytes += vidtv_ts_write_pcr_bits(args.dest_buf,
					  args.dest_offset + nbytes,
					  args.pcr);

	nbytes += vidtv_memset(args.dest_buf,
			       args.dest_offset + nbytes,
			       args.buf_sz,
			       TS_FILL_BYTE,
			       TS_PACKET_LEN - nbytes);

	/* we should have written exactly _one_ 188byte packet */
	if (nbytes != TS_PACKET_LEN)
		pr_warn_ratelimited("Expected exactly %d bytes, got %d\n",
				    TS_PACKET_LEN,
				    nbytes);

	return nbytes;
}
