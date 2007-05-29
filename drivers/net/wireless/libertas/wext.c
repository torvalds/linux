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
#include "join.h"
#include "wext.h"
#include "assoc.h"


/**
 * the rates supported by the card
 */
static u8 libertas_wlan_data_rates[WLAN_SUPPORTED_RATES] =
    { 0x02, 0x04, 0x0B, 0x16, 0x00, 0x0C, 0x12,
      0x18, 0x24, 0x30, 0x48, 0x60, 0x6C, 0x00
};

/**
 *  @brief Convert mw value to dbm value
 *
 *  @param mw	   the value of mw
 *  @return 	   the value of dbm
 */
static int mw_to_dbm(int mw)
{
	if (mw < 2)
		return 0;
	else if (mw < 3)
		return 3;
	else if (mw < 4)
		return 5;
	else if (mw < 6)
		return 7;
	else if (mw < 7)
		return 8;
	else if (mw < 8)
		return 9;
	else if (mw < 10)
		return 10;
	else if (mw < 13)
		return 11;
	else if (mw < 16)
		return 12;
	else if (mw < 20)
		return 13;
	else if (mw < 25)
		return 14;
	else if (mw < 32)
		return 15;
	else if (mw < 40)
		return 16;
	else if (mw < 50)
		return 17;
	else if (mw < 63)
		return 18;
	else if (mw < 79)
		return 19;
	else if (mw < 100)
		return 20;
	else
		return 21;
}

/**
 *  @brief Find the channel frequency power info with specific channel
 *
 *  @param adapter 	A pointer to wlan_adapter structure
 *  @param band		it can be BAND_A, BAND_G or BAND_B
 *  @param channel      the channel for looking
 *  @return 	   	A pointer to struct chan_freq_power structure or NULL if not find.
 */
struct chan_freq_power *libertas_find_cfp_by_band_and_channel(wlan_adapter * adapter,
						 u8 band, u16 channel)
{
	struct chan_freq_power *cfp = NULL;
	struct region_channel *rc;
	int count = sizeof(adapter->region_channel) /
	    sizeof(adapter->region_channel[0]);
	int i, j;

	for (j = 0; !cfp && (j < count); j++) {
		rc = &adapter->region_channel[j];

		if (adapter->enable11d)
			rc = &adapter->universal_channel[j];
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
		lbs_deb_wext("libertas_find_cfp_by_band_and_channel: can't find "
		       "cfp by band %d / channel %d\n", band, channel);

	return cfp;
}

/**
 *  @brief Find the channel frequency power info with specific frequency
 *
 *  @param adapter 	A pointer to wlan_adapter structure
 *  @param band		it can be BAND_A, BAND_G or BAND_B
 *  @param freq	        the frequency for looking
 *  @return 	   	A pointer to struct chan_freq_power structure or NULL if not find.
 */
static struct chan_freq_power *find_cfp_by_band_and_freq(wlan_adapter * adapter,
						     u8 band, u32 freq)
{
	struct chan_freq_power *cfp = NULL;
	struct region_channel *rc;
	int count = sizeof(adapter->region_channel) /
	    sizeof(adapter->region_channel[0]);
	int i, j;

	for (j = 0; !cfp && (j < count); j++) {
		rc = &adapter->region_channel[j];

		if (adapter->enable11d)
			rc = &adapter->universal_channel[j];
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
 *  @brief Set Radio On/OFF
 *
 *  @param priv                 A pointer to wlan_private structure
 *  @option 			Radio Option
 *  @return 	   		0 --success, otherwise fail
 */
int wlan_radio_ioctl(wlan_private * priv, u8 option)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (adapter->radioon != option) {
		lbs_deb_wext("switching radio %s\n", option ? "on" : "off");
		adapter->radioon = option;

		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_radio_control,
					    cmd_act_set,
					    cmd_option_waitforrsp, 0, NULL);
	}

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

/**
 *  @brief Copy rates
 *
 *  @param dest                 A pointer to Dest Buf
 *  @param src		        A pointer to Src Buf
 *  @param len                  The len of Src Buf
 *  @return 	   	        Number of rates copyed
 */
static inline int copyrates(u8 * dest, int pos, u8 * src, int len)
{
	int i;

	for (i = 0; i < len && src[i]; i++, pos++) {
		if (pos >= sizeof(u8) * WLAN_SUPPORTED_RATES)
			break;
		dest[pos] = src[i];
	}

	return pos;
}

/**
 *  @brief Get active data rates
 *
 *  @param adapter              A pointer to wlan_adapter structure
 *  @param rate		        The buf to return the active rates
 *  @return 	   	        The number of rates
 */
static int get_active_data_rates(wlan_adapter * adapter,
				 u8* rates)
{
	int k = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (adapter->connect_status != libertas_connected) {
		if (adapter->mode == IW_MODE_INFRA) {
			lbs_deb_wext("infra\n");
			k = copyrates(rates, k, libertas_supported_rates,
				      sizeof(libertas_supported_rates));
		} else {
			lbs_deb_wext("Adhoc G\n");
			k = copyrates(rates, k, libertas_adhoc_rates_g,
				      sizeof(libertas_adhoc_rates_g));
		}
	} else {
		k = copyrates(rates, 0, adapter->curbssparams.datarates,
			      adapter->curbssparams.numofrates);
	}

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", k);
	return k;
}

static int wlan_get_name(struct net_device *dev, struct iw_request_info *info,
			 char *cwrq, char *extra)
{
	const char *cp;
	char comm[6] = { "COMM-" };
	char mrvl[6] = { "MRVL-" };
	int cnt;

	lbs_deb_enter(LBS_DEB_WEXT);

	strcpy(cwrq, mrvl);

	cp = strstr(libertas_driver_version, comm);
	if (cp == libertas_driver_version)	//skip leading "COMM-"
		cp = libertas_driver_version + strlen(comm);
	else
		cp = libertas_driver_version;

	cnt = strlen(mrvl);
	cwrq += cnt;
	while (cnt < 16 && (*cp != '-')) {
		*cwrq++ = toupper(*cp++);
		cnt++;
	}
	*cwrq = '\0';

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int wlan_get_freq(struct net_device *dev, struct iw_request_info *info,
			 struct iw_freq *fwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct chan_freq_power *cfp;

	lbs_deb_enter(LBS_DEB_WEXT);

	cfp = libertas_find_cfp_by_band_and_channel(adapter, 0,
					   adapter->curbssparams.channel);

	if (!cfp) {
		if (adapter->curbssparams.channel)
			lbs_deb_wext("invalid channel %d\n",
			       adapter->curbssparams.channel);
		return -EINVAL;
	}

	fwrq->m = (long)cfp->freq * 100000;
	fwrq->e = 1;

	lbs_deb_wext("freq %u\n", fwrq->m);
	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int wlan_get_wap(struct net_device *dev, struct iw_request_info *info,
			struct sockaddr *awrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (adapter->connect_status == libertas_connected) {
		memcpy(awrq->sa_data, adapter->curbssparams.bssid, ETH_ALEN);
	} else {
		memset(awrq->sa_data, 0, ETH_ALEN);
	}
	awrq->sa_family = ARPHRD_ETHER;

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int wlan_set_nick(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	/*
	 * Check the size of the string
	 */

	if (dwrq->length > 16) {
		return -E2BIG;
	}

	mutex_lock(&adapter->lock);
	memset(adapter->nodename, 0, sizeof(adapter->nodename));
	memcpy(adapter->nodename, extra, dwrq->length);
	mutex_unlock(&adapter->lock);

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int wlan_get_nick(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	/*
	 * Get the Nick Name saved
	 */

	mutex_lock(&adapter->lock);
	strncpy(extra, adapter->nodename, 16);
	mutex_unlock(&adapter->lock);

	extra[16] = '\0';

	/*
	 * If none, we may want to get the one that was set
	 */

	/*
	 * Push it out !
	 */
	dwrq->length = strlen(extra) + 1;

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int mesh_get_nick(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	/* Use nickname to indicate that mesh is on */

	if (adapter->connect_status == libertas_connected) {
		strncpy(extra, "Mesh", 12);
		extra[12] = '\0';
		dwrq->length = strlen(extra) + 1;
	}

	else {
		extra[0] = '\0';
		dwrq->length = 1 ;
	}

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}
static int wlan_set_rts(struct net_device *dev, struct iw_request_info *info,
			struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	u32 rthr = vwrq->value;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (vwrq->disabled) {
		adapter->rtsthsd = rthr = MRVDRV_RTS_MAX_VALUE;
	} else {
		if (rthr < MRVDRV_RTS_MIN_VALUE || rthr > MRVDRV_RTS_MAX_VALUE)
			return -EINVAL;
		adapter->rtsthsd = rthr;
	}

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_snmp_mib,
				    cmd_act_set, cmd_option_waitforrsp,
				    OID_802_11_RTS_THRESHOLD, &rthr);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_get_rts(struct net_device *dev, struct iw_request_info *info,
			struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	adapter->rtsthsd = 0;
	ret = libertas_prepare_and_send_command(priv, cmd_802_11_snmp_mib,
				    cmd_act_get, cmd_option_waitforrsp,
				    OID_802_11_RTS_THRESHOLD, NULL);
	if (ret)
		goto out;

	vwrq->value = adapter->rtsthsd;
	vwrq->disabled = ((vwrq->value < MRVDRV_RTS_MIN_VALUE)
			  || (vwrq->value > MRVDRV_RTS_MAX_VALUE));
	vwrq->fixed = 1;

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_set_frag(struct net_device *dev, struct iw_request_info *info,
			 struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	u32 fthr = vwrq->value;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (vwrq->disabled) {
		adapter->fragthsd = fthr = MRVDRV_FRAG_MAX_VALUE;
	} else {
		if (fthr < MRVDRV_FRAG_MIN_VALUE
		    || fthr > MRVDRV_FRAG_MAX_VALUE)
			return -EINVAL;
		adapter->fragthsd = fthr;
	}

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_snmp_mib,
				    cmd_act_set, cmd_option_waitforrsp,
				    OID_802_11_FRAGMENTATION_THRESHOLD, &fthr);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_get_frag(struct net_device *dev, struct iw_request_info *info,
			 struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	adapter->fragthsd = 0;
	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_snmp_mib,
				    cmd_act_get, cmd_option_waitforrsp,
				    OID_802_11_FRAGMENTATION_THRESHOLD, NULL);
	if (ret)
		goto out;

	vwrq->value = adapter->fragthsd;
	vwrq->disabled = ((vwrq->value < MRVDRV_FRAG_MIN_VALUE)
			  || (vwrq->value > MRVDRV_FRAG_MAX_VALUE));
	vwrq->fixed = 1;

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_get_mode(struct net_device *dev,
			 struct iw_request_info *info, u32 * uwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	*uwrq = adapter->mode;

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

static int wlan_get_txpow(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_rf_tx_power,
				    cmd_act_tx_power_opt_get,
				    cmd_option_waitforrsp, 0, NULL);

	if (ret)
		goto out;

	lbs_deb_wext("tx power level %d dbm\n", adapter->txpowerlevel);
	vwrq->value = adapter->txpowerlevel;
	vwrq->fixed = 1;
	if (adapter->radioon) {
		vwrq->disabled = 0;
		vwrq->flags = IW_TXPOW_DBM;
	} else {
		vwrq->disabled = 1;
	}

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_set_retry(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (vwrq->flags == IW_RETRY_LIMIT) {
		/* The MAC has a 4-bit Total_Tx_Count register
		   Total_Tx_Count = 1 + Tx_Retry_Count */
#define TX_RETRY_MIN 0
#define TX_RETRY_MAX 14
		if (vwrq->value < TX_RETRY_MIN || vwrq->value > TX_RETRY_MAX)
			return -EINVAL;

		/* Adding 1 to convert retry count to try count */
		adapter->txretrycount = vwrq->value + 1;

		ret = libertas_prepare_and_send_command(priv, cmd_802_11_snmp_mib,
					    cmd_act_set,
					    cmd_option_waitforrsp,
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

static int wlan_get_retry(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	adapter->txretrycount = 0;
	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_snmp_mib,
				    cmd_act_get, cmd_option_waitforrsp,
				    OID_802_11_TX_RETRYCOUNT, NULL);
	if (ret)
		goto out;

	vwrq->disabled = 0;
	if (!vwrq->flags) {
		vwrq->flags = IW_RETRY_LIMIT;
		/* Subtract 1 to convert try count to retry count */
		vwrq->value = adapter->txretrycount - 1;
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
static int wlan_get_range(struct net_device *dev, struct iw_request_info *info,
			  struct iw_point *dwrq, char *extra)
{
	int i, j;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct iw_range *range = (struct iw_range *)extra;
	struct chan_freq_power *cfp;
	u8 rates[WLAN_SUPPORTED_RATES];

	u8 flag = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->min_nwid = 0;
	range->max_nwid = 0;

	memset(rates, 0, sizeof(rates));
	range->num_bitrates = get_active_data_rates(adapter, rates);

	for (i = 0; i < min_t(__u8, range->num_bitrates, IW_MAX_BITRATES) && rates[i];
	     i++) {
		range->bitrate[i] = (rates[i] & 0x7f) * 500000;
	}
	range->num_bitrates = i;
	lbs_deb_wext("IW_MAX_BITRATES %d, num_bitrates %d\n", IW_MAX_BITRATES,
	       range->num_bitrates);

	range->num_frequency = 0;
	if (priv->adapter->enable11d &&
	    adapter->connect_status == libertas_connected) {
		u8 chan_no;
		u8 band;

		struct parsed_region_chan_11d *parsed_region_chan =
		    &adapter->parsed_region_chan;

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
			    (long)libertas_chan_2_freq(chan_no, band) * 100000;
			range->freq[range->num_frequency].e = 1;
			range->num_frequency++;
		}
		flag = 1;
	}
	if (!flag) {
		for (j = 0; (range->num_frequency < IW_MAX_FREQUENCIES)
		     && (j < sizeof(adapter->region_channel)
			 / sizeof(adapter->region_channel[0])); j++) {
			cfp = adapter->region_channel[j].CFP;
			for (i = 0; (range->num_frequency < IW_MAX_FREQUENCIES)
			     && adapter->region_channel[j].valid
			     && cfp
			     && (i < adapter->region_channel[j].nrcfp); i++) {
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

	range->min_pmp = 1000000;
	range->max_pmp = 120000000;
	range->min_pmt = 1000;
	range->max_pmt = 1000000;
	range->pmp_flags = IW_POWER_PERIOD;
	range->pmt_flags = IW_POWER_TIMEOUT;
	range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_ALL_R;

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

	/*
	 * Setup the supported power level ranges
	 */
	memset(range->txpower, 0, sizeof(range->txpower));
	range->txpower[0] = 5;
	range->txpower[1] = 7;
	range->txpower[2] = 9;
	range->txpower[3] = 11;
	range->txpower[4] = 13;
	range->txpower[5] = 15;
	range->txpower[6] = 17;
	range->txpower[7] = 19;

	range->num_txpower = 8;
	range->txpower_capa = IW_TXPOW_DBM;
	range->txpower_capa |= IW_TXPOW_RANGE;

	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
				IW_EVENT_CAPA_MASK(SIOCGIWAP) |
				IW_EVENT_CAPA_MASK(SIOCGIWSCAN));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;

	if (adapter->fwcapinfo & FW_CAPINFO_WPA) {
		range->enc_capa =   IW_ENC_CAPA_WPA
		                  | IW_ENC_CAPA_WPA2
		                  | IW_ENC_CAPA_CIPHER_TKIP
		                  | IW_ENC_CAPA_CIPHER_CCMP;
	}

out:
	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int wlan_set_power(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	/* PS is currently supported only in Infrastructure mode
	 * Remove this check if it is to be supported in IBSS mode also
	 */

	if (vwrq->disabled) {
		adapter->psmode = wlan802_11powermodecam;
		if (adapter->psstate != PS_STATE_FULL_POWER) {
			libertas_ps_wakeup(priv, cmd_option_waitforrsp);
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

	if (adapter->psmode != wlan802_11powermodecam) {
		return 0;
	}

	adapter->psmode = wlan802_11powermodemax_psp;

	if (adapter->connect_status == libertas_connected) {
		libertas_ps_sleep(priv, cmd_option_waitforrsp);
	}

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int wlan_get_power(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int mode;

	lbs_deb_enter(LBS_DEB_WEXT);

	mode = adapter->psmode;

	if ((vwrq->disabled = (mode == wlan802_11powermodecam))
	    || adapter->connect_status == libertas_disconnected)
	{
		goto out;
	}

	vwrq->value = 0;

out:
	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

/*
 * iwpriv settable callbacks
 */

static const iw_handler wlan_private_handler[] = {
	NULL,			/* SIOCIWFIRSTPRIV */
};

static const struct iw_priv_args wlan_private_args[] = {
	/*
	 * { cmd, set_args, get_args, name }
	 */
	/* Using iwpriv sub-command feature */
	{
	 WLAN_SETONEINT_GETNONE,	/* IOCTL: 24 */
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE,
	 ""},
	{
	 WLANSETREGION,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE,
	 "setregioncode"},
	{
	 WLAN_SUBCMD_MESH_SET_TTL,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE,
	 "mesh_set_ttl"},
	{
	 WLAN_SETNONE_GETONEINT,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 ""},
	{
	 WLANGETREGION,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 "getregioncode"},
	{
	 WLAN_SUBCMD_FWT_CLEANUP,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 "fwt_cleanup"},
	{
	 WLAN_SUBCMD_FWT_TIME,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 "fwt_time"},
	{
	 WLAN_SUBCMD_MESH_GET_TTL,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 "mesh_get_ttl"},
	{
	 WLAN_SETNONE_GETNONE,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_NONE,
	 ""},
	{
	 WLAN_SUBCMD_FWT_RESET,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_NONE,
	 "fwt_reset"},
	{
	 WLAN_SUBCMD_BT_RESET,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_NONE,
	 "bt_reset"},
	{
	 WLAN_SET128CHAR_GET128CHAR,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 ""},
	/* BT Management */
	{
	 WLAN_SUBCMD_BT_ADD,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "bt_add"},
	{
	 WLAN_SUBCMD_BT_DEL,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "bt_del"},
	{
	 WLAN_SUBCMD_BT_LIST,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "bt_list"},
	{
	 WLAN_SUBCMD_BT_SET_INVERT,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE,
	 "bt_set_invert"},
	{
	 WLAN_SUBCMD_BT_GET_INVERT,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 "bt_get_invert"},
	/* FWT Management */
	{
	 WLAN_SUBCMD_FWT_ADD,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "fwt_add"},
	{
	 WLAN_SUBCMD_FWT_DEL,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "fwt_del"},
	{
	 WLAN_SUBCMD_FWT_LOOKUP,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "fwt_lookup"},
	{
	 WLAN_SUBCMD_FWT_LIST_NEIGHBOR,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "fwt_list_neigh"},
	{
	 WLAN_SUBCMD_FWT_LIST,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "fwt_list"},
	{
	 WLAN_SUBCMD_FWT_LIST_ROUTE,
	 IW_PRIV_TYPE_CHAR | 128,
	 IW_PRIV_TYPE_CHAR | 128,
	 "fwt_list_route"},
	{
	 WLAN_SET_GET_SIXTEEN_INT,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 ""},
	{
	 WLAN_LED_GPIO_CTRL,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "ledgpio"},
};

static struct iw_statistics *wlan_get_wireless_stats(struct net_device *dev)
{
	enum {
		POOR = 30,
		FAIR = 60,
		GOOD = 80,
		VERY_GOOD = 90,
		EXCELLENT = 95,
		PERFECT = 100
	};
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	u32 rssi_qual;
	u32 tx_qual;
	u32 quality = 0;
	int stats_valid = 0;
	u8 rssi;
	u32 tx_retries;

	lbs_deb_enter(LBS_DEB_WEXT);

	priv->wstats.status = adapter->mode;

	/* If we're not associated, all quality values are meaningless */
	if (adapter->connect_status != libertas_connected)
		goto out;

	/* Quality by RSSI */
	priv->wstats.qual.level =
	    CAL_RSSI(adapter->SNR[TYPE_BEACON][TYPE_NOAVG],
	     adapter->NF[TYPE_BEACON][TYPE_NOAVG]);

	if (adapter->NF[TYPE_BEACON][TYPE_NOAVG] == 0) {
		priv->wstats.qual.noise = MRVDRV_NF_DEFAULT_SCAN_VALUE;
	} else {
		priv->wstats.qual.noise =
		    CAL_NF(adapter->NF[TYPE_BEACON][TYPE_NOAVG]);
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

	tx_retries = le16_to_cpu(adapter->logmsg.retry);

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

	priv->wstats.discard.code = le16_to_cpu(adapter->logmsg.wepundecryptable);
	priv->wstats.discard.fragment = le16_to_cpu(adapter->logmsg.rxfrag);
	priv->wstats.discard.retries = tx_retries;
	priv->wstats.discard.misc = le16_to_cpu(adapter->logmsg.ackfailure);

	/* Calculate quality */
	priv->wstats.qual.qual = max(quality, (u32)100);
	priv->wstats.qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
	stats_valid = 1;

	/* update stats asynchronously for future calls */
	libertas_prepare_and_send_command(priv, cmd_802_11_rssi, 0,
					0, 0, NULL);
	libertas_prepare_and_send_command(priv, cmd_802_11_get_log, 0,
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

static int wlan_set_freq(struct net_device *dev, struct iw_request_info *info,
		  struct iw_freq *fwrq, char *extra)
{
	int ret = -EINVAL;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct chan_freq_power *cfp;
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&adapter->lock);
	assoc_req = wlan_get_association_request(adapter);
	if (!assoc_req) {
		ret = -ENOMEM;
		goto out;
	}

	/* If setting by frequency, convert to a channel */
	if (fwrq->e == 1) {
		long f = fwrq->m / 100000;

		cfp = find_cfp_by_band_and_freq(adapter, 0, f);
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

	cfp = libertas_find_cfp_by_band_and_channel(adapter, 0, fwrq->m);
	if (!cfp) {
		goto out;
	}

	assoc_req->channel = fwrq->m;
	ret = 0;

out:
	if (ret == 0) {
		set_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags);
		wlan_postpone_association_work(priv);
	} else {
		wlan_cancel_association_work(priv);
	}
	mutex_unlock(&adapter->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

/**
 *  @brief use index to get the data rate
 *
 *  @param index                The index of data rate
 *  @return 	   		data rate or 0
 */
u32 libertas_index_to_data_rate(u8 index)
{
	if (index >= sizeof(libertas_wlan_data_rates))
		index = 0;

	return libertas_wlan_data_rates[index];
}

/**
 *  @brief use rate to get the index
 *
 *  @param rate                 data rate
 *  @return 	   		index or 0
 */
u8 libertas_data_rate_to_index(u32 rate)
{
	u8 *ptr;

	if (rate)
		if ((ptr = memchr(libertas_wlan_data_rates, (u8) rate,
				  sizeof(libertas_wlan_data_rates))))
			return (ptr - libertas_wlan_data_rates);

	return 0;
}

static int wlan_set_rate(struct net_device *dev, struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	u32 data_rate;
	u16 action;
	int ret = 0;
	u8 rates[WLAN_SUPPORTED_RATES];
	u8 *rate;

	lbs_deb_enter(LBS_DEB_WEXT);

	lbs_deb_wext("vwrq->value %d\n", vwrq->value);

	if (vwrq->value == -1) {
		action = cmd_act_set_tx_auto;	// Auto
		adapter->is_datarate_auto = 1;
		adapter->datarate = 0;
	} else {
		if (vwrq->value % 100000) {
			return -EINVAL;
		}

		data_rate = vwrq->value / 500000;

		memset(rates, 0, sizeof(rates));
		get_active_data_rates(adapter, rates);
		rate = rates;
		while (*rate) {
			lbs_deb_wext("rate=0x%X, wanted data_rate 0x%X\n", *rate,
			       data_rate);
			if ((*rate & 0x7f) == (data_rate & 0x7f))
				break;
			rate++;
		}
		if (!*rate) {
			lbs_pr_alert("fixed data rate 0x%X out "
			       "of range\n", data_rate);
			return -EINVAL;
		}

		adapter->datarate = data_rate;
		action = cmd_act_set_tx_fix_rate;
		adapter->is_datarate_auto = 0;
	}

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_data_rate,
				    action, cmd_option_waitforrsp, 0, NULL);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_get_rate(struct net_device *dev, struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (adapter->is_datarate_auto) {
		vwrq->fixed = 0;
	} else {
		vwrq->fixed = 1;
	}

	vwrq->value = adapter->datarate * 500000;

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int wlan_set_mode(struct net_device *dev,
		  struct iw_request_info *info, u32 * uwrq, char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (   (*uwrq != IW_MODE_ADHOC)
	    && (*uwrq != IW_MODE_INFRA)
	    && (*uwrq != IW_MODE_AUTO)) {
		lbs_deb_wext("Invalid mode: 0x%x\n", *uwrq);
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&adapter->lock);
	assoc_req = wlan_get_association_request(adapter);
	if (!assoc_req) {
		ret = -ENOMEM;
		wlan_cancel_association_work(priv);
	} else {
		assoc_req->mode = *uwrq;
		set_bit(ASSOC_FLAG_MODE, &assoc_req->flags);
		wlan_postpone_association_work(priv);
		lbs_deb_wext("Switching to mode: 0x%x\n", *uwrq);
	}
	mutex_unlock(&adapter->lock);

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
static int wlan_get_encode(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *dwrq, u8 * extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	lbs_deb_enter(LBS_DEB_WEXT);

	lbs_deb_wext("flags 0x%x, index %d, length %d, wep_tx_keyidx %d\n",
	       dwrq->flags, index, dwrq->length, adapter->wep_tx_keyidx);

	dwrq->flags = 0;

	/* Authentication method */
	switch (adapter->secinfo.auth_mode) {
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

	if (   adapter->secinfo.wep_enabled
	    || adapter->secinfo.WPAenabled
	    || adapter->secinfo.WPA2enabled) {
		dwrq->flags &= ~IW_ENCODE_DISABLED;
	} else {
		dwrq->flags |= IW_ENCODE_DISABLED;
	}

	memset(extra, 0, 16);

	mutex_lock(&adapter->lock);

	/* Default to returning current transmit key */
	if (index < 0)
		index = adapter->wep_tx_keyidx;

	if ((adapter->wep_keys[index].len) && adapter->secinfo.wep_enabled) {
		memcpy(extra, adapter->wep_keys[index].key,
		       adapter->wep_keys[index].len);
		dwrq->length = adapter->wep_keys[index].len;

		dwrq->flags |= (index + 1);
		/* Return WEP enabled */
		dwrq->flags &= ~IW_ENCODE_DISABLED;
	} else if ((adapter->secinfo.WPAenabled)
		   || (adapter->secinfo.WPA2enabled)) {
		/* return WPA enabled */
		dwrq->flags &= ~IW_ENCODE_DISABLED;
	} else {
		dwrq->flags |= IW_ENCODE_DISABLED;
	}

	mutex_unlock(&adapter->lock);

	dwrq->flags |= IW_ENCODE_NOKEY;

	lbs_deb_wext("key: " MAC_FMT ", keylen %d\n",
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
static int wlan_set_wep_key(struct assoc_request *assoc_req,
			    const char *key_material,
			    u16 key_length,
			    u16 index,
			    int set_tx_key)
{
	int ret = 0;
	struct WLAN_802_11_KEY *pkey;

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
		memset(pkey, 0, sizeof(struct WLAN_802_11_KEY));
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

	memset(&assoc_req->wpa_mcast_key, 0, sizeof (struct WLAN_802_11_KEY));
	assoc_req->wpa_mcast_key.flags = KEY_INFO_WPA_MCAST;
	set_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags);

	memset(&assoc_req->wpa_unicast_key, 0, sizeof (struct WLAN_802_11_KEY));
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
static int wlan_set_encode(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_point *dwrq, char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct assoc_request * assoc_req;
	u16 is_default = 0, index = 0, set_tx_key = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&adapter->lock);
	assoc_req = wlan_get_association_request(adapter);
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

	ret = wlan_set_wep_key(assoc_req, extra, dwrq->length, index, set_tx_key);
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
		wlan_postpone_association_work(priv);
	} else {
		wlan_cancel_association_work(priv);
	}
	mutex_unlock(&adapter->lock);

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
static int wlan_get_encodeext(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq,
			      char *extra)
{
	int ret = -EINVAL;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
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
		index = adapter->wep_tx_keyidx;
	}

	if (!ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY &&
	    ext->alg != IW_ENCODE_ALG_WEP) {
		if (index != 0 || adapter->mode != IW_MODE_INFRA)
			goto out;
	}

	dwrq->flags = index + 1;
	memset(ext, 0, sizeof(*ext));

	if (   !adapter->secinfo.wep_enabled
	    && !adapter->secinfo.WPAenabled
	    && !adapter->secinfo.WPA2enabled) {
		ext->alg = IW_ENCODE_ALG_NONE;
		ext->key_len = 0;
		dwrq->flags |= IW_ENCODE_DISABLED;
	} else {
		u8 *key = NULL;

		if (   adapter->secinfo.wep_enabled
		    && !adapter->secinfo.WPAenabled
		    && !adapter->secinfo.WPA2enabled) {
			/* WEP */
			ext->alg = IW_ENCODE_ALG_WEP;
			ext->key_len = adapter->wep_keys[index].len;
			key = &adapter->wep_keys[index].key[0];
		} else if (   !adapter->secinfo.wep_enabled
		           && (adapter->secinfo.WPAenabled ||
		               adapter->secinfo.WPA2enabled)) {
			/* WPA */
			struct WLAN_802_11_KEY * pkey = NULL;

			if (   adapter->wpa_mcast_key.len
			    && (adapter->wpa_mcast_key.flags & KEY_INFO_WPA_ENABLED))
				pkey = &adapter->wpa_mcast_key;
			else if (   adapter->wpa_unicast_key.len
			         && (adapter->wpa_unicast_key.flags & KEY_INFO_WPA_ENABLED))
				pkey = &adapter->wpa_unicast_key;

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
static int wlan_set_encodeext(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq,
			      char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int alg = ext->alg;
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&adapter->lock);
	assoc_req = wlan_get_association_request(adapter);
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
		ret = wlan_set_wep_key (assoc_req, ext->key, ext->key_len, index,
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
		struct WLAN_802_11_KEY * pkey;

		/* validate key length */
		if (((alg == IW_ENCODE_ALG_TKIP)
			&& (ext->key_len != KEY_LEN_WPA_TKIP))
		    || ((alg == IW_ENCODE_ALG_CCMP)
		        && (ext->key_len != KEY_LEN_WPA_AES))) {
				lbs_deb_wext("invalid size %d for key of alg"
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

		memset(pkey, 0, sizeof (struct WLAN_802_11_KEY));
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
		} else {
			ret = -EINVAL;
			goto out;
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
		wlan_postpone_association_work(priv);
	} else {
		wlan_cancel_association_work(priv);
	}
	mutex_unlock(&adapter->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}


static int wlan_set_genie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&adapter->lock);
	assoc_req = wlan_get_association_request(adapter);
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
		memset(&assoc_req->wpa_ie[0], 0, sizeof(adapter->wpa_ie));
		assoc_req->wpa_ie_len = 0;
	}

out:
	if (ret == 0) {
		set_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags);
		wlan_postpone_association_work(priv);
	} else {
		wlan_cancel_association_work(priv);
	}
	mutex_unlock(&adapter->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_get_genie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (adapter->wpa_ie_len == 0) {
		dwrq->length = 0;
		goto out;
	}

	if (dwrq->length < adapter->wpa_ie_len) {
		ret = -E2BIG;
		goto out;
	}

	dwrq->length = adapter->wpa_ie_len;
	memcpy(extra, &adapter->wpa_ie[0], adapter->wpa_ie_len);

out:
	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}


static int wlan_set_auth(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *dwrq,
			 char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct assoc_request * assoc_req;
	int ret = 0;
	int updated = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	mutex_lock(&adapter->lock);
	assoc_req = wlan_get_association_request(adapter);
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
		wlan_postpone_association_work(priv);
	} else if (ret != -EOPNOTSUPP) {
		wlan_cancel_association_work(priv);
	}
	mutex_unlock(&adapter->lock);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_get_auth(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *dwrq,
			 char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	switch (dwrq->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		dwrq->value = 0;
		if (adapter->secinfo.WPAenabled)
			dwrq->value |= IW_AUTH_WPA_VERSION_WPA;
		if (adapter->secinfo.WPA2enabled)
			dwrq->value |= IW_AUTH_WPA_VERSION_WPA2;
		if (!dwrq->value)
			dwrq->value |= IW_AUTH_WPA_VERSION_DISABLED;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		dwrq->value = adapter->secinfo.auth_mode;
		break;

	case IW_AUTH_WPA_ENABLED:
		if (adapter->secinfo.WPAenabled && adapter->secinfo.WPA2enabled)
			dwrq->value = 1;
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}


static int wlan_set_txpow(struct net_device *dev, struct iw_request_info *info,
		   struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	u16 dbm;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (vwrq->disabled) {
		wlan_radio_ioctl(priv, RADIO_OFF);
		return 0;
	}

	adapter->preamble = cmd_type_auto_preamble;

	wlan_radio_ioctl(priv, RADIO_ON);

	if ((vwrq->flags & IW_TXPOW_TYPE) == IW_TXPOW_MWATT) {
		dbm = (u16) mw_to_dbm(vwrq->value);
	} else
		dbm = (u16) vwrq->value;

	/* auto tx power control */

	if (vwrq->fixed == 0)
		dbm = 0xffff;

	lbs_deb_wext("txpower set %d dbm\n", dbm);

	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_rf_tx_power,
				    cmd_act_tx_power_opt_set_low,
				    cmd_option_waitforrsp, 0, (void *)&dbm);

	lbs_deb_leave_args(LBS_DEB_WEXT, "ret %d", ret);
	return ret;
}

static int wlan_get_essid(struct net_device *dev, struct iw_request_info *info,
		   struct iw_point *dwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_WEXT);

	/*
	 * Note : if dwrq->flags != 0, we should get the relevant SSID from
	 * the SSID list...
	 */

	/*
	 * Get the current SSID
	 */
	if (adapter->connect_status == libertas_connected) {
		memcpy(extra, adapter->curbssparams.ssid,
		       adapter->curbssparams.ssid_len);
		extra[adapter->curbssparams.ssid_len] = '\0';
	} else {
		memset(extra, 0, 32);
		extra[adapter->curbssparams.ssid_len] = '\0';
	}
	/*
	 * If none, we may want to get the one that was set
	 */

	/* To make the driver backward compatible with WPA supplicant v0.2.4 */
	if (dwrq->length == 32)	/* check with WPA supplicant buffer size */
		dwrq->length = min_t(size_t, adapter->curbssparams.ssid_len,
				   IW_ESSID_MAX_SIZE);
	else
		dwrq->length = adapter->curbssparams.ssid_len + 1;

	dwrq->flags = 1;	/* active */

	lbs_deb_leave(LBS_DEB_WEXT);
	return 0;
}

static int wlan_set_essid(struct net_device *dev, struct iw_request_info *info,
		   struct iw_point *dwrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	u8 ssid[IW_ESSID_MAX_SIZE];
	u8 ssid_len = 0;
	struct assoc_request * assoc_req;
	int in_ssid_len = dwrq->length;

	lbs_deb_enter(LBS_DEB_WEXT);

	/*
	 * WE-20 and earlier NULL pad the end of the SSID and increment
	 * SSID length so it can be used like a string.  WE-21 and later don't,
	 * but some userspace tools aren't able to cope with the change.
	 */
	if ((in_ssid_len > 0) && (extra[in_ssid_len - 1] == '\0'))
		in_ssid_len--;

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
	mutex_lock(&adapter->lock);
	if (ret == 0) {
		/* Get or create the current association request */
		assoc_req = wlan_get_association_request(adapter);
		if (!assoc_req) {
			ret = -ENOMEM;
		} else {
			/* Copy the SSID to the association request */
			memcpy(&assoc_req->ssid, &ssid, IW_ESSID_MAX_SIZE);
			assoc_req->ssid_len = ssid_len;
			set_bit(ASSOC_FLAG_SSID, &assoc_req->flags);
			wlan_postpone_association_work(priv);
		}
	}

	/* Cancel the association request if there was an error */
	if (ret != 0) {
		wlan_cancel_association_work(priv);
	}

	mutex_unlock(&adapter->lock);

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
static int wlan_set_wap(struct net_device *dev, struct iw_request_info *info,
		 struct sockaddr *awrq, char *extra)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct assoc_request * assoc_req;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_WEXT);

	if (awrq->sa_family != ARPHRD_ETHER)
		return -EINVAL;

	lbs_deb_wext("ASSOC: WAP: sa_data " MAC_FMT "\n", MAC_ARG(awrq->sa_data));

	mutex_lock(&adapter->lock);

	/* Get or create the current association request */
	assoc_req = wlan_get_association_request(adapter);
	if (!assoc_req) {
		wlan_cancel_association_work(priv);
		ret = -ENOMEM;
	} else {
		/* Copy the BSSID to the association request */
		memcpy(&assoc_req->bssid, awrq->sa_data, ETH_ALEN);
		set_bit(ASSOC_FLAG_BSSID, &assoc_req->flags);
		wlan_postpone_association_work(priv);
	}

	mutex_unlock(&adapter->lock);

	return ret;
}

void libertas_get_fwversion(wlan_adapter * adapter, char *fwversion, int maxlen)
{
	char fwver[32];

	mutex_lock(&adapter->lock);

	if (adapter->fwreleasenumber[3] == 0)
		sprintf(fwver, "%u.%u.%u",
			adapter->fwreleasenumber[2],
			adapter->fwreleasenumber[1],
			adapter->fwreleasenumber[0]);
	else
		sprintf(fwver, "%u.%u.%u.p%u",
			adapter->fwreleasenumber[2],
			adapter->fwreleasenumber[1],
			adapter->fwreleasenumber[0],
			adapter->fwreleasenumber[3]);

	mutex_unlock(&adapter->lock);
	snprintf(fwversion, maxlen, fwver);
}


/*
 * iwconfig settable callbacks
 */
static const iw_handler wlan_handler[] = {
	(iw_handler) NULL,	/* SIOCSIWCOMMIT */
	(iw_handler) wlan_get_name,	/* SIOCGIWNAME */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) wlan_set_freq,	/* SIOCSIWFREQ */
	(iw_handler) wlan_get_freq,	/* SIOCGIWFREQ */
	(iw_handler) wlan_set_mode,	/* SIOCSIWMODE */
	(iw_handler) wlan_get_mode,	/* SIOCGIWMODE */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
	(iw_handler) NULL,	/* SIOCSIWRANGE */
	(iw_handler) wlan_get_range,	/* SIOCGIWRANGE */
	(iw_handler) NULL,	/* SIOCSIWPRIV */
	(iw_handler) NULL,	/* SIOCGIWPRIV */
	(iw_handler) NULL,	/* SIOCSIWSTATS */
	(iw_handler) NULL,	/* SIOCGIWSTATS */
	iw_handler_set_spy,	/* SIOCSIWSPY */
	iw_handler_get_spy,	/* SIOCGIWSPY */
	iw_handler_set_thrspy,	/* SIOCSIWTHRSPY */
	iw_handler_get_thrspy,	/* SIOCGIWTHRSPY */
	(iw_handler) wlan_set_wap,	/* SIOCSIWAP */
	(iw_handler) wlan_get_wap,	/* SIOCGIWAP */
	(iw_handler) NULL,	/* SIOCSIWMLME */
	(iw_handler) NULL,	/* SIOCGIWAPLIST - deprecated */
	(iw_handler) libertas_set_scan,	/* SIOCSIWSCAN */
	(iw_handler) libertas_get_scan,	/* SIOCGIWSCAN */
	(iw_handler) wlan_set_essid,	/* SIOCSIWESSID */
	(iw_handler) wlan_get_essid,	/* SIOCGIWESSID */
	(iw_handler) wlan_set_nick,	/* SIOCSIWNICKN */
	(iw_handler) wlan_get_nick,	/* SIOCGIWNICKN */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) wlan_set_rate,	/* SIOCSIWRATE */
	(iw_handler) wlan_get_rate,	/* SIOCGIWRATE */
	(iw_handler) wlan_set_rts,	/* SIOCSIWRTS */
	(iw_handler) wlan_get_rts,	/* SIOCGIWRTS */
	(iw_handler) wlan_set_frag,	/* SIOCSIWFRAG */
	(iw_handler) wlan_get_frag,	/* SIOCGIWFRAG */
	(iw_handler) wlan_set_txpow,	/* SIOCSIWTXPOW */
	(iw_handler) wlan_get_txpow,	/* SIOCGIWTXPOW */
	(iw_handler) wlan_set_retry,	/* SIOCSIWRETRY */
	(iw_handler) wlan_get_retry,	/* SIOCGIWRETRY */
	(iw_handler) wlan_set_encode,	/* SIOCSIWENCODE */
	(iw_handler) wlan_get_encode,	/* SIOCGIWENCODE */
	(iw_handler) wlan_set_power,	/* SIOCSIWPOWER */
	(iw_handler) wlan_get_power,	/* SIOCGIWPOWER */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) wlan_set_genie,	/* SIOCSIWGENIE */
	(iw_handler) wlan_get_genie,	/* SIOCGIWGENIE */
	(iw_handler) wlan_set_auth,	/* SIOCSIWAUTH */
	(iw_handler) wlan_get_auth,	/* SIOCGIWAUTH */
	(iw_handler) wlan_set_encodeext,/* SIOCSIWENCODEEXT */
	(iw_handler) wlan_get_encodeext,/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,		/* SIOCSIWPMKSA */
};

static const iw_handler mesh_wlan_handler[] = {
	(iw_handler) NULL,	/* SIOCSIWCOMMIT */
	(iw_handler) wlan_get_name,	/* SIOCGIWNAME */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) wlan_set_freq,	/* SIOCSIWFREQ */
	(iw_handler) wlan_get_freq,	/* SIOCGIWFREQ */
	(iw_handler) NULL,		/* SIOCSIWMODE */
	(iw_handler) mesh_wlan_get_mode,	/* SIOCGIWMODE */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
	(iw_handler) NULL,	/* SIOCSIWRANGE */
	(iw_handler) wlan_get_range,	/* SIOCGIWRANGE */
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
	(iw_handler) libertas_set_scan,	/* SIOCSIWSCAN */
	(iw_handler) libertas_get_scan,	/* SIOCGIWSCAN */
	(iw_handler) NULL,		/* SIOCSIWESSID */
	(iw_handler) NULL,		/* SIOCGIWESSID */
	(iw_handler) NULL,		/* SIOCSIWNICKN */
	(iw_handler) mesh_get_nick,	/* SIOCGIWNICKN */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) wlan_set_rate,	/* SIOCSIWRATE */
	(iw_handler) wlan_get_rate,	/* SIOCGIWRATE */
	(iw_handler) wlan_set_rts,	/* SIOCSIWRTS */
	(iw_handler) wlan_get_rts,	/* SIOCGIWRTS */
	(iw_handler) wlan_set_frag,	/* SIOCSIWFRAG */
	(iw_handler) wlan_get_frag,	/* SIOCGIWFRAG */
	(iw_handler) wlan_set_txpow,	/* SIOCSIWTXPOW */
	(iw_handler) wlan_get_txpow,	/* SIOCGIWTXPOW */
	(iw_handler) wlan_set_retry,	/* SIOCSIWRETRY */
	(iw_handler) wlan_get_retry,	/* SIOCGIWRETRY */
	(iw_handler) wlan_set_encode,	/* SIOCSIWENCODE */
	(iw_handler) wlan_get_encode,	/* SIOCGIWENCODE */
	(iw_handler) wlan_set_power,	/* SIOCSIWPOWER */
	(iw_handler) wlan_get_power,	/* SIOCGIWPOWER */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) wlan_set_genie,	/* SIOCSIWGENIE */
	(iw_handler) wlan_get_genie,	/* SIOCGIWGENIE */
	(iw_handler) wlan_set_auth,	/* SIOCSIWAUTH */
	(iw_handler) wlan_get_auth,	/* SIOCGIWAUTH */
	(iw_handler) wlan_set_encodeext,/* SIOCSIWENCODEEXT */
	(iw_handler) wlan_get_encodeext,/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,		/* SIOCSIWPMKSA */
};
struct iw_handler_def libertas_handler_def = {
	.num_standard	= sizeof(wlan_handler) / sizeof(iw_handler),
	.num_private	= sizeof(wlan_private_handler) / sizeof(iw_handler),
	.num_private_args = sizeof(wlan_private_args) /
		sizeof(struct iw_priv_args),
	.standard	= (iw_handler *) wlan_handler,
	.private	= (iw_handler *) wlan_private_handler,
	.private_args	= (struct iw_priv_args *)wlan_private_args,
	.get_wireless_stats = wlan_get_wireless_stats,
};

struct iw_handler_def mesh_handler_def = {
	.num_standard	= sizeof(mesh_wlan_handler) / sizeof(iw_handler),
	.num_private	= sizeof(wlan_private_handler) / sizeof(iw_handler),
	.num_private_args = sizeof(wlan_private_args) /
		sizeof(struct iw_priv_args),
	.standard	= (iw_handler *) mesh_wlan_handler,
	.private	= (iw_handler *) wlan_private_handler,
	.private_args	= (struct iw_priv_args *)wlan_private_args,
	.get_wireless_stats = wlan_get_wireless_stats,
};
