/*
 * Atheros CARL9170 driver
 *
 * debug(fs) probing
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, 2010, Christian Lamparter <chunkeey@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2008-2009 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include "carl9170.h"
#include "cmd.h"

#define ADD(buf, off, max, fmt, args...)				\
	off += snprintf(&buf[off], max - off, fmt, ##args);

static int carl9170_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

struct carl9170_debugfs_fops {
	unsigned int read_bufsize;
	mode_t attr;
	char *(*read)(struct ar9170 *ar, char *buf, size_t bufsize,
		      ssize_t *len);
	ssize_t (*write)(struct ar9170 *aru, const char *buf, size_t size);
	const struct file_operations fops;

	enum carl9170_device_state req_dev_state;
};

static ssize_t carl9170_debugfs_read(struct file *file, char __user *userbuf,
				     size_t count, loff_t *ppos)
{
	struct carl9170_debugfs_fops *dfops;
	struct ar9170 *ar;
	char *buf = NULL, *res_buf = NULL;
	ssize_t ret = 0;
	int err = 0;

	if (!count)
		return 0;

	ar = file->private_data;

	if (!ar)
		return -ENODEV;
	dfops = container_of(file->f_op, struct carl9170_debugfs_fops, fops);

	if (!dfops->read)
		return -ENOSYS;

	if (dfops->read_bufsize) {
		buf = vmalloc(dfops->read_bufsize);
		if (!buf)
			return -ENOMEM;
	}

	mutex_lock(&ar->mutex);
	if (!CHK_DEV_STATE(ar, dfops->req_dev_state)) {
		err = -ENODEV;
		res_buf = buf;
		goto out_free;
	}

	res_buf = dfops->read(ar, buf, dfops->read_bufsize, &ret);

	if (ret > 0)
		err = simple_read_from_buffer(userbuf, count, ppos,
					      res_buf, ret);
	else
		err = ret;

	WARN_ON_ONCE(dfops->read_bufsize && (res_buf != buf));

out_free:
	vfree(res_buf);
	mutex_unlock(&ar->mutex);
	return err;
}

static ssize_t carl9170_debugfs_write(struct file *file,
	const char __user *userbuf, size_t count, loff_t *ppos)
{
	struct carl9170_debugfs_fops *dfops;
	struct ar9170 *ar;
	char *buf = NULL;
	int err = 0;

	if (!count)
		return 0;

	if (count > PAGE_SIZE)
		return -E2BIG;

	ar = file->private_data;

	if (!ar)
		return -ENODEV;
	dfops = container_of(file->f_op, struct carl9170_debugfs_fops, fops);

	if (!dfops->write)
		return -ENOSYS;

	buf = vmalloc(count);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, userbuf, count)) {
		err = -EFAULT;
		goto out_free;
	}

	if (mutex_trylock(&ar->mutex) == 0) {
		err = -EAGAIN;
		goto out_free;
	}

	if (!CHK_DEV_STATE(ar, dfops->req_dev_state)) {
		err = -ENODEV;
		goto out_unlock;
	}

	err = dfops->write(ar, buf, count);
	if (err)
		goto out_unlock;

out_unlock:
	mutex_unlock(&ar->mutex);

out_free:
	vfree(buf);
	return err;
}

#define __DEBUGFS_DECLARE_FILE(name, _read, _write, _read_bufsize,	\
			       _attr, _dstate)				\
static const struct carl9170_debugfs_fops carl_debugfs_##name ##_ops = {\
	.read_bufsize = _read_bufsize,					\
	.read = _read,							\
	.write = _write,						\
	.attr = _attr,							\
	.req_dev_state = _dstate,					\
	.fops = {							\
		.open	= carl9170_debugfs_open,			\
		.read	= carl9170_debugfs_read,			\
		.write	= carl9170_debugfs_write,			\
		.owner	= THIS_MODULE					\
	},								\
}

#define DEBUGFS_DECLARE_FILE(name, _read, _write, _read_bufsize, _attr)	\
	__DEBUGFS_DECLARE_FILE(name, _read, _write, _read_bufsize,	\
			       _attr, CARL9170_STARTED)			\

#define DEBUGFS_DECLARE_RO_FILE(name, _read_bufsize)			\
	DEBUGFS_DECLARE_FILE(name, carl9170_debugfs_##name ##_read,	\
			     NULL, _read_bufsize, S_IRUSR)

#define DEBUGFS_DECLARE_WO_FILE(name)					\
	DEBUGFS_DECLARE_FILE(name, NULL, carl9170_debugfs_##name ##_write,\
			     0, S_IWUSR)

#define DEBUGFS_DECLARE_RW_FILE(name, _read_bufsize)			\
	DEBUGFS_DECLARE_FILE(name, carl9170_debugfs_##name ##_read,	\
			     carl9170_debugfs_##name ##_write,		\
			     _read_bufsize, S_IRUSR | S_IWUSR)

#define __DEBUGFS_DECLARE_RW_FILE(name, _read_bufsize, _dstate)		\
	__DEBUGFS_DECLARE_FILE(name, carl9170_debugfs_##name ##_read,	\
			     carl9170_debugfs_##name ##_write,		\
			     _read_bufsize, S_IRUSR | S_IWUSR, _dstate)

#define DEBUGFS_READONLY_FILE(name, _read_bufsize, fmt, value...)	\
static char *carl9170_debugfs_ ##name ## _read(struct ar9170 *ar,	\
					     char *buf, size_t buf_size,\
					     ssize_t *len)		\
{									\
	ADD(buf, *len, buf_size, fmt "\n", ##value);			\
	return buf;							\
}									\
DEBUGFS_DECLARE_RO_FILE(name, _read_bufsize)

static char *carl9170_debugfs_mem_usage_read(struct ar9170 *ar, char *buf,
					     size_t bufsize, ssize_t *len)
{
	ADD(buf, *len, bufsize, "jar: [");

	spin_lock_bh(&ar->mem_lock);

	*len += bitmap_scnprintf(&buf[*len], bufsize - *len,
				  ar->mem_bitmap, ar->fw.mem_blocks);

	ADD(buf, *len, bufsize, "]\n");

	ADD(buf, *len, bufsize, "cookies: used:%3d / total:%3d, allocs:%d\n",
	    bitmap_weight(ar->mem_bitmap, ar->fw.mem_blocks),
	    ar->fw.mem_blocks, atomic_read(&ar->mem_allocs));

	ADD(buf, *len, bufsize, "memory: free:%3d (%3d KiB) / total:%3d KiB)\n",
	    atomic_read(&ar->mem_free_blocks),
	    (atomic_read(&ar->mem_free_blocks) * ar->fw.mem_block_size) / 1024,
	    (ar->fw.mem_blocks * ar->fw.mem_block_size) / 1024);

	spin_unlock_bh(&ar->mem_lock);

	return buf;
}
DEBUGFS_DECLARE_RO_FILE(mem_usage, 512);

static char *carl9170_debugfs_qos_stat_read(struct ar9170 *ar, char *buf,
					    size_t bufsize, ssize_t *len)
{
	ADD(buf, *len, bufsize, "%s QoS AC\n", modparam_noht ? "Hardware" :
	    "Software");

	ADD(buf, *len, bufsize, "[     VO            VI       "
				 "     BE            BK      ]\n");

	spin_lock_bh(&ar->tx_stats_lock);
	ADD(buf, *len, bufsize, "[length/limit  length/limit  "
				 "length/limit  length/limit ]\n"
				"[   %3d/%3d       %3d/%3d    "
				 "   %3d/%3d       %3d/%3d   ]\n\n",
	    ar->tx_stats[0].len, ar->tx_stats[0].limit,
	    ar->tx_stats[1].len, ar->tx_stats[1].limit,
	    ar->tx_stats[2].len, ar->tx_stats[2].limit,
	    ar->tx_stats[3].len, ar->tx_stats[3].limit);

	ADD(buf, *len, bufsize, "[    total         total     "
				 "    total         total    ]\n"
				"[%10d    %10d    %10d    %10d   ]\n\n",
	    ar->tx_stats[0].count, ar->tx_stats[1].count,
	    ar->tx_stats[2].count, ar->tx_stats[3].count);

	spin_unlock_bh(&ar->tx_stats_lock);

	ADD(buf, *len, bufsize, "[  pend/waittx   pend/waittx "
				 "  pend/waittx   pend/waittx]\n"
				"[   %3d/%3d       %3d/%3d    "
				 "   %3d/%3d       %3d/%3d   ]\n\n",
	    skb_queue_len(&ar->tx_pending[0]),
	    skb_queue_len(&ar->tx_status[0]),
	    skb_queue_len(&ar->tx_pending[1]),
	    skb_queue_len(&ar->tx_status[1]),
	    skb_queue_len(&ar->tx_pending[2]),
	    skb_queue_len(&ar->tx_status[2]),
	    skb_queue_len(&ar->tx_pending[3]),
	    skb_queue_len(&ar->tx_status[3]));

	return buf;
}
DEBUGFS_DECLARE_RO_FILE(qos_stat, 512);

static void carl9170_debugfs_format_frame(struct ar9170 *ar,
	struct sk_buff *skb, const char *prefix, char *buf,
	ssize_t *off, ssize_t bufsize)
{
	struct _carl9170_tx_superframe *txc = (void *) skb->data;
	struct ieee80211_tx_info *txinfo = IEEE80211_SKB_CB(skb);
	struct carl9170_tx_info *arinfo = (void *) txinfo->rate_driver_data;
	struct ieee80211_hdr *hdr = (void *) txc->frame_data;

	ADD(buf, *off, bufsize, "%s %p, c:%2x, DA:%pM, sq:%4d, mc:%.4x, "
	    "pc:%.8x, to:%d ms\n", prefix, skb, txc->s.cookie,
	    ieee80211_get_DA(hdr), get_seq_h(hdr),
	    le16_to_cpu(txc->f.mac_control), le32_to_cpu(txc->f.phy_control),
	    jiffies_to_msecs(jiffies - arinfo->timeout));
}


static char *carl9170_debugfs_ampdu_state_read(struct ar9170 *ar, char *buf,
					       size_t bufsize, ssize_t *len)
{
	struct carl9170_sta_tid *iter;
	struct sk_buff *skb;
	int cnt = 0, fc;
	int offset;

	rcu_read_lock();
	list_for_each_entry_rcu(iter, &ar->tx_ampdu_list, list) {

		spin_lock_bh(&iter->lock);
		ADD(buf, *len, bufsize, "Entry: #%2d TID:%1d, BSN:%4d, "
		    "SNX:%4d, HSN:%4d, BAW:%2d, state:%1d, toggles:%d\n",
		    cnt, iter->tid, iter->bsn, iter->snx, iter->hsn,
		    iter->max, iter->state, iter->counter);

		ADD(buf, *len, bufsize, "\tWindow:  [");

		*len += bitmap_scnprintf(&buf[*len], bufsize - *len,
			iter->bitmap, CARL9170_BAW_BITS);

#define BM_STR_OFF(offset)					\
	((CARL9170_BAW_BITS - (offset) - 1) / 4 +		\
	 (CARL9170_BAW_BITS - (offset) - 1) / 32 + 1)

		ADD(buf, *len, bufsize, ",W]\n");

		offset = BM_STR_OFF(0);
		ADD(buf, *len, bufsize, "\tBase Seq: %*s\n", offset, "T");

		offset = BM_STR_OFF(SEQ_DIFF(iter->snx, iter->bsn));
		ADD(buf, *len, bufsize, "\tNext Seq: %*s\n", offset, "W");

		offset = BM_STR_OFF(((int)iter->hsn - (int)iter->bsn) %
				     CARL9170_BAW_BITS);
		ADD(buf, *len, bufsize, "\tLast Seq: %*s\n", offset, "N");

		ADD(buf, *len, bufsize, "\tPre-Aggregation reorder buffer: "
		    " currently queued:%d\n", skb_queue_len(&iter->queue));

		fc = 0;
		skb_queue_walk(&iter->queue, skb) {
			char prefix[32];

			snprintf(prefix, sizeof(prefix), "\t\t%3d :", fc);
			carl9170_debugfs_format_frame(ar, skb, prefix, buf,
						      len, bufsize);

			fc++;
		}
		spin_unlock_bh(&iter->lock);
		cnt++;
	}
	rcu_read_unlock();

	return buf;
}
DEBUGFS_DECLARE_RO_FILE(ampdu_state, 8000);

static void carl9170_debugfs_queue_dump(struct ar9170 *ar, char *buf,
	ssize_t *len, size_t bufsize, struct sk_buff_head *queue)
{
	struct sk_buff *skb;
	char prefix[16];
	int fc = 0;

	spin_lock_bh(&queue->lock);
	skb_queue_walk(queue, skb) {
		snprintf(prefix, sizeof(prefix), "%3d :", fc);
		carl9170_debugfs_format_frame(ar, skb, prefix, buf,
					      len, bufsize);
		fc++;
	}
	spin_unlock_bh(&queue->lock);
}

#define DEBUGFS_QUEUE_DUMP(q, qi)					\
static char *carl9170_debugfs_##q ##_##qi ##_read(struct ar9170 *ar,	\
	char *buf, size_t bufsize, ssize_t *len)			\
{									\
	carl9170_debugfs_queue_dump(ar, buf, len, bufsize, &ar->q[qi]);	\
	return buf;							\
}									\
DEBUGFS_DECLARE_RO_FILE(q##_##qi, 8000);

static char *carl9170_debugfs_sta_psm_read(struct ar9170 *ar, char *buf,
					   size_t bufsize, ssize_t *len)
{
	ADD(buf, *len, bufsize, "psm state: %s\n", (ar->ps.off_override ?
	    "FORCE CAM" : (ar->ps.state ? "PSM" : "CAM")));

	ADD(buf, *len, bufsize, "sleep duration: %d ms.\n", ar->ps.sleep_ms);
	ADD(buf, *len, bufsize, "last power-state transition: %d ms ago.\n",
	    jiffies_to_msecs(jiffies - ar->ps.last_action));
	ADD(buf, *len, bufsize, "last CAM->PSM transition: %d ms ago.\n",
	    jiffies_to_msecs(jiffies - ar->ps.last_slept));

	return buf;
}
DEBUGFS_DECLARE_RO_FILE(sta_psm, 160);

static char *carl9170_debugfs_tx_stuck_read(struct ar9170 *ar, char *buf,
					    size_t bufsize, ssize_t *len)
{
	int i;

	for (i = 0; i < ar->hw->queues; i++) {
		ADD(buf, *len, bufsize, "TX queue [%d]: %10d max:%10d ms.\n",
		    i, ieee80211_queue_stopped(ar->hw, i) ?
		    jiffies_to_msecs(jiffies - ar->queue_stop_timeout[i]) : 0,
		    jiffies_to_msecs(ar->max_queue_stop_timeout[i]));

		ar->max_queue_stop_timeout[i] = 0;
	}

	return buf;
}
DEBUGFS_DECLARE_RO_FILE(tx_stuck, 180);

static char *carl9170_debugfs_phy_noise_read(struct ar9170 *ar, char *buf,
					     size_t bufsize, ssize_t *len)
{
	int err;

	err = carl9170_get_noisefloor(ar);
	if (err) {
		*len = err;
		return buf;
	}

	ADD(buf, *len, bufsize, "Chain 0: %10d dBm, ext. chan.:%10d dBm\n",
	    ar->noise[0], ar->noise[2]);
	ADD(buf, *len, bufsize, "Chain 2: %10d dBm, ext. chan.:%10d dBm\n",
	    ar->noise[1], ar->noise[3]);

	return buf;
}
DEBUGFS_DECLARE_RO_FILE(phy_noise, 180);

static char *carl9170_debugfs_vif_dump_read(struct ar9170 *ar, char *buf,
					    size_t bufsize, ssize_t *len)
{
	struct carl9170_vif_info *iter;
	int i = 0;

	ADD(buf, *len, bufsize, "registered VIFs:%d \\ %d\n",
	    ar->vifs, ar->fw.vif_num);

	ADD(buf, *len, bufsize, "VIF bitmap: [");

	*len += bitmap_scnprintf(&buf[*len], bufsize - *len,
				 &ar->vif_bitmap, ar->fw.vif_num);

	ADD(buf, *len, bufsize, "]\n");

	rcu_read_lock();
	list_for_each_entry_rcu(iter, &ar->vif_list, list) {
		struct ieee80211_vif *vif = carl9170_get_vif(iter);
		ADD(buf, *len, bufsize, "\t%d = [%s VIF, id:%d, type:%x "
		    " mac:%pM %s]\n", i, (carl9170_get_main_vif(ar) == vif ?
		    "Master" : " Slave"), iter->id, vif->type, vif->addr,
		    iter->enable_beacon ? "beaconing " : "");
		i++;
	}
	rcu_read_unlock();

	return buf;
}
DEBUGFS_DECLARE_RO_FILE(vif_dump, 8000);

#define UPDATE_COUNTER(ar, name)	({				\
	u32 __tmp[ARRAY_SIZE(name##_regs)];				\
	unsigned int __i, __err = -ENODEV;				\
									\
	for (__i = 0; __i < ARRAY_SIZE(name##_regs); __i++) {		\
		__tmp[__i] = name##_regs[__i].reg;			\
		ar->debug.stats.name##_counter[__i] = 0;		\
	}								\
									\
	if (IS_STARTED(ar))						\
		__err = carl9170_read_mreg(ar, ARRAY_SIZE(name##_regs),	\
			__tmp, ar->debug.stats.name##_counter);		\
	(__err); })

#define TALLY_SUM_UP(ar, name)	do {					\
	unsigned int __i;						\
									\
	for (__i = 0; __i < ARRAY_SIZE(name##_regs); __i++) {		\
		ar->debug.stats.name##_sum[__i] +=			\
			ar->debug.stats.name##_counter[__i];		\
	}								\
} while (0)

#define DEBUGFS_HW_TALLY_FILE(name, f)					\
static char *carl9170_debugfs_##name ## _read(struct ar9170 *ar,	\
	 char *dum, size_t bufsize, ssize_t *ret)			\
{									\
	char *buf;							\
	int i, max_len, err;						\
									\
	max_len = ARRAY_SIZE(name##_regs) * 80;				\
	buf = vmalloc(max_len);						\
	if (!buf)							\
		return NULL;						\
									\
	err = UPDATE_COUNTER(ar, name);					\
	if (err) {							\
		*ret = err;						\
		return buf;						\
	}								\
									\
	TALLY_SUM_UP(ar, name);						\
									\
	for (i = 0; i < ARRAY_SIZE(name##_regs); i++) {			\
		ADD(buf, *ret, max_len, "%22s = %" f "[+%" f "]\n",	\
		    name##_regs[i].nreg, ar->debug.stats.name ##_sum[i],\
		    ar->debug.stats.name ##_counter[i]);		\
	}								\
									\
	return buf;							\
}									\
DEBUGFS_DECLARE_RO_FILE(name, 0);

#define DEBUGFS_HW_REG_FILE(name, f)					\
static char *carl9170_debugfs_##name ## _read(struct ar9170 *ar,	\
	char *dum, size_t bufsize, ssize_t *ret)			\
{									\
	char *buf;							\
	int i, max_len, err;						\
									\
	max_len = ARRAY_SIZE(name##_regs) * 80;				\
	buf = vmalloc(max_len);						\
	if (!buf)							\
		return NULL;						\
									\
	err = UPDATE_COUNTER(ar, name);					\
	if (err) {							\
		*ret = err;						\
		return buf;						\
	}								\
									\
	for (i = 0; i < ARRAY_SIZE(name##_regs); i++) {			\
		ADD(buf, *ret, max_len, "%22s = %" f "\n",		\
		    name##_regs[i].nreg,				\
		    ar->debug.stats.name##_counter[i]);			\
	}								\
									\
	return buf;							\
}									\
DEBUGFS_DECLARE_RO_FILE(name, 0);

static ssize_t carl9170_debugfs_hw_ioread32_write(struct ar9170 *ar,
	const char *buf, size_t count)
{
	int err = 0, i, n = 0, max_len = 32, res;
	unsigned int reg, tmp;

	if (!count)
		return 0;

	if (count > max_len)
		return -E2BIG;

	res = sscanf(buf, "0x%X %d", &reg, &n);
	if (res < 1) {
		err = -EINVAL;
		goto out;
	}

	if (res == 1)
		n = 1;

	if (n > 15) {
		err = -EMSGSIZE;
		goto out;
	}

	if ((reg >= 0x280000) || ((reg + (n << 2)) >= 0x280000)) {
		err = -EADDRNOTAVAIL;
		goto out;
	}

	if (reg & 3) {
		err = -EINVAL;
		goto out;
	}

	for (i = 0; i < n; i++) {
		err = carl9170_read_reg(ar, reg + (i << 2), &tmp);
		if (err)
			goto out;

		ar->debug.ring[ar->debug.ring_tail].reg = reg + (i << 2);
		ar->debug.ring[ar->debug.ring_tail].value = tmp;
		ar->debug.ring_tail++;
		ar->debug.ring_tail %= CARL9170_DEBUG_RING_SIZE;
	}

out:
	return err ? err : count;
}

static char *carl9170_debugfs_hw_ioread32_read(struct ar9170 *ar, char *buf,
					       size_t bufsize, ssize_t *ret)
{
	int i = 0;

	while (ar->debug.ring_head != ar->debug.ring_tail) {
		ADD(buf, *ret, bufsize, "%.8x = %.8x\n",
		    ar->debug.ring[ar->debug.ring_head].reg,
		    ar->debug.ring[ar->debug.ring_head].value);

		ar->debug.ring_head++;
		ar->debug.ring_head %= CARL9170_DEBUG_RING_SIZE;

		if (i++ == 64)
			break;
	}
	ar->debug.ring_head = ar->debug.ring_tail;
	return buf;
}
DEBUGFS_DECLARE_RW_FILE(hw_ioread32, CARL9170_DEBUG_RING_SIZE * 40);

static ssize_t carl9170_debugfs_bug_write(struct ar9170 *ar, const char *buf,
					  size_t count)
{
	int err;

	if (count < 1)
		return -EINVAL;

	switch (buf[0]) {
	case 'F':
		ar->needs_full_reset = true;
		break;

	case 'R':
		if (!IS_STARTED(ar)) {
			err = -EAGAIN;
			goto out;
		}

		ar->needs_full_reset = false;
		break;

	case 'M':
		err = carl9170_mac_reset(ar);
		if (err < 0)
			count = err;

		goto out;

	case 'P':
		err = carl9170_set_channel(ar, ar->hw->conf.channel,
			ar->hw->conf.channel_type, CARL9170_RFI_COLD);
		if (err < 0)
			count = err;

		goto out;

	default:
		return -EINVAL;
	}

	carl9170_restart(ar, CARL9170_RR_USER_REQUEST);

out:
	return count;
}

static char *carl9170_debugfs_bug_read(struct ar9170 *ar, char *buf,
				       size_t bufsize, ssize_t *ret)
{
	ADD(buf, *ret, bufsize, "[P]hy reinit, [R]estart, [F]ull usb reset, "
	    "[M]ac reset\n");
	ADD(buf, *ret, bufsize, "firmware restarts:%d, last reason:%d\n",
		ar->restart_counter, ar->last_reason);
	ADD(buf, *ret, bufsize, "phy reinit errors:%d (%d)\n",
		ar->total_chan_fail, ar->chan_fail);
	ADD(buf, *ret, bufsize, "reported firmware errors:%d\n",
		ar->fw.err_counter);
	ADD(buf, *ret, bufsize, "reported firmware BUGs:%d\n",
		ar->fw.bug_counter);
	ADD(buf, *ret, bufsize, "pending restart requests:%d\n",
		atomic_read(&ar->pending_restarts));
	return buf;
}
__DEBUGFS_DECLARE_RW_FILE(bug, 400, CARL9170_STOPPED);

static const char *erp_modes[] = {
	[CARL9170_ERP_INVALID] = "INVALID",
	[CARL9170_ERP_AUTO] = "Automatic",
	[CARL9170_ERP_MAC80211] = "Set by MAC80211",
	[CARL9170_ERP_OFF] = "Force Off",
	[CARL9170_ERP_RTS] = "Force RTS",
	[CARL9170_ERP_CTS] = "Force CTS"
};

static char *carl9170_debugfs_erp_read(struct ar9170 *ar, char *buf,
				       size_t bufsize, ssize_t *ret)
{
	ADD(buf, *ret, bufsize, "ERP Setting: (%d) -> %s\n", ar->erp_mode,
	    erp_modes[ar->erp_mode]);
	return buf;
}

static ssize_t carl9170_debugfs_erp_write(struct ar9170 *ar, const char *buf,
					  size_t count)
{
	int res, val;

	if (count < 1)
		return -EINVAL;

	res = sscanf(buf, "%d", &val);
	if (res != 1)
		return -EINVAL;

	if (!((val > CARL9170_ERP_INVALID) &&
	      (val < __CARL9170_ERP_NUM)))
		return -EINVAL;

	ar->erp_mode = val;
	return count;
}

DEBUGFS_DECLARE_RW_FILE(erp, 80);

static ssize_t carl9170_debugfs_hw_iowrite32_write(struct ar9170 *ar,
	const char *buf, size_t count)
{
	int err = 0, max_len = 22, res;
	u32 reg, val;

	if (!count)
		return 0;

	if (count > max_len)
		return -E2BIG;

	res = sscanf(buf, "0x%X 0x%X", &reg, &val);
	if (res != 2) {
		err = -EINVAL;
		goto out;
	}

	if (reg <= 0x100000 || reg >= 0x280000) {
		err = -EADDRNOTAVAIL;
		goto out;
	}

	if (reg & 3) {
		err = -EINVAL;
		goto out;
	}

	err = carl9170_write_reg(ar, reg, val);
	if (err)
		goto out;

out:
	return err ? err : count;
}
DEBUGFS_DECLARE_WO_FILE(hw_iowrite32);

DEBUGFS_HW_TALLY_FILE(hw_tx_tally, "u");
DEBUGFS_HW_TALLY_FILE(hw_rx_tally, "u");
DEBUGFS_HW_TALLY_FILE(hw_phy_errors, "u");
DEBUGFS_HW_REG_FILE(hw_wlan_queue, ".8x");
DEBUGFS_HW_REG_FILE(hw_pta_queue, ".8x");
DEBUGFS_HW_REG_FILE(hw_ampdu_info, ".8x");
DEBUGFS_QUEUE_DUMP(tx_status, 0);
DEBUGFS_QUEUE_DUMP(tx_status, 1);
DEBUGFS_QUEUE_DUMP(tx_status, 2);
DEBUGFS_QUEUE_DUMP(tx_status, 3);
DEBUGFS_QUEUE_DUMP(tx_pending, 0);
DEBUGFS_QUEUE_DUMP(tx_pending, 1);
DEBUGFS_QUEUE_DUMP(tx_pending, 2);
DEBUGFS_QUEUE_DUMP(tx_pending, 3);
DEBUGFS_READONLY_FILE(usb_tx_anch_urbs, 20, "%d",
		      atomic_read(&ar->tx_anch_urbs));
DEBUGFS_READONLY_FILE(usb_rx_anch_urbs, 20, "%d",
		      atomic_read(&ar->rx_anch_urbs));
DEBUGFS_READONLY_FILE(usb_rx_work_urbs, 20, "%d",
		      atomic_read(&ar->rx_work_urbs));
DEBUGFS_READONLY_FILE(usb_rx_pool_urbs, 20, "%d",
		      atomic_read(&ar->rx_pool_urbs));

DEBUGFS_READONLY_FILE(tx_total_queued, 20, "%d",
		      atomic_read(&ar->tx_total_queued));
DEBUGFS_READONLY_FILE(tx_ampdu_scheduler, 20, "%d",
		      atomic_read(&ar->tx_ampdu_scheduler));

DEBUGFS_READONLY_FILE(tx_total_pending, 20, "%d",
		      atomic_read(&ar->tx_total_pending));

DEBUGFS_READONLY_FILE(tx_ampdu_list_len, 20, "%d",
		      ar->tx_ampdu_list_len);

DEBUGFS_READONLY_FILE(tx_ampdu_upload, 20, "%d",
		      atomic_read(&ar->tx_ampdu_upload));

DEBUGFS_READONLY_FILE(tx_janitor_last_run, 64, "last run:%d ms ago",
	jiffies_to_msecs(jiffies - ar->tx_janitor_last_run));

DEBUGFS_READONLY_FILE(tx_dropped, 20, "%d", ar->tx_dropped);

DEBUGFS_READONLY_FILE(rx_dropped, 20, "%d", ar->rx_dropped);

DEBUGFS_READONLY_FILE(sniffer_enabled, 20, "%d", ar->sniffer_enabled);
DEBUGFS_READONLY_FILE(rx_software_decryption, 20, "%d",
		      ar->rx_software_decryption);
DEBUGFS_READONLY_FILE(ampdu_factor, 20, "%d",
		      ar->current_factor);
DEBUGFS_READONLY_FILE(ampdu_density, 20, "%d",
		      ar->current_density);

DEBUGFS_READONLY_FILE(beacon_int, 20, "%d TU", ar->global_beacon_int);
DEBUGFS_READONLY_FILE(pretbtt, 20, "%d TU", ar->global_pretbtt);

void carl9170_debugfs_register(struct ar9170 *ar)
{
	ar->debug_dir = debugfs_create_dir(KBUILD_MODNAME,
		ar->hw->wiphy->debugfsdir);

#define DEBUGFS_ADD(name)						\
	debugfs_create_file(#name, carl_debugfs_##name ##_ops.attr,	\
			    ar->debug_dir, ar,				\
			    &carl_debugfs_##name ## _ops.fops);

	DEBUGFS_ADD(usb_tx_anch_urbs);
	DEBUGFS_ADD(usb_rx_pool_urbs);
	DEBUGFS_ADD(usb_rx_anch_urbs);
	DEBUGFS_ADD(usb_rx_work_urbs);

	DEBUGFS_ADD(tx_total_queued);
	DEBUGFS_ADD(tx_total_pending);
	DEBUGFS_ADD(tx_dropped);
	DEBUGFS_ADD(tx_stuck);
	DEBUGFS_ADD(tx_ampdu_upload);
	DEBUGFS_ADD(tx_ampdu_scheduler);
	DEBUGFS_ADD(tx_ampdu_list_len);

	DEBUGFS_ADD(rx_dropped);
	DEBUGFS_ADD(sniffer_enabled);
	DEBUGFS_ADD(rx_software_decryption);

	DEBUGFS_ADD(mem_usage);
	DEBUGFS_ADD(qos_stat);
	DEBUGFS_ADD(sta_psm);
	DEBUGFS_ADD(ampdu_state);

	DEBUGFS_ADD(hw_tx_tally);
	DEBUGFS_ADD(hw_rx_tally);
	DEBUGFS_ADD(hw_phy_errors);
	DEBUGFS_ADD(phy_noise);

	DEBUGFS_ADD(hw_wlan_queue);
	DEBUGFS_ADD(hw_pta_queue);
	DEBUGFS_ADD(hw_ampdu_info);

	DEBUGFS_ADD(ampdu_density);
	DEBUGFS_ADD(ampdu_factor);

	DEBUGFS_ADD(tx_janitor_last_run);

	DEBUGFS_ADD(tx_status_0);
	DEBUGFS_ADD(tx_status_1);
	DEBUGFS_ADD(tx_status_2);
	DEBUGFS_ADD(tx_status_3);

	DEBUGFS_ADD(tx_pending_0);
	DEBUGFS_ADD(tx_pending_1);
	DEBUGFS_ADD(tx_pending_2);
	DEBUGFS_ADD(tx_pending_3);

	DEBUGFS_ADD(hw_ioread32);
	DEBUGFS_ADD(hw_iowrite32);
	DEBUGFS_ADD(bug);

	DEBUGFS_ADD(erp);

	DEBUGFS_ADD(vif_dump);

	DEBUGFS_ADD(beacon_int);
	DEBUGFS_ADD(pretbtt);

#undef DEBUGFS_ADD
}

void carl9170_debugfs_unregister(struct ar9170 *ar)
{
	debugfs_remove_recursive(ar->debug_dir);
}
