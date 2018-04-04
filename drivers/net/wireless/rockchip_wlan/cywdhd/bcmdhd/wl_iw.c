/*
 * Linux Wireless Extensions support
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
 * $Id: wl_iw.c 666543 2017-08-25 07:42:40Z $
 */

#if defined(USE_IW)
#define LINUX_PORT

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmendian.h>
#include <proto/ethernet.h>

#include <linux/if_arp.h>
#include <asm/uaccess.h>

#include <wlioctl.h>
#include <wlioctl_utils.h>

typedef const struct si_pub	si_t;

#include <wl_dbg.h>
#include <wl_iw.h>


/* Broadcom extensions to WEXT, linux upstream has obsoleted WEXT */
#ifndef IW_AUTH_KEY_MGMT_FT_802_1X
#define IW_AUTH_KEY_MGMT_FT_802_1X 0x04
#endif

#ifndef IW_AUTH_KEY_MGMT_FT_PSK
#define IW_AUTH_KEY_MGMT_FT_PSK 0x08
#endif

#ifndef IW_ENC_CAPA_FW_ROAM_ENABLE
#define IW_ENC_CAPA_FW_ROAM_ENABLE	0x00000020
#endif


/* FC9: wireless.h 2.6.25-14.fc9.i686 is missing these, even though WIRELESS_EXT is set to latest
 * version 22.
 */
#ifndef IW_ENCODE_ALG_PMK
#define IW_ENCODE_ALG_PMK 4
#endif
#ifndef IW_ENC_CAPA_4WAY_HANDSHAKE
#define IW_ENC_CAPA_4WAY_HANDSHAKE 0x00000010
#endif
/* End FC9. */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
#include <linux/rtnetlink.h>
#endif
#if defined(SOFTAP)
struct net_device *ap_net_dev = NULL;
tsk_ctl_t ap_eth_ctl;  /* apsta AP netdev waiter thread */
#endif /* SOFTAP */

extern bool wl_iw_conn_status_str(uint32 event_type, uint32 status,
	uint32 reason, char* stringBuf, uint buflen);

uint wl_msg_level = WL_ERROR_VAL;

#define MAX_WLIW_IOCTL_LEN 1024

/* IOCTL swapping mode for Big Endian host with Little Endian dongle.  Default to off */
#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)

extern struct iw_statistics *dhd_get_wireless_stats(struct net_device *dev);
extern int dhd_wait_pend8021x(struct net_device *dev);

#if WIRELESS_EXT < 19
#define IW_IOCTL_IDX(cmd)	((cmd) - SIOCIWFIRST)
#define IW_EVENT_IDX(cmd)	((cmd) - IWEVFIRST)
#endif /* WIRELESS_EXT < 19 */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
#define DAEMONIZE(a)	do { \
		allow_signal(SIGKILL);	\
		allow_signal(SIGTERM);	\
	} while (0)
#elif ((LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)))
#define DAEMONIZE(a) daemonize(a); \
	allow_signal(SIGKILL); \
	allow_signal(SIGTERM);
#else /* Linux 2.4 (w/o preemption patch) */
#define RAISE_RX_SOFTIRQ() \
	cpu_raise_softirq(smp_processor_id(), NET_RX_SOFTIRQ)
#define DAEMONIZE(a) daemonize(); \
	do { if (a) \
		strncpy(current->comm, a, MIN(sizeof(current->comm), (strlen(a) + 1))); \
	} while (0);
#endif /* LINUX_VERSION_CODE  */

#define ISCAN_STATE_IDLE   0
#define ISCAN_STATE_SCANING 1

/* the buf lengh can be WLC_IOCTL_MAXLEN (8K) to reduce iteration */
#define WLC_IW_ISCAN_MAXLEN   2048
typedef struct iscan_buf {
	struct iscan_buf * next;
	char   iscan_buf[WLC_IW_ISCAN_MAXLEN];
} iscan_buf_t;

typedef struct iscan_info {
	struct net_device *dev;
	struct timer_list timer;
	uint32 timer_ms;
	uint32 timer_on;
	int    iscan_state;
	iscan_buf_t * list_hdr;
	iscan_buf_t * list_cur;

	/* Thread to work on iscan */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	struct task_struct *kthread;
#endif
	long sysioc_pid;
	struct semaphore sysioc_sem;
	struct completion sysioc_exited;


	char ioctlbuf[WLC_IOCTL_SMLEN];
} iscan_info_t;
iscan_info_t *g_iscan = NULL;
static void wl_iw_timerfunc(ulong data);
static void wl_iw_set_event_mask(struct net_device *dev);
static int wl_iw_iscan(iscan_info_t *iscan, wlc_ssid_t *ssid, uint16 action);

/* priv_link becomes netdev->priv and is the link between netdev and wlif struct */
typedef struct priv_link {
	wl_iw_t *wliw;
} priv_link_t;

/* dev to priv_link */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define WL_DEV_LINK(dev)       (priv_link_t*)(dev->priv)
#else
#define WL_DEV_LINK(dev)       (priv_link_t*)netdev_priv(dev)
#endif

/* dev to wl_iw_t */
#define IW_DEV_IF(dev)          ((wl_iw_t*)(WL_DEV_LINK(dev))->wliw)

static void swap_key_from_BE(
	        wl_wsec_key_t *key
)
{
	key->index = htod32(key->index);
	key->len = htod32(key->len);
	key->algo = htod32(key->algo);
	key->flags = htod32(key->flags);
	key->rxiv.hi = htod32(key->rxiv.hi);
	key->rxiv.lo = htod16(key->rxiv.lo);
	key->iv_initialized = htod32(key->iv_initialized);
}

static void swap_key_to_BE(
	        wl_wsec_key_t *key
)
{
	key->index = dtoh32(key->index);
	key->len = dtoh32(key->len);
	key->algo = dtoh32(key->algo);
	key->flags = dtoh32(key->flags);
	key->rxiv.hi = dtoh32(key->rxiv.hi);
	key->rxiv.lo = dtoh16(key->rxiv.lo);
	key->iv_initialized = dtoh32(key->iv_initialized);
}

static int
dev_wlc_ioctl(
	struct net_device *dev,
	int cmd,
	void *arg,
	int len
)
{
	struct ifreq ifr;
	wl_ioctl_t ioc;
	mm_segment_t fs;
	int ret;

	memset(&ioc, 0, sizeof(ioc));
	ioc.cmd = cmd;
	ioc.buf = arg;
	ioc.len = len;

	strncpy(ifr.ifr_name, dev->name, sizeof(ifr.ifr_name));
	ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';
	ifr.ifr_data = (caddr_t) &ioc;

	fs = get_fs();
	set_fs(get_ds());
#if defined(WL_USE_NETDEV_OPS)
	ret = dev->netdev_ops->ndo_do_ioctl(dev, &ifr, SIOCDEVPRIVATE);
#else
	ret = dev->do_ioctl(dev, &ifr, SIOCDEVPRIVATE);
#endif
	set_fs(fs);

	return ret;
}

/*
set named driver variable to int value and return error indication
calling example: dev_wlc_intvar_set(dev, "arate", rate)
*/

static int
dev_wlc_intvar_set(
	struct net_device *dev,
	char *name,
	int val)
{
	char buf[WLC_IOCTL_SMLEN];
	uint len;

	val = htod32(val);
	len = bcm_mkiovar(name, (char *)(&val), sizeof(val), buf, sizeof(buf));
	ASSERT(len);

	return (dev_wlc_ioctl(dev, WLC_SET_VAR, buf, len));
}

static int
dev_iw_iovar_setbuf(
	struct net_device *dev,
	char *iovar,
	void *param,
	int paramlen,
	void *bufptr,
	int buflen)
{
	int iolen;

	iolen = bcm_mkiovar(iovar, param, paramlen, bufptr, buflen);
	ASSERT(iolen);
	BCM_REFERENCE(iolen);

	return (dev_wlc_ioctl(dev, WLC_SET_VAR, bufptr, iolen));
}

static int
dev_iw_iovar_getbuf(
	struct net_device *dev,
	char *iovar,
	void *param,
	int paramlen,
	void *bufptr,
	int buflen)
{
	int iolen;

	iolen = bcm_mkiovar(iovar, param, paramlen, bufptr, buflen);
	ASSERT(iolen);
	BCM_REFERENCE(iolen);

	return (dev_wlc_ioctl(dev, WLC_GET_VAR, bufptr, buflen));
}

#if WIRELESS_EXT > 17
static int
dev_wlc_bufvar_set(
	struct net_device *dev,
	char *name,
	char *buf, int len)
{
	char *ioctlbuf;
	uint buflen;
	int error;

	ioctlbuf = kmalloc(MAX_WLIW_IOCTL_LEN, GFP_KERNEL);
	if (!ioctlbuf)
		return -ENOMEM;

	buflen = bcm_mkiovar(name, buf, len, ioctlbuf, MAX_WLIW_IOCTL_LEN);
	ASSERT(buflen);
	error = dev_wlc_ioctl(dev, WLC_SET_VAR, ioctlbuf, buflen);

	kfree(ioctlbuf);
	return error;
}
#endif /* WIRELESS_EXT > 17 */

/*
get named driver variable to int value and return error indication
calling example: dev_wlc_bufvar_get(dev, "arate", &rate)
*/

static int
dev_wlc_bufvar_get(
	struct net_device *dev,
	char *name,
	char *buf, int buflen)
{
	char *ioctlbuf;
	int error;

	uint len;

	ioctlbuf = kmalloc(MAX_WLIW_IOCTL_LEN, GFP_KERNEL);
	if (!ioctlbuf)
		return -ENOMEM;
	len = bcm_mkiovar(name, NULL, 0, ioctlbuf, MAX_WLIW_IOCTL_LEN);
	ASSERT(len);
	BCM_REFERENCE(len);
	error = dev_wlc_ioctl(dev, WLC_GET_VAR, (void *)ioctlbuf, MAX_WLIW_IOCTL_LEN);
	if (!error)
		bcopy(ioctlbuf, buf, buflen);

	kfree(ioctlbuf);
	return (error);
}

/*
get named driver variable to int value and return error indication
calling example: dev_wlc_intvar_get(dev, "arate", &rate)
*/

static int
dev_wlc_intvar_get(
	struct net_device *dev,
	char *name,
	int *retval)
{
	union {
		char buf[WLC_IOCTL_SMLEN];
		int val;
	} var;
	int error;

	uint len;
	uint data_null;

	len = bcm_mkiovar(name, (char *)(&data_null), 0, (char *)(&var), sizeof(var.buf));
	ASSERT(len);
	error = dev_wlc_ioctl(dev, WLC_GET_VAR, (void *)&var, len);

	*retval = dtoh32(var.val);

	return (error);
}

/* Maintain backward compatibility */
#if WIRELESS_EXT < 13
struct iw_request_info
{
	__u16		cmd;		/* Wireless Extension command */
	__u16		flags;		/* More to come ;-) */
};

typedef int (*iw_handler)(struct net_device *dev, struct iw_request_info *info,
	void *wrqu, char *extra);
#endif /* WIRELESS_EXT < 13 */

#if WIRELESS_EXT > 12
static int
wl_iw_set_leddc(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int dc = *(int *)extra;
	int error;

	error = dev_wlc_intvar_set(dev, "leddc", dc);
	return error;
}

static int
wl_iw_set_vlanmode(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int mode = *(int *)extra;
	int error;

	mode = htod32(mode);
	error = dev_wlc_intvar_set(dev, "vlan_mode", mode);
	return error;
}

static int
wl_iw_set_pm(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	int pm = *(int *)extra;
	int error;

	pm = htod32(pm);
	error = dev_wlc_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm));
	return error;
}

#if WIRELESS_EXT > 17
#endif /* WIRELESS_EXT > 17 */
#endif /* WIRELESS_EXT > 12 */

int
wl_iw_send_priv_event(
	struct net_device *dev,
	char *flag
)
{
	union iwreq_data wrqu;
	char extra[IW_CUSTOM_MAX + 1];
	int cmd;

	cmd = IWEVCUSTOM;
	memset(&wrqu, 0, sizeof(wrqu));
	if (strlen(flag) > sizeof(extra))
		return -1;

	strncpy(extra, flag, sizeof(extra));
	extra[sizeof(extra) - 1] = '\0';
	wrqu.data.length = strlen(extra);
	wireless_send_event(dev, cmd, &wrqu, extra);
	WL_TRACE(("Send IWEVCUSTOM Event as %s\n", extra));

	return 0;
}

static int
wl_iw_config_commit(
	struct net_device *dev,
	struct iw_request_info *info,
	void *zwrq,
	char *extra
)
{
	wlc_ssid_t ssid;
	int error;
	struct sockaddr bssid;

	WL_TRACE(("%s: SIOCSIWCOMMIT\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_SSID, &ssid, sizeof(ssid))))
		return error;

	ssid.SSID_len = dtoh32(ssid.SSID_len);

	if (!ssid.SSID_len)
		return 0;

	bzero(&bssid, sizeof(struct sockaddr));
	if ((error = dev_wlc_ioctl(dev, WLC_REASSOC, &bssid, ETHER_ADDR_LEN))) {
		WL_ERROR(("%s: WLC_REASSOC failed (%d)\n", __FUNCTION__, error));
		return error;
	}

	return 0;
}

static int
wl_iw_get_name(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *cwrq,
	char *extra
)
{
	int phytype, err;
	uint band[3];
	char cap[5];

	WL_TRACE(("%s: SIOCGIWNAME\n", dev->name));

	cap[0] = 0;
	if ((err = dev_wlc_ioctl(dev, WLC_GET_PHYTYPE, &phytype, sizeof(phytype))) < 0)
		goto done;
	if ((err = dev_wlc_ioctl(dev, WLC_GET_BANDLIST, band, sizeof(band))) < 0)
		goto done;

	band[0] = dtoh32(band[0]);
	switch (phytype) {
		case WLC_PHY_TYPE_A:
			strncpy(cap, "a", sizeof(cap));
			break;
		case WLC_PHY_TYPE_B:
			strncpy(cap, "b", sizeof(cap));
			break;
		case WLC_PHY_TYPE_G:
			if (band[0] >= 2)
				strncpy(cap, "abg", sizeof(cap));
			else
				strncpy(cap, "bg", sizeof(cap));
			break;
		case WLC_PHY_TYPE_N:
			if (band[0] >= 2)
				strncpy(cap, "abgn", sizeof(cap));
			else
				strncpy(cap, "bgn", sizeof(cap));
			break;
	}
done:
	(void)snprintf(cwrq->name, IFNAMSIZ, "IEEE 802.11%s", cap);

	return 0;
}

static int
wl_iw_set_freq(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_freq *fwrq,
	char *extra
)
{
	int error, chan;
	uint sf = 0;

	WL_TRACE(("%s: SIOCSIWFREQ\n", dev->name));

	/* Setting by channel number */
	if (fwrq->e == 0 && fwrq->m < MAXCHANNEL) {
		chan = fwrq->m;
	}

	/* Setting by frequency */
	else {
		/* Convert to MHz as best we can */
		if (fwrq->e >= 6) {
			fwrq->e -= 6;
			while (fwrq->e--)
				fwrq->m *= 10;
		} else if (fwrq->e < 6) {
			while (fwrq->e++ < 6)
				fwrq->m /= 10;
		}
	/* handle 4.9GHz frequencies as Japan 4 GHz based channelization */
		if (fwrq->m > 4000 && fwrq->m < 5000)
			sf = WF_CHAN_FACTOR_4_G; /* start factor for 4 GHz */

		chan = wf_mhz2channel(fwrq->m, sf);
	}
	chan = htod32(chan);
	if ((error = dev_wlc_ioctl(dev, WLC_SET_CHANNEL, &chan, sizeof(chan))))
		return error;

	/* -EINPROGRESS: Call commit handler */
	return -EINPROGRESS;
}

static int
wl_iw_get_freq(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_freq *fwrq,
	char *extra
)
{
	channel_info_t ci;
	int error;

	WL_TRACE(("%s: SIOCGIWFREQ\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(ci))))
		return error;

	/* Return radio channel in channel form */
	fwrq->m = dtoh32(ci.hw_channel);
	fwrq->e = dtoh32(0);
	return 0;
}

static int
wl_iw_set_mode(
	struct net_device *dev,
	struct iw_request_info *info,
	__u32 *uwrq,
	char *extra
)
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

	if ((error = dev_wlc_ioctl(dev, WLC_SET_INFRA, &infra, sizeof(infra))) ||
	    (error = dev_wlc_ioctl(dev, WLC_SET_AP, &ap, sizeof(ap))))
		return error;

	/* -EINPROGRESS: Call commit handler */
	return -EINPROGRESS;
}

static int
wl_iw_get_mode(
	struct net_device *dev,
	struct iw_request_info *info,
	__u32 *uwrq,
	char *extra
)
{
	int error, infra = 0, ap = 0;

	WL_TRACE(("%s: SIOCGIWMODE\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_INFRA, &infra, sizeof(infra))) ||
	    (error = dev_wlc_ioctl(dev, WLC_GET_AP, &ap, sizeof(ap))))
		return error;

	infra = dtoh32(infra);
	ap = dtoh32(ap);
	*uwrq = infra ? ap ? IW_MODE_MASTER : IW_MODE_INFRA : IW_MODE_ADHOC;

	return 0;
}

static int
wl_iw_get_range(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	struct iw_range *range = (struct iw_range *) extra;
	static int channels[MAXCHANNEL+1];
	wl_uint32_list_t *list = (wl_uint32_list_t *) channels;
	wl_rateset_t rateset;
	int error, i, k;
	uint sf, ch;

	int phytype;
	int bw_cap = 0, sgi_tx = 0, nmode = 0;
	channel_info_t ci;
	uint8 nrate_list2copy = 0;
	uint16 nrate_list[4][8] = { {13, 26, 39, 52, 78, 104, 117, 130},
		{14, 29, 43, 58, 87, 116, 130, 144},
		{27, 54, 81, 108, 162, 216, 243, 270},
		{30, 60, 90, 120, 180, 240, 270, 300}};
	int fbt_cap = 0;

	WL_TRACE(("%s: SIOCGIWRANGE\n", dev->name));

	if (!extra)
		return -EINVAL;

	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(*range));

	/* We don't use nwids */
	range->min_nwid = range->max_nwid = 0;

	/* Set available channels/frequencies */
	list->count = htod32(MAXCHANNEL);
	if ((error = dev_wlc_ioctl(dev, WLC_GET_VALID_CHANNELS, channels, sizeof(channels))))
		return error;
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

	/* Link quality (use NDIS cutoffs) */
	range->max_qual.qual = 5;
	/* Signal level (use RSSI) */
	range->max_qual.level = 0x100 - 200;	/* -200 dBm */
	/* Noise level (use noise) */
	range->max_qual.noise = 0x100 - 200;	/* -200 dBm */
	/* Signal level threshold range (?) */
	range->sensitivity = 65535;

#if WIRELESS_EXT > 11
	/* Link quality (use NDIS cutoffs) */
	range->avg_qual.qual = 3;
	/* Signal level (use RSSI) */
	range->avg_qual.level = 0x100 + WL_IW_RSSI_GOOD;
	/* Noise level (use noise) */
	range->avg_qual.noise = 0x100 - 75;	/* -75 dBm */
#endif /* WIRELESS_EXT > 11 */

	/* Set available bitrates */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_CURR_RATESET, &rateset, sizeof(rateset))))
		return error;
	rateset.count = dtoh32(rateset.count);
	range->num_bitrates = rateset.count;
	for (i = 0; i < rateset.count && i < IW_MAX_BITRATES; i++)
		range->bitrate[i] = (rateset.rates[i] & 0x7f) * 500000; /* convert to bps */
	if ((error = dev_wlc_intvar_get(dev, "nmode", &nmode)))
		return error;
	if ((error = dev_wlc_ioctl(dev, WLC_GET_PHYTYPE, &phytype, sizeof(phytype))))
		return error;
	if (nmode == 1 && (((phytype == WLC_PHY_TYPE_LCN) ||
	                    (phytype == WLC_PHY_TYPE_LCN40)))) {
		if ((error = dev_wlc_intvar_get(dev, "mimo_bw_cap", &bw_cap)))
			return error;
		if ((error = dev_wlc_intvar_get(dev, "sgi_tx", &sgi_tx)))
			return error;
		if ((error = dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(channel_info_t))))
			return error;
		ci.hw_channel = dtoh32(ci.hw_channel);

		if (bw_cap == 0 ||
			(bw_cap == 2 && ci.hw_channel <= 14)) {
			if (sgi_tx == 0)
				nrate_list2copy = 0;
			else
				nrate_list2copy = 1;
		}
		if (bw_cap == 1 ||
			(bw_cap == 2 && ci.hw_channel >= 36)) {
			if (sgi_tx == 0)
				nrate_list2copy = 2;
			else
				nrate_list2copy = 3;
		}
		range->num_bitrates += 8;
		ASSERT(range->num_bitrates < IW_MAX_BITRATES);
		for (k = 0; i < range->num_bitrates; k++, i++) {
			/* convert to bps */
			range->bitrate[i] = (nrate_list[nrate_list2copy][k]) * 500000;
		}
	}

	/* Set an indication of the max TCP throughput
	 * in bit/s that we can expect using this interface.
	 * May be use for QoS stuff... Jean II
	 */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_PHYTYPE, &i, sizeof(i))))
		return error;
	i = dtoh32(i);
	if (i == WLC_PHY_TYPE_A)
		range->throughput = 24000000;	/* 24 Mbits/s */
	else
		range->throughput = 1500000;	/* 1.5 Mbits/s */

	/* RTS and fragmentation thresholds */
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

	/* Do not support power micro-management */
	range->min_pmp = 0;
	range->max_pmp = 0;
	range->min_pmt = 0;
	range->max_pmt = 0;
	range->pmp_flags = 0;
	range->pm_capa = 0;

	/* Transmit Power - values are in mW */
	range->num_txpower = 2;
	range->txpower[0] = 1;
	range->txpower[1] = 255;
	range->txpower_capa = IW_TXPOW_MWATT;

#if WIRELESS_EXT > 10
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 19;

	/* Only support retry limits */
	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->r_time_flags = 0;
	/* SRL and LRL limits */
	range->min_retry = 1;
	range->max_retry = 255;
	/* Retry lifetime limits unsupported */
	range->min_r_time = 0;
	range->max_r_time = 0;
#endif /* WIRELESS_EXT > 10 */

#if WIRELESS_EXT > 17
	range->enc_capa = IW_ENC_CAPA_WPA;
	range->enc_capa |= IW_ENC_CAPA_CIPHER_TKIP;
	range->enc_capa |= IW_ENC_CAPA_CIPHER_CCMP;
	range->enc_capa |= IW_ENC_CAPA_WPA2;

	/* Determine driver FBT capability. */
	if (dev_wlc_intvar_get(dev, "fbt_cap", &fbt_cap) == 0) {
		if (fbt_cap == WLC_FBT_CAP_DRV_4WAY_AND_REASSOC) {
			/* Tell the host (e.g. wpa_supplicant) to let driver do the handshake */
			range->enc_capa |= IW_ENC_CAPA_4WAY_HANDSHAKE;
		}
	}

#ifdef BCMFW_ROAM_ENABLE_WEXT
	/* Advertise firmware roam capability to the external supplicant */
	range->enc_capa |= IW_ENC_CAPA_FW_ROAM_ENABLE;
#endif /* BCMFW_ROAM_ENABLE_WEXT */

	/* Event capability (kernel) */
	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	/* Event capability (driver) */
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVTXDROP);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVMICHAELMICFAILURE);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVASSOCREQIE);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVASSOCRESPIE);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVPMKIDCAND);

#if WIRELESS_EXT >= 22 && defined(IW_SCAN_CAPA_ESSID)
	/* FC7 wireless.h defines EXT 22 but doesn't define scan_capa bits */
	range->scan_capa = IW_SCAN_CAPA_ESSID;
#endif
#endif /* WIRELESS_EXT > 17 */

	return 0;
}

static int
rssi_to_qual(int rssi)
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
wl_iw_set_spy(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_iw_t *iw = IW_DEV_IF(dev);
	struct sockaddr *addr = (struct sockaddr *) extra;
	int i;

	WL_TRACE(("%s: SIOCSIWSPY\n", dev->name));

	if (!extra)
		return -EINVAL;

	iw->spy_num = MIN(ARRAYSIZE(iw->spy_addr), dwrq->length);
	for (i = 0; i < iw->spy_num; i++)
		memcpy(&iw->spy_addr[i], addr[i].sa_data, ETHER_ADDR_LEN);
	memset(iw->spy_qual, 0, sizeof(iw->spy_qual));

	return 0;
}

static int
wl_iw_get_spy(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_iw_t *iw = IW_DEV_IF(dev);
	struct sockaddr *addr = (struct sockaddr *) extra;
	struct iw_quality *qual = (struct iw_quality *) &addr[iw->spy_num];
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
wl_iw_set_wap(
	struct net_device *dev,
	struct iw_request_info *info,
	struct sockaddr *awrq,
	char *extra
)
{
	int error = -EINVAL;

	WL_TRACE(("%s: SIOCSIWAP\n", dev->name));

	if (awrq->sa_family != ARPHRD_ETHER) {
		WL_ERROR(("%s: Invalid Header...sa_family\n", __FUNCTION__));
		return -EINVAL;
	}

	/* Ignore "auto" or "off" */
	if (ETHER_ISBCAST(awrq->sa_data) || ETHER_ISNULLADDR(awrq->sa_data)) {
		scb_val_t scbval;
		bzero(&scbval, sizeof(scb_val_t));
		if ((error = dev_wlc_ioctl(dev, WLC_DISASSOC, &scbval, sizeof(scb_val_t)))) {
			WL_ERROR(("%s: WLC_DISASSOC failed (%d).\n", __FUNCTION__, error));
		}
		return 0;
	}
	/* WL_ASSOC(("Assoc to %s\n", bcm_ether_ntoa((struct ether_addr *)&(awrq->sa_data),
	 * eabuf)));
	 */
	/* Reassociate to the specified AP */
	if ((error = dev_wlc_ioctl(dev, WLC_REASSOC, awrq->sa_data, ETHER_ADDR_LEN))) {
		WL_ERROR(("%s: WLC_REASSOC failed (%d).\n", __FUNCTION__, error));
		return error;
	}

	return 0;
}

static int
wl_iw_get_wap(
	struct net_device *dev,
	struct iw_request_info *info,
	struct sockaddr *awrq,
	char *extra
)
{
	WL_TRACE(("%s: SIOCGIWAP\n", dev->name));

	awrq->sa_family = ARPHRD_ETHER;
	memset(awrq->sa_data, 0, ETHER_ADDR_LEN);

	/* Ignore error (may be down or disassociated) */
	(void) dev_wlc_ioctl(dev, WLC_GET_BSSID, awrq->sa_data, ETHER_ADDR_LEN);

	return 0;
}

#if WIRELESS_EXT > 17
static int
wl_iw_mlme(
	struct net_device *dev,
	struct iw_request_info *info,
	struct sockaddr *awrq,
	char *extra
)
{
	struct iw_mlme *mlme;
	scb_val_t scbval;
	int error  = -EINVAL;

	WL_TRACE(("%s: SIOCSIWMLME\n", dev->name));

	mlme = (struct iw_mlme *)extra;
	if (mlme == NULL) {
		WL_ERROR(("Invalid ioctl data.\n"));
		return error;
	}

	scbval.val = mlme->reason_code;
	bcopy(&mlme->addr.sa_data, &scbval.ea, ETHER_ADDR_LEN);

	if (mlme->cmd == IW_MLME_DISASSOC) {
		scbval.val = htod32(scbval.val);
		error = dev_wlc_ioctl(dev, WLC_DISASSOC, &scbval, sizeof(scb_val_t));
	}
	else if (mlme->cmd == IW_MLME_DEAUTH) {
		scbval.val = htod32(scbval.val);
		error = dev_wlc_ioctl(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scbval,
			sizeof(scb_val_t));
	}
	else {
		WL_ERROR(("%s: Invalid ioctl data.\n", __FUNCTION__));
		return error;
	}

	return error;
}
#endif /* WIRELESS_EXT > 17 */

static int
wl_iw_get_aplist(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_scan_results_t *list;
	struct sockaddr *addr = (struct sockaddr *) extra;
	struct iw_quality qual[IW_MAX_AP];
	wl_bss_info_t *bi = NULL;
	int error, i;
	uint buflen = dwrq->length;

	WL_TRACE(("%s: SIOCGIWAPLIST\n", dev->name));

	if (!extra)
		return -EINVAL;

	/* Get scan results (too large to put on the stack) */
	list = kmalloc(buflen, GFP_KERNEL);
	if (!list)
		return -ENOMEM;
	memset(list, 0, buflen);
	list->buflen = htod32(buflen);
	if ((error = dev_wlc_ioctl(dev, WLC_SCAN_RESULTS, list, buflen))) {
		WL_ERROR(("%d: Scan results error %d\n", __LINE__, error));
		kfree(list);
		return error;
	}
	list->buflen = dtoh32(list->buflen);
	list->version = dtoh32(list->version);
	list->count = dtoh32(list->count);
	ASSERT(list->version == WL_BSS_INFO_VERSION);

	for (i = 0, dwrq->length = 0; i < list->count && dwrq->length < IW_MAX_AP; i++) {
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : list->bss_info;
		ASSERT(((uintptr)bi + dtoh32(bi->length)) <= ((uintptr)list +
			buflen));

		/* Infrastructure only */
		if (!(dtoh16(bi->capability) & DOT11_CAP_ESS))
			continue;

		/* BSSID */
		memcpy(addr[dwrq->length].sa_data, &bi->BSSID, ETHER_ADDR_LEN);
		addr[dwrq->length].sa_family = ARPHRD_ETHER;
		qual[dwrq->length].qual = rssi_to_qual(dtoh16(bi->RSSI));
		qual[dwrq->length].level = 0x100 + dtoh16(bi->RSSI);
		qual[dwrq->length].noise = 0x100 + bi->phy_noise;

		/* Updated qual, level, and noise */
#if WIRELESS_EXT > 18
		qual[dwrq->length].updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
#else
		qual[dwrq->length].updated = 7;
#endif /* WIRELESS_EXT > 18 */

		dwrq->length++;
	}

	kfree(list);

	if (dwrq->length) {
		memcpy(&addr[dwrq->length], qual, sizeof(struct iw_quality) * dwrq->length);
		/* Provided qual */
		dwrq->flags = 1;
	}

	return 0;
}

static int
wl_iw_iscan_get_aplist(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_scan_results_t *list;
	iscan_buf_t * buf;
	iscan_info_t *iscan = g_iscan;

	struct sockaddr *addr = (struct sockaddr *) extra;
	struct iw_quality qual[IW_MAX_AP];
	wl_bss_info_t *bi = NULL;
	int i;

	WL_TRACE(("%s: SIOCGIWAPLIST\n", dev->name));

	if (!extra)
		return -EINVAL;

	if ((!iscan) || (iscan->sysioc_pid < 0)) {
		return wl_iw_get_aplist(dev, info, dwrq, extra);
	}

	buf = iscan->list_hdr;
	/* Get scan results (too large to put on the stack) */
	while (buf) {
	    list = &((wl_iscan_results_t*)buf->iscan_buf)->results;
	    ASSERT(list->version == WL_BSS_INFO_VERSION);

	    bi = NULL;
	for (i = 0, dwrq->length = 0; i < list->count && dwrq->length < IW_MAX_AP; i++) {
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : list->bss_info;
		ASSERT(((uintptr)bi + dtoh32(bi->length)) <= ((uintptr)list +
			WLC_IW_ISCAN_MAXLEN));

		/* Infrastructure only */
		if (!(dtoh16(bi->capability) & DOT11_CAP_ESS))
			continue;

		/* BSSID */
		memcpy(addr[dwrq->length].sa_data, &bi->BSSID, ETHER_ADDR_LEN);
		addr[dwrq->length].sa_family = ARPHRD_ETHER;
		qual[dwrq->length].qual = rssi_to_qual(dtoh16(bi->RSSI));
		qual[dwrq->length].level = 0x100 + dtoh16(bi->RSSI);
		qual[dwrq->length].noise = 0x100 + bi->phy_noise;

		/* Updated qual, level, and noise */
#if WIRELESS_EXT > 18
		qual[dwrq->length].updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
#else
		qual[dwrq->length].updated = 7;
#endif /* WIRELESS_EXT > 18 */

		dwrq->length++;
	    }
	    buf = buf->next;
	}
	if (dwrq->length) {
		memcpy(&addr[dwrq->length], qual, sizeof(struct iw_quality) * dwrq->length);
		/* Provided qual */
		dwrq->flags = 1;
	}

	return 0;
}

#if WIRELESS_EXT > 13
static int
wl_iw_set_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	wlc_ssid_t ssid;

	WL_TRACE(("%s: SIOCSIWSCAN\n", dev->name));

	/* default Broadcast scan */
	memset(&ssid, 0, sizeof(ssid));

#if WIRELESS_EXT > 17
	/* check for given essid */
	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			struct iw_scan_req *req = (struct iw_scan_req *)extra;
			ssid.SSID_len = MIN(sizeof(ssid.SSID), req->essid_len);
			memcpy(ssid.SSID, req->essid, ssid.SSID_len);
			ssid.SSID_len = htod32(ssid.SSID_len);
		}
	}
#endif
	/* Ignore error (most likely scan in progress) */
	(void) dev_wlc_ioctl(dev, WLC_SCAN, &ssid, sizeof(ssid));

	return 0;
}

static int
wl_iw_iscan_set_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu,
	char *extra
)
{
	wlc_ssid_t ssid;
	iscan_info_t *iscan = g_iscan;

	WL_TRACE(("%s: SIOCSIWSCAN\n", dev->name));

	/* use backup if our thread is not successful */
	if ((!iscan) || (iscan->sysioc_pid < 0)) {
		return wl_iw_set_scan(dev, info, wrqu, extra);
	}
	if (iscan->iscan_state == ISCAN_STATE_SCANING) {
		return 0;
	}

	/* default Broadcast scan */
	memset(&ssid, 0, sizeof(ssid));

#if WIRELESS_EXT > 17
	/* check for given essid */
	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			struct iw_scan_req *req = (struct iw_scan_req *)extra;
			ssid.SSID_len = MIN(sizeof(ssid.SSID), req->essid_len);
			memcpy(ssid.SSID, req->essid, ssid.SSID_len);
			ssid.SSID_len = htod32(ssid.SSID_len);
		}
	}
#endif

	iscan->list_cur = iscan->list_hdr;
	iscan->iscan_state = ISCAN_STATE_SCANING;


	wl_iw_set_event_mask(dev);
	wl_iw_iscan(iscan, &ssid, WL_SCAN_ACTION_START);

	iscan->timer.expires = jiffies + msecs_to_jiffies(iscan->timer_ms);
	add_timer(&iscan->timer);
	iscan->timer_on = 1;

	return 0;
}

#if WIRELESS_EXT > 17
static bool
ie_is_wpa_ie(uint8 **wpaie, uint8 **tlvs, int *tlvs_len)
{
/* Is this body of this tlvs entry a WPA entry? If */
/* not update the tlvs buffer pointer/length */
	uint8 *ie = *wpaie;

	/* If the contents match the WPA_OUI and type=1 */
	if ((ie[1] >= 6) &&
		!bcmp((const void *)&ie[2], (const void *)(WPA_OUI "\x01"), 4)) {
		return TRUE;
	}

	/* point to the next ie */
	ie += ie[1] + 2;
	/* calculate the length of the rest of the buffer */
	*tlvs_len -= (int)(ie - *tlvs);
	/* update the pointer to the start of the buffer */
	*tlvs = ie;
	return FALSE;
}

static bool
ie_is_wps_ie(uint8 **wpsie, uint8 **tlvs, int *tlvs_len)
{
/* Is this body of this tlvs entry a WPS entry? If */
/* not update the tlvs buffer pointer/length */
	uint8 *ie = *wpsie;

	/* If the contents match the WPA_OUI and type=4 */
	if ((ie[1] >= 4) &&
		!bcmp((const void *)&ie[2], (const void *)(WPA_OUI "\x04"), 4)) {
		return TRUE;
	}

	/* point to the next ie */
	ie += ie[1] + 2;
	/* calculate the length of the rest of the buffer */
	*tlvs_len -= (int)(ie - *tlvs);
	/* update the pointer to the start of the buffer */
	*tlvs = ie;
	return FALSE;
}
#endif /* WIRELESS_EXT > 17 */


static int
wl_iw_handle_scanresults_ies(char **event_p, char *end,
	struct iw_request_info *info, wl_bss_info_t *bi)
{
#if WIRELESS_EXT > 17
	struct iw_event	iwe;
	char *event;

	event = *event_p;
	if (bi->ie_length) {
		/* look for wpa/rsn ies in the ie list... */
		bcm_tlv_t *ie;
		uint8 *ptr = ((uint8 *)bi) + bi->ie_offset;
		int ptr_len = bi->ie_length;

		/* OSEN IE */
		if ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_VS_ID)) &&
			ie->len > WFA_OUI_LEN + 1 &&
			!bcmp((const void *)&ie->data[0], (const void *)WFA_OUI, WFA_OUI_LEN) &&
			ie->data[WFA_OUI_LEN] == WFA_OUI_TYPE_OSEN) {
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = ie->len + 2;
			event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)ie);
		}
		ptr = ((uint8 *)bi) + bi->ie_offset;

		if ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_RSN_ID))) {
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = ie->len + 2;
			event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)ie);
		}
		ptr = ((uint8 *)bi) + bi->ie_offset;

		if ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_MDIE_ID))) {
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = ie->len + 2;
			event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)ie);
		}
		ptr = ((uint8 *)bi) + bi->ie_offset;

		while ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_WPA_ID))) {
			/* look for WPS IE */
			if (ie_is_wps_ie(((uint8 **)&ie), &ptr, &ptr_len)) {
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = ie->len + 2;
				event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)ie);
				break;
			}
		}

		ptr = ((uint8 *)bi) + bi->ie_offset;
		ptr_len = bi->ie_length;
		while ((ie = bcm_parse_tlvs(ptr, ptr_len, DOT11_MNG_WPA_ID))) {
			if (ie_is_wpa_ie(((uint8 **)&ie), &ptr, &ptr_len)) {
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = ie->len + 2;
				event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)ie);
				break;
			}
		}

	*event_p = event;
	}

#endif /* WIRELESS_EXT > 17 */
	return 0;
}
static int
wl_iw_get_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	channel_info_t ci;
	wl_scan_results_t *list;
	struct iw_event	iwe;
	wl_bss_info_t *bi = NULL;
	int error, i, j;
	char *event = extra, *end = extra + dwrq->length, *value;
	uint buflen = dwrq->length;

	WL_TRACE(("%s: SIOCGIWSCAN\n", dev->name));

	if (!extra)
		return -EINVAL;

	/* Check for scan in progress */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(ci))))
		return error;
	ci.scan_channel = dtoh32(ci.scan_channel);
	if (ci.scan_channel)
		return -EAGAIN;

	/* Get scan results (too large to put on the stack) */
	list = kmalloc(buflen, GFP_KERNEL);
	if (!list)
		return -ENOMEM;
	memset(list, 0, buflen);
	list->buflen = htod32(buflen);
	if ((error = dev_wlc_ioctl(dev, WLC_SCAN_RESULTS, list, buflen))) {
		kfree(list);
		return error;
	}
	list->buflen = dtoh32(list->buflen);
	list->version = dtoh32(list->version);
	list->count = dtoh32(list->count);

	ASSERT(list->version == WL_BSS_INFO_VERSION);

	for (i = 0; i < list->count && i < IW_MAX_AP; i++) {
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : list->bss_info;
		ASSERT(((uintptr)bi + dtoh32(bi->length)) <= ((uintptr)list +
			buflen));

		/* First entry must be the BSSID */
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data, &bi->BSSID, ETHER_ADDR_LEN);
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_ADDR_LEN);

		/* SSID */
		iwe.u.data.length = dtoh32(bi->SSID_len);
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.flags = 1;
		event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, bi->SSID);

		/* Mode */
		if (dtoh16(bi->capability) & (DOT11_CAP_ESS | DOT11_CAP_IBSS)) {
			iwe.cmd = SIOCGIWMODE;
			if (dtoh16(bi->capability) & DOT11_CAP_ESS)
				iwe.u.mode = IW_MODE_INFRA;
			else
				iwe.u.mode = IW_MODE_ADHOC;
			event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_UINT_LEN);
		}

		/* Channel */
		iwe.cmd = SIOCGIWFREQ;

		iwe.u.freq.m = wf_channel2mhz(CHSPEC_CHANNEL(bi->chanspec),
			(CHSPEC_IS2G(bi->chanspec)) ?
			WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
		iwe.u.freq.e = 6;
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_FREQ_LEN);

		/* Channel quality */
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.qual = rssi_to_qual(dtoh16(bi->RSSI));
		iwe.u.qual.level = 0x100 + dtoh16(bi->RSSI);
		iwe.u.qual.noise = 0x100 + bi->phy_noise;
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_QUAL_LEN);

		/* WPA, WPA2, WPS, WAPI IEs */
		 wl_iw_handle_scanresults_ies(&event, end, info, bi);

		/* Encryption */
		iwe.cmd = SIOCGIWENCODE;
		if (dtoh16(bi->capability) & DOT11_CAP_PRIVACY)
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;
		iwe.u.data.length = 0;
		event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)event);

		/* Rates */
		if (bi->rateset.count) {
			value = event + IW_EV_LCP_LEN;
			iwe.cmd = SIOCGIWRATE;
			/* Those two flags are ignored... */
			iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
			for (j = 0; j < bi->rateset.count && j < IW_MAX_BITRATES; j++) {
				iwe.u.bitrate.value = (bi->rateset.rates[j] & 0x7f) * 500000;
				value = IWE_STREAM_ADD_VALUE(info, event, value, end, &iwe,
					IW_EV_PARAM_LEN);
			}
			event = value;
		}
	}

	kfree(list);

	dwrq->length = event - extra;
	dwrq->flags = 0;	/* todo */

	return 0;
}

static int
wl_iw_iscan_get_scan(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_scan_results_t *list;
	struct iw_event	iwe;
	wl_bss_info_t *bi = NULL;
	int ii, j;
	int apcnt;
	char *event = extra, *end = extra + dwrq->length, *value;
	iscan_info_t *iscan = g_iscan;
	iscan_buf_t * p_buf;

	WL_TRACE(("%s: SIOCGIWSCAN\n", dev->name));

	if (!extra)
		return -EINVAL;

	/* use backup if our thread is not successful */
	if ((!iscan) || (iscan->sysioc_pid < 0)) {
		return wl_iw_get_scan(dev, info, dwrq, extra);
	}

	/* Check for scan in progress */
	if (iscan->iscan_state == ISCAN_STATE_SCANING)
		return -EAGAIN;

	apcnt = 0;
	p_buf = iscan->list_hdr;
	/* Get scan results */
	while (p_buf != iscan->list_cur) {
	    list = &((wl_iscan_results_t*)p_buf->iscan_buf)->results;

	    if (list->version != WL_BSS_INFO_VERSION) {
		WL_ERROR(("list->version %d != WL_BSS_INFO_VERSION\n", list->version));
	    }

	    bi = NULL;
	    for (ii = 0; ii < list->count && apcnt < IW_MAX_AP; apcnt++, ii++) {
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : list->bss_info;
		ASSERT(((uintptr)bi + dtoh32(bi->length)) <= ((uintptr)list +
			WLC_IW_ISCAN_MAXLEN));

		/* overflow check cover fields before wpa IEs */
		if (event + ETHER_ADDR_LEN + bi->SSID_len + IW_EV_UINT_LEN + IW_EV_FREQ_LEN +
			IW_EV_QUAL_LEN >= end)
			return -E2BIG;
		/* First entry must be the BSSID */
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data, &bi->BSSID, ETHER_ADDR_LEN);
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_ADDR_LEN);

		/* SSID */
		iwe.u.data.length = dtoh32(bi->SSID_len);
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.flags = 1;
		event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, bi->SSID);

		/* Mode */
		if (dtoh16(bi->capability) & (DOT11_CAP_ESS | DOT11_CAP_IBSS)) {
			iwe.cmd = SIOCGIWMODE;
			if (dtoh16(bi->capability) & DOT11_CAP_ESS)
				iwe.u.mode = IW_MODE_INFRA;
			else
				iwe.u.mode = IW_MODE_ADHOC;
			event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_UINT_LEN);
		}

		/* Channel */
		iwe.cmd = SIOCGIWFREQ;

		iwe.u.freq.m = wf_channel2mhz(CHSPEC_CHANNEL(bi->chanspec),
			(CHSPEC_IS2G(bi->chanspec)) ?
			WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
		iwe.u.freq.e = 6;
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_FREQ_LEN);

		/* Channel quality */
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.qual = rssi_to_qual(dtoh16(bi->RSSI));
		iwe.u.qual.level = 0x100 + dtoh16(bi->RSSI);
		iwe.u.qual.noise = 0x100 + bi->phy_noise;
		event = IWE_STREAM_ADD_EVENT(info, event, end, &iwe, IW_EV_QUAL_LEN);

		/* WPA, WPA2, WPS, WAPI IEs */
		wl_iw_handle_scanresults_ies(&event, end, info, bi);

		/* Encryption */
		iwe.cmd = SIOCGIWENCODE;
		if (dtoh16(bi->capability) & DOT11_CAP_PRIVACY)
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;
		iwe.u.data.length = 0;
		event = IWE_STREAM_ADD_POINT(info, event, end, &iwe, (char *)event);

		/* Rates */
		if (bi->rateset.count <= sizeof(bi->rateset.rates)) {
			if (event + IW_MAX_BITRATES*IW_EV_PARAM_LEN >= end)
				return -E2BIG;

			value = event + IW_EV_LCP_LEN;
			iwe.cmd = SIOCGIWRATE;
			/* Those two flags are ignored... */
			iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
			for (j = 0; j < bi->rateset.count && j < IW_MAX_BITRATES; j++) {
				iwe.u.bitrate.value = (bi->rateset.rates[j] & 0x7f) * 500000;
				value = IWE_STREAM_ADD_VALUE(info, event, value, end, &iwe,
					IW_EV_PARAM_LEN);
			}
			event = value;
		}
	    }
	    p_buf = p_buf->next;
	} /* while (p_buf) */

	dwrq->length = event - extra;
	dwrq->flags = 0;	/* todo */

	return 0;
}

#endif /* WIRELESS_EXT > 13 */


static int
wl_iw_set_essid(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wlc_ssid_t ssid;
	int error;

	WL_TRACE(("%s: SIOCSIWESSID\n", dev->name));

	/* default Broadcast SSID */
	memset(&ssid, 0, sizeof(ssid));
	if (dwrq->length && extra) {
#if WIRELESS_EXT > 20
		ssid.SSID_len = MIN(sizeof(ssid.SSID), dwrq->length);
#else
		ssid.SSID_len = MIN(sizeof(ssid.SSID), dwrq->length-1);
#endif
		memcpy(ssid.SSID, extra, ssid.SSID_len);
		ssid.SSID_len = htod32(ssid.SSID_len);

		if ((error = dev_wlc_ioctl(dev, WLC_SET_SSID, &ssid, sizeof(ssid))))
			return error;
	}
	/* If essid null then it is "iwconfig <interface> essid off" command */
	else {
		scb_val_t scbval;
		bzero(&scbval, sizeof(scb_val_t));
		if ((error = dev_wlc_ioctl(dev, WLC_DISASSOC, &scbval, sizeof(scb_val_t))))
			return error;
	}
	return 0;
}

static int
wl_iw_get_essid(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wlc_ssid_t ssid;
	int error;

	WL_TRACE(("%s: SIOCGIWESSID\n", dev->name));

	if (!extra)
		return -EINVAL;

	if ((error = dev_wlc_ioctl(dev, WLC_GET_SSID, &ssid, sizeof(ssid)))) {
		WL_ERROR(("Error getting the SSID\n"));
		return error;
	}

	ssid.SSID_len = dtoh32(ssid.SSID_len);

	/* Get the current SSID */
	memcpy(extra, ssid.SSID, ssid.SSID_len);

	dwrq->length = ssid.SSID_len;

	dwrq->flags = 1; /* active */

	return 0;
}

static int
wl_iw_set_nick(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_iw_t *iw = IW_DEV_IF(dev);
	WL_TRACE(("%s: SIOCSIWNICKN\n", dev->name));

	if (!extra)
		return -EINVAL;

	/* Check the size of the string */
	if (dwrq->length > sizeof(iw->nickname))
		return -E2BIG;

	memcpy(iw->nickname, extra, dwrq->length);
	iw->nickname[dwrq->length - 1] = '\0';

	return 0;
}

static int
wl_iw_get_nick(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_iw_t *iw = IW_DEV_IF(dev);
	WL_TRACE(("%s: SIOCGIWNICKN\n", dev->name));

	if (!extra)
		return -EINVAL;

	strcpy(extra, iw->nickname);
	dwrq->length = strlen(extra) + 1;

	return 0;
}

static int wl_iw_set_rate(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	wl_rateset_t rateset;
	int error, rate, i, error_bg, error_a;

	WL_TRACE(("%s: SIOCSIWRATE\n", dev->name));

	/* Get current rateset */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_CURR_RATESET, &rateset, sizeof(rateset))))
		return error;

	rateset.count = dtoh32(rateset.count);

	if (vwrq->value < 0) {
		/* Select maximum rate */
		rate = rateset.rates[rateset.count - 1] & 0x7f;
	} else if (vwrq->value < rateset.count) {
		/* Select rate by rateset index */
		rate = rateset.rates[vwrq->value] & 0x7f;
	} else {
		/* Specified rate in bps */
		rate = vwrq->value / 500000;
	}

	if (vwrq->fixed) {
		/*
			Set rate override,
			Since the is a/b/g-blind, both a/bg_rate are enforced.
		*/
		error_bg = dev_wlc_intvar_set(dev, "bg_rate", rate);
		error_a = dev_wlc_intvar_set(dev, "a_rate", rate);

		if (error_bg && error_a)
			return (error_bg | error_a);
	} else {
		/*
			clear rate override
			Since the is a/b/g-blind, both a/bg_rate are enforced.
		*/
		/* 0 is for clearing rate override */
		error_bg = dev_wlc_intvar_set(dev, "bg_rate", 0);
		/* 0 is for clearing rate override */
		error_a = dev_wlc_intvar_set(dev, "a_rate", 0);

		if (error_bg && error_a)
			return (error_bg | error_a);

		/* Remove rates above selected rate */
		for (i = 0; i < rateset.count; i++)
			if ((rateset.rates[i] & 0x7f) > rate)
				break;
		rateset.count = htod32(i);

		/* Set current rateset */
		if ((error = dev_wlc_ioctl(dev, WLC_SET_RATESET, &rateset, sizeof(rateset))))
			return error;
	}

	return 0;
}

static int wl_iw_get_rate(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, rate;

	WL_TRACE(("%s: SIOCGIWRATE\n", dev->name));

	/* Report the current tx rate */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_RATE, &rate, sizeof(rate))))
		return error;
	rate = dtoh32(rate);
	vwrq->value = rate * 500000;

	return 0;
}

static int
wl_iw_set_rts(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, rts;

	WL_TRACE(("%s: SIOCSIWRTS\n", dev->name));

	if (vwrq->disabled)
		rts = DOT11_DEFAULT_RTS_LEN;
	else if (vwrq->value < 0 || vwrq->value > DOT11_DEFAULT_RTS_LEN)
		return -EINVAL;
	else
		rts = vwrq->value;

	if ((error = dev_wlc_intvar_set(dev, "rtsthresh", rts)))
		return error;

	return 0;
}

static int
wl_iw_get_rts(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, rts;

	WL_TRACE(("%s: SIOCGIWRTS\n", dev->name));

	if ((error = dev_wlc_intvar_get(dev, "rtsthresh", &rts)))
		return error;

	vwrq->value = rts;
	vwrq->disabled = (rts >= DOT11_DEFAULT_RTS_LEN);
	vwrq->fixed = 1;

	return 0;
}

static int
wl_iw_set_frag(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, frag;

	WL_TRACE(("%s: SIOCSIWFRAG\n", dev->name));

	if (vwrq->disabled)
		frag = DOT11_DEFAULT_FRAG_LEN;
	else if (vwrq->value < 0 || vwrq->value > DOT11_DEFAULT_FRAG_LEN)
		return -EINVAL;
	else
		frag = vwrq->value;

	if ((error = dev_wlc_intvar_set(dev, "fragthresh", frag)))
		return error;

	return 0;
}

static int
wl_iw_get_frag(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, fragthreshold;

	WL_TRACE(("%s: SIOCGIWFRAG\n", dev->name));

	if ((error = dev_wlc_intvar_get(dev, "fragthresh", &fragthreshold)))
		return error;

	vwrq->value = fragthreshold;
	vwrq->disabled = (fragthreshold >= DOT11_DEFAULT_FRAG_LEN);
	vwrq->fixed = 1;

	return 0;
}

static int
wl_iw_set_txpow(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, disable;
	uint16 txpwrmw;
	WL_TRACE(("%s: SIOCSIWTXPOW\n", dev->name));

	/* Make sure radio is off or on as far as software is concerned */
	disable = vwrq->disabled ? WL_RADIO_SW_DISABLE : 0;
	disable += WL_RADIO_SW_DISABLE << 16;

	disable = htod32(disable);
	if ((error = dev_wlc_ioctl(dev, WLC_SET_RADIO, &disable, sizeof(disable))))
		return error;

	/* If Radio is off, nothing more to do */
	if (disable & WL_RADIO_SW_DISABLE)
		return 0;

	/* Only handle mW */
	if (!(vwrq->flags & IW_TXPOW_MWATT))
		return -EINVAL;

	/* Value < 0 means just "on" or "off" */
	if (vwrq->value < 0)
		return 0;

	if (vwrq->value > 0xffff) txpwrmw = 0xffff;
	else txpwrmw = (uint16)vwrq->value;


	error = dev_wlc_intvar_set(dev, "qtxpower", (int)(bcm_mw_to_qdbm(txpwrmw)));
	return error;
}

static int
wl_iw_get_txpow(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, disable, txpwrdbm;
	uint8 result;

	WL_TRACE(("%s: SIOCGIWTXPOW\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_RADIO, &disable, sizeof(disable))) ||
	    (error = dev_wlc_intvar_get(dev, "qtxpower", &txpwrdbm)))
		return error;

	disable = dtoh32(disable);
	result = (uint8)(txpwrdbm & ~WL_TXPWR_OVERRIDE);
	vwrq->value = (int32)bcm_qdbm_to_mw(result);
	vwrq->fixed = 0;
	vwrq->disabled = (disable & (WL_RADIO_SW_DISABLE | WL_RADIO_HW_DISABLE)) ? 1 : 0;
	vwrq->flags = IW_TXPOW_MWATT;

	return 0;
}

#if WIRELESS_EXT > 10
static int
wl_iw_set_retry(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, lrl, srl;

	WL_TRACE(("%s: SIOCSIWRETRY\n", dev->name));

	/* Do not handle "off" or "lifetime" */
	if (vwrq->disabled || (vwrq->flags & IW_RETRY_LIFETIME))
		return -EINVAL;

	/* Handle "[min|max] limit" */
	if (vwrq->flags & IW_RETRY_LIMIT) {
		/* "max limit" or just "limit" */
#if WIRELESS_EXT > 20
		if ((vwrq->flags & IW_RETRY_LONG) ||(vwrq->flags & IW_RETRY_MAX) ||
			!((vwrq->flags & IW_RETRY_SHORT) || (vwrq->flags & IW_RETRY_MIN))) {
#else
		if ((vwrq->flags & IW_RETRY_MAX) || !(vwrq->flags & IW_RETRY_MIN)) {
#endif /* WIRELESS_EXT > 20 */

			lrl = htod32(vwrq->value);
			if ((error = dev_wlc_ioctl(dev, WLC_SET_LRL, &lrl, sizeof(lrl))))
				return error;
		}
		/* "min limit" or just "limit" */
#if WIRELESS_EXT > 20
		if ((vwrq->flags & IW_RETRY_SHORT) ||(vwrq->flags & IW_RETRY_MIN) ||
			!((vwrq->flags & IW_RETRY_LONG) || (vwrq->flags & IW_RETRY_MAX))) {
#else
		if ((vwrq->flags & IW_RETRY_MIN) || !(vwrq->flags & IW_RETRY_MAX)) {
#endif /* WIRELESS_EXT > 20 */

			srl = htod32(vwrq->value);
			if ((error = dev_wlc_ioctl(dev, WLC_SET_SRL, &srl, sizeof(srl))))
				return error;
		}
	}

	return 0;
}

static int
wl_iw_get_retry(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, lrl, srl;

	WL_TRACE(("%s: SIOCGIWRETRY\n", dev->name));

	vwrq->disabled = 0;      /* Can't be disabled */

	/* Do not handle lifetime queries */
	if ((vwrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME)
		return -EINVAL;

	/* Get retry limits */
	if ((error = dev_wlc_ioctl(dev, WLC_GET_LRL, &lrl, sizeof(lrl))) ||
	    (error = dev_wlc_ioctl(dev, WLC_GET_SRL, &srl, sizeof(srl))))
		return error;

	lrl = dtoh32(lrl);
	srl = dtoh32(srl);

	/* Note : by default, display the min retry number */
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
#endif /* WIRELESS_EXT > 10 */

static int
wl_iw_set_encode(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_wsec_key_t key;
	int error, val, wsec;

	WL_TRACE(("%s: SIOCSIWENCODE\n", dev->name));

	memset(&key, 0, sizeof(key));

	if ((dwrq->flags & IW_ENCODE_INDEX) == 0) {
		/* Find the current key */
		for (key.index = 0; key.index < DOT11_MAX_DEFAULT_KEYS; key.index++) {
			val = htod32(key.index);
			if ((error = dev_wlc_ioctl(dev, WLC_GET_KEY_PRIMARY, &val, sizeof(val))))
				return error;
			val = dtoh32(val);
			if (val)
				break;
		}
		/* Default to 0 */
		if (key.index == DOT11_MAX_DEFAULT_KEYS)
			key.index = 0;
	} else {
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;
		if (key.index >= DOT11_MAX_DEFAULT_KEYS)
			return -EINVAL;
	}

	/* Interpret "off" to mean no encryption */
	wsec = (dwrq->flags & IW_ENCODE_DISABLED) ? 0 : WEP_ENABLED;

	if ((error = dev_wlc_intvar_set(dev, "wsec", wsec)))
		return error;

	/* Old API used to pass a NULL pointer instead of IW_ENCODE_NOKEY */
	if (!extra || !dwrq->length || (dwrq->flags & IW_ENCODE_NOKEY)) {
		/* Just select a new current key */
		val = htod32(key.index);
		if ((error = dev_wlc_ioctl(dev, WLC_SET_KEY_PRIMARY, &val, sizeof(val))))
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 14)
		case TKIP_KEY_SIZE:
			key.algo = CRYPTO_ALGO_TKIP;
			break;
#endif
		case AES_KEY_SIZE:
			key.algo = CRYPTO_ALGO_AES_CCM;
			break;
		default:
			return -EINVAL;
		}

		/* Set the new key/index */
		swap_key_from_BE(&key);
		if ((error = dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key))))
			return error;
	}

	/* Interpret "restricted" to mean shared key authentication */
	val = (dwrq->flags & IW_ENCODE_RESTRICTED) ? 1 : 0;
	val = htod32(val);
	if ((error = dev_wlc_ioctl(dev, WLC_SET_AUTH, &val, sizeof(val))))
		return error;

	return 0;
}

static int
wl_iw_get_encode(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_wsec_key_t key;
	int error, val, wsec, auth;

	WL_TRACE(("%s: SIOCGIWENCODE\n", dev->name));

	/* assure default values of zero for things we don't touch */
	bzero(&key, sizeof(wl_wsec_key_t));

	if ((dwrq->flags & IW_ENCODE_INDEX) == 0) {
		/* Find the current key */
		for (key.index = 0; key.index < DOT11_MAX_DEFAULT_KEYS; key.index++) {
			val = key.index;
			if ((error = dev_wlc_ioctl(dev, WLC_GET_KEY_PRIMARY, &val, sizeof(val))))
				return error;
			val = dtoh32(val);
			if (val)
				break;
		}
	} else
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	if (key.index >= DOT11_MAX_DEFAULT_KEYS)
		key.index = 0;

	/* Get info */

	if ((error = dev_wlc_ioctl(dev, WLC_GET_WSEC, &wsec, sizeof(wsec))) ||
	    (error = dev_wlc_ioctl(dev, WLC_GET_AUTH, &auth, sizeof(auth))))
		return error;

	swap_key_to_BE(&key);

	wsec = dtoh32(wsec);
	auth = dtoh32(auth);
	/* Get key length */
	dwrq->length = MIN(IW_ENCODING_TOKEN_MAX, key.len);

	/* Get flags */
	dwrq->flags = key.index + 1;
	if (!(wsec & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))) {
		/* Interpret "off" to mean no encryption */
		dwrq->flags |= IW_ENCODE_DISABLED;
	}
	if (auth) {
		/* Interpret "restricted" to mean shared key authentication */
		dwrq->flags |= IW_ENCODE_RESTRICTED;
	}

	/* Get key */
	if (dwrq->length && extra)
		memcpy(extra, key.data, dwrq->length);

	return 0;
}

static int
wl_iw_set_power(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, pm;

	WL_TRACE(("%s: SIOCSIWPOWER\n", dev->name));

	pm = vwrq->disabled ? PM_OFF : PM_MAX;

	pm = htod32(pm);
	if ((error = dev_wlc_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm))))
		return error;

	return 0;
}

static int
wl_iw_get_power(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error, pm;

	WL_TRACE(("%s: SIOCGIWPOWER\n", dev->name));

	if ((error = dev_wlc_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm))))
		return error;

	pm = dtoh32(pm);
	vwrq->disabled = pm ? 0 : 1;
	vwrq->flags = IW_POWER_ALL_R;

	return 0;
}

#if WIRELESS_EXT > 17
static int
wl_iw_set_wpaie(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *iwp,
	char *extra
)
{
		dev_wlc_bufvar_set(dev, "wpaie", extra, iwp->length);

	return 0;
}

static int
wl_iw_get_wpaie(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *iwp,
	char *extra
)
{
	WL_TRACE(("%s: SIOCGIWGENIE\n", dev->name));
	iwp->length = 64;
	dev_wlc_bufvar_get(dev, "wpaie", extra, iwp->length);
	return 0;
}

static int
wl_iw_set_encodeext(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_point *dwrq,
	char *extra
)
{
	wl_wsec_key_t key;
	int error;
	struct iw_encode_ext *iwe;

	WL_TRACE(("%s: SIOCSIWENCODEEXT\n", dev->name));

	memset(&key, 0, sizeof(key));
	iwe = (struct iw_encode_ext *)extra;

	/* disable encryption completely  */
	if (dwrq->flags & IW_ENCODE_DISABLED) {

	}

	/* get the key index */
	key.index = 0;
	if (dwrq->flags & IW_ENCODE_INDEX)
		key.index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	key.len = iwe->key_len;

	/* Instead of bcast for ea address for default wep keys, driver needs it to be Null */
	if (!ETHER_ISMULTI(iwe->addr.sa_data))
		bcopy((void *)&iwe->addr.sa_data, (char *)&key.ea, ETHER_ADDR_LEN);

	/* check for key index change */
	if (key.len == 0) {
		if (iwe->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
			WL_WSEC(("Changing the the primary Key to %d\n", key.index));
			/* change the key index .... */
			key.index = htod32(key.index);
			error = dev_wlc_ioctl(dev, WLC_SET_KEY_PRIMARY,
				&key.index, sizeof(key.index));
			if (error)
				return error;
		}
		/* key delete */
		else {
			swap_key_from_BE(&key);
			error = dev_wlc_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
			if (error)
				return error;
		}
	}
	/* This case is used to allow an external 802.1x supplicant
	 * to pass the PMK to the in-driver supplicant for use in
	 * the 4-way handshake.
	 */
	else if (iwe->alg == IW_ENCODE_ALG_PMK) {
		int j;
		wsec_pmk_t pmk;
		char keystring[WSEC_MAX_PSK_LEN + 1];
		char* charptr = keystring;
		uint len;

		/* copy the raw hex key to the appropriate format */
		for (j = 0; j < (WSEC_MAX_PSK_LEN / 2); j++) {
			(void)snprintf(charptr, 3, "%02x", iwe->key[j]);
			charptr += 2;
		}
		len = strlen(keystring);
		pmk.key_len = htod16(len);
		bcopy(keystring, pmk.key, len);
		pmk.flags = htod16(WSEC_PASSPHRASE);

		error = dev_wlc_ioctl(dev, WLC_SET_WSEC_PMK, &pmk, sizeof(pmk));
		if (error)
			return error;
	}

	else {
		if (iwe->key_len > sizeof(key.data))
			return -EINVAL;

		WL_WSEC(("Setting the key index %d\n", key.index));
		if (iwe->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
			WL_WSEC(("key is a Primary Key\n"));
			key.flags = WL_PRIMARY_KEY;
		}

		bcopy((void *)iwe->key, key.data, iwe->key_len);

		if (iwe->alg == IW_ENCODE_ALG_TKIP) {
			uint8 keybuf[8];
			bcopy(&key.data[24], keybuf, sizeof(keybuf));
			bcopy(&key.data[16], &key.data[24], sizeof(keybuf));
			bcopy(keybuf, &key.data[16], sizeof(keybuf));
		}

		/* rx iv */
		if (iwe->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
			uchar *ivptr;
			ivptr = (uchar *)iwe->rx_seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
				(ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = TRUE;
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
	pmkid_t foo[MAXPMKID-1];
} pmkid_list;
static int
wl_iw_set_pmksa(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	struct iw_pmksa *iwpmksa;
	uint i;
	char eabuf[ETHER_ADDR_STR_LEN];
	pmkid_t * pmkid_array = pmkid_list.pmkids.pmkid;

	WL_TRACE(("%s: SIOCSIWPMKSA\n", dev->name));
	iwpmksa = (struct iw_pmksa *)extra;
	bzero((char *)eabuf, ETHER_ADDR_STR_LEN);
	if (iwpmksa->cmd == IW_PMKSA_FLUSH) {
		WL_TRACE(("wl_iw_set_pmksa - IW_PMKSA_FLUSH\n"));
		bzero((char *)&pmkid_list, sizeof(pmkid_list));
	}
	if (iwpmksa->cmd == IW_PMKSA_REMOVE) {
		pmkid_list_t pmkid, *pmkidptr;
		pmkidptr = &pmkid;
		bcopy(&iwpmksa->bssid.sa_data[0], &pmkidptr->pmkid[0].BSSID, ETHER_ADDR_LEN);
		bcopy(&iwpmksa->pmkid[0], &pmkidptr->pmkid[0].PMKID, WPA2_PMKID_LEN);
		{
			uint j;
			WL_TRACE(("wl_iw_set_pmksa,IW_PMKSA_REMOVE - PMKID: %s = ",
				bcm_ether_ntoa(&pmkidptr->pmkid[0].BSSID,
				eabuf)));
			for (j = 0; j < WPA2_PMKID_LEN; j++)
				WL_TRACE(("%02x ", pmkidptr->pmkid[0].PMKID[j]));
			WL_TRACE(("\n"));
		}
		for (i = 0; i < pmkid_list.pmkids.npmkid; i++)
			if (!bcmp(&iwpmksa->bssid.sa_data[0], &pmkid_array[i].BSSID,
				ETHER_ADDR_LEN))
				break;
		for (; i < pmkid_list.pmkids.npmkid; i++) {
			bcopy(&pmkid_array[i+1].BSSID,
				&pmkid_array[i].BSSID,
				ETHER_ADDR_LEN);
			bcopy(&pmkid_array[i+1].PMKID,
				&pmkid_array[i].PMKID,
				WPA2_PMKID_LEN);
		}
		pmkid_list.pmkids.npmkid--;
	}
	if (iwpmksa->cmd == IW_PMKSA_ADD) {
		bcopy(&iwpmksa->bssid.sa_data[0],
			&pmkid_array[pmkid_list.pmkids.npmkid].BSSID,
			ETHER_ADDR_LEN);
		bcopy(&iwpmksa->pmkid[0], &pmkid_array[pmkid_list.pmkids.npmkid].PMKID,
			WPA2_PMKID_LEN);
		{
			uint j;
			uint k;
			k = pmkid_list.pmkids.npmkid;
			BCM_REFERENCE(k);
			WL_TRACE(("wl_iw_set_pmksa,IW_PMKSA_ADD - PMKID: %s = ",
				bcm_ether_ntoa(&pmkid_array[k].BSSID,
				eabuf)));
			for (j = 0; j < WPA2_PMKID_LEN; j++)
				WL_TRACE(("%02x ", pmkid_array[k].PMKID[j]));
			WL_TRACE(("\n"));
		}
		pmkid_list.pmkids.npmkid++;
	}
	WL_TRACE(("PRINTING pmkid LIST - No of elements %d\n", pmkid_list.pmkids.npmkid));
	for (i = 0; i < pmkid_list.pmkids.npmkid; i++) {
		uint j;
		WL_TRACE(("PMKID[%d]: %s = ", i,
			bcm_ether_ntoa(&pmkid_array[i].BSSID,
			eabuf)));
		for (j = 0; j < WPA2_PMKID_LEN; j++)
			WL_TRACE(("%02x ", pmkid_array[i].PMKID[j]));
		printf("\n");
	}
	WL_TRACE(("\n"));
	dev_wlc_bufvar_set(dev, "pmkid_info", (char *)&pmkid_list, sizeof(pmkid_list));
	return 0;
}
#endif /* WIRELESS_EXT > 17 */

static int
wl_iw_get_encodeext(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	WL_TRACE(("%s: SIOCGIWENCODEEXT\n", dev->name));
	return 0;
}

static int
wl_iw_set_wpaauth(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error = 0;
	int paramid;
	int paramval;
	uint32 cipher_combined;
	int val = 0;
	wl_iw_t *iw = IW_DEV_IF(dev);

	WL_TRACE(("%s: SIOCSIWAUTH\n", dev->name));

	paramid = vwrq->flags & IW_AUTH_INDEX;
	paramval = vwrq->value;

	WL_TRACE(("%s: SIOCSIWAUTH, paramid = 0x%0x, paramval = 0x%0x\n",
		dev->name, paramid, paramval));

	switch (paramid) {

	case IW_AUTH_WPA_VERSION:
		/* supported wpa version disabled or wpa or wpa2 */
		if (paramval & IW_AUTH_WPA_VERSION_DISABLED)
			val = WPA_AUTH_DISABLED;
		else if (paramval & (IW_AUTH_WPA_VERSION_WPA))
			val = WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED;
		else if (paramval & IW_AUTH_WPA_VERSION_WPA2)
			val = WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED;
		WL_TRACE(("%s: %d: setting wpa_auth to 0x%0x\n", __FUNCTION__, __LINE__, val));
		if ((error = dev_wlc_intvar_set(dev, "wpa_auth", val)))
			return error;
		break;

	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP: {
		int fbt_cap = 0;

		if (paramid == IW_AUTH_CIPHER_PAIRWISE) {
			iw->pwsec = paramval;
		}
		else {
			iw->gwsec = paramval;
		}

		if ((error = dev_wlc_intvar_get(dev, "wsec", &val)))
			return error;

		cipher_combined = iw->gwsec | iw->pwsec;
		val &= ~(WEP_ENABLED | TKIP_ENABLED | AES_ENABLED);
		if (cipher_combined & (IW_AUTH_CIPHER_WEP40 | IW_AUTH_CIPHER_WEP104))
			val |= WEP_ENABLED;
		if (cipher_combined & IW_AUTH_CIPHER_TKIP)
			val |= TKIP_ENABLED;
		if (cipher_combined & IW_AUTH_CIPHER_CCMP)
			val |= AES_ENABLED;

		if (iw->privacy_invoked && !val) {
			WL_WSEC(("%s: %s: 'Privacy invoked' TRUE but clearing wsec, assuming "
			         "we're a WPS enrollee\n", dev->name, __FUNCTION__));
			if ((error = dev_wlc_intvar_set(dev, "is_WPS_enrollee", TRUE))) {
				WL_WSEC(("Failed to set iovar is_WPS_enrollee\n"));
				return error;
			}
		} else if (val) {
			if ((error = dev_wlc_intvar_set(dev, "is_WPS_enrollee", FALSE))) {
				WL_WSEC(("Failed to clear iovar is_WPS_enrollee\n"));
				return error;
			}
		}

		if ((error = dev_wlc_intvar_set(dev, "wsec", val)))
			return error;

		/* Ensure in-dongle supplicant is turned on when FBT wants to do the 4-way
		 * handshake.
		 */
		if (dev_wlc_intvar_get(dev, "fbt_cap", &fbt_cap) == 0) {
			if (fbt_cap == WLC_FBT_CAP_DRV_4WAY_AND_REASSOC) {
				if ((paramid == IW_AUTH_CIPHER_PAIRWISE) && (val & AES_ENABLED)) {
					if ((error = dev_wlc_intvar_set(dev, "sup_wpa", 1)))
						return error;
				}
				else if (val == 0) {
					if ((error = dev_wlc_intvar_set(dev, "sup_wpa", 0)))
						return error;
				}
			}
		}
		break;
	}

	case IW_AUTH_KEY_MGMT:
		if ((error = dev_wlc_intvar_get(dev, "wpa_auth", &val)))
			return error;

		if (val & (WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED)) {
			if (paramval & (IW_AUTH_KEY_MGMT_FT_PSK | IW_AUTH_KEY_MGMT_PSK))
				val = WPA_AUTH_PSK;
			else
				val = WPA_AUTH_UNSPECIFIED;
			if (paramval & (IW_AUTH_KEY_MGMT_FT_802_1X | IW_AUTH_KEY_MGMT_FT_PSK))
				val |= WPA2_AUTH_FT;
		}
		else if (val & (WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED)) {
			if (paramval & (IW_AUTH_KEY_MGMT_FT_PSK | IW_AUTH_KEY_MGMT_PSK))
				val = WPA2_AUTH_PSK;
			else
				val = WPA2_AUTH_UNSPECIFIED;
			if (paramval & (IW_AUTH_KEY_MGMT_FT_802_1X | IW_AUTH_KEY_MGMT_FT_PSK))
				val |= WPA2_AUTH_FT;
		}
		WL_TRACE(("%s: %d: setting wpa_auth to %d\n", __FUNCTION__, __LINE__, val));
		if ((error = dev_wlc_intvar_set(dev, "wpa_auth", val)))
			return error;
		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:
		dev_wlc_bufvar_set(dev, "tkip_countermeasures", (char *)&paramval, 1);
		break;

	case IW_AUTH_80211_AUTH_ALG:
		/* open shared */
		WL_ERROR(("Setting the D11auth %d\n", paramval));
		if (paramval & IW_AUTH_ALG_OPEN_SYSTEM)
			val = 0;
		else if (paramval & IW_AUTH_ALG_SHARED_KEY)
			val = 1;
		else
			error = 1;
		if (!error && (error = dev_wlc_intvar_set(dev, "auth", val)))
			return error;
		break;

	case IW_AUTH_WPA_ENABLED:
		if (paramval == 0) {
			val = 0;
			WL_TRACE(("%s: %d: setting wpa_auth to %d\n", __FUNCTION__, __LINE__, val));
			error = dev_wlc_intvar_set(dev, "wpa_auth", val);
			return error;
		}
		else {
			/* If WPA is enabled, wpa_auth is set elsewhere */
		}
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		dev_wlc_bufvar_set(dev, "wsec_restrict", (char *)&paramval, 1);
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		dev_wlc_bufvar_set(dev, "rx_unencrypted_eapol", (char *)&paramval, 1);
		break;

#if WIRELESS_EXT > 17

	case IW_AUTH_ROAMING_CONTROL:
		WL_TRACE(("%s: IW_AUTH_ROAMING_CONTROL\n", __FUNCTION__));
		/* driver control or user space app control */
		break;

	case IW_AUTH_PRIVACY_INVOKED: {
		int wsec;

		if (paramval == 0) {
			iw->privacy_invoked = FALSE;
			if ((error = dev_wlc_intvar_set(dev, "is_WPS_enrollee", FALSE))) {
				WL_WSEC(("Failed to clear iovar is_WPS_enrollee\n"));
				return error;
			}
		} else {
			iw->privacy_invoked = TRUE;
			if ((error = dev_wlc_intvar_get(dev, "wsec", &wsec)))
				return error;

			if (!WSEC_ENABLED(wsec)) {
				/* if privacy is true, but wsec is false, we are a WPS enrollee */
				if ((error = dev_wlc_intvar_set(dev, "is_WPS_enrollee", TRUE))) {
					WL_WSEC(("Failed to set iovar is_WPS_enrollee\n"));
					return error;
				}
			} else {
				if ((error = dev_wlc_intvar_set(dev, "is_WPS_enrollee", FALSE))) {
					WL_WSEC(("Failed to clear iovar is_WPS_enrollee\n"));
					return error;
				}
			}
		}
		break;
	}


#endif /* WIRELESS_EXT > 17 */


	default:
		break;
	}
	return 0;
}
#define VAL_PSK(_val) (((_val) & WPA_AUTH_PSK) || ((_val) & WPA2_AUTH_PSK))

static int
wl_iw_get_wpaauth(
	struct net_device *dev,
	struct iw_request_info *info,
	struct iw_param *vwrq,
	char *extra
)
{
	int error;
	int paramid;
	int paramval = 0;
	int val;
	wl_iw_t *iw = IW_DEV_IF(dev);

	WL_TRACE(("%s: SIOCGIWAUTH\n", dev->name));

	paramid = vwrq->flags & IW_AUTH_INDEX;

	switch (paramid) {
	case IW_AUTH_WPA_VERSION:
		/* supported wpa version disabled or wpa or wpa2 */
		if ((error = dev_wlc_intvar_get(dev, "wpa_auth", &val)))
			return error;
		if (val & (WPA_AUTH_NONE | WPA_AUTH_DISABLED))
			paramval = IW_AUTH_WPA_VERSION_DISABLED;
		else if (val & (WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED))
			paramval = IW_AUTH_WPA_VERSION_WPA;
		else if (val & (WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED))
			paramval = IW_AUTH_WPA_VERSION_WPA2;
		break;

	case IW_AUTH_CIPHER_PAIRWISE:
		paramval = iw->pwsec;
		break;

	case IW_AUTH_CIPHER_GROUP:
		paramval = iw->gwsec;
		break;

	case IW_AUTH_KEY_MGMT:
		/* psk, 1x */
		if ((error = dev_wlc_intvar_get(dev, "wpa_auth", &val)))
			return error;
		if (VAL_PSK(val))
			paramval = IW_AUTH_KEY_MGMT_PSK;
		else
			paramval = IW_AUTH_KEY_MGMT_802_1X;

		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		dev_wlc_bufvar_get(dev, "tkip_countermeasures", (char *)&paramval, 1);
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		dev_wlc_bufvar_get(dev, "wsec_restrict", (char *)&paramval, 1);
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		dev_wlc_bufvar_get(dev, "rx_unencrypted_eapol", (char *)&paramval, 1);
		break;

	case IW_AUTH_80211_AUTH_ALG:
		/* open, shared, leap */
		if ((error = dev_wlc_intvar_get(dev, "auth", &val)))
			return error;
		if (!val)
			paramval = IW_AUTH_ALG_OPEN_SYSTEM;
		else
			paramval = IW_AUTH_ALG_SHARED_KEY;
		break;
	case IW_AUTH_WPA_ENABLED:
		if ((error = dev_wlc_intvar_get(dev, "wpa_auth", &val)))
			return error;
		if (val)
			paramval = TRUE;
		else
			paramval = FALSE;
		break;

#if WIRELESS_EXT > 17

	case IW_AUTH_ROAMING_CONTROL:
		WL_ERROR(("%s: IW_AUTH_ROAMING_CONTROL\n", __FUNCTION__));
		/* driver control or user space app control */
		break;

	case IW_AUTH_PRIVACY_INVOKED:
		paramval = iw->privacy_invoked;
		break;

#endif /* WIRELESS_EXT > 17 */
	}
	vwrq->value = paramval;
	return 0;
}
#endif /* WIRELESS_EXT > 17 */

static const iw_handler wl_iw_handler[] =
{
	(iw_handler) wl_iw_config_commit,	/* SIOCSIWCOMMIT */
	(iw_handler) wl_iw_get_name,		/* SIOCGIWNAME */
	(iw_handler) NULL,			/* SIOCSIWNWID */
	(iw_handler) NULL,			/* SIOCGIWNWID */
	(iw_handler) wl_iw_set_freq,		/* SIOCSIWFREQ */
	(iw_handler) wl_iw_get_freq,		/* SIOCGIWFREQ */
	(iw_handler) wl_iw_set_mode,		/* SIOCSIWMODE */
	(iw_handler) wl_iw_get_mode,		/* SIOCGIWMODE */
	(iw_handler) NULL,			/* SIOCSIWSENS */
	(iw_handler) NULL,			/* SIOCGIWSENS */
	(iw_handler) NULL,			/* SIOCSIWRANGE */
	(iw_handler) wl_iw_get_range,		/* SIOCGIWRANGE */
	(iw_handler) NULL,			/* SIOCSIWPRIV */
	(iw_handler) NULL,			/* SIOCGIWPRIV */
	(iw_handler) NULL,			/* SIOCSIWSTATS */
	(iw_handler) NULL,			/* SIOCGIWSTATS */
	(iw_handler) wl_iw_set_spy,		/* SIOCSIWSPY */
	(iw_handler) wl_iw_get_spy,		/* SIOCGIWSPY */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) wl_iw_set_wap,		/* SIOCSIWAP */
	(iw_handler) wl_iw_get_wap,		/* SIOCGIWAP */
#if WIRELESS_EXT > 17
	(iw_handler) wl_iw_mlme,		/* SIOCSIWMLME */
#else
	(iw_handler) NULL,			/* -- hole -- */
#endif
	(iw_handler) wl_iw_iscan_get_aplist,	/* SIOCGIWAPLIST */
#if WIRELESS_EXT > 13
	(iw_handler) wl_iw_iscan_set_scan,	/* SIOCSIWSCAN */
	(iw_handler) wl_iw_iscan_get_scan,	/* SIOCGIWSCAN */
#else	/* WIRELESS_EXT > 13 */
	(iw_handler) NULL,			/* SIOCSIWSCAN */
	(iw_handler) NULL,			/* SIOCGIWSCAN */
#endif	/* WIRELESS_EXT > 13 */
	(iw_handler) wl_iw_set_essid,		/* SIOCSIWESSID */
	(iw_handler) wl_iw_get_essid,		/* SIOCGIWESSID */
	(iw_handler) wl_iw_set_nick,		/* SIOCSIWNICKN */
	(iw_handler) wl_iw_get_nick,		/* SIOCGIWNICKN */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) wl_iw_set_rate,		/* SIOCSIWRATE */
	(iw_handler) wl_iw_get_rate,		/* SIOCGIWRATE */
	(iw_handler) wl_iw_set_rts,		/* SIOCSIWRTS */
	(iw_handler) wl_iw_get_rts,		/* SIOCGIWRTS */
	(iw_handler) wl_iw_set_frag,		/* SIOCSIWFRAG */
	(iw_handler) wl_iw_get_frag,		/* SIOCGIWFRAG */
	(iw_handler) wl_iw_set_txpow,		/* SIOCSIWTXPOW */
	(iw_handler) wl_iw_get_txpow,		/* SIOCGIWTXPOW */
#if WIRELESS_EXT > 10
	(iw_handler) wl_iw_set_retry,		/* SIOCSIWRETRY */
	(iw_handler) wl_iw_get_retry,		/* SIOCGIWRETRY */
#endif /* WIRELESS_EXT > 10 */
	(iw_handler) wl_iw_set_encode,		/* SIOCSIWENCODE */
	(iw_handler) wl_iw_get_encode,		/* SIOCGIWENCODE */
	(iw_handler) wl_iw_set_power,		/* SIOCSIWPOWER */
	(iw_handler) wl_iw_get_power,		/* SIOCGIWPOWER */
#if WIRELESS_EXT > 17
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) wl_iw_set_wpaie,		/* SIOCSIWGENIE */
	(iw_handler) wl_iw_get_wpaie,		/* SIOCGIWGENIE */
	(iw_handler) wl_iw_set_wpaauth,		/* SIOCSIWAUTH */
	(iw_handler) wl_iw_get_wpaauth,		/* SIOCGIWAUTH */
	(iw_handler) wl_iw_set_encodeext,	/* SIOCSIWENCODEEXT */
	(iw_handler) wl_iw_get_encodeext,	/* SIOCGIWENCODEEXT */
	(iw_handler) wl_iw_set_pmksa,		/* SIOCSIWPMKSA */
#endif /* WIRELESS_EXT > 17 */
};

#if WIRELESS_EXT > 12
enum {
	WL_IW_SET_LEDDC = SIOCIWFIRSTPRIV,
	WL_IW_SET_VLANMODE,
	WL_IW_SET_PM,
#if WIRELESS_EXT > 17
#endif /* WIRELESS_EXT > 17 */
	WL_IW_SET_LAST
};

static iw_handler wl_iw_priv_handler[] = {
	wl_iw_set_leddc,
	wl_iw_set_vlanmode,
	wl_iw_set_pm,
#if WIRELESS_EXT > 17
#endif /* WIRELESS_EXT > 17 */
	NULL
};

static struct iw_priv_args wl_iw_priv_args[] = {
	{
		WL_IW_SET_LEDDC,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
		0,
		"set_leddc"
	},
	{
		WL_IW_SET_VLANMODE,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
		0,
		"set_vlanmode"
	},
	{
		WL_IW_SET_PM,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
		0,
		"set_pm"
	},
#if WIRELESS_EXT > 17
#endif /* WIRELESS_EXT > 17 */
	{ 0, 0, 0, { 0 } }
};

const struct iw_handler_def wl_iw_handler_def =
{
	.num_standard = ARRAYSIZE(wl_iw_handler),
	.num_private = ARRAY_SIZE(wl_iw_priv_handler),
	.num_private_args = ARRAY_SIZE(wl_iw_priv_args),
	.standard = (const iw_handler *) wl_iw_handler,
	.private = wl_iw_priv_handler,
	.private_args = wl_iw_priv_args,
#if WIRELESS_EXT >= 19
	get_wireless_stats: dhd_get_wireless_stats,
#endif /* WIRELESS_EXT >= 19 */
	};
#endif /* WIRELESS_EXT > 12 */

int
wl_iw_ioctl(
	struct net_device *dev,
	struct ifreq *rq,
	int cmd
)
{
	struct iwreq *wrq = (struct iwreq *) rq;
	struct iw_request_info info;
	iw_handler handler;
	char *extra = NULL;
	size_t token_size = 1;
	int max_tokens = 0, ret = 0;

	if (cmd < SIOCIWFIRST ||
		IW_IOCTL_IDX(cmd) >= ARRAYSIZE(wl_iw_handler) ||
		!(handler = wl_iw_handler[IW_IOCTL_IDX(cmd)]))
		return -EOPNOTSUPP;

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
		max_tokens = IW_ENCODING_TOKEN_MAX;
		break;

	case SIOCGIWRANGE:
		max_tokens = sizeof(struct iw_range);
		break;

	case SIOCGIWAPLIST:
		token_size = sizeof(struct sockaddr) + sizeof(struct iw_quality);
		max_tokens = IW_MAX_AP;
		break;

#if WIRELESS_EXT > 13
	case SIOCGIWSCAN:
		if (g_iscan)
			max_tokens = wrq->u.data.length;
		else
		max_tokens = IW_SCAN_MAX_DATA;
		break;
#endif /* WIRELESS_EXT > 13 */

	case SIOCSIWSPY:
		token_size = sizeof(struct sockaddr);
		max_tokens = IW_MAX_SPY;
		break;

	case SIOCGIWSPY:
		token_size = sizeof(struct sockaddr) + sizeof(struct iw_quality);
		max_tokens = IW_MAX_SPY;
		break;
	default:
		break;
	}

	if (max_tokens && wrq->u.data.pointer) {
		if (wrq->u.data.length > max_tokens)
			return -E2BIG;

		if (!(extra = kmalloc(max_tokens * token_size, GFP_KERNEL)))
			return -ENOMEM;

		if (copy_from_user(extra, wrq->u.data.pointer, wrq->u.data.length * token_size)) {
			kfree(extra);
			return -EFAULT;
		}
	}

	info.cmd = cmd;
	info.flags = 0;

	ret = handler(dev, &info, &wrq->u, extra);

	if (extra) {
		if (copy_to_user(wrq->u.data.pointer, extra, wrq->u.data.length * token_size)) {
			kfree(extra);
			return -EFAULT;
		}

		kfree(extra);
	}

	return ret;
}

/* Convert a connection status event into a connection status string.
 * Returns TRUE if a matching connection status string was found.
 */
bool
wl_iw_conn_status_str(uint32 event_type, uint32 status, uint32 reason,
	char* stringBuf, uint buflen)
{
	typedef struct conn_fail_event_map_t {
		uint32 inEvent;			/* input: event type to match */
		uint32 inStatus;		/* input: event status code to match */
		uint32 inReason;		/* input: event reason code to match */
		const char* outName;	/* output: failure type */
		const char* outCause;	/* output: failure cause */
	} conn_fail_event_map_t;

	/* Map of WLC_E events to connection failure strings */
#	define WL_IW_DONT_CARE	9999
	const conn_fail_event_map_t event_map [] = {
		/* inEvent           inStatus                inReason         */
		/* outName outCause                                           */
		{WLC_E_SET_SSID,     WLC_E_STATUS_SUCCESS,   WL_IW_DONT_CARE,
		"Conn", "Success"},
		{WLC_E_SET_SSID,     WLC_E_STATUS_NO_NETWORKS, WL_IW_DONT_CARE,
		"Conn", "NoNetworks"},
		{WLC_E_SET_SSID,     WLC_E_STATUS_FAIL,      WL_IW_DONT_CARE,
		"Conn", "ConfigMismatch"},
		{WLC_E_PRUNE,        WL_IW_DONT_CARE,        WLC_E_PRUNE_ENCR_MISMATCH,
		"Conn", "EncrypMismatch"},
		{WLC_E_PRUNE,        WL_IW_DONT_CARE,        WLC_E_RSN_MISMATCH,
		"Conn", "RsnMismatch"},
		{WLC_E_AUTH,         WLC_E_STATUS_TIMEOUT,   WL_IW_DONT_CARE,
		"Conn", "AuthTimeout"},
		{WLC_E_AUTH,         WLC_E_STATUS_FAIL,      WL_IW_DONT_CARE,
		"Conn", "AuthFail"},
		{WLC_E_AUTH,         WLC_E_STATUS_NO_ACK,    WL_IW_DONT_CARE,
		"Conn", "AuthNoAck"},
		{WLC_E_REASSOC,      WLC_E_STATUS_FAIL,      WL_IW_DONT_CARE,
		"Conn", "ReassocFail"},
		{WLC_E_REASSOC,      WLC_E_STATUS_TIMEOUT,   WL_IW_DONT_CARE,
		"Conn", "ReassocTimeout"},
		{WLC_E_REASSOC,      WLC_E_STATUS_ABORT,     WL_IW_DONT_CARE,
		"Conn", "ReassocAbort"},
		{WLC_E_PSK_SUP,      WLC_SUP_KEYED,          WL_IW_DONT_CARE,
		"Sup", "ConnSuccess"},
		{WLC_E_PSK_SUP,      WL_IW_DONT_CARE,        WL_IW_DONT_CARE,
		"Sup", "WpaHandshakeFail"},
		{WLC_E_DEAUTH_IND,   WL_IW_DONT_CARE,        WL_IW_DONT_CARE,
		"Conn", "Deauth"},
		{WLC_E_DISASSOC_IND, WL_IW_DONT_CARE,        WL_IW_DONT_CARE,
		"Conn", "DisassocInd"},
		{WLC_E_DISASSOC,     WL_IW_DONT_CARE,        WL_IW_DONT_CARE,
		"Conn", "Disassoc"}
	};

	const char* name = "";
	const char* cause = NULL;
	int i;

	/* Search the event map table for a matching event */
	for (i = 0;  i < sizeof(event_map)/sizeof(event_map[0]);  i++) {
		const conn_fail_event_map_t* row = &event_map[i];
		if (row->inEvent == event_type &&
		    (row->inStatus == status || row->inStatus == WL_IW_DONT_CARE) &&
		    (row->inReason == reason || row->inReason == WL_IW_DONT_CARE)) {
			name = row->outName;
			cause = row->outCause;
			break;
		}
	}

	/* If found, generate a connection failure string and return TRUE */
	if (cause) {
		memset(stringBuf, 0, buflen);
		(void)snprintf(stringBuf, buflen, "%s %s %02d %02d", name, cause, status, reason);
		WL_TRACE(("Connection status: %s\n", stringBuf));
		return TRUE;
	} else {
		return FALSE;
	}
}

#if (WIRELESS_EXT > 14)
/* Check if we have received an event that indicates connection failure
 * If so, generate a connection failure report string.
 * The caller supplies a buffer to hold the generated string.
 */
static bool
wl_iw_check_conn_fail(wl_event_msg_t *e, char* stringBuf, uint buflen)
{
	uint32 event = ntoh32(e->event_type);
	uint32 status =  ntoh32(e->status);
	uint32 reason =  ntoh32(e->reason);

	if (wl_iw_conn_status_str(event, status, reason, stringBuf, buflen)) {
		return TRUE;
	} else
	{
		return FALSE;
	}
}
#endif /* WIRELESS_EXT > 14 */

#ifndef IW_CUSTOM_MAX
#define IW_CUSTOM_MAX 256 /* size of extra buffer used for translation of events */
#endif /* IW_CUSTOM_MAX */

void
wl_iw_event(struct net_device *dev, wl_event_msg_t *e, void* data)
{
#if WIRELESS_EXT > 13
	union iwreq_data wrqu;
	char extra[IW_CUSTOM_MAX + 1];
	int cmd = 0;
	uint32 event_type = ntoh32(e->event_type);
	uint16 flags =  ntoh16(e->flags);
	uint32 datalen = ntoh32(e->datalen);
	uint32 status =  ntoh32(e->status);

	memset(&wrqu, 0, sizeof(wrqu));
	memset(extra, 0, sizeof(extra));

	memcpy(wrqu.addr.sa_data, &e->addr, ETHER_ADDR_LEN);
	wrqu.addr.sa_family = ARPHRD_ETHER;

	switch (event_type) {
	case WLC_E_TXFAIL:
		cmd = IWEVTXDROP;
		break;
#if WIRELESS_EXT > 14
	case WLC_E_JOIN:
	case WLC_E_ASSOC_IND:
	case WLC_E_REASSOC_IND:
		cmd = IWEVREGISTERED;
		break;
	case WLC_E_DEAUTH_IND:
	case WLC_E_DISASSOC_IND:
		cmd = SIOCGIWAP;
		wrqu.data.length = strlen(extra);
		bzero(wrqu.addr.sa_data, ETHER_ADDR_LEN);
		bzero(&extra, ETHER_ADDR_LEN);
		break;

	case WLC_E_LINK:
		cmd = SIOCGIWAP;
		wrqu.data.length = strlen(extra);
		if (!(flags & WLC_EVENT_MSG_LINK)) {
			bzero(wrqu.addr.sa_data, ETHER_ADDR_LEN);
			bzero(&extra, ETHER_ADDR_LEN);
		}
		break;
	case WLC_E_ACTION_FRAME:
		cmd = IWEVCUSTOM;
		if (datalen + 1 <= sizeof(extra)) {
			wrqu.data.length = datalen + 1;
			extra[0] = WLC_E_ACTION_FRAME;
			memcpy(&extra[1], data, datalen);
			WL_TRACE(("WLC_E_ACTION_FRAME len %d \n", wrqu.data.length));
		}
		break;

	case WLC_E_ACTION_FRAME_COMPLETE:
		cmd = IWEVCUSTOM;
		if (sizeof(status) + 1 <= sizeof(extra)) {
			wrqu.data.length = sizeof(status) + 1;
			extra[0] = WLC_E_ACTION_FRAME_COMPLETE;
			memcpy(&extra[1], &status, sizeof(status));
			WL_TRACE(("wl_iw_event status %d  \n", status));
		}
		break;
#endif /* WIRELESS_EXT > 14 */
#if WIRELESS_EXT > 17
	case WLC_E_MIC_ERROR: {
		struct	iw_michaelmicfailure  *micerrevt = (struct  iw_michaelmicfailure  *)&extra;
		cmd = IWEVMICHAELMICFAILURE;
		wrqu.data.length = sizeof(struct iw_michaelmicfailure);
		if (flags & WLC_EVENT_MSG_GROUP)
			micerrevt->flags |= IW_MICFAILURE_GROUP;
		else
			micerrevt->flags |= IW_MICFAILURE_PAIRWISE;
		memcpy(micerrevt->src_addr.sa_data, &e->addr, ETHER_ADDR_LEN);
		micerrevt->src_addr.sa_family = ARPHRD_ETHER;

		break;
	}

	case WLC_E_ASSOC_REQ_IE:
		cmd = IWEVASSOCREQIE;
		wrqu.data.length = datalen;
		if (datalen < sizeof(extra))
			memcpy(extra, data, datalen);
		break;

	case WLC_E_ASSOC_RESP_IE:
		cmd = IWEVASSOCRESPIE;
		wrqu.data.length = datalen;
		if (datalen < sizeof(extra))
			memcpy(extra, data, datalen);
		break;

	case WLC_E_PMKID_CACHE: {
		struct iw_pmkid_cand *iwpmkidcand = (struct iw_pmkid_cand *)&extra;
		pmkid_cand_list_t *pmkcandlist;
		pmkid_cand_t	*pmkidcand;
		int count;

		if (data == NULL)
			break;

		cmd = IWEVPMKIDCAND;
		pmkcandlist = data;
		count = ntoh32_ua((uint8 *)&pmkcandlist->npmkid_cand);
		wrqu.data.length = sizeof(struct iw_pmkid_cand);
		pmkidcand = pmkcandlist->pmkid_cand;
		while (count) {
			bzero(iwpmkidcand, sizeof(struct iw_pmkid_cand));
			if (pmkidcand->preauth)
				iwpmkidcand->flags |= IW_PMKID_CAND_PREAUTH;
			bcopy(&pmkidcand->BSSID, &iwpmkidcand->bssid.sa_data,
			      ETHER_ADDR_LEN);
			wireless_send_event(dev, cmd, &wrqu, extra);
			pmkidcand++;
			count--;
		}
		break;
	}
#endif /* WIRELESS_EXT > 17 */

	case WLC_E_SCAN_COMPLETE:
#if WIRELESS_EXT > 14
		cmd = SIOCGIWSCAN;
#endif
		WL_TRACE(("event WLC_E_SCAN_COMPLETE\n"));
		if ((g_iscan) && (g_iscan->sysioc_pid >= 0) &&
			(g_iscan->iscan_state != ISCAN_STATE_IDLE))
			up(&g_iscan->sysioc_sem);
		break;

	default:
		/* Cannot translate event */
		break;
	}

	if (cmd) {
		if (cmd == SIOCGIWSCAN)
			wireless_send_event(dev, cmd, &wrqu, NULL);
		else
			wireless_send_event(dev, cmd, &wrqu, extra);
	}

#if WIRELESS_EXT > 14
	/* Look for WLC events that indicate a connection failure.
	 * If found, generate an IWEVCUSTOM event.
	 */
	memset(extra, 0, sizeof(extra));
	if (wl_iw_check_conn_fail(e, extra, sizeof(extra))) {
		cmd = IWEVCUSTOM;
		wrqu.data.length = strlen(extra);
		wireless_send_event(dev, cmd, &wrqu, extra);
	}
#endif /* WIRELESS_EXT > 14 */

#endif /* WIRELESS_EXT > 13 */
}

static int wl_iw_get_wireless_stats_cbfn(void *ctx, uint8 *data, uint16 type, uint16 len)
{
	struct iw_statistics *wstats = ctx;
	int res = BCME_OK;

	switch (type) {
		case WL_CNT_XTLV_WLC: {
			wl_cnt_wlc_t *cnt = (wl_cnt_wlc_t *)data;
			if (len > sizeof(wl_cnt_wlc_t)) {
				printf("counter structure length invalid! %d > %d\n",
					len, (int)sizeof(wl_cnt_wlc_t));
			}
			wstats->discard.nwid = 0;
			wstats->discard.code = dtoh32(cnt->rxundec);
			wstats->discard.fragment = dtoh32(cnt->rxfragerr);
			wstats->discard.retries = dtoh32(cnt->txfail);
			wstats->discard.misc = dtoh32(cnt->rxrunt) + dtoh32(cnt->rxgiant);
			wstats->miss.beacon = 0;
			WL_TRACE(("wl_iw_get_wireless_stats counters txframe=%d txbyte=%d\n",
				dtoh32(cnt->txframe), dtoh32(cnt->txbyte)));
			WL_TRACE(("wl_iw_get_wireless_stats counters rxundec=%d\n",
				dtoh32(cnt->rxundec)));
			WL_TRACE(("wl_iw_get_wireless_stats counters txfail=%d\n",
				dtoh32(cnt->txfail)));
			WL_TRACE(("wl_iw_get_wireless_stats counters rxfragerr=%d\n",
				dtoh32(cnt->rxfragerr)));
			WL_TRACE(("wl_iw_get_wireless_stats counters rxrunt=%d\n",
				dtoh32(cnt->rxrunt)));
			WL_TRACE(("wl_iw_get_wireless_stats counters rxgiant=%d\n",
				dtoh32(cnt->rxgiant)));
			break;
		}
		case WL_CNT_XTLV_CNTV_LE10_UCODE:
		case WL_CNT_XTLV_LT40_UCODE_V1:
		case WL_CNT_XTLV_GE40_UCODE_V1:
		{
			/* Offsets of rxfrmtoolong and rxbadplcp are the same in
			 * wl_cnt_v_le10_mcst_t, wl_cnt_lt40mcst_v1_t, and wl_cnt_ge40mcst_v1_t.
			 * So we can just cast to wl_cnt_v_le10_mcst_t here.
			 */
			wl_cnt_v_le10_mcst_t *cnt = (wl_cnt_v_le10_mcst_t *)data;
			if (len != WL_CNT_MCST_STRUCT_SZ) {
				printf("counter structure length mismatch! %d != %d\n",
					len, WL_CNT_MCST_STRUCT_SZ);
			}
			WL_TRACE(("wl_iw_get_wireless_stats counters rxfrmtoolong=%d\n",
				dtoh32(cnt->rxfrmtoolong)));
			WL_TRACE(("wl_iw_get_wireless_stats counters rxbadplcp=%d\n",
				dtoh32(cnt->rxbadplcp)));
			BCM_REFERENCE(cnt);
			break;
		}
		default:
			WL_ERROR(("%s %d: Unsupported type %d\n", __FUNCTION__, __LINE__, type));
			break;
	}
	return res;
}

int wl_iw_get_wireless_stats(struct net_device *dev, struct iw_statistics *wstats)
{
	int res = 0;
	int phy_noise;
	int rssi;
	scb_val_t scb_val;
#if WIRELESS_EXT > 11
	char *cntbuf = NULL;
	wl_cnt_info_t *cntinfo;
	uint16 ver;
	uint32 corerev = 0;
#endif /* WIRELESS_EXT > 11 */

	phy_noise = 0;
	if ((res = dev_wlc_ioctl(dev, WLC_GET_PHY_NOISE, &phy_noise, sizeof(phy_noise))))
		goto done;

	phy_noise = dtoh32(phy_noise);
	WL_TRACE(("wl_iw_get_wireless_stats phy noise=%d\n *****", phy_noise));

	scb_val.val = 0;
	if ((res = dev_wlc_ioctl(dev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t))))
		goto done;

	rssi = dtoh32(scb_val.val);
	WL_TRACE(("wl_iw_get_wireless_stats rssi=%d ****** \n", rssi));
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

	/* Wraps to 0 if RSSI is 0 */
	wstats->qual.level = 0x100 + rssi;
	wstats->qual.noise = 0x100 + phy_noise;
#if WIRELESS_EXT > 18
	wstats->qual.updated |= (IW_QUAL_ALL_UPDATED | IW_QUAL_DBM);
#else
	wstats->qual.updated |= 7;
#endif /* WIRELESS_EXT > 18 */

#if WIRELESS_EXT > 11
	WL_TRACE(("wl_iw_get_wireless_stats counters=%d\n *****", WL_CNTBUF_MAX_SIZE));

	if (WL_CNTBUF_MAX_SIZE > MAX_WLIW_IOCTL_LEN)
	{
		WL_ERROR(("wl_iw_get_wireless_stats buffer too short %d < %d\n",
			WL_CNTBUF_MAX_SIZE, MAX_WLIW_IOCTL_LEN));
		res = BCME_BUFTOOSHORT;
		goto done;
	}

	cntbuf = kmalloc(WL_CNTBUF_MAX_SIZE, GFP_KERNEL);
	if (!cntbuf) {
		res = BCME_NOMEM;
		goto done;
	}

	memset(cntbuf, 0, WL_CNTBUF_MAX_SIZE);
	res = dev_wlc_bufvar_get(dev, "counters", cntbuf, WL_CNTBUF_MAX_SIZE);
	if (res)
	{
		WL_ERROR(("wl_iw_get_wireless_stats counters failed error=%d ****** \n", res));
		goto done;
	}

	cntinfo = (wl_cnt_info_t *)cntbuf;
	cntinfo->version = dtoh16(cntinfo->version);
	cntinfo->datalen = dtoh16(cntinfo->datalen);
	ver = cntinfo->version;
	if (ver > WL_CNT_T_VERSION) {
		WL_TRACE(("\tIncorrect version of counters struct: expected %d; got %d\n",
			WL_CNT_T_VERSION, ver));
		res = BCME_VERSION;
		goto done;
	}

	if (ver == WL_CNT_VERSION_11) {
		wlc_rev_info_t revinfo;
		memset(&revinfo, 0, sizeof(revinfo));
		res = dev_wlc_ioctl(dev, WLC_GET_REVINFO, &revinfo, sizeof(revinfo));
		if (res) {
			WL_ERROR(("%s: WLC_GET_REVINFO failed %d\n", __FUNCTION__, res));
			goto done;
		}
		corerev = dtoh32(revinfo.corerev);
	}

	res = wl_cntbuf_to_xtlv_format(NULL, cntinfo, WL_CNTBUF_MAX_SIZE, corerev);
	if (res) {
		WL_ERROR(("%s: wl_cntbuf_to_xtlv_format failed %d\n", __FUNCTION__, res));
		goto done;
	}

	if ((res = bcm_unpack_xtlv_buf(wstats, cntinfo->data, cntinfo->datalen,
		BCM_XTLV_OPTION_ALIGN32, wl_iw_get_wireless_stats_cbfn))) {
		goto done;
	}
#endif /* WIRELESS_EXT > 11 */

done:
#if WIRELESS_EXT > 11
	if (cntbuf) {
		kfree(cntbuf);
	}
#endif /* WIRELESS_EXT > 11 */
	return res;
}

static void
wl_iw_timerfunc(ulong data)
{
	iscan_info_t *iscan = (iscan_info_t *)data;
	iscan->timer_on = 0;
	if (iscan->iscan_state != ISCAN_STATE_IDLE) {
		WL_TRACE(("timer trigger\n"));
		up(&iscan->sysioc_sem);
	}
}

static void
wl_iw_set_event_mask(struct net_device *dev)
{
	char eventmask[WL_EVENTING_MASK_LEN];
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/* Room for "event_msgs" + '\0' + bitvec */

	dev_iw_iovar_getbuf(dev, "event_msgs", "", 0, iovbuf, sizeof(iovbuf));
	bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);
	setbit(eventmask, WLC_E_SCAN_COMPLETE);
	dev_iw_iovar_setbuf(dev, "event_msgs", eventmask, WL_EVENTING_MASK_LEN,
		iovbuf, sizeof(iovbuf));

}

static int
wl_iw_iscan_prep(wl_scan_params_t *params, wlc_ssid_t *ssid)
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

static int
wl_iw_iscan(iscan_info_t *iscan, wlc_ssid_t *ssid, uint16 action)
{
	int params_size = (WL_SCAN_PARAMS_FIXED_SIZE + OFFSETOF(wl_iscan_params_t, params));
	wl_iscan_params_t *params;
	int err = 0;

	if (ssid && ssid->SSID_len) {
		params_size += sizeof(wlc_ssid_t);
	}
	params = (wl_iscan_params_t*)kmalloc(params_size, GFP_KERNEL);
	if (params == NULL) {
		return -ENOMEM;
	}
	memset(params, 0, params_size);
	ASSERT(params_size < WLC_IOCTL_SMLEN);

	err = wl_iw_iscan_prep(&params->params, ssid);

	if (!err) {
		params->version = htod32(ISCAN_REQ_VERSION);
		params->action = htod16(action);
		params->scan_duration = htod16(0);

		/* params_size += OFFSETOF(wl_iscan_params_t, params); */
		(void) dev_iw_iovar_setbuf(iscan->dev, "iscan", params, params_size,
			iscan->ioctlbuf, WLC_IOCTL_SMLEN);
	}

	kfree(params);
	return err;
}

static uint32
wl_iw_iscan_get(iscan_info_t *iscan)
{
	iscan_buf_t * buf;
	iscan_buf_t * ptr;
	wl_iscan_results_t * list_buf;
	wl_iscan_results_t list;
	wl_scan_results_t *results;
	uint32 status;

	/* buffers are allocated on demand */
	if (iscan->list_cur) {
		buf = iscan->list_cur;
		iscan->list_cur = buf->next;
	}
	else {
		buf = kmalloc(sizeof(iscan_buf_t), GFP_KERNEL);
		if (!buf)
			return WL_SCAN_RESULTS_ABORTED;
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
	list_buf = (wl_iscan_results_t*)buf->iscan_buf;
	results = &list_buf->results;
	results->buflen = WL_ISCAN_RESULTS_FIXED_SIZE;
	results->version = 0;
	results->count = 0;

	memset(&list, 0, sizeof(list));
	list.results.buflen = htod32(WLC_IW_ISCAN_MAXLEN);
	(void) dev_iw_iovar_getbuf(
		iscan->dev,
		"iscanresults",
		&list,
		WL_ISCAN_RESULTS_FIXED_SIZE,
		buf->iscan_buf,
		WLC_IW_ISCAN_MAXLEN);
	results->buflen = dtoh32(results->buflen);
	results->version = dtoh32(results->version);
	results->count = dtoh32(results->count);
	WL_TRACE(("results->count = %d\n", results->count));

	WL_TRACE(("results->buflen = %d\n", results->buflen));
	status = dtoh32(list_buf->status);
	return status;
}

static void wl_iw_send_scan_complete(iscan_info_t *iscan)
{
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(wrqu));

	/* wext expects to get no data for SIOCGIWSCAN Event  */
	wireless_send_event(iscan->dev, SIOCGIWSCAN, &wrqu, NULL);
}

static int
_iscan_sysioc_thread(void *data)
{
	uint32 status;
	iscan_info_t *iscan = (iscan_info_t *)data;

	DAEMONIZE("iscan_sysioc");

	status = WL_SCAN_RESULTS_PARTIAL;
	while (down_interruptible(&iscan->sysioc_sem) == 0) {
		if (iscan->timer_on) {
			del_timer(&iscan->timer);
			iscan->timer_on = 0;
		}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
		rtnl_lock();
#endif
		status = wl_iw_iscan_get(iscan);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
		rtnl_unlock();
#endif

		switch (status) {
			case WL_SCAN_RESULTS_PARTIAL:
				WL_TRACE(("iscanresults incomplete\n"));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
				rtnl_lock();
#endif
				/* make sure our buffer size is enough before going next round */
				wl_iw_iscan(iscan, NULL, WL_SCAN_ACTION_CONTINUE);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
				rtnl_unlock();
#endif
				/* Reschedule the timer */
				iscan->timer.expires = jiffies + msecs_to_jiffies(iscan->timer_ms);
				add_timer(&iscan->timer);
				iscan->timer_on = 1;
				break;
			case WL_SCAN_RESULTS_SUCCESS:
				WL_TRACE(("iscanresults complete\n"));
				iscan->iscan_state = ISCAN_STATE_IDLE;
				wl_iw_send_scan_complete(iscan);
				break;
			case WL_SCAN_RESULTS_PENDING:
				WL_TRACE(("iscanresults pending\n"));
				/* Reschedule the timer */
				iscan->timer.expires = jiffies + msecs_to_jiffies(iscan->timer_ms);
				add_timer(&iscan->timer);
				iscan->timer_on = 1;
				break;
			case WL_SCAN_RESULTS_ABORTED:
				WL_TRACE(("iscanresults aborted\n"));
				iscan->iscan_state = ISCAN_STATE_IDLE;
				wl_iw_send_scan_complete(iscan);
				break;
			default:
				WL_TRACE(("iscanresults returned unknown status %d\n", status));
				break;
		 }
	}
	complete_and_exit(&iscan->sysioc_exited, 0);
}

int
wl_iw_attach(struct net_device *dev, void * dhdp)
{
	iscan_info_t *iscan = NULL;

	if (!dev)
		return 0;

	iscan = kmalloc(sizeof(iscan_info_t), GFP_KERNEL);
	if (!iscan)
		return -ENOMEM;
	memset(iscan, 0, sizeof(iscan_info_t));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	iscan->kthread = NULL;
#endif
	iscan->sysioc_pid = -1;
	/* we only care about main interface so save a global here */
	g_iscan = iscan;
	iscan->dev = dev;
	iscan->iscan_state = ISCAN_STATE_IDLE;


	/* Set up the timer */
	iscan->timer_ms    = 2000;
	init_timer(&iscan->timer);
	iscan->timer.data = (ulong)iscan;
	iscan->timer.function = wl_iw_timerfunc;

	sema_init(&iscan->sysioc_sem, 0);
	init_completion(&iscan->sysioc_exited);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	iscan->kthread = kthread_run(_iscan_sysioc_thread, iscan, "iscan_sysioc");
	iscan->sysioc_pid = iscan->kthread->pid;
#else
	iscan->sysioc_pid = kernel_thread(_iscan_sysioc_thread, iscan, 0);
#endif
	if (iscan->sysioc_pid < 0)
		return -ENOMEM;
	return 0;
}

void wl_iw_detach(void)
{
	iscan_buf_t  *buf;
	iscan_info_t *iscan = g_iscan;
	if (!iscan)
		return;
	if (iscan->sysioc_pid >= 0) {
		KILL_PROC(iscan->sysioc_pid, SIGTERM);
		wait_for_completion(&iscan->sysioc_exited);
	}

	while (iscan->list_hdr) {
		buf = iscan->list_hdr->next;
		kfree(iscan->list_hdr);
		iscan->list_hdr = buf;
	}
	kfree(iscan);
	g_iscan = NULL;
}

#endif /* USE_IW */
