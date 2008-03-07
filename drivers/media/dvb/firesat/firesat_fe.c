#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <ieee1394_hotplug.h>
#include <nodemgr.h>
#include <highlevel.h>
#include <ohci1394.h>
#include <hosts.h>
#include <dvbdev.h>

#include "firesat.h"
#include "avc_api.h"
#include "cmp.h"
#include "firesat-rc.h"
#include "firesat-ci.h"

static int firesat_dvb_init(struct dvb_frontend *fe)
{
	struct firesat *firesat = fe->sec_priv;
	printk("fdi: 1\n");
	firesat->isochannel = firesat->adapter->num; //<< 1 | (firesat->subunit & 0x1); // ### ask IRM
	printk("fdi: 2\n");
	try_CMPEstablishPPconnection(firesat, firesat->subunit, firesat->isochannel);
	printk("fdi: 3\n");
//FIXME	hpsb_listen_channel(&firesat_highlevel, firesat->host, firesat->isochannel);
	printk("fdi: 4\n");
	return 0;
}

static int firesat_sleep(struct dvb_frontend *fe)
{
	struct firesat *firesat = fe->sec_priv;

//FIXME	hpsb_unlisten_channel(&firesat_highlevel, firesat->host, firesat->isochannel);
	try_CMPBreakPPconnection(firesat, firesat->subunit, firesat->isochannel);
	firesat->isochannel = -1;
	return 0;
}

static int firesat_diseqc_send_master_cmd(struct dvb_frontend *fe,
					  struct dvb_diseqc_master_cmd *cmd)
{
	struct firesat *firesat = fe->sec_priv;

	return AVCLNBControl(firesat, LNBCONTROL_DONTCARE, LNBCONTROL_DONTCARE,
			     LNBCONTROL_DONTCARE, 1, cmd);
}

static int firesat_diseqc_send_burst(struct dvb_frontend *fe,
				     fe_sec_mini_cmd_t minicmd)
{
	return 0;
}

static int firesat_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct firesat *firesat = fe->sec_priv;

	firesat->tone = tone;
	return 0;
}

static int firesat_set_voltage(struct dvb_frontend *fe,
			       fe_sec_voltage_t voltage)
{
	struct firesat *firesat = fe->sec_priv;

	firesat->voltage = voltage;
	return 0;
}

static int firesat_read_status (struct dvb_frontend *fe, fe_status_t *status)
{
	struct firesat *firesat = fe->sec_priv;
	ANTENNA_INPUT_INFO info;

	if (AVCTunerStatus(firesat, &info))
		return -EINVAL;

	if (info.NoRF)
		*status = 0;
	else
		*status = *status = FE_HAS_SIGNAL	|
				    FE_HAS_VITERBI	|
				    FE_HAS_SYNC		|
				    FE_HAS_CARRIER	|
				    FE_HAS_LOCK;

	return 0;
}

static int firesat_read_ber (struct dvb_frontend *fe, u32 *ber)
{
	struct firesat *firesat = fe->sec_priv;
	ANTENNA_INPUT_INFO info;

	if (AVCTunerStatus(firesat, &info))
		return -EINVAL;

	*ber = ((info.BER[0] << 24) & 0xff)	|
	       ((info.BER[1] << 16) & 0xff)	|
	       ((info.BER[2] << 8) & 0xff)	|
		(info.BER[3] & 0xff);

	return 0;
}

static int firesat_read_signal_strength (struct dvb_frontend *fe, u16 *strength)
{
	struct firesat *firesat = fe->sec_priv;
	ANTENNA_INPUT_INFO info;
	u16 *signal = strength;

	if (AVCTunerStatus(firesat, &info))
		return -EINVAL;

	*signal = info.SignalStrength;

	return 0;
}

static int firesat_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	return -EOPNOTSUPP;
}

static int firesat_read_uncorrected_blocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	return -EOPNOTSUPP;
}

static int firesat_set_frontend(struct dvb_frontend *fe,
				struct dvb_frontend_parameters *params)
{
	struct firesat *firesat = fe->sec_priv;

	if (AVCTuner_DSD(firesat, params, NULL) != ACCEPTED)
		return -EINVAL;
	else
		return 0; //not sure of this...
}

static int firesat_get_frontend(struct dvb_frontend *fe,
				struct dvb_frontend_parameters *params)
{
	return -EOPNOTSUPP;
}

static struct dvb_frontend_info firesat_S_frontend_info;
static struct dvb_frontend_info firesat_C_frontend_info;
static struct dvb_frontend_info firesat_T_frontend_info;

static struct dvb_frontend_ops firesat_ops = {

	.init				= firesat_dvb_init,
	.sleep				= firesat_sleep,

	.set_frontend			= firesat_set_frontend,
	.get_frontend			= firesat_get_frontend,

	.read_status			= firesat_read_status,
	.read_ber			= firesat_read_ber,
	.read_signal_strength		= firesat_read_signal_strength,
	.read_snr			= firesat_read_snr,
	.read_ucblocks			= firesat_read_uncorrected_blocks,

	.diseqc_send_master_cmd 	= firesat_diseqc_send_master_cmd,
	.diseqc_send_burst		= firesat_diseqc_send_burst,
	.set_tone			= firesat_set_tone,
	.set_voltage			= firesat_set_voltage,
};

int firesat_frontend_attach(struct firesat *firesat, struct dvb_frontend *fe)
{
	switch (firesat->type) {
	case FireSAT_DVB_S:
		firesat->model_name = "FireSAT DVB-S";
		firesat->frontend_info = &firesat_S_frontend_info;
		break;
	case FireSAT_DVB_C:
		firesat->model_name = "FireSAT DVB-C";
		firesat->frontend_info = &firesat_C_frontend_info;
		break;
	case FireSAT_DVB_T:
		firesat->model_name = "FireSAT DVB-T";
		firesat->frontend_info = &firesat_T_frontend_info;
		break;
	default:
//		printk("%s: unknown model type 0x%x on subunit %d!\n",
//			__func__, firesat->type,subunit);
		printk("%s: unknown model type 0x%x !\n",
			__func__, firesat->type);
		firesat->model_name = "Unknown";
		firesat->frontend_info = NULL;
	}
	fe->ops = firesat_ops;
	fe->dvb = firesat->adapter;

	return 0;
}

static struct dvb_frontend_info firesat_S_frontend_info = {

	.name			= "FireSAT DVB-S Frontend",
	.type			= FE_QPSK,

	.frequency_min		= 950000,
	.frequency_max		= 2150000,
	.frequency_stepsize	= 125,
	.symbol_rate_min	= 1000000,
	.symbol_rate_max	= 40000000,

	.caps 			= FE_CAN_INVERSION_AUTO		|
				  FE_CAN_FEC_1_2		|
				  FE_CAN_FEC_2_3		|
				  FE_CAN_FEC_3_4		|
				  FE_CAN_FEC_5_6		|
				  FE_CAN_FEC_7_8		|
				  FE_CAN_FEC_AUTO		|
				  FE_CAN_QPSK,
};

static struct dvb_frontend_info firesat_C_frontend_info = {

	.name			= "FireSAT DVB-C Frontend",
	.type			= FE_QAM,

	.frequency_min		= 47000000,
	.frequency_max		= 866000000,
	.frequency_stepsize	= 62500,
	.symbol_rate_min	= 870000,
	.symbol_rate_max	= 6900000,

	.caps 			= FE_CAN_INVERSION_AUTO 	|
				  FE_CAN_QAM_16			|
				  FE_CAN_QAM_32			|
				  FE_CAN_QAM_64			|
				  FE_CAN_QAM_128		|
				  FE_CAN_QAM_256		|
				  FE_CAN_QAM_AUTO,
};

static struct dvb_frontend_info firesat_T_frontend_info = {

	.name			= "FireSAT DVB-T Frontend",
	.type			= FE_OFDM,

	.frequency_min		= 49000000,
	.frequency_max		= 861000000,
	.frequency_stepsize	= 62500,

	.caps 			= FE_CAN_INVERSION_AUTO		|
				  FE_CAN_FEC_2_3		|
				  FE_CAN_TRANSMISSION_MODE_AUTO |
				  FE_CAN_GUARD_INTERVAL_AUTO	|
				  FE_CAN_HIERARCHY_AUTO,
};
