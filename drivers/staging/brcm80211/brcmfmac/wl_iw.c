/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <bcmdefs.h>
#include <linux/netdevice.h>
#include <osl.h>
#include <wlioctl.h>

#include <bcmutils.h>
#include <bcmendian.h>
#include <proto/ethernet.h>

#include <linux/if_arp.h>
#include <asm/uaccess.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhdioctl.h>

typedef void wlc_info_t;
typedef void wl_info_t;
typedef const struct si_pub si_t;
#include <wlioctl.h>

#include <proto/ethernet.h>
#include <dngl_stats.h>
#include <dhd.h>
#define WL_ERROR(x) printf x
#define WL_TRACE(x)
#define WL_ASSOC(x)
#define WL_INFORM(x)
#define WL_WSEC(x)
#define WL_SCAN(x)

#include <wl_iw.h>

#define IW_WSEC_ENABLED(wsec)	((wsec) & (WEP_ENABLED |	\
					 TKIP_ENABLED | AES_ENABLED))

#include <linux/rtnetlink.h>

#define WL_IW_USE_ISCAN  1
#define ENABLE_ACTIVE_PASSIVE_SCAN_SUPPRESS  1

bool g_set_essid_before_scan = true;

#define WL_IW_IOCTL_CALL(func_call) \
	do {				\
		func_call;		\
	} while (0)

static int g_onoff = G_WLAN_SET_ON;
wl_iw_extra_params_t g_wl_iw_params;

extern bool wl_iw_conn_status_str(u32 event_type, u32 status,
				  u32 reason, char *stringBuf, uint buflen);

uint wl_msg_level = WL_ERROR_VAL;

#define MAX_WLIW_IOCTL_LEN 1024

#if defined(IL_BIGENDIAN)
#include <bcmendian.h>
#define htod32(i) (bcmswap32(i))
#define htod16(i) (bcmswap16(i))
#define dtoh32(i) (bcmswap32(i))
#define dtoh16(i) (bcmswap16(i))
#define htodchanspec(i) htod16(i)
#define dtohchanspec(i) dtoh16(i)
#else
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
#endif

#ifdef CONFIG_WIRELESS_EXT

extern struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
extern int dhd_wait_pend8021x(struct net_device *dev);
#endif

#if WIRELESS_EXT < 19
#define IW_IOCTL_IDX(cmd)	((cmd) - SIOCIWFIRST)
#define IW_EVENT_IDX(cmd)	((cmd) - IWEVFIRST)
#endif

static void *g_scan;
static volatile uint g_scan_specified_ssid;
static wlc_ssid_t g_specific_ssid;

static wlc_ssid_t g_ssid;

#if defined(WL_IW_USE_ISCAN)
#define ISCAN_STATE_IDLE   0
#define ISCAN_STATE_SCANING 1

#define WLC_IW_ISCAN_MAXLEN   2048
typedef struct iscan_buf {
	struct iscan_buf *next;
	char iscan_buf[WLC_IW_ISCAN_MAXLEN];
} iscan_buf_t;

typedef struct iscan_info {
	struct net_device *dev;
	struct timer_list timer;
	u32 timer_ms;
	u32 timer_on;
	int iscan_state;
	iscan_buf_t *list_hdr;
	iscan_buf_t *list_cur;

	struct task_struct *sysioc_tsk;
	struct semaphore sysioc_sem;

#if defined CSCAN
	char ioctlbuf[WLC_IOCTL_MEDLEN];
#else
	char ioctlbuf[WLC_IOCTL_SMLEN];
#endif
	wl_iscan_params_t *iscan_ex_params_p;
	int iscan_ex_param_size;
} iscan_info_t;
iscan_info_t *g_iscan;
static void wl_iw_timerfunc(unsigned long data);
static void wl_iw_set_event_mask(struct net_device *dev);
static int wl_iw_iscan(iscan_info_t *iscan, wlc_ssid_t *ssid, u16 action);
#endif				/* defined(WL_IW_USE_ISCAN) */

static int
wl_iw_set_scan(struct net_device *dev,
	       struct iw_request_info *info,
	       union iwreq_data *wrqu, char *extra);

static int
wl_iw_get_scan(struct net_device *dev,
	       struct iw_request_info *info,
	       struct iw_point *dwrq, char *extra);

static uint
wl_iw_get_scan_prep(wl_scan_results_t *list,
		    struct iw_request_info *info, char *extra, short max_size);

static void swap_key_from_BE(wl_wsec_key_t *key)
{
	key->index = htod32(key->index);
	key->len = htod32(key->len);
	key->algo = htod32(key->algo);
	key->flags = htod32(key->flags);
	key->rxiv.hi = htod32(key->rxiv.hi);
	key->rxiv.lo = htod16(key->rxiv.lo);
	key->iv_initialized = htod32(key->iv_initialized);
}

static void swap_key_to_BE(wl_wsec_key_t *key)
{
	key->index = dtoh32(key->index);
	key->len = dtoh32(key->len);
	key->algo = dtoh32(key->algo);
	key->flags = dtoh32(key->flags);
	key->rxiv.hi = dtoh32(key->rxiv.hi);
	key->rxiv.lo = dtoh16(key->rxiv.lo);
	key->iv_initialized = dtoh32(key->iv_initialized);
}

static int dev_wlc_ioctl(struct net_device *dev, int cmd, void *arg, int len)
{
	struct ifreq ifr;
	wl_ioctl_t ioc;
	mm_segment_t fs;
	int ret = -EINVAL;

	if (!dev) {
		WL_ERROR(("%s: dev is null\n", __func__));
		return ret;
	}

	WL_INFORM(("\n%s, PID:%x: send Local IOCTL -> dhd: cmd:0x%x, buf:%p, "
		"len:%d ,\n", __func__, current->pid, cmd, arg, len));

	if (g_onoff == G_WLAN_SET_ON) {
		memset(&ioc, 0, sizeof(ioc));
		ioc.cmd = cmd;
		ioc.buf = arg;
		ioc.len = len;

		strcpy(ifr.ifr_name, dev->name);
		ifr.ifr_data = (caddr_t)&ioc;

		ret = dev_open(dev);
		if (ret) {
			WL_ERROR(("%s: Error dev_open: %d\n", __func__, ret));
			return ret;
		}

		fs = get_fs();
		set_fs(get_ds());
		ret = dev->netdev_ops->ndo_do_ioctl(dev, &ifr, SIOCDEVPRIVATE);
		set_fs(fs);
	} else {
		WL_TRACE(("%s: call after driver stop : ignored\n", __func__));
	}
	return ret;
}

static int dev_wlc_intvar_set(struct net_device *dev, char *name, int val)
{
	char buf[WLC_IOCTL_SMLEN];
	uint len;

	val = htod32(val);
	len = bcm_mkiovar(name, (char *)(&val), sizeof(val), buf, sizeof(buf));
	ASSERT(len);

	return dev_wlc_ioctl(dev, WLC_SET_VAR, buf, len);
}

#if defined(WL_IW_USE_ISCAN)
static int
dev_iw_iovar_setbuf(struct net_device *dev,
		    char *iovar,
		    void *param, int paramlen, void *bufptr, int buflen)
{
	int iolen;

	iolen = bcm_mkiovar(iovar, param, paramlen, bufptr, buflen);
	ASSERT(iolen);

	if (iolen == 0)
		return 0;

	return dev_wlc_ioctl(dev, WLC_SET_VAR, bufptr, iolen);
}

static int
dev_iw_iovar_getbuf(struct net_device *dev,
		    char *iovar,
		    void *param, int paramlen, void *bufptr, int buflen)
{
	int iolen;

	iolen = bcm_mkiovar(iovar, param, paramlen, bufptr, buflen);
	ASSERT(iolen);

	return dev_wlc_ioctl(dev, WLC_GET_VAR, bufptr, buflen);
}
#endif				/* defined(WL_IW_USE_ISCAN) */

#if WIRELESS_EXT > 17
static int
dev_wlc_bufvar_set(struct net_device *dev, char *name, char *buf, int len)
{
	static char ioctlbuf[MAX_WLIW_IOCTL_LEN];
	uint buflen;

	buflen = bcm_mkiovar(name, buf, len, ioctlbuf, sizeof(ioctlbuf));
	ASSERT(buflen);

	return dev_wlc_ioctl(dev, WLC_SET_VAR, ioctlbuf, buflen);
}
#endif				/* WIRELESS_EXT > 17 */

static int
dev_wlc_bufvar_get(struct net_device *dev, char *name, char *buf, int buflen)
{
	static char ioctlbuf[MAX_WLIW_IOCTL_LEN];
	int error;
	uint len;

	len = bcm_mkiovar(name, NULL, 0, ioctlbuf, sizeof(ioctlbuf));
	ASSERT(len);
	error =
	    dev_wlc_ioctl(dev, WLC_GET_VAR, (void *)ioctlbuf,
			  MAX_WLIW_IOCTL_LEN);
	if (!error)
		bcopy(ioctlbuf, buf, buflen);

	return error;
}

static int dev_wlc_intvar_get(struct net_device *dev, char *name, int *retval)
{
	union {
		char buf[WLC_IOCTL_SMLEN];
		int val;
	} var;
	int error;

	uint len;
	uint data_null;

	len =
	    bcm_mkiovar(name, (char *)(&data_null), 0, (char *)(&var),
			sizeof(var.buf));
	ASSERT(len);
	error = dev_wlc_ioctl(dev, WLC_GET_VAR, (void *)&var, len);

	*retval = dtoh32(var.val);

	return error;
}

#if WIRELESS_EXT < 13
struct iw_request_info {
	__u16 cmd;
	__u16 flags;
};

typedef int (*iw_handler) (struct net_device *dev,
			   struct iw_request_info *info,
			   void *wrqu, char *extra);
#endif

static int
wl_iw_config_commit(struct net_device *dev,
		    struct iw_request_info *info, void *zwrq, char *extra)
{
	wlc_ssid_t ssid;
	int error;
	struct sockaddr bssid;

	WL_TRACE(("%s: SIOCSIWCOMMIT\n", dev->name));

	error = dev_wlc_ioctl(dev, WLC_GET_SSID, &ssid, sizeof(ssid));
	if (error)
		return error;

	ssid.SSID_len = dtoh32(ssid.SSID_len);

	if (!ssid.SSID_len)
		return 0;

	memset(&bssid, 0, sizeof(struct sockaddr));
	error = dev_wlc_ioctl(dev, WLC_REASSOC, &bssid, ETHER_ADDR_LEN);
	if (error) {
		WL_ERROR(("%s: WLC_REASSOC to %s failed\n", __func__,
			  ssid.SSID));
		return error;
	}

	return 0;
}

static int
wl_iw_get_name(struct net_device *dev,
	       struct iw_request_info *info, char *cwrq, char *extra)
{
	WL_TRACE(("%s: SIOCGIWNAME\n", dev->name));

	strcpy(cwrq, "IEEE 802.11-DS");

	return 0;
}

static int
wl_iw_set_freq(struct net_device *dev,
	       struct iw_request_info *info, struct iw_freq *fwrq, char *extra)
{
	int error, chan;
	uint sf = 0;

	WL_TRACE(("\n %s %s: SIOCSIWFREQ\n", __func__, dev->name));

	if (fwrq->e == 0 && fwrq->m < MAXCHANNEL) {
		chan = fwrq->m;
	} else {
		if (fwrq->e >= 6) {
			fwrq->e -= 6;
			while (fwrq->e--)
				fwrq->m *= 10;
		} else if (fwrq->e < 6) {
			while (fwrq->e++ < 6)
				fwrq->m /= 10;
		}
		if (fwrq->m > 4000 && fwrq->m < 5000)
			sf = WF_CHAN_FACTOR_4_G;

		chan = wf_mhz2channel(fwrq->m, sf);
	}
	chan = htod32(chan);

	error = dev_wlc_ioctl(dev, WLC_SET_CHANNEL, &chan, sizeof(chan));
	if (error)
		return error;

	g_wl_iw_params.target_channel = chan;
	return -EINPROGRESS;
}

static int
wl_iw_get_freq(struct net_device *dev,
	       struct iw_request_info *info, struct iw_freq *fwrq, char *extra)
{
	channel_info_t ci;
	int error;

	WL_TRACE(("%s: SIOCGIWFREQ\n", dev->name));

	error = dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(ci));
	if (error)
		return error;

	fwrq->m = dtoh32(ci.hw_channel);
	fwrq->e = dtoh32(0);
	return 0;
}

static int
wl_iw_set_mode(struct net_device *dev,
	       struct iw_request_info *info, __u32 *uwrq, char *extra)
{
	int infra = 0, ap = 0, error = 0;

	WL_TRACE(("%s: SIOCSIWMODE\n", dev->name));

	switch (*uwrq) {
	case IW_MODE_MASTER:
		infra = ap = 1;
		break;
	case IW_MODE_ADHOC:
	case IW_MODE_AUTO:
		break;
	case IW_MODE_INFRA:
		infra = 1;
		break;
	default:
		return -EINVAL;
	}
	infra = htod32(infra);
	ap = htod32(ap);

	error = dev_wlc_ioctl(dev, WLC_SET_INFRA, &infra, sizeof(infra));
	if (error)
		return error;

	error = dev_wlc_ioctl(dev, WLC_SET_AP, &ap, sizeof(ap));
	if (error)
		return error;

	return -EINPROGRESS;
}

static int
wl_iw_get_mode(struct net_device *dev,
	       struct iw_request_info *info, __u32 *uwrq, char *extra)
{
	int error, infra = 0, ap = 0;

	WL_TRACE(("%s: SIOCGIWMODE\n", dev->name));

	error = dev_wlc_ioctl(dev, WLC_GET_INFRA, &infra, sizeof(infra));
	if (error)
		return error;

	error = dev_wlc_ioctl(dev, WLC_GET_AP, &ap, sizeof(ap));
	if (error)
		return error;

	infra = dtoh32(infra);
	ap = dtoh32(ap);
	*uwrq = infra ? ap ? IW_MODE_MASTER : IW_MODE_INFRA : IW_MODE_ADHOC;

	return 0;
}

static int
wl_iw_get_range(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *dwrq, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	wl_u32_list_t *list;
	wl_rateset_t rateset;
	s8 *channels;
	int error, i, k;
	uint sf, ch;

	int phytype;
	int bw_cap = 0, sgi_tx = 0, nmode = 0;
	channel_info_t ci;
	u8 nrate_list2copy = 0;
	u16 nrate_list[4][8] = { {13, 26, 39, 52, 78, 104, 117, 130},
	{14, 29, 43, 58, 87, 116, 130, 144},
	{27, 54, 81, 108, 162, 216, 243, 270},
	{30, 60, 90, 120, 180, 240, 270, 300}
	};

	WL_TRACE(("%s: SIOCGIWRANGE\n", dev->name));

	if (!extra)
		return -EINVAL;

	channels = kmalloc((MAXCHANNEL + 1) * 4, GFP_KERNEL);
	if (!channels) {
		WL_ERROR(("Could not alloc channels\n"));
		return -ENOMEM;
	}
	list = (wl_u32_list_t *) channels;

	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(range));

	range->min_nwid = range->max_nwid = 0;

	list->count = htod32(MAXCHANNEL);
	error = dev_wlc_ioctl(dev, WLC_GET_VALID_CHANNELS, channels,
				(MAXCHANNEL + 1) * 4);
	if (error) {
		kfree(channels);
		return error;
	}
	for (i = 0; i < dtoh32(list->count) && i < IW_MAX_FREQUENCIES; i++) {
		range->freq[i].i = dtoh32(list->element[i]);

		ch = dtoh32(list->element[i]);
		if (ch <= CH_MAX_2G_CHANNEL)
			sf = WF_CHAN_FACTOR_2_4_G;
		else
			sf = WF_CHAN_FACTOR_5_G;

		range->freq[i].m = wf_channel2mhz(ch, sf);
		range->freq[i].e = 6;
	}
	range->num_frequency = range->num_channels = i;

	range->max_qual.qual = 5;
	range->max_qual.level = 0x100 - 200;
	range->max_qual.noise = 0x100 - 200;
	range->sensitivity = 65535;

#if WIRELESS_EXT > 11
	range->avg_qual.qual = 3;
	range->avg_qual.level = 0x100 + WL_IW_RSSI_GOOD;
	range->avg_qual.noise = 0x100 - 75;
#endif

	error = dev_wlc_ioctl(dev, WLC_GET_CURR_RATESET, &rateset,
				sizeof(rateset));
	if (error) {
		kfree(channels);
		return error;
	}
	rateset.count = dtoh32(rateset.count);
	range->num_bitrates = rateset.count;
	for (i = 0; i < rateset.count && i < IW_MAX_BITRATES; i++)
		range->bitrate[i] = (rateset.rates[i] & 0x7f) * 500000;
	dev_wlc_intvar_get(dev, "nmode", &nmode);
	dev_wlc_ioctl(dev, WLC_GET_PHYTYPE, &phytype, sizeof(phytype));

	if (nmode == 1 && phytype == WLC_PHY_TYPE_SSN) {
		dev_wlc_intvar_get(dev, "mimo_bw_cap", &bw_cap);
		dev_wlc_intvar_get(dev, "sgi_tx", &sgi_tx);
		dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci,
			      sizeof(channel_info_t));
		ci.hw_channel = dtoh32(ci.hw_channel);

		if (bw_cap == 0 || (bw_cap == 2 && ci.hw_channel <= 14)) {
			if (sgi_tx == 0)
				nrate_list2copy = 0;
			else
				nrate_list2copy = 1;
		}
		if (bw_cap == 1 || (bw_cap == 2 && ci.hw_channel >= 36)) {
			if (sgi_tx == 0)
				nrate_list2copy = 2;
			else
				nrate_list2copy = 3;
		}
		range->num_bitrates += 8;
		for (k = 0; i < range->num_bitrates; k++, i++) {
			range->bitrate[i] =
			    (nrate_list[nrate_list2copy][k]) * 500000;
		}
	}

	error = dev_wlc_ioctl(dev, WLC_GET_PHYTYPE, &i, sizeof(i));
	if (error) {
		kfree(channels);
		return error;
	}
	i = dtoh32(i);
	if (i == WLC_PHY_TYPE_A)
		range->throughput = 24000000;
	else
		range->throughput = 1500000;

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->max_encoding_tokens = DOT11_MAX_DEFAULT_KEYS;
	range->num_encoding_sizes = 4;
	range->encoding_size[0] = WEP1_KEY_SIZE;
	range->encoding_size[1] = WEP128_KEY_SIZE;
#if WIRELESS_EXT > 17
	range->encoding_size[2] = TKIP_KEY_SIZE;
#else
	range->encoding_size[2] = 0;
#endif
	range->encoding_size[3] = AES_KEY_SIZE;

	range->min_pmp = 0;
	range->max_pmp = 0;
	range->min_pmt = 0;
	range->max_pmt = 0;
	range->pmp_flags = 0;
	range->pm_capa = 0;

	range->num_txpower = 2;
	range->txpower[0] = 1;
	range->txpower[1] = 255;
	range->txpower_capa = IW_TXPOW_MWATT;

#if WIRELESS_EXT > 10
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 19;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->r_time_flags = 0;
	range->min_retry = 1;
	range->max_retry = 255;
	range->min_r_time = 0;
	range->max_r_time = 0;
#endif

#if WIRELESS_EXT > 17
	range->enc_capa = IW_ENC_CAPA_WPA;
	range->enc_capa |= IW_ENC_CAPA_CIPHER_TKIP;
	range->enc_capa |= IW_ENC_CAPA_CIPHER_CCMP;
	range->enc_capa |= IW_ENC_CAPA_WPA2;

	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVTXDROP);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVMICHAELMICFAILURE);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVPMKIDCAND);
#endif				/* WIRELESS_EXT > 17 */

	kfree(channels);

	return 0;
}

static int rssi_to_qual(int rssi)
{
	if (rssi <= WL_IW_RSSI_NO_SIGNAL)
		return 0;
	else if (rssi <= WL_IW_RSSI_VERY_LOW)
		return 1;
	else if (rssi <= WL_IW_RSSI_LOW)
		return 2;
	else if (rssi <= WL_IW_RSSI_GOOD)
		return 3;
	else if (rssi <= WL_IW_RSSI_VERY_GOOD)
		return 4;
	else
		return 5;
}

static int
wl_iw_set_spy(struct net_device *dev,
	      struct iw_request_info *info, struct iw_point *dwrq, char *extra)
{
	wl_iw_t *iw = *(wl_iw_t **) netdev_priv(dev);
	struct sockaddr *addr = (struct sockaddr *)extra;
	int i;

	WL_TRACE(("%s: SIOCSIWSPY\n", dev->name));

	if (!extra)
		return -EINVAL;

	iw->spy_num = min_t(int, ARRAY_SIZE(iw->spy_addr), dwrq->length);
	for (i = 0; i < iw->spy_num; i++)
		memcpy(&iw->spy_addr[i], addr[i].sa_data, ETHER_ADDR_LEN);
	memset(iw->spy_qual, 0, sizeof(iw->spy_qual));

	return 0;
}

static int
wl_iw_get_spy(struct net_device *dev,
	      struct iw_request_info *info, struct iw_point *dwrq, char *extra)
{
	wl_iw_t *iw = *(wl_iw_t **) netdev_priv(dev);
	struct sockaddr *addr = (struct sockaddr *)extra;
	struct iw_quality *qual = (struct iw_quality *)&addr[iw->spy_num];
	int i;

	WL_TRACE(("%s: SIOCGIWSPY\n", dev->name));

	if (!extra)
		return -EINVAL;

	dwrq->length = iw->spy_num;
	for (i = 0; i < iw->spy_num; i++) {
		memcpy(addr[i].sa_data, &iw->spy_addr[i], ETHER_ADDR_LEN);
		addr[i].sa_family = AF_UNIX;
		memcpy(&qual[i], &iw->spy_qual[i], sizeof(struct iw_quality));
		iw->spy_qual[i].updated = 0;
	}

	return 0;
}

static int
wl_iw_ch_to_chanspec(int ch, wl_join_params_t *join_params,
		     int *join_params_size)
{
	chanspec_t chanspec = 0;

	if (ch != 0) {
		join_params->params.chanspec_num = 1;
		join_params->params.chanspec_list[0] = ch;

		if (join_params->params.chanspec_list[0])
			chanspec |= WL_CHANSPEC_BAND_2G;
		else
			chanspec |= WL_CHANSPEC_BAND_5G;

		chanspec |= WL_CHANSPEC_BW_20;
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;

		*join_params_size += WL_ASSOC_PARAMS_FIXED_SIZE +
		    join_params->params.chanspec_num * sizeof(chanspec_t);

		join_params->params.chanspec_list[0] &= WL_CHANSPEC_CHAN_MASK;
		join_params->params.chanspec_list[0] |= chanspec;
		join_params->params.chanspec_list[0] =
		    htodchanspec(join_params->params.chanspec_list[0]);

		join_params->params.chanspec_num =
		    htod32(join_params->params.chanspec_num);

		WL_TRACE(("%s  join_params->params.chanspec_list[0]= %X\n",
			  __func__, join_params->params.chanspec_list[0]));
	}
	return 1;
}

static int
wl_iw_set_wap(struct net_device *dev,
	      struct iw_request_info *info, struct sockaddr *awrq, char *extra)
{
	int error = -EINVAL;
	wl_join_params_t join_params;
	int join_params_size;

	WL_TRACE(("%s: SIOCSIWAP\n", dev->name));

	if (awrq->sa_family != ARPHRD_ETHER) {
		WL_ERROR(("Invalid Header...sa_family\n"));
		return -EINVAL;
	}

	if (ETHER_ISBCAST(awrq->sa_data) || ETHER_ISNULLADDR(awrq->sa_data)) {
		scb_val_t scbval;
		memset(&scbval, 0, sizeof(scb_val_t));
		(void)dev_wlc_ioctl(dev, WLC_DISASSOC, &scbval,
				    sizeof(scb_val_t));
		return 0;
	}

	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	memcpy(join_params.ssid.SSID, g_ssid.SSID, g_ssid.SSID_len);
	join_params.ssid.SSID_len = htod32(g_ssid.SSID_len);
	memcpy(&join_params.params.bssid, awrq->sa_data, ETHER_ADDR_LEN);

	WL_TRACE(("%s  target_channel=%d\n", __func__,
		  g_wl_iw_params.target_channel));
	wl_iw_ch_to_chanspec(g_wl_iw_params.target_channel, &join_params,
			     &join_params_size);

	error = dev_wlc_ioctl(dev, WLC_SET_SSID, &join_params,
				join_params_size);
	if (error) {
		WL_ERROR(("%s Invalid ioctl data=%d\n", __func__, error));
	}

	if (g_ssid.SSID_len) {
		WL_TRACE(("%s: join SSID=%s BSSID=%pM ch=%d\n",
			  __func__, g_ssid.SSID, awrq->sa_data,
			  g_wl_iw_params.target_channel));
	}

	memset(&g_ssid, 0, sizeof(g_ssid));
	return 0;
}

static int
wl_iw_get_wap(struct net_device *dev,
	      struct iw_request_info *info, struct sockaddr *awrq, char *extra)
{
	WL_TRACE(("%s: SIOCGIWAP\n", dev->name));

	awrq->sa_family = ARPHRD_ETHER;
	memset(awrq->sa_data, 0, ETHER_ADDR_LEN);

	(void)dev_wlc_ioctl(dev, WLC_GET_BSSID, awrq->sa_data, ETHER_ADDR_LEN);

	return 0;
}

#if WIRELESS_EXT > 17
static int
wl_iw_mlme(struct net_device *dev,
	   struct iw_request_info *info, struct sockaddr *awrq, char *extra)
{
	struct iw_mlme *mlme;
	scb_val_t scbval;
	int error = -EINVAL;

	WL_TRACE(("%s: SIOCSIWMLME DISASSOC/DEAUTH\n", dev->name));

	mlme = (struct iw_mlme *)extra;
	if (mlme == NULL) {
		WL_ERROR(("Invalid ioctl data.\n"));
		return error;
	}

	scbval.val = mlme->reason_code;
	bcopy(&mlme->addr.sa_data, &scbval.ea, ETHER_ADDR_LEN);

	if (mlme->cmd == IW_MLME_DISASSOC) {
		scbval.val = htod32(scbval.val);
		error =
		    dev_wlc_ioctl(dev, WLC_DISASSOC, &scbval,
				  sizeof(scb_val_t));
	} else if (mlme->cmd == IW_MLME_DEAUTH) {
		scbval.val = htod32(scbval.val);
		error =
		    dev_wlc_ioctl(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON,
				  &scbval, sizeof(scb_val_t));
	} else {
		WL_ERROR(("Invalid ioctl data.\n"));
		return error;
	}

	return error;
}
#endif				/* WIRELESS_EXT > 17 */

#ifndef WL_IW_USE_ISCAN
static int
wl_iw_get_aplist(struct net_device *dev,
		 struct iw_request_info *info,
		 struct iw_point *dwrq, char *extra)
{
	wl_scan_results_t *list;
	struct sockaddr *addr = (struct sockaddr *)extra;
	struct iw_quality qual[IW_MAX_AP];
	wl_bss_info_t *bi = NULL;
	int error, i;
	uint buflen = dwrq->length;

	WL_TRACE(("%s: SIOCGIWAPLIST\n", dev->name));

	if (!extra)
		return -EINVAL;

	list = kmalloc(buflen, GFP_KERNEL);
	if (!list)
		return -ENOMEM;
	memset(list, 0, buflen);
	list->buflen = htod32(buflen);
	error = dev_wlc_ioctl(dev, WLC_SCAN_RESULTS, list, buflen);
	if (error) {
		WL_ERROR(("%d: Scan results error %d\n", __LINE__, error));
		kfree(list);
		return error;
	}
	list->buflen = dtoh32(list->buflen);
	list->version = dtoh32(list->version);
	list->count = dtoh32(list->count);
	if (list->version != WL_BSS_INFO_VERSION) {
		WL_ERROR(("%s : list->version %d != WL_BSS_INFO_VERSION\n",
			  __func__, list->version));
		kfree(list);
		return -EINVAL;
	}

	for (i = 0, dwrq->length = 0;
	     i < list->count && dwrq->length < IW_MAX_AP; i++) {
		bi = bi ? (wl_bss_info_t *) ((unsigned long)bi +
					     dtoh32(bi->length)) : list->
		    bss_info;
		ASSERT(((unsigned long)bi + dtoh32(bi->length)) <=
		       ((unsigned long)list + buflen));

		if (!(dtoh16(bi->capability) & DOT11_CAP_ESS))
			continue;

		memcpy(addr[dwrq->length].sa_data, &bi->BSSID, ETHER_ADDR_LEN);
		addr[dwrq->length].sa_family = ARPHRD_ETHER;
		qual[dwrq->length].qual = rssi_to_qual(dtoh16(bi->RSSI));
		qual[dwrq->length].level = 0x100 + dtoh16(bi->RSSI);
		qual[dwrq->length].noise = 0x100 + bi->phy_noise;

#if WIRELESS_EXT > 18
		qual[dwrq->length].updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
#else
		qual[dwrq->length].updated = 7;
#endif
		dwrq->length++;
	}

	kfree(list);

	if (dwrq->length) {
		memcpy(&addr[dwrq->length], qual,
		       sizeof(struct iw_quality) * dwrq->length);
		dwrq->flags = 1;
	}

	return 0;
}
#endif				/* WL_IW_USE_ISCAN */

#ifdef WL_IW_USE_ISCAN
static int
wl_iw_iscan_get_aplist(struct net_device *dev,
		       struct iw_request_info *info,
		       struct iw_point *dwrq, char *extra)
{
	wl_scan_results_t *list;
	iscan_buf_t *buf;
	iscan_info_t *iscan = g_iscan;

	struct sockaddr *addr = (struct sockaddr *)extra;
	struct iw_quality qual[IW_MAX_AP];
	wl_bss_info_t *bi = NULL;
	int i;

	WL_TRACE(("%s: SIOCGIWAPLIST\n", dev->name));

	if (!extra)
		return -EINVAL;

	if ((!iscan) || (!iscan->sysioc_tsk)) {
		WL_ERROR(("%s error\n", __func__));
		return 0;
	}

	buf = iscan->list_hdr;
	while (buf) {
		list = &((wl_iscan_results_t *) buf->iscan_buf)->results;
		if (list->version != WL_BSS_INFO_VERSION) {
			WL_ERROR(("%s : list->version %d != "
				"WL_BSS_INFO_VERSION\n",
				__func__, list->version));
			return -EINVAL;
		}

		bi = NULL;
		for (i = 0, dwrq->length = 0;
		     i < list->count && dwrq->length < IW_MAX_AP; i++) {
			bi = bi ? (wl_bss_info_t *) ((unsigned long)bi +
						     dtoh32(bi->length)) :
			    list->bss_info;
			ASSERT(((unsigned long)bi + dtoh32(bi->length)) <=
			       ((unsigned long)list + WLC_IW_ISCAN_MAXLEN));

			if (!(dtoh16(bi->capability) & DOT11_CAP_ESS))
				continue;

			memcpy(addr[dwrq->length].sa_data, &bi->BSSID,
			       ETHER_ADDR_LEN);
			addr[dwrq->length].sa_family = ARPHRD_ETHER;
			qual[dwrq->length].qual =
			    rssi_to_qual(dtoh16(bi->RSSI));
			qual[dwrq->length].level = 0x100 + dtoh16(bi->RSSI);
			qual[dwrq->length].noise = 0x100 + bi->phy_noise;

#if WIRELESS_EXT > 18
			qual[dwrq->length].updated =
			    IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
#else
			qual[dwrq->length].updated = 7;
#endif

			dwrq->length++;
		}
		buf = buf->next;
	}
	if (dwrq->length) {
		memcpy(&addr[dwrq->length], qual,
		       sizeof(struct iw_quality) * dwrq->length);
		dwrq->flags = 1;
	}

	return 0;
}

static int wl_iw_iscan_prep(wl_scan_params_t *params, wlc_ssid_t *ssid)
{
	int err = 0;

	memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
	params->bss_type = DOT11_BSSTYPE_ANY;
	params->scan_type = 0;
	params->nprobes = -1;
	params->active_time = -1;
	params->passive_time = -1;
	params->home_time = -1;
	params->channel_num = 0;

	params->nprobes = htod32(params->nprobes);
	params->active_time = htod32(params->active_time);
	params->passive_time = htod32(params->passive_time);
	params->home_time = htod32(params->home_time);
	if (ssid && ssid->SSID_len)
		memcpy(&params->ssid, ssid, sizeof(wlc_ssid_t));

	return err;
}

static int wl_iw_iscan(iscan_info_t *iscan, wlc_ssid_t *ssid, u16 action)
{
	int err = 0;

	iscan->iscan_ex_params_p->version = htod32(ISCAN_REQ_VERSION);
	iscan->iscan_ex_params_p->action = htod16(action);
	iscan->iscan_ex_params_p->scan_duration = htod16(0);

	WL_SCAN(("%s : nprobes=%d\n", __func__,
		 iscan->iscan_ex_params_p->params.nprobes));
	WL_SCAN(("active_time=%d\n",
		 iscan->iscan_ex_params_p->params.active_time));
	WL_SCAN(("passive_time=%d\n",
		 iscan->iscan_ex_params_p->params.passive_time));
	WL_SCAN(("home_time=%d\n", iscan->iscan_ex_params_p->params.home_time));
	WL_SCAN(("scan_type=%d\n", iscan->iscan_ex_params_p->params.scan_type));
	WL_SCAN(("bss_type=%d\n", iscan->iscan_ex_params_p->params.bss_type));

	(void)dev_iw_iovar_setbuf(iscan->dev, "iscan", iscan->iscan_ex_params_p,
				  iscan->iscan_ex_param_size, iscan->ioctlbuf,
				  sizeof(iscan->ioctlbuf));

	return err;
}

static void wl_iw_timerfunc(unsigned long data)
{
	iscan_info_t *iscan = (iscan_info_t *) data;
	if (iscan) {
		iscan->timer_on = 0;
		if (iscan->iscan_state != ISCAN_STATE_IDLE) {
			WL_TRACE(("timer trigger\n"));
			up(&iscan->sysioc_sem);
		}
	}
}

static void wl_iw_set_event_mask(struct net_device *dev)
{
	char eventmask[WL_EVENTING_MASK_LEN];
	char iovbuf[WL_EVENTING_MASK_LEN + 12];

	dev_iw_iovar_getbuf(dev, "event_msgs", "", 0, iovbuf, sizeof(iovbuf));
	bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);
	setbit(eventmask, WLC_E_SCAN_COMPLETE);
	dev_iw_iovar_setbuf(dev, "event_msgs", eventmask, WL_EVENTING_MASK_LEN,
			    iovbuf, sizeof(iovbuf));
}

static u32 wl_iw_iscan_get(iscan_info_t *iscan)
{
	iscan_buf_t *buf;
	iscan_buf_t *ptr;
	wl_iscan_results_t *list_buf;
	wl_iscan_results_t list;
	wl_scan_results_t *results;
	u32 status;
	int res = 0;

	MUTEX_LOCK_WL_SCAN_SET();
	if (iscan->list_cur) {
		buf = iscan->list_cur;
		iscan->list_cur = buf->next;
	} else {
		buf = kmalloc(sizeof(iscan_buf_t), GFP_KERNEL);
		if (!buf) {
			WL_ERROR(("%s can't alloc iscan_buf_t : going to abort "
				"currect iscan\n", __func__));
			MUTEX_UNLOCK_WL_SCAN_SET();
			return WL_SCAN_RESULTS_NO_MEM;
		}
		buf->next = NULL;
		if (!iscan->list_hdr)
			iscan->list_hdr = buf;
		else {
			ptr = iscan->list_hdr;
			while (ptr->next) {
				ptr = ptr->next;
			}
			ptr->next = buf;
		}
	}
	memset(buf->iscan_buf, 0, WLC_IW_ISCAN_MAXLEN);
	list_buf = (wl_iscan_results_t *) buf->iscan_buf;
	results = &list_buf->results;
	results->buflen = WL_ISCAN_RESULTS_FIXED_SIZE;
	results->version = 0;
	results->count = 0;

	memset(&list, 0, sizeof(list));
	list.results.buflen = htod32(WLC_IW_ISCAN_MAXLEN);
	res = dev_iw_iovar_getbuf(iscan->dev,
				  "iscanresults",
				  &list,
				  WL_ISCAN_RESULTS_FIXED_SIZE,
				  buf->iscan_buf, WLC_IW_ISCAN_MAXLEN);
	if (res == 0) {
		results->buflen = dtoh32(results->buflen);
		results->version = dtoh32(results->version);
		results->count = dtoh32(results->count);
		WL_TRACE(("results->count = %d\n", results->count));
		WL_TRACE(("results->buflen = %d\n", results->buflen));
		status = dtoh32(list_buf->status);
	} else {
		WL_ERROR(("%s returns error %d\n", __func__, res));
		status = WL_SCAN_RESULTS_NO_MEM;
	}
	MUTEX_UNLOCK_WL_SCAN_SET();
	return status;
}

static void wl_iw_force_specific_scan(iscan_info_t *iscan)
{
	WL_TRACE(("%s force Specific SCAN for %s\n", __func__,
		  g_specific_ssid.SSID));
	rtnl_lock();

	(void)dev_wlc_ioctl(iscan->dev, WLC_SCAN, &g_specific_ssid,
			    sizeof(g_specific_ssid));

	rtnl_unlock();
}

static void wl_iw_send_scan_complete(iscan_info_t *iscan)
{
#ifndef SANDGATE2G
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(wrqu));

	wireless_send_event(iscan->dev, SIOCGIWSCAN, &wrqu, NULL);
	WL_TRACE(("Send Event ISCAN complete\n"));
#endif
}

static int _iscan_sysioc_thread(void *data)
{
	u32 status;
	iscan_info_t *iscan = (iscan_info_t *) data;
	static bool iscan_pass_abort = false;

	allow_signal(SIGTERM);
	status = WL_SCAN_RESULTS_PARTIAL;
	while (down_interruptible(&iscan->sysioc_sem) == 0) {
		if (kthread_should_stop())
			break;

		if (iscan->timer_on) {
			del_timer_sync(&iscan->timer);
			iscan->timer_on = 0;
		}
		rtnl_lock();
		status = wl_iw_iscan_get(iscan);
		rtnl_unlock();
		if (g_scan_specified_ssid && (iscan_pass_abort == true)) {
			WL_TRACE(("%s Get results from specific scan "
				"status = %d\n", __func__, status));
			wl_iw_send_scan_complete(iscan);
			iscan_pass_abort = false;
			status = -1;
		}

		switch (status) {
		case WL_SCAN_RESULTS_PARTIAL:
			WL_TRACE(("iscanresults incomplete\n"));
			rtnl_lock();
			wl_iw_iscan(iscan, NULL, WL_SCAN_ACTION_CONTINUE);
			rtnl_unlock();
			mod_timer(&iscan->timer,
				  jiffies + iscan->timer_ms * HZ / 1000);
			iscan->timer_on = 1;
			break;
		case WL_SCAN_RESULTS_SUCCESS:
			WL_TRACE(("iscanresults complete\n"));
			iscan->iscan_state = ISCAN_STATE_IDLE;
			wl_iw_send_scan_complete(iscan);
			break;
		case WL_SCAN_RESULTS_PENDING:
			WL_TRACE(("iscanresults pending\n"));
			mod_timer(&iscan->timer,
				  jiffies + iscan->timer_ms * HZ / 1000);
			iscan->timer_on = 1;
			break;
		case WL_SCAN_RESULTS_ABORTED:
			WL_TRACE(("iscanresults aborted\n"));
			iscan->iscan_state = ISCAN_STATE_IDLE;
			if (g_scan_specified_ssid == 0)
				wl_iw_send_scan_complete(iscan);
			else {
				iscan_pass_abort = true;
				wl_iw_force_specific_scan(iscan);
			}
			break;
		case WL_SCAN_RESULTS_NO_MEM:
			WL_TRACE(("iscanresults can't alloc memory: skip\n"));
			iscan->iscan_state = ISCAN_STATE_IDLE;
			break;
		default:
			WL_TRACE(("iscanresults returned unknown status %d\n",
				  status));
			break;
		}
	}

	if (iscan->timer_on) {
		del_timer_sync(&iscan->timer);
		iscan->timer_on = 0;
	}
	return 0;
}
#endif				/* WL_IW_USE_ISCAN */

static int
wl_iw_set_scan(struct net_device *dev,
	       struct iw_request_info *info,
	       union iwreq_data *wrqu, char *extra)
{
	int error;
	WL_TRACE(("\n:%s dev:%s: SIOCSIWSCAN : SCAN\n", __func__, dev->name));

	g_set_essid_before_scan = false;
#if defined(CSCAN)
	WL_ERROR(("%s: Scan from SIOCGIWSCAN not supported\n", __func__));
	return -EINVAL;
#endif

	if (g_onoff == G_WLAN_SET_OFF)
		return 0;

	memset(&g_specific_ssid, 0, sizeof(g_specific_ssid));
#ifndef WL_IW_USE_ISCAN
	g_scan_specified_ssid = 0;
#endif

#if WIRELESS_EXT > 17
	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			struct iw_scan_req *req = (struct iw_scan_req *)extra;
			if (g_scan_specified_ssid) {
				WL_TRACE(("%s Specific SCAN is not done ignore "
					"scan for = %s\n",
					__func__, req->essid));
				return -EBUSY;
			} else {
				g_specific_ssid.SSID_len = min_t(size_t,
						sizeof(g_specific_ssid.SSID),
						req->essid_len);
				memcpy(g_specific_ssid.SSID, req->essid,
				       g_specific_ssid.SSID_len);
				g_specific_ssid.SSID_len =
				    htod32(g_specific_ssid.SSID_len);
				g_scan_specified_ssid = 1;
				WL_TRACE(("### Specific scan ssid=%s len=%d\n",
					  g_specific_ssid.SSID,
					  g_specific_ssid.SSID_len));
			}
		}
	}
#endif				/* WIRELESS_EXT > 17 */
	error = dev_wlc_ioctl(dev, WLC_SCAN, &g_specific_ssid,
				sizeof(g_specific_ssid));
	if (error) {
		WL_TRACE(("#### Set SCAN for %s failed with %d\n",
			  g_specific_ssid.SSID, error));
		g_scan_specified_ssid = 0;
		return -EBUSY;
	}

	return 0;
}

#ifdef WL_IW_USE_ISCAN
int wl_iw_iscan_set_scan_broadcast_prep(struct net_device *dev, uint flag)
{
	wlc_ssid_t ssid;
	iscan_info_t *iscan = g_iscan;

	if (flag)
		rtnl_lock();

	wl_iw_set_event_mask(dev);

	WL_TRACE(("+++: Set Broadcast ISCAN\n"));
	memset(&ssid, 0, sizeof(ssid));

	iscan->list_cur = iscan->list_hdr;
	iscan->iscan_state = ISCAN_STATE_SCANING;

	memset(&iscan->iscan_ex_params_p->params, 0,
	       iscan->iscan_ex_param_size);
	wl_iw_iscan_prep(&iscan->iscan_ex_params_p->params, &ssid);
	wl_iw_iscan(iscan, &ssid, WL_SCAN_ACTION_START);

	if (flag)
		rtnl_unlock();

	mod_timer(&iscan->timer, jiffies + iscan->timer_ms * HZ / 1000);

	iscan->timer_on = 1;

	return 0;
}

static int
wl_iw_iscan_set_scan(struct net_device *dev,
		     struct iw_request_info *info,
		     union iwreq_data *wrqu, char *extra)
{
	wlc_ssid_t ssid;
	iscan_info_t *iscan = g_iscan;

	WL_TRACE(("%s: SIOCSIWSCAN : ISCAN\n", dev->name));

#if defined(CSCAN)
	WL_ERROR(("%s: Scan from SIOCGIWSCAN not supported\n", __func__));
	return -EINVAL;
#endif

	if (g_onoff == G_WLAN_SET_OFF) {
		WL_TRACE(("%s: driver is not up yet after START\n", __func__));
		return 0;
	}
#ifdef PNO_SUPPORT
	if (dhd_dev_get_pno_status(dev)) {
		WL_ERROR(("%s: Scan called when PNO is active\n", __func__));
	}
#endif

	if ((!iscan) || (!iscan->sysioc_tsk))
		return wl_iw_set_scan(dev, info, wrqu, extra);

	if (g_scan_specified_ssid) {
		WL_TRACE(("%s Specific SCAN already running ignoring BC scan\n",
			  __func__));
		return EBUSY;
	}

	memset(&ssid, 0, sizeof(ssid));

#if WIRELESS_EXT > 17
	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			struct iw_scan_req *req = (struct iw_scan_req *)extra;
			ssid.SSID_len = min_t(size_t, sizeof(ssid.SSID),
						req->essid_len);
			memcpy(ssid.SSID, req->essid, ssid.SSID_len);
			ssid.SSID_len = htod32(ssid.SSID_len);
		} else {
			g_scan_specified_ssid = 0;

			if (iscan->iscan_state == ISCAN_STATE_SCANING) {
				WL_TRACE(("%s ISCAN already in progress \n",
					  __func__));
				return 0;
			}
		}
	}
#endif				/* WIRELESS_EXT > 17 */
	wl_iw_iscan_set_scan_broadcast_prep(dev, 0);

	return 0;
}
#endif				/* WL_IW_USE_ISCAN */

#if WIRELESS_EXT > 17
static bool ie_is_wpa_ie(u8 **wpaie, u8 **tlvs, int *tlvs_len)
{

	u8 *ie = *wpaie;

	if ((ie[1] >= 6) &&
	    !memcmp((const void *)&ie[2], (const void *)(WPA_OUI "\x01"), 4)) {
		return true;
	}

	ie += ie[1] + 2;
	*tlvs_len -= (int)(ie - *tlvs);
	*tlvs = ie;
	return false;
}

static bool ie_is_wps_ie(u8 **wpsie, u8 **tlvs, int *tlvs_len)
{

	u8 *ie = *wpsie;

	if ((ie[1] >= 4) &&
	    !memcmp((const void *)&ie[2], (const void *)(WPA_OUI "\x04"), 4)) {
		return true;
	}

	ie += ie[1] + 2;
	*tlvs_len -= (int)(ie - *tlvs);
	*tlvs = ie;
	return false;
}
#endif				/* WIRELESS_EXT > 17 */

static int
wl_iw_handle_scanresults_ies(char **event_p, char *end,
			     struct iw_request_info *info, wl_bss_info_t *bi)
{
#if WIRELESS_EXT > 17
	struct iw_event iwe;
	char *event;

	event = *event_p;
	if (bi->ie_length) {
		bcm_tlv_t *ie;
		u8 *ptr = ((u8 *) bi) + sizeof(wl_bss_info_t);
		int ptr_len = bi->ie_length;

		ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_RSN_ID);
		if (ie) {
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = ie->len + 2;
			event =
			    IWE_STREAM_ADD_POINT(info, event, end, &iwe,
						 (char *)ie);
		}
		ptr = ((u8 *) bi) + sizeof(wl_bss_info_t);

		while ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_WPA_ID))) {
			if (ie_is_wps_ie(((u8 **)&ie), &ptr, &ptr_len)) {
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = ie->len + 2;
				event =
				    IWE_STREAM_ADD_POINT(info, event, end, &iwe,
							 (char *)ie);
				break;
			}
		}

		ptr = ((u8 *) bi) + sizeof(wl_bss_info_t);
		ptr_len = bi->ie_length;
		while ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_WPA_ID))) {
			if (ie_is_wpa_ie(((u8 **)&ie), &ptr, &ptr_len)) {
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = ie->len + 2;
				event =
				    IWE_STREAM_ADD_POINT(info, event, end, &iwe,
							 (char *)ie);
				break;
			}
		}

		*event_p = event;
	}
#endif		/* WIRELESS_EXT > 17 */
	return 0;
}

static uint
wl_iw_get_scan_prep(wl_scan_results_t *list,
		    struct iw_request_info *info, char *extra, short max_size)
{
	int i, j;
	struct iw_event iwe;
	wl_bss_info_t *bi = NULL;
	char *event = extra, *end = extra + max_size - WE_ADD_EVENT_FIX, *value;
	int ret = 0;

	ASSERT(list);

	for (i = 0; i < list->count && i < IW_MAX_AP; i++) {
		if (list->version != WL_BSS_INFO_VERSION) {
			WL_ERROR(("%s : list->version %d != "
				"WL_BSS_INFO_VERSION\n",
				__func__, list->version));
			return ret;
		}

		bi = bi ? (wl_bss_info_t *)((unsigned long)bi +
					     dtoh32(bi->length)) : list->
		    bss_info;

		WL_TRACE(("%s : %s\n", __func__, bi->SSID));

		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data, &bi->BSSID, ETHER_ADDR_LEN);
		event =
		    IWE_STREAM_ADD_EVENT(info, event, end, &iwe,
					 IW_EV_ADDR_LEN);
		iwe.u.data.length = dtoh32(bi->SSID_len);
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.flags = 1;
		event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, bi->SSID);

		if (dtoh16(bi->capability) & (DOT11_CAP_ESS | DOT11_CAP_IBSS)) {
			iwe.cmd = SIOCGIWMODE;
			if (dtoh16(bi->capability) & DOT11_CAP_ESS)
				iwe.u.mode = IW_MODE_INFRA;
			else
				iwe.u.mode = IW_MODE_ADHOC;
			event =
			    IWE_STREAM_ADD_EVENT(info, event, end, &iwe,
						 IW_EV_UINT_LEN);
		}

		iwe.cmd = SIOCGIWFREQ;
		iwe.u.freq.m = wf_channel2mhz(CHSPEC_CHANNEL(bi->chanspec),
					      CHSPEC_CHANNEL(bi->chanspec) <=
					      CH_MAX_2G_CHANNEL ?
					      WF_CHAN_FACTOR_2_4_G :
					      WF_CHAN_FACTOR_5_G);
		iwe.u.freq.e = 6;
		event =
		    IWE_STREAM_ADD_EVENT(info, event, end, &iwe,
					 IW_EV_FREQ_LEN);

		iwe.cmd = IWEVQUAL;
		iwe.u.qual.qual = rssi_to_qual(dtoh16(bi->RSSI));
		iwe.u.qual.level = 0x100 + dtoh16(bi->RSSI);
		iwe.u.qual.noise = 0x100 + bi->phy_noise;
		event =
		    IWE_STREAM_ADD_EVENT(info, event, end, &iwe,
					 IW_EV_QUAL_LEN);

		wl_iw_handle_scanresults_ies(&event, end, info, bi);

		iwe.cmd = SIOCGIWENCODE;
		if (dtoh16(bi->capability) & DOT11_CAP_PRIVACY)
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;
		iwe.u.data.length = 0;
		event =
		    IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)event);

		if (bi->rateset.count) {
			if (((event - extra) +
				IW_EV_LCP_LEN) <= (unsigned long)end) {
				value = event + IW_EV_LCP_LEN;
				iwe.cmd = SIOCGIWRATE;
				iwe.u.bitrate.fixed = iwe.u.bitrate.disabled =
				    0;
				for (j = 0;
				     j < bi->rateset.count
				     && j < IW_MAX_BITRATES; j++) {
					iwe.u.bitrate.value =
					    (bi->rateset.rates[j] & 0x7f) *
					    500000;
					value =
					    IWE_STREAM_ADD_VALUE(info, event,
						 value, end, &iwe,
						 IW_EV_PARAM_LEN);
				}
				event = value;
			}
		}
	}

	ret = event - extra;
	if (ret < 0) {
		WL_ERROR(("==> Wrong size\n"));
		ret = 0;
	}
	WL_TRACE(("%s: size=%d bytes prepared\n", __func__,
		  (unsigned int)(event - extra)));
	return (uint)ret;
}

static int
wl_iw_get_scan(struct net_device *dev,
	       struct iw_request_info *info, struct iw_point *dwrq, char *extra)
{
	channel_info_t ci;
	wl_scan_results_t *list_merge;
	wl_scan_results_t *list = (wl_scan_results_t *) g_scan;
	int error;
	uint buflen_from_user = dwrq->length;
	uint len = G_SCAN_RESULTS;
	__u16 len_ret = 0;
#if defined(WL_IW_USE_ISCAN)
	iscan_info_t *iscan = g_iscan;
	iscan_buf_t *p_buf;
#endif

	WL_TRACE(("%s: buflen_from_user %d: \n", dev->name, buflen_from_user));

	if (!extra) {
		WL_TRACE(("%s: wl_iw_get_scan return -EINVAL\n", dev->name));
		return -EINVAL;
	}

	error = dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(ci));
	if (error)
		return error;
	ci.scan_channel = dtoh32(ci.scan_channel);
	if (ci.scan_channel)
		return -EAGAIN;

	if (g_scan_specified_ssid) {
		list = kmalloc(len, GFP_KERNEL);
		if (!list) {
			WL_TRACE(("%s: wl_iw_get_scan return -ENOMEM\n",
				  dev->name));
			g_scan_specified_ssid = 0;
			return -ENOMEM;
		}
	}

	memset(list, 0, len);
	list->buflen = htod32(len);
	error = dev_wlc_ioctl(dev, WLC_SCAN_RESULTS, list, len);
	if (error) {
		WL_ERROR(("%s: %s : Scan_results ERROR %d\n", dev->name,
			  __func__, error));
		dwrq->length = len;
		if (g_scan_specified_ssid) {
			g_scan_specified_ssid = 0;
			kfree(list);
		}
		return 0;
	}
	list->buflen = dtoh32(list->buflen);
	list->version = dtoh32(list->version);
	list->count = dtoh32(list->count);

	if (list->version != WL_BSS_INFO_VERSION) {
		WL_ERROR(("%s : list->version %d != WL_BSS_INFO_VERSION\n",
			  __func__, list->version));
		if (g_scan_specified_ssid) {
			g_scan_specified_ssid = 0;
			kfree(list);
		}
		return -EINVAL;
	}

	if (g_scan_specified_ssid) {
		WL_TRACE(("%s: Specified scan APs in the list =%d\n",
			  __func__, list->count));
		len_ret =
		    (__u16) wl_iw_get_scan_prep(list, info, extra,
						buflen_from_user);
		kfree(list);

#if defined(WL_IW_USE_ISCAN)
		p_buf = iscan->list_hdr;
		while (p_buf != iscan->list_cur) {
			list_merge =
			    &((wl_iscan_results_t *) p_buf->iscan_buf)->results;
			WL_TRACE(("%s: Bcast APs list=%d\n", __func__,
				  list_merge->count));
			if (list_merge->count > 0)
				len_ret +=
				    (__u16) wl_iw_get_scan_prep(list_merge,
					info, extra + len_ret,
					buflen_from_user - len_ret);
			p_buf = p_buf->next;
		}
#else
		list_merge = (wl_scan_results_t *) g_scan;
		WL_TRACE(("%s: Bcast APs list=%d\n", __func__,
			  list_merge->count));
		if (list_merge->count > 0)
			len_ret +=
			    (__u16) wl_iw_get_scan_prep(list_merge, info,
							extra + len_ret,
							buflen_from_user -
							len_ret);
#endif				/* defined(WL_IW_USE_ISCAN) */
	} else {
		list = (wl_scan_results_t *) g_scan;
		len_ret =
		    (__u16) wl_iw_get_scan_prep(list, info, extra,
						buflen_from_user);
	}

#if defined(WL_IW_USE_ISCAN)
	g_scan_specified_ssid = 0;
#endif
	if ((len_ret + WE_ADD_EVENT_FIX) < buflen_from_user)
		len = len_ret;

	dwrq->length = len;
	dwrq->flags = 0;

	WL_TRACE(("%s return to WE %d bytes APs=%d\n", __func__,
		  dwrq->length, list->count));
	return 0;
}

#if defined(WL_IW_USE_ISCAN)
static int
wl_iw_iscan_get_scan(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_point *dwrq, char *extra)
{
	wl_scan_results_t *list;
	struct iw_event iwe;
	wl_bss_info_t *bi = NULL;
	int ii, j;
	int apcnt;
	char *event = extra, *end = extra + dwrq->length, *value;
	iscan_info_t *iscan = g_iscan;
	iscan_buf_t *p_buf;
	u32 counter = 0;
	u8 channel;

	WL_TRACE(("%s %s buflen_from_user %d:\n", dev->name, __func__,
		  dwrq->length));

	if (!extra) {
		WL_TRACE(("%s: INVALID SIOCGIWSCAN GET bad parameter\n",
			  dev->name));
		return -EINVAL;
	}

	if ((!iscan) || (!iscan->sysioc_tsk)) {
		WL_ERROR(("%ssysioc_tsk\n", __func__));
		return wl_iw_get_scan(dev, info, dwrq, extra);
	}

	if (iscan->iscan_state == ISCAN_STATE_SCANING) {
		WL_TRACE(("%s: SIOCGIWSCAN GET still scanning\n", dev->name));
		return -EAGAIN;
	}

	WL_TRACE(("%s: SIOCGIWSCAN GET broadcast results\n", dev->name));
	apcnt = 0;
	p_buf = iscan->list_hdr;
	while (p_buf != iscan->list_cur) {
		list = &((wl_iscan_results_t *) p_buf->iscan_buf)->results;

		counter += list->count;

		if (list->version != WL_BSS_INFO_VERSION) {
			WL_ERROR(("%s : list->version %d != "
				"WL_BSS_INFO_VERSION\n",
				__func__, list->version));
			return -EINVAL;
		}

		bi = NULL;
		for (ii = 0; ii < list->count && apcnt < IW_MAX_AP;
		     apcnt++, ii++) {
			bi = bi ? (wl_bss_info_t *)((unsigned long)bi +
						     dtoh32(bi->length)) :
			    list->bss_info;
			ASSERT(((unsigned long)bi + dtoh32(bi->length)) <=
			       ((unsigned long)list + WLC_IW_ISCAN_MAXLEN));

			if (event + ETHER_ADDR_LEN + bi->SSID_len +
			    IW_EV_UINT_LEN + IW_EV_FREQ_LEN + IW_EV_QUAL_LEN >=
			    end)
				return -E2BIG;
			iwe.cmd = SIOCGIWAP;
			iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
			memcpy(iwe.u.ap_addr.sa_data, &bi->BSSID,
			       ETHER_ADDR_LEN);
			event =
			    IWE_STREAM_ADD_EVENT(info, event, end, &iwe,
						 IW_EV_ADDR_LEN);

			iwe.u.data.length = dtoh32(bi->SSID_len);
			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.flags = 1;
			event =
			    IWE_STREAM_ADD_POINT(info, event, end, &iwe,
						 bi->SSID);

			if (dtoh16(bi->capability) &
			    (DOT11_CAP_ESS | DOT11_CAP_IBSS)) {
				iwe.cmd = SIOCGIWMODE;
				if (dtoh16(bi->capability) & DOT11_CAP_ESS)
					iwe.u.mode = IW_MODE_INFRA;
				else
					iwe.u.mode = IW_MODE_ADHOC;
				event =
				    IWE_STREAM_ADD_EVENT(info, event, end, &iwe,
							 IW_EV_UINT_LEN);
			}

			iwe.cmd = SIOCGIWFREQ;
			channel =
			    (bi->ctl_ch ==
			     0) ? CHSPEC_CHANNEL(bi->chanspec) : bi->ctl_ch;
			iwe.u.freq.m =
			    wf_channel2mhz(channel,
					   channel <=
					   CH_MAX_2G_CHANNEL ?
					   WF_CHAN_FACTOR_2_4_G :
					   WF_CHAN_FACTOR_5_G);
			iwe.u.freq.e = 6;
			event =
			    IWE_STREAM_ADD_EVENT(info, event, end, &iwe,
						 IW_EV_FREQ_LEN);

			iwe.cmd = IWEVQUAL;
			iwe.u.qual.qual = rssi_to_qual(dtoh16(bi->RSSI));
			iwe.u.qual.level = 0x100 + dtoh16(bi->RSSI);
			iwe.u.qual.noise = 0x100 + bi->phy_noise;
			event =
			    IWE_STREAM_ADD_EVENT(info, event, end, &iwe,
						 IW_EV_QUAL_LEN);

			wl_iw_handle_scanresults_ies(&event, end, info, bi);

			iwe.cmd = SIOCGIWENCODE;
			if (dtoh16(bi->capability) & DOT11_CAP_PRIVACY)
				iwe.u.data.flags =
				    IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
			else
				iwe.u.data.flags = IW_ENCODE_DISABLED;
			iwe.u.data.length = 0;
			event =
			    IWE_STREAM_ADD_POINT(info, event, end, &iwe,
						 (char *)event);

			if (bi->rateset.count) {
				if (event + IW_MAX_BITRATES * IW_EV_PARAM_LEN >=
				    end)
					return -E2BIG;

				value = event + IW_EV_LCP_LEN;
				iwe.cmd = SIOCGIWRATE;
				iwe.u.bitrate.fixed = iwe.u.bitrate.disabled =
				    0;
				for (j = 0;
				     j < bi->rateset.count
				     && j < IW_MAX_BITRATES; j++) {
					iwe.u.bitrate.value =
					    (bi->rateset.rates[j] & 0x7f) *
					    500000;
					value =
					    IWE_STREAM_ADD_VALUE(info, event,
						 value, end,
						 &iwe,
						 IW_EV_PARAM_LEN);
				}
				event = value;
			}
		}
		p_buf = p_buf->next;
	}

	dwrq->length = event - extra;
	dwrq->flags = 0;

	WL_TRACE(("%s return to WE %d bytes APs=%d\n", __func__,
		  dwrq->length, counter));

	if (!dwrq->length)
		return -EAGAIN;

	return 0;
}
#endif				/* defined(WL_IW_USE_ISCAN) */

static int
wl_iw_set_essid(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *dwrq, char *extra)
{
	int error;
	wl_join_params_t join_params;
	int join_params_size;

	WL_TRACE(("%s: SIOCSIWESSID\n", dev->name));

	if (g_set_essid_before_scan)
		return -EAGAIN;

	memset(&g_ssid, 0, sizeof(g_ssid));

	CHECK_EXTRA_FOR_NULL(extra);

	if (dwrq->length && extra) {
#if WIRELESS_EXT > 20
		g_ssid.SSID_len = min_t(size_t, sizeof(g_ssid.SSID),
					dwrq->length);
#else
		g_ssid.SSID_len = min_t(size_t, sizeof(g_ssid.SSID),
					dwrq->length - 1);
#endif
		memcpy(g_ssid.SSID, extra, g_ssid.SSID_len);
	} else {
		g_ssid.SSID_len = 0;
	}
	g_ssid.SSID_len = htod32(g_ssid.SSID_len);

	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	memcpy(&join_params.ssid.SSID, g_ssid.SSID, g_ssid.SSID_len);
	join_params.ssid.SSID_len = htod32(g_ssid.SSID_len);
	memcpy(&join_params.params.bssid, &ether_bcast, ETHER_ADDR_LEN);

	wl_iw_ch_to_chanspec(g_wl_iw_params.target_channel, &join_params,
			     &join_params_size);

	error = dev_wlc_ioctl(dev, WLC_SET_SSID, &join_params,
				join_params_size);
	if (error)
		WL_ERROR(("Invalid ioctl data=%d\n", error));

	if (g_ssid.SSID_len) {
		WL_TRACE(("%s: join SSID=%s ch=%d\n", __func__,
			  g_ssid.SSID, g_wl_iw_params.target_channel));
	}
	return 0;
}

static int
wl_iw_get_essid(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *dwrq, char *extra)
{
	wlc_ssid_t ssid;
	int error;

	WL_TRACE(("%s: SIOCGIWESSID\n", dev->name));

	if (!extra)
		return -EINVAL;

	error = dev_wlc_ioctl(dev, WLC_GET_SSID, &ssid, sizeof(ssid));
	if (error) {
		WL_ERROR(("Error getting the SSID\n"));
		return error;
	}

	ssid.SSID_len = dtoh32(ssid.SSID_len);

	memcpy(extra, ssid.SSID, ssid.SSID_len);

	dwrq->length = ssid.SSID_len;

	dwrq->flags = 1;

	return 0;
}

static int
wl_iw_set_nick(struct net_device *dev,
	       struct iw_request_info *info, struct iw_point *dwrq, char *extra)
{
	wl_iw_t *iw = *(wl_iw_t **) netdev_priv(dev);

	WL_TRACE(("%s: SIOCSIWNICKN\n", dev->name));

	if (!extra)
		return -EINVAL;

	if (dwrq->length > sizeof(iw->nickname))
		return -E2BIG;

	memcpy(iw->nickname, extra, dwrq->length);
	iw->nickname[dwrq->length - 1] = '\0';

	return 0;
}

static int
wl_iw_get_nick(struct net_device *dev,
	       struct iw_request_info *info, struct iw_point *dwrq, char *extra)
{
	wl_iw_t *iw = *(wl_iw_t **) netdev_priv(dev);

	WL_TRACE(("%s: SIOCGIWNICKN\n", dev->name));

	if (!extra)
		return -EINVAL;

	strcpy(extra, iw->nickname);
	dwrq->length = strlen(extra) + 1;

	return 0;
}

static int
wl_iw_set_rate(struct net_device *dev,
	       struct iw_request_info *info, struct iw_param *vwrq, char *extra)
{
	wl_rateset_t rateset;
	int error, rate, i, error_bg, error_a;

	WL_TRACE(("%s: SIOCSIWRATE\n", dev->name));

	error = dev_wlc_ioctl(dev, WLC_GET_CURR_RATESET, &rateset,
				sizeof(rateset));
	if (error)
		return error;

	rateset.count = dtoh32(rateset.count);

	if (vwrq->value < 0)
		rate = rateset.rates[rateset.count - 1] & 0x7f;
	else if (vwrq->value < rateset.count)
		rate = rateset.rates[vwrq->value] & 0x7f;
	else
		rate = vwrq->value / 500000;

	if (vwrq->fixed) {
		error_bg = dev_wlc_intvar_set(dev, "bg_rate", rate);
		error_a = dev_wlc_intvar_set(dev, "a_rate", rate);

		if (error_bg && error_a)
			return error_bg | error_a;
	} else {
		error_bg = dev_wlc_intvar_set(dev, "bg_rate", 0);
		error_a = dev_wlc_intvar_set(dev, "a_rate", 0);

		if (error_bg && error_a)
			return error_bg | error_a;

		for (i = 0; i < rateset.count; i++)
			if ((rateset.rates[i] & 0x7f) > rate)
				break;
		rateset.count = htod32(i);

		error = dev_wlc_ioctl(dev, WLC_SET_RATESET, &rateset,
					sizeof(rateset));
		if (error)
			return error;
	}

	return 0;
}

static int
wl_iw_get_rate(struct net_device *dev,
	       struct iw_request_info *info, struct iw_param *vwrq, char *extra)
{
	int error, rate;

	WL_TRACE(("%s: SIOCGIWRATE\n", dev->name));

	error = dev_wlc_ioctl(dev, WLC_GET_RATE, &rate, sizeof(rate));
	if (error)
		return error;
	rate = dtoh32(rate);
	vwrq->value = rate * 500000;

	return 0;
}

static int
wl_iw_set_rts(struct net_device *dev,
	      struct iw_request_info *info, struct iw_param *vwrq, char *extra)
{
	int error, rts;

	WL_TRACE(("%s: SIOCSIWRTS\n", dev->name));

	if (vwrq->disabled)
		rts = DOT11_DEFAULT_RTS_LEN;
	else if (vwrq->value < 0 || vwrq->value > DOT11_DEFAULT_RTS_LEN)
		return -EINVAL;
	else
		rts = vwrq->value;

	error = dev_wlc_intvar_set(dev, "rtsthresh", rts);
	if (error)
		return error;

	return 0;
}

static int
wl_iw_get_rts(struct net_device *dev,
	      struct iw_request_info *info, struct iw_param *vwrq, char *extra)
{
	int error, rts;

	WL_TRACE(("%s: SIOCGIWRTS\n", dev->name));

	error = dev_wlc_intvar_get(dev, "rtsthresh", &rts);
	if (error)
		return error;

	vwrq->value = rts;
	vwrq->disabled = (rts >= DOT11_DEFAULT_RTS_LEN);
	vwrq->fixed = 1;

	return 0;
}

static int
wl_iw_set_frag(struct net_device *dev,
	       struct iw_request_info *info, struct iw_param *vwrq, char *extra)
{
	int error, frag;

	WL_TRACE(("%s: SIOCSIWFRAG\n", dev->name));

	if (vwrq->disabled)
		frag = DOT11_DEFAULT_FRAG_LEN;
	else if (vwrq->value < 0 || vwrq->value > DOT11_DEFAULT_FRAG_LEN)
		return -EINVAL;
	else
		frag = vwrq->value;

	error = dev_wlc_intvar_set(dev, "fragthresh", frag);
	if (error)
		return error;

	return 0;
}

static int
wl_iw_get_frag(struct net_device *dev,
	       struct iw_request_info *info, struct iw_param *vwrq, char *extra)
{
	int error, fragthreshold;

	WL_TRACE(("%s: SIOCGIWFRAG\n", dev->name));

	error = dev_wlc_intvar_get(dev, "fragthresh", &fragthreshold);
	if (error)
		return error;

	vwrq->value = fragthreshold;
	vwrq->disabled = (fragthreshold >= DOT11_DEFAULT_FRAG_LEN);
	vwrq->fixed = 1;

	return 0;
}

static int
wl_iw_set_txpow(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *vwrq, char *extra)
{
	int error, disable;
	u16 txpwrmw;
	WL_TRACE(("%s: SIOCSIWTXPOW\n", dev->name));

	disable = vwrq->disabled ? WL_RADIO_SW_DISABLE : 0;
	disable += WL_RADIO_SW_DISABLE << 16;

	disable = htod32(disable);
	error = dev_wlc_ioctl(dev, WLC_SET_RADIO, &disable, sizeof(disable));
	if (error)
		return error;

	if (disable & WL_RADIO_SW_DISABLE)
		return 0;

	if (!(vwrq->flags & IW_TXPOW_MWATT))
		return -EINVAL;

	if (vwrq->value < 0)
		return 0;

	if (vwrq->value > 0xffff)
		txpwrmw = 0xffff;
	else
		txpwrmw = (u16) vwrq->value;

	error =
	    dev_wlc_intvar_set(dev, "qtxpower", (int)(bcm_mw_to_qdbm(txpwrmw)));
	return error;
}

static int
wl_iw_get_txpow(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *vwrq, char *extra)
{
	int error, disable, txpwrdbm;
	u8 result;

	WL_TRACE(("%s: SIOCGIWTXPOW\n", dev->name));

	error = dev_wlc_ioctl(dev, WLC_GET_RADIO, &disable, sizeof(disable));
	if (error)
		return error;

	error = dev_wlc_intvar_get(dev, "qtxpower", &txpwrdbm);
	if (error)
		return error;

	disable = dtoh32(disable);
	result = (u8) (txpwrdbm & ~WL_TXPWR_OVERRIDE);
	vwrq->value = (s32) bcm_qdbm_to_mw(result);
	vwrq->fixed = 0;
	vwrq->disabled =
	    (disable & (WL_RADIO_SW_DISABLE | WL_RADIO_HW_DISABLE)) ? 1 : 0;
	vwrq->flags = IW_TXPOW_MWATT;

	return 0;
}

#if WIRELESS_EXT > 10
static int
wl_iw_set_retry(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *vwrq, char *extra)
{
	int error, lrl, srl;

	WL_TRACE(("%s: SIOCSIWRETRY\n", dev->name));

	if (vwrq->disabled || (vwrq->flags & IW_RETRY_LIFETIME))
		return -EINVAL;

	if (vwrq->flags & IW_RETRY_LIMIT) {

#if WIRELESS_EXT > 20
		if ((vwrq->flags & IW_RETRY_LONG)
		    || (vwrq->flags & IW_RETRY_MAX)
		    || !((vwrq->flags & IW_RETRY_SHORT)
			 || (vwrq->flags & IW_RETRY_MIN))) {
#else
		if ((vwrq->flags & IW_RETRY_MAX)
		    || !(vwrq->flags & IW_RETRY_MIN)) {
#endif
			lrl = htod32(vwrq->value);
			error = dev_wlc_ioctl(dev, WLC_SET_LRL, &lrl,
						sizeof(lrl));
			if (error)
				return error;
		}
#if WIRELESS_EXT > 20
		if ((vwrq->flags & IW_RETRY_SHORT)
		    || (vwrq->flags & IW_RETRY_MIN)
		    || !((vwrq->flags & IW_RETRY_LONG)
			 || (vwrq->flags & IW_RETRY_MAX))) {
#else
		if ((vwrq->flags & IW_RETRY_MIN)
		    || !(vwrq->flags & IW_RETRY_MAX)) {
#endif
			srl = htod32(vwrq->value);
			error = dev_wlc_ioctl(dev, WLC_SET_SRL, &srl,
						sizeof(srl));
			if (error)
				return error;
		}
	}
	return 0;
}

static int
wl_iw_get_retry(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *vwrq, char *extra)
{
	int error, lrl, srl;

	WL_TRACE(("%s: SIOCGIWRETRY\n", dev->name));

	vwrq->disabled = 0;

	if ((vwrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME)
		return -EINVAL;

	error = dev_wlc_ioctl(dev, WLC_GET_LRL, &lrl, sizeof(lrl));
	if (error)
		return error;

	error = dev_wlc_ioctl(dev, WLC_GET_SRL, &srl, sizeof(srl));
	if (error)
		return error;

	lrl = dtoh32(lrl);
	srl = dtoh32(srl);

	if (vwrq->flags & IW_RETRY_MAX) {
		vwrq->flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
		vwrq->value = lrl;
	} else {
		vwrq->flags = IW_RETRY_LIMIT;
		vwrq->value = srl;
		if (srl != lrl)
			vwrq->flags |= IW_RETRY_MIN;
	}

	return 0;
}
#endif				/* WIRELESS_EXT > 10 */

static int
wl_iw_set_encode(struct net_device *dev,
		 struct iw_request_info *info,
		 struct iw_point *dwrq, char *extra)
{
	wl_wsec_key_t key;
	int error, val, wsec;

	WL_TRACE(("%s: SIOCSIWENCODE\n", dev->name));

	memset(&key, 0, sizeof(key));

	if ((dwrq->flags & IW_ENCODE_INDEX) == 0) {
		for (key.index = 0; key.index < DOT11_MAX_DEFAULT_KEYS;
		     key.index++) {
			val = htod32(key.index);
			error = dev_wlc_ioctl(dev, WLC_GET_KEY_PRIMARY, &val,
						sizeof(val));
			if (error)
				return error;
			val = dtoh32(val);
			if (val)
				break;
		}
		if (key.index == DOT11_MAX_DEFAULT_KEYS)
			key.index = 0;
	} else {
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;
		if (key.index >= DOT11_MAX_DEFAULT_KEYS)
			return -EINVAL;
	}

	if (!extra || !dwrq->length || (dwrq->flags & IW_ENCODE_NOKEY)) {
		val = htod32(key.index);
		error = dev_wlc_ioctl(dev, WLC_SET_KEY_PRIMARY, &val,
					sizeof(val));
		if (error)
			return error;
	} else {
		key.len = dwrq->length;

		if (dwrq->length > sizeof(key.data))
			return -EINVAL;

		memcpy(key.data, extra, dwrq->length);

		key.flags = WL_PRIMARY_KEY;
		switch (key.len) {
		case WEP1_KEY_SIZE:
			key.algo = CRYPTO_ALGO_WEP1;
			break;
		case WEP128_KEY_SIZE:
			key.algo = CRYPTO_ALGO_WEP128;
			break;
		case TKIP_KEY_SIZE:
			key.algo = CRYPTO_ALGO_TKIP;
			break;
		case AES_KEY_SIZE:
			key.algo = CRYPTO_ALGO_AES_CCM;
			break;
		default:
			return -EINVAL;
		}

		swap_key_from_BE(&key);
		error = dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
		if (error)
			return error;
	}

	val = (dwrq->flags & IW_ENCODE_DISABLED) ? 0 : WEP_ENABLED;

	error = dev_wlc_intvar_get(dev, "wsec", &wsec);
	if (error)
		return error;

	wsec &= ~(WEP_ENABLED);
	wsec |= val;

	error = dev_wlc_intvar_set(dev, "wsec", wsec);
	if (error)
		return error;

	val = (dwrq->flags & IW_ENCODE_RESTRICTED) ? 1 : 0;
	val = htod32(val);
	error = dev_wlc_ioctl(dev, WLC_SET_AUTH, &val, sizeof(val));
	if (error)
		return error;

	return 0;
}

static int
wl_iw_get_encode(struct net_device *dev,
		 struct iw_request_info *info,
		 struct iw_point *dwrq, char *extra)
{
	wl_wsec_key_t key;
	int error, val, wsec, auth;

	WL_TRACE(("%s: SIOCGIWENCODE\n", dev->name));

	memset(&key, 0, sizeof(wl_wsec_key_t));

	if ((dwrq->flags & IW_ENCODE_INDEX) == 0) {
		for (key.index = 0; key.index < DOT11_MAX_DEFAULT_KEYS;
		     key.index++) {
			val = key.index;
			error = dev_wlc_ioctl(dev, WLC_GET_KEY_PRIMARY, &val,
						sizeof(val));
			if (error)
				return error;
			val = dtoh32(val);
			if (val)
				break;
		}
	} else
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	if (key.index >= DOT11_MAX_DEFAULT_KEYS)
		key.index = 0;

	error = dev_wlc_ioctl(dev, WLC_GET_WSEC, &wsec, sizeof(wsec));
	if (error)
		return error;

	error = dev_wlc_ioctl(dev, WLC_GET_AUTH, &auth, sizeof(auth));
	if (error)
		return error;

	swap_key_to_BE(&key);

	wsec = dtoh32(wsec);
	auth = dtoh32(auth);
	dwrq->length = min_t(u16, DOT11_MAX_KEY_SIZE, key.len);

	dwrq->flags = key.index + 1;
	if (!(wsec & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED)))
		dwrq->flags |= IW_ENCODE_DISABLED;

	if (auth)
		dwrq->flags |= IW_ENCODE_RESTRICTED;

	if (dwrq->length && extra)
		memcpy(extra, key.data, dwrq->length);

	return 0;
}

static int
wl_iw_set_power(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *vwrq, char *extra)
{
	int error, pm;

	WL_TRACE(("%s: SIOCSIWPOWER\n", dev->name));

	pm = vwrq->disabled ? PM_OFF : PM_MAX;

	pm = htod32(pm);
	error = dev_wlc_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm));
	if (error)
		return error;

	return 0;
}

static int
wl_iw_get_power(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *vwrq, char *extra)
{
	int error, pm;

	WL_TRACE(("%s: SIOCGIWPOWER\n", dev->name));

	error = dev_wlc_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm));
	if (error)
		return error;

	pm = dtoh32(pm);
	vwrq->disabled = pm ? 0 : 1;
	vwrq->flags = IW_POWER_ALL_R;

	return 0;
}

#if WIRELESS_EXT > 17
static int
wl_iw_set_wpaie(struct net_device *dev,
		struct iw_request_info *info, struct iw_point *iwp, char *extra)
{

	WL_TRACE(("%s: SIOCSIWGENIE\n", dev->name));

	CHECK_EXTRA_FOR_NULL(extra);

	dev_wlc_bufvar_set(dev, "wpaie", extra, iwp->length);

	return 0;
}

static int
wl_iw_get_wpaie(struct net_device *dev,
		struct iw_request_info *info, struct iw_point *iwp, char *extra)
{
	WL_TRACE(("%s: SIOCGIWGENIE\n", dev->name));
	iwp->length = 64;
	dev_wlc_bufvar_get(dev, "wpaie", extra, iwp->length);
	return 0;
}

static int
wl_iw_set_encodeext(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_point *dwrq, char *extra)
{
	wl_wsec_key_t key;
	int error;
	struct iw_encode_ext *iwe;

	WL_TRACE(("%s: SIOCSIWENCODEEXT\n", dev->name));

	CHECK_EXTRA_FOR_NULL(extra);

	memset(&key, 0, sizeof(key));
	iwe = (struct iw_encode_ext *)extra;

	if (dwrq->flags & IW_ENCODE_DISABLED) {

	}

	key.index = 0;
	if (dwrq->flags & IW_ENCODE_INDEX)
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	key.len = iwe->key_len;

	if (!ETHER_ISMULTI(iwe->addr.sa_data))
		bcopy((void *)&iwe->addr.sa_data, (char *)&key.ea,
		      ETHER_ADDR_LEN);

	if (key.len == 0) {
		if (iwe->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
			WL_WSEC(("Changing the the primary Key to %d\n",
				 key.index));
			key.index = htod32(key.index);
			error = dev_wlc_ioctl(dev, WLC_SET_KEY_PRIMARY,
					      &key.index, sizeof(key.index));
			if (error)
				return error;
		} else {
			swap_key_from_BE(&key);
			dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
		}
	} else {
		if (iwe->key_len > sizeof(key.data))
			return -EINVAL;

		WL_WSEC(("Setting the key index %d\n", key.index));
		if (iwe->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
			WL_WSEC(("key is a Primary Key\n"));
			key.flags = WL_PRIMARY_KEY;
		}

		bcopy((void *)iwe->key, key.data, iwe->key_len);

		if (iwe->alg == IW_ENCODE_ALG_TKIP) {
			u8 keybuf[8];
			bcopy(&key.data[24], keybuf, sizeof(keybuf));
			bcopy(&key.data[16], &key.data[24], sizeof(keybuf));
			bcopy(keybuf, &key.data[16], sizeof(keybuf));
		}

		if (iwe->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
			unsigned char *ivptr;
			ivptr = (unsigned char *) iwe->rx_seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
			    (ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = true;
		}

		switch (iwe->alg) {
		case IW_ENCODE_ALG_NONE:
			key.algo = CRYPTO_ALGO_OFF;
			break;
		case IW_ENCODE_ALG_WEP:
			if (iwe->key_len == WEP1_KEY_SIZE)
				key.algo = CRYPTO_ALGO_WEP1;
			else
				key.algo = CRYPTO_ALGO_WEP128;
			break;
		case IW_ENCODE_ALG_TKIP:
			key.algo = CRYPTO_ALGO_TKIP;
			break;
		case IW_ENCODE_ALG_CCMP:
			key.algo = CRYPTO_ALGO_AES_CCM;
			break;
		default:
			break;
		}
		swap_key_from_BE(&key);

		dhd_wait_pend8021x(dev);

		error = dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
		if (error)
			return error;
	}
	return 0;
}

#if WIRELESS_EXT > 17
struct {
	pmkid_list_t pmkids;
	pmkid_t foo[MAXPMKID - 1];
} pmkid_list;

static int
wl_iw_set_pmksa(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *vwrq, char *extra)
{
	struct iw_pmksa *iwpmksa;
	uint i;
	int ret = 0;

	WL_WSEC(("%s: SIOCSIWPMKSA\n", dev->name));

	CHECK_EXTRA_FOR_NULL(extra);

	iwpmksa = (struct iw_pmksa *)extra;

	if (iwpmksa->cmd == IW_PMKSA_FLUSH) {
		WL_WSEC(("wl_iw_set_pmksa - IW_PMKSA_FLUSH\n"));
		memset((char *)&pmkid_list, 0, sizeof(pmkid_list));
	}

	else if (iwpmksa->cmd == IW_PMKSA_REMOVE) {
		{
			pmkid_list_t pmkid, *pmkidptr;
			uint j;
			pmkidptr = &pmkid;

			bcopy(&iwpmksa->bssid.sa_data[0],
			      &pmkidptr->pmkid[0].BSSID, ETHER_ADDR_LEN);
			bcopy(&iwpmksa->pmkid[0], &pmkidptr->pmkid[0].PMKID,
			      WPA2_PMKID_LEN);

			WL_WSEC(("wl_iw_set_pmksa:IW_PMKSA_REMOVE:PMKID: "
				"%pM = ", &pmkidptr->pmkid[0].BSSID));
			for (j = 0; j < WPA2_PMKID_LEN; j++)
				WL_WSEC(("%02x ", pmkidptr->pmkid[0].PMKID[j]));
			WL_WSEC(("\n"));
		}

		for (i = 0; i < pmkid_list.pmkids.npmkid; i++)
			if (!memcmp
			    (&iwpmksa->bssid.sa_data[0],
			     &pmkid_list.pmkids.pmkid[i].BSSID, ETHER_ADDR_LEN))
				break;

		if ((pmkid_list.pmkids.npmkid > 0)
		    && (i < pmkid_list.pmkids.npmkid)) {
			memset(&pmkid_list.pmkids.pmkid[i], 0, sizeof(pmkid_t));
			for (; i < (pmkid_list.pmkids.npmkid - 1); i++) {
				bcopy(&pmkid_list.pmkids.pmkid[i + 1].BSSID,
				      &pmkid_list.pmkids.pmkid[i].BSSID,
				      ETHER_ADDR_LEN);
				bcopy(&pmkid_list.pmkids.pmkid[i + 1].PMKID,
				      &pmkid_list.pmkids.pmkid[i].PMKID,
				      WPA2_PMKID_LEN);
			}
			pmkid_list.pmkids.npmkid--;
		} else
			ret = -EINVAL;
	}

	else if (iwpmksa->cmd == IW_PMKSA_ADD) {
		for (i = 0; i < pmkid_list.pmkids.npmkid; i++)
			if (!memcmp
			    (&iwpmksa->bssid.sa_data[0],
			     &pmkid_list.pmkids.pmkid[i].BSSID, ETHER_ADDR_LEN))
				break;
		if (i < MAXPMKID) {
			bcopy(&iwpmksa->bssid.sa_data[0],
			      &pmkid_list.pmkids.pmkid[i].BSSID,
			      ETHER_ADDR_LEN);
			bcopy(&iwpmksa->pmkid[0],
			      &pmkid_list.pmkids.pmkid[i].PMKID,
			      WPA2_PMKID_LEN);
			if (i == pmkid_list.pmkids.npmkid)
				pmkid_list.pmkids.npmkid++;
		} else
			ret = -EINVAL;
		{
			uint j;
			uint k;
			k = pmkid_list.pmkids.npmkid;
			WL_WSEC(("wl_iw_set_pmksa,IW_PMKSA_ADD - PMKID: %pM = ",
				&pmkid_list.pmkids.pmkid[k].BSSID));
			for (j = 0; j < WPA2_PMKID_LEN; j++)
				WL_WSEC(("%02x ",
					 pmkid_list.pmkids.pmkid[k].PMKID[j]));
			WL_WSEC(("\n"));
		}
	}
	WL_WSEC(("PRINTING pmkid LIST - No of elements %d\n",
		 pmkid_list.pmkids.npmkid));
	for (i = 0; i < pmkid_list.pmkids.npmkid; i++) {
		uint j;
		WL_WSEC(("PMKID[%d]: %pM = ", i,
			&pmkid_list.pmkids.pmkid[i].BSSID));
		for (j = 0; j < WPA2_PMKID_LEN; j++)
			WL_WSEC(("%02x ", pmkid_list.pmkids.pmkid[i].PMKID[j]));
		WL_WSEC(("\n"));
	}
	WL_WSEC(("\n"));

	if (!ret)
		ret = dev_wlc_bufvar_set(dev, "pmkid_info", (char *)&pmkid_list,
					 sizeof(pmkid_list));
	return ret;
}
#endif				/* WIRELESS_EXT > 17 */

static int
wl_iw_get_encodeext(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_param *vwrq, char *extra)
{
	WL_TRACE(("%s: SIOCGIWENCODEEXT\n", dev->name));
	return 0;
}

static int
wl_iw_set_wpaauth(struct net_device *dev,
		  struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	int error = 0;
	int paramid;
	int paramval;
	int val = 0;
	wl_iw_t *iw = *(wl_iw_t **) netdev_priv(dev);

	WL_TRACE(("%s: SIOCSIWAUTH\n", dev->name));

	paramid = vwrq->flags & IW_AUTH_INDEX;
	paramval = vwrq->value;

	WL_TRACE(("%s: SIOCSIWAUTH, paramid = 0x%0x, paramval = 0x%0x\n",
		  dev->name, paramid, paramval));

	switch (paramid) {
	case IW_AUTH_WPA_VERSION:
		if (paramval & IW_AUTH_WPA_VERSION_DISABLED)
			val = WPA_AUTH_DISABLED;
		else if (paramval & (IW_AUTH_WPA_VERSION_WPA))
			val = WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED;
		else if (paramval & IW_AUTH_WPA_VERSION_WPA2)
			val = WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED;
		WL_INFORM(("%s: %d: setting wpa_auth to 0x%0x\n", __func__,
			   __LINE__, val));
		error = dev_wlc_intvar_set(dev, "wpa_auth", val);
		if (error)
			return error;
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
		if (paramval & (IW_AUTH_CIPHER_WEP40 | IW_AUTH_CIPHER_WEP104))
			val = WEP_ENABLED;
		if (paramval & IW_AUTH_CIPHER_TKIP)
			val = TKIP_ENABLED;
		if (paramval & IW_AUTH_CIPHER_CCMP)
			val = AES_ENABLED;

		if (paramid == IW_AUTH_CIPHER_PAIRWISE) {
			iw->pwsec = val;
			val |= iw->gwsec;
		} else {
			iw->gwsec = val;
			val |= iw->pwsec;
		}

		if (iw->privacy_invoked && !val) {
			WL_WSEC(("%s: %s: 'Privacy invoked' true but clearing "
				"wsec, assuming " "we're a WPS enrollee\n",
				dev->name, __func__));
			error = dev_wlc_intvar_set(dev, "is_WPS_enrollee",
							true);
			if (error) {
				WL_WSEC(("Failed to set is_WPS_enrollee\n"));
				return error;
			}
		} else if (val) {
			error = dev_wlc_intvar_set(dev, "is_WPS_enrollee",
							false);
			if (error) {
				WL_WSEC(("Failed to clear is_WPS_enrollee\n"));
				return error;
			}
		}

		error = dev_wlc_intvar_set(dev, "wsec", val);
		if (error)
			return error;

		break;

	case IW_AUTH_KEY_MGMT:
		error = dev_wlc_intvar_get(dev, "wpa_auth", &val);
		if (error)
			return error;

		if (val & (WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED)) {
			if (paramval & IW_AUTH_KEY_MGMT_PSK)
				val = WPA_AUTH_PSK;
			else
				val = WPA_AUTH_UNSPECIFIED;
		} else if (val & (WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED)) {
			if (paramval & IW_AUTH_KEY_MGMT_PSK)
				val = WPA2_AUTH_PSK;
			else
				val = WPA2_AUTH_UNSPECIFIED;
		}
		WL_INFORM(("%s: %d: setting wpa_auth to %d\n", __func__,
			   __LINE__, val));
		error = dev_wlc_intvar_set(dev, "wpa_auth", val);
		if (error)
			return error;

		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		dev_wlc_bufvar_set(dev, "tkip_countermeasures",
				   (char *)&paramval, 1);
		break;

	case IW_AUTH_80211_AUTH_ALG:
		WL_INFORM(("Setting the D11auth %d\n", paramval));
		if (paramval == IW_AUTH_ALG_OPEN_SYSTEM)
			val = 0;
		else if (paramval == IW_AUTH_ALG_SHARED_KEY)
			val = 1;
		else if (paramval ==
			 (IW_AUTH_ALG_OPEN_SYSTEM | IW_AUTH_ALG_SHARED_KEY))
			val = 2;
		else
			error = 1;
		if (!error) {
			error = dev_wlc_intvar_set(dev, "auth", val);
			if (error)
				return error;
		}
		break;

	case IW_AUTH_WPA_ENABLED:
		if (paramval == 0) {
			iw->pwsec = 0;
			iw->gwsec = 0;
			error = dev_wlc_intvar_get(dev, "wsec", &val);
			if (error)
				return error;
			if (val & (TKIP_ENABLED | AES_ENABLED)) {
				val &= ~(TKIP_ENABLED | AES_ENABLED);
				dev_wlc_intvar_set(dev, "wsec", val);
			}
			val = 0;
			WL_INFORM(("%s: %d: setting wpa_auth to %d\n",
				   __func__, __LINE__, val));
			dev_wlc_intvar_set(dev, "wpa_auth", 0);
			return error;
		}
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		dev_wlc_bufvar_set(dev, "wsec_restrict", (char *)&paramval, 1);
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		dev_wlc_bufvar_set(dev, "rx_unencrypted_eapol",
				   (char *)&paramval, 1);
		break;

#if WIRELESS_EXT > 17
	case IW_AUTH_ROAMING_CONTROL:
		WL_INFORM(("%s: IW_AUTH_ROAMING_CONTROL\n", __func__));
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		{
			int wsec;

			if (paramval == 0) {
				iw->privacy_invoked = false;
				error = dev_wlc_intvar_set(dev,
						"is_WPS_enrollee", false);
				if (error) {
					WL_WSEC(("Failed to clear iovar "
						"is_WPS_enrollee\n"));
					return error;
				}
			} else {
				iw->privacy_invoked = true;
				error = dev_wlc_intvar_get(dev, "wsec", &wsec);
				if (error)
					return error;

				if (!(IW_WSEC_ENABLED(wsec))) {
					error = dev_wlc_intvar_set(dev,
							"is_WPS_enrollee",
							true);
					if (error) {
						WL_WSEC(("Failed to set iovar "
						"is_WPS_enrollee\n"));
						return error;
					}
				} else {
					error = dev_wlc_intvar_set(dev,
							"is_WPS_enrollee",
							false);
					if (error) {
						WL_WSEC(("Failed to clear "
							"is_WPS_enrollee\n"));
						return error;
					}
				}
			}
			break;
		}
#endif				/* WIRELESS_EXT > 17 */
	default:
		break;
	}
	return 0;
}

#define VAL_PSK(_val) (((_val) & WPA_AUTH_PSK) || ((_val) & WPA2_AUTH_PSK))

static int
wl_iw_get_wpaauth(struct net_device *dev,
		  struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	int error;
	int paramid;
	int paramval = 0;
	int val;
	wl_iw_t *iw = *(wl_iw_t **) netdev_priv(dev);

	WL_TRACE(("%s: SIOCGIWAUTH\n", dev->name));

	paramid = vwrq->flags & IW_AUTH_INDEX;

	switch (paramid) {
	case IW_AUTH_WPA_VERSION:
		error = dev_wlc_intvar_get(dev, "wpa_auth", &val);
		if (error)
			return error;
		if (val & (WPA_AUTH_NONE | WPA_AUTH_DISABLED))
			paramval = IW_AUTH_WPA_VERSION_DISABLED;
		else if (val & (WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED))
			paramval = IW_AUTH_WPA_VERSION_WPA;
		else if (val & (WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED))
			paramval = IW_AUTH_WPA_VERSION_WPA2;
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
		if (paramid == IW_AUTH_CIPHER_PAIRWISE)
			val = iw->pwsec;
		else
			val = iw->gwsec;

		paramval = 0;
		if (val) {
			if (val & WEP_ENABLED)
				paramval |=
				    (IW_AUTH_CIPHER_WEP40 |
				     IW_AUTH_CIPHER_WEP104);
			if (val & TKIP_ENABLED)
				paramval |= (IW_AUTH_CIPHER_TKIP);
			if (val & AES_ENABLED)
				paramval |= (IW_AUTH_CIPHER_CCMP);
		} else
			paramval = IW_AUTH_CIPHER_NONE;
		break;
	case IW_AUTH_KEY_MGMT:
		error = dev_wlc_intvar_get(dev, "wpa_auth", &val);
		if (error)
			return error;
		if (VAL_PSK(val))
			paramval = IW_AUTH_KEY_MGMT_PSK;
		else
			paramval = IW_AUTH_KEY_MGMT_802_1X;

		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		dev_wlc_bufvar_get(dev, "tkip_countermeasures",
				   (char *)&paramval, 1);
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		dev_wlc_bufvar_get(dev, "wsec_restrict", (char *)&paramval, 1);
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		dev_wlc_bufvar_get(dev, "rx_unencrypted_eapol",
				   (char *)&paramval, 1);
		break;

	case IW_AUTH_80211_AUTH_ALG:
		error = dev_wlc_intvar_get(dev, "auth", &val);
		if (error)
			return error;
		if (!val)
			paramval = IW_AUTH_ALG_OPEN_SYSTEM;
		else
			paramval = IW_AUTH_ALG_SHARED_KEY;
		break;
	case IW_AUTH_WPA_ENABLED:
		error = dev_wlc_intvar_get(dev, "wpa_auth", &val);
		if (error)
			return error;
		if (val)
			paramval = true;
		else
			paramval = false;
		break;
#if WIRELESS_EXT > 17
	case IW_AUTH_ROAMING_CONTROL:
		WL_ERROR(("%s: IW_AUTH_ROAMING_CONTROL\n", __func__));
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		paramval = iw->privacy_invoked;
		break;

#endif
	}
	vwrq->value = paramval;
	return 0;
}
#endif				/* WIRELESS_EXT > 17 */

static const iw_handler wl_iw_handler[] = {
	(iw_handler) wl_iw_config_commit,
	(iw_handler) wl_iw_get_name,
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) wl_iw_set_freq,
	(iw_handler) wl_iw_get_freq,
	(iw_handler) wl_iw_set_mode,
	(iw_handler) wl_iw_get_mode,
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) wl_iw_get_range,
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) wl_iw_set_spy,
	(iw_handler) wl_iw_get_spy,
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) wl_iw_set_wap,
	(iw_handler) wl_iw_get_wap,
#if WIRELESS_EXT > 17
	(iw_handler) wl_iw_mlme,
#else
	(iw_handler) NULL,
#endif
#if defined(WL_IW_USE_ISCAN)
	(iw_handler) wl_iw_iscan_get_aplist,
#else
	(iw_handler) wl_iw_get_aplist,
#endif
#if WIRELESS_EXT > 13
#if defined(WL_IW_USE_ISCAN)
	(iw_handler) wl_iw_iscan_set_scan,
	(iw_handler) wl_iw_iscan_get_scan,
#else
	(iw_handler) wl_iw_set_scan,
	(iw_handler) wl_iw_get_scan,
#endif
#else
	(iw_handler) NULL,
	(iw_handler) NULL,
#endif				/* WIRELESS_EXT > 13 */
	(iw_handler) wl_iw_set_essid,
	(iw_handler) wl_iw_get_essid,
	(iw_handler) wl_iw_set_nick,
	(iw_handler) wl_iw_get_nick,
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) wl_iw_set_rate,
	(iw_handler) wl_iw_get_rate,
	(iw_handler) wl_iw_set_rts,
	(iw_handler) wl_iw_get_rts,
	(iw_handler) wl_iw_set_frag,
	(iw_handler) wl_iw_get_frag,
	(iw_handler) wl_iw_set_txpow,
	(iw_handler) wl_iw_get_txpow,
#if WIRELESS_EXT > 10
	(iw_handler) wl_iw_set_retry,
	(iw_handler) wl_iw_get_retry,
#endif
	(iw_handler) wl_iw_set_encode,
	(iw_handler) wl_iw_get_encode,
	(iw_handler) wl_iw_set_power,
	(iw_handler) wl_iw_get_power,
#if WIRELESS_EXT > 17
	(iw_handler) NULL,
	(iw_handler) NULL,
	(iw_handler) wl_iw_set_wpaie,
	(iw_handler) wl_iw_get_wpaie,
	(iw_handler) wl_iw_set_wpaauth,
	(iw_handler) wl_iw_get_wpaauth,
	(iw_handler) wl_iw_set_encodeext,
	(iw_handler) wl_iw_get_encodeext,
	(iw_handler) wl_iw_set_pmksa,
#endif				/* WIRELESS_EXT > 17 */
};

#if WIRELESS_EXT > 12

const struct iw_handler_def wl_iw_handler_def = {
	.num_standard = ARRAY_SIZE(wl_iw_handler),
	.standard = (iw_handler *) wl_iw_handler,
	.num_private = 0,
	.num_private_args = 0,
	.private = 0,
	.private_args = 0,

#if WIRELESS_EXT >= 19
	.get_wireless_stats = dhd_get_wireless_stats,
#endif
};
#endif				/* WIRELESS_EXT > 12 */

int wl_iw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct iwreq *wrq = (struct iwreq *)rq;
	struct iw_request_info info;
	iw_handler handler;
	char *extra = NULL;
	int token_size = 1, max_tokens = 0, ret = 0;

	WL_TRACE(("\n%s, cmd:%x alled via dhd->do_ioctl()entry point\n",
		  __func__, cmd));
	if (cmd < SIOCIWFIRST ||
		IW_IOCTL_IDX(cmd) >= ARRAY_SIZE(wl_iw_handler)) {
		WL_ERROR(("%s: error in cmd=%x : out of range\n", __func__,
			cmd));
		return -EOPNOTSUPP;
	}

	handler = wl_iw_handler[IW_IOCTL_IDX(cmd)];
	if (!handler) {
		WL_ERROR(("%s: error in cmd=%x : not supported\n",
			__func__, cmd));
		return -EOPNOTSUPP;
	}

	switch (cmd) {

	case SIOCSIWESSID:
	case SIOCGIWESSID:
	case SIOCSIWNICKN:
	case SIOCGIWNICKN:
		max_tokens = IW_ESSID_MAX_SIZE + 1;
		break;

	case SIOCSIWENCODE:
	case SIOCGIWENCODE:
#if WIRELESS_EXT > 17
	case SIOCSIWENCODEEXT:
	case SIOCGIWENCODEEXT:
#endif
		max_tokens = wrq->u.data.length;
		break;

	case SIOCGIWRANGE:
		max_tokens = sizeof(struct iw_range) + 500;
		break;

	case SIOCGIWAPLIST:
		token_size =
		    sizeof(struct sockaddr) + sizeof(struct iw_quality);
		max_tokens = IW_MAX_AP;
		break;

#if WIRELESS_EXT > 13
	case SIOCGIWSCAN:
#if defined(WL_IW_USE_ISCAN)
		if (g_iscan)
			max_tokens = wrq->u.data.length;
		else
#endif
			max_tokens = IW_SCAN_MAX_DATA;
		break;
#endif				/* WIRELESS_EXT > 13 */

	case SIOCSIWSPY:
		token_size = sizeof(struct sockaddr);
		max_tokens = IW_MAX_SPY;
		break;

	case SIOCGIWSPY:
		token_size =
		    sizeof(struct sockaddr) + sizeof(struct iw_quality);
		max_tokens = IW_MAX_SPY;
		break;

#if WIRELESS_EXT > 17
	case SIOCSIWPMKSA:
	case SIOCSIWGENIE:
#endif
	case SIOCSIWPRIV:
		max_tokens = wrq->u.data.length;
		break;
	}

	if (max_tokens && wrq->u.data.pointer) {
		if (wrq->u.data.length > max_tokens) {
			WL_ERROR(("%s: error in cmd=%x wrq->u.data.length=%d "
			"> max_tokens=%d\n",
			__func__, cmd, wrq->u.data.length, max_tokens));
			return -E2BIG;
		}
		extra = kmalloc(max_tokens * token_size, GFP_KERNEL);
		if (!extra)
			return -ENOMEM;

		if (copy_from_user
		    (extra, wrq->u.data.pointer,
		     wrq->u.data.length * token_size)) {
			kfree(extra);
			return -EFAULT;
		}
	}

	info.cmd = cmd;
	info.flags = 0;

	ret = handler(dev, &info, &wrq->u, extra);

	if (extra) {
		if (copy_to_user
		    (wrq->u.data.pointer, extra,
		     wrq->u.data.length * token_size)) {
			kfree(extra);
			return -EFAULT;
		}

		kfree(extra);
	}

	return ret;
}

bool
wl_iw_conn_status_str(u32 event_type, u32 status, u32 reason,
		      char *stringBuf, uint buflen)
{
	typedef struct conn_fail_event_map_t {
		u32 inEvent;
		u32 inStatus;
		u32 inReason;
		const char *outName;
		const char *outCause;
	} conn_fail_event_map_t;

#define WL_IW_DONT_CARE	9999
	const conn_fail_event_map_t event_map[] = {
		{WLC_E_SET_SSID, WLC_E_STATUS_SUCCESS, WL_IW_DONT_CARE,
		 "Conn", "Success"},
		{WLC_E_SET_SSID, WLC_E_STATUS_NO_NETWORKS, WL_IW_DONT_CARE,
		 "Conn", "NoNetworks"},
		{WLC_E_SET_SSID, WLC_E_STATUS_FAIL, WL_IW_DONT_CARE,
		 "Conn", "ConfigMismatch"},
		{WLC_E_PRUNE, WL_IW_DONT_CARE, WLC_E_PRUNE_ENCR_MISMATCH,
		 "Conn", "EncrypMismatch"},
		{WLC_E_PRUNE, WL_IW_DONT_CARE, WLC_E_RSN_MISMATCH,
		 "Conn", "RsnMismatch"},
		{WLC_E_AUTH, WLC_E_STATUS_TIMEOUT, WL_IW_DONT_CARE,
		 "Conn", "AuthTimeout"},
		{WLC_E_AUTH, WLC_E_STATUS_FAIL, WL_IW_DONT_CARE,
		 "Conn", "AuthFail"},
		{WLC_E_AUTH, WLC_E_STATUS_NO_ACK, WL_IW_DONT_CARE,
		 "Conn", "AuthNoAck"},
		{WLC_E_REASSOC, WLC_E_STATUS_FAIL, WL_IW_DONT_CARE,
		 "Conn", "ReassocFail"},
		{WLC_E_REASSOC, WLC_E_STATUS_TIMEOUT, WL_IW_DONT_CARE,
		 "Conn", "ReassocTimeout"},
		{WLC_E_REASSOC, WLC_E_STATUS_ABORT, WL_IW_DONT_CARE,
		 "Conn", "ReassocAbort"},
		{WLC_E_PSK_SUP, WLC_SUP_KEYED, WL_IW_DONT_CARE,
		 "Sup", "ConnSuccess"},
		{WLC_E_PSK_SUP, WL_IW_DONT_CARE, WL_IW_DONT_CARE,
		 "Sup", "WpaHandshakeFail"},
		{WLC_E_DEAUTH_IND, WL_IW_DONT_CARE, WL_IW_DONT_CARE,
		 "Conn", "Deauth"},
		{WLC_E_DISASSOC_IND, WL_IW_DONT_CARE, WL_IW_DONT_CARE,
		 "Conn", "DisassocInd"},
		{WLC_E_DISASSOC, WL_IW_DONT_CARE, WL_IW_DONT_CARE,
		 "Conn", "Disassoc"}
	};

	const char *name = "";
	const char *cause = NULL;
	int i;

	for (i = 0; i < sizeof(event_map) / sizeof(event_map[0]); i++) {
		const conn_fail_event_map_t *row = &event_map[i];
		if (row->inEvent == event_type &&
		    (row->inStatus == status
		     || row->inStatus == WL_IW_DONT_CARE)
		    && (row->inReason == reason
			|| row->inReason == WL_IW_DONT_CARE)) {
			name = row->outName;
			cause = row->outCause;
			break;
		}
	}

	if (cause) {
		memset(stringBuf, 0, buflen);
		snprintf(stringBuf, buflen, "%s %s %02d %02d",
			 name, cause, status, reason);
		WL_INFORM(("Connection status: %s\n", stringBuf));
		return true;
	} else {
		return false;
	}
}

#if WIRELESS_EXT > 14

static bool
wl_iw_check_conn_fail(wl_event_msg_t *e, char *stringBuf, uint buflen)
{
	u32 event = ntoh32(e->event_type);
	u32 status = ntoh32(e->status);
	u32 reason = ntoh32(e->reason);

	if (wl_iw_conn_status_str(event, status, reason, stringBuf, buflen)) {
		return true;
	} else
		return false;
}
#endif

#ifndef IW_CUSTOM_MAX
#define IW_CUSTOM_MAX 256
#endif

void wl_iw_event(struct net_device *dev, wl_event_msg_t *e, void *data)
{
#if WIRELESS_EXT > 13
	union iwreq_data wrqu;
	char extra[IW_CUSTOM_MAX + 1];
	int cmd = 0;
	u32 event_type = ntoh32(e->event_type);
	u16 flags = ntoh16(e->flags);
	u32 datalen = ntoh32(e->datalen);
	u32 status = ntoh32(e->status);
	wl_iw_t *iw;
	u32 toto;
	memset(&wrqu, 0, sizeof(wrqu));
	memset(extra, 0, sizeof(extra));
	iw = 0;

	if (!dev) {
		WL_ERROR(("%s: dev is null\n", __func__));
		return;
	}

	iw = *(wl_iw_t **) netdev_priv(dev);

	WL_TRACE(("%s: dev=%s event=%d\n", __func__, dev->name, event_type));

	switch (event_type) {
	case WLC_E_TXFAIL:
		cmd = IWEVTXDROP;
		memcpy(wrqu.addr.sa_data, &e->addr, ETHER_ADDR_LEN);
		wrqu.addr.sa_family = ARPHRD_ETHER;
		break;
#if WIRELESS_EXT > 14
	case WLC_E_JOIN:
	case WLC_E_ASSOC_IND:
	case WLC_E_REASSOC_IND:
		memcpy(wrqu.addr.sa_data, &e->addr, ETHER_ADDR_LEN);
		wrqu.addr.sa_family = ARPHRD_ETHER;
		cmd = IWEVREGISTERED;
		break;
	case WLC_E_DEAUTH_IND:
	case WLC_E_DISASSOC_IND:
		cmd = SIOCGIWAP;
		memset(wrqu.addr.sa_data, 0, ETHER_ADDR_LEN);
		wrqu.addr.sa_family = ARPHRD_ETHER;
		memset(&extra, 0, ETHER_ADDR_LEN);
		break;
	case WLC_E_LINK:
	case WLC_E_NDIS_LINK:
		cmd = SIOCGIWAP;
		if (!(flags & WLC_EVENT_MSG_LINK)) {
			memset(wrqu.addr.sa_data, 0, ETHER_ADDR_LEN);
			memset(&extra, 0, ETHER_ADDR_LEN);
			WAKE_LOCK_TIMEOUT(iw->pub, WAKE_LOCK_LINK_DOWN_TMOUT,
					  20 * HZ);
		} else {
			memcpy(wrqu.addr.sa_data, &e->addr, ETHER_ADDR_LEN);
			WL_TRACE(("Link UP\n"));

		}
		wrqu.addr.sa_family = ARPHRD_ETHER;
		break;
	case WLC_E_ACTION_FRAME:
		cmd = IWEVCUSTOM;
		if (datalen + 1 <= sizeof(extra)) {
			wrqu.data.length = datalen + 1;
			extra[0] = WLC_E_ACTION_FRAME;
			memcpy(&extra[1], data, datalen);
			WL_TRACE(("WLC_E_ACTION_FRAME len %d \n",
				  wrqu.data.length));
		}
		break;

	case WLC_E_ACTION_FRAME_COMPLETE:
		cmd = IWEVCUSTOM;
		memcpy(&toto, data, 4);
		if (sizeof(status) + 1 <= sizeof(extra)) {
			wrqu.data.length = sizeof(status) + 1;
			extra[0] = WLC_E_ACTION_FRAME_COMPLETE;
			memcpy(&extra[1], &status, sizeof(status));
			printf("wl_iw_event status %d PacketId %d\n", status,
			       toto);
			printf("WLC_E_ACTION_FRAME_COMPLETE len %d\n",
			       wrqu.data.length);
		}
		break;
#endif				/* WIRELESS_EXT > 14 */
#if WIRELESS_EXT > 17
	case WLC_E_MIC_ERROR:
		{
			struct iw_michaelmicfailure *micerrevt =
			    (struct iw_michaelmicfailure *)&extra;
			cmd = IWEVMICHAELMICFAILURE;
			wrqu.data.length = sizeof(struct iw_michaelmicfailure);
			if (flags & WLC_EVENT_MSG_GROUP)
				micerrevt->flags |= IW_MICFAILURE_GROUP;
			else
				micerrevt->flags |= IW_MICFAILURE_PAIRWISE;
			memcpy(micerrevt->src_addr.sa_data, &e->addr,
			       ETHER_ADDR_LEN);
			micerrevt->src_addr.sa_family = ARPHRD_ETHER;

			break;
		}
	case WLC_E_PMKID_CACHE:
		{
			if (data) {
				struct iw_pmkid_cand *iwpmkidcand =
				    (struct iw_pmkid_cand *)&extra;
				pmkid_cand_list_t *pmkcandlist;
				pmkid_cand_t *pmkidcand;
				int count;

				cmd = IWEVPMKIDCAND;
				pmkcandlist = data;
				count =
				    ntoh32_ua((u8 *) &
					      pmkcandlist->npmkid_cand);
				ASSERT(count >= 0);
				wrqu.data.length = sizeof(struct iw_pmkid_cand);
				pmkidcand = pmkcandlist->pmkid_cand;
				while (count) {
					memset(iwpmkidcand, 0,
					      sizeof(struct iw_pmkid_cand));
					if (pmkidcand->preauth)
						iwpmkidcand->flags |=
						    IW_PMKID_CAND_PREAUTH;
					bcopy(&pmkidcand->BSSID,
					      &iwpmkidcand->bssid.sa_data,
					      ETHER_ADDR_LEN);
#ifndef SANDGATE2G
					wireless_send_event(dev, cmd, &wrqu,
							    extra);
#endif
					pmkidcand++;
					count--;
				}
			}
			return;
		}
#endif				/* WIRELESS_EXT > 17 */

	case WLC_E_SCAN_COMPLETE:
#if defined(WL_IW_USE_ISCAN)
		if ((g_iscan) && (g_iscan->sysioc_tsk) &&
		    (g_iscan->iscan_state != ISCAN_STATE_IDLE)) {
			up(&g_iscan->sysioc_sem);
		} else {
			cmd = SIOCGIWSCAN;
			wrqu.data.length = strlen(extra);
			WL_TRACE(("Event WLC_E_SCAN_COMPLETE from specific "
				"scan %d\n", g_iscan->iscan_state));
		}
#else
		cmd = SIOCGIWSCAN;
		wrqu.data.length = strlen(extra);
		WL_TRACE(("Event WLC_E_SCAN_COMPLETE\n"));
#endif
		break;

	case WLC_E_PFN_NET_FOUND:
		{
			wlc_ssid_t *ssid;
			ssid = (wlc_ssid_t *) data;
			WL_ERROR(("%s Event WLC_E_PFN_NET_FOUND, send %s up : "
				"find %s len=%d\n", __func__, PNO_EVENT_UP,
				ssid->SSID, ssid->SSID_len));
			WAKE_LOCK_TIMEOUT(iw->pub, WAKE_LOCK_PNO_FIND_TMOUT,
					  20 * HZ);
			cmd = IWEVCUSTOM;
			memset(&wrqu, 0, sizeof(wrqu));
			strcpy(extra, PNO_EVENT_UP);
			wrqu.data.length = strlen(extra);
		}
		break;

	default:
		WL_TRACE(("Unknown Event %d: ignoring\n", event_type));
		break;
	}
#ifndef SANDGATE2G
	if (cmd) {
		if (cmd == SIOCGIWSCAN)
			wireless_send_event(dev, cmd, &wrqu, NULL);
		else
			wireless_send_event(dev, cmd, &wrqu, extra);
	}
#endif

#if WIRELESS_EXT > 14
	memset(extra, 0, sizeof(extra));
	if (wl_iw_check_conn_fail(e, extra, sizeof(extra))) {
		cmd = IWEVCUSTOM;
		wrqu.data.length = strlen(extra);
#ifndef SANDGATE2G
		wireless_send_event(dev, cmd, &wrqu, extra);
#endif
	}
#endif				/* WIRELESS_EXT > 14 */
#endif				/* WIRELESS_EXT > 13 */
}

int
wl_iw_get_wireless_stats(struct net_device *dev, struct iw_statistics *wstats)
{
	int res = 0;
	wl_cnt_t cnt;
	int phy_noise;
	int rssi;
	scb_val_t scb_val;

	phy_noise = 0;
	res = dev_wlc_ioctl(dev, WLC_GET_PHY_NOISE, &phy_noise,
				sizeof(phy_noise));
	if (res)
		goto done;

	phy_noise = dtoh32(phy_noise);
	WL_TRACE(("wl_iw_get_wireless_stats phy noise=%d\n", phy_noise));

	memset(&scb_val, 0, sizeof(scb_val_t));
	res = dev_wlc_ioctl(dev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t));
	if (res)
		goto done;

	rssi = dtoh32(scb_val.val);
	WL_TRACE(("wl_iw_get_wireless_stats rssi=%d\n", rssi));
	if (rssi <= WL_IW_RSSI_NO_SIGNAL)
		wstats->qual.qual = 0;
	else if (rssi <= WL_IW_RSSI_VERY_LOW)
		wstats->qual.qual = 1;
	else if (rssi <= WL_IW_RSSI_LOW)
		wstats->qual.qual = 2;
	else if (rssi <= WL_IW_RSSI_GOOD)
		wstats->qual.qual = 3;
	else if (rssi <= WL_IW_RSSI_VERY_GOOD)
		wstats->qual.qual = 4;
	else
		wstats->qual.qual = 5;

	wstats->qual.level = 0x100 + rssi;
	wstats->qual.noise = 0x100 + phy_noise;
#if WIRELESS_EXT > 18
	wstats->qual.updated |= (IW_QUAL_ALL_UPDATED | IW_QUAL_DBM);
#else
	wstats->qual.updated |= 7;
#endif

#if WIRELESS_EXT > 11
	WL_TRACE(("wl_iw_get_wireless_stats counters=%d\n",
		  (int)sizeof(wl_cnt_t)));

	memset(&cnt, 0, sizeof(wl_cnt_t));
	res =
	    dev_wlc_bufvar_get(dev, "counters", (char *)&cnt, sizeof(wl_cnt_t));
	if (res) {
		WL_ERROR(("wl_iw_get_wireless_stats counters failed error=%d\n",
			  res));
		goto done;
	}

	cnt.version = dtoh16(cnt.version);
	if (cnt.version != WL_CNT_T_VERSION) {
		WL_TRACE(("\tIncorrect version of counters struct: expected "
			"%d; got %d\n",
			WL_CNT_T_VERSION, cnt.version));
		goto done;
	}

	wstats->discard.nwid = 0;
	wstats->discard.code = dtoh32(cnt.rxundec);
	wstats->discard.fragment = dtoh32(cnt.rxfragerr);
	wstats->discard.retries = dtoh32(cnt.txfail);
	wstats->discard.misc = dtoh32(cnt.rxrunt) + dtoh32(cnt.rxgiant);
	wstats->miss.beacon = 0;

	WL_TRACE(("wl_iw_get_wireless_stats counters txframe=%d txbyte=%d\n",
		  dtoh32(cnt.txframe), dtoh32(cnt.txbyte)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxfrmtoolong=%d\n",
		  dtoh32(cnt.rxfrmtoolong)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxbadplcp=%d\n",
		  dtoh32(cnt.rxbadplcp)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxundec=%d\n",
		  dtoh32(cnt.rxundec)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxfragerr=%d\n",
		  dtoh32(cnt.rxfragerr)));
	WL_TRACE(("wl_iw_get_wireless_stats counters txfail=%d\n",
		  dtoh32(cnt.txfail)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxrunt=%d\n",
		  dtoh32(cnt.rxrunt)));
	WL_TRACE(("wl_iw_get_wireless_stats counters rxgiant=%d\n",
		  dtoh32(cnt.rxgiant)));
#endif				/* WIRELESS_EXT > 11 */

done:
	return res;
}

int wl_iw_attach(struct net_device *dev, void *dhdp)
{
	int params_size;
	wl_iw_t *iw;
#if defined(WL_IW_USE_ISCAN)
	iscan_info_t *iscan = NULL;

	if (!dev)
		return 0;

	memset(&g_wl_iw_params, 0, sizeof(wl_iw_extra_params_t));

#ifdef CSCAN
	params_size =
	    (WL_SCAN_PARAMS_FIXED_SIZE + offsetof(wl_iscan_params_t, params)) +
	    (WL_NUMCHANNELS * sizeof(u16)) +
	    WL_SCAN_PARAMS_SSID_MAX * sizeof(wlc_ssid_t);
#else
	params_size =
	    (WL_SCAN_PARAMS_FIXED_SIZE + offsetof(wl_iscan_params_t, params));
#endif
	iscan = kmalloc(sizeof(iscan_info_t), GFP_KERNEL);

	if (!iscan)
		return -ENOMEM;
	memset(iscan, 0, sizeof(iscan_info_t));

	iscan->iscan_ex_params_p = kmalloc(params_size, GFP_KERNEL);
	if (!iscan->iscan_ex_params_p)
		return -ENOMEM;
	iscan->iscan_ex_param_size = params_size;
	iscan->sysioc_tsk = NULL;

	g_iscan = iscan;
	iscan->dev = dev;
	iscan->iscan_state = ISCAN_STATE_IDLE;

	iscan->timer_ms = 3000;
	init_timer(&iscan->timer);
	iscan->timer.data = (unsigned long) iscan;
	iscan->timer.function = wl_iw_timerfunc;

	sema_init(&iscan->sysioc_sem, 0);
	iscan->sysioc_tsk = kthread_run(_iscan_sysioc_thread, iscan,
					"_iscan_sysioc");
	if (IS_ERR(iscan->sysioc_tsk)) {
		iscan->sysioc_tsk = NULL;
		return -ENOMEM;
	}
#endif				/* defined(WL_IW_USE_ISCAN) */

	iw = *(wl_iw_t **) netdev_priv(dev);
	iw->pub = (dhd_pub_t *) dhdp;
	MUTEX_LOCK_INIT(iw->pub);
	MUTEX_LOCK_WL_SCAN_SET_INIT();
#ifdef SOFTAP
	priv_dev = dev;
	MUTEX_LOCK_SOFTAP_SET_INIT(iw->pub);
#endif
	g_scan = kmalloc(G_SCAN_RESULTS, GFP_KERNEL);
	if (!g_scan)
		return -ENOMEM;

	memset(g_scan, 0, G_SCAN_RESULTS);
	g_scan_specified_ssid = 0;

	return 0;
}

void wl_iw_detach(void)
{
#if defined(WL_IW_USE_ISCAN)
	iscan_buf_t *buf;
	iscan_info_t *iscan = g_iscan;

	if (!iscan)
		return;
	if (iscan->sysioc_tsk) {
		send_sig(SIGTERM, iscan->sysioc_tsk, 1);
		kthread_stop(iscan->sysioc_tsk);
		iscan->sysioc_tsk = NULL;
	}

	MUTEX_LOCK_WL_SCAN_SET();
	while (iscan->list_hdr) {
		buf = iscan->list_hdr->next;
		kfree(iscan->list_hdr);
		iscan->list_hdr = buf;
	}
	MUTEX_UNLOCK_WL_SCAN_SET();
	kfree(iscan->iscan_ex_params_p);
	kfree(iscan);
	g_iscan = NULL;
#endif				/* WL_IW_USE_ISCAN */

	kfree(g_scan);

	g_scan = NULL;
}
