/* Host AP driver Info Frame processing (part of hostap.o module) */

#include "hostap_wlan.h"
#include "hostap.h"
#include "hostap_ap.h"

/* Called only as a tasklet (software IRQ) */
static void prism2_info_commtallies16(local_info_t *local, unsigned char *buf,
				      int left)
{
	struct hfa384x_comm_tallies *tallies;

	if (left < sizeof(struct hfa384x_comm_tallies)) {
		printk(KERN_DEBUG "%s: too short (len=%d) commtallies "
		       "info frame\n", local->dev->name, left);
		return;
	}

	tallies = (struct hfa384x_comm_tallies *) buf;
#define ADD_COMM_TALLIES(name) \
local->comm_tallies.name += le16_to_cpu(tallies->name)
	ADD_COMM_TALLIES(tx_unicast_frames);
	ADD_COMM_TALLIES(tx_multicast_frames);
	ADD_COMM_TALLIES(tx_fragments);
	ADD_COMM_TALLIES(tx_unicast_octets);
	ADD_COMM_TALLIES(tx_multicast_octets);
	ADD_COMM_TALLIES(tx_deferred_transmissions);
	ADD_COMM_TALLIES(tx_single_retry_frames);
	ADD_COMM_TALLIES(tx_multiple_retry_frames);
	ADD_COMM_TALLIES(tx_retry_limit_exceeded);
	ADD_COMM_TALLIES(tx_discards);
	ADD_COMM_TALLIES(rx_unicast_frames);
	ADD_COMM_TALLIES(rx_multicast_frames);
	ADD_COMM_TALLIES(rx_fragments);
	ADD_COMM_TALLIES(rx_unicast_octets);
	ADD_COMM_TALLIES(rx_multicast_octets);
	ADD_COMM_TALLIES(rx_fcs_errors);
	ADD_COMM_TALLIES(rx_discards_no_buffer);
	ADD_COMM_TALLIES(tx_discards_wrong_sa);
	ADD_COMM_TALLIES(rx_discards_wep_undecryptable);
	ADD_COMM_TALLIES(rx_message_in_msg_fragments);
	ADD_COMM_TALLIES(rx_message_in_bad_msg_fragments);
#undef ADD_COMM_TALLIES
}


/* Called only as a tasklet (software IRQ) */
static void prism2_info_commtallies32(local_info_t *local, unsigned char *buf,
				      int left)
{
	struct hfa384x_comm_tallies32 *tallies;

	if (left < sizeof(struct hfa384x_comm_tallies32)) {
		printk(KERN_DEBUG "%s: too short (len=%d) commtallies32 "
		       "info frame\n", local->dev->name, left);
		return;
	}

	tallies = (struct hfa384x_comm_tallies32 *) buf;
#define ADD_COMM_TALLIES(name) \
local->comm_tallies.name += le32_to_cpu(tallies->name)
	ADD_COMM_TALLIES(tx_unicast_frames);
	ADD_COMM_TALLIES(tx_multicast_frames);
	ADD_COMM_TALLIES(tx_fragments);
	ADD_COMM_TALLIES(tx_unicast_octets);
	ADD_COMM_TALLIES(tx_multicast_octets);
	ADD_COMM_TALLIES(tx_deferred_transmissions);
	ADD_COMM_TALLIES(tx_single_retry_frames);
	ADD_COMM_TALLIES(tx_multiple_retry_frames);
	ADD_COMM_TALLIES(tx_retry_limit_exceeded);
	ADD_COMM_TALLIES(tx_discards);
	ADD_COMM_TALLIES(rx_unicast_frames);
	ADD_COMM_TALLIES(rx_multicast_frames);
	ADD_COMM_TALLIES(rx_fragments);
	ADD_COMM_TALLIES(rx_unicast_octets);
	ADD_COMM_TALLIES(rx_multicast_octets);
	ADD_COMM_TALLIES(rx_fcs_errors);
	ADD_COMM_TALLIES(rx_discards_no_buffer);
	ADD_COMM_TALLIES(tx_discards_wrong_sa);
	ADD_COMM_TALLIES(rx_discards_wep_undecryptable);
	ADD_COMM_TALLIES(rx_message_in_msg_fragments);
	ADD_COMM_TALLIES(rx_message_in_bad_msg_fragments);
#undef ADD_COMM_TALLIES
}


/* Called only as a tasklet (software IRQ) */
static void prism2_info_commtallies(local_info_t *local, unsigned char *buf,
				    int left)
{
	if (local->tallies32)
		prism2_info_commtallies32(local, buf, left);
	else
		prism2_info_commtallies16(local, buf, left);
}


#ifndef PRISM2_NO_STATION_MODES
#ifndef PRISM2_NO_DEBUG
static const char* hfa384x_linkstatus_str(u16 linkstatus)
{
	switch (linkstatus) {
	case HFA384X_LINKSTATUS_CONNECTED:
		return "Connected";
	case HFA384X_LINKSTATUS_DISCONNECTED:
		return "Disconnected";
	case HFA384X_LINKSTATUS_AP_CHANGE:
		return "Access point change";
	case HFA384X_LINKSTATUS_AP_OUT_OF_RANGE:
		return "Access point out of range";
	case HFA384X_LINKSTATUS_AP_IN_RANGE:
		return "Access point in range";
	case HFA384X_LINKSTATUS_ASSOC_FAILED:
		return "Association failed";
	default:
		return "Unknown";
	}
}
#endif /* PRISM2_NO_DEBUG */


/* Called only as a tasklet (software IRQ) */
static void prism2_info_linkstatus(local_info_t *local, unsigned char *buf,
				    int left)
{
	u16 val;
	int non_sta_mode;

	/* Alloc new JoinRequests to occur since LinkStatus for the previous
	 * has been received */
	local->last_join_time = 0;

	if (left != 2) {
		printk(KERN_DEBUG "%s: invalid linkstatus info frame "
		       "length %d\n", local->dev->name, left);
		return;
	}

	non_sta_mode = local->iw_mode == IW_MODE_MASTER ||
		local->iw_mode == IW_MODE_REPEAT ||
		local->iw_mode == IW_MODE_MONITOR;

	val = buf[0] | (buf[1] << 8);
	if (!non_sta_mode || val != HFA384X_LINKSTATUS_DISCONNECTED) {
		PDEBUG(DEBUG_EXTRA, "%s: LinkStatus=%d (%s)\n",
		       local->dev->name, val, hfa384x_linkstatus_str(val));
	}

	if (non_sta_mode) {
		netif_carrier_on(local->dev);
		netif_carrier_on(local->ddev);
		return;
	}

	/* Get current BSSID later in scheduled task */
	set_bit(PRISM2_INFO_PENDING_LINKSTATUS, &local->pending_info);
	local->prev_link_status = val;
	schedule_work(&local->info_queue);
}


static void prism2_host_roaming(local_info_t *local)
{
	struct hfa384x_join_request req;
	struct net_device *dev = local->dev;
	struct hfa384x_hostscan_result *selected, *entry;
	int i;
	unsigned long flags;

	if (local->last_join_time &&
	    time_before(jiffies, local->last_join_time + 10 * HZ)) {
		PDEBUG(DEBUG_EXTRA, "%s: last join request has not yet been "
		       "completed - waiting for it before issuing new one\n",
		       dev->name);
		return;
	}

	/* ScanResults are sorted: first ESS results in decreasing signal
	 * quality then IBSS results in similar order.
	 * Trivial roaming policy: just select the first entry.
	 * This could probably be improved by adding hysteresis to limit
	 * number of handoffs, etc.
	 *
	 * Could do periodic RID_SCANREQUEST or Inquire F101 to get new
	 * ScanResults */
	spin_lock_irqsave(&local->lock, flags);
	if (local->last_scan_results == NULL ||
	    local->last_scan_results_count == 0) {
		spin_unlock_irqrestore(&local->lock, flags);
		PDEBUG(DEBUG_EXTRA, "%s: no scan results for host roaming\n",
		       dev->name);
		return;
	}

	selected = &local->last_scan_results[0];

	if (local->preferred_ap[0] || local->preferred_ap[1] ||
	    local->preferred_ap[2] || local->preferred_ap[3] ||
	    local->preferred_ap[4] || local->preferred_ap[5]) {
		/* Try to find preferred AP */
		PDEBUG(DEBUG_EXTRA, "%s: Preferred AP BSSID %pM\n",
		       dev->name, local->preferred_ap);
		for (i = 0; i < local->last_scan_results_count; i++) {
			entry = &local->last_scan_results[i];
			if (memcmp(local->preferred_ap, entry->bssid, 6) == 0)
			{
				PDEBUG(DEBUG_EXTRA, "%s: using preferred AP "
				       "selection\n", dev->name);
				selected = entry;
				break;
			}
		}
	}

	memcpy(req.bssid, selected->bssid, 6);
	req.channel = selected->chid;
	spin_unlock_irqrestore(&local->lock, flags);

	PDEBUG(DEBUG_EXTRA, "%s: JoinRequest: BSSID=%pM"
	       " channel=%d\n",
	       dev->name, req.bssid, le16_to_cpu(req.channel));
	if (local->func->set_rid(dev, HFA384X_RID_JOINREQUEST, &req,
				 sizeof(req))) {
		printk(KERN_DEBUG "%s: JoinRequest failed\n", dev->name);
	}
	local->last_join_time = jiffies;
}


static void hostap_report_scan_complete(local_info_t *local)
{
	union iwreq_data wrqu;

	/* Inform user space about new scan results (just empty event,
	 * SIOCGIWSCAN can be used to fetch data */
	wrqu.data.length = 0;
	wrqu.data.flags = 0;
	wireless_send_event(local->dev, SIOCGIWSCAN, &wrqu, NULL);

	/* Allow SIOCGIWSCAN handling to occur since we have received
	 * scanning result */
	local->scan_timestamp = 0;
}


/* Called only as a tasklet (software IRQ) */
static void prism2_info_scanresults(local_info_t *local, unsigned char *buf,
				    int left)
{
	u16 *pos;
	int new_count, i;
	unsigned long flags;
	struct hfa384x_scan_result *res;
	struct hfa384x_hostscan_result *results, *prev;

	if (left < 4) {
		printk(KERN_DEBUG "%s: invalid scanresult info frame "
		       "length %d\n", local->dev->name, left);
		return;
	}

	pos = (u16 *) buf;
	pos++;
	pos++;
	left -= 4;

	new_count = left / sizeof(struct hfa384x_scan_result);
	results = kmalloc(new_count * sizeof(struct hfa384x_hostscan_result),
			  GFP_ATOMIC);
	if (results == NULL)
		return;

	/* Convert to hostscan result format. */
	res = (struct hfa384x_scan_result *) pos;
	for (i = 0; i < new_count; i++) {
		memcpy(&results[i], &res[i],
		       sizeof(struct hfa384x_scan_result));
		results[i].atim = 0;
	}

	spin_lock_irqsave(&local->lock, flags);
	local->last_scan_type = PRISM2_SCAN;
	prev = local->last_scan_results;
	local->last_scan_results = results;
	local->last_scan_results_count = new_count;
	spin_unlock_irqrestore(&local->lock, flags);
	kfree(prev);

	hostap_report_scan_complete(local);

	/* Perform rest of ScanResults handling later in scheduled task */
	set_bit(PRISM2_INFO_PENDING_SCANRESULTS, &local->pending_info);
	schedule_work(&local->info_queue);
}


/* Called only as a tasklet (software IRQ) */
static void prism2_info_hostscanresults(local_info_t *local,
					unsigned char *buf, int left)
{
	int i, result_size, copy_len, new_count;
	struct hfa384x_hostscan_result *results, *prev;
	unsigned long flags;
	__le16 *pos;
	u8 *ptr;

	wake_up_interruptible(&local->hostscan_wq);

	if (left < 4) {
		printk(KERN_DEBUG "%s: invalid hostscanresult info frame "
		       "length %d\n", local->dev->name, left);
		return;
	}

	pos = (__le16 *) buf;
	copy_len = result_size = le16_to_cpu(*pos);
	if (result_size == 0) {
		printk(KERN_DEBUG "%s: invalid result_size (0) in "
		       "hostscanresults\n", local->dev->name);
		return;
	}
	if (copy_len > sizeof(struct hfa384x_hostscan_result))
		copy_len = sizeof(struct hfa384x_hostscan_result);

	pos++;
	pos++;
	left -= 4;
	ptr = (u8 *) pos;

	new_count = left / result_size;
	results = kcalloc(new_count, sizeof(struct hfa384x_hostscan_result),
			  GFP_ATOMIC);
	if (results == NULL)
		return;

	for (i = 0; i < new_count; i++) {
		memcpy(&results[i], ptr, copy_len);
		ptr += result_size;
		left -= result_size;
	}

	if (left) {
		printk(KERN_DEBUG "%s: short HostScan result entry (%d/%d)\n",
		       local->dev->name, left, result_size);
	}

	spin_lock_irqsave(&local->lock, flags);
	local->last_scan_type = PRISM2_HOSTSCAN;
	prev = local->last_scan_results;
	local->last_scan_results = results;
	local->last_scan_results_count = new_count;
	spin_unlock_irqrestore(&local->lock, flags);
	kfree(prev);

	hostap_report_scan_complete(local);
}
#endif /* PRISM2_NO_STATION_MODES */


/* Called only as a tasklet (software IRQ) */
void hostap_info_process(local_info_t *local, struct sk_buff *skb)
{
	struct hfa384x_info_frame *info;
	unsigned char *buf;
	int left;
#ifndef PRISM2_NO_DEBUG
	int i;
#endif /* PRISM2_NO_DEBUG */

	info = (struct hfa384x_info_frame *) skb->data;
	buf = skb->data + sizeof(*info);
	left = skb->len - sizeof(*info);

	switch (le16_to_cpu(info->type)) {
	case HFA384X_INFO_COMMTALLIES:
		prism2_info_commtallies(local, buf, left);
		break;

#ifndef PRISM2_NO_STATION_MODES
	case HFA384X_INFO_LINKSTATUS:
		prism2_info_linkstatus(local, buf, left);
		break;

	case HFA384X_INFO_SCANRESULTS:
		prism2_info_scanresults(local, buf, left);
		break;

	case HFA384X_INFO_HOSTSCANRESULTS:
		prism2_info_hostscanresults(local, buf, left);
		break;
#endif /* PRISM2_NO_STATION_MODES */

#ifndef PRISM2_NO_DEBUG
	default:
		PDEBUG(DEBUG_EXTRA, "%s: INFO - len=%d type=0x%04x\n",
		       local->dev->name, le16_to_cpu(info->len),
		       le16_to_cpu(info->type));
		PDEBUG(DEBUG_EXTRA, "Unknown info frame:");
		for (i = 0; i < (left < 100 ? left : 100); i++)
			PDEBUG2(DEBUG_EXTRA, " %02x", buf[i]);
		PDEBUG2(DEBUG_EXTRA, "\n");
		break;
#endif /* PRISM2_NO_DEBUG */
	}
}


#ifndef PRISM2_NO_STATION_MODES
static void handle_info_queue_linkstatus(local_info_t *local)
{
	int val = local->prev_link_status;
	int connected;
	union iwreq_data wrqu;

	connected =
		val == HFA384X_LINKSTATUS_CONNECTED ||
		val == HFA384X_LINKSTATUS_AP_CHANGE ||
		val == HFA384X_LINKSTATUS_AP_IN_RANGE;

	if (local->func->get_rid(local->dev, HFA384X_RID_CURRENTBSSID,
				 local->bssid, ETH_ALEN, 1) < 0) {
		printk(KERN_DEBUG "%s: could not read CURRENTBSSID after "
		       "LinkStatus event\n", local->dev->name);
	} else {
		PDEBUG(DEBUG_EXTRA, "%s: LinkStatus: BSSID=%pM\n",
		       local->dev->name,
		       (unsigned char *) local->bssid);
		if (local->wds_type & HOSTAP_WDS_AP_CLIENT)
			hostap_add_sta(local->ap, local->bssid);
	}

	/* Get BSSID if we have a valid AP address */
	if (connected) {
		netif_carrier_on(local->dev);
		netif_carrier_on(local->ddev);
		memcpy(wrqu.ap_addr.sa_data, local->bssid, ETH_ALEN);
	} else {
		netif_carrier_off(local->dev);
		netif_carrier_off(local->ddev);
		memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	}
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;

	/*
	 * Filter out sequential disconnect events in order not to cause a
	 * flood of SIOCGIWAP events that have a race condition with EAPOL
	 * frames and can confuse wpa_supplicant about the current association
	 * status.
	 */
	if (connected || local->prev_linkstatus_connected)
		wireless_send_event(local->dev, SIOCGIWAP, &wrqu, NULL);
	local->prev_linkstatus_connected = connected;
}


static void handle_info_queue_scanresults(local_info_t *local)
{
	if (local->host_roaming == 1 && local->iw_mode == IW_MODE_INFRA)
		prism2_host_roaming(local);

	if (local->host_roaming == 2 && local->iw_mode == IW_MODE_INFRA &&
	    memcmp(local->preferred_ap, "\x00\x00\x00\x00\x00\x00",
		   ETH_ALEN) != 0) {
		/*
		 * Firmware seems to be getting into odd state in host_roaming
		 * mode 2 when hostscan is used without join command, so try
		 * to fix this by re-joining the current AP. This does not
		 * actually trigger a new association if the current AP is
		 * still in the scan results.
		 */
		prism2_host_roaming(local);
	}
}


/* Called only as scheduled task after receiving info frames (used to avoid
 * pending too much time in HW IRQ handler). */
static void handle_info_queue(struct work_struct *work)
{
	local_info_t *local = container_of(work, local_info_t, info_queue);

	if (test_and_clear_bit(PRISM2_INFO_PENDING_LINKSTATUS,
			       &local->pending_info))
		handle_info_queue_linkstatus(local);

	if (test_and_clear_bit(PRISM2_INFO_PENDING_SCANRESULTS,
			       &local->pending_info))
		handle_info_queue_scanresults(local);
}
#endif /* PRISM2_NO_STATION_MODES */


void hostap_info_init(local_info_t *local)
{
	skb_queue_head_init(&local->info_list);
#ifndef PRISM2_NO_STATION_MODES
	INIT_WORK(&local->info_queue, handle_info_queue);
#endif /* PRISM2_NO_STATION_MODES */
}


EXPORT_SYMBOL(hostap_info_init);
EXPORT_SYMBOL(hostap_info_process);
