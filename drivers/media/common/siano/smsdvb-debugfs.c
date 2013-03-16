/***********************************************************************
 *
 * Copyright(c) 2013 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/usb.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"

#include "smscoreapi.h"

#include "smsdvb.h"

static struct dentry *smsdvb_debugfs_usb_root;

struct smsdvb_debugfs {
	struct kref		refcount;
	spinlock_t		lock;

	char			stats_data[PAGE_SIZE];
	unsigned		stats_count;
	bool			stats_was_read;

	wait_queue_head_t	stats_queue;
};

void smsdvb_print_dvb_stats(struct smsdvb_debugfs *debug_data,
			    struct SMSHOSTLIB_STATISTICS_ST *p)
{
	int n = 0;
	char *buf;

	spin_lock(&debug_data->lock);
	if (debug_data->stats_count) {
		spin_unlock(&debug_data->lock);
		return;
	}

	buf = debug_data->stats_data;

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsRfLocked = %d\n", p->IsRfLocked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsDemodLocked = %d\n", p->IsDemodLocked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsExternalLNAOn = %d\n", p->IsExternalLNAOn);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SNR = %d\n", p->SNR);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "BER = %d\n", p->BER);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "FIB_CRC = %d\n", p->FIB_CRC);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "TS_PER = %d\n", p->TS_PER);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "MFER = %d\n", p->MFER);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "RSSI = %d\n", p->RSSI);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "InBandPwr = %d\n", p->InBandPwr);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "CarrierOffset = %d\n", p->CarrierOffset);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ModemState = %d\n", p->ModemState);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "Frequency = %d\n", p->Frequency);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "Bandwidth = %d\n", p->Bandwidth);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "TransmissionMode = %d\n", p->TransmissionMode);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ModemState = %d\n", p->ModemState);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "GuardInterval = %d\n", p->GuardInterval);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "CodeRate = %d\n", p->CodeRate);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "LPCodeRate = %d\n", p->LPCodeRate);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "Hierarchy = %d\n", p->Hierarchy);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "Constellation = %d\n", p->Constellation);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "BurstSize = %d\n", p->BurstSize);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "BurstDuration = %d\n", p->BurstDuration);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "BurstCycleTime = %d\n", p->BurstCycleTime);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "CalculatedBurstCycleTime = %d\n",
	              p->CalculatedBurstCycleTime);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumOfRows = %d\n", p->NumOfRows);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumOfPaddCols = %d\n", p->NumOfPaddCols);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumOfPunctCols = %d\n", p->NumOfPunctCols);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ErrorTSPackets = %d\n", p->ErrorTSPackets);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "TotalTSPackets = %d\n", p->TotalTSPackets);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumOfValidMpeTlbs = %d\n", p->NumOfValidMpeTlbs);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumOfInvalidMpeTlbs = %d\n", p->NumOfInvalidMpeTlbs);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumOfCorrectedMpeTlbs = %d\n", p->NumOfCorrectedMpeTlbs);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "BERErrorCount = %d\n", p->BERErrorCount);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "BERBitCount = %d\n", p->BERBitCount);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SmsToHostTxErrors = %d\n", p->SmsToHostTxErrors);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "PreBER = %d\n", p->PreBER);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "CellId = %d\n", p->CellId);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "DvbhSrvIndHP = %d\n", p->DvbhSrvIndHP);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "DvbhSrvIndLP = %d\n", p->DvbhSrvIndLP);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumMPEReceived = %d\n", p->NumMPEReceived);

	debug_data->stats_count = n;
	spin_unlock(&debug_data->lock);
	wake_up(&debug_data->stats_queue);
}

void smsdvb_print_isdb_stats(struct smsdvb_debugfs *debug_data,
			     struct SMSHOSTLIB_STATISTICS_ISDBT_ST *p)
{
	int i, n = 0;
	char *buf;

	spin_lock(&debug_data->lock);
	if (debug_data->stats_count) {
		spin_unlock(&debug_data->lock);
		return;
	}

	buf = debug_data->stats_data;

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "StatisticsType = %d\t", p->StatisticsType);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "FullSize = %d\n", p->FullSize);

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsRfLocked = %d\t\t", p->IsRfLocked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsDemodLocked = %d\t", p->IsDemodLocked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsExternalLNAOn = %d\n", p->IsExternalLNAOn);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SNR = %d dB\t\t", p->SNR);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "RSSI = %d dBm\t\t", p->RSSI);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "InBandPwr = %d dBm\n", p->InBandPwr);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "CarrierOffset = %d\t", p->CarrierOffset);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "Bandwidth = %d\t\t", p->Bandwidth);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "Frequency = %d Hz\n", p->Frequency);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "TransmissionMode = %d\t", p->TransmissionMode);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ModemState = %d\t\t", p->ModemState);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "GuardInterval = %d\n", p->GuardInterval);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SystemType = %d\t\t", p->SystemType);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "PartialReception = %d\t", p->PartialReception);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumOfLayers = %d\n", p->NumOfLayers);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SmsToHostTxErrors = %d\n", p->SmsToHostTxErrors);

	for (i = 0; i < 3; i++) {
		if (p->LayerInfo[i].NumberOfSegments < 1 ||
		    p->LayerInfo[i].NumberOfSegments > 13)
			continue;

		n += snprintf(&buf[n], PAGE_SIZE - n, "\nLayer %d\n", i);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tCodeRate = %d\t",
			      p->LayerInfo[i].CodeRate);
		n += snprintf(&buf[n], PAGE_SIZE - n, "Constellation = %d\n",
			      p->LayerInfo[i].Constellation);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tBER = %-5d\t",
			      p->LayerInfo[i].BER);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tBERErrorCount = %-5d\t",
			      p->LayerInfo[i].BERErrorCount);
		n += snprintf(&buf[n], PAGE_SIZE - n, "BERBitCount = %-5d\n",
			      p->LayerInfo[i].BERBitCount);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tPreBER = %-5d\t",
			      p->LayerInfo[i].PreBER);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tTS_PER = %-5d\n",
			      p->LayerInfo[i].TS_PER);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tErrorTSPackets = %-5d\t",
			      p->LayerInfo[i].ErrorTSPackets);
		n += snprintf(&buf[n], PAGE_SIZE - n, "TotalTSPackets = %-5d\t",
			      p->LayerInfo[i].TotalTSPackets);
		n += snprintf(&buf[n], PAGE_SIZE - n, "TILdepthI = %d\n",
			      p->LayerInfo[i].TILdepthI);
		n += snprintf(&buf[n], PAGE_SIZE - n,
			      "\tNumberOfSegments = %d\t",
			      p->LayerInfo[i].NumberOfSegments);
		n += snprintf(&buf[n], PAGE_SIZE - n, "TMCCErrors = %d\n",
			      p->LayerInfo[i].TMCCErrors);
	}

	debug_data->stats_count = n;
	spin_unlock(&debug_data->lock);
	wake_up(&debug_data->stats_queue);
}

void smsdvb_print_isdb_stats_ex(struct smsdvb_debugfs *debug_data,
				struct SMSHOSTLIB_STATISTICS_ISDBT_EX_ST *p)
{
	int i, n = 0;
	char *buf;

	spin_lock(&debug_data->lock);
	if (debug_data->stats_count) {
		spin_unlock(&debug_data->lock);
		return;
	}

	buf = debug_data->stats_data;

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "StatisticsType = %d\t", p->StatisticsType);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "FullSize = %d\n", p->FullSize);

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsRfLocked = %d\t\t", p->IsRfLocked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsDemodLocked = %d\t", p->IsDemodLocked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "IsExternalLNAOn = %d\n", p->IsExternalLNAOn);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SNR = %d dB\t\t", p->SNR);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "RSSI = %d dBm\t\t", p->RSSI);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "InBandPwr = %d dBm\n", p->InBandPwr);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "CarrierOffset = %d\t", p->CarrierOffset);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "Bandwidth = %d\t\t", p->Bandwidth);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "Frequency = %d Hz\n", p->Frequency);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "TransmissionMode = %d\t", p->TransmissionMode);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ModemState = %d\t\t", p->ModemState);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "GuardInterval = %d\n", p->GuardInterval);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SystemType = %d\t\t", p->SystemType);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "PartialReception = %d\t", p->PartialReception);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "NumOfLayers = %d\n", p->NumOfLayers);
	n += snprintf(&buf[n], PAGE_SIZE - n, "SegmentNumber = %d\t",
		      p->SegmentNumber);
	n += snprintf(&buf[n], PAGE_SIZE - n, "TuneBW = %d\n",
		      p->TuneBW);

	for (i = 0; i < 3; i++) {
		if (p->LayerInfo[i].NumberOfSegments < 1 ||
		    p->LayerInfo[i].NumberOfSegments > 13)
			continue;

		n += snprintf(&buf[n], PAGE_SIZE - n, "\nLayer %d\n", i);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tCodeRate = %d\t",
			      p->LayerInfo[i].CodeRate);
		n += snprintf(&buf[n], PAGE_SIZE - n, "Constellation = %d\n",
			      p->LayerInfo[i].Constellation);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tBER = %-5d\t",
			      p->LayerInfo[i].BER);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tBERErrorCount = %-5d\t",
			      p->LayerInfo[i].BERErrorCount);
		n += snprintf(&buf[n], PAGE_SIZE - n, "BERBitCount = %-5d\n",
			      p->LayerInfo[i].BERBitCount);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tPreBER = %-5d\t",
			      p->LayerInfo[i].PreBER);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tTS_PER = %-5d\n",
			      p->LayerInfo[i].TS_PER);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tErrorTSPackets = %-5d\t",
			      p->LayerInfo[i].ErrorTSPackets);
		n += snprintf(&buf[n], PAGE_SIZE - n, "TotalTSPackets = %-5d\t",
			      p->LayerInfo[i].TotalTSPackets);
		n += snprintf(&buf[n], PAGE_SIZE - n, "TILdepthI = %d\n",
			      p->LayerInfo[i].TILdepthI);
		n += snprintf(&buf[n], PAGE_SIZE - n,
			      "\tNumberOfSegments = %d\t",
			      p->LayerInfo[i].NumberOfSegments);
		n += snprintf(&buf[n], PAGE_SIZE - n, "TMCCErrors = %d\n",
			      p->LayerInfo[i].TMCCErrors);
	}


	debug_data->stats_count = n;
	spin_unlock(&debug_data->lock);

	wake_up(&debug_data->stats_queue);
}

static int smsdvb_stats_open(struct inode *inode, struct file *file)
{
	struct smsdvb_client_t *client = inode->i_private;
	struct smsdvb_debugfs *debug_data = client->debug_data;

	kref_get(&debug_data->refcount);

	spin_lock(&debug_data->lock);
	debug_data->stats_count = 0;
	debug_data->stats_was_read = false;
	spin_unlock(&debug_data->lock);

	file->private_data = debug_data;

	return 0;
}

static int smsdvb_stats_wait_read(struct smsdvb_debugfs *debug_data)
{
	int rc = 1;

	spin_lock(&debug_data->lock);

	if (debug_data->stats_was_read)
		goto exit;

	rc = debug_data->stats_count;

exit:
	spin_unlock(&debug_data->lock);
	return rc;
}

static ssize_t smsdvb_stats_read(struct file *file, char __user *user_buf,
				      size_t nbytes, loff_t *ppos)
{
	int rc = 0;
	struct smsdvb_debugfs *debug_data = file->private_data;

	rc = wait_event_interruptible(debug_data->stats_queue,
				      smsdvb_stats_wait_read(debug_data));
	if (rc < 0)
		return rc;

	rc = simple_read_from_buffer(user_buf, nbytes, ppos,
				     debug_data->stats_data,
				     debug_data->stats_count);
	spin_lock(&debug_data->lock);
	debug_data->stats_was_read = true;
	spin_unlock(&debug_data->lock);

	return rc;
}

static void smsdvb_debugfs_data_release(struct kref *ref)
{
	struct smsdvb_debugfs *debug_data;

	debug_data = container_of(ref, struct smsdvb_debugfs, refcount);
	kfree(debug_data);
}

static int smsdvb_stats_release(struct inode *inode, struct file *file)
{
	struct smsdvb_debugfs *debug_data = file->private_data;

	spin_lock(&debug_data->lock);
	debug_data->stats_was_read = true;
	spin_unlock(&debug_data->lock);
	wake_up_interruptible_sync(&debug_data->stats_queue);

	kref_put(&debug_data->refcount, smsdvb_debugfs_data_release);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations debugfs_stats_ops = {
	.open = smsdvb_stats_open,
	.read = smsdvb_stats_read,
	.release = smsdvb_stats_release,
	.llseek = generic_file_llseek,
};

/*
 * Functions used by smsdvb, in order to create the interfaces
 */

int smsdvb_debugfs_create(struct smsdvb_client_t *client)
{
	struct smscore_device_t *coredev = client->coredev;
	struct dentry *d;
	struct smsdvb_debugfs *debug_data;

	if (!smsdvb_debugfs_usb_root || !coredev->is_usb_device)
		return -ENODEV;

	client->debugfs = debugfs_create_dir(coredev->devpath,
					     smsdvb_debugfs_usb_root);
	if (IS_ERR_OR_NULL(client->debugfs)) {
		pr_info("Unable to create debugfs %s directory.\n",
			coredev->devpath);
		return -ENODEV;
	}

	d = debugfs_create_file("stats", S_IRUGO | S_IWUSR, client->debugfs,
				client, &debugfs_stats_ops);
	if (!d) {
		debugfs_remove(client->debugfs);
		return -ENOMEM;
	}

	debug_data = kzalloc(sizeof(*client->debug_data), GFP_KERNEL);
	if (!debug_data)
		return -ENOMEM;

	client->debug_data        = debug_data;
	client->prt_dvb_stats     = smsdvb_print_dvb_stats;
	client->prt_isdb_stats    = smsdvb_print_isdb_stats;
	client->prt_isdb_stats_ex = smsdvb_print_isdb_stats_ex;

	init_waitqueue_head(&debug_data->stats_queue);
	spin_lock_init(&debug_data->lock);
	kref_init(&debug_data->refcount);

	return 0;
}

void smsdvb_debugfs_release(struct smsdvb_client_t *client)
{
	if (!client->debugfs)
		return;

printk("%s\n", __func__);

	client->prt_dvb_stats     = NULL;
	client->prt_isdb_stats    = NULL;
	client->prt_isdb_stats_ex = NULL;

	debugfs_remove_recursive(client->debugfs);
	kref_put(&client->debug_data->refcount, smsdvb_debugfs_data_release);

	client->debug_data = NULL;
	client->debugfs = NULL;
}

int smsdvb_debugfs_register(void)
{
	struct dentry *d;

	/*
	 * FIXME: This was written to debug Siano USB devices. So, it creates
	 * the debugfs node under <debugfs>/usb.
	 * A similar logic would be needed for Siano sdio devices, but, in that
	 * case, usb_debug_root is not a good choice.
	 *
	 * Perhaps the right fix here would be to create another sysfs root
	 * node for sdio-based boards, but this may need some logic at sdio
	 * subsystem.
	 */
	d = debugfs_create_dir("smsdvb", usb_debug_root);
	if (IS_ERR_OR_NULL(d)) {
		sms_err("Couldn't create sysfs node for smsdvb");
		return PTR_ERR(d);
	} else {
		smsdvb_debugfs_usb_root = d;
	}
	return 0;
}

void smsdvb_debugfs_unregister(void)
{
	if (smsdvb_debugfs_usb_root)
		debugfs_remove_recursive(smsdvb_debugfs_usb_root);
	smsdvb_debugfs_usb_root = NULL;
}
