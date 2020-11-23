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
 * Their events will be concatenated to populate the EIT
 * For each program in the PAT, a PMT section will be created
 * The PMT section for a channel will be assigned its streams.
 * Every stream will have its corresponding encoder polled to produce TS packets
 * These packets may be interleaved by the mux and then delivered to the bridge
 *
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#include <linux/dev_printk.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "vidtv_channel.h"
#include "vidtv_common.h"
#include "vidtv_encoder.h"
#include "vidtv_mux.h"
#include "vidtv_psi.h"
#include "vidtv_s302m.h"

static void vidtv_channel_encoder_destroy(struct vidtv_encoder *e)
{
	struct vidtv_encoder *tmp = NULL;
	struct vidtv_encoder *curr = e;

	while (curr) {
		/* forward the call to the derived type */
		tmp = curr;
		curr = curr->next;
		tmp->destroy(tmp);
	}
}

#define ENCODING_ISO8859_15 "\x0b"
#define TS_NIT_PID	0x10

/*
 * init an audio only channel with a s302m encoder
 */
struct vidtv_channel
*vidtv_channel_s302m_init(struct vidtv_channel *head, u16 transport_stream_id)
{
	const __be32 s302m_fid              = cpu_to_be32(VIDTV_S302M_FORMAT_IDENTIFIER);
	char *event_text = ENCODING_ISO8859_15 "Bagatelle No. 25 in A minor for solo piano, also known as F\xfcr Elise, composed by Ludwig van Beethoven";
	char *event_name = ENCODING_ISO8859_15 "Ludwig van Beethoven: F\xfcr Elise";
	struct vidtv_s302m_encoder_init_args encoder_args = {};
	char *iso_language_code = ENCODING_ISO8859_15 "eng";
	char *provider = ENCODING_ISO8859_15 "LinuxTV.org";
	char *name = ENCODING_ISO8859_15 "Beethoven";
	const u16 s302m_es_pid              = 0x111; /* packet id for the ES */
	const u16 s302m_program_pid         = 0x101; /* packet id for PMT*/
	const u16 s302m_service_id          = 0x880;
	const u16 s302m_program_num         = 0x880;
	const u16 s302m_beethoven_event_id  = 1;
	struct vidtv_channel *s302m;

	s302m = kzalloc(sizeof(*s302m), GFP_KERNEL);
	if (!s302m)
		return NULL;

	s302m->name = kstrdup(name, GFP_KERNEL);
	if (!s302m->name)
		goto free_s302m;

	s302m->service = vidtv_psi_sdt_service_init(NULL, s302m_service_id, false, true);
	if (!s302m->service)
		goto free_name;

	s302m->service->descriptor = (struct vidtv_psi_desc *)
				     vidtv_psi_service_desc_init(NULL,
								 DIGITAL_RADIO_SOUND_SERVICE,
								 name,
								 provider);
	if (!s302m->service->descriptor)
		goto free_service;

	s302m->transport_stream_id = transport_stream_id;

	s302m->program = vidtv_psi_pat_program_init(NULL,
						    s302m_service_id,
						    s302m_program_pid);
	if (!s302m->program)
		goto free_service;

	s302m->program_num = s302m_program_num;

	s302m->streams = vidtv_psi_pmt_stream_init(NULL,
						   STREAM_PRIVATE_DATA,
						   s302m_es_pid);
	if (!s302m->streams)
		goto free_program;

	s302m->streams->descriptor = (struct vidtv_psi_desc *)
				     vidtv_psi_registration_desc_init(NULL,
								      s302m_fid,
								      NULL,
								      0);
	if (!s302m->streams->descriptor)
		goto free_streams;

	encoder_args.es_pid = s302m_es_pid;

	s302m->encoders = vidtv_s302m_encoder_init(encoder_args);
	if (!s302m->encoders)
		goto free_streams;

	s302m->events = vidtv_psi_eit_event_init(NULL, s302m_beethoven_event_id);
	if (!s302m->events)
		goto free_encoders;
	s302m->events->descriptor = (struct vidtv_psi_desc *)
				    vidtv_psi_short_event_desc_init(NULL,
								    iso_language_code,
								    event_name,
								    event_text);
	if (!s302m->events->descriptor)
		goto free_events;

	if (head) {
		while (head->next)
			head = head->next;

		head->next = s302m;
	}

	return s302m;

free_events:
	vidtv_psi_eit_event_destroy(s302m->events);
free_encoders:
	vidtv_s302m_encoder_destroy(s302m->encoders);
free_streams:
	vidtv_psi_pmt_stream_destroy(s302m->streams);
free_program:
	vidtv_psi_pat_program_destroy(s302m->program);
free_service:
	vidtv_psi_sdt_service_destroy(s302m->service);
free_name:
	kfree(s302m->name);
free_s302m:
	kfree(s302m);

	return NULL;
}

static struct vidtv_psi_table_eit_event
*vidtv_channel_eit_event_cat_into_new(struct vidtv_mux *m)
{
	/* Concatenate the events */
	const struct vidtv_channel *cur_chnl = m->channels;
	struct vidtv_psi_table_eit_event *curr = NULL;
	struct vidtv_psi_table_eit_event *head = NULL;
	struct vidtv_psi_table_eit_event *tail = NULL;
	struct vidtv_psi_desc *desc = NULL;
	u16 event_id;

	if (!cur_chnl)
		return NULL;

	while (cur_chnl) {
		curr = cur_chnl->events;

		if (!curr)
			dev_warn_ratelimited(m->dev,
					     "No events found for channel %s\n",
					     cur_chnl->name);

		while (curr) {
			event_id = be16_to_cpu(curr->event_id);
			tail = vidtv_psi_eit_event_init(tail, event_id);
			if (!tail) {
				vidtv_psi_eit_event_destroy(head);
				return NULL;
			}

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
					     "No services found for channel %s\n",
					     cur_chnl->name);

		while (curr) {
			service_id = be16_to_cpu(curr->service_id);
			tail = vidtv_psi_sdt_service_init(tail,
							  service_id,
							  curr->EIT_schedule,
							  curr->EIT_present_following);
			if (!tail)
				goto free;

			desc = vidtv_psi_desc_clone(curr->descriptor);
			if (!desc)
				goto free_tail;
			vidtv_psi_desc_assign(&tail->descriptor, desc);

			if (!head)
				head = tail;

			curr = curr->next;
		}

		cur_chnl = cur_chnl->next;
	}

	return head;

free_tail:
	vidtv_psi_sdt_service_destroy(tail);
free:
	vidtv_psi_sdt_service_destroy(head);
	return NULL;
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
			if (!tail) {
				vidtv_psi_pat_program_destroy(head);
				return NULL;
			}

			if (!head)
				head = tail;

			curr = curr->next;
		}

		cur_chnl = cur_chnl->next;
	}
	/* Add the NIT table */
	vidtv_psi_pat_program_init(tail, 0, TS_NIT_PID);

	return head;
}

/*
 * Match channels to their respective PMT sections, then assign the
 * streams
 */
static void
vidtv_channel_pmt_match_sections(struct vidtv_channel *channels,
				 struct vidtv_psi_table_pmt **sections,
				 u32 nsections)
{
	struct vidtv_psi_table_pmt *curr_section = NULL;
	struct vidtv_psi_table_pmt_stream *head = NULL;
	struct vidtv_psi_table_pmt_stream *tail = NULL;
	struct vidtv_psi_table_pmt_stream *s = NULL;
	struct vidtv_channel *cur_chnl = channels;
	struct vidtv_psi_desc *desc = NULL;
	u16 e_pid; /* elementary stream pid */
	u16 curr_id;
	u32 j;

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
					vidtv_psi_desc_assign(&tail->descriptor,
							      desc);

					s = s->next;
				}

				vidtv_psi_pmt_stream_assign(curr_section, head);
				break;
			}
		}

		cur_chnl = cur_chnl->next;
	}
}

static void
vidtv_channel_destroy_service_list(struct vidtv_psi_desc_service_list_entry *e)
{
	struct vidtv_psi_desc_service_list_entry *tmp;

	while (e) {
		tmp = e;
		e = e->next;
		kfree(tmp);
	}
}

static struct vidtv_psi_desc_service_list_entry
*vidtv_channel_build_service_list(struct vidtv_psi_table_sdt_service *s)
{
	struct vidtv_psi_desc_service_list_entry *curr_e = NULL;
	struct vidtv_psi_desc_service_list_entry *head_e = NULL;
	struct vidtv_psi_desc_service_list_entry *prev_e = NULL;
	struct vidtv_psi_desc *desc = s->descriptor;
	struct vidtv_psi_desc_service *s_desc;

	while (s) {
		while (desc) {
			if (s->descriptor->type != SERVICE_DESCRIPTOR)
				goto next_desc;

			s_desc = (struct vidtv_psi_desc_service *)desc;

			curr_e = kzalloc(sizeof(*curr_e), GFP_KERNEL);
			if (!curr_e) {
				vidtv_channel_destroy_service_list(head_e);
				return NULL;
			}

			curr_e->service_id = s->service_id;
			curr_e->service_type = s_desc->service_type;

			if (!head_e)
				head_e = curr_e;
			if (prev_e)
				prev_e->next = curr_e;

			prev_e = curr_e;

next_desc:
			desc = desc->next;
		}
		s = s->next;
	}
	return head_e;
}

int vidtv_channel_si_init(struct vidtv_mux *m)
{
	struct vidtv_psi_desc_service_list_entry *service_list = NULL;
	struct vidtv_psi_table_pat_program *programs = NULL;
	struct vidtv_psi_table_sdt_service *services = NULL;
	struct vidtv_psi_table_eit_event *events = NULL;

	m->si.pat = vidtv_psi_pat_table_init(m->transport_stream_id);
	if (!m->si.pat)
		return -ENOMEM;

	m->si.sdt = vidtv_psi_sdt_table_init(m->network_id,
					     m->transport_stream_id);
	if (!m->si.sdt)
		goto free_pat;

	programs = vidtv_channel_pat_prog_cat_into_new(m);
	if (!programs)
		goto free_sdt;
	services = vidtv_channel_sdt_serv_cat_into_new(m);
	if (!services)
		goto free_programs;

	events = vidtv_channel_eit_event_cat_into_new(m);
	if (!events)
		goto free_services;

	/* look for a service descriptor for every service */
	service_list = vidtv_channel_build_service_list(services);
	if (!service_list)
		goto free_events;

	/* use these descriptors to build the NIT */
	m->si.nit = vidtv_psi_nit_table_init(m->network_id,
					     m->transport_stream_id,
					     m->network_name,
					     service_list);
	if (!m->si.nit)
		goto free_service_list;

	m->si.eit = vidtv_psi_eit_table_init(m->network_id,
					     m->transport_stream_id,
					     programs->service_id);
	if (!m->si.eit)
		goto free_nit;

	/* assemble all programs and assign to PAT */
	vidtv_psi_pat_program_assign(m->si.pat, programs);

	/* assemble all services and assign to SDT */
	vidtv_psi_sdt_service_assign(m->si.sdt, services);

	/* assemble all events and assign to EIT */
	vidtv_psi_eit_event_assign(m->si.eit, events);

	m->si.pmt_secs = vidtv_psi_pmt_create_sec_for_each_pat_entry(m->si.pat,
								     m->pcr_pid);
	if (!m->si.pmt_secs)
		goto free_eit;

	vidtv_channel_pmt_match_sections(m->channels,
					 m->si.pmt_secs,
					 m->si.pat->num_pmt);

	vidtv_channel_destroy_service_list(service_list);

	return 0;

free_eit:
	vidtv_psi_eit_table_destroy(m->si.eit);
free_nit:
	vidtv_psi_nit_table_destroy(m->si.nit);
free_service_list:
	vidtv_channel_destroy_service_list(service_list);
free_events:
	vidtv_psi_eit_event_destroy(events);
free_services:
	vidtv_psi_sdt_service_destroy(services);
free_programs:
	vidtv_psi_pat_program_destroy(programs);
free_sdt:
	vidtv_psi_sdt_table_destroy(m->si.sdt);
free_pat:
	vidtv_psi_pat_table_destroy(m->si.pat);
	return 0;
}

void vidtv_channel_si_destroy(struct vidtv_mux *m)
{
	u32 i;

	vidtv_psi_pat_table_destroy(m->si.pat);

	for (i = 0; i < m->si.pat->num_pmt; ++i)
		vidtv_psi_pmt_table_destroy(m->si.pmt_secs[i]);

	kfree(m->si.pmt_secs);
	vidtv_psi_sdt_table_destroy(m->si.sdt);
	vidtv_psi_nit_table_destroy(m->si.nit);
	vidtv_psi_eit_table_destroy(m->si.eit);
}

int vidtv_channels_init(struct vidtv_mux *m)
{
	/* this is the place to add new 'channels' for vidtv */
	m->channels = vidtv_channel_s302m_init(NULL, m->transport_stream_id);

	if (!m->channels)
		return -ENOMEM;

	return 0;
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
		vidtv_psi_eit_event_destroy(curr->events);

		tmp = curr;
		curr = curr->next;
		kfree(tmp);
	}
}
