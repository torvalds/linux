#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/export.h>

#include "decl.h"
#include "cmd.h"
#include "debugfs.h"

static struct dentry *lbs_dir;
static char *szStates[] = {
	"Connected",
	"Disconnected"
};

#ifdef PROC_DEBUG
static void lbs_debug_init(struct lbs_private *priv);
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

static ssize_t lbs_dev_info(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	size_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	ssize_t res;
	if (!buf)
		return -ENOMEM;

	pos += snprintf(buf+pos, len-pos, "state = %s\n",
				szStates[priv->connect_status]);
	pos += snprintf(buf+pos, len-pos, "region_code = %02x\n",
				(u32) priv->regioncode);

	res = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

	free_page(addr);
	return res;
}

static ssize_t lbs_sleepparams_write(struct file *file,
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t buf_size, ret;
	struct sleep_params sp;
	int p1, p2, p3, p4, p5, p6;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, user_buf, buf_size)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	ret = sscanf(buf, "%d %d %d %d %d %d", &p1, &p2, &p3, &p4, &p5, &p6);
	if (ret != 6) {
		ret = -EINVAL;
		goto out_unlock;
	}
	sp.sp_error = p1;
	sp.sp_offset = p2;
	sp.sp_stabletime = p3;
	sp.sp_calcontrol = p4;
	sp.sp_extsleepclk = p5;
	sp.sp_reserved = p6;

	ret = lbs_cmd_802_11_sleep_params(priv, CMD_ACT_SET, &sp);
	if (!ret)
		ret = count;
	else if (ret > 0)
		ret = -EINVAL;

out_unlock:
	free_page(addr);
	return ret;
}

static ssize_t lbs_sleepparams_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t ret;
	size_t pos = 0;
	struct sleep_params sp;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

	ret = lbs_cmd_802_11_sleep_params(priv, CMD_ACT_GET, &sp);
	if (ret)
		goto out_unlock;

	pos += snprintf(buf, len, "%d %d %d %d %d %d\n", sp.sp_error,
			sp.sp_offset, sp.sp_stabletime,
			sp.sp_calcontrol, sp.sp_extsleepclk,
			sp.sp_reserved);

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

out_unlock:
	free_page(addr);
	return ret;
}

static ssize_t lbs_host_sleep_write(struct file *file,
				const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t buf_size, ret;
	int host_sleep;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, user_buf, buf_size)) {
		ret = -EFAULT;
		goto out_unlock;
	}
	ret = sscanf(buf, "%d", &host_sleep);
	if (ret != 1) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (host_sleep == 0)
		ret = lbs_set_host_sleep(priv, 0);
	else if (host_sleep == 1) {
		if (priv->wol_criteria == EHS_REMOVE_WAKEUP) {
			netdev_info(priv->dev,
				    "wake parameters not configured\n");
			ret = -EINVAL;
			goto out_unlock;
		}
		ret = lbs_set_host_sleep(priv, 1);
	} else {
		netdev_err(priv->dev, "invalid option\n");
		ret = -EINVAL;
	}

	if (!ret)
		ret = count;

out_unlock:
	free_page(addr);
	return ret;
}

static ssize_t lbs_host_sleep_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t ret;
	size_t pos = 0;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

	pos += snprintf(buf, len, "%d\n", priv->is_host_sleep_activated);

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

	free_page(addr);
	return ret;
}

/*
 * When calling CMD_802_11_SUBSCRIBE_EVENT with CMD_ACT_GET, me might
 * get a bunch of vendor-specific TLVs (a.k.a. IEs) back from the
 * firmware. Here's an example:
 *	04 01 02 00 00 00 05 01 02 00 00 00 06 01 02 00
 *	00 00 07 01 02 00 3c 00 00 00 00 00 00 00 03 03
 *	00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *
 * The 04 01 is the TLV type (here TLV_TYPE_RSSI_LOW), 02 00 is the length,
 * 00 00 are the data bytes of this TLV. For this TLV, their meaning is
 * defined in mrvlietypes_thresholds
 *
 * This function searches in this TLV data chunk for a given TLV type
 * and returns a pointer to the first data byte of the TLV, or to NULL
 * if the TLV hasn't been found.
 */
static void *lbs_tlv_find(uint16_t tlv_type, const uint8_t *tlv, uint16_t size)
{
	struct mrvl_ie_header *tlv_h;
	uint16_t length;
	ssize_t pos = 0;

	while (pos < size) {
		tlv_h = (struct mrvl_ie_header *) tlv;
		if (!tlv_h->len)
			return NULL;
		if (tlv_h->type == cpu_to_le16(tlv_type))
			return tlv_h;
		length = le16_to_cpu(tlv_h->len) + sizeof(*tlv_h);
		pos += length;
		tlv += length;
	}
	return NULL;
}


static ssize_t lbs_threshold_read(uint16_t tlv_type, uint16_t event_mask,
				  struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	struct cmd_ds_802_11_subscribe_event *subscribed;
	struct mrvl_ie_thresholds *got;
	struct lbs_private *priv = file->private_data;
	ssize_t ret = 0;
	size_t pos = 0;
	char *buf;
	u8 value;
	u8 freq;
	int events = 0;

	buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	subscribed = kzalloc(sizeof(*subscribed), GFP_KERNEL);
	if (!subscribed) {
		ret = -ENOMEM;
		goto out_page;
	}

	subscribed->hdr.size = cpu_to_le16(sizeof(*subscribed));
	subscribed->action = cpu_to_le16(CMD_ACT_GET);

	ret = lbs_cmd_with_response(priv, CMD_802_11_SUBSCRIBE_EVENT, subscribed);
	if (ret)
		goto out_cmd;

	got = lbs_tlv_find(tlv_type, subscribed->tlv, sizeof(subscribed->tlv));
	if (got) {
		value = got->value;
		freq  = got->freq;
		events = le16_to_cpu(subscribed->events);

		pos += snprintf(buf, len, "%d %d %d\n", value, freq,
				!!(events & event_mask));
	}

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);

 out_cmd:
	kfree(subscribed);

 out_page:
	free_page((unsigned long)buf);
	return ret;
}


static ssize_t lbs_threshold_write(uint16_t tlv_type, uint16_t event_mask,
				   struct file *file,
				   const char __user *userbuf, size_t count,
				   loff_t *ppos)
{
	struct cmd_ds_802_11_subscribe_event *events;
	struct mrvl_ie_thresholds *tlv;
	struct lbs_private *priv = file->private_data;
	ssize_t buf_size;
	int value, freq, new_mask;
	uint16_t curr_mask;
	char *buf;
	int ret;

	buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		ret = -EFAULT;
		goto out_page;
	}
	ret = sscanf(buf, "%d %d %d", &value, &freq, &new_mask);
	if (ret != 3) {
		ret = -EINVAL;
		goto out_page;
	}
	events = kzalloc(sizeof(*events), GFP_KERNEL);
	if (!events) {
		ret = -ENOMEM;
		goto out_page;
	}

	events->hdr.size = cpu_to_le16(sizeof(*events));
	events->action = cpu_to_le16(CMD_ACT_GET);

	ret = lbs_cmd_with_response(priv, CMD_802_11_SUBSCRIBE_EVENT, events);
	if (ret)
		goto out_events;

	curr_mask = le16_to_cpu(events->events);

	if (new_mask)
		new_mask = curr_mask | event_mask;
	else
		new_mask = curr_mask & ~event_mask;

	/* Now everything is set and we can send stuff down to the firmware */

	tlv = (void *)events->tlv;

	events->action = cpu_to_le16(CMD_ACT_SET);
	events->events = cpu_to_le16(new_mask);
	tlv->header.type = cpu_to_le16(tlv_type);
	tlv->header.len = cpu_to_le16(sizeof(*tlv) - sizeof(tlv->header));
	tlv->value = value;
	if (tlv_type != TLV_TYPE_BCNMISS)
		tlv->freq = freq;

	/* The command header, the action, the event mask, and one TLV */
	events->hdr.size = cpu_to_le16(sizeof(events->hdr) + 4 + sizeof(*tlv));

	ret = lbs_cmd_with_response(priv, CMD_802_11_SUBSCRIBE_EVENT, events);

	if (!ret)
		ret = count;
 out_events:
	kfree(events);
 out_page:
	free_page((unsigned long)buf);
	return ret;
}


static ssize_t lbs_lowrssi_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	return lbs_threshold_read(TLV_TYPE_RSSI_LOW, CMD_SUBSCRIBE_RSSI_LOW,
				  file, userbuf, count, ppos);
}


static ssize_t lbs_lowrssi_write(struct file *file, const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	return lbs_threshold_write(TLV_TYPE_RSSI_LOW, CMD_SUBSCRIBE_RSSI_LOW,
				   file, userbuf, count, ppos);
}


static ssize_t lbs_lowsnr_read(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	return lbs_threshold_read(TLV_TYPE_SNR_LOW, CMD_SUBSCRIBE_SNR_LOW,
				  file, userbuf, count, ppos);
}


static ssize_t lbs_lowsnr_write(struct file *file, const char __user *userbuf,
				size_t count, loff_t *ppos)
{
	return lbs_threshold_write(TLV_TYPE_SNR_LOW, CMD_SUBSCRIBE_SNR_LOW,
				   file, userbuf, count, ppos);
}


static ssize_t lbs_failcount_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	return lbs_threshold_read(TLV_TYPE_FAILCOUNT, CMD_SUBSCRIBE_FAILCOUNT,
				  file, userbuf, count, ppos);
}


static ssize_t lbs_failcount_write(struct file *file, const char __user *userbuf,
				   size_t count, loff_t *ppos)
{
	return lbs_threshold_write(TLV_TYPE_FAILCOUNT, CMD_SUBSCRIBE_FAILCOUNT,
				   file, userbuf, count, ppos);
}


static ssize_t lbs_highrssi_read(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	return lbs_threshold_read(TLV_TYPE_RSSI_HIGH, CMD_SUBSCRIBE_RSSI_HIGH,
				  file, userbuf, count, ppos);
}


static ssize_t lbs_highrssi_write(struct file *file, const char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	return lbs_threshold_write(TLV_TYPE_RSSI_HIGH, CMD_SUBSCRIBE_RSSI_HIGH,
				   file, userbuf, count, ppos);
}


static ssize_t lbs_highsnr_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	return lbs_threshold_read(TLV_TYPE_SNR_HIGH, CMD_SUBSCRIBE_SNR_HIGH,
				  file, userbuf, count, ppos);
}


static ssize_t lbs_highsnr_write(struct file *file, const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	return lbs_threshold_write(TLV_TYPE_SNR_HIGH, CMD_SUBSCRIBE_SNR_HIGH,
				   file, userbuf, count, ppos);
}

static ssize_t lbs_bcnmiss_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	return lbs_threshold_read(TLV_TYPE_BCNMISS, CMD_SUBSCRIBE_BCNMISS,
				  file, userbuf, count, ppos);
}


static ssize_t lbs_bcnmiss_write(struct file *file, const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	return lbs_threshold_write(TLV_TYPE_BCNMISS, CMD_SUBSCRIBE_BCNMISS,
				   file, userbuf, count, ppos);
}


static ssize_t lbs_rdmac_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t pos = 0;
	int ret;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	u32 val = 0;

	if (!buf)
		return -ENOMEM;

	ret = lbs_get_reg(priv, CMD_MAC_REG_ACCESS, priv->mac_offset, &val);
	mdelay(10);
	if (!ret) {
		pos = snprintf(buf, len, "MAC[0x%x] = 0x%08x\n",
				priv->mac_offset, val);
		ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	}
	free_page(addr);
	return ret;
}

static ssize_t lbs_rdmac_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t res, buf_size;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

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

static ssize_t lbs_wrmac_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{

	struct lbs_private *priv = file->private_data;
	ssize_t res, buf_size;
	u32 offset, value;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

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

	res = lbs_set_reg(priv, CMD_MAC_REG_ACCESS, offset, value);
	mdelay(10);

	if (!res)
		res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t lbs_rdbbp_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t pos = 0;
	int ret;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	u32 val;

	if (!buf)
		return -ENOMEM;

	ret = lbs_get_reg(priv, CMD_BBP_REG_ACCESS, priv->bbp_offset, &val);
	mdelay(10);
	if (!ret) {
		pos = snprintf(buf, len, "BBP[0x%x] = 0x%08x\n",
				priv->bbp_offset, val);
		ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	}
	free_page(addr);

	return ret;
}

static ssize_t lbs_rdbbp_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t res, buf_size;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

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

static ssize_t lbs_wrbbp_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{

	struct lbs_private *priv = file->private_data;
	ssize_t res, buf_size;
	u32 offset, value;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

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

	res = lbs_set_reg(priv, CMD_BBP_REG_ACCESS, offset, value);
	mdelay(10);

	if (!res)
		res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t lbs_rdrf_read(struct file *file, char __user *userbuf,
				  size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t pos = 0;
	int ret;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	u32 val;

	if (!buf)
		return -ENOMEM;

	ret = lbs_get_reg(priv, CMD_RF_REG_ACCESS, priv->rf_offset, &val);
	mdelay(10);
	if (!ret) {
		pos = snprintf(buf, len, "RF[0x%x] = 0x%08x\n",
				priv->rf_offset, val);
		ret = simple_read_from_buffer(userbuf, count, ppos, buf, pos);
	}
	free_page(addr);

	return ret;
}

static ssize_t lbs_rdrf_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	struct lbs_private *priv = file->private_data;
	ssize_t res, buf_size;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

	buf_size = min(count, len - 1);
	if (copy_from_user(buf, userbuf, buf_size)) {
		res = -EFAULT;
		goto out_unlock;
	}
	priv->rf_offset = simple_strtoul(buf, NULL, 16);
	res = count;
out_unlock:
	free_page(addr);
	return res;
}

static ssize_t lbs_wrrf_write(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *ppos)
{

	struct lbs_private *priv = file->private_data;
	ssize_t res, buf_size;
	u32 offset, value;
	unsigned long addr = get_zeroed_page(GFP_KERNEL);
	char *buf = (char *)addr;
	if (!buf)
		return -ENOMEM;

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

	res = lbs_set_reg(priv, CMD_RF_REG_ACCESS, offset, value);
	mdelay(10);

	if (!res)
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
	.llseek = generic_file_llseek, \
}

struct lbs_debugfs_files {
	const char *name;
	umode_t perm;
	struct file_operations fops;
};

static const struct lbs_debugfs_files debugfs_files[] = {
	{ "info", 0444, FOPS(lbs_dev_info, write_file_dummy), },
	{ "sleepparams", 0644, FOPS(lbs_sleepparams_read,
				lbs_sleepparams_write), },
	{ "hostsleep", 0644, FOPS(lbs_host_sleep_read,
				lbs_host_sleep_write), },
};

static const struct lbs_debugfs_files debugfs_events_files[] = {
	{"low_rssi", 0644, FOPS(lbs_lowrssi_read,
				lbs_lowrssi_write), },
	{"low_snr", 0644, FOPS(lbs_lowsnr_read,
				lbs_lowsnr_write), },
	{"failure_count", 0644, FOPS(lbs_failcount_read,
				lbs_failcount_write), },
	{"beacon_missed", 0644, FOPS(lbs_bcnmiss_read,
				lbs_bcnmiss_write), },
	{"high_rssi", 0644, FOPS(lbs_highrssi_read,
				lbs_highrssi_write), },
	{"high_snr", 0644, FOPS(lbs_highsnr_read,
				lbs_highsnr_write), },
};

static const struct lbs_debugfs_files debugfs_regs_files[] = {
	{"rdmac", 0644, FOPS(lbs_rdmac_read, lbs_rdmac_write), },
	{"wrmac", 0600, FOPS(NULL, lbs_wrmac_write), },
	{"rdbbp", 0644, FOPS(lbs_rdbbp_read, lbs_rdbbp_write), },
	{"wrbbp", 0600, FOPS(NULL, lbs_wrbbp_write), },
	{"rdrf", 0644, FOPS(lbs_rdrf_read, lbs_rdrf_write), },
	{"wrrf", 0600, FOPS(NULL, lbs_wrrf_write), },
};

void lbs_debugfs_init(void)
{
	if (!lbs_dir)
		lbs_dir = debugfs_create_dir("lbs_wireless", NULL);
}

void lbs_debugfs_remove(void)
{
	if (lbs_dir)
		 debugfs_remove(lbs_dir);
}

void lbs_debugfs_init_one(struct lbs_private *priv, struct net_device *dev)
{
	int i;
	const struct lbs_debugfs_files *files;
	if (!lbs_dir)
		goto exit;

	priv->debugfs_dir = debugfs_create_dir(dev->name, lbs_dir);
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
	lbs_debug_init(priv);
#endif
exit:
	return;
}

void lbs_debugfs_remove_one(struct lbs_private *priv)
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

#define item_size(n)	(FIELD_SIZEOF(struct lbs_private, n))
#define item_addr(n)	(offsetof(struct lbs_private, n))


struct debug_data {
	char name[32];
	u32 size;
	size_t addr;
};

/* To debug any member of struct lbs_private, simply add one line here.
 */
static struct debug_data items[] = {
	{"psmode", item_size(psmode), item_addr(psmode)},
	{"psstate", item_size(psstate), item_addr(psstate)},
};

static int num_of_items = ARRAY_SIZE(items);

/**
 * lbs_debugfs_read - proc read function
 *
 * @file:	file to read
 * @userbuf:	pointer to buffer
 * @count:	number of bytes to read
 * @ppos:	read data starting position
 *
 * returns:	amount of data read or negative error code
 */
static ssize_t lbs_debugfs_read(struct file *file, char __user *userbuf,
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
	if (!buf)
		return -ENOMEM;

	p = buf;

	d = file->private_data;

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
 * lbs_debugfs_write - proc write function
 *
 * @f:		file pointer
 * @buf:	pointer to data buffer
 * @cnt:	data number to write
 * @ppos:	file position
 *
 * returns:	amount of data written
 */
static ssize_t lbs_debugfs_write(struct file *f, const char __user *buf,
			    size_t cnt, loff_t *ppos)
{
	int r, i;
	char *pdata;
	char *p;
	char *p0;
	char *p1;
	char *p2;
	struct debug_data *d = f->private_data;

	pdata = kmalloc(cnt, GFP_KERNEL);
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

static const struct file_operations lbs_debug_fops = {
	.owner = THIS_MODULE,
	.open = open_file_generic,
	.write = lbs_debugfs_write,
	.read = lbs_debugfs_read,
	.llseek = default_llseek,
};

/**
 * lbs_debug_init - create debug proc file
 *
 * @priv:	pointer to &struct lbs_private
 *
 * returns:	N/A
 */
static void lbs_debug_init(struct lbs_private *priv)
{
	int i;

	if (!priv->debugfs_dir)
		return;

	for (i = 0; i < num_of_items; i++)
		items[i].addr += (size_t) priv;

	priv->debugfs_debug = debugfs_create_file("debug", 0644,
						  priv->debugfs_dir, &items[0],
						  &lbs_debug_fops);
}
#endif
