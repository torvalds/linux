/*
 * Intersil Prism2 driver with Host AP (software access point) support
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <j@w1.fi>
 * Copyright (c) 2002-2005, Jouni Malinen <j@w1.fi>
 *
 * This file is to be included into hostap.c when S/W AP functionality is
 * compiled.
 *
 * AP:  FIX:
 * - if unicast Class 2 (assoc,reassoc,disassoc) frame received from
 *   unauthenticated STA, send deauth. frame (8802.11: 5.5)
 * - if unicast Class 3 (data with to/from DS,deauth,pspoll) frame received
 *   from authenticated, but unassoc STA, send disassoc frame (8802.11: 5.5)
 * - if unicast Class 3 received from unauthenticated STA, send deauth. frame
 *   (8802.11: 5.5)
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/moduleparam.h>
#include <linux/etherdevice.h>

#include "hostap_wlan.h"
#include "hostap.h"
#include "hostap_ap.h"

static int other_ap_policy[MAX_PARM_DEVICES] = { AP_OTHER_AP_SKIP_ALL,
						 DEF_INTS };
module_param_array(other_ap_policy, int, NULL, 0444);
MODULE_PARM_DESC(other_ap_policy, "Other AP beacon monitoring policy (0-3)");

static int ap_max_inactivity[MAX_PARM_DEVICES] = { AP_MAX_INACTIVITY_SEC,
						   DEF_INTS };
module_param_array(ap_max_inactivity, int, NULL, 0444);
MODULE_PARM_DESC(ap_max_inactivity, "AP timeout (in seconds) for station "
		 "inactivity");

static int ap_bridge_packets[MAX_PARM_DEVICES] = { 1, DEF_INTS };
module_param_array(ap_bridge_packets, int, NULL, 0444);
MODULE_PARM_DESC(ap_bridge_packets, "Bridge packets directly between "
		 "stations");

static int autom_ap_wds[MAX_PARM_DEVICES] = { 0, DEF_INTS };
module_param_array(autom_ap_wds, int, NULL, 0444);
MODULE_PARM_DESC(autom_ap_wds, "Add WDS connections to other APs "
		 "automatically");


static struct sta_info* ap_get_sta(struct ap_data *ap, u8 *sta);
static void hostap_event_expired_sta(struct net_device *dev,
				     struct sta_info *sta);
static void handle_add_proc_queue(struct work_struct *work);

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
static void handle_wds_oper_queue(struct work_struct *work);
static void prism2_send_mgmt(struct net_device *dev,
			     u16 type_subtype, char *body,
			     int body_len, u8 *addr, u16 tx_cb_idx);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


#ifndef PRISM2_NO_PROCFS_DEBUG
static int ap_debug_proc_show(struct seq_file *m, void *v)
{
	struct ap_data *ap = m->private;

	seq_printf(m, "BridgedUnicastFrames=%u\n", ap->bridged_unicast);
	seq_printf(m, "BridgedMulticastFrames=%u\n", ap->bridged_multicast);
	seq_printf(m, "max_inactivity=%u\n", ap->max_inactivity / HZ);
	seq_printf(m, "bridge_packets=%u\n", ap->bridge_packets);
	seq_printf(m, "nullfunc_ack=%u\n", ap->nullfunc_ack);
	seq_printf(m, "autom_ap_wds=%u\n", ap->autom_ap_wds);
	seq_printf(m, "auth_algs=%u\n", ap->local->auth_algs);
	seq_printf(m, "tx_drop_nonassoc=%u\n", ap->tx_drop_nonassoc);
	return 0;
}

static int ap_debug_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ap_debug_proc_show, PDE_DATA(inode));
}

static const struct file_operations ap_debug_proc_fops = {
	.open		= ap_debug_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif /* PRISM2_NO_PROCFS_DEBUG */


static void ap_sta_hash_add(struct ap_data *ap, struct sta_info *sta)
{
	sta->hnext = ap->sta_hash[STA_HASH(sta->addr)];
	ap->sta_hash[STA_HASH(sta->addr)] = sta;
}

static void ap_sta_hash_del(struct ap_data *ap, struct sta_info *sta)
{
	struct sta_info *s;

	s = ap->sta_hash[STA_HASH(sta->addr)];
	if (s == NULL) return;
	if (ether_addr_equal(s->addr, sta->addr)) {
		ap->sta_hash[STA_HASH(sta->addr)] = s->hnext;
		return;
	}

	while (s->hnext != NULL && !ether_addr_equal(s->hnext->addr, sta->addr))
		s = s->hnext;
	if (s->hnext != NULL)
		s->hnext = s->hnext->hnext;
	else
		printk("AP: could not remove STA %pM from hash table\n",
		       sta->addr);
}

static void ap_free_sta(struct ap_data *ap, struct sta_info *sta)
{
	if (sta->ap && sta->local)
		hostap_event_expired_sta(sta->local->dev, sta);

	if (ap->proc != NULL) {
		char name[20];
		sprintf(name, "%pM", sta->addr);
		remove_proc_entry(name, ap->proc);
	}

	if (sta->crypt) {
		sta->crypt->ops->deinit(sta->crypt->priv);
		kfree(sta->crypt);
		sta->crypt = NULL;
	}

	skb_queue_purge(&sta->tx_buf);

	ap->num_sta--;
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	if (sta->aid > 0)
		ap->sta_aid[sta->aid - 1] = NULL;

	if (!sta->ap && sta->u.sta.challenge)
		kfree(sta->u.sta.challenge);
	del_timer_sync(&sta->timer);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

	kfree(sta);
}


static void hostap_set_tim(local_info_t *local, int aid, int set)
{
	if (local->func->set_tim)
		local->func->set_tim(local->dev, aid, set);
}


static void hostap_event_new_sta(struct net_device *dev, struct sta_info *sta)
{
	union iwreq_data wrqu;
	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.addr.sa_data, sta->addr, ETH_ALEN);
	wrqu.addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(dev, IWEVREGISTERED, &wrqu, NULL);
}


static void hostap_event_expired_sta(struct net_device *dev,
				     struct sta_info *sta)
{
	union iwreq_data wrqu;
	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.addr.sa_data, sta->addr, ETH_ALEN);
	wrqu.addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(dev, IWEVEXPIRED, &wrqu, NULL);
}


#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT

static void ap_handle_timer(unsigned long data)
{
	struct sta_info *sta = (struct sta_info *) data;
	local_info_t *local;
	struct ap_data *ap;
	unsigned long next_time = 0;
	int was_assoc;

	if (sta == NULL || sta->local == NULL || sta->local->ap == NULL) {
		PDEBUG(DEBUG_AP, "ap_handle_timer() called with NULL data\n");
		return;
	}

	local = sta->local;
	ap = local->ap;
	was_assoc = sta->flags & WLAN_STA_ASSOC;

	if (atomic_read(&sta->users) != 0)
		next_time = jiffies + HZ;
	else if ((sta->flags & WLAN_STA_PERM) && !(sta->flags & WLAN_STA_AUTH))
		next_time = jiffies + ap->max_inactivity;

	if (time_before(jiffies, sta->last_rx + ap->max_inactivity)) {
		/* station activity detected; reset timeout state */
		sta->timeout_next = STA_NULLFUNC;
		next_time = sta->last_rx + ap->max_inactivity;
	} else if (sta->timeout_next == STA_DISASSOC &&
		   !(sta->flags & WLAN_STA_PENDING_POLL)) {
		/* STA ACKed data nullfunc frame poll */
		sta->timeout_next = STA_NULLFUNC;
		next_time = jiffies + ap->max_inactivity;
	}

	if (next_time) {
		sta->timer.expires = next_time;
		add_timer(&sta->timer);
		return;
	}

	if (sta->ap)
		sta->timeout_next = STA_DEAUTH;

	if (sta->timeout_next == STA_DEAUTH && !(sta->flags & WLAN_STA_PERM)) {
		spin_lock(&ap->sta_table_lock);
		ap_sta_hash_del(ap, sta);
		list_del(&sta->list);
		spin_unlock(&ap->sta_table_lock);
		sta->flags &= ~(WLAN_STA_AUTH | WLAN_STA_ASSOC);
	} else if (sta->timeout_next == STA_DISASSOC)
		sta->flags &= ~WLAN_STA_ASSOC;

	if (was_assoc && !(sta->flags & WLAN_STA_ASSOC) && !sta->ap)
		hostap_event_expired_sta(local->dev, sta);

	if (sta->timeout_next == STA_DEAUTH && sta->aid > 0 &&
	    !skb_queue_empty(&sta->tx_buf)) {
		hostap_set_tim(local, sta->aid, 0);
		sta->flags &= ~WLAN_STA_TIM;
	}

	if (sta->ap) {
		if (ap->autom_ap_wds) {
			PDEBUG(DEBUG_AP, "%s: removing automatic WDS "
			       "connection to AP %pM\n",
			       local->dev->name, sta->addr);
			hostap_wds_link_oper(local, sta->addr, WDS_DEL);
		}
	} else if (sta->timeout_next == STA_NULLFUNC) {
		/* send data frame to poll STA and check whether this frame
		 * is ACKed */
		/* FIX: IEEE80211_STYPE_NULLFUNC would be more appropriate, but
		 * it is apparently not retried so TX Exc events are not
		 * received for it */
		sta->flags |= WLAN_STA_PENDING_POLL;
		prism2_send_mgmt(local->dev, IEEE80211_FTYPE_DATA |
				 IEEE80211_STYPE_DATA, NULL, 0,
				 sta->addr, ap->tx_callback_poll);
	} else {
		int deauth = sta->timeout_next == STA_DEAUTH;
		__le16 resp;
		PDEBUG(DEBUG_AP, "%s: sending %s info to STA %pM"
		       "(last=%lu, jiffies=%lu)\n",
		       local->dev->name,
		       deauth ? "deauthentication" : "disassociation",
		       sta->addr, sta->last_rx, jiffies);

		resp = cpu_to_le16(deauth ? WLAN_REASON_PREV_AUTH_NOT_VALID :
				   WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY);
		prism2_send_mgmt(local->dev, IEEE80211_FTYPE_MGMT |
				 (deauth ? IEEE80211_STYPE_DEAUTH :
				  IEEE80211_STYPE_DISASSOC),
				 (char *) &resp, 2, sta->addr, 0);
	}

	if (sta->timeout_next == STA_DEAUTH) {
		if (sta->flags & WLAN_STA_PERM) {
			PDEBUG(DEBUG_AP, "%s: STA %pM"
			       " would have been removed, "
			       "but it has 'perm' flag\n",
			       local->dev->name, sta->addr);
		} else
			ap_free_sta(ap, sta);
		return;
	}

	if (sta->timeout_next == STA_NULLFUNC) {
		sta->timeout_next = STA_DISASSOC;
		sta->timer.expires = jiffies + AP_DISASSOC_DELAY;
	} else {
		sta->timeout_next = STA_DEAUTH;
		sta->timer.expires = jiffies + AP_DEAUTH_DELAY;
	}

	add_timer(&sta->timer);
}


void hostap_deauth_all_stas(struct net_device *dev, struct ap_data *ap,
			    int resend)
{
	u8 addr[ETH_ALEN];
	__le16 resp;
	int i;

	PDEBUG(DEBUG_AP, "%s: Deauthenticate all stations\n", dev->name);
	memset(addr, 0xff, ETH_ALEN);

	resp = cpu_to_le16(WLAN_REASON_PREV_AUTH_NOT_VALID);

	/* deauth message sent; try to resend it few times; the message is
	 * broadcast, so it may be delayed until next DTIM; there is not much
	 * else we can do at this point since the driver is going to be shut
	 * down */
	for (i = 0; i < 5; i++) {
		prism2_send_mgmt(dev, IEEE80211_FTYPE_MGMT |
				 IEEE80211_STYPE_DEAUTH,
				 (char *) &resp, 2, addr, 0);

		if (!resend || ap->num_sta <= 0)
			return;

		mdelay(50);
	}
}


static int ap_control_proc_show(struct seq_file *m, void *v)
{
	struct ap_data *ap = m->private;
	char *policy_txt;
	struct mac_entry *entry;

	if (v == SEQ_START_TOKEN) {
		switch (ap->mac_restrictions.policy) {
		case MAC_POLICY_OPEN:
			policy_txt = "open";
			break;
		case MAC_POLICY_ALLOW:
			policy_txt = "allow";
			break;
		case MAC_POLICY_DENY:
			policy_txt = "deny";
			break;
		default:
			policy_txt = "unknown";
			break;
		}
		seq_printf(m, "MAC policy: %s\n", policy_txt);
		seq_printf(m, "MAC entries: %u\n", ap->mac_restrictions.entries);
		seq_puts(m, "MAC list:\n");
		return 0;
	}

	entry = v;
	seq_printf(m, "%pM\n", entry->addr);
	return 0;
}

static void *ap_control_proc_start(struct seq_file *m, loff_t *_pos)
{
	struct ap_data *ap = m->private;
	spin_lock_bh(&ap->mac_restrictions.lock);
	return seq_list_start_head(&ap->mac_restrictions.mac_list, *_pos);
}

static void *ap_control_proc_next(struct seq_file *m, void *v, loff_t *_pos)
{
	struct ap_data *ap = m->private;
	return seq_list_next(v, &ap->mac_restrictions.mac_list, _pos);
}

static void ap_control_proc_stop(struct seq_file *m, void *v)
{
	struct ap_data *ap = m->private;
	spin_unlock_bh(&ap->mac_restrictions.lock);
}

static const struct seq_operations ap_control_proc_seqops = {
	.start	= ap_control_proc_start,
	.next	= ap_control_proc_next,
	.stop	= ap_control_proc_stop,
	.show	= ap_control_proc_show,
};

static int ap_control_proc_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &ap_control_proc_seqops);
	if (ret == 0) {
		struct seq_file *m = file->private_data;
		m->private = PDE_DATA(inode);
	}
	return ret;
}

static const struct file_operations ap_control_proc_fops = {
	.open		= ap_control_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};


int ap_control_add_mac(struct mac_restrictions *mac_restrictions, u8 *mac)
{
	struct mac_entry *entry;

	entry = kmalloc(sizeof(struct mac_entry), GFP_KERNEL);
	if (entry == NULL)
		return -ENOMEM;

	memcpy(entry->addr, mac, ETH_ALEN);

	spin_lock_bh(&mac_restrictions->lock);
	list_add_tail(&entry->list, &mac_restrictions->mac_list);
	mac_restrictions->entries++;
	spin_unlock_bh(&mac_restrictions->lock);

	return 0;
}


int ap_control_del_mac(struct mac_restrictions *mac_restrictions, u8 *mac)
{
	struct list_head *ptr;
	struct mac_entry *entry;

	spin_lock_bh(&mac_restrictions->lock);
	for (ptr = mac_restrictions->mac_list.next;
	     ptr != &mac_restrictions->mac_list; ptr = ptr->next) {
		entry = list_entry(ptr, struct mac_entry, list);

		if (ether_addr_equal(entry->addr, mac)) {
			list_del(ptr);
			kfree(entry);
			mac_restrictions->entries--;
			spin_unlock_bh(&mac_restrictions->lock);
			return 0;
		}
	}
	spin_unlock_bh(&mac_restrictions->lock);
	return -1;
}


static int ap_control_mac_deny(struct mac_restrictions *mac_restrictions,
			       u8 *mac)
{
	struct mac_entry *entry;
	int found = 0;

	if (mac_restrictions->policy == MAC_POLICY_OPEN)
		return 0;

	spin_lock_bh(&mac_restrictions->lock);
	list_for_each_entry(entry, &mac_restrictions->mac_list, list) {
		if (ether_addr_equal(entry->addr, mac)) {
			found = 1;
			break;
		}
	}
	spin_unlock_bh(&mac_restrictions->lock);

	if (mac_restrictions->policy == MAC_POLICY_ALLOW)
		return !found;
	else
		return found;
}


void ap_control_flush_macs(struct mac_restrictions *mac_restrictions)
{
	struct list_head *ptr, *n;
	struct mac_entry *entry;

	if (mac_restrictions->entries == 0)
		return;

	spin_lock_bh(&mac_restrictions->lock);
	for (ptr = mac_restrictions->mac_list.next, n = ptr->next;
	     ptr != &mac_restrictions->mac_list;
	     ptr = n, n = ptr->next) {
		entry = list_entry(ptr, struct mac_entry, list);
		list_del(ptr);
		kfree(entry);
	}
	mac_restrictions->entries = 0;
	spin_unlock_bh(&mac_restrictions->lock);
}


int ap_control_kick_mac(struct ap_data *ap, struct net_device *dev, u8 *mac)
{
	struct sta_info *sta;
	__le16 resp;

	spin_lock_bh(&ap->sta_table_lock);
	sta = ap_get_sta(ap, mac);
	if (sta) {
		ap_sta_hash_del(ap, sta);
		list_del(&sta->list);
	}
	spin_unlock_bh(&ap->sta_table_lock);

	if (!sta)
		return -EINVAL;

	resp = cpu_to_le16(WLAN_REASON_PREV_AUTH_NOT_VALID);
	prism2_send_mgmt(dev, IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DEAUTH,
			 (char *) &resp, 2, sta->addr, 0);

	if ((sta->flags & WLAN_STA_ASSOC) && !sta->ap)
		hostap_event_expired_sta(dev, sta);

	ap_free_sta(ap, sta);

	return 0;
}

#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


void ap_control_kickall(struct ap_data *ap)
{
	struct list_head *ptr, *n;
	struct sta_info *sta;

	spin_lock_bh(&ap->sta_table_lock);
	for (ptr = ap->sta_list.next, n = ptr->next; ptr != &ap->sta_list;
	     ptr = n, n = ptr->next) {
		sta = list_entry(ptr, struct sta_info, list);
		ap_sta_hash_del(ap, sta);
		list_del(&sta->list);
		if ((sta->flags & WLAN_STA_ASSOC) && !sta->ap && sta->local)
			hostap_event_expired_sta(sta->local->dev, sta);
		ap_free_sta(ap, sta);
	}
	spin_unlock_bh(&ap->sta_table_lock);
}


#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT

static int prism2_ap_proc_show(struct seq_file *m, void *v)
{
	struct sta_info *sta = v;
	int i;

	if (v == SEQ_START_TOKEN) {
		seq_printf(m, "# BSSID CHAN SIGNAL NOISE RATE SSID FLAGS\n");
		return 0;
	}

	if (!sta->ap)
		return 0;

	seq_printf(m, "%pM %d %d %d %d '",
		   sta->addr,
		   sta->u.ap.channel, sta->last_rx_signal,
		   sta->last_rx_silence, sta->last_rx_rate);

	for (i = 0; i < sta->u.ap.ssid_len; i++) {
		if (sta->u.ap.ssid[i] >= 32 && sta->u.ap.ssid[i] < 127)
			seq_putc(m, sta->u.ap.ssid[i]);
		else
			seq_printf(m, "<%02x>", sta->u.ap.ssid[i]);
	}

	seq_putc(m, '\'');
	if (sta->capability & WLAN_CAPABILITY_ESS)
		seq_puts(m, " [ESS]");
	if (sta->capability & WLAN_CAPABILITY_IBSS)
		seq_puts(m, " [IBSS]");
	if (sta->capability & WLAN_CAPABILITY_PRIVACY)
		seq_puts(m, " [WEP]");
	seq_putc(m, '\n');
	return 0;
}

static void *prism2_ap_proc_start(struct seq_file *m, loff_t *_pos)
{
	struct ap_data *ap = m->private;
	spin_lock_bh(&ap->sta_table_lock);
	return seq_list_start_head(&ap->sta_list, *_pos);
}

static void *prism2_ap_proc_next(struct seq_file *m, void *v, loff_t *_pos)
{
	struct ap_data *ap = m->private;
	return seq_list_next(v, &ap->sta_list, _pos);
}

static void prism2_ap_proc_stop(struct seq_file *m, void *v)
{
	struct ap_data *ap = m->private;
	spin_unlock_bh(&ap->sta_table_lock);
}

static const struct seq_operations prism2_ap_proc_seqops = {
	.start	= prism2_ap_proc_start,
	.next	= prism2_ap_proc_next,
	.stop	= prism2_ap_proc_stop,
	.show	= prism2_ap_proc_show,
};

static int prism2_ap_proc_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &prism2_ap_proc_seqops);
	if (ret == 0) {
		struct seq_file *m = file->private_data;
		m->private = PDE_DATA(inode);
	}
	return ret;
}

static const struct file_operations prism2_ap_proc_fops = {
	.open		= prism2_ap_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


void hostap_check_sta_fw_version(struct ap_data *ap, int sta_fw_ver)
{
	if (!ap)
		return;

	if (sta_fw_ver == PRISM2_FW_VER(0,8,0)) {
		PDEBUG(DEBUG_AP, "Using data::nullfunc ACK workaround - "
		       "firmware upgrade recommended\n");
		ap->nullfunc_ack = 1;
	} else
		ap->nullfunc_ack = 0;

	if (sta_fw_ver == PRISM2_FW_VER(1,4,2)) {
		printk(KERN_WARNING "%s: Warning: secondary station firmware "
		       "version 1.4.2 does not seem to work in Host AP mode\n",
		       ap->local->dev->name);
	}
}


/* Called only as a tasklet (software IRQ) */
static void hostap_ap_tx_cb(struct sk_buff *skb, int ok, void *data)
{
	struct ap_data *ap = data;
	struct ieee80211_hdr *hdr;

	if (!ap->local->hostapd || !ap->local->apdev) {
		dev_kfree_skb(skb);
		return;
	}

	/* Pass the TX callback frame to the hostapd; use 802.11 header version
	 * 1 to indicate failure (no ACK) and 2 success (frame ACKed) */

	hdr = (struct ieee80211_hdr *) skb->data;
	hdr->frame_control &= cpu_to_le16(~IEEE80211_FCTL_VERS);
	hdr->frame_control |= cpu_to_le16(ok ? BIT(1) : BIT(0));

	skb->dev = ap->local->apdev;
	skb_pull(skb, hostap_80211_get_hdrlen(hdr->frame_control));
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = cpu_to_be16(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}


#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
/* Called only as a tasklet (software IRQ) */
static void hostap_ap_tx_cb_auth(struct sk_buff *skb, int ok, void *data)
{
	struct ap_data *ap = data;
	struct net_device *dev = ap->local->dev;
	struct ieee80211_hdr *hdr;
	u16 auth_alg, auth_transaction, status;
	__le16 *pos;
	struct sta_info *sta = NULL;
	char *txt = NULL;

	if (ap->local->hostapd) {
		dev_kfree_skb(skb);
		return;
	}

	hdr = (struct ieee80211_hdr *) skb->data;
	if (!ieee80211_is_auth(hdr->frame_control) ||
	    skb->len < IEEE80211_MGMT_HDR_LEN + 6) {
		printk(KERN_DEBUG "%s: hostap_ap_tx_cb_auth received invalid "
		       "frame\n", dev->name);
		dev_kfree_skb(skb);
		return;
	}

	pos = (__le16 *) (skb->data + IEEE80211_MGMT_HDR_LEN);
	auth_alg = le16_to_cpu(*pos++);
	auth_transaction = le16_to_cpu(*pos++);
	status = le16_to_cpu(*pos++);

	if (!ok) {
		txt = "frame was not ACKed";
		goto done;
	}

	spin_lock(&ap->sta_table_lock);
	sta = ap_get_sta(ap, hdr->addr1);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock(&ap->sta_table_lock);

	if (!sta) {
		txt = "STA not found";
		goto done;
	}

	if (status == WLAN_STATUS_SUCCESS &&
	    ((auth_alg == WLAN_AUTH_OPEN && auth_transaction == 2) ||
	     (auth_alg == WLAN_AUTH_SHARED_KEY && auth_transaction == 4))) {
		txt = "STA authenticated";
		sta->flags |= WLAN_STA_AUTH;
		sta->last_auth = jiffies;
	} else if (status != WLAN_STATUS_SUCCESS)
		txt = "authentication failed";

 done:
	if (sta)
		atomic_dec(&sta->users);
	if (txt) {
		PDEBUG(DEBUG_AP, "%s: %pM auth_cb - alg=%d "
		       "trans#=%d status=%d - %s\n",
		       dev->name, hdr->addr1,
		       auth_alg, auth_transaction, status, txt);
	}
	dev_kfree_skb(skb);
}


/* Called only as a tasklet (software IRQ) */
static void hostap_ap_tx_cb_assoc(struct sk_buff *skb, int ok, void *data)
{
	struct ap_data *ap = data;
	struct net_device *dev = ap->local->dev;
	struct ieee80211_hdr *hdr;
	u16 status;
	__le16 *pos;
	struct sta_info *sta = NULL;
	char *txt = NULL;

	if (ap->local->hostapd) {
		dev_kfree_skb(skb);
		return;
	}

	hdr = (struct ieee80211_hdr *) skb->data;
	if ((!ieee80211_is_assoc_resp(hdr->frame_control) &&
	     !ieee80211_is_reassoc_resp(hdr->frame_control)) ||
	    skb->len < IEEE80211_MGMT_HDR_LEN + 4) {
		printk(KERN_DEBUG "%s: hostap_ap_tx_cb_assoc received invalid "
		       "frame\n", dev->name);
		dev_kfree_skb(skb);
		return;
	}

	if (!ok) {
		txt = "frame was not ACKed";
		goto done;
	}

	spin_lock(&ap->sta_table_lock);
	sta = ap_get_sta(ap, hdr->addr1);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock(&ap->sta_table_lock);

	if (!sta) {
		txt = "STA not found";
		goto done;
	}

	pos = (__le16 *) (skb->data + IEEE80211_MGMT_HDR_LEN);
	pos++;
	status = le16_to_cpu(*pos++);
	if (status == WLAN_STATUS_SUCCESS) {
		if (!(sta->flags & WLAN_STA_ASSOC))
			hostap_event_new_sta(dev, sta);
		txt = "STA associated";
		sta->flags |= WLAN_STA_ASSOC;
		sta->last_assoc = jiffies;
	} else
		txt = "association failed";

 done:
	if (sta)
		atomic_dec(&sta->users);
	if (txt) {
		PDEBUG(DEBUG_AP, "%s: %pM assoc_cb - %s\n",
		       dev->name, hdr->addr1, txt);
	}
	dev_kfree_skb(skb);
}

/* Called only as a tasklet (software IRQ); TX callback for poll frames used
 * in verifying whether the STA is still present. */
static void hostap_ap_tx_cb_poll(struct sk_buff *skb, int ok, void *data)
{
	struct ap_data *ap = data;
	struct ieee80211_hdr *hdr;
	struct sta_info *sta;

	if (skb->len < 24)
		goto fail;
	hdr = (struct ieee80211_hdr *) skb->data;
	if (ok) {
		spin_lock(&ap->sta_table_lock);
		sta = ap_get_sta(ap, hdr->addr1);
		if (sta)
			sta->flags &= ~WLAN_STA_PENDING_POLL;
		spin_unlock(&ap->sta_table_lock);
	} else {
		PDEBUG(DEBUG_AP,
		       "%s: STA %pM did not ACK activity poll frame\n",
		       ap->local->dev->name, hdr->addr1);
	}

 fail:
	dev_kfree_skb(skb);
}
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


void hostap_init_data(local_info_t *local)
{
	struct ap_data *ap = local->ap;

	if (ap == NULL) {
		printk(KERN_WARNING "hostap_init_data: ap == NULL\n");
		return;
	}
	memset(ap, 0, sizeof(struct ap_data));
	ap->local = local;

	ap->ap_policy = GET_INT_PARM(other_ap_policy, local->card_idx);
	ap->bridge_packets = GET_INT_PARM(ap_bridge_packets, local->card_idx);
	ap->max_inactivity =
		GET_INT_PARM(ap_max_inactivity, local->card_idx) * HZ;
	ap->autom_ap_wds = GET_INT_PARM(autom_ap_wds, local->card_idx);

	spin_lock_init(&ap->sta_table_lock);
	INIT_LIST_HEAD(&ap->sta_list);

	/* Initialize task queue structure for AP management */
	INIT_WORK(&local->ap->add_sta_proc_queue, handle_add_proc_queue);

	ap->tx_callback_idx =
		hostap_tx_callback_register(local, hostap_ap_tx_cb, ap);
	if (ap->tx_callback_idx == 0)
		printk(KERN_WARNING "%s: failed to register TX callback for "
		       "AP\n", local->dev->name);
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	INIT_WORK(&local->ap->wds_oper_queue, handle_wds_oper_queue);

	ap->tx_callback_auth =
		hostap_tx_callback_register(local, hostap_ap_tx_cb_auth, ap);
	ap->tx_callback_assoc =
		hostap_tx_callback_register(local, hostap_ap_tx_cb_assoc, ap);
	ap->tx_callback_poll =
		hostap_tx_callback_register(local, hostap_ap_tx_cb_poll, ap);
	if (ap->tx_callback_auth == 0 || ap->tx_callback_assoc == 0 ||
		ap->tx_callback_poll == 0)
		printk(KERN_WARNING "%s: failed to register TX callback for "
		       "AP\n", local->dev->name);

	spin_lock_init(&ap->mac_restrictions.lock);
	INIT_LIST_HEAD(&ap->mac_restrictions.mac_list);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

	ap->initialized = 1;
}


void hostap_init_ap_proc(local_info_t *local)
{
	struct ap_data *ap = local->ap;

	ap->proc = local->proc;
	if (ap->proc == NULL)
		return;

#ifndef PRISM2_NO_PROCFS_DEBUG
	proc_create_data("ap_debug", 0, ap->proc, &ap_debug_proc_fops, ap);
#endif /* PRISM2_NO_PROCFS_DEBUG */

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	proc_create_data("ap_control", 0, ap->proc, &ap_control_proc_fops, ap);
	proc_create_data("ap", 0, ap->proc, &prism2_ap_proc_fops, ap);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

}


void hostap_free_data(struct ap_data *ap)
{
	struct sta_info *n, *sta;

	if (ap == NULL || !ap->initialized) {
		printk(KERN_DEBUG "hostap_free_data: ap has not yet been "
		       "initialized - skip resource freeing\n");
		return;
	}

	flush_work(&ap->add_sta_proc_queue);

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	flush_work(&ap->wds_oper_queue);
	if (ap->crypt)
		ap->crypt->deinit(ap->crypt_priv);
	ap->crypt = ap->crypt_priv = NULL;
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

	list_for_each_entry_safe(sta, n, &ap->sta_list, list) {
		ap_sta_hash_del(ap, sta);
		list_del(&sta->list);
		if ((sta->flags & WLAN_STA_ASSOC) && !sta->ap && sta->local)
			hostap_event_expired_sta(sta->local->dev, sta);
		ap_free_sta(ap, sta);
	}

#ifndef PRISM2_NO_PROCFS_DEBUG
	if (ap->proc != NULL) {
		remove_proc_entry("ap_debug", ap->proc);
	}
#endif /* PRISM2_NO_PROCFS_DEBUG */

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	if (ap->proc != NULL) {
	  remove_proc_entry("ap", ap->proc);
		remove_proc_entry("ap_control", ap->proc);
	}
	ap_control_flush_macs(&ap->mac_restrictions);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

	ap->initialized = 0;
}


/* caller should have mutex for AP STA list handling */
static struct sta_info* ap_get_sta(struct ap_data *ap, u8 *sta)
{
	struct sta_info *s;

	s = ap->sta_hash[STA_HASH(sta)];
	while (s != NULL && !ether_addr_equal(s->addr, sta))
		s = s->hnext;
	return s;
}


#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT

/* Called from timer handler and from scheduled AP queue handlers */
static void prism2_send_mgmt(struct net_device *dev,
			     u16 type_subtype, char *body,
			     int body_len, u8 *addr, u16 tx_cb_idx)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct ieee80211_hdr *hdr;
	u16 fc;
	struct sk_buff *skb;
	struct hostap_skb_tx_data *meta;
	int hdrlen;

	iface = netdev_priv(dev);
	local = iface->local;
	dev = local->dev; /* always use master radio device */
	iface = netdev_priv(dev);

	if (!(dev->flags & IFF_UP)) {
		PDEBUG(DEBUG_AP, "%s: prism2_send_mgmt - device is not UP - "
		       "cannot send frame\n", dev->name);
		return;
	}

	skb = dev_alloc_skb(sizeof(*hdr) + body_len);
	if (skb == NULL) {
		PDEBUG(DEBUG_AP, "%s: prism2_send_mgmt failed to allocate "
		       "skb\n", dev->name);
		return;
	}

	fc = type_subtype;
	hdrlen = hostap_80211_get_hdrlen(cpu_to_le16(type_subtype));
	hdr = (struct ieee80211_hdr *) skb_put(skb, hdrlen);
	if (body)
		memcpy(skb_put(skb, body_len), body, body_len);

	memset(hdr, 0, hdrlen);

	/* FIX: ctrl::ack sending used special HFA384X_TX_CTRL_802_11
	 * tx_control instead of using local->tx_control */


	memcpy(hdr->addr1, addr, ETH_ALEN); /* DA / RA */
	if (ieee80211_is_data(hdr->frame_control)) {
		fc |= IEEE80211_FCTL_FROMDS;
		memcpy(hdr->addr2, dev->dev_addr, ETH_ALEN); /* BSSID */
		memcpy(hdr->addr3, dev->dev_addr, ETH_ALEN); /* SA */
	} else if (ieee80211_is_ctl(hdr->frame_control)) {
		/* control:ACK does not have addr2 or addr3 */
		memset(hdr->addr2, 0, ETH_ALEN);
		memset(hdr->addr3, 0, ETH_ALEN);
	} else {
		memcpy(hdr->addr2, dev->dev_addr, ETH_ALEN); /* SA */
		memcpy(hdr->addr3, dev->dev_addr, ETH_ALEN); /* BSSID */
	}

	hdr->frame_control = cpu_to_le16(fc);

	meta = (struct hostap_skb_tx_data *) skb->cb;
	memset(meta, 0, sizeof(*meta));
	meta->magic = HOSTAP_SKB_TX_DATA_MAGIC;
	meta->iface = iface;
	meta->tx_cb_idx = tx_cb_idx;

	skb->dev = dev;
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	dev_queue_xmit(skb);
}
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


static int prism2_sta_proc_show(struct seq_file *m, void *v)
{
	struct sta_info *sta = m->private;
	int i;

	/* FIX: possible race condition.. the STA data could have just expired,
	 * but proc entry was still here so that the read could have started;
	 * some locking should be done here.. */

	seq_printf(m,
		   "%s=%pM\nusers=%d\naid=%d\n"
		   "flags=0x%04x%s%s%s%s%s%s%s\n"
		   "capability=0x%02x\nlisten_interval=%d\nsupported_rates=",
		   sta->ap ? "AP" : "STA",
		   sta->addr, atomic_read(&sta->users), sta->aid,
		   sta->flags,
		   sta->flags & WLAN_STA_AUTH ? " AUTH" : "",
		   sta->flags & WLAN_STA_ASSOC ? " ASSOC" : "",
		   sta->flags & WLAN_STA_PS ? " PS" : "",
		   sta->flags & WLAN_STA_TIM ? " TIM" : "",
		   sta->flags & WLAN_STA_PERM ? " PERM" : "",
		   sta->flags & WLAN_STA_AUTHORIZED ? " AUTHORIZED" : "",
		   sta->flags & WLAN_STA_PENDING_POLL ? " POLL" : "",
		   sta->capability, sta->listen_interval);
	/* supported_rates: 500 kbit/s units with msb ignored */
	for (i = 0; i < sizeof(sta->supported_rates); i++)
		if (sta->supported_rates[i] != 0)
			seq_printf(m, "%d%sMbps ",
				   (sta->supported_rates[i] & 0x7f) / 2,
				   sta->supported_rates[i] & 1 ? ".5" : "");
	seq_printf(m,
		   "\njiffies=%lu\nlast_auth=%lu\nlast_assoc=%lu\n"
		   "last_rx=%lu\nlast_tx=%lu\nrx_packets=%lu\n"
		   "tx_packets=%lu\n"
		   "rx_bytes=%lu\ntx_bytes=%lu\nbuffer_count=%d\n"
		   "last_rx: silence=%d dBm signal=%d dBm rate=%d%s Mbps\n"
		   "tx_rate=%d\ntx[1M]=%d\ntx[2M]=%d\ntx[5.5M]=%d\n"
		   "tx[11M]=%d\n"
		   "rx[1M]=%d\nrx[2M]=%d\nrx[5.5M]=%d\nrx[11M]=%d\n",
		   jiffies, sta->last_auth, sta->last_assoc, sta->last_rx,
		   sta->last_tx,
		   sta->rx_packets, sta->tx_packets, sta->rx_bytes,
		   sta->tx_bytes, skb_queue_len(&sta->tx_buf),
		   sta->last_rx_silence,
		   sta->last_rx_signal, sta->last_rx_rate / 10,
		   sta->last_rx_rate % 10 ? ".5" : "",
		   sta->tx_rate, sta->tx_count[0], sta->tx_count[1],
		   sta->tx_count[2], sta->tx_count[3],  sta->rx_count[0],
		   sta->rx_count[1], sta->rx_count[2], sta->rx_count[3]);
	if (sta->crypt && sta->crypt->ops && sta->crypt->ops->print_stats)
		sta->crypt->ops->print_stats(m, sta->crypt->priv);
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	if (sta->ap) {
		if (sta->u.ap.channel >= 0)
			seq_printf(m, "channel=%d\n", sta->u.ap.channel);
		seq_puts(m, "ssid=");
		for (i = 0; i < sta->u.ap.ssid_len; i++) {
			if (sta->u.ap.ssid[i] >= 32 && sta->u.ap.ssid[i] < 127)
				seq_putc(m, sta->u.ap.ssid[i]);
			else
				seq_printf(m, "<%02x>", sta->u.ap.ssid[i]);
		}
		seq_putc(m, '\n');
	}
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

	return 0;
}

static int prism2_sta_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, prism2_sta_proc_show, PDE_DATA(inode));
}

static const struct file_operations prism2_sta_proc_fops = {
	.open		= prism2_sta_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void handle_add_proc_queue(struct work_struct *work)
{
	struct ap_data *ap = container_of(work, struct ap_data,
					  add_sta_proc_queue);
	struct sta_info *sta;
	char name[20];
	struct add_sta_proc_data *entry, *prev;

	entry = ap->add_sta_proc_entries;
	ap->add_sta_proc_entries = NULL;

	while (entry) {
		spin_lock_bh(&ap->sta_table_lock);
		sta = ap_get_sta(ap, entry->addr);
		if (sta)
			atomic_inc(&sta->users);
		spin_unlock_bh(&ap->sta_table_lock);

		if (sta) {
			sprintf(name, "%pM", sta->addr);
			sta->proc = proc_create_data(
				name, 0, ap->proc,
				&prism2_sta_proc_fops, sta);

			atomic_dec(&sta->users);
		}

		prev = entry;
		entry = entry->next;
		kfree(prev);
	}
}


static struct sta_info * ap_add_sta(struct ap_data *ap, u8 *addr)
{
	struct sta_info *sta;

	sta = kzalloc(sizeof(struct sta_info), GFP_ATOMIC);
	if (sta == NULL) {
		PDEBUG(DEBUG_AP, "AP: kmalloc failed\n");
		return NULL;
	}

	/* initialize STA info data */
	sta->local = ap->local;
	skb_queue_head_init(&sta->tx_buf);
	memcpy(sta->addr, addr, ETH_ALEN);

	atomic_inc(&sta->users);
	spin_lock_bh(&ap->sta_table_lock);
	list_add(&sta->list, &ap->sta_list);
	ap->num_sta++;
	ap_sta_hash_add(ap, sta);
	spin_unlock_bh(&ap->sta_table_lock);

	if (ap->proc) {
		struct add_sta_proc_data *entry;
		/* schedule a non-interrupt context process to add a procfs
		 * entry for the STA since procfs code use GFP_KERNEL */
		entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
		if (entry) {
			memcpy(entry->addr, sta->addr, ETH_ALEN);
			entry->next = ap->add_sta_proc_entries;
			ap->add_sta_proc_entries = entry;
			schedule_work(&ap->add_sta_proc_queue);
		} else
			printk(KERN_DEBUG "Failed to add STA proc data\n");
	}

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	init_timer(&sta->timer);
	sta->timer.expires = jiffies + ap->max_inactivity;
	sta->timer.data = (unsigned long) sta;
	sta->timer.function = ap_handle_timer;
	if (!ap->local->hostapd)
		add_timer(&sta->timer);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

	return sta;
}


static int ap_tx_rate_ok(int rateidx, struct sta_info *sta,
			 local_info_t *local)
{
	if (rateidx > sta->tx_max_rate ||
	    !(sta->tx_supp_rates & (1 << rateidx)))
		return 0;

	if (local->tx_rate_control != 0 &&
	    !(local->tx_rate_control & (1 << rateidx)))
		return 0;

	return 1;
}


static void prism2_check_tx_rates(struct sta_info *sta)
{
	int i;

	sta->tx_supp_rates = 0;
	for (i = 0; i < sizeof(sta->supported_rates); i++) {
		if ((sta->supported_rates[i] & 0x7f) == 2)
			sta->tx_supp_rates |= WLAN_RATE_1M;
		if ((sta->supported_rates[i] & 0x7f) == 4)
			sta->tx_supp_rates |= WLAN_RATE_2M;
		if ((sta->supported_rates[i] & 0x7f) == 11)
			sta->tx_supp_rates |= WLAN_RATE_5M5;
		if ((sta->supported_rates[i] & 0x7f) == 22)
			sta->tx_supp_rates |= WLAN_RATE_11M;
	}
	sta->tx_max_rate = sta->tx_rate = sta->tx_rate_idx = 0;
	if (sta->tx_supp_rates & WLAN_RATE_1M) {
		sta->tx_max_rate = 0;
		if (ap_tx_rate_ok(0, sta, sta->local)) {
			sta->tx_rate = 10;
			sta->tx_rate_idx = 0;
		}
	}
	if (sta->tx_supp_rates & WLAN_RATE_2M) {
		sta->tx_max_rate = 1;
		if (ap_tx_rate_ok(1, sta, sta->local)) {
			sta->tx_rate = 20;
			sta->tx_rate_idx = 1;
		}
	}
	if (sta->tx_supp_rates & WLAN_RATE_5M5) {
		sta->tx_max_rate = 2;
		if (ap_tx_rate_ok(2, sta, sta->local)) {
			sta->tx_rate = 55;
			sta->tx_rate_idx = 2;
		}
	}
	if (sta->tx_supp_rates & WLAN_RATE_11M) {
		sta->tx_max_rate = 3;
		if (ap_tx_rate_ok(3, sta, sta->local)) {
			sta->tx_rate = 110;
			sta->tx_rate_idx = 3;
		}
	}
}


#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT

static void ap_crypt_init(struct ap_data *ap)
{
	ap->crypt = lib80211_get_crypto_ops("WEP");

	if (ap->crypt) {
		if (ap->crypt->init) {
			ap->crypt_priv = ap->crypt->init(0);
			if (ap->crypt_priv == NULL)
				ap->crypt = NULL;
			else {
				u8 key[WEP_KEY_LEN];
				get_random_bytes(key, WEP_KEY_LEN);
				ap->crypt->set_key(key, WEP_KEY_LEN, NULL,
						   ap->crypt_priv);
			}
		}
	}

	if (ap->crypt == NULL) {
		printk(KERN_WARNING "AP could not initialize WEP: load module "
		       "lib80211_crypt_wep.ko\n");
	}
}


/* Generate challenge data for shared key authentication. IEEE 802.11 specifies
 * that WEP algorithm is used for generating challenge. This should be unique,
 * but otherwise there is not really need for randomness etc. Initialize WEP
 * with pseudo random key and then use increasing IV to get unique challenge
 * streams.
 *
 * Called only as a scheduled task for pending AP frames.
 */
static char * ap_auth_make_challenge(struct ap_data *ap)
{
	char *tmpbuf;
	struct sk_buff *skb;

	if (ap->crypt == NULL) {
		ap_crypt_init(ap);
		if (ap->crypt == NULL)
			return NULL;
	}

	tmpbuf = kmalloc(WLAN_AUTH_CHALLENGE_LEN, GFP_ATOMIC);
	if (tmpbuf == NULL) {
		PDEBUG(DEBUG_AP, "AP: kmalloc failed for challenge\n");
		return NULL;
	}

	skb = dev_alloc_skb(WLAN_AUTH_CHALLENGE_LEN +
			    ap->crypt->extra_mpdu_prefix_len +
			    ap->crypt->extra_mpdu_postfix_len);
	if (skb == NULL) {
		kfree(tmpbuf);
		return NULL;
	}

	skb_reserve(skb, ap->crypt->extra_mpdu_prefix_len);
	memset(skb_put(skb, WLAN_AUTH_CHALLENGE_LEN), 0,
	       WLAN_AUTH_CHALLENGE_LEN);
	if (ap->crypt->encrypt_mpdu(skb, 0, ap->crypt_priv)) {
		dev_kfree_skb(skb);
		kfree(tmpbuf);
		return NULL;
	}

	skb_copy_from_linear_data_offset(skb, ap->crypt->extra_mpdu_prefix_len,
					 tmpbuf, WLAN_AUTH_CHALLENGE_LEN);
	dev_kfree_skb(skb);

	return tmpbuf;
}


/* Called only as a scheduled task for pending AP frames. */
static void handle_authen(local_info_t *local, struct sk_buff *skb,
			  struct hostap_80211_rx_status *rx_stats)
{
	struct net_device *dev = local->dev;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	size_t hdrlen;
	struct ap_data *ap = local->ap;
	char body[8 + WLAN_AUTH_CHALLENGE_LEN], *challenge = NULL;
	int len, olen;
	u16 auth_alg, auth_transaction, status_code;
	__le16 *pos;
	u16 resp = WLAN_STATUS_SUCCESS;
	struct sta_info *sta = NULL;
	struct lib80211_crypt_data *crypt;
	char *txt = "";

	len = skb->len - IEEE80211_MGMT_HDR_LEN;

	hdrlen = hostap_80211_get_hdrlen(hdr->frame_control);

	if (len < 6) {
		PDEBUG(DEBUG_AP, "%s: handle_authen - too short payload "
		       "(len=%d) from %pM\n", dev->name, len, hdr->addr2);
		return;
	}

	spin_lock_bh(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock_bh(&local->ap->sta_table_lock);

	if (sta && sta->crypt)
		crypt = sta->crypt;
	else {
		int idx = 0;
		if (skb->len >= hdrlen + 3)
			idx = skb->data[hdrlen + 3] >> 6;
		crypt = local->crypt_info.crypt[idx];
	}

	pos = (__le16 *) (skb->data + IEEE80211_MGMT_HDR_LEN);
	auth_alg = __le16_to_cpu(*pos);
	pos++;
	auth_transaction = __le16_to_cpu(*pos);
	pos++;
	status_code = __le16_to_cpu(*pos);
	pos++;

	if (ether_addr_equal(dev->dev_addr, hdr->addr2) ||
	    ap_control_mac_deny(&ap->mac_restrictions, hdr->addr2)) {
		txt = "authentication denied";
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	if (((local->auth_algs & PRISM2_AUTH_OPEN) &&
	     auth_alg == WLAN_AUTH_OPEN) ||
	    ((local->auth_algs & PRISM2_AUTH_SHARED_KEY) &&
	     crypt && auth_alg == WLAN_AUTH_SHARED_KEY)) {
	} else {
		txt = "unsupported algorithm";
		resp = WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG;
		goto fail;
	}

	if (len >= 8) {
		u8 *u = (u8 *) pos;
		if (*u == WLAN_EID_CHALLENGE) {
			if (*(u + 1) != WLAN_AUTH_CHALLENGE_LEN) {
				txt = "invalid challenge len";
				resp = WLAN_STATUS_CHALLENGE_FAIL;
				goto fail;
			}
			if (len - 8 < WLAN_AUTH_CHALLENGE_LEN) {
				txt = "challenge underflow";
				resp = WLAN_STATUS_CHALLENGE_FAIL;
				goto fail;
			}
			challenge = (char *) (u + 2);
		}
	}

	if (sta && sta->ap) {
		if (time_after(jiffies, sta->u.ap.last_beacon +
			       (10 * sta->listen_interval * HZ) / 1024)) {
			PDEBUG(DEBUG_AP, "%s: no beacons received for a while,"
			       " assuming AP %pM is now STA\n",
			       dev->name, sta->addr);
			sta->ap = 0;
			sta->flags = 0;
			sta->u.sta.challenge = NULL;
		} else {
			txt = "AP trying to authenticate?";
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
	}

	if ((auth_alg == WLAN_AUTH_OPEN && auth_transaction == 1) ||
	    (auth_alg == WLAN_AUTH_SHARED_KEY &&
	     (auth_transaction == 1 ||
	      (auth_transaction == 3 && sta != NULL &&
	       sta->u.sta.challenge != NULL)))) {
	} else {
		txt = "unknown authentication transaction number";
		resp = WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION;
		goto fail;
	}

	if (sta == NULL) {
		txt = "new STA";

		if (local->ap->num_sta >= MAX_STA_COUNT) {
			/* FIX: might try to remove some old STAs first? */
			txt = "no more room for new STAs";
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}

		sta = ap_add_sta(local->ap, hdr->addr2);
		if (sta == NULL) {
			txt = "ap_add_sta failed";
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
	}

	switch (auth_alg) {
	case WLAN_AUTH_OPEN:
		txt = "authOK";
		/* IEEE 802.11 standard is not completely clear about
		 * whether STA is considered authenticated after
		 * authentication OK frame has been send or after it
		 * has been ACKed. In order to reduce interoperability
		 * issues, mark the STA authenticated before ACK. */
		sta->flags |= WLAN_STA_AUTH;
		break;

	case WLAN_AUTH_SHARED_KEY:
		if (auth_transaction == 1) {
			if (sta->u.sta.challenge == NULL) {
				sta->u.sta.challenge =
					ap_auth_make_challenge(local->ap);
				if (sta->u.sta.challenge == NULL) {
					resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
					goto fail;
				}
			}
		} else {
			if (sta->u.sta.challenge == NULL ||
			    challenge == NULL ||
			    memcmp(sta->u.sta.challenge, challenge,
				   WLAN_AUTH_CHALLENGE_LEN) != 0 ||
			    !ieee80211_has_protected(hdr->frame_control)) {
				txt = "challenge response incorrect";
				resp = WLAN_STATUS_CHALLENGE_FAIL;
				goto fail;
			}

			txt = "challenge OK - authOK";
			/* IEEE 802.11 standard is not completely clear about
			 * whether STA is considered authenticated after
			 * authentication OK frame has been send or after it
			 * has been ACKed. In order to reduce interoperability
			 * issues, mark the STA authenticated before ACK. */
			sta->flags |= WLAN_STA_AUTH;
			kfree(sta->u.sta.challenge);
			sta->u.sta.challenge = NULL;
		}
		break;
	}

 fail:
	pos = (__le16 *) body;
	*pos = cpu_to_le16(auth_alg);
	pos++;
	*pos = cpu_to_le16(auth_transaction + 1);
	pos++;
	*pos = cpu_to_le16(resp); /* status_code */
	pos++;
	olen = 6;

	if (resp == WLAN_STATUS_SUCCESS && sta != NULL &&
	    sta->u.sta.challenge != NULL &&
	    auth_alg == WLAN_AUTH_SHARED_KEY && auth_transaction == 1) {
		u8 *tmp = (u8 *) pos;
		*tmp++ = WLAN_EID_CHALLENGE;
		*tmp++ = WLAN_AUTH_CHALLENGE_LEN;
		pos++;
		memcpy(pos, sta->u.sta.challenge, WLAN_AUTH_CHALLENGE_LEN);
		olen += 2 + WLAN_AUTH_CHALLENGE_LEN;
	}

	prism2_send_mgmt(dev, IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH,
			 body, olen, hdr->addr2, ap->tx_callback_auth);

	if (sta) {
		sta->last_rx = jiffies;
		atomic_dec(&sta->users);
	}

	if (resp) {
		PDEBUG(DEBUG_AP, "%s: %pM auth (alg=%d "
		       "trans#=%d stat=%d len=%d fc=%04x) ==> %d (%s)\n",
		       dev->name, hdr->addr2,
		       auth_alg, auth_transaction, status_code, len,
		       le16_to_cpu(hdr->frame_control), resp, txt);
	}
}


/* Called only as a scheduled task for pending AP frames. */
static void handle_assoc(local_info_t *local, struct sk_buff *skb,
			 struct hostap_80211_rx_status *rx_stats, int reassoc)
{
	struct net_device *dev = local->dev;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	char body[12], *p, *lpos;
	int len, left;
	__le16 *pos;
	u16 resp = WLAN_STATUS_SUCCESS;
	struct sta_info *sta = NULL;
	int send_deauth = 0;
	char *txt = "";
	u8 prev_ap[ETH_ALEN];

	left = len = skb->len - IEEE80211_MGMT_HDR_LEN;

	if (len < (reassoc ? 10 : 4)) {
		PDEBUG(DEBUG_AP, "%s: handle_assoc - too short payload "
		       "(len=%d, reassoc=%d) from %pM\n",
		       dev->name, len, reassoc, hdr->addr2);
		return;
	}

	spin_lock_bh(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta == NULL || (sta->flags & WLAN_STA_AUTH) == 0) {
		spin_unlock_bh(&local->ap->sta_table_lock);
		txt = "trying to associate before authentication";
		send_deauth = 1;
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		sta = NULL; /* do not decrement sta->users */
		goto fail;
	}
	atomic_inc(&sta->users);
	spin_unlock_bh(&local->ap->sta_table_lock);

	pos = (__le16 *) (skb->data + IEEE80211_MGMT_HDR_LEN);
	sta->capability = __le16_to_cpu(*pos);
	pos++; left -= 2;
	sta->listen_interval = __le16_to_cpu(*pos);
	pos++; left -= 2;

	if (reassoc) {
		memcpy(prev_ap, pos, ETH_ALEN);
		pos++; pos++; pos++; left -= 6;
	} else
		memset(prev_ap, 0, ETH_ALEN);

	if (left >= 2) {
		unsigned int ileft;
		unsigned char *u = (unsigned char *) pos;

		if (*u == WLAN_EID_SSID) {
			u++; left--;
			ileft = *u;
			u++; left--;

			if (ileft > left || ileft > MAX_SSID_LEN) {
				txt = "SSID overflow";
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto fail;
			}

			if (ileft != strlen(local->essid) ||
			    memcmp(local->essid, u, ileft) != 0) {
				txt = "not our SSID";
				resp = WLAN_STATUS_ASSOC_DENIED_UNSPEC;
				goto fail;
			}

			u += ileft;
			left -= ileft;
		}

		if (left >= 2 && *u == WLAN_EID_SUPP_RATES) {
			u++; left--;
			ileft = *u;
			u++; left--;

			if (ileft > left || ileft == 0 ||
			    ileft > WLAN_SUPP_RATES_MAX) {
				txt = "SUPP_RATES len error";
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto fail;
			}

			memset(sta->supported_rates, 0,
			       sizeof(sta->supported_rates));
			memcpy(sta->supported_rates, u, ileft);
			prism2_check_tx_rates(sta);

			u += ileft;
			left -= ileft;
		}

		if (left > 0) {
			PDEBUG(DEBUG_AP, "%s: assoc from %pM"
			       " with extra data (%d bytes) [",
			       dev->name, hdr->addr2, left);
			while (left > 0) {
				PDEBUG2(DEBUG_AP, "<%02x>", *u);
				u++; left--;
			}
			PDEBUG2(DEBUG_AP, "]\n");
		}
	} else {
		txt = "frame underflow";
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	/* get a unique AID */
	if (sta->aid > 0)
		txt = "OK, old AID";
	else {
		spin_lock_bh(&local->ap->sta_table_lock);
		for (sta->aid = 1; sta->aid <= MAX_AID_TABLE_SIZE; sta->aid++)
			if (local->ap->sta_aid[sta->aid - 1] == NULL)
				break;
		if (sta->aid > MAX_AID_TABLE_SIZE) {
			sta->aid = 0;
			spin_unlock_bh(&local->ap->sta_table_lock);
			resp = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
			txt = "no room for more AIDs";
		} else {
			local->ap->sta_aid[sta->aid - 1] = sta;
			spin_unlock_bh(&local->ap->sta_table_lock);
			txt = "OK, new AID";
		}
	}

 fail:
	pos = (__le16 *) body;

	if (send_deauth) {
		*pos = cpu_to_le16(WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH);
		pos++;
	} else {
		/* FIX: CF-Pollable and CF-PollReq should be set to match the
		 * values in beacons/probe responses */
		/* FIX: how about privacy and WEP? */
		/* capability */
		*pos = cpu_to_le16(WLAN_CAPABILITY_ESS);
		pos++;

		/* status_code */
		*pos = cpu_to_le16(resp);
		pos++;

		*pos = cpu_to_le16((sta && sta->aid > 0 ? sta->aid : 0) |
				     BIT(14) | BIT(15)); /* AID */
		pos++;

		/* Supported rates (Information element) */
		p = (char *) pos;
		*p++ = WLAN_EID_SUPP_RATES;
		lpos = p;
		*p++ = 0; /* len */
		if (local->tx_rate_control & WLAN_RATE_1M) {
			*p++ = local->basic_rates & WLAN_RATE_1M ? 0x82 : 0x02;
			(*lpos)++;
		}
		if (local->tx_rate_control & WLAN_RATE_2M) {
			*p++ = local->basic_rates & WLAN_RATE_2M ? 0x84 : 0x04;
			(*lpos)++;
		}
		if (local->tx_rate_control & WLAN_RATE_5M5) {
			*p++ = local->basic_rates & WLAN_RATE_5M5 ?
				0x8b : 0x0b;
			(*lpos)++;
		}
		if (local->tx_rate_control & WLAN_RATE_11M) {
			*p++ = local->basic_rates & WLAN_RATE_11M ?
				0x96 : 0x16;
			(*lpos)++;
		}
		pos = (__le16 *) p;
	}

	prism2_send_mgmt(dev, IEEE80211_FTYPE_MGMT |
			 (send_deauth ? IEEE80211_STYPE_DEAUTH :
			  (reassoc ? IEEE80211_STYPE_REASSOC_RESP :
			   IEEE80211_STYPE_ASSOC_RESP)),
			 body, (u8 *) pos - (u8 *) body,
			 hdr->addr2,
			 send_deauth ? 0 : local->ap->tx_callback_assoc);

	if (sta) {
		if (resp == WLAN_STATUS_SUCCESS) {
			sta->last_rx = jiffies;
			/* STA will be marked associated from TX callback, if
			 * AssocResp is ACKed */
		}
		atomic_dec(&sta->users);
	}

#if 0
	PDEBUG(DEBUG_AP, "%s: %pM %sassoc (len=%d "
	       "prev_ap=%pM) => %d(%d) (%s)\n",
	       dev->name,
	       hdr->addr2,
	       reassoc ? "re" : "", len,
	       prev_ap,
	       resp, send_deauth, txt);
#endif
}


/* Called only as a scheduled task for pending AP frames. */
static void handle_deauth(local_info_t *local, struct sk_buff *skb,
			  struct hostap_80211_rx_status *rx_stats)
{
	struct net_device *dev = local->dev;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	char *body = (char *) (skb->data + IEEE80211_MGMT_HDR_LEN);
	int len;
	u16 reason_code;
	__le16 *pos;
	struct sta_info *sta = NULL;

	len = skb->len - IEEE80211_MGMT_HDR_LEN;

	if (len < 2) {
		printk("handle_deauth - too short payload (len=%d)\n", len);
		return;
	}

	pos = (__le16 *) body;
	reason_code = le16_to_cpu(*pos);

	PDEBUG(DEBUG_AP, "%s: deauthentication: %pM len=%d, "
	       "reason_code=%d\n", dev->name, hdr->addr2,
	       len, reason_code);

	spin_lock_bh(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta != NULL) {
		if ((sta->flags & WLAN_STA_ASSOC) && !sta->ap)
			hostap_event_expired_sta(local->dev, sta);
		sta->flags &= ~(WLAN_STA_AUTH | WLAN_STA_ASSOC);
	}
	spin_unlock_bh(&local->ap->sta_table_lock);
	if (sta == NULL) {
		printk("%s: deauthentication from %pM, "
	       "reason_code=%d, but STA not authenticated\n", dev->name,
		       hdr->addr2, reason_code);
	}
}


/* Called only as a scheduled task for pending AP frames. */
static void handle_disassoc(local_info_t *local, struct sk_buff *skb,
			    struct hostap_80211_rx_status *rx_stats)
{
	struct net_device *dev = local->dev;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	char *body = skb->data + IEEE80211_MGMT_HDR_LEN;
	int len;
	u16 reason_code;
	__le16 *pos;
	struct sta_info *sta = NULL;

	len = skb->len - IEEE80211_MGMT_HDR_LEN;

	if (len < 2) {
		printk("handle_disassoc - too short payload (len=%d)\n", len);
		return;
	}

	pos = (__le16 *) body;
	reason_code = le16_to_cpu(*pos);

	PDEBUG(DEBUG_AP, "%s: disassociation: %pM len=%d, "
	       "reason_code=%d\n", dev->name, hdr->addr2,
	       len, reason_code);

	spin_lock_bh(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta != NULL) {
		if ((sta->flags & WLAN_STA_ASSOC) && !sta->ap)
			hostap_event_expired_sta(local->dev, sta);
		sta->flags &= ~WLAN_STA_ASSOC;
	}
	spin_unlock_bh(&local->ap->sta_table_lock);
	if (sta == NULL) {
		printk("%s: disassociation from %pM, "
		       "reason_code=%d, but STA not authenticated\n",
		       dev->name, hdr->addr2, reason_code);
	}
}


/* Called only as a scheduled task for pending AP frames. */
static void ap_handle_data_nullfunc(local_info_t *local,
				    struct ieee80211_hdr *hdr)
{
	struct net_device *dev = local->dev;

	/* some STA f/w's seem to require control::ACK frame for
	 * data::nullfunc, but at least Prism2 station f/w version 0.8.0 does
	 * not send this..
	 * send control::ACK for the data::nullfunc */

	printk(KERN_DEBUG "Sending control::ACK for data::nullfunc\n");
	prism2_send_mgmt(dev, IEEE80211_FTYPE_CTL | IEEE80211_STYPE_ACK,
			 NULL, 0, hdr->addr2, 0);
}


/* Called only as a scheduled task for pending AP frames. */
static void ap_handle_dropped_data(local_info_t *local,
				   struct ieee80211_hdr *hdr)
{
	struct net_device *dev = local->dev;
	struct sta_info *sta;
	__le16 reason;

	spin_lock_bh(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock_bh(&local->ap->sta_table_lock);

	if (sta != NULL && (sta->flags & WLAN_STA_ASSOC)) {
		PDEBUG(DEBUG_AP, "ap_handle_dropped_data: STA is now okay?\n");
		atomic_dec(&sta->users);
		return;
	}

	reason = cpu_to_le16(WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
	prism2_send_mgmt(dev, IEEE80211_FTYPE_MGMT |
			 ((sta == NULL || !(sta->flags & WLAN_STA_ASSOC)) ?
			  IEEE80211_STYPE_DEAUTH : IEEE80211_STYPE_DISASSOC),
			 (char *) &reason, sizeof(reason), hdr->addr2, 0);

	if (sta)
		atomic_dec(&sta->users);
}

#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


/* Called only as a scheduled task for pending AP frames. */
static void pspoll_send_buffered(local_info_t *local, struct sta_info *sta,
				 struct sk_buff *skb)
{
	struct hostap_skb_tx_data *meta;

	if (!(sta->flags & WLAN_STA_PS)) {
		/* Station has moved to non-PS mode, so send all buffered
		 * frames using normal device queue. */
		dev_queue_xmit(skb);
		return;
	}

	/* add a flag for hostap_handle_sta_tx() to know that this skb should
	 * be passed through even though STA is using PS */
	meta = (struct hostap_skb_tx_data *) skb->cb;
	meta->flags |= HOSTAP_TX_FLAGS_BUFFERED_FRAME;
	if (!skb_queue_empty(&sta->tx_buf)) {
		/* indicate to STA that more frames follow */
		meta->flags |= HOSTAP_TX_FLAGS_ADD_MOREDATA;
	}
	dev_queue_xmit(skb);
}


/* Called only as a scheduled task for pending AP frames. */
static void handle_pspoll(local_info_t *local,
			  struct ieee80211_hdr *hdr,
			  struct hostap_80211_rx_status *rx_stats)
{
	struct net_device *dev = local->dev;
	struct sta_info *sta;
	u16 aid;
	struct sk_buff *skb;

	PDEBUG(DEBUG_PS2, "handle_pspoll: BSSID=%pM, TA=%pM PWRMGT=%d\n",
	       hdr->addr1, hdr->addr2, !!ieee80211_has_pm(hdr->frame_control));

	if (!ether_addr_equal(hdr->addr1, dev->dev_addr)) {
		PDEBUG(DEBUG_AP,
		       "handle_pspoll - addr1(BSSID)=%pM not own MAC\n",
		       hdr->addr1);
		return;
	}

	aid = le16_to_cpu(hdr->duration_id);
	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14))) {
		PDEBUG(DEBUG_PS, "   PSPOLL and AID[15:14] not set\n");
		return;
	}
	aid &= ~(BIT(15) | BIT(14));
	if (aid == 0 || aid > MAX_AID_TABLE_SIZE) {
		PDEBUG(DEBUG_PS, "   invalid aid=%d\n", aid);
		return;
	}
	PDEBUG(DEBUG_PS2, "   aid=%d\n", aid);

	spin_lock_bh(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock_bh(&local->ap->sta_table_lock);

	if (sta == NULL) {
		PDEBUG(DEBUG_PS, "   STA not found\n");
		return;
	}
	if (sta->aid != aid) {
		PDEBUG(DEBUG_PS, "   received aid=%i does not match with "
		       "assoc.aid=%d\n", aid, sta->aid);
		return;
	}

	/* FIX: todo:
	 * - add timeout for buffering (clear aid in TIM vector if buffer timed
	 *   out (expiry time must be longer than ListenInterval for
	 *   the corresponding STA; "8802-11: 11.2.1.9 AP aging function"
	 * - what to do, if buffered, pspolled, and sent frame is not ACKed by
	 *   sta; store buffer for later use and leave TIM aid bit set? use
	 *   TX event to check whether frame was ACKed?
	 */

	while ((skb = skb_dequeue(&sta->tx_buf)) != NULL) {
		/* send buffered frame .. */
		PDEBUG(DEBUG_PS2, "Sending buffered frame to STA after PS POLL"
		       " (buffer_count=%d)\n", skb_queue_len(&sta->tx_buf));

		pspoll_send_buffered(local, sta, skb);

		if (sta->flags & WLAN_STA_PS) {
			/* send only one buffered packet per PS Poll */
			/* FIX: should ignore further PS Polls until the
			 * buffered packet that was just sent is acknowledged
			 * (Tx or TxExc event) */
			break;
		}
	}

	if (skb_queue_empty(&sta->tx_buf)) {
		/* try to clear aid from TIM */
		if (!(sta->flags & WLAN_STA_TIM))
			PDEBUG(DEBUG_PS2,  "Re-unsetting TIM for aid %d\n",
			       aid);
		hostap_set_tim(local, aid, 0);
		sta->flags &= ~WLAN_STA_TIM;
	}

	atomic_dec(&sta->users);
}


#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT

static void handle_wds_oper_queue(struct work_struct *work)
{
	struct ap_data *ap = container_of(work, struct ap_data,
					  wds_oper_queue);
	local_info_t *local = ap->local;
	struct wds_oper_data *entry, *prev;

	spin_lock_bh(&local->lock);
	entry = local->ap->wds_oper_entries;
	local->ap->wds_oper_entries = NULL;
	spin_unlock_bh(&local->lock);

	while (entry) {
		PDEBUG(DEBUG_AP, "%s: %s automatic WDS connection "
		       "to AP %pM\n",
		       local->dev->name,
		       entry->type == WDS_ADD ? "adding" : "removing",
		       entry->addr);
		if (entry->type == WDS_ADD)
			prism2_wds_add(local, entry->addr, 0);
		else if (entry->type == WDS_DEL)
			prism2_wds_del(local, entry->addr, 0, 1);

		prev = entry;
		entry = entry->next;
		kfree(prev);
	}
}


/* Called only as a scheduled task for pending AP frames. */
static void handle_beacon(local_info_t *local, struct sk_buff *skb,
			  struct hostap_80211_rx_status *rx_stats)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	char *body = skb->data + IEEE80211_MGMT_HDR_LEN;
	int len, left;
	u16 beacon_int, capability;
	__le16 *pos;
	char *ssid = NULL;
	unsigned char *supp_rates = NULL;
	int ssid_len = 0, supp_rates_len = 0;
	struct sta_info *sta = NULL;
	int new_sta = 0, channel = -1;

	len = skb->len - IEEE80211_MGMT_HDR_LEN;

	if (len < 8 + 2 + 2) {
		printk(KERN_DEBUG "handle_beacon - too short payload "
		       "(len=%d)\n", len);
		return;
	}

	pos = (__le16 *) body;
	left = len;

	/* Timestamp (8 octets) */
	pos += 4; left -= 8;
	/* Beacon interval (2 octets) */
	beacon_int = le16_to_cpu(*pos);
	pos++; left -= 2;
	/* Capability information (2 octets) */
	capability = le16_to_cpu(*pos);
	pos++; left -= 2;

	if (local->ap->ap_policy != AP_OTHER_AP_EVEN_IBSS &&
	    capability & WLAN_CAPABILITY_IBSS)
		return;

	if (left >= 2) {
		unsigned int ileft;
		unsigned char *u = (unsigned char *) pos;

		if (*u == WLAN_EID_SSID) {
			u++; left--;
			ileft = *u;
			u++; left--;

			if (ileft > left || ileft > MAX_SSID_LEN) {
				PDEBUG(DEBUG_AP, "SSID: overflow\n");
				return;
			}

			if (local->ap->ap_policy == AP_OTHER_AP_SAME_SSID &&
			    (ileft != strlen(local->essid) ||
			     memcmp(local->essid, u, ileft) != 0)) {
				/* not our SSID */
				return;
			}

			ssid = u;
			ssid_len = ileft;

			u += ileft;
			left -= ileft;
		}

		if (*u == WLAN_EID_SUPP_RATES) {
			u++; left--;
			ileft = *u;
			u++; left--;

			if (ileft > left || ileft == 0 || ileft > 8) {
				PDEBUG(DEBUG_AP, " - SUPP_RATES len error\n");
				return;
			}

			supp_rates = u;
			supp_rates_len = ileft;

			u += ileft;
			left -= ileft;
		}

		if (*u == WLAN_EID_DS_PARAMS) {
			u++; left--;
			ileft = *u;
			u++; left--;

			if (ileft > left || ileft != 1) {
				PDEBUG(DEBUG_AP, " - DS_PARAMS len error\n");
				return;
			}

			channel = *u;

			u += ileft;
			left -= ileft;
		}
	}

	spin_lock_bh(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta != NULL)
		atomic_inc(&sta->users);
	spin_unlock_bh(&local->ap->sta_table_lock);

	if (sta == NULL) {
		/* add new AP */
		new_sta = 1;
		sta = ap_add_sta(local->ap, hdr->addr2);
		if (sta == NULL) {
			printk(KERN_INFO "prism2: kmalloc failed for AP "
			       "data structure\n");
			return;
		}
		hostap_event_new_sta(local->dev, sta);

		/* mark APs authentication and associated for pseudo ad-hoc
		 * style communication */
		sta->flags = WLAN_STA_AUTH | WLAN_STA_ASSOC;

		if (local->ap->autom_ap_wds) {
			hostap_wds_link_oper(local, sta->addr, WDS_ADD);
		}
	}

	sta->ap = 1;
	if (ssid) {
		sta->u.ap.ssid_len = ssid_len;
		memcpy(sta->u.ap.ssid, ssid, ssid_len);
		sta->u.ap.ssid[ssid_len] = '\0';
	} else {
		sta->u.ap.ssid_len = 0;
		sta->u.ap.ssid[0] = '\0';
	}
	sta->u.ap.channel = channel;
	sta->rx_packets++;
	sta->rx_bytes += len;
	sta->u.ap.last_beacon = sta->last_rx = jiffies;
	sta->capability = capability;
	sta->listen_interval = beacon_int;

	atomic_dec(&sta->users);

	if (new_sta) {
		memset(sta->supported_rates, 0, sizeof(sta->supported_rates));
		memcpy(sta->supported_rates, supp_rates, supp_rates_len);
		prism2_check_tx_rates(sta);
	}
}

#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */


/* Called only as a tasklet. */
static void handle_ap_item(local_info_t *local, struct sk_buff *skb,
			   struct hostap_80211_rx_status *rx_stats)
{
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	struct net_device *dev = local->dev;
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */
	u16 fc, type, stype;
	struct ieee80211_hdr *hdr;

	/* FIX: should give skb->len to handler functions and check that the
	 * buffer is long enough */
	hdr = (struct ieee80211_hdr *) skb->data;
	fc = le16_to_cpu(hdr->frame_control);
	type = fc & IEEE80211_FCTL_FTYPE;
	stype = fc & IEEE80211_FCTL_STYPE;

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	if (!local->hostapd && type == IEEE80211_FTYPE_DATA) {
		PDEBUG(DEBUG_AP, "handle_ap_item - data frame\n");

		if (!(fc & IEEE80211_FCTL_TODS) ||
		    (fc & IEEE80211_FCTL_FROMDS)) {
			if (stype == IEEE80211_STYPE_NULLFUNC) {
				/* no ToDS nullfunc seems to be used to check
				 * AP association; so send reject message to
				 * speed up re-association */
				ap_handle_dropped_data(local, hdr);
				goto done;
			}
			PDEBUG(DEBUG_AP, "   not ToDS frame (fc=0x%04x)\n",
			       fc);
			goto done;
		}

		if (!ether_addr_equal(hdr->addr1, dev->dev_addr)) {
			PDEBUG(DEBUG_AP, "handle_ap_item - addr1(BSSID)=%pM"
			       " not own MAC\n", hdr->addr1);
			goto done;
		}

		if (local->ap->nullfunc_ack &&
		    stype == IEEE80211_STYPE_NULLFUNC)
			ap_handle_data_nullfunc(local, hdr);
		else
			ap_handle_dropped_data(local, hdr);
		goto done;
	}

	if (type == IEEE80211_FTYPE_MGMT && stype == IEEE80211_STYPE_BEACON) {
		handle_beacon(local, skb, rx_stats);
		goto done;
	}
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

	if (type == IEEE80211_FTYPE_CTL && stype == IEEE80211_STYPE_PSPOLL) {
		handle_pspoll(local, hdr, rx_stats);
		goto done;
	}

	if (local->hostapd) {
		PDEBUG(DEBUG_AP, "Unknown frame in AP queue: type=0x%02x "
		       "subtype=0x%02x\n", type, stype);
		goto done;
	}

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
	if (type != IEEE80211_FTYPE_MGMT) {
		PDEBUG(DEBUG_AP, "handle_ap_item - not a management frame?\n");
		goto done;
	}

	if (!ether_addr_equal(hdr->addr1, dev->dev_addr)) {
		PDEBUG(DEBUG_AP, "handle_ap_item - addr1(DA)=%pM"
		       " not own MAC\n", hdr->addr1);
		goto done;
	}

	if (!ether_addr_equal(hdr->addr3, dev->dev_addr)) {
		PDEBUG(DEBUG_AP, "handle_ap_item - addr3(BSSID)=%pM"
		       " not own MAC\n", hdr->addr3);
		goto done;
	}

	switch (stype) {
	case IEEE80211_STYPE_ASSOC_REQ:
		handle_assoc(local, skb, rx_stats, 0);
		break;
	case IEEE80211_STYPE_ASSOC_RESP:
		PDEBUG(DEBUG_AP, "==> ASSOC RESP (ignored)\n");
		break;
	case IEEE80211_STYPE_REASSOC_REQ:
		handle_assoc(local, skb, rx_stats, 1);
		break;
	case IEEE80211_STYPE_REASSOC_RESP:
		PDEBUG(DEBUG_AP, "==> REASSOC RESP (ignored)\n");
		break;
	case IEEE80211_STYPE_ATIM:
		PDEBUG(DEBUG_AP, "==> ATIM (ignored)\n");
		break;
	case IEEE80211_STYPE_DISASSOC:
		handle_disassoc(local, skb, rx_stats);
		break;
	case IEEE80211_STYPE_AUTH:
		handle_authen(local, skb, rx_stats);
		break;
	case IEEE80211_STYPE_DEAUTH:
		handle_deauth(local, skb, rx_stats);
		break;
	default:
		PDEBUG(DEBUG_AP, "Unknown mgmt frame subtype 0x%02x\n",
		       stype >> 4);
		break;
	}
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

 done:
	dev_kfree_skb(skb);
}


/* Called only as a tasklet (software IRQ) */
void hostap_rx(struct net_device *dev, struct sk_buff *skb,
	       struct hostap_80211_rx_status *rx_stats)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct ieee80211_hdr *hdr;

	iface = netdev_priv(dev);
	local = iface->local;

	if (skb->len < 16)
		goto drop;

	dev->stats.rx_packets++;

	hdr = (struct ieee80211_hdr *) skb->data;

	if (local->ap->ap_policy == AP_OTHER_AP_SKIP_ALL &&
	    ieee80211_is_beacon(hdr->frame_control))
		goto drop;

	skb->protocol = cpu_to_be16(ETH_P_HOSTAP);
	handle_ap_item(local, skb, rx_stats);
	return;

 drop:
	dev_kfree_skb(skb);
}


/* Called only as a tasklet (software IRQ) */
static void schedule_packet_send(local_info_t *local, struct sta_info *sta)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	struct hostap_80211_rx_status rx_stats;

	if (skb_queue_empty(&sta->tx_buf))
		return;

	skb = dev_alloc_skb(16);
	if (skb == NULL) {
		printk(KERN_DEBUG "%s: schedule_packet_send: skb alloc "
		       "failed\n", local->dev->name);
		return;
	}

	hdr = (struct ieee80211_hdr *) skb_put(skb, 16);

	/* Generate a fake pspoll frame to start packet delivery */
	hdr->frame_control = cpu_to_le16(
		IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL);
	memcpy(hdr->addr1, local->dev->dev_addr, ETH_ALEN);
	memcpy(hdr->addr2, sta->addr, ETH_ALEN);
	hdr->duration_id = cpu_to_le16(sta->aid | BIT(15) | BIT(14));

	PDEBUG(DEBUG_PS2,
	       "%s: Scheduling buffered packet delivery for STA %pM\n",
	       local->dev->name, sta->addr);

	skb->dev = local->dev;

	memset(&rx_stats, 0, sizeof(rx_stats));
	hostap_rx(local->dev, skb, &rx_stats);
}


int prism2_ap_get_sta_qual(local_info_t *local, struct sockaddr addr[],
			   struct iw_quality qual[], int buf_size,
			   int aplist)
{
	struct ap_data *ap = local->ap;
	struct list_head *ptr;
	int count = 0;

	spin_lock_bh(&ap->sta_table_lock);

	for (ptr = ap->sta_list.next; ptr != NULL && ptr != &ap->sta_list;
	     ptr = ptr->next) {
		struct sta_info *sta = (struct sta_info *) ptr;

		if (aplist && !sta->ap)
			continue;
		addr[count].sa_family = ARPHRD_ETHER;
		memcpy(addr[count].sa_data, sta->addr, ETH_ALEN);
		if (sta->last_rx_silence == 0)
			qual[count].qual = sta->last_rx_signal < 27 ?
				0 : (sta->last_rx_signal - 27) * 92 / 127;
		else
			qual[count].qual = sta->last_rx_signal -
				sta->last_rx_silence - 35;
		qual[count].level = HFA384X_LEVEL_TO_dBm(sta->last_rx_signal);
		qual[count].noise = HFA384X_LEVEL_TO_dBm(sta->last_rx_silence);
		qual[count].updated = sta->last_rx_updated;

		sta->last_rx_updated = IW_QUAL_DBM;

		count++;
		if (count >= buf_size)
			break;
	}
	spin_unlock_bh(&ap->sta_table_lock);

	return count;
}


/* Translate our list of Access Points & Stations to a card independent
 * format that the Wireless Tools will understand - Jean II */
int prism2_ap_translate_scan(struct net_device *dev,
			     struct iw_request_info *info, char *buffer)
{
	struct hostap_interface *iface;
	local_info_t *local;
	struct ap_data *ap;
	struct list_head *ptr;
	struct iw_event iwe;
	char *current_ev = buffer;
	char *end_buf = buffer + IW_SCAN_MAX_DATA;
#if !defined(PRISM2_NO_KERNEL_IEEE80211_MGMT)
	char buf[64];
#endif

	iface = netdev_priv(dev);
	local = iface->local;
	ap = local->ap;

	spin_lock_bh(&ap->sta_table_lock);

	for (ptr = ap->sta_list.next; ptr != NULL && ptr != &ap->sta_list;
	     ptr = ptr->next) {
		struct sta_info *sta = (struct sta_info *) ptr;

		/* First entry *MUST* be the AP MAC address */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data, sta->addr, ETH_ALEN);
		iwe.len = IW_EV_ADDR_LEN;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_ADDR_LEN);

		/* Use the mode to indicate if it's a station or
		 * an Access Point */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWMODE;
		if (sta->ap)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_INFRA;
		iwe.len = IW_EV_UINT_LEN;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_UINT_LEN);

		/* Some quality */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVQUAL;
		if (sta->last_rx_silence == 0)
			iwe.u.qual.qual = sta->last_rx_signal < 27 ?
				0 : (sta->last_rx_signal - 27) * 92 / 127;
		else
			iwe.u.qual.qual = sta->last_rx_signal -
				sta->last_rx_silence - 35;
		iwe.u.qual.level = HFA384X_LEVEL_TO_dBm(sta->last_rx_signal);
		iwe.u.qual.noise = HFA384X_LEVEL_TO_dBm(sta->last_rx_silence);
		iwe.u.qual.updated = sta->last_rx_updated;
		iwe.len = IW_EV_QUAL_LEN;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_QUAL_LEN);

#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
		if (sta->ap) {
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.length = sta->u.ap.ssid_len;
			iwe.u.data.flags = 1;
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf, &iwe,
							  sta->u.ap.ssid);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWENCODE;
			if (sta->capability & WLAN_CAPABILITY_PRIVACY)
				iwe.u.data.flags =
					IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
			else
				iwe.u.data.flags = IW_ENCODE_DISABLED;
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf, &iwe,
							  sta->u.ap.ssid);

			if (sta->u.ap.channel > 0 &&
			    sta->u.ap.channel <= FREQ_COUNT) {
				memset(&iwe, 0, sizeof(iwe));
				iwe.cmd = SIOCGIWFREQ;
				iwe.u.freq.m = freq_list[sta->u.ap.channel - 1]
					* 100000;
				iwe.u.freq.e = 1;
				current_ev = iwe_stream_add_event(
					info, current_ev, end_buf, &iwe,
					IW_EV_FREQ_LEN);
			}

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			sprintf(buf, "beacon_interval=%d",
				sta->listen_interval);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf, &iwe, buf);
		}
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */

		sta->last_rx_updated = IW_QUAL_DBM;

		/* To be continued, we should make good use of IWEVCUSTOM */
	}

	spin_unlock_bh(&ap->sta_table_lock);

	return current_ev - buffer;
}


static int prism2_hostapd_add_sta(struct ap_data *ap,
				  struct prism2_hostapd_param *param)
{
	struct sta_info *sta;

	spin_lock_bh(&ap->sta_table_lock);
	sta = ap_get_sta(ap, param->sta_addr);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock_bh(&ap->sta_table_lock);

	if (sta == NULL) {
		sta = ap_add_sta(ap, param->sta_addr);
		if (sta == NULL)
			return -1;
	}

	if (!(sta->flags & WLAN_STA_ASSOC) && !sta->ap && sta->local)
		hostap_event_new_sta(sta->local->dev, sta);

	sta->flags |= WLAN_STA_AUTH | WLAN_STA_ASSOC;
	sta->last_rx = jiffies;
	sta->aid = param->u.add_sta.aid;
	sta->capability = param->u.add_sta.capability;
	sta->tx_supp_rates = param->u.add_sta.tx_supp_rates;
	if (sta->tx_supp_rates & WLAN_RATE_1M)
		sta->supported_rates[0] = 2;
	if (sta->tx_supp_rates & WLAN_RATE_2M)
		sta->supported_rates[1] = 4;
 	if (sta->tx_supp_rates & WLAN_RATE_5M5)
		sta->supported_rates[2] = 11;
	if (sta->tx_supp_rates & WLAN_RATE_11M)
		sta->supported_rates[3] = 22;
	prism2_check_tx_rates(sta);
	atomic_dec(&sta->users);
	return 0;
}


static int prism2_hostapd_remove_sta(struct ap_data *ap,
				     struct prism2_hostapd_param *param)
{
	struct sta_info *sta;

	spin_lock_bh(&ap->sta_table_lock);
	sta = ap_get_sta(ap, param->sta_addr);
	if (sta) {
		ap_sta_hash_del(ap, sta);
		list_del(&sta->list);
	}
	spin_unlock_bh(&ap->sta_table_lock);

	if (!sta)
		return -ENOENT;

	if ((sta->flags & WLAN_STA_ASSOC) && !sta->ap && sta->local)
		hostap_event_expired_sta(sta->local->dev, sta);
	ap_free_sta(ap, sta);

	return 0;
}


static int prism2_hostapd_get_info_sta(struct ap_data *ap,
				       struct prism2_hostapd_param *param)
{
	struct sta_info *sta;

	spin_lock_bh(&ap->sta_table_lock);
	sta = ap_get_sta(ap, param->sta_addr);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock_bh(&ap->sta_table_lock);

	if (!sta)
		return -ENOENT;

	param->u.get_info_sta.inactive_sec = (jiffies - sta->last_rx) / HZ;

	atomic_dec(&sta->users);

	return 1;
}


static int prism2_hostapd_set_flags_sta(struct ap_data *ap,
					struct prism2_hostapd_param *param)
{
	struct sta_info *sta;

	spin_lock_bh(&ap->sta_table_lock);
	sta = ap_get_sta(ap, param->sta_addr);
	if (sta) {
		sta->flags |= param->u.set_flags_sta.flags_or;
		sta->flags &= param->u.set_flags_sta.flags_and;
	}
	spin_unlock_bh(&ap->sta_table_lock);

	if (!sta)
		return -ENOENT;

	return 0;
}


static int prism2_hostapd_sta_clear_stats(struct ap_data *ap,
					  struct prism2_hostapd_param *param)
{
	struct sta_info *sta;
	int rate;

	spin_lock_bh(&ap->sta_table_lock);
	sta = ap_get_sta(ap, param->sta_addr);
	if (sta) {
		sta->rx_packets = sta->tx_packets = 0;
		sta->rx_bytes = sta->tx_bytes = 0;
		for (rate = 0; rate < WLAN_RATE_COUNT; rate++) {
			sta->tx_count[rate] = 0;
			sta->rx_count[rate] = 0;
		}
	}
	spin_unlock_bh(&ap->sta_table_lock);

	if (!sta)
		return -ENOENT;

	return 0;
}


int prism2_hostapd(struct ap_data *ap, struct prism2_hostapd_param *param)
{
	switch (param->cmd) {
	case PRISM2_HOSTAPD_FLUSH:
		ap_control_kickall(ap);
		return 0;
	case PRISM2_HOSTAPD_ADD_STA:
		return prism2_hostapd_add_sta(ap, param);
	case PRISM2_HOSTAPD_REMOVE_STA:
		return prism2_hostapd_remove_sta(ap, param);
	case PRISM2_HOSTAPD_GET_INFO_STA:
		return prism2_hostapd_get_info_sta(ap, param);
	case PRISM2_HOSTAPD_SET_FLAGS_STA:
		return prism2_hostapd_set_flags_sta(ap, param);
	case PRISM2_HOSTAPD_STA_CLEAR_STATS:
		return prism2_hostapd_sta_clear_stats(ap, param);
	default:
		printk(KERN_WARNING "prism2_hostapd: unknown cmd=%d\n",
		       param->cmd);
		return -EOPNOTSUPP;
	}
}


/* Update station info for host-based TX rate control and return current
 * TX rate */
static int ap_update_sta_tx_rate(struct sta_info *sta, struct net_device *dev)
{
	int ret = sta->tx_rate;
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	sta->tx_count[sta->tx_rate_idx]++;
	sta->tx_since_last_failure++;
	sta->tx_consecutive_exc = 0;
	if (sta->tx_since_last_failure >= WLAN_RATE_UPDATE_COUNT &&
	    sta->tx_rate_idx < sta->tx_max_rate) {
		/* use next higher rate */
		int old_rate, new_rate;
		old_rate = new_rate = sta->tx_rate_idx;
		while (new_rate < sta->tx_max_rate) {
			new_rate++;
			if (ap_tx_rate_ok(new_rate, sta, local)) {
				sta->tx_rate_idx = new_rate;
				break;
			}
		}
		if (old_rate != sta->tx_rate_idx) {
			switch (sta->tx_rate_idx) {
			case 0: sta->tx_rate = 10; break;
			case 1: sta->tx_rate = 20; break;
			case 2: sta->tx_rate = 55; break;
			case 3: sta->tx_rate = 110; break;
			default: sta->tx_rate = 0; break;
			}
			PDEBUG(DEBUG_AP, "%s: STA %pM TX rate raised to %d\n",
			       dev->name, sta->addr, sta->tx_rate);
		}
		sta->tx_since_last_failure = 0;
	}

	return ret;
}


/* Called only from software IRQ. Called for each TX frame prior possible
 * encryption and transmit. */
ap_tx_ret hostap_handle_sta_tx(local_info_t *local, struct hostap_tx_data *tx)
{
	struct sta_info *sta = NULL;
	struct sk_buff *skb = tx->skb;
	int set_tim, ret;
	struct ieee80211_hdr *hdr;
	struct hostap_skb_tx_data *meta;

	meta = (struct hostap_skb_tx_data *) skb->cb;
	ret = AP_TX_CONTINUE;
	if (local->ap == NULL || skb->len < 10 ||
	    meta->iface->type == HOSTAP_INTERFACE_STA)
		goto out;

	hdr = (struct ieee80211_hdr *) skb->data;

	if (hdr->addr1[0] & 0x01) {
		/* broadcast/multicast frame - no AP related processing */
		if (local->ap->num_sta <= 0)
			ret = AP_TX_DROP;
		goto out;
	}

	/* unicast packet - check whether destination STA is associated */
	spin_lock(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr1);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock(&local->ap->sta_table_lock);

	if (local->iw_mode == IW_MODE_MASTER && sta == NULL &&
	    !(meta->flags & HOSTAP_TX_FLAGS_WDS) &&
	    meta->iface->type != HOSTAP_INTERFACE_MASTER &&
	    meta->iface->type != HOSTAP_INTERFACE_AP) {
#if 0
		/* This can happen, e.g., when wlan0 is added to a bridge and
		 * bridging code does not know which port is the correct target
		 * for a unicast frame. In this case, the packet is send to all
		 * ports of the bridge. Since this is a valid scenario, do not
		 * print out any errors here. */
		if (net_ratelimit()) {
			printk(KERN_DEBUG "AP: drop packet to non-associated "
			       "STA %pM\n", hdr->addr1);
		}
#endif
		local->ap->tx_drop_nonassoc++;
		ret = AP_TX_DROP;
		goto out;
	}

	if (sta == NULL)
		goto out;

	if (!(sta->flags & WLAN_STA_AUTHORIZED))
		ret = AP_TX_CONTINUE_NOT_AUTHORIZED;

	/* Set tx_rate if using host-based TX rate control */
	if (!local->fw_tx_rate_control)
		local->ap->last_tx_rate = meta->rate =
			ap_update_sta_tx_rate(sta, local->dev);

	if (local->iw_mode != IW_MODE_MASTER)
		goto out;

	if (!(sta->flags & WLAN_STA_PS))
		goto out;

	if (meta->flags & HOSTAP_TX_FLAGS_ADD_MOREDATA) {
		/* indicate to STA that more frames follow */
		hdr->frame_control |=
			cpu_to_le16(IEEE80211_FCTL_MOREDATA);
	}

	if (meta->flags & HOSTAP_TX_FLAGS_BUFFERED_FRAME) {
		/* packet was already buffered and now send due to
		 * PS poll, so do not rebuffer it */
		goto out;
	}

	if (skb_queue_len(&sta->tx_buf) >= STA_MAX_TX_BUFFER) {
		PDEBUG(DEBUG_PS, "%s: No more space in STA (%pM)'s"
		       "PS mode buffer\n",
		       local->dev->name, sta->addr);
		/* Make sure that TIM is set for the station (it might not be
		 * after AP wlan hw reset). */
		/* FIX: should fix hw reset to restore bits based on STA
		 * buffer state.. */
		hostap_set_tim(local, sta->aid, 1);
		sta->flags |= WLAN_STA_TIM;
		ret = AP_TX_DROP;
		goto out;
	}

	/* STA in PS mode, buffer frame for later delivery */
	set_tim = skb_queue_empty(&sta->tx_buf);
	skb_queue_tail(&sta->tx_buf, skb);
	/* FIX: could save RX time to skb and expire buffered frames after
	 * some time if STA does not poll for them */

	if (set_tim) {
		if (sta->flags & WLAN_STA_TIM)
			PDEBUG(DEBUG_PS2, "Re-setting TIM for aid %d\n",
			       sta->aid);
		hostap_set_tim(local, sta->aid, 1);
		sta->flags |= WLAN_STA_TIM;
	}

	ret = AP_TX_BUFFERED;

 out:
	if (sta != NULL) {
		if (ret == AP_TX_CONTINUE ||
		    ret == AP_TX_CONTINUE_NOT_AUTHORIZED) {
			sta->tx_packets++;
			sta->tx_bytes += skb->len;
			sta->last_tx = jiffies;
		}

		if ((ret == AP_TX_CONTINUE ||
		     ret == AP_TX_CONTINUE_NOT_AUTHORIZED) &&
		    sta->crypt && tx->host_encrypt) {
			tx->crypt = sta->crypt;
			tx->sta_ptr = sta; /* hostap_handle_sta_release() will
					    * be called to release sta info
					    * later */
		} else
			atomic_dec(&sta->users);
	}

	return ret;
}


void hostap_handle_sta_release(void *ptr)
{
	struct sta_info *sta = ptr;
	atomic_dec(&sta->users);
}


/* Called only as a tasklet (software IRQ) */
void hostap_handle_sta_tx_exc(local_info_t *local, struct sk_buff *skb)
{
	struct sta_info *sta;
	struct ieee80211_hdr *hdr;
	struct hostap_skb_tx_data *meta;

	hdr = (struct ieee80211_hdr *) skb->data;
	meta = (struct hostap_skb_tx_data *) skb->cb;

	spin_lock(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr1);
	if (!sta) {
		spin_unlock(&local->ap->sta_table_lock);
		PDEBUG(DEBUG_AP, "%s: Could not find STA %pM"
		       " for this TX error (@%lu)\n",
		       local->dev->name, hdr->addr1, jiffies);
		return;
	}

	sta->tx_since_last_failure = 0;
	sta->tx_consecutive_exc++;

	if (sta->tx_consecutive_exc >= WLAN_RATE_DECREASE_THRESHOLD &&
	    sta->tx_rate_idx > 0 && meta->rate <= sta->tx_rate) {
		/* use next lower rate */
		int old, rate;
		old = rate = sta->tx_rate_idx;
		while (rate > 0) {
			rate--;
			if (ap_tx_rate_ok(rate, sta, local)) {
				sta->tx_rate_idx = rate;
				break;
			}
		}
		if (old != sta->tx_rate_idx) {
			switch (sta->tx_rate_idx) {
			case 0: sta->tx_rate = 10; break;
			case 1: sta->tx_rate = 20; break;
			case 2: sta->tx_rate = 55; break;
			case 3: sta->tx_rate = 110; break;
			default: sta->tx_rate = 0; break;
			}
			PDEBUG(DEBUG_AP,
			       "%s: STA %pM TX rate lowered to %d\n",
			       local->dev->name, sta->addr, sta->tx_rate);
		}
		sta->tx_consecutive_exc = 0;
	}
	spin_unlock(&local->ap->sta_table_lock);
}


static void hostap_update_sta_ps2(local_info_t *local, struct sta_info *sta,
				  int pwrmgt, int type, int stype)
{
	if (pwrmgt && !(sta->flags & WLAN_STA_PS)) {
		sta->flags |= WLAN_STA_PS;
		PDEBUG(DEBUG_PS2, "STA %pM changed to use PS "
		       "mode (type=0x%02X, stype=0x%02X)\n",
		       sta->addr, type >> 2, stype >> 4);
	} else if (!pwrmgt && (sta->flags & WLAN_STA_PS)) {
		sta->flags &= ~WLAN_STA_PS;
		PDEBUG(DEBUG_PS2, "STA %pM changed to not use "
		       "PS mode (type=0x%02X, stype=0x%02X)\n",
		       sta->addr, type >> 2, stype >> 4);
		if (type != IEEE80211_FTYPE_CTL ||
		    stype != IEEE80211_STYPE_PSPOLL)
			schedule_packet_send(local, sta);
	}
}


/* Called only as a tasklet (software IRQ). Called for each RX frame to update
 * STA power saving state. pwrmgt is a flag from 802.11 frame_control field. */
int hostap_update_sta_ps(local_info_t *local, struct ieee80211_hdr *hdr)
{
	struct sta_info *sta;
	u16 fc;

	spin_lock(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock(&local->ap->sta_table_lock);

	if (!sta)
		return -1;

	fc = le16_to_cpu(hdr->frame_control);
	hostap_update_sta_ps2(local, sta, fc & IEEE80211_FCTL_PM,
			      fc & IEEE80211_FCTL_FTYPE,
			      fc & IEEE80211_FCTL_STYPE);

	atomic_dec(&sta->users);
	return 0;
}


/* Called only as a tasklet (software IRQ). Called for each RX frame after
 * getting RX header and payload from hardware. */
ap_rx_ret hostap_handle_sta_rx(local_info_t *local, struct net_device *dev,
			       struct sk_buff *skb,
			       struct hostap_80211_rx_status *rx_stats,
			       int wds)
{
	int ret;
	struct sta_info *sta;
	u16 fc, type, stype;
	struct ieee80211_hdr *hdr;

	if (local->ap == NULL)
		return AP_RX_CONTINUE;

	hdr = (struct ieee80211_hdr *) skb->data;

	fc = le16_to_cpu(hdr->frame_control);
	type = fc & IEEE80211_FCTL_FTYPE;
	stype = fc & IEEE80211_FCTL_STYPE;

	spin_lock(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock(&local->ap->sta_table_lock);

	if (sta && !(sta->flags & WLAN_STA_AUTHORIZED))
		ret = AP_RX_CONTINUE_NOT_AUTHORIZED;
	else
		ret = AP_RX_CONTINUE;


	if (fc & IEEE80211_FCTL_TODS) {
		if (!wds && (sta == NULL || !(sta->flags & WLAN_STA_ASSOC))) {
			if (local->hostapd) {
				prism2_rx_80211(local->apdev, skb, rx_stats,
						PRISM2_RX_NON_ASSOC);
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
			} else {
				printk(KERN_DEBUG "%s: dropped received packet"
				       " from non-associated STA %pM"
				       " (type=0x%02x, subtype=0x%02x)\n",
				       dev->name, hdr->addr2,
				       type >> 2, stype >> 4);
				hostap_rx(dev, skb, rx_stats);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */
			}
			ret = AP_RX_EXIT;
			goto out;
		}
	} else if (fc & IEEE80211_FCTL_FROMDS) {
		if (!wds) {
			/* FromDS frame - not for us; probably
			 * broadcast/multicast in another BSS - drop */
			if (ether_addr_equal(hdr->addr1, dev->dev_addr)) {
				printk(KERN_DEBUG "Odd.. FromDS packet "
				       "received with own BSSID\n");
				hostap_dump_rx_80211(dev->name, skb, rx_stats);
			}
			ret = AP_RX_DROP;
			goto out;
		}
	} else if (stype == IEEE80211_STYPE_NULLFUNC && sta == NULL &&
		   ether_addr_equal(hdr->addr1, dev->dev_addr)) {

		if (local->hostapd) {
			prism2_rx_80211(local->apdev, skb, rx_stats,
					PRISM2_RX_NON_ASSOC);
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
		} else {
			/* At least Lucent f/w seems to send data::nullfunc
			 * frames with no ToDS flag when the current AP returns
			 * after being unavailable for some time. Speed up
			 * re-association by informing the station about it not
			 * being associated. */
			printk(KERN_DEBUG "%s: rejected received nullfunc frame"
			       " without ToDS from not associated STA %pM\n",
			       dev->name, hdr->addr2);
			hostap_rx(dev, skb, rx_stats);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */
		}
		ret = AP_RX_EXIT;
		goto out;
	} else if (stype == IEEE80211_STYPE_NULLFUNC) {
		/* At least Lucent cards seem to send periodic nullfunc
		 * frames with ToDS. Let these through to update SQ
		 * stats and PS state. Nullfunc frames do not contain
		 * any data and they will be dropped below. */
	} else {
		/* If BSSID (Addr3) is foreign, this frame is a normal
		 * broadcast frame from an IBSS network. Drop it silently.
		 * If BSSID is own, report the dropping of this frame. */
		if (ether_addr_equal(hdr->addr3, dev->dev_addr)) {
			printk(KERN_DEBUG "%s: dropped received packet from %pM"
			       " with no ToDS flag "
			       "(type=0x%02x, subtype=0x%02x)\n", dev->name,
			       hdr->addr2, type >> 2, stype >> 4);
			hostap_dump_rx_80211(dev->name, skb, rx_stats);
		}
		ret = AP_RX_DROP;
		goto out;
	}

	if (sta) {
		hostap_update_sta_ps2(local, sta, fc & IEEE80211_FCTL_PM,
				      type, stype);

		sta->rx_packets++;
		sta->rx_bytes += skb->len;
		sta->last_rx = jiffies;
	}

	if (local->ap->nullfunc_ack && stype == IEEE80211_STYPE_NULLFUNC &&
	    fc & IEEE80211_FCTL_TODS) {
		if (local->hostapd) {
			prism2_rx_80211(local->apdev, skb, rx_stats,
					PRISM2_RX_NULLFUNC_ACK);
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
		} else {
			/* some STA f/w's seem to require control::ACK frame
			 * for data::nullfunc, but Prism2 f/w 0.8.0 (at least
			 * from Compaq) does not send this.. Try to generate
			 * ACK for these frames from the host driver to make
			 * power saving work with, e.g., Lucent WaveLAN f/w */
			hostap_rx(dev, skb, rx_stats);
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */
		}
		ret = AP_RX_EXIT;
		goto out;
	}

 out:
	if (sta)
		atomic_dec(&sta->users);

	return ret;
}


/* Called only as a tasklet (software IRQ) */
int hostap_handle_sta_crypto(local_info_t *local,
			     struct ieee80211_hdr *hdr,
			     struct lib80211_crypt_data **crypt,
			     void **sta_ptr)
{
	struct sta_info *sta;

	spin_lock(&local->ap->sta_table_lock);
	sta = ap_get_sta(local->ap, hdr->addr2);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock(&local->ap->sta_table_lock);

	if (!sta)
		return -1;

	if (sta->crypt) {
		*crypt = sta->crypt;
		*sta_ptr = sta;
		/* hostap_handle_sta_release() will be called to release STA
		 * info */
	} else
		atomic_dec(&sta->users);

	return 0;
}


/* Called only as a tasklet (software IRQ) */
int hostap_is_sta_assoc(struct ap_data *ap, u8 *sta_addr)
{
	struct sta_info *sta;
	int ret = 0;

	spin_lock(&ap->sta_table_lock);
	sta = ap_get_sta(ap, sta_addr);
	if (sta != NULL && (sta->flags & WLAN_STA_ASSOC) && !sta->ap)
		ret = 1;
	spin_unlock(&ap->sta_table_lock);

	return ret;
}


/* Called only as a tasklet (software IRQ) */
int hostap_is_sta_authorized(struct ap_data *ap, u8 *sta_addr)
{
	struct sta_info *sta;
	int ret = 0;

	spin_lock(&ap->sta_table_lock);
	sta = ap_get_sta(ap, sta_addr);
	if (sta != NULL && (sta->flags & WLAN_STA_ASSOC) && !sta->ap &&
	    ((sta->flags & WLAN_STA_AUTHORIZED) ||
	     ap->local->ieee_802_1x == 0))
		ret = 1;
	spin_unlock(&ap->sta_table_lock);

	return ret;
}


/* Called only as a tasklet (software IRQ) */
int hostap_add_sta(struct ap_data *ap, u8 *sta_addr)
{
	struct sta_info *sta;
	int ret = 1;

	if (!ap)
		return -1;

	spin_lock(&ap->sta_table_lock);
	sta = ap_get_sta(ap, sta_addr);
	if (sta)
		ret = 0;
	spin_unlock(&ap->sta_table_lock);

	if (ret == 1) {
		sta = ap_add_sta(ap, sta_addr);
		if (!sta)
			return -1;
		sta->flags = WLAN_STA_AUTH | WLAN_STA_ASSOC;
		sta->ap = 1;
		memset(sta->supported_rates, 0, sizeof(sta->supported_rates));
		/* No way of knowing which rates are supported since we did not
		 * get supported rates element from beacon/assoc req. Assume
		 * that remote end supports all 802.11b rates. */
		sta->supported_rates[0] = 0x82;
		sta->supported_rates[1] = 0x84;
		sta->supported_rates[2] = 0x0b;
		sta->supported_rates[3] = 0x16;
		sta->tx_supp_rates = WLAN_RATE_1M | WLAN_RATE_2M |
			WLAN_RATE_5M5 | WLAN_RATE_11M;
		sta->tx_rate = 110;
		sta->tx_max_rate = sta->tx_rate_idx = 3;
	}

	return ret;
}


/* Called only as a tasklet (software IRQ) */
int hostap_update_rx_stats(struct ap_data *ap,
			   struct ieee80211_hdr *hdr,
			   struct hostap_80211_rx_status *rx_stats)
{
	struct sta_info *sta;

	if (!ap)
		return -1;

	spin_lock(&ap->sta_table_lock);
	sta = ap_get_sta(ap, hdr->addr2);
	if (sta) {
		sta->last_rx_silence = rx_stats->noise;
		sta->last_rx_signal = rx_stats->signal;
		sta->last_rx_rate = rx_stats->rate;
		sta->last_rx_updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
		if (rx_stats->rate == 10)
			sta->rx_count[0]++;
		else if (rx_stats->rate == 20)
			sta->rx_count[1]++;
		else if (rx_stats->rate == 55)
			sta->rx_count[2]++;
		else if (rx_stats->rate == 110)
			sta->rx_count[3]++;
	}
	spin_unlock(&ap->sta_table_lock);

	return sta ? 0 : -1;
}


void hostap_update_rates(local_info_t *local)
{
	struct sta_info *sta;
	struct ap_data *ap = local->ap;

	if (!ap)
		return;

	spin_lock_bh(&ap->sta_table_lock);
	list_for_each_entry(sta, &ap->sta_list, list) {
		prism2_check_tx_rates(sta);
	}
	spin_unlock_bh(&ap->sta_table_lock);
}


void * ap_crypt_get_ptrs(struct ap_data *ap, u8 *addr, int permanent,
			 struct lib80211_crypt_data ***crypt)
{
	struct sta_info *sta;

	spin_lock_bh(&ap->sta_table_lock);
	sta = ap_get_sta(ap, addr);
	if (sta)
		atomic_inc(&sta->users);
	spin_unlock_bh(&ap->sta_table_lock);

	if (!sta && permanent)
		sta = ap_add_sta(ap, addr);

	if (!sta)
		return NULL;

	if (permanent)
		sta->flags |= WLAN_STA_PERM;

	*crypt = &sta->crypt;

	return sta;
}


void hostap_add_wds_links(local_info_t *local)
{
	struct ap_data *ap = local->ap;
	struct sta_info *sta;

	spin_lock_bh(&ap->sta_table_lock);
	list_for_each_entry(sta, &ap->sta_list, list) {
		if (sta->ap)
			hostap_wds_link_oper(local, sta->addr, WDS_ADD);
	}
	spin_unlock_bh(&ap->sta_table_lock);

	schedule_work(&local->ap->wds_oper_queue);
}


void hostap_wds_link_oper(local_info_t *local, u8 *addr, wds_oper_type type)
{
	struct wds_oper_data *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return;
	memcpy(entry->addr, addr, ETH_ALEN);
	entry->type = type;
	spin_lock_bh(&local->lock);
	entry->next = local->ap->wds_oper_entries;
	local->ap->wds_oper_entries = entry;
	spin_unlock_bh(&local->lock);

	schedule_work(&local->ap->wds_oper_queue);
}


EXPORT_SYMBOL(hostap_init_data);
EXPORT_SYMBOL(hostap_init_ap_proc);
EXPORT_SYMBOL(hostap_free_data);
EXPORT_SYMBOL(hostap_check_sta_fw_version);
EXPORT_SYMBOL(hostap_handle_sta_tx_exc);
#ifndef PRISM2_NO_KERNEL_IEEE80211_MGMT
#endif /* PRISM2_NO_KERNEL_IEEE80211_MGMT */
