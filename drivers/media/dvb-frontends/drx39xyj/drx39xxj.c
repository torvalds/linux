/*
 *  Driver for Micronas DRX39xx family (drx3933j)
 *
 *  Written by Devin Heitmueller <devin.heitmueller@kernellabs.com>
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
#include "drxj_mc.h"
#include "drxj.h"

static int drx39xxj_set_powerstate(struct dvb_frontend *fe, int enable)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	drx_demod_instance_t *demod = state->demod;
	int result;
	drx_power_mode_t power_mode;

	if (enable)
		power_mode = DRX_POWER_UP;
	else
		power_mode = DRX_POWER_DOWN;

	result = drx_ctrl(demod, DRX_CTRL_POWER_MODE, &power_mode);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "Power state change failed\n");
		return 0;
	}

	state->powered_up = enable;
	return 0;
}

static int drx39xxj_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	drx_demod_instance_t *demod = state->demod;
	int result;
	drx_lock_status_t lock_status;

	*status = 0;

	result = drx_ctrl(demod, DRX_CTRL_LOCK_STATUS, &lock_status);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "drx39xxj: could not get lock status!\n");
		*status = 0;
	}

	switch (lock_status) {
	case DRX_NEVER_LOCK:
		*status = 0;
		printk(KERN_ERR "drx says NEVER_LOCK\n");
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
		    | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC;
		break;
	case DRX_LOCKED:
		*status = FE_HAS_SIGNAL
		    | FE_HAS_CARRIER
		    | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		break;
	default:
		printk(KERN_ERR "Lock state unknown %d\n", lock_status);
	}

	return 0;
}

static int drx39xxj_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	drx_demod_instance_t *demod = state->demod;
	int result;
	drx_sig_quality_t sig_quality;

	result = drx_ctrl(demod, DRX_CTRL_SIG_QUALITY, &sig_quality);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "drx39xxj: could not get ber!\n");
		*ber = 0;
		return 0;
	}

	*ber = sig_quality.post_reed_solomon_ber;
	return 0;
}

static int drx39xxj_read_signal_strength(struct dvb_frontend *fe,
					 u16 *strength)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	drx_demod_instance_t *demod = state->demod;
	int result;
	drx_sig_quality_t sig_quality;

	result = drx_ctrl(demod, DRX_CTRL_SIG_QUALITY, &sig_quality);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "drx39xxj: could not get signal strength!\n");
		*strength = 0;
		return 0;
	}

	/* 1-100% scaled to 0-65535 */
	*strength = (sig_quality.indicator * 65535 / 100);
	return 0;
}

static int drx39xxj_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	drx_demod_instance_t *demod = state->demod;
	int result;
	drx_sig_quality_t sig_quality;

	result = drx_ctrl(demod, DRX_CTRL_SIG_QUALITY, &sig_quality);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "drx39xxj: could not read snr!\n");
		*snr = 0;
		return 0;
	}

	*snr = sig_quality.MER;
	return 0;
}

static int drx39xxj_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	drx_demod_instance_t *demod = state->demod;
	int result;
	drx_sig_quality_t sig_quality;

	result = drx_ctrl(demod, DRX_CTRL_SIG_QUALITY, &sig_quality);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "drx39xxj: could not get uc blocks!\n");
		*ucblocks = 0;
		return 0;
	}

	*ucblocks = sig_quality.packet_error;
	return 0;
}

static int drx39xxj_set_frontend(struct dvb_frontend *fe)
{
#ifdef DJH_DEBUG
	int i;
#endif
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct drx39xxj_state *state = fe->demodulator_priv;
	drx_demod_instance_t *demod = state->demod;
	enum drx_standard standard = DRX_STANDARD_8VSB;
	drx_channel_t channel;
	int result;
	drxuio_data_t uio_data;
	drx_channel_t def_channel = { /* frequency      */ 0,
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
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	if (standard != state->current_standard || state->powered_up == 0) {
		/* Set the standard (will be powered up if necessary */
		result = drx_ctrl(demod, DRX_CTRL_SET_STANDARD, &standard);
		if (result != DRX_STS_OK) {
			printk(KERN_ERR "Failed to set standard! result=%02x\n",
			       result);
			return -EINVAL;
		}
		state->powered_up = 1;
		state->current_standard = standard;
	}

	/* set channel parameters */
	channel = def_channel;
	channel.frequency = p->frequency / 1000;
	channel.bandwidth = DRX_BANDWIDTH_6MHZ;
	channel.constellation = DRX_CONSTELLATION_AUTO;

	/* program channel */
	result = drx_ctrl(demod, DRX_CTRL_SET_CHANNEL, &channel);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "Failed to set channel!\n");
		return -EINVAL;
	}
	/* Just for giggles, let's shut off the LNA again.... */
	uio_data.uio = DRX_UIO1;
	uio_data.value = false;
	result = drx_ctrl(demod, DRX_CTRL_UIO_WRITE, &uio_data);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "Failed to disable LNA!\n");
		return 0;
	}
#ifdef DJH_DEBUG
	for (i = 0; i < 2000; i++) {
		fe_status_t status;
		drx39xxj_read_status(fe, &status);
		printk(KERN_DBG "i=%d status=%d\n", i, status);
		msleep(100);
		i += 100;
	}
#endif

	return 0;
}

static int drx39xxj_sleep(struct dvb_frontend *fe)
{
	/* power-down the demodulator */
	return drx39xxj_set_powerstate(fe, 0);
}

static int drx39xxj_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	drx_demod_instance_t *demod = state->demod;
	bool i2c_gate_state;
	int result;

#ifdef DJH_DEBUG
	printk(KERN_DBG "i2c gate call: enable=%d state=%d\n", enable,
	       state->i2c_gate_open);
#endif

	if (enable)
		i2c_gate_state = true;
	else
		i2c_gate_state = false;

	if (state->i2c_gate_open == enable) {
		/* We're already in the desired state */
		return 0;
	}

	result = drx_ctrl(demod, DRX_CTRL_I2C_BRIDGE, &i2c_gate_state);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "drx39xxj: could not open i2c gate [%d]\n",
		       result);
		dump_stack();
	} else {
		state->i2c_gate_open = enable;
	}
	return 0;
}

static int drx39xxj_init(struct dvb_frontend *fe)
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

static void drx39xxj_release(struct dvb_frontend *fe)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops drx39xxj_ops;

struct dvb_frontend *drx39xxj_attach(struct i2c_adapter *i2c)
{
	struct drx39xxj_state *state = NULL;

	struct i2c_device_addr *demod_addr = NULL;
	drx_common_attr_t *demod_comm_attr = NULL;
	drxj_data_t *demod_ext_attr = NULL;
	drx_demod_instance_t *demod = NULL;
	drxuio_cfg_t uio_cfg;
	drxuio_data_t uio_data;
	int result;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct drx39xxj_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	demod = kmalloc(sizeof(drx_demod_instance_t), GFP_KERNEL);
	if (demod == NULL)
		goto error;

	demod_addr = kmalloc(sizeof(struct i2c_device_addr), GFP_KERNEL);
	if (demod_addr == NULL)
		goto error;

	demod_comm_attr = kmalloc(sizeof(drx_common_attr_t), GFP_KERNEL);
	if (demod_comm_attr == NULL)
		goto error;

	demod_ext_attr = kmalloc(sizeof(drxj_data_t), GFP_KERNEL);
	if (demod_ext_attr == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	state->demod = demod;

	memcpy(demod, &drxj_default_demod_g, sizeof(drx_demod_instance_t));

	demod->my_i2c_dev_addr = demod_addr;
	memcpy(demod->my_i2c_dev_addr, &drxj_default_addr_g,
	       sizeof(struct i2c_device_addr));
	demod->my_i2c_dev_addr->user_data = state;
	demod->my_common_attr = demod_comm_attr;
	memcpy(demod->my_common_attr, &drxj_default_comm_attr_g,
	       sizeof(drx_common_attr_t));
	demod->my_common_attr->microcode = DRXJ_MC_MAIN;
#if 0
	demod->my_common_attr->verify_microcode = false;
#endif
	demod->my_common_attr->verify_microcode = true;
	demod->my_common_attr->intermediate_freq = 5000;

	demod->my_ext_attr = demod_ext_attr;
	memcpy(demod->my_ext_attr, &drxj_data_g, sizeof(drxj_data_t));
	((drxj_data_t *) demod->my_ext_attr)->uio_sma_tx_mode =
	    DRX_UIO_MODE_READWRITE;

	demod->my_tuner = NULL;

	result = drx_open(demod);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "DRX open failed!  Aborting\n");
		kfree(state);
		return NULL;
	}

	/* Turn off the LNA */
	uio_cfg.uio = DRX_UIO1;
	uio_cfg.mode = DRX_UIO_MODE_READWRITE;
	/* Configure user-I/O #3: enable read/write */
	result = drx_ctrl(demod, DRX_CTRL_UIO_CFG, &uio_cfg);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "Failed to setup LNA GPIO!\n");
		return NULL;
	}

	uio_data.uio = DRX_UIO1;
	uio_data.value = false;
	result = drx_ctrl(demod, DRX_CTRL_UIO_WRITE, &uio_data);
	if (result != DRX_STS_OK) {
		printk(KERN_ERR "Failed to disable LNA!\n");
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
EXPORT_SYMBOL(drx39xxj_attach);

static struct dvb_frontend_ops drx39xxj_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		 .name = "Micronas DRX39xxj family Frontend",
		 .frequency_stepsize = 62500,
		 .frequency_min = 51000000,
		 .frequency_max = 858000000,
		 .caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB},

	.init = drx39xxj_init,
	.i2c_gate_ctrl = drx39xxj_i2c_gate_ctrl,
	.sleep = drx39xxj_sleep,
	.set_frontend = drx39xxj_set_frontend,
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
