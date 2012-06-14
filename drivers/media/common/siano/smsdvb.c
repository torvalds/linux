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

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"

#include "smscoreapi.h"
#include "smsendian.h"
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

	struct SMSHOSTLIB_STATISTICS_DVB_S sms_stat_dvb;
	int event_fe_state;
	int event_unc_state;
};

static struct list_head g_smsdvb_clients;
static struct mutex g_smsdvb_clientslock;

static int sms_dbg;
module_param_named(debug, sms_dbg, int, 0644);
MODULE_PARM_DESC(debug, "set debug level (info=1, adv=2 (or-able))");

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


static void smsdvb_update_dvb_stats(struct RECEPTION_STATISTICS_S *pReceptionData,
				   struct SMSHOSTLIB_STATISTICS_ST *p)
{
	if (sms_dbg & 2) {
		printk(KERN_DEBUG "Reserved = %d", p->Reserved);
		printk(KERN_DEBUG "IsRfLocked = %d", p->IsRfLocked);
		printk(KERN_DEBUG "IsDemodLocked = %d", p->IsDemodLocked);
		printk(KERN_DEBUG "IsExternalLNAOn = %d", p->IsExternalLNAOn);
		printk(KERN_DEBUG "SNR = %d", p->SNR);
		printk(KERN_DEBUG "BER = %d", p->BER);
		printk(KERN_DEBUG "FIB_CRC = %d", p->FIB_CRC);
		printk(KERN_DEBUG "TS_PER = %d", p->TS_PER);
		printk(KERN_DEBUG "MFER = %d", p->MFER);
		printk(KERN_DEBUG "RSSI = %d", p->RSSI);
		printk(KERN_DEBUG "InBandPwr = %d", p->InBandPwr);
		printk(KERN_DEBUG "CarrierOffset = %d", p->CarrierOffset);
		printk(KERN_DEBUG "Frequency = %d", p->Frequency);
		printk(KERN_DEBUG "Bandwidth = %d", p->Bandwidth);
		printk(KERN_DEBUG "TransmissionMode = %d", p->TransmissionMode);
		printk(KERN_DEBUG "ModemState = %d", p->ModemState);
		printk(KERN_DEBUG "GuardInterval = %d", p->GuardInterval);
		printk(KERN_DEBUG "CodeRate = %d", p->CodeRate);
		printk(KERN_DEBUG "LPCodeRate = %d", p->LPCodeRate);
		printk(KERN_DEBUG "Hierarchy = %d", p->Hierarchy);
		printk(KERN_DEBUG "Constellation = %d", p->Constellation);
		printk(KERN_DEBUG "BurstSize = %d", p->BurstSize);
		printk(KERN_DEBUG "BurstDuration = %d", p->BurstDuration);
		printk(KERN_DEBUG "BurstCycleTime = %d", p->BurstCycleTime);
		printk(KERN_DEBUG "CalculatedBurstCycleTime = %d", p->CalculatedBurstCycleTime);
		printk(KERN_DEBUG "NumOfRows = %d", p->NumOfRows);
		printk(KERN_DEBUG "NumOfPaddCols = %d", p->NumOfPaddCols);
		printk(KERN_DEBUG "NumOfPunctCols = %d", p->NumOfPunctCols);
		printk(KERN_DEBUG "ErrorTSPackets = %d", p->ErrorTSPackets);
		printk(KERN_DEBUG "TotalTSPackets = %d", p->TotalTSPackets);
		printk(KERN_DEBUG "NumOfValidMpeTlbs = %d", p->NumOfValidMpeTlbs);
		printk(KERN_DEBUG "NumOfInvalidMpeTlbs = %d", p->NumOfInvalidMpeTlbs);
		printk(KERN_DEBUG "NumOfCorrectedMpeTlbs = %d", p->NumOfCorrectedMpeTlbs);
		printk(KERN_DEBUG "BERErrorCount = %d", p->BERErrorCount);
		printk(KERN_DEBUG "BERBitCount = %d", p->BERBitCount);
		printk(KERN_DEBUG "SmsToHostTxErrors = %d", p->SmsToHostTxErrors);
		printk(KERN_DEBUG "PreBER = %d", p->PreBER);
		printk(KERN_DEBUG "CellId = %d", p->CellId);
		printk(KERN_DEBUG "DvbhSrvIndHP = %d", p->DvbhSrvIndHP);
		printk(KERN_DEBUG "DvbhSrvIndLP = %d", p->DvbhSrvIndLP);
		printk(KERN_DEBUG "NumMPEReceived = %d", p->NumMPEReceived);
	}

	pReceptionData->IsDemodLocked = p->IsDemodLocked;

	pReceptionData->SNR = p->SNR;
	pReceptionData->BER = p->BER;
	pReceptionData->BERErrorCount = p->BERErrorCount;
	pReceptionData->InBandPwr = p->InBandPwr;
	pReceptionData->ErrorTSPackets = p->ErrorTSPackets;
};


static void smsdvb_update_isdbt_stats(struct RECEPTION_STATISTICS_S *pReceptionData,
				    struct SMSHOSTLIB_STATISTICS_ISDBT_ST *p)
{
	int i;

	if (sms_dbg & 2) {
		printk(KERN_DEBUG "IsRfLocked = %d", p->IsRfLocked);
		printk(KERN_DEBUG "IsDemodLocked = %d", p->IsDemodLocked);
		printk(KERN_DEBUG "IsExternalLNAOn = %d", p->IsExternalLNAOn);
		printk(KERN_DEBUG "SNR = %d", p->SNR);
		printk(KERN_DEBUG "RSSI = %d", p->RSSI);
		printk(KERN_DEBUG "InBandPwr = %d", p->InBandPwr);
		printk(KERN_DEBUG "CarrierOffset = %d", p->CarrierOffset);
		printk(KERN_DEBUG "Frequency = %d", p->Frequency);
		printk(KERN_DEBUG "Bandwidth = %d", p->Bandwidth);
		printk(KERN_DEBUG "TransmissionMode = %d", p->TransmissionMode);
		printk(KERN_DEBUG "ModemState = %d", p->ModemState);
		printk(KERN_DEBUG "GuardInterval = %d", p->GuardInterval);
		printk(KERN_DEBUG "SystemType = %d", p->SystemType);
		printk(KERN_DEBUG "PartialReception = %d", p->PartialReception);
		printk(KERN_DEBUG "NumOfLayers = %d", p->NumOfLayers);
		printk(KERN_DEBUG "SmsToHostTxErrors = %d", p->SmsToHostTxErrors);

		for (i = 0; i < 3; i++) {
			printk(KERN_DEBUG "%d: CodeRate = %d", i, p->LayerInfo[i].CodeRate);
			printk(KERN_DEBUG "%d: Constellation = %d", i, p->LayerInfo[i].Constellation);
			printk(KERN_DEBUG "%d: BER = %d", i, p->LayerInfo[i].BER);
			printk(KERN_DEBUG "%d: BERErrorCount = %d", i, p->LayerInfo[i].BERErrorCount);
			printk(KERN_DEBUG "%d: BERBitCount = %d", i, p->LayerInfo[i].BERBitCount);
			printk(KERN_DEBUG "%d: PreBER = %d", i, p->LayerInfo[i].PreBER);
			printk(KERN_DEBUG "%d: TS_PER = %d", i, p->LayerInfo[i].TS_PER);
			printk(KERN_DEBUG "%d: ErrorTSPackets = %d", i, p->LayerInfo[i].ErrorTSPackets);
			printk(KERN_DEBUG "%d: TotalTSPackets = %d", i, p->LayerInfo[i].TotalTSPackets);
			printk(KERN_DEBUG "%d: TILdepthI = %d", i, p->LayerInfo[i].TILdepthI);
			printk(KERN_DEBUG "%d: NumberOfSegments = %d", i, p->LayerInfo[i].NumberOfSegments);
			printk(KERN_DEBUG "%d: TMCCErrors = %d", i, p->LayerInfo[i].TMCCErrors);
		}
	}

	pReceptionData->IsDemodLocked = p->IsDemodLocked;

	pReceptionData->SNR = p->SNR;
	pReceptionData->InBandPwr = p->InBandPwr;

	pReceptionData->ErrorTSPackets = 0;
	pReceptionData->BER = 0;
	pReceptionData->BERErrorCount = 0;
	for (i = 0; i < 3; i++) {
		pReceptionData->BER += p->LayerInfo[i].BER;
		pReceptionData->BERErrorCount += p->LayerInfo[i].BERErrorCount;
		pReceptionData->ErrorTSPackets += p->LayerInfo[i].ErrorTSPackets;
	}
}

static int smsdvb_onresponse(void *context, struct smscore_buffer_t *cb)
{
	struct smsdvb_client_t *client = (struct smsdvb_client_t *) context;
	struct SmsMsgHdr_ST *phdr = (struct SmsMsgHdr_ST *) (((u8 *) cb->p)
			+ cb->offset);
	u32 *pMsgData = (u32 *) phdr + 1;
	/*u32 MsgDataLen = phdr->msgLength - sizeof(struct SmsMsgHdr_ST);*/
	bool is_status_update = false;

	smsendian_handle_rx_message((struct SmsMsgData_ST *) phdr);

	switch (phdr->msgType) {
	case MSG_SMS_DVBT_BDA_DATA:
		dvb_dmx_swfilter(&client->demux, (u8 *)(phdr + 1),
				 cb->size - sizeof(struct SmsMsgHdr_ST));
		break;

	case MSG_SMS_RF_TUNE_RES:
	case MSG_SMS_ISDBT_TUNE_RES:
		complete(&client->tune_done);
		break;

	case MSG_SMS_SIGNAL_DETECTED_IND:
		sms_info("MSG_SMS_SIGNAL_DETECTED_IND");
		client->sms_stat_dvb.TransmissionData.IsDemodLocked = true;
		is_status_update = true;
		break;

	case MSG_SMS_NO_SIGNAL_IND:
		sms_info("MSG_SMS_NO_SIGNAL_IND");
		client->sms_stat_dvb.TransmissionData.IsDemodLocked = false;
		is_status_update = true;
		break;

	case MSG_SMS_TRANSMISSION_IND: {
		sms_info("MSG_SMS_TRANSMISSION_IND");

		pMsgData++;
		memcpy(&client->sms_stat_dvb.TransmissionData, pMsgData,
				sizeof(struct TRANSMISSION_STATISTICS_S));

		/* Mo need to correct guard interval
		 * (as opposed to old statistics message).
		 */
		CORRECT_STAT_BANDWIDTH(client->sms_stat_dvb.TransmissionData);
		CORRECT_STAT_TRANSMISSON_MODE(
				client->sms_stat_dvb.TransmissionData);
		is_status_update = true;
		break;
	}
	case MSG_SMS_HO_PER_SLICES_IND: {
		struct RECEPTION_STATISTICS_S *pReceptionData =
				&client->sms_stat_dvb.ReceptionData;
		struct SRVM_SIGNAL_STATUS_S SignalStatusData;

		/*sms_info("MSG_SMS_HO_PER_SLICES_IND");*/
		pMsgData++;
		SignalStatusData.result = pMsgData[0];
		SignalStatusData.snr = pMsgData[1];
		SignalStatusData.inBandPower = (s32) pMsgData[2];
		SignalStatusData.tsPackets = pMsgData[3];
		SignalStatusData.etsPackets = pMsgData[4];
		SignalStatusData.constellation = pMsgData[5];
		SignalStatusData.hpCode = pMsgData[6];
		SignalStatusData.tpsSrvIndLP = pMsgData[7] & 0x03;
		SignalStatusData.tpsSrvIndHP = pMsgData[8] & 0x03;
		SignalStatusData.cellId = pMsgData[9] & 0xFFFF;
		SignalStatusData.reason = pMsgData[10];
		SignalStatusData.requestId = pMsgData[11];
		pReceptionData->IsRfLocked = pMsgData[16];
		pReceptionData->IsDemodLocked = pMsgData[17];
		pReceptionData->ModemState = pMsgData[12];
		pReceptionData->SNR = pMsgData[1];
		pReceptionData->BER = pMsgData[13];
		pReceptionData->RSSI = pMsgData[14];
		CORRECT_STAT_RSSI(client->sms_stat_dvb.ReceptionData);

		pReceptionData->InBandPwr = (s32) pMsgData[2];
		pReceptionData->CarrierOffset = (s32) pMsgData[15];
		pReceptionData->TotalTSPackets = pMsgData[3];
		pReceptionData->ErrorTSPackets = pMsgData[4];

		/* TS PER */
		if ((SignalStatusData.tsPackets + SignalStatusData.etsPackets)
				> 0) {
			pReceptionData->TS_PER = (SignalStatusData.etsPackets
					* 100) / (SignalStatusData.tsPackets
					+ SignalStatusData.etsPackets);
		} else {
			pReceptionData->TS_PER = 0;
		}

		pReceptionData->BERBitCount = pMsgData[18];
		pReceptionData->BERErrorCount = pMsgData[19];

		pReceptionData->MRC_SNR = pMsgData[20];
		pReceptionData->MRC_InBandPwr = pMsgData[21];
		pReceptionData->MRC_RSSI = pMsgData[22];

		is_status_update = true;
		break;
	}
	case MSG_SMS_GET_STATISTICS_RES: {
		union {
			struct SMSHOSTLIB_STATISTICS_ISDBT_ST  isdbt;
			struct SmsMsgStatisticsInfo_ST         dvb;
		} *p = (void *) (phdr + 1);
		struct RECEPTION_STATISTICS_S *pReceptionData =
				&client->sms_stat_dvb.ReceptionData;

		sms_info("MSG_SMS_GET_STATISTICS_RES");

		is_status_update = true;

		switch (smscore_get_device_mode(client->coredev)) {
		case DEVICE_MODE_ISDBT:
		case DEVICE_MODE_ISDBT_BDA:
			smsdvb_update_isdbt_stats(pReceptionData, &p->isdbt);
			break;
		default:
			smsdvb_update_dvb_stats(pReceptionData, &p->dvb.Stat);
		}
		if (!pReceptionData->IsDemodLocked) {
			pReceptionData->SNR = 0;
			pReceptionData->BER = 0;
			pReceptionData->BERErrorCount = 0;
			pReceptionData->InBandPwr = 0;
			pReceptionData->ErrorTSPackets = 0;
		}

		complete(&client->tune_done);
		break;
	}
	default:
		sms_info("Unhandled message %d", phdr->msgType);

	}
	smscore_putbuffer(client->coredev, cb);

	if (is_status_update) {
		if (client->sms_stat_dvb.ReceptionData.IsDemodLocked) {
			client->fe_status = FE_HAS_SIGNAL | FE_HAS_CARRIER
				| FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
			sms_board_dvb3_event(client, DVB3_EVENT_FE_LOCK);
			if (client->sms_stat_dvb.ReceptionData.ErrorTSPackets
					== 0)
				sms_board_dvb3_event(client, DVB3_EVENT_UNC_OK);
			else
				sms_board_dvb3_event(client,
						DVB3_EVENT_UNC_ERR);

		} else {
			if (client->sms_stat_dvb.ReceptionData.IsRfLocked)
				client->fe_status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
			else
				client->fe_status = 0;
			sms_board_dvb3_event(client, DVB3_EVENT_FE_UNLOCK);
		}
	}

	return 0;
}

static void smsdvb_unregister_client(struct smsdvb_client_t *client)
{
	/* must be called under clientslock */

	list_del(&client->entry);

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

	smsendian_handle_tx_message((struct SmsMsgHdr_ST *)&PidMsg);
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

	smsendian_handle_tx_message((struct SmsMsgHdr_ST *)&PidMsg);
	return smsclient_sendrequest(client->smsclient,
				     &PidMsg, sizeof(PidMsg));
}

static int smsdvb_sendrequest_and_wait(struct smsdvb_client_t *client,
					void *buffer, size_t size,
					struct completion *completion)
{
	int rc;

	smsendian_handle_tx_message((struct SmsMsgHdr_ST *)buffer);
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
	struct SmsMsgHdr_ST Msg = { MSG_SMS_GET_STATISTICS_REQ,
				    DVBT_BDA_CONTROL_MSG_ID,
				    HIF_TASK,
				    sizeof(struct SmsMsgHdr_ST), 0 };

	rc = smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
					  &client->tune_done);

	return rc;
}

static inline int led_feedback(struct smsdvb_client_t *client)
{
	if (client->fe_status & FE_HAS_LOCK)
		return sms_board_led_feedback(client->coredev,
			(client->sms_stat_dvb.ReceptionData.BER
			== 0) ? SMS_LED_HI : SMS_LED_LO);
	else
		return sms_board_led_feedback(client->coredev, SMS_LED_OFF);
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

	*ber = client->sms_stat_dvb.ReceptionData.BER;

	led_feedback(client);

	return rc;
}

static int smsdvb_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	int rc;

	struct smsdvb_client_t *client;
	client = container_of(fe, struct smsdvb_client_t, frontend);

	rc = smsdvb_send_statistics_request(client);

	if (client->sms_stat_dvb.ReceptionData.InBandPwr < -95)
		*strength = 0;
		else if (client->sms_stat_dvb.ReceptionData.InBandPwr > -29)
			*strength = 100;
		else
			*strength =
				(client->sms_stat_dvb.ReceptionData.InBandPwr
				+ 95) * 3 / 2;

	led_feedback(client);

	return rc;
}

static int smsdvb_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	int rc;
	struct smsdvb_client_t *client;
	client = container_of(fe, struct smsdvb_client_t, frontend);

	rc = smsdvb_send_statistics_request(client);

	*snr = client->sms_stat_dvb.ReceptionData.SNR;

	led_feedback(client);

	return rc;
}

static int smsdvb_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	int rc;
	struct smsdvb_client_t *client;
	client = container_of(fe, struct smsdvb_client_t, frontend);

	rc = smsdvb_send_statistics_request(client);

	*ucblocks = client->sms_stat_dvb.ReceptionData.ErrorTSPackets;

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

	client->fe_status = FE_HAS_SIGNAL;
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

	switch (c->isdbt_sb_segment_count) {
	case 3:
		Msg.Data[1] = BW_ISDBT_3SEG;
		break;
	case 1:
		Msg.Data[1] = BW_ISDBT_1SEG;
		break;
	case 0:	/* AUTO */
		switch (c->bandwidth_hz / 1000000) {
		case 8:
		case 7:
			c->isdbt_sb_segment_count = 3;
			Msg.Data[1] = BW_ISDBT_3SEG;
			break;
		case 6:
			c->isdbt_sb_segment_count = 1;
			Msg.Data[1] = BW_ISDBT_1SEG;
			break;
		default: /* Assumes 6 MHZ bw */
			c->isdbt_sb_segment_count = 1;
			c->bandwidth_hz = 6000;
			Msg.Data[1] = BW_ISDBT_1SEG;
			break;
		}
		break;
	default:
		sms_info("Segment count %d not supported", c->isdbt_sb_segment_count);
		return -EINVAL;
	}

	Msg.Data[0] = c->frequency;
	Msg.Data[2] = 12000000;
	Msg.Data[3] = c->isdbt_sb_segment_idx;

	sms_info("%s: freq %d segwidth %d segindex %d\n", __func__,
		 c->frequency, c->isdbt_sb_segment_count,
		 c->isdbt_sb_segment_idx);

	return smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
					   &client->tune_done);
}

static int smsdvb_set_frontend(struct dvb_frontend *fe)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	struct smscore_device_t *coredev = client->coredev;

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

static int smsdvb_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *fep = &fe->dtv_property_cache;
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	struct smscore_device_t *coredev = client->coredev;
	struct TRANSMISSION_STATISTICS_S *td =
		&client->sms_stat_dvb.TransmissionData;

	switch (smscore_get_device_mode(coredev)) {
	case DEVICE_MODE_DVBT:
	case DEVICE_MODE_DVBT_BDA:
		fep->frequency = td->Frequency;

		switch (td->Bandwidth) {
		case 6:
			fep->bandwidth_hz = 6000000;
			break;
		case 7:
			fep->bandwidth_hz = 7000000;
			break;
		case 8:
			fep->bandwidth_hz = 8000000;
			break;
		}

		switch (td->TransmissionMode) {
		case 2:
			fep->transmission_mode = TRANSMISSION_MODE_2K;
			break;
		case 8:
			fep->transmission_mode = TRANSMISSION_MODE_8K;
		}

		switch (td->GuardInterval) {
		case 0:
			fep->guard_interval = GUARD_INTERVAL_1_32;
			break;
		case 1:
			fep->guard_interval = GUARD_INTERVAL_1_16;
			break;
		case 2:
			fep->guard_interval = GUARD_INTERVAL_1_8;
			break;
		case 3:
			fep->guard_interval = GUARD_INTERVAL_1_4;
			break;
		}

		switch (td->CodeRate) {
		case 0:
			fep->code_rate_HP = FEC_1_2;
			break;
		case 1:
			fep->code_rate_HP = FEC_2_3;
			break;
		case 2:
			fep->code_rate_HP = FEC_3_4;
			break;
		case 3:
			fep->code_rate_HP = FEC_5_6;
			break;
		case 4:
			fep->code_rate_HP = FEC_7_8;
			break;
		}

		switch (td->LPCodeRate) {
		case 0:
			fep->code_rate_LP = FEC_1_2;
			break;
		case 1:
			fep->code_rate_LP = FEC_2_3;
			break;
		case 2:
			fep->code_rate_LP = FEC_3_4;
			break;
		case 3:
			fep->code_rate_LP = FEC_5_6;
			break;
		case 4:
			fep->code_rate_LP = FEC_7_8;
			break;
		}

		switch (td->Constellation) {
		case 0:
			fep->modulation = QPSK;
			break;
		case 1:
			fep->modulation = QAM_16;
			break;
		case 2:
			fep->modulation = QAM_64;
			break;
		}

		switch (td->Hierarchy) {
		case 0:
			fep->hierarchy = HIERARCHY_NONE;
			break;
		case 1:
			fep->hierarchy = HIERARCHY_1;
			break;
		case 2:
			fep->hierarchy = HIERARCHY_2;
			break;
		case 3:
			fep->hierarchy = HIERARCHY_4;
			break;
		}

		fep->inversion = INVERSION_AUTO;
		break;
	case DEVICE_MODE_ISDBT:
	case DEVICE_MODE_ISDBT_BDA:
		fep->frequency = td->Frequency;
		fep->bandwidth_hz = 6000000;
		/* todo: retrive the other parameters */
		break;
	default:
		return -EINVAL;
	}

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

	kmutex_lock(&g_smsdvb_clientslock);

	list_add(&client->entry, &g_smsdvb_clients);

	kmutex_unlock(&g_smsdvb_clientslock);

	client->event_fe_state = -1;
	client->event_unc_state = -1;
	sms_board_dvb3_event(client, DVB3_EVENT_HOTPLUG);

	sms_info("success");
	sms_board_setup(coredev);

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

	INIT_LIST_HEAD(&g_smsdvb_clients);
	kmutex_init(&g_smsdvb_clientslock);

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

	kmutex_unlock(&g_smsdvb_clientslock);
}

module_init(smsdvb_module_init);
module_exit(smsdvb_module_exit);

MODULE_DESCRIPTION("SMS DVB subsystem adaptation module");
MODULE_AUTHOR("Siano Mobile Silicon, Inc. (uris@siano-ms.com)");
MODULE_LICENSE("GPL");
