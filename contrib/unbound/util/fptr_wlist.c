/*
 * util/fptr_wlist.c - function pointer whitelists.
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
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains functions that check function pointers.
 * The functions contain a whitelist of known good callback values.
 * Any other values lead to an error. 
 *
 * Due to the listing nature, this file violates all the modularization
 * boundaries in the program.
 */
#include "config.h"
#include "util/fptr_wlist.h"
#include "util/mini_event.h"
#include "services/outside_network.h"
#include "services/mesh.h"
#include "services/localzone.h"
#include "services/authzone.h"
#include "services/cache/infra.h"
#include "services/cache/rrset.h"
#include "services/view.h"
#include "dns64/dns64.h"
#include "iterator/iterator.h"
#include "iterator/iter_fwd.h"
#include "validator/validator.h"
#include "validator/val_anchor.h"
#include "validator/val_nsec3.h"
#include "validator/val_sigcrypt.h"
#include "validator/val_kentry.h"
#include "validator/val_neg.h"
#include "validator/autotrust.h"
#include "util/data/msgreply.h"
#include "util/data/packed_rrset.h"
#include "util/storage/slabhash.h"
#include "util/storage/dnstree.h"
#include "util/locks.h"
#include "libunbound/libworker.h"
#include "libunbound/context.h"
#include "libunbound/worker.h"
#include "util/tube.h"
#include "util/config_file.h"
#ifdef UB_ON_WINDOWS
#include "winrc/win_svc.h"
#endif
#include "respip/respip.h"

#ifdef WITH_PYTHONMODULE
#include "pythonmod/pythonmod.h"
#endif
#ifdef USE_CACHEDB
#include "cachedb/cachedb.h"
#endif
#ifdef USE_IPSECMOD
#include "ipsecmod/ipsecmod.h"
#endif
#ifdef CLIENT_SUBNET
#include "edns-subnet/subnetmod.h"
#endif

int 
fptr_whitelist_comm_point(comm_point_callback_type *fptr)
{
	if(fptr == &worker_handle_request) return 1;
	else if(fptr == &outnet_udp_cb) return 1;
	else if(fptr == &outnet_tcp_cb) return 1;
	else if(fptr == &tube_handle_listen) return 1;
	else if(fptr == &auth_xfer_probe_udp_callback) return 1;
	else if(fptr == &auth_xfer_transfer_tcp_callback) return 1;
	else if(fptr == &auth_xfer_transfer_http_callback) return 1;
	return 0;
}

int 
fptr_whitelist_comm_point_raw(comm_point_callback_type *fptr)
{
	if(fptr == &tube_handle_listen) return 1;
	else if(fptr == &tube_handle_write) return 1;
	else if(fptr == &remote_accept_callback) return 1;
	else if(fptr == &remote_control_callback) return 1;
	return 0;
}

int 
fptr_whitelist_comm_timer(void (*fptr)(void*))
{
	if(fptr == &pending_udp_timer_cb) return 1;
	else if(fptr == &outnet_tcptimer) return 1;
	else if(fptr == &pending_udp_timer_delay_cb) return 1;
	else if(fptr == &worker_stat_timer_cb) return 1;
	else if(fptr == &worker_probe_timer_cb) return 1;
#ifdef UB_ON_WINDOWS
	else if(fptr == &wsvc_cron_cb) return 1;
#endif
	else if(fptr == &auth_xfer_timer) return 1;
	else if(fptr == &auth_xfer_probe_timer_callback) return 1;
	return 0;
}

int 
fptr_whitelist_comm_signal(void (*fptr)(int, void*))
{
	if(fptr == &worker_sighandler) return 1;
	return 0;
}

int fptr_whitelist_start_accept(void (*fptr)(void*))
{
	if(fptr == &worker_start_accept) return 1;
	return 0;
}

int fptr_whitelist_stop_accept(void (*fptr)(void*))
{
	if(fptr == &worker_stop_accept) return 1;
	return 0;
}

int 
fptr_whitelist_event(void (*fptr)(int, short, void *))
{
	if(fptr == &comm_point_udp_callback) return 1;
	else if(fptr == &comm_point_udp_ancil_callback) return 1;
	else if(fptr == &comm_point_tcp_accept_callback) return 1;
	else if(fptr == &comm_point_tcp_handle_callback) return 1;
	else if(fptr == &comm_timer_callback) return 1;
	else if(fptr == &comm_signal_callback) return 1;
	else if(fptr == &comm_point_local_handle_callback) return 1;
	else if(fptr == &comm_point_raw_handle_callback) return 1;
	else if(fptr == &tube_handle_signal) return 1;
	else if(fptr == &comm_base_handle_slow_accept) return 1;
	else if(fptr == &comm_point_http_handle_callback) return 1;
#ifdef UB_ON_WINDOWS
	else if(fptr == &worker_win_stop_cb) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_pending_udp(comm_point_callback_type *fptr)
{
	if(fptr == &serviced_udp_callback) return 1;
	else if(fptr == &worker_handle_reply) return 1;
	else if(fptr == &libworker_handle_reply) return 1;
	return 0;
}

int 
fptr_whitelist_pending_tcp(comm_point_callback_type *fptr)
{
	if(fptr == &serviced_tcp_callback) return 1;
	else if(fptr == &worker_handle_reply) return 1;
	else if(fptr == &libworker_handle_reply) return 1;
	return 0;
}

int 
fptr_whitelist_serviced_query(comm_point_callback_type *fptr)
{
	if(fptr == &worker_handle_service_reply) return 1;
	else if(fptr == &libworker_handle_service_reply) return 1;
	return 0;
}

int 
fptr_whitelist_rbtree_cmp(int (*fptr) (const void *, const void *))
{
	if(fptr == &mesh_state_compare) return 1;
	else if(fptr == &mesh_state_ref_compare) return 1;
	else if(fptr == &addr_tree_compare) return 1;
	else if(fptr == &local_zone_cmp) return 1;
	else if(fptr == &local_data_cmp) return 1;
	else if(fptr == &fwd_cmp) return 1;
	else if(fptr == &pending_cmp) return 1;
	else if(fptr == &serviced_cmp) return 1;
	else if(fptr == &name_tree_compare) return 1;
	else if(fptr == &order_lock_cmp) return 1;
	else if(fptr == &codeline_cmp) return 1;
	else if(fptr == &nsec3_hash_cmp) return 1;
	else if(fptr == &mini_ev_cmp) return 1;
	else if(fptr == &anchor_cmp) return 1;
	else if(fptr == &canonical_tree_compare) return 1;
	else if(fptr == &context_query_cmp) return 1;
	else if(fptr == &val_neg_data_compare) return 1;
	else if(fptr == &val_neg_zone_compare) return 1;
	else if(fptr == &probetree_cmp) return 1;
	else if(fptr == &replay_var_compare) return 1;
	else if(fptr == &view_cmp) return 1;
	else if(fptr == &auth_zone_cmp) return 1;
	else if(fptr == &auth_data_cmp) return 1;
	else if(fptr == &auth_xfer_cmp) return 1;
	return 0;
}

int 
fptr_whitelist_hash_sizefunc(lruhash_sizefunc_type fptr)
{
	if(fptr == &msgreply_sizefunc) return 1;
	else if(fptr == &ub_rrset_sizefunc) return 1;
	else if(fptr == &infra_sizefunc) return 1;
	else if(fptr == &key_entry_sizefunc) return 1;
	else if(fptr == &rate_sizefunc) return 1;
	else if(fptr == &ip_rate_sizefunc) return 1;
	else if(fptr == &test_slabhash_sizefunc) return 1;
#ifdef CLIENT_SUBNET
	else if(fptr == &msg_cache_sizefunc) return 1;
#endif
#ifdef USE_DNSCRYPT
	else if(fptr == &dnsc_shared_secrets_sizefunc) return 1;
	else if(fptr == &dnsc_nonces_sizefunc) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_hash_compfunc(lruhash_compfunc_type fptr)
{
	if(fptr == &query_info_compare) return 1;
	else if(fptr == &ub_rrset_compare) return 1;
	else if(fptr == &infra_compfunc) return 1;
	else if(fptr == &key_entry_compfunc) return 1;
	else if(fptr == &rate_compfunc) return 1;
	else if(fptr == &ip_rate_compfunc) return 1;
	else if(fptr == &test_slabhash_compfunc) return 1;
#ifdef USE_DNSCRYPT
	else if(fptr == &dnsc_shared_secrets_compfunc) return 1;
	else if(fptr == &dnsc_nonces_compfunc) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_hash_delkeyfunc(lruhash_delkeyfunc_type fptr)
{
	if(fptr == &query_entry_delete) return 1;
	else if(fptr == &ub_rrset_key_delete) return 1;
	else if(fptr == &infra_delkeyfunc) return 1;
	else if(fptr == &key_entry_delkeyfunc) return 1;
	else if(fptr == &rate_delkeyfunc) return 1;
	else if(fptr == &ip_rate_delkeyfunc) return 1;
	else if(fptr == &test_slabhash_delkey) return 1;
#ifdef USE_DNSCRYPT
	else if(fptr == &dnsc_shared_secrets_delkeyfunc) return 1;
	else if(fptr == &dnsc_nonces_delkeyfunc) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_hash_deldatafunc(lruhash_deldatafunc_type fptr)
{
	if(fptr == &reply_info_delete) return 1;
	else if(fptr == &rrset_data_delete) return 1;
	else if(fptr == &infra_deldatafunc) return 1;
	else if(fptr == &key_entry_deldatafunc) return 1;
	else if(fptr == &rate_deldatafunc) return 1;
	else if(fptr == &test_slabhash_deldata) return 1;
#ifdef CLIENT_SUBNET
	else if(fptr == &subnet_data_delete) return 1;
#endif
#ifdef USE_DNSCRYPT
	else if(fptr == &dnsc_shared_secrets_deldatafunc) return 1;
	else if(fptr == &dnsc_nonces_deldatafunc) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_hash_markdelfunc(lruhash_markdelfunc_type fptr)
{
	if(fptr == NULL) return 1;
	else if(fptr == &rrset_markdel) return 1;
	return 0;
}

/** whitelist env->send_query callbacks */
int 
fptr_whitelist_modenv_send_query(struct outbound_entry* (*fptr)(
	struct query_info* qinfo, uint16_t flags, int dnssec, int want_dnssec,
	int nocaps, struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t* zone, size_t zonelen, int ssl_upstream, char* tls_auth_name,
	struct module_qstate* q))
{
	if(fptr == &worker_send_query) return 1;
	else if(fptr == &libworker_send_query) return 1;
	return 0;
}

int 
fptr_whitelist_modenv_detach_subs(void (*fptr)(
        struct module_qstate* qstate))
{
	if(fptr == &mesh_detach_subs) return 1;
	return 0;
}

int 
fptr_whitelist_modenv_attach_sub(int (*fptr)(
        struct module_qstate* qstate, struct query_info* qinfo,
        uint16_t qflags, int prime, int valrec, struct module_qstate** newq))
{
	if(fptr == &mesh_attach_sub) return 1;
	return 0;
}

int 
fptr_whitelist_modenv_add_sub(int (*fptr)(
        struct module_qstate* qstate, struct query_info* qinfo,
        uint16_t qflags, int prime, int valrec, struct module_qstate** newq,
	struct mesh_state** sub))
{
	if(fptr == &mesh_add_sub) return 1;
	return 0;
}

int 
fptr_whitelist_modenv_kill_sub(void (*fptr)(struct module_qstate* newq))
{
	if(fptr == &mesh_state_delete) return 1;
	return 0;
}

int 
fptr_whitelist_modenv_detect_cycle(int (*fptr)(        
	struct module_qstate* qstate, struct query_info* qinfo,         
	uint16_t flags, int prime, int valrec))
{
	if(fptr == &mesh_detect_cycle) return 1;
	return 0;
}

int 
fptr_whitelist_mod_init(int (*fptr)(struct module_env* env, int id))
{
	if(fptr == &iter_init) return 1;
	else if(fptr == &val_init) return 1;
	else if(fptr == &dns64_init) return 1;
	else if(fptr == &respip_init) return 1;
#ifdef WITH_PYTHONMODULE
	else if(fptr == &pythonmod_init) return 1;
#endif
#ifdef USE_CACHEDB
	else if(fptr == &cachedb_init) return 1;
#endif
#ifdef USE_IPSECMOD
	else if(fptr == &ipsecmod_init) return 1;
#endif
#ifdef CLIENT_SUBNET
	else if(fptr == &subnetmod_init) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_mod_deinit(void (*fptr)(struct module_env* env, int id))
{
	if(fptr == &iter_deinit) return 1;
	else if(fptr == &val_deinit) return 1;
	else if(fptr == &dns64_deinit) return 1;
	else if(fptr == &respip_deinit) return 1;
#ifdef WITH_PYTHONMODULE
	else if(fptr == &pythonmod_deinit) return 1;
#endif
#ifdef USE_CACHEDB
	else if(fptr == &cachedb_deinit) return 1;
#endif
#ifdef USE_IPSECMOD
	else if(fptr == &ipsecmod_deinit) return 1;
#endif
#ifdef CLIENT_SUBNET
	else if(fptr == &subnetmod_deinit) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_mod_operate(void (*fptr)(struct module_qstate* qstate,
        enum module_ev event, int id, struct outbound_entry* outbound))
{
	if(fptr == &iter_operate) return 1;
	else if(fptr == &val_operate) return 1;
	else if(fptr == &dns64_operate) return 1;
	else if(fptr == &respip_operate) return 1;
#ifdef WITH_PYTHONMODULE
	else if(fptr == &pythonmod_operate) return 1;
#endif
#ifdef USE_CACHEDB
	else if(fptr == &cachedb_operate) return 1;
#endif
#ifdef USE_IPSECMOD
	else if(fptr == &ipsecmod_operate) return 1;
#endif
#ifdef CLIENT_SUBNET
	else if(fptr == &subnetmod_operate) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_mod_inform_super(void (*fptr)(
        struct module_qstate* qstate, int id, struct module_qstate* super))
{
	if(fptr == &iter_inform_super) return 1;
	else if(fptr == &val_inform_super) return 1;
	else if(fptr == &dns64_inform_super) return 1;
	else if(fptr == &respip_inform_super) return 1;
#ifdef WITH_PYTHONMODULE
	else if(fptr == &pythonmod_inform_super) return 1;
#endif
#ifdef USE_CACHEDB
	else if(fptr == &cachedb_inform_super) return 1;
#endif
#ifdef USE_IPSECMOD
	else if(fptr == &ipsecmod_inform_super) return 1;
#endif
#ifdef CLIENT_SUBNET
	else if(fptr == &subnetmod_inform_super) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_mod_clear(void (*fptr)(struct module_qstate* qstate,
        int id))
{
	if(fptr == &iter_clear) return 1;
	else if(fptr == &val_clear) return 1;
	else if(fptr == &dns64_clear) return 1;
	else if(fptr == &respip_clear) return 1;
#ifdef WITH_PYTHONMODULE
	else if(fptr == &pythonmod_clear) return 1;
#endif
#ifdef USE_CACHEDB
	else if(fptr == &cachedb_clear) return 1;
#endif
#ifdef USE_IPSECMOD
	else if(fptr == &ipsecmod_clear) return 1;
#endif
#ifdef CLIENT_SUBNET
	else if(fptr == &subnetmod_clear) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_mod_get_mem(size_t (*fptr)(struct module_env* env, int id))
{
	if(fptr == &iter_get_mem) return 1;
	else if(fptr == &val_get_mem) return 1;
	else if(fptr == &dns64_get_mem) return 1;
	else if(fptr == &respip_get_mem) return 1;
#ifdef WITH_PYTHONMODULE
	else if(fptr == &pythonmod_get_mem) return 1;
#endif
#ifdef USE_CACHEDB
	else if(fptr == &cachedb_get_mem) return 1;
#endif
#ifdef USE_IPSECMOD
	else if(fptr == &ipsecmod_get_mem) return 1;
#endif
#ifdef CLIENT_SUBNET
	else if(fptr == &subnetmod_get_mem) return 1;
#endif
	return 0;
}

int 
fptr_whitelist_alloc_cleanup(void (*fptr)(void*))
{
	if(fptr == &worker_alloc_cleanup) return 1;
	return 0;
}

int fptr_whitelist_tube_listen(tube_callback_type* fptr)
{
	if(fptr == &worker_handle_control_cmd) return 1;
	else if(fptr == &libworker_handle_control_cmd) return 1;
	return 0;
}

int fptr_whitelist_mesh_cb(mesh_cb_func_type fptr)
{
	if(fptr == &libworker_fg_done_cb) return 1;
	else if(fptr == &libworker_bg_done_cb) return 1;
	else if(fptr == &libworker_event_done_cb) return 1;
	else if(fptr == &probe_answer_cb) return 1;
	else if(fptr == &auth_xfer_probe_lookup_callback) return 1;
	else if(fptr == &auth_xfer_transfer_lookup_callback) return 1;
	return 0;
}

int fptr_whitelist_print_func(void (*fptr)(char*,void*))
{
	if(fptr == &config_print_func) return 1;
	else if(fptr == &config_collate_func) return 1;
	else if(fptr == &remote_get_opt_ssl) return 1;
	return 0;
}

int fptr_whitelist_inplace_cb_reply_generic(inplace_cb_reply_func_type* fptr,
	enum inplace_cb_list_type type)
{
#ifndef WITH_PYTHONMODULE
	(void)fptr;
#endif
	if(type == inplace_cb_reply) {
#ifdef WITH_PYTHONMODULE
		if(fptr == &python_inplace_cb_reply_generic) return 1;
#endif
	} else if(type == inplace_cb_reply_cache) {
#ifdef WITH_PYTHONMODULE
		if(fptr == &python_inplace_cb_reply_generic) return 1;
#endif
	} else if(type == inplace_cb_reply_local) {
#ifdef WITH_PYTHONMODULE
		if(fptr == &python_inplace_cb_reply_generic) return 1;
#endif
	} else if(type == inplace_cb_reply_servfail) {
#ifdef WITH_PYTHONMODULE
		if(fptr == &python_inplace_cb_reply_generic) return 1;
#endif
	}
	return 0;
}

int fptr_whitelist_inplace_cb_query(inplace_cb_query_func_type* fptr)
{
#ifdef CLIENT_SUBNET
	if(fptr == &ecs_whitelist_check)
		return 1;
#else
	(void)fptr;
#endif
	return 0;
}

int fptr_whitelist_inplace_cb_edns_back_parsed(
	inplace_cb_edns_back_parsed_func_type* fptr)
{
#ifdef CLIENT_SUBNET
	if(fptr == &ecs_edns_back_parsed)
		return 1;
#else
	(void)fptr;
#endif
	return 0;
}

int fptr_whitelist_inplace_cb_query_response(
	inplace_cb_query_response_func_type* fptr)
{
#ifdef CLIENT_SUBNET
	if(fptr == &ecs_query_response)
		return 1;
#else
	(void)fptr;
#endif
	return 0;
}
