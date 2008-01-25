/*
 * dvb_frontend.h
 *
 * Copyright (C) 2001 convergence integrated media GmbH
 * Copyright (C) 2004 convergence GmbH
 *
 * Written by Ralph Metzler
 * Overhauled by Holger Waechtler
 * Kernel I2C stuff by Michael Hunold <hunold@convergence.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *

 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVB_FRONTEND_H_
#define _DVB_FRONTEND_H_

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include <linux/dvb/frontend.h>

#include "dvbdev.h"

struct dvb_frontend_tune_settings {
	int min_delay_ms;
	int step_size;
	int max_drift;
	struct dvb_frontend_parameters parameters;
};

struct dvb_frontend;

struct dvb_tuner_info {
	char name[128];

	u32 frequency_min;
	u32 frequency_max;
	u32 frequency_step;

	u32 bandwidth_min;
	u32 bandwidth_max;
	u32 bandwidth_step;
};

struct analog_parameters {
	unsigned int frequency;
	unsigned int mode;
	unsigned int audmode;
	u64 std;
};

struct dvb_tuner_ops {

	struct dvb_tuner_info info;

	int (*release)(struct dvb_frontend *fe);
	int (*init)(struct dvb_frontend *fe);
	int (*sleep)(struct dvb_frontend *fe);

	/** This is for simple PLLs - set all parameters in one go. */
	int (*set_params)(struct dvb_frontend *fe, struct dvb_frontend_parameters *p);
	int (*set_analog_params)(struct dvb_frontend *fe, struct analog_parameters *p);

	/** This is support for demods like the mt352 - fills out the supplied buffer with what to write. */
	int (*calc_regs)(struct dvb_frontend *fe, struct dvb_frontend_parameters *p, u8 *buf, int buf_len);

	/** This is to allow setting tuner-specific configs */
	int (*set_config)(struct dvb_frontend *fe, void *priv_cfg);

	int (*get_frequency)(struct dvb_frontend *fe, u32 *frequency);
	int (*get_bandwidth)(struct dvb_frontend *fe, u32 *bandwidth);

#define TUNER_STATUS_LOCKED 1
#define TUNER_STATUS_STEREO 2
	int (*get_status)(struct dvb_frontend *fe, u32 *status);
	int (*get_rf_strength)(struct dvb_frontend *fe, u16 *strength);

	/** These are provided seperately from set_params in order to facilitate silicon
	 * tuners which require sophisticated tuning loops, controlling each parameter seperately. */
	int (*set_frequency)(struct dvb_frontend *fe, u32 frequency);
	int (*set_bandwidth)(struct dvb_frontend *fe, u32 bandwidth);
};

struct analog_demod_info {
	char *name;
};

struct analog_demod_ops {

	struct analog_demod_info info;

	void (*set_params)(struct dvb_frontend *fe,
			   struct analog_parameters *params);
	int  (*has_signal)(struct dvb_frontend *fe);
	int  (*is_stereo)(struct dvb_frontend *fe);
	int  (*get_afc)(struct dvb_frontend *fe);
	void (*tuner_status)(struct dvb_frontend *fe);
	void (*standby)(struct dvb_frontend *fe);
	void (*release)(struct dvb_frontend *fe);
	int  (*i2c_gate_ctrl)(struct dvb_frontend *fe, int enable);

	/** This is to allow setting tuner-specific configuration */
	int (*set_config)(struct dvb_frontend *fe, void *priv_cfg);
};

struct dvb_frontend_ops {

	struct dvb_frontend_info info;

	void (*release)(struct dvb_frontend* fe);
	void (*release_sec)(struct dvb_frontend* fe);

	int (*init)(struct dvb_frontend* fe);
	int (*sleep)(struct dvb_frontend* fe);

	int (*write)(struct dvb_frontend* fe, u8* buf, int len);

	/* if this is set, it overrides the default swzigzag */
	int (*tune)(struct dvb_frontend* fe,
		    struct dvb_frontend_parameters* params,
		    unsigned int mode_flags,
		    unsigned int *delay,
		    fe_status_t *status);
	/* get frontend tuning algorithm from the module */
	int (*get_frontend_algo)(struct dvb_frontend *fe);

	/* these two are only used for the swzigzag code */
	int (*set_frontend)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);
	int (*get_tune_settings)(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* settings);

	int (*get_frontend)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);

	int (*read_status)(struct dvb_frontend* fe, fe_status_t* status);
	int (*read_ber)(struct dvb_frontend* fe, u32* ber);
	int (*read_signal_strength)(struct dvb_frontend* fe, u16* strength);
	int (*read_snr)(struct dvb_frontend* fe, u16* snr);
	int (*read_ucblocks)(struct dvb_frontend* fe, u32* ucblocks);

	int (*diseqc_reset_overload)(struct dvb_frontend* fe);
	int (*diseqc_send_master_cmd)(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd* cmd);
	int (*diseqc_recv_slave_reply)(struct dvb_frontend* fe, struct dvb_diseqc_slave_reply* reply);
	int (*diseqc_send_burst)(struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd);
	int (*set_tone)(struct dvb_frontend* fe, fe_sec_tone_mode_t tone);
	int (*set_voltage)(struct dvb_frontend* fe, fe_sec_voltage_t voltage);
	int (*enable_high_lnb_voltage)(struct dvb_frontend* fe, long arg);
	int (*dishnetwork_send_legacy_command)(struct dvb_frontend* fe, unsigned long cmd);
	int (*i2c_gate_ctrl)(struct dvb_frontend* fe, int enable);
	int (*ts_bus_ctrl)(struct dvb_frontend* fe, int acquire);

	struct dvb_tuner_ops tuner_ops;
	struct analog_demod_ops analog_ops;
};

#define MAX_EVENT 8

struct dvb_fe_events {
	struct dvb_frontend_event events[MAX_EVENT];
	int			  eventw;
	int			  eventr;
	int			  overflow;
	wait_queue_head_t	  wait_queue;
	struct mutex		  mtx;
};

struct dvb_frontend {
	struct dvb_frontend_ops ops;
	struct dvb_adapter *dvb;
	void *demodulator_priv;
	void *tuner_priv;
	void *frontend_priv;
	void *sec_priv;
	void *analog_demod_priv;
};

extern int dvb_register_frontend(struct dvb_adapter *dvb,
				 struct dvb_frontend *fe);

extern int dvb_unregister_frontend(struct dvb_frontend *fe);

extern void dvb_frontend_detach(struct dvb_frontend *fe);

extern void dvb_frontend_reinitialise(struct dvb_frontend *fe);

extern void dvb_frontend_sleep_until(struct timeval *waketime, u32 add_usec);
extern s32 timeval_usec_diff(struct timeval lasttime, struct timeval curtime);

#endif
