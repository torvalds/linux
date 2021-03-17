/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Vidtv serves as a reference DVB driver and helps validate the existing APIs
 * in the media subsystem. It can also aid developers working on userspace
 * applications.
 *
 * This file contains the code for a 'channel' abstraction.
 *
 * When vidtv boots, it will create some hardcoded channels.
 * Their services will be concatenated to populate the SDT.
 * Their programs will be concatenated to populate the PAT
 * Their events will be concatenated to populate the EIT
 * For each program in the PAT, a PMT section will be created
 * The PMT section for a channel will be assigned its streams.
 * Every stream will have its corresponding encoder polled to produce TS packets
 * These packets may be interleaved by the mux and then delivered to the bridge
 *
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_CHANNEL_H
#define VIDTV_CHANNEL_H

#include <linux/types.h>

#include "vidtv_encoder.h"
#include "vidtv_mux.h"
#include "vidtv_psi.h"

/**
 * struct vidtv_channel - A 'channel' abstraction
 *
 * When vidtv boots, it will create some hardcoded channels.
 * Their services will be concatenated to populate the SDT.
 * Their programs will be concatenated to populate the PAT
 * For each program in the PAT, a PMT section will be created
 * The PMT section for a channel will be assigned its streams.
 * Every stream will have its corresponding encoder polled to produce TS packets
 * These packets may be interleaved by the mux and then delivered to the bridge
 *
 * @name: name of the channel
 * @transport_stream_id: a number to identify the TS, chosen at will.
 * @service: A _single_ service. Will be concatenated into the SDT.
 * @program_num: The link between PAT, PMT and SDT.
 * @program: A _single_ program with one or more streams associated with it.
 * Will be concatenated into the PAT.
 * @streams: A stream loop used to populate the PMT section for 'program'
 * @encoders: A encoder loop. There must be one encoder for each stream.
 * @events: Optional event information. This will feed into the EIT.
 * @next: Optionally chain this channel.
 */
struct vidtv_channel {
	char *name;
	u16 transport_stream_id;
	struct vidtv_psi_table_sdt_service *service;
	u16 program_num;
	struct vidtv_psi_table_pat_program *program;
	struct vidtv_psi_table_pmt_stream *streams;
	struct vidtv_encoder *encoders;
	struct vidtv_psi_table_eit_event *events;
	struct vidtv_channel *next;
};

/**
 * vidtv_channel_si_init - Init the PSI tables from the channels in the mux
 * @m: The mux containing the channels.
 */
int vidtv_channel_si_init(struct vidtv_mux *m);
void vidtv_channel_si_destroy(struct vidtv_mux *m);

/**
 * vidtv_channels_init - Init hardcoded, fake 'channels'.
 * @m: The mux to store the channels into.
 */
int vidtv_channels_init(struct vidtv_mux *m);
struct vidtv_channel
*vidtv_channel_s302m_init(struct vidtv_channel *head, u16 transport_stream_id);
void vidtv_channels_destroy(struct vidtv_mux *m);

#endif //VIDTV_CHANNEL_H
