/*
 * NXP Wireless LAN device driver: debugfs
 *
 * Copyright 2011-2020 NXP
 *
 * This software file (the "File") is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
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

	mwifiex_get_ver_ext(priv, 0);

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
		p += sprintf(p, "essid=\"%.*s\"\n", info.ssid.ssid_len,
			     info.ssid.ssid);
		p += sprintf(p, "bssid=\"%pM\"\n", info.bssid);
		p += sprintf(p, "channel=\"%d\"\n", (int) info.bss_chan);
		p += sprintf(p, "country_code = \"%s\"\n", info.country_code);
		p += sprintf(p, "region_code=\"0x%x\"\n",
			     priv->adapter->region_code);

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
 *      - Number of received beacons
 *      - Number of missed beacons
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
		     "wepicverrcnt-4   %u\n"
		     "bcn_rcv_cnt   %u\n"
		     "bcn_miss_cnt   %u\n",
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
		     stats.wep_icv_error[3],
		     stats.bcn_rcv_cnt,
		     stats.bcn_miss_cnt);


	ret = simple_read_from_buffer(ubuf, count, ppos, (char *) page,
				      (unsigned long) p - page);

free_and_exit:
	free_page(page);
	return ret;
}

/* Sysfs histogram file read handler.
 *
 * This function is called when the 'histogram' file is opened for reading
 * It prints the following histogram information -
 *      - Number of histogram samples
 *      - Receive packet number of each rx_rate
 *      - Receive packet number of each snr
 *      - Receive packet number of each nosie_flr
 *      - Receive packet number of each signal streath
 */
static ssize_t
mwifiex_histogram_read(struct file *file, char __user *ubuf,
		       size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv =
		(struct mwifiex_private *)file->private_data;
	ssize_t ret;
	struct mwifiex_histogram_data *phist_data;
	int i, value;
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	char *p = (char *)page;

	if (!p)
		return -ENOMEM;

	if (!priv || !priv->hist_data)
		return -EFAULT;
	phist_data = priv->hist_data;

	p += sprintf(p, "\n"
		     "total samples = %d\n",
		     atomic_read(&phist_data->num_samples));

	p += sprintf(p,
		     "rx rates (in Mbps): 0=1M   1=2M 2=5.5M  3=11M   4=6M   5=9M  6=12M\n"
		     "7=18M  8=24M  9=36M  10=48M  11=54M 12-27=MCS0-15(BW20) 28-43=MCS0-15(BW40)\n");

	if (ISSUPP_11ACENABLED(priv->adapter->fw_cap_info)) {
		p += sprintf(p,
			     "44-53=MCS0-9(VHT:BW20) 54-63=MCS0-9(VHT:BW40) 64-73=MCS0-9(VHT:BW80)\n\n");
	} else {
		p += sprintf(p, "\n");
	}

	for (i = 0; i < MWIFIEX_MAX_RX_RATES; i++) {
		value = atomic_read(&phist_data->rx_rate[i]);
		if (value)
			p += sprintf(p, "rx_rate[%02d] = %d\n", i, value);
	}

	if (ISSUPP_11ACENABLED(priv->adapter->fw_cap_info)) {
		for (i = MWIFIEX_MAX_RX_RATES; i < MWIFIEX_MAX_AC_RX_RATES;
		     i++) {
			value = atomic_read(&phist_data->rx_rate[i]);
			if (value)
				p += sprintf(p, "rx_rate[%02d] = %d\n",
					   i, value);
		}
	}

	for (i = 0; i < MWIFIEX_MAX_SNR; i++) {
		value =  atomic_read(&phist_data->snr[i]);
		if (value)
			p += sprintf(p, "snr[%02ddB] = %d\n", i, value);
	}
	for (i = 0; i < MWIFIEX_MAX_NOISE_FLR; i++) {
		value = atomic_read(&phist_data->noise_flr[i]);
		if (value)
			p += sprintf(p, "noise_flr[%02ddBm] = %d\n",
				(int)(i-128), value);
	}
	for (i = 0; i < MWIFIEX_MAX_SIG_STRENGTH; i++) {
		value = atomic_read(&phist_data->sig_str[i]);
		if (value)
			p += sprintf(p, "sig_strength[-%02ddBm] = %d\n",
				i, value);
	}

	ret = simple_read_from_buffer(ubuf, count, ppos, (char *)page,
				      (unsigned long)p - page);

	return ret;
}

static ssize_t
mwifiex_histogram_write(struct file *file, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv = (void *)file->private_data;

	if (priv && priv->hist_data)
		mwifiex_hist_data_reset(priv);
	return 0;
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
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	char *p = (char *) page;
	ssize_t ret;

	if (!p)
		return -ENOMEM;

	ret = mwifiex_get_debug_info(priv, &info);
	if (ret)
		goto free_and_exit;

	p += mwifiex_debug_info_to_buffer(priv, p, &info);

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
	char *buf;
	int ret;
	u32 reg_type = 0, reg_offset = 0, reg_value = UINT_MAX;

	buf = memdup_user_nul(ubuf, min(count, (size_t)(PAGE_SIZE - 1)));
	if (IS_ERR(buf))
		return PTR_ERR(buf);

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
	kfree(buf);
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

/* Proc debug_mask file read handler.
 * This function is called when the 'debug_mask' file is opened for reading
 * This function can be used read driver debugging mask value.
 */
static ssize_t
mwifiex_debug_mask_read(struct file *file, char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv =
		(struct mwifiex_private *)file->private_data;
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)page;
	size_t ret = 0;
	int pos = 0;

	if (!buf)
		return -ENOMEM;

	pos += snprintf(buf, PAGE_SIZE, "debug mask=0x%08x\n",
			priv->adapter->debug_mask);
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, pos);

	free_page(page);
	return ret;
}

/* Proc debug_mask file read handler.
 * This function is called when the 'debug_mask' file is opened for reading
 * This function can be used read driver debugging mask value.
 */
static ssize_t
mwifiex_debug_mask_write(struct file *file, const char __user *ubuf,
			 size_t count, loff_t *ppos)
{
	int ret;
	unsigned long debug_mask;
	struct mwifiex_private *priv = (void *)file->private_data;
	char *buf;

	buf = memdup_user_nul(ubuf, min(count, (size_t)(PAGE_SIZE - 1)));
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (kstrtoul(buf, 0, &debug_mask)) {
		ret = -EINVAL;
		goto done;
	}

	priv->adapter->debug_mask = debug_mask;
	ret = count;
done:
	kfree(buf);
	return ret;
}

/* debugfs verext file write handler.
 * This function is called when the 'verext' file is opened for write
 */
static ssize_t
mwifiex_verext_write(struct file *file, const char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	int ret;
	u32 versionstrsel;
	struct mwifiex_private *priv = (void *)file->private_data;
	char buf[16];

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	ret = kstrtou32(buf, 10, &versionstrsel);
	if (ret)
		return ret;

	priv->versionstrsel = versionstrsel;

	return count;
}

/* Proc verext file read handler.
 * This function is called when the 'verext' file is opened for reading
 * This function can be used read driver exteneed verion string.
 */
static ssize_t
mwifiex_verext_read(struct file *file, char __user *ubuf,
		    size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv =
		(struct mwifiex_private *)file->private_data;
	char buf[256];
	int ret;

	mwifiex_get_ver_ext(priv, priv->versionstrsel);
	ret = snprintf(buf, sizeof(buf), "version string: %s\n",
		       priv->version_str);

	return simple_read_from_buffer(ubuf, count, ppos, buf, ret);
}

/* Proc memrw file write handler.
 * This function is called when the 'memrw' file is opened for writing
 * This function can be used to write to a memory location.
 */
static ssize_t
mwifiex_memrw_write(struct file *file, const char __user *ubuf, size_t count,
		    loff_t *ppos)
{
	int ret;
	char cmd;
	struct mwifiex_ds_mem_rw mem_rw;
	u16 cmd_action;
	struct mwifiex_private *priv = (void *)file->private_data;
	char *buf;

	buf = memdup_user_nul(ubuf, min(count, (size_t)(PAGE_SIZE - 1)));
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = sscanf(buf, "%c %x %x", &cmd, &mem_rw.addr, &mem_rw.value);
	if (ret != 3) {
		ret = -EINVAL;
		goto done;
	}

	if ((cmd == 'r') || (cmd == 'R')) {
		cmd_action = HostCmd_ACT_GEN_GET;
		mem_rw.value = 0;
	} else if ((cmd == 'w') || (cmd == 'W')) {
		cmd_action = HostCmd_ACT_GEN_SET;
	} else {
		ret = -EINVAL;
		goto done;
	}

	memcpy(&priv->mem_rw, &mem_rw, sizeof(mem_rw));
	if (mwifiex_send_cmd(priv, HostCmd_CMD_MEM_ACCESS, cmd_action, 0,
			     &mem_rw, true))
		ret = -1;
	else
		ret = count;

done:
	kfree(buf);
	return ret;
}

/* Proc memrw file read handler.
 * This function is called when the 'memrw' file is opened for reading
 * This function can be used to read from a memory location.
 */
static ssize_t
mwifiex_memrw_read(struct file *file, char __user *ubuf,
		   size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv = (void *)file->private_data;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	int ret, pos = 0;

	if (!buf)
		return -ENOMEM;

	pos += snprintf(buf, PAGE_SIZE, "0x%x 0x%x\n", priv->mem_rw.addr,
			priv->mem_rw.value);
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, pos);

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
	char *buf;
	int ret = 0;
	int offset = -1, bytes = -1;

	buf = memdup_user_nul(ubuf, min(count, (size_t)(PAGE_SIZE - 1)));
	if (IS_ERR(buf))
		return PTR_ERR(buf);

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
	kfree(buf);
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
	int pos, ret, i;
	u8 value[MAX_EEPROM_DATA];

	if (!buf)
		return -ENOMEM;

	if (saved_offset == -1) {
		/* No command has been given */
		pos = snprintf(buf, PAGE_SIZE, "0");
		goto done;
	}

	/* Get command has been given */
	ret = mwifiex_eeprom_read(priv, (u16) saved_offset,
				  (u16) saved_bytes, value);
	if (ret) {
		ret = -EINVAL;
		goto out_free;
	}

	pos = snprintf(buf, PAGE_SIZE, "%d %d ", saved_offset, saved_bytes);

	for (i = 0; i < saved_bytes; i++)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "%d ", value[i]);

done:
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, pos);
out_free:
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
	char *buf;
	int ret, arg_num;
	struct mwifiex_ds_hs_cfg hscfg;
	int conditions = HS_CFG_COND_DEF;
	u32 gpio = HS_CFG_GPIO_DEF, gap = HS_CFG_GAP_DEF;

	buf = memdup_user_nul(ubuf, min(count, (size_t)(PAGE_SIZE - 1)));
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	arg_num = sscanf(buf, "%d %x %x", &conditions, &gpio, &gap);

	memset(&hscfg, 0, sizeof(struct mwifiex_ds_hs_cfg));

	if (arg_num > 3) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "Too many arguments\n");
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
	clear_bit(MWIFIEX_IS_HS_ENABLING, &priv->adapter->work_flags);
	ret = count;
done:
	kfree(buf);
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

static ssize_t
mwifiex_timeshare_coex_read(struct file *file, char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv = file->private_data;
	char buf[3];
	bool timeshare_coex;
	int ret;
	unsigned int len;

	if (priv->adapter->fw_api_ver != MWIFIEX_FW_V15)
		return -EOPNOTSUPP;

	ret = mwifiex_send_cmd(priv, HostCmd_CMD_ROBUST_COEX,
			       HostCmd_ACT_GEN_GET, 0, &timeshare_coex, true);
	if (ret)
		return ret;

	len = sprintf(buf, "%d\n", timeshare_coex);
	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static ssize_t
mwifiex_timeshare_coex_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	bool timeshare_coex;
	struct mwifiex_private *priv = file->private_data;
	char kbuf[16];
	int ret;

	if (priv->adapter->fw_api_ver != MWIFIEX_FW_V15)
		return -EOPNOTSUPP;

	memset(kbuf, 0, sizeof(kbuf));

	if (copy_from_user(&kbuf, ubuf, min_t(size_t, sizeof(kbuf) - 1, count)))
		return -EFAULT;

	if (strtobool(kbuf, &timeshare_coex))
		return -EINVAL;

	ret = mwifiex_send_cmd(priv, HostCmd_CMD_ROBUST_COEX,
			       HostCmd_ACT_GEN_SET, 0, &timeshare_coex, true);
	if (ret)
		return ret;
	else
		return count;
}

static ssize_t
mwifiex_reset_write(struct file *file,
		    const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct mwifiex_private *priv = file->private_data;
	struct mwifiex_adapter *adapter = priv->adapter;
	bool result;
	int rc;

	rc = kstrtobool_from_user(ubuf, count, &result);
	if (rc)
		return rc;

	if (!result)
		return -EINVAL;

	if (adapter->if_ops.card_reset) {
		dev_info(adapter->dev, "Resetting per request\n");
		adapter->if_ops.card_reset(adapter);
	}

	return count;
}

#define MWIFIEX_DFS_ADD_FILE(name) do {                                 \
	debugfs_create_file(#name, 0644, priv->dfs_dev_dir, priv,       \
			    &mwifiex_dfs_##name##_fops);                \
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
MWIFIEX_DFS_FILE_OPS(regrdwr);
MWIFIEX_DFS_FILE_OPS(rdeeprom);
MWIFIEX_DFS_FILE_OPS(memrw);
MWIFIEX_DFS_FILE_OPS(hscfg);
MWIFIEX_DFS_FILE_OPS(histogram);
MWIFIEX_DFS_FILE_OPS(debug_mask);
MWIFIEX_DFS_FILE_OPS(timeshare_coex);
MWIFIEX_DFS_FILE_WRITE_OPS(reset);
MWIFIEX_DFS_FILE_OPS(verext);

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

	MWIFIEX_DFS_ADD_FILE(memrw);
	MWIFIEX_DFS_ADD_FILE(hscfg);
	MWIFIEX_DFS_ADD_FILE(histogram);
	MWIFIEX_DFS_ADD_FILE(debug_mask);
	MWIFIEX_DFS_ADD_FILE(timeshare_coex);
	MWIFIEX_DFS_ADD_FILE(reset);
	MWIFIEX_DFS_ADD_FILE(verext);
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
	debugfs_remove(mwifiex_dfs_dir);
}
