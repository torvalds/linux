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

struct dvb_tuner_ops {
	/**
	 * Description of the tuner.
	 */
	struct dvb_tuner_info info;

	/**
	 * Cleanup an attached tuner.
	 *
	 * @param fe dvb_frontend structure to clean it up from.
	 * @return 0 on success, <0 on failure.
	 */
	int (*release)(struct dvb_frontend *fe);

	/**
	 * Initialise a tuner.
	 *
	 * @param fe dvb_frontend structure.
	 * @return 0 on success, <0 on failure.
	 */
	int (*init)(struct dvb_frontend *fe);

	/**
	 * Set a tuner into low power mode.
	 *
	 * @param fe dvb_frontend structure.
	 * @return 0 on success, <0 on failure.
	 */
	int (*sleep)(struct dvb_frontend *fe);

	/**
	 * This is for simple PLLs - set all parameters in one go.
	 *
	 * @param fe The dvb_frontend structure.
	 * @param p The parameters to set.
	 * @return 0 on success, <0 on failure.
	 */
	int (*set_params)(struct dvb_frontend *fe, struct dvb_frontend_parameters *p);

	/**
	 * This is support for demods like the mt352 - fills out the supplied buffer with what to write.
	 *
	 * @param fe The dvb_frontend structure.
	 * @param p The parameters to set.
	 * @param buf The buffer to fill with data. For an i2c tuner, the first byte should be the tuner i2c address in linux format.
	 * @param buf_len Size of buffer in bytes.
	 * @return Number of bytes used, or <0 on failure.
	 */
	int (*pllbuf)(struct dvb_frontend *fe, struct dvb_frontend_parameters *p, u8 *buf, int buf_len);

	/**
	 * Get the frequency the tuner was actually set to.
	 *
	 * @param fe The dvb_frontend structure.
	 * @param frequency Where to put it.
	 * @return 0 on success, or <0 on failure.
	 */
	int (*get_frequency)(struct dvb_frontend *fe, u32 *frequency);

	/**
	 * Get the bandwidth the tuner was actually set to.
	 *
	 * @param fe The dvb_frontend structure.
	 * @param bandwidth Where to put it.
	 * @return 0 on success, or <0 on failure.
	 */
	int (*get_bandwidth)(struct dvb_frontend *fe, u32 *bandwidth);

	/**
	 * Get the tuner's status.
	 *
	 * @param fe The dvb_frontend structure.
	 * @param status Where to put it.
	 * @return 0 on success, or <0 on failure.
	 */
#define TUNER_STATUS_LOCKED 1
	int (*get_status)(struct dvb_frontend *fe, u32 *status);

	/**
	 * Set the frequency of the tuner - for complex tuners.
	 *
	 * @param fe The dvb_frontend structure.
	 * @param frequency What to set.
	 * @return 0 on success, or <0 on failure.
	 */
	int (*set_frequency)(struct dvb_frontend *fe, u32 frequency);

	/**
	 * Set the bandwidth of the tuner - for complex tuners.
	 *
	 * @param fe The dvb_frontend structure.
	 * @param bandwidth  What to set.
	 * @return 0 on success, or <0 on failure.
	 */
	int (*set_bandwidth)(struct dvb_frontend *fe, u32 bandwidth);
};

struct dvb_frontend_ops {

	struct dvb_frontend_info info;

	void (*release)(struct dvb_frontend* fe);

	int (*init)(struct dvb_frontend* fe);
	int (*sleep)(struct dvb_frontend* fe);

	/* if this is set, it overrides the default swzigzag */
	int (*tune)(struct dvb_frontend* fe,
		    struct dvb_frontend_parameters* params,
		    unsigned int mode_flags,
		    int *delay,
		    fe_status_t *status);

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

	struct dvb_tuner_ops tuner_ops;
};

#define MAX_EVENT 8

struct dvb_fe_events {
	struct dvb_frontend_event events[MAX_EVENT];
	int			  eventw;
	int			  eventr;
	int			  overflow;
	wait_queue_head_t	  wait_queue;
	struct semaphore	  sem;
};

struct dvb_frontend {
	struct dvb_frontend_ops* ops;
	struct dvb_adapter *dvb;
	void* demodulator_priv;
	void* tuner_priv;
	void* frontend_priv;
	void* misc_priv;
};

extern int dvb_register_frontend(struct dvb_adapter* dvb,
				 struct dvb_frontend* fe);

extern int dvb_unregister_frontend(struct dvb_frontend* fe);

extern void dvb_frontend_reinitialise(struct dvb_frontend *fe);

extern void dvb_frontend_sleep_until(struct timeval *waketime, u32 add_usec);
extern s32 timeval_usec_diff(struct timeval lasttime, struct timeval curtime);

#endif
