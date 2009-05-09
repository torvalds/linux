/* Wireless extensions support.
 *
 * See copyright notice in main.c
 */
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/iw_handler.h>

#include "hermes.h"
#include "hermes_rid.h"
#include "orinoco.h"

#include "hw.h"
#include "mic.h"
#include "scan.h"
#include "main.h"

#include "wext.h"

#define MAX_RID_LEN 1024

static struct iw_statistics *orinoco_get_wireless_stats(struct net_device *dev)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	struct iw_statistics *wstats = &priv->wstats;
	int err;
	unsigned long flags;

	if (!netif_device_present(dev)) {
		printk(KERN_WARNING "%s: get_wireless_stats() called while device not present\n",
		       dev->name);
		return NULL; /* FIXME: Can we do better than this? */
	}

	/* If busy, return the old stats.  Returning NULL may cause
	 * the interface to disappear from /proc/net/wireless */
	if (orinoco_lock(priv, &flags) != 0)
		return wstats;

	/* We can't really wait for the tallies inquiry command to
	 * complete, so we just use the previous results and trigger
	 * a new tallies inquiry command for next time - Jean II */
	/* FIXME: Really we should wait for the inquiry to come back -
	 * as it is the stats we give don't make a whole lot of sense.
	 * Unfortunately, it's not clear how to do that within the
	 * wireless extensions framework: I think we're in user
	 * context, but a lock seems to be held by the time we get in
	 * here so we're not safe to sleep here. */
	hermes_inquire(hw, HERMES_INQ_TALLIES);

	if (priv->iw_mode == IW_MODE_ADHOC) {
		memset(&wstats->qual, 0, sizeof(wstats->qual));
		/* If a spy address is defined, we report stats of the
		 * first spy address - Jean II */
		if (SPY_NUMBER(priv)) {
			wstats->qual.qual = priv->spy_data.spy_stat[0].qual;
			wstats->qual.level = priv->spy_data.spy_stat[0].level;
			wstats->qual.noise = priv->spy_data.spy_stat[0].noise;
			wstats->qual.updated =
				priv->spy_data.spy_stat[0].updated;
		}
	} else {
		struct {
			__le16 qual, signal, noise, unused;
		} __attribute__ ((packed)) cq;

		err = HERMES_READ_RECORD(hw, USER_BAP,
					 HERMES_RID_COMMSQUALITY, &cq);

		if (!err) {
			wstats->qual.qual = (int)le16_to_cpu(cq.qual);
			wstats->qual.level = (int)le16_to_cpu(cq.signal) - 0x95;
			wstats->qual.noise = (int)le16_to_cpu(cq.noise) - 0x95;
			wstats->qual.updated =
				IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
		}
	}

	orinoco_unlock(priv, &flags);
	return wstats;
}

/********************************************************************/
/* Wireless extensions                                              */
/********************************************************************/

static int orinoco_ioctl_getname(struct net_device *dev,
				 struct iw_request_info *info,
				 char *name,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int numrates;
	int err;

	err = orinoco_hw_get_bitratelist(priv, &numrates, NULL, 0);

	if (!err && (numrates > 2))
		strcpy(name, "IEEE 802.11b");
	else
		strcpy(name, "IEEE 802.11-DS");

	return 0;
}

static int orinoco_ioctl_setwap(struct net_device *dev,
				struct iw_request_info *info,
				struct sockaddr *ap_addr,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;
	static const u8 off_addr[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const u8 any_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* Enable automatic roaming - no sanity checks are needed */
	if (memcmp(&ap_addr->sa_data, off_addr, ETH_ALEN) == 0 ||
	    memcmp(&ap_addr->sa_data, any_addr, ETH_ALEN) == 0) {
		priv->bssid_fixed = 0;
		memset(priv->desired_bssid, 0, ETH_ALEN);

		/* "off" means keep existing connection */
		if (ap_addr->sa_data[0] == 0) {
			__orinoco_hw_set_wap(priv);
			err = 0;
		}
		goto out;
	}

	if (priv->firmware_type == FIRMWARE_TYPE_AGERE) {
		printk(KERN_WARNING "%s: Lucent/Agere firmware doesn't "
		       "support manual roaming\n",
		       dev->name);
		err = -EOPNOTSUPP;
		goto out;
	}

	if (priv->iw_mode != IW_MODE_INFRA) {
		printk(KERN_WARNING "%s: Manual roaming supported only in "
		       "managed mode\n", dev->name);
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Intersil firmware hangs without Desired ESSID */
	if (priv->firmware_type == FIRMWARE_TYPE_INTERSIL &&
	    strlen(priv->desired_essid) == 0) {
		printk(KERN_WARNING "%s: Desired ESSID must be set for "
		       "manual roaming\n", dev->name);
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Finally, enable manual roaming */
	priv->bssid_fixed = 1;
	memcpy(priv->desired_bssid, &ap_addr->sa_data, ETH_ALEN);

 out:
	orinoco_unlock(priv, &flags);
	return err;
}

static int orinoco_ioctl_getwap(struct net_device *dev,
				struct iw_request_info *info,
				struct sockaddr *ap_addr,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);

	hermes_t *hw = &priv->hw;
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	ap_addr->sa_family = ARPHRD_ETHER;
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENTBSSID,
			      ETH_ALEN, NULL, ap_addr->sa_data);

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_setmode(struct net_device *dev,
				 struct iw_request_info *info,
				 u32 *mode,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (priv->iw_mode == *mode)
		return 0;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	switch (*mode) {
	case IW_MODE_ADHOC:
		if (!priv->has_ibss && !priv->has_port3)
			err = -EOPNOTSUPP;
		break;

	case IW_MODE_INFRA:
		break;

	case IW_MODE_MONITOR:
		if (priv->broken_monitor && !force_monitor) {
			printk(KERN_WARNING "%s: Monitor mode support is "
			       "buggy in this firmware, not enabling\n",
			       dev->name);
			err = -EOPNOTSUPP;
		}
		break;

	default:
		err = -EOPNOTSUPP;
		break;
	}

	if (err == -EINPROGRESS) {
		priv->iw_mode = *mode;
		set_port_type(priv);
	}

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getmode(struct net_device *dev,
				 struct iw_request_info *info,
				 u32 *mode,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);

	*mode = priv->iw_mode;
	return 0;
}

static int orinoco_ioctl_getiwrange(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *rrq,
				    char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	struct iw_range *range = (struct iw_range *) extra;
	int numrates;
	int i, k;

	rrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 22;

	/* Set available channels/frequencies */
	range->num_channels = NUM_CHANNELS;
	k = 0;
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (priv->channel_mask & (1 << i)) {
			range->freq[k].i = i + 1;
			range->freq[k].m = (ieee80211_dsss_chan_to_freq(i + 1) *
					    100000);
			range->freq[k].e = 1;
			k++;
		}

		if (k >= IW_MAX_FREQUENCIES)
			break;
	}
	range->num_frequency = k;
	range->sensitivity = 3;

	if (priv->has_wep) {
		range->max_encoding_tokens = ORINOCO_MAX_KEYS;
		range->encoding_size[0] = SMALL_KEY_SIZE;
		range->num_encoding_sizes = 1;

		if (priv->has_big_wep) {
			range->encoding_size[1] = LARGE_KEY_SIZE;
			range->num_encoding_sizes = 2;
		}
	}

	if (priv->has_wpa)
		range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_CIPHER_TKIP;

	if ((priv->iw_mode == IW_MODE_ADHOC) && (!SPY_NUMBER(priv))) {
		/* Quality stats meaningless in ad-hoc mode */
	} else {
		range->max_qual.qual = 0x8b - 0x2f;
		range->max_qual.level = 0x2f - 0x95 - 1;
		range->max_qual.noise = 0x2f - 0x95 - 1;
		/* Need to get better values */
		range->avg_qual.qual = 0x24;
		range->avg_qual.level = 0xC2;
		range->avg_qual.noise = 0x9E;
	}

	err = orinoco_hw_get_bitratelist(priv, &numrates,
					 range->bitrate, IW_MAX_BITRATES);
	if (err)
		return err;
	range->num_bitrates = numrates;

	/* Set an indication of the max TCP throughput in bit/s that we can
	 * expect using this interface. May be use for QoS stuff...
	 * Jean II */
	if (numrates > 2)
		range->throughput = 5 * 1000 * 1000;	/* ~5 Mb/s */
	else
		range->throughput = 1.5 * 1000 * 1000;	/* ~1.5 Mb/s */

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->min_pmp = 0;
	range->max_pmp = 65535000;
	range->min_pmt = 0;
	range->max_pmt = 65535 * 1000;	/* ??? */
	range->pmp_flags = IW_POWER_PERIOD;
	range->pmt_flags = IW_POWER_TIMEOUT;
	range->pm_capa = (IW_POWER_PERIOD | IW_POWER_TIMEOUT |
			  IW_POWER_UNICAST_R);

	range->retry_capa = IW_RETRY_LIMIT | IW_RETRY_LIFETIME;
	range->retry_flags = IW_RETRY_LIMIT;
	range->r_time_flags = IW_RETRY_LIFETIME;
	range->min_retry = 0;
	range->max_retry = 65535;	/* ??? */
	range->min_r_time = 0;
	range->max_r_time = 65535 * 1000;	/* ??? */

	if (priv->firmware_type == FIRMWARE_TYPE_AGERE)
		range->scan_capa = IW_SCAN_CAPA_ESSID;
	else
		range->scan_capa = IW_SCAN_CAPA_NONE;

	/* Event capability (kernel) */
	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	/* Event capability (driver) */
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWTHRSPY);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);
	IW_EVENT_CAPA_SET(range->event_capa, IWEVTXDROP);

	return 0;
}

static int orinoco_ioctl_setiwencode(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq,
				     char *keybuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	int setindex = priv->tx_key;
	int encode_alg = priv->encode_alg;
	int restricted = priv->wep_restrict;
	u16 xlen = 0;
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (!priv->has_wep)
		return -EOPNOTSUPP;

	if (erq->pointer) {
		/* We actually have a key to set - check its length */
		if (erq->length > LARGE_KEY_SIZE)
			return -E2BIG;

		if ((erq->length > SMALL_KEY_SIZE) && !priv->has_big_wep)
			return -E2BIG;
	}

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* Clear any TKIP key we have */
	if ((priv->has_wpa) && (priv->encode_alg == IW_ENCODE_ALG_TKIP))
		(void) orinoco_clear_tkip_key(priv, setindex);

	if (erq->length > 0) {
		if ((index < 0) || (index >= ORINOCO_MAX_KEYS))
			index = priv->tx_key;

		/* Adjust key length to a supported value */
		if (erq->length > SMALL_KEY_SIZE)
			xlen = LARGE_KEY_SIZE;
		else if (erq->length > 0)
			xlen = SMALL_KEY_SIZE;
		else
			xlen = 0;

		/* Switch on WEP if off */
		if ((encode_alg != IW_ENCODE_ALG_WEP) && (xlen > 0)) {
			setindex = index;
			encode_alg = IW_ENCODE_ALG_WEP;
		}
	} else {
		/* Important note : if the user do "iwconfig eth0 enc off",
		 * we will arrive there with an index of -1. This is valid
		 * but need to be taken care off... Jean II */
		if ((index < 0) || (index >= ORINOCO_MAX_KEYS)) {
			if ((index != -1) || (erq->flags == 0)) {
				err = -EINVAL;
				goto out;
			}
		} else {
			/* Set the index : Check that the key is valid */
			if (priv->keys[index].len == 0) {
				err = -EINVAL;
				goto out;
			}
			setindex = index;
		}
	}

	if (erq->flags & IW_ENCODE_DISABLED)
		encode_alg = IW_ENCODE_ALG_NONE;
	if (erq->flags & IW_ENCODE_OPEN)
		restricted = 0;
	if (erq->flags & IW_ENCODE_RESTRICTED)
		restricted = 1;

	if (erq->pointer && erq->length > 0) {
		priv->keys[index].len = cpu_to_le16(xlen);
		memset(priv->keys[index].data, 0,
		       sizeof(priv->keys[index].data));
		memcpy(priv->keys[index].data, keybuf, erq->length);
	}
	priv->tx_key = setindex;

	/* Try fast key change if connected and only keys are changed */
	if ((priv->encode_alg == encode_alg) &&
	    (priv->wep_restrict == restricted) &&
	    netif_carrier_ok(dev)) {
		err = __orinoco_hw_setup_wepkeys(priv);
		/* No need to commit if successful */
		goto out;
	}

	priv->encode_alg = encode_alg;
	priv->wep_restrict = restricted;

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getiwencode(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq,
				     char *keybuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	u16 xlen = 0;
	unsigned long flags;

	if (!priv->has_wep)
		return -EOPNOTSUPP;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if ((index < 0) || (index >= ORINOCO_MAX_KEYS))
		index = priv->tx_key;

	erq->flags = 0;
	if (!priv->encode_alg)
		erq->flags |= IW_ENCODE_DISABLED;
	erq->flags |= index + 1;

	if (priv->wep_restrict)
		erq->flags |= IW_ENCODE_RESTRICTED;
	else
		erq->flags |= IW_ENCODE_OPEN;

	xlen = le16_to_cpu(priv->keys[index].len);

	erq->length = xlen;

	memcpy(keybuf, priv->keys[index].data, ORINOCO_MAX_KEY_SIZE);

	orinoco_unlock(priv, &flags);
	return 0;
}

static int orinoco_ioctl_setessid(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *erq,
				  char *essidbuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	/* Note : ESSID is ignored in Ad-Hoc demo mode, but we can set it
	 * anyway... - Jean II */

	/* Hum... Should not use Wireless Extension constant (may change),
	 * should use our own... - Jean II */
	if (erq->length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* NULL the string (for NULL termination & ESSID = ANY) - Jean II */
	memset(priv->desired_essid, 0, sizeof(priv->desired_essid));

	/* If not ANY, get the new ESSID */
	if (erq->flags)
		memcpy(priv->desired_essid, essidbuf, erq->length);

	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getessid(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *erq,
				  char *essidbuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int active;
	int err = 0;
	unsigned long flags;

	if (netif_running(dev)) {
		err = orinoco_hw_get_essid(priv, &active, essidbuf);
		if (err < 0)
			return err;
		erq->length = err;
	} else {
		if (orinoco_lock(priv, &flags) != 0)
			return -EBUSY;
		memcpy(essidbuf, priv->desired_essid, IW_ESSID_MAX_SIZE);
		erq->length = strlen(priv->desired_essid);
		orinoco_unlock(priv, &flags);
	}

	erq->flags = 1;

	return 0;
}

static int orinoco_ioctl_setnick(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *nrq,
				 char *nickbuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	if (nrq->length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	memset(priv->nick, 0, sizeof(priv->nick));
	memcpy(priv->nick, nickbuf, nrq->length);

	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getnick(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *nrq,
				 char *nickbuf)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	memcpy(nickbuf, priv->nick, IW_ESSID_MAX_SIZE);
	orinoco_unlock(priv, &flags);

	nrq->length = strlen(priv->nick);

	return 0;
}

static int orinoco_ioctl_setfreq(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_freq *frq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int chan = -1;
	unsigned long flags;
	int err = -EINPROGRESS;		/* Call commit handler */

	/* In infrastructure mode the AP sets the channel */
	if (priv->iw_mode == IW_MODE_INFRA)
		return -EBUSY;

	if ((frq->e == 0) && (frq->m <= 1000)) {
		/* Setting by channel number */
		chan = frq->m;
	} else {
		/* Setting by frequency */
		int denom = 1;
		int i;

		/* Calculate denominator to rescale to MHz */
		for (i = 0; i < (6 - frq->e); i++)
			denom *= 10;

		chan = ieee80211_freq_to_dsss_chan(frq->m / denom);
	}

	if ((chan < 1) || (chan > NUM_CHANNELS) ||
	     !(priv->channel_mask & (1 << (chan-1))))
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->channel = chan;
	if (priv->iw_mode == IW_MODE_MONITOR) {
		/* Fast channel change - no commit if successful */
		hermes_t *hw = &priv->hw;
		err = hermes_docmd_wait(hw, HERMES_CMD_TEST |
					    HERMES_TEST_SET_CHANNEL,
					chan, NULL);
	}
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getfreq(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_freq *frq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int tmp;

	/* Locking done in there */
	tmp = orinoco_hw_get_freq(priv);
	if (tmp < 0)
		return tmp;

	frq->m = tmp * 100000;
	frq->e = 1;

	return 0;
}

static int orinoco_ioctl_getsens(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *srq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	u16 val;
	int err;
	unsigned long flags;

	if (!priv->has_sensitivity)
		return -EOPNOTSUPP;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFSYSTEMSCALE, &val);
	orinoco_unlock(priv, &flags);

	if (err)
		return err;

	srq->value = val;
	srq->fixed = 0; /* auto */

	return 0;
}

static int orinoco_ioctl_setsens(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *srq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = srq->value;
	unsigned long flags;

	if (!priv->has_sensitivity)
		return -EOPNOTSUPP;

	if ((val < 1) || (val > 3))
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	priv->ap_density = val;
	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_setrts(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *rrq,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = rrq->value;
	unsigned long flags;

	if (rrq->disabled)
		val = 2347;

	if ((val < 0) || (val > 2347))
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->rts_thresh = val;
	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getrts(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *rrq,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);

	rrq->value = priv->rts_thresh;
	rrq->disabled = (rrq->value == 2347);
	rrq->fixed = 1;

	return 0;
}

static int orinoco_ioctl_setfrag(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *frq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (priv->has_mwo) {
		if (frq->disabled)
			priv->mwo_robust = 0;
		else {
			if (frq->fixed)
				printk(KERN_WARNING "%s: Fixed fragmentation "
				       "is not supported on this firmware. "
				       "Using MWO robust instead.\n",
				       dev->name);
			priv->mwo_robust = 1;
		}
	} else {
		if (frq->disabled)
			priv->frag_thresh = 2346;
		else {
			if ((frq->value < 256) || (frq->value > 2346))
				err = -EINVAL;
			else
				/* must be even */
				priv->frag_thresh = frq->value & ~0x1;
		}
	}

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getfrag(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *frq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err;
	u16 val;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (priv->has_mwo) {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMWOROBUST_AGERE,
					  &val);
		if (err)
			val = 0;

		frq->value = val ? 2347 : 0;
		frq->disabled = !val;
		frq->fixed = 0;
	} else {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					  &val);
		if (err)
			val = 0;

		frq->value = val;
		frq->disabled = (val >= 2346);
		frq->fixed = 1;
	}

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_setrate(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int ratemode;
	int bitrate; /* 100s of kilobits */
	unsigned long flags;

	/* As the user space doesn't know our highest rate, it uses -1
	 * to ask us to set the highest rate.  Test it using "iwconfig
	 * ethX rate auto" - Jean II */
	if (rrq->value == -1)
		bitrate = 110;
	else {
		if (rrq->value % 100000)
			return -EINVAL;
		bitrate = rrq->value / 100000;
	}

	ratemode = orinoco_get_bitratemode(bitrate, !rrq->fixed);

	if (ratemode == -1)
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;
	priv->bitratemode = ratemode;
	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;
}

static int orinoco_ioctl_getrate(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	int bitrate, automatic;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	orinoco_get_ratemode_cfg(priv->bitratemode, &bitrate, &automatic);

	/* If the interface is running we try to find more about the
	   current mode */
	if (netif_running(dev))
		err = orinoco_hw_get_act_bitrate(priv, &bitrate);

	orinoco_unlock(priv, &flags);

	rrq->value = bitrate;
	rrq->fixed = !automatic;
	rrq->disabled = 0;

	return err;
}

static int orinoco_ioctl_setpower(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *prq,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (prq->disabled) {
		priv->pm_on = 0;
	} else {
		switch (prq->flags & IW_POWER_MODE) {
		case IW_POWER_UNICAST_R:
			priv->pm_mcast = 0;
			priv->pm_on = 1;
			break;
		case IW_POWER_ALL_R:
			priv->pm_mcast = 1;
			priv->pm_on = 1;
			break;
		case IW_POWER_ON:
			/* No flags : but we may have a value - Jean II */
			break;
		default:
			err = -EINVAL;
			goto out;
		}

		if (prq->flags & IW_POWER_TIMEOUT) {
			priv->pm_on = 1;
			priv->pm_timeout = prq->value / 1000;
		}
		if (prq->flags & IW_POWER_PERIOD) {
			priv->pm_on = 1;
			priv->pm_period = prq->value / 1000;
		}
		/* It's valid to not have a value if we are just toggling
		 * the flags... Jean II */
		if (!priv->pm_on) {
			err = -EINVAL;
			goto out;
		}
	}

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getpower(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *prq,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 enable, period, timeout, mcast;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFPMENABLED, &enable);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFMAXSLEEPDURATION, &period);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFPMHOLDOVERDURATION, &timeout);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFMULTICASTRECEIVE, &mcast);
	if (err)
		goto out;

	prq->disabled = !enable;
	/* Note : by default, display the period */
	if ((prq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		prq->flags = IW_POWER_TIMEOUT;
		prq->value = timeout * 1000;
	} else {
		prq->flags = IW_POWER_PERIOD;
		prq->value = period * 1000;
	}
	if (mcast)
		prq->flags |= IW_POWER_ALL_R;
	else
		prq->flags |= IW_POWER_UNICAST_R;

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_set_encodeext(struct net_device *dev,
				       struct iw_request_info *info,
				       union iwreq_data *wrqu,
				       char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int idx, alg = ext->alg, set_key = 1;
	unsigned long flags;
	int err = -EINVAL;
	u16 key_len;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* Determine and validate the key index */
	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if ((idx < 1) || (idx > 4))
			goto out;
		idx--;
	} else
		idx = priv->tx_key;

	if (encoding->flags & IW_ENCODE_DISABLED)
		alg = IW_ENCODE_ALG_NONE;

	if (priv->has_wpa && (alg != IW_ENCODE_ALG_TKIP)) {
		/* Clear any TKIP TX key we had */
		(void) orinoco_clear_tkip_key(priv, priv->tx_key);
	}

	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
		priv->tx_key = idx;
		set_key = ((alg == IW_ENCODE_ALG_TKIP) ||
			   (ext->key_len > 0)) ? 1 : 0;
	}

	if (set_key) {
		/* Set the requested key first */
		switch (alg) {
		case IW_ENCODE_ALG_NONE:
			priv->encode_alg = alg;
			priv->keys[idx].len = 0;
			break;

		case IW_ENCODE_ALG_WEP:
			if (ext->key_len > SMALL_KEY_SIZE)
				key_len = LARGE_KEY_SIZE;
			else if (ext->key_len > 0)
				key_len = SMALL_KEY_SIZE;
			else
				goto out;

			priv->encode_alg = alg;
			priv->keys[idx].len = cpu_to_le16(key_len);

			key_len = min(ext->key_len, key_len);

			memset(priv->keys[idx].data, 0, ORINOCO_MAX_KEY_SIZE);
			memcpy(priv->keys[idx].data, ext->key, key_len);
			break;

		case IW_ENCODE_ALG_TKIP:
		{
			hermes_t *hw = &priv->hw;
			u8 *tkip_iv = NULL;

			if (!priv->has_wpa ||
			    (ext->key_len > sizeof(priv->tkip_key[0])))
				goto out;

			priv->encode_alg = alg;
			memset(&priv->tkip_key[idx], 0,
			       sizeof(priv->tkip_key[idx]));
			memcpy(&priv->tkip_key[idx], ext->key, ext->key_len);

			if (ext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID)
				tkip_iv = &ext->rx_seq[0];

			err = __orinoco_hw_set_tkip_key(hw, idx,
				 ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY,
				 (u8 *) &priv->tkip_key[idx],
				 tkip_iv, NULL);
			if (err)
				printk(KERN_ERR "%s: Error %d setting TKIP key"
				       "\n", dev->name, err);

			goto out;
		}
		default:
			goto out;
		}
	}
	err = -EINPROGRESS;
 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_get_encodeext(struct net_device *dev,
				       struct iw_request_info *info,
				       union iwreq_data *wrqu,
				       char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int idx, max_key_len;
	unsigned long flags;
	int err;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = -EINVAL;
	max_key_len = encoding->length - sizeof(*ext);
	if (max_key_len < 0)
		goto out;

	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if ((idx < 1) || (idx > 4))
			goto out;
		idx--;
	} else
		idx = priv->tx_key;

	encoding->flags = idx + 1;
	memset(ext, 0, sizeof(*ext));

	ext->alg = priv->encode_alg;
	switch (priv->encode_alg) {
	case IW_ENCODE_ALG_NONE:
		ext->key_len = 0;
		encoding->flags |= IW_ENCODE_DISABLED;
		break;
	case IW_ENCODE_ALG_WEP:
		ext->key_len = min_t(u16, le16_to_cpu(priv->keys[idx].len),
				     max_key_len);
		memcpy(ext->key, priv->keys[idx].data, ext->key_len);
		encoding->flags |= IW_ENCODE_ENABLED;
		break;
	case IW_ENCODE_ALG_TKIP:
		ext->key_len = min_t(u16, sizeof(struct orinoco_tkip_key),
				     max_key_len);
		memcpy(ext->key, &priv->tkip_key[idx], ext->key_len);
		encoding->flags |= IW_ENCODE_ENABLED;
		break;
	}

	err = 0;
 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_set_auth(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *wrqu, char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	struct iw_param *param = &wrqu->param;
	unsigned long flags;
	int ret = -EINPROGRESS;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_PRIVACY_INVOKED:
	case IW_AUTH_DROP_UNENCRYPTED:
		/*
		 * orinoco does not use these parameters
		 */
		break;

	case IW_AUTH_KEY_MGMT:
		/* wl_lkm implies value 2 == PSK for Hermes I
		 * which ties in with WEXT
		 * no other hints tho :(
		 */
		priv->key_mgmt = param->value;
		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:
		/* When countermeasures are enabled, shut down the
		 * card; when disabled, re-enable the card. This must
		 * take effect immediately.
		 *
		 * TODO: Make sure that the EAPOL message is getting
		 *       out before card disabled
		 */
		if (param->value) {
			priv->tkip_cm_active = 1;
			ret = hermes_enable_port(hw, 0);
		} else {
			priv->tkip_cm_active = 0;
			ret = hermes_disable_port(hw, 0);
		}
		break;

	case IW_AUTH_80211_AUTH_ALG:
		if (param->value & IW_AUTH_ALG_SHARED_KEY)
			priv->wep_restrict = 1;
		else if (param->value & IW_AUTH_ALG_OPEN_SYSTEM)
			priv->wep_restrict = 0;
		else
			ret = -EINVAL;
		break;

	case IW_AUTH_WPA_ENABLED:
		if (priv->has_wpa) {
			priv->wpa_enabled = param->value ? 1 : 0;
		} else {
			if (param->value)
				ret = -EOPNOTSUPP;
			/* else silently accept disable of WPA */
			priv->wpa_enabled = 0;
		}
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	orinoco_unlock(priv, &flags);
	return ret;
}

static int orinoco_ioctl_get_auth(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *wrqu, char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct iw_param *param = &wrqu->param;
	unsigned long flags;
	int ret = 0;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_KEY_MGMT:
		param->value = priv->key_mgmt;
		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:
		param->value = priv->tkip_cm_active;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		if (priv->wep_restrict)
			param->value = IW_AUTH_ALG_SHARED_KEY;
		else
			param->value = IW_AUTH_ALG_OPEN_SYSTEM;
		break;

	case IW_AUTH_WPA_ENABLED:
		param->value = priv->wpa_enabled;
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	orinoco_unlock(priv, &flags);
	return ret;
}

static int orinoco_ioctl_set_genie(struct net_device *dev,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	u8 *buf;
	unsigned long flags;

	/* cut off at IEEE80211_MAX_DATA_LEN */
	if ((wrqu->data.length > IEEE80211_MAX_DATA_LEN) ||
	    (wrqu->data.length && (extra == NULL)))
		return -EINVAL;

	if (wrqu->data.length) {
		buf = kmalloc(wrqu->data.length, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;

		memcpy(buf, extra, wrqu->data.length);
	} else
		buf = NULL;

	if (orinoco_lock(priv, &flags) != 0) {
		kfree(buf);
		return -EBUSY;
	}

	kfree(priv->wpa_ie);
	priv->wpa_ie = buf;
	priv->wpa_ie_len = wrqu->data.length;

	if (priv->wpa_ie) {
		/* Looks like wl_lkm wants to check the auth alg, and
		 * somehow pass it to the firmware.
		 * Instead it just calls the key mgmt rid
		 *   - we do this in set auth.
		 */
	}

	orinoco_unlock(priv, &flags);
	return 0;
}

static int orinoco_ioctl_get_genie(struct net_device *dev,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err = 0;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if ((priv->wpa_ie_len == 0) || (priv->wpa_ie == NULL)) {
		wrqu->data.length = 0;
		goto out;
	}

	if (wrqu->data.length < priv->wpa_ie_len) {
		err = -E2BIG;
		goto out;
	}

	wrqu->data.length = priv->wpa_ie_len;
	memcpy(extra, priv->wpa_ie, priv->wpa_ie_len);

out:
	orinoco_unlock(priv, &flags);
	return err;
}

static int orinoco_ioctl_set_mlme(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *wrqu, char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	unsigned long flags;
	int ret = 0;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		/* silently ignore */
		break;

	case IW_MLME_DISASSOC:
	{
		struct {
			u8 addr[ETH_ALEN];
			__le16 reason_code;
		} __attribute__ ((packed)) buf;

		memcpy(buf.addr, mlme->addr.sa_data, ETH_ALEN);
		buf.reason_code = cpu_to_le16(mlme->reason_code);
		ret = HERMES_WRITE_RECORD(hw, USER_BAP,
					  HERMES_RID_CNFDISASSOCIATE,
					  &buf);
		break;
	}
	default:
		ret = -EOPNOTSUPP;
	}

	orinoco_unlock(priv, &flags);
	return ret;
}

static int orinoco_ioctl_getretry(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *rrq,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 short_limit, long_limit, lifetime;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_SHORTRETRYLIMIT,
				  &short_limit);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_LONGRETRYLIMIT,
				  &long_limit);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_MAXTRANSMITLIFETIME,
				  &lifetime);
	if (err)
		goto out;

	rrq->disabled = 0;		/* Can't be disabled */

	/* Note : by default, display the retry number */
	if ((rrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		rrq->flags = IW_RETRY_LIFETIME;
		rrq->value = lifetime * 1000;	/* ??? */
	} else {
		/* By default, display the min number */
		if ((rrq->flags & IW_RETRY_LONG)) {
			rrq->flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
			rrq->value = long_limit;
		} else {
			rrq->flags = IW_RETRY_LIMIT;
			rrq->value = short_limit;
			if (short_limit != long_limit)
				rrq->flags |= IW_RETRY_SHORT;
		}
	}

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_reset(struct net_device *dev,
			       struct iw_request_info *info,
			       void *wrqu,
			       char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (info->cmd == (SIOCIWFIRSTPRIV + 0x1)) {
		printk(KERN_DEBUG "%s: Forcing reset!\n", dev->name);

		/* Firmware reset */
		orinoco_reset(&priv->reset_work);
	} else {
		printk(KERN_DEBUG "%s: Force scheduling reset!\n", dev->name);

		schedule_work(&priv->reset_work);
	}

	return 0;
}

static int orinoco_ioctl_setibssport(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)

{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = *((int *) extra);
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->ibss_port = val ;

	/* Actually update the mode we are using */
	set_port_type(priv);

	orinoco_unlock(priv, &flags);
	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getibssport(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int *val = (int *) extra;

	*val = priv->ibss_port;
	return 0;
}

static int orinoco_ioctl_setport3(struct net_device *dev,
				  struct iw_request_info *info,
				  void *wrqu,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int val = *((int *) extra);
	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	switch (val) {
	case 0: /* Try to do IEEE ad-hoc mode */
		if (!priv->has_ibss) {
			err = -EINVAL;
			break;
		}
		priv->prefer_port3 = 0;

		break;

	case 1: /* Try to do Lucent proprietary ad-hoc mode */
		if (!priv->has_port3) {
			err = -EINVAL;
			break;
		}
		priv->prefer_port3 = 1;
		break;

	default:
		err = -EINVAL;
	}

	if (!err) {
		/* Actually update the mode we are using */
		set_port_type(priv);
		err = -EINPROGRESS;
	}

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_getport3(struct net_device *dev,
				  struct iw_request_info *info,
				  void *wrqu,
				  char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int *val = (int *) extra;

	*val = priv->prefer_port3;
	return 0;
}

static int orinoco_ioctl_setpreamble(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int val;

	if (!priv->has_preamble)
		return -EOPNOTSUPP;

	/* 802.11b has recently defined some short preamble.
	 * Basically, the Phy header has been reduced in size.
	 * This increase performance, especially at high rates
	 * (the preamble is transmitted at 1Mb/s), unfortunately
	 * this give compatibility troubles... - Jean II */
	val = *((int *) extra);

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (val)
		priv->preamble = 1;
	else
		priv->preamble = 0;

	orinoco_unlock(priv, &flags);

	return -EINPROGRESS;		/* Call commit handler */
}

static int orinoco_ioctl_getpreamble(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int *val = (int *) extra;

	if (!priv->has_preamble)
		return -EOPNOTSUPP;

	*val = priv->preamble;
	return 0;
}

/* ioctl interface to hermes_read_ltv()
 * To use with iwpriv, pass the RID as the token argument, e.g.
 * iwpriv get_rid [0xfc00]
 * At least Wireless Tools 25 is required to use iwpriv.
 * For Wireless Tools 25 and 26 append "dummy" are the end. */
static int orinoco_ioctl_getrid(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	int rid = data->flags;
	u16 length;
	int err;
	unsigned long flags;

	/* It's a "get" function, but we don't want users to access the
	 * WEP key and other raw firmware data */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (rid < 0xfc00 || rid > 0xffff)
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hermes_read_ltv(hw, USER_BAP, rid, MAX_RID_LEN, &length,
			      extra);
	if (err)
		goto out;

	data->length = min_t(u16, HERMES_RECLEN_TO_BYTES(length),
			     MAX_RID_LEN);

 out:
	orinoco_unlock(priv, &flags);
	return err;
}

/* Trigger a scan (look for other cells in the vicinity) */
static int orinoco_ioctl_setscan(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *srq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	hermes_t *hw = &priv->hw;
	struct iw_scan_req *si = (struct iw_scan_req *) extra;
	int err = 0;
	unsigned long flags;

	/* Note : you may have realised that, as this is a SET operation,
	 * this is privileged and therefore a normal user can't
	 * perform scanning.
	 * This is not an error, while the device perform scanning,
	 * traffic doesn't flow, so it's a perfect DoS...
	 * Jean II */

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* Scanning with port 0 disabled would fail */
	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	/* In monitor mode, the scan results are always empty.
	 * Probe responses are passed to the driver as received
	 * frames and could be processed in software. */
	if (priv->iw_mode == IW_MODE_MONITOR) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Note : because we don't lock out the irq handler, the way
	 * we access scan variables in priv is critical.
	 *	o scan_inprogress : not touched by irq handler
	 *	o scan_mode : not touched by irq handler
	 * Before modifying anything on those variables, please think hard !
	 * Jean II */

	/* Save flags */
	priv->scan_mode = srq->flags;

	/* Always trigger scanning, even if it's in progress.
	 * This way, if the info frame get lost, we will recover somewhat
	 * gracefully  - Jean II */

	if (priv->has_hostscan) {
		switch (priv->firmware_type) {
		case FIRMWARE_TYPE_SYMBOL:
			err = hermes_write_wordrec(hw, USER_BAP,
						HERMES_RID_CNFHOSTSCAN_SYMBOL,
						HERMES_HOSTSCAN_SYMBOL_ONCE |
						HERMES_HOSTSCAN_SYMBOL_BCAST);
			break;
		case FIRMWARE_TYPE_INTERSIL: {
			__le16 req[3];

			req[0] = cpu_to_le16(0x3fff);	/* All channels */
			req[1] = cpu_to_le16(0x0001);	/* rate 1 Mbps */
			req[2] = 0;			/* Any ESSID */
			err = HERMES_WRITE_RECORD(hw, USER_BAP,
						  HERMES_RID_CNFHOSTSCAN, &req);
		}
		break;
		case FIRMWARE_TYPE_AGERE:
			if (priv->scan_mode & IW_SCAN_THIS_ESSID) {
				struct hermes_idstring idbuf;
				size_t len = min(sizeof(idbuf.val),
						 (size_t) si->essid_len);
				idbuf.len = cpu_to_le16(len);
				memcpy(idbuf.val, si->essid, len);

				err = hermes_write_ltv(hw, USER_BAP,
					       HERMES_RID_CNFSCANSSID_AGERE,
					       HERMES_BYTES_TO_RECLEN(len + 2),
					       &idbuf);
			} else
				err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFSCANSSID_AGERE,
						   0);	/* Any ESSID */
			if (err)
				break;

			if (priv->has_ext_scan) {
				/* Clear scan results at the start of
				 * an extended scan */
				orinoco_clear_scan_results(priv,
						msecs_to_jiffies(15000));

				/* TODO: Is this available on older firmware?
				 *   Can we use it to scan specific channels
				 *   for IW_SCAN_THIS_FREQ? */
				err = hermes_write_wordrec(hw, USER_BAP,
						HERMES_RID_CNFSCANCHANNELS2GHZ,
						0x7FFF);
				if (err)
					goto out;

				err = hermes_inquire(hw,
						     HERMES_INQ_CHANNELINFO);
			} else
				err = hermes_inquire(hw, HERMES_INQ_SCAN);
			break;
		}
	} else
		err = hermes_inquire(hw, HERMES_INQ_SCAN);

	/* One more client */
	if (!err)
		priv->scan_inprogress = 1;

 out:
	orinoco_unlock(priv, &flags);
	return err;
}

#define MAX_CUSTOM_LEN 64

/* Translate scan data returned from the card to a card independant
 * format that the Wireless Tools will understand - Jean II */
static inline char *orinoco_translate_scan(struct net_device *dev,
					   struct iw_request_info *info,
					   char *current_ev,
					   char *end_buf,
					   union hermes_scan_info *bss,
					   unsigned long last_scanned)
{
	struct orinoco_private *priv = netdev_priv(dev);
	u16			capabilities;
	u16			channel;
	struct iw_event		iwe;		/* Temporary buffer */
	char custom[MAX_CUSTOM_LEN];

	memset(&iwe, 0, sizeof(iwe));

	/* First entry *MUST* be the AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bss->a.bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event(info, current_ev, end_buf,
					  &iwe, IW_EV_ADDR_LEN);

	/* Other entries will be displayed in the order we give them */

	/* Add the ESSID */
	iwe.u.data.length = le16_to_cpu(bss->a.essid_len);
	if (iwe.u.data.length > 32)
		iwe.u.data.length = 32;
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	current_ev = iwe_stream_add_point(info, current_ev, end_buf,
					  &iwe, bss->a.essid);

	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	capabilities = le16_to_cpu(bss->a.capabilities);
	if (capabilities & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS)) {
		if (capabilities & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_UINT_LEN);
	}

	channel = bss->s.channel;
	if ((channel >= 1) && (channel <= NUM_CHANNELS)) {
		/* Add channel and frequency */
		iwe.cmd = SIOCGIWFREQ;
		iwe.u.freq.m = channel;
		iwe.u.freq.e = 0;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_FREQ_LEN);

		iwe.u.freq.m = ieee80211_dsss_chan_to_freq(channel) * 100000;
		iwe.u.freq.e = 1;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_FREQ_LEN);
	}

	/* Add quality statistics. level and noise in dB. No link quality */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_DBM | IW_QUAL_QUAL_INVALID;
	iwe.u.qual.level = (__u8) le16_to_cpu(bss->a.level) - 0x95;
	iwe.u.qual.noise = (__u8) le16_to_cpu(bss->a.noise) - 0x95;
	/* Wireless tools prior to 27.pre22 will show link quality
	 * anyway, so we provide a reasonable value. */
	if (iwe.u.qual.level > iwe.u.qual.noise)
		iwe.u.qual.qual = iwe.u.qual.level - iwe.u.qual.noise;
	else
		iwe.u.qual.qual = 0;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf,
					  &iwe, IW_EV_QUAL_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (capabilities & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(info, current_ev, end_buf,
					  &iwe, NULL);

	/* Bit rate is not available in Lucent/Agere firmwares */
	if (priv->firmware_type != FIRMWARE_TYPE_AGERE) {
		char *current_val = current_ev + iwe_stream_lcp_len(info);
		int i;
		int step;

		if (priv->firmware_type == FIRMWARE_TYPE_SYMBOL)
			step = 2;
		else
			step = 1;

		iwe.cmd = SIOCGIWRATE;
		/* Those two flags are ignored... */
		iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
		/* Max 10 values */
		for (i = 0; i < 10; i += step) {
			/* NULL terminated */
			if (bss->p.rates[i] == 0x0)
				break;
			/* Bit rate given in 500 kb/s units (+ 0x80) */
			iwe.u.bitrate.value =
				((bss->p.rates[i] & 0x7f) * 500000);
			current_val = iwe_stream_add_value(info, current_ev,
							   current_val,
							   end_buf, &iwe,
							   IW_EV_PARAM_LEN);
		}
		/* Check if we added any event */
		if ((current_val - current_ev) > iwe_stream_lcp_len(info))
			current_ev = current_val;
	}

	/* Beacon interval */
	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length = snprintf(custom, MAX_CUSTOM_LEN,
				     "bcn_int=%d",
				     le16_to_cpu(bss->a.beacon_interv));
	if (iwe.u.data.length)
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, custom);

	/* Capabilites */
	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length = snprintf(custom, MAX_CUSTOM_LEN,
				     "capab=0x%04x",
				     capabilities);
	if (iwe.u.data.length)
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, custom);

	/* Add EXTRA: Age to display seconds since last beacon/probe response
	 * for given network. */
	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length = snprintf(custom, MAX_CUSTOM_LEN,
				     " Last beacon: %dms ago",
				     jiffies_to_msecs(jiffies - last_scanned));
	if (iwe.u.data.length)
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, custom);

	return current_ev;
}

static inline char *orinoco_translate_ext_scan(struct net_device *dev,
					       struct iw_request_info *info,
					       char *current_ev,
					       char *end_buf,
					       struct agere_ext_scan_info *bss,
					       unsigned long last_scanned)
{
	u16			capabilities;
	u16			channel;
	struct iw_event		iwe;		/* Temporary buffer */
	char custom[MAX_CUSTOM_LEN];
	u8 *ie;

	memset(&iwe, 0, sizeof(iwe));

	/* First entry *MUST* be the AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bss->bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event(info, current_ev, end_buf,
					  &iwe, IW_EV_ADDR_LEN);

	/* Other entries will be displayed in the order we give them */

	/* Add the ESSID */
	ie = bss->data;
	iwe.u.data.length = ie[1];
	if (iwe.u.data.length) {
		if (iwe.u.data.length > 32)
			iwe.u.data.length = 32;
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.flags = 1;
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, &ie[2]);
	}

	/* Add mode */
	capabilities = le16_to_cpu(bss->capabilities);
	if (capabilities & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS)) {
		iwe.cmd = SIOCGIWMODE;
		if (capabilities & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_UINT_LEN);
	}

	ie = orinoco_get_ie(bss->data, sizeof(bss->data), WLAN_EID_DS_PARAMS);
	channel = ie ? ie[2] : 0;
	if ((channel >= 1) && (channel <= NUM_CHANNELS)) {
		/* Add channel and frequency */
		iwe.cmd = SIOCGIWFREQ;
		iwe.u.freq.m = channel;
		iwe.u.freq.e = 0;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_FREQ_LEN);

		iwe.u.freq.m = ieee80211_dsss_chan_to_freq(channel) * 100000;
		iwe.u.freq.e = 1;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_FREQ_LEN);
	}

	/* Add quality statistics. level and noise in dB. No link quality */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_DBM | IW_QUAL_QUAL_INVALID;
	iwe.u.qual.level = bss->level - 0x95;
	iwe.u.qual.noise = bss->noise - 0x95;
	/* Wireless tools prior to 27.pre22 will show link quality
	 * anyway, so we provide a reasonable value. */
	if (iwe.u.qual.level > iwe.u.qual.noise)
		iwe.u.qual.qual = iwe.u.qual.level - iwe.u.qual.noise;
	else
		iwe.u.qual.qual = 0;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf,
					  &iwe, IW_EV_QUAL_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (capabilities & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(info, current_ev, end_buf,
					  &iwe, NULL);

	/* WPA IE */
	ie = orinoco_get_wpa_ie(bss->data, sizeof(bss->data));
	if (ie) {
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = ie[1] + 2;
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, ie);
	}

	/* RSN IE */
	ie = orinoco_get_ie(bss->data, sizeof(bss->data), WLAN_EID_RSN);
	if (ie) {
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = ie[1] + 2;
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, ie);
	}

	ie = orinoco_get_ie(bss->data, sizeof(bss->data), WLAN_EID_SUPP_RATES);
	if (ie) {
		char *p = current_ev + iwe_stream_lcp_len(info);
		int i;

		iwe.cmd = SIOCGIWRATE;
		/* Those two flags are ignored... */
		iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;

		for (i = 2; i < (ie[1] + 2); i++) {
			iwe.u.bitrate.value = ((ie[i] & 0x7F) * 500000);
			p = iwe_stream_add_value(info, current_ev, p, end_buf,
						 &iwe, IW_EV_PARAM_LEN);
		}
		/* Check if we added any event */
		if (p > (current_ev + iwe_stream_lcp_len(info)))
			current_ev = p;
	}

	/* Timestamp */
	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length =
		snprintf(custom, MAX_CUSTOM_LEN, "tsf=%016llx",
			 (unsigned long long) le64_to_cpu(bss->timestamp));
	if (iwe.u.data.length)
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, custom);

	/* Beacon interval */
	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length = snprintf(custom, MAX_CUSTOM_LEN,
				     "bcn_int=%d",
				     le16_to_cpu(bss->beacon_interval));
	if (iwe.u.data.length)
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, custom);

	/* Capabilites */
	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length = snprintf(custom, MAX_CUSTOM_LEN,
				     "capab=0x%04x",
				     capabilities);
	if (iwe.u.data.length)
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, custom);

	/* Add EXTRA: Age to display seconds since last beacon/probe response
	 * for given network. */
	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length = snprintf(custom, MAX_CUSTOM_LEN,
				     " Last beacon: %dms ago",
				     jiffies_to_msecs(jiffies - last_scanned));
	if (iwe.u.data.length)
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, custom);

	return current_ev;
}

/* Return results of a scan */
static int orinoco_ioctl_getscan(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *srq,
				 char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	unsigned long flags;
	char *current_ev = extra;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (priv->scan_inprogress) {
		/* Important note : we don't want to block the caller
		 * until results are ready for various reasons.
		 * First, managing wait queues is complex and racy.
		 * Second, we grab some rtnetlink lock before comming
		 * here (in dev_ioctl()).
		 * Third, we generate an Wireless Event, so the
		 * caller can wait itself on that - Jean II */
		err = -EAGAIN;
		goto out;
	}

	if (priv->has_ext_scan) {
		struct xbss_element *bss;

		list_for_each_entry(bss, &priv->bss_list, list) {
			/* Translate this entry to WE format */
			current_ev =
				orinoco_translate_ext_scan(dev, info,
							   current_ev,
							   extra + srq->length,
							   &bss->bss,
							   bss->last_scanned);

			/* Check if there is space for one more entry */
			if ((extra + srq->length - current_ev)
			    <= IW_EV_ADDR_LEN) {
				/* Ask user space to try again with a
				 * bigger buffer */
				err = -E2BIG;
				goto out;
			}
		}

	} else {
		struct bss_element *bss;

		list_for_each_entry(bss, &priv->bss_list, list) {
			/* Translate this entry to WE format */
			current_ev = orinoco_translate_scan(dev, info,
							    current_ev,
							    extra + srq->length,
							    &bss->bss,
							    bss->last_scanned);

			/* Check if there is space for one more entry */
			if ((extra + srq->length - current_ev)
			    <= IW_EV_ADDR_LEN) {
				/* Ask user space to try again with a
				 * bigger buffer */
				err = -E2BIG;
				goto out;
			}
		}
	}

	srq->length = (current_ev - extra);
	srq->flags = (__u16) priv->scan_mode;

out:
	orinoco_unlock(priv, &flags);
	return err;
}

/* Commit handler, called after set operations */
static int orinoco_ioctl_commit(struct net_device *dev,
				struct iw_request_info *info,
				void *wrqu,
				char *extra)
{
	struct orinoco_private *priv = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	unsigned long flags;
	int err = 0;

	if (!priv->open)
		return 0;

	if (priv->broken_disableport) {
		orinoco_reset(&priv->reset_work);
		return 0;
	}

	if (orinoco_lock(priv, &flags) != 0)
		return err;

	err = hermes_disable_port(hw, 0);
	if (err) {
		printk(KERN_WARNING "%s: Unable to disable port "
		       "while reconfiguring card\n", dev->name);
		priv->broken_disableport = 1;
		goto out;
	}

	err = __orinoco_program_rids(dev);
	if (err) {
		printk(KERN_WARNING "%s: Unable to reconfigure card\n",
		       dev->name);
		goto out;
	}

	err = hermes_enable_port(hw, 0);
	if (err) {
		printk(KERN_WARNING "%s: Unable to enable port while reconfiguring card\n",
		       dev->name);
		goto out;
	}

 out:
	if (err) {
		printk(KERN_WARNING "%s: Resetting instead...\n", dev->name);
		schedule_work(&priv->reset_work);
		err = 0;
	}

	orinoco_unlock(priv, &flags);
	return err;
}

static const struct iw_priv_args orinoco_privtab[] = {
	{ SIOCIWFIRSTPRIV + 0x0, 0, 0, "force_reset" },
	{ SIOCIWFIRSTPRIV + 0x1, 0, 0, "card_reset" },
	{ SIOCIWFIRSTPRIV + 0x2, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  0, "set_port3" },
	{ SIOCIWFIRSTPRIV + 0x3, 0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  "get_port3" },
	{ SIOCIWFIRSTPRIV + 0x4, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  0, "set_preamble" },
	{ SIOCIWFIRSTPRIV + 0x5, 0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  "get_preamble" },
	{ SIOCIWFIRSTPRIV + 0x6, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  0, "set_ibssport" },
	{ SIOCIWFIRSTPRIV + 0x7, 0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  "get_ibssport" },
	{ SIOCIWFIRSTPRIV + 0x9, 0, IW_PRIV_TYPE_BYTE | MAX_RID_LEN,
	  "get_rid" },
};


/*
 * Structures to export the Wireless Handlers
 */

#define STD_IW_HANDLER(id, func) \
	[IW_IOCTL_IDX(id)] = (iw_handler) func
static const iw_handler	orinoco_handler[] = {
	STD_IW_HANDLER(SIOCSIWCOMMIT,	orinoco_ioctl_commit),
	STD_IW_HANDLER(SIOCGIWNAME,	orinoco_ioctl_getname),
	STD_IW_HANDLER(SIOCSIWFREQ,	orinoco_ioctl_setfreq),
	STD_IW_HANDLER(SIOCGIWFREQ,	orinoco_ioctl_getfreq),
	STD_IW_HANDLER(SIOCSIWMODE,	orinoco_ioctl_setmode),
	STD_IW_HANDLER(SIOCGIWMODE,	orinoco_ioctl_getmode),
	STD_IW_HANDLER(SIOCSIWSENS,	orinoco_ioctl_setsens),
	STD_IW_HANDLER(SIOCGIWSENS,	orinoco_ioctl_getsens),
	STD_IW_HANDLER(SIOCGIWRANGE,	orinoco_ioctl_getiwrange),
	STD_IW_HANDLER(SIOCSIWSPY,	iw_handler_set_spy),
	STD_IW_HANDLER(SIOCGIWSPY,	iw_handler_get_spy),
	STD_IW_HANDLER(SIOCSIWTHRSPY,	iw_handler_set_thrspy),
	STD_IW_HANDLER(SIOCGIWTHRSPY,	iw_handler_get_thrspy),
	STD_IW_HANDLER(SIOCSIWAP,	orinoco_ioctl_setwap),
	STD_IW_HANDLER(SIOCGIWAP,	orinoco_ioctl_getwap),
	STD_IW_HANDLER(SIOCSIWSCAN,	orinoco_ioctl_setscan),
	STD_IW_HANDLER(SIOCGIWSCAN,	orinoco_ioctl_getscan),
	STD_IW_HANDLER(SIOCSIWESSID,	orinoco_ioctl_setessid),
	STD_IW_HANDLER(SIOCGIWESSID,	orinoco_ioctl_getessid),
	STD_IW_HANDLER(SIOCSIWNICKN,	orinoco_ioctl_setnick),
	STD_IW_HANDLER(SIOCGIWNICKN,	orinoco_ioctl_getnick),
	STD_IW_HANDLER(SIOCSIWRATE,	orinoco_ioctl_setrate),
	STD_IW_HANDLER(SIOCGIWRATE,	orinoco_ioctl_getrate),
	STD_IW_HANDLER(SIOCSIWRTS,	orinoco_ioctl_setrts),
	STD_IW_HANDLER(SIOCGIWRTS,	orinoco_ioctl_getrts),
	STD_IW_HANDLER(SIOCSIWFRAG,	orinoco_ioctl_setfrag),
	STD_IW_HANDLER(SIOCGIWFRAG,	orinoco_ioctl_getfrag),
	STD_IW_HANDLER(SIOCGIWRETRY,	orinoco_ioctl_getretry),
	STD_IW_HANDLER(SIOCSIWENCODE,	orinoco_ioctl_setiwencode),
	STD_IW_HANDLER(SIOCGIWENCODE,	orinoco_ioctl_getiwencode),
	STD_IW_HANDLER(SIOCSIWPOWER,	orinoco_ioctl_setpower),
	STD_IW_HANDLER(SIOCGIWPOWER,	orinoco_ioctl_getpower),
	STD_IW_HANDLER(SIOCSIWGENIE,	orinoco_ioctl_set_genie),
	STD_IW_HANDLER(SIOCGIWGENIE,	orinoco_ioctl_get_genie),
	STD_IW_HANDLER(SIOCSIWMLME,	orinoco_ioctl_set_mlme),
	STD_IW_HANDLER(SIOCSIWAUTH,	orinoco_ioctl_set_auth),
	STD_IW_HANDLER(SIOCGIWAUTH,	orinoco_ioctl_get_auth),
	STD_IW_HANDLER(SIOCSIWENCODEEXT, orinoco_ioctl_set_encodeext),
	STD_IW_HANDLER(SIOCGIWENCODEEXT, orinoco_ioctl_get_encodeext),
};


/*
  Added typecasting since we no longer use iwreq_data -- Moustafa
 */
static const iw_handler	orinoco_private_handler[] = {
	[0] = (iw_handler) orinoco_ioctl_reset,
	[1] = (iw_handler) orinoco_ioctl_reset,
	[2] = (iw_handler) orinoco_ioctl_setport3,
	[3] = (iw_handler) orinoco_ioctl_getport3,
	[4] = (iw_handler) orinoco_ioctl_setpreamble,
	[5] = (iw_handler) orinoco_ioctl_getpreamble,
	[6] = (iw_handler) orinoco_ioctl_setibssport,
	[7] = (iw_handler) orinoco_ioctl_getibssport,
	[9] = (iw_handler) orinoco_ioctl_getrid,
};

const struct iw_handler_def orinoco_handler_def = {
	.num_standard = ARRAY_SIZE(orinoco_handler),
	.num_private = ARRAY_SIZE(orinoco_private_handler),
	.num_private_args = ARRAY_SIZE(orinoco_privtab),
	.standard = orinoco_handler,
	.private = orinoco_private_handler,
	.private_args = orinoco_privtab,
	.get_wireless_stats = orinoco_get_wireless_stats,
};
