/**
 * Copyright (c) 2014 Redpine Signals Inc.
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

#include "rsi_debugfs.h"
#include "rsi_sdio.h"

/**
 * rsi_sdio_stats_read() - This function returns the sdio status of the driver.
 * @seq: Pointer to the sequence file structure.
 * @data: Pointer to the data.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_sdio_stats_read(struct seq_file *seq, void *data)
{
	struct rsi_common *common = seq->private;
	struct rsi_hw *adapter = common->priv;
	struct rsi_91x_sdiodev *dev =
		(struct rsi_91x_sdiodev *)adapter->rsi_dev;

	seq_printf(seq, "total_sdio_interrupts: %d\n",
		   dev->rx_info.sdio_int_counter);
	seq_printf(seq, "sdio_msdu_pending_intr_count: %d\n",
		   dev->rx_info.total_sdio_msdu_pending_intr);
	seq_printf(seq, "sdio_buff_full_count : %d\n",
		   dev->rx_info.buf_full_counter);
	seq_printf(seq, "sdio_buf_semi_full_count %d\n",
		   dev->rx_info.buf_semi_full_counter);
	seq_printf(seq, "sdio_unknown_intr_count: %d\n",
		   dev->rx_info.total_sdio_unknown_intr);
	/* RX Path Stats */
	seq_printf(seq, "BUFFER FULL STATUS  : %d\n",
		   dev->rx_info.buffer_full);
	seq_printf(seq, "SEMI BUFFER FULL STATUS  : %d\n",
		   dev->rx_info.semi_buffer_full);
	seq_printf(seq, "MGMT BUFFER FULL STATUS  : %d\n",
		   dev->rx_info.mgmt_buffer_full);
	seq_printf(seq, "BUFFER FULL COUNTER  : %d\n",
		   dev->rx_info.buf_full_counter);
	seq_printf(seq, "BUFFER SEMI FULL COUNTER  : %d\n",
		   dev->rx_info.buf_semi_full_counter);
	seq_printf(seq, "MGMT BUFFER FULL COUNTER  : %d\n",
		   dev->rx_info.mgmt_buf_full_counter);

	return 0;
}

/**
 * rsi_sdio_stats_open() - This function calls single open function of seq_file
 *			   to open file and read contents from it.
 * @inode: Pointer to the inode structure.
 * @file: Pointer to the file structure.
 *
 * Return: Pointer to the opened file status: 0 on success, ENOMEM on failure.
 */
static int rsi_sdio_stats_open(struct inode *inode,
			       struct file *file)
{
	return single_open(file, rsi_sdio_stats_read, inode->i_private);
}

/**
 * rsi_version_read() - This function gives driver and firmware version number.
 * @seq: Pointer to the sequence file structure.
 * @data: Pointer to the data.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_version_read(struct seq_file *seq, void *data)
{
	struct rsi_common *common = seq->private;

	seq_printf(seq, "LMAC   : %d.%d.%d.%d\n",
		   common->lmac_ver.major,
		   common->lmac_ver.minor,
		   common->lmac_ver.release_num,
		   common->lmac_ver.patch_num);

	return 0;
}

/**
 * rsi_version_open() - This function calls single open function of seq_file to
 *			open file and read contents from it.
 * @inode: Pointer to the inode structure.
 * @file: Pointer to the file structure.
 *
 * Return: Pointer to the opened file status: 0 on success, ENOMEM on failure.
 */
static int rsi_version_open(struct inode *inode,
				 struct file *file)
{
	return single_open(file, rsi_version_read, inode->i_private);
}

/**
 * rsi_stats_read() - This function return the status of the driver.
 * @seq: Pointer to the sequence file structure.
 * @data: Pointer to the data.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_stats_read(struct seq_file *seq, void *data)
{
	struct rsi_common *common = seq->private;

	unsigned char fsm_state[][32] = {
		"FSM_FW_NOT_LOADED",
		"FSM_CARD_NOT_READY",
		"FSM_COMMON_DEV_PARAMS_SENT",
		"FSM_BOOT_PARAMS_SENT",
		"FSM_EEPROM_READ_MAC_ADDR",
		"FSM_EEPROM_READ_RF_TYPE",
		"FSM_RESET_MAC_SENT",
		"FSM_RADIO_CAPS_SENT",
		"FSM_BB_RF_PROG_SENT",
		"FSM_MAC_INIT_DONE"
	};
	seq_puts(seq, "==> RSI STA DRIVER STATUS <==\n");
	seq_puts(seq, "DRIVER_FSM_STATE: ");

	BUILD_BUG_ON(ARRAY_SIZE(fsm_state) != NUM_FSM_STATES);

	if (common->fsm_state <= FSM_MAC_INIT_DONE)
		seq_printf(seq, "%s", fsm_state[common->fsm_state]);

	seq_printf(seq, "(%d)\n\n", common->fsm_state);

	/* Mgmt TX Path Stats */
	seq_printf(seq, "total_mgmt_pkt_send : %d\n",
		   common->tx_stats.total_tx_pkt_send[MGMT_SOFT_Q]);
	seq_printf(seq, "total_mgmt_pkt_queued : %d\n",
		   skb_queue_len(&common->tx_queue[MGMT_SOFT_Q]));
	seq_printf(seq, "total_mgmt_pkt_freed  : %d\n",
		   common->tx_stats.total_tx_pkt_freed[MGMT_SOFT_Q]);

	/* Data TX Path Stats */
	seq_printf(seq, "total_data_vo_pkt_send: %8d\t",
		   common->tx_stats.total_tx_pkt_send[VO_Q]);
	seq_printf(seq, "total_data_vo_pkt_queued:  %8d\t",
		   skb_queue_len(&common->tx_queue[VO_Q]));
	seq_printf(seq, "total_vo_pkt_freed: %8d\n",
		   common->tx_stats.total_tx_pkt_freed[VO_Q]);
	seq_printf(seq, "total_data_vi_pkt_send: %8d\t",
		   common->tx_stats.total_tx_pkt_send[VI_Q]);
	seq_printf(seq, "total_data_vi_pkt_queued:  %8d\t",
		   skb_queue_len(&common->tx_queue[VI_Q]));
	seq_printf(seq, "total_vi_pkt_freed: %8d\n",
		   common->tx_stats.total_tx_pkt_freed[VI_Q]);
	seq_printf(seq,  "total_data_be_pkt_send: %8d\t",
		   common->tx_stats.total_tx_pkt_send[BE_Q]);
	seq_printf(seq, "total_data_be_pkt_queued:  %8d\t",
		   skb_queue_len(&common->tx_queue[BE_Q]));
	seq_printf(seq, "total_be_pkt_freed: %8d\n",
		   common->tx_stats.total_tx_pkt_freed[BE_Q]);
	seq_printf(seq, "total_data_bk_pkt_send: %8d\t",
		   common->tx_stats.total_tx_pkt_send[BK_Q]);
	seq_printf(seq, "total_data_bk_pkt_queued:  %8d\t",
		   skb_queue_len(&common->tx_queue[BK_Q]));
	seq_printf(seq, "total_bk_pkt_freed: %8d\n",
		   common->tx_stats.total_tx_pkt_freed[BK_Q]);

	seq_puts(seq, "\n");
	return 0;
}

/**
 * rsi_stats_open() - This function calls single open function of seq_file to
 *		      open file and read contents from it.
 * @inode: Pointer to the inode structure.
 * @file: Pointer to the file structure.
 *
 * Return: Pointer to the opened file status: 0 on success, ENOMEM on failure.
 */
static int rsi_stats_open(struct inode *inode,
			  struct file *file)
{
	return single_open(file, rsi_stats_read, inode->i_private);
}

/**
 * rsi_debug_zone_read() - This function display the currently enabled debug zones.
 * @seq: Pointer to the sequence file structure.
 * @data: Pointer to the data.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_debug_zone_read(struct seq_file *seq, void *data)
{
	rsi_dbg(FSM_ZONE, "%x: rsi_enabled zone", rsi_zone_enabled);
	seq_printf(seq, "The zones available are %#x\n",
		   rsi_zone_enabled);
	return 0;
}

/**
 * rsi_debug_read() - This function calls single open function of seq_file to
 *		      open file and read contents from it.
 * @inode: Pointer to the inode structure.
 * @file: Pointer to the file structure.
 *
 * Return: Pointer to the opened file status: 0 on success, ENOMEM on failure.
 */
static int rsi_debug_read(struct inode *inode,
			  struct file *file)
{
	return single_open(file, rsi_debug_zone_read, inode->i_private);
}

/**
 * rsi_debug_zone_write() - This function writes into hal queues as per user
 *			    requirement.
 * @filp: Pointer to the file structure.
 * @buff: Pointer to the character buffer.
 * @len: Length of the data to be written into buffer.
 * @data: Pointer to the data.
 *
 * Return: len: Number of bytes read.
 */
static ssize_t rsi_debug_zone_write(struct file *filp,
				    const char __user *buff,
				    size_t len,
				    loff_t *data)
{
	unsigned long dbg_zone;
	int ret;

	if (!len)
		return 0;

	ret = kstrtoul_from_user(buff, len, 16, &dbg_zone);

	if (ret)
		return ret;

	rsi_zone_enabled = dbg_zone;
	return len;
}

#define FOPS(fopen) { \
	.owner = THIS_MODULE, \
	.open = (fopen), \
	.read = seq_read, \
	.llseek = seq_lseek, \
}

#define FOPS_RW(fopen, fwrite) { \
	.owner = THIS_MODULE, \
	.open = (fopen), \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.write = (fwrite), \
}

static const struct rsi_dbg_files dev_debugfs_files[] = {
	{"version", 0644, FOPS(rsi_version_open),},
	{"stats", 0644, FOPS(rsi_stats_open),},
	{"debug_zone", 0666, FOPS_RW(rsi_debug_read, rsi_debug_zone_write),},
	{"sdio_stats", 0644, FOPS(rsi_sdio_stats_open),},
};

/**
 * rsi_init_dbgfs() - This function initializes the dbgfs entry.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_init_dbgfs(struct rsi_hw *adapter)
{
	struct rsi_common *common = adapter->priv;
	struct rsi_debugfs *dev_dbgfs;
	char devdir[6];
	int ii;
	const struct rsi_dbg_files *files;

	dev_dbgfs = kzalloc(sizeof(*dev_dbgfs), GFP_KERNEL);
	if (!dev_dbgfs)
		return -ENOMEM;

	adapter->dfsentry = dev_dbgfs;

	snprintf(devdir, sizeof(devdir), "%s",
		 wiphy_name(adapter->hw->wiphy));

	dev_dbgfs->subdir = debugfs_create_dir(devdir, NULL);

	for (ii = 0; ii < adapter->num_debugfs_entries; ii++) {
		files = &dev_debugfs_files[ii];
		dev_dbgfs->rsi_files[ii] =
		debugfs_create_file(files->name,
				    files->perms,
				    dev_dbgfs->subdir,
				    common,
				    &files->fops);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(rsi_init_dbgfs);

/**
 * rsi_remove_dbgfs() - Removes the previously created dbgfs file entries
 *			in the reverse order of creation.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: None.
 */
void rsi_remove_dbgfs(struct rsi_hw *adapter)
{
	struct rsi_debugfs *dev_dbgfs = adapter->dfsentry;

	if (!dev_dbgfs)
		return;

	debugfs_remove_recursive(dev_dbgfs->subdir);
}
EXPORT_SYMBOL_GPL(rsi_remove_dbgfs);
