/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Ben Backx <ben@bbackx.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/bug.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <ieee1394_transactions.h>
#include <nodemgr.h>

#include "avc_api.h"
#include "firesat.h"
#include "firesat-rc.h"

#define FCP_COMMAND_REGISTER	0xfffff0000b00ULL

static int __avc_write(struct firesat *firesat,
		       const AVCCmdFrm *CmdFrm, AVCRspFrm *RspFrm)
{
	int err, retry;

	if (RspFrm)
		firesat->avc_reply_received = false;

	for (retry = 0; retry < 6; retry++) {
		err = hpsb_node_write(firesat->ud->ne, FCP_COMMAND_REGISTER,
				      (quadlet_t *)CmdFrm, CmdFrm->length);
		if (err) {
			firesat->avc_reply_received = true;
			dev_err(&firesat->ud->device,
				"FCP command write failed\n");
			return err;
		}

		if (!RspFrm)
			return 0;

		/*
		 * AV/C specs say that answers should be sent within 150 ms.
		 * Time out after 200 ms.
		 */
		if (wait_event_timeout(firesat->avc_wait,
				       firesat->avc_reply_received,
				       HZ / 5) != 0) {
			memcpy(RspFrm, firesat->respfrm, firesat->resp_length);
			RspFrm->length = firesat->resp_length;

			return 0;
		}
	}
	dev_err(&firesat->ud->device, "FCP response timed out\n");
	return -ETIMEDOUT;
}

static int avc_write(struct firesat *firesat,
		     const AVCCmdFrm *CmdFrm, AVCRspFrm *RspFrm)
{
	int ret;

	if (mutex_lock_interruptible(&firesat->avc_mutex))
		return -EINTR;

	ret = __avc_write(firesat, CmdFrm, RspFrm);

	mutex_unlock(&firesat->avc_mutex);
	return ret;
}

int avc_recv(struct firesat *firesat, u8 *data, size_t length)
{
	AVCRspFrm *RspFrm = (AVCRspFrm *)data;

	if (length >= 8 &&
	    RspFrm->operand[0] == SFE_VENDOR_DE_COMPANYID_0 &&
	    RspFrm->operand[1] == SFE_VENDOR_DE_COMPANYID_1 &&
	    RspFrm->operand[2] == SFE_VENDOR_DE_COMPANYID_2 &&
	    RspFrm->operand[3] == SFE_VENDOR_OPCODE_REGISTER_REMOTE_CONTROL) {
		if (RspFrm->resp == CHANGED) {
			firesat_handle_rc(firesat,
			    RspFrm->operand[4] << 8 | RspFrm->operand[5]);
			schedule_work(&firesat->remote_ctrl_work);
		} else if (RspFrm->resp != INTERIM) {
			dev_info(&firesat->ud->device,
				 "remote control result = %d\n", RspFrm->resp);
		}
		return 0;
	}

	if (firesat->avc_reply_received) {
		dev_err(&firesat->ud->device,
			"received out-of-order AVC response, ignored\n");
		return -EIO;
	}

	memcpy(firesat->respfrm, data, length);
	firesat->resp_length = length;

	firesat->avc_reply_received = true;
	wake_up(&firesat->avc_wait);

	return 0;
}

/*
 * tuning command for setting the relative LNB frequency
 * (not supported by the AVC standard)
 */
static void avc_tuner_tuneqpsk(struct firesat *firesat,
		struct dvb_frontend_parameters *params, AVCCmdFrm *CmdFrm)
{
	CmdFrm->opcode = VENDOR;

	CmdFrm->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm->operand[3] = SFE_VENDOR_OPCODE_TUNE_QPSK;

	CmdFrm->operand[4] = (params->frequency >> 24) & 0xff;
	CmdFrm->operand[5] = (params->frequency >> 16) & 0xff;
	CmdFrm->operand[6] = (params->frequency >> 8) & 0xff;
	CmdFrm->operand[7] = params->frequency & 0xff;

	CmdFrm->operand[8] = ((params->u.qpsk.symbol_rate / 1000) >> 8) & 0xff;
	CmdFrm->operand[9] = (params->u.qpsk.symbol_rate / 1000) & 0xff;

	switch(params->u.qpsk.fec_inner) {
	case FEC_1_2:
		CmdFrm->operand[10] = 0x1; break;
	case FEC_2_3:
		CmdFrm->operand[10] = 0x2; break;
	case FEC_3_4:
		CmdFrm->operand[10] = 0x3; break;
	case FEC_5_6:
		CmdFrm->operand[10] = 0x4; break;
	case FEC_7_8:
		CmdFrm->operand[10] = 0x5; break;
	case FEC_4_5:
	case FEC_8_9:
	case FEC_AUTO:
	default:
		CmdFrm->operand[10] = 0x0;
	}

	if (firesat->voltage == 0xff)
		CmdFrm->operand[11] = 0xff;
	else if (firesat->voltage == SEC_VOLTAGE_18) /* polarisation */
		CmdFrm->operand[11] = 0;
	else
		CmdFrm->operand[11] = 1;

	if (firesat->tone == 0xff)
		CmdFrm->operand[12] = 0xff;
	else if (firesat->tone == SEC_TONE_ON) /* band */
		CmdFrm->operand[12] = 1;
	else
		CmdFrm->operand[12] = 0;

	if (firesat->type == FireSAT_DVB_S2) {
		CmdFrm->operand[13] = 0x1;
		CmdFrm->operand[14] = 0xff;
		CmdFrm->operand[15] = 0xff;
		CmdFrm->length = 20;
	} else {
		CmdFrm->length = 16;
	}
}

static void avc_tuner_dsd_dvb_c(struct dvb_frontend_parameters *params,
		AVCCmdFrm *CmdFrm)
{
	M_VALID_FLAGS flags;

	flags.Bits.Modulation = params->u.qam.modulation != QAM_AUTO;
	flags.Bits.FEC_inner = params->u.qam.fec_inner != FEC_AUTO;
	flags.Bits.FEC_outer = 0;
	flags.Bits.Symbol_Rate = 1;
	flags.Bits.Frequency = 1;
	flags.Bits.Orbital_Pos = 0;
	flags.Bits.Polarisation = 0;
	flags.Bits.reserved_fields = 0;
	flags.Bits.reserved1 = 0;
	flags.Bits.Network_ID = 0;

	CmdFrm->opcode	= DSD;

	CmdFrm->operand[0]  = 0;    /* source plug */
	CmdFrm->operand[1]  = 0xd2; /* subfunction replace */
	CmdFrm->operand[2]  = 0x20; /* system id = DVB */
	CmdFrm->operand[3]  = 0x00; /* antenna number */
	/* system_specific_multiplex selection_length */
	CmdFrm->operand[4]  = 0x11;
	CmdFrm->operand[5]  = flags.Valid_Word.ByteHi; /* valid_flags [0] */
	CmdFrm->operand[6]  = flags.Valid_Word.ByteLo; /* valid_flags [1] */
	CmdFrm->operand[7]  = 0x00;
	CmdFrm->operand[8]  = 0x00;
	CmdFrm->operand[9]  = 0x00;
	CmdFrm->operand[10] = 0x00;

	CmdFrm->operand[11] =
		(((params->frequency / 4000) >> 16) & 0xff) | (2 << 6);
	CmdFrm->operand[12] =
		((params->frequency / 4000) >> 8) & 0xff;
	CmdFrm->operand[13] = (params->frequency / 4000) & 0xff;
	CmdFrm->operand[14] =
		((params->u.qpsk.symbol_rate / 1000) >> 12) & 0xff;
	CmdFrm->operand[15] =
		((params->u.qpsk.symbol_rate / 1000) >> 4) & 0xff;
	CmdFrm->operand[16] =
		((params->u.qpsk.symbol_rate / 1000) << 4) & 0xf0;
	CmdFrm->operand[17] = 0x00;

	switch (params->u.qpsk.fec_inner) {
	case FEC_1_2:
		CmdFrm->operand[18] = 0x1; break;
	case FEC_2_3:
		CmdFrm->operand[18] = 0x2; break;
	case FEC_3_4:
		CmdFrm->operand[18] = 0x3; break;
	case FEC_5_6:
		CmdFrm->operand[18] = 0x4; break;
	case FEC_7_8:
		CmdFrm->operand[18] = 0x5; break;
	case FEC_8_9:
		CmdFrm->operand[18] = 0x6; break;
	case FEC_4_5:
		CmdFrm->operand[18] = 0x8; break;
	case FEC_AUTO:
	default:
		CmdFrm->operand[18] = 0x0;
	}
	switch (params->u.qam.modulation) {
	case QAM_16:
		CmdFrm->operand[19] = 0x08; break;
	case QAM_32:
		CmdFrm->operand[19] = 0x10; break;
	case QAM_64:
		CmdFrm->operand[19] = 0x18; break;
	case QAM_128:
		CmdFrm->operand[19] = 0x20; break;
	case QAM_256:
		CmdFrm->operand[19] = 0x28; break;
	case QAM_AUTO:
	default:
		CmdFrm->operand[19] = 0x00;
	}
	CmdFrm->operand[20] = 0x00;
	CmdFrm->operand[21] = 0x00;
	/* Nr_of_dsd_sel_specs = 0 -> no PIDs are transmitted */
	CmdFrm->operand[22] = 0x00;

	CmdFrm->length = 28;
}

static void avc_tuner_dsd_dvb_t(struct dvb_frontend_parameters *params,
		AVCCmdFrm *CmdFrm)
{
	M_VALID_FLAGS flags;

	flags.Bits_T.GuardInterval =
		params->u.ofdm.guard_interval != GUARD_INTERVAL_AUTO;
	flags.Bits_T.CodeRateLPStream =
		params->u.ofdm.code_rate_LP != FEC_AUTO;
	flags.Bits_T.CodeRateHPStream =
		params->u.ofdm.code_rate_HP != FEC_AUTO;
	flags.Bits_T.HierarchyInfo =
		params->u.ofdm.hierarchy_information != HIERARCHY_AUTO;
	flags.Bits_T.Constellation =
		params->u.ofdm.constellation != QAM_AUTO;
	flags.Bits_T.Bandwidth =
		params->u.ofdm.bandwidth != BANDWIDTH_AUTO;
	flags.Bits_T.CenterFrequency = 1;
	flags.Bits_T.reserved1 = 0;
	flags.Bits_T.reserved2 = 0;
	flags.Bits_T.OtherFrequencyFlag = 0;
	flags.Bits_T.TransmissionMode =
		params->u.ofdm.transmission_mode != TRANSMISSION_MODE_AUTO;
	flags.Bits_T.NetworkId = 0;

	CmdFrm->opcode	= DSD;

	CmdFrm->operand[0]  = 0;    /* source plug */
	CmdFrm->operand[1]  = 0xd2; /* subfunction replace */
	CmdFrm->operand[2]  = 0x20; /* system id = DVB */
	CmdFrm->operand[3]  = 0x00; /* antenna number */
	/* system_specific_multiplex selection_length */
	CmdFrm->operand[4]  = 0x0c;
	CmdFrm->operand[5]  = flags.Valid_Word.ByteHi; /* valid_flags [0] */
	CmdFrm->operand[6]  = flags.Valid_Word.ByteLo; /* valid_flags [1] */
	CmdFrm->operand[7]  = 0x0;
	CmdFrm->operand[8]  = (params->frequency / 10) >> 24;
	CmdFrm->operand[9]  = ((params->frequency / 10) >> 16) & 0xff;
	CmdFrm->operand[10] = ((params->frequency / 10) >>  8) & 0xff;
	CmdFrm->operand[11] = (params->frequency / 10) & 0xff;

	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_7_MHZ:
		CmdFrm->operand[12] = 0x20; break;
	case BANDWIDTH_8_MHZ:
	case BANDWIDTH_6_MHZ: /* not defined by AVC spec */
	case BANDWIDTH_AUTO:
	default:
		CmdFrm->operand[12] = 0x00;
	}
	switch (params->u.ofdm.constellation) {
	case QAM_16:
		CmdFrm->operand[13] = 1 << 6; break;
	case QAM_64:
		CmdFrm->operand[13] = 2 << 6; break;
	case QPSK:
	default:
		CmdFrm->operand[13] = 0x00;
	}
	switch (params->u.ofdm.hierarchy_information) {
	case HIERARCHY_1:
		CmdFrm->operand[13] |= 1 << 3; break;
	case HIERARCHY_2:
		CmdFrm->operand[13] |= 2 << 3; break;
	case HIERARCHY_4:
		CmdFrm->operand[13] |= 3 << 3; break;
	case HIERARCHY_AUTO:
	case HIERARCHY_NONE:
	default:
		break;
	}
	switch (params->u.ofdm.code_rate_HP) {
	case FEC_2_3:
		CmdFrm->operand[13] |= 1; break;
	case FEC_3_4:
		CmdFrm->operand[13] |= 2; break;
	case FEC_5_6:
		CmdFrm->operand[13] |= 3; break;
	case FEC_7_8:
		CmdFrm->operand[13] |= 4; break;
	case FEC_1_2:
	default:
		break;
	}
	switch (params->u.ofdm.code_rate_LP) {
	case FEC_2_3:
		CmdFrm->operand[14] = 1 << 5; break;
	case FEC_3_4:
		CmdFrm->operand[14] = 2 << 5; break;
	case FEC_5_6:
		CmdFrm->operand[14] = 3 << 5; break;
	case FEC_7_8:
		CmdFrm->operand[14] = 4 << 5; break;
	case FEC_1_2:
	default:
		CmdFrm->operand[14] = 0x00; break;
	}
	switch (params->u.ofdm.guard_interval) {
	case GUARD_INTERVAL_1_16:
		CmdFrm->operand[14] |= 1 << 3; break;
	case GUARD_INTERVAL_1_8:
		CmdFrm->operand[14] |= 2 << 3; break;
	case GUARD_INTERVAL_1_4:
		CmdFrm->operand[14] |= 3 << 3; break;
	case GUARD_INTERVAL_1_32:
	case GUARD_INTERVAL_AUTO:
	default:
		break;
	}
	switch (params->u.ofdm.transmission_mode) {
	case TRANSMISSION_MODE_8K:
		CmdFrm->operand[14] |= 1 << 1; break;
	case TRANSMISSION_MODE_2K:
	case TRANSMISSION_MODE_AUTO:
	default:
		break;
	}

	CmdFrm->operand[15] = 0x00; /* network_ID[0] */
	CmdFrm->operand[16] = 0x00; /* network_ID[1] */
	/* Nr_of_dsd_sel_specs = 0 -> no PIDs are transmitted */
	CmdFrm->operand[17] = 0x00;

	CmdFrm->length = 24;
}

int avc_tuner_dsd(struct firesat *firesat,
		  struct dvb_frontend_parameters *params)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));

	CmdFrm.cts	= AVC;
	CmdFrm.ctype	= CONTROL;
	CmdFrm.sutyp	= 0x5;
	CmdFrm.suid	= firesat->subunit;

	switch (firesat->type) {
	case FireSAT_DVB_S:
	case FireSAT_DVB_S2:
		avc_tuner_tuneqpsk(firesat, params, &CmdFrm); break;
	case FireSAT_DVB_C:
		avc_tuner_dsd_dvb_c(params, &CmdFrm); break;
	case FireSAT_DVB_T:
		avc_tuner_dsd_dvb_t(params, &CmdFrm); break;
	default:
		BUG();
	}

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	msleep(500);
#if 0
	/* FIXME: */
	/* u8 *status was an out-parameter of avc_tuner_dsd, unused by caller */
	if(status)
		*status=RspFrm.operand[2];
#endif
	return 0;
}

int avc_tuner_set_pids(struct firesat *firesat, unsigned char pidc, u16 pid[])
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int pos, k;

	if (pidc > 16 && pidc != 0xff)
		return -EINVAL;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));

	CmdFrm.cts	= AVC;
	CmdFrm.ctype	= CONTROL;
	CmdFrm.sutyp	= 0x5;
	CmdFrm.suid	= firesat->subunit;
	CmdFrm.opcode	= DSD;

	CmdFrm.operand[0]  = 0; // source plug
	CmdFrm.operand[1]  = 0xD2; // subfunction replace
	CmdFrm.operand[2]  = 0x20; // system id = DVB
	CmdFrm.operand[3]  = 0x00; // antenna number
	CmdFrm.operand[4]  = 0x00; // system_specific_multiplex selection_length
	CmdFrm.operand[5]  = pidc; // Nr_of_dsd_sel_specs

	pos = 6;
	if (pidc != 0xff)
		for (k = 0; k < pidc; k++) {
			CmdFrm.operand[pos++] = 0x13; // flowfunction relay
			CmdFrm.operand[pos++] = 0x80; // dsd_sel_spec_valid_flags -> PID
			CmdFrm.operand[pos++] = (pid[k] >> 8) & 0x1F;
			CmdFrm.operand[pos++] = pid[k] & 0xFF;
			CmdFrm.operand[pos++] = 0x00; // tableID
			CmdFrm.operand[pos++] = 0x00; // filter_length
		}

	CmdFrm.length = ALIGN(3 + pos, 4);

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	msleep(50);
	return 0;
}

int avc_tuner_get_ts(struct firesat *firesat)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));

	CmdFrm.cts		= AVC;
	CmdFrm.ctype	= CONTROL;
	CmdFrm.sutyp	= 0x5;
	CmdFrm.suid		= firesat->subunit;
	CmdFrm.opcode	= DSIT;

	CmdFrm.operand[0]  = 0; // source plug
	CmdFrm.operand[1]  = 0xD2; // subfunction replace
	CmdFrm.operand[2]  = 0xFF; //status
	CmdFrm.operand[3]  = 0x20; // system id = DVB
	CmdFrm.operand[4]  = 0x00; // antenna number
	CmdFrm.operand[5]  = 0x0;  // system_specific_search_flags
	CmdFrm.operand[6]  = (firesat->type == FireSAT_DVB_T)?0x0c:0x11; // system_specific_multiplex selection_length
	CmdFrm.operand[7]  = 0x00; // valid_flags [0]
	CmdFrm.operand[8]  = 0x00; // valid_flags [1]
	CmdFrm.operand[7 + (firesat->type == FireSAT_DVB_T)?0x0c:0x11] = 0x00; // nr_of_dsit_sel_specs (always 0)

	CmdFrm.length = (firesat->type == FireSAT_DVB_T)?24:28;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	msleep(250);
	return 0;
}

int avc_identify_subunit(struct firesat *firesat)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm,0,sizeof(AVCCmdFrm));

	CmdFrm.cts = AVC;
	CmdFrm.ctype = CONTROL;
	CmdFrm.sutyp = 0x5; // tuner
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = READ_DESCRIPTOR;

	CmdFrm.operand[0]=DESCRIPTOR_SUBUNIT_IDENTIFIER;
	CmdFrm.operand[1]=0xff;
	CmdFrm.operand[2]=0x00;
	CmdFrm.operand[3]=0x00; // length highbyte
	CmdFrm.operand[4]=0x08; // length lowbyte
	CmdFrm.operand[5]=0x00; // offset highbyte
	CmdFrm.operand[6]=0x0d; // offset lowbyte

	CmdFrm.length=12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	if ((RspFrm.resp != STABLE && RspFrm.resp != ACCEPTED) ||
	    (RspFrm.operand[3] << 8) + RspFrm.operand[4] != 8) {
		dev_err(&firesat->ud->device,
			"cannot read subunit identifier\n");
		return -EINVAL;
	}
	return 0;
}

int avc_tuner_status(struct firesat *firesat,
		     ANTENNA_INPUT_INFO *antenna_input_info)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int length;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));

	CmdFrm.cts=AVC;
	CmdFrm.ctype=CONTROL;
	CmdFrm.sutyp=0x05; // tuner
	CmdFrm.suid=firesat->subunit;
	CmdFrm.opcode=READ_DESCRIPTOR;

	CmdFrm.operand[0]=DESCRIPTOR_TUNER_STATUS;
	CmdFrm.operand[1]=0xff; //read_result_status
	CmdFrm.operand[2]=0x00; // reserver
	CmdFrm.operand[3]=0;//sizeof(ANTENNA_INPUT_INFO) >> 8;
	CmdFrm.operand[4]=0;//sizeof(ANTENNA_INPUT_INFO) & 0xFF;
	CmdFrm.operand[5]=0x00;
	CmdFrm.operand[6]=0x00;
	CmdFrm.length=12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	if (RspFrm.resp != STABLE && RspFrm.resp != ACCEPTED) {
		dev_err(&firesat->ud->device, "cannot read tuner status\n");
		return -EINVAL;
	}

	length = RspFrm.operand[9];
	if (RspFrm.operand[1] != 0x10 || length != sizeof(ANTENNA_INPUT_INFO)) {
		dev_err(&firesat->ud->device, "got invalid tuner status\n");
		return -EINVAL;
	}

	memcpy(antenna_input_info, &RspFrm.operand[10], length);
	return 0;
}

int avc_lnb_control(struct firesat *firesat, char voltage, char burst,
		    char conttone, char nrdiseq,
		    struct dvb_diseqc_master_cmd *diseqcmd)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int i, j, k;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));

	CmdFrm.cts=AVC;
	CmdFrm.ctype=CONTROL;
	CmdFrm.sutyp=0x05;
	CmdFrm.suid=firesat->subunit;
	CmdFrm.opcode=VENDOR;

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_LNB_CONTROL;

	CmdFrm.operand[4]=voltage;
	CmdFrm.operand[5]=nrdiseq;

	i=6;

	for (j = 0; j < nrdiseq; j++) {
		CmdFrm.operand[i++] = diseqcmd[j].msg_len;

		for (k = 0; k < diseqcmd[j].msg_len; k++)
			CmdFrm.operand[i++] = diseqcmd[j].msg[k];
	}

	CmdFrm.operand[i++]=burst;
	CmdFrm.operand[i++]=conttone;

	CmdFrm.length = ALIGN(3 + i, 4);

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	if (RspFrm.resp != ACCEPTED) {
		dev_err(&firesat->ud->device, "LNB control failed\n");
		return -EINVAL;
	}

	return 0;
}

int avc_register_remote_control(struct firesat *firesat)
{
	AVCCmdFrm CmdFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));

	CmdFrm.cts = AVC;
	CmdFrm.ctype = NOTIFY;
	CmdFrm.sutyp = 0x1f;
	CmdFrm.suid = 0x7;
	CmdFrm.opcode = VENDOR;

	CmdFrm.operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3] = SFE_VENDOR_OPCODE_REGISTER_REMOTE_CONTROL;

	CmdFrm.length = 8;

	return avc_write(firesat, &CmdFrm, NULL);
}

void avc_remote_ctrl_work(struct work_struct *work)
{
	struct firesat *firesat =
			container_of(work, struct firesat, remote_ctrl_work);

	/* Should it be rescheduled in failure cases? */
	avc_register_remote_control(firesat);
}

#if 0 /* FIXME: unused */
int avc_tuner_host2ca(struct firesat *firesat)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));
	CmdFrm.cts = AVC;
	CmdFrm.ctype = CONTROL;
	CmdFrm.sutyp = 0x5;
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = VENDOR;

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_HOST2CA;
	CmdFrm.operand[4] = 0; // slot
	CmdFrm.operand[5] = SFE_VENDOR_TAG_CA_APPLICATION_INFO; // ca tag
	CmdFrm.operand[6] = 0; // more/last
	CmdFrm.operand[7] = 0; // length
	CmdFrm.length = 12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	return 0;
}
#endif

static int get_ca_object_pos(AVCRspFrm *RspFrm)
{
	int length = 1;

	/* Check length of length field */
	if (RspFrm->operand[7] & 0x80)
		length = (RspFrm->operand[7] & 0x7f) + 1;
	return length + 7;
}

static int get_ca_object_length(AVCRspFrm *RspFrm)
{
#if 0 /* FIXME: unused */
	int size = 0;
	int i;

	if (RspFrm->operand[7] & 0x80)
		for (i = 0; i < (RspFrm->operand[7] & 0x7f); i++) {
			size <<= 8;
			size += RspFrm->operand[8 + i];
		}
#endif
	return RspFrm->operand[7];
}

int avc_ca_app_info(struct firesat *firesat, char *app_info, unsigned int *len)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int pos;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));
	CmdFrm.cts = AVC;
	CmdFrm.ctype = STATUS;
	CmdFrm.sutyp = 0x5;
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = VENDOR;

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_CA2HOST;
	CmdFrm.operand[4] = 0; // slot
	CmdFrm.operand[5] = SFE_VENDOR_TAG_CA_APPLICATION_INFO; // ca tag
	CmdFrm.length = 12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	/* FIXME: check response code and validate response data */

	pos = get_ca_object_pos(&RspFrm);
	app_info[0] = (TAG_APP_INFO >> 16) & 0xFF;
	app_info[1] = (TAG_APP_INFO >> 8) & 0xFF;
	app_info[2] = (TAG_APP_INFO >> 0) & 0xFF;
	app_info[3] = 6 + RspFrm.operand[pos + 4];
	app_info[4] = 0x01;
	memcpy(&app_info[5], &RspFrm.operand[pos], 5 + RspFrm.operand[pos + 4]);
	*len = app_info[3] + 4;

	return 0;
}

int avc_ca_info(struct firesat *firesat, char *app_info, unsigned int *len)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int pos;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));
	CmdFrm.cts = AVC;
	CmdFrm.ctype = STATUS;
	CmdFrm.sutyp = 0x5;
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = VENDOR;

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_CA2HOST;
	CmdFrm.operand[4] = 0; // slot
	CmdFrm.operand[5] = SFE_VENDOR_TAG_CA_APPLICATION_INFO; // ca tag
	CmdFrm.length = 12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	pos = get_ca_object_pos(&RspFrm);
	app_info[0] = (TAG_CA_INFO >> 16) & 0xFF;
	app_info[1] = (TAG_CA_INFO >> 8) & 0xFF;
	app_info[2] = (TAG_CA_INFO >> 0) & 0xFF;
	app_info[3] = 2;
	app_info[4] = RspFrm.operand[pos + 0];
	app_info[5] = RspFrm.operand[pos + 1];
	*len = app_info[3] + 4;

	return 0;
}

int avc_ca_reset(struct firesat *firesat)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));
	CmdFrm.cts = AVC;
	CmdFrm.ctype = CONTROL;
	CmdFrm.sutyp = 0x5;
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = VENDOR;

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_HOST2CA;
	CmdFrm.operand[4] = 0; // slot
	CmdFrm.operand[5] = SFE_VENDOR_TAG_CA_RESET; // ca tag
	CmdFrm.operand[6] = 0; // more/last
	CmdFrm.operand[7] = 1; // length
	CmdFrm.operand[8] = 0; // force hardware reset
	CmdFrm.length = 12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	return 0;
}

int avc_ca_pmt(struct firesat *firesat, char *msg, int length)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int list_management;
	int program_info_length;
	int pmt_cmd_id;
	int read_pos;
	int write_pos;
	int es_info_length;
	int crc32_csum;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));
	CmdFrm.cts = AVC;
	CmdFrm.ctype = CONTROL;
	CmdFrm.sutyp = 0x5;
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = VENDOR;

	if (msg[0] != LIST_MANAGEMENT_ONLY) {
		dev_info(&firesat->ud->device,
			 "forcing list_management to ONLY\n");
		msg[0] = LIST_MANAGEMENT_ONLY;
	}
	// We take the cmd_id from the programme level only!
	list_management = msg[0];
	program_info_length = ((msg[4] & 0x0F) << 8) + msg[5];
	if (program_info_length > 0)
		program_info_length--; // Remove pmt_cmd_id
	pmt_cmd_id = msg[6];

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_HOST2CA;
	CmdFrm.operand[4] = 0; // slot
	CmdFrm.operand[5] = SFE_VENDOR_TAG_CA_PMT; // ca tag
	CmdFrm.operand[6] = 0; // more/last
	//CmdFrm.operand[7] = XXXprogram_info_length + 17; // length
	CmdFrm.operand[8] = list_management;
	CmdFrm.operand[9] = 0x01; // pmt_cmd=OK_descramble

	// TS program map table

	// Table id=2
	CmdFrm.operand[10] = 0x02;
	// Section syntax + length
	CmdFrm.operand[11] = 0x80;
	//CmdFrm.operand[12] = XXXprogram_info_length + 12;
	// Program number
	CmdFrm.operand[13] = msg[1];
	CmdFrm.operand[14] = msg[2];
	// Version number=0 + current/next=1
	CmdFrm.operand[15] = 0x01;
	// Section number=0
	CmdFrm.operand[16] = 0x00;
	// Last section number=0
	CmdFrm.operand[17] = 0x00;
	// PCR_PID=1FFF
	CmdFrm.operand[18] = 0x1F;
	CmdFrm.operand[19] = 0xFF;
	// Program info length
	CmdFrm.operand[20] = (program_info_length >> 8);
	CmdFrm.operand[21] = (program_info_length & 0xFF);
	// CA descriptors at programme level
	read_pos = 6;
	write_pos = 22;
	if (program_info_length > 0) {
		pmt_cmd_id = msg[read_pos++];
		if (pmt_cmd_id != 1 && pmt_cmd_id != 4)
			dev_err(&firesat->ud->device,
				"invalid pmt_cmd_id %d\n", pmt_cmd_id);

		memcpy(&CmdFrm.operand[write_pos], &msg[read_pos],
		       program_info_length);
		read_pos += program_info_length;
		write_pos += program_info_length;
	}
	while (read_pos < length) {
		CmdFrm.operand[write_pos++] = msg[read_pos++];
		CmdFrm.operand[write_pos++] = msg[read_pos++];
		CmdFrm.operand[write_pos++] = msg[read_pos++];
		es_info_length =
			((msg[read_pos] & 0x0F) << 8) + msg[read_pos + 1];
		read_pos += 2;
		if (es_info_length > 0)
			es_info_length--; // Remove pmt_cmd_id
		CmdFrm.operand[write_pos++] = es_info_length >> 8;
		CmdFrm.operand[write_pos++] = es_info_length & 0xFF;
		if (es_info_length > 0) {
			pmt_cmd_id = msg[read_pos++];
			if (pmt_cmd_id != 1 && pmt_cmd_id != 4)
				dev_err(&firesat->ud->device,
					"invalid pmt_cmd_id %d "
					"at stream level\n", pmt_cmd_id);

			memcpy(&CmdFrm.operand[write_pos], &msg[read_pos],
			       es_info_length);
			read_pos += es_info_length;
			write_pos += es_info_length;
		}
	}

	// CRC
	CmdFrm.operand[write_pos++] = 0x00;
	CmdFrm.operand[write_pos++] = 0x00;
	CmdFrm.operand[write_pos++] = 0x00;
	CmdFrm.operand[write_pos++] = 0x00;

	CmdFrm.operand[7] = write_pos - 8;
	CmdFrm.operand[12] = write_pos - 13;

	crc32_csum = crc32_be(0, &CmdFrm.operand[10],
			   CmdFrm.operand[12] - 1);
	CmdFrm.operand[write_pos - 4] = (crc32_csum >> 24) & 0xFF;
	CmdFrm.operand[write_pos - 3] = (crc32_csum >> 16) & 0xFF;
	CmdFrm.operand[write_pos - 2] = (crc32_csum >>  8) & 0xFF;
	CmdFrm.operand[write_pos - 1] = (crc32_csum >>  0) & 0xFF;

	CmdFrm.length = ALIGN(3 + write_pos, 4);

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	if (RspFrm.resp != ACCEPTED) {
		dev_err(&firesat->ud->device,
			"CA PMT failed with response 0x%x\n", RspFrm.resp);
		return -EFAULT;
	}

	return 0;
}

int avc_ca_get_time_date(struct firesat *firesat, int *interval)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));
	CmdFrm.cts = AVC;
	CmdFrm.ctype = STATUS;
	CmdFrm.sutyp = 0x5;
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = VENDOR;

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_CA2HOST;
	CmdFrm.operand[4] = 0; // slot
	CmdFrm.operand[5] = SFE_VENDOR_TAG_CA_DATE_TIME; // ca tag
	CmdFrm.operand[6] = 0; // more/last
	CmdFrm.operand[7] = 0; // length
	CmdFrm.length = 12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	/* FIXME: check response code and validate response data */

	*interval = RspFrm.operand[get_ca_object_pos(&RspFrm)];

	return 0;
}

int avc_ca_enter_menu(struct firesat *firesat)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));
	CmdFrm.cts = AVC;
	CmdFrm.ctype = STATUS;
	CmdFrm.sutyp = 0x5;
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = VENDOR;

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_HOST2CA;
	CmdFrm.operand[4] = 0; // slot
	CmdFrm.operand[5] = SFE_VENDOR_TAG_CA_ENTER_MENU;
	CmdFrm.operand[6] = 0; // more/last
	CmdFrm.operand[7] = 0; // length
	CmdFrm.length = 12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	return 0;
}

int avc_ca_get_mmi(struct firesat *firesat, char *mmi_object, unsigned int *len)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));
	CmdFrm.cts = AVC;
	CmdFrm.ctype = STATUS;
	CmdFrm.sutyp = 0x5;
	CmdFrm.suid = firesat->subunit;
	CmdFrm.opcode = VENDOR;

	CmdFrm.operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm.operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm.operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm.operand[3]=SFE_VENDOR_OPCODE_CA2HOST;
	CmdFrm.operand[4] = 0; // slot
	CmdFrm.operand[5] = SFE_VENDOR_TAG_CA_MMI;
	CmdFrm.operand[6] = 0; // more/last
	CmdFrm.operand[7] = 0; // length
	CmdFrm.length = 12;

	if (avc_write(firesat, &CmdFrm, &RspFrm) < 0)
		return -EIO;

	/* FIXME: check response code and validate response data */

	*len = get_ca_object_length(&RspFrm);
	memcpy(mmi_object, &RspFrm.operand[get_ca_object_pos(&RspFrm)], *len);

	return 0;
}
