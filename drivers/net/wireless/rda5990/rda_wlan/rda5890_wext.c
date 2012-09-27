/**
  * This file contains ioctl functions
  */
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/bitops.h>

#include <net/iw_handler.h>
#include <linux/etherdevice.h>

#include "rda5890_defs.h"
#include "rda5890_dev.h"
#include "rda5890_ioctl.h"
#include "rda5890_wid.h"
#include "rda5890_wext.h"

#define WLAN_AUTH_OPEN 0
#define WLAN_AUTH_SHARED_KEY 1
#define WLAN_AUTH_FT 2
#define WLAN_AUTH_LEAP 128

#define WLAN_AUTH_CHALLENGE_LEN 128

#define WLAN_CAPABILITY_ESS		(1<<0)
#define WLAN_CAPABILITY_IBSS		(1<<1)
#define WLAN_CAPABILITY_CF_POLLABLE	(1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST	(1<<3)
#define WLAN_CAPABILITY_PRIVACY		(1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE	(1<<5)
#define WLAN_CAPABILITY_PBCC		(1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY	(1<<7)
#define IW_AUTH_ALG_WAPI      0x08
#define IW_ENCODE_ALG_WAPI    0x80

static int rda5890_get_name(struct net_device *dev, struct iw_request_info *info,
			 char *cwrq, char *extra)
{

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	/* We could add support for 802.11n here as needed. Jean II */
	snprintf(cwrq, IFNAMSIZ, "IEEE 802.11b/g");

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_freq(struct net_device *dev, struct iw_request_info *info,
			 struct iw_freq *fwrq, char *extra)
{
	//struct rda5890_private *priv =  netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	fwrq->m = (long)2437 * 100000;
	fwrq->e = 1;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_wap(struct net_device *dev, struct iw_request_info *info,
			struct sockaddr *awrq, char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *) netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	if (priv->connect_status == MAC_CONNECTED) {
		memcpy(awrq->sa_data, priv->curbssparams.bssid, ETH_ALEN);
	} else {
		memset(awrq->sa_data, 0, ETH_ALEN);
	}
	awrq->sa_family = ARPHRD_ETHER;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_set_nick(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra)
{
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_nick(struct net_device *dev, struct iw_request_info *info,
			 struct iw_point *dwrq, char *extra)
{
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_set_rts(struct net_device *dev, struct iw_request_info *info,
			struct iw_param *vwrq, char *extra)
{
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_rts(struct net_device *dev, struct iw_request_info *info,
			struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = netdev_priv(dev);
	int ret = 0;
	u16 val = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	val = 600;

	vwrq->value = val;
	vwrq->disabled = val > RDA5890_RTS_MAX_VALUE; /* min rts value is 0 */
	vwrq->fixed = 1;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return ret;
}

static int rda5890_set_frag(struct net_device *dev, struct iw_request_info *info,
			 struct iw_param *vwrq, char *extra)
{
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_frag(struct net_device *dev, struct iw_request_info *info,
			 struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = netdev_priv(dev);
	int ret = 0;
	u16 val = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	val = 1460;

	vwrq->value = val;
	vwrq->disabled = ((val < RDA5890_FRAG_MIN_VALUE)
			  || (val > RDA5890_FRAG_MAX_VALUE));
	vwrq->fixed = 1;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return ret;
}

static int rda5890_get_mode(struct net_device *dev,
			 struct iw_request_info *info, u32 * uwrq, char *extra)
{
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	*uwrq = IW_MODE_INFRA;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_txpow(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	vwrq->value = 20;  // in dbm
	vwrq->fixed = 1;
	vwrq->disabled = 0;
	vwrq->flags = IW_TXPOW_DBM;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_set_retry(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_retry(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = netdev_priv(dev);
	int ret = 0;
	u16 val = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	vwrq->disabled = 0;

	if (vwrq->flags & IW_RETRY_LONG) {
		val = 7;

		/* Subtract 1 to convert try count to retry count */
		vwrq->value = val - 1;
		vwrq->flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
	} else {
		val = 6;

		/* Subtract 1 to convert try count to retry count */
		vwrq->value = val - 1;
		vwrq->flags = IW_RETRY_LIMIT | IW_RETRY_SHORT;
	}

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return ret;
}

/** 
 * 802.11b/g supported bitrates (in 500Kb/s units) 
 */
u8 rda5890_bg_rates[MAX_RATES] =
	{ 0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 
	0x24, 0x30, 0x48, 0x60, 0x6c,0x00, 0x00 }; 

u16 rda5890_nr_chan = 11;

/**
 *  @brief Get Range Info
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info			A pointer to iw_request_info structure
 *  @param vwrq 		A pointer to iw_param structure
 *  @param extra		A pointer to extra data buf
 *  @return 	   		0 --success, otherwise fail
 */
static int rda5890_get_range(struct net_device *dev, struct iw_request_info *info,
			  struct iw_point *dwrq, char *extra)
{
	//struct rda5890_private *priv = netdev_priv(dev);
	struct iw_range *range = (struct iw_range *)extra;
	int i;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->min_nwid = 0;
	range->max_nwid = 0;

	range->num_bitrates = sizeof(rda5890_bg_rates);
	for (i = 0; i < range->num_bitrates; i++)
		range->bitrate[i] = rda5890_bg_rates[i] * 500000;
	range->num_bitrates = i;

	range->num_frequency = 0;

	range->scan_capa = IW_SCAN_CAPA_ESSID;

	for (i = 0; (range->num_frequency < IW_MAX_FREQUENCIES)
		     && (i < rda5890_nr_chan); i++) {
			range->freq[range->num_frequency].i = (long)(i + 1);
			range->freq[range->num_frequency].m =
			    (long)((2412 + 5 * i) * 100000);
			range->freq[range->num_frequency].e = 1;
			range->num_frequency++;
	}

	range->num_channels = range->num_frequency;

	/*
	 * Set an indication of the max TCP throughput in bit/s that we can
	 * expect using this interface
	 */
	range->throughput = 5000 * 1000;

	range->min_rts = RDA5890_RTS_MIN_VALUE;
	range->max_rts = RDA5890_RTS_MAX_VALUE;
	range->min_frag = RDA5890_FRAG_MIN_VALUE;
	range->max_frag = RDA5890_FRAG_MAX_VALUE;

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

	range->min_retry = 0;
	range->max_retry = 14;

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
	range->txpower[0] = 0;
	range->txpower[1] = 20;
	range->num_txpower = 2;

	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
				IW_EVENT_CAPA_MASK(SIOCGIWAP) |
				IW_EVENT_CAPA_MASK(SIOCGIWSCAN));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;

	range->enc_capa =   IW_ENC_CAPA_WPA
		                  | IW_ENC_CAPA_WPA2
		                  | IW_ENC_CAPA_CIPHER_TKIP
		                  | IW_ENC_CAPA_CIPHER_CCMP;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_set_power(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_power(struct net_device *dev, struct iw_request_info *info,
			  struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	vwrq->value = 0;
	vwrq->flags = 0;
	vwrq->disabled = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_update_bss_stats(struct rda5890_private *priv)
{
	int ret = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);
	
	memcpy(priv->curbssparams.ssid, 
		priv->assoc_ssid, sizeof(priv->curbssparams.ssid));

    if(priv->scan_running == 1)
        return ret;

	ret = rda5890_get_bssid(priv, priv->curbssparams.bssid);
	if (ret) {
		RDA5890_ERRP("rda5890_get_bssid, ret = %d\n", ret);
		goto out;
	}

#if 0
	ret = rda5890_get_channel(priv, &priv->curbssparams.channel);
	if (ret) {
		RDA5890_ERRP("rda5890_get_channel, ret = %d\n", ret);
		goto out;
	}
#endif

	ret = rda5890_get_rssi(priv, &priv->curbssparams.rssi);
	if (ret) {
		RDA5890_ERRP("rda5890_get_rssi, ret = %d\n", ret);
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<< ch = %d  rssi = %d\n", __func__, priv->curbssparams.channel, priv->curbssparams.rssi);

out:
	return ret;
}

static struct iw_statistics *rda5890_get_wireless_stats(struct net_device *dev)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int stats_valid = 0;
	u8 snr;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	if (priv->connect_status != MAC_CONNECTED)
		goto out;

	rda5890_update_bss_stats(priv);

	priv->wstats.miss.beacon = 0;
	priv->wstats.discard.retries = 0;
	priv->wstats.qual.level = priv->curbssparams.rssi > 127? priv->curbssparams.rssi - 271
        :priv->curbssparams.rssi - 15;
    
	snr = priv->wstats.qual.level - RDA5890_NF_DEFAULT_SCAN_VALUE;
	priv->wstats.qual.qual =
		(100 * RSSI_DIFF * RSSI_DIFF - (PERFECT_RSSI - snr) *
		 (15 * (RSSI_DIFF) + 62 * (PERFECT_RSSI - snr))) /
		(RSSI_DIFF * RSSI_DIFF);
	if (priv->wstats.qual.qual > 100)
		priv->wstats.qual.qual = 100;
	priv->wstats.qual.noise = RDA5890_NF_DEFAULT_SCAN_VALUE;
	priv->wstats.qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;

	stats_valid = 1;

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

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return &priv->wstats;
}

static int rda5890_set_freq(struct net_device *dev, struct iw_request_info *info,
		  struct iw_freq *fwrq, char *extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);


	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_set_rate(struct net_device *dev, struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);


	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_get_rate(struct net_device *dev, struct iw_request_info *info,
		  struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	vwrq->fixed = 0;
	vwrq->value = 108*500000;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_set_mode(struct net_device *dev,
		  struct iw_request_info *info, u32 * uwrq, char *extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
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
static int rda5890_get_encode(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *dwrq, u8 * extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
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
static int rda5890_set_encode(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_point *dwrq, char *extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);
    
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
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
static int copy_wep_key(struct rda5890_private *priv,
			    const char *key_material,
			    u16 key_length,
			    u16 index,
			    int set_tx_key)
{
	int ret = 0;
	struct enc_key *pkey;

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

	if (key_length == KEY_LEN_WEP_40) {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"WEP40 : %02x%02x%02x%02x%02x\n",
			key_material[0], key_material[1], key_material[2],
			key_material[3], key_material[4]);
	}
	else if (key_length == KEY_LEN_WEP_104) {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"WEP104 : %02x%02x%02x%02x%02x"
			" %02x%02x%02x%02x%02x"
			" %02x%02x%02x\n",
			key_material[0], key_material[1], key_material[2],
			key_material[3], key_material[4], key_material[5],
			key_material[6], key_material[7], key_material[8],
			key_material[9], key_material[10], key_material[11],
			key_material[12]);
	}
	else {
		RDA5890_ERRP("Error in WEP Key length %d\n", key_length);
	}

	pkey = &priv->wep_keys[index];

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
			RDA5890_ERRP("key not set, so cannot enable it\n");
			ret = -EINVAL;
			goto out;
		}
		priv->wep_tx_keyidx = index;
	}

	priv->secinfo.wep_enabled = 1;

out:
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

static void disable_wep(struct rda5890_private *priv)
{
	int i;

	/* Set Open System auth mode */
	priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;

	/* Clear WEP keys and mark WEP as disabled */
	priv->secinfo.wep_enabled = 0;
	for (i = 0; i < 4; i++)
		priv->wep_keys[i].len = 0;

	set_bit(ASSOC_FLAG_SECINFO, &priv->assoc_flags);
	set_bit(ASSOC_FLAG_WEP_KEYS, &priv->assoc_flags);
}

static void disable_wpa(struct rda5890_private *priv)
{
	memset(&priv->wpa_mcast_key, 0, sizeof (struct enc_key));
	priv->wpa_mcast_key.flags = KEY_INFO_WPA_MCAST;
	set_bit(ASSOC_FLAG_WPA_MCAST_KEY, &priv->assoc_flags);

	memset(&priv->wpa_unicast_key, 0, sizeof (struct enc_key));
	priv->wpa_unicast_key.flags = KEY_INFO_WPA_UNICAST;
	set_bit(ASSOC_FLAG_WPA_UCAST_KEY, &priv->assoc_flags);

	priv->secinfo.WPAenabled = 0;
	priv->secinfo.WPA2enabled = 0;
    priv->secinfo.cipther_type = 0;
    priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
	set_bit(ASSOC_FLAG_SECINFO, &priv->assoc_flags);
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
static int rda5890_get_encodeext(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq,
			      char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = -EINVAL;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int index, max_key_len;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

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
		if (index != 0)
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
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
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
static int rda5890_set_encodeext(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq,
			      char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = 0;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int alg = ext->alg;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	if ((alg == IW_ENCODE_ALG_NONE) || (dwrq->flags & IW_ENCODE_DISABLED)) {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"NO SEC\n");
        if(test_bit(ASSOC_FLAG_ASSOC_START ,&priv->assoc_flags))
        {
            if(priv->imode != 3 && priv->imode != 5)
	            disable_wep (priv);
        }
		disable_wpa (priv);
	} else if (alg == IW_ENCODE_ALG_WEP) {
		u16 is_default = 0, index, set_tx_key = 0;

		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"WEP, flags = 0x%04x\n", dwrq->flags);

		ret = validate_key_index(priv->wep_tx_keyidx,
		                         (dwrq->flags & IW_ENCODE_INDEX),
		                         &index, &is_default);
		if (ret)
			goto out;

		/* If WEP isn't enabled, or if there is no key data but a valid
		 * index, or if the set-TX-key flag was passed, set the TX key.
		 */
		if (   !priv->secinfo.wep_enabled
		    || (dwrq->length == 0 && !is_default)
		    || (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY))
			set_tx_key = 1;

		/* Copy key to driver */
		ret = copy_wep_key(priv, ext->key, ext->key_len, index, set_tx_key);
		if (ret)
			goto out;

		/* Set Key to Mac */
		/* Move to assoc_helper_secinfo(), wep_key need to be set after imode */
		//ret = rda5890_set_wepkey(priv, index, ext->key, ext->key_len);
		//if (ret)
		//	goto out;

		if (dwrq->flags & IW_ENCODE_RESTRICTED) {
			priv->secinfo.auth_mode = IW_AUTH_ALG_SHARED_KEY;
		} else if (dwrq->flags & IW_ENCODE_OPEN) {
			priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
		}

		/* Mark the various WEP bits as modified */
		set_bit(ASSOC_FLAG_SECINFO, &priv->assoc_flags);
		if (dwrq->length)
			set_bit(ASSOC_FLAG_WEP_KEYS, &priv->assoc_flags);
		if (set_tx_key)
			set_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &priv->assoc_flags);
	} else if ((alg == IW_ENCODE_ALG_TKIP) || (alg == IW_ENCODE_ALG_CCMP)) {
		struct enc_key * pkey;

		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"TKIP or CCMP, flags = 0x%04x, alg = %d\n", dwrq->flags, alg);

		/* validate key length */
		if (((alg == IW_ENCODE_ALG_TKIP)
			&& (ext->key_len != KEY_LEN_WPA_TKIP))
		    || ((alg == IW_ENCODE_ALG_CCMP)
		        && (ext->key_len != KEY_LEN_WPA_AES))) {
				RDA5890_ERRP("invalid size %d for key of alg, type %d\n",
				       ext->key_len, alg);
				ret = -EINVAL;
				goto out;
		}

		/* Copy key to driver */
		if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
			pkey = &priv->wpa_mcast_key;
			set_bit(ASSOC_FLAG_WPA_MCAST_KEY, &priv->assoc_flags);
		} else {
			pkey = &priv->wpa_unicast_key;
			set_bit(ASSOC_FLAG_WPA_UCAST_KEY, &priv->assoc_flags);
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
			if (!(ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)
			    && !(priv->imode & (BIT6))) {
				RDA5890_ERRP("imode [0x%x] not match with cipher alg TKIP\n",
					priv->imode);
			}
		} else if (alg == IW_ENCODE_ALG_CCMP) {
			pkey->type = KEY_TYPE_ID_AES;
			if (!(ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)
			    && !(priv->imode & (BIT5))) {
				RDA5890_ERRP("imode [0x%x] not match with cipher alg CCMP\n",
					priv->imode);
			}
		}

		/* If WPA isn't enabled yet, do that now */
		if (   priv->secinfo.WPAenabled == 0
		    && priv->secinfo.WPA2enabled == 0) {
			priv->secinfo.WPAenabled = 1;
			priv->secinfo.WPA2enabled = 1;
			set_bit(ASSOC_FLAG_SECINFO, &priv->assoc_flags);
		}

		/* Set Keys to MAC*/
		if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
			/* Set GTK */
			/* 
			 *Always use key_id = 1 for now
			 * need to toggle among 1, 2, 3
			 */
			ret = rda5890_set_gtk(priv, 1, ext->tx_seq, IW_ENCODE_SEQ_MAX_SIZE,
				pkey->key, pkey->len);
			if (ret)
				goto out;
		} else {
			pkey->flags |= KEY_INFO_WPA_UNICAST;
			/* Set PTK */
			ret = rda5890_set_ptk(priv, pkey->key, pkey->len);
			if (ret)
				goto out;
		}

		/* Only disable wep if necessary: can't waste time here. */
		disable_wep(priv);
	} else if (alg == IW_ENCODE_ALG_WAPI) { //wapi
		if(ext->key_len != 32)
			goto out;

		priv->is_wapi = 1;

		/* Set Keys to MAC*/
		if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
			unsigned char tmp[8];
			/* Set GTK */
			/* 
			 * Always use key_id = 1 for now
			 * need to toggle among 1, 2, 3
			 */
			ret = rda5890_set_gtk(priv, 1, tmp, IW_ENCODE_SEQ_MAX_SIZE,
				ext->key, ext->key_len);
			if (ret)
				goto out;
		} else {
			/* Set PTK */
			ret = rda5890_set_ptk(priv, ext->key, ext->key_len);
			if (ret)
				goto out;
		}
	}

out:
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

/**
 *  @brief PMKSA cache operation (WPA/802.1x and WEP)
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info			A pointer to iw_request_info structure
 *  @param vwrq 		A pointer to iw_param structure
 *  @param extra		A pointer to extra data buf
 *  @return 	   		0 on success, otherwise failure
 */
static int rda5890_set_pmksa(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq,
			      char *extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

static int rda5890_set_genie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	if(extra[0] == 0x44) //wapi ie
	{
		unsigned char ie_len = extra[1] + 2;
		rda5890_generic_set_str(priv, WID_WAPI_ASSOC_IE, extra ,ie_len);
		goto out;
	}
          
	if (dwrq->length > MAX_WPA_IE_LEN ||
	    (dwrq->length && extra == NULL)) {
		ret = -EINVAL;
		goto out;
	}

	if (dwrq->length) {
		memcpy(&priv->wpa_ie[0], extra, dwrq->length);
		priv->wpa_ie_len = dwrq->length;
	} else {
		memset(&priv->wpa_ie[0], 0, sizeof(priv->wpa_ie));
		priv->wpa_ie_len = 0;
	}

out:

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return ret;
}

static int rda5890_get_genie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

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
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return ret;
}

static int rda5890_set_auth(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *dwrq,
			 char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = 0;
	int updated = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
		"flags = 0x%04x, value = 0x%x\n", dwrq->flags, dwrq->value);

	switch (dwrq->flags & IW_AUTH_INDEX) {
      	case IW_AUTH_CIPHER_PAIRWISE:
#ifdef GET_SCAN_FROM_NETWORK_INFO
        if (dwrq->value & (IW_AUTH_CIPHER_WEP104 | IW_AUTH_CIPHER_WEP40)) 
        {
	      RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
		    "WEP Selected \n");
			priv->secinfo.wep_enabled = 1;
               if(dwrq->value & IW_AUTH_CIPHER_WEP104)
                   priv->secinfo.cipther_type |= IW_AUTH_CIPHER_WEP104;
               else if(dwrq->value & IW_AUTH_CIPHER_WEP40)
                  priv->secinfo.cipther_type |= IW_AUTH_CIPHER_WEP40;
		}
		if (dwrq->value & IW_AUTH_CIPHER_TKIP) {
                    RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"IW_AUTH_CIPHER_TKIP \n");
                     priv->secinfo.cipther_type |= IW_AUTH_CIPHER_TKIP;
		}
		if (dwrq->value & IW_AUTH_CIPHER_CCMP) {
                    RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"IW_AUTH_CIPHER_CCMP \n");
                    priv->secinfo.cipther_type |= IW_AUTH_CIPHER_CCMP;
		}
		if (dwrq->value & IW_AUTH_CIPHER_NONE) {
                    RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"OPEN System \n");
            priv->secinfo.cipther_type = IW_AUTH_CIPHER_NONE;
		}
    break;
#endif
	case IW_AUTH_TKIP_COUNTERMEASURES:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_DROP_UNENCRYPTED:
		/*
		 * rda5890 does not use these parameters
		 */
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"DO NOT USE\n");
		break;

	case IW_AUTH_KEY_MGMT:
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"KEY_MGMT, val = %d\n", dwrq->value);
		priv->secinfo.key_mgmt = dwrq->value;
		updated = 1;
		break;

	case IW_AUTH_WPA_VERSION:
		if (dwrq->value & IW_AUTH_WPA_VERSION_DISABLED) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"WPA_VERSION, DISABLED\n");
			priv->secinfo.WPAenabled = 0;
			priv->secinfo.WPA2enabled = 0;
			disable_wpa (priv);
		}
		if (dwrq->value & IW_AUTH_WPA_VERSION_WPA) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"WPA_VERSION, WPA\n");
			priv->secinfo.WPAenabled = 1;
			priv->secinfo.wep_enabled = 0;
			priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
		}
		if (dwrq->value & IW_AUTH_WPA_VERSION_WPA2) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"WPA_VERSION, WPA2\n");
			priv->secinfo.WPA2enabled = 1;
			priv->secinfo.wep_enabled = 0;
			priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
		}
		updated = 1;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		if (dwrq->value & IW_AUTH_ALG_SHARED_KEY || 
            dwrq->value & IW_AUTH_ALG_OPEN_SYSTEM) 
        {
            if(dwrq->value & IW_AUTH_ALG_SHARED_KEY )
            {
    			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
    				"80211_AUTH_ALG, SHARED_KEY\n");
    			priv->secinfo.auth_mode |= IW_AUTH_ALG_SHARED_KEY;
    		}
    		if(dwrq->value & IW_AUTH_ALG_OPEN_SYSTEM) {
    			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
    				"80211_AUTH_ALG, OPEN\n");
    			priv->secinfo.auth_mode |= IW_AUTH_ALG_OPEN_SYSTEM;
    		}
        }
		else if(dwrq->value & IW_AUTH_ALG_LEAP) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"80211_AUTH_ALG, LEAP\n");
			priv->secinfo.auth_mode = IW_AUTH_ALG_LEAP;
		}
        else if(dwrq->value & IW_AUTH_ALG_WAPI) {
			priv->secinfo.auth_mode = IW_AUTH_ALG_WAPI;
		}      
		else{
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"80211_AUTH_ALG, unknown\n");
			ret = -EINVAL;
		}
		updated = 1;
		break;

	case IW_AUTH_WPA_ENABLED:
		if (dwrq->value) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"WPA_ENABLED, value = 0x%x\n", dwrq->value);
			if (!priv->secinfo.WPAenabled &&
			    !priv->secinfo.WPA2enabled) {
				priv->secinfo.WPAenabled = 1;
				priv->secinfo.WPA2enabled = 1;
				priv->secinfo.wep_enabled = 0;
				priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
			}
		} else {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"WPA_ENABLED, value = ZERO\n");
			priv->secinfo.WPAenabled = 0;
			priv->secinfo.WPA2enabled = 0;
			disable_wpa (priv);
		}
		updated = 1;
		break;

	default:
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"NOT SUPPORT\n");
		ret = -EOPNOTSUPP;
		break;
	}

	if (ret == 0) {
		if (updated) {
			set_bit(ASSOC_FLAG_SECINFO, &priv->assoc_flags);
		}
	}

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return ret;
}

static int rda5890_get_auth(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *dwrq,
			 char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);

	switch (dwrq->flags & IW_AUTH_INDEX) {
	case IW_AUTH_KEY_MGMT:
		dwrq->value = priv->secinfo.key_mgmt;
		break;

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

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);
	return ret;
}

static int rda5890_set_txpow(struct net_device *dev, struct iw_request_info *info,
		   struct iw_param *vwrq, char *extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);
	return 0;
}

static int rda5890_get_essid(struct net_device *dev, struct iw_request_info *info,
		   struct iw_point *dwrq, char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);


	memcpy(extra, priv->curbssparams.ssid,
	       strlen(priv->curbssparams.ssid));
	dwrq->length = strlen(priv->curbssparams.ssid);
    extra[dwrq->length] = '\0';

	/*
	 * If none, we may want to get the one that was set
	 */

	dwrq->flags = 1;	/* active */

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>> \n", __func__);
	return 0;
}

void rda5890_indicate_disconnected(struct rda5890_private *priv)
{
	union iwreq_data wrqu;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_NORM, "%s <<<\n", __func__);

	memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

    /*report disconnect to upper layer*/
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_NORM, "%s >>>\n", __func__);   
}

void rda5890_indicate_connected(struct rda5890_private *priv)
{
	union iwreq_data wrqu;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_NORM, "%s <<<\n", __func__);

	memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_NORM, "%s >>>\n", __func__);
}

void rda5890_assoc_done_worker(struct work_struct *work)
{
    u8 bssid[6], zero_bssid[6];
	struct rda5890_private *priv = container_of(work, struct rda5890_private,
		assoc_done_work.work);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<< \n", __func__);

    memset(bssid, 0, sizeof(bssid));
    memset(zero_bssid, 0, sizeof(zero_bssid));
    rda5890_get_bssid(priv, bssid);

    if(memcmp(bssid, zero_bssid, sizeof(zero_bssid)))
    {
        memcpy(priv->curbssparams.bssid, bssid, sizeof(bssid));
    }

    rda5890_get_rssi(priv, &priv->curbssparams.rssi);
    priv->curbssparams.rssi = priv->curbssparams.rssi > 127? 
        priv->curbssparams.rssi - 271: priv->curbssparams.rssi - 15;
    
    rda5990_assoc_power_save(priv);
    clear_bit(ASSOC_FLAG_ASSOC_START, &priv->assoc_flags);
    
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);
}

void rda5890_wlan_connect_worker(struct work_struct *work)
{
	struct rda5890_private *priv = container_of(work, struct rda5890_private,
	wlan_connect_work.work);

    rda5890_set_txrate(priv, 0);

    if(priv)
        clear_bit(ASSOC_FLAG_WLAN_CONNECTING ,&priv->assoc_flags);
}

//imode
/* BIT0: 1 -> Security ON              0 -> OFF                          */
/* BIT1: 1 -> WEP40  cypher supported  0 -> Not supported                */
/* BIT2: 1 -> WEP104 cypher supported  0 -> Not supported                */
/* BIT3: 1 -> WPA mode      supported  0 -> Not supported                */
/* BIT4: 1 -> WPA2 (RSN)    supported  0 -> Not supported                */
/* BIT5: 1 -> AES-CCMP cphr supported  0 -> Not supported                */
/* BIT6: 1 -> TKIP   cypher supported  0 -> Not supported                */
/* BIT7: 1 -> TSN           supported  0 -> Not supported                */

//authtype
/* BIT0: 1 -> OPEN SYSTEM  */
/* BIT1: 1 -> SHARED KEY  */
/* BIT3: 1 -> WPA RSN  802.1x*/
/* BIT7: 1 -> WAPI   */
static int assoc_helper_secinfo(struct rda5890_private *priv, 
		struct bss_descriptor *assoc_bss)
{
	int ret = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);

	/* set imode and key */
	if (   !priv->secinfo.wep_enabled
	    && !priv->secinfo.WPAenabled
	    && !priv->secinfo.WPA2enabled) {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"%s, NO SEC\n", __func__);
		priv->imode = 0;
	} else {
		u16 key_len = 0;
		u16 i;
		if (   priv->secinfo.wep_enabled
		    && !priv->secinfo.WPAenabled
		    && !priv->secinfo.WPA2enabled) {
			/* WEP */
			key_len = priv->wep_keys[0].len;
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"%s, WEP, len = %d\n", __func__, key_len * 8);
			if (key_len == KEY_LEN_WEP_40) {
				priv->imode = BIT0 | BIT1;
			}
			else if (key_len == KEY_LEN_WEP_104) {
				priv->imode = BIT0 | BIT2;
			}
			else {
				RDA5890_ERRP("Invalide WEP Key length %d\n", key_len);
				ret = -EINVAL;
				goto out;
			}
		} else if (   !priv->secinfo.wep_enabled
		           && (priv->secinfo.WPAenabled ||
		               priv->secinfo.WPA2enabled)) {
			/* WPA */
			struct enc_key * pkey = NULL;

			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"%s, WPA cp:%x \n", __func__, priv->secinfo.cipther_type);

			if (   priv->wpa_mcast_key.len
			    && (priv->wpa_mcast_key.flags & KEY_INFO_WPA_ENABLED))
				pkey = &priv->wpa_mcast_key;
			else if (   priv->wpa_unicast_key.len
			         && (priv->wpa_unicast_key.flags & KEY_INFO_WPA_ENABLED))
				pkey = &priv->wpa_unicast_key;

			//priv->imode = assoc_bss->data.dot11i_info;
			priv->imode = 0;
			/* turn on security */
			priv->imode |= (BIT0);
			priv->imode &= ~(BIT3 | BIT4);
			if (priv->secinfo.WPA2enabled)
				priv->imode |= (BIT4);
			else if (priv->secinfo.WPAenabled)
				priv->imode |= (BIT3);
			/* 
			 * we don't know the cipher type by now
			 * use dot11i_info to decide
			 * and use CCMP if possible
			 */
			priv->imode &= ~(BIT5 | BIT6);
#ifdef GET_SCAN_FROM_NETWORK_INFO
			if (priv->secinfo.cipther_type & IW_AUTH_CIPHER_CCMP)
				priv->imode |= BIT5;
			else if (priv->secinfo.cipther_type & IW_AUTH_CIPHER_TKIP)
				priv->imode |= BIT6;
#else
			if (assoc_bss->data.dot11i_info & (BIT5))
				priv->imode |= BIT5;
			else if (assoc_bss->data.dot11i_info & (BIT6))
				priv->imode |= BIT6;
#endif            
		} else {
			RDA5890_ERRP("WEP and WPA/WPA2 enabled simutanously\n");
			ret = -EINVAL;
			goto out;
		}
	}

	/* set authtype */
	if (priv->secinfo.auth_mode & IW_AUTH_ALG_OPEN_SYSTEM 
        || priv->secinfo.auth_mode & IW_AUTH_ALG_SHARED_KEY)
    {

        if (priv->secinfo.auth_mode & IW_AUTH_ALG_OPEN_SYSTEM)
            {
        		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
        			"%s, Open Auth, KEY_MGMT = %d, AUTH_ALG mode:%x\n", __func__, priv->secinfo.key_mgmt, priv->secinfo.auth_mode);
        		if (priv->secinfo.key_mgmt == 0x01) {
        			/* for 802.1x, set auth type to 0x04 */
        			priv->authtype = BIT3;   			
        		}
        		else 
                {
        			priv->authtype = BIT0;
    		    }
    	    }
        else if(priv->secinfo.auth_mode & IW_AUTH_ALG_SHARED_KEY) 
    	    {
        		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
        			"%s, Shared-Key Auth AUTH_ALG mode:%x \n", __func__, priv->secinfo.auth_mode);
        		priv->authtype = BIT1;
        	}
    }
    else if (priv->secinfo.auth_mode == IW_AUTH_ALG_WAPI) {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"%s, Shared-Key Auth\n", __func__);
		priv->authtype = IW_AUTH_ALG_WAPI;
	}
	else if (priv->secinfo.auth_mode == IW_AUTH_ALG_LEAP) {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"%s, LEAP Auth, not supported\n", __func__);
		ret = -EINVAL;
		goto out;
	}
	else {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"%s, Unknown Auth\n", __func__);
		ret = -EINVAL;
		goto out;
	}

out:
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>> \n", __func__);
	return ret;
}

static struct bss_descriptor *get_bss_desc_from_scanlist(
		struct rda5890_private *priv, unsigned char *ssid)
{
	struct bss_descriptor *iter_bss;
	struct bss_descriptor *ret_bss = NULL;
	/* report all bss to upper layer */
	list_for_each_entry (iter_bss, &priv->network_list, list) {
#ifdef 	GET_SCAN_FROM_NETWORK_INFO
        if (strcmp(iter_bss->ssid, ssid) == 0) {
#else
		if (strcmp(iter_bss->data.ssid, ssid) == 0) {
#endif            
			ret_bss = iter_bss;
			break;
		}
	}
	return ret_bss;
}

void rda5890_assoc_worker(struct work_struct *work)
{
    static char old_imode = 0xff, old_bssid[6], assoc_count = 0;
	struct rda5890_private *priv = container_of(work, struct rda5890_private,
		assoc_work.work);
	int ret = 0;
	struct bss_descriptor *assoc_bss;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	assoc_bss = get_bss_desc_from_scanlist(priv, priv->assoc_ssid);
	if (assoc_bss == NULL) {
		RDA5890_ERRP("****fail to find bss in the scan list\n");
		ret = -EINVAL;
		goto out;
	}

#ifdef  GET_SCAN_FROM_NETWORK_INFO
	if(assoc_bss->rssi > 200)
	{
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "assoc_bss rssi =%d > 200\n", assoc_bss->rssi);
	    rda5890_rssi_up_to_200(priv);	
	}
#else
	if(assoc_bss->data.rssi > 200)
	{
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "assoc_bss rssi =%d > 200\n", assoc_bss->data.rssi);
	    rda5890_rssi_up_to_200(priv);	
	}
#endif    
#ifdef GET_SCAN_FROM_NETWORK_INFO
    priv->curbssparams.channel = assoc_bss->channel; 
    memcpy(priv->curbssparams.bssid, assoc_bss->bssid, ETH_ALEN);
    memcpy(priv->curbssparams.ssid, assoc_bss->ssid,IW_ESSID_MAX_SIZE + 1);
#else
    priv->curbssparams.channel = assoc_bss->data.channel; 
    memcpy(priv->curbssparams.bssid, assoc_bss->data.bssid, ETH_ALEN);
    memcpy(priv->curbssparams.ssid, assoc_bss->data.ssid,IW_ESSID_MAX_SIZE + 1);
#endif
	ret = assoc_helper_secinfo(priv, assoc_bss);
	if (ret) {
		RDA5890_ERRP("assoc_helper_secinfo fail, ret = %d\n", ret);
		goto out;
	}

    //if the bssid is same and the association is start then break out
    if((old_imode == priv->imode) && !memcmp(old_bssid, priv->assoc_bssid,6))
    {
        //WEP THE Second retry should change to shared key
        if((old_imode == 3 || old_imode ==  5) && assoc_count%2)  
        {
            priv->authtype = BIT1;
        }
        assoc_count ++;        
    }
    else
    {
        //save old bssid para
        old_imode = priv->imode;
        memcpy(old_bssid, priv->assoc_bssid, 6);
        assoc_count = 0;
    }

    set_bit(ASSOC_FLAG_ASSOC_START ,&priv->assoc_flags);    
    set_bit(ASSOC_FLAG_ASSOC_RETRY, &priv->assoc_flags);

    if((priv->imode == 3) || (priv->imode ==5))
        {
            if(assoc_count > 5)
            {
                clear_bit(ASSOC_FLAG_ASSOC_RETRY, &priv->assoc_flags);
                clear_bit(ASSOC_FLAG_ASSOC_START ,&priv->assoc_flags);
                old_imode = 0xff;
            }
        }
    else
        {
            if(assoc_count)
            {
                clear_bit(ASSOC_FLAG_ASSOC_RETRY, &priv->assoc_flags);
                clear_bit(ASSOC_FLAG_ASSOC_START ,&priv->assoc_flags);
                old_imode = 0xff;
            }
        }
    
    ret = rda5890_start_join(priv);
	if (ret) {
		RDA5890_ERRP("rda5890_set_ssid fail, ret = %d\n", ret);
		goto out;
	}
  
    if(test_bit(ASSOC_FLAG_ASSOC_RETRY, &priv->assoc_flags))
    {       
        queue_delayed_work(priv->work_thread, &priv->assoc_work, 3*HZ);
    }
    
out:
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<< \n", __func__);
}

static int rda5890_set_essid(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *dwrq, char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = 0;
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len = 0;
	int in_ssid_len = dwrq->length;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

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
		ssid[in_ssid_len] = '\0';
		ssid_len = in_ssid_len;
	}

	if (!ssid_len) {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"requested any SSID\n");
	} else {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"requested SSID len = %d ssid:%s\n",
			ssid_len, ssid);
	}

    if(ssid_len)
    {
        memcpy(&priv->assoc_ssid[0], ssid, sizeof(ssid));
        priv->assoc_ssid_len = ssid_len;
    }

    if(!test_bit(ASSOC_FLAG_SSID, &priv->assoc_flags))
        goto out;

    if(!test_bit(ASSOC_FLAG_ASSOC_RETRY, &priv->assoc_flags))
    {
        if(ssid_len)
        {
        	cancel_delayed_work(&priv->assoc_work);
        	queue_delayed_work(priv->work_thread, &priv->assoc_work, HZ/2);
        }
    }

out:
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
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
static int rda5890_set_wap(struct net_device *dev, struct iw_request_info *info,
		struct sockaddr *awrq, char *extra)
{
    unsigned char * ap_addr = NULL;
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

    ap_addr = awrq->sa_data;
    if(!test_bit(ASSOC_FLAG_ASSOC_RETRY, &priv->assoc_flags)
        && !is_zero_eth_addr(ap_addr))
    {
    	cancel_delayed_work(&priv->assoc_work);
    	queue_delayed_work(priv->work_thread, &priv->assoc_work, HZ/2);
        set_bit(ASSOC_FLAG_SSID, &priv->assoc_flags);
        memcpy(priv->assoc_bssid, ap_addr, 6);
        printk("rda5890_set_wap addr is not null \n");
    }

    if(is_zero_eth_addr(ap_addr))
    {
        clear_bit(ASSOC_FLAG_SSID, &priv->assoc_flags);
        disable_wep( priv);
        disable_wpa(priv);
    }
        

	RDA5890_ERRP("%s <<< \n connect mac: %2x:%2x:%2x:%2x:%2x:%2x \n", __func__, 
         ap_addr[0],ap_addr[1],ap_addr[2],ap_addr[3],ap_addr[4],ap_addr[5]);
    
	return 0;
}

static inline char *translate_scan(struct rda5890_private *priv,
					    struct iw_request_info *info,
					    char *start, char *stop,
					    struct bss_descriptor *bss_desc)
{
#ifndef GET_SCAN_FROM_NETWORK_INFO
	struct iw_event iwe;	/* Temporary buffer */
	u8 snr;
	struct rda5890_bss_descriptor *bss = &bss_desc->data;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
		"translate_scan, ssid = %s\n", bss->ssid);

	/* First entry *MUST* be the BSSID */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, &bss->bssid, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_ADDR_LEN);

	/* SSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	iwe.u.data.length = strlen(bss->ssid);
	start = iwe_stream_add_point(info, start, stop, &iwe, bss->ssid);

	/* Mode */
	iwe.cmd = SIOCGIWMODE;
	//iwe.u.mode = bss->mode;
	iwe.u.mode = IW_MODE_INFRA;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_UINT_LEN);

	/* Frequency */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = (2412 + 5 * (bss->channel - 1)) * 100000;
	iwe.u.freq.e = 1;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_FREQ_LEN);

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_ALL_UPDATED;
	iwe.u.qual.level = bss->rssi > 127? bss->rssi - 271: bss->rssi - 15;

	snr = iwe.u.qual.level - RDA5890_NF_DEFAULT_SCAN_VALUE;
	iwe.u.qual.qual =
		(100 * RSSI_DIFF * RSSI_DIFF - (PERFECT_RSSI - snr) *
		 (15 * (RSSI_DIFF) + 62 * (PERFECT_RSSI - snr))) /
		(RSSI_DIFF * RSSI_DIFF);
	if (iwe.u.qual.qual > 100)
		iwe.u.qual.qual = 100;
	iwe.u.qual.noise = RDA5890_NF_DEFAULT_SCAN_VALUE;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (bss->dot11i_info & BIT0) {
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	} else {
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	}
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(info, start, stop, &iwe, bss->ssid);

#if 0
	current_val = start + iwe_stream_lcp_len(info);

	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = 0;
	iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = 0;

	for (j = 0; bss->rates[j] && (j < sizeof(bss->rates)); j++) {
		/* Bit rate given in 500 kb/s units */
		iwe.u.bitrate.value = bss->rates[j] * 500000;
		current_val = iwe_stream_add_value(info, start, current_val,
						   stop, &iwe, IW_EV_PARAM_LEN);
	}
	if ((bss->mode == IW_MODE_ADHOC) && priv->adhoccreate
	    && !lbs_ssid_cmp(priv->curbssparams.ssid,
			     priv->curbssparams.ssid_len,
			     bss->ssid, bss->ssid_len)) {
		iwe.u.bitrate.value = 22 * 500000;
		current_val = iwe_stream_add_value(info, start, current_val,
						   stop, &iwe, IW_EV_PARAM_LEN);
	}
	/* Check if we added any event */
	if ((current_val - start) > iwe_stream_lcp_len(info))
		start = current_val;
#endif

	memset(&iwe, 0, sizeof(iwe));
	if (bss_desc->wpa_ie_len) {
		char buf[MAX_WPA_IE_LEN];

		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"translate_scan, wpa_ie, len %d\n", bss_desc->wpa_ie_len);
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"%02x %02x %02x %02x ... ... %02x %02x %02x %02x\n",
			bss_desc->wpa_ie[0], bss_desc->wpa_ie[1], 
			bss_desc->wpa_ie[2], bss_desc->wpa_ie[3], 
			bss_desc->wpa_ie[bss_desc->wpa_ie_len - 4], 
			bss_desc->wpa_ie[bss_desc->wpa_ie_len - 3],
			bss_desc->wpa_ie[bss_desc->wpa_ie_len - 2], 
			bss_desc->wpa_ie[bss_desc->wpa_ie_len - 1]);

		memcpy(buf, bss_desc->wpa_ie, bss_desc->wpa_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss_desc->wpa_ie_len;
		start = iwe_stream_add_point(info, start, stop, &iwe, buf);
	}

	memset(&iwe, 0, sizeof(iwe));
	if (bss_desc->rsn_ie_len) {
		char buf[MAX_WPA_IE_LEN];

		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"translate_scan, rsn_ie, len %d\n", bss_desc->rsn_ie_len);
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"%02x %02x %02x %02x ... ... %02x %02x %02x %02x\n",
			bss_desc->rsn_ie[0], bss_desc->rsn_ie[1], 
			bss_desc->rsn_ie[2], bss_desc->rsn_ie[3], 
			bss_desc->rsn_ie[bss_desc->rsn_ie_len - 4], 
			bss_desc->rsn_ie[bss_desc->rsn_ie_len - 3],
			bss_desc->rsn_ie[bss_desc->rsn_ie_len - 2], 
			bss_desc->rsn_ie[bss_desc->rsn_ie_len - 1]);

		memcpy(buf, bss_desc->rsn_ie, bss_desc->rsn_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss_desc->rsn_ie_len;
		start = iwe_stream_add_point(info, start, stop, &iwe, buf);
	}

	return start;
#else
	struct iw_event iwe;	/* Temporary buffer */
	u8 snr;
	struct bss_descriptor *bss = bss_desc;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
		"translate_scan, ssid = %s ssi=%d ssid_len=%d \n", bss->ssid, bss->rssi, bss->ssid_len);

	/* First entry *MUST* be the BSSID */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bss->bssid, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_ADDR_LEN);

	/* SSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	iwe.u.data.length = bss->ssid_len;
	start = iwe_stream_add_point(info, start, stop, &iwe, bss->ssid);

	/* Mode */
	iwe.cmd = SIOCGIWMODE;
	iwe.u.mode = bss->mode;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_UINT_LEN);

	/* Frequency */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = (2412 + 5 * (bss->channel - 1)) * 100000;
	iwe.u.freq.e = 1;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_FREQ_LEN);

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_ALL_UPDATED;
	iwe.u.qual.level = bss->rssi > 127? bss->rssi - 271: bss->rssi - 15;

	snr = iwe.u.qual.level - RDA5890_NF_DEFAULT_SCAN_VALUE;
	iwe.u.qual.qual =
		(100 * RSSI_DIFF * RSSI_DIFF - (PERFECT_RSSI - snr) *
		 (15 * (RSSI_DIFF) + 62 * (PERFECT_RSSI - snr))) /
		(RSSI_DIFF * RSSI_DIFF);
	if (iwe.u.qual.qual > 100)
		iwe.u.qual.qual = 100;
	iwe.u.qual.noise = RDA5890_NF_DEFAULT_SCAN_VALUE;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (bss->capability & WLAN_CAPABILITY_PRIVACY) {
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	} else {
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	}
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(info, start, stop, &iwe, bss->ssid);

	memset(&iwe, 0, sizeof(iwe));
	if (bss_desc->wpa_ie_len && !bss_desc->wapi_ie_len) {
		char buf[MAX_WPA_IE_LEN];

		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"translate_scan, wpa_ie, len %d\n", bss_desc->wpa_ie_len);
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"%02x %02x %02x %02x ... ... %02x %02x %02x %02x\n",
			bss_desc->wpa_ie[0], bss_desc->wpa_ie[1], 
			bss_desc->wpa_ie[2], bss_desc->wpa_ie[3], 
			bss_desc->wpa_ie[bss_desc->wpa_ie_len - 4], 
			bss_desc->wpa_ie[bss_desc->wpa_ie_len - 3],
			bss_desc->wpa_ie[bss_desc->wpa_ie_len - 2], 
			bss_desc->wpa_ie[bss_desc->wpa_ie_len - 1]);

		memcpy(buf, bss->wpa_ie, bss->wpa_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss_desc->wpa_ie_len;
		start = iwe_stream_add_point(info, start, stop, &iwe, buf);
	}

	memset(&iwe, 0, sizeof(iwe));
	if (bss_desc->rsn_ie_len && !bss_desc->wapi_ie_len) {
		char buf[MAX_WPA_IE_LEN];

		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"translate_scan, rsn_ie, len %d\n", bss_desc->rsn_ie_len);
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"%02x %02x %02x %02x ... ... %02x %02x %02x %02x\n",
			bss_desc->rsn_ie[0], bss_desc->rsn_ie[1], 
			bss_desc->rsn_ie[2], bss_desc->rsn_ie[3], 
			bss_desc->rsn_ie[bss_desc->rsn_ie_len - 4], 
			bss_desc->rsn_ie[bss_desc->rsn_ie_len - 3],
			bss_desc->rsn_ie[bss_desc->rsn_ie_len - 2], 
			bss_desc->rsn_ie[bss_desc->rsn_ie_len - 1]);

		memcpy(buf, bss->rsn_ie, bss->rsn_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss_desc->rsn_ie_len;
		start = iwe_stream_add_point(info, start, stop, &iwe, buf);
	}

	memset(&iwe, 0, sizeof(iwe));
	if (bss_desc->wapi_ie_len) {
		char buf[100];

		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"translate_scan, wapi_len %d\n", bss_desc->wapi_ie_len);
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE,
			"%02x %02x %02x %02x ... ... %02x %02x %02x %02x\n",
			bss_desc->wapi_ie[0], bss_desc->wapi_ie[1], 
			bss_desc->wapi_ie[2], bss_desc->wapi_ie[3], 
			bss_desc->wapi_ie[bss_desc->wapi_ie_len - 4], 
			bss_desc->wapi_ie[bss_desc->wapi_ie_len - 3],
			bss_desc->wapi_ie[bss_desc->wapi_ie_len - 2], 
			bss_desc->wapi_ie[bss_desc->wapi_ie_len - 1]);

		memcpy(buf, bss->wapi_ie, bss->wapi_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss_desc->wapi_ie_len;
		start = iwe_stream_add_point(info, start, stop, &iwe, buf);
	}

	return start;
#endif
}

int is_same_network(struct bss_descriptor *src,
				  struct bss_descriptor *dst)
{
	/* A network is only a duplicate if the channel, BSSID, and ESSID
	 * all match.  We treat all <hidden> with the same BSSID and channel
	 * as one network */
#ifndef GET_SCAN_FROM_NETWORK_INFO
	return ((src->data.channel == dst->data.channel) &&
		!compare_ether_addr(src->data.bssid, dst->data.bssid) &&
		!memcmp(src->data.ssid, dst->data.ssid, IW_ESSID_MAX_SIZE));
#else
      	return ((src->channel == dst->channel) &&
		!compare_ether_addr(src->bssid, dst->bssid) &&
		!memcmp(src->ssid, dst->ssid, IW_ESSID_MAX_SIZE));
#endif
}

void clear_bss_descriptor(struct bss_descriptor *bss)
{
	/* Don't blow away ->list, just BSS data */
	memset(bss, 0, offsetof(struct bss_descriptor, list));
}

static void dump_bss_desc(struct rda5890_bss_descriptor *bss_desc)
{
	RDA5890_DBGP("########## dump bss ##########\n");
	RDA5890_DBGP("ssid = %s\n", bss_desc->ssid);
	RDA5890_DBGP("bss_type = %d\n", bss_desc->bss_type);
	RDA5890_DBGP("channel = %d\n", bss_desc->channel);
	RDA5890_DBGP("dot11i_info = 0x%02x\n", bss_desc->dot11i_info);
	RDA5890_DBGP("bssid = %02x:%02x:%02x:%02x:%02x:%02x\n", 
		bss_desc->bssid[0], bss_desc->bssid[1], bss_desc->bssid[2], 
		bss_desc->bssid[3], bss_desc->bssid[4], bss_desc->bssid[5]);
	RDA5890_DBGP("rssi = %d\n", (char)bss_desc->rssi);
	RDA5890_DBGP("auth_info = 0x%02x\n", bss_desc->auth_info);
	RDA5890_DBGP("rsn_cap = 0x%04x\n",
		(bss_desc->rsn_cap[1] << 8) | bss_desc->rsn_cap[0]);
	RDA5890_DBGP("########## dump bss ##########\n");
}

/* Element Ids used in Management frames in 802.11i mode */
typedef enum{ IRSNELEMENT = 48,  /* RSN Information Element  */
              IWPAELEMENT = 221 /* WPA Information Element  */
}ELEMENTID_11I_T;

/* CIPHER set for RSN or WPA element  */
typedef enum { CIPHER_TYPE_USE_GROUP_SET  = 0,
               CIPHER_TYPE_WEP40          = 1,
               CIPHER_TYPE_TKIP           = 2,
               CIPHER_TYPE_CCMP           = 4,
               CIPHER_TYPE_WEP104         = 5
} CIPHER_TYPE_T;

unsigned char oui_rsn[3] = {0x00, 0x0F, 0xAC};
unsigned char oui_wpa[3] = {0x00, 0x50, 0xf2};

static void fill_rsn_wpa_ie(unsigned char *data, unsigned char ie_type,
		struct rda5890_bss_descriptor *bss, size_t *len)
{
	unsigned char index = 0;
	unsigned char *oui;

	if (ie_type == IRSNELEMENT) {
		oui = &oui_rsn[0];

		/* Set RSN Information Element element ID */
		data[index] = IRSNELEMENT;
		index += 2;
	}
	else {
		oui = &oui_wpa[0];

		/* Set WPA Information Element element ID */
		data[index] = IWPAELEMENT;
		index += 2;

		/* Copy OUI */
		memcpy(&data[index], oui, 3);
		index += 3;
		data[index++] = 0x01;
	}

	/* Set the version of RSN Element to 1 */
	data[index++] = 1;
	data[index++] = 0;

	/* Set Group Cipher Suite */
	memcpy(&data[index], oui, 3);
	index += 3;
	if ((bss->dot11i_info & BIT5) && !(bss->dot11i_info & BIT6)) {
		/* only CCMP and !TKIP, use CCMP, otherwise, always TKIP */
		data[index++] = CIPHER_TYPE_CCMP;
	}
	else {
		data[index++] = CIPHER_TYPE_TKIP;
	}

	/* Set Pairwise cipher Suite */
	if ((bss->dot11i_info & BIT5) && (bss->dot11i_info & BIT6)) {
		/* both CCMP and TKIP */
		data[index++] = 2;
		data[index++] = 0;

		/* Check BIT7 to determine who goes first */
		if (bss->dot11i_info & BIT7) {
			/* BIT7 is 1 => CCMP goes first */
			memcpy(&data[index], oui, 3);
			index += 3;
			data[index++] = CIPHER_TYPE_CCMP;

			memcpy(&data[index], oui, 3);
			index += 3;
			data[index++] = CIPHER_TYPE_TKIP;
		}
		else {
		/* BIT7 is 0 => TKIP goes first */
			memcpy(&data[index], oui, 3);
			index += 3;
			data[index++] = CIPHER_TYPE_TKIP;

			memcpy(&data[index], oui, 3);
			index += 3;
			data[index++] = CIPHER_TYPE_CCMP;
		}
	}
	else if ((bss->dot11i_info & BIT5) && !(bss->dot11i_info & BIT6)) {
		/* CCMP and !TKIP */
		data[index++] = 1;
		data[index++] = 0;

		memcpy(&data[index], oui, 3);
		index += 3;
		data[index++] = CIPHER_TYPE_CCMP;
	}
	else if (!(bss->dot11i_info & BIT5) && (bss->dot11i_info & BIT6)) {
		/* !CCMP and TKIP */
		data[index++] = 1;
		data[index++] = 0;

		memcpy(&data[index], oui, 3);
		index += 3;
		data[index++] = CIPHER_TYPE_TKIP;
	}
	else {
		/* neither CCMP nor TKIP, use TKIP for WPA, and CCMP for RSN */
		data[index++] = 1;
		data[index++] = 0;

		memcpy(&data[index], oui, 3);
		index += 3;
		if (ie_type == IRSNELEMENT) {
			data[index++] = CIPHER_TYPE_CCMP;
		}
		else {
			data[index++] = CIPHER_TYPE_TKIP;
		}
	}

	/* Set Authentication Suite */
	if ((bss->auth_info & 0x01) && (bss->auth_info & 0x02)) {
		/* both 802.1X and PSK */
		data[index++] = 2;
		data[index++] = 0;

		memcpy(&data[index], oui, 3);
		index += 3;
		data[index++] = 0x01;

		memcpy(&data[index], oui, 3);
		index += 3;
		data[index++] = 0x02;
	}
	else if ((bss->auth_info & 0x01) && !(bss->auth_info & 0x02)) {
		/* 802.1X and !PSK */
		data[index++] = 1;
		data[index++] = 0;

		memcpy(&data[index], oui, 3);
		index += 3;
		data[index++] = 0x01;
	}
	else if (!(bss->auth_info & 0x01) && (bss->auth_info & 0x02)) {
		/* !802.1X and PSK */
		data[index++] = 1;
		data[index++] = 0;

		memcpy(&data[index], oui, 3);
		index += 3;
		data[index++] = 0x02;
	}
	else {
		/* neither 802.1X nor PSK, use 802.1X */
		data[index++] = 1;
		data[index++] = 0;

		memcpy(&data[index], oui, 3);
		index += 3;
		data[index++] = 0x01;
	}

	/* The RSN Capabilities, for RSN IE only */
	if (ie_type == IRSNELEMENT) {
		data[index++] = bss->rsn_cap[0];
		data[index++] = bss->rsn_cap[1];
	}

	/* Set the length of the RSN Information Element */
	data[1] = (index - 2);

	/* Return the Extended Supported Rates element length */
	*len = (size_t)index;
}

/* reconstruct wpa/rsn ie from the dot11i_info and auth_info fields */
/* TODO: 
 * assuming RSN and WPA are using same cipher suite, no space to store each
 * assuming grp and unicast are using same cipher suite
 */
static void reconstruct_rsn_wpa_ie(struct bss_descriptor *bss_desc)
{
	bss_desc->wpa_ie_len = 0;
	bss_desc->rsn_ie_len = 0;
	
	if (bss_desc->data.dot11i_info & BIT0) {
		if (bss_desc->data.dot11i_info & BIT3) {
			/* WPA IE present */
			fill_rsn_wpa_ie(&bss_desc->wpa_ie[0], IWPAELEMENT, 
				&bss_desc->data, &bss_desc->wpa_ie_len);
		}

		if (bss_desc->data.dot11i_info & BIT4) {
			/* RSN IE present */
			fill_rsn_wpa_ie(&bss_desc->rsn_ie[0], IRSNELEMENT, 
				&bss_desc->data, &bss_desc->rsn_ie_len);			
		}
	}
	else {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"form_rsn_ie, NO SEC\n");
	}
}

void rda5890_scan_worker(struct work_struct *work)
{
	struct rda5890_private *priv = container_of(work, struct rda5890_private,
		scan_work.work);
	int ret = 0;
	struct rda5890_bss_descriptor bss_desc[RDA5890_MAX_NETWORK_NUM];
	int bss_index, bss_count;
	struct bss_descriptor *iter_bss;
	union iwreq_data wrqu;
    unsigned char fist_send = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

    if(test_bit(ASSOC_FLAG_ASSOC_START, &priv->assoc_flags)
        || test_bit(ASSOC_FLAG_WLAN_CONNECTING, &priv->assoc_flags))
    {
        cancel_delayed_work(&priv->scan_work);
        queue_delayed_work(priv->work_thread, &priv->scan_work, HZ/2);
        return;
    }

	priv->scan_running = 1;
    
#ifdef WIFI_UNLOCK_SYSTEM
    rda5990_wakeLock();
#endif

#ifdef GET_SCAN_FROM_NETWORK_INFO
	ret = rda5890_start_scan_enable_network_info(priv);
#else
    ret = rda5890_start_scan(priv);
#endif 
	if (ret) {
		RDA5890_ERRP("rda5890_start_scan fail, ret = %d\n", ret);
		goto out;
	}

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "SCANNING ...\n");
	/* TODO: delay 2 sec for now, need to be put into a worker */
	rda5890_shedule_timeout(1500);

#ifndef GET_SCAN_FROM_NETWORK_INFO
retry:
	bss_count = rda5890_get_scan_results(priv, bss_desc);

    fist_send = (bss_count >> 8) & 0xff;
    bss_count &= 0xff;
	if (bss_count < 0 || bss_count >= RDA5890_MAX_NETWORK_NUM) {
		RDA5890_ERRP("rda5890_get_scan_results fail, ret = %d\n", bss_count);
		goto out;
	}
	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"Get Scan Result, count = %d, fist_send= %d \n", bss_count, fist_send);

	/* add scaned bss into list */
	for(bss_index = 0; bss_index < bss_count; bss_index++) {
		struct bss_descriptor new;
		struct bss_descriptor *found = NULL;
		struct bss_descriptor *oldest = NULL;

    	if (RDA5890_DBGLA(RDA5890_DA_WEXT, RDA5890_DL_TRACE))
	    	dump_bss_desc(&bss_desc[bss_index]);

		memcpy(&new.data, &bss_desc[bss_index], sizeof(struct rda5890_bss_descriptor));
		reconstruct_rsn_wpa_ie(&new);
		new.last_scanned = jiffies;

		/* Try to find this bss in the scan table */
		list_for_each_entry (iter_bss, &priv->network_list, list) {
			if (is_same_network(iter_bss, &new)) {
				found = iter_bss;
				break;
			}

			if ((oldest == NULL) ||
			    (iter_bss->last_scanned < oldest->last_scanned))
				oldest = iter_bss;
		}

		if (found) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"FOUND SAME %s, update\n", found->data.ssid);
			/* found, clear it */
			clear_bss_descriptor(found);
		} else if (!list_empty(&priv->network_free_list)) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"FOUND NEW %s, add\n", new.data.ssid);
			/* Pull one from the free list */
			found = list_entry(priv->network_free_list.next,
					   struct bss_descriptor, list);
			list_move_tail(&found->list, &priv->network_list);
		} else if (oldest) {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"FOUND NEW %s, no space, replace oldest %s\n", 
				new.data.ssid, oldest->data.ssid);
			/* If there are no more slots, expire the oldest */
			found = oldest;
			clear_bss_descriptor(found);
			list_move_tail(&found->list, &priv->network_list);
		} else {
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"FOUND NEW but no space to store\n");
		}

		/* Copy the locally created newbssentry to the scan table */
		memcpy(found, &new, offsetof(struct bss_descriptor, list));
	}

    if(bss_count >= 5 && !fist_send)
        goto retry;

#else
    //do noting in get network info modle

#endif

out:
    priv->scan_running = 0;
    memset(&wrqu, 0, sizeof(union iwreq_data));
    wireless_send_event(priv->dev, SIOCGIWSCAN, &wrqu, NULL);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);

#ifdef WIFI_UNLOCK_SYSTEM
    rda5990_wakeUnlock();
#endif
}

/**
 *  @brief Handle Scan Network ioctl
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param vwrq         A pointer to iw_param structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success, otherwise fail
 */
int rda5890_set_scan(struct net_device *dev, struct iw_request_info *info,
		 union iwreq_data *wrqu, char *extra)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	if (priv->scan_running)
		goto out;


    cancel_delayed_work(&priv->scan_work);
    queue_delayed_work(priv->work_thread, &priv->scan_work, HZ/50);

out:
	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return 0;
}

/**
 *  @brief  Handle Retrieve scan table ioctl
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param dwrq         A pointer to iw_point structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success, otherwise fail
 */
int rda5890_get_scan(struct net_device *dev, struct iw_request_info *info,
		 struct iw_point *dwrq, char *extra)
{
#define SCAN_ITEM_SIZE 128
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = 0;
	struct bss_descriptor *iter_bss;
	struct bss_descriptor *safe;
	char *ev = extra;
	char *stop = ev + dwrq->length;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	/* iwlist should wait until the current scan is finished */
	if (priv->scan_running) {
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"Scan is Running, return AGAIN\n");
		return -EAGAIN;
	}

	/* report all bss to upper layer */
	list_for_each_entry_safe (iter_bss, safe, &priv->network_list, list) {
		char *next_ev;
		unsigned long stale_time;

		if (stop - ev < SCAN_ITEM_SIZE) {
			ret = -E2BIG;
			break;
		}

		/* Prune old an old scan result */
		stale_time = iter_bss->last_scanned + DEFAULT_MAX_SCAN_AGE;
		if (time_after(jiffies, stale_time)) {
			list_move_tail(&iter_bss->list, &priv->network_free_list);
#ifdef GET_SCAN_FROM_NETWORK_INFO              
			RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"Prune Old Bss %s\n", iter_bss->ssid);
#else
            RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
				"Prune Old Bss %s\n", iter_bss->ssid);
#endif
			clear_bss_descriptor(iter_bss);
			continue;
		}

		/* Translate to WE format this entry */
		next_ev = translate_scan(priv, info, ev, stop, iter_bss);
#ifdef GET_SCAN_FROM_NETWORK_INFO        
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"Report BSS %s\n", iter_bss->ssid);
#else
        RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
            "Report BSS %s\n", iter_bss->data.ssid);
#endif
		if (next_ev == NULL)
			continue;
		ev = next_ev;
	}
	dwrq->length = (ev - extra);
	dwrq->flags = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return ret;
}

int rda5890_set_mlme(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *wrqu, char *extra)
{
	//struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	int ret = 0;

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s >>>\n", __func__);

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"DEAUTH\n");
		/* silently ignore */
		break;

	case IW_MLME_DISASSOC:
	{
		unsigned char ssid[6];
		memset(ssid, 0, 6);
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"DISASSOC\n");
		/* silently ignore */
		rda5890_set_ssid((struct rda5890_private *)netdev_priv(dev) , ssid, 6);
	}
		break;
	default:
		RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, 
			"Not supported cmd %d\n", mlme->cmd);
		ret = -EOPNOTSUPP;
	}

	RDA5890_DBGLAP(RDA5890_DA_WEXT, RDA5890_DL_TRACE, "%s <<<\n", __func__);
	return ret;
}

/*
 * iwconfig settable callbacks
 */
static const iw_handler rda5890_wext_handler[] = {
	(iw_handler) NULL,	/* SIOCSIWCOMMIT */
	(iw_handler) rda5890_get_name,	/* SIOCGIWNAME */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) rda5890_set_freq,	/* SIOCSIWFREQ */
	(iw_handler) rda5890_get_freq,	/* SIOCGIWFREQ */
	(iw_handler) rda5890_set_mode,	/* SIOCSIWMODE */
	(iw_handler) rda5890_get_mode,	/* SIOCGIWMODE */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
	(iw_handler) NULL,	/* SIOCSIWRANGE */
	(iw_handler) rda5890_get_range,	/* SIOCGIWRANGE */
	(iw_handler) NULL,	/* SIOCSIWPRIV */
	(iw_handler) NULL,	/* SIOCGIWPRIV */
	(iw_handler) NULL,	/* SIOCSIWSTATS */
	(iw_handler) NULL,	/* SIOCGIWSTATS */
	(iw_handler) NULL,	/* SIOCSIWSPY */
	(iw_handler) NULL,	/* SIOCGIWSPY */
	(iw_handler) NULL,	/* SIOCSIWTHRSPY */
	(iw_handler) NULL,	/* SIOCGIWTHRSPY */
	(iw_handler) rda5890_set_wap,	/* SIOCSIWAP */
	(iw_handler) rda5890_get_wap,	/* SIOCGIWAP */
	(iw_handler) rda5890_set_mlme,	/* SIOCSIWMLME */
	(iw_handler) NULL,	/* SIOCGIWAPLIST - deprecated */
	(iw_handler) rda5890_set_scan,	/* SIOCSIWSCAN */
	(iw_handler) rda5890_get_scan,	/* SIOCGIWSCAN */
	(iw_handler) rda5890_set_essid,	/* SIOCSIWESSID */
	(iw_handler) rda5890_get_essid,	/* SIOCGIWESSID */
	(iw_handler) rda5890_set_nick,	/* SIOCSIWNICKN */
	(iw_handler) rda5890_get_nick,	/* SIOCGIWNICKN */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) rda5890_set_rate,	/* SIOCSIWRATE */
	(iw_handler) rda5890_get_rate,	/* SIOCGIWRATE */
	(iw_handler) rda5890_set_rts,	/* SIOCSIWRTS */
	(iw_handler) rda5890_get_rts,	/* SIOCGIWRTS */
	(iw_handler) rda5890_set_frag,	/* SIOCSIWFRAG */
	(iw_handler) rda5890_get_frag,	/* SIOCGIWFRAG */
	(iw_handler) rda5890_set_txpow,	/* SIOCSIWTXPOW */
	(iw_handler) rda5890_get_txpow,	/* SIOCGIWTXPOW */
	(iw_handler) rda5890_set_retry,	/* SIOCSIWRETRY */
	(iw_handler) rda5890_get_retry,	/* SIOCGIWRETRY */
	(iw_handler) rda5890_set_encode,	/* SIOCSIWENCODE */
	(iw_handler) rda5890_get_encode,	/* SIOCGIWENCODE */
	(iw_handler) rda5890_set_power,	/* SIOCSIWPOWER */
	(iw_handler) rda5890_get_power,	/* SIOCGIWPOWER */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) rda5890_set_genie,	/* SIOCSIWGENIE */
	(iw_handler) rda5890_get_genie,	/* SIOCGIWGENIE */
	(iw_handler) rda5890_set_auth,	/* SIOCSIWAUTH */
	(iw_handler) rda5890_get_auth,	/* SIOCGIWAUTH */
	(iw_handler) rda5890_set_encodeext,/* SIOCSIWENCODEEXT */
	(iw_handler) rda5890_get_encodeext,/* SIOCGIWENCODEEXT */
	(iw_handler) rda5890_set_pmksa,		/* SIOCSIWPMKSA */
};

struct iw_handler_def rda5890_wext_handler_def = {
	.num_standard	= ARRAY_SIZE(rda5890_wext_handler),
	.standard	= (iw_handler *) rda5890_wext_handler,
	.get_wireless_stats = rda5890_get_wireless_stats,
};

