/* dnstap support for Unbound */

/*
 * Copyright (c) 2013-2014, Farsight Security, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dnstap/dnstap_config.h"

#ifdef USE_DNSTAP

#include "config.h"
#include <string.h>
#include <sys/time.h>
#include "sldns/sbuffer.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "util/netevent.h"
#include "util/log.h"

#include <fstrm.h>
#include <protobuf-c/protobuf-c.h>

#include "dnstap/dnstap.h"
#include "dnstap/dnstap.pb-c.h"

#define DNSTAP_CONTENT_TYPE		"protobuf:dnstap.Dnstap"
#define DNSTAP_INITIAL_BUF_SIZE		256

struct dt_msg {
	void		*buf;
	size_t		len_buf;
	Dnstap__Dnstap	d;
	Dnstap__Message	m;
};

static int
dt_pack(const Dnstap__Dnstap *d, void **buf, size_t *sz)
{
	ProtobufCBufferSimple sbuf;

	memset(&sbuf, 0, sizeof(sbuf));
	sbuf.base.append = protobuf_c_buffer_simple_append;
	sbuf.len = 0;
	sbuf.alloced = DNSTAP_INITIAL_BUF_SIZE;
	sbuf.data = malloc(sbuf.alloced);
	if (sbuf.data == NULL)
		return 0;
	sbuf.must_free_data = 1;

	*sz = dnstap__dnstap__pack_to_buffer(d, (ProtobufCBuffer *) &sbuf);
	if (sbuf.data == NULL)
		return 0;
	*buf = sbuf.data;

	return 1;
}

static void
dt_send(const struct dt_env *env, void *buf, size_t len_buf)
{
	fstrm_res res;
	if (!buf)
		return;
	res = fstrm_iothr_submit(env->iothr, env->ioq, buf, len_buf,
				 fstrm_free_wrapper, NULL);
	if (res != fstrm_res_success)
		free(buf);
}

static void
dt_msg_init(const struct dt_env *env,
	    struct dt_msg *dm,
	    Dnstap__Message__Type mtype)
{
	memset(dm, 0, sizeof(*dm));
	dm->d.base.descriptor = &dnstap__dnstap__descriptor;
	dm->m.base.descriptor = &dnstap__message__descriptor;
	dm->d.type = DNSTAP__DNSTAP__TYPE__MESSAGE;
	dm->d.message = &dm->m;
	dm->m.type = mtype;
	if (env->identity != NULL) {
		dm->d.identity.data = (uint8_t *) env->identity;
		dm->d.identity.len = (size_t) env->len_identity;
		dm->d.has_identity = 1;
	}
	if (env->version != NULL) {
		dm->d.version.data = (uint8_t *) env->version;
		dm->d.version.len = (size_t) env->len_version;
		dm->d.has_version = 1;
	}
}

struct dt_env *
dt_create(const char *socket_path, unsigned num_workers)
{
#ifdef UNBOUND_DEBUG
	fstrm_res res;
#endif
	struct dt_env *env;
	struct fstrm_iothr_options *fopt;
	struct fstrm_unix_writer_options *fuwopt;
	struct fstrm_writer *fw;
	struct fstrm_writer_options *fwopt;

	verbose(VERB_OPS, "attempting to connect to dnstap socket %s",
		socket_path);
	log_assert(socket_path != NULL);
	log_assert(num_workers > 0);

	env = (struct dt_env *) calloc(1, sizeof(struct dt_env));
	if (!env)
		return NULL;

	fwopt = fstrm_writer_options_init();
#ifdef UNBOUND_DEBUG
	res = 
#else
	(void)
#endif
	    fstrm_writer_options_add_content_type(fwopt,
		DNSTAP_CONTENT_TYPE, sizeof(DNSTAP_CONTENT_TYPE) - 1);
	log_assert(res == fstrm_res_success);

	fuwopt = fstrm_unix_writer_options_init();
	fstrm_unix_writer_options_set_socket_path(fuwopt, socket_path);

	fw = fstrm_unix_writer_init(fuwopt, fwopt);
	log_assert(fw != NULL);

	fopt = fstrm_iothr_options_init();
	fstrm_iothr_options_set_num_input_queues(fopt, num_workers);
	env->iothr = fstrm_iothr_init(fopt, &fw);
	if (env->iothr == NULL) {
		verbose(VERB_DETAIL, "dt_create: fstrm_iothr_init() failed");
		fstrm_writer_destroy(&fw);
		free(env);
		env = NULL;
	}
	fstrm_iothr_options_destroy(&fopt);
	fstrm_unix_writer_options_destroy(&fuwopt);
	fstrm_writer_options_destroy(&fwopt);

	return env;
}

static void
dt_apply_identity(struct dt_env *env, struct config_file *cfg)
{
	char buf[MAXHOSTNAMELEN+1];
	if (!cfg->dnstap_send_identity)
		return;
	free(env->identity);
	if (cfg->dnstap_identity == NULL || cfg->dnstap_identity[0] == 0) {
		if (gethostname(buf, MAXHOSTNAMELEN) == 0) {
			buf[MAXHOSTNAMELEN] = 0;
			env->identity = strdup(buf);
		} else {
			fatal_exit("dt_apply_identity: gethostname() failed");
		}
	} else {
		env->identity = strdup(cfg->dnstap_identity);
	}
	if (env->identity == NULL)
		fatal_exit("dt_apply_identity: strdup() failed");
	env->len_identity = (unsigned int)strlen(env->identity);
	verbose(VERB_OPS, "dnstap identity field set to \"%s\"",
		env->identity);
}

static void
dt_apply_version(struct dt_env *env, struct config_file *cfg)
{
	if (!cfg->dnstap_send_version)
		return;
	free(env->version);
	if (cfg->dnstap_version == NULL || cfg->dnstap_version[0] == 0)
		env->version = strdup(PACKAGE_STRING);
	else
		env->version = strdup(cfg->dnstap_version);
	if (env->version == NULL)
		fatal_exit("dt_apply_version: strdup() failed");
	env->len_version = (unsigned int)strlen(env->version);
	verbose(VERB_OPS, "dnstap version field set to \"%s\"",
		env->version);
}

void
dt_apply_cfg(struct dt_env *env, struct config_file *cfg)
{
	if (!cfg->dnstap)
		return;

	dt_apply_identity(env, cfg);
	dt_apply_version(env, cfg);
	if ((env->log_resolver_query_messages = (unsigned int)
	     cfg->dnstap_log_resolver_query_messages))
	{
		verbose(VERB_OPS, "dnstap Message/RESOLVER_QUERY enabled");
	}
	if ((env->log_resolver_response_messages = (unsigned int)
	     cfg->dnstap_log_resolver_response_messages))
	{
		verbose(VERB_OPS, "dnstap Message/RESOLVER_RESPONSE enabled");
	}
	if ((env->log_client_query_messages = (unsigned int)
	     cfg->dnstap_log_client_query_messages))
	{
		verbose(VERB_OPS, "dnstap Message/CLIENT_QUERY enabled");
	}
	if ((env->log_client_response_messages = (unsigned int)
	     cfg->dnstap_log_client_response_messages))
	{
		verbose(VERB_OPS, "dnstap Message/CLIENT_RESPONSE enabled");
	}
	if ((env->log_forwarder_query_messages = (unsigned int)
	     cfg->dnstap_log_forwarder_query_messages))
	{
		verbose(VERB_OPS, "dnstap Message/FORWARDER_QUERY enabled");
	}
	if ((env->log_forwarder_response_messages = (unsigned int)
	     cfg->dnstap_log_forwarder_response_messages))
	{
		verbose(VERB_OPS, "dnstap Message/FORWARDER_RESPONSE enabled");
	}
}

int
dt_init(struct dt_env *env)
{
	env->ioq = fstrm_iothr_get_input_queue(env->iothr);
	if (env->ioq == NULL)
		return 0;
	return 1;
}

void
dt_delete(struct dt_env *env)
{
	if (!env)
		return;
	verbose(VERB_OPS, "closing dnstap socket");
	fstrm_iothr_destroy(&env->iothr);
	free(env->identity);
	free(env->version);
	free(env);
}

static void
dt_fill_timeval(const struct timeval *tv,
		uint64_t *time_sec, protobuf_c_boolean *has_time_sec,
		uint32_t *time_nsec, protobuf_c_boolean *has_time_nsec)
{
#ifndef S_SPLINT_S
	*time_sec = tv->tv_sec;
	*time_nsec = tv->tv_usec * 1000;
#endif
	*has_time_sec = 1;
	*has_time_nsec = 1;
}

static void
dt_fill_buffer(sldns_buffer *b, ProtobufCBinaryData *p, protobuf_c_boolean *has)
{
	log_assert(b != NULL);
	p->len = sldns_buffer_limit(b);
	p->data = sldns_buffer_begin(b);
	*has = 1;
}

static void
dt_msg_fill_net(struct dt_msg *dm,
		struct sockaddr_storage *ss,
		enum comm_point_type cptype,
		ProtobufCBinaryData *addr, protobuf_c_boolean *has_addr,
		uint32_t *port, protobuf_c_boolean *has_port)
{
	log_assert(ss->ss_family == AF_INET6 || ss->ss_family == AF_INET);
	if (ss->ss_family == AF_INET6) {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *) ss;

		/* socket_family */
		dm->m.socket_family = DNSTAP__SOCKET_FAMILY__INET6;
		dm->m.has_socket_family = 1;

		/* addr: query_address or response_address */
		addr->data = s->sin6_addr.s6_addr;
		addr->len = 16; /* IPv6 */
		*has_addr = 1;

		/* port: query_port or response_port */
		*port = ntohs(s->sin6_port);
		*has_port = 1;
	} else if (ss->ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *) ss;

		/* socket_family */
		dm->m.socket_family = DNSTAP__SOCKET_FAMILY__INET;
		dm->m.has_socket_family = 1;

		/* addr: query_address or response_address */
		addr->data = (uint8_t *) &s->sin_addr.s_addr;
		addr->len = 4; /* IPv4 */
		*has_addr = 1;

		/* port: query_port or response_port */
		*port = ntohs(s->sin_port);
		*has_port = 1;
	}

	log_assert(cptype == comm_udp || cptype == comm_tcp);
	if (cptype == comm_udp) {
		/* socket_protocol */
		dm->m.socket_protocol = DNSTAP__SOCKET_PROTOCOL__UDP;
		dm->m.has_socket_protocol = 1;
	} else if (cptype == comm_tcp) {
		/* socket_protocol */
		dm->m.socket_protocol = DNSTAP__SOCKET_PROTOCOL__TCP;
		dm->m.has_socket_protocol = 1;
	}
}

void
dt_msg_send_client_query(struct dt_env *env,
			 struct sockaddr_storage *qsock,
			 enum comm_point_type cptype,
			 sldns_buffer *qmsg)
{
	struct dt_msg dm;
	struct timeval qtime;

	gettimeofday(&qtime, NULL);

	/* type */
	dt_msg_init(env, &dm, DNSTAP__MESSAGE__TYPE__CLIENT_QUERY);

	/* query_time */
	dt_fill_timeval(&qtime,
			&dm.m.query_time_sec, &dm.m.has_query_time_sec,
			&dm.m.query_time_nsec, &dm.m.has_query_time_nsec);

	/* query_message */
	dt_fill_buffer(qmsg, &dm.m.query_message, &dm.m.has_query_message);

	/* socket_family, socket_protocol, query_address, query_port */
	log_assert(cptype == comm_udp || cptype == comm_tcp);
	dt_msg_fill_net(&dm, qsock, cptype,
			&dm.m.query_address, &dm.m.has_query_address,
			&dm.m.query_port, &dm.m.has_query_port);

	if (dt_pack(&dm.d, &dm.buf, &dm.len_buf))
		dt_send(env, dm.buf, dm.len_buf);
}

void
dt_msg_send_client_response(struct dt_env *env,
			    struct sockaddr_storage *qsock,
			    enum comm_point_type cptype,
			    sldns_buffer *rmsg)
{
	struct dt_msg dm;
	struct timeval rtime;

	gettimeofday(&rtime, NULL);

	/* type */
	dt_msg_init(env, &dm, DNSTAP__MESSAGE__TYPE__CLIENT_RESPONSE);

	/* response_time */
	dt_fill_timeval(&rtime,
			&dm.m.response_time_sec, &dm.m.has_response_time_sec,
			&dm.m.response_time_nsec, &dm.m.has_response_time_nsec);

	/* response_message */
	dt_fill_buffer(rmsg, &dm.m.response_message, &dm.m.has_response_message);

	/* socket_family, socket_protocol, query_address, query_port */
	log_assert(cptype == comm_udp || cptype == comm_tcp);
	dt_msg_fill_net(&dm, qsock, cptype,
			&dm.m.query_address, &dm.m.has_query_address,
			&dm.m.query_port, &dm.m.has_query_port);

	if (dt_pack(&dm.d, &dm.buf, &dm.len_buf))
		dt_send(env, dm.buf, dm.len_buf);
}

void
dt_msg_send_outside_query(struct dt_env *env,
			  struct sockaddr_storage *rsock,
			  enum comm_point_type cptype,
			  uint8_t *zone, size_t zone_len,
			  sldns_buffer *qmsg)
{
	struct dt_msg dm;
	struct timeval qtime;
	uint16_t qflags;

	gettimeofday(&qtime, NULL);
	qflags = sldns_buffer_read_u16_at(qmsg, 2);

	/* type */
	if (qflags & BIT_RD) {
		if (!env->log_forwarder_query_messages)
			return;
		dt_msg_init(env, &dm, DNSTAP__MESSAGE__TYPE__FORWARDER_QUERY);
	} else {
		if (!env->log_resolver_query_messages)
			return;
		dt_msg_init(env, &dm, DNSTAP__MESSAGE__TYPE__RESOLVER_QUERY);
	}

	/* query_zone */
	dm.m.query_zone.data = zone;
	dm.m.query_zone.len = zone_len;
	dm.m.has_query_zone = 1;

	/* query_time_sec, query_time_nsec */
	dt_fill_timeval(&qtime,
			&dm.m.query_time_sec, &dm.m.has_query_time_sec,
			&dm.m.query_time_nsec, &dm.m.has_query_time_nsec);

	/* query_message */
	dt_fill_buffer(qmsg, &dm.m.query_message, &dm.m.has_query_message);

	/* socket_family, socket_protocol, response_address, response_port */
	log_assert(cptype == comm_udp || cptype == comm_tcp);
	dt_msg_fill_net(&dm, rsock, cptype,
			&dm.m.response_address, &dm.m.has_response_address,
			&dm.m.response_port, &dm.m.has_response_port);

	if (dt_pack(&dm.d, &dm.buf, &dm.len_buf))
		dt_send(env, dm.buf, dm.len_buf);
}

void
dt_msg_send_outside_response(struct dt_env *env,
			     struct sockaddr_storage *rsock,
			     enum comm_point_type cptype,
			     uint8_t *zone, size_t zone_len,
			     uint8_t *qbuf, size_t qbuf_len,
			     const struct timeval *qtime,
			     const struct timeval *rtime,
			     sldns_buffer *rmsg)
{
	struct dt_msg dm;
	uint16_t qflags;

	log_assert(qbuf_len >= sizeof(qflags));
	memcpy(&qflags, qbuf, sizeof(qflags));
	qflags = ntohs(qflags);

	/* type */
	if (qflags & BIT_RD) {
		if (!env->log_forwarder_response_messages)
			return;
		dt_msg_init(env, &dm, DNSTAP__MESSAGE__TYPE__FORWARDER_RESPONSE);
	} else {
		if (!env->log_resolver_response_messages)
			return;
		dt_msg_init(env, &dm, DNSTAP__MESSAGE__TYPE__RESOLVER_RESPONSE);
	}

	/* query_zone */
	dm.m.query_zone.data = zone;
	dm.m.query_zone.len = zone_len;
	dm.m.has_query_zone = 1;

	/* query_time_sec, query_time_nsec */
	dt_fill_timeval(qtime,
			&dm.m.query_time_sec, &dm.m.has_query_time_sec,
			&dm.m.query_time_nsec, &dm.m.has_query_time_nsec);

	/* response_time_sec, response_time_nsec */
	dt_fill_timeval(rtime,
			&dm.m.response_time_sec, &dm.m.has_response_time_sec,
			&dm.m.response_time_nsec, &dm.m.has_response_time_nsec);

	/* response_message */
	dt_fill_buffer(rmsg, &dm.m.response_message, &dm.m.has_response_message);

	/* socket_family, socket_protocol, response_address, response_port */
	log_assert(cptype == comm_udp || cptype == comm_tcp);
	dt_msg_fill_net(&dm, rsock, cptype,
			&dm.m.response_address, &dm.m.has_response_address,
			&dm.m.response_port, &dm.m.has_response_port);

	if (dt_pack(&dm.d, &dm.buf, &dm.len_buf))
		dt_send(env, dm.buf, dm.len_buf);
}

#endif /* USE_DNSTAP */
