#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <net/iw_handler.h>

#include "dev.h"
#include "decl.h"
#include "host.h"
#include "debugfs.h"

static struct dentry *libertas_dir = NULL;
static char *szStates[] = {
	"Connected",
	"Disconnected"
};

#ifdef PROC_DEBUG
static void libertas_debug_init(wlan_private * priv, struct net_device *dev);
#endif

static int open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t write_file_dummy(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
        return -EINVAL;
}

static const size_t len = PAGE_SIZE;

static ssize_t libertas_dev_info(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	size_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	ssize_t res;

	pos += snprintf(buf+pos, len-pos, "state = %s\n",
				szStates[priv->adapter->connect_status]);
	pos += snprintf(buf+pos, len-pos, "region_code = %02x\n",
				(u32) priv->adapter->regioncode);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

	free_page(addr);
	return res;
}


static ssize_t libertas_getscantable(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	size_t pos = 0;
	int numscansdone = 0, res;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	struct bss_descriptor * iter_bss;

	pos += snprintf(buf+pos, len-pos,
		"# | ch  | ss  |       bssid       |   cap    |    TSF   | Qual | SSID \n");

	mutex_lock(&priv->adapter->lock);
	list_for_each_entry (iter_bss, &priv->adapter->network_list, list) {
		u16 cap;

		memcpy(&cap, &iter_bss->cap, sizeof(cap));
		pos += snprintf(buf+pos, len-pos,
			"%02u| %03d | %03ld | " MAC_FMT " |",
			numscansdone, iter_bss->channel, iter_bss->rssi,
			MAC_ARG(iter_bss->bssid));
		pos += snprintf(buf+pos, len-pos, " %04x-", cap);
		pos += snprintf(buf+pos, len-pos, "%c%c%c |",
				iter_bss->cap.ibss ? 'A' : 'I',
				iter_bss->cap.privacy ? 'P' : ' ',
				iter_bss->cap.spectrummgmt ? 'S' : ' ');
		pos += snprintf(buf+pos, len-pos, " %08llx |", iter_bss->networktsf);
		pos += snprintf(buf+pos, len-pos, " %d |", SCAN_RSSI(iter_bss->rssi));
		pos += snprintf(buf+pos, len-pos, " %s\n",
		                escape_essid(iter_bss->ssid, iter_bss->ssid_len));

		numscansdone++;
	}
	mutex_unlock(&priv->adapter->lock);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

	free_page(addr);
	return res;
}

static ssize_t libertas_sleepparams_write(struct file *file,
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	ssize_t buf_size, res;
	int p1, p2, p3, p4, p5, p6;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, user_buf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%d %d %d %d %d %d", &p1, &p2, &p3, &p4, &p5, &p6);
	if (res != 6) {
		res = -EFAULT;
		goto out_unlock;
	}
	priv->adapter->sp.sp_error = p1;
	priv->adapter->sp.sp_offset = p2;
	priv->adapter->sp.sp_stabletime = p3;
	priv->adapter->sp.sp_calcontrol = p4;
	priv->adapter->sp.sp_extsleepclk = p5;
	priv->adapter->sp.sp_reserved = p6;

        res = libertas_prepare_and_send_command(priv,
				cmd_802_11_sleep_params,
				cmd_act_set,
				cmd_option_waitforrsp, 0, NULL);

	if (!res)
		res = count;
	else
		res = -EINVAL;

out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_sleepparams_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	ssize_t res;
	size_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

        res = libertas_prepare_and_send_command(priv,
				cmd_802_11_sleep_params,
				cmd_act_get,
				cmd_option_waitforrsp, 0, NULL);
	if (res) {
		res = -EFAULT;
		goto out_unlock;
	}

	pos += snprintf(buf, len, "%d %d %d %d %d %d\n", adapter->sp.sp_error,
			adapter->sp.sp_offset, adapter->sp.sp_stabletime,
			adapter->sp.sp_calcontrol, adapter->sp.sp_extsleepclk,
			adapter->sp.sp_reserved);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_extscan(struct file *file, const char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	ssize_t res, buf_size;
	union iwreq_data wrqu;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}

	libertas_send_specific_ssid_scan(priv, buf, strlen(buf)-1, 0);

	memset(&wrqu, 0, sizeof(union iwreq_data));
	wireless_send_event(priv->dev, SIOCGIWSCAN, &wrqu, NULL);

out_unlock:
	free_page(addr);
	return count;
}

static int libertas_parse_chan(char *buf, size_t count,
			struct wlan_ioctl_user_scan_cfg *scan_cfg, int dur)
{
	char *start, *end, *hold, *str;
	int i = 0;

	start = strstr(buf, "chan=");
	if (!start)
		return -EINVAL;
	start += 5;
	end = strstr(start, " ");
	if (!end)
		end = buf + count;
	hold = kzalloc((end - start)+1, GFP_KERNEL);
	if (!hold)
		return -ENOMEM;
	strncpy(hold, start, end - start);
	hold[(end-start)+1] = '\0';
	while(hold && (str = strsep(&hold, ","))) {
		int chan;
		char band, passive = 0;
		sscanf(str, "%d%c%c", &chan, &band, &passive);
		scan_cfg->chanlist[i].channumber = chan;
		scan_cfg->chanlist[i].scantype = passive ? 1 : 0;
		if (band == 'b' || band == 'g')
			scan_cfg->chanlist[i].radiotype = 0;
		else if (band == 'a')
			scan_cfg->chanlist[i].radiotype = 1;

		scan_cfg->chanlist[i].scantime = dur;
		i++;
	}

	kfree(hold);
	return i;
}

static void libertas_parse_bssid(char *buf, size_t count,
                        struct wlan_ioctl_user_scan_cfg *scan_cfg)
{
	char *hold;
	unsigned int mac[ETH_ALEN];

	hold = strstr(buf, "bssid=");
	if (!hold)
		return;
	hold += 6;
	sscanf(hold, MAC_FMT, mac, mac+1, mac+2, mac+3, mac+4, mac+5);
	memcpy(scan_cfg->bssid, mac, ETH_ALEN);
}

static void libertas_parse_ssid(char *buf, size_t count,
                        struct wlan_ioctl_user_scan_cfg *scan_cfg)
{
	char *hold, *end;
	ssize_t size;

	hold = strstr(buf, "ssid=");
	if (!hold)
		return;
	hold += 5;
	end = strstr(hold, " ");
	if (!end)
		end = buf + count - 1;

	size = min((size_t)IW_ESSID_MAX_SIZE, (size_t) (end - hold));
	strncpy(scan_cfg->ssid, hold, size);

	return;
}

static int libertas_parse_clear(char *buf, size_t count, const char *tag)
{
	char *hold;
	int val;

	hold = strstr(buf, tag);
	if (!hold)
		return 0;
	hold += strlen(tag);
	sscanf(hold, "%d", &val);

	if (val != 0)
		val = 1;

	return val;
}

static int libertas_parse_dur(char *buf, size_t count,
                        struct wlan_ioctl_user_scan_cfg *scan_cfg)
{
	char *hold;
	int val;

	hold = strstr(buf, "dur=");
	if (!hold)
		return 0;
	hold += 4;
	sscanf(hold, "%d", &val);

	return val;
}

static void libertas_parse_probes(char *buf, size_t count,
                        struct wlan_ioctl_user_scan_cfg *scan_cfg)
{
	char *hold;
	int val;

	hold = strstr(buf, "probes=");
	if (!hold)
		return;
	hold += 7;
	sscanf(hold, "%d", &val);

	scan_cfg->numprobes = val;

	return;
}

static void libertas_parse_type(char *buf, size_t count,
                        struct wlan_ioctl_user_scan_cfg *scan_cfg)
{
	char *hold;
	int val;

	hold = strstr(buf, "type=");
	if (!hold)
		return;
	hold += 5;
	sscanf(hold, "%d", &val);

	/* type=1,2 or 3 */
	if (val < 1 || val > 3)
		return;

	scan_cfg->bsstype = val;

	return;
}

static ssize_t libertas_setuserscan(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	ssize_t res, buf_size;
	struct wlan_ioctl_user_scan_cfg *scan_cfg;
	union iwreq_data wrqu;
	int dur;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	scan_cfg = kzalloc(sizeof(struct wlan_ioctl_user_scan_cfg), GFP_KERNEL);
	if (!scan_cfg)
		return -ENOMEM;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}

	scan_cfg->bsstype = WLAN_SCAN_BSS_TYPE_ANY;

	dur = libertas_parse_dur(buf, count, scan_cfg);
	libertas_parse_chan(buf, count, scan_cfg, dur);
	libertas_parse_bssid(buf, count, scan_cfg);
	scan_cfg->clear_bssid = libertas_parse_clear(buf, count, "clear_bssid=");
	libertas_parse_ssid(buf, count, scan_cfg);
	scan_cfg->clear_ssid = libertas_parse_clear(buf, count, "clear_ssid=");
	libertas_parse_probes(buf, count, scan_cfg);
	libertas_parse_type(buf, count, scan_cfg);

	wlan_scan_networks(priv, scan_cfg, 1);
	wait_event_interruptible(priv->adapter->cmd_pending,
				 !priv->adapter->nr_cmd_pending);

	memset(&wrqu, 0x00, sizeof(union iwreq_data));
	wireless_send_event(priv->dev, SIOCGIWSCAN, &wrqu, NULL);

out_unlock:
	free_page(addr);
	kfree(scan_cfg);
	return count;
}

static int libertas_event_initcmd(wlan_private *priv, void **response_buf,
			struct cmd_ctrl_node **cmdnode,
			struct cmd_ds_command **cmd)
{
	u16 wait_option = cmd_option_waitforrsp;

	if (!(*cmdnode = libertas_get_free_cmd_ctrl_node(priv))) {
		lbs_deb_debugfs("failed libertas_get_free_cmd_ctrl_node\n");
		return -ENOMEM;
	}
	if (!(*response_buf = kmalloc(3000, GFP_KERNEL))) {
		lbs_deb_debugfs("failed to allocate response buffer!\n");
		return -ENOMEM;
	}
	libertas_set_cmd_ctrl_node(priv, *cmdnode, 0, wait_option, NULL);
	init_waitqueue_head(&(*cmdnode)->cmdwait_q);
	(*cmdnode)->pdata_buf = *response_buf;
	(*cmdnode)->cmdflags |= CMD_F_HOSTCMD;
	(*cmdnode)->cmdwaitqwoken = 0;
	*cmd = (struct cmd_ds_command *)(*cmdnode)->bufvirtualaddr;
	(*cmd)->command = cpu_to_le16(cmd_802_11_subscribe_event);
	(*cmd)->seqnum = cpu_to_le16(++priv->adapter->seqnum);
	(*cmd)->result = 0;
	return 0;
}

static ssize_t libertas_lowrssi_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	void *response_buf;
	int res, cmd_len;
	ssize_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0) {
		free_page(addr);
		return res;
	}

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_get);
	pcmdptr->size = cpu_to_le16(sizeof(*event) + S_DS_GEN);
	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;
	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	event = (void *)(response_buf + S_DS_GEN);
	while (cmd_len < le16_to_cpu(pcmdptr->size)) {
		struct mrvlietypesheader *header = (void *)(response_buf + cmd_len);
		switch (header->type) {
		struct mrvlietypes_rssithreshold  *Lowrssi;
		case __constant_cpu_to_le16(TLV_TYPE_RSSI_LOW):
			Lowrssi = (void *)(response_buf + cmd_len);
			pos += snprintf(buf+pos, len-pos, "%d %d %d\n",
					Lowrssi->rssivalue,
					Lowrssi->rssifreq,
					(event->events & cpu_to_le16(0x0001))?1:0);
		default:
			cmd_len += sizeof(struct mrvlietypes_snrthreshold);
			break;
		}
	}

	kfree(response_buf);
	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);
	return res;
}

static u16 libertas_get_events_bitmap(wlan_private *priv)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	void *response_buf;
	int res;
	u16 event_bitmap;

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0)
		return res;

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_get);
	pcmdptr->size = cpu_to_le16(sizeof(*event) + S_DS_GEN);
	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		return 0;
	}

	if (pcmdptr->command != cmd_ret_802_11_subscribe_event) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		return 0;
	}

	event = (struct cmd_ds_802_11_subscribe_event *)(response_buf + S_DS_GEN);
	event_bitmap = le16_to_cpu(event->events);
	kfree(response_buf);
	return event_bitmap;
}

static ssize_t libertas_lowrssi_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	ssize_t res, buf_size;
	int value, freq, subscribed, cmd_len;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	struct mrvlietypes_rssithreshold *rssi_threshold;
	void *response_buf;
	u16 event_bitmap;
	u8 *ptr;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%d %d %d", &value, &freq, &subscribed);
	if (res != 3) {
		res = -EFAULT;
		goto out_unlock;
	}

	event_bitmap = libertas_get_events_bitmap(priv);

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0)
		goto out_unlock;

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_set);
	pcmdptr->size = cpu_to_le16(S_DS_GEN +
		sizeof(struct cmd_ds_802_11_subscribe_event) +
		sizeof(struct mrvlietypes_rssithreshold));

	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	ptr = (u8*) pcmdptr+cmd_len;
	rssi_threshold = (struct mrvlietypes_rssithreshold *)(ptr);
	rssi_threshold->header.type = cpu_to_le16(0x0104);
	rssi_threshold->header.len = cpu_to_le16(2);
	rssi_threshold->rssivalue = value;
	rssi_threshold->rssifreq = freq;
	event_bitmap |= subscribed ? 0x0001 : 0x0;
	event->events = cpu_to_le16(event_bitmap);

	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_lowsnr_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	void *response_buf;
	int res, cmd_len;
	ssize_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0) {
		free_page(addr);
		return res;
	}

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_get);
	pcmdptr->size = cpu_to_le16(sizeof(*event) + S_DS_GEN);
	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	event = (void *)(response_buf + S_DS_GEN);
	while (cmd_len < le16_to_cpu(pcmdptr->size)) {
		struct mrvlietypesheader *header = (void *)(response_buf + cmd_len);
		switch (header->type) {
		struct mrvlietypes_snrthreshold *LowSnr;
		case __constant_cpu_to_le16(TLV_TYPE_SNR_LOW):
			LowSnr = (void *)(response_buf + cmd_len);
			pos += snprintf(buf+pos, len-pos, "%d %d %d\n",
					LowSnr->snrvalue,
					LowSnr->snrfreq,
					(event->events & cpu_to_le16(0x0002))?1:0);
		default:
			cmd_len += sizeof(struct mrvlietypes_snrthreshold);
			break;
		}
	}

	kfree(response_buf);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);
	return res;
}

static ssize_t libertas_lowsnr_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	ssize_t res, buf_size;
	int value, freq, subscribed, cmd_len;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	struct mrvlietypes_snrthreshold *snr_threshold;
	void *response_buf;
	u16 event_bitmap;
	u8 *ptr;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%d %d %d", &value, &freq, &subscribed);
	if (res != 3) {
		res = -EFAULT;
		goto out_unlock;
	}

	event_bitmap = libertas_get_events_bitmap(priv);

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0)
		goto out_unlock;

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_set);
	pcmdptr->size = cpu_to_le16(S_DS_GEN +
		sizeof(struct cmd_ds_802_11_subscribe_event) +
		sizeof(struct mrvlietypes_snrthreshold));
	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	ptr = (u8*) pcmdptr+cmd_len;
	snr_threshold = (struct mrvlietypes_snrthreshold *)(ptr);
	snr_threshold->header.type = cpu_to_le16(TLV_TYPE_SNR_LOW);
	snr_threshold->header.len = cpu_to_le16(2);
	snr_threshold->snrvalue = value;
	snr_threshold->snrfreq = freq;
	event_bitmap |= subscribed ? 0x0002 : 0x0;
	event->events = cpu_to_le16(event_bitmap);

	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	res = count;

out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_failcount_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	void *response_buf;
	int res, cmd_len;
	ssize_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0) {
		free_page(addr);
		return res;
	}

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_get);
	pcmdptr->size =	cpu_to_le16(sizeof(*event) + S_DS_GEN);
	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	event = (void *)(response_buf + S_DS_GEN);
	while (cmd_len < le16_to_cpu(pcmdptr->size)) {
		struct mrvlietypesheader *header = (void *)(response_buf + cmd_len);
		switch (header->type) {
		struct mrvlietypes_failurecount *failcount;
		case __constant_cpu_to_le16(TLV_TYPE_FAILCOUNT):
			failcount = (void *)(response_buf + cmd_len);
			pos += snprintf(buf+pos, len-pos, "%d %d %d\n",
					failcount->failvalue,
					failcount->Failfreq,
					(event->events & cpu_to_le16(0x0004))?1:0);
		default:
			cmd_len += sizeof(struct mrvlietypes_failurecount);
			break;
		}
	}

	kfree(response_buf);
	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);
	return res;
}

static ssize_t libertas_failcount_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	ssize_t res, buf_size;
	int value, freq, subscribed, cmd_len;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	struct mrvlietypes_failurecount *failcount;
	void *response_buf;
	u16 event_bitmap;
	u8 *ptr;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%d %d %d", &value, &freq, &subscribed);
	if (res != 3) {
		res = -EFAULT;
		goto out_unlock;
	}

	event_bitmap = libertas_get_events_bitmap(priv);

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0)
		goto out_unlock;

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_set);
	pcmdptr->size = cpu_to_le16(S_DS_GEN +
		sizeof(struct cmd_ds_802_11_subscribe_event) +
		sizeof(struct mrvlietypes_failurecount));
	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	ptr = (u8*) pcmdptr+cmd_len;
	failcount = (struct mrvlietypes_failurecount *)(ptr);
	failcount->header.type = cpu_to_le16(TLV_TYPE_FAILCOUNT);
	failcount->header.len = cpu_to_le16(2);
	failcount->failvalue = value;
	failcount->Failfreq = freq;
	event_bitmap |= subscribed ? 0x0004 : 0x0;
	event->events = cpu_to_le16(event_bitmap);

	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = (struct cmd_ds_command *)response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_bcnmiss_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	void *response_buf;
	int res, cmd_len;
	ssize_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0) {
		free_page(addr);
		return res;
	}

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_get);
	pcmdptr->size = cpu_to_le16(sizeof(*event) + S_DS_GEN);
	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		free_page(addr);
		kfree(response_buf);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		free_page(addr);
		kfree(response_buf);
		return 0;
	}

	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	event = (void *)(response_buf + S_DS_GEN);
	while (cmd_len < le16_to_cpu(pcmdptr->size)) {
		struct mrvlietypesheader *header = (void *)(response_buf + cmd_len);
		switch (header->type) {
		struct mrvlietypes_beaconsmissed *bcnmiss;
		case __constant_cpu_to_le16(TLV_TYPE_BCNMISS):
			bcnmiss = (void *)(response_buf + cmd_len);
			pos += snprintf(buf+pos, len-pos, "%d N/A %d\n",
					bcnmiss->beaconmissed,
					(event->events & cpu_to_le16(0x0008))?1:0);
		default:
			cmd_len += sizeof(struct mrvlietypes_beaconsmissed);
			break;
		}
	}

	kfree(response_buf);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);
	return res;
}

static ssize_t libertas_bcnmiss_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	ssize_t res, buf_size;
	int value, freq, subscribed, cmd_len;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	struct mrvlietypes_beaconsmissed *bcnmiss;
	void *response_buf;
	u16 event_bitmap;
	u8 *ptr;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%d %d %d", &value, &freq, &subscribed);
	if (res != 3) {
		res = -EFAULT;
		goto out_unlock;
	}

	event_bitmap = libertas_get_events_bitmap(priv);

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0)
		goto out_unlock;

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_set);
	pcmdptr->size = cpu_to_le16(S_DS_GEN +
		sizeof(struct cmd_ds_802_11_subscribe_event) +
		sizeof(struct mrvlietypes_beaconsmissed));
	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	ptr = (u8*) pcmdptr+cmd_len;
	bcnmiss = (struct mrvlietypes_beaconsmissed *)(ptr);
	bcnmiss->header.type = cpu_to_le16(TLV_TYPE_BCNMISS);
	bcnmiss->header.len = cpu_to_le16(2);
	bcnmiss->beaconmissed = value;
	event_bitmap |= subscribed ? 0x0008 : 0x0;
	event->events = cpu_to_le16(event_bitmap);

	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		free_page(addr);
		kfree(response_buf);
		return 0;
	}

	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_highrssi_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	void *response_buf;
	int res, cmd_len;
	ssize_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0) {
		free_page(addr);
		return res;
	}

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_get);
	pcmdptr->size = cpu_to_le16(sizeof(*event) + S_DS_GEN);
	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	event = (void *)(response_buf + S_DS_GEN);
	while (cmd_len < le16_to_cpu(pcmdptr->size)) {
		struct mrvlietypesheader *header = (void *)(response_buf + cmd_len);
		switch (header->type) {
		struct mrvlietypes_rssithreshold  *Highrssi;
		case __constant_cpu_to_le16(TLV_TYPE_RSSI_HIGH):
			Highrssi = (void *)(response_buf + cmd_len);
			pos += snprintf(buf+pos, len-pos, "%d %d %d\n",
					Highrssi->rssivalue,
					Highrssi->rssifreq,
					(event->events & cpu_to_le16(0x0010))?1:0);
		default:
			cmd_len += sizeof(struct mrvlietypes_snrthreshold);
			break;
		}
	}

	kfree(response_buf);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);
	return res;
}

static ssize_t libertas_highrssi_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	ssize_t res, buf_size;
	int value, freq, subscribed, cmd_len;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	struct mrvlietypes_rssithreshold *rssi_threshold;
	void *response_buf;
	u16 event_bitmap;
	u8 *ptr;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%d %d %d", &value, &freq, &subscribed);
	if (res != 3) {
		res = -EFAULT;
		goto out_unlock;
	}

	event_bitmap = libertas_get_events_bitmap(priv);

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0)
		goto out_unlock;

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_set);
	pcmdptr->size = cpu_to_le16(S_DS_GEN +
		sizeof(struct cmd_ds_802_11_subscribe_event) +
		sizeof(struct mrvlietypes_rssithreshold));
	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	ptr = (u8*) pcmdptr+cmd_len;
	rssi_threshold = (struct mrvlietypes_rssithreshold *)(ptr);
	rssi_threshold->header.type = cpu_to_le16(TLV_TYPE_RSSI_HIGH);
	rssi_threshold->header.len = cpu_to_le16(2);
	rssi_threshold->rssivalue = value;
	rssi_threshold->rssifreq = freq;
	event_bitmap |= subscribed ? 0x0010 : 0x0;
	event->events = cpu_to_le16(event_bitmap);

	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		return 0;
	}

	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_highsnr_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	void *response_buf;
	int res, cmd_len;
	ssize_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0) {
		free_page(addr);
		return res;
	}

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_get);
	pcmdptr->size = cpu_to_le16(sizeof(*event) + S_DS_GEN);
	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	event = (void *)(response_buf + S_DS_GEN);
	while (cmd_len < le16_to_cpu(pcmdptr->size)) {
		struct mrvlietypesheader *header = (void *)(response_buf + cmd_len);
		switch (header->type) {
		struct mrvlietypes_snrthreshold *HighSnr;
		case __constant_cpu_to_le16(TLV_TYPE_SNR_HIGH):
			HighSnr = (void *)(response_buf + cmd_len);
			pos += snprintf(buf+pos, len-pos, "%d %d %d\n",
					HighSnr->snrvalue,
					HighSnr->snrfreq,
					(event->events & cpu_to_le16(0x0020))?1:0);
		default:
			cmd_len += sizeof(struct mrvlietypes_snrthreshold);
			break;
		}
	}

	kfree(response_buf);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);
	return res;
}

static ssize_t libertas_highsnr_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	ssize_t res, buf_size;
	int value, freq, subscribed, cmd_len;
	struct cmd_ctrl_node *pcmdnode;
	struct cmd_ds_command *pcmdptr;
	struct cmd_ds_802_11_subscribe_event *event;
	struct mrvlietypes_snrthreshold *snr_threshold;
	void *response_buf;
	u16 event_bitmap;
	u8 *ptr;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%d %d %d", &value, &freq, &subscribed);
	if (res != 3) {
		res = -EFAULT;
		goto out_unlock;
	}

	event_bitmap = libertas_get_events_bitmap(priv);

	res = libertas_event_initcmd(priv, &response_buf, &pcmdnode, &pcmdptr);
	if (res < 0)
		goto out_unlock;

	event = &pcmdptr->params.subscribe_event;
	event->action = cpu_to_le16(cmd_act_set);
	pcmdptr->size = cpu_to_le16(S_DS_GEN +
		sizeof(struct cmd_ds_802_11_subscribe_event) +
		sizeof(struct mrvlietypes_snrthreshold));
	cmd_len = S_DS_GEN + sizeof(struct cmd_ds_802_11_subscribe_event);
	ptr = (u8*) pcmdptr+cmd_len;
	snr_threshold = (struct mrvlietypes_snrthreshold *)(ptr);
	snr_threshold->header.type = cpu_to_le16(TLV_TYPE_SNR_HIGH);
	snr_threshold->header.len = cpu_to_le16(2);
	snr_threshold->snrvalue = value;
	snr_threshold->snrfreq = freq;
	event_bitmap |= subscribed ? 0x0020 : 0x0;
	event->events = cpu_to_le16(event_bitmap);

	libertas_queue_cmd(adapter, pcmdnode, 1);
	wake_up_interruptible(&priv->mainthread.waitq);

	/* Sleep until response is generated by FW */
	wait_event_interruptible(pcmdnode->cmdwait_q,
				 pcmdnode->cmdwaitqwoken);

	pcmdptr = response_buf;

	if (pcmdptr->result) {
		lbs_pr_err("%s: fail, result=%d\n", __func__,
			   le16_to_cpu(pcmdptr->result));
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	if (pcmdptr->command != cpu_to_le16(cmd_ret_802_11_subscribe_event)) {
		lbs_pr_err("command response incorrect!\n");
		kfree(response_buf);
		free_page(addr);
		return 0;
	}

	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_rdmac_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct wlan_offset_value offval;
	ssize_t pos = 0;
	int ret;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	offval.offset = priv->mac_offset;
	offval.value = 0;

	ret = libertas_prepare_and_send_command(priv,
				cmd_mac_reg_access, 0,
				cmd_option_waitforrsp, 0, &offval);
	mdelay(10);
	pos += snprintf(buf+pos, len-pos, "MAC[0x%x] = 0x%08x\n",
				priv->mac_offset, adapter->offsetvalue.value);

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);
	return ret;
}

static ssize_t libertas_rdmac_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	ssize_t res, buf_size;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	priv->mac_offset = simple_strtoul((char *)buf, NULL, 16);
	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_wrmac_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{

	wlan_private *priv = file->private_data;
	ssize_t res, buf_size;
	u32 offset, value;
	struct wlan_offset_value offval;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%x %x", &offset, &value);
	if (res != 2) {
		res = -EFAULT;
		goto out_unlock;
	}

	offval.offset = offset;
	offval.value = value;
	res = libertas_prepare_and_send_command(priv,
				cmd_mac_reg_access, 1,
				cmd_option_waitforrsp, 0, &offval);
	mdelay(10);

	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_rdbbp_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct wlan_offset_value offval;
	ssize_t pos = 0;
	int ret;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	offval.offset = priv->bbp_offset;
	offval.value = 0;

	ret = libertas_prepare_and_send_command(priv,
				cmd_bbp_reg_access, 0,
				cmd_option_waitforrsp, 0, &offval);
	mdelay(10);
	pos += snprintf(buf+pos, len-pos, "BBP[0x%x] = 0x%08x\n",
				priv->bbp_offset, adapter->offsetvalue.value);

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);

	return ret;
}

static ssize_t libertas_rdbbp_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	ssize_t res, buf_size;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	priv->bbp_offset = simple_strtoul((char *)buf, NULL, 16);
	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_wrbbp_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{

	wlan_private *priv = file->private_data;
	ssize_t res, buf_size;
	u32 offset, value;
	struct wlan_offset_value offval;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%x %x", &offset, &value);
	if (res != 2) {
		res = -EFAULT;
		goto out_unlock;
	}

	offval.offset = offset;
	offval.value = value;
	res = libertas_prepare_and_send_command(priv,
				cmd_bbp_reg_access, 1,
				cmd_option_waitforrsp, 0, &offval);
	mdelay(10);

	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_rdrf_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	wlan_adapter *adapter = priv->adapter;
	struct wlan_offset_value offval;
	ssize_t pos = 0;
	int ret;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	offval.offset = priv->rf_offset;
	offval.value = 0;

	ret = libertas_prepare_and_send_command(priv,
				cmd_rf_reg_access, 0,
				cmd_option_waitforrsp, 0, &offval);
	mdelay(10);
	pos += snprintf(buf+pos, len-pos, "RF[0x%x] = 0x%08x\n",
				priv->rf_offset, adapter->offsetvalue.value);

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	free_page(addr);

	return ret;
}

static ssize_t libertas_rdrf_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	wlan_private *priv = file->private_data;
	ssize_t res, buf_size;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	priv->rf_offset = simple_strtoul((char *)buf, NULL, 16);
	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t libertas_wrrf_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{

	wlan_private *priv = file->private_data;
	ssize_t res, buf_size;
	u32 offset, value;
	struct wlan_offset_value offval;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	res = sscanf(buf, "%x %x", &offset, &value);
	if (res != 2) {
		res = -EFAULT;
		goto out_unlock;
	}

	offval.offset = offset;
	offval.value = value;
	res = libertas_prepare_and_send_command(priv,
				cmd_rf_reg_access, 1,
				cmd_option_waitforrsp, 0, &offval);
	mdelay(10);

	res = count;
out_unlock:
	free_page(addr);
	return res;
}

#define FOPS(fread, fwrite) { \
	.owner = THIS_MODULE, \
	.open = open_file_generic, \
	.read = (fread), \
	.write = (fwrite), \
}

struct libertas_debugfs_files {
	char *name;
	int perm;
	struct file_operations fops;
};

static struct libertas_debugfs_files debugfs_files[] = {
	{ "info", 0444, FOPS(libertas_dev_info, write_file_dummy), },
	{ "getscantable", 0444, FOPS(libertas_getscantable,
					write_file_dummy), },
	{ "sleepparams", 0644, FOPS(libertas_sleepparams_read,
				libertas_sleepparams_write), },
	{ "extscan", 0600, FOPS(NULL, libertas_extscan), },
	{ "setuserscan", 0600, FOPS(NULL, libertas_setuserscan), },
};

static struct libertas_debugfs_files debugfs_events_files[] = {
	{"low_rssi", 0644, FOPS(libertas_lowrssi_read,
				libertas_lowrssi_write), },
	{"low_snr", 0644, FOPS(libertas_lowsnr_read,
				libertas_lowsnr_write), },
	{"failure_count", 0644, FOPS(libertas_failcount_read,
				libertas_failcount_write), },
	{"beacon_missed", 0644, FOPS(libertas_bcnmiss_read,
				libertas_bcnmiss_write), },
	{"high_rssi", 0644, FOPS(libertas_highrssi_read,
				libertas_highrssi_write), },
	{"high_snr", 0644, FOPS(libertas_highsnr_read,
				libertas_highsnr_write), },
};

static struct libertas_debugfs_files debugfs_regs_files[] = {
	{"rdmac", 0644, FOPS(libertas_rdmac_read, libertas_rdmac_write), },
	{"wrmac", 0600, FOPS(NULL, libertas_wrmac_write), },
	{"rdbbp", 0644, FOPS(libertas_rdbbp_read, libertas_rdbbp_write), },
	{"wrbbp", 0600, FOPS(NULL, libertas_wrbbp_write), },
	{"rdrf", 0644, FOPS(libertas_rdrf_read, libertas_rdrf_write), },
	{"wrrf", 0600, FOPS(NULL, libertas_wrrf_write), },
};

void libertas_debugfs_init(void)
{
	if (!libertas_dir)
		libertas_dir = debugfs_create_dir("libertas_wireless", NULL);

	return;
}

void libertas_debugfs_remove(void)
{
	if (libertas_dir)
		 debugfs_remove(libertas_dir);
	return;
}

void libertas_debugfs_init_one(wlan_private *priv, struct net_device *dev)
{
	int i;
	struct libertas_debugfs_files *files;
	if (!libertas_dir)
		goto exit;

	priv->debugfs_dir = debugfs_create_dir(dev->name, libertas_dir);
	if (!priv->debugfs_dir)
		goto exit;

	for (i=0; i<ARRAY_SIZE(debugfs_files); i++) {
		files = &debugfs_files[i];
		priv->debugfs_files[i] = debugfs_create_file(files->name,
							     files->perm,
							     priv->debugfs_dir,
							     priv,
							     &files->fops);
	}

	priv->events_dir = debugfs_create_dir("subscribed_events", priv->debugfs_dir);
	if (!priv->events_dir)
		goto exit;

	for (i=0; i<ARRAY_SIZE(debugfs_events_files); i++) {
		files = &debugfs_events_files[i];
		priv->debugfs_events_files[i] = debugfs_create_file(files->name,
							     files->perm,
							     priv->events_dir,
							     priv,
							     &files->fops);
	}

	priv->regs_dir = debugfs_create_dir("registers", priv->debugfs_dir);
	if (!priv->regs_dir)
		goto exit;

	for (i=0; i<ARRAY_SIZE(debugfs_regs_files); i++) {
		files = &debugfs_regs_files[i];
		priv->debugfs_regs_files[i] = debugfs_create_file(files->name,
							     files->perm,
							     priv->regs_dir,
							     priv,
							     &files->fops);
	}

#ifdef PROC_DEBUG
	libertas_debug_init(priv, dev);
#endif
exit:
	return;
}

void libertas_debugfs_remove_one(wlan_private *priv)
{
	int i;

	for(i=0; i<ARRAY_SIZE(debugfs_regs_files); i++)
		debugfs_remove(priv->debugfs_regs_files[i]);

	debugfs_remove(priv->regs_dir);

	for(i=0; i<ARRAY_SIZE(debugfs_events_files); i++)
		debugfs_remove(priv->debugfs_events_files[i]);

	debugfs_remove(priv->events_dir);
#ifdef PROC_DEBUG
	debugfs_remove(priv->debugfs_debug);
#endif
	for(i=0; i<ARRAY_SIZE(debugfs_files); i++)
		debugfs_remove(priv->debugfs_files[i]);
	debugfs_remove(priv->debugfs_dir);
}



/* debug entry */

#ifdef PROC_DEBUG

#define item_size(n)	(FIELD_SIZEOF(wlan_adapter, n))
#define item_addr(n)	(offsetof(wlan_adapter, n))


struct debug_data {
	char name[32];
	u32 size;
	size_t addr;
};

/* To debug any member of wlan_adapter, simply add one line here.
 */
static struct debug_data items[] = {
	{"intcounter", item_size(intcounter), item_addr(intcounter)},
	{"psmode", item_size(psmode), item_addr(psmode)},
	{"psstate", item_size(psstate), item_addr(psstate)},
};

static int num_of_items = ARRAY_SIZE(items);

/**
 *  @brief proc read function
 *
 *  @param page	   pointer to buffer
 *  @param s       read data starting position
 *  @param off     offset
 *  @param cnt     counter
 *  @param eof     end of file flag
 *  @param data    data to output
 *  @return 	   number of output data
 */
static ssize_t wlan_debugfs_read(struct file *file, char __user *userbuf,
			size_t count, loff_t *ppos)
{
	int val = 0;
	size_t pos = 0;
	ssize_t res;
	char *p;
	int i;
	struct debug_data *d;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;

	p = buf;

	d = (struct debug_data *)file->private_data;

	for (i = 0; i < num_of_items; i++) {
		if (d[i].size == 1)
			val = *((u8 *) d[i].addr);
		else if (d[i].size == 2)
			val = *((u16 *) d[i].addr);
		else if (d[i].size == 4)
			val = *((u32 *) d[i].addr);
		else if (d[i].size == 8)
			val = *((u64 *) d[i].addr);

		pos += sprintf(p + pos, "%s=%d\n", d[i].name, val);
	}

	res = simple_read_from_buffer(userbuf, count, ppos, p, pos);

	free_page(addr);
	return res;
}

/**
 *  @brief proc write function
 *
 *  @param f	   file pointer
 *  @param buf     pointer to data buffer
 *  @param cnt     data number to write
 *  @param data    data to write
 *  @return 	   number of data
 */
static ssize_t wlan_debugfs_write(struct file *f, const char __user *buf,
			    size_t cnt, loff_t *ppos)
{
	int r, i;
	char *pdata;
	char *p;
	char *p0;
	char *p1;
	char *p2;
	struct debug_data *d = (struct debug_data *)f->private_data;

	pdata = (char *)kmalloc(cnt, GFP_KERNEL);
	if (pdata == NULL)
		return 0;

	if (copy_from_user(pdata, buf, cnt)) {
		lbs_deb_debugfs("Copy from user failed\n");
		kfree(pdata);
		return 0;
	}

	p0 = pdata;
	for (i = 0; i < num_of_items; i++) {
		do {
			p = strstr(p0, d[i].name);
			if (p == NULL)
				break;
			p1 = strchr(p, '\n');
			if (p1 == NULL)
				break;
			p0 = p1++;
			p2 = strchr(p, '=');
			if (!p2)
				break;
			p2++;
			r = simple_strtoul(p2, NULL, 0);
			if (d[i].size == 1)
				*((u8 *) d[i].addr) = (u8) r;
			else if (d[i].size == 2)
				*((u16 *) d[i].addr) = (u16) r;
			else if (d[i].size == 4)
				*((u32 *) d[i].addr) = (u32) r;
			else if (d[i].size == 8)
				*((u64 *) d[i].addr) = (u64) r;
			break;
		} while (1);
	}
	kfree(pdata);

	return (ssize_t)cnt;
}

static struct file_operations libertas_debug_fops = {
	.owner = THIS_MODULE,
	.open = open_file_generic,
	.write = wlan_debugfs_write,
	.read = wlan_debugfs_read,
};

/**
 *  @brief create debug proc file
 *
 *  @param priv	   pointer wlan_private
 *  @param dev     pointer net_device
 *  @return 	   N/A
 */
static void libertas_debug_init(wlan_private * priv, struct net_device *dev)
{
	int i;

	if (!priv->debugfs_dir)
		return;

	for (i = 0; i < num_of_items; i++)
		items[i].addr += (size_t) priv->adapter;

	priv->debugfs_debug = debugfs_create_file("debug", 0644,
						  priv->debugfs_dir, &items[0],
						  &libertas_debug_fops);
}
#endif

