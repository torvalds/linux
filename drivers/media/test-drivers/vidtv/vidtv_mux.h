/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Vidtv serves as a reference DVB driver and helps validate the existing APIs
 * in the media subsystem. It can also aid developers working on userspace
 * applications.
 *
 * This file contains the muxer logic for TS packets from different
 * elementary streams.
 *
 * Loosely based on libavcodec/mpegtsenc.c
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_MUX_H
#define VIDTV_MUX_H

#include <linux/types.h>
#include <linux/hashtable.h>
#include <linux/workqueue.h>
#include <media/dvb_frontend.h>

#include "vidtv_psi.h"

/**
 * struct vidtv_mux_timing - Timing related information
 *
 * This is used to decide when PCR or PSI packets should be sent. This will also
 * provide storage for the clock, which is used to compute the value for the PCR.
 *
 * @start_jiffies: The value of 'jiffies' when we started the mux thread.
 * @current_jiffies: The value of 'jiffies' for the current iteration.
 * @past_jiffies: The value of 'jiffies' for the past iteration.
 * @clk: A 27Mhz clock from which we will drive the PCR. Updated proportionally
 * on every iteration.
 * @pcr_period_usecs: How often we should send PCR packets.
 * @si_period_usecs: How often we should send PSI packets.
 */
struct vidtv_mux_timing {
	u64 start_jiffies;
	u64 current_jiffies;
	u64 past_jiffies;

	u64 clk;

	u64 pcr_period_usecs;
	u64 si_period_usecs;
};

/**
 * struct vidtv_mux_si - Store the PSI context.
 *
 * This is used to store the PAT, PMT sections and SDT in use by the muxer.
 *
 * The muxer acquire these by looking into the hardcoded channels in
 * vidtv_channel and then periodically sends the TS packets for them>
 *
 * @pat: The PAT in use by the muxer.
 * @pmt_secs: The PMT sections in use by the muxer. One for each program in the PAT.
 * @sdt: The SDT in use by the muxer.
 */
struct vidtv_mux_si {
	/* the SI tables */
	struct vidtv_psi_table_pat *pat;
	struct vidtv_psi_table_pmt **pmt_secs; /* the PMT sections */
	struct vidtv_psi_table_sdt *sdt;
};

/**
 * struct vidtv_mux_pid_ctx - Store the context for a given TS PID.
 * @pid: The TS PID.
 * @cc: The continuity counter for this PID. It is incremented on every TS
 * pack and it will wrap around at 0xf0. If the decoder notices a sudden jump in
 * this counter this will trigger a discontinuity state.
 * @h: This is embedded in a hash table, mapping pid -> vidtv_mux_pid_ctx
 */
struct vidtv_mux_pid_ctx {
	u16 pid;
	u8 cc; /* continuity counter */
	struct hlist_node h;
};

/**
 * struct vidtv_mux - A muxer abstraction loosely based in libavcodec/mpegtsenc.c
 * @mux_rate_kbytes_sec: The bit rate for the TS, in kbytes.
 * @timing: Keeps track of timing related information.
 * @pid_ctx: A hash table to keep track of per-PID metadata.
 * @on_new_packets_available_cb: A callback to inform of new TS packets ready.
 * @mux_buf: A pointer to a buffer for this muxer. TS packets are stored there
 * and then passed on to the bridge driver.
 * @mux_buf_sz: The size for 'mux_buf'.
 * @mux_buf_offset: The current offset into 'mux_buf'.
 * @channels: The channels associated with this muxer.
 * @si: Keeps track of the PSI context.
 * @num_streamed_pcr: Number of PCR packets streamed.
 * @num_streamed_si: The number of PSI packets streamed.
 * @mpeg_thread: Thread responsible for the muxer loop.
 * @streaming: whether 'mpeg_thread' is running.
 * @pcr_pid: The TS PID used for the PSI packets. All channels will share the
 * same PCR.
 * @transport_stream_id: The transport stream ID
 * @priv: Private data.
 */
struct vidtv_mux {
	struct dvb_frontend *fe;
	struct device *dev;

	struct vidtv_mux_timing timing;

	u32 mux_rate_kbytes_sec;

	DECLARE_HASHTABLE(pid_ctx, 3);

	void (*on_new_packets_available_cb)(void *priv, u8 *buf, u32 npackets);

	u8 *mux_buf;
	u32 mux_buf_sz;
	u32 mux_buf_offset;

	struct vidtv_channel  *channels;

	struct vidtv_mux_si si;
	u64 num_streamed_pcr;
	u64 num_streamed_si;

	struct work_struct mpeg_thread;
	bool streaming;

	u16 pcr_pid;
	u16 transport_stream_id;
	void *priv;
};

/**
 * struct vidtv_mux_init_args - Arguments used to inix the muxer.
 * @mux_rate_kbytes_sec: The bit rate for the TS, in kbytes.
 * @on_new_packets_available_cb: A callback to inform of new TS packets ready.
 * @mux_buf_sz: The size for 'mux_buf'.
 * @pcr_period_usecs: How often we should send PCR packets.
 * @si_period_usecs: How often we should send PSI packets.
 * @pcr_pid: The TS PID used for the PSI packets. All channels will share the
 * same PCR.
 * @transport_stream_id: The transport stream ID
 * @channels: an optional list of channels to use
 * @priv: Private data.
 */
struct vidtv_mux_init_args {
	u32 mux_rate_kbytes_sec;
	void (*on_new_packets_available_cb)(void *priv, u8 *buf, u32 npackets);
	u32 mux_buf_sz;
	u64 pcr_period_usecs;
	u64 si_period_usecs;
	u16 pcr_pid;
	u16 transport_stream_id;
	struct vidtv_channel *channels;
	void *priv;
};

struct vidtv_mux *vidtv_mux_init(struct dvb_frontend *fe,
				 struct device *dev,
				 struct vidtv_mux_init_args args);
void vidtv_mux_destroy(struct vidtv_mux *m);

void vidtv_mux_start_thread(struct vidtv_mux *m);
void vidtv_mux_stop_thread(struct vidtv_mux *m);

#endif //VIDTV_MUX_H
