/* Wireless extensions support.
 *
 * See copyright notice in main.c
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>
#include <net/cfg80211.h>
#include <net/cfg80211-wext.h>

#include "hermes.h"
#include "hermes_rid.h"
#include "orinoco.h"

#include "hw.h"
#include "mic.h"
#include "scan.h"
#include "main.h"

#include "wext.h"

#define MAX_RID_LEN 1024

/* Helper routine to record keys
 * It is called under orinoco_lock so it may not sleep */
static int orinoco_set_key(struct orinoco_private *priv, int index,
			   enum orinoco_alg alg, const u8 *key, int key_len,
			   const u8 *seq, int seq_len)
{
	kzfree(priv->keys[index].key);
	kzfree(priv->keys[index].seq);

	if (key_len) {
		priv->keys[index].key = kzalloc(key_len, GFP_ATOMIC);
		if (!priv->keys[index].key)
			goto nomem;
	} else
		priv->keys[index].key = NULL;

	if (seq_len) {
		priv->keys[index].seq = kzalloc(seq_len, GFP_ATOMIC);
		if (!priv->keys[index].seq)
			goto free_key;
	} else
		priv->keys[index].seq = NULL;

	priv->keys[index].key_len = key_len;
	priv->keys[index].seq_len = seq_len;

	if (key_len)
		memcpy(priv->keys[index].key, key, key_len);
	if (seq_len)
		memcpy(priv->keys[index].seq, seq, seq_len);

	switch (alg) {
	case ORINOCO_ALG_TKIP:
		priv->keys[index].cipher = WLAN_CIPHER_SUITE_TKIP;
		break;

	case ORINOCO_ALG_WEP:
		priv->keys[index].cipher = (key_len > SMALL_KEY_SIZE) ?
			WLAN_CIPHER_SUITE_WEP104 : WLAN_CIPHER_SUITE_WEP40;
		break;

	case ORINOCO_ALG_NONE:
	default:
		priv->keys[index].cipher = 0;
		break;
	}

	return 0;

free_key:
	kfree(priv->keys[index].key);
	priv->keys[index].key = NULL;

nomem:
	priv->keys[index].key_len = 0;
	priv->keys[index].seq_len = 0;
	priv->keys[index].cipher = 0;

	return -ENOMEM;
}

static struct iw_statistics *orinoco_get_wireless_stats(struct net_device *dev)
{
	struct orinoco_private *priv = ndev_priv(dev);
	struct hermes *hw = &priv->hw;
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

	if (priv->iw_mode == NL80211_IFTYPE_ADHOC) {
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
		} __packed cq;

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

static int orinoco_ioctl_setwap(struct net_device *dev,
				struct iw_request_info *info,
				struct sockaddr *ap_addr,
				char *extra)
{
	struct orinoco_private *priv = ndev_priv(dev);
	int err = -EINPROGRESS;		/* Call commit handler */
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* Enable automatic roaming - no sanity checks are needed */
	if (is_zero_ether_addr(ap_addr->sa_data) ||
	    is_broadcast_ether_addr(ap_addr->sa_data)) {
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

	if (priv->iw_mode != NL80211_IFTYPE_STATION) {
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
	struct orinoco_private *priv = ndev_priv(dev);

	int err = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	ap_addr->sa_family = ARPHRD_ETHER;
	err = orinoco_hw_get_current_bssid(priv, ap_addr->sa_data);

	orinoco_unlock(priv, &flags);

	return err;
}

static int orinoco_ioctl_setiwencode(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq,
				     char *keybuf)
{
	struct orinoco_private *priv = ndev_priv(dev);
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	int setindex = priv->tx_key;
	enum orinoco_alg encode_alg = priv->encode_alg;
	int restricted = priv->wep_restrict;
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
	if ((priv->has_wpa) && (priv->encode_alg == ORINOCO_ALG_TKIP))
		(void) orinoco_clear_tkip_key(priv, setindex);

	if (erq->length > 0) {
		if ((index < 0) || (index >= ORINOCO_MAX_KEYS))
			index = priv->tx_key;

		/* Switch on WEP if off */
		if (encode_alg != ORINOCO_ALG_WEP) {
			setindex = index;
			encode_alg = ORINOCO_ALG_WEP;
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
			if (priv->keys[index].key_len == 0) {
				err = -EINVAL;
				goto out;
			}
			setindex = index;
		}
	}

	if (erq->flags & IW_ENCODE_DISABLED)
		encode_alg = ORINOCO_ALG_NONE;
	if (erq->flags & IW_ENCODE_OPEN)
		restricted = 0;
	if (erq->flags & IW_ENCODE_RESTRICTED)
		restricted = 1;

	if (erq->pointer && erq->length > 0) {
		err = orinoco_set_key(priv, index, ORINOCO_ALG_WEP, keybuf,
				      erq->length, NULL, 0);
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
	struct orinoco_private *priv = ndev_priv(dev);
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
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

	erq->length = priv->keys[index].key_len;

	memcpy(keybuf, priv->keys[index].key, erq->length);

	orinoco_unlock(priv, &flags);
	return 0;
}

static int orinoco_ioctl_setessid(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *erq,
				  char *essidbuf)
{
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
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

static int orinoco_ioctl_setfreq(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_freq *frq,
				 char *extra)
{
	struct orinoco_private *priv = ndev_priv(dev);
	int chan = -1;
	unsigned long flags;
	int err = -EINPROGRESS;		/* Call commit handler */

	/* In infrastructure mode the AP sets the channel */
	if (priv->iw_mode == NL80211_IFTYPE_STATION)
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
	     !(priv->channel_mask & (1 << (chan - 1))))
		return -EINVAL;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->channel = chan;
	if (priv->iw_mode == NL80211_IFTYPE_MONITOR) {
		/* Fast channel change - no commit if successful */
		struct hermes *hw = &priv->hw;
		err = hw->ops->cmd_wait(hw, HERMES_CMD_TEST |
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
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
	struct hermes *hw = &priv->hw;
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
	struct orinoco_private *priv = ndev_priv(dev);
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

static int orinoco_ioctl_setrate(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq,
				 char *extra)
{
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
	int err = 0;
	int bitrate, automatic;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	orinoco_get_ratemode_cfg(priv->bitratemode, &bitrate, &automatic);

	/* If the interface is running we try to find more about the
	   current mode */
	if (netif_running(dev)) {
		int act_bitrate;
		int lerr;

		/* Ignore errors if we can't get the actual bitrate */
		lerr = orinoco_hw_get_act_bitrate(priv, &act_bitrate);
		if (!lerr)
			bitrate = act_bitrate;
	}

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
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
	struct hermes *hw = &priv->hw;
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
	struct orinoco_private *priv = ndev_priv(dev);
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int idx, alg = ext->alg, set_key = 1;
	unsigned long flags;
	int err = -EINVAL;

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
			priv->encode_alg = ORINOCO_ALG_NONE;
			err = orinoco_set_key(priv, idx, ORINOCO_ALG_NONE,
					      NULL, 0, NULL, 0);
			break;

		case IW_ENCODE_ALG_WEP:
			if (ext->key_len <= 0)
				goto out;

			priv->encode_alg = ORINOCO_ALG_WEP;
			err = orinoco_set_key(priv, idx, ORINOCO_ALG_WEP,
					      ext->key, ext->key_len, NULL, 0);
			break;

		case IW_ENCODE_ALG_TKIP:
		{
			u8 *tkip_iv = NULL;

			if (!priv->has_wpa ||
			    (ext->key_len > sizeof(struct orinoco_tkip_key)))
				goto out;

			priv->encode_alg = ORINOCO_ALG_TKIP;

			if (ext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID)
				tkip_iv = &ext->rx_seq[0];

			err = orinoco_set_key(priv, idx, ORINOCO_ALG_TKIP,
					      ext->key, ext->key_len, tkip_iv,
					      ORINOCO_SEQ_LEN);

			err = __orinoco_hw_set_tkip_key(priv, idx,
				 ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY,
				 priv->keys[idx].key,
				 tkip_iv, ORINOCO_SEQ_LEN, NULL, 0);
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
	struct orinoco_private *priv = ndev_priv(dev);
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

	switch (priv->encode_alg) {
	case ORINOCO_ALG_NONE:
		ext->alg = IW_ENCODE_ALG_NONE;
		ext->key_len = 0;
		encoding->flags |= IW_ENCODE_DISABLED;
		break;
	case ORINOCO_ALG_WEP:
		ext->alg = IW_ENCODE_ALG_WEP;
		ext->key_len = min(priv->keys[idx].key_len, max_key_len);
		memcpy(ext->key, priv->keys[idx].key, ext->key_len);
		encoding->flags |= IW_ENCODE_ENABLED;
		break;
	case ORINOCO_ALG_TKIP:
		ext->alg = IW_ENCODE_ALG_TKIP;
		ext->key_len = min(priv->keys[idx].key_len, max_key_len);
		memcpy(ext->key, priv->keys[idx].key, ext->key_len);
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
	struct orinoco_private *priv = ndev_priv(dev);
	struct hermes *hw = &priv->hw;
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

	case IW_AUTH_MFP:
		/* Management Frame Protection not supported.
		 * Only fail if set to required.
		 */
		if (param->value == IW_AUTH_MFP_REQUIRED)
			ret = -EINVAL;
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
			ret = hermes_disable_port(hw, 0);
		} else {
			priv->tkip_cm_active = 0;
			ret = hermes_enable_port(hw, 0);
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
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
	u8 *buf;
	unsigned long flags;

	/* cut off at IEEE80211_MAX_DATA_LEN */
	if ((wrqu->data.length > IEEE80211_MAX_DATA_LEN) ||
	    (wrqu->data.length && (extra == NULL)))
		return -EINVAL;

	if (wrqu->data.length) {
		buf = kmemdup(extra, wrqu->data.length, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;
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
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
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

		ret = orinoco_hw_disassociate(priv, mlme->addr.sa_data,
					      mlme->reason_code);
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	orinoco_unlock(priv, &flags);
	return ret;
}

static int orinoco_ioctl_reset(struct net_device *dev,
			       struct iw_request_info *info,
			       void *wrqu,
			       char *extra)
{
	struct orinoco_private *priv = ndev_priv(dev);

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
	struct orinoco_private *priv = ndev_priv(dev);
	int val = *((int *) extra);
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	priv->ibss_port = val;

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
	struct orinoco_private *priv = ndev_priv(dev);
	int *val = (int *) extra;

	*val = priv->ibss_port;
	return 0;
}

static int orinoco_ioctl_setport3(struct net_device *dev,
				  struct iw_request_info *info,
				  void *wrqu,
				  char *extra)
{
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
	int *val = (int *) extra;

	*val = priv->prefer_port3;
	return 0;
}

static int orinoco_ioctl_setpreamble(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu,
				     char *extra)
{
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
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
	struct orinoco_private *priv = ndev_priv(dev);
	struct hermes *hw = &priv->hw;
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

	err = hw->ops->read_ltv(hw, USER_BAP, rid, MAX_RID_LEN, &length,
				extra);
	if (err)
		goto out;

	data->length = min_t(u16, HERMES_RECLEN_TO_BYTES(length),
			     MAX_RID_LEN);

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
	struct orinoco_private *priv = ndev_priv(dev);
	unsigned long flags;
	int err = 0;

	if (!priv->open)
		return 0;

	if (orinoco_lock(priv, &flags) != 0)
		return err;

	err = orinoco_commit(priv);

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

static const iw_handler	orinoco_handler[] = {
	IW_HANDLER(SIOCSIWCOMMIT,	(iw_handler)orinoco_ioctl_commit),
	IW_HANDLER(SIOCGIWNAME,		(iw_handler)cfg80211_wext_giwname),
	IW_HANDLER(SIOCSIWFREQ,		(iw_handler)orinoco_ioctl_setfreq),
	IW_HANDLER(SIOCGIWFREQ,		(iw_handler)orinoco_ioctl_getfreq),
	IW_HANDLER(SIOCSIWMODE,		(iw_handler)cfg80211_wext_siwmode),
	IW_HANDLER(SIOCGIWMODE,		(iw_handler)cfg80211_wext_giwmode),
	IW_HANDLER(SIOCSIWSENS,		(iw_handler)orinoco_ioctl_setsens),
	IW_HANDLER(SIOCGIWSENS,		(iw_handler)orinoco_ioctl_getsens),
	IW_HANDLER(SIOCGIWRANGE,	(iw_handler)cfg80211_wext_giwrange),
	IW_HANDLER(SIOCSIWSPY,		iw_handler_set_spy),
	IW_HANDLER(SIOCGIWSPY,		iw_handler_get_spy),
	IW_HANDLER(SIOCSIWTHRSPY,	iw_handler_set_thrspy),
	IW_HANDLER(SIOCGIWTHRSPY,	iw_handler_get_thrspy),
	IW_HANDLER(SIOCSIWAP,		(iw_handler)orinoco_ioctl_setwap),
	IW_HANDLER(SIOCGIWAP,		(iw_handler)orinoco_ioctl_getwap),
	IW_HANDLER(SIOCSIWSCAN,		(iw_handler)cfg80211_wext_siwscan),
	IW_HANDLER(SIOCGIWSCAN,		(iw_handler)cfg80211_wext_giwscan),
	IW_HANDLER(SIOCSIWESSID,	(iw_handler)orinoco_ioctl_setessid),
	IW_HANDLER(SIOCGIWESSID,	(iw_handler)orinoco_ioctl_getessid),
	IW_HANDLER(SIOCSIWRATE,		(iw_handler)orinoco_ioctl_setrate),
	IW_HANDLER(SIOCGIWRATE,		(iw_handler)orinoco_ioctl_getrate),
	IW_HANDLER(SIOCSIWRTS,		(iw_handler)cfg80211_wext_siwrts),
	IW_HANDLER(SIOCGIWRTS,		(iw_handler)cfg80211_wext_giwrts),
	IW_HANDLER(SIOCSIWFRAG,		(iw_handler)cfg80211_wext_siwfrag),
	IW_HANDLER(SIOCGIWFRAG,		(iw_handler)cfg80211_wext_giwfrag),
	IW_HANDLER(SIOCGIWRETRY,	(iw_handler)cfg80211_wext_giwretry),
	IW_HANDLER(SIOCSIWENCODE,	(iw_handler)orinoco_ioctl_setiwencode),
	IW_HANDLER(SIOCGIWENCODE,	(iw_handler)orinoco_ioctl_getiwencode),
	IW_HANDLER(SIOCSIWPOWER,	(iw_handler)orinoco_ioctl_setpower),
	IW_HANDLER(SIOCGIWPOWER,	(iw_handler)orinoco_ioctl_getpower),
	IW_HANDLER(SIOCSIWGENIE,	orinoco_ioctl_set_genie),
	IW_HANDLER(SIOCGIWGENIE,	orinoco_ioctl_get_genie),
	IW_HANDLER(SIOCSIWMLME,		orinoco_ioctl_set_mlme),
	IW_HANDLER(SIOCSIWAUTH,		orinoco_ioctl_set_auth),
	IW_HANDLER(SIOCGIWAUTH,		orinoco_ioctl_get_auth),
	IW_HANDLER(SIOCSIWENCODEEXT,	orinoco_ioctl_set_encodeext),
	IW_HANDLER(SIOCGIWENCODEEXT,	orinoco_ioctl_get_encodeext),
};


/*
  Added typecasting since we no longer use iwreq_data -- Moustafa
 */
static const iw_handler	orinoco_private_handler[] = {
	[0] = (iw_handler)orinoco_ioctl_reset,
	[1] = (iw_handler)orinoco_ioctl_reset,
	[2] = (iw_handler)orinoco_ioctl_setport3,
	[3] = (iw_handler)orinoco_ioctl_getport3,
	[4] = (iw_handler)orinoco_ioctl_setpreamble,
	[5] = (iw_handler)orinoco_ioctl_getpreamble,
	[6] = (iw_handler)orinoco_ioctl_setibssport,
	[7] = (iw_handler)orinoco_ioctl_getibssport,
	[9] = (iw_handler)orinoco_ioctl_getrid,
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
