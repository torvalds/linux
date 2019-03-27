/*
 * Copyright (C) 2009-2012  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

#ifndef ISCAPI_NAMESPACE_H
#define ISCAPI_NAMESPACE_H 1

/*%
 * name space conversions
 */

#ifdef BIND9

#define isc_app_start isc__app_start
#define isc_app_ctxstart isc__app_ctxstart
#define isc_app_onrun isc__app_onrun
#define isc_app_run isc__app_run
#define isc_app_ctxrun isc__app_ctxrun
#define isc_app_shutdown isc__app_shutdown
#define isc_app_ctxfinish isc__app_ctxfinish
#define isc_app_ctxshutdown isc__app_ctxshutdown
#define isc_app_ctxsuspend isc__app_ctxsuspend
#define isc_app_reload isc__app_reload
#define isc_app_finish isc__app_finish
#define isc_app_block isc__app_block
#define isc_app_unblock isc__app_unblock
#define isc_appctx_create isc__appctx_create
#define isc_appctx_destroy isc__appctx_destroy
#define isc_appctx_settaskmgr isc__appctx_settaskmgr
#define isc_appctx_setsocketmgr isc__appctx_setsocketmgr
#define isc_appctx_settimermgr isc__appctx_settimermgr

#define isc_mem_checkdestroyed isc__mem_checkdestroyed
#define isc_mem_createx isc__mem_createx
#define isc_mem_createx2 isc__mem_createx2
#define isc_mem_create isc__mem_create
#define isc_mem_create2 isc__mem_create2
#define isc_mem_attach isc__mem_attach
#define isc_mem_detach isc__mem_detach
#define isc__mem_putanddetach isc___mem_putanddetach
#define isc_mem_destroy isc__mem_destroy
#define isc_mem_ondestroy isc__mem_ondestroy
#define isc__mem_get isc___mem_get
#define isc__mem_put isc___mem_put
#define isc_mem_stats isc__mem_stats
#define isc__mem_allocate isc___mem_allocate
#define isc__mem_free isc___mem_free
#define isc__mem_strdup isc___mem_strdup
#define isc__mem_reallocate isc___mem_reallocate
#define isc_mem_references isc__mem_references
#define isc_mem_setdestroycheck isc__mem_setdestroycheck
#define isc_mem_setquota isc__mem_setquota
#define isc_mem_getname isc__mem_getname
#define isc_mem_getquota isc__mem_getquota
#define isc_mem_gettag isc__mem_gettag
#define isc_mem_inuse isc__mem_inuse
#define isc_mem_isovermem isc__mem_isovermem
#define isc_mem_setname isc__mem_setname
#define isc_mem_setwater isc__mem_setwater
#define isc_mem_printallactive isc__mem_printallactive
#define isc_mem_waterack isc__mem_waterack
#define isc_mempool_create isc__mempool_create
#define isc_mempool_setname isc__mempool_setname
#define isc_mempool_destroy isc__mempool_destroy
#define isc_mempool_associatelock isc__mempool_associatelock
#define isc__mempool_get isc___mempool_get
#define isc__mempool_put isc___mempool_put
#define isc_mempool_setfreemax isc__mempool_setfreemax
#define isc_mempool_getfreemax isc__mempool_getfreemax
#define isc_mempool_getfreecount isc__mempool_getfreecount
#define isc_mempool_setmaxalloc isc__mempool_setmaxalloc
#define isc_mempool_getmaxalloc isc__mempool_getmaxalloc
#define isc_mempool_getallocated isc__mempool_getallocated
#define isc_mempool_setfillcount isc__mempool_setfillcount
#define isc_mempool_getfillcount isc__mempool_getfillcount

#define isc_socket_create isc__socket_create
#define isc_socket_dup isc__socket_dup
#define isc_socket_attach isc__socket_attach
#define isc_socket_detach isc__socket_detach
#define isc_socketmgr_create isc__socketmgr_create
#define isc_socketmgr_create2 isc__socketmgr_create2
#define isc_socketmgr_destroy isc__socketmgr_destroy
#define isc_socket_open isc__socket_open
#define isc_socket_close isc__socket_close
#define isc_socket_recvv isc__socket_recvv
#define isc_socket_recv isc__socket_recv
#define isc_socket_recv2 isc__socket_recv2
#define isc_socket_send isc__socket_send
#define isc_socket_sendto isc__socket_sendto
#define isc_socket_sendv isc__socket_sendv
#define isc_socket_sendtov isc__socket_sendtov
#define isc_socket_sendto2 isc__socket_sendto2
#define isc_socket_cleanunix isc__socket_cleanunix
#define isc_socket_permunix isc__socket_permunix
#define isc_socket_bind isc__socket_bind
#define isc_socket_filter isc__socket_filter
#define isc_socket_listen isc__socket_listen
#define isc_socket_accept isc__socket_accept
#define isc_socket_connect isc__socket_connect
#define isc_socket_getfd isc__socket_getfd
#define isc_socket_getname isc__socket_getname
#define isc_socket_gettag isc__socket_gettag
#define isc_socket_getpeername isc__socket_getpeername
#define isc_socket_getsockname isc__socket_getsockname
#define isc_socket_cancel isc__socket_cancel
#define isc_socket_gettype isc__socket_gettype
#define isc_socket_isbound isc__socket_isbound
#define isc_socket_ipv6only isc__socket_ipv6only
#define isc_socket_setname isc__socket_setname
#define isc_socketmgr_getmaxsockets isc__socketmgr_getmaxsockets
#define isc_socketmgr_setstats isc__socketmgr_setstats
#define isc_socketmgr_setreserved isc__socketmgr_setreserved
#define isc__socketmgr_maxudp isc___socketmgr_maxudp
#define isc_socket_fdwatchcreate isc__socket_fdwatchcreate
#define isc_socket_fdwatchpoke isc__socket_fdwatchpoke

#define isc_task_create isc__task_create
#define isc_task_attach isc__task_attach
#define isc_task_detach isc__task_detach
/* #define isc_task_exiting isc__task_exiting XXXMPA */
#define isc_task_send isc__task_send
#define isc_task_sendanddetach isc__task_sendanddetach
#define isc_task_purgerange isc__task_purgerange
#define isc_task_purge isc__task_purge
#define isc_task_purgeevent isc__task_purgeevent
#define isc_task_unsendrange isc__task_unsendrange
#define isc_task_unsend isc__task_unsend
#define isc_task_onshutdown isc__task_onshutdown
#define isc_task_shutdown isc__task_shutdown
#define isc_task_destroy isc__task_destroy
#define isc_task_setname isc__task_setname
#define isc_task_getname isc__task_getname
#define isc_task_gettag isc__task_gettag
#define isc_task_getcurrenttime isc__task_getcurrenttime
#define isc_taskmgr_create isc__taskmgr_create
#define isc_taskmgr_setmode isc__taskmgr_setmode
#define isc_taskmgr_mode isc__taskmgr_mode
#define isc_taskmgr_destroy isc__taskmgr_destroy
#define isc_task_beginexclusive isc__task_beginexclusive
#define isc_task_endexclusive isc__task_endexclusive
#define isc_task_setprivilege isc__task_setprivilege
#define isc_task_privilege isc__task_privilege

#define isc_timer_create isc__timer_create
#define isc_timer_reset isc__timer_reset
#define isc_timer_gettype isc__timer_gettype
#define isc_timer_touch isc__timer_touch
#define isc_timer_attach isc__timer_attach
#define isc_timer_detach isc__timer_detach
#define isc_timermgr_create isc__timermgr_create
#define isc_timermgr_poke isc__timermgr_poke
#define isc_timermgr_destroy isc__timermgr_destroy

#endif /* BIND9 */

#endif /* ISCAPI_NAMESPACE_H */
