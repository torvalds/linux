/*
 * Host AP (software wireless LAN access point) driver for
 * Intersil Prism2/2.5/3 - hostap.o module, common routines
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <j@w1.fi>
 * Copyright (c) 2002-2005, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <linux/kmod.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>
#include <net/ieee80211.h>
#include <net/ieee80211_crypt.h>
#include <asm/uaccess.h>

#include "hostap_wlan.h"
#include "hostap_80211.h"
#include "hostap_ap.h"
#include "hostap.h"

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Host AP common routines");
MODULE_LICENSE("GPL");

#define TX_TIMEOUT (2 * HZ)

#define PRISM2_MAX_FRAME_SIZE 2304
#define PRISM2_MIN_MTU 256
/* FIX: */
#define PRISM2_MAX_MTU (PRISM2_MAX_FRAME_SIZE - (6 /* LLC */ + 8 /* WEP */))


struct net_device * hostap_add_interface(struct local_info *local,
					 int type, int rtnl_locked,
					 const char *prefix,
					 const char *name)
{
	struct net_device *dev, *mdev;
	struct hostap_interface *iface;
	int ret;

	dev = alloc_etherdev(sizeof(struct hostap_interface));
	if (dev == NULL)
		return NULL;

	iface = netdev_priv(dev);
	iface->dev = dev;
	iface->local = local;
	iface->type = type;
	list_add(&iface->list, &local->hostap_interfaces);

	mdev = local->dev;
	memcpy(dev->dev_addr, mdev->dev_addr, ETH_ALEN);
	dev->base_addr = mdev->base_addr;
	dev->irq = mdev->irq;
	dev->mem_start = mdev->mem_start;
	dev->mem_end = mdev->mem_end;

	hostap_setup_dev(dev, local, 0);
	dev->destructor = free_netdev;

	sprintf(dev->name, "%s%s", prefix, name);
	if (!rtnl_locked)
		rtnl_lock();

	ret = 0;
	if (strchr(dev->name, '%'))
		ret = dev_alloc_name(dev, dev->name);

	SET_NETDEV_DEV(dev, mdev->dev.parent);
	if (ret >= 0)
		ret = register_netdevice(dev);

	if (!rtnl_locked)
		rtnl_unlock();

	if (ret < 0) {
		printk(KERN_WARNING "%s: failed to add new netdevice!\n",
		       dev->name);
		free_netdev(dev);
		return NULL;
	}

	printk(KERN_DEBUG "%s: registered netdevice %s\n",
	       mdev->name, dev->name);

	return dev;
}


void hostap_remove_interface(struct net_device *dev, int rtnl_locked,
			     int remove_from_list)
{
	struct hostap_interface *iface;

	if (!dev)
		return;

	iface = netdev_priv(dev);

	if (remove_from_list) {
		list_del(&iface->list);
	}

	if (dev == iface->local->ddev)
		iface->local->ddev = NULL;
	else if (dev == iface->local->apdev)
		iface->local->apdev = NULL;
	else if (dev == iface->local->stadev)
		iface->local->stadev = NULL;

	if (rtnl_locked)
		unregister_netdevice(dev);
	else
		unregister_netdev(dev);

	/* dev->destructor = free_netdev() will free the device data, including
	 * private data, when removing the device */
}


static inline int prism2_wds_special_addr(u8 *addr)
{
	if (addr[0] || addr[1] || addr[2] || addr[3] || addr[4] || addr[5])
		return 0;

	return 1;
}


int prism2_wds_add(local_info_t *local, u8 *remote_addr,
		   int rtnl_locked)
{
	struct net_device *dev;
	struct list_head *ptr;
	struct hostap_interface *iface, *empty, *match;

	empty = match = NULL;
	read_lock_bh(&local->iface_lock);
	list_for_each(ptr, &local->hostap_interfaces) {
		iface = list_entry(ptr, struct hostap_interface, list);
		if (iface->type != HOSTAP_INTERFACE_WDS)
			continue;

		if (prism2_wds_special_addr(iface->u.wds.remote_addr))
			empty = iface;
		else if (memcmp(iface->u.wds.remote_addr, remote_addr,
				ETH_ALEN) == 0) {
			match = iface;
			break;
		}
	}
	if (!match && empty && !prism2_wds_special_addr(remote_addr)) {
		/* take pre-allocated entry into use */
		memcpy(empty->u.wds.remote_addr, remote_addr, ETH_ALEN);
		read_unlock_bh(&local->iface_lock);
		printk(KERN_DEBUG "%s: using pre-allocated WDS netdevice %s\n",
		       local->dev->name, empty->dev->name);
		return 0;
	}
	read_unlock_bh(&local->iface_lock);

	if (!prism2_wds_special_addr(remote_addr)) {
		if (match)
			return -EEXIST;
		hostap_add_sta(local->ap, remote_addr);
	}

	if (local->wds_connections >= local->wds_max_connections)
		return -ENOBUFS;

	/* verify that there is room for wds# postfix in the interface name */
	if (strlen(local->dev->name) > IFNAMSIZ - 5) {
		printk(KERN_DEBUG "'%s' too long base device name\n",
		       local->dev->name);
		return -EINVAL;
	}

	dev = hostap_add_interface(local, HOSTAP_INTERFACE_WDS, rtnl_locked,
				   local->ddev->name, "wds%d");
	if (dev == NULL)
		return -ENOMEM;

	iface = netdev_priv(dev);
	memcpy(iface->u.wds.remote_addr, remote_addr, ETH_ALEN);

	local->wds_connections++;

	return 0;
}


int prism2_wds_del(local_info_t *local, u8 *remote_addr,
		   int rtnl_locked, int do_not_remove)
{
	unsigned long flags;
	struct list_head *ptr;
	struct hostap_interface *iface, *selected = NULL;

	write_lock_irqsave(&local->iface_lock, flags);
	list_for_each(ptr, &local->hostap_interfaces) {
		iface = list_entry(ptr, struct hostap_interface, list);
		if (iface->type != HOSTAP_INTERFACE_WDS)
			continue;

		if (memcmp(iface->u.wds.remote_addr, remote_addr,
			   ETH_ALEN) == 0) {
			selected = iface;
			break;
		}
	}
	if (selected && !do_not_remove)
		list_del(&selected->list);
	write_unlock_irqrestore(&local->iface_lock, flags);

	if (selected) {
		if (do_not_remove)
			memset(selected->u.wds.remote_addr, 0, ETH_ALEN);
		else {
			hostap_remove_interface(selected->dev, rtnl_locked, 0);
			local->wds_connections--;
		}
	}

	return selected ? 0 : -ENODEV;
}


u16 hostap_tx_callback_register(local_info_t *local,
				void (*func)(struct sk_buff *, int ok, void *),
				void *data)
{
	unsigned long flags;
	struct hostap_tx_callback_info *entry;

	entry = kmalloc(sizeof(*entry),
							   GFP_ATOMIC);
	if (entry == NULL)
		return 0;

	entry->func = func;
	entry->data = data;

	spin_lock_irqsave(&local->lock, flags);
	entry->idx = local->tx_callback ? local->tx_callback->idx + 1 : 1;
	entry->next = local->tx_callback;
	local->tx_callback = entry;
	spin_unlock_irqrestore(&local->lock, flags);

	return entry->idx;
}


int hostap_tx_callback_unregister(local_info_t *local, u16 idx)
{
	unsigned long flags;
	struct hostap_tx_callback_info *cb, *prev = NULL;

	spin_lock_irqsave(&local->lock, flags);
	cb = local->tx_callback;
	while (cb != NULL && cb->idx != idx) {
		prev = cb;
		cb = cb->next;
	}
	if (cb) {
		if (prev == NULL)
			local->tx_callback = cb->next;
		else
			prev->next = cb->next;
		kfree(cb);
	}
	spin_unlock_irqrestore(&local->lock, flags);

	return cb ? 0 : -1;
}


/* val is in host byte order */
int hostap_set_word(struct net_device *dev, int rid, u16 val)
{
	struct hostap_interface *iface;
	u16 tmp = cpu_to_le16(val);
	iface = netdev_priv(dev);
	return iface->local->func->set_rid(dev, rid, &tmp, 2);
}


int hostap_set_string(struct net_device *dev, int rid, const char *val)
{
	struct hostap_interface *iface;
	char buf[MAX_SSID_LEN + 2];
	int len;

	iface = netdev_priv(dev);
	len = strlen(val);
	if (len > MAX_SSID_LEN)
		return -1;
	memset(buf, 0, sizeof(buf));
	buf[0] = len; /* little endian 16 bit word */
	memcpy(buf + 2, val, len);

	return iface->local->func->set_rid(dev, rid, &buf, MAX_SSID_LEN + 2);
}


u16 hostap_get_porttype(local_info_t *local)
{
	if (local->iw_mode == IW_MODE_ADHOC && local->pseudo_adhoc)
		return HFA384X_PORTTYPE_PSEUDO_IBSS;
	if (local->iw_mode == IW_MODE_ADHOC)
		return HFA384X_PORTTYPE_IBSS;
	if (local->iw_mode == IW_MODE_INFRA)
		return HFA384X_PORTTYPE_BSS;
	if (local->iw_mode == IW_MODE_REPEAT)
		return HFA384X_PORTTYPE_WDS;
	if (local->iw_mode == IW_MODE_MONITOR)
		return HFA384X_PORTTYPE_PSEUDO_IBSS;
	return HFA384X_PORTTYPE_HOSTAP;
}


int hostap_set_encryption(local_info_t *local)
{
	u16 val, old_val;
	int i, keylen, len, idx;
	char keybuf[WEP_KEY_LEN + 1];
	enum { NONE, WEP, OTHER } encrypt_type;

	idx = local->tx_keyidx;
	if (local->crypt[idx] == NULL || local->crypt[idx]->ops == NULL)
		encrypt_type = NONE;
	else if (strcmp(local->crypt[idx]->ops->name, "WEP") == 0)
		encrypt_type = WEP;
	else
		encrypt_type = OTHER;

	if (local->func->get_rid(local->dev, HFA384X_RID_CNFWEPFLAGS, &val, 2,
				 1) < 0) {
		printk(KERN_DEBUG "Could not read current WEP flags.\n");
		goto fail;
	}
	le16_to_cpus(&val);
	old_val = val;

	if (encrypt_type != NONE || local->privacy_invoked)
		val |= HFA384X_WEPFLAGS_PRIVACYINVOKED;
	else
		val &= ~HFA384X_WEPFLAGS_PRIVACYINVOKED;

	if (local->open_wep || encrypt_type == NONE ||
	    ((local->ieee_802_1x || local->wpa) && local->host_decrypt))
		val &= ~HFA384X_WEPFLAGS_EXCLUDEUNENCRYPTED;
	else
		val |= HFA384X_WEPFLAGS_EXCLUDEUNENCRYPTED;

	if ((encrypt_type != NONE || local->privacy_invoked) &&
	    (encrypt_type == OTHER || local->host_encrypt))
		val |= HFA384X_WEPFLAGS_HOSTENCRYPT;
	else
		val &= ~HFA384X_WEPFLAGS_HOSTENCRYPT;
	if ((encrypt_type != NONE || local->privacy_invoked) &&
	    (encrypt_type == OTHER || local->host_decrypt))
		val |= HFA384X_WEPFLAGS_HOSTDECRYPT;
	else
		val &= ~HFA384X_WEPFLAGS_HOSTDECRYPT;

	if (val != old_val &&
	    hostap_set_word(local->dev, HFA384X_RID_CNFWEPFLAGS, val)) {
		printk(KERN_DEBUG "Could not write new WEP flags (0x%x)\n",
		       val);
		goto fail;
	}

	if (encrypt_type != WEP)
		return 0;

	/* 104-bit support seems to require that all the keys are set to the
	 * same keylen */
	keylen = 6; /* first 5 octets */
	len = local->crypt[idx]->ops->get_key(keybuf, sizeof(keybuf),
					      NULL, local->crypt[idx]->priv);
	if (idx >= 0 && idx < WEP_KEYS && len > 5)
		keylen = WEP_KEY_LEN + 1; /* first 13 octets */

	for (i = 0; i < WEP_KEYS; i++) {
		memset(keybuf, 0, sizeof(keybuf));
		if (local->crypt[i]) {
			(void) local->crypt[i]->ops->get_key(
				keybuf, sizeof(keybuf),
				NULL, local->crypt[i]->priv);
		}
		if (local->func->set_rid(local->dev,
					 HFA384X_RID_CNFDEFAULTKEY0 + i,
					 keybuf, keylen)) {
			printk(KERN_DEBUG "Could not set key %d (len=%d)\n",
			       i, keylen);
			goto fail;
		}
	}
	if (hostap_set_word(local->dev, HFA384X_RID_CNFWEPDEFAULTKEYID, idx)) {
		printk(KERN_DEBUG "Could not set default keyid %d\n", idx);
		goto fail;
	}

	return 0;

 fail:
	printk(KERN_DEBUG "%s: encryption setup failed\n", local->dev->name);
	return -1;
}


int hostap_set_antsel(local_info_t *local)
{
	u16 val;
	int ret = 0;

	if (local->antsel_tx != HOSTAP_ANTSEL_DO_NOT_TOUCH &&
	    local->func->cmd(local->dev, HFA384X_CMDCODE_READMIF,
			     HFA386X_CR_TX_CONFIGURE,
			     NULL, &val) == 0) {
		val &= ~(BIT(2) | BIT(1));
		switch (local->antsel_tx) {
		case HOSTAP_ANTSEL_DIVERSITY:
			val |= BIT(1);
			break;
		case HOSTAP_ANTSEL_LOW:
			break;
		case HOSTAP_ANTSEL_HIGH:
			val |= BIT(2);
			break;
		}

		if (local->func->cmd(local->dev, HFA384X_CMDCODE_WRITEMIF,
				     HFA386X_CR_TX_CONFIGURE, &val, NULL)) {
			printk(KERN_INFO "%s: setting TX AntSel failed\n",
			       local->dev->name);
			ret = -1;
		}
	}

	if (local->antsel_rx != HOSTAP_ANTSEL_DO_NOT_TOUCH &&
	    local->func->cmd(local->dev, HFA384X_CMDCODE_READMIF,
			     HFA386X_CR_RX_CONFIGURE,
			     NULL, &val) == 0) {
		val &= ~(BIT(1) | BIT(0));
		switch (local->antsel_rx) {
		case HOSTAP_ANTSEL_DIVERSITY:
			break;
		case HOSTAP_ANTSEL_LOW:
			val |= BIT(0);
			break;
		case HOSTAP_ANTSEL_HIGH:
			val |= BIT(0) | BIT(1);
			break;
		}

		if (local->func->cmd(local->dev, HFA384X_CMDCODE_WRITEMIF,
				     HFA386X_CR_RX_CONFIGURE, &val, NULL)) {
			printk(KERN_INFO "%s: setting RX AntSel failed\n",
			       local->dev->name);
			ret = -1;
		}
	}

	return ret;
}


int hostap_set_roaming(local_info_t *local)
{
	u16 val;

	switch (local->host_roaming) {
	case 1:
		val = HFA384X_ROAMING_HOST;
		break;
	case 2:
		val = HFA384X_ROAMING_DISABLED;
		break;
	case 0:
	default:
		val = HFA384X_ROAMING_FIRMWARE;
		break;
	}

	return hostap_set_word(local->dev, HFA384X_RID_CNFROAMINGMODE, val);
}


int hostap_set_auth_algs(local_info_t *local)
{
	int val = local->auth_algs;
	/* At least STA f/w v0.6.2 seems to have issues with cnfAuthentication
	 * set to include both Open and Shared Key flags. It tries to use
	 * Shared Key authentication in that case even if WEP keys are not
	 * configured.. STA f/w v0.7.6 is able to handle such configuration,
	 * but it is unknown when this was fixed between 0.6.2 .. 0.7.6. */
	if (local->sta_fw_ver < PRISM2_FW_VER(0,7,0) &&
	    val != PRISM2_AUTH_OPEN && val != PRISM2_AUTH_SHARED_KEY)
		val = PRISM2_AUTH_OPEN;

	if (hostap_set_word(local->dev, HFA384X_RID_CNFAUTHENTICATION, val)) {
		printk(KERN_INFO "%s: cnfAuthentication setting to 0x%x "
		       "failed\n", local->dev->name, local->auth_algs);
		return -EINVAL;
	}

	return 0;
}


void hostap_dump_rx_header(const char *name, const struct hfa384x_rx_frame *rx)
{
	u16 status, fc;

	status = __le16_to_cpu(rx->status);

	printk(KERN_DEBUG "%s: RX status=0x%04x (port=%d, type=%d, "
	       "fcserr=%d) silence=%d signal=%d rate=%d rxflow=%d; "
	       "jiffies=%ld\n",
	       name, status, (status >> 8) & 0x07, status >> 13, status & 1,
	       rx->silence, rx->signal, rx->rate, rx->rxflow, jiffies);

	fc = __le16_to_cpu(rx->frame_control);
	printk(KERN_DEBUG "   FC=0x%04x (type=%d:%d) dur=0x%04x seq=0x%04x "
	       "data_len=%d%s%s\n",
	       fc, WLAN_FC_GET_TYPE(fc) >> 2, WLAN_FC_GET_STYPE(fc) >> 4,
	       __le16_to_cpu(rx->duration_id), __le16_to_cpu(rx->seq_ctrl),
	       __le16_to_cpu(rx->data_len),
	       fc & IEEE80211_FCTL_TODS ? " [ToDS]" : "",
	       fc & IEEE80211_FCTL_FROMDS ? " [FromDS]" : "");

	printk(KERN_DEBUG "   A1=" MACSTR " A2=" MACSTR " A3=" MACSTR " A4="
	       MACSTR "\n",
	       MAC2STR(rx->addr1), MAC2STR(rx->addr2), MAC2STR(rx->addr3),
	       MAC2STR(rx->addr4));

	printk(KERN_DEBUG "   dst=" MACSTR " src=" MACSTR " len=%d\n",
	       MAC2STR(rx->dst_addr), MAC2STR(rx->src_addr),
	       __be16_to_cpu(rx->len));
}


void hostap_dump_tx_header(const char *name, const struct hfa384x_tx_frame *tx)
{
	u16 fc;

	printk(KERN_DEBUG "%s: TX status=0x%04x retry_count=%d tx_rate=%d "
	       "tx_control=0x%04x; jiffies=%ld\n",
	       name, __le16_to_cpu(tx->status), tx->retry_count, tx->tx_rate,
	       __le16_to_cpu(tx->tx_control), jiffies);

	fc = __le16_to_cpu(tx->frame_control);
	printk(KERN_DEBUG "   FC=0x%04x (type=%d:%d) dur=0x%04x seq=0x%04x "
	       "data_len=%d%s%s\n",
	       fc, WLAN_FC_GET_TYPE(fc) >> 2, WLAN_FC_GET_STYPE(fc) >> 4,
	       __le16_to_cpu(tx->duration_id), __le16_to_cpu(tx->seq_ctrl),
	       __le16_to_cpu(tx->data_len),
	       fc & IEEE80211_FCTL_TODS ? " [ToDS]" : "",
	       fc & IEEE80211_FCTL_FROMDS ? " [FromDS]" : "");

	printk(KERN_DEBUG "   A1=" MACSTR " A2=" MACSTR " A3=" MACSTR " A4="
	       MACSTR "\n",
	       MAC2STR(tx->addr1), MAC2STR(tx->addr2), MAC2STR(tx->addr3),
	       MAC2STR(tx->addr4));

	printk(KERN_DEBUG "   dst=" MACSTR " src=" MACSTR " len=%d\n",
	       MAC2STR(tx->dst_addr), MAC2STR(tx->src_addr),
	       __be16_to_cpu(tx->len));
}


int hostap_80211_header_parse(struct sk_buff *skb, unsigned char *haddr)
{
	memcpy(haddr, skb_mac_header(skb) + 10, ETH_ALEN); /* addr2 */
	return ETH_ALEN;
}


int hostap_80211_prism_header_parse(struct sk_buff *skb, unsigned char *haddr)
{
	const unsigned char *mac = skb_mac_header(skb);

	if (*(u32 *)mac == LWNG_CAP_DID_BASE) {
		memcpy(haddr, mac + sizeof(struct linux_wlan_ng_prism_hdr) + 10,
		       ETH_ALEN); /* addr2 */
	} else { /* (*(u32 *)mac == htonl(LWNG_CAPHDR_VERSION)) */
		memcpy(haddr, mac + sizeof(struct linux_wlan_ng_cap_hdr) + 10,
		       ETH_ALEN); /* addr2 */
	}
	return ETH_ALEN;
}


int hostap_80211_get_hdrlen(u16 fc)
{
	int hdrlen = 24;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case IEEE80211_FTYPE_DATA:
		if ((fc & IEEE80211_FCTL_FROMDS) && (fc & IEEE80211_FCTL_TODS))
			hdrlen = 30; /* Addr4 */
		break;
	case IEEE80211_FTYPE_CTL:
		switch (WLAN_FC_GET_STYPE(fc)) {
		case IEEE80211_STYPE_CTS:
		case IEEE80211_STYPE_ACK:
			hdrlen = 10;
			break;
		default:
			hdrlen = 16;
			break;
		}
		break;
	}

	return hdrlen;
}


struct net_device_stats *hostap_get_stats(struct net_device *dev)
{
	struct hostap_interface *iface;
	iface = netdev_priv(dev);
	return &iface->stats;
}


static int prism2_close(struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;

	PDEBUG(DEBUG_FLOW, "%s: prism2_close\n", dev->name);

	iface = netdev_priv(dev);
	local = iface->local;

	if (dev == local->ddev) {
		prism2_sta_deauth(local, WLAN_REASON_DEAUTH_LEAVING);
	}
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	if (!local->hostapd && dev == local->dev &&
	    (!local->func->card_present || local->func->card_present(local)) &&
	    local->hw_ready && local->ap && local->iw_mode == IW_MODE_MASTER)
		hostap_deauth_all_stas(dev, local->ap, 1);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

	if (dev == local->dev) {
		local->func->hw_shutdown(dev, HOSTAP_HW_ENABLE_CMDCOMPL);
	}

	if (netif_running(dev)) {
		netif_stop_queue(dev);
		netif_device_detach(dev);
	}

	flush_scheduled_work();

	module_put(local->hw_module);

	local->num_dev_open--;

	if (dev != local->dev && local->dev->flags & IFF_UP &&
	    local->master_dev_auto_open && local->num_dev_open == 1) {
		/* Close master radio interface automatically if it was also
		 * opened automatically and we are now closing the last
		 * remaining non-master device. */
		dev_close(local->dev);
	}

	return 0;
}


static int prism2_open(struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;

	PDEBUG(DEBUG_FLOW, "%s: prism2_open\n", dev->name);

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->no_pri) {
		printk(KERN_DEBUG "%s: could not set interface UP - no PRI "
		       "f/w\n", dev->name);
		return 1;
	}

	if ((local->func->card_present && !local->func->card_present(local)) ||
	    local->hw_downloading)
		return -ENODEV;

	if (!try_module_get(local->hw_module))
		return -ENODEV;
	local->num_dev_open++;

	if (!local->dev_enabled && local->func->hw_enable(dev, 1)) {
		printk(KERN_WARNING "%s: could not enable MAC port\n",
		       dev->name);
		prism2_close(dev);
		return 1;
	}
	if (!local->dev_enabled)
		prism2_callback(local, PRISM2_CALLBACK_ENABLE);
	local->dev_enabled = 1;

	if (dev != local->dev && !(local->dev->flags & IFF_UP)) {
		/* Master radio interface is needed for all operation, so open
		 * it automatically when any virtual net_device is opened. */
		local->master_dev_auto_open = 1;
		dev_open(local->dev);
	}

	netif_device_attach(dev);
	netif_start_queue(dev);

	return 0;
}


static int prism2_set_mac_address(struct net_device *dev, void *p)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct list_head *ptr;
	struct sockaddr *addr = p;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->func->set_rid(dev, HFA384X_RID_CNFOWNMACADDR, addr->sa_data,
				 ETH_ALEN) < 0 || local->func->reset_port(dev))
		return -EINVAL;

	read_lock_bh(&local->iface_lock);
	list_for_each(ptr, &local->hostap_interfaces) {
		iface = list_entry(ptr, struct hostap_interface, list);
		memcpy(iface->dev->dev_addr, addr->sa_data, ETH_ALEN);
	}
	memcpy(local->dev->dev_addr, addr->sa_data, ETH_ALEN);
	read_unlock_bh(&local->iface_lock);

	return 0;
}


/* TODO: to be further implemented as soon as Prism2 fully supports
 *       GroupAddresses and correct documentation is available */
void hostap_set_multicast_list_queue(struct work_struct *work)
{
	local_info_t *local =
		container_of(work, local_info_t, set_multicast_list_queue);
	struct net_device *dev = local->dev;
	struct hostap_interface *iface;

	iface = netdev_priv(dev);
	if (hostap_set_word(dev, HFA384X_RID_PROMISCUOUSMODE,
			    local->is_promisc)) {
		printk(KERN_INFO "%s: %sabling promiscuous mode failed\n",
		       dev->name, local->is_promisc ? "en" : "dis");
	}
}


static void hostap_set_multicast_list(struct net_device *dev)
{
#if 0
	/* FIX: promiscuous mode seems to be causing a lot of problems with
	 * some station firmware versions (FCSErr frames, invalid MACPort, etc.
	 * corrupted incoming frames). This code is now commented out while the
	 * problems are investigated. */
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;
	if ((dev->flags & IFF_ALLMULTI) || (dev->flags & IFF_PROMISC)) {
		local->is_promisc = 1;
	} else {
		local->is_promisc = 0;
	}

	schedule_work(&local->set_multicast_list_queue);
#endif
}


static int prism2_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < PRISM2_MIN_MTU || new_mtu > PRISM2_MAX_MTU)
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}


static void prism2_tx_timeout(struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct hfa384x_regs regs;

	iface = netdev_priv(dev);
	local = iface->local;

	printk(KERN_WARNING "%s Tx timed out! Resetting card\n", dev->name);
	netif_stop_queue(local->dev);

	local->func->read_regs(dev, &regs);
	printk(KERN_DEBUG "%s: CMD=%04x EVSTAT=%04x "
	       "OFFSET0=%04x OFFSET1=%04x SWSUPPORT0=%04x\n",
	       dev->name, regs.cmd, regs.evstat, regs.offset0, regs.offset1,
	       regs.swsupport0);

	local->func->schedule_reset(local);
}


void hostap_setup_dev(struct net_device *dev, local_info_t *local,
		      int main_dev)
{
	struct hostap_interface *iface;

	iface = netdev_priv(dev);
	ether_setup(dev);

	/* kernel callbacks */
	dev->get_stats = hostap_get_stats;
	if (iface) {
		/* Currently, we point to the proper spy_data only on
		 * the main_dev. This could be fixed. Jean II */
		iface->wireless_data.spy_data = &iface->spy_data;
		dev->wireless_data = &iface->wireless_data;
	}
	dev->wireless_handlers =
		(struct iw_handler_def *) &hostap_iw_handler_def;
	dev->do_ioctl = hostap_ioctl;
	dev->open = prism2_open;
	dev->stop = prism2_close;
	dev->hard_start_xmit = hostap_data_start_xmit;
	dev->set_mac_address = prism2_set_mac_address;
	dev->set_multicast_list = hostap_set_multicast_list;
	dev->change_mtu = prism2_change_mtu;
	dev->tx_timeout = prism2_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	dev->mtu = local->mtu;
	if (!main_dev) {
		/* use main radio device queue */
		dev->tx_queue_len = 0;
	}

	SET_ETHTOOL_OPS(dev, &prism2_ethtool_ops);

	netif_stop_queue(dev);
}


static int hostap_enable_hostapd(local_info_t *local, int rtnl_locked)
{
	struct net_device *dev = local->dev;

	if (local->apdev)
		return -EEXIST;

	printk(KERN_DEBUG "%s: enabling hostapd mode\n", dev->name);

	local->apdev = hostap_add_interface(local, HOSTAP_INTERFACE_AP,
					    rtnl_locked, local->ddev->name,
					    "ap");
	if (local->apdev == NULL)
		return -ENOMEM;

	local->apdev->hard_start_xmit = hostap_mgmt_start_xmit;
	local->apdev->type = ARPHRD_IEEE80211;
	local->apdev->hard_header_parse = hostap_80211_header_parse;

	return 0;
}


static int hostap_disable_hostapd(local_info_t *local, int rtnl_locked)
{
	struct net_device *dev = local->dev;

	printk(KERN_DEBUG "%s: disabling hostapd mode\n", dev->name);

	hostap_remove_interface(local->apdev, rtnl_locked, 1);
	local->apdev = NULL;

	return 0;
}


static int hostap_enable_hostapd_sta(local_info_t *local, int rtnl_locked)
{
	struct net_device *dev = local->dev;

	if (local->stadev)
		return -EEXIST;

	printk(KERN_DEBUG "%s: enabling hostapd STA mode\n", dev->name);

	local->stadev = hostap_add_interface(local, HOSTAP_INTERFACE_STA,
					     rtnl_locked, local->ddev->name,
					     "sta");
	if (local->stadev == NULL)
		return -ENOMEM;

	return 0;
}


static int hostap_disable_hostapd_sta(local_info_t *local, int rtnl_locked)
{
	struct net_device *dev = local->dev;

	printk(KERN_DEBUG "%s: disabling hostapd mode\n", dev->name);

	hostap_remove_interface(local->stadev, rtnl_locked, 1);
	local->stadev = NULL;

	return 0;
}


int hostap_set_hostapd(local_info_t *local, int val, int rtnl_locked)
{
	int ret;

	if (val < 0 || val > 1)
		return -EINVAL;

	if (local->hostapd == val)
		return 0;

	if (val) {
		ret = hostap_enable_hostapd(local, rtnl_locked);
		if (ret == 0)
			local->hostapd = 1;
	} else {
		local->hostapd = 0;
		ret = hostap_disable_hostapd(local, rtnl_locked);
		if (ret != 0)
			local->hostapd = 1;
	}

	return ret;
}


int hostap_set_hostapd_sta(local_info_t *local, int val, int rtnl_locked)
{
	int ret;

	if (val < 0 || val > 1)
		return -EINVAL;

	if (local->hostapd_sta == val)
		return 0;

	if (val) {
		ret = hostap_enable_hostapd_sta(local, rtnl_locked);
		if (ret == 0)
			local->hostapd_sta = 1;
	} else {
		local->hostapd_sta = 0;
		ret = hostap_disable_hostapd_sta(local, rtnl_locked);
		if (ret != 0)
			local->hostapd_sta = 1;
	}


	return ret;
}


int prism2_update_comms_qual(struct net_device *dev)
{
	struct hostap_interface *iface;
	local_info_t *local;
	int ret = 0;
	struct hfa384x_comms_quality sq;

	iface = netdev_priv(dev);
	local = iface->local;
	if (!local->sta_fw_ver)
		ret = -1;
	else if (local->sta_fw_ver >= PRISM2_FW_VER(1,3,1)) {
		if (local->func->get_rid(local->dev,
					 HFA384X_RID_DBMCOMMSQUALITY,
					 &sq, sizeof(sq), 1) >= 0) {
			local->comms_qual = (s16) le16_to_cpu(sq.comm_qual);
			local->avg_signal = (s16) le16_to_cpu(sq.signal_level);
			local->avg_noise = (s16) le16_to_cpu(sq.noise_level);
			local->last_comms_qual_update = jiffies;
		} else
			ret = -1;
	} else {
		if (local->func->get_rid(local->dev, HFA384X_RID_COMMSQUALITY,
					 &sq, sizeof(sq), 1) >= 0) {
			local->comms_qual = le16_to_cpu(sq.comm_qual);
			local->avg_signal = HFA384X_LEVEL_TO_dBm(
				le16_to_cpu(sq.signal_level));
			local->avg_noise = HFA384X_LEVEL_TO_dBm(
				le16_to_cpu(sq.noise_level));
			local->last_comms_qual_update = jiffies;
		} else
			ret = -1;
	}

	return ret;
}


int prism2_sta_send_mgmt(local_info_t *local, u8 *dst, u16 stype,
			 u8 *body, size_t bodylen)
{
	struct sk_buff *skb;
	struct hostap_ieee80211_mgmt *mgmt;
	struct hostap_skb_tx_data *meta;
	struct net_device *dev = local->dev;

	skb = dev_alloc_skb(IEEE80211_MGMT_HDR_LEN + bodylen);
	if (skb == NULL)
		return -ENOMEM;

	mgmt = (struct hostap_ieee80211_mgmt *)
		skb_put(skb, IEEE80211_MGMT_HDR_LEN);
	memset(mgmt, 0, IEEE80211_MGMT_HDR_LEN);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | stype);
	memcpy(mgmt->da, dst, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, dst, ETH_ALEN);
	if (body)
		memcpy(skb_put(skb, bodylen), body, bodylen);

	meta = (struct hostap_skb_tx_data *) skb->cb;
	memset(meta, 0, sizeof(*meta));
	meta->magic = HOSTAP_SKB_TX_DATA_MAGIC;
	meta->iface = netdev_priv(dev);

	skb->dev = dev;
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	dev_queue_xmit(skb);

	return 0;
}


int prism2_sta_deauth(local_info_t *local, u16 reason)
{
	union iwreq_data wrqu;
	int ret;

	if (local->iw_mode != IW_MODE_INFRA ||
	    memcmp(local->bssid, "\x00\x00\x00\x00\x00\x00", ETH_ALEN) == 0 ||
	    memcmp(local->bssid, "\x44\x44\x44\x44\x44\x44", ETH_ALEN) == 0)
		return 0;

	reason = cpu_to_le16(reason);
	ret = prism2_sta_send_mgmt(local, local->bssid, IEEE80211_STYPE_DEAUTH,
				   (u8 *) &reason, 2);
	memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	wireless_send_event(local->dev, SIOCGIWAP, &wrqu, NULL);
	return ret;
}


struct proc_dir_entry *hostap_proc;

static int __init hostap_init(void)
{
	if (proc_net != NULL) {
		hostap_proc = proc_mkdir("hostap", proc_net);
		if (!hostap_proc)
			printk(KERN_WARNING "Failed to mkdir "
			       "/proc/net/hostap\n");
	} else
		hostap_proc = NULL;

	return 0;
}


static void __exit hostap_exit(void)
{
	if (hostap_proc != NULL) {
		hostap_proc = NULL;
		remove_proc_entry("hostap", proc_net);
	}
}


EXPORT_SYMBOL(hostap_set_word);
EXPORT_SYMBOL(hostap_set_string);
EXPORT_SYMBOL(hostap_get_porttype);
EXPORT_SYMBOL(hostap_set_encryption);
EXPORT_SYMBOL(hostap_set_antsel);
EXPORT_SYMBOL(hostap_set_roaming);
EXPORT_SYMBOL(hostap_set_auth_algs);
EXPORT_SYMBOL(hostap_dump_rx_header);
EXPORT_SYMBOL(hostap_dump_tx_header);
EXPORT_SYMBOL(hostap_80211_header_parse);
EXPORT_SYMBOL(hostap_80211_get_hdrlen);
EXPORT_SYMBOL(hostap_get_stats);
EXPORT_SYMBOL(hostap_setup_dev);
EXPORT_SYMBOL(hostap_set_multicast_list_queue);
EXPORT_SYMBOL(hostap_set_hostapd);
EXPORT_SYMBOL(hostap_set_hostapd_sta);
EXPORT_SYMBOL(hostap_add_interface);
EXPORT_SYMBOL(hostap_remove_interface);
EXPORT_SYMBOL(prism2_update_comms_qual);

module_init(hostap_init);
module_exit(hostap_exit);
