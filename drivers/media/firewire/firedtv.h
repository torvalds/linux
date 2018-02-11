/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#ifndef _FIREDTV_H
#define _FIREDTV_H

#include <linux/time.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <media/demux.h>
#include <media/dmxdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>
#include <media/dvbdev.h>

struct firedtv_tuner_status {
	unsigned active_system:8;
	unsigned searching:1;
	unsigned moving:1;
	unsigned no_rf:1;
	unsigned input:1;
	unsigned selected_antenna:7;
	unsigned ber:32;
	unsigned signal_strength:8;
	unsigned raster_frequency:2;
	unsigned rf_frequency:22;
	unsigned man_dep_info_length:8;
	unsigned front_end_error:1;
	unsigned antenna_error:1;
	unsigned front_end_power_status:1;
	unsigned power_supply:1;
	unsigned carrier_noise_ratio:16;
	unsigned power_supply_voltage:8;
	unsigned antenna_voltage:8;
	unsigned firewire_bus_voltage:8;
	unsigned ca_mmi:1;
	unsigned ca_pmt_reply:1;
	unsigned ca_date_time_request:1;
	unsigned ca_application_info:1;
	unsigned ca_module_present_status:1;
	unsigned ca_dvb_flag:1;
	unsigned ca_error_flag:1;
	unsigned ca_initialization_status:1;
};

enum model_type {
	FIREDTV_UNKNOWN = 0,
	FIREDTV_DVB_S   = 1,
	FIREDTV_DVB_C   = 2,
	FIREDTV_DVB_T   = 3,
	FIREDTV_DVB_S2  = 4,
};

struct device;
struct input_dev;
struct fdtv_ir_context;

struct firedtv {
	struct device *device;
	struct list_head list;

	struct dvb_adapter	adapter;
	struct dmxdev		dmxdev;
	struct dvb_demux	demux;
	struct dmx_frontend	frontend;
	struct dvb_net		dvbnet;
	struct dvb_frontend	fe;

	struct dvb_device	*cadev;
	int			ca_last_command;
	int			ca_time_interval;

	struct mutex		avc_mutex;
	wait_queue_head_t	avc_wait;
	bool			avc_reply_received;
	struct work_struct	remote_ctrl_work;
	struct input_dev	*remote_ctrl_dev;

	enum model_type		type;
	char			subunit;
	s8			isochannel;
	struct fdtv_ir_context	*ir_context;

	enum fe_sec_voltage	voltage;
	enum fe_sec_tone_mode	tone;

	struct mutex		demux_mutex;
	unsigned long		channel_active;
	u16			channel_pid[16];

	int			avc_data_length;
	u8			avc_data[512];
};

/* firedtv-avc.c */
int avc_recv(struct firedtv *fdtv, void *data, size_t length);
int avc_tuner_status(struct firedtv *fdtv, struct firedtv_tuner_status *stat);
struct dtv_frontend_properties;
int avc_tuner_dsd(struct firedtv *fdtv, struct dtv_frontend_properties *params);
int avc_tuner_set_pids(struct firedtv *fdtv, unsigned char pidc, u16 pid[]);
int avc_tuner_get_ts(struct firedtv *fdtv);
int avc_identify_subunit(struct firedtv *fdtv);
struct dvb_diseqc_master_cmd;
int avc_lnb_control(struct firedtv *fdtv, char voltage, char burst,
		    char conttone, char nrdiseq,
		    struct dvb_diseqc_master_cmd *diseqcmd);
void avc_remote_ctrl_work(struct work_struct *work);
int avc_register_remote_control(struct firedtv *fdtv);
int avc_ca_app_info(struct firedtv *fdtv, char *app_info, unsigned int *len);
int avc_ca_info(struct firedtv *fdtv, char *app_info, unsigned int *len);
int avc_ca_reset(struct firedtv *fdtv);
int avc_ca_pmt(struct firedtv *fdtv, char *app_info, int length);
int avc_ca_get_time_date(struct firedtv *fdtv, int *interval);
int avc_ca_enter_menu(struct firedtv *fdtv);
int avc_ca_get_mmi(struct firedtv *fdtv, char *mmi_object, unsigned int *len);
int cmp_establish_pp_connection(struct firedtv *fdtv, int plug, int channel);
void cmp_break_pp_connection(struct firedtv *fdtv, int plug, int channel);

/* firedtv-ci.c */
int fdtv_ca_register(struct firedtv *fdtv);
void fdtv_ca_release(struct firedtv *fdtv);

/* firedtv-dvb.c */
int fdtv_start_feed(struct dvb_demux_feed *dvbdmxfeed);
int fdtv_stop_feed(struct dvb_demux_feed *dvbdmxfeed);
int fdtv_dvb_register(struct firedtv *fdtv, const char *name);
void fdtv_dvb_unregister(struct firedtv *fdtv);

/* firedtv-fe.c */
void fdtv_frontend_init(struct firedtv *fdtv, const char *name);

/* firedtv-fw.c */
int fdtv_lock(struct firedtv *fdtv, u64 addr, void *data);
int fdtv_read(struct firedtv *fdtv, u64 addr, void *data);
int fdtv_write(struct firedtv *fdtv, u64 addr, void *data, size_t len);
int fdtv_start_iso(struct firedtv *fdtv);
void fdtv_stop_iso(struct firedtv *fdtv);

/* firedtv-rc.c */
#ifdef CONFIG_DVB_FIREDTV_INPUT
int fdtv_register_rc(struct firedtv *fdtv, struct device *dev);
void fdtv_unregister_rc(struct firedtv *fdtv);
void fdtv_handle_rc(struct firedtv *fdtv, unsigned int code);
#else
static inline int fdtv_register_rc(struct firedtv *fdtv,
				   struct device *dev) { return 0; }
static inline void fdtv_unregister_rc(struct firedtv *fdtv) {}
static inline void fdtv_handle_rc(struct firedtv *fdtv, unsigned int code) {}
#endif

#endif /* _FIREDTV_H */
