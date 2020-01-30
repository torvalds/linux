// SPDX-License-Identifier: GPL-2.0
/* /proc routines for Host AP driver */

#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/export.h>
#include <net/lib80211.h>

#include "hostap_wlan.h"
#include "hostap.h"

#define PROC_LIMIT (PAGE_SIZE - 80)

#if !defined(PRISM2_NO_PROCFS_DEBUG) && defined(CONFIG_PROC_FS)
static int prism2_debug_proc_show(struct seq_file *m, void *v)
{
	local_info_t *local = m->private;
	int i;

	seq_printf(m, "next_txfid=%d next_alloc=%d\n",
		   local->next_txfid, local->next_alloc);
	for (i = 0; i < PRISM2_TXFID_COUNT; i++)
		seq_printf(m, "FID: tx=%04X intransmit=%04X\n",
			   local->txfid[i], local->intransmitfid[i]);
	seq_printf(m, "FW TX rate control: %d\n", local->fw_tx_rate_control);
	seq_printf(m, "beacon_int=%d\n", local->beacon_int);
	seq_printf(m, "dtim_period=%d\n", local->dtim_period);
	seq_printf(m, "wds_max_connections=%d\n", local->wds_max_connections);
	seq_printf(m, "dev_enabled=%d\n", local->dev_enabled);
	seq_printf(m, "sw_tick_stuck=%d\n", local->sw_tick_stuck);
	for (i = 0; i < WEP_KEYS; i++) {
		if (local->crypt_info.crypt[i] &&
		    local->crypt_info.crypt[i]->ops) {
			seq_printf(m, "crypt[%d]=%s\n", i,
				   local->crypt_info.crypt[i]->ops->name);
		}
	}
	seq_printf(m, "pri_only=%d\n", local->pri_only);
	seq_printf(m, "pci=%d\n", local->func->hw_type == HOSTAP_HW_PCI);
	seq_printf(m, "sram_type=%d\n", local->sram_type);
	seq_printf(m, "no_pri=%d\n", local->no_pri);

	return 0;
}
#endif

#ifdef CONFIG_PROC_FS
static int prism2_stats_proc_show(struct seq_file *m, void *v)
{
	local_info_t *local = m->private;
	struct comm_tallies_sums *sums = &local->comm_tallies;

	seq_printf(m, "TxUnicastFrames=%u\n", sums->tx_unicast_frames);
	seq_printf(m, "TxMulticastframes=%u\n", sums->tx_multicast_frames);
	seq_printf(m, "TxFragments=%u\n", sums->tx_fragments);
	seq_printf(m, "TxUnicastOctets=%u\n", sums->tx_unicast_octets);
	seq_printf(m, "TxMulticastOctets=%u\n", sums->tx_multicast_octets);
	seq_printf(m, "TxDeferredTransmissions=%u\n",
		   sums->tx_deferred_transmissions);
	seq_printf(m, "TxSingleRetryFrames=%u\n", sums->tx_single_retry_frames);
	seq_printf(m, "TxMultipleRetryFrames=%u\n",
		   sums->tx_multiple_retry_frames);
	seq_printf(m, "TxRetryLimitExceeded=%u\n",
		   sums->tx_retry_limit_exceeded);
	seq_printf(m, "TxDiscards=%u\n", sums->tx_discards);
	seq_printf(m, "RxUnicastFrames=%u\n", sums->rx_unicast_frames);
	seq_printf(m, "RxMulticastFrames=%u\n", sums->rx_multicast_frames);
	seq_printf(m, "RxFragments=%u\n", sums->rx_fragments);
	seq_printf(m, "RxUnicastOctets=%u\n", sums->rx_unicast_octets);
	seq_printf(m, "RxMulticastOctets=%u\n", sums->rx_multicast_octets);
	seq_printf(m, "RxFCSErrors=%u\n", sums->rx_fcs_errors);
	seq_printf(m, "RxDiscardsNoBuffer=%u\n", sums->rx_discards_no_buffer);
	seq_printf(m, "TxDiscardsWrongSA=%u\n", sums->tx_discards_wrong_sa);
	seq_printf(m, "RxDiscardsWEPUndecryptable=%u\n",
		   sums->rx_discards_wep_undecryptable);
	seq_printf(m, "RxMessageInMsgFragments=%u\n",
		   sums->rx_message_in_msg_fragments);
	seq_printf(m, "RxMessageInBadMsgFragments=%u\n",
		   sums->rx_message_in_bad_msg_fragments);
	/* FIX: this may grow too long for one page(?) */

	return 0;
}
#endif

static int prism2_wds_proc_show(struct seq_file *m, void *v)
{
	struct list_head *ptr = v;
	struct hostap_interface *iface;

	iface = list_entry(ptr, struct hostap_interface, list);
	if (iface->type == HOSTAP_INTERFACE_WDS)
		seq_printf(m, "%s\t%pM\n",
			   iface->dev->name, iface->u.wds.remote_addr);
	return 0;
}

static void *prism2_wds_proc_start(struct seq_file *m, loff_t *_pos)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	read_lock_bh(&local->iface_lock);
	return seq_list_start(&local->hostap_interfaces, *_pos);
}

static void *prism2_wds_proc_next(struct seq_file *m, void *v, loff_t *_pos)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	return seq_list_next(v, &local->hostap_interfaces, _pos);
}

static void prism2_wds_proc_stop(struct seq_file *m, void *v)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	read_unlock_bh(&local->iface_lock);
}

static const struct seq_operations prism2_wds_proc_seqops = {
	.start	= prism2_wds_proc_start,
	.next	= prism2_wds_proc_next,
	.stop	= prism2_wds_proc_stop,
	.show	= prism2_wds_proc_show,
};

static int prism2_bss_list_proc_show(struct seq_file *m, void *v)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	struct list_head *ptr = v;
	struct hostap_bss_info *bss;

	if (ptr == &local->bss_list) {
		seq_printf(m, "#BSSID\tlast_update\tcount\tcapab_info\tSSID(txt)\t"
			   "SSID(hex)\tWPA IE\n");
		return 0;
	}

	bss = list_entry(ptr, struct hostap_bss_info, list);
	seq_printf(m, "%pM\t%lu\t%u\t0x%x\t",
		   bss->bssid, bss->last_update,
		   bss->count, bss->capab_info);

	seq_printf(m, "%*pE", (int)bss->ssid_len, bss->ssid);

	seq_putc(m, '\t');
	seq_printf(m, "%*phN", (int)bss->ssid_len, bss->ssid);
	seq_putc(m, '\t');
	seq_printf(m, "%*phN", (int)bss->wpa_ie_len, bss->wpa_ie);
	seq_putc(m, '\n');
	return 0;
}

static void *prism2_bss_list_proc_start(struct seq_file *m, loff_t *_pos)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	spin_lock_bh(&local->lock);
	return seq_list_start_head(&local->bss_list, *_pos);
}

static void *prism2_bss_list_proc_next(struct seq_file *m, void *v, loff_t *_pos)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	return seq_list_next(v, &local->bss_list, _pos);
}

static void prism2_bss_list_proc_stop(struct seq_file *m, void *v)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	spin_unlock_bh(&local->lock);
}

static const struct seq_operations prism2_bss_list_proc_seqops = {
	.start	= prism2_bss_list_proc_start,
	.next	= prism2_bss_list_proc_next,
	.stop	= prism2_bss_list_proc_stop,
	.show	= prism2_bss_list_proc_show,
};

#ifdef CONFIG_PROC_FS
static int prism2_crypt_proc_show(struct seq_file *m, void *v)
{
	local_info_t *local = m->private;
	int i;

	seq_printf(m, "tx_keyidx=%d\n", local->crypt_info.tx_keyidx);
	for (i = 0; i < WEP_KEYS; i++) {
		if (local->crypt_info.crypt[i] &&
		    local->crypt_info.crypt[i]->ops &&
		    local->crypt_info.crypt[i]->ops->print_stats) {
			local->crypt_info.crypt[i]->ops->print_stats(
				m, local->crypt_info.crypt[i]->priv);
		}
	}
	return 0;
}
#endif

static ssize_t prism2_pda_proc_read(struct file *file, char __user *buf,
				    size_t count, loff_t *_pos)
{
	local_info_t *local = PDE_DATA(file_inode(file));
	size_t off;

	if (local->pda == NULL || *_pos >= PRISM2_PDA_SIZE)
		return 0;

	off = *_pos;
	if (count > PRISM2_PDA_SIZE - off)
		count = PRISM2_PDA_SIZE - off;
	if (copy_to_user(buf, local->pda + off, count) != 0)
		return -EFAULT;
	*_pos += count;
	return count;
}

static const struct file_operations prism2_pda_proc_fops = {
	.read		= prism2_pda_proc_read,
	.llseek		= generic_file_llseek,
};


static ssize_t prism2_aux_dump_proc_no_read(struct file *file, char __user *buf,
					    size_t bufsize, loff_t *_pos)
{
	return 0;
}

static const struct file_operations prism2_aux_dump_proc_fops = {
	.read		= prism2_aux_dump_proc_no_read,
};


#ifdef PRISM2_IO_DEBUG
static int prism2_io_debug_proc_read(char *page, char **start, off_t off,
				     int count, int *eof, void *data)
{
	local_info_t *local = (local_info_t *) data;
	int head = local->io_debug_head;
	int start_bytes, left, copy;

	if (off + count > PRISM2_IO_DEBUG_SIZE * 4) {
		*eof = 1;
		if (off >= PRISM2_IO_DEBUG_SIZE * 4)
			return 0;
		count = PRISM2_IO_DEBUG_SIZE * 4 - off;
	}

	start_bytes = (PRISM2_IO_DEBUG_SIZE - head) * 4;
	left = count;

	if (off < start_bytes) {
		copy = start_bytes - off;
		if (copy > count)
			copy = count;
		memcpy(page, ((u8 *) &local->io_debug[head]) + off, copy);
		left -= copy;
		if (left > 0)
			memcpy(&page[copy], local->io_debug, left);
	} else {
		memcpy(page, ((u8 *) local->io_debug) + (off - start_bytes),
		       left);
	}

	*start = page;

	return count;
}
#endif /* PRISM2_IO_DEBUG */


#ifndef PRISM2_NO_STATION_MODES
static int prism2_scan_results_proc_show(struct seq_file *m, void *v)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	unsigned long entry;
	int i, len;
	struct hfa384x_hostscan_result *scanres;
	u8 *p;

	if (v == SEQ_START_TOKEN) {
		seq_printf(m,
			   "CHID ANL SL BcnInt Capab Rate BSSID ATIM SupRates SSID\n");
		return 0;
	}

	entry = (unsigned long)v - 2;
	scanres = &local->last_scan_results[entry];

	seq_printf(m, "%d %d %d %d 0x%02x %d %pM %d ",
		   le16_to_cpu(scanres->chid),
		   (s16) le16_to_cpu(scanres->anl),
		   (s16) le16_to_cpu(scanres->sl),
		   le16_to_cpu(scanres->beacon_interval),
		   le16_to_cpu(scanres->capability),
		   le16_to_cpu(scanres->rate),
		   scanres->bssid,
		   le16_to_cpu(scanres->atim));

	p = scanres->sup_rates;
	for (i = 0; i < sizeof(scanres->sup_rates); i++) {
		if (p[i] == 0)
			break;
		seq_printf(m, "<%02x>", p[i]);
	}
	seq_putc(m, ' ');

	p = scanres->ssid;
	len = le16_to_cpu(scanres->ssid_len);
	if (len > 32)
		len = 32;
	for (i = 0; i < len; i++) {
		unsigned char c = p[i];
		if (c >= 32 && c < 127)
			seq_putc(m, c);
		else
			seq_printf(m, "<%02x>", c);
	}
	seq_putc(m, '\n');
	return 0;
}

static void *prism2_scan_results_proc_start(struct seq_file *m, loff_t *_pos)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	spin_lock_bh(&local->lock);

	/* We have a header (pos 0) + N results to show (pos 1...N) */
	if (*_pos > local->last_scan_results_count)
		return NULL;
	return (void *)(unsigned long)(*_pos + 1); /* 0 would be EOF */
}

static void *prism2_scan_results_proc_next(struct seq_file *m, void *v, loff_t *_pos)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));

	++*_pos;
	if (*_pos > local->last_scan_results_count)
		return NULL;
	return (void *)(unsigned long)(*_pos + 1); /* 0 would be EOF */
}

static void prism2_scan_results_proc_stop(struct seq_file *m, void *v)
{
	local_info_t *local = PDE_DATA(file_inode(m->file));
	spin_unlock_bh(&local->lock);
}

static const struct seq_operations prism2_scan_results_proc_seqops = {
	.start	= prism2_scan_results_proc_start,
	.next	= prism2_scan_results_proc_next,
	.stop	= prism2_scan_results_proc_stop,
	.show	= prism2_scan_results_proc_show,
};
#endif /* PRISM2_NO_STATION_MODES */


void hostap_init_proc(local_info_t *local)
{
	local->proc = NULL;

	if (hostap_proc == NULL) {
		printk(KERN_WARNING "%s: hostap proc directory not created\n",
		       local->dev->name);
		return;
	}

	local->proc = proc_mkdir(local->ddev->name, hostap_proc);
	if (local->proc == NULL) {
		printk(KERN_INFO "/proc/net/hostap/%s creation failed\n",
		       local->ddev->name);
		return;
	}

#ifndef PRISM2_NO_PROCFS_DEBUG
	proc_create_single_data("debug", 0, local->proc,
			prism2_debug_proc_show, local);
#endif /* PRISM2_NO_PROCFS_DEBUG */
	proc_create_single_data("stats", 0, local->proc, prism2_stats_proc_show,
			local);
	proc_create_seq_data("wds", 0, local->proc,
			&prism2_wds_proc_seqops, local);
	proc_create_data("pda", 0, local->proc,
			 &prism2_pda_proc_fops, local);
	proc_create_data("aux_dump", 0, local->proc,
			 local->func->read_aux_fops ?: &prism2_aux_dump_proc_fops,
			 local);
	proc_create_seq_data("bss_list", 0, local->proc,
			&prism2_bss_list_proc_seqops, local);
	proc_create_single_data("crypt", 0, local->proc, prism2_crypt_proc_show,
		local);
#ifdef PRISM2_IO_DEBUG
	proc_create_single_data("io_debug", 0, local->proc,
			prism2_debug_proc_show, local);
#endif /* PRISM2_IO_DEBUG */
#ifndef PRISM2_NO_STATION_MODES
	proc_create_seq_data("scan_results", 0, local->proc,
			&prism2_scan_results_proc_seqops, local);
#endif /* PRISM2_NO_STATION_MODES */
}


void hostap_remove_proc(local_info_t *local)
{
	proc_remove(local->proc);
}


EXPORT_SYMBOL(hostap_init_proc);
EXPORT_SYMBOL(hostap_remove_proc);
