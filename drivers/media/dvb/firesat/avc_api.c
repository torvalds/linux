/*
 * FireSAT AVC driver
 *
 * Copyright (c) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (c) 2008 Ben Backx <ben@bbackx.com>
 * Copyright (c) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include "firesat.h"
#include <ieee1394_transactions.h>
#include <nodemgr.h>
#include <asm/byteorder.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include "avc_api.h"
#include "firesat-rc.h"

#define RESPONSE_REGISTER				0xFFFFF0000D00ULL
#define COMMAND_REGISTER				0xFFFFF0000B00ULL
#define PCR_BASE_ADDRESS				0xFFFFF0000900ULL

static unsigned int avc_comm_debug = 0;
module_param(avc_comm_debug, int, 0644);
MODULE_PARM_DESC(avc_comm_debug, "debug logging of AV/C communication, default is 0 (no)");

static int __AVCRegisterRemoteControl(struct firesat*firesat, int internal);

/* Frees an allocated packet */
static void avc_free_packet(struct hpsb_packet *packet)
{
	hpsb_free_tlabel(packet);
	hpsb_free_packet(packet);
}

/*
 * Goofy routine that basically does a down_timeout function.
 * Stolen from sbp2.c
 */
static int avc_down_timeout(atomic_t *done, int timeout)
{
	int i;

	for (i = timeout; (i > 0 && atomic_read(done) == 0); i-= HZ/10) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (schedule_timeout(HZ/10))	/* 100ms */
			return(1);
	}
	return ((i > 0) ? 0:1);
}

static const char* get_ctype_string(__u8 ctype)
{
	switch(ctype)
	{
	case 0:
		return "CONTROL";
	case 1:
		return "STATUS";
	case 2:
		return "SPECIFIC_INQUIRY";
	case 3:
		return "NOTIFY";
	case 4:
		return "GENERAL_INQUIRY";
	}
	return "UNKNOWN";
}

static const char* get_resp_string(__u8 ctype)
{
	switch(ctype)
	{
	case 8:
		return "NOT_IMPLEMENTED";
	case 9:
		return "ACCEPTED";
	case 10:
		return "REJECTED";
	case 11:
		return "IN_TRANSITION";
	case 12:
		return "IMPLEMENTED_STABLE";
	case 13:
		return "CHANGED";
	case 15:
		return "INTERIM";
	}
	return "UNKNOWN";
}

static const char* get_subunit_address(__u8 subunit_id, __u8 subunit_type)
{
	if (subunit_id == 7 && subunit_type == 0x1F)
		return "Unit";
	if (subunit_id == 0 && subunit_type == 0x05)
		return "Tuner(0)";
	return "Unsupported";
}

static const char* get_opcode_string(__u8 opcode)
{
	switch(opcode)
	{
	case 0x02:
		return "PlugInfo";
	case 0x08:
		return "OpenDescriptor";
	case 0x09:
		return "ReadDescriptor";
	case 0x18:
		return "OutputPlugSignalFormat";
	case 0x31:
		return "SubunitInfo";
	case 0x30:
		return "UnitInfo";
	case 0xB2:
		return "Power";
	case 0xC8:
		return "DirectSelectInformationType";
	case 0xCB:
		return "DirectSelectData";
	case 0x00:
		return "Vendor";

	}
	return "Unknown";
}

static void log_command_frame(const AVCCmdFrm *CmdFrm)
{
	int k;
	printk(KERN_INFO "AV/C Command Frame:\n");
	printk("CommandType=%s, Address=%s(0x%02X,0x%02X), opcode=%s(0x%02X), "
	       "length=%d\n", get_ctype_string(CmdFrm->ctype),
	       get_subunit_address(CmdFrm->suid, CmdFrm->sutyp),
	       CmdFrm->suid, CmdFrm->sutyp, get_opcode_string(CmdFrm->opcode),
	       CmdFrm->opcode, CmdFrm->length);
	for(k = 0; k < CmdFrm->length - 3; k++) {
		if (k % 5 != 0)
			printk(", ");
		else if (k != 0)
			printk("\n");
		printk("operand[%d] = %02X", k, CmdFrm->operand[k]);
	}
	printk("\n");
}

static void log_response_frame(const AVCRspFrm *RspFrm)
{
	int k;
	printk(KERN_INFO "AV/C Response Frame:\n");
	printk("Response=%s, Address=%s(0x%02X,0x%02X), opcode=%s(0x%02X), "
	       "length=%d\n", get_resp_string(RspFrm->resp),
	       get_subunit_address(RspFrm->suid, RspFrm->sutyp),
	       RspFrm->suid, RspFrm->sutyp, get_opcode_string(RspFrm->opcode),
	       RspFrm->opcode, RspFrm->length);
	for(k = 0; k < RspFrm->length - 3; k++) {
		if (k % 5 != 0)
			printk(", ");
		else if (k != 0)
			printk("\n");
		printk("operand[%d] = %02X", k, RspFrm->operand[k]);
	}
	printk("\n");
}

static int __AVCWrite(struct firesat *firesat, const AVCCmdFrm *CmdFrm,
		      AVCRspFrm *RspFrm) {
	struct hpsb_packet *packet;
	struct node_entry *ne;

	ne = firesat->nodeentry;
	if(!ne) {
		printk("%s: lost node!\n",__func__);
		return -EIO;
	}

	/* need all input data */
	if(!firesat || !ne || !CmdFrm) {
		printk("%s: missing input data!\n",__func__);
		return -EINVAL;
	}

	if (avc_comm_debug == 1) {
		log_command_frame(CmdFrm);
	}

	if(RspFrm)
		atomic_set(&firesat->avc_reply_received, 0);

	packet=hpsb_make_writepacket(ne->host, ne->nodeid,
				     COMMAND_REGISTER,
				     (quadlet_t*)CmdFrm,
				     CmdFrm->length);
	hpsb_set_packet_complete_task(packet,
				      (void (*)(void*))avc_free_packet,
				      packet);
	hpsb_node_fill_packet(ne, packet);

	if (hpsb_send_packet(packet) < 0) {
		avc_free_packet(packet);
		atomic_set(&firesat->avc_reply_received, 1);
		printk("%s: send failed!\n",__func__);
		return -EIO;
	}

	if(RspFrm) {
		// AV/C specs say that answers should be send within
		// 150 ms so let's time out after 200 ms
		if(avc_down_timeout(&firesat->avc_reply_received,
				    HZ / 5)) {
			printk("%s: timeout waiting for avc response\n",
			       __func__);
			atomic_set(&firesat->avc_reply_received, 1);
			return -ETIMEDOUT;
		}
		memcpy(RspFrm, firesat->respfrm,
		       firesat->resp_length);
		RspFrm->length = firesat->resp_length;
		if (avc_comm_debug == 1) {
			log_response_frame(RspFrm);
		}
	}

	return 0;
}

int AVCWrite(struct firesat*firesat, const AVCCmdFrm *CmdFrm, AVCRspFrm *RspFrm) {
	int ret;
	if(down_interruptible(&firesat->avc_sem))
		return -EINTR;

	ret = __AVCWrite(firesat, CmdFrm, RspFrm);

	up(&firesat->avc_sem);
	return ret;
}

static void do_schedule_remotecontrol(unsigned long ignored);
DECLARE_TASKLET(schedule_remotecontrol, do_schedule_remotecontrol, 0);

static void do_schedule_remotecontrol(unsigned long ignored) {
	struct firesat *firesat;
	unsigned long flags;

	spin_lock_irqsave(&firesat_list_lock, flags);
	list_for_each_entry(firesat,&firesat_list,list) {
		if(atomic_read(&firesat->reschedule_remotecontrol) == 1) {
			if(down_trylock(&firesat->avc_sem))
				tasklet_schedule(&schedule_remotecontrol);
			else {
				if(__AVCRegisterRemoteControl(firesat, 1) == 0)
					atomic_set(&firesat->reschedule_remotecontrol, 0);
				else
					tasklet_schedule(&schedule_remotecontrol);

				up(&firesat->avc_sem);
			}
		}
	}
	spin_unlock_irqrestore(&firesat_list_lock, flags);
}

int AVCRecv(struct firesat *firesat, u8 *data, size_t length) {
//	printk(KERN_INFO "%s\n",__func__);

	// remote control handling

#if 0
	AVCRspFrm *RspFrm = (AVCRspFrm*)data;

	if(/*RspFrm->length >= 8 && ###*/
			((RspFrm->operand[0] == SFE_VENDOR_DE_COMPANYID_0 &&
			RspFrm->operand[1] == SFE_VENDOR_DE_COMPANYID_1 &&
			RspFrm->operand[2] == SFE_VENDOR_DE_COMPANYID_2)) &&
			RspFrm->operand[3] == SFE_VENDOR_OPCODE_REGISTER_REMOTE_CONTROL) {
		if(RspFrm->resp == CHANGED) {
//			printk(KERN_INFO "%s: code = %02x %02x\n",__func__,RspFrm->operand[4],RspFrm->operand[5]);
			firesat_got_remotecontrolcode((((u16)RspFrm->operand[4]) << 8) | ((u16)RspFrm->operand[5]));

			// schedule
			atomic_set(&firesat->reschedule_remotecontrol, 1);
			tasklet_schedule(&schedule_remotecontrol);
		} else if(RspFrm->resp != INTERIM)
			printk(KERN_INFO "%s: remote control result = %d\n",__func__, RspFrm->resp);
		return 0;
	}
#endif
	if(atomic_read(&firesat->avc_reply_received) == 1) {
		printk("%s: received out-of-order AVC response, ignored\n",__func__);
		return -EINVAL;
	}
//	AVCRspFrm *resp=(AVCRspFrm *)data;
//	int k;

//	printk(KERN_INFO "resp=0x%x\n",resp->resp);
//	printk(KERN_INFO "cts=0x%x\n",resp->cts);
//	printk(KERN_INFO "suid=0x%x\n",resp->suid);
//	printk(KERN_INFO "sutyp=0x%x\n",resp->sutyp);
//	printk(KERN_INFO "opcode=0x%x\n",resp->opcode);
//	printk(KERN_INFO "length=%d\n",resp->length);

//	for(k=0;k<2;k++)
//		printk(KERN_INFO "operand[%d]=%02x\n",k,resp->operand[k]);

	memcpy(firesat->respfrm,data,length);
	firesat->resp_length=length;

	atomic_set(&firesat->avc_reply_received, 1);

	return 0;
}

// tuning command for setting the relative LNB frequency (not supported by the AVC standard)
static void AVCTuner_tuneQPSK(struct firesat *firesat, struct dvb_frontend_parameters *params, AVCCmdFrm *CmdFrm) {

	memset(CmdFrm, 0, sizeof(AVCCmdFrm));

	CmdFrm->cts = AVC;
	CmdFrm->ctype = CONTROL;
	CmdFrm->sutyp = 0x5;
	CmdFrm->suid = firesat->subunit;
	CmdFrm->opcode = VENDOR;

	CmdFrm->operand[0]=SFE_VENDOR_DE_COMPANYID_0;
	CmdFrm->operand[1]=SFE_VENDOR_DE_COMPANYID_1;
	CmdFrm->operand[2]=SFE_VENDOR_DE_COMPANYID_2;
	CmdFrm->operand[3]=SFE_VENDOR_OPCODE_TUNE_QPSK;

	printk(KERN_INFO "%s: tuning to frequency %u\n",__func__,params->frequency);

	CmdFrm->operand[4] = (params->frequency >> 24) & 0xFF;
	CmdFrm->operand[5] = (params->frequency >> 16) & 0xFF;
	CmdFrm->operand[6] = (params->frequency >> 8) & 0xFF;
	CmdFrm->operand[7] = params->frequency & 0xFF;

	printk(KERN_INFO "%s: symbol rate = %uBd\n",__func__,params->u.qpsk.symbol_rate);

	CmdFrm->operand[8] = ((params->u.qpsk.symbol_rate/1000) >> 8) & 0xFF;
	CmdFrm->operand[9] = (params->u.qpsk.symbol_rate/1000) & 0xFF;

	switch(params->u.qpsk.fec_inner) {
	case FEC_1_2:
		CmdFrm->operand[10] = 0x1;
		break;
	case FEC_2_3:
		CmdFrm->operand[10] = 0x2;
		break;
	case FEC_3_4:
		CmdFrm->operand[10] = 0x3;
		break;
	case FEC_5_6:
		CmdFrm->operand[10] = 0x4;
		break;
	case FEC_7_8:
		CmdFrm->operand[10] = 0x5;
		break;
	case FEC_4_5:
	case FEC_8_9:
	case FEC_AUTO:
	default:
		CmdFrm->operand[10] = 0x0;
	}

	if(firesat->voltage == 0xff)
		CmdFrm->operand[11] = 0xff;
	else
		CmdFrm->operand[11] = (firesat->voltage==SEC_VOLTAGE_18)?0:1; // polarisation
	if(firesat->tone == 0xff)
		CmdFrm->operand[12] = 0xff;
	else
		CmdFrm->operand[12] = (firesat->tone==SEC_TONE_ON)?1:0; // band

	if (firesat->type == FireSAT_DVB_S2) {
		CmdFrm->operand[13] = 0x1;
		CmdFrm->operand[14] = 0xFF;
		CmdFrm->operand[15] = 0xFF;
	}

	CmdFrm->length = 16;
}

int AVCTuner_DSD(struct firesat *firesat, struct dvb_frontend_parameters *params, __u8 *status) {
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	M_VALID_FLAGS flags;
	int k;

//	printk(KERN_INFO "%s\n", __func__);

	if (firesat->type == FireSAT_DVB_S || firesat->type == FireSAT_DVB_S2)
		AVCTuner_tuneQPSK(firesat, params, &CmdFrm);
	else {
		if(firesat->type == FireSAT_DVB_T) {
			flags.Bits_T.GuardInterval = (params->u.ofdm.guard_interval != GUARD_INTERVAL_AUTO);
			flags.Bits_T.CodeRateLPStream = (params->u.ofdm.code_rate_LP != FEC_AUTO);
			flags.Bits_T.CodeRateHPStream = (params->u.ofdm.code_rate_HP != FEC_AUTO);
			flags.Bits_T.HierarchyInfo = (params->u.ofdm.hierarchy_information != HIERARCHY_AUTO);
			flags.Bits_T.Constellation = (params->u.ofdm.constellation != QAM_AUTO);
			flags.Bits_T.Bandwidth = (params->u.ofdm.bandwidth != BANDWIDTH_AUTO);
			flags.Bits_T.CenterFrequency = 1;
			flags.Bits_T.reserved1 = 0;
			flags.Bits_T.reserved2 = 0;
			flags.Bits_T.OtherFrequencyFlag = 0;
			flags.Bits_T.TransmissionMode = (params->u.ofdm.transmission_mode != TRANSMISSION_MODE_AUTO);
			flags.Bits_T.NetworkId = 0;
		} else {
			flags.Bits.Modulation =
				(params->u.qam.modulation != QAM_AUTO);
			flags.Bits.FEC_inner =
				(params->u.qam.fec_inner != FEC_AUTO);
			flags.Bits.FEC_outer = 0;
			flags.Bits.Symbol_Rate = 1;
			flags.Bits.Frequency = 1;
			flags.Bits.Orbital_Pos = 0;
			flags.Bits.Polarisation = 0;
			flags.Bits.reserved_fields = 0;
			flags.Bits.reserved1 = 0;
			flags.Bits.Network_ID = 0;
		}

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
		// system_specific_multiplex selection_length
		CmdFrm.operand[4]  = (firesat->type == FireSAT_DVB_T)?0x0c:0x11;
		CmdFrm.operand[5]  = flags.Valid_Word.ByteHi; // valid_flags [0]
		CmdFrm.operand[6]  = flags.Valid_Word.ByteLo; // valid_flags [1]

		if(firesat->type == FireSAT_DVB_T) {
			CmdFrm.operand[7]  = 0x0;
			CmdFrm.operand[8]  = (params->frequency/10) >> 24;
			CmdFrm.operand[9]  =
				((params->frequency/10) >> 16) & 0xFF;
			CmdFrm.operand[10] =
				((params->frequency/10) >>  8) & 0xFF;
			CmdFrm.operand[11] = (params->frequency/10) & 0xFF;
			switch(params->u.ofdm.bandwidth) {
			case BANDWIDTH_7_MHZ:
				CmdFrm.operand[12] = 0x20;
				break;
			case BANDWIDTH_8_MHZ:
			case BANDWIDTH_6_MHZ: // not defined by AVC spec
			case BANDWIDTH_AUTO:
			default:
				CmdFrm.operand[12] = 0x00;
			}
			switch(params->u.ofdm.constellation) {
			case QAM_16:
				CmdFrm.operand[13] = 1 << 6;
				break;
			case QAM_64:
				CmdFrm.operand[13] = 2 << 6;
				break;
			case QPSK:
			default:
				CmdFrm.operand[13] = 0x00;
			}
			switch(params->u.ofdm.hierarchy_information) {
			case HIERARCHY_1:
				CmdFrm.operand[13] |= 1 << 3;
				break;
			case HIERARCHY_2:
				CmdFrm.operand[13] |= 2 << 3;
				break;
			case HIERARCHY_4:
				CmdFrm.operand[13] |= 3 << 3;
				break;
			case HIERARCHY_AUTO:
			case HIERARCHY_NONE:
			default:
				break;
			}
			switch(params->u.ofdm.code_rate_HP) {
			case FEC_2_3:
				CmdFrm.operand[13] |= 1;
				break;
			case FEC_3_4:
				CmdFrm.operand[13] |= 2;
				break;
			case FEC_5_6:
				CmdFrm.operand[13] |= 3;
				break;
			case FEC_7_8:
				CmdFrm.operand[13] |= 4;
				break;
			case FEC_1_2:
			default:
				break;
			}
			switch(params->u.ofdm.code_rate_LP) {
			case FEC_2_3:
				CmdFrm.operand[14] = 1 << 5;
				break;
			case FEC_3_4:
				CmdFrm.operand[14] = 2 << 5;
				break;
			case FEC_5_6:
				CmdFrm.operand[14] = 3 << 5;
				break;
			case FEC_7_8:
				CmdFrm.operand[14] = 4 << 5;
				break;
			case FEC_1_2:
			default:
				CmdFrm.operand[14] = 0x00;
				break;
			}
			switch(params->u.ofdm.guard_interval) {
			case GUARD_INTERVAL_1_16:
				CmdFrm.operand[14] |= 1 << 3;
				break;
			case GUARD_INTERVAL_1_8:
				CmdFrm.operand[14] |= 2 << 3;
				break;
			case GUARD_INTERVAL_1_4:
				CmdFrm.operand[14] |= 3 << 3;
				break;
			case GUARD_INTERVAL_1_32:
			case GUARD_INTERVAL_AUTO:
			default:
				break;
			}
			switch(params->u.ofdm.transmission_mode) {
			case TRANSMISSION_MODE_8K:
				CmdFrm.operand[14] |= 1 << 1;
				break;
			case TRANSMISSION_MODE_2K:
			case TRANSMISSION_MODE_AUTO:
			default:
				break;
			}

			CmdFrm.operand[15] = 0x00; // network_ID[0]
			CmdFrm.operand[16] = 0x00; // network_ID[1]
			CmdFrm.operand[17] = 0x00; // Nr_of_dsd_sel_specs = 0 - > No PIDs are transmitted

			CmdFrm.length = 24;
		} else {
			CmdFrm.operand[7]  = 0x00;
			CmdFrm.operand[8]  = 0x00;
			CmdFrm.operand[9]  = 0x00;
			CmdFrm.operand[10] = 0x00;

			CmdFrm.operand[11] =
				(((params->frequency/4000) >> 16) & 0xFF) | (2 << 6);
			CmdFrm.operand[12] =
				((params->frequency/4000) >> 8) & 0xFF;
			CmdFrm.operand[13] = (params->frequency/4000) & 0xFF;
			CmdFrm.operand[14] =
				((params->u.qpsk.symbol_rate/1000) >> 12) & 0xFF;
			CmdFrm.operand[15] =
				((params->u.qpsk.symbol_rate/1000) >> 4) & 0xFF;
			CmdFrm.operand[16] =
				((params->u.qpsk.symbol_rate/1000) << 4) & 0xF0;
			CmdFrm.operand[17] = 0x00;
			switch(params->u.qpsk.fec_inner) {
			case FEC_1_2:
				CmdFrm.operand[18] = 0x1;
				break;
			case FEC_2_3:
				CmdFrm.operand[18] = 0x2;
				break;
			case FEC_3_4:
				CmdFrm.operand[18] = 0x3;
				break;
			case FEC_5_6:
				CmdFrm.operand[18] = 0x4;
				break;
			case FEC_7_8:
				CmdFrm.operand[18] = 0x5;
				break;
			case FEC_8_9:
				CmdFrm.operand[18] = 0x6;
				break;
			case FEC_4_5:
				CmdFrm.operand[18] = 0x8;
				break;
			case FEC_AUTO:
			default:
				CmdFrm.operand[18] = 0x0;
			}
			switch(params->u.qam.modulation) {
			case QAM_16:
				CmdFrm.operand[19] = 0x08; // modulation
				break;
			case QAM_32:
				CmdFrm.operand[19] = 0x10; // modulation
				break;
			case QAM_64:
				CmdFrm.operand[19] = 0x18; // modulation
				break;
			case QAM_128:
				CmdFrm.operand[19] = 0x20; // modulation
				break;
			case QAM_256:
				CmdFrm.operand[19] = 0x28; // modulation
				break;
			case QAM_AUTO:
			default:
				CmdFrm.operand[19] = 0x00; // modulation
			}
			CmdFrm.operand[20] = 0x00;
			CmdFrm.operand[21] = 0x00;
			CmdFrm.operand[22] = 0x00; // Nr_of_dsd_sel_specs = 0 - > No PIDs are transmitted

			CmdFrm.length=28;
		}
	} // AVCTuner_DSD_direct

	if((k=AVCWrite(firesat,&CmdFrm,&RspFrm)))
		return k;

	mdelay(500);

	if(status)
		*status=RspFrm.operand[2];
	return 0;
}

int AVCTuner_SetPIDs(struct firesat *firesat, unsigned char pidc, u16 pid[])
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int pos,k;

	if(pidc > 16 && pidc != 0xFF)
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

	pos=6;
	if(pidc != 0xFF) {
		for(k=0;k<pidc;k++) {
			CmdFrm.operand[pos++] = 0x13; // flowfunction relay
			CmdFrm.operand[pos++] = 0x80; // dsd_sel_spec_valid_flags -> PID
			CmdFrm.operand[pos++] = (pid[k] >> 8) & 0x1F;
			CmdFrm.operand[pos++] = pid[k] & 0xFF;
			CmdFrm.operand[pos++] = 0x00; // tableID
			CmdFrm.operand[pos++] = 0x00; // filter_length
		}
	}

	CmdFrm.length = pos+3;
	if((pos+3)%4)
		CmdFrm.length += 4 - ((pos+3)%4);

	if((k=AVCWrite(firesat,&CmdFrm,&RspFrm)))
		return k;

	mdelay(50);
	return 0;
}

int AVCTuner_GetTS(struct firesat *firesat){
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int k;

	//printk(KERN_INFO "%s\n", __func__);

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

	if ((k=AVCWrite(firesat, &CmdFrm, &RspFrm)))
		return k;

	mdelay(250);
	return 0;
}

int AVCIdentifySubunit(struct firesat *firesat, unsigned char *systemId, int *transport) {
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

	if(AVCWrite(firesat,&CmdFrm,&RspFrm)<0)
		return -EIO;

	if(RspFrm.resp != STABLE && RspFrm.resp != ACCEPTED) {
		printk("%s: AVCWrite returned error code %d\n",__func__,RspFrm.resp);
		return -EINVAL;
	}
	if(((RspFrm.operand[3] << 8) + RspFrm.operand[4]) != 8) {
		printk("%s: Invalid response length\n",__func__);
		return -EINVAL;
	}
	if(systemId)
		*systemId = RspFrm.operand[7];
	return 0;
}

int AVCTunerStatus(struct firesat *firesat, ANTENNA_INPUT_INFO *antenna_input_info) {
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
	if (AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	if(RspFrm.resp != STABLE && RspFrm.resp != ACCEPTED) {
		printk("%s: AVCWrite returned code %d\n",__func__,RspFrm.resp);
		return -EINVAL;
	}

	length = RspFrm.operand[9];
	if(RspFrm.operand[1] == 0x10 && length == sizeof(ANTENNA_INPUT_INFO))
	{
		memcpy(antenna_input_info, &RspFrm.operand[10],
		       sizeof(ANTENNA_INPUT_INFO));
		return 0;
	}
	printk("%s: invalid info returned from AVC\n",__func__);
	return -EINVAL;
}

int AVCLNBControl(struct firesat *firesat, char voltage, char burst,
		  char conttone, char nrdiseq,
		  struct dvb_diseqc_master_cmd *diseqcmd)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int i,j;

	printk(KERN_INFO "%s: voltage = %x, burst = %x, conttone = %x\n",__func__,voltage,burst,conttone);

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

	for(j=0;j<nrdiseq;j++) {
		int k;
		printk(KERN_INFO "%s: diseq %d len %x\n",__func__,j,diseqcmd[j].msg_len);
		CmdFrm.operand[i++]=diseqcmd[j].msg_len;

		for(k=0;k<diseqcmd[j].msg_len;k++) {
			printk(KERN_INFO "%s: diseq %d msg[%d] = %x\n",__func__,j,k,diseqcmd[j].msg[k]);
			CmdFrm.operand[i++]=diseqcmd[j].msg[k];
		}
	}

	CmdFrm.operand[i++]=burst;
	CmdFrm.operand[i++]=conttone;

	CmdFrm.length=i+3;
	if((i+3)%4)
		CmdFrm.length += 4 - ((i+3)%4);

/*	for(j=0;j<CmdFrm.length;j++)
		printk(KERN_INFO "%s: CmdFrm.operand[%d]=0x%x\n",__func__,j,CmdFrm.operand[j]);

	printk(KERN_INFO "%s: cmdfrm.length = %u\n",__func__,CmdFrm.length);
	*/
	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	if(RspFrm.resp != ACCEPTED) {
		printk("%s: AVCWrite returned code %d\n",__func__,RspFrm.resp);
		return -EINVAL;
	}

	return 0;
}

int AVCSubUnitInfo(struct firesat *firesat, char *subunitcount)
{
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;

	memset(&CmdFrm, 0, sizeof(AVCCmdFrm));

	CmdFrm.cts = AVC;
	CmdFrm.ctype = STATUS;
	CmdFrm.sutyp = 0x1f;
	CmdFrm.suid = 0x7;
	CmdFrm.opcode = SUBUNIT_Info;

	CmdFrm.operand[0] = 0x07;
	CmdFrm.operand[1] = 0xff;
	CmdFrm.operand[2] = 0xff;
	CmdFrm.operand[3] = 0xff;
	CmdFrm.operand[4] = 0xff;

	CmdFrm.length = 8;

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	if(RspFrm.resp != STABLE) {
		printk("%s: AVCWrite returned code %d\n",__func__,RspFrm.resp);
		return -EINVAL;
	}

	if(subunitcount)
		*subunitcount = (RspFrm.operand[1] & 0x7) + 1;

	return 0;
}

static int __AVCRegisterRemoteControl(struct firesat*firesat, int internal)
{
	AVCCmdFrm CmdFrm;

//	printk(KERN_INFO "%s\n",__func__);

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

	if(internal) {
		if(__AVCWrite(firesat,&CmdFrm,NULL) < 0)
			return -EIO;
	} else
		if(AVCWrite(firesat,&CmdFrm,NULL) < 0)
			return -EIO;

	return 0;
}

int AVCRegisterRemoteControl(struct firesat*firesat)
{
	return __AVCRegisterRemoteControl(firesat, 0);
}

int AVCTuner_Host2Ca(struct firesat *firesat)
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

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	return 0;
}

static int get_ca_object_pos(AVCRspFrm *RspFrm)
{
	int length = 1;

	// Check length of length field
	if (RspFrm->operand[7] & 0x80)
		length = (RspFrm->operand[7] & 0x7F) + 1;
	return length + 7;
}

static int get_ca_object_length(AVCRspFrm *RspFrm)
{
	int size = 0;
	int i;

	if (RspFrm->operand[7] & 0x80) {
		for (i = 0; i < (RspFrm->operand[7] & 0x7F); i++) {
			size <<= 8;
			size += RspFrm->operand[8 + i];
		}
	}
	return RspFrm->operand[7];
}

int avc_ca_app_info(struct firesat *firesat, char *app_info, int *length)
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

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;


	pos = get_ca_object_pos(&RspFrm);
	app_info[0] = (TAG_APP_INFO >> 16) & 0xFF;
	app_info[1] = (TAG_APP_INFO >> 8) & 0xFF;
	app_info[2] = (TAG_APP_INFO >> 0) & 0xFF;
	app_info[3] = 6 + RspFrm.operand[pos + 4];
	app_info[4] = 0x01;
	memcpy(&app_info[5], &RspFrm.operand[pos], 5 + RspFrm.operand[pos + 4]);
	*length = app_info[3] + 4;

	return 0;
}

int avc_ca_info(struct firesat *firesat, char *app_info, int *length)
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

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	pos = get_ca_object_pos(&RspFrm);
	app_info[0] = (TAG_CA_INFO >> 16) & 0xFF;
	app_info[1] = (TAG_CA_INFO >> 8) & 0xFF;
	app_info[2] = (TAG_CA_INFO >> 0) & 0xFF;
	app_info[3] = 2;
	app_info[4] = app_info[5];
	app_info[5] = app_info[6];
	*length = app_info[3] + 4;

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

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
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
		printk(KERN_ERR "The only list_manasgement parameter that is "
		       "supported by the firesat driver is \"only\" (3).");
		return -EFAULT;
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
/* 		printk(KERN_INFO "Copying descriptors at programme level.\n"); */
		pmt_cmd_id = msg[read_pos++];
		if (pmt_cmd_id != 1 && pmt_cmd_id !=4) {
			printk(KERN_ERR "Invalid pmt_cmd_id=%d.\n",
			       pmt_cmd_id);
		}
		memcpy(&CmdFrm.operand[write_pos], &msg[read_pos],
		       program_info_length);
		read_pos += program_info_length;
		write_pos += program_info_length;
	}
	while (read_pos < length) {
/* 		printk(KERN_INFO "Copying descriptors at stream level for " */
/* 		       "stream type %d.\n", msg[read_pos]); */
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
			if (pmt_cmd_id != 1 && pmt_cmd_id !=4) {
				printk(KERN_ERR "Invalid pmt_cmd_id=%d at "
				       "stream level.\n", pmt_cmd_id);
			}
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

	CmdFrm.length = write_pos + 3;
	if ((write_pos + 3) % 4)
		CmdFrm.length += 4 - ((write_pos + 3) % 4);

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	if (RspFrm.resp != ACCEPTED) {
		printk(KERN_ERR "Answer to CA PMT was %d\n", RspFrm.resp);
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

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

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

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	return 0;
}

int avc_ca_get_mmi(struct firesat *firesat, char *mmi_object, int *length)
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

	if(AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	*length = get_ca_object_length(&RspFrm);
	memcpy(mmi_object, &RspFrm.operand[get_ca_object_pos(&RspFrm)], *length);

	return 0;
}
