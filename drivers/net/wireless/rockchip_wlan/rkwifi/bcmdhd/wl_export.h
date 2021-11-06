/*
 * Required functions exported by the port-specific (os-dependent) driver
 * to common (os-independent) driver code.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef _wl_export_h_
#define _wl_export_h_

/* misc callbacks */
struct wl_info;
struct wl_if;
struct wlc_if;
struct wlc_event;
struct wlc_info;
struct wl_timer;
struct wl_rxsts;
struct wl_txsts;
struct reorder_rxcpl_id_list;

/** wl_init() is called upon fault ('big hammer') conditions and as part of a 'wlc up' */
extern void wl_init(struct wl_info *wl);
extern uint wl_reset(struct wl_info *wl);
extern void wl_intrson(struct wl_info *wl);
extern void wl_intrsoff(struct wl_info *wl, bcm_int_bitmask_t *curr_mask);
extern void wl_intrsrestore(struct wl_info *wl, bcm_int_bitmask_t *macintmask);
extern int wl_up(struct wl_info *wl);
extern void wl_down(struct wl_info *wl);
extern void wl_dump_ver(struct wl_info *wl, struct bcmstrbuf *b);
extern void wl_txflowcontrol(struct wl_info *wl, struct wl_if *wlif, bool state, int prio);
extern void wl_set_copycount_bytes(struct wl_info *wl, uint16 copycount,
	uint16 d11rxoffset);
extern int wl_bus_pcie_config_access(struct wl_info *wl, uint32 configaddr, uint32 *configdata,
	bool read);

extern bool wl_alloc_dma_resources(struct wl_info *wl, uint dmaddrwidth);

#ifdef TKO
extern void * wl_get_tko(struct wl_info *wl, struct wl_if *wlif);
#endif	/* TKO */
#ifdef ICMP
extern void * wl_get_icmp(struct wl_info *wl, struct wl_if *wlif);
#endif	/* ICMP */
/* timer functions */
extern struct wl_timer *wl_init_timer(struct wl_info *wl, void (*fn)(void* arg), void *arg,
	const char *name);
extern void wl_free_timer(struct wl_info *wl, struct wl_timer *timer);
/* Add timer guarantees the callback fn will not be called until AT LEAST ms later.  In the
 *  case of a periodic timer, this guarantee is true of consecutive callback fn invocations.
 *  As a result, the period may not average ms duration and the callbacks may "drift".
 *
 * A periodic timer must have a non-zero ms delay.
 */
extern void wl_add_timer(struct wl_info *wl, struct wl_timer *timer, uint ms, int periodic);
extern void wl_add_timer_us(struct wl_info *wl, struct wl_timer *timer, uint us, int periodic);
extern bool wl_del_timer(struct wl_info *wl, struct wl_timer *timer);

#ifdef WLATF_DONGLE
int wlfc_upd_flr_weight(struct wl_info *wl, uint8 mac_handle, uint8 tid, void* params);
int wlfc_enab_fair_fetch_scheduling(struct wl_info *wl, uint32 enab);
int wlfc_get_fair_fetch_scheduling(struct wl_info *wl, uint32 *status);
#endif /* WLATF_DONGLE */

#ifdef MONITOR_DNGL_CONV
extern void wl_sendup_monitor(struct wl_info *wl, void *p);
#endif

/* data receive and interface management functions */
extern void wl_sendup_fp(struct wl_info *wl, struct wl_if *wlif, void *p);
extern void wl_sendup(struct wl_info *wl, struct wl_if *wlif, void *p, int numpkt);
extern void wl_sendup_event(struct wl_info *wl, struct wl_if *wlif, void *pkt);
extern void wl_event(struct wl_info *wl, char *ifname, struct wlc_event *e);
extern void wl_event_sync(struct wl_info *wl, char *ifname, struct wlc_event *e);
extern void wl_event_sendup(struct wl_info *wl, const struct wlc_event *e, uint8 *data, uint32 len);

/* interface manipulation functions */
extern char *wl_ifname(struct wl_info *wl, struct wl_if *wlif);
void wl_set_ifname(struct wl_if *wlif, char *name);
extern struct wl_if *wl_add_if(struct wl_info *wl, struct wlc_if* wlcif, uint unit,
	struct ether_addr *remote);
extern void wl_del_if(struct wl_info *wl, struct wl_if *wlif);
/* RSDB specific interface update function */
void wl_update_wlcif(struct wlc_if *wlcif);
extern void wl_update_if(struct wl_info *from_wl, struct wl_info *to_wl, struct wl_if *from_wlif,
	struct wlc_if *to_wlcif);
int wl_find_if(struct wl_if *wlif);
extern int wl_rebind_if(struct wl_if *wlif, int idx, bool rebind);

/* contexts in wlif structure. Currently following are valid */
#define IFCTX_ARPI	(1)
#define IFCTX_NDI	(2)
#define IFCTX_NETDEV	(3)
extern void *wl_get_ifctx(struct wl_info *wl, int ctx_id, struct wl_if *wlif);

/* pcie root complex operations
	op == 0: get link capability in configuration space
	op == 1: hot reset
*/
extern int wl_osl_pcie_rc(struct wl_info *wl, uint op, int param);

/* monitor mode functions */
#ifndef MONITOR_DNGL_CONV
extern void wl_monitor(struct wl_info *wl, struct wl_rxsts *rxsts, void *p);
#endif
extern void wl_set_monitor(struct wl_info *wl, int val);

#define wl_sort_bsslist(a, b, c) FALSE

#if defined(D0_COALESCING) || defined(WLAWDL)
extern void wl_sendup_no_filter(struct wl_info *wl, struct wl_if *wlif, void *p, int numpkt);
#endif

#ifdef LINUX_CRYPTO
struct wlc_key_info;
extern int wl_tkip_miccheck(struct wl_info *wl, void *p, int hdr_len, bool group_key, int id);
extern int wl_tkip_micadd(struct wl_info *wl, void *p, int hdr_len);
extern int wl_tkip_encrypt(struct wl_info *wl, void *p, int hdr_len);
extern int wl_tkip_decrypt(struct wl_info *wl, void *p, int hdr_len, bool group_key);
extern void wl_tkip_printstats(struct wl_info *wl, bool group_key);
#ifdef BCMINTERNAL
extern int wl_tkip_keydump(struct wl_info *wl, bool group);
#endif /*  BCMINTERNAL */
extern int wl_tkip_keyset(struct wl_info *wl, const struct wlc_key_info *key_info,
	const uint8 *key_data, size_t key_len, const uint8 *rx_seq, size_t rx_seq_len);
#endif /* LINUX_CRYPTO */

#ifdef DONGLEBUILD
/* XXX 156-byte dongle size savings hack (rte version of routine doesn't use names) */
#define wl_init_timer(wl, fn, arg, name)	wl_init_timer(wl, fn, arg, NULL)
extern int wl_busioctl(struct wl_info *wl, uint32 cmd, void *buf, int len, int *used,
	int *needed, int set);
extern void wl_isucodereclaimed(uint8 *value);
extern void wl_reclaim(void);
extern void wl_reclaim_postattach(void);
extern bool wl_dngl_is_ss(struct wl_info *wl);
extern void wl_sendctl_tx(struct wl_info *wl, uint8 type, uint32 op, void *opdata);
extern void wl_flowring_ctl(struct wl_info *wl, uint32 op, void *opdata);
extern void wl_indicate_maccore_state(struct wl_info *wl, uint8 state);
extern void wl_indicate_macwake_state(struct wl_info *wl, uint8 state);
extern void wl_flush_rxreorderqeue_flow(struct wl_info *wl, struct reorder_rxcpl_id_list *list);
extern uint32 wl_chain_rxcomplete_id(struct reorder_rxcpl_id_list *list, uint16 id, bool head);
extern void wl_chain_rxcompletions_amsdu(osl_t *osh, void *p, bool norxcpl);
extern void wl_timesync_add_rx_timestamp(struct wl_info *wl, void *p,
		uint32 ts_low, uint32 ts_high);
extern void wl_timesync_add_tx_timestamp(struct wl_info *wl, void *p,
		uint32 ts_low, uint32 ts_high);
extern void wl_timesync_get_tx_timestamp(struct wl_info *wl, void *p,
		uint32 *ts_low, uint32 *ts_high);

#define wl_chain_rxcomplete_id_tail(a, b) wl_chain_rxcomplete_id(a, b, FALSE)
#define wl_chain_rxcomplete_id_head(a, b) wl_chain_rxcomplete_id(a, b, TRUE)
extern void wl_inform_additional_buffers(struct wl_info *wl, uint16 buf_cnts);
extern void wl_health_check_notify(struct wl_info *wl, mbool notification, bool state);
extern void wl_health_check_notify_clear_all(struct wl_info *wl);
extern void wl_health_check_log(struct wl_info *wl,  uint32 hc_log_type,
	uint32 val, uint32 caller);
#ifdef BCMPCIEDEV
extern bool wl_get_hcapistimesync(void);
extern bool wl_get_hcapispkttxs(void);
#endif /* BCMPCIEDEV */
#else
#define wl_indicate_maccore_state(a, b) do { } while (0)
#define wl_indicate_macwake_state(a, b) do { } while (0)
#define wl_flush_rxreorderqeue_flow(a, b) do { } while (0)
#define wl_chain_rxcomplete_id_tail(a, b) 0
#define wl_chain_rxcomplete_id_head(a, b) 0
#define wl_chain_rxcompletions_amsdu(a, b, c) do {} while (0)
#define wl_inform_additional_buffers(a, b) do { } while (0)
#define wl_health_check_notify(a, b, c) do { } while (0)
#define wl_health_check_notify_clear_all(a) do { } while (0)
#define wl_health_check_log(a, b, c, d) do { } while (0)
#define wl_get_hcapistimesync() do  { } while (0)
#define wl_get_hcapispkttxs() do  { } while (0)
#endif /* DONGLEBUILD */

extern int wl_fatal_error(void * wl, int rc);

#ifdef NEED_HARD_RESET
extern int wl_powercycle(void * wl);
extern bool wl_powercycle_inprogress(void * wl);
#else
#define wl_powercycle(a)
#define wl_powercycle_inprogress(a) (0)
#endif /* NEED_HARD_RESET */

void *wl_create_fwdpkt(struct wl_info *wl, void *p, struct wl_if *wlif);

#ifdef BCMFRWDPOOLREORG
void wl_upd_frwd_resrv_bufcnt(struct wl_info *wl);
#endif /* BCMFRWDPOOLREORG */
#ifdef BCMFRWDPKT
void wl_prepare_frwd_pkt_rxcmplt(struct wl_info *wl, void *p);
#endif /* BCMFRWDPKT */

#ifdef WL_NATOE
void wl_natoe_notify_pktc(struct wl_info *wl, uint8 action);
int wl_natoe_ampdu_config_upd(struct wl_info *wl);
#endif /* WL_NATOE */

#ifdef ENABLE_CORECAPTURE
extern int wl_log_system_state(void * wl, const char * reason, bool capture);
#else
#define wl_log_system_state(a, b, c)
#endif

#define WL_DUMP_MEM_SOCRAM	1
#define WL_DUMP_MEM_UCM		2

extern void wl_dump_mem(char *addr, int len, int type);

#ifdef HEALTH_CHECK
typedef int (*wl_health_check_fn)(uint8 *buffer, uint16 length, void *context,
		int16 *bytes_written);
typedef int (*health_check_event_mask_fn)(void *context, bool get, uint32 *evt_bits);
extern int wl_health_check_evtmask_upd(struct wl_info *wl, int module_id, bool get,
	uint32 *evt_mask);

typedef struct health_check_info health_check_info_t;
typedef struct health_check_client_info health_check_client_info_t;

/* WL wrapper to health check APIs. */
extern health_check_client_info_t* wl_health_check_module_register(struct wl_info *wl,
	const char* name, wl_health_check_fn fn, health_check_event_mask_fn evt_fn,
	void *context, int module_id);

extern void wl_health_check_execute(void *wl);

extern int wl_health_check_execute_clients(struct wl_info *wl,
	health_check_client_info_t** modules, uint16 num_modules);

/* Following are not implemented in dongle health check */
extern int wl_health_check_deinit(struct wl_info *wl);
extern int wl_health_check_module_unregister(struct wl_info *wl,
	health_check_client_info_t *client);
#endif /* HEALTH_CHECK */

#ifdef ECOUNTERS
#define WL_ECOUNTERS_CALLBACK_V2
typedef int (*wl_ecounters_stats_get)(uint16 stats_type, struct wlc_info *wlc,
	const ecounters_stats_types_report_req_t * req, struct bcm_xtlvbuf *xtlvbuf,
	uint32 *cookie, const bcm_xtlv_t* tlv, uint16 *attempted_write_len);

extern int wl_ecounters_register_source(struct wl_info *wl, uint16 stats_type,
	wl_ecounters_stats_get some_fn);
extern int wl_ecounters_register_source_periodic(struct wl_info *wl, uint16 stats_type,
	wl_ecounters_stats_get periodic_fn, wl_ecounters_stats_get some_fn);

extern int wl_ecounters_trigger(void *trigger_context, uint16 reason);
#endif
extern bool wl_health_check_enabled(struct wl_info *wl);
typedef void (*wl_send_if_event_cb_fn_t)(void *ctx);
extern int wl_if_event_send_cb_fn_register(struct wl_info *wl, wl_send_if_event_cb_fn_t fn,
	void *arg);
extern int wl_if_event_send_cb_fn_unregister(struct wl_info *wl,
	wl_send_if_event_cb_fn_t fn, void *arg);
extern uint32 wl_get_ramsize(void);
#ifdef PACKET_FILTER
extern void wl_periodic_pktfltr_cntrs_state_upd(wlc_info_t *wlc);
#endif /* PACKET_FILTER */

#ifdef BCMPCIE_LATENCY
int wl_bus_pcie_latency_enab(struct wl_info *wl, bool val);
#endif /* BCMPCIE_LATENCY */
void wl_hp2p_update_prio(struct wl_info *wl, void *p);
#endif	/* _wl_export_h_ */
