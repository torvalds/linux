/*
 * Marvell Wireless LAN device driver: debugfs
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include <linux/debugfs.h>

#include "main.h"
#include "11n.h"


static struct dentry *mwifiex_dfs_dir;

static char *bss_modes[] = {
	"UNSPECIFIED",
	"ADHOC",
	"STATION",
	"AP",
	"AP_VLAN",
	"WDS",
	"MONITOR",
	"MESH_POINT",
	"P2P_CLIENT",
	"P2P_GO",
	"P2P_DEVICE",
};

/* size/addr for mwifiex_debug_info */
#define item_size(n)		(FIELD_SIZEOF(struct mwifiex_debug_info, n))
#define item_addr(n)		(offsetof(struct mwifiex_debug_info, n))

/* size/addr for struct mwifiex_adapter */
#define adapter_item_size(n)	(FIELD_SIZEOF(struct mwifiex_adapter, n))
#define adapter_item_addr(n)	(offsetof(struct mwifiex_adapter, n))

struct mwifiex_debug_data {
	char name[32];		/* variable/array name */
	u32 size;		/* size of the variable/array */
	size_t addr;		/* address of the variable/array */
	int num;		/* number of variables in an array */
};

static struct mwifiex_debug_data items[] = {
	{"int_counter", item_size(int_counter),
	 item_addr(int_counter), 1},
	{"wmm_ac_vo", item_size(packets_out[WMM_AC_VO]),
	 item_addr(packets_out[WMM_AC_VO]), 1},
	{"wmm_ac_vi", item_size(packets_out[WMM_AC_VI]),
	 item_addr(packets_out[WMM_AC_VI]), 1},
	{"wmm_ac_be", item_size(packets_out[WMM_AC_BE]),
	 item_addr(packets_out[WMM_AC_BE]), 1},
	{"wmm_ac_bk", item_size(packets_out[WMM_AC_BK]),
	 item_addr(packets_out[WMM_AC_BK]), 1},
	{"tx_buf_size", item_size(tx_buf_size),
	 item_addr(tx_buf_size), 1},
	{"curr_tx_buf_size", item_size(curr_tx_buf_size),
	 item_addr(curr_tx_buf_size), 1},
	{"ps_mode", item_size(ps_mode),
	 item_addr(ps_mode), 1},
	{"ps_state", item_size(ps_state),
	 item_addr(ps_state), 1},
	{"is_deep_sleep", item_size(is_deep_sleep),
	 item_addr(is_deep_sleep), 1},
	{"wakeup_dev_req", item_size(pm_wakeup_card_req),
	 item_addr(pm_wakeup_card_req), 1},
	{"wakeup_tries", item_size(pm_wakeup_fw_try),
	 item_addr(pm_wakeup_fw_try), 1},
	{"hs_configured", item_size(is_hs_configured),
	 item_addr(is_hs_configured), 1},
	{"hs_activated", item_size(hs_activated),
	 item_addr(hs_activated), 1},
	{"num_tx_timeout", item_size(num_tx_timeout),
	 item_addr(num_tx_timeout), 1},
	{"is_cmd_timedout", item_size(is_cmd_timedout),
	 item_addr(is_cmd_timedout), 1},
	{"timeout_cmd_id", item_size(timeout_cmd_id),
	 item_addr(timeout_cmd_id), 1},
	{"timeout_cmd_act", item_size(timeout_cmd_act),
	 item_addr(timeout_cmd_act), 1},
	{"last_cmd_id", item_size(last_cmd_id),
	 item_addr(last_cmd_id), DBG_CMD_NUM},
	{"last_cmd_act", item_size(last_cmd_act),
	 item_addr(last_cmd_act), DBG_CMD_NUM},
	{"last_cmd_index", item_size(last_cmd_index),
	 item_addr(last_cmd_index), 1},
	{"last_cmd_resp_id", item_size(last_cmd_resp_id),
	 item_addr(last_cmd_resp_id), DBG_CMD_NUM},
	{"last_cmd_resp_index", item_size(last_cmd_resp_index),
	 item_addr(last_cmd_resp_index), 1},
	{"last_event", item_size(last_event),
	 item_addr(last_event), DBG_CMD_NUM},
	{"last_event_index", item_size(last_event_index),
	 item_addr(last_event_index), 1},
	{"num_cmd_h2c_fail", item_size(num_cmd_host_to_card_failure),
	 item_addr(num_cmd_host_to_card_failure), 1},
	{"num_cmd_sleep_cfm_fail",
	 item_size(num_cmd_sleep_cfm_host_to_card_failure),
	 item_addr(num_cmd_sleep_cfm_host_to_card_failure), 1},
	{"num_tx_h2c_fail", item_size(num_tx_host_to_card_failure),
	 item_addr(num_tx_host_to_card_failure), 1},
	{"num_evt_deauth", item_size(num_event_deauth),
	 item_addr(num_event_deauth), 1},
	{"num_evt_disassoc", item_size(num_event_disassoc),
	 item_addr(num_event_disassoc), 1},
	{"num_evt_link_lost", item_size(num_event_link_lost),
	 item_addr(num_event_link_lost), 1},
	{"num_cmd_deauth", item_size(num_cmd_deauth),
	 item_addr(num_cmd_deauth), 1},
	{"num_cmd_assoc_ok", item_size(num_cmd_assoc_success),
	 item_addr(num_cmd_assoc_success), 1},
	{"num_cmd_assoc_fail", item_size(num_cmd_assoc_failure),
	 item_addr(num_cmd_assoc_failure), 1},
	{"cmd_sent", item_size(cmd_sent),
	 item_addr(cmd_sent), 1},
	{"data_sent", item_size(data_sent),
	 item_addr(data_sent), 1},
	{"cmd_resp_received", item_size(cmd_resp_received),
	 item_addr(cmd_resp_received), 1},
	{"event_received", item_size(event_received),
	 item_addr(event_received), 1},

	/* variables defined in struct mwifiex_adapter */
	{"cmd_pending", adapter_item_size(cmd_pending),
	 adapter_item_addr(cmd_pending), 1},
	{"tx_pending", adapter_item_size(tx_pending),
	 adapter_item_addr(tx_pending), 1},
	{"rx_pending", adapter_item_size(rx_pending),
	 adapter_item_addr(rx_pending), 1},
};

static int num_of_items = ARRAY_SIZE(items);

/*
 * Proc info file read handler.
 *
 * This function is called when the 'info' file is opened for reading.
 * It prints the following driver related information -
 *      - Driver name
 *      - Driver version
 *      - Driver extended version
 *      - Interface name
 *      - BSS mode
 *      - Media state (connected or disconnected)
 *      - MAC address
 *      - Total number of Tx bytes
 *      - Total number of Rx bytes
 *      - Total number of Tx packets
 *      - Total number of Rx packets
 *      - Total number of dropped Tx packets
 *      - Total number of dropped Rx packets
 *      - Total number of corrupted Tx packets
 *      - Total number of corrupted Rx packets
 *      - Carrier status (on or off)
 *      - Tx queue status (started or stopped)
 *
 * For STA mode drivers, it also prints the following extra -
 *      - ESSID
 *      - BSSID
 *      - Channel
 *      - Region code
 *      - Multicast count
 *      - Multicast addresses
 */
static ssize_t
mwifiex_info_read(struct file *file, char __user *ubuf,
		  size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv =
		(struct mwifiex_private *) file->private_data;
	struct net_device *netdev = priv->netdev;
	struct netdev_hw_addr *ha;
	struct netdev_queue *txq;
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	char *p = (char *) page, fmt[64];
	struct mwifiex_bss_info info;
	ssize_t ret;
	int i = 0;

	if (!p)
		return -ENOMEM;

	memset(&info, 0, sizeof(info));
	ret = mwifiex_get_bss_info(priv, &info);
	if (ret)
		goto free_and_exit;

	mwifiex_drv_get_driver_version(priv->adapter, fmt, sizeof(fmt) - 1);

	if (!priv->version_str[0])
		mwifiex_get_ver_ext(priv);

	p += sprintf(p, "driver_name = " "\"mwifiex\"\n");
	p += sprintf(p, "driver_version = %s", fmt);
	p += sprintf(p, "\nverext = %s", priv->version_str);
	p += sprintf(p, "\ninterface_name=\"%s\"\n", netdev->name);

	if (info.bss_mode >= ARRAY_SIZE(bss_modes))
		p += sprintf(p, "bss_mode=\"%d\"\n", info.bss_mode);
	else
		p += sprintf(p, "bss_mode=\"%s\"\n", bss_modes[info.bss_mode]);

	p += sprintf(p, "media_state=\"%s\"\n",
		     (!priv->media_connected ? "Disconnected" : "Connected"));
	p += sprintf(p, "mac_address=\"%pM\"\n", netdev->dev_addr);

	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA) {
		p += sprintf(p, "multicast_count=\"%d\"\n",
			     netdev_mc_count(netdev));
		p += sprintf(p, "essid=\"%s\"\n", info.ssid.ssid);
		p += sprintf(p, "bssid=\"%pM\"\n", info.bssid);
		p += sprintf(p, "channel=\"%d\"\n", (int) info.bss_chan);
		p += sprintf(p, "country_code = \"%s\"\n", info.country_code);

		netdev_for_each_mc_addr(ha, netdev)
			p += sprintf(p, "multicast_address[%d]=\"%pM\"\n",
					i++, ha->addr);
	}

	p += sprintf(p, "num_tx_bytes = %lu\n", priv->stats.tx_bytes);
	p += sprintf(p, "num_rx_bytes = %lu\n", priv->stats.rx_bytes);
	p += sprintf(p, "num_tx_pkts = %lu\n", priv->stats.tx_packets);
	p += sprintf(p, "num_rx_pkts = %lu\n", priv->stats.rx_packets);
	p += sprintf(p, "num_tx_pkts_dropped = %lu\n", priv->stats.tx_dropped);
	p += sprintf(p, "num_rx_pkts_dropped = %lu\n", priv->stats.rx_dropped);
	p += sprintf(p, "num_tx_pkts_err = %lu\n", priv->stats.tx_errors);
	p += sprintf(p, "num_rx_pkts_err = %lu\n", priv->stats.rx_errors);
	p += sprintf(p, "carrier %s\n", ((netif_carrier_ok(priv->netdev))
					 ? "on" : "off"));
	p += sprintf(p, "tx queue");
	for (i = 0; i < netdev->num_tx_queues; i++) {
		txq = netdev_get_tx_queue(netdev, i);
		p += sprintf(p, " %d:%s", i, netif_tx_queue_stopped(txq) ?
			     "stopped" : "started");
	}
	p += sprintf(p, "\n");

	ret = simple_read_from_buffer(ubuf, count, ppos, (char *) page,
				      (unsigned long) p - page);

free_and_exit:
	free_page(page);
	return ret;
}

/*
 * Proc firmware dump read handler.
 *
 * This function is called when the 'fw_dump' file is opened for
 * reading.
 * This function dumps firmware memory in different files
 * (ex. DTCM, ITCM, SQRAM etc.) based on the the segments for
 * debugging.
 */
static ssize_t
mwifiex_fw_dump_read(struct file *file, char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv = file->private_data;

	if (!priv->adapter->if_ops.fw_dump)
		return -EIO;

	priv->adapter->if_ops.fw_dump(priv->adapter);

	return 0;
}

/*
 * Proc getlog file read handler.
 *
 * This function is called when the 'getlog' file is opened for reading
 * It prints the following log information -
 *      - Number of multicast Tx frames
 *      - Number of failed packets
 *      - Number of Tx retries
 *      - Number of multicast Tx retries
 *      - Number of duplicate frames
 *      - Number of RTS successes
 *      - Number of RTS failures
 *      - Number of ACK failures
 *      - Number of fragmented Rx frames
 *      - Number of multicast Rx frames
 *      - Number of FCS errors
 *      - Number of Tx frames
 *      - WEP ICV error counts
 */
static ssize_t
mwifiex_getlog_read(struct file *file, char __user *ubuf,
		    size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv =
		(struct mwifiex_private *) file->private_data;
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	char *p = (char *) page;
	ssize_t ret;
	struct mwifiex_ds_get_stats stats;

	if (!p)
		return -ENOMEM;

	memset(&stats, 0, sizeof(stats));
	ret = mwifiex_get_stats_info(priv, &stats);
	if (ret)
		goto free_and_exit;

	p += sprintf(p, "\n"
		     "mcasttxframe     %u\n"
		     "failed           %u\n"
		     "retry            %u\n"
		     "multiretry       %u\n"
		     "framedup         %u\n"
		     "rtssuccess       %u\n"
		     "rtsfailure       %u\n"
		     "ackfailure       %u\n"
		     "rxfrag           %u\n"
		     "mcastrxframe     %u\n"
		     "fcserror         %u\n"
		     "txframe          %u\n"
		     "wepicverrcnt-1   %u\n"
		     "wepicverrcnt-2   %u\n"
		     "wepicverrcnt-3   %u\n"
		     "wepicverrcnt-4   %u\n",
		     stats.mcast_tx_frame,
		     stats.failed,
		     stats.retry,
		     stats.multi_retry,
		     stats.frame_dup,
		     stats.rts_success,
		     stats.rts_failure,
		     stats.ack_failure,
		     stats.rx_frag,
		     stats.mcast_rx_frame,
		     stats.fcs_error,
		     stats.tx_frame,
		     stats.wep_icv_error[0],
		     stats.wep_icv_error[1],
		     stats.wep_icv_error[2],
		     stats.wep_icv_error[3]);


	ret = simple_read_from_buffer(ubuf, count, ppos, (char *) page,
				      (unsigned long) p - page);

free_and_exit:
	free_page(page);
	return ret;
}

static struct mwifiex_debug_info info;

/*
 * Proc debug file read handler.
 *
 * This function is called when the 'debug' file is opened for reading
 * It prints the following log information -
 *      - Interrupt count
 *      - WMM AC VO packets count
 *      - WMM AC VI packets count
 *      - WMM AC BE packets count
 *      - WMM AC BK packets count
 *      - Maximum Tx buffer size
 *      - Tx buffer size
 *      - Current Tx buffer size
 *      - Power Save mode
 *      - Power Save state
 *      - Deep Sleep status
 *      - Device wakeup required status
 *      - Number of wakeup tries
 *      - Host Sleep configured status
 *      - Host Sleep activated status
 *      - Number of Tx timeouts
 *      - Number of command timeouts
 *      - Last timed out command ID
 *      - Last timed out command action
 *      - Last command ID
 *      - Last command action
 *      - Last command index
 *      - Last command response ID
 *      - Last command response index
 *      - Last event
 *      - Last event index
 *      - Number of host to card command failures
 *      - Number of sleep confirm command failures
 *      - Number of host to card data failure
 *      - Number of deauthentication events
 *      - Number of disassociation events
 *      - Number of link lost events
 *      - Number of deauthentication commands
 *      - Number of association success commands
 *      - Number of association failure commands
 *      - Number of commands sent
 *      - Number of data packets sent
 *      - Number of command responses received
 *      - Number of events received
 *      - Tx BA stream table (TID, RA)
 *      - Rx reorder table (TID, TA, Start window, Window size, Buffer)
 */
static ssize_t
mwifiex_debug_read(struct file *file, char __user *ubuf,
		   size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv =
		(struct mwifiex_private *) file->private_data;
	struct mwifiex_debug_data *d = &items[0];
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	char *p = (char *) page;
	ssize_t ret;
	size_t size, addr;
	long val;
	int i, j;

	if (!p)
		return -ENOMEM;

	ret = mwifiex_get_debug_info(priv, &info);
	if (ret)
		goto free_and_exit;

	for (i = 0; i < num_of_items; i++) {
		p += sprintf(p, "%s=", d[i].name);

		size = d[i].size / d[i].num;

		if (i < (num_of_items - 3))
			addr = d[i].addr + (size_t) &info;
		else /* The last 3 items are struct mwifiex_adapter variables */
			addr = d[i].addr + (size_t) priv->adapter;

		for (j = 0; j < d[i].num; j++) {
			switch (size) {
			case 1:
				val = *((u8 *) addr);
				break;
			case 2:
				val = *((u16 *) addr);
				break;
			case 4:
				val = *((u32 *) addr);
				break;
			case 8:
				val = *((long long *) addr);
				break;
			default:
				val = -1;
				break;
			}

			p += sprintf(p, "%#lx ", val);
			addr += size;
		}

		p += sprintf(p, "\n");
	}

	if (info.tx_tbl_num) {
		p += sprintf(p, "Tx BA stream table:\n");
		for (i = 0; i < info.tx_tbl_num; i++)
			p += sprintf(p, "tid = %d, ra = %pM\n",
				     info.tx_tbl[i].tid, info.tx_tbl[i].ra);
	}

	if (info.rx_tbl_num) {
		p += sprintf(p, "Rx reorder table:\n");
		for (i = 0; i < info.rx_tbl_num; i++) {
			p += sprintf(p, "tid = %d, ta = %pM, "
				     "start_win = %d, "
				     "win_size = %d, buffer: ",
				     info.rx_tbl[i].tid,
				     info.rx_tbl[i].ta,
				     info.rx_tbl[i].start_win,
				     info.rx_tbl[i].win_size);

			for (j = 0; j < info.rx_tbl[i].win_size; j++)
				p += sprintf(p, "%c ",
					     info.rx_tbl[i].buffer[j] ?
					     '1' : '0');

			p += sprintf(p, "\n");
		}
	}

	ret = simple_read_from_buffer(ubuf, count, ppos, (char *) page,
				      (unsigned long) p - page);

free_and_exit:
	free_page(page);
	return ret;
}

static u32 saved_reg_type, saved_reg_offset, saved_reg_value;

/*
 * Proc regrdwr file write handler.
 *
 * This function is called when the 'regrdwr' file is opened for writing
 *
 * This function can be used to write to a register.
 */
static ssize_t
mwifiex_regrdwr_write(struct file *file,
		      const char __user *ubuf, size_t count, loff_t *ppos)
{
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *) addr;
	size_t buf_size = min_t(size_t, count, PAGE_SIZE - 1);
	int ret;
	u32 reg_type = 0, reg_offset = 0, reg_value = UINT_MAX;

	if (!buf)
		return -ENOMEM;


	if (copy_from_user(buf, ubuf, buf_size)) {
		ret = -EFAULT;
		goto done;
	}

	sscanf(buf, "%u %x %x", &reg_type, &reg_offset, &reg_value);

	if (reg_type == 0 || reg_offset == 0) {
		ret = -EINVAL;
		goto done;
	} else {
		saved_reg_type = reg_type;
		saved_reg_offset = reg_offset;
		saved_reg_value = reg_value;
		ret = count;
	}
done:
	free_page(addr);
	return ret;
}

/*
 * Proc regrdwr file read handler.
 *
 * This function is called when the 'regrdwr' file is opened for reading
 *
 * This function can be used to read from a register.
 */
static ssize_t
mwifiex_regrdwr_read(struct file *file, char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv =
		(struct mwifiex_private *) file->private_data;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *) addr;
	int pos = 0, ret = 0;
	u32 reg_value;

	if (!buf)
		return -ENOMEM;

	if (!saved_reg_type) {
		/* No command has been given */
		pos += snprintf(buf, PAGE_SIZE, "0");
		goto done;
	}
	/* Set command has been given */
	if (saved_reg_value != UINT_MAX) {
		ret = mwifiex_reg_write(priv, saved_reg_type, saved_reg_offset,
					saved_reg_value);

		pos += snprintf(buf, PAGE_SIZE, "%u 0x%x 0x%x\n",
				saved_reg_type, saved_reg_offset,
				saved_reg_value);

		ret = simple_read_from_buffer(ubuf, count, ppos, buf, pos);

		goto done;
	}
	/* Get command has been given */
	ret = mwifiex_reg_read(priv, saved_reg_type,
			       saved_reg_offset, &reg_value);
	if (ret) {
		ret = -EINVAL;
		goto done;
	}

	pos += snprintf(buf, PAGE_SIZE, "%u 0x%x 0x%x\n", saved_reg_type,
			saved_reg_offset, reg_value);

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, pos);

done:
	free_page(addr);
	return ret;
}

static u32 saved_offset = -1, saved_bytes = -1;

/*
 * Proc rdeeprom file write handler.
 *
 * This function is called when the 'rdeeprom' file is opened for writing
 *
 * This function can be used to write to a RDEEPROM location.
 */
static ssize_t
mwifiex_rdeeprom_write(struct file *file,
		       const char __user *ubuf, size_t count, loff_t *ppos)
{
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *) addr;
	size_t buf_size = min_t(size_t, count, PAGE_SIZE - 1);
	int ret = 0;
	int offset = -1, bytes = -1;

	if (!buf)
		return -ENOMEM;


	if (copy_from_user(buf, ubuf, buf_size)) {
		ret = -EFAULT;
		goto done;
	}

	sscanf(buf, "%d %d", &offset, &bytes);

	if (offset == -1 || bytes == -1) {
		ret = -EINVAL;
		goto done;
	} else {
		saved_offset = offset;
		saved_bytes = bytes;
		ret = count;
	}
done:
	free_page(addr);
	return ret;
}

/*
 * Proc rdeeprom read write handler.
 *
 * This function is called when the 'rdeeprom' file is opened for reading
 *
 * This function can be used to read from a RDEEPROM location.
 */
static ssize_t
mwifiex_rdeeprom_read(struct file *file, char __user *ubuf,
		      size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv =
		(struct mwifiex_private *) file->private_data;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *) addr;
	int pos = 0, ret = 0, i;
	u8 value[MAX_EEPROM_DATA];

	if (!buf)
		return -ENOMEM;

	if (saved_offset == -1) {
		/* No command has been given */
		pos += snprintf(buf, PAGE_SIZE, "0");
		goto done;
	}

	/* Get command has been given */
	ret = mwifiex_eeprom_read(priv, (u16) saved_offset,
				  (u16) saved_bytes, value);
	if (ret) {
		ret = -EINVAL;
		goto done;
	}

	pos += snprintf(buf, PAGE_SIZE, "%d %d ", saved_offset, saved_bytes);

	for (i = 0; i < saved_bytes; i++)
		pos += snprintf(buf + strlen(buf), PAGE_SIZE, "%d ", value[i]);

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, pos);

done:
	free_page(addr);
	return ret;
}

/* Proc hscfg file write handler
 * This function can be used to configure the host sleep parameters.
 */
static ssize_t
mwifiex_hscfg_write(struct file *file, const char __user *ubuf,
		    size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv = (void *)file->private_data;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	size_t buf_size = min_t(size_t, count, PAGE_SIZE - 1);
	int ret, arg_num;
	struct mwifiex_ds_hs_cfg hscfg;
	int conditions = HS_CFG_COND_DEF;
	u32 gpio = HS_CFG_GPIO_DEF, gap = HS_CFG_GAP_DEF;

	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, buf_size)) {
		ret = -EFAULT;
		goto done;
	}

	arg_num = sscanf(buf, "%d %x %x", &conditions, &gpio, &gap);

	memset(&hscfg, 0, sizeof(struct mwifiex_ds_hs_cfg));

	if (arg_num > 3) {
		dev_err(priv->adapter->dev, "Too many arguments\n");
		ret = -EINVAL;
		goto done;
	}

	if (arg_num >= 1 && arg_num < 3)
		mwifiex_set_hs_params(priv, HostCmd_ACT_GEN_GET,
				      MWIFIEX_SYNC_CMD, &hscfg);

	if (arg_num) {
		if (conditions == HS_CFG_CANCEL) {
			mwifiex_cancel_hs(priv, MWIFIEX_ASYNC_CMD);
			ret = count;
			goto done;
		}
		hscfg.conditions = conditions;
	}
	if (arg_num >= 2)
		hscfg.gpio = gpio;
	if (arg_num == 3)
		hscfg.gap = gap;

	hscfg.is_invoke_hostcmd = false;
	mwifiex_set_hs_params(priv, HostCmd_ACT_GEN_SET,
			      MWIFIEX_SYNC_CMD, &hscfg);

	mwifiex_enable_hs(priv->adapter);
	priv->adapter->hs_enabling = false;
	ret = count;
done:
	free_page(addr);
	return ret;
}

/* Proc hscfg file read handler
 * This function can be used to read host sleep configuration
 * parameters from driver.
 */
static ssize_t
mwifiex_hscfg_read(struct file *file, char __user *ubuf,
		   size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv = (void *)file->private_data;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	int pos, ret;
	struct mwifiex_ds_hs_cfg hscfg;

	if (!buf)
		return -ENOMEM;

	mwifiex_set_hs_params(priv, HostCmd_ACT_GEN_GET,
			      MWIFIEX_SYNC_CMD, &hscfg);

	pos = snprintf(buf, PAGE_SIZE, "%u 0x%x 0x%x\n", hscfg.conditions,
		       hscfg.gpio, hscfg.gap);

	ret = simple_read_from_buffer(ubuf, count, ppos, buf, pos);

	free_page(addr);
	return ret;
}

#define MWIFIEX_DFS_ADD_FILE(name) do {                                 \
	if (!debugfs_create_file(#name, 0644, priv->dfs_dev_dir,        \
			priv, &mwifiex_dfs_##name##_fops))              \
		return;                                                 \
} while (0);

#define MWIFIEX_DFS_FILE_OPS(name)                                      \
static const struct file_operations mwifiex_dfs_##name##_fops = {       \
	.read = mwifiex_##name##_read,                                  \
	.write = mwifiex_##name##_write,                                \
	.open = simple_open,                                            \
};

#define MWIFIEX_DFS_FILE_READ_OPS(name)                                 \
static const struct file_operations mwifiex_dfs_##name##_fops = {       \
	.read = mwifiex_##name##_read,                                  \
	.open = simple_open,                                            \
};

#define MWIFIEX_DFS_FILE_WRITE_OPS(name)                                \
static const struct file_operations mwifiex_dfs_##name##_fops = {       \
	.write = mwifiex_##name##_write,                                \
	.open = simple_open,                                            \
};


MWIFIEX_DFS_FILE_READ_OPS(info);
MWIFIEX_DFS_FILE_READ_OPS(debug);
MWIFIEX_DFS_FILE_READ_OPS(getlog);
MWIFIEX_DFS_FILE_READ_OPS(fw_dump);
MWIFIEX_DFS_FILE_OPS(regrdwr);
MWIFIEX_DFS_FILE_OPS(rdeeprom);
MWIFIEX_DFS_FILE_OPS(hscfg);

/*
 * This function creates the debug FS directory structure and the files.
 */
void
mwifiex_dev_debugfs_init(struct mwifiex_private *priv)
{
	if (!mwifiex_dfs_dir || !priv)
		return;

	priv->dfs_dev_dir = debugfs_create_dir(priv->netdev->name,
					       mwifiex_dfs_dir);

	if (!priv->dfs_dev_dir)
		return;

	MWIFIEX_DFS_ADD_FILE(info);
	MWIFIEX_DFS_ADD_FILE(debug);
	MWIFIEX_DFS_ADD_FILE(getlog);
	MWIFIEX_DFS_ADD_FILE(regrdwr);
	MWIFIEX_DFS_ADD_FILE(rdeeprom);
	MWIFIEX_DFS_ADD_FILE(fw_dump);
	MWIFIEX_DFS_ADD_FILE(hscfg);
}

/*
 * This function removes the debug FS directory structure and the files.
 */
void
mwifiex_dev_debugfs_remove(struct mwifiex_private *priv)
{
	if (!priv)
		return;

	debugfs_remove_recursive(priv->dfs_dev_dir);
}

/*
 * This function creates the top level proc directory.
 */
void
mwifiex_debugfs_init(void)
{
	if (!mwifiex_dfs_dir)
		mwifiex_dfs_dir = debugfs_create_dir("mwifiex", NULL);
}

/*
 * This function removes the top level proc directory.
 */
void
mwifiex_debugfs_remove(void)
{
	if (mwifiex_dfs_dir)
		debugfs_remove(mwifiex_dfs_dir);
}
