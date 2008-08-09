/*
 * FireSAT AVC driver
 *
 * Copyright (c) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (c) 2008 Ben Backx <ben@bbackx.com>
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
#include "avc_api.h"
#include "firesat-rc.h"

#define RESPONSE_REGISTER				0xFFFFF0000D00ULL
#define COMMAND_REGISTER				0xFFFFF0000B00ULL
#define PCR_BASE_ADDRESS				0xFFFFF0000900ULL

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

static int __AVCWrite(struct firesat *firesat, const AVCCmdFrm *CmdFrm, AVCRspFrm *RspFrm) {
	struct hpsb_packet *packet;
	struct node_entry *ne;

	ne = firesat->nodeentry;
	if(!ne) {
		printk("%s: lost node!\n",__func__);
		return -EIO;
	}

	/* need all input data */
	if(!firesat || !ne || !CmdFrm)
		return -EINVAL;

//	printk(KERN_INFO "AVCWrite command %x\n",CmdFrm->opcode);

//	for(k=0;k<CmdFrm->length;k++)
//		printk(KERN_INFO "CmdFrm[%d] = %08x\n", k, ((quadlet_t*)CmdFrm)[k]);

	packet=hpsb_make_writepacket(ne->host, ne->nodeid, COMMAND_REGISTER,
			(quadlet_t*)CmdFrm, CmdFrm->length);

	hpsb_set_packet_complete_task(packet, (void (*)(void*))avc_free_packet,
				  packet);

	hpsb_node_fill_packet(ne, packet);

	if(RspFrm)
		atomic_set(&firesat->avc_reply_received, 0);

	if (hpsb_send_packet(packet) < 0) {
		avc_free_packet(packet);
		atomic_set(&firesat->avc_reply_received, 1);
		return -EIO;
	}

	if(RspFrm) {
		if(avc_down_timeout(&firesat->avc_reply_received,HZ/2)) {
		printk("%s: timeout waiting for avc response\n",__func__);
			atomic_set(&firesat->avc_reply_received, 1);
		return -ETIMEDOUT;
	}

		memcpy(RspFrm,firesat->respfrm,firesat->resp_length);
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

	if(atomic_read(&firesat->avc_reply_received) == 1) {
		printk("%s: received out-of-order AVC response, ignored\n",__func__);
		return -EINVAL;
	}
//	AVCRspFrm *resp=(AVCRspFrm *)data;
//	int k;
/*
	printk(KERN_INFO "resp=0x%x\n",resp->resp);
	printk(KERN_INFO "cts=0x%x\n",resp->cts);
	printk(KERN_INFO "suid=0x%x\n",resp->suid);
	printk(KERN_INFO "sutyp=0x%x\n",resp->sutyp);
	printk(KERN_INFO "opcode=0x%x\n",resp->opcode);
	printk(KERN_INFO "length=%d\n",resp->length);
*/
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

int AVCTuner_DSD(struct firesat *firesat, struct dvb_frontend_parameters *params, BYTE *status) {
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
			flags.Bits.Modulation = 0;
			if(firesat->type == FireSAT_DVB_S) {
				flags.Bits.FEC_inner = 1;
			} else if(firesat->type == FireSAT_DVB_C) {
				flags.Bits.FEC_inner = 0;
			}
			flags.Bits.FEC_outer = 0;
			flags.Bits.Symbol_Rate = 1;
			flags.Bits.Frequency = 1;
			flags.Bits.Orbital_Pos = 0;
			if(firesat->type == FireSAT_DVB_S) {
				flags.Bits.Polarisation = 1;
			} else if(firesat->type == FireSAT_DVB_C) {
				flags.Bits.Polarisation = 0;
			}
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
		CmdFrm.operand[4]  = (firesat->type == FireSAT_DVB_T)?0x0c:0x11; // system_specific_multiplex selection_length
		CmdFrm.operand[5]  = flags.Valid_Word.ByteHi; // valid_flags [0]
		CmdFrm.operand[6]  = flags.Valid_Word.ByteLo; // valid_flags [1]

		if(firesat->type == FireSAT_DVB_T) {
			CmdFrm.operand[7]  = 0x0;
			CmdFrm.operand[8]  = (params->frequency/10) >> 24;
			CmdFrm.operand[9]  = ((params->frequency/10) >> 16) & 0xFF;
			CmdFrm.operand[10] = ((params->frequency/10) >>  8) & 0xFF;
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

			CmdFrm.length = 20;
		} else {
			CmdFrm.operand[7]  = 0x00;
			CmdFrm.operand[8]  = (((firesat->voltage==SEC_VOLTAGE_18)?0:1)<<6); /* 0 = H, 1 = V */
			CmdFrm.operand[9]  = 0x00;
			CmdFrm.operand[10] = 0x00;

			if(firesat->type == FireSAT_DVB_S) {
				/* ### relative frequency -> absolute frequency */
				CmdFrm.operand[11] = (((params->frequency/4) >> 16) & 0xFF) | (2 << 6);
				CmdFrm.operand[12] = ((params->frequency/4) >> 8) & 0xFF;
				CmdFrm.operand[13] = (params->frequency/4) & 0xFF;
			} else if(firesat->type == FireSAT_DVB_C) {
				CmdFrm.operand[11] = (((params->frequency/4000) >> 16) & 0xFF) | (2 << 6);
				CmdFrm.operand[12] = ((params->frequency/4000) >> 8) & 0xFF;
				CmdFrm.operand[13] = (params->frequency/4000) & 0xFF;
			}

			CmdFrm.operand[14] = ((params->u.qpsk.symbol_rate/1000) >> 12) & 0xFF;
			CmdFrm.operand[15] = ((params->u.qpsk.symbol_rate/1000) >> 4) & 0xFF;
			CmdFrm.operand[16] = ((params->u.qpsk.symbol_rate/1000) << 4) & 0xF0;

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
			case FEC_4_5:
			case FEC_8_9:
			case FEC_AUTO:
			default:
				CmdFrm.operand[18] = 0x0;
			}
			if(firesat->type == FireSAT_DVB_S) {
				CmdFrm.operand[19] = 0x08; // modulation
			} else if(firesat->type == FireSAT_DVB_C) {
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
			}
			CmdFrm.operand[20] = 0x00;
			CmdFrm.operand[21] = 0x00;
			CmdFrm.operand[22] = 0x00; // Nr_of_dsd_sel_specs = 0 - > No PIDs are transmitted

			CmdFrm.length=28;
		}
	} // AVCTuner_DSD_direct

	if((k=AVCWrite(firesat,&CmdFrm,&RspFrm)))
		return k;

//	msleep(250);
	mdelay(500);

	if(status)
		*status=RspFrm.operand[2];
	return 0;
}

int AVCTuner_SetPIDs(struct firesat *firesat, unsigned char pidc, u16 pid[]) {
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int pos,k;

	printk(KERN_INFO "%s\n", __func__);

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
	CmdFrm.operand[4]  = 0x11; // system_specific_multiplex selection_length
	CmdFrm.operand[5]  = 0x00; // valid_flags [0]
	CmdFrm.operand[6]  = 0x00; // valid_flags [1]

	if(firesat->type == FireSAT_DVB_T) {
/*		CmdFrm.operand[7]  = 0x00;
		CmdFrm.operand[8]  = 0x00;//(params->frequency/10) >> 24;
		CmdFrm.operand[9]  = 0x00;//((params->frequency/10) >> 16) & 0xFF;
		CmdFrm.operand[10] = 0x00;//((params->frequency/10) >>  8) & 0xFF;
		CmdFrm.operand[11] = 0x00;//(params->frequency/10) & 0xFF;
		CmdFrm.operand[12] = 0x00;
		CmdFrm.operand[13] = 0x00;
		CmdFrm.operand[14] = 0x00;

		CmdFrm.operand[15] = 0x00; // network_ID[0]
		CmdFrm.operand[16] = 0x00; // network_ID[1]
*/		CmdFrm.operand[17] = pidc; // Nr_of_dsd_sel_specs

		pos=18;
	} else {
/*		CmdFrm.operand[7]  = 0x00;
		CmdFrm.operand[8]  = 0x00;
		CmdFrm.operand[9]  = 0x00;
		CmdFrm.operand[10] = 0x00;

		CmdFrm.operand[11] = 0x00;//(((params->frequency/4) >> 16) & 0xFF) | (2 << 6);
		CmdFrm.operand[12] = 0x00;//((params->frequency/4) >> 8) & 0xFF;
		CmdFrm.operand[13] = 0x00;//(params->frequency/4) & 0xFF;

		CmdFrm.operand[14] = 0x00;//((params->u.qpsk.symbol_rate/1000) >> 12) & 0xFF;
		CmdFrm.operand[15] = 0x00;//((params->u.qpsk.symbol_rate/1000) >> 4) & 0xFF;
		CmdFrm.operand[16] = 0x00;//((params->u.qpsk.symbol_rate/1000) << 4) & 0xF0;

		CmdFrm.operand[17] = 0x00;
		CmdFrm.operand[18] = 0x00;
		CmdFrm.operand[19] = 0x00; // modulation
		CmdFrm.operand[20] = 0x00;
		CmdFrm.operand[21] = 0x00;*/
		CmdFrm.operand[22] = pidc; // Nr_of_dsd_sel_specs

		pos=23;
	}
	if(pidc != 0xFF)
		for(k=0;k<pidc;k++) {
			CmdFrm.operand[pos++] = 0x13; // flowfunction relay
			CmdFrm.operand[pos++] = 0x80; // dsd_sel_spec_valid_flags -> PID
			CmdFrm.operand[pos++] = (pid[k] >> 8) & 0x1F;
			CmdFrm.operand[pos++] = pid[k] & 0xFF;
			CmdFrm.operand[pos++] = 0x00; // tableID
			CmdFrm.operand[pos++] = 0x00; // filter_length
		}

	CmdFrm.length = pos+3;

	if((pos+3)%4)
		CmdFrm.length += 4 - ((pos+3)%4);

	if((k=AVCWrite(firesat,&CmdFrm,&RspFrm)))
		return k;

	mdelay(250);

	return 0;
}

int AVCTuner_GetTS(struct firesat *firesat){
	AVCCmdFrm CmdFrm;
	AVCRspFrm RspFrm;
	int k;

	printk(KERN_INFO "%s\n", __func__);

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
	CmdFrm.operand[6]  = 0x11; // system_specific_multiplex selection_length
	CmdFrm.operand[7]  = 0x00; // valid_flags [0]
	CmdFrm.operand[8]  = 0x00; // valid_flags [1]
	CmdFrm.operand[24] = 0x00; // nr_of_dsit_sel_specs (always 0)

	CmdFrm.length = 28;

	if((k=AVCWrite(firesat, &CmdFrm, &RspFrm))) return k;

	mdelay(250);
	return 0;
}

int AVCIdentifySubunit(struct firesat *firesat, unsigned char *systemId, int *transport, int *has_ci) {
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
	if(has_ci)
		*has_ci = (RspFrm.operand[14] >> 4) & 0x1;
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
	CmdFrm.operand[1]=0xff;
	CmdFrm.operand[2]=0x00;
	CmdFrm.operand[3]=sizeof(ANTENNA_INPUT_INFO) >> 8;
	CmdFrm.operand[4]=sizeof(ANTENNA_INPUT_INFO) & 0xFF;
	CmdFrm.operand[5]=0x00;
	CmdFrm.operand[6]=0x03;
	CmdFrm.length=12;
	//Absenden des AVC request und warten auf response
	if (AVCWrite(firesat,&CmdFrm,&RspFrm) < 0)
		return -EIO;

	if(RspFrm.resp != STABLE && RspFrm.resp != ACCEPTED) {
		printk("%s: AVCWrite returned code %d\n",__func__,RspFrm.resp);
		return -EINVAL;
	}

	length = (RspFrm.operand[3] << 8) + RspFrm.operand[4];
	if(length == sizeof(ANTENNA_INPUT_INFO))
	{
		memcpy(antenna_input_info,&RspFrm.operand[7],length);
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
