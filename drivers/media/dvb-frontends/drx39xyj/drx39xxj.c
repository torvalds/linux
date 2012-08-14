/*
 *  Driver for Micronas DRX39xx family (drx3933j)
 *
 *  Written by Devin Heitmueller <devin.heitmueller@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "drx39xxj.h"
#include "drx_driver.h"
#include "bsp_types.h"
#include "bsp_tuner.h"
#include "drxj_mc.h"
#include "drxj.h"

static int drx39xxj_set_powerstate(struct dvb_frontend* fe, int enable)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	DRXDemodInstance_t *demod = state->demod;
	DRXStatus_t result;
	DRXPowerMode_t powerMode;

	if (enable)
		powerMode = DRX_POWER_UP;
	else
		powerMode = DRX_POWER_DOWN;

	result = DRX_Ctrl(demod, DRX_CTRL_POWER_MODE, &powerMode);
	if (result != DRX_STS_OK) {
		printk("Power state change failed\n");
		return 0;
	}

	state->powered_up = enable;
	return 0;
}

static int drx39xxj_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct drx39xxj_state* state = fe->demodulator_priv;
	DRXDemodInstance_t  *demod = state->demod;
	DRXStatus_t result;
	DRXLockStatus_t lock_status;

	*status = 0;

	result = DRX_Ctrl(demod, DRX_CTRL_LOCK_STATUS, &lock_status);
	if (result != DRX_STS_OK) {
		printk("drx39xxj: could not get lock status!\n");
		*status = 0;
	}

	switch (lock_status) {
	case DRX_NEVER_LOCK:
		*status = 0;
		printk("drx says NEVER_LOCK\n");
		break;
	case DRX_NOT_LOCKED:
		*status = 0;
		break;
	case DRX_LOCK_STATE_1:
	case DRX_LOCK_STATE_2:
	case DRX_LOCK_STATE_3:
	case DRX_LOCK_STATE_4:
	case DRX_LOCK_STATE_5:
	case DRX_LOCK_STATE_6:
	case DRX_LOCK_STATE_7:
	case DRX_LOCK_STATE_8:
	case DRX_LOCK_STATE_9:
		*status = FE_HAS_SIGNAL
			| FE_HAS_CARRIER
			| FE_HAS_VITERBI
			| FE_HAS_SYNC;
		break;
	case DRX_LOCKED:
		*status = FE_HAS_SIGNAL
			| FE_HAS_CARRIER
			| FE_HAS_VITERBI
			| FE_HAS_SYNC
			| FE_HAS_LOCK;
		break;
	default:
		printk("Lock state unknown %d\n", lock_status);
	}

	return 0;
}

static int drx39xxj_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct drx39xxj_state* state = fe->demodulator_priv;
	DRXDemodInstance_t  *demod = state->demod;
	DRXStatus_t result;
	DRXSigQuality_t sig_quality;

	result = DRX_Ctrl(demod, DRX_CTRL_SIG_QUALITY, &sig_quality);
	if (result != DRX_STS_OK) {
		printk("drx39xxj: could not get ber!\n");
		*ber = 0;
		return 0;
	}

	*ber = sig_quality.postReedSolomonBER;
	return 0;
}

static int drx39xxj_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct drx39xxj_state* state = fe->demodulator_priv;
	DRXDemodInstance_t  *demod = state->demod;
	DRXStatus_t result;
	DRXSigQuality_t sig_quality;

	result = DRX_Ctrl(demod, DRX_CTRL_SIG_QUALITY, &sig_quality);
	if (result != DRX_STS_OK) {
		printk("drx39xxj: could not get signal strength!\n");
		*strength = 0;
		return 0;
	}

	/* 1-100% scaled to 0-65535 */
	*strength = (sig_quality.indicator * 65535 / 100);
	return 0;
}

static int drx39xxj_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct drx39xxj_state* state = fe->demodulator_priv;
	DRXDemodInstance_t  *demod = state->demod;
	DRXStatus_t result;
	DRXSigQuality_t sig_quality;

	result = DRX_Ctrl(demod, DRX_CTRL_SIG_QUALITY, &sig_quality);
	if (result != DRX_STS_OK) {
		printk("drx39xxj: could not read snr!\n");
		*snr = 0;
		return 0;
	}

	*snr = sig_quality.MER;
	return 0;
}

static int drx39xxj_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct drx39xxj_state* state = fe->demodulator_priv;
	DRXDemodInstance_t  *demod = state->demod;
	DRXStatus_t result;
	DRXSigQuality_t sig_quality;

	result = DRX_Ctrl(demod, DRX_CTRL_SIG_QUALITY, &sig_quality);
	if (result != DRX_STS_OK) {
		printk("drx39xxj: could not get uc blocks!\n");
		*ucblocks = 0;
		return 0;
	}

	*ucblocks = sig_quality.packetError;
	return 0;
}

static int drx39xxj_get_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	return 0;
}

static int drx39xxj_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
#ifdef DJH_DEBUG
	int i;
#endif
	struct drx39xxj_state* state = fe->demodulator_priv;
	DRXDemodInstance_t  *demod = state->demod;
	DRXStandard_t standard = DRX_STANDARD_8VSB;
	DRXChannel_t channel;
	DRXStatus_t result;
	DRXUIOData_t uioData;
	DRXChannel_t defChannel = {/* frequency      */ 0,
				 /* bandwidth      */ DRX_BANDWIDTH_6MHZ,
				 /* mirror         */ DRX_MIRROR_NO,
				 /* constellation  */ DRX_CONSTELLATION_AUTO,
				 /* hierarchy      */ DRX_HIERARCHY_UNKNOWN,
				 /* priority       */ DRX_PRIORITY_UNKNOWN,
				 /* coderate       */ DRX_CODERATE_UNKNOWN,
				 /* guard          */ DRX_GUARD_UNKNOWN,
				 /* fftmode        */ DRX_FFTMODE_UNKNOWN,
				 /* classification */ DRX_CLASSIFICATION_AUTO,
				 /* symbolrate     */ 5057000,
				 /* interleavemode */ DRX_INTERLEAVEMODE_UNKNOWN,
				 /* ldpc           */ DRX_LDPC_UNKNOWN,
				 /* carrier        */ DRX_CARRIER_UNKNOWN,
				 /* frame mode     */ DRX_FRAMEMODE_UNKNOWN
				 };

	/* Bring the demod out of sleep */
	drx39xxj_set_powerstate(fe, 1);

	/* Now make the tuner do it's thing... */
	if (fe->ops.tuner_ops.set_params) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	if (standard != state->current_standard || state->powered_up == 0) {
		/* Set the standard (will be powered up if necessary */
		result = DRX_Ctrl(demod, DRX_CTRL_SET_STANDARD, &standard);
		if (result != DRX_STS_OK) {
			printk("Failed to set standard! result=%02x\n", result);
			return -EINVAL;
		}
		state->powered_up = 1;
		state->current_standard = standard;
	}

	/* set channel parameters */
	channel = defChannel;
	channel.frequency      = p->frequency / 1000;
	channel.bandwidth      = DRX_BANDWIDTH_6MHZ;
	channel.constellation  = DRX_CONSTELLATION_AUTO;

	/* program channel */
	result = DRX_Ctrl(demod, DRX_CTRL_SET_CHANNEL, &channel);
	if (result != DRX_STS_OK) {
		printk("Failed to set channel!\n");
		return -EINVAL;
	}

	// Just for giggles, let's shut off the LNA again....
	uioData.uio   = DRX_UIO1;
	uioData.value = FALSE;
	result = DRX_Ctrl(demod, DRX_CTRL_UIO_WRITE, &uioData);
	if (result != DRX_STS_OK) {
		printk("Failed to disable LNA!\n");
		return 0;
	}

#ifdef DJH_DEBUG
	for(i = 0; i < 2000; i++) {
	  fe_status_t  status;
	  drx39xxj_read_status(fe,  &status);
	  printk("i=%d status=%d\n", i, status);
	  msleep(100);
	  i += 100;
	}
#endif

	return 0;
}


static int drx39xxj_sleep(struct dvb_frontend* fe)
{
	/* power-down the demodulator */
	return drx39xxj_set_powerstate(fe, 0);
}

static int drx39xxj_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	DRXDemodInstance_t *demod = state->demod;
	Bool_t i2c_gate_state;
	DRXStatus_t result;

#ifdef DJH_DEBUG
	printk("i2c gate call: enable=%d state=%d\n", enable,
	       state->i2c_gate_open);
#endif

	if (enable)
		i2c_gate_state = TRUE;
	else
		i2c_gate_state = FALSE;

	if (state->i2c_gate_open == enable) {
		/* We're already in the desired state */
		return 0;
	}

	result = DRX_Ctrl(demod, DRX_CTRL_I2C_BRIDGE, &i2c_gate_state);
	if (result != DRX_STS_OK) {
		printk("drx39xxj: could not open i2c gate [%d]\n", result);
		dump_stack();
	} else {
		state->i2c_gate_open = enable;
	}
	return 0;
}


static int drx39xxj_init(struct dvb_frontend* fe)
{
	/* Bring the demod out of sleep */
	drx39xxj_set_powerstate(fe, 1);

	return 0;
}

static int drx39xxj_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void drx39xxj_release(struct dvb_frontend* fe)
{
	struct drx39xxj_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops drx39xxj_ops;

struct dvb_frontend *drx39xxj_attach(struct i2c_adapter *i2c)
{
	struct drx39xxj_state* state = NULL;

	I2CDeviceAddr_t     *demodAddr = NULL;
	DRXCommonAttr_t     *demodCommAttr = NULL;
	DRXJData_t          *demodExtAttr = NULL;
	DRXDemodInstance_t  *demod = NULL;
	DRXUIOCfg_t uioCfg;
	DRXUIOData_t uioData;
	DRXStatus_t result;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct drx39xxj_state), GFP_KERNEL);
	if (state == NULL) goto error;

	demod = kmalloc(sizeof(DRXDemodInstance_t), GFP_KERNEL);
	if (demod == NULL) goto error;

	demodAddr = kmalloc(sizeof(I2CDeviceAddr_t), GFP_KERNEL);
	if (demodAddr == NULL) goto error;

	demodCommAttr = kmalloc(sizeof(DRXCommonAttr_t), GFP_KERNEL);
	if (demodCommAttr == NULL) goto error;

	demodExtAttr = kmalloc(sizeof(DRXJData_t), GFP_KERNEL);
	if (demodExtAttr == NULL) goto error;

	/* setup the state */
	state->i2c = i2c;
	state->demod = demod;

	memcpy(demod, &DRXJDefaultDemod_g, sizeof(DRXDemodInstance_t));

	demod->myI2CDevAddr = demodAddr;
	memcpy(demod->myI2CDevAddr, &DRXJDefaultAddr_g,
	       sizeof(I2CDeviceAddr_t));
	demod->myI2CDevAddr->userData = state;
	demod->myCommonAttr = demodCommAttr;
	memcpy(demod->myCommonAttr, &DRXJDefaultCommAttr_g,
	       sizeof(DRXCommonAttr_t));
	demod->myCommonAttr->microcode = DRXJ_MC_MAIN;
	//	demod->myCommonAttr->verifyMicrocode = FALSE;
	demod->myCommonAttr->verifyMicrocode = TRUE;
	demod->myCommonAttr->intermediateFreq = 5000;

	demod->myExtAttr = demodExtAttr;
	memcpy(demod->myExtAttr, &DRXJData_g, sizeof(DRXJData_t));
	((DRXJData_t *) demod->myExtAttr)->uioSmaTxMode = DRX_UIO_MODE_READWRITE;

	demod->myTuner = NULL;

	result = DRX_Open(demod);
	if (result != DRX_STS_OK) {
		printk("DRX open failed!  Aborting\n");
		kfree(state);
		return NULL;
	}

	/* Turn off the LNA */
	uioCfg.uio    = DRX_UIO1;
	uioCfg.mode   = DRX_UIO_MODE_READWRITE;
	/* Configure user-I/O #3: enable read/write */
	result = DRX_Ctrl(demod, DRX_CTRL_UIO_CFG, &uioCfg);
	if (result != DRX_STS_OK) {
		printk("Failed to setup LNA GPIO!\n");
		return NULL;
	}

	uioData.uio   = DRX_UIO1;
	uioData.value = FALSE;
	result = DRX_Ctrl(demod, DRX_CTRL_UIO_WRITE, &uioData);
	if (result != DRX_STS_OK) {
		printk("Failed to disable LNA!\n");
		return NULL;
	}

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &drx39xxj_ops,
	       sizeof(struct dvb_frontend_ops));

	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	if (state != NULL)
		kfree(state);
	if (demod != NULL)
		kfree(demod);
	return NULL;
}

static struct dvb_frontend_ops drx39xxj_ops = {

	.info = {
		.name			= "Micronas DRX39xxj family Frontend",
		.type			= FE_ATSC | FE_QAM,
		.frequency_stepsize	= 62500,
		.frequency_min		= 51000000,
		.frequency_max		= 858000000,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},

	.init = drx39xxj_init,
	.i2c_gate_ctrl = drx39xxj_i2c_gate_ctrl,
	.sleep = drx39xxj_sleep,
	.set_frontend = drx39xxj_set_frontend,
	.get_frontend = drx39xxj_get_frontend,
	.get_tune_settings = drx39xxj_get_tune_settings,
	.read_status = drx39xxj_read_status,
	.read_ber = drx39xxj_read_ber,
	.read_signal_strength = drx39xxj_read_signal_strength,
	.read_snr = drx39xxj_read_snr,
	.read_ucblocks = drx39xxj_read_ucblocks,
	.release = drx39xxj_release,
};

MODULE_DESCRIPTION("Micronas DRX39xxj Frontend");
MODULE_AUTHOR("Devin Heitmueller");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(drx39xxj_attach);
