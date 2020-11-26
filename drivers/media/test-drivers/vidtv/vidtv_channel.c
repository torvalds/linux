// SPDX-License-Identifier: GPL-2.0
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
 * For each program in the PAT, a PMT section will be created
 * The PMT section for a channel will be assigned its streams.
 * Every stream will have its corresponding encoder polled to produce TS packets
 * These packets may be interleaved by the mux and then delivered to the bridge
 *
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/dev_printk.h>
#include <linux/ratelimit.h>

#include "vidtv_channel.h"
#include "vidtv_psi.h"
#include "vidtv_encoder.h"
#include "vidtv_mux.h"
#include "vidtv_common.h"
#include "vidtv_s302m.h"

static void vidtv_channel_encoder_destroy(struct vidtv_encoder *e)
{
	struct vidtv_encoder *curr = e;
	struct vidtv_encoder *tmp = NULL;

	while (curr) {
		/* forward the call to the derived type */
		tmp = curr;
		curr = curr->next;
		tmp->destroy(tmp);
	}
}

#define ENCODING_ISO8859_15 "\x0b"

struct vidtv_channel
*vidtv_channel_s302m_init(struct vidtv_channel *head, u16 transport_stream_id)
{
	/*
	 * init an audio only channel with a s302m encoder
	 */
	const u16 s302m_service_id          = 0x880;
	const u16 s302m_program_num         = 0x880;
	const u16 s302m_program_pid         = 0x101; /* packet id for PMT*/
	const u16 s302m_es_pid              = 0x111; /* packet id for the ES */
	const __be32 s302m_fid              = cpu_to_be32(VIDTV_S302M_FORMAT_IDENTIFIER);

	char *name = ENCODING_ISO8859_15 "Beethoven";
	char *provider = ENCODING_ISO8859_15 "LinuxTV.org";

	struct vidtv_channel *s302m = kzalloc(sizeof(*s302m), GFP_KERNEL);
	struct vidtv_s302m_encoder_init_args encoder_args = {};

	s302m->name = kstrdup(name, GFP_KERNEL);

	s302m->service = vidtv_psi_sdt_service_init(NULL, s302m_service_id);

	s302m->service->descriptor = (struct vidtv_psi_desc *)
				     vidtv_psi_service_desc_init(NULL,
								 DIGITAL_TELEVISION_SERVICE,
								 name,
								 provider);

	s302m->transport_stream_id = transport_stream_id;

	s302m->program = vidtv_psi_pat_program_init(NULL,
						    s302m_service_id,
						    s302m_program_pid);

	s302m->program_num = s302m_program_num;

	s302m->streams = vidtv_psi_pmt_stream_init(NULL,
						   STREAM_PRIVATE_DATA,
						   s302m_es_pid);

	s302m->streams->descriptor = (struct vidtv_psi_desc *)
				     vidtv_psi_registration_desc_init(NULL,
								      s302m_fid,
								      NULL,
								      0);
	encoder_args.es_pid = s302m_es_pid;

	s302m->encoders = vidtv_s302m_encoder_init(encoder_args);

	if (head) {
		while (head->next)
			head = head->next;

		head->next = s302m;
	}

	return s302m;
}

static struct vidtv_psi_table_sdt_service
*vidtv_channel_sdt_serv_cat_into_new(struct vidtv_mux *m)
{
	/* Concatenate the services */
	const struct vidtv_channel *cur_chnl = m->channels;

	struct vidtv_psi_table_sdt_service *curr = NULL;
	struct vidtv_psi_table_sdt_service *head = NULL;
	struct vidtv_psi_table_sdt_service *tail = NULL;

	struct vidtv_psi_desc *desc = NULL;
	u16 service_id;

	if (!cur_chnl)
		return NULL;

	while (cur_chnl) {
		curr = cur_chnl->service;

		if (!curr)
			dev_warn_ratelimited(m->dev,
					     "No services found for channel %s\n", cur_chnl->name);

		while (curr) {
			service_id = be16_to_cpu(curr->service_id);
			tail = vidtv_psi_sdt_service_init(tail, service_id);

			desc = vidtv_psi_desc_clone(curr->descriptor);
			vidtv_psi_desc_assign(&tail->descriptor, desc);

			if (!head)
				head = tail;

			curr = curr->next;
		}

		cur_chnl = cur_chnl->next;
	}

	return head;
}

static struct vidtv_psi_table_pat_program*
vidtv_channel_pat_prog_cat_into_new(struct vidtv_mux *m)
{
	/* Concatenate the programs */
	const struct vidtv_channel *cur_chnl = m->channels;
	struct vidtv_psi_table_pat_program *curr = NULL;
	struct vidtv_psi_table_pat_program *head = NULL;
	struct vidtv_psi_table_pat_program *tail = NULL;
	u16 serv_id;
	u16 pid;

	if (!cur_chnl)
		return NULL;

	while (cur_chnl) {
		curr = cur_chnl->program;

		if (!curr)
			dev_warn_ratelimited(m->dev,
					     "No programs found for channel %s\n",
					     cur_chnl->name);

		while (curr) {
			serv_id = be16_to_cpu(curr->service_id);
			pid = vidtv_psi_get_pat_program_pid(curr);
			tail = vidtv_psi_pat_program_init(tail,
							  serv_id,
							  pid);

			if (!head)
				head = tail;

			curr = curr->next;
		}

		cur_chnl = cur_chnl->next;
	}

	return head;
}

static void
vidtv_channel_pmt_match_sections(struct vidtv_channel *channels,
				 struct vidtv_psi_table_pmt **sections,
				 u32 nsections)
{
	/*
	 * Match channels to their respective PMT sections, then assign the
	 * streams
	 */
	struct vidtv_psi_table_pmt *curr_section = NULL;
	struct vidtv_channel *cur_chnl = channels;

	struct vidtv_psi_table_pmt_stream *s = NULL;
	struct vidtv_psi_table_pmt_stream *head = NULL;
	struct vidtv_psi_table_pmt_stream *tail = NULL;

	struct vidtv_psi_desc *desc = NULL;
	u32 j;
	u16 curr_id;
	u16 e_pid; /* elementary stream pid */

	while (cur_chnl) {
		for (j = 0; j < nsections; ++j) {
			curr_section = sections[j];

			if (!curr_section)
				continue;

			curr_id = be16_to_cpu(curr_section->header.id);

			/* we got a match */
			if (curr_id == cur_chnl->program_num) {
				s = cur_chnl->streams;

				/* clone the streams for the PMT */
				while (s) {
					e_pid = vidtv_psi_pmt_stream_get_elem_pid(s);
					tail = vidtv_psi_pmt_stream_init(tail,
									 s->type,
									 e_pid);

					if (!head)
						head = tail;

					desc = vidtv_psi_desc_clone(s->descriptor);
					vidtv_psi_desc_assign(&tail->descriptor, desc);

					s = s->next;
				}

				vidtv_psi_pmt_stream_assign(curr_section, head);
				break;
			}
		}

		cur_chnl = cur_chnl->next;
	}
}

void vidtv_channel_si_init(struct vidtv_mux *m)
{
	struct vidtv_psi_table_pat_program *programs = NULL;
	struct vidtv_psi_table_sdt_service *services = NULL;

	m->si.pat = vidtv_psi_pat_table_init(m->transport_stream_id);

	m->si.sdt = vidtv_psi_sdt_table_init(m->transport_stream_id);

	programs = vidtv_channel_pat_prog_cat_into_new(m);
	services = vidtv_channel_sdt_serv_cat_into_new(m);

	/* assemble all programs and assign to PAT */
	vidtv_psi_pat_program_assign(m->si.pat, programs);

	/* assemble all services and assign to SDT */
	vidtv_psi_sdt_service_assign(m->si.sdt, services);

	m->si.pmt_secs = vidtv_psi_pmt_create_sec_for_each_pat_entry(m->si.pat, m->pcr_pid);

	vidtv_channel_pmt_match_sections(m->channels,
					 m->si.pmt_secs,
					 m->si.pat->programs);
}

void vidtv_channel_si_destroy(struct vidtv_mux *m)
{
	u32 i;
	u16 num_programs = m->si.pat->programs;

	vidtv_psi_pat_table_destroy(m->si.pat);

	for (i = 0; i < num_programs; ++i)
		vidtv_psi_pmt_table_destroy(m->si.pmt_secs[i]);

	kfree(m->si.pmt_secs);
	vidtv_psi_sdt_table_destroy(m->si.sdt);
}

void vidtv_channels_init(struct vidtv_mux *m)
{
	/* this is the place to add new 'channels' for vidtv */
	m->channels = vidtv_channel_s302m_init(NULL, m->transport_stream_id);
}

void vidtv_channels_destroy(struct vidtv_mux *m)
{
	struct vidtv_channel *curr = m->channels;
	struct vidtv_channel *tmp = NULL;

	while (curr) {
		kfree(curr->name);
		vidtv_psi_sdt_service_destroy(curr->service);
		vidtv_psi_pat_program_destroy(curr->program);
		vidtv_psi_pmt_stream_destroy(curr->streams);
		vidtv_channel_encoder_destroy(curr->encoders);

		tmp = curr;
		curr = curr->next;
		kfree(tmp);
	}
}
