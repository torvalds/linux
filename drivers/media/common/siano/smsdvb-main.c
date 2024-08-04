// SPDX-License-Identifier: GPL-2.0-or-later
/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2006-2008, Uri Shkolnik


****************************************************************/

#include "smscoreapi.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/div64.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>

#include "sms-cards.h"

#include "smsdvb.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static LIST_HEAD(g_smsdvb_clients);
static DEFINE_MUTEX(g_smsdvb_clientslock);

static u32 sms_to_guard_interval_table[] = {
	[0] = GUARD_INTERVAL_1_32,
	[1] = GUARD_INTERVAL_1_16,
	[2] = GUARD_INTERVAL_1_8,
	[3] = GUARD_INTERVAL_1_4,
};

static u32 sms_to_code_rate_table[] = {
	[0] = FEC_1_2,
	[1] = FEC_2_3,
	[2] = FEC_3_4,
	[3] = FEC_5_6,
	[4] = FEC_7_8,
};


static u32 sms_to_hierarchy_table[] = {
	[0] = HIERARCHY_NONE,
	[1] = HIERARCHY_1,
	[2] = HIERARCHY_2,
	[3] = HIERARCHY_4,
};

static u32 sms_to_modulation_table[] = {
	[0] = QPSK,
	[1] = QAM_16,
	[2] = QAM_64,
	[3] = DQPSK,
};


/* Events that may come from DVB v3 adapter */
static void sms_board_dvb3_event(struct smsdvb_client_t *client,
		enum SMS_DVB3_EVENTS event) {

	struct smscore_device_t *coredev = client->coredev;
	switch (event) {
	case DVB3_EVENT_INIT:
		pr_debug("DVB3_EVENT_INIT\n");
		sms_board_event(coredev, BOARD_EVENT_BIND);
		break;
	case DVB3_EVENT_SLEEP:
		pr_debug("DVB3_EVENT_SLEEP\n");
		sms_board_event(coredev, BOARD_EVENT_POWER_SUSPEND);
		break;
	case DVB3_EVENT_HOTPLUG:
		pr_debug("DVB3_EVENT_HOTPLUG\n");
		sms_board_event(coredev, BOARD_EVENT_POWER_INIT);
		break;
	case DVB3_EVENT_FE_LOCK:
		if (client->event_fe_state != DVB3_EVENT_FE_LOCK) {
			client->event_fe_state = DVB3_EVENT_FE_LOCK;
			pr_debug("DVB3_EVENT_FE_LOCK\n");
			sms_board_event(coredev, BOARD_EVENT_FE_LOCK);
		}
		break;
	case DVB3_EVENT_FE_UNLOCK:
		if (client->event_fe_state != DVB3_EVENT_FE_UNLOCK) {
			client->event_fe_state = DVB3_EVENT_FE_UNLOCK;
			pr_debug("DVB3_EVENT_FE_UNLOCK\n");
			sms_board_event(coredev, BOARD_EVENT_FE_UNLOCK);
		}
		break;
	case DVB3_EVENT_UNC_OK:
		if (client->event_unc_state != DVB3_EVENT_UNC_OK) {
			client->event_unc_state = DVB3_EVENT_UNC_OK;
			pr_debug("DVB3_EVENT_UNC_OK\n");
			sms_board_event(coredev, BOARD_EVENT_MULTIPLEX_OK);
		}
		break;
	case DVB3_EVENT_UNC_ERR:
		if (client->event_unc_state != DVB3_EVENT_UNC_ERR) {
			client->event_unc_state = DVB3_EVENT_UNC_ERR;
			pr_debug("DVB3_EVENT_UNC_ERR\n");
			sms_board_event(coredev, BOARD_EVENT_MULTIPLEX_ERRORS);
		}
		break;

	default:
		pr_err("Unknown dvb3 api event\n");
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
		break;
	default:
		n_layers = 1;
	}

	/* Global stats */
	c->strength.len = 1;
	c->cnr.len = 1;
	c->strength.stat[0].scale = FE_SCALE_DECIBEL;
	c->cnr.stat[0].scale = FE_SCALE_DECIBEL;

	/* Per-layer stats */
	c->post_bit_error.len = n_layers;
	c->post_bit_count.len = n_layers;
	c->block_error.len = n_layers;
	c->block_count.len = n_layers;

	/*
	 * Put all of them at FE_SCALE_NOT_AVAILABLE. They're dynamically
	 * changed when the stats become available.
	 */
	for (i = 0; i < n_layers; i++) {
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

static inline int sms_to_isdbt_mode(u32 mode)
{
	switch (mode) {
	case 1:
		return TRANSMISSION_MODE_2K;
	case 2:
		return TRANSMISSION_MODE_4K;
	case 3:
		return TRANSMISSION_MODE_8K;
	}
	return TRANSMISSION_MODE_AUTO;
}

static inline int sms_to_isdbt_guard_interval(u32 interval)
{
	switch (interval) {
	case 4:
		return GUARD_INTERVAL_1_4;
	case 8:
		return GUARD_INTERVAL_1_8;
	case 16:
		return GUARD_INTERVAL_1_16;
	case 32:
		return GUARD_INTERVAL_1_32;
	}
	return GUARD_INTERVAL_AUTO;
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

static inline u32 sms_to_bw(u32 value)
{
	return value * 1000000;
}

#define convert_from_table(value, table, defval) ({			\
	u32 __ret;							\
	if (value < ARRAY_SIZE(table))					\
		__ret = table[value];					\
	else								\
		__ret = defval;						\
	__ret;								\
})

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
				    struct sms_tx_stats *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	c->frequency = p->frequency;
	client->fe_status = sms_to_status(p->is_demod_locked, 0);
	c->bandwidth_hz = sms_to_bw(p->bandwidth);
	c->transmission_mode = sms_to_mode(p->transmission_mode);
	c->guard_interval = sms_to_guard_interval(p->guard_interval);
	c->code_rate_HP = sms_to_code_rate(p->code_rate);
	c->code_rate_LP = sms_to_code_rate(p->lp_code_rate);
	c->hierarchy = sms_to_hierarchy(p->hierarchy);
	c->modulation = sms_to_modulation(p->constellation);
}

static void smsdvb_update_per_slices(struct smsdvb_client_t *client,
				     struct RECEPTION_STATISTICS_PER_SLICES_S *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u64 tmp;

	client->fe_status = sms_to_status(p->is_demod_locked, p->is_rf_locked);
	c->modulation = sms_to_modulation(p->constellation);

	/* signal Strength, in DBm */
	c->strength.stat[0].uvalue = p->in_band_power * 1000;

	/* Carrier to noise ratio, in DB */
	c->cnr.stat[0].svalue = p->snr * 1000;

	/* PER/BER requires demod lock */
	if (!p->is_demod_locked)
		return;

	/* TS PER */
	client->last_per = c->block_error.stat[0].uvalue;
	c->block_error.stat[0].scale = FE_SCALE_COUNTER;
	c->block_count.stat[0].scale = FE_SCALE_COUNTER;
	c->block_error.stat[0].uvalue += p->ets_packets;
	c->block_count.stat[0].uvalue += p->ets_packets + p->ts_packets;

	/* ber */
	c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_error.stat[0].uvalue += p->ber_error_count;
	c->post_bit_count.stat[0].uvalue += p->ber_bit_count;

	/* Legacy PER/BER */
	tmp = p->ets_packets * 65535ULL;
	if (p->ts_packets + p->ets_packets)
		do_div(tmp, p->ts_packets + p->ets_packets);
	client->legacy_per = tmp;
}

static void smsdvb_update_dvb_stats(struct smsdvb_client_t *client,
				    struct sms_stats *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (client->prt_dvb_stats)
		client->prt_dvb_stats(client->debug_data, p);

	client->fe_status = sms_to_status(p->is_demod_locked, p->is_rf_locked);

	/* Update DVB modulation parameters */
	c->frequency = p->frequency;
	client->fe_status = sms_to_status(p->is_demod_locked, 0);
	c->bandwidth_hz = sms_to_bw(p->bandwidth);
	c->transmission_mode = sms_to_mode(p->transmission_mode);
	c->guard_interval = sms_to_guard_interval(p->guard_interval);
	c->code_rate_HP = sms_to_code_rate(p->code_rate);
	c->code_rate_LP = sms_to_code_rate(p->lp_code_rate);
	c->hierarchy = sms_to_hierarchy(p->hierarchy);
	c->modulation = sms_to_modulation(p->constellation);

	/* update reception data */
	c->lna = p->is_external_lna_on ? 1 : 0;

	/* Carrier to noise ratio, in DB */
	c->cnr.stat[0].svalue = p->SNR * 1000;

	/* signal Strength, in DBm */
	c->strength.stat[0].uvalue = p->in_band_pwr * 1000;

	/* PER/BER requires demod lock */
	if (!p->is_demod_locked)
		return;

	/* TS PER */
	client->last_per = c->block_error.stat[0].uvalue;
	c->block_error.stat[0].scale = FE_SCALE_COUNTER;
	c->block_count.stat[0].scale = FE_SCALE_COUNTER;
	c->block_error.stat[0].uvalue += p->error_ts_packets;
	c->block_count.stat[0].uvalue += p->total_ts_packets;

	/* ber */
	c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_error.stat[0].uvalue += p->ber_error_count;
	c->post_bit_count.stat[0].uvalue += p->ber_bit_count;

	/* Legacy PER/BER */
	client->legacy_ber = p->ber;
};

static void smsdvb_update_isdbt_stats(struct smsdvb_client_t *client,
				      struct sms_isdbt_stats *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct sms_isdbt_layer_stats *lr;
	int i, n_layers;

	if (client->prt_isdb_stats)
		client->prt_isdb_stats(client->debug_data, p);

	client->fe_status = sms_to_status(p->is_demod_locked, p->is_rf_locked);

	/*
	 * Firmware 2.1 seems to report only lock status and
	 * signal strength. The signal strength indicator is at the
	 * wrong field.
	 */
	if (p->statistics_type == 0) {
		c->strength.stat[0].uvalue = ((s32)p->transmission_mode) * 1000;
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}

	/* Update ISDB-T transmission parameters */
	c->frequency = p->frequency;
	c->bandwidth_hz = sms_to_bw(p->bandwidth);
	c->transmission_mode = sms_to_isdbt_mode(p->transmission_mode);
	c->guard_interval = sms_to_isdbt_guard_interval(p->guard_interval);
	c->isdbt_partial_reception = p->partial_reception ? 1 : 0;
	n_layers = p->num_of_layers;
	if (n_layers < 1)
		n_layers = 1;
	if (n_layers > 3)
		n_layers = 3;
	c->isdbt_layer_enabled = 0;

	/* update reception data */
	c->lna = p->is_external_lna_on ? 1 : 0;

	/* Carrier to noise ratio, in DB */
	c->cnr.stat[0].svalue = p->SNR * 1000;

	/* signal Strength, in DBm */
	c->strength.stat[0].uvalue = p->in_band_pwr * 1000;

	/* PER/BER and per-layer stats require demod lock */
	if (!p->is_demod_locked)
		return;

	client->last_per = c->block_error.stat[0].uvalue;

	/* Clears global counters, as the code below will sum it again */
	c->block_error.stat[0].uvalue = 0;
	c->block_count.stat[0].uvalue = 0;
	c->block_error.stat[0].scale = FE_SCALE_COUNTER;
	c->block_count.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_error.stat[0].uvalue = 0;
	c->post_bit_count.stat[0].uvalue = 0;
	c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;

	for (i = 0; i < n_layers; i++) {
		lr = &p->layer_info[i];

		/* Update per-layer transmission parameters */
		if (lr->number_of_segments > 0 && lr->number_of_segments < 13) {
			c->isdbt_layer_enabled |= 1 << i;
			c->layer[i].segment_count = lr->number_of_segments;
		} else {
			continue;
		}
		c->layer[i].modulation = sms_to_modulation(lr->constellation);
		c->layer[i].fec = sms_to_code_rate(lr->code_rate);

		/* Time interleaving */
		c->layer[i].interleaving = (u8)lr->ti_ldepth_i;

		/* TS PER */
		c->block_error.stat[i + 1].scale = FE_SCALE_COUNTER;
		c->block_count.stat[i + 1].scale = FE_SCALE_COUNTER;
		c->block_error.stat[i + 1].uvalue += lr->error_ts_packets;
		c->block_count.stat[i + 1].uvalue += lr->total_ts_packets;

		/* Update global PER counter */
		c->block_error.stat[0].uvalue += lr->error_ts_packets;
		c->block_count.stat[0].uvalue += lr->total_ts_packets;

		/* BER */
		c->post_bit_error.stat[i + 1].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[i + 1].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[i + 1].uvalue += lr->ber_error_count;
		c->post_bit_count.stat[i + 1].uvalue += lr->ber_bit_count;

		/* Update global BER counter */
		c->post_bit_error.stat[0].uvalue += lr->ber_error_count;
		c->post_bit_count.stat[0].uvalue += lr->ber_bit_count;
	}
}

static void smsdvb_update_isdbt_stats_ex(struct smsdvb_client_t *client,
					 struct sms_isdbt_stats_ex *p)
{
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct sms_isdbt_layer_stats *lr;
	int i, n_layers;

	if (client->prt_isdb_stats_ex)
		client->prt_isdb_stats_ex(client->debug_data, p);

	/* Update ISDB-T transmission parameters */
	c->frequency = p->frequency;
	client->fe_status = sms_to_status(p->is_demod_locked, 0);
	c->bandwidth_hz = sms_to_bw(p->bandwidth);
	c->transmission_mode = sms_to_isdbt_mode(p->transmission_mode);
	c->guard_interval = sms_to_isdbt_guard_interval(p->guard_interval);
	c->isdbt_partial_reception = p->partial_reception ? 1 : 0;
	n_layers = p->num_of_layers;
	if (n_layers < 1)
		n_layers = 1;
	if (n_layers > 3)
		n_layers = 3;
	c->isdbt_layer_enabled = 0;

	/* update reception data */
	c->lna = p->is_external_lna_on ? 1 : 0;

	/* Carrier to noise ratio, in DB */
	c->cnr.stat[0].svalue = p->SNR * 1000;

	/* signal Strength, in DBm */
	c->strength.stat[0].uvalue = p->in_band_pwr * 1000;

	/* PER/BER and per-layer stats require demod lock */
	if (!p->is_demod_locked)
		return;

	client->last_per = c->block_error.stat[0].uvalue;

	/* Clears global counters, as the code below will sum it again */
	c->block_error.stat[0].uvalue = 0;
	c->block_count.stat[0].uvalue = 0;
	c->block_error.stat[0].scale = FE_SCALE_COUNTER;
	c->block_count.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_error.stat[0].uvalue = 0;
	c->post_bit_count.stat[0].uvalue = 0;
	c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;

	c->post_bit_error.len = n_layers + 1;
	c->post_bit_count.len = n_layers + 1;
	c->block_error.len = n_layers + 1;
	c->block_count.len = n_layers + 1;
	for (i = 0; i < n_layers; i++) {
		lr = &p->layer_info[i];

		/* Update per-layer transmission parameters */
		if (lr->number_of_segments > 0 && lr->number_of_segments < 13) {
			c->isdbt_layer_enabled |= 1 << i;
			c->layer[i].segment_count = lr->number_of_segments;
		} else {
			continue;
		}
		c->layer[i].modulation = sms_to_modulation(lr->constellation);
		c->layer[i].fec = sms_to_code_rate(lr->code_rate);

		/* Time interleaving */
		c->layer[i].interleaving = (u8)lr->ti_ldepth_i;

		/* TS PER */
		c->block_error.stat[i + 1].scale = FE_SCALE_COUNTER;
		c->block_count.stat[i + 1].scale = FE_SCALE_COUNTER;
		c->block_error.stat[i + 1].uvalue += lr->error_ts_packets;
		c->block_count.stat[i + 1].uvalue += lr->total_ts_packets;

		/* Update global PER counter */
		c->block_error.stat[0].uvalue += lr->error_ts_packets;
		c->block_count.stat[0].uvalue += lr->total_ts_packets;

		/* ber */
		c->post_bit_error.stat[i + 1].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[i + 1].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[i + 1].uvalue += lr->ber_error_count;
		c->post_bit_count.stat[i + 1].uvalue += lr->ber_bit_count;

		/* Update global ber counter */
		c->post_bit_error.stat[0].uvalue += lr->ber_error_count;
		c->post_bit_count.stat[0].uvalue += lr->ber_bit_count;
	}
}

static int smsdvb_onresponse(void *context, struct smscore_buffer_t *cb)
{
	struct smsdvb_client_t *client = (struct smsdvb_client_t *) context;
	struct sms_msg_hdr *phdr = (struct sms_msg_hdr *) (((u8 *) cb->p)
			+ cb->offset);
	void *p = phdr + 1;
	struct dvb_frontend *fe = &client->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	bool is_status_update = false;

	switch (phdr->msg_type) {
	case MSG_SMS_DVBT_BDA_DATA:
		/*
		 * Only feed data to dvb demux if are there any feed listening
		 * to it and if the device has tuned
		 */
		if (client->feed_users && client->has_tuned)
			dvb_dmx_swfilter(&client->demux, p,
					 cb->size - sizeof(struct sms_msg_hdr));
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
			/* Skip sms_msg_statistics_info:request_result field */
			smsdvb_update_dvb_stats(client, p + sizeof(u32));
		}

		is_status_update = true;
		break;

	/* Only for ISDB-T */
	case MSG_SMS_GET_STATISTICS_EX_RES:
		/* Skip sms_msg_statistics_info:request_result field? */
		smsdvb_update_isdbt_stats_ex(client, p + sizeof(u32));
		is_status_update = true;
		break;
	default:
		pr_debug("message not handled\n");
	}
	smscore_putbuffer(client->coredev, cb);

	if (is_status_update) {
		if (client->fe_status & FE_HAS_LOCK) {
			sms_board_dvb3_event(client, DVB3_EVENT_FE_LOCK);
			if (client->last_per == c->block_error.stat[0].uvalue)
				sms_board_dvb3_event(client, DVB3_EVENT_UNC_OK);
			else
				sms_board_dvb3_event(client, DVB3_EVENT_UNC_ERR);
			client->has_tuned = true;
		} else {
			smsdvb_stats_not_ready(fe);
			client->has_tuned = false;
			sms_board_dvb3_event(client, DVB3_EVENT_FE_UNLOCK);
		}
		complete(&client->stats_done);
	}

	return 0;
}

static void smsdvb_media_device_unregister(struct smsdvb_client_t *client)
{
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	struct smscore_device_t *coredev = client->coredev;

	if (!coredev->media_dev)
		return;
	media_device_unregister(coredev->media_dev);
	media_device_cleanup(coredev->media_dev);
	kfree(coredev->media_dev);
	coredev->media_dev = NULL;
#endif
}

static void smsdvb_unregister_client(struct smsdvb_client_t *client)
{
	/* must be called under clientslock */

	list_del(&client->entry);

	smsdvb_debugfs_release(client);
	smscore_unregister_client(client->smsclient);
	dvb_unregister_frontend(&client->frontend);
	dvb_dmxdev_release(&client->dmxdev);
	dvb_dmx_release(&client->demux);
	smsdvb_media_device_unregister(client);
	dvb_unregister_adapter(&client->adapter);
	kfree(client);
}

static void smsdvb_onremove(void *context)
{
	mutex_lock(&g_smsdvb_clientslock);

	smsdvb_unregister_client((struct smsdvb_client_t *) context);

	mutex_unlock(&g_smsdvb_clientslock);
}

static int smsdvb_start_feed(struct dvb_demux_feed *feed)
{
	struct smsdvb_client_t *client =
		container_of(feed->demux, struct smsdvb_client_t, demux);
	struct sms_msg_data pid_msg;

	pr_debug("add pid %d(%x)\n",
		  feed->pid, feed->pid);

	client->feed_users++;

	pid_msg.x_msg_header.msg_src_id = DVBT_BDA_CONTROL_MSG_ID;
	pid_msg.x_msg_header.msg_dst_id = HIF_TASK;
	pid_msg.x_msg_header.msg_flags = 0;
	pid_msg.x_msg_header.msg_type  = MSG_SMS_ADD_PID_FILTER_REQ;
	pid_msg.x_msg_header.msg_length = sizeof(pid_msg);
	pid_msg.msg_data = feed->pid;

	return smsclient_sendrequest(client->smsclient,
				     &pid_msg, sizeof(pid_msg));
}

static int smsdvb_stop_feed(struct dvb_demux_feed *feed)
{
	struct smsdvb_client_t *client =
		container_of(feed->demux, struct smsdvb_client_t, demux);
	struct sms_msg_data pid_msg;

	pr_debug("remove pid %d(%x)\n",
		  feed->pid, feed->pid);

	client->feed_users--;

	pid_msg.x_msg_header.msg_src_id = DVBT_BDA_CONTROL_MSG_ID;
	pid_msg.x_msg_header.msg_dst_id = HIF_TASK;
	pid_msg.x_msg_header.msg_flags = 0;
	pid_msg.x_msg_header.msg_type  = MSG_SMS_REMOVE_PID_FILTER_REQ;
	pid_msg.x_msg_header.msg_length = sizeof(pid_msg);
	pid_msg.msg_data = feed->pid;

	return smsclient_sendrequest(client->smsclient,
				     &pid_msg, sizeof(pid_msg));
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
	struct sms_msg_hdr msg;

	/* Don't request stats too fast */
	if (client->get_stats_jiffies &&
	   (!time_after(jiffies, client->get_stats_jiffies)))
		return 0;
	client->get_stats_jiffies = jiffies + msecs_to_jiffies(100);

	msg.msg_src_id = DVBT_BDA_CONTROL_MSG_ID;
	msg.msg_dst_id = HIF_TASK;
	msg.msg_flags = 0;
	msg.msg_length = sizeof(msg);

	switch (smscore_get_device_mode(client->coredev)) {
	case DEVICE_MODE_ISDBT:
	case DEVICE_MODE_ISDBT_BDA:
		/*
		* Check for firmware version, to avoid breaking for old cards
		*/
		if (client->coredev->fw_version >= 0x800)
			msg.msg_type = MSG_SMS_GET_STATISTICS_EX_REQ;
		else
			msg.msg_type = MSG_SMS_GET_STATISTICS_REQ;
		break;
	default:
		msg.msg_type = MSG_SMS_GET_STATISTICS_REQ;
	}

	rc = smsdvb_sendrequest_and_wait(client, &msg, sizeof(msg),
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

static int smsdvb_read_status(struct dvb_frontend *fe, enum fe_status *stat)
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
	*snr = ((u32)c->cnr.stat[0].svalue) / 100;

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
	pr_debug("\n");

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
		struct sms_msg_hdr	msg;
		u32		Data[3];
	} msg;

	int ret;

	client->fe_status = 0;
	client->event_fe_state = -1;
	client->event_unc_state = -1;
	fe->dtv_property_cache.delivery_system = SYS_DVBT;

	msg.msg.msg_src_id = DVBT_BDA_CONTROL_MSG_ID;
	msg.msg.msg_dst_id = HIF_TASK;
	msg.msg.msg_flags = 0;
	msg.msg.msg_type = MSG_SMS_RF_TUNE_REQ;
	msg.msg.msg_length = sizeof(msg);
	msg.Data[0] = c->frequency;
	msg.Data[2] = 12000000;

	pr_debug("%s: freq %d band %d\n", __func__, c->frequency,
		 c->bandwidth_hz);

	switch (c->bandwidth_hz / 1000000) {
	case 8:
		msg.Data[1] = BW_8_MHZ;
		break;
	case 7:
		msg.Data[1] = BW_7_MHZ;
		break;
	case 6:
		msg.Data[1] = BW_6_MHZ;
		break;
	case 0:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
	/* Disable LNA, if any. An error is returned if no LNA is present */
	ret = sms_board_lna_control(client->coredev, 0);
	if (ret == 0) {
		enum fe_status status;

		/* tune with LNA off at first */
		ret = smsdvb_sendrequest_and_wait(client, &msg, sizeof(msg),
						  &client->tune_done);

		smsdvb_read_status(fe, &status);

		if (status & FE_HAS_LOCK)
			return ret;

		/* previous tune didn't lock - enable LNA and tune again */
		sms_board_lna_control(client->coredev, 1);
	}

	return smsdvb_sendrequest_and_wait(client, &msg, sizeof(msg),
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
		struct sms_msg_hdr	msg;
		u32		Data[4];
	} msg;

	fe->dtv_property_cache.delivery_system = SYS_ISDBT;

	msg.msg.msg_src_id  = DVBT_BDA_CONTROL_MSG_ID;
	msg.msg.msg_dst_id  = HIF_TASK;
	msg.msg.msg_flags  = 0;
	msg.msg.msg_type   = MSG_SMS_ISDBT_TUNE_REQ;
	msg.msg.msg_length = sizeof(msg);

	if (c->isdbt_sb_segment_idx == -1)
		c->isdbt_sb_segment_idx = 0;

	if (!c->isdbt_layer_enabled)
		c->isdbt_layer_enabled = 7;

	msg.Data[0] = c->frequency;
	msg.Data[1] = BW_ISDBT_1SEG;
	msg.Data[2] = 12000000;
	msg.Data[3] = c->isdbt_sb_segment_idx;

	if (c->isdbt_partial_reception) {
		if ((type == SMS_PELE || type == SMS_RIO) &&
		    c->isdbt_sb_segment_count > 3)
			msg.Data[1] = BW_ISDBT_13SEG;
		else if (c->isdbt_sb_segment_count > 1)
			msg.Data[1] = BW_ISDBT_3SEG;
	} else if (type == SMS_PELE || type == SMS_RIO)
		msg.Data[1] = BW_ISDBT_13SEG;

	c->bandwidth_hz = 6000000;

	pr_debug("freq %d segwidth %d segindex %d\n",
		 c->frequency, c->isdbt_sb_segment_count,
		 c->isdbt_sb_segment_idx);

	/* Disable LNA, if any. An error is returned if no LNA is present */
	ret = sms_board_lna_control(client->coredev, 0);
	if (ret == 0) {
		enum fe_status status;

		/* tune with LNA off at first */
		ret = smsdvb_sendrequest_and_wait(client, &msg, sizeof(msg),
						  &client->tune_done);

		smsdvb_read_status(fe, &status);

		if (status & FE_HAS_LOCK)
			return ret;

		/* previous tune didn't lock - enable LNA and tune again */
		sms_board_lna_control(client->coredev, 1);
	}
	return smsdvb_sendrequest_and_wait(client, &msg, sizeof(msg),
					   &client->tune_done);
}

static int smsdvb_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct smsdvb_client_t *client =
		container_of(fe, struct smsdvb_client_t, frontend);
	struct smscore_device_t *coredev = client->coredev;

	smsdvb_stats_not_ready(fe);
	c->strength.stat[0].uvalue = 0;
	c->cnr.stat[0].uvalue = 0;

	client->has_tuned = false;

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

static const struct dvb_frontend_ops smsdvb_fe_ops = {
	.info = {
		.name			= "Siano Mobile Digital MDTV Receiver",
		.frequency_min_hz	=  44250 * kHz,
		.frequency_max_hz	= 867250 * kHz,
		.frequency_stepsize_hz	=    250 * kHz,
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
	if (!client)
		return -ENOMEM;

	/* register dvb adapter */
	rc = dvb_register_adapter(&client->adapter,
				  sms_get_board(
					smscore_get_board_id(coredev))->name,
				  THIS_MODULE, device, adapter_nr);
	if (rc < 0) {
		pr_err("dvb_register_adapter() failed %d\n", rc);
		goto adapter_error;
	}
	dvb_register_media_controller(&client->adapter, coredev->media_dev);

	/* init dvb demux */
	client->demux.dmx.capabilities = DMX_TS_FILTERING;
	client->demux.filternum = 32; /* todo: nova ??? */
	client->demux.feednum = 32;
	client->demux.start_feed = smsdvb_start_feed;
	client->demux.stop_feed = smsdvb_stop_feed;

	rc = dvb_dmx_init(&client->demux);
	if (rc < 0) {
		pr_err("dvb_dmx_init failed %d\n", rc);
		goto dvbdmx_error;
	}

	/* init dmxdev */
	client->dmxdev.filternum = 32;
	client->dmxdev.demux = &client->demux.dmx;
	client->dmxdev.capabilities = 0;

	rc = dvb_dmxdev_init(&client->dmxdev, &client->adapter);
	if (rc < 0) {
		pr_err("dvb_dmxdev_init failed %d\n", rc);
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
		pr_err("frontend registration failed %d\n", rc);
		goto frontend_error;
	}

	params.initial_id = 1;
	params.data_type = MSG_SMS_DVBT_BDA_DATA;
	params.onresponse_handler = smsdvb_onresponse;
	params.onremove_handler = smsdvb_onremove;
	params.context = client;

	rc = smscore_register_client(coredev, &params, &client->smsclient);
	if (rc < 0) {
		pr_err("smscore_register_client() failed %d\n", rc);
		goto client_error;
	}

	client->coredev = coredev;

	init_completion(&client->tune_done);
	init_completion(&client->stats_done);

	mutex_lock(&g_smsdvb_clientslock);

	list_add(&client->entry, &g_smsdvb_clients);

	mutex_unlock(&g_smsdvb_clientslock);

	client->event_fe_state = -1;
	client->event_unc_state = -1;
	sms_board_dvb3_event(client, DVB3_EVENT_HOTPLUG);

	sms_board_setup(coredev);

	if (smsdvb_debugfs_create(client) < 0)
		pr_info("failed to create debugfs node\n");

	rc = dvb_create_media_graph(&client->adapter, true);
	if (rc < 0) {
		pr_err("dvb_create_media_graph failed %d\n", rc);
		goto media_graph_error;
	}

	pr_info("DVB interface registered.\n");
	return 0;

media_graph_error:
	mutex_lock(&g_smsdvb_clientslock);
	list_del(&client->entry);
	mutex_unlock(&g_smsdvb_clientslock);

	smsdvb_debugfs_release(client);

client_error:
	dvb_unregister_frontend(&client->frontend);

frontend_error:
	dvb_dmxdev_release(&client->dmxdev);

dmxdev_error:
	dvb_dmx_release(&client->demux);

dvbdmx_error:
	smsdvb_media_device_unregister(client);
	dvb_unregister_adapter(&client->adapter);

adapter_error:
	kfree(client);
	return rc;
}

static int __init smsdvb_module_init(void)
{
	int rc;

	smsdvb_debugfs_register();

	rc = smscore_register_hotplug(smsdvb_hotplug);

	pr_debug("\n");

	return rc;
}

static void __exit smsdvb_module_exit(void)
{
	smscore_unregister_hotplug(smsdvb_hotplug);

	mutex_lock(&g_smsdvb_clientslock);

	while (!list_empty(&g_smsdvb_clients))
		smsdvb_unregister_client((struct smsdvb_client_t *)g_smsdvb_clients.next);

	smsdvb_debugfs_unregister();

	mutex_unlock(&g_smsdvb_clientslock);
}

module_init(smsdvb_module_init);
module_exit(smsdvb_module_exit);

MODULE_DESCRIPTION("SMS DVB subsystem adaptation module");
MODULE_AUTHOR("Siano Mobile Silicon, Inc. <uris@siano-ms.com>");
MODULE_LICENSE("GPL");
