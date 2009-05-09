/*
 *  Driver for the Siano SMS1xxx USB dongle
 *
 *  Author: Uri Shkolni
 *
 *  Copyright (c), 2005-2008 Siano Mobile Silicon, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation;
 *
 *  Software distributed under the License is distributed on an "AS IS"
 *  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>

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
	int                     fe_ber, fe_snr, fe_unc, fe_signal_strength;

	struct completion       tune_done, stat_done;

	/* todo: save freq/band instead whole struct */
	struct dvb_frontend_parameters fe_params;
};

static struct list_head g_smsdvb_clients;
static struct mutex g_smsdvb_clientslock;

static int sms_dbg;
module_param_named(debug, sms_dbg, int, 0644);
MODULE_PARM_DESC(debug, "set debug level (info=1, adv=2 (or-able))");

static int smsdvb_onresponse(void *context, struct smscore_buffer_t *cb)
{
	struct smsdvb_client_t *client = (struct smsdvb_client_t *) context;
	struct SmsMsgHdr_ST *phdr =
		(struct SmsMsgHdr_ST *)(((u8 *) cb->p) + cb->offset);

	switch (phdr->msgType) {
	case MSG_SMS_DVBT_BDA_DATA:
		dvb_dmx_swfilter(&client->demux, (u8 *)(phdr + 1),
				 cb->size - sizeof(struct SmsMsgHdr_ST));
		break;

	case MSG_SMS_RF_TUNE_RES:
		complete(&client->tune_done);
		break;

	case MSG_SMS_GET_STATISTICS_RES:
	{
		struct SmsMsgStatisticsInfo_ST *p =
			(struct SmsMsgStatisticsInfo_ST *)(phdr + 1);

		if (p->Stat.IsDemodLocked) {
			client->fe_status = FE_HAS_SIGNAL |
					    FE_HAS_CARRIER |
					    FE_HAS_VITERBI |
					    FE_HAS_SYNC |
					    FE_HAS_LOCK;

			client->fe_snr = p->Stat.SNR;
			client->fe_ber = p->Stat.BER;
			client->fe_unc = p->Stat.BERErrorCount;

			if (p->Stat.InBandPwr < -95)
				client->fe_signal_strength = 0;
			else if (p->Stat.InBandPwr > -29)
				client->fe_signal_strength = 100;
			else
				client->fe_signal_strength =
					(p->Stat.InBandPwr + 95) * 3 / 2;
		} else {
			client->fe_status = 0;
			client->fe_snr =
			client->fe_ber =
			client->fe_unc =
			client->fe_signal_strength = 0;
		}

		complete(&client->stat_done);
		break;
	} }

	smscore_putbuffer(client->coredev, cb);

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
	int rc = smsclient_sendrequest(client->smsclient, buffer, size);
	if (rc < 0)
		return rc;

	return wait_for_completion_timeout(completion,
					   msecs_to_jiffies(2000)) ?
						0 : -ETIME;
}

static int smsdvb_send_statistics_request(struct smsdvb_client_t *client)
{
	struct SmsMsgHdr_ST Msg = { MSG_SMS_GET_STATISTICS_REQ,
			     DVBT_BDA_CONTROL_MSG_ID,
			     HIF_TASK, sizeof(struct SmsMsgHdr_ST), 0 };
	int ret = smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
					      &client->stat_done);
	if (ret < 0)
		return ret;

	if (client->fe_status & FE_HAS_LOCK)
		sms_board_led_feedback(client->coredev,
				       (client->fe_unc == 0) ?
				       SMS_LED_HI : SMS_LED_LO);
	else
		sms_board_led_feedback(client->coredev, SMS_LED_OFF);
	return ret;
}

static int smsdvb_read_status(struct dvb_frontend *fe, fe_status_t *stat)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	int rc = smsdvb_send_statistics_request(client);

	if (!rc)
		*stat = client->fe_status;

	return rc;
}

static int smsdvb_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	int rc = smsdvb_send_statistics_request(client);

	if (!rc)
		*ber = client->fe_ber;

	return rc;
}

static int smsdvb_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	int rc = smsdvb_send_statistics_request(client);

	if (!rc)
		*strength = client->fe_signal_strength;

	return rc;
}

static int smsdvb_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	int rc = smsdvb_send_statistics_request(client);

	if (!rc)
		*snr = client->fe_snr;

	return rc;
}

static int smsdvb_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	int rc = smsdvb_send_statistics_request(client);

	if (!rc)
		*ucblocks = client->fe_unc;

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

static int smsdvb_set_frontend(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *fep)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);

	struct {
		struct SmsMsgHdr_ST	Msg;
		u32		Data[3];
	} Msg;
	int ret;

	Msg.Msg.msgSrcId  = DVBT_BDA_CONTROL_MSG_ID;
	Msg.Msg.msgDstId  = HIF_TASK;
	Msg.Msg.msgFlags  = 0;
	Msg.Msg.msgType   = MSG_SMS_RF_TUNE_REQ;
	Msg.Msg.msgLength = sizeof(Msg);
	Msg.Data[0] = fep->frequency;
	Msg.Data[2] = 12000000;

	sms_debug("freq %d band %d",
		  fep->frequency, fep->u.ofdm.bandwidth);

	switch (fep->u.ofdm.bandwidth) {
	case BANDWIDTH_8_MHZ: Msg.Data[1] = BW_8_MHZ; break;
	case BANDWIDTH_7_MHZ: Msg.Data[1] = BW_7_MHZ; break;
	case BANDWIDTH_6_MHZ: Msg.Data[1] = BW_6_MHZ; break;
	case BANDWIDTH_AUTO: return -EOPNOTSUPP;
	default: return -EINVAL;
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

		/* previous tune didnt lock - enable LNA and tune again */
		sms_board_lna_control(client->coredev, 1);
	}

	return smsdvb_sendrequest_and_wait(client, &Msg, sizeof(Msg),
					   &client->tune_done);
}

static int smsdvb_get_frontend(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *fep)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);

	sms_debug("");

	/* todo: */
	memcpy(fep, &client->fe_params,
	       sizeof(struct dvb_frontend_parameters));

	return 0;
}

static int smsdvb_init(struct dvb_frontend *fe)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);

	sms_board_power(client->coredev, 1);

	return 0;
}

static int smsdvb_sleep(struct dvb_frontend *fe)
{
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);

	sms_board_led_feedback(client->coredev, SMS_LED_OFF);
	sms_board_power(client->coredev, 0);

	return 0;
}

static void smsdvb_release(struct dvb_frontend *fe)
{
	/* do nothing */
}

static struct dvb_frontend_ops smsdvb_fe_ops = {
	.info = {
		.name			= "Siano Mobile Digital MDTV Receiver",
		.type			= FE_OFDM,
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

	if (smscore_get_device_mode(coredev) != DEVICE_MODE_DVBT_BDA) {
		sms_err("SMS Device mode is not set for "
			"DVB operation.");
		return 0;
	}

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
	init_completion(&client->stat_done);

	kmutex_lock(&g_smsdvb_clientslock);

	list_add(&client->entry, &g_smsdvb_clients);

	kmutex_unlock(&g_smsdvb_clientslock);

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

int smsdvb_module_init(void)
{
	int rc;

	INIT_LIST_HEAD(&g_smsdvb_clients);
	kmutex_init(&g_smsdvb_clientslock);

	rc = smscore_register_hotplug(smsdvb_hotplug);

	sms_debug("");

	return rc;
}

void smsdvb_module_exit(void)
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
MODULE_AUTHOR("Siano Mobile Silicon, INC. (uris@siano-ms.com)");
MODULE_LICENSE("GPL");
