#ifndef __FIRESAT_H
#define __FIRESAT_H

#include "dvb_frontend.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_net.h"

#include <linux/semaphore.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

enum model_type {
    FireSAT_DVB_S = 1,
    FireSAT_DVB_C = 2,
    FireSAT_DVB_T = 3,
    FireSAT_DVB_S2 = 4
};

struct firesat {
	struct dvb_demux dvb_demux;
	char *model_name;

	/* DVB bits */
	struct dvb_adapter		*adapter;
	struct dmxdev			dmxdev;
	struct dvb_demux		demux;
	struct dmx_frontend		frontend;
	struct dvb_net			dvbnet;
	struct dvb_frontend_info	*frontend_info;
	struct dvb_frontend		*fe;

	struct dvb_device		*cadev;
	int				has_ci;

	struct semaphore		avc_sem;
	atomic_t				avc_reply_received;

	atomic_t				reschedule_remotecontrol;

	struct firesat_channel {
		struct firesat *firesat;
		struct dvb_demux_feed *dvbdmxfeed;

		int active;
		int id;
		int pid;
		int type;	/* 1 - TS, 2 - Filter */
	} channel[16];
	struct semaphore		demux_sem;

	/* needed by avc_api */
	void *respfrm;
	int resp_length;

//    nodeid_t nodeid;
    struct hpsb_host *host;
	u64 guid;			/* GUID of this node */
	u32 guid_vendor_id;		/* Top 24bits of guid */
	struct node_entry *nodeentry;

    enum model_type type;
    char subunit;
	fe_sec_voltage_t voltage;
	fe_sec_tone_mode_t tone;

	int isochannel;

    struct list_head list;
};

extern struct list_head firesat_list;
extern spinlock_t firesat_list_lock;

/* firesat_dvb.c */
extern int firesat_start_feed(struct dvb_demux_feed *dvbdmxfeed);
extern int firesat_stop_feed(struct dvb_demux_feed *dvbdmxfeed);
extern int firesat_dvbdev_init(struct firesat *firesat,
				struct device *dev,
				struct dvb_frontend *fe);

/* firesat_fe.c */
extern int firesat_frontend_attach(struct firesat *firesat, struct dvb_frontend *fe);


#endif
