/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "core.h"

#include <linux/skbuff.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/export.h>

#include "debug.h"
#include "target.h"

struct ath6kl_fwlog_slot {
	__le32 timestamp;
	__le32 length;

	/* max ATH6KL_FWLOG_PAYLOAD_SIZE bytes */
	u8 payload[0];
};

#define ATH6KL_FWLOG_MAX_ENTRIES 20

#define ATH6KL_FWLOG_VALID_MASK 0x1ffff

void ath6kl_printk(const char *level, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk("%sath6kl: %pV", level, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(ath6kl_printk);

void ath6kl_info(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	ath6kl_printk(KERN_INFO, "%pV", &vaf);
	trace_ath6kl_log_info(&vaf);
	va_end(args);
}
EXPORT_SYMBOL(ath6kl_info);

void ath6kl_err(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	ath6kl_printk(KERN_ERR, "%pV", &vaf);
	trace_ath6kl_log_err(&vaf);
	va_end(args);
}
EXPORT_SYMBOL(ath6kl_err);

void ath6kl_warn(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	ath6kl_printk(KERN_WARNING, "%pV", &vaf);
	trace_ath6kl_log_warn(&vaf);
	va_end(args);
}
EXPORT_SYMBOL(ath6kl_warn);

int ath6kl_read_tgt_stats(struct ath6kl *ar, struct ath6kl_vif *vif)
{
	long left;

	if (down_interruptible(&ar->sem))
		return -EBUSY;

	set_bit(STATS_UPDATE_PEND, &vif->flags);

	if (ath6kl_wmi_get_stats_cmd(ar->wmi, 0)) {
		up(&ar->sem);
		return -EIO;
	}

	left = wait_event_interruptible_timeout(ar->event_wq,
						!test_bit(STATS_UPDATE_PEND,
						&vif->flags), WMI_TIMEOUT);

	up(&ar->sem);

	if (left <= 0)
		return -ETIMEDOUT;

	return 0;
}
EXPORT_SYMBOL(ath6kl_read_tgt_stats);

#ifdef CONFIG_ATH6KL_DEBUG

void ath6kl_dbg(enum ATH6K_DEBUG_MASK mask, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (debug_mask & mask)
		ath6kl_printk(KERN_DEBUG, "%pV", &vaf);

	trace_ath6kl_log_dbg(mask, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(ath6kl_dbg);

void ath6kl_dbg_dump(enum ATH6K_DEBUG_MASK mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len)
{
	if (debug_mask & mask) {
		if (msg)
			ath6kl_dbg(mask, "%s\n", msg);

		print_hex_dump_bytes(prefix, DUMP_PREFIX_OFFSET, buf, len);
	}

	/* tracing code doesn't like null strings :/ */
	trace_ath6kl_log_dbg_dump(msg ? msg : "", prefix ? prefix : "",
				  buf, len);
}
EXPORT_SYMBOL(ath6kl_dbg_dump);

#define REG_OUTPUT_LEN_PER_LINE	25
#define REGTYPE_STR_LEN		100

struct ath6kl_diag_reg_info {
	u32 reg_start;
	u32 reg_end;
	const char *reg_info;
};

static const struct ath6kl_diag_reg_info diag_reg[] = {
	{ 0x20000, 0x200fc, "General DMA and Rx registers" },
	{ 0x28000, 0x28900, "MAC PCU register & keycache" },
	{ 0x20800, 0x20a40, "QCU" },
	{ 0x21000, 0x212f0, "DCU" },
	{ 0x4000,  0x42e4, "RTC" },
	{ 0x540000, 0x540000 + (256 * 1024), "RAM" },
	{ 0x29800, 0x2B210, "Base Band" },
	{ 0x1C000, 0x1C748, "Analog" },
};

void ath6kl_dump_registers(struct ath6kl_device *dev,
			   struct ath6kl_irq_proc_registers *irq_proc_reg,
			   struct ath6kl_irq_enable_reg *irq_enable_reg)
{
	ath6kl_dbg(ATH6KL_DBG_IRQ, ("<------- Register Table -------->\n"));

	if (irq_proc_reg != NULL) {
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "Host Int status:           0x%x\n",
			   irq_proc_reg->host_int_status);
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "CPU Int status:            0x%x\n",
			   irq_proc_reg->cpu_int_status);
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "Error Int status:          0x%x\n",
			   irq_proc_reg->error_int_status);
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "Counter Int status:        0x%x\n",
			   irq_proc_reg->counter_int_status);
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "Mbox Frame:                0x%x\n",
			   irq_proc_reg->mbox_frame);
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "Rx Lookahead Valid:        0x%x\n",
			   irq_proc_reg->rx_lkahd_valid);
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "Rx Lookahead 0:            0x%x\n",
			   irq_proc_reg->rx_lkahd[0]);
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "Rx Lookahead 1:            0x%x\n",
			   irq_proc_reg->rx_lkahd[1]);

		if (dev->ar->mbox_info.gmbox_addr != 0) {
			/*
			 * If the target supports GMBOX hardware, dump some
			 * additional state.
			 */
			ath6kl_dbg(ATH6KL_DBG_IRQ,
				   "GMBOX Host Int status 2:   0x%x\n",
				   irq_proc_reg->host_int_status2);
			ath6kl_dbg(ATH6KL_DBG_IRQ,
				   "GMBOX RX Avail:            0x%x\n",
				   irq_proc_reg->gmbox_rx_avail);
			ath6kl_dbg(ATH6KL_DBG_IRQ,
				   "GMBOX lookahead alias 0:   0x%x\n",
				   irq_proc_reg->rx_gmbox_lkahd_alias[0]);
			ath6kl_dbg(ATH6KL_DBG_IRQ,
				   "GMBOX lookahead alias 1:   0x%x\n",
				   irq_proc_reg->rx_gmbox_lkahd_alias[1]);
		}
	}

	if (irq_enable_reg != NULL) {
		ath6kl_dbg(ATH6KL_DBG_IRQ,
			   "Int status Enable:         0x%x\n",
			   irq_enable_reg->int_status_en);
		ath6kl_dbg(ATH6KL_DBG_IRQ, "Counter Int status Enable: 0x%x\n",
			   irq_enable_reg->cntr_int_status_en);
	}
	ath6kl_dbg(ATH6KL_DBG_IRQ, "<------------------------------->\n");
}

static void dump_cred_dist(struct htc_endpoint_credit_dist *ep_dist)
{
	ath6kl_dbg(ATH6KL_DBG_CREDIT,
		   "--- endpoint: %d  svc_id: 0x%X ---\n",
		   ep_dist->endpoint, ep_dist->svc_id);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " dist_flags     : 0x%X\n",
		   ep_dist->dist_flags);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " cred_norm      : %d\n",
		   ep_dist->cred_norm);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " cred_min       : %d\n",
		   ep_dist->cred_min);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " credits        : %d\n",
		   ep_dist->credits);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " cred_assngd    : %d\n",
		   ep_dist->cred_assngd);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " seek_cred      : %d\n",
		   ep_dist->seek_cred);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " cred_sz        : %d\n",
		   ep_dist->cred_sz);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " cred_per_msg   : %d\n",
		   ep_dist->cred_per_msg);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " cred_to_dist   : %d\n",
		   ep_dist->cred_to_dist);
	ath6kl_dbg(ATH6KL_DBG_CREDIT, " txq_depth      : %d\n",
		   get_queue_depth(&ep_dist->htc_ep->txq));
	ath6kl_dbg(ATH6KL_DBG_CREDIT,
		   "----------------------------------\n");
}

/* FIXME: move to htc.c */
void dump_cred_dist_stats(struct htc_target *target)
{
	struct htc_endpoint_credit_dist *ep_list;

	list_for_each_entry(ep_list, &target->cred_dist_list, list)
		dump_cred_dist(ep_list);

	ath6kl_dbg(ATH6KL_DBG_CREDIT,
		   "credit distribution total %d free %d\n",
		   target->credit_info->total_avail_credits,
		   target->credit_info->cur_free_credits);
}

void ath6kl_debug_war(struct ath6kl *ar, enum ath6kl_war war)
{
	switch (war) {
	case ATH6KL_WAR_INVALID_RATE:
		ar->debug.war_stats.invalid_rate++;
		break;
	}
}

static ssize_t read_file_war_stats(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	char *buf;
	unsigned int len = 0, buf_len = 1500;
	ssize_t ret_cnt;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%25s\n",
			 "Workaround stats");
	len += scnprintf(buf + len, buf_len - len, "%25s\n\n",
			 "=================");
	len += scnprintf(buf + len, buf_len - len, "%20s %10u\n",
			 "Invalid rates", ar->debug.war_stats.invalid_rate);

	if (WARN_ON(len > buf_len))
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret_cnt;
}

static const struct file_operations fops_war_stats = {
	.read = read_file_war_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath6kl_debug_fwlog_event(struct ath6kl *ar, const void *buf, size_t len)
{
	struct ath6kl_fwlog_slot *slot;
	struct sk_buff *skb;
	size_t slot_len;

	if (WARN_ON(len > ATH6KL_FWLOG_PAYLOAD_SIZE))
		return;

	slot_len = sizeof(*slot) + ATH6KL_FWLOG_PAYLOAD_SIZE;

	skb = alloc_skb(slot_len, GFP_KERNEL);
	if (!skb)
		return;

	slot = (struct ath6kl_fwlog_slot *) skb_put(skb, slot_len);
	slot->timestamp = cpu_to_le32(jiffies);
	slot->length = cpu_to_le32(len);
	memcpy(slot->payload, buf, len);

	/* Need to pad each record to fixed length ATH6KL_FWLOG_PAYLOAD_SIZE */
	memset(slot->payload + len, 0, ATH6KL_FWLOG_PAYLOAD_SIZE - len);

	spin_lock(&ar->debug.fwlog_queue.lock);

	__skb_queue_tail(&ar->debug.fwlog_queue, skb);
	complete(&ar->debug.fwlog_completion);

	/* drop oldest entries */
	while (skb_queue_len(&ar->debug.fwlog_queue) >
	       ATH6KL_FWLOG_MAX_ENTRIES) {
		skb = __skb_dequeue(&ar->debug.fwlog_queue);
		kfree_skb(skb);
	}

	spin_unlock(&ar->debug.fwlog_queue.lock);

	return;
}

static int ath6kl_fwlog_open(struct inode *inode, struct file *file)
{
	struct ath6kl *ar = inode->i_private;

	if (ar->debug.fwlog_open)
		return -EBUSY;

	ar->debug.fwlog_open = true;

	file->private_data = inode->i_private;
	return 0;
}

static int ath6kl_fwlog_release(struct inode *inode, struct file *file)
{
	struct ath6kl *ar = inode->i_private;

	ar->debug.fwlog_open = false;

	return 0;
}

static ssize_t ath6kl_fwlog_read(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct sk_buff *skb;
	ssize_t ret_cnt;
	size_t len = 0;
	char *buf;

	buf = vmalloc(count);
	if (!buf)
		return -ENOMEM;

	/* read undelivered logs from firmware */
	ath6kl_read_fwlogs(ar);

	spin_lock(&ar->debug.fwlog_queue.lock);

	while ((skb = __skb_dequeue(&ar->debug.fwlog_queue))) {
		if (skb->len > count - len) {
			/* not enough space, put skb back and leave */
			__skb_queue_head(&ar->debug.fwlog_queue, skb);
			break;
		}


		memcpy(buf + len, skb->data, skb->len);
		len += skb->len;

		kfree_skb(skb);
	}

	spin_unlock(&ar->debug.fwlog_queue.lock);

	/* FIXME: what to do if len == 0? */

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	vfree(buf);

	return ret_cnt;
}

static const struct file_operations fops_fwlog = {
	.open = ath6kl_fwlog_open,
	.release = ath6kl_fwlog_release,
	.read = ath6kl_fwlog_read,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_fwlog_block_read(struct file *file,
				       char __user *user_buf,
				       size_t count,
				       loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct sk_buff *skb;
	ssize_t ret_cnt;
	size_t len = 0, not_copied;
	char *buf;
	int ret;

	buf = vmalloc(count);
	if (!buf)
		return -ENOMEM;

	spin_lock(&ar->debug.fwlog_queue.lock);

	if (skb_queue_len(&ar->debug.fwlog_queue) == 0) {
		/* we must init under queue lock */
		init_completion(&ar->debug.fwlog_completion);

		spin_unlock(&ar->debug.fwlog_queue.lock);

		ret = wait_for_completion_interruptible(
			&ar->debug.fwlog_completion);
		if (ret == -ERESTARTSYS) {
			vfree(buf);
			return ret;
		}

		spin_lock(&ar->debug.fwlog_queue.lock);
	}

	while ((skb = __skb_dequeue(&ar->debug.fwlog_queue))) {
		if (skb->len > count - len) {
			/* not enough space, put skb back and leave */
			__skb_queue_head(&ar->debug.fwlog_queue, skb);
			break;
		}


		memcpy(buf + len, skb->data, skb->len);
		len += skb->len;

		kfree_skb(skb);
	}

	spin_unlock(&ar->debug.fwlog_queue.lock);

	/* FIXME: what to do if len == 0? */

	not_copied = copy_to_user(user_buf, buf, len);
	if (not_copied != 0) {
		ret_cnt = -EFAULT;
		goto out;
	}

	*ppos = *ppos + len;

	ret_cnt = len;

out:
	vfree(buf);

	return ret_cnt;
}

static const struct file_operations fops_fwlog_block = {
	.open = ath6kl_fwlog_open,
	.release = ath6kl_fwlog_release,
	.read = ath6kl_fwlog_block_read,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_fwlog_mask_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	char buf[16];
	int len;

	len = snprintf(buf, sizeof(buf), "0x%x\n", ar->debug.fwlog_mask);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath6kl_fwlog_mask_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	int ret;

	ret = kstrtou32_from_user(user_buf, count, 0, &ar->debug.fwlog_mask);
	if (ret)
		return ret;

	ret = ath6kl_wmi_config_debug_module_cmd(ar->wmi,
						 ATH6KL_FWLOG_VALID_MASK,
						 ar->debug.fwlog_mask);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations fops_fwlog_mask = {
	.open = simple_open,
	.read = ath6kl_fwlog_mask_read,
	.write = ath6kl_fwlog_mask_write,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_tgt_stats(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct ath6kl_vif *vif;
	struct target_stats *tgt_stats;
	char *buf;
	unsigned int len = 0, buf_len = 1500;
	int i;
	ssize_t ret_cnt;
	int rv;

	vif = ath6kl_vif_first(ar);
	if (!vif)
		return -EIO;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rv = ath6kl_read_tgt_stats(ar, vif);
	if (rv < 0) {
		kfree(buf);
		return rv;
	}

	tgt_stats = &vif->target_stats;

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%25s\n",
			 "Target Tx stats");
	len += scnprintf(buf + len, buf_len - len, "%25s\n\n",
			 "=================");
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Ucast packets", tgt_stats->tx_ucast_pkt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Bcast packets", tgt_stats->tx_bcast_pkt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Ucast byte", tgt_stats->tx_ucast_byte);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Bcast byte", tgt_stats->tx_bcast_byte);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Rts success cnt", tgt_stats->tx_rts_success_cnt);
	for (i = 0; i < 4; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%18s %d %10llu\n", "PER on ac",
				 i, tgt_stats->tx_pkt_per_ac[i]);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Error", tgt_stats->tx_err);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Fail count", tgt_stats->tx_fail_cnt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Retry count", tgt_stats->tx_retry_cnt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Multi retry cnt", tgt_stats->tx_mult_retry_cnt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Rts fail cnt", tgt_stats->tx_rts_fail_cnt);
	len += scnprintf(buf + len, buf_len - len, "%25s %10llu\n\n",
			 "TKIP counter measure used",
			 tgt_stats->tkip_cnter_measures_invoked);

	len += scnprintf(buf + len, buf_len - len, "%25s\n",
			 "Target Rx stats");
	len += scnprintf(buf + len, buf_len - len, "%25s\n",
			 "=================");

	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Ucast packets", tgt_stats->rx_ucast_pkt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10d\n",
			 "Ucast Rate", tgt_stats->rx_ucast_rate);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Bcast packets", tgt_stats->rx_bcast_pkt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Ucast byte", tgt_stats->rx_ucast_byte);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Bcast byte", tgt_stats->rx_bcast_byte);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Fragmented pkt", tgt_stats->rx_frgment_pkt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Error", tgt_stats->rx_err);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "CRC Err", tgt_stats->rx_crc_err);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Key chache miss", tgt_stats->rx_key_cache_miss);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Decrypt Err", tgt_stats->rx_decrypt_err);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Duplicate frame", tgt_stats->rx_dupl_frame);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Tkip Mic failure", tgt_stats->tkip_local_mic_fail);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "TKIP format err", tgt_stats->tkip_fmt_err);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "CCMP format Err", tgt_stats->ccmp_fmt_err);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n\n",
			 "CCMP Replay Err", tgt_stats->ccmp_replays);

	len += scnprintf(buf + len, buf_len - len, "%25s\n",
			 "Misc Target stats");
	len += scnprintf(buf + len, buf_len - len, "%25s\n",
			 "=================");
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Beacon Miss count", tgt_stats->cs_bmiss_cnt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Num Connects", tgt_stats->cs_connect_cnt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10llu\n",
			 "Num disconnects", tgt_stats->cs_discon_cnt);
	len += scnprintf(buf + len, buf_len - len, "%20s %10d\n",
			 "Beacon avg rssi", tgt_stats->cs_ave_beacon_rssi);
	len += scnprintf(buf + len, buf_len - len, "%20s %10d\n",
			 "ARP pkt received", tgt_stats->arp_received);
	len += scnprintf(buf + len, buf_len - len, "%20s %10d\n",
			 "ARP pkt matched", tgt_stats->arp_matched);
	len += scnprintf(buf + len, buf_len - len, "%20s %10d\n",
			 "ARP pkt replied", tgt_stats->arp_replied);

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret_cnt;
}

static const struct file_operations fops_tgt_stats = {
	.read = read_file_tgt_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define print_credit_info(fmt_str, ep_list_field)		\
	(len += scnprintf(buf + len, buf_len - len, fmt_str,	\
			 ep_list->ep_list_field))
#define CREDIT_INFO_DISPLAY_STRING_LEN	200
#define CREDIT_INFO_LEN	128

static ssize_t read_file_credit_dist_stats(struct file *file,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct htc_target *target = ar->htc_target;
	struct htc_endpoint_credit_dist *ep_list;
	char *buf;
	unsigned int buf_len, len = 0;
	ssize_t ret_cnt;

	buf_len = CREDIT_INFO_DISPLAY_STRING_LEN +
		  get_queue_depth(&target->cred_dist_list) * CREDIT_INFO_LEN;
	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, buf_len - len, "%25s%5d\n",
			 "Total Avail Credits: ",
			 target->credit_info->total_avail_credits);
	len += scnprintf(buf + len, buf_len - len, "%25s%5d\n",
			 "Free credits :",
			 target->credit_info->cur_free_credits);

	len += scnprintf(buf + len, buf_len - len,
			 " Epid  Flags    Cred_norm  Cred_min  Credits  Cred_assngd"
			 "  Seek_cred  Cred_sz  Cred_per_msg  Cred_to_dist"
			 "  qdepth\n");

	list_for_each_entry(ep_list, &target->cred_dist_list, list) {
		print_credit_info("  %2d", endpoint);
		print_credit_info("%10x", dist_flags);
		print_credit_info("%8d", cred_norm);
		print_credit_info("%9d", cred_min);
		print_credit_info("%9d", credits);
		print_credit_info("%10d", cred_assngd);
		print_credit_info("%13d", seek_cred);
		print_credit_info("%12d", cred_sz);
		print_credit_info("%9d", cred_per_msg);
		print_credit_info("%14d", cred_to_dist);
		len += scnprintf(buf + len, buf_len - len, "%12d\n",
				 get_queue_depth(&ep_list->htc_ep->txq));
	}

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return ret_cnt;
}

static const struct file_operations fops_credit_dist_stats = {
	.read = read_file_credit_dist_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static unsigned int print_endpoint_stat(struct htc_target *target, char *buf,
					unsigned int buf_len, unsigned int len,
					int offset, const char *name)
{
	int i;
	struct htc_endpoint_stats *ep_st;
	u32 *counter;

	len += scnprintf(buf + len, buf_len - len, "%s:", name);
	for (i = 0; i < ENDPOINT_MAX; i++) {
		ep_st = &target->endpoint[i].ep_st;
		counter = ((u32 *) ep_st) + (offset / 4);
		len += scnprintf(buf + len, buf_len - len, " %u", *counter);
	}
	len += scnprintf(buf + len, buf_len - len, "\n");

	return len;
}

static ssize_t ath6kl_endpoint_stats_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct htc_target *target = ar->htc_target;
	char *buf;
	unsigned int buf_len, len = 0;
	ssize_t ret_cnt;

	buf_len = sizeof(struct htc_endpoint_stats) / sizeof(u32) *
		(25 + ENDPOINT_MAX * 11);
	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

#define EPSTAT(name)							\
	do {								\
		len = print_endpoint_stat(target, buf, buf_len, len,	\
					  offsetof(struct htc_endpoint_stats, \
						   name),		\
					  #name);			\
	} while (0)

	EPSTAT(cred_low_indicate);
	EPSTAT(tx_issued);
	EPSTAT(tx_pkt_bundled);
	EPSTAT(tx_bundles);
	EPSTAT(tx_dropped);
	EPSTAT(tx_cred_rpt);
	EPSTAT(cred_rpt_from_rx);
	EPSTAT(cred_rpt_from_other);
	EPSTAT(cred_rpt_ep0);
	EPSTAT(cred_from_rx);
	EPSTAT(cred_from_other);
	EPSTAT(cred_from_ep0);
	EPSTAT(cred_cosumd);
	EPSTAT(cred_retnd);
	EPSTAT(rx_pkts);
	EPSTAT(rx_lkahds);
	EPSTAT(rx_bundl);
	EPSTAT(rx_bundle_lkahd);
	EPSTAT(rx_bundle_from_hdr);
	EPSTAT(rx_alloc_thresh_hit);
	EPSTAT(rxalloc_thresh_byte);
#undef EPSTAT

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return ret_cnt;
}

static ssize_t ath6kl_endpoint_stats_write(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct htc_target *target = ar->htc_target;
	int ret, i;
	u32 val;
	struct htc_endpoint_stats *ep_st;

	ret = kstrtou32_from_user(user_buf, count, 0, &val);
	if (ret)
		return ret;
	if (val == 0) {
		for (i = 0; i < ENDPOINT_MAX; i++) {
			ep_st = &target->endpoint[i].ep_st;
			memset(ep_st, 0, sizeof(*ep_st));
		}
	}

	return count;
}

static const struct file_operations fops_endpoint_stats = {
	.open = simple_open,
	.read = ath6kl_endpoint_stats_read,
	.write = ath6kl_endpoint_stats_write,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static unsigned long ath6kl_get_num_reg(void)
{
	int i;
	unsigned long n_reg = 0;

	for (i = 0; i < ARRAY_SIZE(diag_reg); i++)
		n_reg = n_reg +
		     (diag_reg[i].reg_end - diag_reg[i].reg_start) / 4 + 1;

	return n_reg;
}

static bool ath6kl_dbg_is_diag_reg_valid(u32 reg_addr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(diag_reg); i++) {
		if (reg_addr >= diag_reg[i].reg_start &&
		    reg_addr <= diag_reg[i].reg_end)
			return true;
	}

	return false;
}

static ssize_t ath6kl_regread_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	u8 buf[50];
	unsigned int len = 0;

	if (ar->debug.dbgfs_diag_reg)
		len += scnprintf(buf + len, sizeof(buf) - len, "0x%x\n",
				ar->debug.dbgfs_diag_reg);
	else
		len += scnprintf(buf + len, sizeof(buf) - len,
				 "All diag registers\n");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath6kl_regread_write(struct file *file,
				    const char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	unsigned long reg_addr;

	if (kstrtoul_from_user(user_buf, count, 0, &reg_addr))
		return -EINVAL;

	if ((reg_addr % 4) != 0)
		return -EINVAL;

	if (reg_addr && !ath6kl_dbg_is_diag_reg_valid(reg_addr))
		return -EINVAL;

	ar->debug.dbgfs_diag_reg = reg_addr;

	return count;
}

static const struct file_operations fops_diag_reg_read = {
	.read = ath6kl_regread_read,
	.write = ath6kl_regread_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath6kl_regdump_open(struct inode *inode, struct file *file)
{
	struct ath6kl *ar = inode->i_private;
	u8 *buf;
	unsigned long int reg_len;
	unsigned int len = 0, n_reg;
	u32 addr;
	__le32 reg_val;
	int i, status;

	/* Dump all the registers if no register is specified */
	if (!ar->debug.dbgfs_diag_reg)
		n_reg = ath6kl_get_num_reg();
	else
		n_reg = 1;

	reg_len = n_reg * REG_OUTPUT_LEN_PER_LINE;
	if (n_reg > 1)
		reg_len += REGTYPE_STR_LEN;

	buf = vmalloc(reg_len);
	if (!buf)
		return -ENOMEM;

	if (n_reg == 1) {
		addr = ar->debug.dbgfs_diag_reg;

		status = ath6kl_diag_read32(ar,
				TARG_VTOP(ar->target_type, addr),
				(u32 *)&reg_val);
		if (status)
			goto fail_reg_read;

		len += scnprintf(buf + len, reg_len - len,
				 "0x%06x 0x%08x\n", addr, le32_to_cpu(reg_val));
		goto done;
	}

	for (i = 0; i < ARRAY_SIZE(diag_reg); i++) {
		len += scnprintf(buf + len, reg_len - len,
				"%s\n", diag_reg[i].reg_info);
		for (addr = diag_reg[i].reg_start;
		     addr <= diag_reg[i].reg_end; addr += 4) {
			status = ath6kl_diag_read32(ar,
					TARG_VTOP(ar->target_type, addr),
					(u32 *)&reg_val);
			if (status)
				goto fail_reg_read;

			len += scnprintf(buf + len, reg_len - len,
					"0x%06x 0x%08x\n",
					addr, le32_to_cpu(reg_val));
		}
	}

done:
	file->private_data = buf;
	return 0;

fail_reg_read:
	ath6kl_warn("Unable to read memory:%u\n", addr);
	vfree(buf);
	return -EIO;
}

static ssize_t ath6kl_regdump_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	u8 *buf = file->private_data;
	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static int ath6kl_regdump_release(struct inode *inode, struct file *file)
{
	vfree(file->private_data);
	return 0;
}

static const struct file_operations fops_reg_dump = {
	.open = ath6kl_regdump_open,
	.read = ath6kl_regdump_read,
	.release = ath6kl_regdump_release,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_lrssi_roam_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	unsigned long lrssi_roam_threshold;

	if (kstrtoul_from_user(user_buf, count, 0, &lrssi_roam_threshold))
		return -EINVAL;

	ar->lrssi_roam_threshold = lrssi_roam_threshold;

	ath6kl_wmi_set_roam_lrssi_cmd(ar->wmi, ar->lrssi_roam_threshold);

	return count;
}

static ssize_t ath6kl_lrssi_roam_read(struct file *file,
				      char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	char buf[32];
	unsigned int len;

	len = snprintf(buf, sizeof(buf), "%u\n", ar->lrssi_roam_threshold);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_lrssi_roam_threshold = {
	.read = ath6kl_lrssi_roam_read,
	.write = ath6kl_lrssi_roam_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_regwrite_read(struct file *file,
				    char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	u8 buf[32];
	unsigned int len = 0;

	len = scnprintf(buf, sizeof(buf), "Addr: 0x%x Val: 0x%x\n",
			ar->debug.diag_reg_addr_wr, ar->debug.diag_reg_val_wr);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath6kl_regwrite_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	char buf[32];
	char *sptr, *token;
	unsigned int len = 0;
	u32 reg_addr, reg_val;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, "=");
	if (!token)
		return -EINVAL;

	if (kstrtou32(token, 0, &reg_addr))
		return -EINVAL;

	if (!ath6kl_dbg_is_diag_reg_valid(reg_addr))
		return -EINVAL;

	if (kstrtou32(sptr, 0, &reg_val))
		return -EINVAL;

	ar->debug.diag_reg_addr_wr = reg_addr;
	ar->debug.diag_reg_val_wr = reg_val;

	if (ath6kl_diag_write32(ar, ar->debug.diag_reg_addr_wr,
				cpu_to_le32(ar->debug.diag_reg_val_wr)))
		return -EIO;

	return count;
}

static const struct file_operations fops_diag_reg_write = {
	.read = ath6kl_regwrite_read,
	.write = ath6kl_regwrite_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath6kl_debug_roam_tbl_event(struct ath6kl *ar, const void *buf,
				size_t len)
{
	const struct wmi_target_roam_tbl *tbl;
	u16 num_entries;

	if (len < sizeof(*tbl))
		return -EINVAL;

	tbl = (const struct wmi_target_roam_tbl *) buf;
	num_entries = le16_to_cpu(tbl->num_entries);
	if (sizeof(*tbl) + num_entries * sizeof(struct wmi_bss_roam_info) >
	    len)
		return -EINVAL;

	if (ar->debug.roam_tbl == NULL ||
	    ar->debug.roam_tbl_len < (unsigned int) len) {
		kfree(ar->debug.roam_tbl);
		ar->debug.roam_tbl = kmalloc(len, GFP_ATOMIC);
		if (ar->debug.roam_tbl == NULL)
			return -ENOMEM;
	}

	memcpy(ar->debug.roam_tbl, buf, len);
	ar->debug.roam_tbl_len = len;

	if (test_bit(ROAM_TBL_PEND, &ar->flag)) {
		clear_bit(ROAM_TBL_PEND, &ar->flag);
		wake_up(&ar->event_wq);
	}

	return 0;
}

static ssize_t ath6kl_roam_table_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	int ret;
	long left;
	struct wmi_target_roam_tbl *tbl;
	u16 num_entries, i;
	char *buf;
	unsigned int len, buf_len;
	ssize_t ret_cnt;

	if (down_interruptible(&ar->sem))
		return -EBUSY;

	set_bit(ROAM_TBL_PEND, &ar->flag);

	ret = ath6kl_wmi_get_roam_tbl_cmd(ar->wmi);
	if (ret) {
		up(&ar->sem);
		return ret;
	}

	left = wait_event_interruptible_timeout(
		ar->event_wq, !test_bit(ROAM_TBL_PEND, &ar->flag), WMI_TIMEOUT);
	up(&ar->sem);

	if (left <= 0)
		return -ETIMEDOUT;

	if (ar->debug.roam_tbl == NULL)
		return -ENOMEM;

	tbl = (struct wmi_target_roam_tbl *) ar->debug.roam_tbl;
	num_entries = le16_to_cpu(tbl->num_entries);

	buf_len = 100 + num_entries * 100;
	buf = kzalloc(buf_len, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	len = 0;
	len += scnprintf(buf + len, buf_len - len,
			 "roam_mode=%u\n\n"
			 "# roam_util bssid rssi rssidt last_rssi util bias\n",
			 le16_to_cpu(tbl->roam_mode));

	for (i = 0; i < num_entries; i++) {
		struct wmi_bss_roam_info *info = &tbl->info[i];
		len += scnprintf(buf + len, buf_len - len,
				 "%d %pM %d %d %d %d %d\n",
				 a_sle32_to_cpu(info->roam_util), info->bssid,
				 info->rssi, info->rssidt, info->last_rssi,
				 info->util, info->bias);
	}

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret_cnt;
}

static const struct file_operations fops_roam_table = {
	.read = ath6kl_roam_table_read,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_force_roam_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	int ret;
	char buf[20];
	size_t len;
	u8 bssid[ETH_ALEN];

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	if (!mac_pton(buf, bssid))
		return -EINVAL;

	ret = ath6kl_wmi_force_roam_cmd(ar->wmi, bssid);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations fops_force_roam = {
	.write = ath6kl_force_roam_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_roam_mode_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	int ret;
	char buf[20];
	size_t len;
	enum wmi_roam_mode mode;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	if (strcasecmp(buf, "default") == 0)
		mode = WMI_DEFAULT_ROAM_MODE;
	else if (strcasecmp(buf, "bssbias") == 0)
		mode = WMI_HOST_BIAS_ROAM_MODE;
	else if (strcasecmp(buf, "lock") == 0)
		mode = WMI_LOCK_BSS_MODE;
	else
		return -EINVAL;

	ret = ath6kl_wmi_set_roam_mode_cmd(ar->wmi, mode);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations fops_roam_mode = {
	.write = ath6kl_roam_mode_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath6kl_debug_set_keepalive(struct ath6kl *ar, u8 keepalive)
{
	ar->debug.keepalive = keepalive;
}

static ssize_t ath6kl_keepalive_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	char buf[16];
	int len;

	len = snprintf(buf, sizeof(buf), "%u\n", ar->debug.keepalive);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath6kl_keepalive_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	int ret;
	u8 val;

	ret = kstrtou8_from_user(user_buf, count, 0, &val);
	if (ret)
		return ret;

	ret = ath6kl_wmi_set_keepalive_cmd(ar->wmi, 0, val);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations fops_keepalive = {
	.open = simple_open,
	.read = ath6kl_keepalive_read,
	.write = ath6kl_keepalive_write,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath6kl_debug_set_disconnect_timeout(struct ath6kl *ar, u8 timeout)
{
	ar->debug.disc_timeout = timeout;
}

static ssize_t ath6kl_disconnect_timeout_read(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	char buf[16];
	int len;

	len = snprintf(buf, sizeof(buf), "%u\n", ar->debug.disc_timeout);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath6kl_disconnect_timeout_write(struct file *file,
					       const char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	int ret;
	u8 val;

	ret = kstrtou8_from_user(user_buf, count, 0, &val);
	if (ret)
		return ret;

	ret = ath6kl_wmi_disctimeout_cmd(ar->wmi, 0, val);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations fops_disconnect_timeout = {
	.open = simple_open,
	.read = ath6kl_disconnect_timeout_read,
	.write = ath6kl_disconnect_timeout_write,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_create_qos_write(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct ath6kl_vif *vif;
	char buf[200];
	ssize_t len;
	char *sptr, *token;
	struct wmi_create_pstream_cmd pstream;
	u32 val32;
	u16 val16;

	vif = ath6kl_vif_first(ar);
	if (!vif)
		return -EIO;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &pstream.user_pri))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &pstream.traffic_direc))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &pstream.traffic_class))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &pstream.traffic_type))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &pstream.voice_psc_cap))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.min_service_int = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.max_service_int = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.inactivity_int = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.suspension_int = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.service_start_time = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &pstream.tsid))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou16(token, 0, &val16))
		return -EINVAL;
	pstream.nominal_msdu = cpu_to_le16(val16);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou16(token, 0, &val16))
		return -EINVAL;
	pstream.max_msdu = cpu_to_le16(val16);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.min_data_rate = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.mean_data_rate = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.peak_data_rate = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.max_burst_size = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.delay_bound = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.min_phy_rate = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.sba = cpu_to_le32(val32);

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &val32))
		return -EINVAL;
	pstream.medium_time = cpu_to_le32(val32);

	pstream.nominal_phy = le32_to_cpu(pstream.min_phy_rate) / 1000000;

	ath6kl_wmi_create_pstream_cmd(ar->wmi, vif->fw_vif_idx, &pstream);

	return count;
}

static const struct file_operations fops_create_qos = {
	.write = ath6kl_create_qos_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_delete_qos_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct ath6kl_vif *vif;
	char buf[100];
	ssize_t len;
	char *sptr, *token;
	u8 traffic_class;
	u8 tsid;

	vif = ath6kl_vif_first(ar);
	if (!vif)
		return -EIO;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &traffic_class))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &tsid))
		return -EINVAL;

	ath6kl_wmi_delete_pstream_cmd(ar->wmi, vif->fw_vif_idx,
				      traffic_class, tsid);

	return count;
}

static const struct file_operations fops_delete_qos = {
	.write = ath6kl_delete_qos_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_bgscan_int_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct ath6kl_vif *vif;
	u16 bgscan_int;
	char buf[32];
	ssize_t len;

	vif = ath6kl_vif_first(ar);
	if (!vif)
		return -EIO;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtou16(buf, 0, &bgscan_int))
		return -EINVAL;

	if (bgscan_int == 0)
		bgscan_int = 0xffff;

	vif->bg_scan_period = bgscan_int;

	ath6kl_wmi_scanparams_cmd(ar->wmi, 0, 0, 0, bgscan_int, 0, 0, 0, 3,
				  0, 0, 0);

	return count;
}

static const struct file_operations fops_bgscan_int = {
	.write = ath6kl_bgscan_int_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_listen_int_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct ath6kl_vif *vif;
	u16 listen_interval;
	char buf[32];
	ssize_t len;

	vif = ath6kl_vif_first(ar);
	if (!vif)
		return -EIO;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtou16(buf, 0, &listen_interval))
		return -EINVAL;

	if ((listen_interval < 15) || (listen_interval > 3000))
		return -EINVAL;

	vif->listen_intvl_t = listen_interval;
	ath6kl_wmi_listeninterval_cmd(ar->wmi, vif->fw_vif_idx,
				      vif->listen_intvl_t, 0);

	return count;
}

static ssize_t ath6kl_listen_int_read(struct file *file,
				      char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	struct ath6kl_vif *vif;
	char buf[32];
	int len;

	vif = ath6kl_vif_first(ar);
	if (!vif)
		return -EIO;

	len = scnprintf(buf, sizeof(buf), "%u\n", vif->listen_intvl_t);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_listen_int = {
	.read = ath6kl_listen_int_read,
	.write = ath6kl_listen_int_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath6kl_power_params_write(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath6kl *ar = file->private_data;
	u8 buf[100];
	unsigned int len = 0;
	char *sptr, *token;
	u16 idle_period, ps_poll_num, dtim,
		tx_wakeup, num_tx;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	sptr = buf;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou16(token, 0, &idle_period))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou16(token, 0, &ps_poll_num))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou16(token, 0, &dtim))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou16(token, 0, &tx_wakeup))
		return -EINVAL;

	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou16(token, 0, &num_tx))
		return -EINVAL;

	ath6kl_wmi_pmparams_cmd(ar->wmi, 0, idle_period, ps_poll_num,
				dtim, tx_wakeup, num_tx, 0);

	return count;
}

static const struct file_operations fops_power_params = {
	.write = ath6kl_power_params_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath6kl_debug_init(struct ath6kl *ar)
{
	skb_queue_head_init(&ar->debug.fwlog_queue);
	init_completion(&ar->debug.fwlog_completion);

	/*
	 * Actually we are lying here but don't know how to read the mask
	 * value from the firmware.
	 */
	ar->debug.fwlog_mask = 0;
}

/*
 * Initialisation needs to happen in two stages as fwlog events can come
 * before cfg80211 is initialised, and debugfs depends on cfg80211
 * initialisation.
 */
int ath6kl_debug_init_fs(struct ath6kl *ar)
{
	ar->debugfs_phy = debugfs_create_dir("ath6kl",
					     ar->wiphy->debugfsdir);
	if (!ar->debugfs_phy)
		return -ENOMEM;

	debugfs_create_file("tgt_stats", S_IRUSR, ar->debugfs_phy, ar,
			    &fops_tgt_stats);

	if (ar->hif_type == ATH6KL_HIF_TYPE_SDIO)
		debugfs_create_file("credit_dist_stats", S_IRUSR,
				    ar->debugfs_phy, ar,
				    &fops_credit_dist_stats);

	debugfs_create_file("endpoint_stats", S_IRUSR | S_IWUSR,
			    ar->debugfs_phy, ar, &fops_endpoint_stats);

	debugfs_create_file("fwlog", S_IRUSR, ar->debugfs_phy, ar,
			    &fops_fwlog);

	debugfs_create_file("fwlog_block", S_IRUSR, ar->debugfs_phy, ar,
			    &fops_fwlog_block);

	debugfs_create_file("fwlog_mask", S_IRUSR | S_IWUSR, ar->debugfs_phy,
			    ar, &fops_fwlog_mask);

	debugfs_create_file("reg_addr", S_IRUSR | S_IWUSR, ar->debugfs_phy, ar,
			    &fops_diag_reg_read);

	debugfs_create_file("reg_dump", S_IRUSR, ar->debugfs_phy, ar,
			    &fops_reg_dump);

	debugfs_create_file("lrssi_roam_threshold", S_IRUSR | S_IWUSR,
			    ar->debugfs_phy, ar, &fops_lrssi_roam_threshold);

	debugfs_create_file("reg_write", S_IRUSR | S_IWUSR,
			    ar->debugfs_phy, ar, &fops_diag_reg_write);

	debugfs_create_file("war_stats", S_IRUSR, ar->debugfs_phy, ar,
			    &fops_war_stats);

	debugfs_create_file("roam_table", S_IRUSR, ar->debugfs_phy, ar,
			    &fops_roam_table);

	debugfs_create_file("force_roam", S_IWUSR, ar->debugfs_phy, ar,
			    &fops_force_roam);

	debugfs_create_file("roam_mode", S_IWUSR, ar->debugfs_phy, ar,
			    &fops_roam_mode);

	debugfs_create_file("keepalive", S_IRUSR | S_IWUSR, ar->debugfs_phy, ar,
			    &fops_keepalive);

	debugfs_create_file("disconnect_timeout", S_IRUSR | S_IWUSR,
			    ar->debugfs_phy, ar, &fops_disconnect_timeout);

	debugfs_create_file("create_qos", S_IWUSR, ar->debugfs_phy, ar,
			    &fops_create_qos);

	debugfs_create_file("delete_qos", S_IWUSR, ar->debugfs_phy, ar,
			    &fops_delete_qos);

	debugfs_create_file("bgscan_interval", S_IWUSR,
			    ar->debugfs_phy, ar, &fops_bgscan_int);

	debugfs_create_file("listen_interval", S_IRUSR | S_IWUSR,
			    ar->debugfs_phy, ar, &fops_listen_int);

	debugfs_create_file("power_params", S_IWUSR, ar->debugfs_phy, ar,
			    &fops_power_params);

	return 0;
}

void ath6kl_debug_cleanup(struct ath6kl *ar)
{
	skb_queue_purge(&ar->debug.fwlog_queue);
	complete(&ar->debug.fwlog_completion);
	kfree(ar->debug.roam_tbl);
}

#endif
