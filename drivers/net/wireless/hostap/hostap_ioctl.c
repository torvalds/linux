/* ioctl() (mostly Linux Wireless Extensions) routines for Host AP driver */

#include <linux/types.h>
#include <linux/smp_lock.h>
#include <linux/ethtool.h>
#include <net/ieee80211_crypt.h>

#include "hostap_wlan.h"
#include "hostap.h"
#include "hostap_ap.h"

static struct iw_statistics *hostap_get_wireless_stats(struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct iw_statistics *wstats;

	iface = netdev_priv(dev);
	local = iface->local;

	/* Why are we doing that ? Jean II */
	if (iface->type != HOSTAP_INTERFACE_MAIN)
		return NULL;

	wstats = &local->wstats;

	wstats->status = 0;
	wstats->discard.code =
		local->comm_tallies.rx_discards_wep_undecryptable;
	wstats->discard.misc =
		local->comm_tallies.rx_fcs_errors +
		local->comm_tallies.rx_discards_no_buffer +
		local->comm_tallies.tx_discards_wrong_sa;

	wstats->discard.retries =
		local->comm_tallies.tx_retry_limit_exceeded;
	wstats->discard.fragment =
		local->comm_tallies.rx_message_in_bad_msg_fragments;

	if (local->iw_mode != IW_MODE_MASTER &&
	    local->iw_mode != IW_MODE_REPEAT) {
		int update = 1;
#ifdef in_atomic
		/* RID reading might sleep and it must not be called in
		 * interrupt context or while atomic. However, this
		 * function seems to be called while atomic (at least in Linux
		 * 2.5.59). Update signal quality values only if in suitable
		 * context. Otherwise, previous values read from tick timer
		 * will be used. */
		if (in_atomic())
			update = 0;
#endif /* in_atomic */

		if (update && prism2_update_comms_qual(dev) == 0)
			wstats->qual.updated = IW_QUAL_ALL_UPDATED |
				IW_QUAL_DBM;

		wstats->qual.qual = local->comms_qual;
		wstats->qual.level = local->avg_signal;
		wstats->qual.noise = local->avg_noise;
	} else {
		wstats->qual.qual = 0;
		wstats->qual.level = 0;
		wstats->qual.noise = 0;
		wstats->qual.updated = IW_QUAL_ALL_INVALID;
	}

	return wstats;
}


static int prism2_get_datarates(struct net_device *dev, u8 *rates)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u8 buf[12];
	int len;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	len = local->func->get_rid(dev, HFA384X_RID_SUPPORTEDDATARATES, buf,
				   sizeof(buf), 0);
	if (len < 2)
		return 0;

	val = le16_to_cpu(*(u16 *) buf); /* string length */

	if (len - 2 < val || val > 10)
		return 0;

	memcpy(rates, buf + 2, val);
	return val;
}


static int prism2_get_name(struct net_device *dev,
			   struct iw_request_info *info,
			   char *name, char *extra)
{
	u8 rates[10];
	int len, i, over2 = 0;

	len = prism2_get_datarates(dev, rates);

	for (i = 0; i < len; i++) {
		if (rates[i] == 0x0b || rates[i] == 0x16) {
			over2 = 1;
			break;
		}
	}

	strcpy(name, over2 ? "IEEE 802.11b" : "IEEE 802.11-DS");

	return 0;
}


static void prism2_crypt_delayed_deinit(local_info_t *local,
					struct ieee80211_crypt_data **crypt)
{
	struct ieee80211_crypt_data *tmp;
	unsigned long flags;

	tmp = *crypt;
	*crypt = NULL;

	if (tmp == NULL)
		return;

	/* must not run ops->deinit() while there may be pending encrypt or
	 * decrypt operations. Use a list of delayed deinits to avoid needing
	 * locking. */

	spin_lock_irqsave(&local->lock, flags);
	list_add(&tmp->list, &local->crypt_deinit_list);
	if (!timer_pending(&local->crypt_deinit_timer)) {
		local->crypt_deinit_timer.expires = jiffies + HZ;
		add_timer(&local->crypt_deinit_timer);
	}
	spin_unlock_irqrestore(&local->lock, flags);
}


static int prism2_ioctl_siwencode(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *erq, char *keybuf)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int i;
	struct ieee80211_crypt_data **crypt;

	iface = netdev_priv(dev);
	local = iface->local;

	i = erq->flags & IW_ENCODE_INDEX;
	if (i < 1 || i > 4)
		i = local->tx_keyidx;
	else
		i--;
	if (i < 0 || i >= WEP_KEYS)
		return -EINVAL;

	crypt = &local->crypt[i];

	if (erq->flags & IW_ENCODE_DISABLED) {
		if (*crypt)
			prism2_crypt_delayed_deinit(local, crypt);
		goto done;
	}

	if (*crypt != NULL && (*crypt)->ops != NULL &&
	    strcmp((*crypt)->ops->name, "WEP") != 0) {
		/* changing to use WEP; deinit previously used algorithm */
		prism2_crypt_delayed_deinit(local, crypt);
	}

	if (*crypt == NULL) {
		struct ieee80211_crypt_data *new_crypt;

		/* take WEP into use */
		new_crypt = kzalloc(sizeof(struct ieee80211_crypt_data),
				GFP_KERNEL);
		if (new_crypt == NULL)
			return -ENOMEM;
		new_crypt->ops = ieee80211_get_crypto_ops("WEP");
		if (!new_crypt->ops) {
			request_module("ieee80211_crypt_wep");
			new_crypt->ops = ieee80211_get_crypto_ops("WEP");
		}
		if (new_crypt->ops)
			new_crypt->priv = new_crypt->ops->init(i);
		if (!new_crypt->ops || !new_crypt->priv) {
			kfree(new_crypt);
			new_crypt = NULL;

			printk(KERN_WARNING "%s: could not initialize WEP: "
			       "load module hostap_crypt_wep.o\n",
			       dev->name);
			return -EOPNOTSUPP;
		}
		*crypt = new_crypt;
	}

	if (erq->length > 0) {
		int len = erq->length <= 5 ? 5 : 13;
		int first = 1, j;
		if (len > erq->length)
			memset(keybuf + erq->length, 0, len - erq->length);
		(*crypt)->ops->set_key(keybuf, len, NULL, (*crypt)->priv);
		for (j = 0; j < WEP_KEYS; j++) {
			if (j != i && local->crypt[j]) {
				first = 0;
				break;
			}
		}
		if (first)
			local->tx_keyidx = i;
	} else {
		/* No key data - just set the default TX key index */
		local->tx_keyidx = i;
	}

 done:
	local->open_wep = erq->flags & IW_ENCODE_OPEN;

	if (hostap_set_encryption(local)) {
		printk(KERN_DEBUG "%s: set_encryption failed\n", dev->name);
		return -EINVAL;
	}

	/* Do not reset port0 if card is in Managed mode since resetting will
	 * generate new IEEE 802.11 authentication which may end up in looping
	 * with IEEE 802.1X. Prism2 documentation seem to require port reset
	 * after WEP configuration. However, keys are apparently changed at
	 * least in Managed mode. */
	if (local->iw_mode != IW_MODE_INFRA && local->func->reset_port(dev)) {
		printk(KERN_DEBUG "%s: reset_port failed\n", dev->name);
		return -EINVAL;
	}

	return 0;
}


static int prism2_ioctl_giwencode(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *erq, char *key)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int i, len;
	u16 val;
	struct ieee80211_crypt_data *crypt;

	iface = netdev_priv(dev);
	local = iface->local;

	i = erq->flags & IW_ENCODE_INDEX;
	if (i < 1 || i > 4)
		i = local->tx_keyidx;
	else
		i--;
	if (i < 0 || i >= WEP_KEYS)
		return -EINVAL;

	crypt = local->crypt[i];
	erq->flags = i + 1;

	if (crypt == NULL || crypt->ops == NULL) {
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		return 0;
	}

	if (strcmp(crypt->ops->name, "WEP") != 0) {
		/* only WEP is supported with wireless extensions, so just
		 * report that encryption is used */
		erq->length = 0;
		erq->flags |= IW_ENCODE_ENABLED;
		return 0;
	}

	/* Reads from HFA384X_RID_CNFDEFAULTKEY* return bogus values, so show
	 * the keys from driver buffer */
	len = crypt->ops->get_key(key, WEP_KEY_LEN, NULL, crypt->priv);
	erq->length = (len >= 0 ? len : 0);

	if (local->func->get_rid(dev, HFA384X_RID_CNFWEPFLAGS, &val, 2, 1) < 0)
	{
		printk("CNFWEPFLAGS reading failed\n");
		return -EOPNOTSUPP;
	}
	le16_to_cpus(&val);
	if (val & HFA384X_WEPFLAGS_PRIVACYINVOKED)
		erq->flags |= IW_ENCODE_ENABLED;
	else
		erq->flags |= IW_ENCODE_DISABLED;
	if (val & HFA384X_WEPFLAGS_EXCLUDEUNENCRYPTED)
		erq->flags |= IW_ENCODE_RESTRICTED;
	else
		erq->flags |= IW_ENCODE_OPEN;

	return 0;
}


static int hostap_set_rate(struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int ret, basic_rates;

	iface = netdev_priv(dev);
	local = iface->local;

	basic_rates = local->basic_rates & local->tx_rate_control;
	if (!basic_rates || basic_rates != local->basic_rates) {
		printk(KERN_INFO "%s: updating basic rate set automatically "
		       "to match with the new supported rate set\n",
		       dev->name);
		if (!basic_rates)
			basic_rates = local->tx_rate_control;

		local->basic_rates = basic_rates;
		if (hostap_set_word(dev, HFA384X_RID_CNFBASICRATES,
				    basic_rates))
			printk(KERN_WARNING "%s: failed to set "
			       "cnfBasicRates\n", dev->name);
	}

	ret = (hostap_set_word(dev, HFA384X_RID_TXRATECONTROL,
			       local->tx_rate_control) ||
	       hostap_set_word(dev, HFA384X_RID_CNFSUPPORTEDRATES,
			       local->tx_rate_control) ||
	       local->func->reset_port(dev));

	if (ret) {
		printk(KERN_WARNING "%s: TXRateControl/cnfSupportedRates "
		       "setting to 0x%x failed\n",
		       dev->name, local->tx_rate_control);
	}

	/* Update TX rate configuration for all STAs based on new operational
	 * rate set. */
	hostap_update_rates(local);

	return ret;
}


static int prism2_ioctl_siwrate(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *rrq, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	if (rrq->fixed) {
		switch (rrq->value) {
		case 11000000:
			local->tx_rate_control = HFA384X_RATES_11MBPS;
			break;
		case 5500000:
			local->tx_rate_control = HFA384X_RATES_5MBPS;
			break;
		case 2000000:
			local->tx_rate_control = HFA384X_RATES_2MBPS;
			break;
		case 1000000:
			local->tx_rate_control = HFA384X_RATES_1MBPS;
			break;
		default:
			local->tx_rate_control = HFA384X_RATES_1MBPS |
				HFA384X_RATES_2MBPS | HFA384X_RATES_5MBPS |
				HFA384X_RATES_11MBPS;
			break;
		}
	} else {
		switch (rrq->value) {
		case 11000000:
			local->tx_rate_control = HFA384X_RATES_1MBPS |
				HFA384X_RATES_2MBPS | HFA384X_RATES_5MBPS |
				HFA384X_RATES_11MBPS;
			break;
		case 5500000:
			local->tx_rate_control = HFA384X_RATES_1MBPS |
				HFA384X_RATES_2MBPS | HFA384X_RATES_5MBPS;
			break;
		case 2000000:
			local->tx_rate_control = HFA384X_RATES_1MBPS |
				HFA384X_RATES_2MBPS;
			break;
		case 1000000:
			local->tx_rate_control = HFA384X_RATES_1MBPS;
			break;
		default:
			local->tx_rate_control = HFA384X_RATES_1MBPS |
				HFA384X_RATES_2MBPS | HFA384X_RATES_5MBPS |
				HFA384X_RATES_11MBPS;
			break;
		}
	}

	return hostap_set_rate(dev);
}


static int prism2_ioctl_giwrate(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *rrq, char *extra)
{
	u16 val;
	struct hostap_interface *iface;
	local_info_t *local;
	int ret = 0;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->get_rid(dev, HFA384X_RID_TXRATECONTROL, &val, 2, 1) <
	    0)
		return -EINVAL;

	if ((val & 0x1) && (val > 1))
		rrq->fixed = 0;
	else
		rrq->fixed = 1;

	if (local->iw_mode == IW_MODE_MASTER && local->ap != NULL &&
	    !local->fw_tx_rate_control) {
		/* HFA384X_RID_CURRENTTXRATE seems to always be 2 Mbps in
		 * Host AP mode, so use the recorded TX rate of the last sent
		 * frame */
		rrq->value = local->ap->last_tx_rate > 0 ?
			local->ap->last_tx_rate * 100000 : 11000000;
		return 0;
	}

	if (local->func->get_rid(dev, HFA384X_RID_CURRENTTXRATE, &val, 2, 1) <
	    0)
		return -EINVAL;

	switch (val) {
	case HFA384X_RATES_1MBPS:
		rrq->value = 1000000;
		break;
	case HFA384X_RATES_2MBPS:
		rrq->value = 2000000;
		break;
	case HFA384X_RATES_5MBPS:
		rrq->value = 5500000;
		break;
	case HFA384X_RATES_11MBPS:
		rrq->value = 11000000;
		break;
	default:
		/* should not happen */
		rrq->value = 11000000;
		ret = -EINVAL;
		break;
	}

	return ret;
}


static int prism2_ioctl_siwsens(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *sens, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	/* Set the desired AP density */
	if (sens->value < 1 || sens->value > 3)
		return -EINVAL;

	if (hostap_set_word(dev, HFA384X_RID_CNFSYSTEMSCALE, sens->value) ||
	    local->func->reset_port(dev))
		return -EINVAL;

	return 0;
}

static int prism2_ioctl_giwsens(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *sens, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	/* Get the current AP density */
	if (local->func->get_rid(dev, HFA384X_RID_CNFSYSTEMSCALE, &val, 2, 1) <
	    0)
		return -EINVAL;

	sens->value = __le16_to_cpu(val);
	sens->fixed = 1;

	return 0;
}


/* Deprecated in new wireless extension API */
static int prism2_ioctl_giwaplist(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *data, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct sockaddr *addr;
	struct iw_quality *qual;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->iw_mode != IW_MODE_MASTER) {
		printk(KERN_DEBUG "SIOCGIWAPLIST is currently only supported "
		       "in Host AP mode\n");
		data->length = 0;
		return -EOPNOTSUPP;
	}

	addr = kmalloc(sizeof(struct sockaddr) * IW_MAX_AP, GFP_KERNEL);
	qual = kmalloc(sizeof(struct iw_quality) * IW_MAX_AP, GFP_KERNEL);
	if (addr == NULL || qual == NULL) {
		kfree(addr);
		kfree(qual);
		data->length = 0;
		return -ENOMEM;
	}

	data->length = prism2_ap_get_sta_qual(local, addr, qual, IW_MAX_AP, 1);

	memcpy(extra, &addr, sizeof(struct sockaddr) * data->length);
	data->flags = 1; /* has quality information */
	memcpy(extra + sizeof(struct sockaddr) * data->length, &qual,
	       sizeof(struct iw_quality) * data->length);

	kfree(addr);
	kfree(qual);
	return 0;
}


static int prism2_ioctl_siwrts(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_param *rts, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	if (rts->disabled)
		val = __constant_cpu_to_le16(2347);
	else if (rts->value < 0 || rts->value > 2347)
		return -EINVAL;
	else
		val = __cpu_to_le16(rts->value);

	if (local->func->set_rid(dev, HFA384X_RID_RTSTHRESHOLD, &val, 2) ||
	    local->func->reset_port(dev))
		return -EINVAL;

	local->rts_threshold = rts->value;

	return 0;
}

static int prism2_ioctl_giwrts(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_param *rts, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->get_rid(dev, HFA384X_RID_RTSTHRESHOLD, &val, 2, 1) <
	    0)
		return -EINVAL;

	rts->value = __le16_to_cpu(val);
	rts->disabled = (rts->value == 2347);
	rts->fixed = 1;

	return 0;
}


static int prism2_ioctl_siwfrag(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *rts, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	if (rts->disabled)
		val = __constant_cpu_to_le16(2346);
	else if (rts->value < 256 || rts->value > 2346)
		return -EINVAL;
	else
		val = __cpu_to_le16(rts->value & ~0x1); /* even numbers only */

	local->fragm_threshold = rts->value & ~0x1;
	if (local->func->set_rid(dev, HFA384X_RID_FRAGMENTATIONTHRESHOLD, &val,
				 2)
	    || local->func->reset_port(dev))
		return -EINVAL;

	return 0;
}

static int prism2_ioctl_giwfrag(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *rts, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->get_rid(dev, HFA384X_RID_FRAGMENTATIONTHRESHOLD,
				 &val, 2, 1) < 0)
		return -EINVAL;

	rts->value = __le16_to_cpu(val);
	rts->disabled = (rts->value == 2346);
	rts->fixed = 1;

	return 0;
}


#ifndef PRISM2_NO_STATION_MODES
static int hostap_join_ap(struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct hfa384x_join_request req;
	unsigned long flags;
	int i;
	struct hfa384x_hostscan_result *entry;

	iface = netdev_priv(dev);
	local = iface->local;

	memcpy(req.bssid, local->preferred_ap, ETH_ALEN);
	req.channel = 0;

	spin_lock_irqsave(&local->lock, flags);
	for (i = 0; i < local->last_scan_results_count; i++) {
		if (!local->last_scan_results)
			break;
		entry = &local->last_scan_results[i];
		if (memcmp(local->preferred_ap, entry->bssid, ETH_ALEN) == 0) {
			req.channel = entry->chid;
			break;
		}
	}
	spin_unlock_irqrestore(&local->lock, flags);

	if (local->func->set_rid(dev, HFA384X_RID_JOINREQUEST, &req,
				 sizeof(req))) {
		printk(KERN_DEBUG "%s: JoinRequest " MACSTR
		       " failed\n",
		       dev->name, MAC2STR(local->preferred_ap));
		return -1;
	}

	printk(KERN_DEBUG "%s: Trying to join BSSID " MACSTR "\n",
	       dev->name, MAC2STR(local->preferred_ap));

	return 0;
}
#endif /* PRISM2_NO_STATION_MODES */


static int prism2_ioctl_siwap(struct net_device *dev,
			      struct iw_request_info *info,
			      struct sockaddr *ap_addr, char *extra)
{
#ifdef PRISM2_NO_STATION_MODES
	return -EOPNOTSUPP;
#else /* PRISM2_NO_STATION_MODES */
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	memcpy(local->preferred_ap, &ap_addr->sa_data, ETH_ALEN);

	if (local->host_roaming == 1 && local->iw_mode == IW_MODE_INFRA) {
		struct hfa384x_scan_request scan_req;
		memset(&scan_req, 0, sizeof(scan_req));
		scan_req.channel_list = __constant_cpu_to_le16(0x3fff);
		scan_req.txrate = __constant_cpu_to_le16(HFA384X_RATES_1MBPS);
		if (local->func->set_rid(dev, HFA384X_RID_SCANREQUEST,
					 &scan_req, sizeof(scan_req))) {
			printk(KERN_DEBUG "%s: ScanResults request failed - "
			       "preferred AP delayed to next unsolicited "
			       "scan\n", dev->name);
		}
	} else if (local->host_roaming == 2 &&
		   local->iw_mode == IW_MODE_INFRA) {
		if (hostap_join_ap(dev))
			return -EINVAL;
	} else {
		printk(KERN_DEBUG "%s: Preferred AP (SIOCSIWAP) is used only "
		       "in Managed mode when host_roaming is enabled\n",
		       dev->name);
	}

	return 0;
#endif /* PRISM2_NO_STATION_MODES */
}

static int prism2_ioctl_giwap(struct net_device *dev,
			      struct iw_request_info *info,
			      struct sockaddr *ap_addr, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	ap_addr->sa_family = ARPHRD_ETHER;
	switch (iface->type) {
	case HOSTAP_INTERFACE_AP:
		memcpy(&ap_addr->sa_data, dev->dev_addr, ETH_ALEN);
		break;
	case HOSTAP_INTERFACE_STA:
		memcpy(&ap_addr->sa_data, local->assoc_ap_addr, ETH_ALEN);
		break;
	case HOSTAP_INTERFACE_WDS:
		memcpy(&ap_addr->sa_data, iface->u.wds.remote_addr, ETH_ALEN);
		break;
	default:
		if (local->func->get_rid(dev, HFA384X_RID_CURRENTBSSID,
					 &ap_addr->sa_data, ETH_ALEN, 1) < 0)
			return -EOPNOTSUPP;

		/* local->bssid is also updated in LinkStatus handler when in
		 * station mode */
		memcpy(local->bssid, &ap_addr->sa_data, ETH_ALEN);
		break;
	}

	return 0;
}


static int prism2_ioctl_siwnickn(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *nickname)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	memset(local->name, 0, sizeof(local->name));
	memcpy(local->name, nickname, data->length);
	local->name_set = 1;

	if (hostap_set_string(dev, HFA384X_RID_CNFOWNNAME, local->name) ||
	    local->func->reset_port(dev))
		return -EINVAL;

	return 0;
}

static int prism2_ioctl_giwnickn(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *nickname)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int len;
	char name[MAX_NAME_LEN + 3];
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	len = local->func->get_rid(dev, HFA384X_RID_CNFOWNNAME,
				   &name, MAX_NAME_LEN + 2, 0);
	val = __le16_to_cpu(*(u16 *) name);
	if (len > MAX_NAME_LEN + 2 || len < 0 || val > MAX_NAME_LEN)
		return -EOPNOTSUPP;

	name[val + 2] = '\0';
	data->length = val + 1;
	memcpy(nickname, name + 2, val + 1);

	return 0;
}


static int prism2_ioctl_siwfreq(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_freq *freq, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	/* freq => chan. */
	if (freq->e == 1 &&
	    freq->m / 100000 >= freq_list[0] &&
	    freq->m / 100000 <= freq_list[FREQ_COUNT - 1]) {
		int ch;
		int fr = freq->m / 100000;
		for (ch = 0; ch < FREQ_COUNT; ch++) {
			if (fr == freq_list[ch]) {
				freq->e = 0;
				freq->m = ch + 1;
				break;
			}
		}
	}

	if (freq->e != 0 || freq->m < 1 || freq->m > FREQ_COUNT ||
	    !(local->channel_mask & (1 << (freq->m - 1))))
		return -EINVAL;

	local->channel = freq->m; /* channel is used in prism2_setup_rids() */
	if (hostap_set_word(dev, HFA384X_RID_CNFOWNCHANNEL, local->channel) ||
	    local->func->reset_port(dev))
		return -EINVAL;

	return 0;
}

static int prism2_ioctl_giwfreq(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_freq *freq, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->get_rid(dev, HFA384X_RID_CURRENTCHANNEL, &val, 2, 1) <
	    0)
		return -EINVAL;

	le16_to_cpus(&val);
	if (val < 1 || val > FREQ_COUNT)
		return -EINVAL;

	freq->m = freq_list[val - 1] * 100000;
	freq->e = 1;

	return 0;
}


static void hostap_monitor_set_type(local_info_t *local)
{
	struct net_device *dev = local->ddev;

	if (dev == NULL)
		return;

	if (local->monitor_type == PRISM2_MONITOR_PRISM ||
	    local->monitor_type == PRISM2_MONITOR_CAPHDR) {
		dev->type = ARPHRD_IEEE80211_PRISM;
		dev->hard_header_parse =
			hostap_80211_prism_header_parse;
	} else {
		dev->type = ARPHRD_IEEE80211;
		dev->hard_header_parse = hostap_80211_header_parse;
	}
}


static int prism2_ioctl_siwessid(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *ssid)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	if (iface->type == HOSTAP_INTERFACE_WDS)
		return -EOPNOTSUPP;

	if (data->flags == 0)
		ssid[0] = '\0'; /* ANY */

	if (local->iw_mode == IW_MODE_MASTER && ssid[0] == '\0') {
		/* Setting SSID to empty string seems to kill the card in
		 * Host AP mode */
		printk(KERN_DEBUG "%s: Host AP mode does not support "
		       "'Any' essid\n", dev->name);
		return -EINVAL;
	}

	memcpy(local->essid, ssid, data->length);
	local->essid[data->length] = '\0';

	if ((!local->fw_ap &&
	     hostap_set_string(dev, HFA384X_RID_CNFDESIREDSSID, local->essid))
	    || hostap_set_string(dev, HFA384X_RID_CNFOWNSSID, local->essid) ||
	    local->func->reset_port(dev))
		return -EINVAL;

	return 0;
}

static int prism2_ioctl_giwessid(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *essid)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	if (iface->type == HOSTAP_INTERFACE_WDS)
		return -EOPNOTSUPP;

	data->flags = 1; /* active */
	if (local->iw_mode == IW_MODE_MASTER) {
		data->length = strlen(local->essid);
		memcpy(essid, local->essid, IW_ESSID_MAX_SIZE);
	} else {
		int len;
		char ssid[MAX_SSID_LEN + 2];
		memset(ssid, 0, sizeof(ssid));
		len = local->func->get_rid(dev, HFA384X_RID_CURRENTSSID,
					   &ssid, MAX_SSID_LEN + 2, 0);
		val = __le16_to_cpu(*(u16 *) ssid);
		if (len > MAX_SSID_LEN + 2 || len < 0 || val > MAX_SSID_LEN) {
			return -EOPNOTSUPP;
		}
		data->length = val;
		memcpy(essid, ssid + 2, IW_ESSID_MAX_SIZE);
	}

	return 0;
}


static int prism2_ioctl_giwrange(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct iw_range *range = (struct iw_range *) extra;
	u8 rates[10];
	u16 val;
	int i, len, over2;

	iface = netdev_priv(dev);
	local = iface->local;

	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	/* TODO: could fill num_txpower and txpower array with
	 * something; however, there are 128 different values.. */

	range->txpower_capa = IW_TXPOW_DBM;

	if (local->iw_mode == IW_MODE_INFRA || local->iw_mode == IW_MODE_ADHOC)
	{
		range->min_pmp = 1 * 1024;
		range->max_pmp = 65535 * 1024;
		range->min_pmt = 1 * 1024;
		range->max_pmt = 1000 * 1024;
		range->pmp_flags = IW_POWER_PERIOD;
		range->pmt_flags = IW_POWER_TIMEOUT;
		range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT |
			IW_POWER_UNICAST_R | IW_POWER_ALL_R;
	}

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 18;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;

	range->num_channels = FREQ_COUNT;

	val = 0;
	for (i = 0; i < FREQ_COUNT; i++) {
		if (local->channel_mask & (1 << i)) {
			range->freq[val].i = i + 1;
			range->freq[val].m = freq_list[i] * 100000;
			range->freq[val].e = 1;
			val++;
		}
		if (val == IW_MAX_FREQUENCIES)
			break;
	}
	range->num_frequency = val;

	if (local->sta_fw_ver >= PRISM2_FW_VER(1,3,1)) {
		range->max_qual.qual = 70; /* what is correct max? This was not
					    * documented exactly. At least
					    * 69 has been observed. */
		range->max_qual.level = 0; /* dB */
		range->max_qual.noise = 0; /* dB */

		/* What would be suitable values for "average/typical" qual? */
		range->avg_qual.qual = 20;
		range->avg_qual.level = -60;
		range->avg_qual.noise = -95;
	} else {
		range->max_qual.qual = 92; /* 0 .. 92 */
		range->max_qual.level = 154; /* 27 .. 154 */
		range->max_qual.noise = 154; /* 27 .. 154 */
	}
	range->sensitivity = 3;

	range->max_encoding_tokens = WEP_KEYS;
	range->num_encoding_sizes = 2;
	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;

	over2 = 0;
	len = prism2_get_datarates(dev, rates);
	range->num_bitrates = 0;
	for (i = 0; i < len; i++) {
		if (range->num_bitrates < IW_MAX_BITRATES) {
			range->bitrate[range->num_bitrates] =
				rates[i] * 500000;
			range->num_bitrates++;
		}
		if (rates[i] == 0x0b || rates[i] == 0x16)
			over2 = 1;
	}
	/* estimated maximum TCP throughput values (bps) */
	range->throughput = over2 ? 5500000 : 1500000;

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	/* Event capability (kernel + driver) */
	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
				IW_EVENT_CAPA_MASK(SIOCGIWTHRSPY) |
				IW_EVENT_CAPA_MASK(SIOCGIWAP) |
				IW_EVENT_CAPA_MASK(SIOCGIWSCAN));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;
	range->event_capa[4] = (IW_EVENT_CAPA_MASK(IWEVTXDROP) |
				IW_EVENT_CAPA_MASK(IWEVCUSTOM) |
				IW_EVENT_CAPA_MASK(IWEVREGISTERED) |
				IW_EVENT_CAPA_MASK(IWEVEXPIRED));

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
		IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

	return 0;
}


static int hostap_monitor_mode_enable(local_info_t *local)
{
	struct net_device *dev = local->dev;

	printk(KERN_DEBUG "Enabling monitor mode\n");
	hostap_monitor_set_type(local);

	if (hostap_set_word(dev, HFA384X_RID_CNFPORTTYPE,
			    HFA384X_PORTTYPE_PSEUDO_IBSS)) {
		printk(KERN_DEBUG "Port type setting for monitor mode "
		       "failed\n");
		return -EOPNOTSUPP;
	}

	/* Host decrypt is needed to get the IV and ICV fields;
	 * however, monitor mode seems to remove WEP flag from frame
	 * control field */
	if (hostap_set_word(dev, HFA384X_RID_CNFWEPFLAGS,
			    HFA384X_WEPFLAGS_HOSTENCRYPT |
			    HFA384X_WEPFLAGS_HOSTDECRYPT)) {
		printk(KERN_DEBUG "WEP flags setting failed\n");
		return -EOPNOTSUPP;
	}

	if (local->func->reset_port(dev) ||
	    local->func->cmd(dev, HFA384X_CMDCODE_TEST |
			     (HFA384X_TEST_MONITOR << 8),
			     0, NULL, NULL)) {
		printk(KERN_DEBUG "Setting monitor mode failed\n");
		return -EOPNOTSUPP;
	}

	return 0;
}


static int hostap_monitor_mode_disable(local_info_t *local)
{
	struct net_device *dev = local->ddev;

	if (dev == NULL)
		return -1;

	printk(KERN_DEBUG "%s: Disabling monitor mode\n", dev->name);
	dev->type = ARPHRD_ETHER;
	dev->hard_header_parse = local->saved_eth_header_parse;
	if (local->func->cmd(dev, HFA384X_CMDCODE_TEST |
			     (HFA384X_TEST_STOP << 8),
			     0, NULL, NULL))
		return -1;
	return hostap_set_encryption(local);
}


static int prism2_ioctl_siwmode(struct net_device *dev,
				struct iw_request_info *info,
				__u32 *mode, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int double_reset = 0;

	iface = netdev_priv(dev);
	local = iface->local;

	if (*mode != IW_MODE_ADHOC && *mode != IW_MODE_INFRA &&
	    *mode != IW_MODE_MASTER && *mode != IW_MODE_REPEAT &&
	    *mode != IW_MODE_MONITOR)
		return -EOPNOTSUPP;

#ifdef PRISM2_NO_STATION_MODES
	if (*mode == IW_MODE_ADHOC || *mode == IW_MODE_INFRA)
		return -EOPNOTSUPP;
#endif /* PRISM2_NO_STATION_MODES */

	if (*mode == local->iw_mode)
		return 0;

	if (*mode == IW_MODE_MASTER && local->essid[0] == '\0') {
		printk(KERN_WARNING "%s: empty SSID not allowed in Master "
		       "mode\n", dev->name);
		return -EINVAL;
	}

	if (local->iw_mode == IW_MODE_MONITOR)
		hostap_monitor_mode_disable(local);

	if ((local->iw_mode == IW_MODE_ADHOC ||
	     local->iw_mode == IW_MODE_MONITOR) && *mode == IW_MODE_MASTER) {
		/* There seems to be a firmware bug in at least STA f/w v1.5.6
		 * that leaves beacon frames to use IBSS type when moving from
		 * IBSS to Host AP mode. Doing double Port0 reset seems to be
		 * enough to workaround this. */
		double_reset = 1;
	}

	printk(KERN_DEBUG "prism2: %s: operating mode changed "
	       "%d -> %d\n", dev->name, local->iw_mode, *mode);
	local->iw_mode = *mode;

	if (local->iw_mode == IW_MODE_MONITOR)
		hostap_monitor_mode_enable(local);
	else if (local->iw_mode == IW_MODE_MASTER && !local->host_encrypt &&
		 !local->fw_encrypt_ok) {
		printk(KERN_DEBUG "%s: defaulting to host-based encryption as "
		       "a workaround for firmware bug in Host AP mode WEP\n",
		       dev->name);
		local->host_encrypt = 1;
	}

	if (hostap_set_word(dev, HFA384X_RID_CNFPORTTYPE,
			    hostap_get_porttype(local)))
		return -EOPNOTSUPP;

	if (local->func->reset_port(dev))
		return -EINVAL;
	if (double_reset && local->func->reset_port(dev))
		return -EINVAL;

	if (local->iw_mode != IW_MODE_INFRA && local->iw_mode != IW_MODE_ADHOC)
	{
		/* netif_carrier is used only in client modes for now, so make
		 * sure carrier is on when moving to non-client modes. */
		netif_carrier_on(local->dev);
		netif_carrier_on(local->ddev);
	}
	return 0;
}


static int prism2_ioctl_giwmode(struct net_device *dev,
				struct iw_request_info *info,
				__u32 *mode, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	switch (iface->type) {
	case HOSTAP_INTERFACE_STA:
		*mode = IW_MODE_INFRA;
		break;
	case HOSTAP_INTERFACE_WDS:
		*mode = IW_MODE_REPEAT;
		break;
	default:
		*mode = local->iw_mode;
		break;
	}
	return 0;
}


static int prism2_ioctl_siwpower(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *wrq, char *extra)
{
#ifdef PRISM2_NO_STATION_MODES
	return -EOPNOTSUPP;
#else /* PRISM2_NO_STATION_MODES */
	int ret = 0;

	if (wrq->disabled)
		return hostap_set_word(dev, HFA384X_RID_CNFPMENABLED, 0);

	switch (wrq->flags & IW_POWER_MODE) {
	case IW_POWER_UNICAST_R:
		ret = hostap_set_word(dev, HFA384X_RID_CNFMULTICASTRECEIVE, 0);
		if (ret)
			return ret;
		ret = hostap_set_word(dev, HFA384X_RID_CNFPMENABLED, 1);
		if (ret)
			return ret;
		break;
	case IW_POWER_ALL_R:
		ret = hostap_set_word(dev, HFA384X_RID_CNFMULTICASTRECEIVE, 1);
		if (ret)
			return ret;
		ret = hostap_set_word(dev, HFA384X_RID_CNFPMENABLED, 1);
		if (ret)
			return ret;
		break;
	case IW_POWER_ON:
		break;
	default:
		return -EINVAL;
	}

	if (wrq->flags & IW_POWER_TIMEOUT) {
		ret = hostap_set_word(dev, HFA384X_RID_CNFPMENABLED, 1);
		if (ret)
			return ret;
		ret = hostap_set_word(dev, HFA384X_RID_CNFPMHOLDOVERDURATION,
				      wrq->value / 1024);
		if (ret)
			return ret;
	}
	if (wrq->flags & IW_POWER_PERIOD) {
		ret = hostap_set_word(dev, HFA384X_RID_CNFPMENABLED, 1);
		if (ret)
			return ret;
		ret = hostap_set_word(dev, HFA384X_RID_CNFMAXSLEEPDURATION,
				      wrq->value / 1024);
		if (ret)
			return ret;
	}

	return ret;
#endif /* PRISM2_NO_STATION_MODES */
}


static int prism2_ioctl_giwpower(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq, char *extra)
{
#ifdef PRISM2_NO_STATION_MODES
	return -EOPNOTSUPP;
#else /* PRISM2_NO_STATION_MODES */
	struct hostap_interface *iface;
	local_info_t *local;
	u16 enable, mcast;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->get_rid(dev, HFA384X_RID_CNFPMENABLED, &enable, 2, 1)
	    < 0)
		return -EINVAL;

	if (!__le16_to_cpu(enable)) {
		rrq->disabled = 1;
		return 0;
	}

	rrq->disabled = 0;

	if ((rrq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		u16 timeout;
		if (local->func->get_rid(dev,
					 HFA384X_RID_CNFPMHOLDOVERDURATION,
					 &timeout, 2, 1) < 0)
			return -EINVAL;

		rrq->flags = IW_POWER_TIMEOUT;
		rrq->value = __le16_to_cpu(timeout) * 1024;
	} else {
		u16 period;
		if (local->func->get_rid(dev, HFA384X_RID_CNFMAXSLEEPDURATION,
					 &period, 2, 1) < 0)
			return -EINVAL;

		rrq->flags = IW_POWER_PERIOD;
		rrq->value = __le16_to_cpu(period) * 1024;
	}

	if (local->func->get_rid(dev, HFA384X_RID_CNFMULTICASTRECEIVE, &mcast,
				 2, 1) < 0)
		return -EINVAL;

	if (__le16_to_cpu(mcast))
		rrq->flags |= IW_POWER_ALL_R;
	else
		rrq->flags |= IW_POWER_UNICAST_R;

	return 0;
#endif /* PRISM2_NO_STATION_MODES */
}


static int prism2_ioctl_siwretry(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	if (rrq->disabled)
		return -EINVAL;

	/* setting retry limits is not supported with the current station
	 * firmware code; simulate this with alternative retry count for now */
	if (rrq->flags == IW_RETRY_LIMIT) {
		if (rrq->value < 0) {
			/* disable manual retry count setting and use firmware
			 * defaults */
			local->manual_retry_count = -1;
			local->tx_control &= ~HFA384X_TX_CTRL_ALT_RTRY;
		} else {
			if (hostap_set_word(dev, HFA384X_RID_CNFALTRETRYCOUNT,
					    rrq->value)) {
				printk(KERN_DEBUG "%s: Alternate retry count "
				       "setting to %d failed\n",
				       dev->name, rrq->value);
				return -EOPNOTSUPP;
			}

			local->manual_retry_count = rrq->value;
			local->tx_control |= HFA384X_TX_CTRL_ALT_RTRY;
		}
		return 0;
	}

	return -EOPNOTSUPP;

#if 0
	/* what could be done, if firmware would support this.. */

	if (rrq->flags & IW_RETRY_LIMIT) {
		if (rrq->flags & IW_RETRY_LONG)
			HFA384X_RID_LONGRETRYLIMIT = rrq->value;
		else if (rrq->flags & IW_RETRY_SHORT)
			HFA384X_RID_SHORTRETRYLIMIT = rrq->value;
		else {
			HFA384X_RID_LONGRETRYLIMIT = rrq->value;
			HFA384X_RID_SHORTRETRYLIMIT = rrq->value;
		}

	}

	if (rrq->flags & IW_RETRY_LIFETIME) {
		HFA384X_RID_MAXTRANSMITLIFETIME = rrq->value / 1024;
	}

	return 0;
#endif /* 0 */
}

static int prism2_ioctl_giwretry(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 shortretry, longretry, lifetime, altretry;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->get_rid(dev, HFA384X_RID_SHORTRETRYLIMIT, &shortretry,
				 2, 1) < 0 ||
	    local->func->get_rid(dev, HFA384X_RID_LONGRETRYLIMIT, &longretry,
				 2, 1) < 0 ||
	    local->func->get_rid(dev, HFA384X_RID_MAXTRANSMITLIFETIME,
				 &lifetime, 2, 1) < 0)
		return -EINVAL;

	le16_to_cpus(&shortretry);
	le16_to_cpus(&longretry);
	le16_to_cpus(&lifetime);

	rrq->disabled = 0;

	if ((rrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		rrq->flags = IW_RETRY_LIFETIME;
		rrq->value = lifetime * 1024;
	} else {
		if (local->manual_retry_count >= 0) {
			rrq->flags = IW_RETRY_LIMIT;
			if (local->func->get_rid(dev,
						 HFA384X_RID_CNFALTRETRYCOUNT,
						 &altretry, 2, 1) >= 0)
				rrq->value = le16_to_cpu(altretry);
			else
				rrq->value = local->manual_retry_count;
		} else if ((rrq->flags & IW_RETRY_LONG)) {
			rrq->flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
			rrq->value = longretry;
		} else {
			rrq->flags = IW_RETRY_LIMIT;
			rrq->value = shortretry;
			if (shortretry != longretry)
				rrq->flags |= IW_RETRY_SHORT;
		}
	}
	return 0;
}


/* Note! This TX power controlling is experimental and should not be used in
 * production use. It just sets raw power register and does not use any kind of
 * feedback information from the measured TX power (CR58). This is now
 * commented out to make sure that it is not used by accident. TX power
 * configuration will be enabled again after proper algorithm using feedback
 * has been implemented. */

#ifdef RAW_TXPOWER_SETTING
/* Map HFA386x's CR31 to and from dBm with some sort of ad hoc mapping..
 * This version assumes following mapping:
 * CR31 is 7-bit value with -64 to +63 range.
 * -64 is mapped into +20dBm and +63 into -43dBm.
 * This is certainly not an exact mapping for every card, but at least
 * increasing dBm value should correspond to increasing TX power.
 */

static int prism2_txpower_hfa386x_to_dBm(u16 val)
{
	signed char tmp;

	if (val > 255)
		val = 255;

	tmp = val;
	tmp >>= 2;

	return -12 - tmp;
}

static u16 prism2_txpower_dBm_to_hfa386x(int val)
{
	signed char tmp;

	if (val > 20)
		return 128;
	else if (val < -43)
		return 127;

	tmp = val;
	tmp = -12 - tmp;
	tmp <<= 2;

	return (unsigned char) tmp;
}
#endif /* RAW_TXPOWER_SETTING */


static int prism2_ioctl_siwtxpow(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
#ifdef RAW_TXPOWER_SETTING
	char *tmp;
#endif
	u16 val;
	int ret = 0;

	iface = netdev_priv(dev);
	local = iface->local;

	if (rrq->disabled) {
		if (local->txpower_type != PRISM2_TXPOWER_OFF) {
			val = 0xff; /* use all standby and sleep modes */
			ret = local->func->cmd(dev, HFA384X_CMDCODE_WRITEMIF,
					       HFA386X_CR_A_D_TEST_MODES2,
					       &val, NULL);
			printk(KERN_DEBUG "%s: Turning radio off: %s\n",
			       dev->name, ret ? "failed" : "OK");
			local->txpower_type = PRISM2_TXPOWER_OFF;
		}
		return (ret ? -EOPNOTSUPP : 0);
	}

	if (local->txpower_type == PRISM2_TXPOWER_OFF) {
		val = 0; /* disable all standby and sleep modes */
		ret = local->func->cmd(dev, HFA384X_CMDCODE_WRITEMIF,
				       HFA386X_CR_A_D_TEST_MODES2, &val, NULL);
		printk(KERN_DEBUG "%s: Turning radio on: %s\n",
		       dev->name, ret ? "failed" : "OK");
		local->txpower_type = PRISM2_TXPOWER_UNKNOWN;
	}

#ifdef RAW_TXPOWER_SETTING
	if (!rrq->fixed && local->txpower_type != PRISM2_TXPOWER_AUTO) {
		printk(KERN_DEBUG "Setting ALC on\n");
		val = HFA384X_TEST_CFG_BIT_ALC;
		local->func->cmd(dev, HFA384X_CMDCODE_TEST |
				 (HFA384X_TEST_CFG_BITS << 8), 1, &val, NULL);
		local->txpower_type = PRISM2_TXPOWER_AUTO;
		return 0;
	}

	if (local->txpower_type != PRISM2_TXPOWER_FIXED) {
		printk(KERN_DEBUG "Setting ALC off\n");
		val = HFA384X_TEST_CFG_BIT_ALC;
		local->func->cmd(dev, HFA384X_CMDCODE_TEST |
				 (HFA384X_TEST_CFG_BITS << 8), 0, &val, NULL);
			local->txpower_type = PRISM2_TXPOWER_FIXED;
	}

	if (rrq->flags == IW_TXPOW_DBM)
		tmp = "dBm";
	else if (rrq->flags == IW_TXPOW_MWATT)
		tmp = "mW";
	else
		tmp = "UNKNOWN";
	printk(KERN_DEBUG "Setting TX power to %d %s\n", rrq->value, tmp);

	if (rrq->flags != IW_TXPOW_DBM) {
		printk("SIOCSIWTXPOW with mW is not supported; use dBm\n");
		return -EOPNOTSUPP;
	}

	local->txpower = rrq->value;
	val = prism2_txpower_dBm_to_hfa386x(local->txpower);
	if (local->func->cmd(dev, HFA384X_CMDCODE_WRITEMIF,
			     HFA386X_CR_MANUAL_TX_POWER, &val, NULL))
		ret = -EOPNOTSUPP;
#else /* RAW_TXPOWER_SETTING */
	if (rrq->fixed)
		ret = -EOPNOTSUPP;
#endif /* RAW_TXPOWER_SETTING */

	return ret;
}

static int prism2_ioctl_giwtxpow(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rrq, char *extra)
{
#ifdef RAW_TXPOWER_SETTING
	struct hostap_interface *iface;
	local_info_t *local;
	u16 resp0;

	iface = netdev_priv(dev);
	local = iface->local;

	rrq->flags = IW_TXPOW_DBM;
	rrq->disabled = 0;
	rrq->fixed = 0;

	if (local->txpower_type == PRISM2_TXPOWER_AUTO) {
		if (local->func->cmd(dev, HFA384X_CMDCODE_READMIF,
				     HFA386X_CR_MANUAL_TX_POWER,
				     NULL, &resp0) == 0) {
			rrq->value = prism2_txpower_hfa386x_to_dBm(resp0);
		} else {
			/* Could not get real txpower; guess 15 dBm */
			rrq->value = 15;
		}
	} else if (local->txpower_type == PRISM2_TXPOWER_OFF) {
		rrq->value = 0;
		rrq->disabled = 1;
	} else if (local->txpower_type == PRISM2_TXPOWER_FIXED) {
		rrq->value = local->txpower;
		rrq->fixed = 1;
	} else {
		printk("SIOCGIWTXPOW - unknown txpower_type=%d\n",
		       local->txpower_type);
	}
	return 0;
#else /* RAW_TXPOWER_SETTING */
	return -EOPNOTSUPP;
#endif /* RAW_TXPOWER_SETTING */
}


#ifndef PRISM2_NO_STATION_MODES

/* HostScan request works with and without host_roaming mode. In addition, it
 * does not break current association. However, it requires newer station
 * firmware version (>= 1.3.1) than scan request. */
static int prism2_request_hostscan(struct net_device *dev,
				   u8 *ssid, u8 ssid_len)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct hfa384x_hostscan_request scan_req;

	iface = netdev_priv(dev);
	local = iface->local;

	memset(&scan_req, 0, sizeof(scan_req));
	scan_req.channel_list = cpu_to_le16(local->channel_mask &
					    local->scan_channel_mask);
	scan_req.txrate = __constant_cpu_to_le16(HFA384X_RATES_1MBPS);
	if (ssid) {
		if (ssid_len > 32)
			return -EINVAL;
		scan_req.target_ssid_len = cpu_to_le16(ssid_len);
		memcpy(scan_req.target_ssid, ssid, ssid_len);
	}

	if (local->func->set_rid(dev, HFA384X_RID_HOSTSCAN, &scan_req,
				 sizeof(scan_req))) {
		printk(KERN_DEBUG "%s: HOSTSCAN failed\n", dev->name);
		return -EINVAL;
	}
	return 0;
}


static int prism2_request_scan(struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct hfa384x_scan_request scan_req;
	int ret = 0;

	iface = netdev_priv(dev);
	local = iface->local;

	memset(&scan_req, 0, sizeof(scan_req));
	scan_req.channel_list = cpu_to_le16(local->channel_mask &
					    local->scan_channel_mask);
	scan_req.txrate = __constant_cpu_to_le16(HFA384X_RATES_1MBPS);

	/* FIX:
	 * It seems to be enough to set roaming mode for a short moment to
	 * host-based and then setup scanrequest data and return the mode to
	 * firmware-based.
	 *
	 * Master mode would need to drop to Managed mode for a short while
	 * to make scanning work.. Or sweep through the different channels and
	 * use passive scan based on beacons. */

	if (!local->host_roaming)
		hostap_set_word(dev, HFA384X_RID_CNFROAMINGMODE,
				HFA384X_ROAMING_HOST);

	if (local->func->set_rid(dev, HFA384X_RID_SCANREQUEST, &scan_req,
				 sizeof(scan_req))) {
		printk(KERN_DEBUG "SCANREQUEST failed\n");
		ret = -EINVAL;
	}

	if (!local->host_roaming)
		hostap_set_word(dev, HFA384X_RID_CNFROAMINGMODE,
				HFA384X_ROAMING_FIRMWARE);

	return 0;
}

#else /* !PRISM2_NO_STATION_MODES */

static inline int prism2_request_hostscan(struct net_device *dev,
					  u8 *ssid, u8 ssid_len)
{
	return -EOPNOTSUPP;
}


static inline int prism2_request_scan(struct net_device *dev)
{
	return -EOPNOTSUPP;
}

#endif /* !PRISM2_NO_STATION_MODES */


static int prism2_ioctl_siwscan(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int ret;
	u8 *ssid = NULL, ssid_len = 0;
	struct iw_scan_req *req = (struct iw_scan_req *) extra;

	iface = netdev_priv(dev);
	local = iface->local;

	if (data->length < sizeof(struct iw_scan_req))
		req = NULL;

	if (local->iw_mode == IW_MODE_MASTER) {
		/* In master mode, we just return the results of our local
		 * tables, so we don't need to start anything...
		 * Jean II */
		data->length = 0;
		return 0;
	}

	if (!local->dev_enabled)
		return -ENETDOWN;

	if (req && data->flags & IW_SCAN_THIS_ESSID) {
		ssid = req->essid;
		ssid_len = req->essid_len;

		if (ssid_len &&
		    ((local->iw_mode != IW_MODE_INFRA &&
		      local->iw_mode != IW_MODE_ADHOC) ||
		     (local->sta_fw_ver < PRISM2_FW_VER(1,3,1))))
			return -EOPNOTSUPP;
	}

	if (local->sta_fw_ver >= PRISM2_FW_VER(1,3,1))
		ret = prism2_request_hostscan(dev, ssid, ssid_len);
	else
		ret = prism2_request_scan(dev);

	if (ret == 0)
		local->scan_timestamp = jiffies;

	/* Could inquire F101, F103 or wait for SIOCGIWSCAN and read RID */

	return ret;
}


#ifndef PRISM2_NO_STATION_MODES
static char * __prism2_translate_scan(local_info_t *local,
				      struct hfa384x_hostscan_result *scan,
				      struct hostap_bss_info *bss,
				      char *current_ev, char *end_buf)
{
	int i, chan;
	struct iw_event iwe;
	char *current_val;
	u16 capabilities;
	u8 *pos;
	u8 *ssid, *bssid;
	size_t ssid_len;
	char *buf;

	if (bss) {
		ssid = bss->ssid;
		ssid_len = bss->ssid_len;
		bssid = bss->bssid;
	} else {
		ssid = scan->ssid;
		ssid_len = le16_to_cpu(scan->ssid_len);
		bssid = scan->bssid;
	}
	if (ssid_len > 32)
		ssid_len = 32;

	/* First entry *MUST* be the AP MAC address */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
					  IW_EV_ADDR_LEN);

	/* Other entries will be displayed in the order we give them */

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.length = ssid_len;
	iwe.u.data.flags = 1;
	current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe, ssid);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWMODE;
	if (bss) {
		capabilities = bss->capab_info;
	} else {
		capabilities = le16_to_cpu(scan->capability);
	}
	if (capabilities & (WLAN_CAPABILITY_ESS |
			    WLAN_CAPABILITY_IBSS)) {
		if (capabilities & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
						  IW_EV_UINT_LEN);
	}

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	if (scan) {
		chan = le16_to_cpu(scan->chid);
	} else if (bss) {
		chan = bss->chan;
	} else {
		chan = 0;
	}

	if (chan > 0) {
		iwe.u.freq.m = freq_list[chan - 1] * 100000;
		iwe.u.freq.e = 1;
		current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
						  IW_EV_FREQ_LEN);
	}

	if (scan) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVQUAL;
		if (local->last_scan_type == PRISM2_HOSTSCAN) {
			iwe.u.qual.level = le16_to_cpu(scan->sl);
			iwe.u.qual.noise = le16_to_cpu(scan->anl);
		} else {
			iwe.u.qual.level =
				HFA384X_LEVEL_TO_dBm(le16_to_cpu(scan->sl));
			iwe.u.qual.noise =
				HFA384X_LEVEL_TO_dBm(le16_to_cpu(scan->anl));
		}
		iwe.u.qual.updated = IW_QUAL_LEVEL_UPDATED
			| IW_QUAL_NOISE_UPDATED
			| IW_QUAL_QUAL_INVALID
			| IW_QUAL_DBM;
		current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
						  IW_EV_QUAL_LEN);
	}

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWENCODE;
	if (capabilities & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe, "");

	/* TODO: add SuppRates into BSS table */
	if (scan) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWRATE;
		current_val = current_ev + IW_EV_LCP_LEN;
		pos = scan->sup_rates;
		for (i = 0; i < sizeof(scan->sup_rates); i++) {
			if (pos[i] == 0)
				break;
			/* Bit rate given in 500 kb/s units (+ 0x80) */
			iwe.u.bitrate.value = ((pos[i] & 0x7f) * 500000);
			current_val = iwe_stream_add_value(
				current_ev, current_val, end_buf, &iwe,
				IW_EV_PARAM_LEN);
		}
		/* Check if we added any event */
		if ((current_val - current_ev) > IW_EV_LCP_LEN)
			current_ev = current_val;
	}

	/* TODO: add BeaconInt,resp_rate,atim into BSS table */
	buf = kmalloc(MAX_WPA_IE_LEN * 2 + 30, GFP_ATOMIC);
	if (buf && scan) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		sprintf(buf, "bcn_int=%d", le16_to_cpu(scan->beacon_interval));
		iwe.u.data.length = strlen(buf);
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
						  buf);

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		sprintf(buf, "resp_rate=%d", le16_to_cpu(scan->rate));
		iwe.u.data.length = strlen(buf);
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
						  buf);

		if (local->last_scan_type == PRISM2_HOSTSCAN &&
		    (capabilities & WLAN_CAPABILITY_IBSS)) {
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			sprintf(buf, "atim=%d", le16_to_cpu(scan->atim));
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(current_ev, end_buf,
							  &iwe, buf);
		}
	}
	kfree(buf);

	if (bss && bss->wpa_ie_len > 0 && bss->wpa_ie_len <= MAX_WPA_IE_LEN) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->wpa_ie_len;
		current_ev = iwe_stream_add_point(
			current_ev, end_buf, &iwe, bss->wpa_ie);
	}

	if (bss && bss->rsn_ie_len > 0 && bss->rsn_ie_len <= MAX_WPA_IE_LEN) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->rsn_ie_len;
		current_ev = iwe_stream_add_point(
			current_ev, end_buf, &iwe, bss->rsn_ie);
	}

	return current_ev;
}


/* Translate scan data returned from the card to a card independant
 * format that the Wireless Tools will understand - Jean II */
static inline int prism2_translate_scan(local_info_t *local,
					char *buffer, int buflen)
{
	struct hfa384x_hostscan_result *scan;
	int entry, hostscan;
	char *current_ev = buffer;
	char *end_buf = buffer + buflen;
	struct list_head *ptr;

	spin_lock_bh(&local->lock);

	list_for_each(ptr, &local->bss_list) {
		struct hostap_bss_info *bss;
		bss = list_entry(ptr, struct hostap_bss_info, list);
		bss->included = 0;
	}

	hostscan = local->last_scan_type == PRISM2_HOSTSCAN;
	for (entry = 0; entry < local->last_scan_results_count; entry++) {
		int found = 0;
		scan = &local->last_scan_results[entry];

		/* Report every SSID if the AP is using multiple SSIDs. If no
		 * BSS record is found (e.g., when WPA mode is disabled),
		 * report the AP once. */
		list_for_each(ptr, &local->bss_list) {
			struct hostap_bss_info *bss;
			bss = list_entry(ptr, struct hostap_bss_info, list);
			if (memcmp(bss->bssid, scan->bssid, ETH_ALEN) == 0) {
				bss->included = 1;
				current_ev = __prism2_translate_scan(
					local, scan, bss, current_ev, end_buf);
				found++;
			}
		}
		if (!found) {
			current_ev = __prism2_translate_scan(
				local, scan, NULL, current_ev, end_buf);
		}
		/* Check if there is space for one more entry */
		if ((end_buf - current_ev) <= IW_EV_ADDR_LEN) {
			/* Ask user space to try again with a bigger buffer */
			spin_unlock_bh(&local->lock);
			return -E2BIG;
		}
	}

	/* Prism2 firmware has limits (32 at least in some versions) for number
	 * of BSSes in scan results. Extend this limit by using local BSS list.
	 */
	list_for_each(ptr, &local->bss_list) {
		struct hostap_bss_info *bss;
		bss = list_entry(ptr, struct hostap_bss_info, list);
		if (bss->included)
			continue;
		current_ev = __prism2_translate_scan(local, NULL, bss,
						     current_ev, end_buf);
		/* Check if there is space for one more entry */
		if ((end_buf - current_ev) <= IW_EV_ADDR_LEN) {
			/* Ask user space to try again with a bigger buffer */
			spin_unlock_bh(&local->lock);
			return -E2BIG;
		}
	}

	spin_unlock_bh(&local->lock);

	return current_ev - buffer;
}
#endif /* PRISM2_NO_STATION_MODES */


static inline int prism2_ioctl_giwscan_sta(struct net_device *dev,
					   struct iw_request_info *info,
					   struct iw_point *data, char *extra)
{
#ifdef PRISM2_NO_STATION_MODES
	return -EOPNOTSUPP;
#else /* PRISM2_NO_STATION_MODES */
	struct hostap_interface *iface;
	local_info_t *local;
	int res;

	iface = netdev_priv(dev);
	local = iface->local;

	/* Wait until the scan is finished. We can probably do better
	 * than that - Jean II */
	if (local->scan_timestamp &&
	    time_before(jiffies, local->scan_timestamp + 3 * HZ)) {
		/* Important note : we don't want to block the caller
		 * until results are ready for various reasons.
		 * First, managing wait queues is complex and racy
		 * (there may be multiple simultaneous callers).
		 * Second, we grab some rtnetlink lock before comming
		 * here (in dev_ioctl()).
		 * Third, the caller can wait on the Wireless Event
		 * - Jean II */
		return -EAGAIN;
	}
	local->scan_timestamp = 0;

	res = prism2_translate_scan(local, extra, data->length);

	if (res >= 0) {
		data->length = res;
		return 0;
	} else {
		data->length = 0;
		return res;
	}
#endif /* PRISM2_NO_STATION_MODES */
}


static int prism2_ioctl_giwscan(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int res;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->iw_mode == IW_MODE_MASTER) {
		/* In MASTER mode, it doesn't make sense to go around
		 * scanning the frequencies and make the stations we serve
		 * wait when what the user is really interested about is the
		 * list of stations and access points we are talking to.
		 * So, just extract results from our cache...
		 * Jean II */

		/* Translate to WE format */
		res = prism2_ap_translate_scan(dev, extra);
		if (res >= 0) {
			printk(KERN_DEBUG "Scan result translation succeeded "
			       "(length=%d)\n", res);
			data->length = res;
			return 0;
		} else {
			printk(KERN_DEBUG
			       "Scan result translation failed (res=%d)\n",
			       res);
			data->length = 0;
			return res;
		}
	} else {
		/* Station mode */
		return prism2_ioctl_giwscan_sta(dev, info, data, extra);
	}
}


static const struct iw_priv_args prism2_priv[] = {
	{ PRISM2_IOCTL_MONITOR,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "monitor" },
	{ PRISM2_IOCTL_READMIF,
	  IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1,
	  IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, "readmif" },
	{ PRISM2_IOCTL_WRITEMIF,
	  IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 2, 0, "writemif" },
	{ PRISM2_IOCTL_RESET,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "reset" },
	{ PRISM2_IOCTL_INQUIRE,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "inquire" },
	{ PRISM2_IOCTL_SET_RID_WORD,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "set_rid_word" },
	{ PRISM2_IOCTL_MACCMD,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "maccmd" },
	{ PRISM2_IOCTL_WDS_ADD,
	  IW_PRIV_TYPE_ADDR | IW_PRIV_SIZE_FIXED | 1, 0, "wds_add" },
	{ PRISM2_IOCTL_WDS_DEL,
	  IW_PRIV_TYPE_ADDR | IW_PRIV_SIZE_FIXED | 1, 0, "wds_del" },
	{ PRISM2_IOCTL_ADDMAC,
	  IW_PRIV_TYPE_ADDR | IW_PRIV_SIZE_FIXED | 1, 0, "addmac" },
	{ PRISM2_IOCTL_DELMAC,
	  IW_PRIV_TYPE_ADDR | IW_PRIV_SIZE_FIXED | 1, 0, "delmac" },
	{ PRISM2_IOCTL_KICKMAC,
	  IW_PRIV_TYPE_ADDR | IW_PRIV_SIZE_FIXED | 1, 0, "kickmac" },
	/* --- raw access to sub-ioctls --- */
	{ PRISM2_IOCTL_PRISM2_PARAM,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "prism2_param" },
	{ PRISM2_IOCTL_GET_PRISM2_PARAM,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getprism2_param" },
	/* --- sub-ioctls handlers --- */
	{ PRISM2_IOCTL_PRISM2_PARAM,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "" },
	{ PRISM2_IOCTL_GET_PRISM2_PARAM,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "" },
	/* --- sub-ioctls definitions --- */
	{ PRISM2_PARAM_TXRATECTRL,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "txratectrl" },
	{ PRISM2_PARAM_TXRATECTRL,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gettxratectrl" },
	{ PRISM2_PARAM_BEACON_INT,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "beacon_int" },
	{ PRISM2_PARAM_BEACON_INT,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getbeacon_int" },
#ifndef PRISM2_NO_STATION_MODES
	{ PRISM2_PARAM_PSEUDO_IBSS,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "pseudo_ibss" },
	{ PRISM2_PARAM_PSEUDO_IBSS,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getpseudo_ibss" },
#endif /* PRISM2_NO_STATION_MODES */
	{ PRISM2_PARAM_ALC,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "alc" },
	{ PRISM2_PARAM_ALC,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getalc" },
	{ PRISM2_PARAM_DUMP,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "dump" },
	{ PRISM2_PARAM_DUMP,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getdump" },
	{ PRISM2_PARAM_OTHER_AP_POLICY,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "other_ap_policy" },
	{ PRISM2_PARAM_OTHER_AP_POLICY,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getother_ap_pol" },
	{ PRISM2_PARAM_AP_MAX_INACTIVITY,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "max_inactivity" },
	{ PRISM2_PARAM_AP_MAX_INACTIVITY,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getmax_inactivi" },
	{ PRISM2_PARAM_AP_BRIDGE_PACKETS,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "bridge_packets" },
	{ PRISM2_PARAM_AP_BRIDGE_PACKETS,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getbridge_packe" },
	{ PRISM2_PARAM_DTIM_PERIOD,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "dtim_period" },
	{ PRISM2_PARAM_DTIM_PERIOD,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getdtim_period" },
	{ PRISM2_PARAM_AP_NULLFUNC_ACK,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "nullfunc_ack" },
	{ PRISM2_PARAM_AP_NULLFUNC_ACK,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getnullfunc_ack" },
	{ PRISM2_PARAM_MAX_WDS,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "max_wds" },
	{ PRISM2_PARAM_MAX_WDS,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getmax_wds" },
	{ PRISM2_PARAM_AP_AUTOM_AP_WDS,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "autom_ap_wds" },
	{ PRISM2_PARAM_AP_AUTOM_AP_WDS,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getautom_ap_wds" },
	{ PRISM2_PARAM_AP_AUTH_ALGS,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "ap_auth_algs" },
	{ PRISM2_PARAM_AP_AUTH_ALGS,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getap_auth_algs" },
	{ PRISM2_PARAM_MONITOR_ALLOW_FCSERR,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "allow_fcserr" },
	{ PRISM2_PARAM_MONITOR_ALLOW_FCSERR,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getallow_fcserr" },
	{ PRISM2_PARAM_HOST_ENCRYPT,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "host_encrypt" },
	{ PRISM2_PARAM_HOST_ENCRYPT,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gethost_encrypt" },
	{ PRISM2_PARAM_HOST_DECRYPT,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "host_decrypt" },
	{ PRISM2_PARAM_HOST_DECRYPT,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gethost_decrypt" },
#ifndef PRISM2_NO_STATION_MODES
	{ PRISM2_PARAM_HOST_ROAMING,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "host_roaming" },
	{ PRISM2_PARAM_HOST_ROAMING,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gethost_roaming" },
#endif /* PRISM2_NO_STATION_MODES */
	{ PRISM2_PARAM_BCRX_STA_KEY,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "bcrx_sta_key" },
	{ PRISM2_PARAM_BCRX_STA_KEY,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getbcrx_sta_key" },
	{ PRISM2_PARAM_IEEE_802_1X,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "ieee_802_1x" },
	{ PRISM2_PARAM_IEEE_802_1X,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getieee_802_1x" },
	{ PRISM2_PARAM_ANTSEL_TX,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "antsel_tx" },
	{ PRISM2_PARAM_ANTSEL_TX,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getantsel_tx" },
	{ PRISM2_PARAM_ANTSEL_RX,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "antsel_rx" },
	{ PRISM2_PARAM_ANTSEL_RX,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getantsel_rx" },
	{ PRISM2_PARAM_MONITOR_TYPE,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "monitor_type" },
	{ PRISM2_PARAM_MONITOR_TYPE,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getmonitor_type" },
	{ PRISM2_PARAM_WDS_TYPE,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wds_type" },
	{ PRISM2_PARAM_WDS_TYPE,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getwds_type" },
	{ PRISM2_PARAM_HOSTSCAN,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "hostscan" },
	{ PRISM2_PARAM_HOSTSCAN,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gethostscan" },
	{ PRISM2_PARAM_AP_SCAN,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "ap_scan" },
	{ PRISM2_PARAM_AP_SCAN,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getap_scan" },
	{ PRISM2_PARAM_ENH_SEC,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "enh_sec" },
	{ PRISM2_PARAM_ENH_SEC,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getenh_sec" },
#ifdef PRISM2_IO_DEBUG
	{ PRISM2_PARAM_IO_DEBUG,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "io_debug" },
	{ PRISM2_PARAM_IO_DEBUG,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getio_debug" },
#endif /* PRISM2_IO_DEBUG */
	{ PRISM2_PARAM_BASIC_RATES,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "basic_rates" },
	{ PRISM2_PARAM_BASIC_RATES,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getbasic_rates" },
	{ PRISM2_PARAM_OPER_RATES,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "oper_rates" },
	{ PRISM2_PARAM_OPER_RATES,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getoper_rates" },
	{ PRISM2_PARAM_HOSTAPD,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "hostapd" },
	{ PRISM2_PARAM_HOSTAPD,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gethostapd" },
	{ PRISM2_PARAM_HOSTAPD_STA,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "hostapd_sta" },
	{ PRISM2_PARAM_HOSTAPD_STA,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gethostapd_sta" },
	{ PRISM2_PARAM_WPA,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wpa" },
	{ PRISM2_PARAM_WPA,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getwpa" },
	{ PRISM2_PARAM_PRIVACY_INVOKED,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "privacy_invoked" },
	{ PRISM2_PARAM_PRIVACY_INVOKED,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getprivacy_invo" },
	{ PRISM2_PARAM_TKIP_COUNTERMEASURES,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "tkip_countermea" },
	{ PRISM2_PARAM_TKIP_COUNTERMEASURES,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gettkip_counter" },
	{ PRISM2_PARAM_DROP_UNENCRYPTED,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "drop_unencrypte" },
	{ PRISM2_PARAM_DROP_UNENCRYPTED,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getdrop_unencry" },
	{ PRISM2_PARAM_SCAN_CHANNEL_MASK,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "scan_channels" },
	{ PRISM2_PARAM_SCAN_CHANNEL_MASK,
	  0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getscan_channel" },
};


static int prism2_ioctl_priv_inquire(struct net_device *dev, int *i)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->cmd(dev, HFA384X_CMDCODE_INQUIRE, *i, NULL, NULL))
		return -EOPNOTSUPP;

	return 0;
}


static int prism2_ioctl_priv_prism2_param(struct net_device *dev,
					  struct iw_request_info *info,
					  void *wrqu, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int *i = (int *) extra;
	int param = *i;
	int value = *(i + 1);
	int ret = 0;
	u16 val;

	iface = netdev_priv(dev);
	local = iface->local;

	switch (param) {
	case PRISM2_PARAM_TXRATECTRL:
		local->fw_tx_rate_control = value;
		break;

	case PRISM2_PARAM_BEACON_INT:
		if (hostap_set_word(dev, HFA384X_RID_CNFBEACONINT, value) ||
		    local->func->reset_port(dev))
			ret = -EINVAL;
		else
			local->beacon_int = value;
		break;

#ifndef PRISM2_NO_STATION_MODES
	case PRISM2_PARAM_PSEUDO_IBSS:
		if (value == local->pseudo_adhoc)
			break;

		if (value != 0 && value != 1) {
			ret = -EINVAL;
			break;
		}

		printk(KERN_DEBUG "prism2: %s: pseudo IBSS change %d -> %d\n",
		       dev->name, local->pseudo_adhoc, value);
		local->pseudo_adhoc = value;
		if (local->iw_mode != IW_MODE_ADHOC)
			break;

		if (hostap_set_word(dev, HFA384X_RID_CNFPORTTYPE,
				    hostap_get_porttype(local))) {
			ret = -EOPNOTSUPP;
			break;
		}

		if (local->func->reset_port(dev))
			ret = -EINVAL;
		break;
#endif /* PRISM2_NO_STATION_MODES */

	case PRISM2_PARAM_ALC:
		printk(KERN_DEBUG "%s: %s ALC\n", dev->name,
		       value == 0 ? "Disabling" : "Enabling");
		val = HFA384X_TEST_CFG_BIT_ALC;
		local->func->cmd(dev, HFA384X_CMDCODE_TEST |
				 (HFA384X_TEST_CFG_BITS << 8),
				 value == 0 ? 0 : 1, &val, NULL);
		break;

	case PRISM2_PARAM_DUMP:
		local->frame_dump = value;
		break;

	case PRISM2_PARAM_OTHER_AP_POLICY:
		if (value < 0 || value > 3) {
			ret = -EINVAL;
			break;
		}
		if (local->ap != NULL)
			local->ap->ap_policy = value;
		break;

	case PRISM2_PARAM_AP_MAX_INACTIVITY:
		if (value < 0 || value > 7 * 24 * 60 * 60) {
			ret = -EINVAL;
			break;
		}
		if (local->ap != NULL)
			local->ap->max_inactivity = value * HZ;
		break;

	case PRISM2_PARAM_AP_BRIDGE_PACKETS:
		if (local->ap != NULL)
			local->ap->bridge_packets = value;
		break;

	case PRISM2_PARAM_DTIM_PERIOD:
		if (value < 0 || value > 65535) {
			ret = -EINVAL;
			break;
		}
		if (hostap_set_word(dev, HFA384X_RID_CNFOWNDTIMPERIOD, value)
		    || local->func->reset_port(dev))
			ret = -EINVAL;
		else
			local->dtim_period = value;
		break;

	case PRISM2_PARAM_AP_NULLFUNC_ACK:
		if (local->ap != NULL)
			local->ap->nullfunc_ack = value;
		break;

	case PRISM2_PARAM_MAX_WDS:
		local->wds_max_connections = value;
		break;

	case PRISM2_PARAM_AP_AUTOM_AP_WDS:
		if (local->ap != NULL) {
			if (!local->ap->autom_ap_wds && value) {
				/* add WDS link to all APs in STA table */
				hostap_add_wds_links(local);
			}
			local->ap->autom_ap_wds = value;
		}
		break;

	case PRISM2_PARAM_AP_AUTH_ALGS:
		local->auth_algs = value;
		if (hostap_set_auth_algs(local))
			ret = -EINVAL;
		break;

	case PRISM2_PARAM_MONITOR_ALLOW_FCSERR:
		local->monitor_allow_fcserr = value;
		break;

	case PRISM2_PARAM_HOST_ENCRYPT:
		local->host_encrypt = value;
		if (hostap_set_encryption(local) ||
		    local->func->reset_port(dev))
			ret = -EINVAL;
		break;

	case PRISM2_PARAM_HOST_DECRYPT:
		local->host_decrypt = value;
		if (hostap_set_encryption(local) ||
		    local->func->reset_port(dev))
			ret = -EINVAL;
		break;

#ifndef PRISM2_NO_STATION_MODES
	case PRISM2_PARAM_HOST_ROAMING:
		if (value < 0 || value > 2) {
			ret = -EINVAL;
			break;
		}
		local->host_roaming = value;
		if (hostap_set_roaming(local) || local->func->reset_port(dev))
			ret = -EINVAL;
		break;
#endif /* PRISM2_NO_STATION_MODES */

	case PRISM2_PARAM_BCRX_STA_KEY:
		local->bcrx_sta_key = value;
		break;

	case PRISM2_PARAM_IEEE_802_1X:
		local->ieee_802_1x = value;
		break;

	case PRISM2_PARAM_ANTSEL_TX:
		if (value < 0 || value > HOSTAP_ANTSEL_HIGH) {
			ret = -EINVAL;
			break;
		}
		local->antsel_tx = value;
		hostap_set_antsel(local);
		break;

	case PRISM2_PARAM_ANTSEL_RX:
		if (value < 0 || value > HOSTAP_ANTSEL_HIGH) {
			ret = -EINVAL;
			break;
		}
		local->antsel_rx = value;
		hostap_set_antsel(local);
		break;

	case PRISM2_PARAM_MONITOR_TYPE:
		if (value != PRISM2_MONITOR_80211 &&
		    value != PRISM2_MONITOR_CAPHDR &&
		    value != PRISM2_MONITOR_PRISM) {
			ret = -EINVAL;
			break;
		}
		local->monitor_type = value;
		if (local->iw_mode == IW_MODE_MONITOR)
			hostap_monitor_set_type(local);
		break;

	case PRISM2_PARAM_WDS_TYPE:
		local->wds_type = value;
		break;

	case PRISM2_PARAM_HOSTSCAN:
	{
		struct hfa384x_hostscan_request scan_req;
		u16 rate;

		memset(&scan_req, 0, sizeof(scan_req));
		scan_req.channel_list = __constant_cpu_to_le16(0x3fff);
		switch (value) {
		case 1: rate = HFA384X_RATES_1MBPS; break;
		case 2: rate = HFA384X_RATES_2MBPS; break;
		case 3: rate = HFA384X_RATES_5MBPS; break;
		case 4: rate = HFA384X_RATES_11MBPS; break;
		default: rate = HFA384X_RATES_1MBPS; break;
		}
		scan_req.txrate = cpu_to_le16(rate);
		/* leave SSID empty to accept all SSIDs */

		if (local->iw_mode == IW_MODE_MASTER) {
			if (hostap_set_word(dev, HFA384X_RID_CNFPORTTYPE,
					    HFA384X_PORTTYPE_BSS) ||
			    local->func->reset_port(dev))
				printk(KERN_DEBUG "Leaving Host AP mode "
				       "for HostScan failed\n");
		}

		if (local->func->set_rid(dev, HFA384X_RID_HOSTSCAN, &scan_req,
					 sizeof(scan_req))) {
			printk(KERN_DEBUG "HOSTSCAN failed\n");
			ret = -EINVAL;
		}
		if (local->iw_mode == IW_MODE_MASTER) {
			wait_queue_t __wait;
			init_waitqueue_entry(&__wait, current);
			add_wait_queue(&local->hostscan_wq, &__wait);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ);
			if (signal_pending(current))
				ret = -EINTR;
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&local->hostscan_wq, &__wait);

			if (hostap_set_word(dev, HFA384X_RID_CNFPORTTYPE,
					    HFA384X_PORTTYPE_HOSTAP) ||
			    local->func->reset_port(dev))
				printk(KERN_DEBUG "Returning to Host AP mode "
				       "after HostScan failed\n");
		}
		break;
	}

	case PRISM2_PARAM_AP_SCAN:
		local->passive_scan_interval = value;
		if (timer_pending(&local->passive_scan_timer))
			del_timer(&local->passive_scan_timer);
		if (value > 0) {
			local->passive_scan_timer.expires = jiffies +
				local->passive_scan_interval * HZ;
			add_timer(&local->passive_scan_timer);
		}
		break;

	case PRISM2_PARAM_ENH_SEC:
		if (value < 0 || value > 3) {
			ret = -EINVAL;
			break;
		}
		local->enh_sec = value;
		if (hostap_set_word(dev, HFA384X_RID_CNFENHSECURITY,
				    local->enh_sec) ||
		    local->func->reset_port(dev)) {
			printk(KERN_INFO "%s: cnfEnhSecurity requires STA f/w "
			       "1.6.3 or newer\n", dev->name);
			ret = -EOPNOTSUPP;
		}
		break;

#ifdef PRISM2_IO_DEBUG
	case PRISM2_PARAM_IO_DEBUG:
		local->io_debug_enabled = value;
		break;
#endif /* PRISM2_IO_DEBUG */

	case PRISM2_PARAM_BASIC_RATES:
		if ((value & local->tx_rate_control) != value || value == 0) {
			printk(KERN_INFO "%s: invalid basic rate set - basic "
			       "rates must be in supported rate set\n",
			       dev->name);
			ret = -EINVAL;
			break;
		}
		local->basic_rates = value;
		if (hostap_set_word(dev, HFA384X_RID_CNFBASICRATES,
				    local->basic_rates) ||
		    local->func->reset_port(dev))
			ret = -EINVAL;
		break;

	case PRISM2_PARAM_OPER_RATES:
		local->tx_rate_control = value;
		if (hostap_set_rate(dev))
			ret = -EINVAL;
		break;

	case PRISM2_PARAM_HOSTAPD:
		ret = hostap_set_hostapd(local, value, 1);
		break;

	case PRISM2_PARAM_HOSTAPD_STA:
		ret = hostap_set_hostapd_sta(local, value, 1);
		break;

	case PRISM2_PARAM_WPA:
		local->wpa = value;
		if (local->sta_fw_ver < PRISM2_FW_VER(1,7,0))
			ret = -EOPNOTSUPP;
		else if (hostap_set_word(dev, HFA384X_RID_SSNHANDLINGMODE,
					 value ? 1 : 0))
			ret = -EINVAL;
		break;

	case PRISM2_PARAM_PRIVACY_INVOKED:
		local->privacy_invoked = value;
		if (hostap_set_encryption(local) ||
		    local->func->reset_port(dev))
			ret = -EINVAL;
		break;

	case PRISM2_PARAM_TKIP_COUNTERMEASURES:
		local->tkip_countermeasures = value;
		break;

	case PRISM2_PARAM_DROP_UNENCRYPTED:
		local->drop_unencrypted = value;
		break;

	case PRISM2_PARAM_SCAN_CHANNEL_MASK:
		local->scan_channel_mask = value;
		break;

	default:
		printk(KERN_DEBUG "%s: prism2_param: unknown param %d\n",
		       dev->name, param);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}


static int prism2_ioctl_priv_get_prism2_param(struct net_device *dev,
					      struct iw_request_info *info,
					      void *wrqu, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int *param = (int *) extra;
	int ret = 0;

	iface = netdev_priv(dev);
	local = iface->local;

	switch (*param) {
	case PRISM2_PARAM_TXRATECTRL:
		*param = local->fw_tx_rate_control;
		break;

	case PRISM2_PARAM_BEACON_INT:
		*param = local->beacon_int;
		break;

	case PRISM2_PARAM_PSEUDO_IBSS:
		*param = local->pseudo_adhoc;
		break;

	case PRISM2_PARAM_ALC:
		ret = -EOPNOTSUPP; /* FIX */
		break;

	case PRISM2_PARAM_DUMP:
		*param = local->frame_dump;
		break;

	case PRISM2_PARAM_OTHER_AP_POLICY:
		if (local->ap != NULL)
			*param = local->ap->ap_policy;
		else
			ret = -EOPNOTSUPP;
		break;

	case PRISM2_PARAM_AP_MAX_INACTIVITY:
		if (local->ap != NULL)
			*param = local->ap->max_inactivity / HZ;
		else
			ret = -EOPNOTSUPP;
		break;

	case PRISM2_PARAM_AP_BRIDGE_PACKETS:
		if (local->ap != NULL)
			*param = local->ap->bridge_packets;
		else
			ret = -EOPNOTSUPP;
		break;

	case PRISM2_PARAM_DTIM_PERIOD:
		*param = local->dtim_period;
		break;

	case PRISM2_PARAM_AP_NULLFUNC_ACK:
		if (local->ap != NULL)
			*param = local->ap->nullfunc_ack;
		else
			ret = -EOPNOTSUPP;
		break;

	case PRISM2_PARAM_MAX_WDS:
		*param = local->wds_max_connections;
		break;

	case PRISM2_PARAM_AP_AUTOM_AP_WDS:
		if (local->ap != NULL)
			*param = local->ap->autom_ap_wds;
		else
			ret = -EOPNOTSUPP;
		break;

	case PRISM2_PARAM_AP_AUTH_ALGS:
		*param = local->auth_algs;
		break;

	case PRISM2_PARAM_MONITOR_ALLOW_FCSERR:
		*param = local->monitor_allow_fcserr;
		break;

	case PRISM2_PARAM_HOST_ENCRYPT:
		*param = local->host_encrypt;
		break;

	case PRISM2_PARAM_HOST_DECRYPT:
		*param = local->host_decrypt;
		break;

	case PRISM2_PARAM_HOST_ROAMING:
		*param = local->host_roaming;
		break;

	case PRISM2_PARAM_BCRX_STA_KEY:
		*param = local->bcrx_sta_key;
		break;

	case PRISM2_PARAM_IEEE_802_1X:
		*param = local->ieee_802_1x;
		break;

	case PRISM2_PARAM_ANTSEL_TX:
		*param = local->antsel_tx;
		break;

	case PRISM2_PARAM_ANTSEL_RX:
		*param = local->antsel_rx;
		break;

	case PRISM2_PARAM_MONITOR_TYPE:
		*param = local->monitor_type;
		break;

	case PRISM2_PARAM_WDS_TYPE:
		*param = local->wds_type;
		break;

	case PRISM2_PARAM_HOSTSCAN:
		ret = -EOPNOTSUPP;
		break;

	case PRISM2_PARAM_AP_SCAN:
		*param = local->passive_scan_interval;
		break;

	case PRISM2_PARAM_ENH_SEC:
		*param = local->enh_sec;
		break;

#ifdef PRISM2_IO_DEBUG
	case PRISM2_PARAM_IO_DEBUG:
		*param = local->io_debug_enabled;
		break;
#endif /* PRISM2_IO_DEBUG */

	case PRISM2_PARAM_BASIC_RATES:
		*param = local->basic_rates;
		break;

	case PRISM2_PARAM_OPER_RATES:
		*param = local->tx_rate_control;
		break;

	case PRISM2_PARAM_HOSTAPD:
		*param = local->hostapd;
		break;

	case PRISM2_PARAM_HOSTAPD_STA:
		*param = local->hostapd_sta;
		break;

	case PRISM2_PARAM_WPA:
		if (local->sta_fw_ver < PRISM2_FW_VER(1,7,0))
			ret = -EOPNOTSUPP;
		*param = local->wpa;
		break;

	case PRISM2_PARAM_PRIVACY_INVOKED:
		*param = local->privacy_invoked;
		break;

	case PRISM2_PARAM_TKIP_COUNTERMEASURES:
		*param = local->tkip_countermeasures;
		break;

	case PRISM2_PARAM_DROP_UNENCRYPTED:
		*param = local->drop_unencrypted;
		break;

	case PRISM2_PARAM_SCAN_CHANNEL_MASK:
		*param = local->scan_channel_mask;
		break;

	default:
		printk(KERN_DEBUG "%s: get_prism2_param: unknown param %d\n",
		       dev->name, *param);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}


static int prism2_ioctl_priv_readmif(struct net_device *dev,
				     struct iw_request_info *info,
				     void *wrqu, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 resp0;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->cmd(dev, HFA384X_CMDCODE_READMIF, *extra, NULL,
			     &resp0))
		return -EOPNOTSUPP;
	else
		*extra = resp0;

	return 0;
}


static int prism2_ioctl_priv_writemif(struct net_device *dev,
				      struct iw_request_info *info,
				      void *wrqu, char *extra)
{
	struct hostap_interface *iface;
	local_info_t *local;
	u16 cr, val;

	iface = netdev_priv(dev);
	local = iface->local;

	cr = *extra;
	val = *(extra + 1);
	if (local->func->cmd(dev, HFA384X_CMDCODE_WRITEMIF, cr, &val, NULL))
		return -EOPNOTSUPP;

	return 0;
}


static int prism2_ioctl_priv_monitor(struct net_device *dev, int *i)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int ret = 0;
	u32 mode;

	iface = netdev_priv(dev);
	local = iface->local;

	printk(KERN_DEBUG "%s: process %d (%s) used deprecated iwpriv monitor "
	       "- update software to use iwconfig mode monitor\n",
	       dev->name, current->pid, current->comm);

	/* Backward compatibility code - this can be removed at some point */

	if (*i == 0) {
		/* Disable monitor mode - old mode was not saved, so go to
		 * Master mode */
		mode = IW_MODE_MASTER;
		ret = prism2_ioctl_siwmode(dev, NULL, &mode, NULL);
	} else if (*i == 1) {
		/* netlink socket mode is not supported anymore since it did
		 * not separate different devices from each other and was not
		 * best method for delivering large amount of packets to
		 * user space */
		ret = -EOPNOTSUPP;
	} else if (*i == 2 || *i == 3) {
		switch (*i) {
		case 2:
			local->monitor_type = PRISM2_MONITOR_80211;
			break;
		case 3:
			local->monitor_type = PRISM2_MONITOR_PRISM;
			break;
		}
		mode = IW_MODE_MONITOR;
		ret = prism2_ioctl_siwmode(dev, NULL, &mode, NULL);
		hostap_monitor_mode_enable(local);
	} else
		ret = -EINVAL;

	return ret;
}


static int prism2_ioctl_priv_reset(struct net_device *dev, int *i)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	printk(KERN_DEBUG "%s: manual reset request(%d)\n", dev->name, *i);
	switch (*i) {
	case 0:
		/* Disable and enable card */
		local->func->hw_shutdown(dev, 1);
		local->func->hw_config(dev, 0);
		break;

	case 1:
		/* COR sreset */
		local->func->hw_reset(dev);
		break;

	case 2:
		/* Disable and enable port 0 */
		local->func->reset_port(dev);
		break;

	case 3:
		prism2_sta_deauth(local, WLAN_REASON_DEAUTH_LEAVING);
		if (local->func->cmd(dev, HFA384X_CMDCODE_DISABLE, 0, NULL,
				     NULL))
			return -EINVAL;
		break;

	case 4:
		if (local->func->cmd(dev, HFA384X_CMDCODE_ENABLE, 0, NULL,
				     NULL))
			return -EINVAL;
		break;

	default:
		printk(KERN_DEBUG "Unknown reset request %d\n", *i);
		return -EOPNOTSUPP;
	}

	return 0;
}


static int prism2_ioctl_priv_set_rid_word(struct net_device *dev, int *i)
{
	int rid = *i;
	int value = *(i + 1);

	printk(KERN_DEBUG "%s: Set RID[0x%X] = %d\n", dev->name, rid, value);

	if (hostap_set_word(dev, rid, value))
		return -EINVAL;

	return 0;
}


#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
static int ap_mac_cmd_ioctl(local_info_t *local, int *cmd)
{
	int ret = 0;

	switch (*cmd) {
	case AP_MAC_CMD_POLICY_OPEN:
		local->ap->mac_restrictions.policy = MAC_POLICY_OPEN;
		break;
	case AP_MAC_CMD_POLICY_ALLOW:
		local->ap->mac_restrictions.policy = MAC_POLICY_ALLOW;
		break;
	case AP_MAC_CMD_POLICY_DENY:
		local->ap->mac_restrictions.policy = MAC_POLICY_DENY;
		break;
	case AP_MAC_CMD_FLUSH:
		ap_control_flush_macs(&local->ap->mac_restrictions);
		break;
	case AP_MAC_CMD_KICKALL:
		ap_control_kickall(local->ap);
		hostap_deauth_all_stas(local->dev, local->ap, 0);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


#ifdef PRISM2_DOWNLOAD_SUPPORT
static int prism2_ioctl_priv_download(local_info_t *local, struct iw_point *p)
{
	struct prism2_download_param *param;
	int ret = 0;

	if (p->length < sizeof(struct prism2_download_param) ||
	    p->length > 1024 || !p->pointer)
		return -EINVAL;

	param = (struct prism2_download_param *)
		kmalloc(p->length, GFP_KERNEL);
	if (param == NULL)
		return -ENOMEM;

	if (copy_from_user(param, p->pointer, p->length)) {
		ret = -EFAULT;
		goto out;
	}

	if (p->length < sizeof(struct prism2_download_param) +
	    param->num_areas * sizeof(struct prism2_download_area)) {
		ret = -EINVAL;
		goto out;
	}

	ret = local->func->download(local, param);

 out:
	kfree(param);
	return ret;
}
#endif /* PRISM2_DOWNLOAD_SUPPORT */


static int prism2_set_genericelement(struct net_device *dev, u8 *elem,
				     size_t len)
{
	struct hostap_interface *iface = dev->priv;
	local_info_t *local = iface->local;
	u8 *buf;

	/*
	 * Add 16-bit length in the beginning of the buffer because Prism2 RID
	 * includes it.
	 */
	buf = kmalloc(len + 2, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	*((u16 *) buf) = cpu_to_le16(len);
	memcpy(buf + 2, elem, len);

	kfree(local->generic_elem);
	local->generic_elem = buf;
	local->generic_elem_len = len + 2;

	return local->func->set_rid(local->dev, HFA384X_RID_GENERICELEMENT,
				    buf, len + 2);
}


static int prism2_ioctl_siwauth(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *data, char *extra)
{
	struct hostap_interface *iface = dev->priv;
	local_info_t *local = iface->local;

	switch (data->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
		/*
		 * Host AP driver does not use these parameters and allows
		 * wpa_supplicant to control them internally.
		 */
		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		local->tkip_countermeasures = data->value;
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		local->drop_unencrypted = data->value;
		break;
	case IW_AUTH_80211_AUTH_ALG:
		local->auth_algs = data->value;
		break;
	case IW_AUTH_WPA_ENABLED:
		if (data->value == 0) {
			local->wpa = 0;
			if (local->sta_fw_ver < PRISM2_FW_VER(1,7,0))
				break;
			prism2_set_genericelement(dev, "", 0);
			local->host_roaming = 0;
			local->privacy_invoked = 0;
			if (hostap_set_word(dev, HFA384X_RID_SSNHANDLINGMODE,
					    0) ||
			    hostap_set_roaming(local) ||
			    hostap_set_encryption(local) ||
			    local->func->reset_port(dev))
				return -EINVAL;
			break;
		}
		if (local->sta_fw_ver < PRISM2_FW_VER(1,7,0))
			return -EOPNOTSUPP;
		local->host_roaming = 2;
		local->privacy_invoked = 1;
		local->wpa = 1;
		if (hostap_set_word(dev, HFA384X_RID_SSNHANDLINGMODE, 1) ||
		    hostap_set_roaming(local) ||
		    hostap_set_encryption(local) ||
		    local->func->reset_port(dev))
			return -EINVAL;
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		local->ieee_802_1x = data->value;
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		local->privacy_invoked = data->value;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}


static int prism2_ioctl_giwauth(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_param *data, char *extra)
{
	struct hostap_interface *iface = dev->priv;
	local_info_t *local = iface->local;

	switch (data->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
		/*
		 * Host AP driver does not use these parameters and allows
		 * wpa_supplicant to control them internally.
		 */
		return -EOPNOTSUPP;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		data->value = local->tkip_countermeasures;
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		data->value = local->drop_unencrypted;
		break;
	case IW_AUTH_80211_AUTH_ALG:
		data->value = local->auth_algs;
		break;
	case IW_AUTH_WPA_ENABLED:
		data->value = local->wpa;
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		data->value = local->ieee_802_1x;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}


static int prism2_ioctl_siwencodeext(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq, char *extra)
{
	struct hostap_interface *iface = dev->priv;
	local_info_t *local = iface->local;
	struct iw_encode_ext *ext = (struct iw_encode_ext *) extra;
	int i, ret = 0;
	struct ieee80211_crypto_ops *ops;
	struct ieee80211_crypt_data **crypt;
	void *sta_ptr;
	u8 *addr;
	const char *alg, *module;

	i = erq->flags & IW_ENCODE_INDEX;
	if (i > WEP_KEYS)
		return -EINVAL;
	if (i < 1 || i > WEP_KEYS)
		i = local->tx_keyidx;
	else
		i--;
	if (i < 0 || i >= WEP_KEYS)
		return -EINVAL;

	addr = ext->addr.sa_data;
	if (addr[0] == 0xff && addr[1] == 0xff && addr[2] == 0xff &&
	    addr[3] == 0xff && addr[4] == 0xff && addr[5] == 0xff) {
		sta_ptr = NULL;
		crypt = &local->crypt[i];
	} else {
		if (i != 0)
			return -EINVAL;
		sta_ptr = ap_crypt_get_ptrs(local->ap, addr, 0, &crypt);
		if (sta_ptr == NULL) {
			if (local->iw_mode == IW_MODE_INFRA) {
				/*
				 * TODO: add STA entry for the current AP so
				 * that unicast key can be used. For now, this
				 * is emulated by using default key idx 0.
				 */
				i = 0;
				crypt = &local->crypt[i];
			} else
				return -EINVAL;
		}
	}

	if ((erq->flags & IW_ENCODE_DISABLED) ||
	    ext->alg == IW_ENCODE_ALG_NONE) {
		if (*crypt)
			prism2_crypt_delayed_deinit(local, crypt);
		goto done;
	}

	switch (ext->alg) {
	case IW_ENCODE_ALG_WEP:
		alg = "WEP";
		module = "ieee80211_crypt_wep";
		break;
	case IW_ENCODE_ALG_TKIP:
		alg = "TKIP";
		module = "ieee80211_crypt_tkip";
		break;
	case IW_ENCODE_ALG_CCMP:
		alg = "CCMP";
		module = "ieee80211_crypt_ccmp";
		break;
	default:
		printk(KERN_DEBUG "%s: unsupported algorithm %d\n",
		       local->dev->name, ext->alg);
		ret = -EOPNOTSUPP;
		goto done;
	}

	ops = ieee80211_get_crypto_ops(alg);
	if (ops == NULL) {
		request_module(module);
		ops = ieee80211_get_crypto_ops(alg);
	}
	if (ops == NULL) {
		printk(KERN_DEBUG "%s: unknown crypto alg '%s'\n",
		       local->dev->name, alg);
		ret = -EOPNOTSUPP;
		goto done;
	}

	if (sta_ptr || ext->alg != IW_ENCODE_ALG_WEP) {
		/*
		 * Per station encryption and other than WEP algorithms
		 * require host-based encryption, so force them on
		 * automatically.
		 */
		local->host_decrypt = local->host_encrypt = 1;
	}

	if (*crypt == NULL || (*crypt)->ops != ops) {
		struct ieee80211_crypt_data *new_crypt;

		prism2_crypt_delayed_deinit(local, crypt);

		new_crypt = kzalloc(sizeof(struct ieee80211_crypt_data),
				GFP_KERNEL);
		if (new_crypt == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		new_crypt->ops = ops;
		new_crypt->priv = new_crypt->ops->init(i);
		if (new_crypt->priv == NULL) {
			kfree(new_crypt);
			ret = -EINVAL;
			goto done;
		}

		*crypt = new_crypt;
	}

	/*
	 * TODO: if ext_flags does not have IW_ENCODE_EXT_RX_SEQ_VALID, the
	 * existing seq# should not be changed.
	 * TODO: if ext_flags has IW_ENCODE_EXT_TX_SEQ_VALID, next TX seq#
	 * should be changed to something else than zero.
	 */
	if ((!(ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) || ext->key_len > 0)
	    && (*crypt)->ops->set_key &&
	    (*crypt)->ops->set_key(ext->key, ext->key_len, ext->rx_seq,
				   (*crypt)->priv) < 0) {
		printk(KERN_DEBUG "%s: key setting failed\n",
		       local->dev->name);
		ret = -EINVAL;
		goto done;
	}

	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
		if (!sta_ptr)
			local->tx_keyidx = i;
	}


	if (sta_ptr == NULL && ext->key_len > 0) {
		int first = 1, j;
		for (j = 0; j < WEP_KEYS; j++) {
			if (j != i && local->crypt[j]) {
				first = 0;
				break;
			}
		}
		if (first)
			local->tx_keyidx = i;
	}

 done:
	if (sta_ptr)
		hostap_handle_sta_release(sta_ptr);

	local->open_wep = erq->flags & IW_ENCODE_OPEN;

	/*
	 * Do not reset port0 if card is in Managed mode since resetting will
	 * generate new IEEE 802.11 authentication which may end up in looping
	 * with IEEE 802.1X. Prism2 documentation seem to require port reset
	 * after WEP configuration. However, keys are apparently changed at
	 * least in Managed mode.
	 */
	if (ret == 0 &&
	    (hostap_set_encryption(local) ||
	     (local->iw_mode != IW_MODE_INFRA &&
	      local->func->reset_port(local->dev))))
		ret = -EINVAL;

	return ret;
}


static int prism2_ioctl_giwencodeext(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq, char *extra)
{
	struct hostap_interface *iface = dev->priv;
	local_info_t *local = iface->local;
	struct ieee80211_crypt_data **crypt;
	void *sta_ptr;
	int max_key_len, i;
	struct iw_encode_ext *ext = (struct iw_encode_ext *) extra;
	u8 *addr;

	max_key_len = erq->length - sizeof(*ext);
	if (max_key_len < 0)
		return -EINVAL;

	i = erq->flags & IW_ENCODE_INDEX;
	if (i < 1 || i > WEP_KEYS)
		i = local->tx_keyidx;
	else
		i--;

	addr = ext->addr.sa_data;
	if (addr[0] == 0xff && addr[1] == 0xff && addr[2] == 0xff &&
	    addr[3] == 0xff && addr[4] == 0xff && addr[5] == 0xff) {
		sta_ptr = NULL;
		crypt = &local->crypt[i];
	} else {
		i = 0;
		sta_ptr = ap_crypt_get_ptrs(local->ap, addr, 0, &crypt);
		if (sta_ptr == NULL)
			return -EINVAL;
	}
	erq->flags = i + 1;
	memset(ext, 0, sizeof(*ext));

	if (*crypt == NULL || (*crypt)->ops == NULL) {
		ext->alg = IW_ENCODE_ALG_NONE;
		ext->key_len = 0;
		erq->flags |= IW_ENCODE_DISABLED;
	} else {
		if (strcmp((*crypt)->ops->name, "WEP") == 0)
			ext->alg = IW_ENCODE_ALG_WEP;
		else if (strcmp((*crypt)->ops->name, "TKIP") == 0)
			ext->alg = IW_ENCODE_ALG_TKIP;
		else if (strcmp((*crypt)->ops->name, "CCMP") == 0)
			ext->alg = IW_ENCODE_ALG_CCMP;
		else
			return -EINVAL;

		if ((*crypt)->ops->get_key) {
			ext->key_len =
				(*crypt)->ops->get_key(ext->key,
						       max_key_len,
						       ext->tx_seq,
						       (*crypt)->priv);
			if (ext->key_len &&
			    (ext->alg == IW_ENCODE_ALG_TKIP ||
			     ext->alg == IW_ENCODE_ALG_CCMP))
				ext->ext_flags |= IW_ENCODE_EXT_TX_SEQ_VALID;
		}
	}

	if (sta_ptr)
		hostap_handle_sta_release(sta_ptr);

	return 0;
}


static int prism2_ioctl_set_encryption(local_info_t *local,
				       struct prism2_hostapd_param *param,
				       int param_len)
{
	int ret = 0;
	struct ieee80211_crypto_ops *ops;
	struct ieee80211_crypt_data **crypt;
	void *sta_ptr;

	param->u.crypt.err = 0;
	param->u.crypt.alg[HOSTAP_CRYPT_ALG_NAME_LEN - 1] = '\0';

	if (param_len !=
	    (int) ((char *) param->u.crypt.key - (char *) param) +
	    param->u.crypt.key_len)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		if (param->u.crypt.idx >= WEP_KEYS)
			return -EINVAL;
		sta_ptr = NULL;
		crypt = &local->crypt[param->u.crypt.idx];
	} else {
		if (param->u.crypt.idx)
			return -EINVAL;
		sta_ptr = ap_crypt_get_ptrs(
			local->ap, param->sta_addr,
			(param->u.crypt.flags & HOSTAP_CRYPT_FLAG_PERMANENT),
			&crypt);

		if (sta_ptr == NULL) {
			param->u.crypt.err = HOSTAP_CRYPT_ERR_UNKNOWN_ADDR;
			return -EINVAL;
		}
	}

	if (strcmp(param->u.crypt.alg, "none") == 0) {
		if (crypt)
			prism2_crypt_delayed_deinit(local, crypt);
		goto done;
	}

	ops = ieee80211_get_crypto_ops(param->u.crypt.alg);
	if (ops == NULL && strcmp(param->u.crypt.alg, "WEP") == 0) {
		request_module("ieee80211_crypt_wep");
		ops = ieee80211_get_crypto_ops(param->u.crypt.alg);
	} else if (ops == NULL && strcmp(param->u.crypt.alg, "TKIP") == 0) {
		request_module("ieee80211_crypt_tkip");
		ops = ieee80211_get_crypto_ops(param->u.crypt.alg);
	} else if (ops == NULL && strcmp(param->u.crypt.alg, "CCMP") == 0) {
		request_module("ieee80211_crypt_ccmp");
		ops = ieee80211_get_crypto_ops(param->u.crypt.alg);
	}
	if (ops == NULL) {
		printk(KERN_DEBUG "%s: unknown crypto alg '%s'\n",
		       local->dev->name, param->u.crypt.alg);
		param->u.crypt.err = HOSTAP_CRYPT_ERR_UNKNOWN_ALG;
		ret = -EINVAL;
		goto done;
	}

	/* station based encryption and other than WEP algorithms require
	 * host-based encryption, so force them on automatically */
	local->host_decrypt = local->host_encrypt = 1;

	if (*crypt == NULL || (*crypt)->ops != ops) {
		struct ieee80211_crypt_data *new_crypt;

		prism2_crypt_delayed_deinit(local, crypt);

		new_crypt = kzalloc(sizeof(struct ieee80211_crypt_data),
				GFP_KERNEL);
		if (new_crypt == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		new_crypt->ops = ops;
		new_crypt->priv = new_crypt->ops->init(param->u.crypt.idx);
		if (new_crypt->priv == NULL) {
			kfree(new_crypt);
			param->u.crypt.err =
				HOSTAP_CRYPT_ERR_CRYPT_INIT_FAILED;
			ret = -EINVAL;
			goto done;
		}

		*crypt = new_crypt;
	}

	if ((!(param->u.crypt.flags & HOSTAP_CRYPT_FLAG_SET_TX_KEY) ||
	     param->u.crypt.key_len > 0) && (*crypt)->ops->set_key &&
	    (*crypt)->ops->set_key(param->u.crypt.key,
				   param->u.crypt.key_len, param->u.crypt.seq,
				   (*crypt)->priv) < 0) {
		printk(KERN_DEBUG "%s: key setting failed\n",
		       local->dev->name);
		param->u.crypt.err = HOSTAP_CRYPT_ERR_KEY_SET_FAILED;
		ret = -EINVAL;
		goto done;
	}

	if (param->u.crypt.flags & HOSTAP_CRYPT_FLAG_SET_TX_KEY) {
		if (!sta_ptr)
			local->tx_keyidx = param->u.crypt.idx;
		else if (param->u.crypt.idx) {
			printk(KERN_DEBUG "%s: TX key idx setting failed\n",
			       local->dev->name);
			param->u.crypt.err =
				HOSTAP_CRYPT_ERR_TX_KEY_SET_FAILED;
			ret = -EINVAL;
			goto done;
		}
	}

 done:
	if (sta_ptr)
		hostap_handle_sta_release(sta_ptr);

	/* Do not reset port0 if card is in Managed mode since resetting will
	 * generate new IEEE 802.11 authentication which may end up in looping
	 * with IEEE 802.1X. Prism2 documentation seem to require port reset
	 * after WEP configuration. However, keys are apparently changed at
	 * least in Managed mode. */
	if (ret == 0 &&
	    (hostap_set_encryption(local) ||
	     (local->iw_mode != IW_MODE_INFRA &&
	      local->func->reset_port(local->dev)))) {
		param->u.crypt.err = HOSTAP_CRYPT_ERR_CARD_CONF_FAILED;
		return -EINVAL;
	}

	return ret;
}


static int prism2_ioctl_get_encryption(local_info_t *local,
				       struct prism2_hostapd_param *param,
				       int param_len)
{
	struct ieee80211_crypt_data **crypt;
	void *sta_ptr;
	int max_key_len;

	param->u.crypt.err = 0;

	max_key_len = param_len -
		(int) ((char *) param->u.crypt.key - (char *) param);
	if (max_key_len < 0)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		sta_ptr = NULL;
		if (param->u.crypt.idx >= WEP_KEYS)
			param->u.crypt.idx = local->tx_keyidx;
		crypt = &local->crypt[param->u.crypt.idx];
	} else {
		param->u.crypt.idx = 0;
		sta_ptr = ap_crypt_get_ptrs(local->ap, param->sta_addr, 0,
					    &crypt);

		if (sta_ptr == NULL) {
			param->u.crypt.err = HOSTAP_CRYPT_ERR_UNKNOWN_ADDR;
			return -EINVAL;
		}
	}

	if (*crypt == NULL || (*crypt)->ops == NULL) {
		memcpy(param->u.crypt.alg, "none", 5);
		param->u.crypt.key_len = 0;
		param->u.crypt.idx = 0xff;
	} else {
		strncpy(param->u.crypt.alg, (*crypt)->ops->name,
			HOSTAP_CRYPT_ALG_NAME_LEN);
		param->u.crypt.key_len = 0;

		memset(param->u.crypt.seq, 0, 8);
		if ((*crypt)->ops->get_key) {
			param->u.crypt.key_len =
				(*crypt)->ops->get_key(param->u.crypt.key,
						       max_key_len,
						       param->u.crypt.seq,
						       (*crypt)->priv);
		}
	}

	if (sta_ptr)
		hostap_handle_sta_release(sta_ptr);

	return 0;
}


static int prism2_ioctl_get_rid(local_info_t *local,
				struct prism2_hostapd_param *param,
				int param_len)
{
	int max_len, res;

	max_len = param_len - PRISM2_HOSTAPD_RID_HDR_LEN;
	if (max_len < 0)
		return -EINVAL;

	res = local->func->get_rid(local->dev, param->u.rid.rid,
				   param->u.rid.data, param->u.rid.len, 0);
	if (res >= 0) {
		param->u.rid.len = res;
		return 0;
	}

	return res;
}


static int prism2_ioctl_set_rid(local_info_t *local,
				struct prism2_hostapd_param *param,
				int param_len)
{
	int max_len;

	max_len = param_len - PRISM2_HOSTAPD_RID_HDR_LEN;
	if (max_len < 0 || max_len < param->u.rid.len)
		return -EINVAL;

	return local->func->set_rid(local->dev, param->u.rid.rid,
				    param->u.rid.data, param->u.rid.len);
}


static int prism2_ioctl_set_assoc_ap_addr(local_info_t *local,
					  struct prism2_hostapd_param *param,
					  int param_len)
{
	printk(KERN_DEBUG "%ssta: associated as client with AP " MACSTR "\n",
	       local->dev->name, MAC2STR(param->sta_addr));
	memcpy(local->assoc_ap_addr, param->sta_addr, ETH_ALEN);
	return 0;
}


static int prism2_ioctl_siwgenie(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *extra)
{
	return prism2_set_genericelement(dev, extra, data->length);
}


static int prism2_ioctl_giwgenie(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *extra)
{
	struct hostap_interface *iface = dev->priv;
	local_info_t *local = iface->local;
	int len = local->generic_elem_len - 2;

	if (len <= 0 || local->generic_elem == NULL) {
		data->length = 0;
		return 0;
	}

	if (data->length < len)
		return -E2BIG;

	data->length = len;
	memcpy(extra, local->generic_elem + 2, len);

	return 0;
}


static int prism2_ioctl_set_generic_element(local_info_t *local,
					    struct prism2_hostapd_param *param,
					    int param_len)
{
	int max_len, len;

	len = param->u.generic_elem.len;
	max_len = param_len - PRISM2_HOSTAPD_GENERIC_ELEMENT_HDR_LEN;
	if (max_len < 0 || max_len < len)
		return -EINVAL;

	return prism2_set_genericelement(local->dev,
					 param->u.generic_elem.data, len);
}


static int prism2_ioctl_siwmlme(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct hostap_interface *iface = dev->priv;
	local_info_t *local = iface->local;
	struct iw_mlme *mlme = (struct iw_mlme *) extra;
	u16 reason;

	reason = cpu_to_le16(mlme->reason_code);

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		return prism2_sta_send_mgmt(local, mlme->addr.sa_data,
					    IEEE80211_STYPE_DEAUTH,
					    (u8 *) &reason, 2);
	case IW_MLME_DISASSOC:
		return prism2_sta_send_mgmt(local, mlme->addr.sa_data,
					    IEEE80211_STYPE_DISASSOC,
					    (u8 *) &reason, 2);
	default:
		return -EOPNOTSUPP;
	}
}


static int prism2_ioctl_mlme(local_info_t *local,
			     struct prism2_hostapd_param *param)
{
	u16 reason;

	reason = cpu_to_le16(param->u.mlme.reason_code);
	switch (param->u.mlme.cmd) {
	case MLME_STA_DEAUTH:
		return prism2_sta_send_mgmt(local, param->sta_addr,
					    IEEE80211_STYPE_DEAUTH,
					    (u8 *) &reason, 2);
	case MLME_STA_DISASSOC:
		return prism2_sta_send_mgmt(local, param->sta_addr,
					    IEEE80211_STYPE_DISASSOC,
					    (u8 *) &reason, 2);
	default:
		return -EOPNOTSUPP;
	}
}


static int prism2_ioctl_scan_req(local_info_t *local,
				 struct prism2_hostapd_param *param)
{
#ifndef PRISM2_NO_STATION_MODES
	if ((local->iw_mode != IW_MODE_INFRA &&
	     local->iw_mode != IW_MODE_ADHOC) ||
	    (local->sta_fw_ver < PRISM2_FW_VER(1,3,1)))
		return -EOPNOTSUPP;

	if (!local->dev_enabled)
		return -ENETDOWN;

	return prism2_request_hostscan(local->dev, param->u.scan_req.ssid,
				       param->u.scan_req.ssid_len);
#else /* PRISM2_NO_STATION_MODES */
	return -EOPNOTSUPP;
#endif /* PRISM2_NO_STATION_MODES */
}


static int prism2_ioctl_priv_hostapd(local_info_t *local, struct iw_point *p)
{
	struct prism2_hostapd_param *param;
	int ret = 0;
	int ap_ioctl = 0;

	if (p->length < sizeof(struct prism2_hostapd_param) ||
	    p->length > PRISM2_HOSTAPD_MAX_BUF_SIZE || !p->pointer)
		return -EINVAL;

	param = kmalloc(p->length, GFP_KERNEL);
	if (param == NULL)
		return -ENOMEM;

	if (copy_from_user(param, p->pointer, p->length)) {
		ret = -EFAULT;
		goto out;
	}

	switch (param->cmd) {
	case PRISM2_SET_ENCRYPTION:
		ret = prism2_ioctl_set_encryption(local, param, p->length);
		break;
	case PRISM2_GET_ENCRYPTION:
		ret = prism2_ioctl_get_encryption(local, param, p->length);
		break;
	case PRISM2_HOSTAPD_GET_RID:
		ret = prism2_ioctl_get_rid(local, param, p->length);
		break;
	case PRISM2_HOSTAPD_SET_RID:
		ret = prism2_ioctl_set_rid(local, param, p->length);
		break;
	case PRISM2_HOSTAPD_SET_ASSOC_AP_ADDR:
		ret = prism2_ioctl_set_assoc_ap_addr(local, param, p->length);
		break;
	case PRISM2_HOSTAPD_SET_GENERIC_ELEMENT:
		ret = prism2_ioctl_set_generic_element(local, param,
						       p->length);
		break;
	case PRISM2_HOSTAPD_MLME:
		ret = prism2_ioctl_mlme(local, param);
		break;
	case PRISM2_HOSTAPD_SCAN_REQ:
		ret = prism2_ioctl_scan_req(local, param);
		break;
	default:
		ret = prism2_hostapd(local->ap, param);
		ap_ioctl = 1;
		break;
	}

	if (ret == 1 || !ap_ioctl) {
		if (copy_to_user(p->pointer, param, p->length)) {
			ret = -EFAULT;
			goto out;
		} else if (ap_ioctl)
			ret = 0;
	}

 out:
	kfree(param);
	return ret;
}


static void prism2_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	strncpy(info->driver, "hostap", sizeof(info->driver) - 1);
	strncpy(info->version, PRISM2_VERSION,
		sizeof(info->version) - 1);
	snprintf(info->fw_version, sizeof(info->fw_version) - 1,
		 "%d.%d.%d", (local->sta_fw_ver >> 16) & 0xff,
		 (local->sta_fw_ver >> 8) & 0xff,
		 local->sta_fw_ver & 0xff);
}

const struct ethtool_ops prism2_ethtool_ops = {
	.get_drvinfo = prism2_get_drvinfo
};


/* Structures to export the Wireless Handlers */

static const iw_handler prism2_handler[] =
{
	(iw_handler) NULL,				/* SIOCSIWCOMMIT */
	(iw_handler) prism2_get_name,			/* SIOCGIWNAME */
	(iw_handler) NULL,				/* SIOCSIWNWID */
	(iw_handler) NULL,				/* SIOCGIWNWID */
	(iw_handler) prism2_ioctl_siwfreq,		/* SIOCSIWFREQ */
	(iw_handler) prism2_ioctl_giwfreq,		/* SIOCGIWFREQ */
	(iw_handler) prism2_ioctl_siwmode,		/* SIOCSIWMODE */
	(iw_handler) prism2_ioctl_giwmode,		/* SIOCGIWMODE */
	(iw_handler) prism2_ioctl_siwsens,		/* SIOCSIWSENS */
	(iw_handler) prism2_ioctl_giwsens,		/* SIOCGIWSENS */
	(iw_handler) NULL /* not used */,		/* SIOCSIWRANGE */
	(iw_handler) prism2_ioctl_giwrange,		/* SIOCGIWRANGE */
	(iw_handler) NULL /* not used */,		/* SIOCSIWPRIV */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWPRIV */
	(iw_handler) NULL /* not used */,		/* SIOCSIWSTATS */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWSTATS */
	iw_handler_set_spy,				/* SIOCSIWSPY */
	iw_handler_get_spy,				/* SIOCGIWSPY */
	iw_handler_set_thrspy,				/* SIOCSIWTHRSPY */
	iw_handler_get_thrspy,				/* SIOCGIWTHRSPY */
	(iw_handler) prism2_ioctl_siwap,		/* SIOCSIWAP */
	(iw_handler) prism2_ioctl_giwap,		/* SIOCGIWAP */
	(iw_handler) prism2_ioctl_siwmlme,		/* SIOCSIWMLME */
	(iw_handler) prism2_ioctl_giwaplist,		/* SIOCGIWAPLIST */
	(iw_handler) prism2_ioctl_siwscan,		/* SIOCSIWSCAN */
	(iw_handler) prism2_ioctl_giwscan,		/* SIOCGIWSCAN */
	(iw_handler) prism2_ioctl_siwessid,		/* SIOCSIWESSID */
	(iw_handler) prism2_ioctl_giwessid,		/* SIOCGIWESSID */
	(iw_handler) prism2_ioctl_siwnickn,		/* SIOCSIWNICKN */
	(iw_handler) prism2_ioctl_giwnickn,		/* SIOCGIWNICKN */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) prism2_ioctl_siwrate,		/* SIOCSIWRATE */
	(iw_handler) prism2_ioctl_giwrate,		/* SIOCGIWRATE */
	(iw_handler) prism2_ioctl_siwrts,		/* SIOCSIWRTS */
	(iw_handler) prism2_ioctl_giwrts,		/* SIOCGIWRTS */
	(iw_handler) prism2_ioctl_siwfrag,		/* SIOCSIWFRAG */
	(iw_handler) prism2_ioctl_giwfrag,		/* SIOCGIWFRAG */
	(iw_handler) prism2_ioctl_siwtxpow,		/* SIOCSIWTXPOW */
	(iw_handler) prism2_ioctl_giwtxpow,		/* SIOCGIWTXPOW */
	(iw_handler) prism2_ioctl_siwretry,		/* SIOCSIWRETRY */
	(iw_handler) prism2_ioctl_giwretry,		/* SIOCGIWRETRY */
	(iw_handler) prism2_ioctl_siwencode,		/* SIOCSIWENCODE */
	(iw_handler) prism2_ioctl_giwencode,		/* SIOCGIWENCODE */
	(iw_handler) prism2_ioctl_siwpower,		/* SIOCSIWPOWER */
	(iw_handler) prism2_ioctl_giwpower,		/* SIOCGIWPOWER */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) prism2_ioctl_siwgenie,		/* SIOCSIWGENIE */
	(iw_handler) prism2_ioctl_giwgenie,		/* SIOCGIWGENIE */
	(iw_handler) prism2_ioctl_siwauth,		/* SIOCSIWAUTH */
	(iw_handler) prism2_ioctl_giwauth,		/* SIOCGIWAUTH */
	(iw_handler) prism2_ioctl_siwencodeext,		/* SIOCSIWENCODEEXT */
	(iw_handler) prism2_ioctl_giwencodeext,		/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCSIWPMKSA */
	(iw_handler) NULL,				/* -- hole -- */
};

static const iw_handler prism2_private_handler[] =
{							/* SIOCIWFIRSTPRIV + */
	(iw_handler) prism2_ioctl_priv_prism2_param,	/* 0 */
	(iw_handler) prism2_ioctl_priv_get_prism2_param, /* 1 */
	(iw_handler) prism2_ioctl_priv_writemif,	/* 2 */
	(iw_handler) prism2_ioctl_priv_readmif,		/* 3 */
};

const struct iw_handler_def hostap_iw_handler_def =
{
	.num_standard	= sizeof(prism2_handler) / sizeof(iw_handler),
	.num_private	= sizeof(prism2_private_handler) / sizeof(iw_handler),
	.num_private_args = sizeof(prism2_priv) / sizeof(struct iw_priv_args),
	.standard	= (iw_handler *) prism2_handler,
	.private	= (iw_handler *) prism2_private_handler,
	.private_args	= (struct iw_priv_args *) prism2_priv,
	.get_wireless_stats = hostap_get_wireless_stats,
};


int hostap_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct iwreq *wrq = (struct iwreq *) ifr;
	struct hostap_interface *iface;
	local_info_t *local;
	int ret = 0;

	iface = netdev_priv(dev);
	local = iface->local;

	switch (cmd) {
		/* Private ioctls (iwpriv) that have not yet been converted
		 * into new wireless extensions API */

	case PRISM2_IOCTL_INQUIRE:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = prism2_ioctl_priv_inquire(dev, (int *) wrq->u.name);
		break;

	case PRISM2_IOCTL_MONITOR:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = prism2_ioctl_priv_monitor(dev, (int *) wrq->u.name);
		break;

	case PRISM2_IOCTL_RESET:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = prism2_ioctl_priv_reset(dev, (int *) wrq->u.name);
		break;

	case PRISM2_IOCTL_WDS_ADD:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = prism2_wds_add(local, wrq->u.ap_addr.sa_data, 1);
		break;

	case PRISM2_IOCTL_WDS_DEL:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = prism2_wds_del(local, wrq->u.ap_addr.sa_data, 1, 0);
		break;

	case PRISM2_IOCTL_SET_RID_WORD:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = prism2_ioctl_priv_set_rid_word(dev,
							  (int *) wrq->u.name);
		break;

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	case PRISM2_IOCTL_MACCMD:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = ap_mac_cmd_ioctl(local, (int *) wrq->u.name);
		break;

	case PRISM2_IOCTL_ADDMAC:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = ap_control_add_mac(&local->ap->mac_restrictions,
					      wrq->u.ap_addr.sa_data);
		break;
	case PRISM2_IOCTL_DELMAC:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = ap_control_del_mac(&local->ap->mac_restrictions,
					      wrq->u.ap_addr.sa_data);
		break;
	case PRISM2_IOCTL_KICKMAC:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = ap_control_kick_mac(local->ap, local->dev,
					       wrq->u.ap_addr.sa_data);
		break;
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


		/* Private ioctls that are not used with iwpriv;
		 * in SIOCDEVPRIVATE range */

#ifdef PRISM2_DOWNLOAD_SUPPORT
	case PRISM2_IOCTL_DOWNLOAD:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = prism2_ioctl_priv_download(local, &wrq->u.data);
		break;
#endif /* PRISM2_DOWNLOAD_SUPPORT */

	case PRISM2_IOCTL_HOSTAPD:
		if (!capable(CAP_NET_ADMIN)) ret = -EPERM;
		else ret = prism2_ioctl_priv_hostapd(local, &wrq->u.data);
		break;

	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}
