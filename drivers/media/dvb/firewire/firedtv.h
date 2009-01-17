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

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <demux.h>
#include <dmxdev.h>
#include <dvb_demux.h>
#include <dvb_frontend.h>
#include <dvb_net.h>
#include <dvbdev.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 25)
#define DVB_REGISTER_ADAPTER(x, y, z, w, v) dvb_register_adapter(x, y, z, w, v)
#else
#define DVB_REGISTER_ADAPTER(x, y, z, w, v) dvb_register_adapter(x, y, z, w)
#define DVB_DEFINE_MOD_OPT_ADAPTER_NR(x)
#endif

/*****************************************************************
 * CA message command constants from en50221_app_tags.h of libdvb
 *****************************************************************/
/*	Resource Manager		*/
#define TAG_PROFILE_ENQUIRY		0x9f8010
#define TAG_PROFILE			0x9f8011
#define TAG_PROFILE_CHANGE		0x9f8012

/*	Application Info		*/
#define TAG_APP_INFO_ENQUIRY		0x9f8020
#define TAG_APP_INFO			0x9f8021
#define TAG_ENTER_MENU			0x9f8022

/*	CA Support			*/
#define TAG_CA_INFO_ENQUIRY		0x9f8030
#define TAG_CA_INFO			0x9f8031
#define TAG_CA_PMT			0x9f8032
#define TAG_CA_PMT_REPLY		0x9f8033

/*	Host Control			*/
#define TAG_TUNE			0x9f8400
#define TAG_REPLACE			0x9f8401
#define TAG_CLEAR_REPLACE		0x9f8402
#define TAG_ASK_RELEASE			0x9f8403

/*	Date and Time			*/
#define TAG_DATE_TIME_ENQUIRY		0x9f8440
#define TAG_DATE_TIME			0x9f8441

/*	Man Machine Interface (MMI)	*/
#define TAG_CLOSE_MMI			0x9f8800
#define TAG_DISPLAY_CONTROL		0x9f8801
#define TAG_DISPLAY_REPLY		0x9f8802
#define TAG_TEXT_LAST			0x9f8803
#define TAG_TEXT_MORE			0x9f8804
#define TAG_KEYPAD_CONTROL		0x9f8805
#define TAG_KEYPRESS			0x9f8806
#define TAG_ENQUIRY			0x9f8807
#define TAG_ANSWER			0x9f8808
#define TAG_MENU_LAST			0x9f8809
#define TAG_MENU_MORE			0x9f880a
#define TAG_MENU_ANSWER			0x9f880b
#define TAG_LIST_LAST			0x9f880c
#define TAG_LIST_MORE			0x9f880d
#define TAG_SUBTITLE_SEGMENT_LAST	0x9f880e
#define TAG_SUBTITLE_SEGMENT_MORE	0x9f880f
#define TAG_DISPLAY_MESSAGE		0x9f8810
#define TAG_SCENE_END_MARK		0x9f8811
#define TAG_SCENE_DONE			0x9f8812
#define TAG_SCENE_CONTROL		0x9f8813
#define TAG_SUBTITLE_DOWNLOAD_LAST	0x9f8814
#define TAG_SUBTITLE_DOWNLOAD_MORE	0x9f8815
#define TAG_FLUSH_DOWNLOAD		0x9f8816
#define TAG_DOWNLOAD_REPLY		0x9f8817

/*	Low Speed Communications	*/
#define TAG_COMMS_COMMAND		0x9f8c00
#define TAG_CONNECTION_DESCRIPTOR	0x9f8c01
#define TAG_COMMS_REPLY			0x9f8c02
#define TAG_COMMS_SEND_LAST		0x9f8c03
#define TAG_COMMS_SEND_MORE		0x9f8c04
#define TAG_COMMS_RECV_LAST		0x9f8c05
#define TAG_COMMS_RECV_MORE		0x9f8c06

/* Authentication */
#define TAG_AUTH_REQ			0x9f8200
#define TAG_AUTH_RESP			0x9f8201

/* Teletext */
#define TAG_TELETEXT_EBU		0x9f9000

/* Smartcard */
#define TAG_SMARTCARD_COMMAND		0x9f8e00
#define TAG_SMARTCARD_REPLY		0x9f8e01
#define TAG_SMARTCARD_SEND		0x9f8e02
#define TAG_SMARTCARD_RCV		0x9f8e03

/* EPG */
#define TAG_EPG_ENQUIRY         	0x9f8f00
#define TAG_EPG_REPLY           	0x9f8f01


enum model_type {
	FIREDTV_UNKNOWN = 0,
	FIREDTV_DVB_S   = 1,
	FIREDTV_DVB_C   = 2,
	FIREDTV_DVB_T   = 3,
	FIREDTV_DVB_S2  = 4,
};

struct input_dev;
struct hpsb_iso;
struct unit_directory;

struct firedtv {
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

	struct firedtv_channel {
		bool active;
		int pid;
	} channel[16];
	struct mutex demux_mutex;

	struct unit_directory *ud;

	enum model_type type;
	char subunit;
	fe_sec_voltage_t voltage;
	fe_sec_tone_mode_t tone;

	int isochannel;
	struct hpsb_iso *iso_handle;

	struct list_head list;

	/* needed by avc_api */
	int resp_length;
	u8 respfrm[512];
};

struct firewireheader {
	union {
		struct {
			__u8 tcode:4;
			__u8 sy:4;
			__u8 tag:2;
			__u8 channel:6;

			__u8 length_l;
			__u8 length_h;
		} hdr;
		__u32 val;
	};
};

struct CIPHeader {
	union {
		struct {
			__u8 syncbits:2;
			__u8 sid:6;
			__u8 dbs;
			__u8 fn:2;
			__u8 qpc:3;
			__u8 sph:1;
			__u8 rsv:2;
			__u8 dbc;
			__u8 syncbits2:2;
			__u8 fmt:6;
			__u32 fdf:24;
		} cip;
		__u64 val;
	};
};

extern const char *fdtv_model_names[];
extern struct list_head fdtv_list;
extern spinlock_t fdtv_list_lock;

struct device;

/* firedtv-dvb.c */
int fdtv_start_feed(struct dvb_demux_feed *dvbdmxfeed);
int fdtv_stop_feed(struct dvb_demux_feed *dvbdmxfeed);
int fdtv_dvbdev_init(struct firedtv *fdtv, struct device *dev);

/* firedtv-fe.c */
void fdtv_frontend_init(struct firedtv *fdtv);

/* firedtv-iso.c */
int setup_iso_channel(struct firedtv *fdtv);
void tear_down_iso_channel(struct firedtv *fdtv);

#endif /* _FIREDTV_H */
