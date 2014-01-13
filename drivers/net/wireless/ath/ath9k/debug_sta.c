/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
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

#include "ath9k.h"

static ssize_t read_file_node_aggr(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath_node *an = file->private_data;
	struct ath_softc *sc = an->sc;
	struct ath_atx_tid *tid;
	struct ath_atx_ac *ac;
	struct ath_txq *txq;
	u32 len = 0, size = 4096;
	char *buf;
	size_t retval;
	int tidno, acno;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (!an->sta->ht_cap.ht_supported) {
		len = scnprintf(buf, size, "%s\n",
				"HT not supported");
		goto exit;
	}

	len = scnprintf(buf, size, "Max-AMPDU: %d\n",
			an->maxampdu);
	len += scnprintf(buf + len, size - len, "MPDU Density: %d\n\n",
			 an->mpdudensity);

	len += scnprintf(buf + len, size - len,
			 "%2s%7s\n", "AC", "SCHED");

	for (acno = 0, ac = &an->ac[acno];
	     acno < IEEE80211_NUM_ACS; acno++, ac++) {
		txq = ac->txq;
		ath_txq_lock(sc, txq);
		len += scnprintf(buf + len, size - len,
				 "%2d%7d\n",
				 acno, ac->sched);
		ath_txq_unlock(sc, txq);
	}

	len += scnprintf(buf + len, size - len,
			 "\n%3s%11s%10s%10s%10s%10s%9s%6s%8s\n",
			 "TID", "SEQ_START", "SEQ_NEXT", "BAW_SIZE",
			 "BAW_HEAD", "BAW_TAIL", "BAR_IDX", "SCHED", "PAUSED");

	for (tidno = 0, tid = &an->tid[tidno];
	     tidno < IEEE80211_NUM_TIDS; tidno++, tid++) {
		txq = tid->ac->txq;
		ath_txq_lock(sc, txq);
		len += scnprintf(buf + len, size - len,
				 "%3d%11d%10d%10d%10d%10d%9d%6d%8d\n",
				 tid->tidno, tid->seq_start, tid->seq_next,
				 tid->baw_size, tid->baw_head, tid->baw_tail,
				 tid->bar_index, tid->sched, tid->paused);
		ath_txq_unlock(sc, txq);
	}
exit:
	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_node_aggr = {
	.read = read_file_node_aggr,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_sta_add_debugfs(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct dentry *dir)
{
	struct ath_node *an = (struct ath_node *)sta->drv_priv;
	debugfs_create_file("node_aggr", S_IRUGO, dir, an, &fops_node_aggr);
}
