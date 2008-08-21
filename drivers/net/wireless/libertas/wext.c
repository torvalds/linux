/**
  * This file contains ioctl functions
  */
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/bitops.h>

#include <net/ieee80211.h>
#include <net/iw_handler.h>

#include "host.h"
#include "radiotap.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "wext.h"
#include "scan.h"
#include "assoc.h"
#include "cmd.h"


static inline void lbs_postpone_association_work(struct lbs_private *priv)
{
	if (priv->surpriseremoved)
		return;
	cancel_delayed_work(&priv->assoc_work);
	queue_delayed_work(priv->work_thread, &priv->assoc_work, HZ / 2);
}

static inline void lbs_cancel_association_work(struct lbs_private *priv)
{
	cancel_delayed_work(&priv->assoc_work);
	kfree(priv->pending_assoc_req);
	priv->pending_assoc_req = NULL;
}


/**
 *  @brief Find the channel frequency power info with specific channel
 *
 *  @param priv 	A pointer to struct lbs_private structure
 *  @param band		it can be BAND_A, BAND_G or BAND_B
 *  @param channel      the channel for looking
 *  @return 	   	A pointer to struct chan_freq_power structure or NULL if not find.
 */
struct chan_freq_power *lbs_find_cfp_by_band_and_channel(
	struct lbs_private *priv,
	u8 band,
	u16 channel)
{
	struct chan_freq_power *cfp = NULL;
	struct region_channel *rc;
	int i, j;

	for (j = 0; !cfp && (j < ARRAY_SIZE(priv->region_channel)); j++) {
		rc = &priv->region_channel[j];

		if (priv->enable11d)
			rc = &priv->universal_channel[j];
		if (!rc->valid || !rc->CFP)
			continue;
		if (rc->band != band)
			continue;
		for (i = 0; i < rc->nrcfp; i++) {
			if (rc->CFP[i].channel == channel) {
				cfp = &rc->CFP[i];
				break;
			}
		}
	}

	if (!cfp && channel)
		lbs_deb_wext("lbs_find_cfp_by_band_and_channel: can't find "
		       "cfp by band %d / channel %d\n", band, channel);

	return cfp;
}

/**
 *  @brief Find the channel frequency power info with specific frequency
 *
 *  @param priv 	A pointer to struct lbs_private structure
 *  @param band		it can be BAND_A, BAND_G or BAND_B
 *  @param freq	        the frequency for looking
 *  @return 	   	A pointer to struct chan_freq_power structure or NULL if not find.
 */
static struct chan_freq_power *find_cfp_by_band_and_freq(
	struct lbs_private *priv,
	u8 band,
	u32 freq)
{
	struct chan_freq_power *cfp = NULL;
	struct region_channel *rc;
	int i, j;

	for (j = 0; !cfp && (j < ARRAY_SIZE(priv->region_channel)); j++) {
		rc = &priv->region_channel[j];

		if (priv->enable11d)
			rc = &priv->universal_channel[j];
		if (!rc->valid || !rc->CFP)
			continue;
		if (rc->band != band)
			continue;
		for (i = 0; i < rc->nrcfp; i++) {
			if (rc->CFP[i].freq == freq) {
				cfp = &rc->CFP[i];
				break;
			}
		}
	}

	if (!cfp && freq)
		lbs_deb_wext("find_cfp_by_band_and_freql: can't find cfp by "
		       "band %d / freq %d\n", band, freq);

	return cfp;
}

/**
 *  @brief Copy active data rates based on adapter mode and status
 *
 *  @param priv              A pointer to struct lbs_private structure
 *  @param rate		        The buf to return the active rates
 */
static void copy_active_data_rates(struct lbs_private *priv, u8 *rates)
{
	lbs_deb_enter(LBS_DEB_WEXT);

	if ((priv->connect_status != LBS_CONNECTED) &&
		(priv->mesh_connect_status != LBS_CONNECTED))
		memcpy(rates, lbs_bg_rates, MAX_RATES);
	else
		memcpy(rates, priv->curbssparams.rates, MAX_RATES);

	lbs_deb_leave(LBS_DEB_WEXT);
}

static int lbs_get_name(struct net_device *dev, struct iw_request_info *info,
			 char *cwrq, char *extra)
{

	lbs_deb_enter(LBS_DEB_WEXT);

	/* We could add support for 802.11n here as needed. Jean II */
	snprintf(cwrq, IFNAMSIZ, "IEEE 802.11b/g");

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_get_freq(struct net_device *dev, struct iw_request_info *info,
			 struct iw_freq *fwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;
	struct chan_freq_power *cfp;

	lbs_deb_enter(LBS_DEB_WEXT);

	cfp = lbs_find_cfp_by_band_and_channel(priv, 0,
					   priv->curbssparams.channel);

	if (!cfp) {
		if (priv->curbssparams.channel)
			lbs_deb_wext("invalid channel %d\n",
			       priv->curbssparams.channel);
		return -EINVAL;
	}

	fwrq->m = (long)cfp->freq * 100000;
	fwrq->e = 1;

	lbs_deb_wext("freq %u\n", fwrq->m);
	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_get_wap(struct net_device *dev, struct iw_request_info *info,
			struct sockaddr *awrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (priv->connect_status == LBS_CONNECTED) {
		memcpy(awrq->sa_data, priv->curbssparams.bssid, ETH_ALEN);
	} else {
		memset(awrq->sa_data, 0, ETH_ALEN);
	}
	awrq->sa_family = ARPHRD_ETHER;

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_set_nick(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	/*
	 * Check the size of the string
	 */

	if (dwrq->length > 16) {
		return -E2BIG;
	}

	mutex_lock(&priv->lock);
	memset(priv->nodename, 0, sizeof(priv->nodename));
	memcpy(priv->nodename, extra, dwrq->length);
	mutex_unlock(&priv->lock);

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_get_nick(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	dwrq->length = strlen(priv->nodename);
	memcpy(extra, priv->nodename, dwrq->length);
	extra[dwrq->length] = '\0';

	dwrq->flags = 1;	/* active */

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int mesh_get_nick(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	/* Use nickname to indicate that mesh is on */

	if (priv->mesh_connect_status == LBS_CONNECTED) {
		strncpy(extra, "Mesh", 12);
		extra[12] = '\0';
		dwrq->length = strlen(extra);
	}

	else {
		extra[0] = '\0';
		dwrq->length = 0;
	}

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_set_rts(struct net_device *dev, struct iw_request_info *info,
			struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;
	u32 rthr = vwrq->value;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (vwrq->disabled) {
		priv->rtsthsd = rthr = MRVDRV_RTS_MAX_VALUE;
	} else {
		if (rthr < MRVDRV_RTS_MIN_VALUE || rthr > MRVDRV_RTS_MAX_VALUE)
			return -EINVAL;
		priv->rtsthsd = rthr;
	}

	ret = lbs_prepare_and_send_command(priv, CMD_802_11_SNMP_MIB,
				    CMD_ACT_SET, CMD_OPTION_WAITFORRSP,
				    OID_802_11_RTS_THRESHOLD, &rthr);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_get_rts(struct net_device *dev, struct iw_request_info *info,
			struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	priv->rtsthsd = 0;
	ret = lbs_prepare_and_send_command(priv, CMD_802_11_SNMP_MIB,
				    CMD_ACT_GET, CMD_OPTION_WAITFORRSP,
				    OID_802_11_RTS_THRESHOLD, NULL);
	if (ret)
		goto out;

	vwrq->value = priv->rtsthsd;
	vwrq->disabled = ((vwrq->value < MRVDRV_RTS_MIN_VALUE)
			  || (vwrq->value > MRVDRV_RTS_MAX_VALUE));
	vwrq->fixed = 1;

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_set_frag(struct net_device *dev, struct iw_request_info *info,
			 struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	u32 fthr = vwrq->value;
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (vwrq->disabled) {
		priv->fragthsd = fthr = MRVDRV_FRAG_MAX_VALUE;
	} else {
		if (fthr < MRVDRV_FRAG_MIN_VALUE
		    || fthr > MRVDRV_FRAG_MAX_VALUE)
			return -EINVAL;
		priv->fragthsd = fthr;
	}

	ret = lbs_prepare_and_send_command(priv, CMD_802_11_SNMP_MIB,
				    CMD_ACT_SET, CMD_OPTION_WAITFORRSP,
				    OID_802_11_FRAGMENTATION_THRESHOLD, &fthr);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_get_frag(struct net_device *dev, struct iw_request_info *info,
			 struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	priv->fragthsd = 0;
	ret = lbs_prepare_and_send_command(priv,
				    CMD_802_11_SNMP_MIB,
				    CMD_ACT_GET, CMD_OPTION_WAITFORRSP,
				    OID_802_11_FRAGMENTATION_THRESHOLD, NULL);
	if (ret)
		goto out;

	vwrq->value = priv->fragthsd;
	vwrq->disabled = ((vwrq->value < MRVDRV_FRAG_MIN_VALUE)
			  || (vwrq->value > MRVDRV_FRAG_MAX_VALUE));
	vwrq->fixed = 1;

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_get_mode(struct net_device *dev,
			 struct iw_request_info *info, u32 * uwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	*uwrq = priv->mode;

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int mesh_wlan_get_mode(struct net_device *dev,
		              struct iw_request_info *info, u32 * uwrq,
			      char *extra)
{
	lbs_deb_enter(LBS_DEB_WEXT);

	*uwrq = IW_MODE_REPEAT ;

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_get_txpow(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;
	s16 curlevel = 0;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (!priv->radio_on) {
		lbs_deb_wext("tx power off\n");
		vwrq->value = 0;
		vwrq->disabled = 1;
		goto out;
	}

	ret = lbs_get_tx_power(priv, &curlevel, NULL, NULL);
	if (ret)
		goto out;

	lbs_deb_wext("tx power level %d dbm\n", curlevel);
	priv->txpower_cur = curlevel;

	vwrq->value = curlevel;
	vwrq->fixed = 1;
	vwrq->disabled = 0;
	vwrq->flags = IW_TXPOW_DBM;

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_set_retry(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (vwrq->flags == IW_RETRY_LIMIT) {
		/* The MAC has a 4-bit Total_Tx_Count register
		   Total_Tx_Count = 1 + Tx_Retry_Count */
#define TX_RETRY_MIN 0
#define TX_RETRY_MAX 14
		if (vwrq->value < TX_RETRY_MIN || vwrq->value > TX_RETRY_MAX)
			return -EINVAL;

		/* Adding 1 to convert retry count to try count */
		priv->txretrycount = vwrq->value + 1;

		ret = lbs_prepare_and_send_command(priv, CMD_802_11_SNMP_MIB,
					    CMD_ACT_SET,
					    CMD_OPTION_WAITFORRSP,
					    OID_802_11_TX_RETRYCOUNT, NULL);

		if (ret)
			goto out;
	} else {
		return -EOPNOTSUPP;
	}

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_get_retry(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	priv->txretrycount = 0;
	ret = lbs_prepare_and_send_command(priv,
				    CMD_802_11_SNMP_MIB,
				    CMD_ACT_GET, CMD_OPTION_WAITFORRSP,
				    OID_802_11_TX_RETRYCOUNT, NULL);
	if (ret)
		goto out;

	vwrq->disabled = 0;
	if (!vwrq->flags) {
		vwrq->flags = IW_RETRY_LIMIT;
		/* Subtract 1 to convert try count to retry count */
		vwrq->value = priv->txretrycount - 1;
	}

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static inline void sort_channels(struct iw_freq *freq, int num)
{
	int i, j;
	struct iw_freq temp;

	for (i = 0; i < num; i++)
		for (j = i + 1; j < num; j++)
			if (freq[i].i > freq[j].i) {
				temp.i = freq[i].i;
				temp.m = freq[i].m;

				freq[i].i = freq[j].i;
				freq[i].m = freq[j].m;

				freq[j].i = temp.i;
				freq[j].m = temp.m;
			}
}

/* data rate listing
	MULTI_BANDS:
		abg		a	b	b/g
   Infra 	G(12)		A(8)	B(4)	G(12)
   Adhoc 	A+B(12)		A(8)	B(4)	B(4)

	non-MULTI_BANDS:
					b	b/g
   Infra 	     		    	B(4)	G(12)
   Adhoc 	      		    	B(4)	B(4)
 */
/**
 *  @brief Get Range Info
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info			A pointer to iw_request_info structure
 *  @param vwrq 		A pointer to iw_param structure
 *  @param extra		A pointer to extra data buf
 *  @return 	   		0 --success, otherwise fail
 */
static int lbs_get_range(struct net_device *dev, struct iw_request_info *info,
			  struct iw_point *dwrq, char *extra)
{
	int i, j;
	struct lbs_private *priv = dev->priv;
	struct iw_range *range = (struct iw_range *)extra;
	struct chan_freq_power *cfp;
	u8 rates[MAX_RATES + 1];

	u8 flag = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->min_nwid = 0;
	range->max_nwid = 0;

	memset(rates, 0, sizeof(rates));
	copy_active_data_rates(priv, rates);
	range->num_bitrates = strnlen(rates, IW_MAX_BITRATES);
	for (i = 0; i < range->num_bitrates; i++)
		range->bitrate[i] = rates[i] * 500000;
	range->num_bitrates = i;
	lbs_deb_wext("IW_MAX_BITRATES %d, num_bitrates %d\n", IW_MAX_BITRATES,
	       range->num_bitrates);

	range->num_frequency = 0;

	range->scan_capa = IW_SCAN_CAPA_ESSID;

	if (priv->enable11d &&
	    (priv->connect_status == LBS_CONNECTED ||
	    priv->mesh_connect_status == LBS_CONNECTED)) {
		u8 chan_no;
		u8 band;

		struct parsed_region_chan_11d *parsed_region_chan =
		    &priv->parsed_region_chan;

		if (parsed_region_chan == NULL) {
			lbs_deb_wext("11d: parsed_region_chan is NULL\n");
			goto out;
		}
		band = parsed_region_chan->band;
		lbs_deb_wext("band %d, nr_char %d\n", band,
		       parsed_region_chan->nr_chan);

		for (i = 0; (range->num_frequency < IW_MAX_FREQUENCIES)
		     && (i < parsed_region_chan->nr_chan); i++) {
			chan_no = parsed_region_chan->chanpwr[i].chan;
			lbs_deb_wext("chan_no %d\n", chan_no);
			range->freq[range->num_frequency].i = (long)chan_no;
			range->freq[range->num_frequency].m =
			    (long)lbs_chan_2_freq(chan_no) * 100000;
			range->freq[range->num_frequency].e = 1;
			range->num_frequency++;
		}
		flag = 1;
	}
	if (!flag) {
		for (j = 0; (range->num_frequency < IW_MAX_FREQUENCIES)
		     && (j < ARRAY_SIZE(priv->region_channel)); j++) {
			cfp = priv->region_channel[j].CFP;
			for (i = 0; (range->num_frequency < IW_MAX_FREQUENCIES)
			     && priv->region_channel[j].valid
			     && cfp
			     && (i < priv->region_channel[j].nrcfp); i++) {
				range->freq[range->num_frequency].i =
				    (long)cfp->channel;
				range->freq[range->num_frequency].m =
				    (long)cfp->freq * 100000;
				range->freq[range->num_frequency].e = 1;
				cfp++;
				range->num_frequency++;
			}
		}
	}

	lbs_deb_wext("IW_MAX_FREQUENCIES %d, num_frequency %d\n",
	       IW_MAX_FREQUENCIES, range->num_frequency);

	range->num_channels = range->num_frequency;

	sort_channels(&range->freq[0], range->num_frequency);

	/*
	 * Set an indication of the max TCP throughput in bit/s that we can
	 * expect using this interface
	 */
	if (i > 2)
		range->throughput = 5000 * 1000;
	else
		range->throughput = 1500 * 1000;

	range->min_rts = MRVDRV_RTS_MIN_VALUE;
	range->max_rts = MRVDRV_RTS_MAX_VALUE;
	range->min_frag = MRVDRV_FRAG_MIN_VALUE;
	range->max_frag = MRVDRV_FRAG_MAX_VALUE;

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;
	range->num_encoding_sizes = 2;
	range->max_encoding_tokens = 4;

	/*
	 * Right now we support only "iwconfig ethX power on|off"
	 */
	range->pm_capa = IW_POWER_ON;

	/*
	 * Minimum version we recommend
	 */
	range->we_version_source = 15;

	/*
	 * Version we are compiled with
	 */
	range->we_version_compiled = WIRELESS_EXT;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT | IW_RETRY_MAX;

	range->min_retry = TX_RETRY_MIN;
	range->max_retry = TX_RETRY_MAX;

	/*
	 * Set the qual, level and noise range values
	 */
	range->max_qual.qual = 100;
	range->max_qual.level = 0;
	range->max_qual.noise = 0;
	range->max_qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;

	range->avg_qual.qual = 70;
	/* TODO: Find real 'good' to 'bad' threshold value for RSSI */
	range->avg_qual.level = 0;
	range->avg_qual.noise = 0;
	range->avg_qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;

	range->sensitivity = 0;

	/* Setup the supported power level ranges */
	memset(range->txpower, 0, sizeof(range->txpower));
	range->txpower_capa = IW_TXPOW_DBM | IW_TXPOW_RANGE;
	range->txpower[0] = priv->txpower_min;
	range->txpower[1] = priv->txpower_max;
	range->num_txpower = 2;

	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
				IW_EVENT_CAPA_MASK(SIOCGIWAP) |
				IW_EVENT_CAPA_MASK(SIOCGIWSCAN));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;

	if (priv->fwcapinfo & FW_CAPINFO_WPA) {
		range->enc_capa =   IW_ENC_CAPA_WPA
		                  | IW_ENC_CAPA_WPA2
		                  | IW_ENC_CAPA_CIPHER_TKIP
		                  | IW_ENC_CAPA_CIPHER_CCMP;
	}

out:
	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_set_power(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (!priv->ps_supported) {
		if (vwrq->disabled)
			return 0;
		else
			return -EINVAL;
	}

	/* PS is currently supported only in Infrastructure mode
	 * Remove this check if it is to be supported in IBSS mode also
	 */

	if (vwrq->disabled) {
		priv->psmode = LBS802_11POWERMODECAM;
		if (priv->psstate != PS_STATE_FULL_POWER) {
			lbs_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
		}

		return 0;
	}

	if ((vwrq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		lbs_deb_wext(
		       "setting power timeout is not supported\n");
		return -EINVAL;
	} else if ((vwrq->flags & IW_POWER_TYPE) == IW_POWER_PERIOD) {
		lbs_deb_wext("setting power period not supported\n");
		return -EINVAL;
	}

	if (priv->psmode != LBS802_11POWERMODECAM) {
		return 0;
	}

	priv->psmode = LBS802_11POWERMODEMAX_PSP;

	if (priv->connect_status == LBS_CONNECTED) {
		lbs_ps_sleep(priv, CMD_OPTION_WAITFORRSP);
	}

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_get_power(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	vwrq->value = 0;
	vwrq->flags = 0;
	vwrq->disabled = priv->psmode == LBS802_11POWERMODECAM
		|| priv->connect_status == LBS_DISCONNECTED;

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static struct iw_statistics *lbs_get_wireless_stats(struct net_device *dev)
{
	enum {
		POOR = 30,
		FAIR = 60,
		GOOD = 80,
		VERY_GOOD = 90,
		EXCELLENT = 95,
		PERFECT = 100
	};
	struct lbs_private *priv = dev->priv;
	u32 rssi_qual;
	u32 tx_qual;
	u32 quality = 0;
	int stats_valid = 0;
	u8 rssi;
	u32 tx_retries;
	struct cmd_ds_802_11_get_log log;

	lbs_deb_enter(LBS_DEB_WEXT);

	priv->wstats.status = priv->mode;

	/* If we're not associated, all quality values are meaningless */
	if ((priv->connect_status != LBS_CONNECTED) &&
	    (priv->mesh_connect_status != LBS_CONNECTED))
		goto out;

	/* Quality by RSSI */
	priv->wstats.qual.level =
	    CAL_RSSI(priv->SNR[TYPE_BEACON][TYPE_NOAVG],
	     priv->NF[TYPE_BEACON][TYPE_NOAVG]);

	if (priv->NF[TYPE_BEACON][TYPE_NOAVG] == 0) {
		priv->wstats.qual.noise = MRVDRV_NF_DEFAULT_SCAN_VALUE;
	} else {
		priv->wstats.qual.noise =
		    CAL_NF(priv->NF[TYPE_BEACON][TYPE_NOAVG]);
	}

	lbs_deb_wext("signal level %#x\n", priv->wstats.qual.level);
	lbs_deb_wext("noise %#x\n", priv->wstats.qual.noise);

	rssi = priv->wstats.qual.level - priv->wstats.qual.noise;
	if (rssi < 15)
		rssi_qual = rssi * POOR / 10;
	else if (rssi < 20)
		rssi_qual = (rssi - 15) * (FAIR - POOR) / 5 + POOR;
	else if (rssi < 30)
		rssi_qual = (rssi - 20) * (GOOD - FAIR) / 5 + FAIR;
	else if (rssi < 40)
		rssi_qual = (rssi - 30) * (VERY_GOOD - GOOD) /
		    10 + GOOD;
	else
		rssi_qual = (rssi - 40) * (PERFECT - VERY_GOOD) /
		    10 + VERY_GOOD;
	quality = rssi_qual;

	/* Quality by TX errors */
	priv->wstats.discard.retries = priv->stats.tx_errors;

	memset(&log, 0, sizeof(log));
	log.hdr.size = cpu_to_le16(sizeof(log));
	lbs_cmd_with_response(priv, CMD_802_11_GET_LOG, &log);

	tx_retries = le32_to_cpu(log.retry);

	if (tx_retries > 75)
		tx_qual = (90 - tx_retries) * POOR / 15;
	else if (tx_retries > 70)
		tx_qual = (75 - tx_retries) * (FAIR - POOR) / 5 + POOR;
	else if (tx_retries > 65)
		tx_qual = (70 - tx_retries) * (GOOD - FAIR) / 5 + FAIR;
	else if (tx_retries > 50)
		tx_qual = (65 - tx_retries) * (VERY_GOOD - GOOD) /
		    15 + GOOD;
	else
		tx_qual = (50 - tx_retries) *
		    (PERFECT - VERY_GOOD) / 50 + VERY_GOOD;
	quality = min(quality, tx_qual);

	priv->wstats.discard.code = le32_to_cpu(log.wepundecryptable);
	priv->wstats.discard.retries = tx_retries;
	priv->wstats.discard.misc = le32_to_cpu(log.ackfailure);

	/* Calculate quality */
	priv->wstats.qual.qual = min_t(u8, quality, 100);
	priv->wstats.qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
	stats_valid = 1;

	/* update stats asynchronously for future calls */
	lbs_prepare_and_send_command(priv, CMD_802_11_RSSI, 0,
					0, 0, NULL);
out:
	if (!stats_valid) {
		priv->wstats.miss.beacon = 0;
		priv->wstats.discard.retries = 0;
		priv->wstats.qual.qual = 0;
		priv->wstats.qual.level = 0;
		priv->wstats.qual.noise = 0;
		priv->wstats.qual.updated = IW_QUAL_ALL_UPDATED;
		priv->wstats.qual.updated |= IW_QUAL_NOISE_INVALID |
		    IW_QUAL_QUAL_INVALID | IW_QUAL_LEVEL_INVALID;
	}

	lbs_deb_leave(LBS_DEB_WEXT);
	return &priv->wstats;


}

static int lbs_set_freq(struct net_device *dev, struct iw_request_info *info,
		  struct iw_freq *fwrq, char *extra)
{
	int ret = -EINVAL;
	struct lbs_private *priv = dev->priv;
	struct chan_freq_power *cfp;
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&priv->lock);
	assoc_req = lbs_get_association_request(priv);
	if (!assoc_req) {
		ret = -ENOMEM;
		goto out;
	}

	/* If setting by frequency, convert to a channel */
	if (fwrq->e == 1) {
		long f = fwrq->m / 100000;

		cfp = find_cfp_by_band_and_freq(priv, 0, f);
		if (!cfp) {
			lbs_deb_wext("invalid freq %ld\n", f);
			goto out;
		}

		fwrq->e = 0;
		fwrq->m = (int) cfp->channel;
	}

	/* Setting by channel number */
	if (fwrq->m > 1000 || fwrq->e > 0) {
		goto out;
	}

	cfp = lbs_find_cfp_by_band_and_channel(priv, 0, fwrq->m);
	if (!cfp) {
		goto out;
	}

	assoc_req->channel = fwrq->m;
	ret = 0;

out:
	if (ret == 0) {
		set_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags);
		lbs_postpone_association_work(priv);
	} else {
		lbs_cancel_association_work(priv);
	}
	mutex_unlock(&priv->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_mesh_set_freq(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_freq *fwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;
	struct chan_freq_power *cfp;
	int ret = -EINVAL;

	lbs_deb_enter(LBS_DEB_WEXT);

	/* If setting by frequency, convert to a channel */
	if (fwrq->e == 1) {
		long f = fwrq->m / 100000;

		cfp = find_cfp_by_band_and_freq(priv, 0, f);
		if (!cfp) {
			lbs_deb_wext("invalid freq %ld\n", f);
			goto out;
		}

		fwrq->e = 0;
		fwrq->m = (int) cfp->channel;
	}

	/* Setting by channel number */
	if (fwrq->m > 1000 || fwrq->e > 0) {
		goto out;
	}

	cfp = lbs_find_cfp_by_band_and_channel(priv, 0, fwrq->m);
	if (!cfp) {
		goto out;
	}

	if (fwrq->m != priv->curbssparams.channel) {
		lbs_deb_wext("mesh channel change forces eth disconnect\n");
		if (priv->mode == IW_MODE_INFRA)
			lbs_cmd_80211_deauthenticate(priv,
						     priv->curbssparams.bssid,
						     WLAN_REASON_DEAUTH_LEAVING);
		else if (priv->mode == IW_MODE_ADHOC)
			lbs_stop_adhoc_network(priv);
	}
	lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START, fwrq->m);
	lbs_update_channel(priv);
	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_set_rate(struct net_device *dev, struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;
	u8 new_rate = 0;
	int ret = -EINVAL;
	u8 rates[MAX_RATES + 1];

	lbs_deb_enter(LBS_DEB_WEXT);
	lbs_deb_wext("vwrq->value %d\n", vwrq->value);
	lbs_deb_wext("vwrq->fixed %d\n", vwrq->fixed);

	if (vwrq->fixed && vwrq->value == -1)
		goto out;

	/* Auto rate? */
	priv->enablehwauto = !vwrq->fixed;

	if (vwrq->value == -1)
		priv->cur_rate = 0;
	else {
		if (vwrq->value % 100000)
			goto out;

		new_rate = vwrq->value / 500000;
		priv->cur_rate = new_rate;
		/* the rest is only needed for lbs_set_data_rate() */
		memset(rates, 0, sizeof(rates));
		copy_active_data_rates(priv, rates);
		if (!memchr(rates, new_rate, sizeof(rates))) {
			lbs_pr_alert("fixed data rate 0x%X out of range\n",
				new_rate);
			goto out;
		}
	}

	/* Try the newer command first (Firmware Spec 5.1 and above) */
	ret = lbs_cmd_802_11_rate_adapt_rateset(priv, CMD_ACT_SET);

	/* Fallback to older version */
	if (ret)
		ret = lbs_set_data_rate(priv, new_rate);

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_get_rate(struct net_device *dev, struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (priv->connect_status == LBS_CONNECTED) {
		vwrq->value = priv->cur_rate * 500000;

		if (priv->enablehwauto)
			vwrq->fixed = 0;
		else
			vwrq->fixed = 1;

	} else {
		vwrq->fixed = 0;
		vwrq->value = 0;
	}

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_set_mode(struct net_device *dev,
		  struct iw_request_info *info, u32 * uwrq, char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (   (*uwrq != IW_MODE_ADHOC)
	    && (*uwrq != IW_MODE_INFRA)
	    && (*uwrq != IW_MODE_AUTO)) {
		lbs_deb_wext("Invalid mode: 0x%x\n", *uwrq);
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&priv->lock);
	assoc_req = lbs_get_association_request(priv);
	if (!assoc_req) {
		ret = -ENOMEM;
		lbs_cancel_association_work(priv);
	} else {
		assoc_req->mode = *uwrq;
		set_bit(ASSOC_FLAG_MODE, &assoc_req->flags);
		lbs_postpone_association_work(priv);
		lbs_deb_wext("Switching to mode: 0x%x\n", *uwrq);
	}
	mutex_unlock(&priv->lock);

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}


/**
 *  @brief Get Encryption key
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info			A pointer to iw_request_info structure
 *  @param vwrq 		A pointer to iw_param structure
 *  @param extra		A pointer to extra data buf
 *  @return 	   		0 --success, otherwise fail
 */
static int lbs_get_encode(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *dwrq, u8 * extra)
{
	struct lbs_private *priv = dev->priv;
	int index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	lbs_deb_enter(LBS_DEB_WEXT);

	lbs_deb_wext("flags 0x%x, index %d, length %d, wep_tx_keyidx %d\n",
	       dwrq->flags, index, dwrq->length, priv->wep_tx_keyidx);

	dwrq->flags = 0;

	/* Authentication method */
	switch (priv->secinfo.auth_mode) {
	case IW_AUTH_ALG_OPEN_SYSTEM:
		dwrq->flags = IW_ENCODE_OPEN;
		break;

	case IW_AUTH_ALG_SHARED_KEY:
	case IW_AUTH_ALG_LEAP:
		dwrq->flags = IW_ENCODE_RESTRICTED;
		break;
	default:
		dwrq->flags = IW_ENCODE_DISABLED | IW_ENCODE_OPEN;
		break;
	}

	memset(extra, 0, 16);

	mutex_lock(&priv->lock);

	/* Default to returning current transmit key */
	if (index < 0)
		index = priv->wep_tx_keyidx;

	if ((priv->wep_keys[index].len) && priv->secinfo.wep_enabled) {
		memcpy(extra, priv->wep_keys[index].key,
		       priv->wep_keys[index].len);
		dwrq->length = priv->wep_keys[index].len;

		dwrq->flags |= (index + 1);
		/* Return WEP enabled */
		dwrq->flags &= ~IW_ENCODE_DISABLED;
	} else if ((priv->secinfo.WPAenabled)
		   || (priv->secinfo.WPA2enabled)) {
		/* return WPA enabled */
		dwrq->flags &= ~IW_ENCODE_DISABLED;
		dwrq->flags |= IW_ENCODE_NOKEY;
	} else {
		dwrq->flags |= IW_ENCODE_DISABLED;
	}

	mutex_unlock(&priv->lock);

	lbs_deb_wext("key: %02x:%02x:%02x:%02x:%02x:%02x, keylen %d\n",
	       extra[0], extra[1], extra[2],
	       extra[3], extra[4], extra[5], dwrq->length);

	lbs_deb_wext("return flags 0x%x\n", dwrq->flags);

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

/**
 *  @brief Set Encryption key (internal)
 *
 *  @param priv			A pointer to private card structure
 *  @param key_material		A pointer to key material
 *  @param key_length		length of key material
 *  @param index		key index to set
 *  @param set_tx_key		Force set TX key (1 = yes, 0 = no)
 *  @return 	   		0 --success, otherwise fail
 */
static int lbs_set_wep_key(struct assoc_request *assoc_req,
			    const char *key_material,
			    u16 key_length,
			    u16 index,
			    int set_tx_key)
{
	int ret = 0;
	struct enc_key *pkey;

	lbs_deb_enter(LBS_DEB_WEXT);

	/* Paranoid validation of key index */
	if (index > 3) {
		ret = -EINVAL;
		goto out;
	}

	/* validate max key length */
	if (key_length > KEY_LEN_WEP_104) {
		ret = -EINVAL;
		goto out;
	}

	pkey = &assoc_req->wep_keys[index];

	if (key_length > 0) {
		memset(pkey, 0, sizeof(struct enc_key));
		pkey->type = KEY_TYPE_ID_WEP;

		/* Standardize the key length */
		pkey->len = (key_length > KEY_LEN_WEP_40) ?
		                KEY_LEN_WEP_104 : KEY_LEN_WEP_40;
		memcpy(pkey->key, key_material, key_length);
	}

	if (set_tx_key) {
		/* Ensure the chosen key is valid */
		if (!pkey->len) {
			lbs_deb_wext("key not set, so cannot enable it\n");
			ret = -EINVAL;
			goto out;
		}
		assoc_req->wep_tx_keyidx = index;
	}

	assoc_req->secinfo.wep_enabled = 1;

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int validate_key_index(u16 def_index, u16 raw_index,
			      u16 *out_index, u16 *is_default)
{
	if (!out_index || !is_default)
		return -EINVAL;

	/* Verify index if present, otherwise use default TX key index */
	if (raw_index > 0) {
		if (raw_index > 4)
			return -EINVAL;
		*out_index = raw_index - 1;
	} else {
		*out_index = def_index;
		*is_default = 1;
	}
	return 0;
}

static void disable_wep(struct assoc_request *assoc_req)
{
	int i;

	lbs_deb_enter(LBS_DEB_WEXT);

	/* Set Open System auth mode */
	assoc_req->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;

	/* Clear WEP keys and mark WEP as disabled */
	assoc_req->secinfo.wep_enabled = 0;
	for (i = 0; i < 4; i++)
		assoc_req->wep_keys[i].len = 0;

	set_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags);
	set_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags);

	lbs_deb_leave(LBS_DEB_WEXT);
}

static void disable_wpa(struct assoc_request *assoc_req)
{
	lbs_deb_enter(LBS_DEB_WEXT);

	memset(&assoc_req->wpa_mcast_key, 0, sizeof (struct enc_key));
	assoc_req->wpa_mcast_key.flags = KEY_INFO_WPA_MCAST;
	set_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags);

	memset(&assoc_req->wpa_unicast_key, 0, sizeof (struct enc_key));
	assoc_req->wpa_unicast_key.flags = KEY_INFO_WPA_UNICAST;
	set_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags);

	assoc_req->secinfo.WPAenabled = 0;
	assoc_req->secinfo.WPA2enabled = 0;
	set_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags);

	lbs_deb_leave(LBS_DEB_WEXT);
}

/**
 *  @brief Set Encryption key
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info			A pointer to iw_request_info structure
 *  @param vwrq 		A pointer to iw_param structure
 *  @param extra		A pointer to extra data buf
 *  @return 	   		0 --success, otherwise fail
 */
static int lbs_set_encode(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_point *dwrq, char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;
	struct assoc_request * assoc_req;
	u16 is_default = 0, index = 0, set_tx_key = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&priv->lock);
	assoc_req = lbs_get_association_request(priv);
	if (!assoc_req) {
		ret = -ENOMEM;
		goto out;
	}

	if (dwrq->flags & IW_ENCODE_DISABLED) {
		disable_wep (assoc_req);
		disable_wpa (assoc_req);
		goto out;
	}

	ret = validate_key_index(assoc_req->wep_tx_keyidx,
	                         (dwrq->flags & IW_ENCODE_INDEX),
	                         &index, &is_default);
	if (ret) {
		ret = -EINVAL;
		goto out;
	}

	/* If WEP isn't enabled, or if there is no key data but a valid
	 * index, set the TX key.
	 */
	if (!assoc_req->secinfo.wep_enabled || (dwrq->length == 0 && !is_default))
		set_tx_key = 1;

	ret = lbs_set_wep_key(assoc_req, extra, dwrq->length, index, set_tx_key);
	if (ret)
		goto out;

	if (dwrq->length)
		set_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags);
	if (set_tx_key)
		set_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags);

	if (dwrq->flags & IW_ENCODE_RESTRICTED) {
		assoc_req->secinfo.auth_mode = IW_AUTH_ALG_SHARED_KEY;
	} else if (dwrq->flags & IW_ENCODE_OPEN) {
		assoc_req->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
	}

out:
	if (ret == 0) {
		set_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags);
		lbs_postpone_association_work(priv);
	} else {
		lbs_cancel_association_work(priv);
	}
	mutex_unlock(&priv->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

/**
 *  @brief Get Extended Encryption key (WPA/802.1x and WEP)
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info			A pointer to iw_request_info structure
 *  @param vwrq 		A pointer to iw_param structure
 *  @param extra		A pointer to extra data buf
 *  @return 	   		0 on success, otherwise failure
 */
static int lbs_get_encodeext(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq,
			      char *extra)
{
	int ret = -EINVAL;
	struct lbs_private *priv = dev->priv;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int index, max_key_len;

	lbs_deb_enter(LBS_DEB_WEXT);

	max_key_len = dwrq->length - sizeof(*ext);
	if (max_key_len < 0)
		goto out;

	index = dwrq->flags & IW_ENCODE_INDEX;
	if (index) {
		if (index < 1 || index > 4)
			goto out;
		index--;
	} else {
		index = priv->wep_tx_keyidx;
	}

	if (!(ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) &&
	    ext->alg != IW_ENCODE_ALG_WEP) {
		if (index != 0 || priv->mode != IW_MODE_INFRA)
			goto out;
	}

	dwrq->flags = index + 1;
	memset(ext, 0, sizeof(*ext));

	if (   !priv->secinfo.wep_enabled
	    && !priv->secinfo.WPAenabled
	    && !priv->secinfo.WPA2enabled) {
		ext->alg = IW_ENCODE_ALG_NONE;
		ext->key_len = 0;
		dwrq->flags |= IW_ENCODE_DISABLED;
	} else {
		u8 *key = NULL;

		if (   priv->secinfo.wep_enabled
		    && !priv->secinfo.WPAenabled
		    && !priv->secinfo.WPA2enabled) {
			/* WEP */
			ext->alg = IW_ENCODE_ALG_WEP;
			ext->key_len = priv->wep_keys[index].len;
			key = &priv->wep_keys[index].key[0];
		} else if (   !priv->secinfo.wep_enabled
		           && (priv->secinfo.WPAenabled ||
		               priv->secinfo.WPA2enabled)) {
			/* WPA */
			struct enc_key * pkey = NULL;

			if (   priv->wpa_mcast_key.len
			    && (priv->wpa_mcast_key.flags & KEY_INFO_WPA_ENABLED))
				pkey = &priv->wpa_mcast_key;
			else if (   priv->wpa_unicast_key.len
			         && (priv->wpa_unicast_key.flags & KEY_INFO_WPA_ENABLED))
				pkey = &priv->wpa_unicast_key;

			if (pkey) {
				if (pkey->type == KEY_TYPE_ID_AES) {
					ext->alg = IW_ENCODE_ALG_CCMP;
				} else {
					ext->alg = IW_ENCODE_ALG_TKIP;
				}
				ext->key_len = pkey->len;
				key = &pkey->key[0];
			} else {
				ext->alg = IW_ENCODE_ALG_TKIP;
				ext->key_len = 0;
			}
		} else {
			goto out;
		}

		if (ext->key_len > max_key_len) {
			ret = -E2BIG;
			goto out;
		}

		if (ext->key_len)
			memcpy(ext->key, key, ext->key_len);
		else
			dwrq->flags |= IW_ENCODE_NOKEY;
		dwrq->flags |= IW_ENCODE_ENABLED;
	}
	ret = 0;

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

/**
 *  @brief Set Encryption key Extended (WPA/802.1x and WEP)
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info			A pointer to iw_request_info structure
 *  @param vwrq 		A pointer to iw_param structure
 *  @param extra		A pointer to extra data buf
 *  @return 	   		0 --success, otherwise fail
 */
static int lbs_set_encodeext(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq,
			      char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int alg = ext->alg;
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&priv->lock);
	assoc_req = lbs_get_association_request(priv);
	if (!assoc_req) {
		ret = -ENOMEM;
		goto out;
	}

	if ((alg == IW_ENCODE_ALG_NONE) || (dwrq->flags & IW_ENCODE_DISABLED)) {
		disable_wep (assoc_req);
		disable_wpa (assoc_req);
	} else if (alg == IW_ENCODE_ALG_WEP) {
		u16 is_default = 0, index, set_tx_key = 0;

		ret = validate_key_index(assoc_req->wep_tx_keyidx,
		                         (dwrq->flags & IW_ENCODE_INDEX),
		                         &index, &is_default);
		if (ret)
			goto out;

		/* If WEP isn't enabled, or if there is no key data but a valid
		 * index, or if the set-TX-key flag was passed, set the TX key.
		 */
		if (   !assoc_req->secinfo.wep_enabled
		    || (dwrq->length == 0 && !is_default)
		    || (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY))
			set_tx_key = 1;

		/* Copy key to driver */
		ret = lbs_set_wep_key(assoc_req, ext->key, ext->key_len, index,
					set_tx_key);
		if (ret)
			goto out;

		if (dwrq->flags & IW_ENCODE_RESTRICTED) {
			assoc_req->secinfo.auth_mode = IW_AUTH_ALG_SHARED_KEY;
		} else if (dwrq->flags & IW_ENCODE_OPEN) {
			assoc_req->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
		}

		/* Mark the various WEP bits as modified */
		set_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags);
		if (dwrq->length)
			set_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags);
		if (set_tx_key)
			set_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags);
	} else if ((alg == IW_ENCODE_ALG_TKIP) || (alg == IW_ENCODE_ALG_CCMP)) {
		struct enc_key * pkey;

		/* validate key length */
		if (((alg == IW_ENCODE_ALG_TKIP)
			&& (ext->key_len != KEY_LEN_WPA_TKIP))
		    || ((alg == IW_ENCODE_ALG_CCMP)
		        && (ext->key_len != KEY_LEN_WPA_AES))) {
				lbs_deb_wext("invalid size %d for key of alg "
				       "type %d\n",
				       ext->key_len,
				       alg);
				ret = -EINVAL;
				goto out;
		}

		if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
			pkey = &assoc_req->wpa_mcast_key;
			set_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags);
		} else {
			pkey = &assoc_req->wpa_unicast_key;
			set_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags);
		}

		memset(pkey, 0, sizeof (struct enc_key));
		memcpy(pkey->key, ext->key, ext->key_len);
		pkey->len = ext->key_len;
		if (pkey->len)
			pkey->flags |= KEY_INFO_WPA_ENABLED;

		/* Do this after zeroing key structure */
		if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
			pkey->flags |= KEY_INFO_WPA_MCAST;
		} else {
			pkey->flags |= KEY_INFO_WPA_UNICAST;
		}

		if (alg == IW_ENCODE_ALG_TKIP) {
			pkey->type = KEY_TYPE_ID_TKIP;
		} else if (alg == IW_ENCODE_ALG_CCMP) {
			pkey->type = KEY_TYPE_ID_AES;
		}

		/* If WPA isn't enabled yet, do that now */
		if (   assoc_req->secinfo.WPAenabled == 0
		    && assoc_req->secinfo.WPA2enabled == 0) {
			assoc_req->secinfo.WPAenabled = 1;
			assoc_req->secinfo.WPA2enabled = 1;
			set_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags);
		}

		disable_wep (assoc_req);
	}

out:
	if (ret == 0) {
		lbs_postpone_association_work(priv);
	} else {
		lbs_cancel_association_work(priv);
	}
	mutex_unlock(&priv->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}


static int lbs_set_genie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	struct lbs_private *priv = dev->priv;
	int ret = 0;
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&priv->lock);
	assoc_req = lbs_get_association_request(priv);
	if (!assoc_req) {
		ret = -ENOMEM;
		goto out;
	}

	if (dwrq->length > MAX_WPA_IE_LEN ||
	    (dwrq->length && extra == NULL)) {
		ret = -EINVAL;
		goto out;
	}

	if (dwrq->length) {
		memcpy(&assoc_req->wpa_ie[0], extra, dwrq->length);
		assoc_req->wpa_ie_len = dwrq->length;
	} else {
		memset(&assoc_req->wpa_ie[0], 0, sizeof(priv->wpa_ie));
		assoc_req->wpa_ie_len = 0;
	}

out:
	if (ret == 0) {
		set_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags);
		lbs_postpone_association_work(priv);
	} else {
		lbs_cancel_association_work(priv);
	}
	mutex_unlock(&priv->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_get_genie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (priv->wpa_ie_len == 0) {
		dwrq->length = 0;
		goto out;
	}

	if (dwrq->length < priv->wpa_ie_len) {
		ret = -E2BIG;
		goto out;
	}

	dwrq->length = priv->wpa_ie_len;
	memcpy(extra, &priv->wpa_ie[0], priv->wpa_ie_len);

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}


static int lbs_set_auth(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *dwrq,
			 char *extra)
{
	struct lbs_private *priv = dev->priv;
	struct assoc_request * assoc_req;
	int ret = 0;
	int updated = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&priv->lock);
	assoc_req = lbs_get_association_request(priv);
	if (!assoc_req) {
		ret = -ENOMEM;
		goto out;
	}

	switch (dwrq->flags & IW_AUTH_INDEX) {
	case IW_AUTH_TKIP_COUNTERMEASURES:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
	case IW_AUTH_DROP_UNENCRYPTED:
		/*
		 * libertas does not use these parameters
		 */
		break;

	case IW_AUTH_WPA_VERSION:
		if (dwrq->value & IW_AUTH_WPA_VERSION_DISABLED) {
			assoc_req->secinfo.WPAenabled = 0;
			assoc_req->secinfo.WPA2enabled = 0;
			disable_wpa (assoc_req);
		}
		if (dwrq->value & IW_AUTH_WPA_VERSION_WPA) {
			assoc_req->secinfo.WPAenabled = 1;
			assoc_req->secinfo.wep_enabled = 0;
			assoc_req->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
		}
		if (dwrq->value & IW_AUTH_WPA_VERSION_WPA2) {
			assoc_req->secinfo.WPA2enabled = 1;
			assoc_req->secinfo.wep_enabled = 0;
			assoc_req->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
		}
		updated = 1;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		if (dwrq->value & IW_AUTH_ALG_SHARED_KEY) {
			assoc_req->secinfo.auth_mode = IW_AUTH_ALG_SHARED_KEY;
		} else if (dwrq->value & IW_AUTH_ALG_OPEN_SYSTEM) {
			assoc_req->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
		} else if (dwrq->value & IW_AUTH_ALG_LEAP) {
			assoc_req->secinfo.auth_mode = IW_AUTH_ALG_LEAP;
		} else {
			ret = -EINVAL;
		}
		updated = 1;
		break;

	case IW_AUTH_WPA_ENABLED:
		if (dwrq->value) {
			if (!assoc_req->secinfo.WPAenabled &&
			    !assoc_req->secinfo.WPA2enabled) {
				assoc_req->secinfo.WPAenabled = 1;
				assoc_req->secinfo.WPA2enabled = 1;
				assoc_req->secinfo.wep_enabled = 0;
				assoc_req->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
			}
		} else {
			assoc_req->secinfo.WPAenabled = 0;
			assoc_req->secinfo.WPA2enabled = 0;
			disable_wpa (assoc_req);
		}
		updated = 1;
		break;

	default:
		ret = -EOPNOTSUPP;
		break;
	}

out:
	if (ret == 0) {
		if (updated)
			set_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags);
		lbs_postpone_association_work(priv);
	} else if (ret != -EOPNOTSUPP) {
		lbs_cancel_association_work(priv);
	}
	mutex_unlock(&priv->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_get_auth(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *dwrq,
			 char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	switch (dwrq->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		dwrq->value = 0;
		if (priv->secinfo.WPAenabled)
			dwrq->value |= IW_AUTH_WPA_VERSION_WPA;
		if (priv->secinfo.WPA2enabled)
			dwrq->value |= IW_AUTH_WPA_VERSION_WPA2;
		if (!dwrq->value)
			dwrq->value |= IW_AUTH_WPA_VERSION_DISABLED;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		dwrq->value = priv->secinfo.auth_mode;
		break;

	case IW_AUTH_WPA_ENABLED:
		if (priv->secinfo.WPAenabled && priv->secinfo.WPA2enabled)
			dwrq->value = 1;
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}


static int lbs_set_txpow(struct net_device *dev, struct iw_request_info *info,
		   struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	struct lbs_private *priv = dev->priv;
	s16 dbm = (s16) vwrq->value;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (vwrq->disabled) {
		lbs_set_radio(priv, RADIO_PREAMBLE_AUTO, 0);
		goto out;
	}

	if (vwrq->fixed == 0) {
		/* Auto power control */
		dbm = priv->txpower_max;
	} else {
		/* Userspace check in iwrange if it should use dBm or mW,
		 * therefore this should never happen... Jean II */
		if ((vwrq->flags & IW_TXPOW_TYPE) != IW_TXPOW_DBM) {
			ret = -EOPNOTSUPP;
			goto out;
		}

		/* Validate requested power level against firmware allowed levels */
		if (priv->txpower_min && (dbm < priv->txpower_min)) {
			ret = -EINVAL;
			goto out;
		}

		if (priv->txpower_max && (dbm > priv->txpower_max)) {
			ret = -EINVAL;
			goto out;
		}
	}

	/* If the radio was off, turn it on */
	if (!priv->radio_on) {
		ret = lbs_set_radio(priv, RADIO_PREAMBLE_AUTO, 1);
		if (ret)
			goto out;
	}

	lbs_deb_wext("txpower set %d dBm\n", dbm);

	ret = lbs_set_tx_power(priv, dbm);

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_get_essid(struct net_device *dev, struct iw_request_info *info,
		   struct iw_point *dwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	/*
	 * Note : if dwrq->flags != 0, we should get the relevant SSID from
	 * the SSID list...
	 */

	/*
	 * Get the current SSID
	 */
	if (priv->connect_status == LBS_CONNECTED) {
		memcpy(extra, priv->curbssparams.ssid,
		       priv->curbssparams.ssid_len);
		extra[priv->curbssparams.ssid_len] = '\0';
	} else {
		memset(extra, 0, 32);
		extra[priv->curbssparams.ssid_len] = '\0';
	}
	/*
	 * If none, we may want to get the one that was set
	 */

	dwrq->length = priv->curbssparams.ssid_len;

	dwrq->flags = 1;	/* active */

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_set_essid(struct net_device *dev, struct iw_request_info *info,
		   struct iw_point *dwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;
	int ret = 0;
	u8 ssid[IW_ESSID_MAX_SIZE];
	u8 ssid_len = 0;
	struct assoc_request * assoc_req;
	int in_ssid_len = dwrq->length;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (!priv->radio_on) {
		ret = -EINVAL;
		goto out;
	}

	/* Check the size of the string */
	if (in_ssid_len > IW_ESSID_MAX_SIZE) {
		ret = -E2BIG;
		goto out;
	}

	memset(&ssid, 0, sizeof(ssid));

	if (!dwrq->flags || !in_ssid_len) {
		/* "any" SSID requested; leave SSID blank */
	} else {
		/* Specific SSID requested */
		memcpy(&ssid, extra, in_ssid_len);
		ssid_len = in_ssid_len;
	}

	if (!ssid_len) {
		lbs_deb_wext("requested any SSID\n");
	} else {
		lbs_deb_wext("requested SSID '%s'\n",
		             escape_essid(ssid, ssid_len));
	}

out:
	mutex_lock(&priv->lock);
	if (ret == 0) {
		/* Get or create the current association request */
		assoc_req = lbs_get_association_request(priv);
		if (!assoc_req) {
			ret = -ENOMEM;
		} else {
			/* Copy the SSID to the association request */
			memcpy(&assoc_req->ssid, &ssid, IW_ESSID_MAX_SIZE);
			assoc_req->ssid_len = ssid_len;
			set_bit(ASSOC_FLAG_SSID, &assoc_req->flags);
			lbs_postpone_association_work(priv);
		}
	}

	/* Cancel the association request if there was an error */
	if (ret != 0) {
		lbs_cancel_association_work(priv);
	}

	mutex_unlock(&priv->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int lbs_mesh_get_essid(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_WEXT);

	memcpy(extra, priv->mesh_ssid, priv->mesh_ssid_len);

	dwrq->length = priv->mesh_ssid_len;

	dwrq->flags = 1;	/* active */

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int lbs_mesh_set_essid(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq, char *extra)
{
	struct lbs_private *priv = dev->priv;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (!priv->radio_on) {
		ret = -EINVAL;
		goto out;
	}

	/* Check the size of the string */
	if (dwrq->length > IW_ESSID_MAX_SIZE) {
		ret = -E2BIG;
		goto out;
	}

	if (!dwrq->flags || !dwrq->length) {
		ret = -EINVAL;
		goto out;
	} else {
		/* Specific SSID requested */
		memcpy(priv->mesh_ssid, extra, dwrq->length);
		priv->mesh_ssid_len = dwrq->length;
	}

	lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START,
			priv->curbssparams.channel);
 out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

/**
 *  @brief Connect to the AP or Ad-hoc Network with specific bssid
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param awrq         A pointer to iw_param structure
 *  @param extra        A pointer to extra data buf
 *  @return             0 --success, otherwise fail
 */
static int lbs_set_wap(struct net_device *dev, struct iw_request_info *info,
		 struct sockaddr *awrq, char *extra)
{
	struct lbs_private *priv = dev->priv;
	struct assoc_request * assoc_req;
	int ret = 0;
	DECLARE_MAC_BUF(mac);

	lbs_deb_enter(LBS_DEB_WEXT);

	if (!priv->radio_on)
		return -EINVAL;

	if (awrq->sa_family != ARPHRD_ETHER)
		return -EINVAL;

	lbs_deb_wext("ASSOC: WAP: sa_data %s\n", print_mac(mac, awrq->sa_data));

	mutex_lock(&priv->lock);

	/* Get or create the current association request */
	assoc_req = lbs_get_association_request(priv);
	if (!assoc_req) {
		lbs_cancel_association_work(priv);
		ret = -ENOMEM;
	} else {
		/* Copy the BSSID to the association request */
		memcpy(&assoc_req->bssid, awrq->sa_data, ETH_ALEN);
		set_bit(ASSOC_FLAG_BSSID, &assoc_req->flags);
		lbs_postpone_association_work(priv);
	}

	mutex_unlock(&priv->lock);

	return ret;
}

/*
 * iwconfig settable callbacks
 */
static const iw_handler lbs_handler[] = {
	(iw_handler) NULL,	/* SIOCSIWCOMMIT */
	(iw_handler) lbs_get_name,	/* SIOCGIWNAME */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) lbs_set_freq,	/* SIOCSIWFREQ */
	(iw_handler) lbs_get_freq,	/* SIOCGIWFREQ */
	(iw_handler) lbs_set_mode,	/* SIOCSIWMODE */
	(iw_handler) lbs_get_mode,	/* SIOCGIWMODE */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
	(iw_handler) NULL,	/* SIOCSIWRANGE */
	(iw_handler) lbs_get_range,	/* SIOCGIWRANGE */
	(iw_handler) NULL,	/* SIOCSIWPRIV */
	(iw_handler) NULL,	/* SIOCGIWPRIV */
	(iw_handler) NULL,	/* SIOCSIWSTATS */
	(iw_handler) NULL,	/* SIOCGIWSTATS */
	iw_handler_set_spy,	/* SIOCSIWSPY */
	iw_handler_get_spy,	/* SIOCGIWSPY */
	iw_handler_set_thrspy,	/* SIOCSIWTHRSPY */
	iw_handler_get_thrspy,	/* SIOCGIWTHRSPY */
	(iw_handler) lbs_set_wap,	/* SIOCSIWAP */
	(iw_handler) lbs_get_wap,	/* SIOCGIWAP */
	(iw_handler) NULL,	/* SIOCSIWMLME */
	(iw_handler) NULL,	/* SIOCGIWAPLIST - deprecated */
	(iw_handler) lbs_set_scan,	/* SIOCSIWSCAN */
	(iw_handler) lbs_get_scan,	/* SIOCGIWSCAN */
	(iw_handler) lbs_set_essid,	/* SIOCSIWESSID */
	(iw_handler) lbs_get_essid,	/* SIOCGIWESSID */
	(iw_handler) lbs_set_nick,	/* SIOCSIWNICKN */
	(iw_handler) lbs_get_nick,	/* SIOCGIWNICKN */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) lbs_set_rate,	/* SIOCSIWRATE */
	(iw_handler) lbs_get_rate,	/* SIOCGIWRATE */
	(iw_handler) lbs_set_rts,	/* SIOCSIWRTS */
	(iw_handler) lbs_get_rts,	/* SIOCGIWRTS */
	(iw_handler) lbs_set_frag,	/* SIOCSIWFRAG */
	(iw_handler) lbs_get_frag,	/* SIOCGIWFRAG */
	(iw_handler) lbs_set_txpow,	/* SIOCSIWTXPOW */
	(iw_handler) lbs_get_txpow,	/* SIOCGIWTXPOW */
	(iw_handler) lbs_set_retry,	/* SIOCSIWRETRY */
	(iw_handler) lbs_get_retry,	/* SIOCGIWRETRY */
	(iw_handler) lbs_set_encode,	/* SIOCSIWENCODE */
	(iw_handler) lbs_get_encode,	/* SIOCGIWENCODE */
	(iw_handler) lbs_set_power,	/* SIOCSIWPOWER */
	(iw_handler) lbs_get_power,	/* SIOCGIWPOWER */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) lbs_set_genie,	/* SIOCSIWGENIE */
	(iw_handler) lbs_get_genie,	/* SIOCGIWGENIE */
	(iw_handler) lbs_set_auth,	/* SIOCSIWAUTH */
	(iw_handler) lbs_get_auth,	/* SIOCGIWAUTH */
	(iw_handler) lbs_set_encodeext,/* SIOCSIWENCODEEXT */
	(iw_handler) lbs_get_encodeext,/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,		/* SIOCSIWPMKSA */
};

static const iw_handler mesh_wlan_handler[] = {
	(iw_handler) NULL,	/* SIOCSIWCOMMIT */
	(iw_handler) lbs_get_name,	/* SIOCGIWNAME */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) lbs_mesh_set_freq,	/* SIOCSIWFREQ */
	(iw_handler) lbs_get_freq,	/* SIOCGIWFREQ */
	(iw_handler) NULL,		/* SIOCSIWMODE */
	(iw_handler) mesh_wlan_get_mode,	/* SIOCGIWMODE */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
	(iw_handler) NULL,	/* SIOCSIWRANGE */
	(iw_handler) lbs_get_range,	/* SIOCGIWRANGE */
	(iw_handler) NULL,	/* SIOCSIWPRIV */
	(iw_handler) NULL,	/* SIOCGIWPRIV */
	(iw_handler) NULL,	/* SIOCSIWSTATS */
	(iw_handler) NULL,	/* SIOCGIWSTATS */
	iw_handler_set_spy,	/* SIOCSIWSPY */
	iw_handler_get_spy,	/* SIOCGIWSPY */
	iw_handler_set_thrspy,	/* SIOCSIWTHRSPY */
	iw_handler_get_thrspy,	/* SIOCGIWTHRSPY */
	(iw_handler) NULL,	/* SIOCSIWAP */
	(iw_handler) NULL,	/* SIOCGIWAP */
	(iw_handler) NULL,	/* SIOCSIWMLME */
	(iw_handler) NULL,	/* SIOCGIWAPLIST - deprecated */
	(iw_handler) lbs_set_scan,	/* SIOCSIWSCAN */
	(iw_handler) lbs_get_scan,	/* SIOCGIWSCAN */
	(iw_handler) lbs_mesh_set_essid,/* SIOCSIWESSID */
	(iw_handler) lbs_mesh_get_essid,/* SIOCGIWESSID */
	(iw_handler) NULL,		/* SIOCSIWNICKN */
	(iw_handler) mesh_get_nick,	/* SIOCGIWNICKN */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) lbs_set_rate,	/* SIOCSIWRATE */
	(iw_handler) lbs_get_rate,	/* SIOCGIWRATE */
	(iw_handler) lbs_set_rts,	/* SIOCSIWRTS */
	(iw_handler) lbs_get_rts,	/* SIOCGIWRTS */
	(iw_handler) lbs_set_frag,	/* SIOCSIWFRAG */
	(iw_handler) lbs_get_frag,	/* SIOCGIWFRAG */
	(iw_handler) lbs_set_txpow,	/* SIOCSIWTXPOW */
	(iw_handler) lbs_get_txpow,	/* SIOCGIWTXPOW */
	(iw_handler) lbs_set_retry,	/* SIOCSIWRETRY */
	(iw_handler) lbs_get_retry,	/* SIOCGIWRETRY */
	(iw_handler) lbs_set_encode,	/* SIOCSIWENCODE */
	(iw_handler) lbs_get_encode,	/* SIOCGIWENCODE */
	(iw_handler) lbs_set_power,	/* SIOCSIWPOWER */
	(iw_handler) lbs_get_power,	/* SIOCGIWPOWER */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) lbs_set_genie,	/* SIOCSIWGENIE */
	(iw_handler) lbs_get_genie,	/* SIOCGIWGENIE */
	(iw_handler) lbs_set_auth,	/* SIOCSIWAUTH */
	(iw_handler) lbs_get_auth,	/* SIOCGIWAUTH */
	(iw_handler) lbs_set_encodeext,/* SIOCSIWENCODEEXT */
	(iw_handler) lbs_get_encodeext,/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,		/* SIOCSIWPMKSA */
};
struct iw_handler_def lbs_handler_def = {
	.num_standard	= ARRAY_SIZE(lbs_handler),
	.standard	= (iw_handler *) lbs_handler,
	.get_wireless_stats = lbs_get_wireless_stats,
};

struct iw_handler_def mesh_handler_def = {
	.num_standard	= ARRAY_SIZE(mesh_wlan_handler),
	.standard	= (iw_handler *) mesh_wlan_handler,
	.get_wireless_stats = lbs_get_wireless_stats,
};
