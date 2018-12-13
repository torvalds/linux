/*
 * Broadcom Dongle Host Driver (DHD), common DHD core.
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
 * $Id: dhd_common.c 710862 2017-07-14 07:43:59Z $
 */
#include <typedefs.h>
#include <osl.h>

#include <epivers.h>
#include <bcmutils.h>

#include <bcmendian.h>
#include <dngl_stats.h>
#include <wlioctl.h>
#include <dhd.h>
#include <dhd_ip.h>
#include <bcmevent.h>

#ifdef PCIE_FULL_DONGLE
#include <bcmmsgbuf.h>
#endif /* PCIE_FULL_DONGLE */

#ifdef SHOW_LOGTRACE
#include <event_log.h>
#endif /* SHOW_LOGTRACE */

#ifdef BCMPCIE
#include <dhd_flowring.h>
#endif

#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_config.h>
#include <bcmsdbus.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>
#include <dhd_mschdbg.h>
#include <msgtrace.h>

#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif
#ifdef RTT_SUPPORT
#include <dhd_rtt.h>
#endif

#ifdef DNGL_EVENT_SUPPORT
#include <dnglevent.h>
#endif

#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)

#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif

#ifdef DHD_WMF
#include <dhd_linux.h>
#include <dhd_wmf_linux.h>
#endif /* DHD_WMF */

#ifdef DHD_L2_FILTER
#include <dhd_l2_filter.h>
#endif /* DHD_L2_FILTER */

#ifdef DHD_PSTA
#include <dhd_psta.h>
#endif /* DHD_PSTA */
#ifdef DHD_TIMESYNC
#include <dhd_timesync.h>
#endif /* DHD_TIMESYNC */

#ifdef DHD_WET
#include <dhd_wet.h>
#endif /* DHD_WET */

#if defined(BCMEMBEDIMAGE) && defined(DHD_EFI)
#include <nvram_4364.h>
#endif

#ifdef WLMEDIA_HTSF
extern void htsf_update(struct dhd_info *dhd, void *data);
#endif

extern int is_wlc_event_frame(void *pktdata, uint pktlen, uint16 exp_usr_subtype,
	bcm_event_msg_u_t *out_event);

/* By default all logs are enabled */
int dhd_msg_level = DHD_ERROR_VAL | DHD_MSGTRACE_VAL | DHD_FWLOG_VAL;


#if defined(WL_WLC_SHIM)
#include <wl_shim.h>
#else
#endif /* WL_WLC_SHIM */

#ifdef DHD_ULP
#include <dhd_ulp.h>
#endif /* DHD_ULP */

#ifdef DHD_DEBUG
#include <sdiovar.h>
#endif /* DHD_DEBUG */

#ifdef SOFTAP
char fw_path2[MOD_PARAM_PATHLEN];
extern bool softap_enabled;
#endif

#ifdef REPORT_FATAL_TIMEOUTS
/* Default timeout value in ms */
#define SCAN_TIMEOUT_DEFAULT    1
#define JOIN_TIMEOUT_DEFAULT    7500
#ifdef DHD_EFI
#define BUS_TIMEOUT_DEFAULT     8000000  /* 800ms, in units of 100ns */
#define CMD_TIMEOUT_DEFAULT     15000000 /* 1.5sec, in units of 100ns */
#else
#define BUS_TIMEOUT_DEFAULT     800
#define CMD_TIMEOUT_DEFAULT     1200
#endif /* DHD_EFI */
#endif /* REPORT_FATAL_TIMEOUTS */

#ifdef SHOW_LOGTRACE
#define BYTES_AHEAD_NUM		11	/* address in map file is before these many bytes */
#define READ_NUM_BYTES		1000 /* read map file each time this No. of bytes */
#define GO_BACK_FILE_POS_NUM_BYTES	100 /* set file pos back to cur pos */
static char *ramstart_str = "text_start"; /* string in mapfile has addr ramstart */
static char *rodata_start_str = "rodata_start"; /* string in mapfile has addr rodata start */
static char *rodata_end_str = "rodata_end"; /* string in mapfile has addr rodata end */
#define RAMSTART_BIT	0x01
#define RDSTART_BIT		0x02
#define RDEND_BIT		0x04
#define ALL_MAP_VAL		(RAMSTART_BIT | RDSTART_BIT | RDEND_BIT)
#endif /* SHOW_LOGTRACE */

/* Last connection success/failure status */
uint32 dhd_conn_event;
uint32 dhd_conn_status;
uint32 dhd_conn_reason;

extern int dhd_iscan_request(void * dhdp, uint16 action);
extern void dhd_ind_scan_confirm(void *h, bool status);
extern int dhd_iscan_in_progress(void *h);
void dhd_iscan_lock(void);
void dhd_iscan_unlock(void);
extern int dhd_change_mtu(dhd_pub_t *dhd, int new_mtu, int ifidx);
#if !defined(AP) && defined(WLP2P)
extern int dhd_get_concurrent_capabilites(dhd_pub_t *dhd);
#endif

extern int dhd_socram_dump(struct dhd_bus *bus);

#ifdef DNGL_EVENT_SUPPORT
static void dngl_host_event_process(dhd_pub_t *dhdp, bcm_dngl_event_t *event,
	bcm_dngl_event_msg_t *dngl_event, size_t pktlen);
static int dngl_host_event(dhd_pub_t *dhdp, void *pktdata, bcm_dngl_event_msg_t *dngl_event,
	size_t pktlen);
#endif /* DNGL_EVENT_SUPPORT */

#define MAX_CHUNK_LEN 1408 /* 8 * 8 * 22 */

bool ap_cfg_running = FALSE;
bool ap_fw_loaded = FALSE;

/* Version string to report */
#ifdef DHD_DEBUG
#ifndef SRCBASE
#define SRCBASE        "drivers/net/wireless/bcmdhd"
#endif
#define DHD_COMPILED "\nCompiled in " SRCBASE
#endif /* DHD_DEBUG */

#define CHIPID_MISMATCH	8

#if defined(DHD_DEBUG)
const char dhd_version[] = "Dongle Host Driver, version " EPI_VERSION_STR;
#else
const char dhd_version[] = "\nDongle Host Driver, version " EPI_VERSION_STR;
#endif 
char fw_version[FW_VER_STR_LEN] = "\0";
char clm_version[CLM_VER_STR_LEN] = "\0";

char bus_api_revision[BUS_API_REV_STR_LEN] = "\0";

void dhd_set_timer(void *bus, uint wdtick);

#if defined(TRAFFIC_MGMT_DWM)
static int traffic_mgmt_add_dwm_filter(dhd_pub_t *dhd,
	trf_mgmt_filter_list_t * trf_mgmt_filter_list, int len);
#endif

/* IOVar table */
enum {
	IOV_VERSION = 1,
	IOV_WLMSGLEVEL,
	IOV_MSGLEVEL,
	IOV_BCMERRORSTR,
	IOV_BCMERROR,
	IOV_WDTICK,
	IOV_DUMP,
	IOV_CLEARCOUNTS,
	IOV_LOGDUMP,
	IOV_LOGCAL,
	IOV_LOGSTAMP,
	IOV_GPIOOB,
	IOV_IOCTLTIMEOUT,
	IOV_CONS,
	IOV_DCONSOLE_POLL,
#if defined(DHD_DEBUG)
	IOV_DHD_JOIN_TIMEOUT_DBG,
	IOV_SCAN_TIMEOUT,
	IOV_MEM_DEBUG,
#endif /* defined(DHD_DEBUG) */
#ifdef PROP_TXSTATUS
	IOV_PROPTXSTATUS_ENABLE,
	IOV_PROPTXSTATUS_MODE,
	IOV_PROPTXSTATUS_OPT,
	IOV_PROPTXSTATUS_MODULE_IGNORE,
	IOV_PROPTXSTATUS_CREDIT_IGNORE,
	IOV_PROPTXSTATUS_TXSTATUS_IGNORE,
	IOV_PROPTXSTATUS_RXPKT_CHK,
#endif /* PROP_TXSTATUS */
	IOV_BUS_TYPE,
#ifdef WLMEDIA_HTSF
	IOV_WLPKTDLYSTAT_SZ,
#endif
	IOV_CHANGEMTU,
	IOV_HOSTREORDER_FLOWS,
#ifdef DHDTCPACK_SUPPRESS
	IOV_TCPACK_SUPPRESS,
#endif /* DHDTCPACK_SUPPRESS */
#ifdef DHD_WMF
	IOV_WMF_BSS_ENAB,
	IOV_WMF_UCAST_IGMP,
	IOV_WMF_MCAST_DATA_SENDUP,
#ifdef WL_IGMP_UCQUERY
	IOV_WMF_UCAST_IGMP_QUERY,
#endif /* WL_IGMP_UCQUERY */
#ifdef DHD_UCAST_UPNP
	IOV_WMF_UCAST_UPNP,
#endif /* DHD_UCAST_UPNP */
	IOV_WMF_PSTA_DISABLE,
#endif /* DHD_WMF */
#if defined(TRAFFIC_MGMT_DWM)
	IOV_TRAFFIC_MGMT_DWM,
#endif 
	IOV_AP_ISOLATE,
#ifdef DHD_L2_FILTER
	IOV_DHCP_UNICAST,
	IOV_BLOCK_PING,
	IOV_PROXY_ARP,
	IOV_GRAT_ARP,
#endif /* DHD_L2_FILTER */
	IOV_DHD_IE,
#ifdef DHD_PSTA
	IOV_PSTA,
#endif /* DHD_PSTA */
#ifdef DHD_WET
	IOV_WET,
	IOV_WET_HOST_IPV4,
	IOV_WET_HOST_MAC,
#endif /* DHD_WET */
	IOV_CFG80211_OPMODE,
	IOV_ASSERT_TYPE,
	IOV_LMTEST,
#ifdef DHD_MCAST_REGEN
	IOV_MCAST_REGEN_BSS_ENABLE,
#endif
#ifdef SHOW_LOGTRACE
	IOV_DUMP_TRACE_LOG,
#endif /* SHOW_LOGTRACE */
#ifdef REPORT_FATAL_TIMEOUTS
	IOV_SCAN_TO,
	IOV_JOIN_TO,
	IOV_CMD_TO,
	IOV_OQS_TO,
#endif /* REPORT_FATAL_TIMEOUTS */
	IOV_DONGLE_TRAP_TYPE,
	IOV_DONGLE_TRAP_INFO,
	IOV_BPADDR,
	IOV_LAST,
#if defined(DHD_EFI) && defined(DHD_LOG_DUMP)
	IOV_LOG_CAPTURE_ENABLE,
	IOV_LOG_DUMP
#endif /* DHD_EFI && DHD_LOG_DUMP */
};

const bcm_iovar_t dhd_iovars[] = {
	{"version",	IOV_VERSION,	0,	0,	IOVT_BUFFER,	sizeof(dhd_version) },
	{"wlmsglevel",	IOV_WLMSGLEVEL,	0,	0,	IOVT_UINT32,	0 },
#ifdef DHD_DEBUG
	{"msglevel",	IOV_MSGLEVEL,	0,	0,	IOVT_UINT32,	0 },
	{"mem_debug",   IOV_MEM_DEBUG,  0,      0,      IOVT_BUFFER,    0 },
#endif /* DHD_DEBUG */
	{"bcmerrorstr", IOV_BCMERRORSTR, 0, 0,	IOVT_BUFFER,	BCME_STRLEN },
	{"bcmerror",	IOV_BCMERROR,	0,	0,	IOVT_INT8,	0 },
	{"wdtick",	IOV_WDTICK, 0,	0,	IOVT_UINT32,	0 },
	{"dump",	IOV_DUMP,	0,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
	{"cons",	IOV_CONS,	0,	0,	IOVT_BUFFER,	0 },
	{"dconpoll",	IOV_DCONSOLE_POLL, 0,	0,	IOVT_UINT32,	0 },
	{"clearcounts", IOV_CLEARCOUNTS, 0, 0,	IOVT_VOID,	0 },
	{"gpioob",	IOV_GPIOOB,	0,	0,	IOVT_UINT32,	0 },
	{"ioctl_timeout",	IOV_IOCTLTIMEOUT,	0,	0,	IOVT_UINT32,	0 },
#ifdef PROP_TXSTATUS
	{"proptx",	IOV_PROPTXSTATUS_ENABLE,	0,	0,	IOVT_BOOL,	0 },
	/*
	set the proptxtstatus operation mode:
	0 - Do not do any proptxtstatus flow control
	1 - Use implied credit from a packet status
	2 - Use explicit credit
	*/
	{"ptxmode",	IOV_PROPTXSTATUS_MODE,	0,	0, IOVT_UINT32,	0 },
	{"proptx_opt", IOV_PROPTXSTATUS_OPT,	0,	0, IOVT_UINT32,	0 },
	{"pmodule_ignore", IOV_PROPTXSTATUS_MODULE_IGNORE, 0, 0, IOVT_BOOL, 0 },
	{"pcredit_ignore", IOV_PROPTXSTATUS_CREDIT_IGNORE, 0, 0, IOVT_BOOL, 0 },
	{"ptxstatus_ignore", IOV_PROPTXSTATUS_TXSTATUS_IGNORE, 0, 0, IOVT_BOOL, 0 },
	{"rxpkt_chk", IOV_PROPTXSTATUS_RXPKT_CHK, 0, 0, IOVT_BOOL, 0 },
#endif /* PROP_TXSTATUS */
	{"bustype", IOV_BUS_TYPE, 0, 0, IOVT_UINT32, 0},
#ifdef WLMEDIA_HTSF
	{"pktdlystatsz", IOV_WLPKTDLYSTAT_SZ, 0, 0, IOVT_UINT8, 0 },
#endif
	{"changemtu", IOV_CHANGEMTU, 0, 0, IOVT_UINT32, 0 },
	{"host_reorder_flows", IOV_HOSTREORDER_FLOWS, 0, 0, IOVT_BUFFER,
	(WLHOST_REORDERDATA_MAXFLOWS + 1) },
#ifdef DHDTCPACK_SUPPRESS
	{"tcpack_suppress",	IOV_TCPACK_SUPPRESS,	0,	0, IOVT_UINT8,	0 },
#endif /* DHDTCPACK_SUPPRESS */
#ifdef DHD_WMF
	{"wmf_bss_enable", IOV_WMF_BSS_ENAB,	0,	0, IOVT_BOOL,	0 },
	{"wmf_ucast_igmp", IOV_WMF_UCAST_IGMP,	0,	0, IOVT_BOOL,	0 },
	{"wmf_mcast_data_sendup", IOV_WMF_MCAST_DATA_SENDUP,	0,	0, IOVT_BOOL,	0 },
#ifdef WL_IGMP_UCQUERY
	{"wmf_ucast_igmp_query", IOV_WMF_UCAST_IGMP_QUERY, (0), 0, IOVT_BOOL, 0 },
#endif /* WL_IGMP_UCQUERY */
#ifdef DHD_UCAST_UPNP
	{"wmf_ucast_upnp", IOV_WMF_UCAST_UPNP, (0), 0, IOVT_BOOL, 0 },
#endif /* DHD_UCAST_UPNP */
	{"wmf_psta_disable", IOV_WMF_PSTA_DISABLE, (0), 0, IOVT_BOOL, 0 },
#endif /* DHD_WMF */
#if defined(TRAFFIC_MGMT_DWM)
	{"trf_mgmt_filters_add", IOV_TRAFFIC_MGMT_DWM, (0), 0, IOVT_BUFFER, 0},
#endif 
#ifdef DHD_L2_FILTER
	{"dhcp_unicast", IOV_DHCP_UNICAST, (0), 0, IOVT_BOOL, 0 },
#endif /* DHD_L2_FILTER */
	{"ap_isolate", IOV_AP_ISOLATE, (0), 0, IOVT_BOOL, 0},
#ifdef DHD_L2_FILTER
	{"block_ping", IOV_BLOCK_PING, (0), 0, IOVT_BOOL, 0},
	{"proxy_arp", IOV_PROXY_ARP, (0), 0, IOVT_BOOL, 0},
	{"grat_arp", IOV_GRAT_ARP, (0), 0, IOVT_BOOL, 0},
#endif /* DHD_L2_FILTER */
	{"dhd_ie", IOV_DHD_IE, (0), 0, IOVT_BUFFER, 0},
#ifdef DHD_PSTA
	/* PSTA/PSR Mode configuration. 0: DIABLED 1: PSTA 2: PSR */
	{"psta", IOV_PSTA, 0, 0, IOVT_UINT32, 0},
#endif /* DHD PSTA */
#ifdef DHD_WET
	/* WET Mode configuration. 0: DIABLED 1: WET */
	{"wet", IOV_WET, 0, 0, IOVT_UINT32, 0},
	{"wet_host_ipv4", IOV_WET_HOST_IPV4, 0, 0, IOVT_UINT32, 0},
	{"wet_host_mac", IOV_WET_HOST_MAC, 0, 0, IOVT_BUFFER, 0},
#endif /* DHD WET */
	{"op_mode",	IOV_CFG80211_OPMODE,	0,	0, IOVT_UINT32,	0 },
	{"assert_type", IOV_ASSERT_TYPE, (0), 0, IOVT_UINT32, 0},
	{"lmtest", IOV_LMTEST,	0,	0, IOVT_UINT32,	0 },
#ifdef DHD_MCAST_REGEN
	{"mcast_regen_bss_enable", IOV_MCAST_REGEN_BSS_ENABLE, 0, 0, IOVT_BOOL, 0},
#endif
#ifdef SHOW_LOGTRACE
	{"dump_trace_buf", IOV_DUMP_TRACE_LOG,	0, 0, IOVT_BUFFER,	sizeof(trace_buf_info_t) },
#endif /* SHOW_LOGTRACE */
#ifdef REPORT_FATAL_TIMEOUTS
	{"scan_timeout", IOV_SCAN_TO, 0, 0, IOVT_UINT32, 0 },
	{"join_timeout", IOV_JOIN_TO, 0, 0, IOVT_UINT32, 0 },
	{"cmd_timeout", IOV_CMD_TO, 0, 0, IOVT_UINT32, 0 },
	{"oqs_timeout", IOV_OQS_TO, 0, 0, IOVT_UINT32, 0 },
#endif /* REPORT_FATAL_TIMEOUTS */
	{"trap_type", IOV_DONGLE_TRAP_TYPE, 0, 0, IOVT_UINT32, 0 },
	{"trap_info", IOV_DONGLE_TRAP_INFO, 0, 0, IOVT_BUFFER, sizeof(trap_t) },
#ifdef DHD_DEBUG
	{"bpaddr", IOV_BPADDR,	0, 0, IOVT_BUFFER,	sizeof(sdreg_t) },
#endif /* DHD_DEBUG */
#if defined(DHD_EFI) && defined(DHD_LOG_DUMP)
	{"log_capture_enable", IOV_LOG_CAPTURE_ENABLE, 0, 0, IOVT_UINT8, 0},
	{"log_dump", IOV_LOG_DUMP,	0, 0, IOVT_UINT8, 0},
#endif /* DHD_EFI && DHD_LOG_DUMP */
	{NULL, 0, 0, 0, 0, 0 }
};

#define DHD_IOVAR_BUF_SIZE	128

bool
dhd_query_bus_erros(dhd_pub_t *dhdp)
{
	bool ret = FALSE;

	if (dhdp->dongle_reset) {
		DHD_ERROR(("%s: Dongle Reset occurred, cannot proceed\n",
			__FUNCTION__));
		ret = TRUE;
	}

	if (dhdp->dongle_trap_occured) {
		DHD_ERROR(("%s: FW TRAP has occurred, cannot proceed\n",
			__FUNCTION__));
		ret = TRUE;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
		dhdp->hang_reason = HANG_REASON_DONGLE_TRAP;
		dhd_os_send_hang_message(dhdp);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) */
	}

	if (dhdp->iovar_timeout_occured) {
		DHD_ERROR(("%s: Resumed on timeout for previous IOVAR, cannot proceed\n",
			__FUNCTION__));
		ret = TRUE;
	}

#ifdef PCIE_FULL_DONGLE
	if (dhdp->d3ack_timeout_occured) {
		DHD_ERROR(("%s: Resumed on timeout for previous D3ACK, cannot proceed\n",
			__FUNCTION__));
		ret = TRUE;
	}
#endif /* PCIE_FULL_DONGLE */

	return ret;
}

#ifdef DHD_SSSR_DUMP
int
dhd_sssr_mempool_init(dhd_pub_t *dhd)
{
	dhd->sssr_mempool = (uint8 *) MALLOCZ(dhd->osh, DHD_SSSR_MEMPOOL_SIZE);
	if (dhd->sssr_mempool == NULL) {
		DHD_ERROR(("%s: MALLOC of sssr_mempool failed\n",
			__FUNCTION__));
		return BCME_ERROR;
	}
	return BCME_OK;
}

void
dhd_sssr_mempool_deinit(dhd_pub_t *dhd)
{
	if (dhd->sssr_mempool) {
		MFREE(dhd->osh, dhd->sssr_mempool, DHD_SSSR_MEMPOOL_SIZE);
		dhd->sssr_mempool = NULL;
	}
}

int
dhd_get_sssr_reg_info(dhd_pub_t *dhd)
{
	int ret = BCME_ERROR;

	DHD_ERROR(("%s: get sssr_reg_info\n", __FUNCTION__));
	/* get sssr_reg_info from firmware */
	memset((void *)&dhd->sssr_reg_info, 0, sizeof(dhd->sssr_reg_info));
	if (bcm_mkiovar("sssr_reg_info", 0, 0, (char *)&dhd->sssr_reg_info,
		sizeof(dhd->sssr_reg_info))) {
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, &dhd->sssr_reg_info,
			sizeof(dhd->sssr_reg_info), FALSE, 0)) < 0) {
			DHD_ERROR(("%s: dhd_wl_ioctl_cmd failed (error=%d)\n", __FUNCTION__, ret));
		}
	} else {
			DHD_ERROR(("%s: bcm_mkiovar failed\n", __FUNCTION__));
	}

	return ret;
}

uint32
dhd_get_sssr_bufsize(dhd_pub_t *dhd)
{
	int i;
	uint32 sssr_bufsize = 0;
	/* Init all pointers to NULL */
	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		sssr_bufsize += dhd->sssr_reg_info.mac_regs[i].sr_size;
	}
	sssr_bufsize += dhd->sssr_reg_info.vasip_regs.vasip_sr_size;

	/* Double the size as different dumps will be saved before and after SR */
	sssr_bufsize = 2 * sssr_bufsize;

	return sssr_bufsize;
}

int
dhd_sssr_dump_init(dhd_pub_t *dhd)
{
	int i;
	uint32 sssr_bufsize;
	uint32 mempool_used = 0;

	dhd->sssr_inited = FALSE;

	/* check if sssr mempool is allocated */
	if (dhd->sssr_mempool == NULL) {
		DHD_ERROR(("%s: sssr_mempool is not allocated\n",
			__FUNCTION__));
		return BCME_ERROR;
	}

	/* Get SSSR reg info */
	if (dhd_get_sssr_reg_info(dhd) != BCME_OK) {
		DHD_ERROR(("%s: dhd_get_sssr_reg_info failed\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* Validate structure version */
	if (dhd->sssr_reg_info.version != SSSR_REG_INFO_VER) {
		DHD_ERROR(("%s: dhd->sssr_reg_info.version (%d : %d) mismatch\n",
			__FUNCTION__, (int)dhd->sssr_reg_info.version, SSSR_REG_INFO_VER));
		return BCME_ERROR;
	}

	/* Validate structure length */
	if (dhd->sssr_reg_info.length != sizeof(dhd->sssr_reg_info)) {
		DHD_ERROR(("%s: dhd->sssr_reg_info.length (%d : %d) mismatch\n",
			__FUNCTION__, (int)dhd->sssr_reg_info.length,
			(int)sizeof(dhd->sssr_reg_info)));
		return BCME_ERROR;
	}

	/* validate fifo size */
	sssr_bufsize = dhd_get_sssr_bufsize(dhd);
	if (sssr_bufsize > DHD_SSSR_MEMPOOL_SIZE) {
		DHD_ERROR(("%s: sssr_bufsize(%d) is greater than sssr_mempool(%d)\n",
			__FUNCTION__, (int)sssr_bufsize, DHD_SSSR_MEMPOOL_SIZE));
		return BCME_ERROR;
	}

	/* init all pointers to NULL */
	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		dhd->sssr_d11_before[i] = NULL;
		dhd->sssr_d11_after[i] = NULL;
	}
	dhd->sssr_vasip_buf_before = NULL;
	dhd->sssr_vasip_buf_after = NULL;

	/* Allocate memory */
	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		if (dhd->sssr_reg_info.mac_regs[i].sr_size) {
			dhd->sssr_d11_before[i] = (uint32 *)(dhd->sssr_mempool + mempool_used);
			mempool_used += dhd->sssr_reg_info.mac_regs[i].sr_size;

			dhd->sssr_d11_after[i] = (uint32 *)(dhd->sssr_mempool + mempool_used);
			mempool_used += dhd->sssr_reg_info.mac_regs[i].sr_size;
		}
	}

	if (dhd->sssr_reg_info.vasip_regs.vasip_sr_size) {
		dhd->sssr_vasip_buf_before = (uint32 *)(dhd->sssr_mempool + mempool_used);
		mempool_used += dhd->sssr_reg_info.vasip_regs.vasip_sr_size;

		dhd->sssr_vasip_buf_after = (uint32 *)(dhd->sssr_mempool + mempool_used);
		mempool_used += dhd->sssr_reg_info.vasip_regs.vasip_sr_size;
	}

	dhd->sssr_inited = TRUE;

	return BCME_OK;

}

void
dhd_sssr_dump_deinit(dhd_pub_t *dhd)
{
	int i;

	dhd->sssr_inited = FALSE;
	/* init all pointers to NULL */
	for (i = 0; i < MAX_NUM_D11CORES; i++) {
		dhd->sssr_d11_before[i] = NULL;
		dhd->sssr_d11_after[i] = NULL;
	}
	dhd->sssr_vasip_buf_before = NULL;
	dhd->sssr_vasip_buf_after = NULL;

	return;
}

#endif /* DHD_SSSR_DUMP */

#ifdef DHD_FW_COREDUMP
void* dhd_get_fwdump_buf(dhd_pub_t *dhd_pub, uint32 length)
{
	if (!dhd_pub->soc_ram) {
#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_MEMDUMP)
		dhd_pub->soc_ram = (uint8*)DHD_OS_PREALLOC(dhd_pub,
			DHD_PREALLOC_MEMDUMP_RAM, length);
#else
		dhd_pub->soc_ram = (uint8*) MALLOC(dhd_pub->osh, length);
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_MEMDUMP */
	}

	if (dhd_pub->soc_ram == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory for fw crash snap shot.\n",
			__FUNCTION__));
		dhd_pub->soc_ram_length = 0;
	} else {
		memset(dhd_pub->soc_ram, 0, length);
		dhd_pub->soc_ram_length = length;
	}

	/* soc_ram free handled in dhd_{free,clear} */
	return dhd_pub->soc_ram;
}
#endif /* DHD_FW_COREDUMP */

/* to NDIS developer, the structure dhd_common is redundant,
 * please do NOT merge it back from other branches !!!
 */

int
dhd_common_socram_dump(dhd_pub_t *dhdp)
{
#ifdef BCMDBUS
	return 0;
#else
	return dhd_socram_dump(dhdp->bus);
#endif /* BCMDBUS */
}

static int
dhd_dump(dhd_pub_t *dhdp, char *buf, int buflen)
{
	char eabuf[ETHER_ADDR_STR_LEN];

	struct bcmstrbuf b;
	struct bcmstrbuf *strbuf = &b;

	if (!dhdp || !dhdp->prot || !buf) {
		return BCME_ERROR;
	}

	bcm_binit(strbuf, buf, buflen);

	/* Base DHD info */
	bcm_bprintf(strbuf, "%s\n", dhd_version);
	bcm_bprintf(strbuf, "\n");
	bcm_bprintf(strbuf, "pub.up %d pub.txoff %d pub.busstate %d\n",
	            dhdp->up, dhdp->txoff, dhdp->busstate);
	bcm_bprintf(strbuf, "pub.hdrlen %u pub.maxctl %u pub.rxsz %u\n",
	            dhdp->hdrlen, dhdp->maxctl, dhdp->rxsz);
	bcm_bprintf(strbuf, "pub.iswl %d pub.drv_version %ld pub.mac %s\n",
	            dhdp->iswl, dhdp->drv_version, bcm_ether_ntoa(&dhdp->mac, eabuf));
	bcm_bprintf(strbuf, "pub.bcmerror %d tickcnt %u\n", dhdp->bcmerror, dhdp->tickcnt);

	bcm_bprintf(strbuf, "dongle stats:\n");
	bcm_bprintf(strbuf, "tx_packets %lu tx_bytes %lu tx_errors %lu tx_dropped %lu\n",
	            dhdp->dstats.tx_packets, dhdp->dstats.tx_bytes,
	            dhdp->dstats.tx_errors, dhdp->dstats.tx_dropped);
	bcm_bprintf(strbuf, "rx_packets %lu rx_bytes %lu rx_errors %lu rx_dropped %lu\n",
	            dhdp->dstats.rx_packets, dhdp->dstats.rx_bytes,
	            dhdp->dstats.rx_errors, dhdp->dstats.rx_dropped);
	bcm_bprintf(strbuf, "multicast %lu\n", dhdp->dstats.multicast);

	bcm_bprintf(strbuf, "bus stats:\n");
	bcm_bprintf(strbuf, "tx_packets %lu  tx_dropped %lu tx_multicast %lu tx_errors %lu\n",
	            dhdp->tx_packets, dhdp->tx_dropped, dhdp->tx_multicast, dhdp->tx_errors);
	bcm_bprintf(strbuf, "tx_ctlpkts %lu tx_ctlerrs %lu\n",
	            dhdp->tx_ctlpkts, dhdp->tx_ctlerrs);
	bcm_bprintf(strbuf, "rx_packets %lu rx_multicast %lu rx_errors %lu \n",
	            dhdp->rx_packets, dhdp->rx_multicast, dhdp->rx_errors);
	bcm_bprintf(strbuf, "rx_ctlpkts %lu rx_ctlerrs %lu rx_dropped %lu\n",
	            dhdp->rx_ctlpkts, dhdp->rx_ctlerrs, dhdp->rx_dropped);
	bcm_bprintf(strbuf, "rx_readahead_cnt %lu tx_realloc %lu\n",
	            dhdp->rx_readahead_cnt, dhdp->tx_realloc);
	bcm_bprintf(strbuf, "tx_pktgetfail %lu rx_pktgetfail %lu\n",
	            dhdp->tx_pktgetfail, dhdp->rx_pktgetfail);
	bcm_bprintf(strbuf, "\n");

#ifdef DMAMAP_STATS
	/* Add DMA MAP info */
	bcm_bprintf(strbuf, "DMA MAP stats: \n");
	bcm_bprintf(strbuf, "txdata: %lu size: %luK, rxdata: %lu size: %luK\n",
			dhdp->dma_stats.txdata, KB(dhdp->dma_stats.txdata_sz),
			dhdp->dma_stats.rxdata, KB(dhdp->dma_stats.rxdata_sz));
#ifndef IOCTLRESP_USE_CONSTMEM
	bcm_bprintf(strbuf, "IOCTL RX: %lu size: %luK ,",
			dhdp->dma_stats.ioctl_rx, KB(dhdp->dma_stats.ioctl_rx_sz));
#endif /* !IOCTLRESP_USE_CONSTMEM */
	bcm_bprintf(strbuf, "EVENT RX: %lu size: %luK, INFO RX: %lu size: %luK, "
			"TSBUF RX: %lu size %luK\n",
			dhdp->dma_stats.event_rx, KB(dhdp->dma_stats.event_rx_sz),
			dhdp->dma_stats.info_rx, KB(dhdp->dma_stats.info_rx_sz),
			dhdp->dma_stats.tsbuf_rx, KB(dhdp->dma_stats.tsbuf_rx_sz));
	bcm_bprintf(strbuf, "Total : %luK \n",
			KB(dhdp->dma_stats.txdata_sz + dhdp->dma_stats.rxdata_sz +
			dhdp->dma_stats.ioctl_rx_sz + dhdp->dma_stats.event_rx_sz +
			dhdp->dma_stats.tsbuf_rx_sz));
#endif /* DMAMAP_STATS */

	/* Add any prot info */
	dhd_prot_dump(dhdp, strbuf);
	bcm_bprintf(strbuf, "\n");

	/* Add any bus info */
	dhd_bus_dump(dhdp, strbuf);


#if defined(DHD_LB_STATS)
	dhd_lb_stats_dump(dhdp, strbuf);
#endif /* DHD_LB_STATS */
#ifdef DHD_WET
	if (dhd_get_wet_mode(dhdp)) {
		bcm_bprintf(strbuf, "Wet Dump:\n");
		dhd_wet_dump(dhdp, strbuf);
		}
#endif /* DHD_WET */
	return (!strbuf->size ? BCME_BUFTOOSHORT : 0);
}

void
dhd_dump_to_kernelog(dhd_pub_t *dhdp)
{
	char buf[512];

	DHD_ERROR(("F/W version: %s\n", fw_version));
	bcm_bprintf_bypass = TRUE;
	dhd_dump(dhdp, buf, sizeof(buf));
	bcm_bprintf_bypass = FALSE;
}

int
dhd_wl_ioctl_cmd(dhd_pub_t *dhd_pub, int cmd, void *arg, int len, uint8 set, int ifidx)
{
	wl_ioctl_t ioc;

	ioc.cmd = cmd;
	ioc.buf = arg;
	ioc.len = len;
	ioc.set = set;

	return dhd_wl_ioctl(dhd_pub, ifidx, &ioc, arg, len);
}

int
dhd_wl_ioctl_get_intiovar(dhd_pub_t *dhd_pub, char *name, uint *pval,
	int cmd, uint8 set, int ifidx)
{
	char iovbuf[WLC_IOCTL_SMLEN];
	int ret = -1;

	memset(iovbuf, 0, sizeof(iovbuf));
	if (bcm_mkiovar(name, NULL, 0, iovbuf, sizeof(iovbuf))) {
		ret = dhd_wl_ioctl_cmd(dhd_pub, cmd, iovbuf, sizeof(iovbuf), set, ifidx);
		if (!ret) {
			*pval = ltoh32(*((uint*)iovbuf));
		} else {
			DHD_ERROR(("%s: get int iovar %s failed, ERR %d\n",
				__FUNCTION__, name, ret));
		}
	} else {
		DHD_ERROR(("%s: mkiovar %s failed\n",
			__FUNCTION__, name));
	}

	return ret;
}

int
dhd_wl_ioctl_set_intiovar(dhd_pub_t *dhd_pub, char *name, uint val,
	int cmd, uint8 set, int ifidx)
{
	char iovbuf[WLC_IOCTL_SMLEN] = {0};
	int ret = -1;
	int lval = htol32(val);
	uint len;

	len = bcm_mkiovar(name, (char*)&lval, sizeof(lval), iovbuf, sizeof(iovbuf));

	if (len) {
		ret = dhd_wl_ioctl_cmd(dhd_pub, cmd, iovbuf, len, set, ifidx);
		if (ret) {
			DHD_ERROR(("%s: set int iovar %s failed, ERR %d\n",
				__FUNCTION__, name, ret));
		}
	} else {
		DHD_ERROR(("%s: mkiovar %s failed\n",
			__FUNCTION__, name));
	}

	return ret;
}

int
dhd_wl_ioctl(dhd_pub_t *dhd_pub, int ifidx, wl_ioctl_t *ioc, void *buf, int len)
{
	int ret = BCME_ERROR;
	unsigned long flags;
#ifdef DUMP_IOCTL_IOV_LIST
	dhd_iov_li_t *iov_li;
#endif /* DUMP_IOCTL_IOV_LIST */
#ifdef KEEPIF_ON_DEVICE_RESET
		if (ioc->cmd == WLC_GET_VAR) {
			dbus_config_t config;
			config.general_param = 0;
			if (!strcmp(buf, "wowl_activate")) {
				config.general_param = 2; /* 1 (TRUE) after decreased by 1 */
			} else if (!strcmp(buf, "wowl_clear")) {
				config.general_param = 1; /* 0 (FALSE) after decreased by 1 */
			}
			if (config.general_param) {
				config.config_id = DBUS_CONFIG_ID_KEEPIF_ON_DEVRESET;
				config.general_param--;
				dbus_set_config(dhd_pub->dbus, &config);
			}
		}
#endif /* KEEPIF_ON_DEVICE_RESET */

	if (dhd_os_proto_block(dhd_pub))
	{
#ifdef DHD_LOG_DUMP
		int slen, i, val, rem, lval, min_len;
		char *pval, *pos, *msg;
		char tmp[64];

		/* WLC_GET_VAR */
		if (ioc->cmd == WLC_GET_VAR) {
			min_len = MIN(sizeof(tmp) - 1, strlen(buf));
			memset(tmp, 0, sizeof(tmp));
			bcopy(buf, tmp, min_len);
			tmp[min_len] = '\0';
		}
#endif /* DHD_LOG_DUMP */
		DHD_LINUX_GENERAL_LOCK(dhd_pub, flags);
		if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhd_pub)) {
#ifdef DHD_EFI
			DHD_INFO(("%s: returning as busstate=%d\n",
				__FUNCTION__, dhd_pub->busstate));
#else
			DHD_ERROR(("%s: returning as busstate=%d\n",
				__FUNCTION__, dhd_pub->busstate));
#endif /* DHD_EFI */
			DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);
			dhd_os_proto_unblock(dhd_pub);
			return -ENODEV;
		}
		DHD_BUS_BUSY_SET_IN_IOVAR(dhd_pub);
		DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);

#ifdef DHD_PCIE_RUNTIMEPM
		dhdpcie_runtime_bus_wake(dhd_pub, TRUE, dhd_wl_ioctl);
#endif /* DHD_PCIE_RUNTIMEPM */

		DHD_LINUX_GENERAL_LOCK(dhd_pub, flags);
		if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(dhd_pub)) {
			DHD_ERROR(("%s: bus is in suspend(%d) or suspending(0x%x) state!!\n",
				__FUNCTION__, dhd_pub->busstate, dhd_pub->dhd_bus_busy_state));
			DHD_BUS_BUSY_CLEAR_IN_IOVAR(dhd_pub);
			dhd_os_busbusy_wake(dhd_pub);
			DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);
			dhd_os_proto_unblock(dhd_pub);
			return -ENODEV;
		}
		DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);

#if defined(WL_WLC_SHIM)
		{
			struct wl_shim_node *shim = dhd_pub_shim(dhd_pub);

			wl_io_pport_t io_pport;
			io_pport.dhd_pub = dhd_pub;
			io_pport.ifidx = ifidx;

			ret = wl_shim_ioctl(shim, ioc, len, &io_pport);
			if (ret != BCME_OK) {
				DHD_TRACE(("%s: wl_shim_ioctl(%d) ERR %d\n",
					__FUNCTION__, ioc->cmd, ret));
			}
		}
#else
#ifdef DUMP_IOCTL_IOV_LIST
		if (ioc->cmd != WLC_GET_MAGIC && ioc->cmd != WLC_GET_VERSION && buf) {
			if (!(iov_li = MALLOC(dhd_pub->osh, sizeof(*iov_li)))) {
				DHD_ERROR(("iovar dump list item allocation Failed\n"));
			} else {
				iov_li->cmd = ioc->cmd;
				bcopy((char *)buf, iov_li->buff, strlen((char *)buf)+1);
				dhd_iov_li_append(dhd_pub, &dhd_pub->dump_iovlist_head,
					&iov_li->list);
			}
		}
#endif /* DUMP_IOCTL_IOV_LIST */
		ret = dhd_prot_ioctl(dhd_pub, ifidx, ioc, buf, len);
#ifdef DUMP_IOCTL_IOV_LIST
		if (ret == -ETIMEDOUT) {
			DHD_ERROR(("Last %d issued commands: Latest one is at bottom.\n",
				IOV_LIST_MAX_LEN));
			dhd_iov_li_print(&dhd_pub->dump_iovlist_head);
		}
#endif /* DUMP_IOCTL_IOV_LIST */
#endif /* defined(WL_WLC_SHIM) */
#ifdef DHD_LOG_DUMP
		if (ioc->cmd == WLC_GET_VAR || ioc->cmd == WLC_SET_VAR) {
			lval = 0;
			slen = strlen(buf) + 1;
			msg = (char*)buf;
			if (len >= slen + sizeof(lval)) {
				if (ioc->cmd == WLC_GET_VAR) {
					msg = tmp;
					lval = *(int*)buf;
				} else {
					min_len = MIN(ioc->len - slen, sizeof(int));
					bcopy((msg + slen), &lval, min_len);
				}
			}
			DHD_ERROR_MEM(("%s: cmd: %d, msg: %s, val: 0x%x, len: %d, set: %d\n",
				ioc->cmd == WLC_GET_VAR ? "WLC_GET_VAR" : "WLC_SET_VAR",
				ioc->cmd, msg, lval, ioc->len, ioc->set));
		} else {
			slen = ioc->len;
			if (buf != NULL) {
				val = *(int*)buf;
				pval = (char*)buf;
				pos = tmp;
				rem = sizeof(tmp);
				memset(tmp, 0, sizeof(tmp));
				for (i = 0; i < slen; i++) {
					if (rem <= 3) {
						/* At least 2 byte required + 1 byte(NULL) */
						break;
					}
					pos += snprintf(pos, rem, "%02x ", pval[i]);
					rem = sizeof(tmp) - (int)(pos - tmp);
				}
				/* Do not dump for WLC_GET_MAGIC and WLC_GET_VERSION */
				if (ioc->cmd != WLC_GET_MAGIC && ioc->cmd != WLC_GET_VERSION)
					DHD_ERROR_MEM(("WLC_IOCTL: cmd: %d, val: %d(%s), "
						"len: %d, set: %d\n",
						ioc->cmd, val, tmp, ioc->len, ioc->set));
			} else {
				DHD_ERROR_MEM(("WLC_IOCTL: cmd: %d, buf is NULL\n", ioc->cmd));
			}
		}
#endif /* DHD_LOG_DUMP */
		if (ret && dhd_pub->up) {
			/* Send hang event only if dhd_open() was success */
			dhd_os_check_hang(dhd_pub, ifidx, ret);
		}

		if (ret == -ETIMEDOUT && !dhd_pub->up) {
			DHD_ERROR(("%s: 'resumed on timeout' error is "
				"occurred before the interface does not"
				" bring up\n", __FUNCTION__));
			dhd_pub->busstate = DHD_BUS_DOWN;
		}

		DHD_LINUX_GENERAL_LOCK(dhd_pub, flags);
		DHD_BUS_BUSY_CLEAR_IN_IOVAR(dhd_pub);
		dhd_os_busbusy_wake(dhd_pub);
		DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);

		dhd_os_proto_unblock(dhd_pub);

	}

	return ret;
}

uint wl_get_port_num(wl_io_pport_t *io_pport)
{
	return 0;
}

/* Get bssidx from iovar params
 * Input:   dhd_pub - pointer to dhd_pub_t
 *	    params  - IOVAR params
 * Output:  idx	    - BSS index
 *	    val	    - ponter to the IOVAR arguments
 */
static int
dhd_iovar_parse_bssidx(dhd_pub_t *dhd_pub, const char *params, uint32 *idx, const char **val)
{
	char *prefix = "bsscfg:";
	uint32	bssidx;

	if (!(strncmp(params, prefix, strlen(prefix)))) {
		/* per bss setting should be prefixed with 'bsscfg:' */
		const char *p = params + strlen(prefix);

		/* Skip Name */
		while (*p != '\0')
			p++;
		/* consider null */
		p = p + 1;
		bcopy(p, &bssidx, sizeof(uint32));
		/* Get corresponding dhd index */
		bssidx = dhd_bssidx2idx(dhd_pub, htod32(bssidx));

		if (bssidx >= DHD_MAX_IFS) {
			DHD_ERROR(("%s Wrong bssidx provided\n", __FUNCTION__));
			return BCME_ERROR;
		}

		/* skip bss idx */
		p += sizeof(uint32);
		*val = p;
		*idx = bssidx;
	} else {
		DHD_ERROR(("%s: bad parameter for per bss iovar\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return BCME_OK;
}

#if defined(DHD_DEBUG) && defined(BCMDBUS)
/* USB Device console input function */
int dhd_bus_console_in(dhd_pub_t *dhd, uchar *msg, uint msglen)
{
	DHD_TRACE(("%s \n", __FUNCTION__));

	return dhd_iovar(dhd, 0, "cons", msg, msglen, NULL, 0, TRUE);

}
#endif /* DHD_DEBUG && BCMDBUS  */

#ifdef DHD_DEBUG
int
dhd_mem_debug(dhd_pub_t *dhd, char *msg, uint msglen)
{
	unsigned long int_arg = 0;
	char *p;
	char *end_ptr = NULL;
	dhd_dbg_mwli_t *mw_li;
	dll_t *item, *next;
	/* check if mwalloc, mwquery or mwfree was supplied arguement with space */
	p = bcmstrstr(msg, " ");
	if (p != NULL) {
		/* space should be converted to null as separation flag for firmware */
		*p = '\0';
		/* store the argument in int_arg */
		int_arg = bcm_strtoul(p+1, &end_ptr, 10);
	}

	if (!p && !strcmp(msg, "query")) {
		/* lets query the list inetrnally */
		if (dll_empty(dll_head_p(&dhd->mw_list_head))) {
			DHD_ERROR(("memwaste list is empty, call mwalloc < size > to allocate\n"));
			/* reset the id */
			dhd->mw_id = 0;
		} else {
			for (item = dll_head_p(&dhd->mw_list_head);
					!dll_end(&dhd->mw_list_head, item); item = next) {
				next = dll_next_p(item);
				mw_li = (dhd_dbg_mwli_t *)CONTAINEROF(item, dhd_dbg_mwli_t, list);
				DHD_ERROR(("item: <id=%d, size=%d>\n", mw_li->id, mw_li->size));
			}
		}
	} else if (p && end_ptr && (*end_ptr == '\0') && !strcmp(msg, "alloc")) {
		int32 alloc_handle;
		/* convert size into KB and append as integer */
		*((int32 *)(p+1)) = int_arg*1024;
		*(p+1+sizeof(int32)) = '\0';

		/* recalculated length -> 5 bytes for "alloc" + 4 bytes for size +
		 *1 bytes for null caracter
		 */
		msglen = strlen(msg) + sizeof(int32) + 1;
		if (dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, msg, msglen, FALSE, 0) < 0) {
			DHD_ERROR(("IOCTL failed for memdebug alloc\n"));
		}

		/* returned allocated handle from dongle, basically address of the allocated unit */
		alloc_handle = *((int32 *)msg);

		/* add a node in the list with tuple <id, handle, size> */
		if (alloc_handle == 0) {
			DHD_ERROR(("Reuqested size could not be allocated\n"));
		} else if (!(mw_li = MALLOC(dhd->osh, sizeof(*mw_li)))) {
			DHD_ERROR(("mw list item allocation Failed\n"));
		} else {
			mw_li->id = dhd->mw_id++;
			mw_li->handle = alloc_handle;
			mw_li->size = int_arg;
			/* append the node in the list */
			dll_append(&dhd->mw_list_head, &mw_li->list);
		}
	} else if (p && end_ptr && (*end_ptr == '\0') && !strcmp(msg, "free")) {
		/* inform dongle to free wasted chunk */
		int handle = 0;
		int size = 0;
		for (item = dll_head_p(&dhd->mw_list_head);
				!dll_end(&dhd->mw_list_head, item); item = next) {
			next = dll_next_p(item);
			mw_li = (dhd_dbg_mwli_t *)CONTAINEROF(item, dhd_dbg_mwli_t, list);

			if (mw_li->id == (int)int_arg) {
				handle = mw_li->handle;
				size = mw_li->size;
				dll_delete(item);
				MFREE(dhd->osh, mw_li, sizeof(*mw_li));
			}
		}
		if (handle) {
			int len;
			/* append the free handle and the chunk size in first 8 bytes
			 * after the command and null character
			 */
			*((int32 *)(p+1)) = handle;
			*((int32 *)((p+1)+sizeof(int32))) = size;
			/* append null as terminator */
			*(p+1+2*sizeof(int32)) = '\0';
			/* recalculated length -> 4 bytes for "free" + 8 bytes for hadnle and size
			 * + 1 bytes for null caracter
			 */
			len = strlen(msg) + 2*sizeof(int32) + 1;
			/* send iovar to free the chunk */
			if (dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, msg, len, FALSE, 0) < 0) {
				DHD_ERROR(("IOCTL failed for memdebug free\n"));
			}
		} else {
			DHD_ERROR(("specified id does not exist\n"));
		}
	} else {
		/* for all the wrong argument formats */
		return BCME_BADARG;
	}
	return 0;
}

extern void
dhd_mw_list_delete(dhd_pub_t *dhd, dll_t *list_head)
{
	dll_t *item;
	dhd_dbg_mwli_t *mw_li;
	while (!(dll_empty(list_head))) {
		item = dll_head_p(list_head);
		mw_li = (dhd_dbg_mwli_t *)CONTAINEROF(item, dhd_dbg_mwli_t, list);
		dll_delete(item);
		MFREE(dhd->osh, mw_li, sizeof(*mw_li));
	}
}
#endif /* DHD_DEBUG */

#ifdef PKT_STATICS
extern pkt_statics_t tx_statics;
extern void dhdsdio_txpktstatics(void);
#endif

static int
dhd_doiovar(dhd_pub_t *dhd_pub, const bcm_iovar_t *vi, uint32 actionid, const char *name,
            void *params, int plen, void *arg, int len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;
	uint32 dhd_ver_len, bus_api_rev_len;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	DHD_TRACE(("%s: actionid = %d; name %s\n", __FUNCTION__, actionid, name));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	switch (actionid) {
	case IOV_GVAL(IOV_VERSION):
		/* Need to have checked buffer length */
		dhd_ver_len = strlen(dhd_version);
		bus_api_rev_len = strlen(bus_api_revision);
		if (dhd_ver_len)
			bcm_strncpy_s((char*)arg, dhd_ver_len, dhd_version, dhd_ver_len);
		if (bus_api_rev_len)
			bcm_strncat_s((char*)arg + dhd_ver_len, bus_api_rev_len, bus_api_revision,
				bus_api_rev_len);
#ifdef PKT_STATICS
		memset((uint8*) &tx_statics, 0, sizeof(pkt_statics_t));
#endif
		break;

	case IOV_GVAL(IOV_WLMSGLEVEL):
		printf("android_msg_level=0x%x\n", android_msg_level);
		printf("config_msg_level=0x%x\n", config_msg_level);
#if defined(WL_WIRELESS_EXT)
		int_val = (int32)iw_msg_level;
		bcopy(&int_val, arg, val_size);
		printf("iw_msg_level=0x%x\n", iw_msg_level);
#endif
#ifdef WL_CFG80211
		int_val = (int32)wl_dbg_level;
		bcopy(&int_val, arg, val_size);
		printf("cfg_msg_level=0x%x\n", wl_dbg_level);
#endif
		break;

	case IOV_SVAL(IOV_WLMSGLEVEL):
		if (int_val & DHD_ANDROID_VAL) {
			android_msg_level = (uint)(int_val & 0xFFFF);
			printf("android_msg_level=0x%x\n", android_msg_level);
		}
		if (int_val & DHD_CONFIG_VAL) {
			config_msg_level = (uint)(int_val & 0xFFFF);
			printf("config_msg_level=0x%x\n", config_msg_level);
		}
#if defined(WL_WIRELESS_EXT)
		if (int_val & DHD_IW_VAL) {
			iw_msg_level = (uint)(int_val & 0xFFFF);
			printf("iw_msg_level=0x%x\n", iw_msg_level);
		}
#endif
#ifdef WL_CFG80211
		if (int_val & DHD_CFG_VAL) {
			wl_cfg80211_enable_trace((u32)(int_val & 0xFFFF));
		}
#endif
		break;

	case IOV_GVAL(IOV_MSGLEVEL):
		int_val = (int32)dhd_msg_level;
		bcopy(&int_val, arg, val_size);
#ifdef PKT_STATICS
		dhdsdio_txpktstatics();
#endif
		break;

	case IOV_SVAL(IOV_MSGLEVEL):
		dhd_msg_level = int_val;
		break;

	case IOV_GVAL(IOV_BCMERRORSTR):
		bcm_strncpy_s((char *)arg, len, bcmerrorstr(dhd_pub->bcmerror), BCME_STRLEN);
		((char *)arg)[BCME_STRLEN - 1] = 0x00;
		break;

	case IOV_GVAL(IOV_BCMERROR):
		int_val = (int32)dhd_pub->bcmerror;
		bcopy(&int_val, arg, val_size);
		break;

#ifndef BCMDBUS
	case IOV_GVAL(IOV_WDTICK):
		int_val = (int32)dhd_watchdog_ms;
		bcopy(&int_val, arg, val_size);
		break;
#endif /* !BCMDBUS */

	case IOV_SVAL(IOV_WDTICK):
		if (!dhd_pub->up) {
			bcmerror = BCME_NOTUP;
			break;
		}

		if (CUSTOM_DHD_WATCHDOG_MS == 0 && int_val == 0) {
			dhd_watchdog_ms = (uint)int_val;
		}

		dhd_os_wd_timer(dhd_pub, (uint)int_val);
		break;

	case IOV_GVAL(IOV_DUMP):
		bcmerror = dhd_dump(dhd_pub, arg, len);
		break;

#ifndef BCMDBUS
	case IOV_GVAL(IOV_DCONSOLE_POLL):
		int_val = (int32)dhd_console_ms;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DCONSOLE_POLL):
		dhd_console_ms = (uint)int_val;
		break;

	case IOV_SVAL(IOV_CONS):
		if (len > 0)
			bcmerror = dhd_bus_console_in(dhd_pub, arg, len - 1);
		break;
#endif /* !BCMDBUS */

	case IOV_SVAL(IOV_CLEARCOUNTS):
		dhd_pub->tx_packets = dhd_pub->rx_packets = 0;
		dhd_pub->tx_errors = dhd_pub->rx_errors = 0;
		dhd_pub->tx_ctlpkts = dhd_pub->rx_ctlpkts = 0;
		dhd_pub->tx_ctlerrs = dhd_pub->rx_ctlerrs = 0;
		dhd_pub->tx_dropped = 0;
		dhd_pub->rx_dropped = 0;
		dhd_pub->tx_pktgetfail = 0;
		dhd_pub->rx_pktgetfail = 0;
		dhd_pub->rx_readahead_cnt = 0;
		dhd_pub->tx_realloc = 0;
		dhd_pub->wd_dpc_sched = 0;
		memset(&dhd_pub->dstats, 0, sizeof(dhd_pub->dstats));
		dhd_bus_clearcounts(dhd_pub);
#ifdef PROP_TXSTATUS
		/* clear proptxstatus related counters */
		dhd_wlfc_clear_counts(dhd_pub);
#endif /* PROP_TXSTATUS */
#if defined(DHD_LB_STATS)
		DHD_LB_STATS_RESET(dhd_pub);
#endif /* DHD_LB_STATS */
		break;


	case IOV_GVAL(IOV_IOCTLTIMEOUT): {
		int_val = (int32)dhd_os_get_ioctl_resp_timeout();
		bcopy(&int_val, arg, sizeof(int_val));
		break;
	}

	case IOV_SVAL(IOV_IOCTLTIMEOUT): {
		if (int_val <= 0)
			bcmerror = BCME_BADARG;
		else
			dhd_os_set_ioctl_resp_timeout((unsigned int)int_val);
		break;
	}

#ifdef PROP_TXSTATUS
	case IOV_GVAL(IOV_PROPTXSTATUS_ENABLE): {
		bool wlfc_enab = FALSE;
		bcmerror = dhd_wlfc_get_enable(dhd_pub, &wlfc_enab);
		if (bcmerror != BCME_OK)
			goto exit;
		int_val = wlfc_enab ? 1 : 0;
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_PROPTXSTATUS_ENABLE): {
		bool wlfc_enab = FALSE;
		bcmerror = dhd_wlfc_get_enable(dhd_pub, &wlfc_enab);
		if (bcmerror != BCME_OK)
			goto exit;

		/* wlfc is already set as desired */
		if (wlfc_enab == (int_val == 0 ? FALSE : TRUE))
			goto exit;

		if (int_val == TRUE)
			bcmerror = dhd_wlfc_init(dhd_pub);
		else
			bcmerror = dhd_wlfc_deinit(dhd_pub);

		break;
	}
	case IOV_GVAL(IOV_PROPTXSTATUS_MODE):
		bcmerror = dhd_wlfc_get_mode(dhd_pub, &int_val);
		if (bcmerror != BCME_OK)
			goto exit;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PROPTXSTATUS_MODE):
		dhd_wlfc_set_mode(dhd_pub, int_val);
		break;

	case IOV_GVAL(IOV_PROPTXSTATUS_MODULE_IGNORE):
		bcmerror = dhd_wlfc_get_module_ignore(dhd_pub, &int_val);
		if (bcmerror != BCME_OK)
			goto exit;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PROPTXSTATUS_MODULE_IGNORE):
		dhd_wlfc_set_module_ignore(dhd_pub, int_val);
		break;

	case IOV_GVAL(IOV_PROPTXSTATUS_CREDIT_IGNORE):
		bcmerror = dhd_wlfc_get_credit_ignore(dhd_pub, &int_val);
		if (bcmerror != BCME_OK)
			goto exit;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PROPTXSTATUS_CREDIT_IGNORE):
		dhd_wlfc_set_credit_ignore(dhd_pub, int_val);
		break;

	case IOV_GVAL(IOV_PROPTXSTATUS_TXSTATUS_IGNORE):
		bcmerror = dhd_wlfc_get_txstatus_ignore(dhd_pub, &int_val);
		if (bcmerror != BCME_OK)
			goto exit;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PROPTXSTATUS_TXSTATUS_IGNORE):
		dhd_wlfc_set_txstatus_ignore(dhd_pub, int_val);
		break;

	case IOV_GVAL(IOV_PROPTXSTATUS_RXPKT_CHK):
		bcmerror = dhd_wlfc_get_rxpkt_chk(dhd_pub, &int_val);
		if (bcmerror != BCME_OK)
			goto exit;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_PROPTXSTATUS_RXPKT_CHK):
		dhd_wlfc_set_rxpkt_chk(dhd_pub, int_val);
		break;

#endif /* PROP_TXSTATUS */

	case IOV_GVAL(IOV_BUS_TYPE):
		/* The dhd application queries the driver to check if its usb or sdio.  */
#ifdef BCMDBUS
		int_val = BUS_TYPE_USB;
#endif /* BCMDBUS */
#ifdef BCMSDIO
		int_val = BUS_TYPE_SDIO;
#endif
#ifdef PCIE_FULL_DONGLE
		int_val = BUS_TYPE_PCIE;
#endif
		bcopy(&int_val, arg, val_size);
		break;


#ifdef WLMEDIA_HTSF
	case IOV_GVAL(IOV_WLPKTDLYSTAT_SZ):
		int_val = dhd_pub->htsfdlystat_sz;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_WLPKTDLYSTAT_SZ):
		dhd_pub->htsfdlystat_sz = int_val & 0xff;
		printf("Setting tsfdlystat_sz:%d\n", dhd_pub->htsfdlystat_sz);
		break;
#endif
	case IOV_SVAL(IOV_CHANGEMTU):
		int_val &= 0xffff;
		bcmerror = dhd_change_mtu(dhd_pub, int_val, 0);
		break;

	case IOV_GVAL(IOV_HOSTREORDER_FLOWS):
	{
		uint i = 0;
		uint8 *ptr = (uint8 *)arg;
		uint8 count = 0;

		ptr++;
		for (i = 0; i < WLHOST_REORDERDATA_MAXFLOWS; i++) {
			if (dhd_pub->reorder_bufs[i] != NULL) {
				*ptr = dhd_pub->reorder_bufs[i]->flow_id;
				ptr++;
				count++;
			}
		}
		ptr = (uint8 *)arg;
		*ptr = count;
		break;
	}
#ifdef DHDTCPACK_SUPPRESS
	case IOV_GVAL(IOV_TCPACK_SUPPRESS): {
		int_val = (uint32)dhd_pub->tcpack_sup_mode;
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_TCPACK_SUPPRESS): {
		bcmerror = dhd_tcpack_suppress_set(dhd_pub, (uint8)int_val);
		break;
	}
#endif /* DHDTCPACK_SUPPRESS */
#ifdef DHD_WMF
	case IOV_GVAL(IOV_WMF_BSS_ENAB): {
		uint32	bssidx;
		dhd_wmf_t *wmf;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: wmf_bss_enable: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		wmf = dhd_wmf_conf(dhd_pub, bssidx);
		int_val = wmf->wmf_enable ? 1 :0;
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_WMF_BSS_ENAB): {
		/* Enable/Disable WMF */
		uint32	bssidx;
		dhd_wmf_t *wmf;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: wmf_bss_enable: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		ASSERT(val);
		bcopy(val, &int_val, sizeof(uint32));
		wmf = dhd_wmf_conf(dhd_pub, bssidx);
		if (wmf->wmf_enable == int_val)
			break;
		if (int_val) {
			/* Enable WMF */
			if (dhd_wmf_instance_add(dhd_pub, bssidx) != BCME_OK) {
				DHD_ERROR(("%s: Error in creating WMF instance\n",
				__FUNCTION__));
				break;
			}
			if (dhd_wmf_start(dhd_pub, bssidx) != BCME_OK) {
				DHD_ERROR(("%s: Failed to start WMF\n", __FUNCTION__));
				break;
			}
			wmf->wmf_enable = TRUE;
		} else {
			/* Disable WMF */
			wmf->wmf_enable = FALSE;
			dhd_wmf_stop(dhd_pub, bssidx);
			dhd_wmf_instance_del(dhd_pub, bssidx);
		}
		break;
	}
	case IOV_GVAL(IOV_WMF_UCAST_IGMP):
		int_val = dhd_pub->wmf_ucast_igmp ? 1 : 0;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_WMF_UCAST_IGMP):
		if (dhd_pub->wmf_ucast_igmp == int_val)
			break;

		if (int_val >= OFF && int_val <= ON)
			dhd_pub->wmf_ucast_igmp = int_val;
		else
			bcmerror = BCME_RANGE;
		break;
	case IOV_GVAL(IOV_WMF_MCAST_DATA_SENDUP):
		int_val = dhd_wmf_mcast_data_sendup(dhd_pub, 0, FALSE, FALSE);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_WMF_MCAST_DATA_SENDUP):
		dhd_wmf_mcast_data_sendup(dhd_pub, 0, TRUE, int_val);
		break;

#ifdef WL_IGMP_UCQUERY
	case IOV_GVAL(IOV_WMF_UCAST_IGMP_QUERY):
		int_val = dhd_pub->wmf_ucast_igmp_query ? 1 : 0;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_WMF_UCAST_IGMP_QUERY):
		if (dhd_pub->wmf_ucast_igmp_query == int_val)
			break;

		if (int_val >= OFF && int_val <= ON)
			dhd_pub->wmf_ucast_igmp_query = int_val;
		else
			bcmerror = BCME_RANGE;
		break;
#endif /* WL_IGMP_UCQUERY */
#ifdef DHD_UCAST_UPNP
	case IOV_GVAL(IOV_WMF_UCAST_UPNP):
		int_val = dhd_pub->wmf_ucast_upnp ? 1 : 0;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_WMF_UCAST_UPNP):
		if (dhd_pub->wmf_ucast_upnp == int_val)
			break;

		if (int_val >= OFF && int_val <= ON)
			dhd_pub->wmf_ucast_upnp = int_val;
		else
			bcmerror = BCME_RANGE;
		break;
#endif /* DHD_UCAST_UPNP */

	case IOV_GVAL(IOV_WMF_PSTA_DISABLE): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, (char *)name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: ap isoalate: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		int_val = dhd_get_wmf_psta_disable(dhd_pub, bssidx);
		bcopy(&int_val, arg, val_size);
		break;
	}

	case IOV_SVAL(IOV_WMF_PSTA_DISABLE): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, (char *)name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: ap isolate: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		ASSERT(val);
		bcopy(val, &int_val, sizeof(uint32));
		dhd_set_wmf_psta_disable(dhd_pub, bssidx, int_val);
		break;
	}
#endif /* DHD_WMF */

#if defined(TRAFFIC_MGMT_DWM)
	case IOV_SVAL(IOV_TRAFFIC_MGMT_DWM): {
			trf_mgmt_filter_list_t   *trf_mgmt_filter_list =
				(trf_mgmt_filter_list_t *)(arg);
			bcmerror = traffic_mgmt_add_dwm_filter(dhd_pub, trf_mgmt_filter_list, len);
		}
		break;
#endif 

#ifdef DHD_L2_FILTER
	case IOV_GVAL(IOV_DHCP_UNICAST): {
		uint32 bssidx;
		const char *val;
		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: IOV_DHCP_UNICAST: bad parameterand name = %s\n",
				__FUNCTION__, name));
			bcmerror = BCME_BADARG;
			break;
		}
		int_val = dhd_get_dhcp_unicast_status(dhd_pub, bssidx);
		memcpy(arg, &int_val, val_size);
		break;
	}
	case IOV_SVAL(IOV_DHCP_UNICAST): {
		uint32	bssidx;
		const char *val;
		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: IOV_DHCP_UNICAST: bad parameterand name = %s\n",
				__FUNCTION__, name));
			bcmerror = BCME_BADARG;
			break;
		}
		memcpy(&int_val, val, sizeof(int_val));
		bcmerror = dhd_set_dhcp_unicast_status(dhd_pub, bssidx, int_val ? 1 : 0);
		break;
	}
	case IOV_GVAL(IOV_BLOCK_PING): {
		uint32 bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: IOV_BLOCK_PING: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}
		int_val = dhd_get_block_ping_status(dhd_pub, bssidx);
		memcpy(arg, &int_val, val_size);
		break;
	}
	case IOV_SVAL(IOV_BLOCK_PING): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: IOV_BLOCK_PING: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}
		memcpy(&int_val, val, sizeof(int_val));
		bcmerror = dhd_set_block_ping_status(dhd_pub, bssidx, int_val ? 1 : 0);
		break;
	}
	case IOV_GVAL(IOV_PROXY_ARP): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: IOV_PROXY_ARP: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}
		int_val = dhd_get_parp_status(dhd_pub, bssidx);
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_PROXY_ARP): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: IOV_PROXY_ARP: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}
		bcopy(val, &int_val, sizeof(int_val));

		/* Issue a iovar request to WL to update the proxy arp capability bit
		 * in the Extended Capability IE of beacons/probe responses.
		 */
		bcmerror = dhd_iovar(dhd_pub, bssidx, "proxy_arp_advertise", val, sizeof(int_val),
				NULL, 0, TRUE);
		if (bcmerror == BCME_OK) {
			dhd_set_parp_status(dhd_pub, bssidx, int_val ? 1 : 0);
		}
		break;
	}
	case IOV_GVAL(IOV_GRAT_ARP): {
		uint32 bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: IOV_GRAT_ARP: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}
		int_val = dhd_get_grat_arp_status(dhd_pub, bssidx);
		memcpy(arg, &int_val, val_size);
		break;
	}
	case IOV_SVAL(IOV_GRAT_ARP): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: IOV_GRAT_ARP: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}
		memcpy(&int_val, val, sizeof(int_val));
		bcmerror = dhd_set_grat_arp_status(dhd_pub, bssidx, int_val ? 1 : 0);
		break;
	}
#endif /* DHD_L2_FILTER */
	case IOV_SVAL(IOV_DHD_IE): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: dhd ie: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		break;
	}
	case IOV_GVAL(IOV_AP_ISOLATE): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: ap isoalate: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		int_val = dhd_get_ap_isolate(dhd_pub, bssidx);
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_AP_ISOLATE): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: ap isolate: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		ASSERT(val);
		bcopy(val, &int_val, sizeof(uint32));
		dhd_set_ap_isolate(dhd_pub, bssidx, int_val);
		break;
	}
#ifdef DHD_PSTA
	case IOV_GVAL(IOV_PSTA): {
		int_val = dhd_get_psta_mode(dhd_pub);
		bcopy(&int_val, arg, val_size);
		break;
		}
	case IOV_SVAL(IOV_PSTA): {
		if (int_val >= DHD_MODE_PSTA_DISABLED && int_val <= DHD_MODE_PSR) {
			dhd_set_psta_mode(dhd_pub, int_val);
		} else {
			bcmerror = BCME_RANGE;
		}
		break;
		}
#endif /* DHD_PSTA */
#ifdef DHD_WET
	case IOV_GVAL(IOV_WET):
		 int_val = dhd_get_wet_mode(dhd_pub);
		 bcopy(&int_val, arg, val_size);
		 break;

	case IOV_SVAL(IOV_WET):
		 if (int_val == 0 || int_val == 1) {
			 dhd_set_wet_mode(dhd_pub, int_val);
			 /* Delete the WET DB when disabled */
			 if (!int_val) {
				 dhd_wet_sta_delete_list(dhd_pub);
			 }
		 } else {
			 bcmerror = BCME_RANGE;
		 }
				 break;
	case IOV_SVAL(IOV_WET_HOST_IPV4):
			dhd_set_wet_host_ipv4(dhd_pub, params, plen);
			break;
	case IOV_SVAL(IOV_WET_HOST_MAC):
			dhd_set_wet_host_mac(dhd_pub, params, plen);
		break;
#endif /* DHD_WET */
#ifdef DHD_MCAST_REGEN
	case IOV_GVAL(IOV_MCAST_REGEN_BSS_ENABLE): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, (char *)name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: mcast_regen_bss_enable: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		int_val = dhd_get_mcast_regen_bss_enable(dhd_pub, bssidx);
		bcopy(&int_val, arg, val_size);
		break;
	}

	case IOV_SVAL(IOV_MCAST_REGEN_BSS_ENABLE): {
		uint32	bssidx;
		const char *val;

		if (dhd_iovar_parse_bssidx(dhd_pub, (char *)name, &bssidx, &val) != BCME_OK) {
			DHD_ERROR(("%s: mcast_regen_bss_enable: bad parameter\n", __FUNCTION__));
			bcmerror = BCME_BADARG;
			break;
		}

		ASSERT(val);
		bcopy(val, &int_val, sizeof(uint32));
		dhd_set_mcast_regen_bss_enable(dhd_pub, bssidx, int_val);
		break;
	}
#endif /* DHD_MCAST_REGEN */

	case IOV_GVAL(IOV_CFG80211_OPMODE): {
		int_val = (int32)dhd_pub->op_mode;
		bcopy(&int_val, arg, sizeof(int_val));
		break;
		}
	case IOV_SVAL(IOV_CFG80211_OPMODE): {
		if (int_val <= 0)
			bcmerror = BCME_BADARG;
		else
			dhd_pub->op_mode = int_val;
		break;
	}

	case IOV_GVAL(IOV_ASSERT_TYPE):
		int_val = g_assert_type;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_ASSERT_TYPE):
		g_assert_type = (uint32)int_val;
		break;


#if !defined(MACOSX_DHD)
	case IOV_GVAL(IOV_LMTEST): {
		*(uint32 *)arg = (uint32)lmtest;
		break;
	}

	case IOV_SVAL(IOV_LMTEST): {
		uint32 val = *(uint32 *)arg;
		if (val > 50)
			bcmerror = BCME_BADARG;
		else {
			lmtest = (uint)val;
			DHD_ERROR(("%s: lmtest %s\n",
				__FUNCTION__, (lmtest == FALSE)? "OFF" : "ON"));
		}
		break;
	}
#endif 

#ifdef SHOW_LOGTRACE
	case IOV_GVAL(IOV_DUMP_TRACE_LOG): {
		trace_buf_info_t *trace_buf_info;

		trace_buf_info = (trace_buf_info_t *)MALLOC(dhd_pub->osh,
				sizeof(trace_buf_info_t));
		if (trace_buf_info != NULL) {
			dhd_get_read_buf_ptr(dhd_pub, trace_buf_info);
			memcpy((void*)arg, (void*)trace_buf_info, sizeof(trace_buf_info_t));
			MFREE(dhd_pub->osh, trace_buf_info, sizeof(trace_buf_info_t));
		} else {
			DHD_ERROR(("Memory allocation Failed\n"));
			bcmerror = BCME_NOMEM;
		}
		break;
	}
#endif /* SHOW_LOGTRACE */
#ifdef REPORT_FATAL_TIMEOUTS
	case IOV_GVAL(IOV_SCAN_TO): {
		dhd_get_scan_to_val(dhd_pub, (uint32 *)&int_val);
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_SCAN_TO): {
		dhd_set_scan_to_val(dhd_pub, (uint32)int_val);
		break;
	}
	case IOV_GVAL(IOV_JOIN_TO): {
		dhd_get_join_to_val(dhd_pub, (uint32 *)&int_val);
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_JOIN_TO): {
		dhd_set_join_to_val(dhd_pub, (uint32)int_val);
		break;
	}
	case IOV_GVAL(IOV_CMD_TO): {
		dhd_get_cmd_to_val(dhd_pub, (uint32 *)&int_val);
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_CMD_TO): {
		dhd_set_cmd_to_val(dhd_pub, (uint32)int_val);
		break;
	}
	case IOV_GVAL(IOV_OQS_TO): {
		dhd_get_bus_to_val(dhd_pub, (uint32 *)&int_val);
		bcopy(&int_val, arg, val_size);
		break;
	}
	case IOV_SVAL(IOV_OQS_TO): {
		dhd_set_bus_to_val(dhd_pub, (uint32)int_val);
		break;
	}
#endif /* REPORT_FATAL_TIMEOUTS */
#ifdef DHD_DEBUG
#if defined(BCMSDIO) || defined(BCMPCIE)
	case IOV_GVAL(IOV_DONGLE_TRAP_TYPE):
		if (dhd_pub->dongle_trap_occured)
			int_val = ltoh32(dhd_pub->last_trap_info.type);
		else
			int_val = 0;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_DONGLE_TRAP_INFO):
	{
		struct bcmstrbuf strbuf;
		bcm_binit(&strbuf, arg, len);
		if (dhd_pub->dongle_trap_occured == FALSE) {
			bcm_bprintf(&strbuf, "no trap recorded\n");
			break;
		}
		dhd_bus_dump_trap_info(dhd_pub->bus, &strbuf);
		break;
	}

	case IOV_GVAL(IOV_BPADDR):
		{
			sdreg_t sdreg;
			uint32 addr, size;

			memcpy(&sdreg, params, sizeof(sdreg));

			addr = sdreg.offset;
			size = sdreg.func;

			bcmerror = dhd_bus_readwrite_bp_addr(dhd_pub, addr, size,
				(uint *)&int_val, TRUE);

			memcpy(arg, &int_val, sizeof(int32));

			break;
		}

	case IOV_SVAL(IOV_BPADDR):
		{
			sdreg_t sdreg;
			uint32 addr, size;

			memcpy(&sdreg, params, sizeof(sdreg));

			addr = sdreg.offset;
			size = sdreg.func;

			bcmerror = dhd_bus_readwrite_bp_addr(dhd_pub, addr, size,
				(uint *)&sdreg.value,
				FALSE);

			break;
		}
#endif /* BCMSDIO || BCMPCIE */
	case IOV_SVAL(IOV_MEM_DEBUG):
		if (len > 0) {
			bcmerror = dhd_mem_debug(dhd_pub, arg, len - 1);
		}
		break;
#endif /* DHD_DEBUG */
#if defined(DHD_EFI) && defined(DHD_LOG_DUMP)
	case IOV_GVAL(IOV_LOG_CAPTURE_ENABLE):
		{
			int_val = dhd_pub->log_capture_enable;
			bcopy(&int_val, arg, val_size);
			break;
		}

	case IOV_SVAL(IOV_LOG_CAPTURE_ENABLE):
		{
			dhd_pub->log_capture_enable = (uint8)int_val;
			break;
		}

	case IOV_GVAL(IOV_LOG_DUMP):
		{
			dhd_prot_debug_info_print(dhd_pub);
			dhd_bus_mem_dump(dhd_pub);
			break;
		}
#endif /* DHD_EFI && DHD_LOG_DUMP */
	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	DHD_TRACE(("%s: actionid %d, bcmerror %d\n", __FUNCTION__, actionid, bcmerror));
	return bcmerror;
}

/* Store the status of a connection attempt for later retrieval by an iovar */
void
dhd_store_conn_status(uint32 event, uint32 status, uint32 reason)
{
	/* Do not overwrite a WLC_E_PRUNE with a WLC_E_SET_SSID
	 * because an encryption/rsn mismatch results in both events, and
	 * the important information is in the WLC_E_PRUNE.
	 */
	if (!(event == WLC_E_SET_SSID && status == WLC_E_STATUS_FAIL &&
	      dhd_conn_event == WLC_E_PRUNE)) {
		dhd_conn_event = event;
		dhd_conn_status = status;
		dhd_conn_reason = reason;
	}
}

bool
dhd_prec_enq(dhd_pub_t *dhdp, struct pktq *q, void *pkt, int prec)
{
	void *p;
	int eprec = -1;		/* precedence to evict from */
	bool discard_oldest;

	/* Fast case, precedence queue is not full and we are also not
	 * exceeding total queue length
	 */
	if (!pktq_pfull(q, prec) && !pktq_full(q)) {
		pktq_penq(q, prec, pkt);
		return TRUE;
	}

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = pktq_peek_tail(q, &eprec);
		ASSERT(p);
		if (eprec > prec || eprec < 0)
			return FALSE;
	}

	/* Evict if needed */
	if (eprec >= 0) {
		/* Detect queueing to unconfigured precedence */
		ASSERT(!pktq_pempty(q, eprec));
		discard_oldest = AC_BITMAP_TST(dhdp->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			return FALSE;		/* refuse newer (incoming) packet */
		/* Evict packet according to discard policy */
		p = discard_oldest ? pktq_pdeq(q, eprec) : pktq_pdeq_tail(q, eprec);
		ASSERT(p);
#ifdef DHDTCPACK_SUPPRESS
		if (dhd_tcpack_check_xmit(dhdp, p) == BCME_ERROR) {
			DHD_ERROR(("%s %d: tcpack_suppress ERROR!!! Stop using it\n",
				__FUNCTION__, __LINE__));
			dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_OFF);
		}
#endif /* DHDTCPACK_SUPPRESS */
		PKTFREE(dhdp->osh, p, TRUE);
	}

	/* Enqueue */
	p = pktq_penq(q, prec, pkt);
	ASSERT(p);

	return TRUE;
}

/*
 * Functions to drop proper pkts from queue:
 *	If one pkt in queue is non-fragmented, drop first non-fragmented pkt only
 *	If all pkts in queue are all fragmented, find and drop one whole set fragmented pkts
 *	If can't find pkts matching upper 2 cases, drop first pkt anyway
 */
bool
dhd_prec_drop_pkts(dhd_pub_t *dhdp, struct pktq *pq, int prec, f_droppkt_t fn)
{
	struct pktq_prec *q = NULL;
	void *p, *prev = NULL, *next = NULL, *first = NULL, *last = NULL, *prev_first = NULL;
	pkt_frag_t frag_info;

	ASSERT(dhdp && pq);
	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];
	p = q->head;

	if (p == NULL)
		return FALSE;

	while (p) {
		frag_info = pkt_frag_info(dhdp->osh, p);
		if (frag_info == DHD_PKT_FRAG_NONE) {
			break;
		} else if (frag_info == DHD_PKT_FRAG_FIRST) {
			if (first) {
				/* No last frag pkt, use prev as last */
				last = prev;
				break;
			} else {
				first = p;
				prev_first = prev;
			}
		} else if (frag_info == DHD_PKT_FRAG_LAST) {
			if (first) {
				last = p;
				break;
			}
		}

		prev = p;
		p = PKTLINK(p);
	}

	if ((p == NULL) || ((frag_info != DHD_PKT_FRAG_NONE) && !(first && last))) {
		/* Not found matching pkts, use oldest */
		prev = NULL;
		p = q->head;
		frag_info = 0;
	}

	if (frag_info == DHD_PKT_FRAG_NONE) {
		first = last = p;
		prev_first = prev;
	}

	p = first;
	while (p) {
		next = PKTLINK(p);
		q->len--;
		pq->len--;

		PKTSETLINK(p, NULL);

		if (fn)
			fn(dhdp, prec, p, TRUE);

		if (p == last)
			break;

		p = next;
	}

	if (prev_first == NULL) {
		if ((q->head = next) == NULL)
			q->tail = NULL;
	} else {
		PKTSETLINK(prev_first, next);
		if (!next)
			q->tail = prev_first;
	}

	return TRUE;
}

static int
dhd_iovar_op(dhd_pub_t *dhd_pub, const char *name,
	void *params, int plen, void *arg, int len, bool set)
{
	int bcmerror = 0;
	int val_size;
	const bcm_iovar_t *vi = NULL;
	uint32 actionid;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));

	if ((vi = bcm_iovar_lookup(dhd_iovars, name)) == NULL) {
		bcmerror = BCME_UNSUPPORTED;
		goto exit;
	}

	DHD_CTL(("%s: %s %s, len %d plen %d\n", __FUNCTION__,
		name, (set ? "set" : "get"), len, plen));

	/* set up 'params' pointer in case this is a set command so that
	 * the convenience int and bool code can be common to set and get
	 */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		/* all other types are integer sized */
		val_size = sizeof(int);

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);

	bcmerror = dhd_doiovar(dhd_pub, vi, actionid, name, params, plen, arg, len, val_size);

exit:
	return bcmerror;
}

int
dhd_ioctl(dhd_pub_t * dhd_pub, dhd_ioctl_t *ioc, void *buf, uint buflen)
{
	int bcmerror = 0;
	unsigned long flags;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!buf) {
		return BCME_BADARG;
	}

	dhd_os_dhdiovar_lock(dhd_pub);
	switch (ioc->cmd) {
		case DHD_GET_MAGIC:
			if (buflen < sizeof(int))
				bcmerror = BCME_BUFTOOSHORT;
			else
				*(int*)buf = DHD_IOCTL_MAGIC;
			break;

		case DHD_GET_VERSION:
			if (buflen < sizeof(int))
				bcmerror = BCME_BUFTOOSHORT;
			else
				*(int*)buf = DHD_IOCTL_VERSION;
			break;

		case DHD_GET_VAR:
		case DHD_SET_VAR:
			{
				char *arg;
				uint arglen;

				DHD_LINUX_GENERAL_LOCK(dhd_pub, flags);
				if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(dhd_pub)) {
					/* In platforms like FC19, the FW download is done via IOCTL
					 * and should not return error for IOCTLs fired before FW
					 * Download is done
					 */
					if (dhd_fw_download_status(dhd_pub)) {
						DHD_ERROR(("%s: returning as busstate=%d\n",
								__FUNCTION__, dhd_pub->busstate));
						DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);
						dhd_os_dhdiovar_unlock(dhd_pub);
						return -ENODEV;
					}
				}
				DHD_BUS_BUSY_SET_IN_DHD_IOVAR(dhd_pub);
				DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);

#ifdef DHD_PCIE_RUNTIMEPM
				dhdpcie_runtime_bus_wake(dhd_pub, TRUE, dhd_ioctl);
#endif /* DHD_PCIE_RUNTIMEPM */

				DHD_LINUX_GENERAL_LOCK(dhd_pub, flags);
				if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(dhd_pub)) {
					/* If Suspend/Resume is tested via pcie_suspend IOVAR
					 * then continue to execute the IOVAR, return from here for
					 * other IOVARs, also include pciecfgreg and devreset to go
					 * through.
					 */
#ifdef DHD_EFI
					if (bcmstricmp((char *)buf, "pcie_suspend") &&
						bcmstricmp((char *)buf, "pciecfgreg") &&
						bcmstricmp((char *)buf, "devreset") &&
						bcmstricmp((char *)buf, "sdio_suspend") &&
						bcmstricmp((char *)buf, "control_signal"))
#else
					if (bcmstricmp((char *)buf, "pcie_suspend") &&
					    bcmstricmp((char *)buf, "pciecfgreg") &&
					    bcmstricmp((char *)buf, "devreset") &&
					    bcmstricmp((char *)buf, "sdio_suspend"))
#endif /* DHD_EFI */
					{
						DHD_ERROR(("%s: bus is in suspend(%d)"
							"or suspending(0x%x) state\n",
							__FUNCTION__, dhd_pub->busstate,
							dhd_pub->dhd_bus_busy_state));
						DHD_BUS_BUSY_CLEAR_IN_DHD_IOVAR(dhd_pub);
						dhd_os_busbusy_wake(dhd_pub);
						DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);
						dhd_os_dhdiovar_unlock(dhd_pub);
						return -ENODEV;
					}
				}
				/* During devreset ioctl, we call dhdpcie_advertise_bus_cleanup,
				 * which will wait for all the busy contexts to get over for
				 * particular time and call ASSERT if timeout happens. As during
				 * devreset ioctal, we made DHD_BUS_BUSY_SET_IN_DHD_IOVAR,
				 * to avoid ASSERT, clear the IOCTL busy state. "devreset" ioctl is
				 * not used in Production platforms but only used in FC19 setups.
				 */
				if (!bcmstricmp((char *)buf, "devreset")) {
					DHD_BUS_BUSY_CLEAR_IN_DHD_IOVAR(dhd_pub);
				}
				DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);

				/* scan past the name to any arguments */
				for (arg = buf, arglen = buflen; *arg && arglen; arg++, arglen--)
					;

				if (*arg) {
					bcmerror = BCME_BUFTOOSHORT;
					goto unlock_exit;
				}

				/* account for the NUL terminator */
				arg++, arglen--;
				/* call with the appropriate arguments */
				if (ioc->cmd == DHD_GET_VAR) {
					bcmerror = dhd_iovar_op(dhd_pub, buf, arg, arglen,
							buf, buflen, IOV_GET);
				} else {
					bcmerror = dhd_iovar_op(dhd_pub, buf, NULL, 0,
							arg, arglen, IOV_SET);
				}
				if (bcmerror != BCME_UNSUPPORTED) {
					goto unlock_exit;
				}

				/* not in generic table, try protocol module */
				if (ioc->cmd == DHD_GET_VAR) {
					bcmerror = dhd_prot_iovar_op(dhd_pub, buf, arg,
							arglen, buf, buflen, IOV_GET);
				} else {
					bcmerror = dhd_prot_iovar_op(dhd_pub, buf,
							NULL, 0, arg, arglen, IOV_SET);
				}
				if (bcmerror != BCME_UNSUPPORTED) {
					goto unlock_exit;
				}

				/* if still not found, try bus module */
				if (ioc->cmd == DHD_GET_VAR) {
					bcmerror = dhd_bus_iovar_op(dhd_pub, buf,
							arg, arglen, buf, buflen, IOV_GET);
				} else {
					bcmerror = dhd_bus_iovar_op(dhd_pub, buf,
							NULL, 0, arg, arglen, IOV_SET);
				}
				if (bcmerror != BCME_UNSUPPORTED) {
					goto unlock_exit;
				}

#ifdef DHD_TIMESYNC
				/* check TS module */
				if (ioc->cmd == DHD_GET_VAR)
					bcmerror = dhd_timesync_iovar_op(dhd_pub->ts, buf, arg,
						arglen, buf, buflen, IOV_GET);
				else
					bcmerror = dhd_timesync_iovar_op(dhd_pub->ts, buf,
						NULL, 0, arg, arglen, IOV_SET);
#endif /* DHD_TIMESYNC */
			}
			goto unlock_exit;

		default:
			bcmerror = BCME_UNSUPPORTED;
	}
	dhd_os_dhdiovar_unlock(dhd_pub);
	return bcmerror;

unlock_exit:
	DHD_LINUX_GENERAL_LOCK(dhd_pub, flags);
	DHD_BUS_BUSY_CLEAR_IN_DHD_IOVAR(dhd_pub);
	dhd_os_busbusy_wake(dhd_pub);
	DHD_LINUX_GENERAL_UNLOCK(dhd_pub, flags);
	dhd_os_dhdiovar_unlock(dhd_pub);
	return bcmerror;
}

#ifdef SHOW_EVENTS
static void
wl_show_host_event(dhd_pub_t *dhd_pub, wl_event_msg_t *event, void *event_data,
	void *raw_event_ptr, char *eventmask)
{
	uint i, status, reason;
	bool group = FALSE, flush_txq = FALSE, link = FALSE;
	bool host_data = FALSE; /* prints  event data after the case  when set */
	const char *auth_str;
	const char *event_name;
	uchar *buf;
	char err_msg[256], eabuf[ETHER_ADDR_STR_LEN];
	uint event_type, flags, auth_type, datalen;

	event_type = ntoh32(event->event_type);
	flags = ntoh16(event->flags);
	status = ntoh32(event->status);
	reason = ntoh32(event->reason);
	BCM_REFERENCE(reason);
	auth_type = ntoh32(event->auth_type);
	datalen = ntoh32(event->datalen);

	/* debug dump of event messages */
	snprintf(eabuf, sizeof(eabuf), "%02x:%02x:%02x:%02x:%02x:%02x",
	        (uchar)event->addr.octet[0]&0xff,
	        (uchar)event->addr.octet[1]&0xff,
	        (uchar)event->addr.octet[2]&0xff,
	        (uchar)event->addr.octet[3]&0xff,
	        (uchar)event->addr.octet[4]&0xff,
	        (uchar)event->addr.octet[5]&0xff);

	event_name = bcmevent_get_name(event_type);
	BCM_REFERENCE(event_name);

	if (flags & WLC_EVENT_MSG_LINK)
		link = TRUE;
	if (flags & WLC_EVENT_MSG_GROUP)
		group = TRUE;
	if (flags & WLC_EVENT_MSG_FLUSHTXQ)
		flush_txq = TRUE;

	switch (event_type) {
	case WLC_E_START:
	case WLC_E_DEAUTH:
	case WLC_E_DISASSOC:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case WLC_E_ASSOC_IND:
	case WLC_E_REASSOC_IND:

		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case WLC_E_ASSOC:
	case WLC_E_REASSOC:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, SUCCESS\n", event_name, eabuf));
		} else if (status == WLC_E_STATUS_TIMEOUT) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, TIMEOUT\n", event_name, eabuf));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, FAILURE, reason %d\n",
			       event_name, eabuf, (int)reason));
		} else {
			DHD_EVENT(("MACEVENT: %s, MAC %s, unexpected status %d\n",
			       event_name, eabuf, (int)status));
		}
		break;

	case WLC_E_DEAUTH_IND:
	case WLC_E_DISASSOC_IND:
		DHD_EVENT(("MACEVENT: %s, MAC %s, reason %d\n", event_name, eabuf, (int)reason));
		break;

	case WLC_E_AUTH:
	case WLC_E_AUTH_IND:
		if (auth_type == DOT11_OPEN_SYSTEM)
			auth_str = "Open System";
		else if (auth_type == DOT11_SHARED_KEY)
			auth_str = "Shared Key";
		else {
			snprintf(err_msg, sizeof(err_msg), "AUTH unknown: %d", (int)auth_type);
			auth_str = err_msg;
		}

	if (event_type == WLC_E_AUTH_IND) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s\n", event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, SUCCESS\n",
				event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_TIMEOUT) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, TIMEOUT\n",
				event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, FAILURE, reason %d\n",
			       event_name, eabuf, auth_str, (int)reason));
		}
		BCM_REFERENCE(auth_str);

		break;

	case WLC_E_JOIN:
	case WLC_E_ROAM:
	case WLC_E_SET_SSID:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
#ifdef REPORT_FATAL_TIMEOUTS
			dhd_clear_join_error(dhd_pub, WLC_SSID_MASK);
#endif /* REPORT_FATAL_TIMEOUTS */
		} else {
#ifdef REPORT_FATAL_TIMEOUTS
			dhd_set_join_error(dhd_pub, WLC_SSID_MASK);
#endif /* REPORT_FATAL_TIMEOUTS */
			if (status == WLC_E_STATUS_FAIL) {
				DHD_EVENT(("MACEVENT: %s, failed\n", event_name));
			} else if (status == WLC_E_STATUS_NO_NETWORKS) {
				DHD_EVENT(("MACEVENT: %s, no networks found\n", event_name));
			} else {
				DHD_EVENT(("MACEVENT: %s, unexpected status %d\n",
					event_name, (int)status));
			}
		}
		break;

	case WLC_E_BEACON_RX:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, SUCCESS\n", event_name));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, FAIL\n", event_name));
		} else {
			DHD_EVENT(("MACEVENT: %s, status %d\n", event_name, status));
		}
		break;

	case WLC_E_LINK:
		DHD_EVENT(("MACEVENT: %s %s\n", event_name, link?"UP":"DOWN"));
		BCM_REFERENCE(link);
		break;

	case WLC_E_MIC_ERROR:
		DHD_EVENT(("MACEVENT: %s, MAC %s, Group %d, Flush %d\n",
		       event_name, eabuf, group, flush_txq));
		BCM_REFERENCE(group);
		BCM_REFERENCE(flush_txq);
		break;

	case WLC_E_ICV_ERROR:
	case WLC_E_UNICAST_DECODE_ERROR:
	case WLC_E_MULTICAST_DECODE_ERROR:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n",
		       event_name, eabuf));
		break;

	case WLC_E_TXFAIL:
		DHD_EVENT(("MACEVENT: %s, RA %s status %d\n", event_name, eabuf, status));
		break;

	case WLC_E_ASSOC_REQ_IE:
	case WLC_E_ASSOC_RESP_IE:
	case WLC_E_PMKID_CACHE:
		DHD_EVENT(("MACEVENT: %s\n", event_name));
		break;

	case WLC_E_SCAN_COMPLETE:
		DHD_EVENT(("MACEVENT: %s\n", event_name));
#ifdef REPORT_FATAL_TIMEOUTS
		dhd_stop_scan_timer(dhd_pub);
#endif /* REPORT_FATAL_TIMEOUTS */
		break;
	case WLC_E_RSSI_LQM:
	case WLC_E_PFN_NET_FOUND:
	case WLC_E_PFN_NET_LOST:
	case WLC_E_PFN_SCAN_COMPLETE:
	case WLC_E_PFN_SCAN_NONE:
	case WLC_E_PFN_SCAN_ALLGONE:
	case WLC_E_PFN_GSCAN_FULL_RESULT:
	case WLC_E_PFN_SSID_EXT:
		DHD_EVENT(("PNOEVENT: %s\n", event_name));
		break;

	case WLC_E_PSK_SUP:
	case WLC_E_PRUNE:
		DHD_EVENT(("MACEVENT: %s, status %d, reason %d\n",
		           event_name, (int)status, (int)reason));
#ifdef REPORT_FATAL_TIMEOUTS
		if ((status == WLC_E_STATUS_SUCCESS || status == WLC_E_STATUS_UNSOLICITED) &&
				(reason == WLC_E_SUP_OTHER)) {
			dhd_clear_join_error(dhd_pub, WLC_WPA_MASK);
		} else {
			dhd_set_join_error(dhd_pub, WLC_WPA_MASK);
		}
#endif /* REPORT_FATAL_TIMEOUTS */
		break;

#ifdef WIFI_ACT_FRAME
	case WLC_E_ACTION_FRAME:
		DHD_TRACE(("MACEVENT: %s Bssid %s\n", event_name, eabuf));
		break;
#endif /* WIFI_ACT_FRAME */

#ifdef SHOW_LOGTRACE
	case WLC_E_TRACE:
		DHD_EVENT(("MACEVENT: %s Logtrace\n", event_name));
		dhd_dbg_trace_evnt_handler(dhd_pub, event_data, raw_event_ptr, datalen);
		break;
#endif /* SHOW_LOGTRACE */

	case WLC_E_RSSI:
		DHD_EVENT(("MACEVENT: %s %d\n", event_name, ntoh32(*((int *)event_data))));
		break;

	case WLC_E_SERVICE_FOUND:
	case WLC_E_P2PO_ADD_DEVICE:
	case WLC_E_P2PO_DEL_DEVICE:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

#ifdef BT_WIFI_HANDOBER
	case WLC_E_BT_WIFI_HANDOVER_REQ:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;
#endif

	case WLC_E_CCA_CHAN_QUAL:
		if (datalen) {
			buf = (uchar *) event_data;
			DHD_EVENT(("MACEVENT: %s %d, MAC %s, status %d, reason %d, auth %d, "
				"channel 0x%02x \n", event_name, event_type, eabuf, (int)status,
				(int)reason, (int)auth_type, *(buf + 4)));
		}
		break;
	case WLC_E_ESCAN_RESULT:
	{
		DHD_EVENT(("MACEVENT: %s %d, MAC %s, status %d \n",
		       event_name, event_type, eabuf, (int)status));
	}
		break;
	case WLC_E_PSK_AUTH:
		DHD_EVENT(("MACEVENT: %s, RA %s status %d Reason:%d\n",
		event_name, eabuf, status, reason));
		break;
	case WLC_E_IF:
	{
		struct wl_event_data_if *ifevent = (struct wl_event_data_if *)event_data;
		BCM_REFERENCE(ifevent);

		DHD_EVENT(("MACEVENT: %s, opcode:0x%d  ifidx:%d\n",
		event_name, ifevent->opcode, ifevent->ifidx));
		break;
	}

#ifdef SHOW_LOGTRACE
	case WLC_E_MSCH:
	{
		wl_mschdbg_event_handler(dhd_pub, raw_event_ptr, reason, event_data, datalen);
		break;
	}
#endif /* SHOW_LOGTRACE */

	default:
		DHD_EVENT(("MACEVENT: %s %d, MAC %s, status %d, reason %d, auth %d\n",
		       event_name, event_type, eabuf, (int)status, (int)reason,
		       (int)auth_type));
		break;
	}

	/* show any appended data if message level is set to bytes or host_data is set */
	if ((DHD_BYTES_ON() || (host_data == TRUE)) && DHD_EVENT_ON() && datalen) {
		buf = (uchar *) event_data;
		BCM_REFERENCE(buf);
		DHD_EVENT((" data (%d) : ", datalen));
		for (i = 0; i < datalen; i++) {
			DHD_EVENT((" 0x%02x ", buf[i]));
		}
		DHD_EVENT(("\n"));
	}
}
#endif /* SHOW_EVENTS */

#ifdef DNGL_EVENT_SUPPORT
/* Check whether packet is a BRCM dngl event pkt. If it is, process event data. */
int
dngl_host_event(dhd_pub_t *dhdp, void *pktdata, bcm_dngl_event_msg_t *dngl_event, size_t pktlen)
{
	bcm_dngl_event_t *pvt_data = (bcm_dngl_event_t *)pktdata;

	dngl_host_event_process(dhdp, pvt_data, dngl_event, pktlen);
	return BCME_OK;
}

void
dngl_host_event_process(dhd_pub_t *dhdp, bcm_dngl_event_t *event,
	bcm_dngl_event_msg_t *dngl_event, size_t pktlen)
{
	uint8 *p = (uint8 *)(event + 1);
	uint16 type = ntoh16_ua((void *)&dngl_event->event_type);
	uint16 datalen = ntoh16_ua((void *)&dngl_event->datalen);
	uint16 version = ntoh16_ua((void *)&dngl_event->version);

	DHD_EVENT(("VERSION:%d, EVENT TYPE:%d, DATALEN:%d\n", version, type, datalen));
	if (datalen > (pktlen - sizeof(bcm_dngl_event_t) + ETHER_TYPE_LEN)) {
		return;
	}
	if (version != BCM_DNGL_EVENT_MSG_VERSION) {
		DHD_ERROR(("%s:version mismatch:%d:%d\n", __FUNCTION__,
			version, BCM_DNGL_EVENT_MSG_VERSION));
		return;
	}
	switch (type) {
	   case DNGL_E_SOCRAM_IND:
		{
		   bcm_dngl_socramind_t *socramind_ptr = (bcm_dngl_socramind_t *)p;
		   uint16 tag = ltoh32(socramind_ptr->tag);
		   uint16 taglen = ltoh32(socramind_ptr->length);
		   p = (uint8 *)socramind_ptr->value;
		   DHD_EVENT(("Tag:%d Len:%d Datalen:%d\n", tag, taglen, datalen));
		   switch (tag) {
			case SOCRAM_IND_ASSERT_TAG:
			    {
				/*
				* The payload consists of -
				* null terminated function name padded till 32 bit boundary +
				* Line number - (32 bits)
				* Caller address (32 bits)
				*/
				char *fnname = (char *)p;
				if (datalen < (ROUNDUP(strlen(fnname) + 1, sizeof(uint32)) +
					sizeof(uint32) * 2)) {
					DHD_ERROR(("Wrong length:%d\n", datalen));
					return;
				}
				DHD_EVENT(("ASSRT Function:%s ", p));
				p += ROUNDUP(strlen(p) + 1, sizeof(uint32));
				DHD_EVENT(("Line:%d ", *(uint32 *)p));
				p += sizeof(uint32);
				DHD_EVENT(("Caller Addr:0x%x\n", *(uint32 *)p));
				break;
			    }
			case SOCRAM_IND_TAG_HEALTH_CHECK:
			   {
				bcm_dngl_healthcheck_t *dngl_hc = (bcm_dngl_healthcheck_t *)p;
				DHD_EVENT(("SOCRAM_IND_HEALTHCHECK_TAG:%d Len:%d\n",
				ltoh32(dngl_hc->top_module_tag), ltoh32(dngl_hc->top_module_len)));
				if (DHD_EVENT_ON()) {
					prhex("HEALTHCHECK", p, ltoh32(dngl_hc->top_module_len));
				}
				p = (uint8 *)dngl_hc->value;

				switch (ltoh32(dngl_hc->top_module_tag)) {
					case HEALTH_CHECK_TOP_LEVEL_MODULE_PCIEDEV_RTE:
					   {
						bcm_dngl_pcie_hc_t *pcie_hc;
						pcie_hc = (bcm_dngl_pcie_hc_t *)p;
						BCM_REFERENCE(pcie_hc);
						if (ltoh32(dngl_hc->top_module_len) <
								sizeof(bcm_dngl_pcie_hc_t)) {
							DHD_ERROR(("Wrong length:%d\n",
								ltoh32(dngl_hc->top_module_len)));
							return;
						}
						DHD_EVENT(("%d:PCIE HC error:%d flag:0x%x,"
							" control:0x%x\n",
							ltoh32(pcie_hc->version),
							ltoh32(pcie_hc->pcie_err_ind_type),
							ltoh32(pcie_hc->pcie_flag),
							ltoh32(pcie_hc->pcie_control_reg)));
						break;
					   }
					default:
						DHD_ERROR(("%s:Unknown module TAG:%d\n",
						  __FUNCTION__,
						  ltoh32(dngl_hc->top_module_tag)));
						break;
				}
				break;
			   }
			default:
			   DHD_ERROR(("%s:Unknown TAG", __FUNCTION__));
			   if (p && DHD_EVENT_ON()) {
				   prhex("SOCRAMIND", p, taglen);
			   }
			   break;
		   }
		   break;
		}
	   default:
		DHD_ERROR(("%s:Unknown DNGL Event Type:%d", __FUNCTION__, type));
		if (p && DHD_EVENT_ON()) {
			prhex("SOCRAMIND", p, datalen);
		}
		break;
	}
#ifdef DHD_FW_COREDUMP
	dhdp->memdump_type = DUMP_TYPE_DONGLE_HOST_EVENT;
#endif /* DHD_FW_COREDUMP */
#ifndef BCMDBUS
	if (dhd_socram_dump(dhdp->bus)) {
		DHD_ERROR(("%s: socram dump failed\n", __FUNCTION__));
	} else {
		/* Notify framework */
		dhd_dbg_send_urgent_evt(dhdp, p, datalen);
	}
#endif /* !BCMDBUS */
}
#endif /* DNGL_EVENT_SUPPORT */

/* Stub for now. Will become real function as soon as shim
 * is being integrated to Android, Linux etc.
 */
int
wl_event_process_default(wl_event_msg_t *event, struct wl_evt_pport *evt_pport)
{
	return BCME_OK;
}

int
wl_event_process(dhd_pub_t *dhd_pub, int *ifidx, void *pktdata,
	uint pktlen, void **data_ptr, void *raw_event)
{
	wl_evt_pport_t evt_pport;
	wl_event_msg_t event;
	bcm_event_msg_u_t evu;
	int ret;

	/* make sure it is a BRCM event pkt and record event data */
	ret = wl_host_event_get_data(pktdata, pktlen, &evu);
	if (ret != BCME_OK) {
		return ret;
	}

	memcpy(&event, &evu.event, sizeof(wl_event_msg_t));

	/* convert event from network order to host order */
	wl_event_to_host_order(&event);

	/* record event params to evt_pport */
	evt_pport.dhd_pub = dhd_pub;
	evt_pport.ifidx = ifidx;
	evt_pport.pktdata = pktdata;
	evt_pport.data_ptr = data_ptr;
	evt_pport.raw_event = raw_event;
	evt_pport.data_len = pktlen;

#if defined(WL_WLC_SHIM) && defined(WL_WLC_SHIM_EVENTS)
	{
		struct wl_shim_node *shim = dhd_pub_shim(dhd_pub);
		if (shim) {
			ret = wl_shim_event_process(shim, &event, &evt_pport);
		} else {
			/* events can come even before shim is initialized
			 (when waiting for "wlc_ver" response)
			 * handle them in a non-shim way.
			 */
			DHD_ERROR(("%s: Events coming before shim initialization!\n",
				__FUNCTION__));
			ret = wl_event_process_default(&event, &evt_pport);
		}
	}
#else
	ret = wl_event_process_default(&event, &evt_pport);
#endif /* WL_WLC_SHIM && WL_WLC_SHIM_EVENTS */

	return ret;
}

/* Check whether packet is a BRCM event pkt. If it is, record event data. */
int
wl_host_event_get_data(void *pktdata, uint pktlen, bcm_event_msg_u_t *evu)
{
	int ret;

	ret = is_wlc_event_frame(pktdata, pktlen, 0, evu);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s: Invalid event frame, err = %d\n",
			__FUNCTION__, ret));
	}

	return ret;
}

int
wl_process_host_event(dhd_pub_t *dhd_pub, int *ifidx, void *pktdata, uint pktlen,
	wl_event_msg_t *event, void **data_ptr, void *raw_event)
{
	bcm_event_t *pvt_data = (bcm_event_t *)pktdata;
	bcm_event_msg_u_t evu;
	uint8 *event_data;
	uint32 type, status, datalen;
	uint16 flags;
	uint evlen;
	int ret;
	uint16 usr_subtype;
	char macstr[ETHER_ADDR_STR_LEN];

	BCM_REFERENCE(macstr);

	ret = wl_host_event_get_data(pktdata, pktlen, &evu);
	if (ret != BCME_OK) {
		return ret;
	}

	usr_subtype = ntoh16_ua((void *)&pvt_data->bcm_hdr.usr_subtype);
	switch (usr_subtype) {
	case BCMILCP_BCM_SUBTYPE_EVENT:
		memcpy(event, &evu.event, sizeof(wl_event_msg_t));
		*data_ptr = &pvt_data[1];
		break;
	case BCMILCP_BCM_SUBTYPE_DNGLEVENT:
#ifdef DNGL_EVENT_SUPPORT
		/* If it is a DNGL event process it first */
		if (dngl_host_event(dhd_pub, pktdata, &evu.dngl_event, pktlen) == BCME_OK) {
			/*
			 * Return error purposely to prevent DNGL event being processed
			 * as BRCM event
			 */
			return BCME_ERROR;
		}
#endif /* DNGL_EVENT_SUPPORT */
		return BCME_NOTFOUND;
	default:
		return BCME_NOTFOUND;
	}

	/* start wl_event_msg process */
	event_data = *data_ptr;
	type = ntoh32_ua((void *)&event->event_type);
	flags = ntoh16_ua((void *)&event->flags);
	status = ntoh32_ua((void *)&event->status);
	datalen = ntoh32_ua((void *)&event->datalen);
	evlen = datalen + sizeof(bcm_event_t);

	switch (type) {
#ifdef PROP_TXSTATUS
	case WLC_E_FIFO_CREDIT_MAP:
		dhd_wlfc_enable(dhd_pub);
		dhd_wlfc_FIFOcreditmap_event(dhd_pub, event_data);
		WLFC_DBGMESG(("WLC_E_FIFO_CREDIT_MAP:(AC0,AC1,AC2,AC3),(BC_MC),(OTHER): "
			"(%d,%d,%d,%d),(%d),(%d)\n", event_data[0], event_data[1],
			event_data[2],
			event_data[3], event_data[4], event_data[5]));
		break;

	case WLC_E_BCMC_CREDIT_SUPPORT:
		dhd_wlfc_BCMCCredit_support_event(dhd_pub);
		break;
#ifdef LIMIT_BORROW
	case WLC_E_ALLOW_CREDIT_BORROW:
		dhd_wlfc_disable_credit_borrow_event(dhd_pub, event_data);
		break;
#endif /* LIMIT_BORROW */
#endif /* PROP_TXSTATUS */


	case WLC_E_ULP:
#ifdef DHD_ULP
	{
		wl_ulp_event_t *ulp_evt = (wl_ulp_event_t *)event_data;

		/* Flush and disable console messages */
		if (ulp_evt->ulp_dongle_action == WL_ULP_DISABLE_CONSOLE) {
#ifdef DHD_ULP_NOT_USED
			dhd_bus_ulp_disable_console(dhd_pub);
#endif /* DHD_ULP_NOT_USED */
		}
		if (ulp_evt->ulp_dongle_action == WL_ULP_UCODE_DOWNLOAD) {
			dhd_bus_ucode_download(dhd_pub->bus);
		}
	}
#endif /* DHD_ULP */
		break;
	case WLC_E_TDLS_PEER_EVENT:
#if defined(WLTDLS) && defined(PCIE_FULL_DONGLE)
		{
			dhd_tdls_event_handler(dhd_pub, event);
		}
#endif
		break;

	case WLC_E_IF:
		{
		struct wl_event_data_if *ifevent = (struct wl_event_data_if *)event_data;

		/* Ignore the event if NOIF is set */
		if (ifevent->reserved & WLC_E_IF_FLAGS_BSSCFG_NOIF) {
			DHD_ERROR(("WLC_E_IF: NO_IF set, event Ignored\r\n"));
			return (BCME_UNSUPPORTED);
		}
#ifdef PCIE_FULL_DONGLE
		dhd_update_interface_flow_info(dhd_pub, ifevent->ifidx,
			ifevent->opcode, ifevent->role);
#endif
#ifdef PROP_TXSTATUS
		{
			uint8* ea = pvt_data->eth.ether_dhost;
			WLFC_DBGMESG(("WLC_E_IF: idx:%d, action:%s, iftype:%s, "
						  "[%02x:%02x:%02x:%02x:%02x:%02x]\n",
						  ifevent->ifidx,
						  ((ifevent->opcode == WLC_E_IF_ADD) ? "ADD":"DEL"),
						  ((ifevent->role == 0) ? "STA":"AP "),
						  ea[0], ea[1], ea[2], ea[3], ea[4], ea[5]));
			(void)ea;

			if (ifevent->opcode == WLC_E_IF_CHANGE)
				dhd_wlfc_interface_event(dhd_pub,
					eWLFC_MAC_ENTRY_ACTION_UPDATE,
					ifevent->ifidx, ifevent->role, ea);
			else
				dhd_wlfc_interface_event(dhd_pub,
					((ifevent->opcode == WLC_E_IF_ADD) ?
					eWLFC_MAC_ENTRY_ACTION_ADD : eWLFC_MAC_ENTRY_ACTION_DEL),
					ifevent->ifidx, ifevent->role, ea);

			/* dhd already has created an interface by default, for 0 */
			if (ifevent->ifidx == 0)
				break;
		}
#endif /* PROP_TXSTATUS */

		if (ifevent->ifidx > 0 && ifevent->ifidx < DHD_MAX_IFS) {
			if (ifevent->opcode == WLC_E_IF_ADD) {
				if (dhd_event_ifadd(dhd_pub->info, ifevent, event->ifname,
					event->addr.octet)) {

					DHD_ERROR(("%s: dhd_event_ifadd failed ifidx: %d  %s\n",
						__FUNCTION__, ifevent->ifidx, event->ifname));
					return (BCME_ERROR);
				}
			} else if (ifevent->opcode == WLC_E_IF_DEL) {
#ifdef PCIE_FULL_DONGLE
				/* Delete flowrings unconditionally for i/f delete */
				dhd_flow_rings_delete(dhd_pub, (uint8)dhd_ifname2idx(dhd_pub->info,
					event->ifname));
#endif /* PCIE_FULL_DONGLE */
				dhd_event_ifdel(dhd_pub->info, ifevent, event->ifname,
					event->addr.octet);
				/* Return ifidx (for vitual i/f, it will be > 0)
				 * so that no other operations on deleted interface
				 * are carried out
				 */
				ret = ifevent->ifidx;
				goto exit;
			} else if (ifevent->opcode == WLC_E_IF_CHANGE) {
#ifdef WL_CFG80211
				dhd_event_ifchange(dhd_pub->info, ifevent, event->ifname,
					event->addr.octet);
#endif /* WL_CFG80211 */
			}
		} else {
#if !defined(PROP_TXSTATUS) && !defined(PCIE_FULL_DONGLE) && defined(WL_CFG80211)
			DHD_INFO(("%s: Invalid ifidx %d for %s\n",
			   __FUNCTION__, ifevent->ifidx, event->ifname));
#endif /* !PROP_TXSTATUS && !PCIE_FULL_DONGLE && WL_CFG80211 */
		}
			/* send up the if event: btamp user needs it */
			*ifidx = dhd_ifname2idx(dhd_pub->info, event->ifname);
			/* push up to external supp/auth */
			dhd_event(dhd_pub->info, (char *)pvt_data, evlen, *ifidx);
		break;
	}

#ifdef WLMEDIA_HTSF
	case WLC_E_HTSFSYNC:
		htsf_update(dhd_pub->info, event_data);
		break;
#endif /* WLMEDIA_HTSF */
	case WLC_E_NDIS_LINK:
		break;
	case WLC_E_PFN_NET_FOUND:
	case WLC_E_PFN_SCAN_ALLGONE: /* share with WLC_E_PFN_BSSID_NET_LOST */
	case WLC_E_PFN_NET_LOST:
		break;
#if defined(PNO_SUPPORT)
	case WLC_E_PFN_BSSID_NET_FOUND:
	case WLC_E_PFN_BEST_BATCHING:
		dhd_pno_event_handler(dhd_pub, event, (void *)event_data);
		break;
#endif 
#if defined(RTT_SUPPORT)
	case WLC_E_PROXD:
		dhd_rtt_event_handler(dhd_pub, event, (void *)event_data);
		break;
#endif /* RTT_SUPPORT */
		/* These are what external supplicant/authenticator wants */
	case WLC_E_ASSOC_IND:
	case WLC_E_AUTH_IND:
	case WLC_E_REASSOC_IND:
		dhd_findadd_sta(dhd_pub,
			dhd_ifname2idx(dhd_pub->info, event->ifname),
			&event->addr.octet);
		break;
#ifndef BCMDBUS
#if defined(DHD_FW_COREDUMP)
	case WLC_E_PSM_WATCHDOG:
		DHD_ERROR(("%s: WLC_E_PSM_WATCHDOG event received : \n", __FUNCTION__));
		if (dhd_socram_dump(dhd_pub->bus) != BCME_OK) {
			DHD_ERROR(("%s: socram dump ERROR : \n", __FUNCTION__));
		}
	break;
#endif
#endif /* !BCMDBUS */
#ifdef DHD_WMF
	case WLC_E_PSTA_PRIMARY_INTF_IND:
		dhd_update_psta_interface_for_sta(dhd_pub, event->ifname,
			(void *)(event->addr.octet), (void*) event_data);
		break;
#endif
	case WLC_E_LINK:
#ifdef PCIE_FULL_DONGLE
		DHD_EVENT(("%s: Link event %d, flags %x, status %x\n",
			__FUNCTION__, type, flags, status));
		if (dhd_update_interface_link_status(dhd_pub, (uint8)dhd_ifname2idx(dhd_pub->info,
			event->ifname), (uint8)flags) != BCME_OK) {
			DHD_ERROR(("%s: dhd_update_interface_link_status Failed.\n",
				__FUNCTION__));
			break;
		}
		if (!flags) {
			DHD_ERROR(("%s: Deleting all STA from assoc list and flowrings.\n",
				__FUNCTION__));
			/* Delete all sta and flowrings */
			dhd_del_all_sta(dhd_pub, dhd_ifname2idx(dhd_pub->info, event->ifname));
			dhd_flow_rings_delete(dhd_pub, (uint8)dhd_ifname2idx(dhd_pub->info,
				event->ifname));
		}
		/* fall through */
#endif /* PCIE_FULL_DONGLE */
	case WLC_E_DEAUTH:
	case WLC_E_DEAUTH_IND:
	case WLC_E_DISASSOC:
	case WLC_E_DISASSOC_IND:
#ifdef PCIE_FULL_DONGLE
		if (type != WLC_E_LINK) {
			uint8 ifindex = (uint8)dhd_ifname2idx(dhd_pub->info, event->ifname);
			uint8 role = dhd_flow_rings_ifindex2role(dhd_pub, ifindex);
			uint8 del_sta = TRUE;
#ifdef WL_CFG80211
			if (role == WLC_E_IF_ROLE_STA &&
				!wl_cfg80211_is_roam_offload(dhd_idx2net(dhd_pub, ifindex)) &&
					!wl_cfg80211_is_event_from_connected_bssid(
						dhd_idx2net(dhd_pub, ifindex), event, *ifidx)) {
				del_sta = FALSE;
			}
#endif /* WL_CFG80211 */
			DHD_EVENT(("%s: Link event %d, flags %x, status %x, role %d, del_sta %d\n",
				__FUNCTION__, type, flags, status, role, del_sta));

			if (del_sta) {
				DHD_MAC_TO_STR((event->addr.octet), macstr);
				DHD_EVENT(("%s: Deleting STA %s\n", __FUNCTION__, macstr));

				dhd_del_sta(dhd_pub, dhd_ifname2idx(dhd_pub->info,
					event->ifname), &event->addr.octet);
				/* Delete all flowrings for STA and P2P Client */
				if (role == WLC_E_IF_ROLE_STA || role == WLC_E_IF_ROLE_P2P_CLIENT) {
					dhd_flow_rings_delete(dhd_pub, ifindex);
				} else {
					dhd_flow_rings_delete_for_peer(dhd_pub, ifindex,
						(char *)&event->addr.octet[0]);
				}
			}
		}
#endif /* PCIE_FULL_DONGLE */
		/* fall through */

	default:
		*ifidx = dhd_ifname2idx(dhd_pub->info, event->ifname);
#ifdef DHD_UPDATE_INTF_MAC
		if ((WLC_E_LINK==type)&&(WLC_EVENT_MSG_LINK&flags)) {
			dhd_event_ifchange(dhd_pub->info,
			(struct wl_event_data_if *)event,
			event->ifname,
			event->addr.octet);
		}
#endif /* DHD_UPDATE_INTF_MAC */
		/* push up to external supp/auth */
		dhd_event(dhd_pub->info, (char *)pvt_data, evlen, *ifidx);
		DHD_TRACE(("%s: MAC event %d, flags %x, status %x\n",
			__FUNCTION__, type, flags, status));
		BCM_REFERENCE(flags);
		BCM_REFERENCE(status);

		break;
	}
#if defined(STBAP)
	/* For routers, EAPD will be working on these events.
	 * Overwrite interface name to that event is pushed
	 * to host with its registered interface name
	 */
	memcpy(pvt_data->event.ifname, dhd_ifname(dhd_pub, *ifidx), IFNAMSIZ);
#endif

exit:

#ifdef SHOW_EVENTS
	if (DHD_FWLOG_ON() || DHD_EVENT_ON()) {
		wl_show_host_event(dhd_pub, event,
			(void *)event_data, raw_event, dhd_pub->enable_log);
	}
#endif /* SHOW_EVENTS */

	return ret;
}

int
wl_host_event(dhd_pub_t *dhd_pub, int *ifidx, void *pktdata, uint pktlen,
	wl_event_msg_t *event, void **data_ptr, void *raw_event)
{
	return wl_process_host_event(dhd_pub, ifidx, pktdata, pktlen, event, data_ptr,
			raw_event);
}

void
dhd_print_buf(void *pbuf, int len, int bytes_per_line)
{
#ifdef DHD_DEBUG
	int i, j = 0;
	unsigned char *buf = pbuf;

	if (bytes_per_line == 0) {
		bytes_per_line = len;
	}

	for (i = 0; i < len; i++) {
		printf("%2.2x", *buf++);
		j++;
		if (j == bytes_per_line) {
			printf("\n");
			j = 0;
		} else {
			printf(":");
		}
	}
	printf("\n");
#endif /* DHD_DEBUG */
}
#ifndef strtoul
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#endif

#if defined(PKT_FILTER_SUPPORT) || defined(DHD_PKT_LOGGING)
/* Convert user's input in hex pattern to byte-size mask */
int
wl_pattern_atoh(char *src, char *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 &&
	    strncmp(src, "0X", 2) != 0) {
		DHD_ERROR(("Mask invalid format. Needs to start with 0x\n"));
		return -1;
	}
	src = src + 2; /* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		DHD_ERROR(("Mask invalid format. Needs to be of even length\n"));
		return -1;
	}
	for (i = 0; *src != '\0'; i++) {
		char num[3];
		bcm_strncpy_s(num, sizeof(num), src, 2);
		num[2] = '\0';
		dst[i] = (uint8)strtoul(num, NULL, 16);
		src += 2;
	}
	return i;
}
#endif /* PKT_FILTER_SUPPORT || DHD_PKT_LOGGING */

#ifdef PKT_FILTER_SUPPORT
void
dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode)
{
	char				*argv[8];
	int					i = 0;
	const char			*str;
	int					buf_len;
	int					str_len;
	char				*arg_save = 0, *arg_org = 0;
	int					rc;
	char				buf[32] = {0};
	wl_pkt_filter_enable_t	enable_parm;
	wl_pkt_filter_enable_t	* pkt_filterp;

	if (!arg)
		return;

	if (!(arg_save = MALLOC(dhd->osh, strlen(arg) + 1))) {
		DHD_ERROR(("%s: malloc failed\n", __FUNCTION__));
		goto fail;
	}
	arg_org = arg_save;
	memcpy(arg_save, arg, strlen(arg) + 1);

	argv[i] = bcmstrtok(&arg_save, " ", 0);

	i = 0;
	if (argv[i] == NULL) {
		DHD_ERROR(("No args provided\n"));
		goto fail;
	}

	str = "pkt_filter_enable";
	str_len = strlen(str);
	bcm_strncpy_s(buf, sizeof(buf) - 1, str, sizeof(buf) - 1);
	buf[ sizeof(buf) - 1 ] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (wl_pkt_filter_enable_t *)(buf + str_len + 1);

	/* Parse packet filter id. */
	enable_parm.id = htod32(strtoul(argv[i], NULL, 0));
	if (dhd_conf_del_pkt_filter(dhd, enable_parm.id))
		goto fail;

	/* Parse enable/disable value. */
	enable_parm.enable = htod32(enable);

	buf_len += sizeof(enable_parm);
	memcpy((char *)pkt_filterp,
	       &enable_parm,
	       sizeof(enable_parm));

	/* Enable/disable the specified filter. */
	rc = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, buf_len, TRUE, 0);
	rc = rc >= 0 ? 0 : rc;
	if (rc)
		DHD_ERROR(("%s: failed to %s pktfilter %s, retcode = %d\n",
		__FUNCTION__, enable?"enable":"disable", arg, rc));
	else
		DHD_TRACE(("%s: successfully %s pktfilter %s\n",
		__FUNCTION__, enable?"enable":"disable", arg));

	/* Contorl the master mode */
	rc = dhd_wl_ioctl_set_intiovar(dhd, "pkt_filter_mode",
		master_mode, WLC_SET_VAR, TRUE, 0);
	rc = rc >= 0 ? 0 : rc;
	if (rc)
		DHD_TRACE(("%s: failed to set pkt_filter_mode %d, retcode = %d\n",
			__FUNCTION__, master_mode, rc));

fail:
	if (arg_org)
		MFREE(dhd->osh, arg_org, strlen(arg) + 1);
}

/* Packet filter section: extended filters have named offsets, add table here */
typedef struct {
	char *name;
	uint16 base;
} wl_pfbase_t;

static wl_pfbase_t basenames[] = { WL_PKT_FILTER_BASE_NAMES };

static int
wl_pkt_filter_base_parse(char *name)
{
	uint i;
	char *bname, *uname;

	for (i = 0; i < ARRAYSIZE(basenames); i++) {
		bname = basenames[i].name;
		for (uname = name; *uname; bname++, uname++) {
			if (*bname != bcm_toupper(*uname)) {
				break;
			}
		}
		if (!*uname && !*bname) {
			break;
		}
	}

	if (i < ARRAYSIZE(basenames)) {
		return basenames[i].base;
	} else {
		return -1;
	}
}

void
dhd_pktfilter_offload_set(dhd_pub_t * dhd, char *arg)
{
	const char 			*str;
	wl_pkt_filter_t		pkt_filter;
	wl_pkt_filter_t		*pkt_filterp;
	int					buf_len;
	int					str_len;
	int 				rc;
	uint32				mask_size;
	uint32				pattern_size;
	char				*argv[16], * buf = 0;
	int					i = 0;
	char				*arg_save = 0, *arg_org = 0;
#define BUF_SIZE		2048

	if (!arg)
		return;

	if (!(arg_save = MALLOC(dhd->osh, strlen(arg) + 1))) {
		DHD_ERROR(("%s: malloc failed\n", __FUNCTION__));
		goto fail;
	}

	arg_org = arg_save;

	if (!(buf = MALLOC(dhd->osh, BUF_SIZE))) {
		DHD_ERROR(("%s: malloc failed\n", __FUNCTION__));
		goto fail;
	}
	memset(buf, 0, BUF_SIZE);
	memcpy(arg_save, arg, strlen(arg) + 1);

	if (strlen(arg) > BUF_SIZE) {
		DHD_ERROR(("Not enough buffer %d < %d\n", (int)strlen(arg), (int)sizeof(buf)));
		goto fail;
	}

	argv[i] = bcmstrtok(&arg_save, " ", 0);
	while (argv[i++])
		argv[i] = bcmstrtok(&arg_save, " ", 0);

	i = 0;
	if (argv[i] == NULL) {
		DHD_ERROR(("No args provided\n"));
		goto fail;
	}

	str = "pkt_filter_add";
	str_len = strlen(str);
	bcm_strncpy_s(buf, BUF_SIZE, str, str_len);
	buf[ str_len ] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (wl_pkt_filter_t *) (buf + str_len + 1);

	/* Parse packet filter id. */
	pkt_filter.id = htod32(strtoul(argv[i], NULL, 0));
	if (dhd_conf_del_pkt_filter(dhd, pkt_filter.id))
		goto fail;

	if (argv[++i] == NULL) {
		DHD_ERROR(("Polarity not provided\n"));
		goto fail;
	}

	/* Parse filter polarity. */
	pkt_filter.negate_match = htod32(strtoul(argv[i], NULL, 0));

	if (argv[++i] == NULL) {
		DHD_ERROR(("Filter type not provided\n"));
		goto fail;
	}

	/* Parse filter type. */
	pkt_filter.type = htod32(strtoul(argv[i], NULL, 0));

	if ((pkt_filter.type == 0) || (pkt_filter.type == 1)) {
		if (argv[++i] == NULL) {
			DHD_ERROR(("Offset not provided\n"));
			goto fail;
		}

		/* Parse pattern filter offset. */
		pkt_filter.u.pattern.offset = htod32(strtoul(argv[i], NULL, 0));

		if (argv[++i] == NULL) {
			DHD_ERROR(("Bitmask not provided\n"));
			goto fail;
		}

		/* Parse pattern filter mask. */
		mask_size =
			htod32(wl_pattern_atoh(argv[i],
			(char *) pkt_filterp->u.pattern.mask_and_pattern));

		if (argv[++i] == NULL) {
			DHD_ERROR(("Pattern not provided\n"));
			goto fail;
		}

		/* Parse pattern filter pattern. */
		pattern_size =
			htod32(wl_pattern_atoh(argv[i],
			(char *) &pkt_filterp->u.pattern.mask_and_pattern[mask_size]));

		if (mask_size != pattern_size) {
			DHD_ERROR(("Mask and pattern not the same size\n"));
			goto fail;
		}

		pkt_filter.u.pattern.size_bytes = mask_size;
		buf_len += WL_PKT_FILTER_FIXED_LEN;
		buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + 2 * mask_size);

		/* Keep-alive attributes are set in local	variable (keep_alive_pkt), and
		 * then memcpy'ed into buffer (keep_alive_pktp) since there is no
		 * guarantee that the buffer is properly aligned.
		 */
		memcpy((char *)pkt_filterp,
			&pkt_filter,
			WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN);
	} else if ((pkt_filter.type == 2) || (pkt_filter.type == 6)) {
		int list_cnt = 0;
		char *endptr = '\0';
		wl_pkt_filter_pattern_listel_t *pf_el = &pkt_filterp->u.patlist.patterns[0];

		while (argv[++i] != NULL) {
			/* Parse pattern filter base and offset. */
			if (bcm_isdigit(*argv[i])) {
				/* Numeric base */
				rc = strtoul(argv[i], &endptr, 0);
			} else {
				endptr = strchr(argv[i], ':');
				if (endptr) {
					*endptr = '\0';
					rc = wl_pkt_filter_base_parse(argv[i]);
					if (rc == -1) {
						 printf("Invalid base %s\n", argv[i]);
						goto fail;
					}
					*endptr = ':';
				} else {
					printf("Invalid [base:]offset format: %s\n", argv[i]);
					goto fail;
				}
			}

			if (*endptr == ':') {
				pkt_filter.u.patlist.patterns[0].base_offs = htod16(rc);
				rc = strtoul(endptr + 1, &endptr, 0);
			} else {
				/* Must have had a numeric offset only */
				pkt_filter.u.patlist.patterns[0].base_offs = htod16(0);
			}

			if (*endptr) {
				printf("Invalid [base:]offset format: %s\n", argv[i]);
				goto fail;
			}
			if (rc > 0x0000FFFF) {
				printf("Offset too large\n");
				goto fail;
			}
			pkt_filter.u.patlist.patterns[0].rel_offs = htod16(rc);

			/* Clear match_flag (may be set in parsing which follows) */
			pkt_filter.u.patlist.patterns[0].match_flags = htod16(0);

			/* Parse pattern filter mask and pattern directly into ioctl buffer */
			if (argv[++i] == NULL) {
				printf("Bitmask not provided\n");
				goto fail;
			}
			rc = wl_pattern_atoh(argv[i], (char*)pf_el->mask_and_data);
			if (rc == -1) {
				printf("Rejecting: %s\n", argv[i]);
				goto fail;
			}
			mask_size = htod16(rc);

			if (argv[++i] == NULL) {
				printf("Pattern not provided\n");
				goto fail;
			}

			if (*argv[i] == '!') {
				pkt_filter.u.patlist.patterns[0].match_flags =
					htod16(WL_PKT_FILTER_MFLAG_NEG);
				(argv[i])++;
			}
			if (*argv[i] == '\0') {
				printf("Pattern not provided\n");
				goto fail;
			}
			rc = wl_pattern_atoh(argv[i], (char*)&pf_el->mask_and_data[rc]);
			if (rc == -1) {
				printf("Rejecting: %s\n", argv[i]);
				goto fail;
			}
			pattern_size = htod16(rc);

			if (mask_size != pattern_size) {
				printf("Mask and pattern not the same size\n");
				goto fail;
			}

			pkt_filter.u.patlist.patterns[0].size_bytes = mask_size;

			/* Account for the size of this pattern element */
			buf_len += WL_PKT_FILTER_PATTERN_LISTEL_FIXED_LEN + 2 * rc;

			/* And the pattern element fields that were put in a local for
			 * alignment purposes now get copied to the ioctl buffer.
			 */
			memcpy((char*)pf_el, &pkt_filter.u.patlist.patterns[0],
				WL_PKT_FILTER_PATTERN_FIXED_LEN);

			/* Move to next element location in ioctl buffer */
			pf_el = (wl_pkt_filter_pattern_listel_t*)
				((uint8*)pf_el + WL_PKT_FILTER_PATTERN_LISTEL_FIXED_LEN + 2 * rc);

			/* Count list element */
			list_cnt++;
		}

		/* Account for initial fixed size, and copy initial fixed fields */
		buf_len += WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_LIST_FIXED_LEN;

		/* Update list count and total size */
		pkt_filter.u.patlist.list_cnt = list_cnt;
		pkt_filter.u.patlist.PAD1[0] = 0;
		pkt_filter.u.patlist.totsize = buf + buf_len - (char*)pkt_filterp;
		pkt_filter.u.patlist.totsize -= WL_PKT_FILTER_FIXED_LEN;

		memcpy((char *)pkt_filterp, &pkt_filter,
			WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_LIST_FIXED_LEN);
	} else {
		DHD_ERROR(("Invalid filter type %d\n", pkt_filter.type));
		goto fail;
	}

	rc = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, buf_len, TRUE, 0);
	rc = rc >= 0 ? 0 : rc;

	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
		__FUNCTION__, arg, rc));
	else
		DHD_TRACE(("%s: successfully added pktfilter %s\n",
		__FUNCTION__, arg));

fail:
	if (arg_org)
		MFREE(dhd->osh, arg_org, strlen(arg) + 1);

	if (buf)
		MFREE(dhd->osh, buf, BUF_SIZE);
}

void
dhd_pktfilter_offload_delete(dhd_pub_t *dhd, int id)
{
	int ret;

	ret = dhd_wl_ioctl_set_intiovar(dhd, "pkt_filter_delete",
		id, WLC_SET_VAR, TRUE, 0);
	if (ret < 0) {
		DHD_ERROR(("%s: Failed to delete filter ID:%d, ret=%d\n",
			__FUNCTION__, id, ret));
	}
	else
		DHD_TRACE(("%s: successfully deleted pktfilter %d\n",
		__FUNCTION__, id));
}
#endif /* PKT_FILTER_SUPPORT */

/* ========================== */
/* ==== ARP OFFLOAD SUPPORT = */
/* ========================== */
#ifdef ARP_OFFLOAD_SUPPORT
void
dhd_arp_offload_set(dhd_pub_t * dhd, int arp_mode)
{
	int retcode;

	retcode = dhd_wl_ioctl_set_intiovar(dhd, "arp_ol",
		arp_mode, WLC_SET_VAR, TRUE, 0);

	retcode = retcode >= 0 ? 0 : retcode;
	if (retcode)
		DHD_ERROR(("%s: failed to set ARP offload mode to 0x%x, retcode = %d\n",
			__FUNCTION__, arp_mode, retcode));
	else
		DHD_ARPOE(("%s: successfully set ARP offload mode to 0x%x\n",
			__FUNCTION__, arp_mode));
}

void
dhd_arp_offload_enable(dhd_pub_t * dhd, int arp_enable)
{
	int retcode;

	retcode = dhd_wl_ioctl_set_intiovar(dhd, "arpoe",
		arp_enable, WLC_SET_VAR, TRUE, 0);

	retcode = retcode >= 0 ? 0 : retcode;
	if (retcode)
		DHD_ERROR(("%s: failed to enabe ARP offload to %d, retcode = %d\n",
			__FUNCTION__, arp_enable, retcode));
	else
		DHD_ARPOE(("%s: successfully enabed ARP offload to %d\n",
			__FUNCTION__, arp_enable));
	if (arp_enable) {
		uint32 version;
		retcode = dhd_wl_ioctl_get_intiovar(dhd, "arp_version",
			&version, WLC_GET_VAR, FALSE, 0);
		if (retcode) {
			DHD_INFO(("%s: fail to get version (maybe version 1:retcode = %d\n",
				__FUNCTION__, retcode));
			dhd->arp_version = 1;
		}
		else {
			DHD_INFO(("%s: ARP Version= %x\n", __FUNCTION__, version));
			dhd->arp_version = version;
		}
	}
}

void
dhd_aoe_arp_clr(dhd_pub_t *dhd, int idx)
{
	int ret = 0;

	if (dhd == NULL) return;
	if (dhd->arp_version == 1)
		idx = 0;

	ret = dhd_iovar(dhd, idx, "arp_table_clear", NULL, 0, NULL, 0, TRUE);
	if (ret < 0)
		DHD_ERROR(("%s failed code %d\n", __FUNCTION__, ret));
}

void
dhd_aoe_hostip_clr(dhd_pub_t *dhd, int idx)
{
	int ret = 0;

	if (dhd == NULL) return;
	if (dhd->arp_version == 1)
		idx = 0;

	ret = dhd_iovar(dhd, idx, "arp_hostip_clear", NULL, 0, NULL, 0, TRUE);
	if (ret < 0)
		DHD_ERROR(("%s failed code %d\n", __FUNCTION__, ret));
}

void
dhd_arp_offload_add_ip(dhd_pub_t *dhd, uint32 ipaddr, int idx)
{
	int ret;

	if (dhd == NULL) return;
	if (dhd->arp_version == 1)
		idx = 0;

	ret = dhd_iovar(dhd, idx, "arp_hostip", (char *)&ipaddr, sizeof(ipaddr),
			NULL, 0, TRUE);
	if (ret)
		DHD_ERROR(("%s: ARP ip addr add failed, ret = %d\n", __FUNCTION__, ret));
	else
		DHD_ARPOE(("%s: sARP H ipaddr entry added \n",
			__FUNCTION__));
}

int
dhd_arp_get_arp_hostip_table(dhd_pub_t *dhd, void *buf, int buflen, int idx)
{
	int ret, i;
	uint32 *ptr32 = buf;
	bool clr_bottom = FALSE;

	if (!buf)
		return -1;
	if (dhd == NULL) return -1;
	if (dhd->arp_version == 1)
		idx = 0;

	ret = dhd_iovar(dhd, idx, "arp_hostip", NULL, 0, (char *)buf, buflen,
			FALSE);
	if (ret) {
		DHD_ERROR(("%s: ioctl WLC_GET_VAR error %d\n",
		__FUNCTION__, ret));

		return -1;
	}

	/* clean up the buf, ascii reminder */
	for (i = 0; i < MAX_IPV4_ENTRIES; i++) {
		if (!clr_bottom) {
			if (*ptr32 == 0)
				clr_bottom = TRUE;
		} else {
			*ptr32 = 0;
		}
		ptr32++;
	}

	return 0;
}
#endif /* ARP_OFFLOAD_SUPPORT  */

/*
 * Neighbor Discovery Offload: enable NDO feature
 * Called  by ipv6 event handler when interface comes up/goes down
 */
int
dhd_ndo_enable(dhd_pub_t * dhd, int ndo_enable)
{
	int retcode;

	if (dhd == NULL)
		return -1;

	retcode = dhd_wl_ioctl_set_intiovar(dhd, "ndoe",
		ndo_enable, WLC_SET_VAR, TRUE, 0);
	if (retcode)
		DHD_ERROR(("%s: failed to enabe ndo to %d, retcode = %d\n",
			__FUNCTION__, ndo_enable, retcode));
	else
		DHD_TRACE(("%s: successfully enabed ndo offload to %d\n",
			__FUNCTION__, ndo_enable));

	return retcode;
}

/*
 * Neighbor Discover Offload: enable NDO feature
 * Called  by ipv6 event handler when interface comes up
 */
int
dhd_ndo_add_ip(dhd_pub_t *dhd, char* ipv6addr, int idx)
{
	int iov_len = 0;
	char iovbuf[DHD_IOVAR_BUF_SIZE] = {0};
	int retcode;

	if (dhd == NULL)
		return -1;

	iov_len = bcm_mkiovar("nd_hostip", (char *)ipv6addr,
		IPV6_ADDR_LEN, iovbuf, sizeof(iovbuf));
	if (!iov_len) {
		DHD_ERROR(("%s: Insufficient iovar buffer size %zu \n",
			__FUNCTION__, sizeof(iovbuf)));
		return -1;
	}
	retcode = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, iov_len, TRUE, idx);

	if (retcode)
		DHD_ERROR(("%s: ndo ip addr add failed, retcode = %d\n",
		__FUNCTION__, retcode));
	else
		DHD_TRACE(("%s: ndo ipaddr entry added \n",
		__FUNCTION__));

	return retcode;
}

/*
 * Neighbor Discover Offload: enable NDO feature
 * Called  by ipv6 event handler when interface goes down
 */
int
dhd_ndo_remove_ip(dhd_pub_t *dhd, int idx)
{
	int iov_len = 0;
	char iovbuf[DHD_IOVAR_BUF_SIZE] = {0};
	int retcode;

	if (dhd == NULL)
		return -1;

	iov_len = bcm_mkiovar("nd_hostip_clear", NULL,
		0, iovbuf, sizeof(iovbuf));
	if (!iov_len) {
		DHD_ERROR(("%s: Insufficient iovar buffer size %zu \n",
			__FUNCTION__, sizeof(iovbuf)));
		return -1;
	}
	retcode = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovbuf, iov_len, TRUE, idx);

	if (retcode)
		DHD_ERROR(("%s: ndo ip addr remove failed, retcode = %d\n",
		__FUNCTION__, retcode));
	else
		DHD_TRACE(("%s: ndo ipaddr entry removed \n",
		__FUNCTION__));

	return retcode;
}

/* Enhanced ND offload */
uint16
dhd_ndo_get_version(dhd_pub_t *dhdp)
{
	char iovbuf[DHD_IOVAR_BUF_SIZE];
	wl_nd_hostip_t ndo_get_ver;
	int iov_len;
	int retcode;
	uint16 ver = 0;

	if (dhdp == NULL) {
		return BCME_ERROR;
	}

	memset(&iovbuf, 0, sizeof(iovbuf));
	ndo_get_ver.version = htod16(WL_ND_HOSTIP_IOV_VER);
	ndo_get_ver.op_type = htod16(WL_ND_HOSTIP_OP_VER);
	ndo_get_ver.length = htod32(WL_ND_HOSTIP_FIXED_LEN + sizeof(uint16));
	ndo_get_ver.u.version = 0;
	iov_len = bcm_mkiovar("nd_hostip", (char *)&ndo_get_ver,
			WL_ND_HOSTIP_FIXED_LEN + sizeof(uint16), iovbuf, sizeof(iovbuf));
	if (!iov_len) {
		DHD_ERROR(("%s: Insufficient iovar buffer size %zu \n",
			__FUNCTION__, sizeof(iovbuf)));
		return BCME_ERROR;
	}

	retcode = dhd_wl_ioctl_cmd(dhdp, WLC_GET_VAR, iovbuf, iov_len, FALSE, 0);
	if (retcode) {
		DHD_ERROR(("%s: failed, retcode = %d\n", __FUNCTION__, retcode));
		/* ver iovar not supported. NDO version is 0 */
		ver = 0;
	} else {
		wl_nd_hostip_t *ndo_ver_ret = (wl_nd_hostip_t *)iovbuf;

		if ((dtoh16(ndo_ver_ret->version) == WL_ND_HOSTIP_IOV_VER) &&
				(dtoh16(ndo_ver_ret->op_type) == WL_ND_HOSTIP_OP_VER) &&
				(dtoh32(ndo_ver_ret->length) == WL_ND_HOSTIP_FIXED_LEN
					+ sizeof(uint16))) {
			/* nd_hostip iovar version */
			ver = dtoh16(ndo_ver_ret->u.version);
		}

		DHD_TRACE(("%s: successfully get version: %d\n", __FUNCTION__, ver));
	}

	return ver;
}

int
dhd_ndo_add_ip_with_type(dhd_pub_t *dhdp, char *ipv6addr, uint8 type, int idx)
{
	char iovbuf[DHD_IOVAR_BUF_SIZE];
	wl_nd_hostip_t ndo_add_addr;
	int iov_len;
	int retcode;

	if (dhdp == NULL || ipv6addr == 0) {
		return BCME_ERROR;
	}

	/* wl_nd_hostip_t fixed param */
	ndo_add_addr.version = htod16(WL_ND_HOSTIP_IOV_VER);
	ndo_add_addr.op_type = htod16(WL_ND_HOSTIP_OP_ADD);
	ndo_add_addr.length = htod32(WL_ND_HOSTIP_WITH_ADDR_LEN);
	/* wl_nd_host_ip_addr_t param for add */
	memcpy(&ndo_add_addr.u.host_ip.ip_addr, ipv6addr, IPV6_ADDR_LEN);
	ndo_add_addr.u.host_ip.type = type;

	iov_len = bcm_mkiovar("nd_hostip", (char *)&ndo_add_addr,
		WL_ND_HOSTIP_WITH_ADDR_LEN, iovbuf, sizeof(iovbuf));
	if (!iov_len) {
		DHD_ERROR(("%s: Insufficient iovar buffer size %zu \n",
			__FUNCTION__, sizeof(iovbuf)));
		return BCME_ERROR;
	}

	retcode = dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, iov_len, TRUE, idx);
	if (retcode) {
		DHD_ERROR(("%s: failed, retcode = %d\n", __FUNCTION__, retcode));
#ifdef NDO_CONFIG_SUPPORT
		if (retcode == BCME_NORESOURCE) {
			/* number of host ip addr exceeds FW capacity, Deactivate ND offload */
			DHD_INFO(("%s: Host IP count exceed device capacity,"
				"ND offload deactivated\n", __FUNCTION__));
			dhdp->ndo_host_ip_overflow = TRUE;
			dhd_ndo_enable(dhdp, 0);
		}
#endif /* NDO_CONFIG_SUPPORT */
	} else {
		DHD_TRACE(("%s: successfully added: %d\n", __FUNCTION__, retcode));
	}

	return retcode;
}

int
dhd_ndo_remove_ip_by_addr(dhd_pub_t *dhdp, char *ipv6addr, int idx)
{
	char iovbuf[DHD_IOVAR_BUF_SIZE];
	wl_nd_hostip_t ndo_del_addr;
	int iov_len;
	int retcode;

	if (dhdp == NULL || ipv6addr == 0) {
		return BCME_ERROR;
	}

	/* wl_nd_hostip_t fixed param */
	ndo_del_addr.version = htod16(WL_ND_HOSTIP_IOV_VER);
	ndo_del_addr.op_type = htod16(WL_ND_HOSTIP_OP_DEL);
	ndo_del_addr.length = htod32(WL_ND_HOSTIP_WITH_ADDR_LEN);
	/* wl_nd_host_ip_addr_t param for del */
	memcpy(&ndo_del_addr.u.host_ip.ip_addr, ipv6addr, IPV6_ADDR_LEN);
	ndo_del_addr.u.host_ip.type = 0;	/* don't care */

	iov_len = bcm_mkiovar("nd_hostip", (char *)&ndo_del_addr,
		WL_ND_HOSTIP_WITH_ADDR_LEN, iovbuf, sizeof(iovbuf));
	if (!iov_len) {
		DHD_ERROR(("%s: Insufficient iovar buffer size %zu \n",
			__FUNCTION__, sizeof(iovbuf)));
		return BCME_ERROR;
	}

	retcode = dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, iov_len, TRUE, idx);
	if (retcode) {
		DHD_ERROR(("%s: failed, retcode = %d\n", __FUNCTION__, retcode));
	} else {
		DHD_TRACE(("%s: successfully removed: %d\n", __FUNCTION__, retcode));
	}

	return retcode;
}

int
dhd_ndo_remove_ip_by_type(dhd_pub_t *dhdp, uint8 type, int idx)
{
	char iovbuf[DHD_IOVAR_BUF_SIZE];
	wl_nd_hostip_t ndo_del_addr;
	int iov_len;
	int retcode;

	if (dhdp == NULL) {
		return BCME_ERROR;
	}

	/* wl_nd_hostip_t fixed param */
	ndo_del_addr.version = htod16(WL_ND_HOSTIP_IOV_VER);
	if (type == WL_ND_IPV6_ADDR_TYPE_UNICAST) {
		ndo_del_addr.op_type = htod16(WL_ND_HOSTIP_OP_DEL_UC);
	} else if (type == WL_ND_IPV6_ADDR_TYPE_ANYCAST) {
		ndo_del_addr.op_type = htod16(WL_ND_HOSTIP_OP_DEL_AC);
	} else {
		return BCME_BADARG;
	}
	ndo_del_addr.length = htod32(WL_ND_HOSTIP_FIXED_LEN);

	iov_len = bcm_mkiovar("nd_hostip", (char *)&ndo_del_addr, WL_ND_HOSTIP_FIXED_LEN,
			iovbuf, sizeof(iovbuf));
	if (!iov_len) {
		DHD_ERROR(("%s: Insufficient iovar buffer size %zu \n",
			__FUNCTION__, sizeof(iovbuf)));
		return BCME_ERROR;
	}

	retcode = dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, iov_len, TRUE, idx);
	if (retcode) {
		DHD_ERROR(("%s: failed, retcode = %d\n", __FUNCTION__, retcode));
	} else {
		DHD_TRACE(("%s: successfully removed: %d\n", __FUNCTION__, retcode));
	}

	return retcode;
}

int
dhd_ndo_unsolicited_na_filter_enable(dhd_pub_t *dhdp, int enable)
{
	char iovbuf[DHD_IOVAR_BUF_SIZE];
	int iov_len;
	int retcode;

	if (dhdp == NULL) {
		return BCME_ERROR;
	}

	iov_len = bcm_mkiovar("nd_unsolicited_na_filter", (char *)&enable, sizeof(int),
			iovbuf, sizeof(iovbuf));
	if (!iov_len) {
		DHD_ERROR(("%s: Insufficient iovar buffer size %zu \n",
			__FUNCTION__, sizeof(iovbuf)));
		return BCME_ERROR;
	}
	retcode = dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, iovbuf, iov_len, TRUE, 0);
	if (retcode)
		DHD_ERROR(("%s: failed to enable Unsolicited NA filter to %d, retcode = %d\n",
			__FUNCTION__, enable, retcode));
	else {
		DHD_TRACE(("%s: successfully enabled Unsolicited NA filter to %d\n",
			__FUNCTION__, enable));
	}

	return retcode;
}


/*
 * returns = TRUE if associated, FALSE if not associated
 */
bool dhd_is_associated(dhd_pub_t *dhd, uint8 ifidx, int *retval)
{
	char bssid[6], zbuf[6];
	int ret = -1;

	bzero(bssid, 6);
	bzero(zbuf, 6);

	ret  = dhd_wl_ioctl_cmd(dhd, WLC_GET_BSSID, (char *)&bssid,
		ETHER_ADDR_LEN, FALSE, ifidx);
	DHD_TRACE((" %s WLC_GET_BSSID ioctl res = %d\n", __FUNCTION__, ret));

	if (ret == BCME_NOTASSOCIATED) {
		DHD_TRACE(("%s: not associated! res:%d\n", __FUNCTION__, ret));
	}

	if (retval)
		*retval = ret;

	if (ret < 0)
		return FALSE;

	if ((memcmp(bssid, zbuf, ETHER_ADDR_LEN) == 0)) {
		DHD_TRACE(("%s: WLC_GET_BSSID ioctl returned zero bssid\n", __FUNCTION__));
		return FALSE;
	}
	return TRUE;
}

/* Function to estimate possible DTIM_SKIP value */
#if defined(BCMPCIE)
int
dhd_get_suspend_bcn_li_dtim(dhd_pub_t *dhd, int *dtim_period, int *bcn_interval)
{
	int bcn_li_dtim = 1; /* deafult no dtim skip setting */
	int ret = -1;
	int allowed_skip_dtim_cnt = 0;

	/* Check if associated */
	if (dhd_is_associated(dhd, 0, NULL) == FALSE) {
		DHD_TRACE(("%s NOT assoc ret %d\n", __FUNCTION__, ret));
		return bcn_li_dtim;
	}

	if (dtim_period == NULL || bcn_interval == NULL)
		return bcn_li_dtim;

	/* read associated AP beacon interval */
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_BCNPRD,
		bcn_interval, sizeof(*bcn_interval), FALSE, 0)) < 0) {
		DHD_ERROR(("%s get beacon failed code %d\n", __FUNCTION__, ret));
		return bcn_li_dtim;
	}

	/* read associated AP dtim setup */
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_DTIMPRD,
		dtim_period, sizeof(*dtim_period), FALSE, 0)) < 0) {
		DHD_ERROR(("%s failed code %d\n", __FUNCTION__, ret));
		return bcn_li_dtim;
	}

	/* if not assocated just return */
	if (*dtim_period == 0) {
		return bcn_li_dtim;
	}

	if (dhd->max_dtim_enable) {
		bcn_li_dtim =
			(int) (MAX_DTIM_ALLOWED_INTERVAL / ((*dtim_period) * (*bcn_interval)));
		if (bcn_li_dtim == 0) {
			bcn_li_dtim = 1;
		}
	} else {
		/* attemp to use platform defined dtim skip interval */
		bcn_li_dtim = dhd->suspend_bcn_li_dtim;

		/* check if sta listen interval fits into AP dtim */
		if (*dtim_period > CUSTOM_LISTEN_INTERVAL) {
			/* AP DTIM to big for our Listen Interval : no dtim skiping */
			bcn_li_dtim = NO_DTIM_SKIP;
			DHD_ERROR(("%s DTIM=%d > Listen=%d : too big ...\n",
				__FUNCTION__, *dtim_period, CUSTOM_LISTEN_INTERVAL));
			return bcn_li_dtim;
		}

		if (((*dtim_period) * (*bcn_interval) * bcn_li_dtim) > MAX_DTIM_ALLOWED_INTERVAL) {
			allowed_skip_dtim_cnt =
				MAX_DTIM_ALLOWED_INTERVAL / ((*dtim_period) * (*bcn_interval));
			bcn_li_dtim =
				(allowed_skip_dtim_cnt != 0) ? allowed_skip_dtim_cnt : NO_DTIM_SKIP;
		}

		if ((bcn_li_dtim * (*dtim_period)) > CUSTOM_LISTEN_INTERVAL) {
			/* Round up dtim_skip to fit into STAs Listen Interval */
			bcn_li_dtim = (int)(CUSTOM_LISTEN_INTERVAL / *dtim_period);
			DHD_TRACE(("%s agjust dtim_skip as %d\n", __FUNCTION__, bcn_li_dtim));
		}
	}

	DHD_ERROR(("%s beacon=%d bcn_li_dtim=%d DTIM=%d Listen=%d\n",
		__FUNCTION__, *bcn_interval, bcn_li_dtim, *dtim_period, CUSTOM_LISTEN_INTERVAL));

	return bcn_li_dtim;
}
#else /* OEM_ANDROID && BCMPCIE */
int
dhd_get_suspend_bcn_li_dtim(dhd_pub_t *dhd)
{
	int bcn_li_dtim = 1; /* deafult no dtim skip setting */
	int ret = -1;
	int dtim_period = 0;
	int ap_beacon = 0;
	int allowed_skip_dtim_cnt = 0;
	/* Check if associated */
	if (dhd_is_associated(dhd, 0, NULL) == FALSE) {
		DHD_TRACE(("%s NOT assoc ret %d\n", __FUNCTION__, ret));
		goto exit;
	}

	/* read associated AP beacon interval */
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_BCNPRD,
		&ap_beacon, sizeof(ap_beacon), FALSE, 0)) < 0) {
		DHD_ERROR(("%s get beacon failed code %d\n", __FUNCTION__, ret));
		goto exit;
	}

	/* read associated ap's dtim setup */
	if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_DTIMPRD,
		&dtim_period, sizeof(dtim_period), FALSE, 0)) < 0) {
		DHD_ERROR(("%s failed code %d\n", __FUNCTION__, ret));
		goto exit;
	}

	/* if not assocated just exit */
	if (dtim_period == 0) {
		goto exit;
	}

	if (dhd->max_dtim_enable) {
		bcn_li_dtim = (int) (MAX_DTIM_ALLOWED_INTERVAL / (ap_beacon * dtim_period));
		if (bcn_li_dtim == 0) {
			bcn_li_dtim = 1;
		}
		bcn_li_dtim = MAX(dhd->suspend_bcn_li_dtim, bcn_li_dtim);
	} else {
		/* attemp to use platform defined dtim skip interval */
		bcn_li_dtim = dhd->suspend_bcn_li_dtim;

		/* check if sta listen interval fits into AP dtim */
		if (dtim_period > CUSTOM_LISTEN_INTERVAL) {
			/* AP DTIM to big for our Listen Interval : no dtim skiping */
			bcn_li_dtim = NO_DTIM_SKIP;
			DHD_ERROR(("%s DTIM=%d > Listen=%d : too big ...\n",
				__FUNCTION__, dtim_period, CUSTOM_LISTEN_INTERVAL));
			goto exit;
		}

		if ((dtim_period * ap_beacon * bcn_li_dtim) > MAX_DTIM_ALLOWED_INTERVAL) {
			allowed_skip_dtim_cnt =
				MAX_DTIM_ALLOWED_INTERVAL / (dtim_period * ap_beacon);
			bcn_li_dtim =
				(allowed_skip_dtim_cnt != 0) ? allowed_skip_dtim_cnt : NO_DTIM_SKIP;
		}

		if ((bcn_li_dtim * dtim_period) > CUSTOM_LISTEN_INTERVAL) {
			/* Round up dtim_skip to fit into STAs Listen Interval */
			bcn_li_dtim = (int)(CUSTOM_LISTEN_INTERVAL / dtim_period);
			DHD_TRACE(("%s agjust dtim_skip as %d\n", __FUNCTION__, bcn_li_dtim));
		}
	}

	if (dhd->conf->suspend_bcn_li_dtim >= 0)
		bcn_li_dtim = dhd->conf->suspend_bcn_li_dtim;
	DHD_ERROR(("%s beacon=%d bcn_li_dtim=%d DTIM=%d Listen=%d\n",
		__FUNCTION__, ap_beacon, bcn_li_dtim, dtim_period, CUSTOM_LISTEN_INTERVAL));

exit:
	return bcn_li_dtim;
}
#endif /* OEM_ANDROID && BCMPCIE */

/* Check if the mode supports STA MODE */
bool dhd_support_sta_mode(dhd_pub_t *dhd)
{

#ifdef  WL_CFG80211
	if (!(dhd->op_mode & DHD_FLAG_STA_MODE))
		return FALSE;
	else
#endif /* WL_CFG80211 */
		return TRUE;
}

#if defined(KEEP_ALIVE)
int dhd_keep_alive_onoff(dhd_pub_t *dhd)
{
	char				buf[32] = {0};
	const char			*str;
	wl_mkeep_alive_pkt_t	mkeep_alive_pkt = {0, 0, 0, 0, 0, {0}};
	wl_mkeep_alive_pkt_t	*mkeep_alive_pktp;
	int					buf_len;
	int					str_len;
	int res					= -1;

	if (!dhd_support_sta_mode(dhd))
		return res;

	DHD_TRACE(("%s execution\n", __FUNCTION__));

	str = "mkeep_alive";
	str_len = strlen(str);
	strncpy(buf, str, sizeof(buf) - 1);
	buf[ sizeof(buf) - 1 ] = '\0';
	mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) (buf + str_len + 1);
	mkeep_alive_pkt.period_msec = dhd->conf->keep_alive_period;
	buf_len = str_len + 1;
	mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
	mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);
	/* Setup keep alive zero for null packet generation */
	mkeep_alive_pkt.keep_alive_id = 0;
	mkeep_alive_pkt.len_bytes = 0;
	buf_len += WL_MKEEP_ALIVE_FIXED_LEN;
	bzero(mkeep_alive_pkt.data, sizeof(mkeep_alive_pkt.data));
	/* Keep-alive attributes are set in local	variable (mkeep_alive_pkt), and
	 * then memcpy'ed into buffer (mkeep_alive_pktp) since there is no
	 * guarantee that the buffer is properly aligned.
	 */
	memcpy((char *)mkeep_alive_pktp, &mkeep_alive_pkt, WL_MKEEP_ALIVE_FIXED_LEN);

	res = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf, buf_len, TRUE, 0);

	return res;
}
#endif /* defined(KEEP_ALIVE) */

#define CSCAN_TLV_TYPE_SSID_IE	'S'
/*
 *  SSIDs list parsing from cscan tlv list
 */
int
wl_parse_ssid_list_tlv(char** list_str, wlc_ssid_ext_t* ssid, int max, int *bytes_left)
{
	char* str;
	int idx = 0;

	if ((list_str == NULL) || (*list_str == NULL) || (*bytes_left < 0)) {
		DHD_ERROR(("%s error paramters\n", __FUNCTION__));
		return -1;
	}
	str = *list_str;
	while (*bytes_left > 0) {

		if (str[0] != CSCAN_TLV_TYPE_SSID_IE) {
			*list_str = str;
			DHD_TRACE(("nssid=%d left_parse=%d %d\n", idx, *bytes_left, str[0]));
			return idx;
		}

		/* Get proper CSCAN_TLV_TYPE_SSID_IE */
		*bytes_left -= 1;
		str += 1;
		ssid[idx].rssi_thresh = 0;
		ssid[idx].flags = 0;
		if (str[0] == 0) {
			/* Broadcast SSID */
			ssid[idx].SSID_len = 0;
			memset((char*)ssid[idx].SSID, 0x0, DOT11_MAX_SSID_LEN);
			*bytes_left -= 1;
			str += 1;

			DHD_TRACE(("BROADCAST SCAN  left=%d\n", *bytes_left));
		}
		else if (str[0] <= DOT11_MAX_SSID_LEN) {
			/* Get proper SSID size */
			ssid[idx].SSID_len = str[0];
			*bytes_left -= 1;
			str += 1;

			/* Get SSID */
			if (ssid[idx].SSID_len > *bytes_left) {
				DHD_ERROR(("%s out of memory range len=%d but left=%d\n",
				__FUNCTION__, ssid[idx].SSID_len, *bytes_left));
				return -1;
			}

			memcpy((char*)ssid[idx].SSID, str, ssid[idx].SSID_len);

			*bytes_left -= ssid[idx].SSID_len;
			str += ssid[idx].SSID_len;
			ssid[idx].hidden = TRUE;

			DHD_TRACE(("%s :size=%d left=%d\n",
				(char*)ssid[idx].SSID, ssid[idx].SSID_len, *bytes_left));
		}
		else {
			DHD_ERROR(("### SSID size more that %d\n", str[0]));
			return -1;
		}

		if (idx++ >  max) {
			DHD_ERROR(("%s number of SSIDs more that %d\n", __FUNCTION__, idx));
			return -1;
		}
	}

	*list_str = str;
	return idx;
}

#if defined(WL_WIRELESS_EXT)
/* Android ComboSCAN support */

/*
 *  data parsing from ComboScan tlv list
*/
int
wl_iw_parse_data_tlv(char** list_str, void *dst, int dst_size, const char token,
                     int input_size, int *bytes_left)
{
	char* str;
	uint16 short_temp;
	uint32 int_temp;

	if ((list_str == NULL) || (*list_str == NULL) ||(bytes_left == NULL) || (*bytes_left < 0)) {
		DHD_ERROR(("%s error paramters\n", __FUNCTION__));
		return -1;
	}
	str = *list_str;

	/* Clean all dest bytes */
	memset(dst, 0, dst_size);
	while (*bytes_left > 0) {

		if (str[0] != token) {
			DHD_TRACE(("%s NOT Type=%d get=%d left_parse=%d \n",
				__FUNCTION__, token, str[0], *bytes_left));
			return -1;
		}

		*bytes_left -= 1;
		str += 1;

		if (input_size == 1) {
			memcpy(dst, str, input_size);
		}
		else if (input_size == 2) {
			memcpy(dst, (char *)htod16(memcpy(&short_temp, str, input_size)),
				input_size);
		}
		else if (input_size == 4) {
			memcpy(dst, (char *)htod32(memcpy(&int_temp, str, input_size)),
				input_size);
		}

		*bytes_left -= input_size;
		str += input_size;
		*list_str = str;
		return 1;
	}
	return 1;
}

/*
 *  channel list parsing from cscan tlv list
*/
int
wl_iw_parse_channel_list_tlv(char** list_str, uint16* channel_list,
                             int channel_num, int *bytes_left)
{
	char* str;
	int idx = 0;

	if ((list_str == NULL) || (*list_str == NULL) ||(bytes_left == NULL) || (*bytes_left < 0)) {
		DHD_ERROR(("%s error paramters\n", __FUNCTION__));
		return -1;
	}
	str = *list_str;

	while (*bytes_left > 0) {

		if (str[0] != CSCAN_TLV_TYPE_CHANNEL_IE) {
			*list_str = str;
			DHD_TRACE(("End channel=%d left_parse=%d %d\n", idx, *bytes_left, str[0]));
			return idx;
		}
		/* Get proper CSCAN_TLV_TYPE_CHANNEL_IE */
		*bytes_left -= 1;
		str += 1;

		if (str[0] == 0) {
			/* All channels */
			channel_list[idx] = 0x0;
		}
		else {
			channel_list[idx] = (uint16)str[0];
			DHD_TRACE(("%s channel=%d \n", __FUNCTION__,  channel_list[idx]));
		}
		*bytes_left -= 1;
		str += 1;

		if (idx++ > 255) {
			DHD_ERROR(("%s Too many channels \n", __FUNCTION__));
			return -1;
		}
	}

	*list_str = str;
	return idx;
}

/* Parse a comma-separated list from list_str into ssid array, starting
 * at index idx.  Max specifies size of the ssid array.  Parses ssids
 * and returns updated idx; if idx >= max not all fit, the excess have
 * not been copied.  Returns -1 on empty string, or on ssid too long.
 */
int
wl_iw_parse_ssid_list(char** list_str, wlc_ssid_t* ssid, int idx, int max)
{
	char* str, *ptr;

	if ((list_str == NULL) || (*list_str == NULL))
		return -1;

	for (str = *list_str; str != NULL; str = ptr) {

		/* check for next TAG */
		if (!strncmp(str, GET_CHANNEL, strlen(GET_CHANNEL))) {
			*list_str	 = str + strlen(GET_CHANNEL);
			return idx;
		}

		if ((ptr = strchr(str, ',')) != NULL) {
			*ptr++ = '\0';
		}

		if (strlen(str) > DOT11_MAX_SSID_LEN) {
			DHD_ERROR(("ssid <%s> exceeds %d\n", str, DOT11_MAX_SSID_LEN));
			return -1;
		}

		if (strlen(str) == 0)
			ssid[idx].SSID_len = 0;

		if (idx < max) {
			bzero(ssid[idx].SSID, sizeof(ssid[idx].SSID));
			strncpy((char*)ssid[idx].SSID, str, sizeof(ssid[idx].SSID) - 1);
			ssid[idx].SSID_len = strlen(str);
		}
		idx++;
	}
	return idx;
}

/*
 * Parse channel list from iwpriv CSCAN
 */
int
wl_iw_parse_channel_list(char** list_str, uint16* channel_list, int channel_num)
{
	int num;
	int val;
	char* str;
	char* endptr = NULL;

	if ((list_str == NULL)||(*list_str == NULL))
		return -1;

	str = *list_str;
	num = 0;
	while (strncmp(str, GET_NPROBE, strlen(GET_NPROBE))) {
		val = (int)strtoul(str, &endptr, 0);
		if (endptr == str) {
			printf("could not parse channel number starting at"
				" substring \"%s\" in list:\n%s\n",
				str, *list_str);
			return -1;
		}
		str = endptr + strspn(endptr, " ,");

		if (num == channel_num) {
			DHD_ERROR(("too many channels (more than %d) in channel list:\n%s\n",
				channel_num, *list_str));
			return -1;
		}

		channel_list[num++] = (uint16)val;
	}
	*list_str = str;
	return num;
}

#endif 

#if defined(TRAFFIC_MGMT_DWM)
static int traffic_mgmt_add_dwm_filter(dhd_pub_t *dhd,
	trf_mgmt_filter_list_t * trf_mgmt_filter_list, int len)
{
	int ret = 0;
	uint32              i;
	trf_mgmt_filter_t   *trf_mgmt_filter;
	uint8               dwm_tbl_entry;
	uint32              dscp = 0;
	uint16              dwm_filter_enabled = 0;


	/* Check parameter length is adequate */
	if (len < (OFFSETOF(trf_mgmt_filter_list_t, filter) +
		trf_mgmt_filter_list->num_filters * sizeof(trf_mgmt_filter_t))) {
		ret = BCME_BUFTOOSHORT;
		return ret;
	}

	bzero(&dhd->dhd_tm_dwm_tbl, sizeof(dhd_trf_mgmt_dwm_tbl_t));

	for (i = 0; i < trf_mgmt_filter_list->num_filters; i++) {
		trf_mgmt_filter = &trf_mgmt_filter_list->filter[i];

		dwm_filter_enabled = (trf_mgmt_filter->flags & TRF_FILTER_DWM);

		if (dwm_filter_enabled) {
			dscp = trf_mgmt_filter->dscp;
			if (dscp >= DHD_DWM_TBL_SIZE) {
				ret = BCME_BADARG;
			return ret;
			}
		}

		dhd->dhd_tm_dwm_tbl.dhd_dwm_enabled = 1;
		/* set WMM AC bits */
		dwm_tbl_entry = (uint8) trf_mgmt_filter->priority;
		DHD_TRF_MGMT_DWM_SET_FILTER(dwm_tbl_entry);

		/* set favored bits */
		if (trf_mgmt_filter->flags & TRF_FILTER_FAVORED)
			DHD_TRF_MGMT_DWM_SET_FAVORED(dwm_tbl_entry);

		dhd->dhd_tm_dwm_tbl.dhd_dwm_tbl[dscp] =  dwm_tbl_entry;
	}
	return ret;
}
#endif 

/* Given filename and download type,  returns a buffer pointer and length
 * for download to f/w. Type can be FW or NVRAM.
 *
 */
int dhd_get_download_buffer(dhd_pub_t	*dhd, char *file_path, download_type_t component,
	char ** buffer, int *length)

{
	int ret = BCME_ERROR;
	int len = 0;
	int file_len;
	void *image = NULL;
	uint8 *buf = NULL;

	/* Point to cache if available. */
#ifdef CACHE_FW_IMAGES
	if (component == FW) {
		if (dhd->cached_fw_length) {
			len = dhd->cached_fw_length;
			buf = dhd->cached_fw;
		}
	}
	else if (component == NVRAM) {
		if (dhd->cached_nvram_length) {
			len = dhd->cached_nvram_length;
			buf = dhd->cached_nvram;
		}
	}
	else if (component == CLM_BLOB) {
		if (dhd->cached_clm_length) {
			len = dhd->cached_clm_length;
			buf = dhd->cached_clm;
		}
	} else {
		return ret;
	}
#endif /* CACHE_FW_IMAGES */
	/* No Valid cache found on this call */
	if (!len) {
		file_len = *length;
		*length = 0;

		if (file_path) {
			image = dhd_os_open_image(file_path);
			if (image == NULL) {
				printf("%s: Open image file failed %s\n", __FUNCTION__, file_path);
				goto err;
			}
		}

		buf = MALLOCZ(dhd->osh, file_len);
		if (buf == NULL) {
			DHD_ERROR(("%s: Failed to allocate memory %d bytes\n",
				__FUNCTION__, file_len));
			goto err;
		}

		/* Download image */
#if defined(BCMEMBEDIMAGE) && defined(DHD_EFI)
		if (!image) {
			memcpy(buf, nvram_arr, sizeof(nvram_arr));
			len = sizeof(nvram_arr);
		} else {
			len = dhd_os_get_image_block((char *)buf, file_len, image);
			if ((len <= 0 || len > file_len)) {
				MFREE(dhd->osh, buf, file_len);
				goto err;
			}
		}
#else
		len = dhd_os_get_image_block((char *)buf, file_len, image);
		if ((len <= 0 || len > file_len)) {
			MFREE(dhd->osh, buf, file_len);
			goto err;
		}
#endif /* DHD_EFI */
	}

	ret = BCME_OK;
	*length = len;
	*buffer = (char *)buf;

	/* Cache if first call. */
#ifdef CACHE_FW_IMAGES
	if (component == FW) {
		if (!dhd->cached_fw_length) {
			dhd->cached_fw = buf;
			dhd->cached_fw_length = len;
		}
	}
	else if (component == NVRAM) {
		if (!dhd->cached_nvram_length) {
			dhd->cached_nvram = buf;
			dhd->cached_nvram_length = len;
		}
	}
	else if (component == CLM_BLOB) {
		if (!dhd->cached_clm_length) {
			 dhd->cached_clm = buf;
			 dhd->cached_clm_length = len;
		}
	}
#endif /* CACHE_FW_IMAGES */

err:
	if (image)
		dhd_os_close_image(image);

	return ret;
}

int
dhd_download_2_dongle(dhd_pub_t	*dhd, char *iovar, uint16 flag, uint16 dload_type,
	unsigned char *dload_buf, int len)
{
	struct wl_dload_data *dload_ptr = (struct wl_dload_data *)dload_buf;
	int err = 0;
	int dload_data_offset;
	static char iovar_buf[WLC_IOCTL_MEDLEN];
	int iovar_len;

	memset(iovar_buf, 0, sizeof(iovar_buf));

	dload_data_offset = OFFSETOF(wl_dload_data_t, data);
	dload_ptr->flag = (DLOAD_HANDLER_VER << DLOAD_FLAG_VER_SHIFT) | flag;
	dload_ptr->dload_type = dload_type;
	dload_ptr->len = htod32(len - dload_data_offset);
	dload_ptr->crc = 0;
	len = ROUNDUP(len, 8);

	iovar_len = bcm_mkiovar(iovar, (char *)dload_buf,
		(uint)len, iovar_buf, sizeof(iovar_buf));
	if (iovar_len == 0) {
		DHD_ERROR(("%s: insufficient buffer space passed to bcm_mkiovar for '%s' \n",
		           __FUNCTION__, iovar));
		return BCME_BUFTOOSHORT;
	}

	err = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, iovar_buf,
			iovar_len, IOV_SET, 0);

	return err;
}

int
dhd_download_blob(dhd_pub_t	*dhd, unsigned char *image,
		uint32 len, char *iovar)
{
	int chunk_len;
	int size2alloc;
	unsigned char *new_buf;
	int err = 0, data_offset;
	uint16 dl_flag = DL_BEGIN;

	data_offset = OFFSETOF(wl_dload_data_t, data);
	size2alloc = data_offset + MAX_CHUNK_LEN;
	size2alloc = ROUNDUP(size2alloc, 8);

	if ((new_buf = (unsigned char *)MALLOCZ(dhd->osh, size2alloc)) != NULL) {
		do {
			chunk_len = dhd_os_get_image_block((char *)(new_buf + data_offset),
				MAX_CHUNK_LEN, image);
			if (chunk_len < 0) {
				DHD_ERROR(("%s: dhd_os_get_image_block failed (%d)\n",
					__FUNCTION__, chunk_len));
				err = BCME_ERROR;
				goto exit;
			}

			if (len - chunk_len == 0)
				dl_flag |= DL_END;

			err = dhd_download_2_dongle(dhd, iovar, dl_flag, DL_TYPE_CLM,
				new_buf, data_offset + chunk_len);

			dl_flag &= ~DL_BEGIN;

			len = len - chunk_len;
		} while ((len > 0) && (err == 0));
	} else {
		err = BCME_NOMEM;
	}
exit:
	if (new_buf) {
		MFREE(dhd->osh, new_buf, size2alloc);
	}

	return err;
}

int
dhd_check_current_clm_data(dhd_pub_t *dhd)
{
	char iovbuf[WLC_IOCTL_SMLEN] = {0};
	wl_country_t *cspec;
	int err = BCME_OK;

	bcm_mkiovar("country", NULL, 0, iovbuf, sizeof(iovbuf));
	err = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0);
	if (err) {
		DHD_ERROR(("%s: country code get failed\n", __FUNCTION__));
		return err;
	}

	cspec = (wl_country_t *)iovbuf;
	if ((strncmp(cspec->ccode, WL_CCODE_NULL_COUNTRY, WLC_CNTRY_BUF_SZ)) == 0) {
		DHD_ERROR(("%s: ----- This FW is not included CLM data -----\n",
			__FUNCTION__));
		return FALSE;
	}
	DHD_ERROR(("%s: ----- This FW is included CLM data -----\n",
		__FUNCTION__));
	return TRUE;
}

int
dhd_apply_default_clm(dhd_pub_t *dhd, char *clm_path)
{
	char *clm_blob_path;
	int len;
	unsigned char *imgbuf = NULL;
	int err = BCME_OK;
	char iovbuf[WLC_IOCTL_SMLEN] = {0};
	int status = FALSE;

	if (clm_path && clm_path[0] != '\0') {
		if (strlen(clm_path) > MOD_PARAM_PATHLEN) {
			DHD_ERROR(("clm path exceeds max len\n"));
			return BCME_ERROR;
		}
		clm_blob_path = clm_path;
		DHD_TRACE(("clm path from module param:%s\n", clm_path));
	} else {
		clm_blob_path = CONFIG_BCMDHD_CLM_PATH;
	}

	/* If CLM blob file is found on the filesystem, download the file.
	 * After CLM file download or If the blob file is not present,
	 * validate the country code before proceeding with the initialization.
	 * If country code is not valid, fail the initialization.
	 */

	imgbuf = dhd_os_open_image((char *)clm_blob_path);
	if (imgbuf == NULL) {
		printf("%s: Ignore clm file %s\n", __FUNCTION__, clm_path);
#if defined(DHD_BLOB_EXISTENCE_CHECK)
		if (dhd->is_blob) {
			err = BCME_ERROR;
		} else {
			status = dhd_check_current_clm_data(dhd);
			if (status == TRUE) {
				err = BCME_OK;
			} else {
				err = BCME_ERROR;
			}
		}
#endif /* DHD_BLOB_EXISTENCE_CHECK */
		goto exit;
	}

	len = dhd_os_get_image_size(imgbuf);

	if ((len > 0) && (len < MAX_CLM_BUF_SIZE) && imgbuf) {
		status = dhd_check_current_clm_data(dhd);
		if (status == TRUE) {
#if defined(DHD_BLOB_EXISTENCE_CHECK)
			if (dhd->op_mode != DHD_FLAG_MFG_MODE) {
				if (dhd->is_blob) {
					err = BCME_ERROR;
				}
				goto exit;
			}
#else
			DHD_ERROR(("%s: CLM already exist in F/W, "
				"new CLM data will be added to the end of existing CLM data!\n",
				__FUNCTION__));
#endif /* DHD_BLOB_EXISTENCE_CHECK */
		}

		/* Found blob file. Download the file */
		DHD_ERROR(("clm file download from %s \n", clm_blob_path));
		err = dhd_download_blob(dhd, imgbuf, len, "clmload");
		if (err) {
			DHD_ERROR(("%s: CLM download failed err=%d\n", __FUNCTION__, err));
			/* Retrieve clmload_status and print */
			bcm_mkiovar("clmload_status", NULL, 0, iovbuf, sizeof(iovbuf));
			err = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0);
			if (err) {
				DHD_ERROR(("%s: clmload_status get failed err=%d \n",
					__FUNCTION__, err));
			} else {
				DHD_ERROR(("%s: clmload_status: %d \n",
					__FUNCTION__, *((int *)iovbuf)));
				if (*((int *)iovbuf) == CHIPID_MISMATCH) {
					DHD_ERROR(("Chip ID mismatch error \n"));
				}
			}
			err = BCME_ERROR;
			goto exit;
		} else {
			DHD_INFO(("%s: CLM download succeeded \n", __FUNCTION__));
		}
	} else {
		DHD_INFO(("Skipping the clm download. len:%d memblk:%p \n", len, imgbuf));
	}

	/* Verify country code */
	status = dhd_check_current_clm_data(dhd);

	if (status != TRUE) {
		/* Country code not initialized or CLM download not proper */
		DHD_ERROR(("country code not initialized\n"));
		err = BCME_ERROR;
	}
exit:

	if (imgbuf) {
		dhd_os_close_image(imgbuf);
	}

	return err;
}

void dhd_free_download_buffer(dhd_pub_t	*dhd, void *buffer, int length)
{
#ifdef CACHE_FW_IMAGES
	return;
#endif
	MFREE(dhd->osh, buffer, length);
}

#if defined(DHD_8021X_DUMP)
#define EAP_PRINT(str) \
	DHD_ERROR(("ETHER_TYPE_802_1X[%s] [%s]: " str "\n", \
	ifname, direction ? "TX" : "RX"));
#else
#define EAP_PRINT(str)
#endif /* DHD_8021X_DUMP */
/* Parse EAPOL 4 way handshake messages */
void
dhd_dump_eapol_4way_message(dhd_pub_t *dhd, char *ifname,
	char *dump_data, bool direction)
{
	unsigned char type;
	int pair, ack, mic, kerr, req, sec, install;
	unsigned short us_tmp;

	type = dump_data[15];
	if (type == 0) {
		if ((dump_data[22] == 1) && (dump_data[18] == 1)) {
			dhd->conf->eapol_status = EAPOL_STATUS_WPS_REQID;
			EAP_PRINT("EAP Packet, Request, Identity");
		} else if ((dump_data[22] == 1) && (dump_data[18] == 2)) {
			dhd->conf->eapol_status = EAPOL_STATUS_WPS_RSPID;
			EAP_PRINT("EAP Packet, Response, Identity");
		} else if (dump_data[22] == 254) {
			if (dump_data[30] == 1) {
				dhd->conf->eapol_status = EAPOL_STATUS_WPS_WSC_START;
				EAP_PRINT("EAP Packet, WSC Start");
			} else if (dump_data[30] == 4) {
				if (dump_data[41] == 4) {
					dhd->conf->eapol_status = EAPOL_STATUS_WPS_M1;
					EAP_PRINT("EAP Packet, WPS M1");
				} else if (dump_data[41] == 5) {
					dhd->conf->eapol_status = EAPOL_STATUS_WPS_M2;
					EAP_PRINT("EAP Packet, WPS M2");
				} else if (dump_data[41] == 7) {
					dhd->conf->eapol_status = EAPOL_STATUS_WPS_M3;
					EAP_PRINT("EAP Packet, WPS M3");
				} else if (dump_data[41] == 8) {
					dhd->conf->eapol_status = EAPOL_STATUS_WPS_M4;
					EAP_PRINT("EAP Packet, WPS M4");
				} else if (dump_data[41] == 9) {
					dhd->conf->eapol_status = EAPOL_STATUS_WPS_M5;
					EAP_PRINT("EAP Packet, WPS M5");
				} else if (dump_data[41] == 10) {
					dhd->conf->eapol_status = EAPOL_STATUS_WPS_M6;
					EAP_PRINT("EAP Packet, WPS M6");
				} else if (dump_data[41] == 11) {
					dhd->conf->eapol_status = EAPOL_STATUS_WPS_M7;
					EAP_PRINT("EAP Packet, WPS M7");
				} else if (dump_data[41] == 12) {
					dhd->conf->eapol_status = EAPOL_STATUS_WPS_M8;
					EAP_PRINT("EAP Packet, WPS M8");
				}
			} else if (dump_data[30] == 5) {
				dhd->conf->eapol_status = EAPOL_STATUS_WPS_DONE;
				EAP_PRINT("EAP Packet, WSC Done");
			}
		} else {
			DHD_ERROR(("ETHER_TYPE_802_1X[%s] [%s]: ver %d, type %d, replay %d\n",
				ifname, direction ? "TX" : "RX",
				dump_data[14], dump_data[15], dump_data[30]));
		}
	} else if (type == 3 && dump_data[18] == 2) {
		us_tmp = (dump_data[19] << 8) | dump_data[20];
		pair =  0 != (us_tmp & 0x08);
		ack = 0  != (us_tmp & 0x80);
		mic = 0  != (us_tmp & 0x100);
		kerr =  0 != (us_tmp & 0x400);
		req = 0  != (us_tmp & 0x800);
		sec = 0  != (us_tmp & 0x200);
		install  = 0 != (us_tmp & 0x40);

		if (!sec && !mic && ack && !install && pair && !kerr && !req) {
			dhd->conf->eapol_status = EAPOL_STATUS_WPA_M1;
			EAP_PRINT("EAPOL Packet, 4-way handshake, M1");
		} else if (pair && !install && !ack && mic && !sec && !kerr && !req) {
			dhd->conf->eapol_status = EAPOL_STATUS_WPA_M2;
			EAP_PRINT("EAPOL Packet, 4-way handshake, M2");
		} else if (pair && ack && mic && sec && !kerr && !req) {
			dhd->conf->eapol_status = EAPOL_STATUS_WPA_M3;
			EAP_PRINT("EAPOL Packet, 4-way handshake, M3");
		} else if (pair && !install && !ack && mic && sec && !req && !kerr) {
			dhd->conf->eapol_status = EAPOL_STATUS_WPA_M4;
			EAP_PRINT("EAPOL Packet, 4-way handshake, M4");
		} else {
			DHD_ERROR(("ETHER_TYPE_802_1X[%s] [%s]: ver %d, type %d, replay %d\n",
				ifname, direction ? "TX" : "RX",
				dump_data[14], dump_data[15], dump_data[30]));
		}
	} else {
		DHD_ERROR(("ETHER_TYPE_802_1X[%s] [%s]: ver %d, type %d, replay %d\n",
			ifname, direction ? "TX" : "RX",
			dump_data[14], dump_data[15], dump_data[30]));
	}
}

#ifdef REPORT_FATAL_TIMEOUTS
void init_dhd_timeouts(dhd_pub_t *pub)
{
	pub->timeout_info = MALLOC(pub->osh, sizeof(timeout_info_t));
	if (pub->timeout_info == NULL) {
		DHD_ERROR(("%s: Failed to alloc timeout_info\n", __FUNCTION__));
	} else {
		DHD_INFO(("Initializing dhd_timeouts\n"));
		pub->timeout_info->scan_timer_lock = dhd_os_spin_lock_init(pub->osh);
		pub->timeout_info->join_timer_lock = dhd_os_spin_lock_init(pub->osh);
		pub->timeout_info->bus_timer_lock = dhd_os_spin_lock_init(pub->osh);
		pub->timeout_info->cmd_timer_lock = dhd_os_spin_lock_init(pub->osh);
		pub->timeout_info->scan_timeout_val = SCAN_TIMEOUT_DEFAULT;
		pub->timeout_info->join_timeout_val = JOIN_TIMEOUT_DEFAULT;
		pub->timeout_info->cmd_timeout_val = CMD_TIMEOUT_DEFAULT;
		pub->timeout_info->bus_timeout_val = BUS_TIMEOUT_DEFAULT;
		pub->timeout_info->scan_timer_active = FALSE;
		pub->timeout_info->join_timer_active = FALSE;
		pub->timeout_info->cmd_timer_active = FALSE;
		pub->timeout_info->bus_timer_active = FALSE;
		pub->timeout_info->cmd_join_error = WLC_SSID_MASK;
		pub->timeout_info->cmd_request_id = 0;
	}
}

void
deinit_dhd_timeouts(dhd_pub_t *pub)
{
	/* stop the join, scan bus, cmd timers
	* as failing to do so may cause a kernel panic if
	* an rmmod is done
	*/
	if (!pub->timeout_info) {
		DHD_ERROR(("timeout_info pointer is NULL\n"));
		ASSERT(0);
		return;
	}
	if (dhd_stop_scan_timer(pub)) {
		DHD_ERROR(("dhd_stop_scan_timer failed\n"));
		ASSERT(0);
	}
	if (dhd_stop_bus_timer(pub)) {
		DHD_ERROR(("dhd_stop_bus_timer failed\n"));
		ASSERT(0);
	}
	if (dhd_stop_cmd_timer(pub)) {
		DHD_ERROR(("dhd_stop_cmd_timer failed\n"));
		ASSERT(0);
	}
	if (dhd_stop_join_timer(pub)) {
		DHD_ERROR(("dhd_stop_join_timer failed\n"));
		ASSERT(0);
	}

	dhd_os_spin_lock_deinit(pub->osh, pub->timeout_info->scan_timer_lock);
	dhd_os_spin_lock_deinit(pub->osh, pub->timeout_info->join_timer_lock);
	dhd_os_spin_lock_deinit(pub->osh, pub->timeout_info->bus_timer_lock);
	dhd_os_spin_lock_deinit(pub->osh, pub->timeout_info->cmd_timer_lock);
	MFREE(pub->osh, pub->timeout_info, sizeof(timeout_info_t));
	pub->timeout_info = NULL;
}

static void
dhd_cmd_timeout(void *ctx)
{
	dhd_pub_t *pub = (dhd_pub_t *)ctx;
	unsigned long flags;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ASSERT(0);
		return;
	}

	DHD_TIMER_LOCK(pub->timeout_info->cmd_timer_lock, flags);
	if (pub->timeout_info && pub->timeout_info->cmd_timer_active) {
		DHD_ERROR(("\nERROR COMMAND TIMEOUT TO:%d\n", pub->timeout_info->cmd_timeout_val));
		DHD_TIMER_UNLOCK(pub->timeout_info->cmd_timer_lock, flags);
#ifdef PCIE_OOB
		/* Assert device_wake so that UART_Rx is available */
		if (dhd_bus_set_device_wake(pub->bus, TRUE)) {
			DHD_ERROR(("%s: dhd_bus_set_device_wake() failed\n", __FUNCTION__));
			ASSERT(0);
		}
#endif /* PCIE_OOB */
		if (dhd_stop_cmd_timer(pub)) {
			DHD_ERROR(("%s: dhd_stop_cmd_timer() failed\n", __FUNCTION__));
			ASSERT(0);
		}
		dhd_wakeup_ioctl_event(pub, IOCTL_RETURN_ON_ERROR);
		if (!dhd_query_bus_erros(pub))
			dhd_send_trap_to_fw_for_timeout(pub, DHD_REASON_COMMAND_TO);
	} else {
		DHD_TIMER_UNLOCK(pub->timeout_info->cmd_timer_lock, flags);
	}
}

int
dhd_start_cmd_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	unsigned long flags = 0;
	uint32 cmd_to_ms;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ret = BCME_ERROR;
		ASSERT(0);
		goto exit_null;
	}
	DHD_TIMER_LOCK(pub->timeout_info->cmd_timer_lock, flags);
	cmd_to_ms = pub->timeout_info->cmd_timeout_val;

	if (pub->timeout_info->cmd_timeout_val == 0) {
		/* Disable Command timer timeout */
		DHD_INFO(("DHD: Command Timeout Disabled\n"));
		goto exit;
	}
	if (pub->timeout_info->cmd_timer_active) {
		DHD_ERROR(("%s:Timer already active\n", __FUNCTION__));
		ret = BCME_ERROR;
		ASSERT(0);
	} else {
		pub->timeout_info->cmd_timer = osl_timer_init(pub->osh,
			"cmd_timer", dhd_cmd_timeout, pub);
		osl_timer_update(pub->osh, pub->timeout_info->cmd_timer,
			cmd_to_ms, 0);
		pub->timeout_info->cmd_timer_active = TRUE;
	}
	if (ret == BCME_OK) {
		DHD_INFO(("%s Cmd Timer started\n", __FUNCTION__));
	}
exit:
	DHD_TIMER_UNLOCK(pub->timeout_info->cmd_timer_lock, flags);
exit_null:
	return ret;
}

int
dhd_stop_cmd_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	unsigned long flags = 0;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ret = BCME_ERROR;
		ASSERT(0);
		goto exit;
	}
	DHD_TIMER_LOCK(pub->timeout_info->cmd_timer_lock, flags);

	if (pub->timeout_info->cmd_timer_active) {
		osl_timer_del(pub->osh, pub->timeout_info->cmd_timer);
		pub->timeout_info->cmd_timer_active = FALSE;
	}
	else {
		DHD_INFO(("DHD: CMD timer is not active\n"));
	}
	if (ret == BCME_OK) {
		DHD_INFO(("%s Cmd Timer Stopped\n", __FUNCTION__));
	}
	DHD_TIMER_UNLOCK(pub->timeout_info->cmd_timer_lock, flags);
exit:
	return ret;
}

static int
__dhd_stop_join_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ASSERT(0);
		return BCME_ERROR;
	}

	if (pub->timeout_info->join_timer_active) {
		osl_timer_del(pub->osh, pub->timeout_info->join_timer);
		pub->timeout_info->join_timer_active = FALSE;
	} else {
		DHD_INFO(("DHD: JOIN timer is not active\n"));
	}
	if (ret == BCME_OK) {
		DHD_INFO(("%s: Join Timer Stopped\n", __FUNCTION__));
	}
	return ret;
}

static void
dhd_join_timeout(void *ctx)
{
	dhd_pub_t *pub = (dhd_pub_t *)ctx;
	unsigned long flags;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ASSERT(0);
		return;
	}

	DHD_TIMER_LOCK(pub->timeout_info->join_timer_lock, flags);
	if (pub->timeout_info->join_timer_active) {
		DHD_TIMER_UNLOCK(pub->timeout_info->join_timer_lock, flags);
		if (dhd_stop_join_timer(pub)) {
			DHD_ERROR(("%s: dhd_stop_join_timer() failed\n", __FUNCTION__));
			ASSERT(0);
		}
		if (pub->timeout_info->cmd_join_error) {
			DHD_ERROR(("\nERROR JOIN TIMEOUT TO:%d:0x%x\n",
				pub->timeout_info->join_timeout_val,
				pub->timeout_info->cmd_join_error));
#ifdef DHD_FW_COREDUMP
				/* collect core dump and crash */
				pub->memdump_enabled = DUMP_MEMFILE_BUGON;
				pub->memdump_type = DUMP_TYPE_JOIN_TIMEOUT;
				dhd_bus_mem_dump(pub);
#endif /* DHD_FW_COREDUMP */

		}
	} else {
		DHD_TIMER_UNLOCK(pub->timeout_info->join_timer_lock, flags);
	}
}

int
dhd_start_join_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	unsigned long flags = 0;
	uint32 join_to_ms;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ret = BCME_ERROR;
		ASSERT(0);
		goto exit;
	}

	join_to_ms = pub->timeout_info->join_timeout_val;
	DHD_TIMER_LOCK(pub->timeout_info->join_timer_lock, flags);
	if (pub->timeout_info->join_timer_active) {
		DHD_ERROR(("%s:Stoping active timer\n", __FUNCTION__));
		__dhd_stop_join_timer(pub);
	}
	if (pub->timeout_info->join_timeout_val == 0) {
		/* Disable Join timer timeout */
		DHD_INFO(("DHD: Join Timeout Disabled\n"));
	} else {
		pub->timeout_info->join_timer = osl_timer_init(pub->osh,
			"join_timer", dhd_join_timeout, pub);
		osl_timer_update(pub->osh, pub->timeout_info->join_timer, join_to_ms, 0);
		pub->timeout_info->join_timer_active = TRUE;
		pub->timeout_info->cmd_join_error |= WLC_SSID_MASK;
	}
	if (ret == BCME_OK) {
		DHD_INFO(("%s:Join Timer started 0x%x\n", __FUNCTION__,
			pub->timeout_info->cmd_join_error));
	}
	DHD_TIMER_UNLOCK(pub->timeout_info->join_timer_lock, flags);
exit:
	return ret;
}

int
dhd_stop_join_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	unsigned long flags;

	DHD_TIMER_LOCK(pub->timeout_info->join_timer_lock, flags);
	ret = __dhd_stop_join_timer(pub);
	DHD_TIMER_UNLOCK(pub->timeout_info->join_timer_lock, flags);
	return ret;
}

static void
dhd_scan_timeout(void *ctx)
{
	dhd_pub_t *pub = (dhd_pub_t *)ctx;
	unsigned long flags;

	 if (pub->timeout_info == NULL) {
		DHD_ERROR(("timeout_info pointer is NULL\n"));
		ASSERT(0);
		return;
	 }

	DHD_TIMER_LOCK(pub->timeout_info->scan_timer_lock, flags);
	if (pub->timeout_info && pub->timeout_info->scan_timer_active) {
		DHD_ERROR(("\nERROR SCAN TIMEOUT TO:%d\n", pub->timeout_info->scan_timeout_val));
		DHD_TIMER_UNLOCK(pub->timeout_info->scan_timer_lock, flags);
		dhd_stop_scan_timer(pub);
		if (!dhd_query_bus_erros(pub))
			dhd_send_trap_to_fw_for_timeout(pub, DHD_REASON_SCAN_TO);
	} else {
		DHD_TIMER_UNLOCK(pub->timeout_info->scan_timer_lock, flags);
	}
}

int
dhd_start_scan_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	unsigned long flags = 0;
	uint32 scan_to_ms;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ret = BCME_ERROR;
		ASSERT(0);
		goto exit_null;
	}
	DHD_TIMER_LOCK(pub->timeout_info->scan_timer_lock, flags);
	scan_to_ms = pub->timeout_info->scan_timeout_val;

	if (pub->timeout_info->scan_timer_active) {
		/* NOTE : New scan timeout value will be effective
		 * only once current scan is completed.
		 */
		DHD_ERROR(("%s:Timer already active\n", __FUNCTION__));
		ret = BCME_ERROR;
		goto exit;
	}

	if (pub->timeout_info->scan_timeout_val == 0) {
		/* Disable Scan timer timeout */
		DHD_INFO(("DHD: Scan Timeout Disabled\n"));
	} else {
		pub->timeout_info->scan_timer = osl_timer_init(pub->osh, "scan_timer",
			dhd_scan_timeout, pub);
		pub->timeout_info->scan_timer_active = TRUE;
		osl_timer_update(pub->osh, pub->timeout_info->scan_timer, scan_to_ms, 0);
	}
	if (ret == BCME_OK) {
		DHD_INFO(("%s Scan Timer started\n", __FUNCTION__));
	}
exit:
	DHD_TIMER_UNLOCK(pub->timeout_info->scan_timer_lock, flags);
exit_null:
	return ret;
}

int
dhd_stop_scan_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	unsigned long flags = 0;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ret = BCME_ERROR;
		ASSERT(0);
		goto exit;
	}
	DHD_TIMER_LOCK(pub->timeout_info->scan_timer_lock, flags);

	if (pub->timeout_info->scan_timer_active) {
		osl_timer_del(pub->osh, pub->timeout_info->scan_timer);
		pub->timeout_info->scan_timer_active = FALSE;
	}
	else {
		DHD_INFO(("DHD: SCAN timer is not active\n"));
	}

	if (ret == BCME_OK) {
		DHD_INFO(("%s Scan Timer Stopped\n", __FUNCTION__));
	}
	DHD_TIMER_UNLOCK(pub->timeout_info->scan_timer_lock, flags);
exit:
	return ret;
}

static void
dhd_bus_timeout(void *ctx)
{
	dhd_pub_t *pub = (dhd_pub_t *)ctx;
	unsigned long flags;

	if (pub->timeout_info == NULL) {
		DHD_ERROR(("timeout_info pointer is NULL\n"));
		ASSERT(0);
		return;
	}

	DHD_TIMER_LOCK(pub->timeout_info->bus_timer_lock, flags);
	if (pub->timeout_info->bus_timer_active) {
		DHD_ERROR(("\nERROR BUS TIMEOUT TO:%d\n", pub->timeout_info->bus_timeout_val));
		DHD_TIMER_UNLOCK(pub->timeout_info->bus_timer_lock, flags);
#ifdef PCIE_OOB
		/* Assert device_wake so that UART_Rx is available */
		if (dhd_bus_set_device_wake(pub->bus, TRUE)) {
			DHD_ERROR(("%s: dhd_bus_set_device_wake() failed\n", __FUNCTION__));
			ASSERT(0);
		}
#endif /* PCIE_OOB */
		if (dhd_stop_bus_timer(pub)) {
			DHD_ERROR(("%s: dhd_stop_bus_timer() failed\n", __FUNCTION__));
			ASSERT(0);
		}
		if (!dhd_query_bus_erros(pub))
			dhd_send_trap_to_fw_for_timeout(pub, DHD_REASON_OQS_TO);
	} else {
		DHD_TIMER_UNLOCK(pub->timeout_info->bus_timer_lock, flags);
	}
}

int
dhd_start_bus_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	unsigned long flags = 0;
	uint32 bus_to_ms;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ret = BCME_ERROR;
		ASSERT(0);
		goto exit_null;
	}
	DHD_TIMER_LOCK(pub->timeout_info->bus_timer_lock, flags);
	bus_to_ms = pub->timeout_info->bus_timeout_val;

	if (pub->timeout_info->bus_timeout_val == 0) {
		/* Disable Bus timer timeout */
		DHD_INFO(("DHD: Bus Timeout Disabled\n"));
		goto exit;
	}
	if (pub->timeout_info->bus_timer_active) {
		DHD_ERROR(("%s:Timer already active\n", __FUNCTION__));
		ret = BCME_ERROR;
		ASSERT(0);
	} else {
		pub->timeout_info->bus_timer = osl_timer_init(pub->osh,
			"bus_timer", dhd_bus_timeout, pub);
		pub->timeout_info->bus_timer_active = TRUE;
		osl_timer_update(pub->osh, pub->timeout_info->bus_timer, bus_to_ms, 0);
	}
	if (ret == BCME_OK) {
		DHD_INFO(("%s: BUS Timer started\n", __FUNCTION__));
	}
exit:
	DHD_TIMER_UNLOCK(pub->timeout_info->bus_timer_lock, flags);
exit_null:
	return ret;
}

int
dhd_stop_bus_timer(dhd_pub_t *pub)
{
	int ret = BCME_OK;
	unsigned long flags = 0;

	if (!pub->timeout_info) {
		DHD_ERROR(("DHD: timeout_info NULL\n"));
		ret = BCME_ERROR;
		ASSERT(0);
		goto exit;
	}
	DHD_TIMER_LOCK(pub->timeout_info->bus_timer_lock, flags);

	if (pub->timeout_info->bus_timer_active) {
		osl_timer_del(pub->osh, pub->timeout_info->bus_timer);
		pub->timeout_info->bus_timer_active = FALSE;
	}
	else {
		DHD_INFO(("DHD: BUS timer is not active\n"));
	}
	if (ret == BCME_OK) {
		DHD_INFO(("%s: Bus Timer Stopped\n", __FUNCTION__));
	}
	DHD_TIMER_UNLOCK(pub->timeout_info->bus_timer_lock, flags);
exit:
	return ret;
}

int
dhd_set_request_id(dhd_pub_t *pub, uint16 id, uint32 cmd)
{
	DHD_INFO(("%s: id:%d\n", __FUNCTION__, id));
	if (pub->timeout_info) {
		pub->timeout_info->cmd_request_id = id;
		pub->timeout_info->cmd = cmd;
		return BCME_OK;
	} else {
		return BCME_ERROR;
	}
}

uint16
dhd_get_request_id(dhd_pub_t *pub)
{
	if (pub->timeout_info) {
		return (pub->timeout_info->cmd_request_id);
	} else {
		return 0;
	}
}

void
dhd_set_join_error(dhd_pub_t *pub, uint32 mask)
{
	DHD_INFO(("Setting join Error %d\n", mask));
	if (pub->timeout_info) {
		pub->timeout_info->cmd_join_error |= mask;
	}
}

void
dhd_clear_join_error(dhd_pub_t *pub, uint32 mask)
{
	DHD_INFO(("clear join Error %d\n", mask));
	if (pub->timeout_info) {
		pub->timeout_info->cmd_join_error &= ~mask;
	}
}

void
dhd_get_scan_to_val(dhd_pub_t *pub, uint32 *to_val)
{
	if (pub->timeout_info) {
		*to_val = pub->timeout_info->scan_timeout_val;
	} else {
		*to_val = 0;
	}
}

void
dhd_set_scan_to_val(dhd_pub_t *pub, uint32 to_val)
{
	if (pub->timeout_info) {
		DHD_INFO(("Setting TO val:%d\n", to_val));
		pub->timeout_info->scan_timeout_val = to_val;
	}
}

void
dhd_get_join_to_val(dhd_pub_t *pub, uint32 *to_val)
{
	if (pub->timeout_info) {
		*to_val = pub->timeout_info->join_timeout_val;
	} else {
		*to_val = 0;
	}
}

void
dhd_set_join_to_val(dhd_pub_t *pub, uint32 to_val)
{
	if (pub->timeout_info) {
		DHD_INFO(("Setting TO val:%d\n", to_val));
		pub->timeout_info->join_timeout_val = to_val;
	}
}

void
dhd_get_cmd_to_val(dhd_pub_t *pub, uint32 *to_val)
{
	if (pub->timeout_info) {
		*to_val = pub->timeout_info->cmd_timeout_val;
	} else {
		*to_val = 0;
	}
}

void
dhd_set_cmd_to_val(dhd_pub_t *pub, uint32 to_val)
{
	if (pub->timeout_info) {
		DHD_INFO(("Setting TO val:%d\n", to_val));
		pub->timeout_info->cmd_timeout_val = to_val;
	}
}

void
dhd_get_bus_to_val(dhd_pub_t *pub, uint32 *to_val)
{
	if (pub->timeout_info) {
		*to_val = pub->timeout_info->bus_timeout_val;
	} else {
		*to_val = 0;
	}
}

void
dhd_set_bus_to_val(dhd_pub_t *pub, uint32 to_val)
{
	if (pub->timeout_info) {
		DHD_INFO(("Setting TO val:%d\n", to_val));
		pub->timeout_info->bus_timeout_val = to_val;
	}
}
#endif /* REPORT_FATAL_TIMEOUTS */

#ifdef SHOW_LOGTRACE
int
dhd_parse_logstrs_file(osl_t *osh, char *raw_fmts, int logstrs_size,
		dhd_event_log_t *event_log)
{
	logstr_header_t *hdr = NULL;
	uint32 *lognums = NULL;
	char *logstrs = NULL;
	int ram_index = 0;
	char **fmts;
	int num_fmts = 0;
	int32 i = 0;

	/* Remember header from the logstrs.bin file */
	hdr = (logstr_header_t *) (raw_fmts + logstrs_size -
		sizeof(logstr_header_t));

	if (hdr->log_magic == LOGSTRS_MAGIC) {
		/*
		* logstrs.bin start with header.
		*/
		num_fmts =	hdr->rom_logstrs_offset / sizeof(uint32);
		ram_index = (hdr->ram_lognums_offset -
			hdr->rom_lognums_offset) / sizeof(uint32);
		lognums = (uint32 *) &raw_fmts[hdr->rom_lognums_offset];
		logstrs = (char *)	 &raw_fmts[hdr->rom_logstrs_offset];
	} else {
		/*
		 * Legacy logstrs.bin format without header.
		 */
		num_fmts = *((uint32 *) (raw_fmts)) / sizeof(uint32);
		if (num_fmts == 0) {
			/* Legacy ROM/RAM logstrs.bin format:
			  *  - ROM 'lognums' section
			  *   - RAM 'lognums' section
			  *   - ROM 'logstrs' section.
			  *   - RAM 'logstrs' section.
			  *
			  * 'lognums' is an array of indexes for the strings in the
			  * 'logstrs' section. The first uint32 is 0 (index of first
			  * string in ROM 'logstrs' section).
			  *
			  * The 4324b5 is the only ROM that uses this legacy format. Use the
			  * fixed number of ROM fmtnums to find the start of the RAM
			  * 'lognums' section. Use the fixed first ROM string ("Con\n") to
			  * find the ROM 'logstrs' section.
			  */
			#define NUM_4324B5_ROM_FMTS	186
			#define FIRST_4324B5_ROM_LOGSTR "Con\n"
			ram_index = NUM_4324B5_ROM_FMTS;
			lognums = (uint32 *) raw_fmts;
			num_fmts =	ram_index;
			logstrs = (char *) &raw_fmts[num_fmts << 2];
			while (strncmp(FIRST_4324B5_ROM_LOGSTR, logstrs, 4)) {
				num_fmts++;
				logstrs = (char *) &raw_fmts[num_fmts << 2];
			}
		} else {
				/* Legacy RAM-only logstrs.bin format:
				 *	  - RAM 'lognums' section
				 *	  - RAM 'logstrs' section.
				 *
				 * 'lognums' is an array of indexes for the strings in the
				 * 'logstrs' section. The first uint32 is an index to the
				 * start of 'logstrs'. Therefore, if this index is divided
				 * by 'sizeof(uint32)' it provides the number of logstr
				 *	entries.
				 */
				ram_index = 0;
				lognums = (uint32 *) raw_fmts;
				logstrs = (char *)	&raw_fmts[num_fmts << 2];
			}
	}
	fmts = MALLOC(osh, num_fmts  * sizeof(char *));
	if (fmts == NULL) {
		DHD_ERROR(("%s: Failed to allocate fmts memory\n", __FUNCTION__));
		return BCME_ERROR;
	}
	event_log->fmts_size = num_fmts  * sizeof(char *);

	for (i = 0; i < num_fmts; i++) {
		/* ROM lognums index into logstrs using 'rom_logstrs_offset' as a base
		* (they are 0-indexed relative to 'rom_logstrs_offset').
		*
		* RAM lognums are already indexed to point to the correct RAM logstrs (they
		* are 0-indexed relative to the start of the logstrs.bin file).
		*/
		if (i == ram_index) {
			logstrs = raw_fmts;
		}
		fmts[i] = &logstrs[lognums[i]];
	}
	event_log->fmts = fmts;
	event_log->raw_fmts_size = logstrs_size;
	event_log->raw_fmts = raw_fmts;
	event_log->num_fmts = num_fmts;

	return BCME_OK;
}

int dhd_parse_map_file(osl_t *osh, void *file, uint32 *ramstart, uint32 *rodata_start,
		uint32 *rodata_end)
{
	char *raw_fmts =  NULL;
	uint32 read_size = READ_NUM_BYTES;
	int error = 0;
	char * cptr = NULL;
	char c;
	uint8 count = 0;

	*ramstart = 0;
	*rodata_start = 0;
	*rodata_end = 0;

	/* Allocate 1 byte more than read_size to terminate it with NULL */
	raw_fmts = MALLOC(osh, read_size + 1);
	if (raw_fmts == NULL) {
		DHD_ERROR(("%s: Failed to allocate raw_fmts memory \n", __FUNCTION__));
		goto fail;
	}

	/* read ram start, rodata_start and rodata_end values from map  file */
	while (count != ALL_MAP_VAL)
	{
		error = dhd_os_read_file(file, raw_fmts, read_size);
		if (error < 0) {
			DHD_ERROR(("%s: map file read failed err:%d \n", __FUNCTION__,
					error));
			goto fail;
		}

		/* End raw_fmts with NULL as strstr expects NULL terminated strings */
		raw_fmts[read_size] = '\0';

		/* Get ramstart address */
		if ((cptr = strstr(raw_fmts, ramstart_str))) {
			cptr = cptr - BYTES_AHEAD_NUM;
			sscanf(cptr, "%x %c text_start", ramstart, &c);
			count |= RAMSTART_BIT;
		}

		/* Get ram rodata start address */
		if ((cptr = strstr(raw_fmts, rodata_start_str))) {
			cptr = cptr - BYTES_AHEAD_NUM;
			sscanf(cptr, "%x %c rodata_start", rodata_start, &c);
			count |= RDSTART_BIT;
		}

		/* Get ram rodata end address */
		if ((cptr = strstr(raw_fmts, rodata_end_str))) {
			cptr = cptr - BYTES_AHEAD_NUM;
			sscanf(cptr, "%x %c rodata_end", rodata_end, &c);
			count |= RDEND_BIT;
		}

		if (error < (int)read_size) {
			/*
			* since we reset file pos back to earlier pos by
			* GO_BACK_FILE_POS_NUM_BYTES bytes we won't reach EOF.
			* The reason for this is if string is spreaded across
			* bytes, the read function should not miss it.
			* So if ret value is less than read_size, reached EOF don't read further
			*/
			break;
		}
		memset(raw_fmts, 0, read_size);
		/*
		* go back to predefined NUM of bytes so that we won't miss
		* the string and  addr even if it comes as splited in next read.
		*/
		dhd_os_seek_file(file, -GO_BACK_FILE_POS_NUM_BYTES);
	}


fail:
	if (raw_fmts) {
		MFREE(osh, raw_fmts, read_size + 1);
		raw_fmts = NULL;
	}
	if (count == ALL_MAP_VAL)
		return BCME_OK;
	else {
		DHD_ERROR(("%s: readmap error 0X%x \n", __FUNCTION__,
				count));
		return BCME_ERROR;
	}

}

#ifdef PCIE_FULL_DONGLE
int
dhd_event_logtrace_infobuf_pkt_process(dhd_pub_t *dhdp, void *pktbuf,
		dhd_event_log_t *event_data)
{
	uint32 infobuf_version;
	info_buf_payload_hdr_t *payload_hdr_ptr;
	uint16 payload_hdr_type;
	uint16 payload_hdr_length;

	DHD_TRACE(("%s:Enter\n", __FUNCTION__));

	if (PKTLEN(dhdp->osh, pktbuf) < sizeof(uint32)) {
		DHD_ERROR(("%s: infobuf too small for version field\n",
			__FUNCTION__));
		goto exit;
	}
	infobuf_version = *((uint32 *)PKTDATA(dhdp->osh, pktbuf));
	PKTPULL(dhdp->osh, pktbuf, sizeof(uint32));
	if (infobuf_version != PCIE_INFOBUF_V1) {
		DHD_ERROR(("%s: infobuf version %d is not PCIE_INFOBUF_V1\n",
			__FUNCTION__, infobuf_version));
		goto exit;
	}

	/* Version 1 infobuf has a single type/length (and then value) field */
	if (PKTLEN(dhdp->osh, pktbuf) < sizeof(info_buf_payload_hdr_t)) {
		DHD_ERROR(("%s: infobuf too small for v1 type/length  fields\n",
			__FUNCTION__));
		goto exit;
	}
	/* Process/parse the common info payload header (type/length) */
	payload_hdr_ptr = (info_buf_payload_hdr_t *)PKTDATA(dhdp->osh, pktbuf);
	payload_hdr_type = ltoh16(payload_hdr_ptr->type);
	payload_hdr_length = ltoh16(payload_hdr_ptr->length);
	if (payload_hdr_type != PCIE_INFOBUF_V1_TYPE_LOGTRACE) {
		DHD_ERROR(("%s: payload_hdr_type %d is not V1_TYPE_LOGTRACE\n",
			__FUNCTION__, payload_hdr_type));
		goto exit;
	}
	PKTPULL(dhdp->osh, pktbuf, sizeof(info_buf_payload_hdr_t));

	/* Validate that the specified length isn't bigger than the
	 * provided data.
	 */
	if (payload_hdr_length > PKTLEN(dhdp->osh, pktbuf)) {
		DHD_ERROR(("%s: infobuf logtrace length is bigger"
			" than actual buffer data\n", __FUNCTION__));
		goto exit;
	}
	dhd_dbg_trace_evnt_handler(dhdp, PKTDATA(dhdp->osh, pktbuf),
		event_data, payload_hdr_length);

	return BCME_OK;

exit:
	return BCME_ERROR;
}
#endif /* PCIE_FULL_DONGLE */
#endif /* SHOW_LOGTRACE */

#if defined(WLTDLS) && defined(PCIE_FULL_DONGLE)

/* To handle the TDLS event in the dhd_common.c
 */
int dhd_tdls_event_handler(dhd_pub_t *dhd_pub, wl_event_msg_t *event)
{
	int ret = BCME_OK;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	ret = dhd_tdls_update_peer_info(dhd_pub, event);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	return ret;
}

int dhd_free_tdls_peer_list(dhd_pub_t *dhd_pub)
{
	tdls_peer_node_t *cur = NULL, *prev = NULL;
	if (!dhd_pub)
		return BCME_ERROR;
	cur = dhd_pub->peer_tbl.node;

	if ((dhd_pub->peer_tbl.node == NULL) && !dhd_pub->peer_tbl.tdls_peer_count)
		return BCME_ERROR;

	while (cur != NULL) {
		prev = cur;
		cur = cur->next;
		MFREE(dhd_pub->osh, prev, sizeof(tdls_peer_node_t));
	}
	dhd_pub->peer_tbl.tdls_peer_count = 0;
	dhd_pub->peer_tbl.node = NULL;
	return BCME_OK;
}
#endif	/* #if defined(WLTDLS) && defined(PCIE_FULL_DONGLE) */

#ifdef DUMP_IOCTL_IOV_LIST
void
dhd_iov_li_append(dhd_pub_t *dhd, dll_t *list_head, dll_t *node)
{
	dll_t *item;
	dhd_iov_li_t *iov_li;
	dhd->dump_iovlist_len++;

	if (dhd->dump_iovlist_len == IOV_LIST_MAX_LEN+1) {
		item = dll_head_p(list_head);
		iov_li = (dhd_iov_li_t *)CONTAINEROF(item, dhd_iov_li_t, list);
		dll_delete(item);
		MFREE(dhd->osh, iov_li, sizeof(*iov_li));
		dhd->dump_iovlist_len--;
	}
	dll_append(list_head, node);
}

void
dhd_iov_li_print(dll_t *list_head)
{
	dhd_iov_li_t *iov_li;
	dll_t *item, *next;
	uint8 index = 0;
	for (item = dll_head_p(list_head); !dll_end(list_head, item); item = next) {
		next = dll_next_p(item);
		iov_li = (dhd_iov_li_t *)CONTAINEROF(item, dhd_iov_li_t, list);
		index++;
		DHD_ERROR(("%d:cmd_name = %s, cmd = %d.\n", index, iov_li->buff, iov_li->cmd));
	}
}

void
dhd_iov_li_delete(dhd_pub_t *dhd, dll_t *list_head)
{
	dll_t *item;
	dhd_iov_li_t *iov_li;
	while (!(dll_empty(list_head))) {
		item = dll_head_p(list_head);
		iov_li = (dhd_iov_li_t *)CONTAINEROF(item, dhd_iov_li_t, list);
		dll_delete(item);
		MFREE(dhd->osh, iov_li, sizeof(*iov_li));
	}
}
#endif /* DUMP_IOCTL_IOV_LIST */
