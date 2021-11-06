/**
 * @file Broadcom Dongle Host Driver (DHD), time sync protocol handler
 *
 * timesync mesasages are exchanged between the host and device to synchronize the source time
 * for ingress and egress packets.
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
 * $Id$:
 */

#include <typedefs.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhd_timesync.h>
#include <bcmpcie.h>
#include <bcmmsgbuf.h>

extern void dhd_msgbuf_delay_post_ts_bufs(dhd_pub_t *dhd);

#define MAX_FW_CLKINFO_TYPES		8
#define MAX_SIZE_FW_CLKINFO_TYPE	(MAX_FW_CLKINFO_TYPES * sizeof(ts_fw_clock_info_t))

#define MAX_FW_TS_LOG_SAMPLES		64

#define BCMMSGBUF_HOST_TS_BADTAG	0xF0

#define DHD_DEFAULT_TIMESYNC_TIMER_VALUE 20	/* ms */
#define DHD_DEFAULT_TIMESYNC_TIMER_VALUE_MAX 9000	/* ms */

#define MAX_TS_LOG_SAMPLES_DATA		128
#define TS_NODROP_CONFIG_TO		1
#define TS_DROP_CONFIG_TO		5

typedef struct clksrc_ts_log {
	uchar name[4];
	uint32 inuse;
	ts_timestamp_srcid_t log[MAX_FW_TS_LOG_SAMPLES];
} clksrc_ts_log_t;

typedef struct clk_ts_log {
	uint32	clk_ts_inited;
	uint32 cur_idx;
	uint32	seqnum[MAX_FW_TS_LOG_SAMPLES];
	clksrc_ts_log_t ts_log[MAX_CLKSRC_ID+1];
} clk_ts_log_t;

typedef struct dhd_ts_xt_id {
	uint16	host_timestamping_config;
	uint16	host_clock_selection;
	uint16	host_clk_info;
	uint16	d2h_clk_correction;
} dhd_ts_xt_id_t;

typedef struct dhd_ts_log_ts_item {
	uint16	flowid; /* interface, Flow ID */
	uint8	intf; /* interface */
	uint8	rsvd;
	uint32	ts_low; /* time stamp values */
	uint32	ts_high; /* time stamp values */
	uint32	proto;
	uint32	t1;
	uint32	t2;
} dhd_ts_log_ts_item_t;

typedef struct dhd_ts_log_ts {
	uint32	max_idx;
	uint32	cur_idx;
	dhd_ts_log_ts_item_t	ts_log[MAX_TS_LOG_SAMPLES_DATA];
} dhd_ts_log_ts_t;

#define MAX_BUF_SIZE_HOST_CLOCK_INFO	512

#define HOST_TS_CONFIG_FW_TIMESTAMP_PERIOD_DEFAULT	1000

struct dhd_ts {
	dhd_pub_t *dhdp;
	osl_t	*osh;
	uint32	xt_id;
	uint16	host_ts_capture_cnt;
	uint32	fw_ts_capture_cnt;
	uint32	fw_ts_disc_cnt;
	uint32	h_clkid_min;
	uint32	h_clkid_max;
	uint32	h_tsconf_period;

	/* sould these be per clock source */
	ts_correction_m_t	correction_m;
	ts_correction_m_t	correction_b;

	ts_fw_clock_info_t	fw_tlv[MAX_FW_CLKINFO_TYPES];
	uint32			fw_tlv_len;
	clk_ts_log_t 	fw_ts_log;
	uchar		host_ts_host_clk_info_buffer[MAX_BUF_SIZE_HOST_CLOCK_INFO];
	bool		host_ts_host_clk_info_buffer_in_use;
	dhd_ts_xt_id_t	xt_ids;
	uint32	active_ipc_version;

	uint32 	fwts2hsts_delay;
	uint32 	fwts2hsts_delay_wdcount;
	uint32	ts_watchdog_calls;
	uint64  last_ts_watchdog_time;
	uint32	pending_requests;

	dhd_ts_log_ts_t	tx_timestamps;
	dhd_ts_log_ts_t	rx_timestamps;
	/* outside modules could stop timesync independent of the user config */
	bool timesync_disabled;
	uint32	host_reset_cnt;
	bool nodrop_config;

	uint32	suspend_req;
	uint32	resume_req;
};
struct dhd_ts *g_dhd_ts;
static uint32 dhd_timesync_send_D2H_clk_correction(dhd_ts_t *ts);
static uint32 dhd_timesync_send_host_clk_info(dhd_ts_t *ts);
static uint32 dhd_timesync_send_host_clock_selection(dhd_ts_t *ts);
static uint32 dhd_timesync_send_host_timestamping_config(dhd_ts_t *ts, bool inject_err);
static void dhd_timesync_ts_log_dump_item(dhd_ts_log_ts_t *tsl, struct bcmstrbuf *b);

/* Check for and handle local prot-specific iovar commands */

enum {
	IOV_TS_INFO_DUMP,
	IOV_TS_TX_TS_DUMP,
	IOV_TS_RX_TS_DUMP,
	IOV_TS_FW_CLKINFO_DUMP,
	IOV_TS_HCLK_CLKID_MIN,
	IOV_TS_HCLK_CLKID_MAX,
	IOV_TS_HTSCONF_PERIOD,
	IOV_TS_SEND_TSCONFIG,
	IOV_TS_SEND_HCLK_SEL,
	IOV_TS_SEND_HCLK_INFO,
	IOV_TS_SEND_D2H_CRCT,
	IOV_TS_TXS_LOG,
	IOV_TS_RXS_LOG,
	IOV_TS_INJECT_BAD_XTID,
	IOV_TS_INJECT_BAD_TAG,
	IOV_TS_FWTS2HSTS_DELAY,
	IOV_TS_NODROP_CONFIG,
	IOV_TS_CLEAR_LOGS,
	IOV_TS_NO_RETRY,
	IOV_TS_NO_AGGR,
	IOV_TS_FIXED_RATE,
	IOV_LAST
};
const bcm_iovar_t dhd_ts_iovars[] = {
	{"ts_info_dump", IOV_TS_INFO_DUMP,	0,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
	{"ts_tx_ts_dump", IOV_TS_TX_TS_DUMP,	0,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
	{"ts_rx_ts_dump", IOV_TS_RX_TS_DUMP,	0,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
	{"ts_fw_clkinfo_dump", IOV_TS_FW_CLKINFO_DUMP, 0, 0, IOVT_BUFFER, DHD_IOCTL_MAXLEN },
	{"ts_hclk_clkid_min",	IOV_TS_HCLK_CLKID_MIN,	0,	0,	IOVT_UINT32,	0 },
	{"ts_hclk_clkid_max",	IOV_TS_HCLK_CLKID_MAX,	0,	0,	IOVT_UINT32,	0 },
	{"ts_htsconf_period",	IOV_TS_HTSCONF_PERIOD,	0,	0,	IOVT_UINT32,	0 },
	{"ts_send_tsconfig",	IOV_TS_SEND_TSCONFIG,	0,	0,	IOVT_UINT32,	0 },
	{"ts_send_hostclk_sel",	IOV_TS_SEND_HCLK_SEL,	0,	0,	IOVT_UINT32,	0 },
	{"ts_send_hostclk_info", IOV_TS_SEND_HCLK_INFO,	0,	0,	IOVT_UINT32,	0 },
	{"ts_send_d2h_corect ",	IOV_TS_SEND_D2H_CRCT,	0,	0,	IOVT_UINT32,	0 },
	{"ts_txs_log",		IOV_TS_TXS_LOG,		0,	0,	IOVT_UINT32,	0 },
	{"ts_rxs_log",		IOV_TS_RXS_LOG,		0,	0,	IOVT_UINT32,	0 },

	/* error injection cases */
	{"ts_inject_bad_xtid",	IOV_TS_INJECT_BAD_XTID,	0,	0,	IOVT_UINT32,	0 },
	{"ts_inject_bad_tag",	IOV_TS_INJECT_BAD_TAG,	0,	0,	IOVT_UINT32,	0 },
	{"ts_fwts2hsts_delay",	IOV_TS_FWTS2HSTS_DELAY,	0,	0,	IOVT_UINT32,	0 },
	{"ts_nodrop_config",	IOV_TS_NODROP_CONFIG,	0,	0,	IOVT_UINT32,	0 },
	{"ts_clear_logs",	IOV_TS_CLEAR_LOGS,	0,	0,	IOVT_UINT32,	0 },
	{"ts_set_no_retry",	IOV_TS_NO_RETRY,	0,	0,	IOVT_UINT32,	0 },
	{"ts_set_no_aggr",	IOV_TS_NO_AGGR,		0,	0,	IOVT_UINT32,	0 },
	{"ts_set_fixed_rate",	IOV_TS_FIXED_RATE,	0,	0,	IOVT_UINT32,	0 },

	{NULL, 0, 0, 0,	0, 0 }
};

static int dhd_ts_fw_clksrc_dump(dhd_ts_t *ts, char *buf, int buflen);
#ifdef CONFIG_PROC_FS
static int dhd_open_proc_ts_fw_clk_dump(struct inode *inode, struct file *file);
ssize_t dhd_read_proc_ts_fw_clk_dump(struct file *file, char *user_buf, size_t count, loff_t *loff);
static int dhd_open_proc_ts_tx_dump(struct inode *inode, struct file *file);
ssize_t dhd_read_proc_ts_tx_dump(struct file *file, char *user_buf, size_t count, loff_t *loff);
static int dhd_open_proc_ts_rx_dump(struct inode *inode, struct file *file);
ssize_t dhd_read_proc_ts_rx_dump(struct file *file, char *user_buf, size_t count, loff_t *loff);

static int
dhd_open_proc_ts_fw_clk_dump(struct inode *inode, struct file *file)
{
	return single_open(file, 0, NULL);
}
ssize_t
dhd_read_proc_ts_fw_clk_dump(struct file *file, char *user_buf, size_t count, loff_t *loff)
{
	dhd_ts_t *ts;
	char *buf;
	ssize_t ret = 0;

	ts = g_dhd_ts;
	if (ts == NULL) {
		return -EAGAIN;
	}
	if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(ts->dhdp)) {
		DHD_INFO(("%s bus is in suspend or suspend in progress\n", __func__));
		return -EAGAIN;
	}
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(ts->dhdp)) {
		DHD_ERROR(("%s rmmod in progress\n", __func__));
		return -ENOENT;
	}
	buf = kmalloc(count, GFP_KERNEL);
	if (buf == NULL) {
		DHD_ERROR(("%s failed to allocate buf with size %zu\n", __func__, count));
		return -ENOMEM;
	}
	ret = dhd_ts_fw_clksrc_dump(ts, buf, count);
	if (ret < 0) {
		return 0;
	}
	ret = simple_read_from_buffer(user_buf, count, loff, buf, (count - ret));
	kfree(buf);
	return ret;
}
static int dhd_open_proc_ts_tx_dump(struct inode *inode, struct file *file)
{
	return single_open(file, 0, NULL);
}
ssize_t
dhd_read_proc_ts_tx_dump(struct file *file, char *user_buf, size_t count, loff_t *loff)
{
	dhd_ts_t *ts;
	char *buf;
	ssize_t ret = 0;
	struct bcmstrbuf strbuf;

	ts = g_dhd_ts;
	if (ts == NULL) {
		return -EAGAIN;
	}
	if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(ts->dhdp)) {
		DHD_INFO(("%s bus is in suspend or suspend in progress\n", __func__));
		return -EAGAIN;
	}
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(ts->dhdp)) {
		DHD_ERROR(("%s rmmod in progress\n", __func__));
		return -ENOENT;
	}
	buf = kmalloc(count, GFP_KERNEL);
	if (buf == NULL) {
		DHD_ERROR(("%s failed to allocate buf with size %zu\n", __func__, count));
		return -ENOMEM;
	}
	bcm_binit(&strbuf, buf, count);
	bcm_bprintf(&strbuf, "Tx Log dump\n");
	dhd_timesync_ts_log_dump_item(&ts->tx_timestamps, &strbuf);
	ret = simple_read_from_buffer(user_buf, count, loff, buf, (count - strbuf.size));
	kfree(buf);
	return ret;
}

static int dhd_open_proc_ts_rx_dump(struct inode *inode, struct file *file)
{
	return single_open(file, 0, NULL);
}

ssize_t
dhd_read_proc_ts_rx_dump(struct file *file, char *user_buf, size_t count, loff_t *loff)
{
	dhd_ts_t *ts;
	char *buf;
	ssize_t ret = 0;
	struct bcmstrbuf strbuf;

	ts = g_dhd_ts;
	if (ts == NULL) {
		return -EAGAIN;
	}
	if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(ts->dhdp)) {
		DHD_INFO(("%s bus is in suspend or suspend in progress\n", __func__));
		return -EAGAIN;
	}
	if (DHD_BUS_CHECK_DOWN_OR_DOWN_IN_PROGRESS(ts->dhdp)) {
		DHD_ERROR(("%s rmmod in progress\n", __func__));
		return -ENOENT;
	}
	buf = kmalloc(count, GFP_KERNEL);
	if (buf == NULL) {
		DHD_ERROR(("%s failed to allocate buf with size %zu\n", __func__, count));
		return -ENOMEM;
	}
	bcm_binit(&strbuf, buf, count);
	bcm_bprintf(&strbuf, "Rx Log dump\n");
	dhd_timesync_ts_log_dump_item(&ts->rx_timestamps, &strbuf);
	ret = simple_read_from_buffer(user_buf, count, loff, buf, (count - strbuf.size));
	kfree(buf);
	return ret;
}

static const struct file_operations proc_fops_ts_fw_clk_dump = {
	.read = dhd_read_proc_ts_fw_clk_dump,
	.open = dhd_open_proc_ts_fw_clk_dump,
	.release = seq_release,
};
static const struct file_operations proc_fops_ts_tx_dump = {
	.read = dhd_read_proc_ts_tx_dump,
	.open = dhd_open_proc_ts_tx_dump,
	.release = seq_release,
};
static const struct file_operations proc_fops_ts_rx_dump = {
	.read = dhd_read_proc_ts_rx_dump,
	.open = dhd_open_proc_ts_rx_dump,
	.release = seq_release,
};
#endif /* CONFIG_PROC_FS */

int
dhd_timesync_detach(dhd_pub_t *dhdp)
{
	dhd_ts_t *ts;

	DHD_TRACE(("%s: %d\n", __FUNCTION__, __LINE__));

	if (!dhdp) {
		return BCME_OK;
	}
	ts = dhdp->ts;
#ifdef CONFIG_PROC_FS
	remove_proc_entry("ts_fw_clk_dump", NULL);
	remove_proc_entry("ts_tx_dump", NULL);
	remove_proc_entry("ts_rx_dump", NULL);
#endif /* CONFIG_PROC_FS */
#ifndef CONFIG_DHD_USE_STATIC_BUF
		MFREE(dhdp->osh, ts, sizeof(dhd_ts_t));
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	g_dhd_ts = NULL;
	dhdp->ts = NULL;
	DHD_INFO(("Deallocated DHD TS\n"));
	return BCME_OK;
}
int
dhd_timesync_attach(dhd_pub_t *dhdp)
{
	dhd_ts_t *ts;

	DHD_TRACE(("%s: %d\n", __FUNCTION__, __LINE__));
	/* Allocate prot structure */
	if (!(ts = (dhd_ts_t *)DHD_OS_PREALLOC(dhdp, DHD_PREALLOC_PROT,
		sizeof(dhd_ts_t)))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}
	memset(ts, 0, sizeof(*ts));

	g_dhd_ts = ts;
	ts->osh = dhdp->osh;
	dhdp->ts = ts;
	ts->dhdp = dhdp;

	ts->correction_m.low = 1;
	ts->correction_m.high = 1;

	ts->correction_b.low = 0;
	ts->correction_m.high = 0;

	ts->fwts2hsts_delay = DHD_DEFAULT_TIMESYNC_TIMER_VALUE;
	ts->fwts2hsts_delay_wdcount = 0;

	ts->tx_timestamps.max_idx = MAX_TS_LOG_SAMPLES_DATA;
	ts->rx_timestamps.max_idx = MAX_TS_LOG_SAMPLES_DATA;

	ts->xt_id = 1;

	DHD_INFO(("allocated DHD TS\n"));

#ifdef CONFIG_PROC_FS
	if (proc_create("ts_fw_clk_dump", S_IRUSR, NULL, &proc_fops_ts_fw_clk_dump) == NULL) {
		DHD_ERROR(("Failed to create /proc/ts_fw_clk_dump procfs interface\n"));
	}
	if (proc_create("ts_tx_dump", S_IRUSR, NULL, &proc_fops_ts_tx_dump) == NULL) {
		DHD_ERROR(("Failed to create /proc/ts_tx_dump procfs interface\n"));
	}
	if (proc_create("ts_rx_dump", S_IRUSR, NULL, &proc_fops_ts_rx_dump) == NULL) {
		DHD_ERROR(("Failed to create /proc/ts_rx_dump procfs interface\n"));
	}
#endif /* CONFIG_PROC_FS */

	return BCME_OK;

fail:
	if (dhdp->ts != NULL) {
		dhd_timesync_detach(dhdp);
	}
	return BCME_NOMEM;
}

static void
dhd_timesync_ts_log_dump_item(dhd_ts_log_ts_t *tsl, struct bcmstrbuf *b)
{
	uint32 i = 0;

	bcm_bprintf(b, "Max_idx: %d, cur_idx %d\n", tsl->max_idx, tsl->cur_idx);
	for (i = 0; i < tsl->max_idx; i++) {
		bcm_bprintf(b, "\t idx: %03d, (%d: %d) timestamp: 0x%08x:0x%08x "
				" proto: %02d, t1: 0x%08x t2: 0x%08x\n",
				i, tsl->ts_log[i].intf, tsl->ts_log[i].flowid,
				tsl->ts_log[i].ts_high, tsl->ts_log[i].ts_low,
				tsl->ts_log[i].proto, tsl->ts_log[i].t1,
				tsl->ts_log[i].t2);
	}
}

static int
dhd_timesync_ts_log_dump(dhd_ts_t *ts, char *buf, int buflen, bool tx)
{
	struct bcmstrbuf b;
	struct bcmstrbuf *strbuf = &b;

	bcm_binit(strbuf, buf, buflen);

	if (tx) {
		bcm_bprintf(strbuf, "Tx Log dump\t");
		dhd_timesync_ts_log_dump_item(&ts->tx_timestamps, strbuf);
	}
	else {
		bcm_bprintf(strbuf, "Rx Log dump\n");
		dhd_timesync_ts_log_dump_item(&ts->rx_timestamps, strbuf);
	}
	return BCME_OK;
}

static void
dhd_timesync_clear_logs(dhd_ts_t *ts)
{
	dhd_ts_log_ts_t *tsl;

	tsl = &ts->rx_timestamps;
	tsl->cur_idx = 0;
	memset(tsl->ts_log, 0, sizeof(dhd_ts_log_ts_item_t) *
			MAX_TS_LOG_SAMPLES_DATA);

	tsl = &ts->tx_timestamps;
	tsl->cur_idx = 0;
	memset(tsl->ts_log, 0, sizeof(dhd_ts_log_ts_item_t) *
			MAX_TS_LOG_SAMPLES_DATA);

	return;
}

void
dhd_timesync_debug_info_print(dhd_pub_t *dhdp)
{
	dhd_ts_t *ts = dhdp->ts;
	uint64 current_time;

	if (!ts) {
		DHD_ERROR(("%s: %d ts is NULL\n", __FUNCTION__, __LINE__));
		return;
	}

	DHD_ERROR(("\nts info dump: active_ipc_version %d\n", ts->active_ipc_version));
	current_time = OSL_LOCALTIME_NS();
	DHD_ERROR(("current_time="SEC_USEC_FMT" last_ts_watchdog_time="SEC_USEC_FMT"\n",
		GET_SEC_USEC(current_time), GET_SEC_USEC(ts->last_ts_watchdog_time)));
	DHD_ERROR(("timesync disabled %d\n", ts->timesync_disabled));
	DHD_ERROR(("Host TS dump cnt %d, fw TS dump cnt %d, descrepency %d\n",
		ts->host_ts_capture_cnt, ts->fw_ts_capture_cnt, ts->fw_ts_disc_cnt));
	DHD_ERROR(("ts_watchdog calls %d reset cnt %d\n",
		ts->ts_watchdog_calls, ts->host_reset_cnt));
	DHD_ERROR(("xt_ids tag/ID %d/%d, %d/%d, %d/%d, %d/%d\n",
		BCMMSGBUF_HOST_TIMESTAMPING_CONFIG_TAG, ts->xt_ids.host_timestamping_config,
		BCMMSGBUF_HOST_CLOCK_SELECT_TAG, ts->xt_ids.host_clock_selection,
		BCMMSGBUF_HOST_CLOCK_INFO_TAG, ts->xt_ids.host_clk_info,
		BCMMSGBUF_D2H_CLOCK_CORRECTION_TAG, ts->xt_ids.d2h_clk_correction));
	DHD_ERROR(("pending requests %d suspend req %d resume req %d\n",
		ts->pending_requests, ts->suspend_req, ts->resume_req));

}

static int
dhd_timesync_dump(dhd_ts_t *ts, char *buf, int buflen)
{
	struct bcmstrbuf b;
	struct bcmstrbuf *strbuf = &b;

	bcm_binit(strbuf, buf, buflen);

	bcm_bprintf(strbuf, "ts info dump: active_ipc_version %d\n", ts->active_ipc_version);
	bcm_bprintf(strbuf, "timesync disabled %d\n", ts->timesync_disabled);
	bcm_bprintf(strbuf, "Host TS dump cnt %d, fw TS dump cnt %d, descrepency %d\n",
		ts->host_ts_capture_cnt, ts->fw_ts_capture_cnt, ts->fw_ts_disc_cnt);
	bcm_bprintf(strbuf, "ts_watchdog calls %d reset cnt %d\n",
		ts->ts_watchdog_calls, ts->host_reset_cnt);
	bcm_bprintf(strbuf, "xt_ids tag/ID %d/%d, %d/%d, %d/%d, %d/%d\n",
		BCMMSGBUF_HOST_TIMESTAMPING_CONFIG_TAG, ts->xt_ids.host_timestamping_config,
		BCMMSGBUF_HOST_CLOCK_SELECT_TAG, ts->xt_ids.host_clock_selection,
		BCMMSGBUF_HOST_CLOCK_INFO_TAG, ts->xt_ids.host_clk_info,
		BCMMSGBUF_D2H_CLOCK_CORRECTION_TAG, ts->xt_ids.d2h_clk_correction);
	bcm_bprintf(strbuf, "pending requests %d suspend req %d resume req %d\n",
		ts->pending_requests, ts->suspend_req, ts->resume_req);

	return BCME_OK;
}

static int
dhd_timesync_doiovar(dhd_ts_t *ts, const bcm_iovar_t *vi, uint32 actionid, const char *name,
            void *params, uint plen, void *arg, uint len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	DHD_TRACE(("%s: actionid = %d; name %s\n", __FUNCTION__, actionid, name));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	switch (actionid) {
	case IOV_GVAL(IOV_TS_INFO_DUMP):
		dhd_timesync_dump(ts, arg, len);
		break;
	case IOV_GVAL(IOV_TS_TX_TS_DUMP):
		dhd_timesync_ts_log_dump(ts, arg, len, TRUE);
		break;
	case IOV_GVAL(IOV_TS_RX_TS_DUMP):
		dhd_timesync_ts_log_dump(ts, arg, len, FALSE);
		break;
	case IOV_GVAL(IOV_TS_FW_CLKINFO_DUMP):
		dhd_ts_fw_clksrc_dump(ts, arg, len);
		break;
	case IOV_SVAL(IOV_TS_SEND_TSCONFIG):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		bcmerror = dhd_timesync_send_host_timestamping_config(ts, FALSE);
		break;
	case IOV_SVAL(IOV_TS_SEND_HCLK_SEL):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		bcmerror = dhd_timesync_send_host_clock_selection(ts);
		break;
	case IOV_SVAL(IOV_TS_SEND_HCLK_INFO):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		bcmerror = dhd_timesync_send_host_clk_info(ts);
		break;
	case IOV_SVAL(IOV_TS_SEND_D2H_CRCT):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		bcmerror = dhd_timesync_send_D2H_clk_correction(ts);
		break;
	case IOV_SVAL(IOV_TS_INJECT_BAD_TAG):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		bcmerror = dhd_timesync_send_host_timestamping_config(ts, TRUE);
		break;
	case IOV_SVAL(IOV_TS_INJECT_BAD_XTID): {
		uint16 old_xt_id;

		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		old_xt_id = ts->xt_id;
		ts->xt_id += 10; /* will cause the error now */
		DHD_ERROR(("generating bad XTID transaction for the device exp %d, sending %d",
			old_xt_id, ts->xt_id));
		bcmerror = dhd_timesync_send_host_timestamping_config(ts, FALSE);
		ts->xt_id = old_xt_id;
		break;
	}
	case IOV_GVAL(IOV_TS_FWTS2HSTS_DELAY):
		bcopy(&ts->fwts2hsts_delay, arg, val_size);
		break;
	case IOV_SVAL(IOV_TS_FWTS2HSTS_DELAY):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		if (int_val > DHD_DEFAULT_TIMESYNC_TIMER_VALUE_MAX) {
			bcmerror = BCME_RANGE;
			break;
		}
		if (int_val <= DHD_DEFAULT_TIMESYNC_TIMER_VALUE) {
			bcmerror = BCME_RANGE;
			break;
		}
		ts->fwts2hsts_delay = int_val;
		break;
	case IOV_GVAL(IOV_TS_NODROP_CONFIG):
		bcopy(&ts->nodrop_config, arg, val_size);
		break;
	case IOV_SVAL(IOV_TS_NODROP_CONFIG):
		ts->nodrop_config = int_val;
		break;
	case IOV_GVAL(IOV_TS_NO_RETRY):
		int_val = dhd_prot_pkt_noretry(ts->dhdp, 0, FALSE);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_TS_NO_RETRY):
		dhd_prot_pkt_noretry(ts->dhdp, int_val, TRUE);
		break;
	case IOV_GVAL(IOV_TS_NO_AGGR):
		int_val = dhd_prot_pkt_noaggr(ts->dhdp, 0, FALSE);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_TS_NO_AGGR):
		dhd_prot_pkt_noaggr(ts->dhdp, int_val, TRUE);
		break;
	case IOV_GVAL(IOV_TS_FIXED_RATE):
		int_val = dhd_prot_pkt_fixed_rate(ts->dhdp, 0, FALSE);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_TS_FIXED_RATE):
		dhd_prot_pkt_fixed_rate(ts->dhdp, int_val, TRUE);
		break;
	case IOV_SVAL(IOV_TS_CLEAR_LOGS):
		dhd_timesync_clear_logs(ts);
		break;
	case IOV_GVAL(IOV_TS_TXS_LOG):
		int_val = dhd_prot_data_path_tx_timestamp_logging(ts->dhdp, 0,  FALSE);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_TS_TXS_LOG):
		dhd_prot_data_path_tx_timestamp_logging(ts->dhdp, int_val,  TRUE);
		break;
	case IOV_GVAL(IOV_TS_RXS_LOG):
		int_val = dhd_prot_data_path_rx_timestamp_logging(ts->dhdp, 0,  FALSE);
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_TS_RXS_LOG):
		dhd_prot_data_path_rx_timestamp_logging(ts->dhdp, int_val,  TRUE);
		break;
	case IOV_SVAL(IOV_TS_HTSCONF_PERIOD):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		ts->h_tsconf_period = int_val;
		break;
	case IOV_GVAL(IOV_TS_HTSCONF_PERIOD):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		bcopy(&ts->h_tsconf_period, arg, val_size);
		break;
	case IOV_SVAL(IOV_TS_HCLK_CLKID_MAX):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		ts->h_clkid_max = int_val;
		break;
	case IOV_GVAL(IOV_TS_HCLK_CLKID_MAX):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		bcopy(&ts->h_clkid_max, arg, val_size);
		break;

	case IOV_SVAL(IOV_TS_HCLK_CLKID_MIN):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		ts->h_clkid_min = int_val;
		break;
	case IOV_GVAL(IOV_TS_HCLK_CLKID_MIN):
		if (ts->active_ipc_version < 7) {
			bcmerror = BCME_ERROR;
			break;
		}
		bcopy(&ts->h_clkid_min, arg, val_size);
		break;
	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}
exit:
	DHD_TRACE(("%s: actionid %d, bcmerror %d\n", __FUNCTION__, actionid, bcmerror));
	return bcmerror;
}

int
dhd_timesync_iovar_op(dhd_ts_t *ts, const char *name,
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

	if ((vi = bcm_iovar_lookup(dhd_ts_iovars, name)) == NULL) {
		DHD_TRACE(("%s: not ours\n", name));
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

	bcmerror = dhd_timesync_doiovar(ts, vi, actionid, name, params, plen, arg, len, val_size);

exit:
	return bcmerror;
}

void
dhd_timesync_handle_host_ts_complete(dhd_ts_t *ts, uint16 xt_id, uint16 status)
{
	if (ts == NULL) {
		DHD_ERROR(("%s: called with ts null\n", __FUNCTION__));
		return;
	}
	DHD_INFO(("Host send TS complete, for ID %d, status %d\n", xt_id, status));
	if (xt_id == ts->xt_ids.host_clk_info) {
		if (ts->host_ts_host_clk_info_buffer_in_use != TRUE) {
			DHD_ERROR(("same ID as the host clock info, but buffer not in use: %d\n",
				ts->xt_ids.host_clk_info));
			return;
		}
		ts->host_ts_host_clk_info_buffer_in_use = FALSE;
	}
	ts->pending_requests--;
}

void
dhd_timesync_notify_ipc_rev(dhd_ts_t *ts, uint32 ipc_rev)
{
	if (ts != NULL)
		ts->active_ipc_version = ipc_rev;
}

static int
dhd_ts_fw_clksrc_dump(dhd_ts_t *ts, char *buf, int buflen)
{
	struct bcmstrbuf b;
	struct bcmstrbuf *strbuf = &b;
	clk_ts_log_t *fw_ts_log;
	uint32 i = 0, j = 0;
	clksrc_ts_log_t *clk_src;

	fw_ts_log = &ts->fw_ts_log;

	bcm_binit(strbuf, buf, buflen);

	while (i <= MAX_CLKSRC_ID) {
		clk_src = &fw_ts_log->ts_log[i];
		if (clk_src->inuse == FALSE) {
			bcm_bprintf(strbuf, "clkID %d: not in use\n", i);
		}
		else {
			bcm_bprintf(strbuf, "clkID %d: name %s Max idx %d, cur_idx %d\n",
				i, clk_src->name, MAX_FW_TS_LOG_SAMPLES, fw_ts_log->cur_idx);
			j = 0;
			while (j < MAX_FW_TS_LOG_SAMPLES) {
				bcm_bprintf(strbuf, "%03d: %03d: 0x%08x-0x%08x\n", j,
					fw_ts_log->seqnum[j], clk_src->log[j].ts_high,
					clk_src->log[j].ts_low);
				j++;
			}
		}
		i++;
	}
	return b.size;
}

static void
dhd_ts_fw_clksrc_log(dhd_ts_t *ts, uchar *tlvs, uint32 tlv_len, uint32 seqnum)
{
	ts_fw_clock_info_t *fw_clock_info;
	clksrc_ts_log_t	*clk_src;
	clk_ts_log_t *fw_ts_log;

	fw_ts_log = &ts->fw_ts_log;

	fw_ts_log->seqnum[fw_ts_log->cur_idx] = seqnum;
	while (tlv_len) {
		fw_clock_info = (ts_fw_clock_info_t *)tlvs;
		clk_src = &fw_ts_log->ts_log[(fw_clock_info->ts.ts_high >> 28) & 0xF];
		if (clk_src->inuse == FALSE) {
			bcopy(fw_clock_info->clk_src, clk_src->name, sizeof(clk_src->name));
			clk_src->inuse = TRUE;
		}
		clk_src->log[fw_ts_log->cur_idx].ts_low = fw_clock_info->ts.ts_low;
		clk_src->log[fw_ts_log->cur_idx].ts_high = fw_clock_info->ts.ts_high;

		tlvs += sizeof(*fw_clock_info);
		tlv_len -= sizeof(*fw_clock_info);
	}
	fw_ts_log->cur_idx++;
	if (fw_ts_log->cur_idx >= MAX_FW_TS_LOG_SAMPLES)
		fw_ts_log->cur_idx = 0;
}

void
dhd_timesync_handle_fw_timestamp(dhd_ts_t *ts, uchar *tlvs, uint32 tlv_len, uint32 seqnum)
{
	ts_fw_clock_info_t *fw_clock_info;
	uint16 tag_id;

	DHD_INFO(("FW sent timestamp message, tlv_len %d, seqnum %d\n", tlv_len, seqnum));
	bcm_print_bytes("fw ts", tlvs, tlv_len);
	/* we are expecting only one TLV type from the firmware side */
	/* BCMMSGBUF_FW_CLOCK_INFO_TAG */
	/* Validate the tag ID */
	if (ts == NULL) {
		DHD_ERROR(("%s:  NULL TS \n", __FUNCTION__));
		return;
	}
	if (tlvs == NULL) {
		DHD_ERROR(("%s:  NULL TLV \n", __FUNCTION__));
		return;
	}
	if (tlv_len < BCM_XTLV_HDR_SIZE) {
		DHD_ERROR(("%s:  bad length %d\n", __FUNCTION__, tlv_len));
		return;
	}
	if (tlv_len > MAX_SIZE_FW_CLKINFO_TYPE) {
		DHD_ERROR(("tlv_len %d more than what is supported in Host %d\n", tlv_len,
			(uint32)MAX_SIZE_FW_CLKINFO_TYPE));
		return;
	}
	if (tlv_len % (sizeof(*fw_clock_info))) {
		DHD_ERROR(("bad tlv_len for the packet %d, needs to be multiple of %d\n", tlv_len,
			(uint32)(sizeof(*fw_clock_info))));
		return;
	}

	/* validate the tag for all the include tag IDs */
	{
		uint32 check_len = 0;
		uchar *tag_ptr = (uchar *)(tlvs);
		while (check_len < tlv_len) {
			bcopy(tag_ptr+check_len, &tag_id, sizeof(uint16));
			DHD_INFO(("FWTS: tag_id %d, offset %d \n",
					tag_id, check_len));
			if (tag_id != BCMMSGBUF_FW_CLOCK_INFO_TAG) {
				DHD_ERROR(("Fatal: invalid tag from FW in TS: %d, offset %d \n",
					tag_id, check_len));
				return;
			}
			check_len += sizeof(*fw_clock_info);
		}
	}

	if (seqnum != (ts->fw_ts_capture_cnt + 1)) {
		DHD_ERROR(("FW TS descrepency: out of sequence exp %d, got %d, resyncing %d\n",
			ts->fw_ts_capture_cnt + 1, seqnum, seqnum));
		ts->fw_ts_disc_cnt++;
	}
	ts->fw_ts_capture_cnt = seqnum;

	/* copy it into local info */
	bcopy(tlvs, &ts->fw_tlv[0], tlv_len);
	ts->fw_tlv_len = tlv_len;

	dhd_ts_fw_clksrc_log(ts, tlvs, tlv_len, seqnum);
	/* launch the watchdog to send the host time stamp as per the delay programmed */
	if (ts->fwts2hsts_delay_wdcount != 0) {
		DHD_ERROR(("FATAL: Last Host sync is not sent out yet\n"));
		return;
	}
	if (dhd_watchdog_ms == 0) {
		DHD_ERROR(("FATAL: WATCHDOG is set to 0, timesync can't work properly \n"));
		return;
	}
	/* schedule sending host time sync values to device */
	ts->fwts2hsts_delay_wdcount = ts->fwts2hsts_delay / dhd_watchdog_ms;
	if (ts->fwts2hsts_delay_wdcount == 0)
		ts->fwts2hsts_delay_wdcount = 1;
}

static uint32
dhd_timesync_send_host_timestamping_config(dhd_ts_t *ts, bool inject_err)
{
	ts_host_timestamping_config_t ts_config;
	int ret_val;

	if (ts->timesync_disabled) {
		DHD_ERROR(("Timesync Disabled: Cannot send HOST TS config msg\n"));
		return BCME_ERROR;
	}
	bzero(&ts_config, sizeof(ts_config));

	ts_config.xtlv.id = BCMMSGBUF_HOST_TIMESTAMPING_CONFIG_TAG;
	if (inject_err)
		ts_config.xtlv.id = BCMMSGBUF_HOST_TS_BADTAG;

	ts_config.xtlv.len = sizeof(ts_config) - sizeof(_bcm_xtlv_t);
	ts_config.period_ms = ts->h_tsconf_period;

	if (ts_config.period_ms) {
		ts_config.flags |= FLAG_HOST_RESET;
		ts_config.reset_cnt = ts->host_reset_cnt + 1;
	}

	if (ts->nodrop_config) {
		ts_config.flags |= FLAG_CONFIG_NODROP;
		ts_config.post_delay = TS_NODROP_CONFIG_TO;
	} else {
		ts_config.post_delay = TS_DROP_CONFIG_TO;
	}

	DHD_ERROR(("sending Host Timestamping Config: TLV (ID %d, LEN %d), period %d, seq %d\n",
		ts_config.xtlv.id, ts_config.xtlv.len, ts_config.period_ms,
		ts->host_ts_capture_cnt));
	ret_val = dhd_prot_send_host_timestamp(ts->dhdp, (uchar *)&ts_config, sizeof(ts_config),
		ts->host_ts_capture_cnt, ts->xt_id);
	if (ret_val != 0) {
		DHD_ERROR(("Fatal: Error sending HOST TS config msg to device: %d\n", ret_val));
		return BCME_ERROR;
	}

	if (ts_config.period_ms) {
		ts->host_reset_cnt++;
	}

	ts->pending_requests++;
	ts->xt_ids.host_timestamping_config = ts->xt_id;
	ts->xt_id++;
	return BCME_OK;
}

static uint32
dhd_timesync_send_host_clock_selection(dhd_ts_t *ts)
{
	ts_host_clock_sel_t ts_clk_sel;
	int ret_val;

	if (ts->timesync_disabled) {
		DHD_ERROR(("Timesync Disabled: Cannot send HOST clock sel  msg\n"));
		return BCME_ERROR;
	}

	bzero(&ts_clk_sel, sizeof(ts_clk_sel));

	ts_clk_sel.xtlv.id = BCMMSGBUF_HOST_CLOCK_SELECT_TAG;
	ts_clk_sel.xtlv.len = sizeof(ts_clk_sel) - sizeof(_bcm_xtlv_t);
	ts_clk_sel.min_clk_idx = ts->h_clkid_min;
	ts_clk_sel.max_clk_idx = ts->h_clkid_max;
	DHD_INFO(("sending Host ClockSel Config: TLV (ID %d, LEN %d), min %d, max %d, seq %d\n",
		ts_clk_sel.xtlv.id, ts_clk_sel.xtlv.len, ts_clk_sel.min_clk_idx,
		ts_clk_sel.max_clk_idx,
		ts->host_ts_capture_cnt));
	ret_val = dhd_prot_send_host_timestamp(ts->dhdp, (uchar *)&ts_clk_sel, sizeof(ts_clk_sel),
		ts->host_ts_capture_cnt, ts->xt_id);
	if (ret_val != 0) {
		DHD_ERROR(("Fatal: Error sending HOST ClockSel msg to device: %d\n", ret_val));
		return BCME_ERROR;
	}
	ts->xt_ids.host_clock_selection = ts->xt_id;
	ts->xt_id++;
	ts->pending_requests++;
	return BCME_OK;
}

static uint32
dhd_timesync_send_host_clk_info(dhd_ts_t *ts)
{
	ts_host_clock_info_t *host_clock_info;
	uchar *clk_info_buffer;
	uint32 clk_info_bufsize;
	int ret_val;

	if (ts->timesync_disabled) {
		DHD_ERROR(("Timesync Disabled: Cannot send HOST clock config msg\n"));
		return BCME_ERROR;
	}
	if (ts->host_ts_host_clk_info_buffer_in_use == TRUE) {
		DHD_ERROR(("Host Ts Clock info buffer in Use\n"));
		return BCME_ERROR;
	}
	clk_info_buffer = &ts->host_ts_host_clk_info_buffer[0];
	clk_info_bufsize = sizeof(ts->host_ts_host_clk_info_buffer);

	DHD_INFO(("clk_info_buf size %d, tlv_len %d, host clk_info_len %d\n",
		clk_info_bufsize, ts->fw_tlv_len, (uint32)sizeof(*host_clock_info)));

	if (clk_info_bufsize < sizeof(*host_clock_info)) {
		DHD_ERROR(("clock_info_buf_size too small to fit host clock info %d, %d\n",
			clk_info_bufsize, (uint32)sizeof(*host_clock_info)));
		return BCME_ERROR;
	}

	host_clock_info = (ts_host_clock_info_t *)clk_info_buffer;
	host_clock_info->xtlv.id = BCMMSGBUF_HOST_CLOCK_INFO_TAG;
	host_clock_info->xtlv.len = sizeof(*host_clock_info) - sizeof(_bcm_xtlv_t);
	/* OSL_GET_CYCLES */
	host_clock_info->ticks.low = 0;
	host_clock_info->ticks.high = 0;
	/* OSL_SYS_UPTIME?? */
	host_clock_info->ns.low = 0;
	host_clock_info->ns.high = 0;
	clk_info_buffer += (sizeof(*host_clock_info));
	clk_info_bufsize -= sizeof(*host_clock_info);

	/* copy the device clk config as that is the reference for this */
	if (clk_info_bufsize < ts->fw_tlv_len) {
		DHD_ERROR(("clock info buffer is small to fit dev clk info %d, %d\n",
			clk_info_bufsize, ts->fw_tlv_len));
		return BCME_ERROR;
	}
	bcopy(ts->fw_tlv, clk_info_buffer, ts->fw_tlv_len);
	clk_info_bufsize -= ts->fw_tlv_len;

	DHD_INFO(("sending Host TS msg Len %d, xt_id %d, host_ts_capture_count %d\n",
		(uint32)(sizeof(ts->host_ts_host_clk_info_buffer) - clk_info_bufsize),
		ts->xt_id, ts->host_ts_capture_cnt));

	bcm_print_bytes("host ts",  (uchar *)ts->host_ts_host_clk_info_buffer,
		sizeof(ts->host_ts_host_clk_info_buffer) - clk_info_bufsize);

	ret_val = dhd_prot_send_host_timestamp(ts->dhdp, (uchar *)ts->host_ts_host_clk_info_buffer,
		sizeof(ts->host_ts_host_clk_info_buffer) - clk_info_bufsize,
		ts->host_ts_capture_cnt, ts->xt_id);
	if (ret_val != 0) {
		DHD_ERROR(("Fatal: Error sending HOST ClockSel msg to device: %d\n", ret_val));
		return BCME_ERROR;
	}
	ts->host_ts_host_clk_info_buffer_in_use = TRUE;
	ts->xt_ids.host_clk_info = ts->xt_id;
	ts->xt_id++;
	ts->pending_requests++;
	return BCME_OK;
}

static uint32
dhd_timesync_send_D2H_clk_correction(dhd_ts_t *ts)
{
	ts_d2h_clock_correction_t ts_clk_crtion;
	int ret_val;

	if (ts->timesync_disabled) {
		DHD_ERROR(("Timesync Disabled: Cannot send d2h clock correction msg\n"));
		return BCME_ERROR;
	}

	bzero(&ts_clk_crtion, sizeof(ts_clk_crtion));

	/* XXX: should this be sending for all the clock sources */

	ts_clk_crtion.xtlv.id = BCMMSGBUF_D2H_CLOCK_CORRECTION_TAG;
	ts_clk_crtion.xtlv.len = sizeof(ts_clk_crtion) - sizeof(_bcm_xtlv_t);
	ts_clk_crtion.clk_id = ts->h_clkid_max;
	ts_clk_crtion.m.low = ts->correction_m.low;
	ts_clk_crtion.m.high = ts->correction_m.high;
	ts_clk_crtion.b.low = ts->correction_b.low;
	ts_clk_crtion.b.high = ts->correction_b.high;

	DHD_INFO(("sending D2H Correction: ID %d, LEN %d, clkid %d, m %d:%d, b %d:%d, seq %d\n",
		ts_clk_crtion.xtlv.id, ts_clk_crtion.xtlv.len, ts_clk_crtion.clk_id,
		ts_clk_crtion.m.high,
		ts_clk_crtion.m.low,
		ts_clk_crtion.b.high,
		ts_clk_crtion.b.low,
		ts->host_ts_capture_cnt));

	ret_val = dhd_prot_send_host_timestamp(ts->dhdp, (uchar *)&ts_clk_crtion,
		sizeof(ts_clk_crtion), ts->host_ts_capture_cnt, ts->xt_id);
	if (ret_val != 0) {
		DHD_ERROR(("Fatal: Error sending HOST ClockSel msg to device: %d\n", ret_val));
		return BCME_ERROR;
	}
	ts->xt_ids.d2h_clk_correction = ts->xt_id;
	ts->xt_id++;
	ts->pending_requests++;
	return BCME_OK;
}

bool
dhd_timesync_delay_post_bufs(dhd_pub_t *dhdp)
{
	return (dhdp->ts->fwts2hsts_delay != 0);
}

bool
dhd_timesync_watchdog(dhd_pub_t *dhdp)
{
	dhd_ts_t *ts = dhdp->ts;

	if (ts == NULL)
		return FALSE;

	ts->last_ts_watchdog_time = OSL_LOCALTIME_NS();
	ts->ts_watchdog_calls++;

	/* XXX: this is relying the watchdog to be running..which may not hold good */
	if (ts->fwts2hsts_delay_wdcount) {
		ts->fwts2hsts_delay_wdcount--;
		if (ts->fwts2hsts_delay != 0 && dhdp->busstate == DHD_BUS_DATA &&
			(ts->fwts2hsts_delay_wdcount == 0)) {
			/* see if we need to send the host clock info */
			dhd_timesync_send_host_clk_info(ts);
			dhd_msgbuf_delay_post_ts_bufs(dhdp);
		}
	}
	return FALSE;
}

static void
dhd_timesync_log_timestamp_item(dhd_ts_log_ts_t *tsl, uint16 flowid, uint8 intf,
	uint32 ts_low, uint32 ts_high, dhd_pkt_parse_t *pkt)
{
	tsl->ts_log[tsl->cur_idx].ts_low = ts_low;
	tsl->ts_log[tsl->cur_idx].ts_high = ts_high;
	tsl->ts_log[tsl->cur_idx].intf = intf;
	tsl->ts_log[tsl->cur_idx].proto = pkt->proto;
	tsl->ts_log[tsl->cur_idx].t1 = pkt->t1;
	tsl->ts_log[tsl->cur_idx].t2 = pkt->t2;
	tsl->cur_idx++;
	if (tsl->cur_idx == tsl->max_idx)
		tsl->cur_idx = 0;
}

void
dhd_timesync_log_tx_timestamp(dhd_ts_t *ts, uint16 flowid, uint8 intf,
	uint32 ts_low, uint32 ts_high, dhd_pkt_parse_t *pkt)
{
	if (ts != NULL) {
		dhd_timesync_log_timestamp_item(&ts->tx_timestamps, flowid, intf,
			ts_low, ts_high, pkt);
	}
}

void
dhd_timesync_log_rx_timestamp(dhd_ts_t *ts, uint8 intf, uint32 ts_low, uint32 ts_high,
	dhd_pkt_parse_t *pkt)
{
	if (ts != NULL) {
		dhd_timesync_log_timestamp_item(&ts->rx_timestamps, 0, intf,
			ts_low, ts_high, pkt);
	}
}

void
dhd_timesync_control(dhd_pub_t *dhdp, bool disabled)
{
	dhd_ts_t *ts;
	if (dhdp == NULL)
		return;

	ts = dhdp->ts;
	if (ts != NULL) {
		if (disabled) {
			DHD_ERROR(("resetting the timesync counter, current(%d)\n",
				ts->fw_ts_capture_cnt));

			ts->fw_ts_capture_cnt = 0;

			/* Suspend case: Disable timesync after the config message */
			if ((ts->active_ipc_version >= 7) && (ts->h_tsconf_period != 0)) {
				uint32	tsconf_period;

				tsconf_period = ts->h_tsconf_period;
				ts->h_tsconf_period = 0;

				dhd_timesync_send_host_timestamping_config(ts, FALSE);
				ts->h_tsconf_period = tsconf_period;
			}
			ts->timesync_disabled = TRUE;
			ts->suspend_req++;
		} else {
			/* Resume case: Enable timesync before the config message */
			DHD_ERROR(("enabling the timesync counter, current(%d)\n",
				ts->fw_ts_capture_cnt));

			ts->timesync_disabled = FALSE;
			ts->resume_req++;

			if ((ts->active_ipc_version >= 7) && (ts->h_tsconf_period != 0))
				dhd_timesync_send_host_timestamping_config(ts, FALSE);
		}
	}
	/* XXX: may be all the other internal iovar calls should check for disabled state */
}
