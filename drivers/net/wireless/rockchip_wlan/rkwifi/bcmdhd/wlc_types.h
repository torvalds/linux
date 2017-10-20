/*
 * Forward declarations for commonly used wl driver structs
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: wlc_types.h 665242 2016-10-17 05:59:26Z $
 */

#ifndef _wlc_types_h_
#define _wlc_types_h_
#include <wlioctl.h>

/* Version of WLC interface to be returned as a part of wl_wlc_version structure.
 * WLC_API_VERSION_MINOR is currently not in use.
 */
#define WLC_API_VERSION_MAJOR      8
#define WLC_API_VERSION_MINOR      0

/* forward declarations */

typedef struct wlc_info wlc_info_t;
typedef struct wlcband wlcband_t;
typedef struct wlc_cmn_info wlc_cmn_info_t;
typedef struct wlc_assoc_info wlc_assoc_info_t;
typedef struct wlc_pm_info wlc_pm_info_t;

typedef struct wlc_bsscfg wlc_bsscfg_t;
typedef struct wlc_mbss_info wlc_mbss_info_t;
typedef struct wlc_spt wlc_spt_t;
typedef struct scb scb_t;
typedef struct scb_iter scb_iter_t;
typedef struct vndr_ie_listel vndr_ie_listel_t;
typedef struct wlc_if wlc_if_t;
typedef struct wl_if wl_if_t;
typedef struct led_info led_info_t;
typedef struct bmac_led bmac_led_t;
typedef struct bmac_led_info bmac_led_info_t;
typedef struct seq_cmds_info wlc_seq_cmds_info_t;
typedef struct ota_test_info ota_test_info_t;
typedef struct wlc_ccx ccx_t;
typedef struct wlc_ccx_rm ccx_rm_t;
typedef struct apps_wlc_psinfo apps_wlc_psinfo_t;
typedef struct scb_module scb_module_t;
typedef struct ba_info ba_info_t;
typedef struct wlc_frminfo wlc_frminfo_t;
typedef struct amsdu_info amsdu_info_t;
typedef struct txq_info txq_info_t;
typedef struct txq txq_t;
typedef struct cram_info cram_info_t;
typedef struct wlc_extlog_info wlc_extlog_info_t;
typedef struct wlc_txq_info wlc_txq_info_t;
typedef struct wlc_hrt_info wlc_hrt_info_t;
typedef struct wlc_hrt_to wlc_hrt_to_t;
typedef struct wlc_cac wlc_cac_t;
typedef struct ampdu_tx_info ampdu_tx_info_t;
typedef struct ampdu_rx_info ampdu_rx_info_t;
typedef struct wlc_ratesel_info wlc_ratesel_info_t;
typedef struct ratesel_info ratesel_info_t;
typedef struct wlc_ap_info wlc_ap_info_t;
typedef struct cs_info cs_info_t;
typedef struct wlc_scan_info wlc_scan_info_t;
typedef struct wlc_scan_cmn_info wlc_scan_cmn_t;
typedef struct tdls_info tdls_info_t;
typedef struct dls_info dls_info_t;
typedef struct l2_filter_info l2_filter_info_t;
typedef struct wlc_auth_info wlc_auth_info_t;
typedef struct wlc_sup_info wlc_sup_info_t;
typedef struct wlc_fbt_info wlc_fbt_info_t;
typedef struct wlc_assoc_mgr_info wlc_assoc_mgr_info_t;
typedef struct wlc_ccxsup_info wlc_ccxsup_info_t;
typedef struct wlc_psta_info wlc_psta_info_t;
typedef struct wlc_mcnx_info wlc_mcnx_info_t;
typedef struct wlc_p2p_info wlc_p2p_info_t;
typedef struct wlc_cxnoa_info wlc_cxnoa_info_t;
typedef struct mchan_info mchan_info_t;
typedef struct wlc_mchan_context wlc_mchan_context_t;
typedef struct bta_info bta_info_t;
typedef struct wowl_info wowl_info_t;
typedef struct wowlpf_info wowlpf_info_t;
typedef struct wlc_plt_info wlc_plt_pub_t;
typedef struct antsel_info antsel_info_t;
typedef struct bmac_pmq bmac_pmq_t;
typedef struct wmf_info wmf_info_t;
typedef struct wlc_rrm_info wlc_rrm_info_t;
typedef struct rm_info rm_info_t;

struct d11init;

typedef struct wlc_dpc_info wlc_dpc_info_t;

typedef struct wlc_11h_info wlc_11h_info_t;
typedef struct wlc_tpc_info wlc_tpc_info_t;
typedef struct wlc_csa_info wlc_csa_info_t;
typedef struct wlc_quiet_info wlc_quiet_info_t;
typedef struct cca_info cca_info_t;
typedef struct itfr_info itfr_info_t;

typedef struct wlc_wnm_info wlc_wnm_info_t;
typedef struct wlc_11d_info wlc_11d_info_t;
typedef struct wlc_cntry_info wlc_cntry_info_t;

typedef struct wlc_dfs_info wlc_dfs_info_t;

typedef struct bsscfg_module bsscfg_module_t;

typedef struct wlc_prot_info wlc_prot_info_t;
typedef struct wlc_prot_g_info wlc_prot_g_info_t;
typedef struct wlc_prot_n_info wlc_prot_n_info_t;
typedef struct wlc_prot_obss_info wlc_prot_obss_info_t;
typedef struct wlc_obss_dynbw wlc_obss_dynbw_t;
typedef struct wlc_11u_info wlc_11u_info_t;
typedef struct wlc_probresp_info wlc_probresp_info_t;
typedef struct wlc_wapi_info wlc_wapi_info_t;

typedef struct wlc_tbtt_info wlc_tbtt_info_t;
typedef struct wlc_nic_info wlc_nic_info_t;

typedef struct wlc_bssload_info wlc_bssload_info_t;

typedef struct wlc_pcb_info wlc_pcb_info_t;
typedef struct wlc_txc_info wlc_txc_info_t;

typedef struct wlc_trf_mgmt_ctxt    wlc_trf_mgmt_ctxt_t;
typedef struct wlc_trf_mgmt_info    wlc_trf_mgmt_info_t;

typedef struct wlc_net_detect_ctxt  wlc_net_detect_ctxt_t;

typedef struct wlc_powersel_info wlc_powersel_info_t;
typedef struct powersel_info powersel_info_t;

typedef struct wlc_lpc_info wlc_lpc_info_t;
typedef struct lpc_info lpc_info_t;
typedef struct rate_lcb_info rate_lcb_info_t;
typedef struct wlc_txbf_info wlc_txbf_info_t;
typedef struct wlc_murx_info wlc_murx_info_t;

typedef struct wlc_olpc_eng_info_t wlc_olpc_eng_info_t;
/* used by olpc to register for callbacks from stf */
typedef void (*wlc_stf_txchain_evt_notify)(wlc_info_t *wlc);

typedef struct wlc_rfc wlc_rfc_t;
typedef struct wlc_pktc_info wlc_pktc_info_t;

typedef struct wlc_mfp_info wlc_mfp_info_t;

typedef struct wlc_mdns_info wlc_mdns_info_t;

typedef struct wlc_macfltr_info wlc_macfltr_info_t;
typedef struct wlc_bmon_info wlc_bmon_info_t;

typedef struct wlc_nar_info wlc_nar_info_t;
typedef struct wlc_bs_data_info wlc_bs_data_info_t;

typedef struct wlc_keymgmt wlc_keymgmt_t;
typedef struct wlc_key	wlc_key_t;
typedef struct wlc_key_info wlc_key_info_t;

typedef struct wlc_hw wlc_hw_t;
typedef struct wlc_hw_info wlc_hw_info_t;
typedef struct wlc_hwband wlc_hwband_t;

typedef struct wlc_rx_stall_info wlc_rx_stall_info_t;

typedef struct wlc_rmc_info wlc_rmc_info_t;

typedef struct wlc_iem_info wlc_iem_info_t;

typedef struct wlc_ier_info wlc_ier_info_t;
typedef struct wlc_ier_reg wlc_ier_reg_t;

typedef struct wlc_ht_info wlc_ht_info_t;
typedef struct wlc_obss_info wlc_obss_info_t;
typedef struct wlc_vht_info wlc_vht_info_t;
typedef struct wlc_akm_info wlc_akm_info_t;
typedef struct wlc_srvsdb_info wlc_srvsdb_info_t;

typedef struct wlc_bss_info wlc_bss_info_t;

typedef struct wlc_hs20_info wlc_hs20_info_t;
typedef struct wlc_pmkid_info	wlc_pmkid_info_t;
typedef struct wlc_btc_info wlc_btc_info_t;

typedef struct wlc_txh_info wlc_txh_info_t;
typedef union wlc_txd wlc_txd_t;

typedef struct wlc_staprio_info wlc_staprio_info_t;
typedef struct wlc_stamon_info wlc_stamon_info_t;
typedef struct wlc_monitor_info wlc_monitor_info_t;

typedef struct wlc_debug_crash_info wlc_debug_crash_info_t;

typedef struct wlc_nan_info wlc_nan_info_t;
typedef struct wlc_tsmap_info wlc_tsmap_info_t;

typedef struct wlc_wds_info wlc_wds_info_t;
typedef struct okc_info okc_info_t;
typedef struct wlc_aibss_info wlc_aibss_info_t;
typedef struct wlc_ipfo_info wlc_ipfo_info_t;
typedef struct wlc_stats_info wlc_stats_info_t;

typedef struct wlc_pps_info wlc_pps_info_t;

typedef struct duration_info duration_info_t;

typedef struct wlc_pdsvc_info wlc_pdsvc_info_t;

/* For LTE Coex */
typedef struct wlc_ltecx_info wlc_ltecx_info_t;

typedef struct wlc_probresp_mac_filter_info wlc_probresp_mac_filter_info_t;

typedef struct wlc_ltr_info wlc_ltr_info_t;

typedef struct bwte_info bwte_info_t;

typedef struct tbow_info tbow_info_t;

typedef struct wlc_modesw_info wlc_modesw_info_t;

typedef struct wlc_pm_mute_tx_info wlc_pm_mute_tx_t;

typedef struct wlc_bcntrim_info wlc_bcntrim_info_t;

typedef struct wlc_smfs_info wlc_smfs_info_t;
typedef struct wlc_misc_info wlc_misc_info_t;
typedef struct wlc_ulb_info wlc_ulb_info_t;

typedef struct wlc_eventq wlc_eventq_t;
typedef struct wlc_event wlc_event_t;
typedef struct wlc_ulp_info wlc_ulp_info_t;

typedef struct wlc_bsscfg_psq_info wlc_bsscfg_psq_info_t;
typedef struct wlc_bsscfg_viel_info wlc_bsscfg_viel_info_t;

typedef struct wlc_txmod_info wlc_txmod_info_t;
typedef struct tx_path_node tx_path_node_t;

typedef struct wlc_linkstats_info wlc_linkstats_info_t;
typedef struct wlc_lq_info wlc_lq_info_t;
typedef struct chanim_info chanim_info_t;

typedef struct wlc_mesh_info wlc_mesh_info_t;
typedef struct wlc_wlfc_info wlc_wlfc_info_t;

typedef struct wlc_frag_info wlc_frag_info_t;
typedef struct wlc_bss_list wlc_bss_list_t;

typedef struct wlc_msch_info wlc_msch_info_t;
typedef struct wlc_msch_req_handle wlc_msch_req_handle_t;

typedef struct wlc_randmac_info wlc_randmac_info_t;

typedef struct wlc_chanctxt wlc_chanctxt_t;
typedef struct wlc_chanctxt_info wlc_chanctxt_info_t;
typedef struct wlc_sta_info wlc_sta_info_t;

typedef struct health_check_info health_check_info_t;
typedef struct wlc_act_frame_info wlc_act_frame_info_t;
typedef struct nan_sched_req_handle nan_sched_req_handle_t;

typedef struct wlc_qos_info wlc_qos_info_t;

typedef struct wlc_assoc wlc_assoc_t;
typedef struct wlc_roam wlc_roam_t;
typedef struct wlc_pm_st wlc_pm_st_t;
typedef struct wlc_wme wlc_wme_t;

typedef struct wlc_link_qual wlc_link_qual_t;

typedef struct wlc_rsdb_info wlc_rsdb_info_t;

typedef struct wlc_asdb wlc_asdb_t;

typedef struct rsdb_common_info rsdb_cmn_info_t;

typedef struct wlc_macdbg_info wlc_macdbg_info_t;
typedef struct wlc_rspec_info wlc_rspec_info_t;
typedef struct wlc_ndis_info wlc_ndis_info_t;

typedef struct wlc_join_pref wlc_join_pref_t;

typedef struct wlc_scan_utils wlc_scan_utils_t;
#ifdef ACKSUPR_MAC_FILTER
typedef struct wlc_addrmatch_info wlc_addrmatch_info_t;
#endif /* ACKSUPR_MAC_FILTER */

typedef struct cca_ucode_counts cca_ucode_counts_t;
typedef struct cca_chan_qual cca_chan_qual_t;

typedef struct wlc_perf_utils wlc_perf_utils_t;
typedef struct wlc_test_info wlc_test_info_t;

typedef struct chanswitch_times chanswitch_times_t;
typedef struct wlc_dump_info wlc_dump_info_t;

typedef struct wlc_stf wlc_stf_t;

typedef sta_info_v4_t sta_info_t;
typedef struct wl_roam_prof_band_v2 wl_roam_prof_band_t;
typedef struct wl_roam_prof_v2 wl_roam_prof_t;

/* Inteface version mapping for versioned pfn structures */
#undef PFN_SCANRESULT_VERSION
#define PFN_SCANRESULT_VERSION PFN_SCANRESULT_VERSION_V2
#define PFN_SCANRESULTS_VERSION PFN_SCANRESULTS_VERSION_V2
#define PFN_LBEST_SCAN_RESULT_VERSION PFN_LBEST_SCAN_RESULT_VERSION_V2
typedef wl_pfn_subnet_info_v2_t wl_pfn_subnet_info_t;
typedef wl_pfn_net_info_v2_t wl_pfn_net_info_t;
typedef wl_pfn_lnet_info_v2_t wl_pfn_lnet_info_t;
typedef wl_pfn_lscanresults_v2_t wl_pfn_lscanresults_t;
typedef wl_pfn_scanresults_v2_t wl_pfn_scanresults_t;
typedef wl_pfn_scanresult_v2_t wl_pfn_scanresult_t;

typedef wl_dfs_ap_move_status_v2_t wl_dfs_ap_move_status_t;

#endif	/* _wlc_types_h_ */
