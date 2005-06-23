/*
 * dvb-dibusb.h
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * for more information see dvb-dibusb-core.c .
 */
#ifndef __DVB_DIBUSB_H__
#define __DVB_DIBUSB_H__

#include <linux/input.h>
#include <linux/config.h>
#include <linux/usb.h>

#include "dvb_frontend.h"
#include "dvb_demux.h"
#include "dvb_net.h"
#include "dmxdev.h"

#include "dib3000.h"
#include "mt352.h"

/* debug */
#ifdef CONFIG_DVB_DIBCOM_DEBUG
#define dprintk(level,args...) \
	    do { if ((dvb_dibusb_debug & level)) { printk(args); } } while (0)

#define debug_dump(b,l) {\
	int i; \
	for (i = 0; i < l; i++) deb_xfer("%02x ", b[i]); \
	deb_xfer("\n");\
}

#else
#define dprintk(args...)
#define debug_dump(b,l)
#endif

extern int dvb_dibusb_debug;

/* Version information */
#define DRIVER_VERSION "0.3"
#define DRIVER_DESC "DiBcom based USB Budget DVB-T device"
#define DRIVER_AUTHOR "Patrick Boettcher, patrick.boettcher@desy.de"

#define deb_info(args...) dprintk(0x01,args)
#define deb_xfer(args...) dprintk(0x02,args)
#define deb_alot(args...) dprintk(0x04,args)
#define deb_ts(args...)   dprintk(0x08,args)
#define deb_err(args...)  dprintk(0x10,args)
#define deb_rc(args...)   dprintk(0x20,args)

/* generic log methods - taken from usb.h */
#undef err
#define err(format, arg...)  printk(KERN_ERR     "dvb-dibusb: " format "\n" , ## arg)
#undef info
#define info(format, arg...) printk(KERN_INFO    "dvb-dibusb: " format "\n" , ## arg)
#undef warn
#define warn(format, arg...) printk(KERN_WARNING "dvb-dibusb: " format "\n" , ## arg)

struct dibusb_usb_controller {
	const char *name;       /* name of the usb controller */
	u16 cpu_cs_register;    /* needs to be restarted, when the firmware has been downloaded. */
};

typedef enum {
	DIBUSB1_1 = 0,
	DIBUSB1_1_AN2235,
	DIBUSB2_0,
	UMT2_0,
	DIBUSB2_0B,
	NOVAT_USB2,
	DTT200U,
} dibusb_class_t;

typedef enum {
	DIBUSB_TUNER_CABLE_THOMSON = 0,
	DIBUSB_TUNER_COFDM_PANASONIC_ENV57H1XD5,
	DIBUSB_TUNER_CABLE_LG_TDTP_E102P,
	DIBUSB_TUNER_COFDM_PANASONIC_ENV77H11D5,
} dibusb_tuner_t;

typedef enum {
	DIBUSB_DIB3000MB = 0,
	DIBUSB_DIB3000MC,
	DIBUSB_MT352,
	DTT200U_FE,
} dibusb_demodulator_t;

typedef enum {
	DIBUSB_RC_NO = 0,
	DIBUSB_RC_NEC_PROTOCOL,
	DIBUSB_RC_HAUPPAUGE_PROTO,
} dibusb_remote_t;

struct dibusb_tuner {
	dibusb_tuner_t id;

	u8 pll_addr;       /* tuner i2c address */
};
extern struct dibusb_tuner dibusb_tuner[];

#define DIBUSB_POSSIBLE_I2C_ADDR_NUM 4
struct dibusb_demod {
	dibusb_demodulator_t id;

	int pid_filter_count;                       /* counter of the internal pid_filter */
	u8 i2c_addrs[DIBUSB_POSSIBLE_I2C_ADDR_NUM]; /* list of possible i2c addresses of the demod */
};

#define DIBUSB_MAX_TUNER_NUM 2
struct dibusb_device_class {
	dibusb_class_t id;

	const struct dibusb_usb_controller *usb_ctrl; /* usb controller */
	const char *firmware;                         /* valid firmware filenames */

	int pipe_cmd;                                 /* command pipe (read/write) */
	int pipe_data;                                /* data pipe */

	int urb_count;                                /* number of data URBs to be submitted */
	int urb_buffer_size;                          /* the size of the buffer for each URB */

	dibusb_remote_t remote_type;                  /* does this device have a ir-receiver */

	struct dibusb_demod *demod;                   /* which demodulator is mount */
	struct dibusb_tuner *tuner;                   /* which tuner can be found here */
};

#define DIBUSB_ID_MAX_NUM 15
struct dibusb_usb_device {
	const char *name;                                 /* real name of the box */
	struct dibusb_device_class *dev_cl;               /* which dibusb_device_class is this device part of */

	struct usb_device_id *cold_ids[DIBUSB_ID_MAX_NUM]; /* list of USB ids when this device is at pre firmware state */
	struct usb_device_id *warm_ids[DIBUSB_ID_MAX_NUM]; /* list of USB ids when this device is at post firmware state */
};

/* a PID for the pid_filter list, when in use */
struct dibusb_pid
{
	int index;
	u16 pid;
	int active;
};

struct usb_dibusb {
	/* usb */
	struct usb_device * udev;

	struct dibusb_usb_device * dibdev;

#define DIBUSB_STATE_INIT       0x000
#define DIBUSB_STATE_URB_LIST   0x001
#define DIBUSB_STATE_URB_BUF    0x002
#define DIBUSB_STATE_URB_INIT	0x004
#define DIBUSB_STATE_DVB        0x008
#define DIBUSB_STATE_I2C        0x010
#define DIBUSB_STATE_REMOTE		0x020
#define DIBUSB_STATE_URB_SUBMIT 0x040
	int init_state;

	int feedcount;
	struct dib_fe_xfer_ops xfer_ops;

	struct dibusb_tuner *tuner;

	struct urb **urb_list;
	u8 *buffer;
	dma_addr_t dma_handle;

	/* I2C */
	struct i2c_adapter i2c_adap;

	/* locking */
	struct semaphore usb_sem;
	struct semaphore i2c_sem;

	/* dvb */
	struct dvb_adapter adapter;
	struct dmxdev dmxdev;
	struct dvb_demux demux;
	struct dvb_net dvb_net;
	struct dvb_frontend* fe;

	int (*fe_sleep) (struct dvb_frontend *);
	int (*fe_init) (struct dvb_frontend *);

	/* remote control */
	struct input_dev rc_input_dev;
	struct work_struct rc_query_work;
	int last_event;
	int last_state; /* for Hauppauge RC protocol */
	int repeat_key_count;
	int rc_key_repeat_count; /* module parameter */

	/* module parameters */
	int pid_parse;
	int rc_query_interval;
};

/* commonly used functions in the separated files */

/* dvb-dibusb-firmware.c */
int dibusb_loadfirmware(struct usb_device *udev, struct dibusb_usb_device *dibdev);

/* dvb-dibusb-remote.c */
int dibusb_remote_exit(struct usb_dibusb *dib);
int dibusb_remote_init(struct usb_dibusb *dib);

/* dvb-dibusb-fe-i2c.c */
int dibusb_fe_init(struct usb_dibusb* dib);
int dibusb_fe_exit(struct usb_dibusb *dib);
int dibusb_i2c_init(struct usb_dibusb *dib);
int dibusb_i2c_exit(struct usb_dibusb *dib);

/* dvb-dibusb-dvb.c */
void dibusb_urb_complete(struct urb *urb, struct pt_regs *ptregs);
int dibusb_dvb_init(struct usb_dibusb *dib);
int dibusb_dvb_exit(struct usb_dibusb *dib);

/* dvb-dibusb-usb.c */
int dibusb_readwrite_usb(struct usb_dibusb *dib, u8 *wbuf, u16 wlen, u8 *rbuf,
	u16 rlen);
int dibusb_write_usb(struct usb_dibusb *dib, u8 *buf, u16 len);

int dibusb_hw_wakeup(struct dvb_frontend *);
int dibusb_hw_sleep(struct dvb_frontend *);
int dibusb_set_streaming_mode(struct usb_dibusb *,u8);
int dibusb_streaming(struct usb_dibusb *,int);

int dibusb_urb_init(struct usb_dibusb *);
int dibusb_urb_exit(struct usb_dibusb *);

/* dvb-fe-dtt200u.c */
struct dvb_frontend* dtt200u_fe_attach(struct usb_dibusb *,struct dib_fe_xfer_ops *);

/* i2c and transfer stuff */
#define DIBUSB_I2C_TIMEOUT				5000

/*
 * protocol of all dibusb related devices
 */

/*
 * bulk msg to/from endpoint 0x01
 *
 * general structure:
 * request_byte parameter_bytes
 */

#define DIBUSB_REQ_START_READ			0x00
#define DIBUSB_REQ_START_DEMOD			0x01

/*
 * i2c read
 * bulk write: 0x02 ((7bit i2c_addr << 1) & 0x01) register_bytes length_word
 * bulk read:  byte_buffer (length_word bytes)
 */
#define DIBUSB_REQ_I2C_READ			0x02

/*
 * i2c write
 * bulk write: 0x03 (7bit i2c_addr << 1) register_bytes value_bytes
 */
#define DIBUSB_REQ_I2C_WRITE			0x03

/*
 * polling the value of the remote control
 * bulk write: 0x04
 * bulk read:  byte_buffer (5 bytes)
 *
 * first byte of byte_buffer shows the status (0x00, 0x01, 0x02)
 */
#define DIBUSB_REQ_POLL_REMOTE			0x04

#define DIBUSB_RC_NEC_EMPTY				0x00
#define DIBUSB_RC_NEC_KEY_PRESSED		0x01
#define DIBUSB_RC_NEC_KEY_REPEATED		0x02

/* additional status values for Hauppauge Remote Control Protocol */
#define DIBUSB_RC_HAUPPAUGE_KEY_PRESSED	0x01
#define DIBUSB_RC_HAUPPAUGE_KEY_EMPTY	0x03

/* streaming mode:
 * bulk write: 0x05 mode_byte
 *
 * mode_byte is mostly 0x00
 */
#define DIBUSB_REQ_SET_STREAMING_MODE	0x05

/* interrupt the internal read loop, when blocking */
#define DIBUSB_REQ_INTR_READ			0x06

/* io control
 * 0x07 cmd_byte param_bytes
 *
 * param_bytes can be up to 32 bytes
 *
 * cmd_byte function    parameter name
 * 0x00     power mode
 *                      0x00      sleep
 *                      0x01      wakeup
 *
 * 0x01     enable streaming
 * 0x02     disable streaming
 *
 *
 */
#define DIBUSB_REQ_SET_IOCTL			0x07

/* IOCTL commands */

/* change the power mode in firmware */
#define DIBUSB_IOCTL_CMD_POWER_MODE		0x00
#define DIBUSB_IOCTL_POWER_SLEEP			0x00
#define DIBUSB_IOCTL_POWER_WAKEUP			0x01

/* modify streaming of the FX2 */
#define DIBUSB_IOCTL_CMD_ENABLE_STREAM	0x01
#define DIBUSB_IOCTL_CMD_DISABLE_STREAM	0x02

#endif
