/*
 * libunbound/worker.h - prototypes for worker methods.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file declares the methods any worker has to implement.
 */

#ifndef LIBUNBOUND_WORKER_H
#define LIBUNBOUND_WORKER_H

#include "sldns/sbuffer.h"
#include "util/data/packed_rrset.h" /* for enum sec_status */
struct comm_reply;
struct comm_point;
struct module_qstate;
struct tube;
struct edns_option;
struct query_info;

/**
 * Worker service routine to send serviced queries to authoritative servers.
 * @param qinfo: query info.
 * @param flags: host order flags word, with opcode and CD bit.
 * @param dnssec: if set, EDNS record will have DO bit set.
 * @param want_dnssec: signatures needed.
 * @param nocaps: ignore capsforid(if in config), do not perturb qname.
 * @param addr: where to.
 * @param addrlen: length of addr.
 * @param zone: delegation point name.
 * @param zonelen: length of zone name wireformat dname.
 * @param ssl_upstream: use SSL for upstream queries.
 * @param tls_auth_name: if ssl_upstream, use this name with TLS
 * 	authentication.
 * @param q: wich query state to reactivate upon return.
 * @return: false on failure (memory or socket related). no query was
 *      sent.
 */
struct outbound_entry* libworker_send_query(struct query_info* qinfo,
	uint16_t flags, int dnssec, int want_dnssec, int nocaps,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* zone,
	size_t zonelen, int ssl_upstream, char* tls_auth_name,
	struct module_qstate* q);

/** process incoming replies from the network */
int libworker_handle_reply(struct comm_point* c, void* arg, int error,
        struct comm_reply* reply_info);

/** process incoming serviced query replies from the network */
int libworker_handle_service_reply(struct comm_point* c, void* arg, int error,
        struct comm_reply* reply_info);

/** handle control command coming into server */
void libworker_handle_control_cmd(struct tube* tube, uint8_t* msg, size_t len,
	int err, void* arg);

/** mesh callback with fg results */
void libworker_fg_done_cb(void* arg, int rcode, sldns_buffer* buf, 
	enum sec_status s, char* why_bogus, int was_ratelimited);

/** mesh callback with bg results */
void libworker_bg_done_cb(void* arg, int rcode, sldns_buffer* buf, 
	enum sec_status s, char* why_bogus, int was_ratelimited);

/** mesh callback with event results */
void libworker_event_done_cb(void* arg, int rcode, struct sldns_buffer* buf, 
	enum sec_status s, char* why_bogus, int was_ratelimited);

/**
 * Worker signal handler function. User argument is the worker itself.
 * @param sig: signal number.
 * @param arg: the worker (main worker) that handles signals.
 */
void worker_sighandler(int sig, void* arg);

/**
 * Worker service routine to send serviced queries to authoritative servers.
 * @param qinfo: query info.
 * @param flags: host order flags word, with opcode and CD bit.
 * @param dnssec: if set, EDNS record will have DO bit set.
 * @param want_dnssec: signatures needed.
 * @param nocaps: ignore capsforid(if in config), do not perturb qname.
 * @param addr: where to.
 * @param addrlen: length of addr.
 * @param zone: wireformat dname of the zone.
 * @param zonelen: length of zone name.
 * @param ssl_upstream: use SSL for upstream queries.
 * @param tls_auth_name: if ssl_upstream, use this name with TLS
 * 	authentication.
 * @param q: wich query state to reactivate upon return.
 * @return: false on failure (memory or socket related). no query was
 *      sent.
 */
struct outbound_entry* worker_send_query(struct query_info* qinfo,
	uint16_t flags, int dnssec, int want_dnssec, int nocaps,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* zone,
	size_t zonelen, int ssl_upstream, char* tls_auth_name,
	struct module_qstate* q);

/** 
 * process control messages from the main thread. Frees the control 
 * command message.
 * @param tube: tube control message came on.
 * @param msg: message contents.  Is freed.
 * @param len: length of message.
 * @param error: if error (NETEVENT_*) happened.
 * @param arg: user argument
 */
void worker_handle_control_cmd(struct tube* tube, uint8_t* msg, size_t len,
	int error, void* arg);

/** handles callbacks from listening event interface */
int worker_handle_request(struct comm_point* c, void* arg, int error,
	struct comm_reply* repinfo);

/** process incoming replies from the network */
int worker_handle_reply(struct comm_point* c, void* arg, int error, 
	struct comm_reply* reply_info);

/** process incoming serviced query replies from the network */
int worker_handle_service_reply(struct comm_point* c, void* arg, int error, 
	struct comm_reply* reply_info);

/** cleanup the cache to remove all rrset IDs from it, arg is worker */
void worker_alloc_cleanup(void* arg);

/** statistics timer callback handler */
void worker_stat_timer_cb(void* arg);

/** probe timer callback handler */
void worker_probe_timer_cb(void* arg);

/** start accept callback handler */
void worker_start_accept(void* arg);

/** stop accept callback handler */
void worker_stop_accept(void* arg);

/** handle remote control accept callbacks */
int remote_accept_callback(struct comm_point*, void*, int, struct comm_reply*);

/** handle remote control data callbacks */
int remote_control_callback(struct comm_point*, void*, int, struct comm_reply*);

/** routine to printout option values over SSL */
void  remote_get_opt_ssl(char* line, void* arg);

#endif /* LIBUNBOUND_WORKER_H */
