/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2006-2008, Uri Shkolnik

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

 This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/debugfs.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"

#include "smscoreapi.h"
#include "sms-cards.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct smsdvb_client_t {
	struct list_head entry;

	struct smscore_device_t *coredev;
	struct smscore_client_t *smsclient;

	struct dvb_adapter      adapter;
	struct dvb_demux        demux;
	struct dmxdev           dmxdev;
	struct dvb_frontend     frontend;

	fe_status_t             fe_status;

	struct completion       tune_done;
	struct completion       stats_done;

	int last_per;

	int legacy_ber, legacy_per;

	int event_fe_state;
	int event_unc_state;

	/* Stats debugfs data */
	struct dentry		*debugfs;
	char			*stats_data;
	atomic_t		stats_count;
	bool			stats_was_read;
	wait_queue_head_t	stats_queue;
};

static struct list_head g_smsdvb_clients;
static struct mutex g_smsdvb_clientslock;

static int sms_dbg;
module_param_named(debug, sms_dbg, int, 0644);
MODULE_PARM_DESC(debug, "set debug level (info=1, adv=2 (or-able))");

/*
 * This struct is a mix of RECEPTION_STATISTICS_EX_S and SRVM_SIGNAL_STATUS_S.
 * It was obtained by comparing the way it was filled by the original code
 */
struct RECEPTION_STATISTICS_PER_SLICES_S {
	u32 result;
	u32 snr;
	s32 inBandPower;
	u32 tsPackets;
	u32 etsPackets;
	u32 constellation;
	u32 hpCode;
	u32 tpsSrvIndLP;
	u32 tpsSrvIndHP;
	u32 cellId;
	u32 reason;
	u32 requestId;
	u32 ModemState;		/* from SMSHOSTLIB_DVB_MODEM_STATE_ET */

	u32 BER;		/* Post Viterbi BER [1E-5] */
	s32 RSSI;		/* dBm */
	s32 CarrierOffset;	/* Carrier Offset in bin/1024 */

	u32 IsRfLocked;		/* 0 - not locked, 1 - locked */
	u32 IsDemodLocked;	/* 0 - not locked, 1 - locked */

	u32 BERBitCount;	/* Total number of SYNC bits. */
	u32 BERErrorCount;	/* Number of erronous SYNC bits. */

	s32 MRC_SNR;		/* dB */
	s32 MRC_InBandPwr;	/* In band power in dBM */
	s32 MRC_RSSI;		/* dBm */
};

u32 sms_to_bw_table[] = {
	[BW_8_MHZ]		= 8000000,
	[BW_7_MHZ]		= 7000000,
	[BW_6_MHZ]		= 6000000,
	[BW_5_MHZ]		= 5000000,
	[BW_2_MHZ]		= 2000000,
	[BW_1_5_MHZ]		= 1500000,
	[BW_ISDBT_1SEG]		= 6000000,
	[BW_ISDBT_3SEG]		= 6000000,
	[BW_ISDBT_13SEG]	= 6000000,
};

u32 sms_to_guard_interval_table[] = {
	[0] = GUARD_INTERVAL_1_32,
	[1] = GUARD_INTERVAL_1_16,
	[2] = GUARD_INTERVAL_1_8,
	[3] = GUARD_INTERVAL_1_4,
};

u32 sms_to_code_rate_table[] = {
	[0] = FEC_1_2,
	[1] = FEC_2_3,
	[2] = FEC_3_4,
	[3] = FEC_5_6,
	[4] = FEC_7_8,
};


u32 sms_to_hierarchy_table[] = {
	[0] = HIERARCHY_NONE,
	[1] = HIERARCHY_1,
	[2] = HIERARCHY_2,
	[3] = HIERARCHY_4,
};

u32 sms_to_modulation_table[] = {
	[0] = QPSK,
	[1] = QAM_16,
	[2] = QAM_64,
	[3] = DQPSK,
};

static struct dentry *smsdvb_debugfs;

static void smsdvb_print_dvb_stats(struct smsdvb_client_t *client,
				   struct SMSHOSTLIB_STATISTICS_ST *p)
{
	int n = 0;
	char *buf;

	if (!client->stats_data || atomic_read(&client->stats_count))
		return;

	buf = client->stats_data;

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

	atomic_set(&client->stats_count, n);
	wake_up(&client->stats_queue);
}

static void smsdvb_print_isdb_stats(struct smsdvb_client_t *client,
				    struct SMSHOSTLIB_STATISTICS_ISDBT_ST *p)
{
	int i, n = 0;
	char *buf;

	if (!client->stats_data || atomic_read(&client->stats_count))
		return;

	buf = client->stats_data;

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

	atomic_set(&client->stats_count, n);
	wake_up(&client->stats_queue);
}

static void
smsdvb_print_isdb_stats_ex(struct smsdvb_client_t *client,
			   struct SMSHOSTLIB_STATISTICS_ISDBT_EX_ST *p)
{
	int i, n = 0;
	char *buf;

	if (!client->stats_data || atomic_read(&client->stats_count))
		return;

	buf = client->stats_data;

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

	atomic_set(&client->stats_count, n);
	wake_up(&client->stats_queue);
}

static int smsdvb_stats_open(struct inode *inode, struct file *file)
{
	struct smsdvb_client_t *client = inode->i_private;

	atomic_set(&client->stats_count, 0);
	client->stats_was_read = false;

	init_waitqueue_head(&client->stats_queue);

	client->stats_data = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (client->stats_data == NULL)
		return -ENOMEM;

	file->private_data = client;

	return 0;
}

static ssize_t smsdvb_stats_read(struct file *file, char __user *user_buf,
				      size_t nbytes, loff_t *ppos)
{
	struct smsdvb_client_t *client = file->private_data;

	if (!client->stats_data || client->stats_was_read)
		return 0;

	wait_event_interruptible(client->stats_queue,
				 atomic_read(&client->stats_count));

	return simple_read_from_buffer(user_buf, nbytes, ppos,
				       client->stats_data,
				       atomic_read(&client->stats_count));

	client->stats_was_read = true;
}

static int smsdvb_stats_release(struct inode *inode, struct file *file)
{
	struct smsdvb_client_t *client = file->private_data;

	kfree(client->stats_data);
	client->stats_data = NULL;

	return 0;
}

static const struct file_operations debugfs_stats_ops = {
	.open = smsdvb_stats_open,
	.read = smsdvb_stats_read,
	.release = smsdvb_stats_release,
	.llseek = generic_file_llseek,
};

static int create_stats_debugfs(struct smsdvb_client_t *client)
{
	struct smscore_device_t *coredev = client->coredev;
	struct dentry *d;

	if (!smsdvb_debugfs)
		return -ENODEV;

	client->debugfs = debugfs_create_dir(coredev->devpath, smsdvb_debugfs);
	if (IS_ERR_OR_NULL(client->debugfs)) {
		sms_info("Unable to create debugfs %s directory.\n",
			 coredev->devpath);
		return -ENODEV;
	}

	d = debugfs_create_file("stats", S_IRUGO | S_IWUSR, client->debugfs,
				client, &debugfs_stats_ops);
	if (!d) {
		debugfs_remove(client->debugfs);
		return -ENOMEM;
	}

	return 0;
}

static void release_stats_debugfs(struct smsdvb_client_t *client)
{
	if (!client->debugfs)
		return;

	debugfs_remove_recursive(client->debugfs);

	client->debugfs = NULL;
}

/* Events that may come from DVB v3 adapter */
static void sms_board_dvb3_event(struct smsdvb_client_t *client,
		enum SMS_DVB3_EVENTS event) {

	struct smscore_device_t *coredev = client->coredev;
	switch (event) {
	case DVB3_EVENT_INIT:
		sms_debug("DVB3_EVENT_INIT");
		sms_board_event(coredev, BOARD_EVENT_BIND);
		break;
	case DVB3_EVENT_SLEEP:
		sms_debug("DVB3_EVENT_SLEEP");
		sms_board_event(coredev, BOARD_EVENT_POWER_SUSPEND);
		break;
	case DVB3_EVENT_HOTPLUG:
		sms_debug("DVB3_EVENT_HOTPLUG");
		sms_board_event(coredev, BOARD_EVENT_POWER_INIT);
		break;
	case DVB3_EVENT_FE_LOCK:
		if (client->event_fe_state != DVB3_EVENT_FE_LOCK) {
			client->event_fe_state = DVB3_EVENT_FE_LOCK;
			sms_debug("DVB3_EVENT_FE_LOCK");
			sms_board_event(coredev, BOARD_EVENT_FE_LOCK);
		}
		break;
	case DVB3_EVENT_FE_UNLOCK:
		if (client->event_fe_state != DVB3_EVENT_FE_UNLOCK) {
			client->event_fe_state = DVB3_EVENT_FE_UNLOCK;
			sms_debug("DVB3_EVENT_FE_UNLOCK");
			sms_board_event(coredev, BOARD_EVENT_FE_UNLOCK);
		}
		break;
	case DVB3_EVENT_UNC_OK:
		if (client->event_unc_state != DVB3_EVENT_UNC_OK) {
			client->event_unc_state = DVB3_EVENT_UNC_OK;
			sms_debug("DVB3_EVENT_UNC_OK");
			sms_board_event(coredev, BOARD_EVENT_MULTIPLEX_OK);
		}
		break;
	case DVB3_EVENT_UNC_ERR:
		if (client->event_unc_state != DVB3_EVENT_UNC_ERR) {
			client->event_unc_state = DVB3_EVENT_UNC_ERR;
			sms_debug("DVB3_EVENT_UNC_ERR");
			sms_board_event(coredev, BOARD_EVENT_MULTIPLEX_ERRORS);
		}
		break;

	default:
		sms_err("Unknown dvb3 api event");
		break;
	}
}

static void smsdvb_stats_not_ready(struct dvb_frontend *fe)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	struct smscore_device_t *coredev = client->coredev;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int i, n_layers;

	switch (smscore_get_device_mode(coredev)) {
	case DEVICE_MODE_ISDBT:
	case DEVICE_MODE_ISDBT_BDA:
		n_layers = 4;
	default:
		n_layers = 1;
	}

	/* Fill the length of each status counter */

	/* Only global stats */
	c->strength.len = 1;
	c->cnr.len = 1;

	/* Per-layer stats */
	c->post_bit_error.len = n_layers;
	c->post_bit_count.len = n_layers;
	c->block_error.len = n_layers;
	c->block_count.len = n_layers;

	/* Signal is always available */
	c->strength.stat[0].scale = FE_SCALE_RELATIVE;
	c->strength.stat[0].uvalue = 0;

	/* Put all of them at FE_SCALE_NOT_AVAILABLE */
	for (i = 0; i < n_layers; i++) {
		c->cnr.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_error.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_error.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_count.stat[i].scale = FE_SCALE_NOT_AVAILABLE;
	}
}

static inline int sms_to_mode(u32 mode)
{
	switch (mode) {
	case 2:
		return TRANSMISSION_MODE_2K;
	case 4:
		return TRANSMISSION_MODE_4K;
	case 8:
		return TRANSMISSION_MODE_8K;
	}
	return TRANSMISSION_MODE_AUTO;
}

static inline int sms_to_status(u32 is_demod_locked, u32 is_rf_locked)
{
	if (is_demod_locked)
		return FE_HAS_SIGNAL  | FE_HAS_CARRIER | FE_HAS_VITERBI |
		       FE_HAS_SYNC    | FE_HAS_LOCK;

	if (is_rf_locked)
		return FE_HAS_SIGNAL | FE_HAS_CARRIER;

	return 0;
}


#define convert_from_table(value, table, defval) ({			\
	u32 __ret;							\
	if (value < ARRAY_SIZE(table))					\
		__ret = table[value];					\
	else								\
		__ret = defval;						\
	__ret;								\
})

#define sms_to_bw(value)						\
	convert_from_table(value, sms_to_bw_table, 0);

#define sms_to_guard_interval(value)					\
	convert_from_table(value, sms_to_guard_interval_table,		\
			   GUARD_INTERVAL_AUTO);

#define sms_to_code_rate(value)						\
	convert_from_table(value, sms_to_code_rate_table,		\
			   FEC_NONE);

#define sms_to_hierarchy(value)						\
	convert_from_table(value, sms_to_hierarchy_table,		\
			   FEC_NONE);

#define sms_to_modulation(value)					\
	convert_from_table(value, sms_to_modulation_table,		\
			   FEC_NONE);

static void smsdvb_update_tx_params(struct smsdvb_client_t *client,
				    struct TRANSMISSION_STATISTICS_S *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	c->frequency = p->Frequency;
	client->fe_status = sms_to_status(p->IsDemodLocked, 0);
	c->bandwidth_hz = sms_to_bw(p->Bandwidth);
	c->transmission_mode = sms_to_mode(p->TransmissionMode);
	c->guard_interval = sms_to_guard_interval(p->GuardInterval);
	c->code_rate_HP = sms_to_code_rate(p->CodeRate);
	c->code_rate_LP = sms_to_code_rate(p->LPCodeRate);
	c->hierarchy = sms_to_hierarchy(p->Hierarchy);
	c->modulation = sms_to_modulation(p->Constellation);
}

static void smsdvb_update_per_slices(struct smsdvb_client_t *client,
				     struct RECEPTION_STATISTICS_PER_SLICES_S *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	client->fe_status = sms_to_status(p->IsDemodLocked, p->IsRfLocked);
	c->modulation = sms_to_modulation(p->constellation);

	/* TS PER */
	client->last_per = c->block_error.stat[0].uvalue;
	c->block_error.stat[0].scale = FE_SCALE_COUNTER;
	c->block_count.stat[0].scale = FE_SCALE_COUNTER;
	c->block_error.stat[0].uvalue += p->etsPackets;
	c->block_count.stat[0].uvalue += p->etsPackets + p->tsPackets;

	/* BER */
	c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_error.stat[0].uvalue += p->BERErrorCount;
	c->post_bit_count.stat[0].uvalue += p->BERBitCount;

	/* Legacy PER/BER */
	client->legacy_per = (p->etsPackets * 65535) /
			     (p->tsPackets + p->etsPackets);

	/* Signal Strength, in DBm */
	c->strength.stat[0].uvalue = p->RSSI * 1000;

	/* Carrier to Noise ratio, in DB */
	c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	c->cnr.stat[0].svalue = p->snr * 1000;
}

static void smsdvb_update_dvb_stats(struct smsdvb_client_t *client,
				    struct SMSHOSTLIB_STATISTICS_ST *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	smsdvb_print_dvb_stats(client, p);

	client->fe_status = sms_to_status(p->IsDemodLocked, p->IsRfLocked);

	/* Update DVB modulation parameters */
	c->frequency = p->Frequency;
	client->fe_status = sms_to_status(p->IsDemodLocked, 0);
	c->bandwidth_hz = sms_to_bw(p->Bandwidth);
	c->transmission_mode = sms_to_mode(p->TransmissionMode);
	c->guard_interval = sms_to_guard_interval(p->GuardInterval);
	c->code_rate_HP = sms_to_code_rate(p->CodeRate);
	c->code_rate_LP = sms_to_code_rate(p->LPCodeRate);
	c->hierarchy = sms_to_hierarchy(p->Hierarchy);
	c->modulation = sms_to_modulation(p->Constellation);

	/* update reception data */
	c->lna = p->IsExternalLNAOn ? 1 : 0;

	/* Carrier to Noise ratio, in DB */
	c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	c->cnr.stat[0].svalue = p->SNR * 1000;

	/* Signal Strength, in DBm */
	c->strength.stat[0].uvalue = p->RSSI * 1000;

	/* TS PER */
	client->last_per = c->block_error.stat[0].uvalue;
	c->block_error.stat[0].scale = FE_SCALE_COUNTER;
	c->block_count.stat[0].scale = FE_SCALE_COUNTER;
	c->block_error.stat[0].uvalue += p->ErrorTSPackets;
	c->block_count.stat[0].uvalue += p->TotalTSPackets;

	/* BER */
	c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_error.stat[0].uvalue += p->BERErrorCount;
	c->post_bit_count.stat[0].uvalue += p->BERBitCount;

	/* Legacy PER/BER */
	client->legacy_ber = p->BER;
};

static void smsdvb_update_isdbt_stats(struct smsdvb_client_t *client,
				      struct SMSHOSTLIB_STATISTICS_ISDBT_ST *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct SMSHOSTLIB_ISDBT_LAYER_STAT_ST *lr;
	int i, n_layers;

	smsdvb_print_isdb_stats(client, p);

	/* Update ISDB-T transmission parameters */
	c->frequency = p->Frequency;
	client->fe_status = sms_to_status(p->IsDemodLocked, 0);
	c->bandwidth_hz = sms_to_bw(p->Bandwidth);
	c->transmission_mode = sms_to_mode(p->TransmissionMode);
	c->guard_interval = sms_to_guard_interval(p->GuardInterval);
	c->isdbt_partial_reception = p->PartialReception ? 1 : 0;
	n_layers = p->NumOfLayers;
	if (n_layers < 1)
		n_layers = 1;
	if (n_layers > 3)
		n_layers = 3;
	c->isdbt_layer_enabled = 0;

	/* update reception data */
	c->lna = p->IsExternalLNAOn ? 1 : 0;

	/* Carrier to Noise ratio, in DB */
	c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	c->cnr.stat[0].svalue = p->SNR * 1000;

	/* Signal Strength, in DBm */
	c->strength.stat[0].uvalue = p->RSSI * 1000;

	client->last_per = c->block_error.stat[0].uvalue;

	/* Clears global counters, as the code below will sum it again */
	c->block_error.stat[0].uvalue = 0;
	c->block_count.stat[0].uvalue = 0;
	c->post_bit_error.stat[0].uvalue = 0;
	c->post_bit_count.stat[0].uvalue = 0;

	for (i = 0; i < n_layers; i++) {
		lr = &p->LayerInfo[i];

		/* Update per-layer transmission parameters */
		if (lr->NumberOfSegments > 0 && lr->NumberOfSegments < 13) {
			c->isdbt_layer_enabled |= 1 << i;
			c->layer[i].segment_count = lr->NumberOfSegments;
		} else {
			continue;
		}
		c->layer[i].modulation = sms_to_modulation(lr->Constellation);

		/* TS PER */
		c->block_error.stat[i].scale = FE_SCALE_COUNTER;
		c->block_count.stat[i].scale = FE_SCALE_COUNTER;
		c->block_error.stat[i].uvalue += lr->ErrorTSPackets;
		c->block_count.stat[i].uvalue += lr->TotalTSPackets;

		/* Update global PER counter */
		c->block_error.stat[0].uvalue += lr->ErrorTSPackets;
		c->block_count.stat[0].uvalue += lr->TotalTSPackets;

		/* BER */
		c->post_bit_error.stat[i].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[i].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[i].uvalue += lr->BERErrorCount;
		c->post_bit_count.stat[i].uvalue += lr->BERBitCount;

		/* Update global BER counter */
		c->post_bit_error.stat[0].uvalue += lr->BERErrorCount;
		c->post_bit_count.stat[0].uvalue += lr->BERBitCount;
	}
}

static void smsdvb_update_isdbt_stats_ex(struct smsdvb_client_t *client,
					 struct SMSHOSTLIB_STATISTICS_ISDBT_EX_ST *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct SMSHOSTLIB_ISDBT_LAYER_STAT_ST *lr;
	int i, n_layers;

	smsdvb_print_isdb_stats_ex(client, p);

	/* Update ISDB-T transmission parameters */
	c->frequency = p->Frequency;
	client->fe_status = sms_to_status(p->IsDemodLocked, 0);
	c->bandwidth_hz = sms_to_bw(p->Bandwidth);
	c->transmission_mode = sms_to_mode(p->TransmissionMode);
	c->guard_interval = sms_to_guard_interval(p->GuardInterval);
	c->isdbt_partial_reception = p->PartialReception ? 1 : 0;
	n_layers = p->NumOfLayers;
	if (n_layers < 1)
		n_layers = 1;
	if (n_layers > 3)
		n_layers = 3;
	c->isdbt_layer_enabled = 0;

	/* update reception data */
	c->lna = p->IsExternalLNAOn ? 1 : 0;

	/* Carrier to Noise ratio, in DB */
	c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	c->cnr.stat[0].svalue = p->SNR * 1000;

	/* Signal Strength, in DBm */
	c->strength.stat[0].uvalue = p->RSSI * 1000;

	client->last_per = c->block_error.stat[0].uvalue;

	/* Clears global counters, as the code below will sum it again */
	c->block_error.stat[0].uvalue = 0;
	c->block_count.stat[0].uvalue = 0;
	c->post_bit_error.stat[0].uvalue = 0;
	c->post_bit_count.stat[0].uvalue = 0;

	for (i = 0; i < n_layers; i++) {
		lr = &p->LayerInfo[i];

		/* Update per-layer transmission parameters */
		if (lr->NumberOfSegments > 0 && lr->NumberOfSegments < 13) {
			c->isdbt_layer_enabled |= 1 << i;
			c->layer[i].segment_count = lr->NumberOfSegments;
		} else {
			continue;
		}
		c->layer[i].modulation = sms_to_modulation(lr->Constellation);

		/* TS PER */
		c->block_error.stat[i].scale = FE_SCALE_COUNTER;
		c->block_count.stat[i].scale = FE_SCALE_COUNTER;
		c->block_error.stat[i].uvalue += lr->ErrorTSPackets;
		c->block_count.stat[i].uvalue += lr->TotalTSPackets;

		/* Update global PER counter */
		c->block_error.stat[0].uvalue += lr->ErrorTSPackets;
		c->block_count.stat[0].uvalue += lr->TotalTSPackets;

		/* BER */
		c->post_bit_error.stat[i].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[i].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[i].uvalue += lr->BERErrorCount;
		c->post_bit_count.stat[i].uvalue += lr->BERBitCount;

		/* Update global BER counter */
		c->post_bit_error.stat[0].uvalue += lr->BERErrorCount;
		c->post_bit_count.stat[0].uvalue += lr->BERBitCount;
	}
}

static int smsdvb_onresponse(void *context, struct smscore_buffer_t *cb)
{
	struct smsdvb_client_t *client = (struct smsdvb_client_t *) context;
	struct SmsMsgHdr_ST *phdr = (struct SmsMsgHdr_ST *) (((u8 *) cb->p)
			+ cb->offset);
	void *p = phdr + 1;
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	bool is_status_update = false;

	switch (phdr->msgType) {
	case MSG_SMS_DVBT_BDA_DATA:
		dvb_dmx_swfilter(&client->demux, p,
				 cb->size - sizeof(struct SmsMsgHdr_ST));
		break;

	case MSG_SMS_RF_TUNE_RES:
	case MSG_SMS_ISDBT_TUNE_RES:
		complete(&client->tune_done);
		break;

	case MSG_SMS_SIGNAL_DETECTED_IND:
		client->fe_status = FE_HAS_SIGNAL  | FE_HAS_CARRIER |
				    FE_HAS_VITERBI | FE_HAS_SYNC    |
				    FE_HAS_LOCK;

		is_status_update = true;
		break;

	case MSG_SMS_NO_SIGNAL_IND:
		client->fe_status = 0;

		is_status_update = true;
		break;

	case MSG_SMS_TRANSMISSION_IND:
		smsdvb_update_tx_params(client, p);

		is_status_update = true;
		break;

	case MSG_SMS_HO_PER_SLICES_IND:
		smsdvb_update_per_slices(client, p);

		is_status_update = true;
		break;

	case MSG_SMS_GET_STATISTICS_RES:
		switch (smscore_get_device_mode(client->coredev)) {
		case DEVICE_MODE_ISDBT:
		case DEVICE_MODE_ISDBT_BDA:
			smsdvb_update_isdbt_stats(client, p);
			break;
		default:
			/* Skip SmsMsgStatisticsInfo_ST:RequestResult field */
			smsdvb_update_dvb_stats(client, p + sizeof(u32));
		}

		is_status_update = true;
		break;

	/* Only for ISDB-T */
	case MSG_SMS_GET_STATISTICS_EX_RES:
		/* Skip SmsMsgStatisticsInfo_ST:RequestResult field? */
		smsdvb_update_isdbt_stats_ex(client, p + sizeof(u32));
		is_status_update = true;
		break;
	default:
		sms_info("message not handled");
	}
	smscore_putbuffer(client->coredev, cb);

	if (is_status_update) {
		if (client->fe_status == FE_HAS_LOCK) {
			sms_board_dvb3_event(client, DVB3_EVENT_FE_LOCK);
			if (client->last_per == c->block_error.stat[0].uvalue)
				sms_board_dvb3_event(client, DVB3_EVENT_UNC_OK);
			else
				sms_board_dvb3_event(client, DVB3_EVENT_UNC_ERR);
		} else {
			smsdvb_stats_not_ready(fe);

			sms_board_dvb3_event(client, DVB3_EVENT_FE_UNLOCK);
		}
		complete(&client->stats_done);
	}

	return 0;
}

static void smsdvb_unregister_client(struct smsdvb_client_t *client)
{
	/* must be called under clientslock */

	list_del(&client->entry);

	release_stats_debugfs(client);
	smscore_unregister_client(client->smsclient);
	dvb_unregister_frontend(&client->frontend);
	dvb_dmxdev_release(&client->dmxdev);
	dvb_dmx_release(&client->demux);
	dvb_unregister_adapter(&client->adapter);
	kfree(client);
}

static void smsdvb_onremove(void *context)
{
	kmutex_lock(&g_smsdvb_clientslock);

	smsdvb_unregister_client((struct smsdvb_client_t *) context);

	kmutex_unlock(&g_smsdvb_clientslock);
}

static int smsdvb_start_feed(struct dvb_demux_feed *feed)
{
	struct smsdvb_client_t *client =
		container_of(feed->demux, struct smsdvb_client_t, demux);
	struct SmsMsgData_ST PidMsg;

	sms_debug("add pid %d(%x)",
		  feed->pid, feed->pid);

	PidMsg.xMsgHeader.msgSrcId = DVBT_BDA_CONTROL_MSG_ID;
	PidMsg.xMsgHeader.msgDstId = HIF_TASK;
	PidMsg.xMsgHeader.msgFlags = 0;
	PidMsg.xMsgHeader.msgType  = MSG_SMS_ADD_PID_FILTER_REQ;
	PidMsg.xMsgHeader.msgLength = sizeof(PidMsg);
	PidMsg.msgData[0] = feed->pid;

	return smsclient_sendrequest(client->smsclient,
				     &PidMsg, sizeof(PidMsg));
}

static int smsdvb_stop_feed(struct dvb_demux_feed *feed)
{
	struct smsdvb_client_t *client =
		container_of(feed->demux, struct smsdvb_client_t, demux);
	struct SmsMsgData_ST PidMsg;

	sms_debug("remove pid %d(%x)",
		  feed->pid, feed->pid);

	PidMsg.xMsgHeader.msgSrcId = DVBT_BDA_CONTROL_MSG_ID;
	PidMsg.xMsgHeader.msgDstId = HIF_TASK;
	PidMsg.xMsgHeader.msgFlags = 0;
	PidMsg.xMsgHeader.msgType  = MSG_SMS_REMOVE_PID_FILTER_REQ;
	PidMsg.xMsgHeader.msgLength = sizeof(PidMsg);
	PidMsg.msgData[0] = feed->pid;

	return smsclient_sendrequest(client->smsclient,
				     &PidMsg, sizeof(PidMsg));
}

static int smsdvb_sendrequest_and_wait(struct smsdvb_client_t *client,
					void *buffer, size_t size,
					struct completion *completion)
{
	int rc;

	rc = smsclient_sendrequest(client->smsclient, buffer, size);
	if (rc < 0)
		return rc;

	return wait_for_completion_timeout(completion,
					   msecs_to_jiffies(2000)) ?
						0 : -ETIME;
}

static int smsdvb_send_statistics_request(struct smsdvb_client_t *client)
{
	int rc;
	struct SmsMsgHdr_ST Msg;


	Msg.msgSrcId = DVBT_BDA_CONTROL_MSG_ID;
	Msg.msgDstId = HIF_TASK;
	Msg.msgFlags = 0;
	Msg.msgLength = sizeof(Msg);

	switch (smscore_get_device_mode(client->coredev)) {
	case DEVICE_MODE_ISDBT:
	case DEVICE_MODE_ISDBT_BDA:
		/*
		* Check for firmware version, to avoid breaking for old cards
		*/
		if (client->coredev->fw_version >= 0x800)
			Msg.msgType = MSG_SMS_GET_STATISTICS_EX_REQ;
		else
			Msg.msgType = MSG_SMS_GET_STATISTICS_REQ;
		break;
	default:
		Msg.msgType = MSG_SMS_GET_STATISTICS_REQ;
	}

	rc = smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
					 &client->stats_done);

	return rc;
}

static inline int led_feedback(struct smsdvb_client_t *client)
{
	if (!(client->fe_status & FE_HAS_LOCK))
		return sms_board_led_feedback(client->coredev, SMS_LED_OFF);

	return sms_board_led_feedback(client->coredev,
				     (client->legacy_ber == 0) ?
				     SMS_LED_HI : SMS_LED_LO);
}

static int smsdvb_read_status(struct dvb_frontend *fe, fe_status_t *stat)
{
	int rc;
	struct smsdvb_client_t *client;
	client = container_of(fe, struct smsdvb_client_t, frontend);

	rc = smsdvb_send_statistics_request(client);

	*stat = client->fe_status;

	led_feedback(client);

	return rc;
}

static int smsdvb_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	int rc;
	struct smsdvb_client_t *client;

	client = container_of(fe, struct smsdvb_client_t, frontend);

	rc = smsdvb_send_statistics_request(client);

	*ber = client->legacy_ber;

	led_feedback(client);

	return rc;
}

static int smsdvb_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int rc;
	s32 power = (s32) c->strength.stat[0].uvalue;
	struct smsdvb_client_t *client;

	client = container_of(fe, struct smsdvb_client_t, frontend);

	rc = smsdvb_send_statistics_request(client);

	if (power < -95)
		*strength = 0;
		else if (power > -29)
			*strength = 65535;
		else
			*strength = (power + 95) * 65535 / 66;

	led_feedback(client);

	return rc;
}

static int smsdvb_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int rc;
	struct smsdvb_client_t *client;

	client = container_of(fe, struct smsdvb_client_t, frontend);

	rc = smsdvb_send_statistics_request(client);

	/* Preferred scale for SNR with legacy API: 0.1 dB */
	*snr = c->cnr.stat[0].svalue / 100;

	led_feedback(client);

	return rc;
}

static int smsdvb_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	int rc;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct smsdvb_client_t *client;

	client = container_of(fe, struct smsdvb_client_t, frontend);

	rc = smsdvb_send_statistics_request(client);

	*ucblocks = c->block_error.stat[0].uvalue;

	led_feedback(client);

	return rc;
}

static int smsdvb_get_tune_settings(struct dvb_frontend *fe,
				    struct dvb_frontend_tune_settings *tune)
{
	sms_debug("");

	tune->min_delay_ms = 400;
	tune->step_size = 250000;
	tune->max_drift = 0;
	return 0;
}

static int smsdvb_dvbt_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);

	struct {
		struct SmsMsgHdr_ST	Msg;
		u32		Data[3];
	} Msg;

	int ret;

	client->fe_status = 0;
	client->event_fe_state = -1;
	client->event_unc_state = -1;
	fe->dtv_property_cache.delivery_system = SYS_DVBT;

	Msg.Msg.msgSrcId = DVBT_BDA_CONTROL_MSG_ID;
	Msg.Msg.msgDstId = HIF_TASK;
	Msg.Msg.msgFlags = 0;
	Msg.Msg.msgType = MSG_SMS_RF_TUNE_REQ;
	Msg.Msg.msgLength = sizeof(Msg);
	Msg.Data[0] = c->frequency;
	Msg.Data[2] = 12000000;

	sms_info("%s: freq %d band %d", __func__, c->frequency,
		 c->bandwidth_hz);

	switch (c->bandwidth_hz / 1000000) {
	case 8:
		Msg.Data[1] = BW_8_MHZ;
		break;
	case 7:
		Msg.Data[1] = BW_7_MHZ;
		break;
	case 6:
		Msg.Data[1] = BW_6_MHZ;
		break;
	case 0:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
	/* Disable LNA, if any. An error is returned if no LNA is present */
	ret = sms_board_lna_control(client->coredev, 0);
	if (ret == 0) {
		fe_status_t status;

		/* tune with LNA off at first */
		ret = smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
						  &client->tune_done);

		smsdvb_read_status(fe, &status);

		if (status & FE_HAS_LOCK)
			return ret;

		/* previous tune didn't lock - enable LNA and tune again */
		sms_board_lna_control(client->coredev, 1);
	}

	return smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
					   &client->tune_done);
}

static int smsdvb_isdbt_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	int board_id = smscore_get_board_id(client->coredev);
	struct sms_board *board = sms_get_board(board_id);
	enum sms_device_type_st type = board->type;
	int ret;
	struct {
		struct SmsMsgHdr_ST	Msg;
		u32		Data[4];
	} Msg;

	fe->dtv_property_cache.delivery_system = SYS_ISDBT;

	Msg.Msg.msgSrcId  = DVBT_BDA_CONTROL_MSG_ID;
	Msg.Msg.msgDstId  = HIF_TASK;
	Msg.Msg.msgFlags  = 0;
	Msg.Msg.msgType   = MSG_SMS_ISDBT_TUNE_REQ;
	Msg.Msg.msgLength = sizeof(Msg);

	if (c->isdbt_sb_segment_idx == -1)
		c->isdbt_sb_segment_idx = 0;

	if (!c->isdbt_layer_enabled)
		c->isdbt_layer_enabled = 7;

	Msg.Data[0] = c->frequency;
	Msg.Data[1] = BW_ISDBT_1SEG;
	Msg.Data[2] = 12000000;
	Msg.Data[3] = c->isdbt_sb_segment_idx;

	if (c->isdbt_partial_reception) {
		if ((type == SMS_PELE || type == SMS_RIO) &&
		    c->isdbt_sb_segment_count > 3)
			Msg.Data[1] = BW_ISDBT_13SEG;
		else if (c->isdbt_sb_segment_count > 1)
			Msg.Data[1] = BW_ISDBT_3SEG;
	} else if (type == SMS_PELE || type == SMS_RIO)
		Msg.Data[1] = BW_ISDBT_13SEG;

	c->bandwidth_hz = 6000000;

	sms_info("%s: freq %d segwidth %d segindex %d\n", __func__,
		 c->frequency, c->isdbt_sb_segment_count,
		 c->isdbt_sb_segment_idx);

	/* Disable LNA, if any. An error is returned if no LNA is present */
	ret = sms_board_lna_control(client->coredev, 0);
	if (ret == 0) {
		fe_status_t status;

		/* tune with LNA off at first */
		ret = smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
						  &client->tune_done);

		smsdvb_read_status(fe, &status);

		if (status & FE_HAS_LOCK)
			return ret;

		/* previous tune didn't lock - enable LNA and tune again */
		sms_board_lna_control(client->coredev, 1);
	}
	return smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
					   &client->tune_done);
}

static int smsdvb_set_frontend(struct dvb_frontend *fe)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	struct smscore_device_t *coredev = client->coredev;

	smsdvb_stats_not_ready(fe);

	switch (smscore_get_device_mode(coredev)) {
	case DEVICE_MODE_DVBT:
	case DEVICE_MODE_DVBT_BDA:
		return smsdvb_dvbt_set_frontend(fe);
	case DEVICE_MODE_ISDBT:
	case DEVICE_MODE_ISDBT_BDA:
		return smsdvb_isdbt_set_frontend(fe);
	default:
		return -EINVAL;
	}
}

/* Nothing to do here, as stats are automatically updated */
static int smsdvb_get_frontend(struct dvb_frontend *fe)
{
	return 0;
}

static int smsdvb_init(struct dvb_frontend *fe)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);

	sms_board_power(client->coredev, 1);

	sms_board_dvb3_event(client, DVB3_EVENT_INIT);
	return 0;
}

static int smsdvb_sleep(struct dvb_frontend *fe)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);

	sms_board_led_feedback(client->coredev, SMS_LED_OFF);
	sms_board_power(client->coredev, 0);

	sms_board_dvb3_event(client, DVB3_EVENT_SLEEP);

	return 0;
}

static void smsdvb_release(struct dvb_frontend *fe)
{
	/* do nothing */
}

static struct dvb_frontend_ops smsdvb_fe_ops = {
	.info = {
		.name			= "Siano Mobile Digital MDTV Receiver",
		.frequency_min		= 44250000,
		.frequency_max		= 867250000,
		.frequency_stepsize	= 250000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_HIERARCHY_AUTO,
	},

	.release = smsdvb_release,

	.set_frontend = smsdvb_set_frontend,
	.get_frontend = smsdvb_get_frontend,
	.get_tune_settings = smsdvb_get_tune_settings,

	.read_status = smsdvb_read_status,
	.read_ber = smsdvb_read_ber,
	.read_signal_strength = smsdvb_read_signal_strength,
	.read_snr = smsdvb_read_snr,
	.read_ucblocks = smsdvb_read_ucblocks,

	.init = smsdvb_init,
	.sleep = smsdvb_sleep,
};

static int smsdvb_hotplug(struct smscore_device_t *coredev,
			  struct device *device, int arrival)
{
	struct smsclient_params_t params;
	struct smsdvb_client_t *client;
	int rc;

	/* device removal handled by onremove callback */
	if (!arrival)
		return 0;
	client = kzalloc(sizeof(struct smsdvb_client_t), GFP_KERNEL);
	if (!client) {
		sms_err("kmalloc() failed");
		return -ENOMEM;
	}

	/* register dvb adapter */
	rc = dvb_register_adapter(&client->adapter,
				  sms_get_board(
					smscore_get_board_id(coredev))->name,
				  THIS_MODULE, device, adapter_nr);
	if (rc < 0) {
		sms_err("dvb_register_adapter() failed %d", rc);
		goto adapter_error;
	}

	/* init dvb demux */
	client->demux.dmx.capabilities = DMX_TS_FILTERING;
	client->demux.filternum = 32; /* todo: nova ??? */
	client->demux.feednum = 32;
	client->demux.start_feed = smsdvb_start_feed;
	client->demux.stop_feed = smsdvb_stop_feed;

	rc = dvb_dmx_init(&client->demux);
	if (rc < 0) {
		sms_err("dvb_dmx_init failed %d", rc);
		goto dvbdmx_error;
	}

	/* init dmxdev */
	client->dmxdev.filternum = 32;
	client->dmxdev.demux = &client->demux.dmx;
	client->dmxdev.capabilities = 0;

	rc = dvb_dmxdev_init(&client->dmxdev, &client->adapter);
	if (rc < 0) {
		sms_err("dvb_dmxdev_init failed %d", rc);
		goto dmxdev_error;
	}

	/* init and register frontend */
	memcpy(&client->frontend.ops, &smsdvb_fe_ops,
	       sizeof(struct dvb_frontend_ops));

	switch (smscore_get_device_mode(coredev)) {
	case DEVICE_MODE_DVBT:
	case DEVICE_MODE_DVBT_BDA:
		client->frontend.ops.delsys[0] = SYS_DVBT;
		break;
	case DEVICE_MODE_ISDBT:
	case DEVICE_MODE_ISDBT_BDA:
		client->frontend.ops.delsys[0] = SYS_ISDBT;
		break;
	}

	rc = dvb_register_frontend(&client->adapter, &client->frontend);
	if (rc < 0) {
		sms_err("frontend registration failed %d", rc);
		goto frontend_error;
	}

	params.initial_id = 1;
	params.data_type = MSG_SMS_DVBT_BDA_DATA;
	params.onresponse_handler = smsdvb_onresponse;
	params.onremove_handler = smsdvb_onremove;
	params.context = client;

	rc = smscore_register_client(coredev, &params, &client->smsclient);
	if (rc < 0) {
		sms_err("smscore_register_client() failed %d", rc);
		goto client_error;
	}

	client->coredev = coredev;

	init_completion(&client->tune_done);
	init_completion(&client->stats_done);

	kmutex_lock(&g_smsdvb_clientslock);

	list_add(&client->entry, &g_smsdvb_clients);

	kmutex_unlock(&g_smsdvb_clientslock);

	client->event_fe_state = -1;
	client->event_unc_state = -1;
	sms_board_dvb3_event(client, DVB3_EVENT_HOTPLUG);

	sms_info("success");
	sms_board_setup(coredev);

	if (create_stats_debugfs(client) < 0)
		sms_info("failed to create debugfs node");

	return 0;

client_error:
	dvb_unregister_frontend(&client->frontend);

frontend_error:
	dvb_dmxdev_release(&client->dmxdev);

dmxdev_error:
	dvb_dmx_release(&client->demux);

dvbdmx_error:
	dvb_unregister_adapter(&client->adapter);

adapter_error:
	kfree(client);
	return rc;
}

static int __init smsdvb_module_init(void)
{
	int rc;
	struct dentry *d;

	INIT_LIST_HEAD(&g_smsdvb_clients);
	kmutex_init(&g_smsdvb_clientslock);

	d = debugfs_create_dir("smsdvb", usb_debug_root);
	if (IS_ERR_OR_NULL(d))
		sms_err("Couldn't create sysfs node for smsdvb");
	else
		smsdvb_debugfs = d;

	rc = smscore_register_hotplug(smsdvb_hotplug);

	sms_debug("");

	return rc;
}

static void __exit smsdvb_module_exit(void)
{
	smscore_unregister_hotplug(smsdvb_hotplug);

	kmutex_lock(&g_smsdvb_clientslock);

	while (!list_empty(&g_smsdvb_clients))
	       smsdvb_unregister_client(
			(struct smsdvb_client_t *) g_smsdvb_clients.next);

	if (smsdvb_debugfs)
		debugfs_remove_recursive(smsdvb_debugfs);

	kmutex_unlock(&g_smsdvb_clientslock);
}

module_init(smsdvb_module_init);
module_exit(smsdvb_module_exit);

MODULE_DESCRIPTION("SMS DVB subsystem adaptation module");
MODULE_AUTHOR("Siano Mobile Silicon, Inc. (uris@siano-ms.com)");
MODULE_LICENSE("GPL");
