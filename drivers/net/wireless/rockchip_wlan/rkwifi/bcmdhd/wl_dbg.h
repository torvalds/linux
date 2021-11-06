/*
 * Minimal debug/trace/assert driver definitions for
 * Broadcom 802.11 Networking Adapter.
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

/* XXX, some old "wl msglevel" for PHY module has been moved to phy on 6/10/2009 "wl phymsglevel"
 * They are spare in TOT and can be reused if needed. see src/wl/phy/wlc_phy_int.h
 */

#ifndef _wl_dbg_h_
#define _wl_dbg_h_

#include <event_log.h>

/* wl_msg_level is a bit vector with defs in wlioctl.h */
extern uint32 wl_msg_level;
extern uint32 wl_msg_level2;
extern uint32 wl_msg_level3;

#if defined (BCMDBG) && \
!defined(BCMDONGLEHOST) && !defined(BCMDBG_EXCLUDE_HW_TIMESTAMP)
extern char* wlc_dbg_get_hw_timestamp(void);

#define WL_TIMESTAMP() 		do { if (wl_msg_level2 & WL_TIMESTAMP_VAL) {\
	                            printf(wlc_dbg_get_hw_timestamp()); }\
	                        } while (0)
#else
#define WL_TIMESTAMP()
#endif /* BCMDBG && !BCMDONGLEHOST && !BCMDBG_EXCLUDE_HW_TIMESTAMP */

#ifdef ENABLE_CORECAPTURE
#define MAX_BACKTRACE_DEPTH 32
extern int wl_print_backtrace(const char * prefix, void * i_backtrace, int i_backtrace_depth);
#else
#define wl_print_backtrace(a, b, c)
#endif /* ENABLE_CORECAPTURE */

#define WIFICC_CAPTURE(_reason)
#define WIFICC_LOGDEBUGIF(_flags, _args)
#define WIFICC_LOGDEBUG(_args)

#define WL_PRINT(args)		do { WL_TIMESTAMP(); printf args; } while (0)

#ifdef BCM_UPTIME_PROFILE
#define WL_PROF(args)		WL_PRINT(args)
#else
#define WL_PROF(args)
#endif /* BCM_UPTIME_PROFILE */

#if defined(ERR_USE_EVENT_LOG) && defined(EVENT_LOG_COMPILE)
#define EVENT_LOG_PRSRV_DUMP()	EVENT_LOG_PRSRV_FLUSH()
#else
#define EVENT_LOG_PRSRV_DUMP()
#endif /* ERR_USE_EVENT_LOG && EVENT_LOG_COMPILE */

#if defined(BCMDBG)

/* DBGONLY() macro to reduce ifdefs in code for statements that are only needed when
 * BCMDBG is defined.
 * Ex.
 * myfn() {
 *     int i;
 *     DBGONLY(int dbg; )
 */
#define DBGONLY(x) x

#define WL_WARN(x)	WL_ERROR(x)

/* To disable a message completely ... until you need it again */
#define	WL_NONE(args)		do {if (wl_msg_level & 0) WL_PRINT(args);} while (0)

#define	WL_ERROR(args)		do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args);} while (0)

#define WL_IE_ERROR(args)	do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)

#define WL_AMSDU_ERROR(args)	do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)

#define WL_ASSOC_ERROR(args)	do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)

#define WL_SCAN_ERROR(args)	do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)
#define KM_ERR(args)		do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)

#define WL_WBTEXT_ERROR(args)	do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)

#define WL_MBO_ERR(args)	do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)

#define WL_RANDMAC_ERR(args)        do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)

#define	WL_TRACE(args)		do {if (wl_msg_level & WL_TRACE_VAL) WL_PRINT(args);} while (0)

#define WL_PFN_ERROR(args)	do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)

#define	WL_PRHDRS_MSG(args)	do {if (wl_msg_level & WL_PRHDRS_VAL) WL_PRINT(args);} while (0)
#define WL_PRHDRS(i, p, f, t, r, l)	do { \
						if (wl_msg_level & WL_PRHDRS_VAL) \
							wlc_print_hdrs(i, p, f, t, r, l); \
					} while (0)
#define	WL_PRPKT(m, b, n)	do {if (wl_msg_level & WL_PRPKT_VAL) prhex(m, b, n);} while (0)
#define	WL_INFORM(args)		do {if (wl_msg_level & WL_INFORM_VAL) WL_PRINT(args);} while (0)
#define	WL_TMP(args)		do {if (wl_msg_level & WL_TMP_VAL) WL_PRINT(args);} while (0)
#define	WL_OID(args)		do {if (wl_msg_level & WL_OID_VAL) WL_PRINT(args);} while (0)
#define	WL_RATE(args)		do {if (wl_msg_level & WL_RATE_VAL) WL_PRINT(args);} while (0)
#define WL_ASSOC(args)		do {if (wl_msg_level & WL_ASSOC_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args);} while (0)
#define	WL_PRUSR(m, b, n)	do {if (wl_msg_level & WL_PRUSR_VAL) prhex(m, b, n);} while (0)
#define WL_PS(args)		do {if (wl_msg_level & WL_PS_VAL) WL_PRINT(args);} while (0)
#define WL_SPARE1(args)		do {if (wl_msg_level & WL_TXPWR_VAL) WL_PRINT(args);} while (0)
#define WL_PORT(args)		do {if (wl_msg_level & WL_PORT_VAL) WL_PRINT(args);} while (0)
#define WL_DUAL(args)		do {if (wl_msg_level & WL_DUAL_VAL) WL_PRINT(args);} while (0)
#define WL_WSEC(args)		do {if (wl_msg_level & WL_WSEC_VAL) WL_PRINT(args);} while (0)
#define WL_WSEC_DUMP(args)	do {if (wl_msg_level & WL_WSEC_DUMP_VAL) WL_PRINT(args);} while (0)
#define WL_SPARE2(args)		do {if (wl_msg_level & WL_NRSSI_VAL) WL_PRINT(args);} while (0)
#define WL_SPARE3(args)		do {if (wl_msg_level & WL_LOFT_VAL) WL_PRINT(args);} while (0)
#define WL_REGULATORY(args)	do {if (wl_msg_level & WL_REGULATORY_VAL) WL_PRINT(args);} while (0)
#define WL_SPARE4(args)		do {if (wl_msg_level & WL_PHYCAL_VAL) WL_PRINT(args);} while (0)
#define WL_WDI(args)		do {if (wl_msg_level & WL_WDI_VAL) WL_PRINT(args);} while (0)
#define WL_MPC(args)		do {if (wl_msg_level & WL_MPC_VAL) WL_PRINT(args);} while (0)
#define WL_APSTA(args)		do {if (wl_msg_level & WL_APSTA_VAL) WL_PRINT(args);} while (0)
#define WL_DFS(args)		do {if (wl_msg_level & WL_DFS_VAL) WL_PRINT(args);} while (0)
#define WL_MUMIMO(args)		do {if (wl_msg_level & WL_MUMIMO_VAL) WL_PRINT(args);} while (0)
#define WL_MODE_SWITCH(args) do {if (wl_msg_level & WL_MODE_SWITCH_VAL) WL_PRINT(args);} while (0)
#define WL_BCNTRIM_DBG(args) do {if (wl_msg_level & WL_BCNTRIM_VAL) WL_PRINT(args);} while (0)
#define WL_MBSS(args)		do {if (wl_msg_level & WL_MBSS_VAL) WL_PRINT(args);} while (0)
#define WL_CAC(args)		do {if (wl_msg_level & WL_CAC_VAL) WL_PRINT(args);} while (0)
#define WL_AMSDU(args)		do {if (wl_msg_level & WL_AMSDU_VAL) WL_PRINT(args);} while (0)
#define WL_AMPDU(args)		do {if (wl_msg_level & WL_AMPDU_VAL) WL_PRINT(args);} while (0)
#define WL_FFPLD(args)		do {if (wl_msg_level & WL_FFPLD_VAL) WL_PRINT(args);} while (0)
#define WL_PFN(args)		do {if (wl_msg_level & WL_PFN_VAL) WL_PRINT(args);} while (0)
/* wl_msg_level is full. Use wl_msg_level_2 now */
#define WL_WOWL(args)		do {if (wl_msg_level2 & WL_WOWL_VAL) WL_PRINT(args);} while (0)
#define WL_SCAN(args)		do {if (wl_msg_level2 & WL_SCAN_VAL) WL_PRINT(args);} while (0)
#define WL_SCAN_WARN(args)	do {if (wl_msg_level2 & WL_SCAN_VAL) WL_PRINT(args);} while (0)
#define	WL_COEX(args)		do {if (wl_msg_level2 & WL_COEX_VAL) WL_PRINT(args);} while (0)
#define WL_RTDC(w,s,i,j)	do {if (wl_msg_level2 & WL_RTDC_VAL) wlc_log(w,s,i,j);} while (0)
#define	WL_PROTO(args)		do {if (wl_msg_level2 & WL_PROTO_VAL) WL_PRINT(args);} while (0)
#define WL_RTDC2(w,s,i,j)	do {if (wl_msg_level2 & 0) wlc_log(w,s,i,j);} while (0)
#define WL_CHANINT(args)	do {if (wl_msg_level2 & WL_CHANINT_VAL) WL_PRINT(args);} while (0)
#define WL_WMF(args)		do {if (wl_msg_level2 & WL_WMF_VAL) WL_PRINT(args);} while (0)
#define WL_P2P(args)		do {if (wl_msg_level2 & WL_P2P_VAL) WL_PRINT(args);} while (0)
#define WL_ITFR(args)		do {if (wl_msg_level2 & WL_ITFR_VAL) WL_PRINT(args);} while (0)
#define WL_MCHAN(args)		do {if (wl_msg_level2 & WL_MCHAN_VAL) WL_PRINT(args);} while (0)
#define WL_TDLS(args)		do {if (wl_msg_level2 & WL_TDLS_VAL) WL_PRINT(args);} while (0)
#define WL_MCNX(args)		do {if (wl_msg_level2 & WL_MCNX_VAL) WL_PRINT(args);} while (0)
#define WL_PROT(args)		do {if (wl_msg_level2 & WL_PROT_VAL) WL_PRINT(args);} while (0)
#define WL_PSTA(args)		do {if (wl_msg_level2 & WL_PSTA_VAL) WL_PRINT(args);} while (0)
#define WL_TBTT(args)		do {if (wl_msg_level2 & WL_TBTT_VAL) WL_PRINT(args);} while (0)
#define WL_TRF_MGMT(args)	do {if (wl_msg_level2 & WL_TRF_MGMT_VAL) WL_PRINT(args);} while (0)
#define WL_L2FILTER(args)	do {if (wl_msg_level2 & WL_L2FILTER_VAL) WL_PRINT(args);} while (0)
#define WL_TSO(args)		do {if (wl_msg_level2 & WL_TSO_VAL) WL_PRINT(args);} while (0)
#define WL_MQ(args)		do {if (wl_msg_level2 & WL_MQ_VAL) WL_PRINT(args);} while (0)
#define WL_P2PO(args)		do {if (wl_msg_level2 & WL_P2PO_VAL) WL_PRINT(args);} while (0)
#ifdef WLAWDL
#define WL_AWDL(args)		do {if (wl_msg_level2 & WL_AWDL_VAL) WL_PRINT(args);} while (0)
#endif /* WLAWDL */
#define WL_WNM(args)		do {if (wl_msg_level2 & WL_WNM_VAL) WL_PRINT(args);} while (0)
#define WL_TXBF(args)		do {if (wl_msg_level2 & WL_TXBF_VAL) WL_PRINT(args);} while (0)
#define	WL_PCIE(args)		do {if (wl_msg_level2 & WL_PCIE_VAL) WL_PRINT(args);} while (0)
#define WL_PMDUR(args)		do {if (wl_msg_level2 & WL_PMDUR_VAL) WL_PRINT(args);} while (0)
#ifdef BCMTSTAMPEDLOGS
void wlc_bmac_tslog(struct wlc_hw_info *hw, const char *str, uint32 p1, uint32 p2);
#else
#define wlc_bmac_tslog(hw, str, p1, p2)	do {} while (0)
#endif
#define WL_TSLOG(w, s, i, j) \
	do { \
		if (wl_msg_level2 & WL_TIMESTAMP_VAL) { \
			wlc_bmac_tslog(w, s, i, j); \
		} \
	} while (0)
/* not using WL_ROAM for BCMDBG at the moment */
#define WL_ROAM(args)
#define WL_PRMAC(args)		do {if (wl_msg_level & WL_PRMAC_VAL) WL_PRINT(args);} while (0)
#define WL_FBT(args)		do {if (wl_msg_level2 & WL_FBT_VAL) WL_PRINT(args);} while (0)
#define WL_MESH(args)		do {if (wl_msg_level2 & WL_MESH_VAL) WL_PRINT(args);} while (0)
#define WL_SWDIV(args)		do {if (wl_msg_level2 & WL_SWDIV_VAL) WL_PRINT(args);} while (0)
#define WL_MBO_DBG(args)	do {if (wl_msg_level2 & WL_MBO_VAL) WL_PRINT(args);} while (0)
#define WL_RANDMAC_INFO(args)	do {if (wl_msg_level2 & WL_RANDMAC_VAL) WL_PRINT(args);} while (0)
#define WL_BAM_ERR(args)	do {if (wl_msg_level2 & WL_ERROR_VAL)	WL_PRINT(args);} while (0)
#define WL_ADPS(args)		do {if (wl_msg_level2 & WL_ADPS_VAL) WL_PRINT(args);} while (0)
#define WL_OCE_DBG(args)	do {if (wl_msg_level2 & WL_OCE_VAL) WL_PRINT(args);} while (0)
#define WL_WBTEXT_INFO(args)	do {if (wl_msg_level2 & WL_WNM_VAL) WL_PRINT(args);} while (0)
#define WL_ASSOC_AP(args) \
	do { \
		if (wl_msg_level3 & WL_ASSOC_AP_VAL) { \
			WL_PRINT(args);	\
		} else { \
			WIFICC_LOGDEBUG(args); \
		} \
	} while (0)
#define WL_TPA_ERR(args)	do {if (wl_msg_level2 & WL_ERROR_VAL)	WL_PRINT(args);} while (0)
#define WL_TPA_INFO(args)	do {if (wl_msg_level2 & WL_INFORM_VAL)	WL_PRINT(args);} while (0)
#define WL_LATENCY_INFO(args) do {if (wl_msg_level3 & WL_LATENCY_VAL) WL_PRINT(args);} while (0)

#define WL_ERROR_ON()		(wl_msg_level & WL_ERROR_VAL)
#define WL_TRACE_ON()		(wl_msg_level & WL_TRACE_VAL)
#define WL_PRHDRS_ON()		(wl_msg_level & WL_PRHDRS_VAL)
#define WL_PRPKT_ON()		(wl_msg_level & WL_PRPKT_VAL)
#define WL_INFORM_ON()		(wl_msg_level & WL_INFORM_VAL)
#define WL_TMP_ON()		(wl_msg_level & WL_TMP_VAL)
#define WL_OID_ON()		(wl_msg_level & WL_OID_VAL)
#define WL_RATE_ON()		(wl_msg_level & WL_RATE_VAL)
#define WL_ASSOC_ON()		(wl_msg_level & WL_ASSOC_VAL)
#define WL_PORT_ON()		(wl_msg_level & WL_PORT_VAL)
#define WL_WSEC_ON()		(wl_msg_level & WL_WSEC_VAL)
#define WL_WSEC_DUMP_ON()	(wl_msg_level & WL_WSEC_DUMP_VAL)
#define WL_MPC_ON()		(wl_msg_level & WL_MPC_VAL)
#define WL_REGULATORY_ON()	(wl_msg_level & WL_REGULATORY_VAL)
#define WL_APSTA_ON()		(wl_msg_level & WL_APSTA_VAL)
#define WL_DFS_ON()		(wl_msg_level & WL_DFS_VAL)
#define WL_MUMIMO_ON()		(wl_msg_level & WL_MUMIMO_VAL)
#define WL_MODE_SWITCH_ON()	(wl_msg_level & WL_MODE_SWITCH_VAL)
#define WL_MBSS_ON()		(wl_msg_level & WL_MBSS_VAL)
#define WL_AMPDU_ON()		(wl_msg_level & WL_AMPDU_VAL)
#define WL_PFN_ON()		(wl_msg_level & WL_PFN_VAL)
#define WL_WOWL_ON()		(wl_msg_level2 & WL_WOWL_VAL)
#define WL_SCAN_ON()		(wl_msg_level2 & WL_SCAN_VAL)
#define WL_WMF_ON()	        (wl_msg_level2 & WL_WMF_VAL)
#define WL_P2P_ON()		(wl_msg_level2 & WL_P2P_VAL)
#define WL_ITFR_ON()		(wl_msg_level2 & WL_ITFR_VAL)
#define WL_MCHAN_ON()		(wl_msg_level2 & WL_MCHAN_VAL)
#define WL_TDLS_ON()		(wl_msg_level2 & WL_TDLS_VAL)
#define WL_MCNX_ON()		(wl_msg_level2 & WL_MCNX_VAL)
#define WL_PROT_ON()		(wl_msg_level2 & WL_PROT_VAL)
#define WL_PSTA_ON()		(wl_msg_level2 & WL_PSTA_VAL)
#define WL_TBTT_ON()		(wl_msg_level2 & WL_TBTT_VAL)
#define WL_TRF_MGMT_ON()	(wl_msg_level2 & WL_TRF_MGMT)
#define WL_LPC_ON()		(wl_msg_level2 & WL_LPC_VAL)
#define WL_L2FILTER_ON()	(wl_msg_level2 & WL_L2FILTER_VAL)
#define WL_MQ_ON()		(wl_msg_level2 & WL_MQ_VAL)
#define WL_P2PO_ON()		(wl_msg_level2 & WL_P2PO_VAL)
#ifdef WLAWDL
#define WL_AWDL_ON()		(wl_msg_level2 & WL_AWDL_VAL)
#endif /* WLAWDL */
#define WL_WNM_ON()		(wl_msg_level2 & WL_WNM_VAL)
#define WL_TXBF_ON()	        (wl_msg_level2 & WL_TXBF_VAL)
#define WL_PCIE_ON()		(wl_msg_level2 & WL_PCIE_VAL)
#define WL_TSLOG_ON()		(wl_msg_level2 & WL_TIMESTAMP_VAL)
#define WL_MESH_ON()		(wl_msg_level2 & WL_MESH_VAL)
#define WL_SWDIV_ON()		(wl_msg_level2 & WL_SWDIV_VAL)
#define WL_MBO_DBG_ON()		(wl_msg_level2 & WL_MBO_VAL)
#define WL_RANDMAC_DBG_ON()	(wl_msg_level2 & WL_RANDMAC_VAL)
#define WL_ADPS_ON()            (wl_msg_level2 & WL_ADPS_VAL)
#define WL_OCE_DBG_ON()		(wl_msg_level2 & WL_OCE_VAL)
#define WL_ASSOC_AP_ON()	(wl_msg_level3 & WL_ASSOC_AP_VAL)
#define WL_FILS_DBG_ON()	(wl_msg_level3 & WL_FILS_DBG_VAL)

/* Extra message control for APSTA debugging */
#define	WL_APSTA_UPDN_VAL	0x00000001 /* Config up/down related  */
#define	WL_APSTA_BCN_VAL	0x00000002 /* Calls to beacon update  */
#define	WL_APSTA_TX_VAL		0x00000004 /* Transmit data path */
#define	WL_APSTA_RX_VAL		0x00000008 /* Receive data path  */
#define	WL_APSTA_TSF_VAL	0x00000010 /* TSF-related items  */
#define	WL_APSTA_BSSID_VAL	0x00000020 /* Calls to set bssid */

extern uint32 wl_apsta_dbg;

#define WL_APSTA_UPDN(args) do {if (wl_apsta_dbg & WL_APSTA_UPDN_VAL) {WL_APSTA(args);}} while (0)
#define WL_APSTA_BCN(args) do {if (wl_apsta_dbg & WL_APSTA_BCN_VAL) {WL_APSTA(args);}} while (0)
#define WL_APSTA_TX(args) do {if (wl_apsta_dbg & WL_APSTA_TX_VAL) {WL_APSTA(args);}} while (0)
#define WL_APSTA_RX(args) do {if (wl_apsta_dbg & WL_APSTA_RX_VAL) {WL_APSTA(args);}} while (0)
#define WL_APSTA_TSF(args) do {if (wl_apsta_dbg & WL_APSTA_TSF_VAL) {WL_APSTA(args);}} while (0)
#define WL_APSTA_BSSID(args) do {if (wl_apsta_dbg & WL_APSTA_BSSID_VAL) {WL_APSTA(args);}} while (0)

/* Extra message control for AMPDU debugging */
#define   WL_AMPDU_UPDN_VAL	0x00000001 /* Config up/down related  */
#define   WL_AMPDU_ERR_VAL	0x00000002 /* Calls to beaocn update  */
#define   WL_AMPDU_TX_VAL	0x00000004 /* Transmit data path */
#define   WL_AMPDU_RX_VAL	0x00000008 /* Receive data path  */
#define   WL_AMPDU_CTL_VAL	0x00000010 /* TSF-related items  */
#define   WL_AMPDU_HW_VAL       0x00000020 /* AMPDU_HW */
#define   WL_AMPDU_HWTXS_VAL    0x00000040 /* AMPDU_HWTXS */
#define   WL_AMPDU_HWDBG_VAL    0x00000080 /* AMPDU_DBG */
#define   WL_AMPDU_STAT_VAL     0x00000100 /* statistics */

extern uint32 wl_ampdu_dbg;

#define WL_AMPDU_UPDN(args) do {if (wl_ampdu_dbg & WL_AMPDU_UPDN_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_RX(args) do {if (wl_ampdu_dbg & WL_AMPDU_RX_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_ERR(args) do {if (wl_ampdu_dbg & WL_AMPDU_ERR_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_TX(args) do {if (wl_ampdu_dbg & WL_AMPDU_TX_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_CTL(args) do {if (wl_ampdu_dbg & WL_AMPDU_CTL_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_HW(args) do {if (wl_ampdu_dbg & WL_AMPDU_HW_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_HWTXS(args) do {if (wl_ampdu_dbg & WL_AMPDU_HWTXS_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_HWDBG(args) do {if (wl_ampdu_dbg & WL_AMPDU_HWDBG_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_STAT(args) do {if (wl_ampdu_dbg & WL_AMPDU_STAT_VAL) {WL_AMPDU(args);}} while (0)
#define WL_AMPDU_ERR_ON() (wl_ampdu_dbg & WL_AMPDU_ERR_VAL)
#define WL_AMPDU_HW_ON() (wl_ampdu_dbg & WL_AMPDU_HW_VAL)
#define WL_AMPDU_HWTXS_ON() (wl_ampdu_dbg & WL_AMPDU_HWTXS_VAL)

/* Extra Message control for Mesh debugging */
extern uint32 wl_mesh_dbg;
#define  WL_MESH_AMPE_VAL	0x00000001
#define  WL_MESH_ROUTE_VAL	0x00000002
#define  WL_MESH_BCN_VAL	0x00000004

#define  WL_MESH_AMPE(args)  do {if (wl_mesh_dbg & WL_MESH_AMPE_VAL) {WL_MESH(args);}} while (0)
#define  WL_MESH_ROUTE(args) do {if (wl_mesh_dbg & WL_MESH_ROUTE_VAL) {WL_MESH(args);}} while (0)
#define  WL_MESH_BCN(args)   do {if (wl_mesh_dbg & WL_MESH_BCN_VAL) {WL_MESH(args);}} while (0)

/* BCMDBG */
#elif defined(BCMCONDITIONAL_LOGGING)

/* Ideally this should be some include file that vendors can include to conditionalize logging */

/* DBGONLY() macro to reduce ifdefs in code for statements that are only needed when
 * BCMDBG is defined.
 */
#define DBGONLY(x) x

/* To disable a message completely ... until you need it again */
#define WL_NONE(args)
#define WL_WARN(x)	WL_ERROR(x)
#define WL_ERROR(args)		do {if (wl_msg_level & WL_ERROR_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args); } while (0)
#define	WL_SCAN_ERROR(args)
#define	WL_IE_ERROR(args)
#define	WL_AMSDU_ERROR(args)
#define	WL_ASSOC_ERROR(args)
#define WL_WNM_PDT_ERROR(args)
#define	KM_ERR(args)
#define	WL_WBTEXT_ERROR(args)
#define	WL_WBTEXT_INFO(args)
#define	WL_LATENCY_INFO(args)

#define WL_TRACE(args)
#define WL_PRHDRS_MSG(args)
#define WL_PRHDRS(i, p, f, t, r, l)
#define WL_PRPKT(m, b, n)
#define WL_INFORM(args)
#define WL_TMP(args)
#define WL_OID(args)
#define WL_RATE(args)		do {if (wl_msg_level & WL_RATE_VAL) WL_PRINT(args);} while (0)
#define WL_ASSOC(args)		do {if (wl_msg_level & WL_ASSOC_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args);} while (0)
#define WL_PRUSR(m, b, n)
#define WL_PS(args)		do {if (wl_msg_level & WL_PS_VAL) WL_PRINT(args);} while (0)

#define WL_PORT(args)
#define WL_DUAL(args)
#define WL_REGULATORY(args)	do {if (wl_msg_level & WL_REGULATORY_VAL) WL_PRINT(args); \
					else WIFICC_LOGDEBUG(args);} while (0)

#define WL_MPC(args)
#define WL_APSTA(args)
#define WL_APSTA_BCN(args)
#define WL_APSTA_TX(args)
#define WL_APSTA_TSF(args)
#define WL_APSTA_BSSID(args)
#define WL_BA(args)
#define WL_MBSS(args)
#define WL_MODE_SWITCH(args)
#define WL_PROTO(args)

#define	WL_CAC(args)		do {if (wl_msg_level & WL_CAC_VAL) WL_PRINT(args);} while (0)
#define WL_AMSDU(args)
#define WL_AMPDU(args)
#define WL_FFPLD(args)
#define WL_MCHAN(args)

#define WL_DFS(args)
#define WL_WOWL(args)
#define WL_DPT(args)
#define WL_ASSOC_OR_DPT(args)
#define WL_SCAN(args)		do {if (wl_msg_level2 & WL_SCAN_VAL) WL_PRINT(args);} while (0)
#define WL_SCAN_WARN(args)	do {if (wl_msg_level2 & WL_SCAN_VAL) WL_PRINT(args);} while (0)
#define WL_COEX(args)
#define WL_RTDC(w, s, i, j)
#define WL_RTDC2(w, s, i, j)
#define WL_CHANINT(args)
#define WL_BTA(args)
#define WL_P2P(args)
#define WL_ITFR(args)
#define WL_TDLS(args)
#define WL_MCNX(args)
#define WL_PROT(args)
#define WL_PSTA(args)
#define WL_WFDS(m, b, n)
#define WL_TRF_MGMT(args)
#define WL_L2FILTER(args)
#define WL_MQ(args)
#define WL_TXBF(args)
#define WL_MUMIMO(args)
#define WL_P2PO(args)
#ifdef WLAWDL
#define WL_AWDL(args)
#endif /* WLAWDL */
#define WL_ROAM(args)
#define WL_WNM(args)

#ifdef WLMSG_MESH
#define WL_MESH(args)       WL_PRINT(args)
#define WL_MESH_AMPE(args)  WL_PRINT(args)
#define WL_MESH_ROUTE(args) WL_PRINT(args)
#define WL_MESH_BCN(args)
#else
#define WL_MESH(args)
#define WL_MESH_AMPE(args)
#define WL_MESH_ROUTE(args)
#define WL_MESH_BCN(args)
#endif
#define WL_ASSOC_AP(args) \
	do { \
		if (wl_msg_level3 & WL_ASSOC_AP_VAL) { \
			WL_PRINT(args); \
		} else { \
			WIFICC_LOGDEBUG(args); \
		} \
	} while (0)
#ifdef BCMDBG_ERR
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_PFN_ERROR(args)   EVENT_LOG_RA(EVENT_LOG_TAG_SCAN_ERR, args)
#else
#define   WL_PFN_ERROR(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_SCAN_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_PFN_ERROR(args)    WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define WL_PFN_ERROR(args)
#endif /* BCMDBG_ERR */

#define WL_AMPDU_UPDN(args)
#define WL_AMPDU_RX(args)
#define WL_AMPDU_ERR(args)
#define WL_AMPDU_TX(args)
#define WL_AMPDU_CTL(args)
#define WL_AMPDU_HW(args)
#define WL_AMPDU_HWTXS(args)
#define WL_AMPDU_HWDBG(args)
#define WL_AMPDU_STAT(args)
#define WL_AMPDU_ERR_ON()       0
#define WL_AMPDU_HW_ON()        0
#define WL_AMPDU_HWTXS_ON()     0

#define WL_APSTA_UPDN(args)
#define WL_APSTA_RX(args)
#define WL_WSEC(args)
#define WL_WSEC_DUMP(args)
#define WL_PCIE(args)
#define WL_PMDUR(args)
#define WL_TSLOG(w, s, i, j)
#define WL_FBT(args)
#define WL_MBO_DBG(args)
#define WL_RANDMAC_DBG(args)
#define WL_BAM_ERR(args)
#define WL_ADPS(args)
#define WL_OCE_DBG(args)
#define WL_TPA_ERR(args)
#define WL_TPA_INFO(args)

#define WL_ERROR_ON()		(wl_msg_level & WL_ERROR_VAL)
#define WL_TRACE_ON()		0
#define WL_PRHDRS_ON()		0
#define WL_PRPKT_ON()		0
#define WL_INFORM_ON()		0
#define WL_TMP_ON()		0
#define WL_OID_ON()		0
#define WL_RATE_ON()		(wl_msg_level & WL_RATE_VAL)
#define WL_ASSOC_ON()		(wl_msg_level & WL_ASSOC_VAL)
#define WL_PRUSR_ON()		0
#define WL_PS_ON()		(wl_msg_level & WL_PS_VAL)
#define WL_PORT_ON()		0
#define WL_WSEC_ON()		0
#define WL_WSEC_DUMP_ON()	0
#define WL_MPC_ON()		0
#define WL_REGULATORY_ON()	(wl_msg_level & WL_REGULATORY_VAL)
#define WL_APSTA_ON()		0
#define WL_DFS_ON()		0
#define WL_MBSS_ON()		0
#define WL_CAC_ON()		(wl_msg_level & WL_CAC_VAL)
#define WL_AMPDU_ON()		0
#define WL_DPT_ON()		0
#define WL_WOWL_ON()		0
#define WL_SCAN_ON()		(wl_msg_level2 & WL_SCAN_VAL)
#define WL_BTA_ON()		0
#define WL_P2P_ON()		0
#define WL_ITFR_ON()		0
#define WL_MCHAN_ON()		0
#define WL_TDLS_ON()		0
#define WL_MCNX_ON()		0
#define WL_PROT_ON()		0
#define WL_PSTA_ON()		0
#define WL_TRF_MGMT_ON()	0
#define WL_LPC_ON()		0
#define WL_L2FILTER_ON()	0
#define WL_TXBF_ON()		0
#define WL_P2PO_ON()		0
#ifdef WLAWDL
#define WL_AWDL_ON()		0
#endif /* WLAWDL */
#define WL_TSLOG_ON()		0
#define WL_WNM_ON()		0
#define WL_PCIE_ON()		0
#define WL_MUMIMO_ON()		0
#define WL_MESH_ON()		0
#define WL_MBO_DBG_ON()		0
#define WL_RANDMAC_DBG_ON()	0
#define WL_ADPS_ON()            0
#define WL_OCE_DBG_ON()		0
#define WL_FILS_DBG_ON()	0
#define WL_ASSOC_AP_ON()	(wl_msg_level3 & WL_ASSOC_AP_VAL)

#else /* !BCMDBG */

/* DBGONLY() macro to reduce ifdefs in code for statements that are only needed when
 * BCMDBG is defined.
 */
#define DBGONLY(x)

/* To disable a message completely ... until you need it again */
#define WL_NONE(args)

#ifdef BCMDBG_ERR
/* ROM and ROML optimized builds */
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_ERROR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_ERROR, args)
#define   WL_WARN(args)		EVENT_LOG_RA(EVENT_LOG_TAG_WL_WARN, args)
#else
#define   WL_ERROR(args)        EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_ERROR, args)
#define   WL_WARN(args)		EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_WARN, args)
#endif /* ERR_USE_EVENT_LOG_RA */

#else
#define   WL_ERROR(args)	WL_PRINT(args)
#define   WL_WARN(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define	WL_ERROR(args)
#define WL_WARN(args)
#endif /* BCMDBG_ERR */

#ifdef TS_PLOT
#define   TS_LOG_DBG(x)	x
#define   TS_LOG(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_TSLOG, args)
#else
#define   TS_LOG_DBG(x)
#define   TS_LOG(args)
#endif

#ifdef BCMDBG_ERR
/* ROM and ROML optimized builds */
#if defined(EVENT_LOG_COMPILE)&& defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define  KM_ERR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_KM_ERROR, args)
#else
#define  KM_ERR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_KM_ERROR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define  KM_ERR(args)     WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define  KM_ERR(args)
#endif /* BCMDBG_ERR */

#ifdef BCMDBG_ERR
/* ROM and ROML optimized builds */
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define WL_AMPDU_ERR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_AMPDU_ERROR, args)
#else
#define WL_AMPDU_ERR(args)    EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_AMPDU_ERROR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define WL_AMPDU_ERR(args)    WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define	WL_AMPDU_ERR(args)
#endif /* BCMDBG_ERR */

#define	WL_TRACE(args)
#ifdef WLMSG_PRHDRS
#define	WL_PRHDRS_MSG(args)		WL_PRINT(args)
#define WL_PRHDRS(i, p, f, t, r, l)	wlc_print_hdrs(i, p, f, t, r, l)
#else
#define	WL_PRHDRS_MSG(args)
#define	WL_PRHDRS(i, p, f, t, r, l)
#endif
#ifdef WLMSG_PRPKT
#define	WL_PRPKT(m, b, n)	prhex(m, b, n)
#else
#define	WL_PRPKT(m, b, n)
#endif
#ifdef WLMSG_INFORM
#define	WL_INFORM(args)		WL_PRINT(args)
#else
#define	WL_INFORM(args)
#endif
#define	WL_TMP(args)
#ifdef WLMSG_OID
#define WL_OID(args)		WL_PRINT(args)
#else
#define WL_OID(args)
#endif
#define	WL_RATE(args)

#ifdef BCMDBG_ERR
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_IE_ERROR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_IE_ERROR, args)
#else
#define   WL_IE_ERROR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_IE_ERROR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_IE_ERROR(args)	WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define	  WL_IE_ERROR(args)
#endif /* BCMDBG_ERR */

#ifdef WLMSG_WNM_BSSTRANS
#if defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_WBTEXT_INFO(args)		EVENT_LOG_RA(EVENT_LOG_TAG_WNM_BSSTRANS_INFO, args)
#else
#define   WL_WBTEXT_INFO(args)	 \
			EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WNM_BSSTRANS_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_WBTEXT_INFO(args)		WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_WBTEXT_INFO(args)
#endif /* WLMSG_WNM_BSSTRANS */

#if defined(BCMPCIE_LATENCY) && defined(BCMPCIE_LATENCY_DEBUG)
#if defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_LATENCY_INFO(args)		EVENT_LOG_RA(EVENT_LOG_TAG_LATENCY_INFO, args)
#else
#define   WL_LATENCY_INFO(args)	 \
			EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_LATENCY_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_LATENCY_INFO(args)		WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_LATENCY_INFO(args)
#endif /* BCMPCIE_LATENCY && BCMPCIE_LATENCY_DEBUG */

#ifdef BCMDBG_ERR
#if defined(ERR_USE_EVENT_LOG) && defined(EVENT_LOG_COMPILE)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_WBTEXT_ERROR(args)		EVENT_LOG_RA(EVENT_LOG_TAG_WNM_BSSTRANS_ERR, args)
#else
#define   WL_WBTEXT_ERROR(args)	 \
			EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WNM_BSSTRANS_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_WBTEXT_ERROR(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG && EVENT_LOG_COMPILE */
#else
#define	  WL_WBTEXT_ERROR(args)
#endif /* BCMDBG_ERR */

#ifdef WLMSG_WNM_BSSTRANS
#if defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_WNM_PDT_INFO(args)		EVENT_LOG_RA(EVENT_LOG_TAG_WNM_BSSTRANS_INFO, args)
#else
#define   WL_WNM_PDT_INFO(args)	 \
		EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WNM_BSSTRANS_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_WNM_PDT_INFO(args)		WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_WNM_PDT_INFO(args)
#endif /* WLMSG_WNM_BSSTRANS */

#ifdef BCMDBG_ERR
#if defined(ERR_USE_EVENT_LOG) && defined(EVENT_LOG_COMPILE)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_WNM_PDT_ERROR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WNM_BSSTRANS_ERR, args)
#else
#define   WL_WNM_PDT_ERROR(args)	 \
			EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WNM_BSSTRANS_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_WNM_PDT_ERROR(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG && EVENT_LOG_COMPILE */
#else
#define	  WL_WNM_PDT_ERROR(args)
#endif /* BCMDBG_ERR */

#ifdef WLMSG_ASSOC
#if defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define WL_ASSOC(args)   EVENT_LOG_RA(EVENT_LOG_TAG_WL_ASSOC_LOG, args)
#else
#define WL_ASSOC(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_ASSOC_LOG, args)
#define WL_ASSOC_DP(args)   EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_ASSOC_LOG, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_ASSOC(args)   WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#define WL_ASSOC_AP(args)   WL_PRINT(args)
#else
#define	WL_ASSOC(args)
#define	WL_ASSOC_AP(args)
#endif /* WLMSG_ASSOC */

#ifdef BCMDBG_ERR
/* ROM and ROML optimized builds */
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define WL_ASSOC_ERROR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_ASSOC_ERROR, args)
#else
#define WL_ASSOC_ERROR(args)  EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_ASSOC_ERROR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define  WL_ASSOC_ERROR(args)      WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define	 WL_ASSOC_ERROR(args)
#endif /* BCMDBG_ERR */

#ifdef BCMDBG_ERR
/* ROM and ROML optimized builds */
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_SCAN_ERROR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_SCAN_ERR, args)
#else
#define   WL_SCAN_ERROR(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_SCAN_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_SCAN_ERROR(args)    WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define	WL_SCAN_ERROR(args)
#endif /* BCMDBG_ERR */

#define	WL_PRUSR(m, b, n)

#ifdef WLMSG_PS
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define WL_PS(args)   EVENT_LOG_RA(EVENT_LOG_TAG_WL_PS_LOG, args)
#else
#define WL_PS(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_PS_LOG, args)
#define WL_PS_DP(args)   EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_PS_LOG, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_PS(args)   WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_PS(args)
#endif /* WLMSG_PS */

#ifdef BCMDBG_ERR
/* ROM and ROML optimized builds */
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define WL_AMSDU_ERROR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_AMSDU_ERROR, args)
#else
#define WL_AMSDU_ERROR(args)    EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_AMSDU_ERROR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_AMSDU_ERROR(args)          WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define	WL_AMSDU_ERROR(args)
#endif /* BCMDBG_ERR */

#ifdef BCMDBG_PRINT_EAP_PKT_INFO
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define WL_8021X_ERR(args)   do {printf args; \
	EVENT_LOG_RA(EVENT_LOG_TAG_4WAYHANDSHAKE, args);} while (0)
#else
#define WL_8021X_ERR(args)   do {printf args; \
	EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_4WAYHANDSHAKE, args);} while (0)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_8021X_ERR(args)   WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_8021X_ERR(args)
#endif /* BCMDBG_PRINT_EAP_PKT_INFO */

#ifdef WLMSG_ROAM
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define WL_ROAM(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_ROAM_LOG, args)
#else
#define WL_ROAM(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_ROAM_LOG, args)
#define WL_ROAM_DP(args)   EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_ROAM_LOG, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_ROAM(args)   WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_ROAM(args)
#endif /* WLMSG_ROAM */

#define WL_PORT(args)
#define WL_DUAL(args)
#define WL_REGULATORY(args)

#ifdef WLMSG_MPC
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define WL_MPC(args)   EVENT_LOG_RA(EVENT_LOG_TAG_WL_MPC_LOG, args)
#else
#define WL_MPC(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_MPC_LOG, args)
#define WL_MPC_DP(args)   EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_MPC_LOG, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_MPC(args)   WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_MPC(args)
#endif /* WLMSG_MPC */

#define WL_APSTA(args)
#define WL_APSTA_BCN(args)
#define WL_APSTA_TX(args)
#define WL_APSTA_TSF(args)
#define WL_APSTA_BSSID(args)
#define WL_BA(args)
#define WL_MBSS(args)
#define WL_MODE_SWITCH(args)
#define	WL_PROTO(args)

#define	WL_CAC(args)
#define WL_AMSDU(args)
#define WL_AMPDU(args)
#define WL_FFPLD(args)
#define WL_MCHAN(args)
#define WL_BCNTRIM_DBG(args)

/* Define WLMSG_DFS automatically for WLTEST builds */
#if defined(WLTEST) && !defined(WLTEST_DISABLED)
#ifndef WLMSG_DFS
#define WLMSG_DFS
#endif
#endif /* WLTEST */

#ifdef WLMSG_DFS
#define WL_DFS(args)		do {if (wl_msg_level & WL_DFS_VAL) WL_PRINT(args);} while (0)
#else /* WLMSG_DFS */
#define WL_DFS(args)
#endif /* WLMSG_DFS */
#define WL_WOWL(args)

#ifdef WLMSG_SCAN
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_SCAN(args)		EVENT_LOG_RA(EVENT_LOG_TAG_SCAN_TRACE_LOW, args)
#define   WL_SCAN_WARN(args)	EVENT_LOG_RA(EVENT_LOG_TAG_SCAN_WARN, args)
#else
#define   WL_SCAN(args)	\
		EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_SCAN_TRACE_LOW, args)
#define   WL_SCAN_DP(args) \
		EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_SCAN_TRACE_LOW, args)
#define   WL_SCAN_WARN(args)	EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_SCAN_WARN, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_SCAN(args)		WL_PRINT(args)
#define   WL_SCAN_WARN(args)	WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_SCAN(args)
#define WL_SCAN_WARN(args)
#endif /* WLMSG_SCAN */

#define	WL_COEX(args)
#define WL_RTDC(w, s, i, j)
#define WL_RTDC2(w, s, i, j)
#define WL_CHANINT(args)
#ifdef WLMSG_BTA
#define WL_BTA(args)		WL_PRINT(args)
#else
#define WL_BTA(args)
#endif
#define WL_WMF(args)
#define WL_P2P(args)
#define WL_ITFR(args)
#define WL_TDLS(args)

#ifdef WLMSG_MCNX
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define WL_MCNX(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_MCNX_LOG, args)
#else
#define WL_MCNX(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_MCNX_LOG, args)
#define WL_MCNX_DP(args)   EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_MCNX_LOG, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_MCNX(args)   WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_MCNX(args)
#endif /* WLMSG_MCNX */

#define WL_PROT(args)
#define WL_PSTA(args)
#define WL_TBTT(args)
#define WL_TRF_MGMT(args)
#define WL_L2FILTER(args)
#define WL_MQ(args)
#define WL_P2PO(args)
#ifdef WLAWDL
#define WL_AWDL(args)
#endif /* WLAWDL */
#define WL_WNM(args)
#define WL_TXBF(args)
#define WL_TSLOG(w, s, i, j)
#define WL_FBT(args)
#define WL_MUMIMO(args)
#ifdef WLMSG_MESH
#define WL_MESH(args)		WL_PRINT(args)
#define WL_MESH_AMPE(args)	WL_PRINT(args)
#define WL_MESH_ROUTE(args)	WL_PRINT(args)
#define WL_MESH_BCN(args)
#else
#define WL_MESH(args)
#define WL_MESH_AMPE(args)
#define WL_MESH_ROUTE(args)
#define WL_MESH_BCN(args)
#endif
#define WL_SWDIV(args)
#define WL_ADPS(args)

#ifdef BCMDBG_ERR
#define WL_ERROR_ON()		1
#else
#define WL_ERROR_ON()		0
#endif
#define WL_TRACE_ON()		0
#ifdef WLMSG_PRHDRS
#define WL_PRHDRS_ON()		1
#else
#define WL_PRHDRS_ON()		0
#endif
#ifdef WLMSG_PRPKT
#define WL_PRPKT_ON()		1
#else
#define WL_PRPKT_ON()		0
#endif
#ifdef WLMSG_INFORM
#define WL_INFORM_ON()		1
#else
#define WL_INFORM_ON()		0
#endif
#ifdef WLMSG_OID
#define WL_OID_ON()		1
#else
#define WL_OID_ON()		0
#endif
#define WL_TMP_ON()		0
#define WL_RATE_ON()		0
#ifdef WLMSG_ASSOC
#define WL_ASSOC_ON()		1
#define WL_ASSOC_AP_ON()	1
#else
#define WL_ASSOC_ON()		0
#define WL_ASSOC_AP_ON()	0
#endif
#define WL_PORT_ON()		0
#ifdef WLMSG_WSEC
#define WL_WSEC_ON()		1
#define WL_WSEC_DUMP_ON()	1
#else
#define WL_WSEC_ON()		0
#define WL_WSEC_DUMP_ON()	0
#endif
#ifdef WLMSG_MPC
#define WL_MPC_ON()		1
#else
#define WL_MPC_ON()		0
#endif
#define WL_REGULATORY_ON()	0

#define WL_APSTA_ON()		0
#define WL_BA_ON()		0
#define WL_MBSS_ON()		0
#define WL_MODE_SWITCH_ON()	0
#ifdef WLMSG_DFS
#define WL_DFS_ON()		1
#else /* WLMSG_DFS */
#define WL_DFS_ON()		0
#endif /* WLMSG_DFS */
#ifdef WLMSG_SCAN
#define WL_SCAN_ON()            1
#else
#define WL_SCAN_ON()            0
#endif
#ifdef WLMSG_BTA
#define WL_BTA_ON()		1
#else
#define WL_BTA_ON()		0
#endif
#define WL_WMF_ON()		0
#define WL_P2P_ON()		0
#define WL_MCHAN_ON()		0
#define WL_TDLS_ON()		0
#define WL_MCNX_ON()		0
#define WL_PROT_ON()		0
#define WL_TBTT_ON()		0
#define WL_LPC_ON()		0
#define WL_L2FILTER_ON()	0
#define WL_MQ_ON()		0
#define WL_P2PO_ON()		0
#ifdef WLAWDL
#define WL_AWDL_ON()		0
#endif /* WLAWDL */
#define WL_TXBF_ON()            0
#define WL_TSLOG_ON()		0
#define WL_MUMIMO_ON()		0
#define WL_SWDIV_ON()		0

#define WL_AMPDU_UPDN(args)
#define WL_AMPDU_RX(args)
#define WL_AMPDU_TX(args)
#define WL_AMPDU_CTL(args)
#define WL_AMPDU_HW(args)
#define WL_AMPDU_HWTXS(args)
#define WL_AMPDU_HWDBG(args)
#define WL_AMPDU_STAT(args)
#define WL_AMPDU_ERR_ON()       0
#define WL_AMPDU_HW_ON()        0
#define WL_AMPDU_HWTXS_ON()     0

#define WL_WNM_ON()		0
#ifdef WLMSG_MBO
#define WL_MBO_DBG_ON()		1
#else
#define WL_MBO_DBG_ON()		0
#endif /* WLMSG_MBO */
#ifdef WLMSG_RANDMAC
#define WL_RANDMAC_DBG_ON()        1
#else
#define WL_RANDMAC_DBG_ON()        0
#endif /* WLMSG_RANDMAC */
#define WL_ADPS_ON()            0
#ifdef WLMSG_OCE
#define WL_OCE_DBG_ON()		1
#else
#define WL_OCE_DBG_ON()		0
#endif /* WLMSG_OCE */
#ifdef WLMSG_FILS
#define WL_FILS_DBG_ON()		1
#else
#define WL_FILS_DBG_ON()		0
#endif /* WLMSG_FILS */

#define WL_APSTA_UPDN(args)
#define WL_APSTA_RX(args)

#ifdef WLMSG_WSEC
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_WSEC(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_WSEC_LOG, args)
#define   WL_WSEC_DUMP(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_WSEC_DUMP, args)
#else
#define   WL_WSEC(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_WSEC_LOG, args)
#define   WL_WSEC_DUMP(args)   EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_WSEC_DUMP, args)
#define   WL_WSEC_DP(args)   EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_WSEC_LOG, args)
#define   WL_WSEC_DUMP_DP(args)   EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_WSEC_DUMP, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_WSEC(args)		WL_PRINT(args)
#define WL_WSEC_DUMP(args)	WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_WSEC(args)
#define WL_WSEC_DUMP(args)
#endif /* WLMSG_WSEC */

#ifdef WLMSG_MBO
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_MBO_DBG(args)		EVENT_LOG_RA(EVENT_LOG_TAG_MBO_DBG, args)
#define   WL_MBO_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_MBO_INFO, args)
#else
#define   WL_MBO_DBG(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_MBO_DBG, args)
#define   WL_MBO_INFO(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_MBO_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_MBO_DBG(args)		   WL_PRINT(args)
#define   WL_MBO_INFO(args)		WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_MBO_DBG(args)
#define	  WL_MBO_INFO(args)
#endif /* WLMSG_MBO */

#ifdef BCMDBG_ERR
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_MBO_ERR(args)		EVENT_LOG_RA(EVENT_LOG_TAG_MBO_ERR, args)
#else
#define   WL_MBO_ERR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_MBO_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_MBO_ERR(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define   WL_MBO_ERR(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */

#ifdef WLMSG_RANDMAC
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_RANDMAC_DBG(args)              EVENT_LOG_RA(EVENT_LOG_TAG_RANDMAC_DBG, args)
#define   WL_RANDMAC_INFO(args)     EVENT_LOG_RA(EVENT_LOG_TAG_RANDMAC_INFO, args)
#else
#define   WL_RANDMAC_DBG(args)       \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_RANDMAC_DBG, args)
#define   WL_RANDMAC_INFO(args)      \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_RANDMAC_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_RANDMAC_DBG(args)                 WL_PRINT(args)
#define   WL_RANDMAC_INFO(args)             WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define   WL_RANDMAC_DBG(args)
#define   WL_RANDMAC_INFO(args)
#endif /* WLMSG_RANDMAC */

#ifdef BCMDBG_ERR
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_RANDMAC_ERR(args)              EVENT_LOG_RA(EVENT_LOG_TAG_RANDMAC_ERR, args)
#else
#define   WL_RANDMAC_ERR(args)     \
		EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_RANDMAC_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_RANDMAC_ERR(args)              WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define   WL_RANDMAC_ERR(args)              WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */

#ifdef WLMSG_OCE
#if defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_OCE_DBG(args)		EVENT_LOG_RA(EVENT_LOG_TAG_OCE_DBG, args)
#define   WL_OCE_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_OCE_INFO, args)
#else
#define   WL_OCE_DBG(args)	 \
		EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_OCE_DBG, args)
#define   WL_OCE_INFO(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_OCE_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_OCE_DBG(args)		WL_PRINT(args)
#define   WL_OCE_INFO(args)		WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_OCE_DBG(args)
#define	  WL_OCE_INFO(args)
#endif /* WLMSG_OCE */

#ifdef WLMSG_FILS
#if defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_FILS_DBG(args)	EVENT_LOG_RA(EVENT_LOG_TAG_FILS_DBG, args)
#define   WL_FILS_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_FILS_INFO, args)
#else
#define   WL_FILS_DBG(args)	 \
		EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_FILS_DBG, args)
#define   WL_FILS_INFO(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_FILS_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_FILS_DBG(args)	WL_PRINT(args)
#define   WL_FILS_INFO(args)	WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_FILS_DBG(args)
#define	  WL_FILS_INFO(args)
#endif /* WLMSG_FILS */
#ifdef BCMDBG_ERR
#if defined(ERR_USE_EVENT_LOG) && defined(EVENT_LOG_COMPILE)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_OCE_ERR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_OCE_ERR, args)
#define   WL_FILS_ERR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_FILS_ERROR, args)

#else
#define   WL_OCE_ERR(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_OCE_ERR, args)
#define   WL_FILS_ERR(args)	 \
				EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_FILS_ERROR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_OCE_ERR(args)	WL_PRINT(args)
#define   WL_FILS_ERR(args)	WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG && EVENT_LOG_COMPILE */
#else
#define   WL_OCE_ERR(args)	WL_PRINT(args)
#define   WL_FILS_ERR(args)	WL_PRINT(args)
#endif /* BCMDBG_ERR */

#define WL_PCIE(args)		do {if (wl_msg_level2 & WL_PCIE_VAL) WL_PRINT(args);} while (0)
#define WL_PCIE_ON()		(wl_msg_level2 & WL_PCIE_VAL)
#define WL_PFN(args)      do {if (wl_msg_level & WL_PFN_VAL) WL_PRINT(args);} while (0)
#define WL_PFN_ON()		(wl_msg_level & WL_PFN_VAL)
#define WL_PMDUR(args)

#ifdef WLMSG_BAM
#if defined(EVENT_LOG_COMPILE)
#ifdef USE_EVENT_LOG_RA
#define WL_BAM_ERR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_BAM, args)
#else
#define WL_BAM_ERR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_BAM, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_BAM_ERR(args)	WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_BAM_ERR(args)
#endif /* WLMSG_BAM */
#endif /* BCMDBG */

#ifdef BCMDBG_ERR
/* ROM and ROML optimized builds */
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define WL_HE_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_HE_INFO, args)
#define WL_HE_TRACE(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_HE_TRACE, args)
#define WL_HE_WARN(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_HE_WARN, args)
#define WL_HE_ERR(args)		EVENT_LOG_RA(EVENT_LOG_TAG_WL_HE_ERROR, args)
#define WL_TWT_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_TWT_INFO, args)
#define WL_TWT_TRACE(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_TWT_TRACE, args)
#define WL_TWT_WARN(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_TWT_WARN, args)
#define WL_TWT_ERR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_TWT_ERROR, args)
#define WL_HEB_ERR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_HEB_ERROR, args)
#define WL_HEB_TRACE(args)	EVENT_LOG_RA(EVENT_LOG_TAG_WL_HEB_TRACE, args)
#else
#define WL_HE_INFO(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_HE_INFO, args)
#define WL_HE_TRACE(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_HE_TRACE, args)
#define WL_HE_WARN(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_HE_WARN, args)
#define WL_HE_ERR(args)		EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_HE_ERROR, args)
#define WL_TWT_INFO(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_TWT_INFO, args)
#define WL_TWT_TRACE(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_TWT_TRACE, args)
#define WL_TWT_WARN(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_TWT_WARN, args)
#define WL_TWT_ERR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_TWT_ERROR, args)
#define WL_HEB_ERR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_HEB_ERROR, args)
#define WL_HEB_TRACE(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_HEB_TRACE, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define WL_HE_INFO(args)	WL_PRINT(args)
#define WL_HE_TRACE(args)	WL_PRINT(args)
#define WL_HE_WARN(args)	WL_PRINT(args)
#define WL_HE_ERR(args)		WL_PRINT(args)
#define WL_TWT_INFO(args)	WL_PRINT(args)
#define WL_TWT_TRACE(args)	WL_PRINT(args)
#define WL_TWT_WARN(args)	WL_PRINT(args)
#define WL_TWT_ERR(args)	WL_PRINT(args)
#define WL_HEB_ERR(args)	WL_PRINT(args)
#define WL_HEB_TRACE(args)	WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define WL_HE_INFO(args)
#define WL_HE_TRACE(args)
#define WL_HE_WARN(args)
#define WL_HE_ERR(args)
#define WL_TWT_INFO(args)
#define WL_TWT_TRACE(args)
#define WL_TWT_WARN(args)
#define WL_TWT_ERR(args)
#define WL_HEB_ERR(args)
#define WL_HEB_TRACE(args)
#endif /* BCMDBG_ERR */

#ifdef WLMSG_TPA
#ifdef EVENT_LOG_COMPILE
#ifdef USE_EVENT_LOG_RA
#define WL_TPA_ERR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_TPA_ERR, args)
#define WL_TPA_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_TPA_INFO, args)
#else
#define WL_TPA_ERR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_TPA_ERR, args)
#define WL_TPA_INFO(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_TPA_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_TPA_ERR(args)	WL_PRINT(args)
#define WL_TPA_INFO(args)	WL_INFORM(args)
#endif /* EVENT_LOG_COMPILE */
#else
#ifndef WL_TPA_ERR
#define WL_TPA_ERR(args)
#endif /* WL_TPA_ERR */
#ifndef WL_TPA_INFO
#define WL_TPA_INFO(args)
#endif /* WL_TPA_INFO */
#endif /* WLMSG_TPA */

#ifdef WLMSG_WNM_BSSTRANS
#if defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_BSSTRANS_INFO(args)		EVENT_LOG_RA(EVENT_LOG_TAG_WNM_BSSTRANS_INFO, args)
#else
#define   WL_BSSTRANS_INFO(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WNM_BSSTRANS_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_BSSTRANS_INFO(args)		WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_BSSTRANS_INFO(args)
#endif /* WLMSG_WNM_BSSTRANS */

#ifdef BCMDBG_ERR
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_BSSTRANS_ERR(args)      EVENT_LOG_RA(EVENT_LOG_TAG_WNM_BSSTRANS_ERR, args)
#else
#define   WL_BSSTRANS_ERR(args)      \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WNM_BSSTRANS_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_BSSTRANS_ERR(args)      WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define   WL_BSSTRANS_ERR(args)      WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */

#if defined(BCMDBG) || defined(BCMDBG_ERR)
#define DBGERRONLY(x) x
#else
#define DBGERRONLY(x)
#endif

#ifdef EVENT_LOG_COMPILE
#ifdef USE_EVENT_LOG_RA
#define WL_ADPS_ELOG(args)		EVENT_LOG_RA(EVENT_LOG_TAG_ADPS, args)
#define WL_ADPS_ELOG_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_ADPS_INFO, args)
#else
#define WL_ADPS_ELOG(args)	\
				EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_ADPS, args)
#define WL_ADPS_ELOG_INFO(args)  \
				EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_ADPS_INFO, args)
#endif	/* USE_EVENT_LOG_RA */
#else
#define WL_ADPS_ELOG(args)		WL_ADPS(args)
#define WL_ADPS_ELOG_INFO(args)	WL_ADPS(args)
#endif	/* EVENT_LOG_COMPILE */

#ifdef WLMSG_RRM
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_RRM_DBG(args)		EVENT_LOG_RA(EVENT_LOG_TAG_RRM_DBG, args)
#define   WL_RRM_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_RRM_INFO, args)
#else
#define   WL_RRM_DBG(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_RRM_DBG, args)
#define   WL_RRM_INFO(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_RRM_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_RRM_DBG(args)		   WL_PRINT(args)
#define   WL_RRM_INFO(args)		WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_RRM_DBG(args)
#define	  WL_RRM_INFO(args)
#endif /* WLMSG_RRM */

#ifdef BCMDBG_ERR
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_RRM_ERR(args)		EVENT_LOG_RA(EVENT_LOG_TAG_RRM_ERR, args)
#else
#define   WL_RRM_ERR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_RRM_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_RRM_ERR(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define   WL_RRM_ERR(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */

#ifdef WLMSG_ESP
#if defined(EVENT_LOG_COMPILE) && defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define   WL_ESP_DBG(args)		EVENT_LOG_RA(EVENT_LOG_TAG_ESP_DBG, args)
#define   WL_ESP_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_ESP_INFO, args)
#else
#define   WL_ESP_DBG(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_ESP_DBG, args)
#define   WL_ESP_INFO(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_ESP_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define   WL_ESP_DBG(args)		   WL_PRINT(args)
#define   WL_ESP_INFO(args)		WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define	  WL_ESP_DBG(args)
#define	  WL_ESP_INFO(args)
#endif /* WLMSG_ESP */

#ifdef BCMDBG_ERR
#if defined(EVENT_LOG_COMPILE) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define   WL_ESP_ERR(args)		EVENT_LOG_RA(EVENT_LOG_TAG_ESP_ERR, args)
#else
#define   WL_ESP_ERR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_ESP_ERR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#else
#define   WL_ESP_ERR(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */
#else
#define   WL_ESP_ERR(args)		WL_PRINT(args)
#endif /* ERR_USE_EVENT_LOG */

#ifndef WL_ASSOC_DP
#define WL_ASSOC_DP(args) WL_ASSOC(args)
#endif

#ifndef WL_ROAM_DP
#define WL_ROAM_DP(args) WL_ROAM(args)
#endif

#ifndef WL_PS_DP
#define WL_PS_DP(args) WL_PS(args)
#endif

#ifndef WL_WSEC_DP
#define WL_WSEC_DP(args) WL_WSEC(args)
#endif

#ifdef EVENT_LOG_COMPILE
#ifdef USE_EVENT_LOG_RA
#define WL_EVT_NOTIF_INFO(args)	EVENT_LOG_RA(EVENT_LOG_TAG_EVT_NOTIF_INFO, args)
#else
#define WL_EVT_NOTIF_INFO(args)	 \
			EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_EVT_NOTIF_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_EVT_NOTIF_INFO(args)	WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */

#ifdef BCMDBG_ERR
#if defined(EVENT_LOG_COMPILE)
#if defined(USE_EVENT_LOG_RA)
#define WL_PKTFLTR_CNT(args)   EVENT_LOG_RA(EVENT_LOG_TAG_WL_PS_LOG, args)
#else
#define WL_PKTFLTR_CNT(args)   EVENT_LOG_FAST_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_PS_LOG, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_PKTFLTR_CNT(args)   WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */
#else
#define WL_PKTFLTR_CNT(args)
#endif /* BCMDBG_ERR */
#ifdef EVENT_LOG_COMPILE

#ifdef USE_EVENT_LOG_RA
#define WL_TDLS_INFO(args) EVENT_LOG_RA(EVENT_LOG_TAG_WL_TDLS_INFO, args)
#else
#define WL_TDLS_INFO(args) EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_WL_TDLS_INFO, args)
#endif /* USE_EVENT_LOG_RA */
#else
#define WL_TDLS_INFO(args)	WL_PRINT(args)
#endif /* EVENT_LOG_COMPILE */

#ifndef WL_OCE_INFO
#define WL_OCE_INFO(args)
#endif

#ifndef WL_OCE_ERR
#define WL_OCE_ERR(args)
#endif

#ifndef WL_MBO_INFO
#define WL_MBO_INFO(args)
#endif

#ifndef WL_FILS_ERR
#define WL_FILS_ERR(args)
#endif

#ifndef WL_FILS_DBG
#define WL_FILS_DBG(args)
#endif

#ifndef WL_FILS_INFO
#define WL_FILS_INFO(args)
#endif

/* ===============================================================
 * ====define BCMDBG_RATESET/WL_RATESET_ON()/WL_RATESET_PRT(x)====
 * ===============================================================
 */
/* 1. #define BCMDBG_RATESET explicitly turns on WL_RATESET_ON() */
#ifdef BCMDBG_RATESET
#define WL_RATESET_ON()	1
#define WL_RATESET_PRT(x) WL_PRINT(x)
#endif
/* 2. #define BCMDBG implicitly turns on BCMDBG_RATESET but not WL_RATESET_ON() */
#ifdef BCMDBG
#ifndef BCMDBG_RATESET
#define BCMDBG_RATESET
#endif
#endif /* BCMDBG */
/* 3. default WL_RATESET_ON() is 0 */
#ifndef WL_RATESET_ON
#define WL_RATESET_ON()	0
#endif
/* 4. default WL_RATESET_PRT(x) is WL_RATE(x) */
#ifndef WL_RATESET_PRT
#define WL_RATESET_PRT(x) WL_RATE(x)
#endif

#endif /* _wl_dbg_h_ */
