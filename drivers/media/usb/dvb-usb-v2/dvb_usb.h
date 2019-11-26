/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * DVB USB framework
 *
 * Copyright (C) 2004-6 Patrick Boettcher <patrick.boettcher@posteo.de>
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 */

#ifndef DVB_USB_H
#define DVB_USB_H

#include <linux/usb/input.h>
#include <linux/firmware.h>
#include <media/rc-core.h>
#include <media/media-device.h>

#include <media/dvb_frontend.h>
#include <media/dvb_demux.h>
#include <media/dvb_net.h>
#include <media/dmxdev.h>
#include <media/dvb-usb-ids.h>

/*
 * device file: /dev/dvb/adapter[0-1]/frontend[0-2]
 *
 * |-- device
 * |   |-- adapter0
 * |   |   |-- frontend0
 * |   |   |-- frontend1
 * |   |   `-- frontend2
 * |   `-- adapter1
 * |       |-- frontend0
 * |       |-- frontend1
 * |       `-- frontend2
 *
 *
 * Commonly used variable names:
 * d = pointer to device (struct dvb_usb_device *)
 * adap = pointer to adapter (struct dvb_usb_adapter *)
 * fe = pointer to frontend (struct dvb_frontend *)
 *
 * Use macros defined in that file to resolve needed pointers.
 */

/* helper macros for every DVB USB driver use */
#define adap_to_d(adap) (container_of(adap, struct dvb_usb_device, \
		adapter[adap->id]))
#define adap_to_priv(adap) (adap_to_d(adap)->priv)
#define fe_to_adap(fe) ((struct dvb_usb_adapter *) ((fe)->dvb->priv))
#define fe_to_d(fe) (adap_to_d(fe_to_adap(fe)))
#define fe_to_priv(fe) (fe_to_d(fe)->priv)
#define d_to_priv(d) (d->priv)

#define dvb_usb_dbg_usb_control_msg(udev, r, t, v, i, b, l) { \
	char *direction; \
	if (t == (USB_TYPE_VENDOR | USB_DIR_OUT)) \
		direction = ">>>"; \
	else \
		direction = "<<<"; \
	dev_dbg(&udev->dev, "%s: %02x %02x %02x %02x %02x %02x %02x %02x " \
			"%s %*ph\n",  __func__, t, r, v & 0xff, v >> 8, \
			i & 0xff, i >> 8, l & 0xff, l >> 8, direction, l, b); \
}

#define DVB_USB_STREAM_BULK(endpoint_, count_, size_) { \
	.type = USB_BULK, \
	.count = count_, \
	.endpoint = endpoint_, \
	.u = { \
		.bulk = { \
			.buffersize = size_, \
		} \
	} \
}

#define DVB_USB_STREAM_ISOC(endpoint_, count_, frames_, size_, interval_) { \
	.type = USB_ISOC, \
	.count = count_, \
	.endpoint = endpoint_, \
	.u = { \
		.isoc = { \
			.framesperurb = frames_, \
			.framesize = size_,\
			.interval = interval_, \
		} \
	} \
}

#define DVB_USB_DEVICE(vend, prod, props_, name_, rc) \
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE, \
	.idVendor = (vend), \
	.idProduct = (prod), \
	.driver_info = (kernel_ulong_t) &((const struct dvb_usb_driver_info) { \
		.props = (props_), \
		.name = (name_), \
		.rc_map = (rc), \
	})

struct dvb_usb_device;
struct dvb_usb_adapter;

/**
 * structure for carrying all needed data from the device driver to the general
 * dvb usb routines
 * @name: device name
 * @rc_map: name of rc codes table
 * @props: structure containing all device properties
 */
struct dvb_usb_driver_info {
	const char *name;
	const char *rc_map;
	const struct dvb_usb_device_properties *props;
};

/**
 * structure for remote controller configuration
 * @map_name: name of rc codes table
 * @allowed_protos: protocol(s) supported by the driver
 * @change_protocol: callback to change protocol
 * @query: called to query an event from the device
 * @interval: time in ms between two queries
 * @driver_type: used to point if a device supports raw mode
 * @bulk_mode: device supports bulk mode for rc (disable polling mode)
 * @timeout: set to length of last space before raw IR goes idle
 */
struct dvb_usb_rc {
	const char *map_name;
	u64 allowed_protos;
	int (*change_protocol)(struct rc_dev *dev, u64 *rc_proto);
	int (*query) (struct dvb_usb_device *d);
	unsigned int interval;
	enum rc_driver_type driver_type;
	bool bulk_mode;
	int timeout;
};

/**
 * usb streaming configuration for adapter
 * @type: urb type
 * @count: count of used urbs
 * @endpoint: stream usb endpoint number
 */
struct usb_data_stream_properties {
#define USB_BULK  1
#define USB_ISOC  2
	u8 type;
	u8 count;
	u8 endpoint;

	union {
		struct {
			unsigned int buffersize; /* per URB */
		} bulk;
		struct {
			int framesperurb;
			int framesize;
			int interval;
		} isoc;
	} u;
};

/**
 * properties of dvb usb device adapter
 * @caps: adapter capabilities
 * @pid_filter_count: pid count of adapter pid-filter
 * @pid_filter_ctrl: called to enable/disable pid-filter
 * @pid_filter: called to set/unset pid for filtering
 * @stream: adapter usb stream configuration
 */
#define MAX_NO_OF_FE_PER_ADAP 3
struct dvb_usb_adapter_properties {
#define DVB_USB_ADAP_HAS_PID_FILTER               0x01
#define DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF 0x02
#define DVB_USB_ADAP_NEED_PID_FILTERING           0x04
	u8 caps;

	u8 pid_filter_count;
	int (*pid_filter_ctrl) (struct dvb_usb_adapter *, int);
	int (*pid_filter) (struct dvb_usb_adapter *, int, u16, int);

	struct usb_data_stream_properties stream;
};

/**
 * struct dvb_usb_device_properties - properties of a dvb-usb-device
 * @driver_name: name of the owning driver module
 * @owner: owner of the dvb_adapter
 * @adapter_nr: values from the DVB_DEFINE_MOD_OPT_ADAPTER_NR() macro
 * @bInterfaceNumber: usb interface number driver binds
 * @size_of_priv: bytes allocated for the driver private data
 * @generic_bulk_ctrl_endpoint: bulk control endpoint number for sent
 * @generic_bulk_ctrl_endpoint_response: bulk control endpoint number for
 *  receive
 * @generic_bulk_ctrl_delay: delay between bulk control sent and receive message
 * @probe: like probe on driver model
 * @disconnect: like disconnect on driver model
 * @identify_state: called to determine the firmware state (cold or warm) and
 *  return possible firmware file name to be loaded
 * @firmware: name of the firmware file to be loaded
 * @download_firmware: called to download the firmware
 * @i2c_algo: i2c_algorithm if the device has i2c-adapter
 * @num_adapters: dvb usb device adapter count
 * @get_adapter_count: called to resolve adapter count
 * @adapter: array of all adapter properties of device
 * @power_ctrl: called to enable/disable power of the device
 * @read_config: called to resolve device configuration
 * @read_mac_address: called to resolve adapter mac-address
 * @frontend_attach: called to attach the possible frontends
 * @frontend_detach: called to detach the possible frontends
 * @tuner_attach: called to attach the possible tuners
 * @frontend_ctrl: called to power on/off active frontend
 * @streaming_ctrl: called to start/stop the usb streaming of adapter
 * @init: called after adapters are created in order to finalize device
 *  configuration
 * @exit: called when driver is unloaded
 * @get_rc_config: called to resolve used remote controller configuration
 * @get_stream_config: called to resolve input and output stream configuration
 *  of the adapter just before streaming is started. input stream is transport
 *  stream from the demodulator and output stream is usb stream to host.
 */
#define MAX_NO_OF_ADAPTER_PER_DEVICE 2
struct dvb_usb_device_properties {
	const char *driver_name;
	struct module *owner;
	short *adapter_nr;

	u8 bInterfaceNumber;
	unsigned int size_of_priv;
	u8 generic_bulk_ctrl_endpoint;
	u8 generic_bulk_ctrl_endpoint_response;
	unsigned int generic_bulk_ctrl_delay;

	int (*probe)(struct dvb_usb_device *);
	void (*disconnect)(struct dvb_usb_device *);
#define WARM                  0
#define COLD                  1
	int (*identify_state) (struct dvb_usb_device *, const char **);
	const char *firmware;
#define RECONNECTS_USB        1
	int (*download_firmware) (struct dvb_usb_device *,
			const struct firmware *);

	struct i2c_algorithm *i2c_algo;

	unsigned int num_adapters;
	int (*get_adapter_count) (struct dvb_usb_device *);
	struct dvb_usb_adapter_properties adapter[MAX_NO_OF_ADAPTER_PER_DEVICE];
	int (*power_ctrl) (struct dvb_usb_device *, int);
	int (*read_config) (struct dvb_usb_device *d);
	int (*read_mac_address) (struct dvb_usb_adapter *, u8 []);
	int (*frontend_attach) (struct dvb_usb_adapter *);
	int (*frontend_detach)(struct dvb_usb_adapter *);
	int (*tuner_attach) (struct dvb_usb_adapter *);
	int (*tuner_detach)(struct dvb_usb_adapter *);
	int (*frontend_ctrl) (struct dvb_frontend *, int);
	int (*streaming_ctrl) (struct dvb_frontend *, int);
	int (*init) (struct dvb_usb_device *);
	void (*exit) (struct dvb_usb_device *);
	int (*get_rc_config) (struct dvb_usb_device *, struct dvb_usb_rc *);
#define DVB_USB_FE_TS_TYPE_188        0
#define DVB_USB_FE_TS_TYPE_204        1
#define DVB_USB_FE_TS_TYPE_RAW        2
	int (*get_stream_config) (struct dvb_frontend *,  u8 *,
			struct usb_data_stream_properties *);
};

/**
 * generic object of an usb stream
 * @buf_num: number of buffer allocated
 * @buf_size: size of each buffer in buf_list
 * @buf_list: array containing all allocate buffers for streaming
 * @dma_addr: list of dma_addr_t for each buffer in buf_list
 *
 * @urbs_initialized: number of URBs initialized
 * @urbs_submitted: number of URBs submitted
 */
#define MAX_NO_URBS_FOR_DATA_STREAM 10
struct usb_data_stream {
	struct usb_device *udev;
	struct usb_data_stream_properties props;

#define USB_STATE_INIT    0x00
#define USB_STATE_URB_BUF 0x01
	u8 state;

	void (*complete) (struct usb_data_stream *, u8 *, size_t);

	struct urb    *urb_list[MAX_NO_URBS_FOR_DATA_STREAM];
	int            buf_num;
	unsigned long  buf_size;
	u8            *buf_list[MAX_NO_URBS_FOR_DATA_STREAM];
	dma_addr_t     dma_addr[MAX_NO_URBS_FOR_DATA_STREAM];

	int urbs_initialized;
	int urbs_submitted;

	void *user_priv;
};

/**
 * dvb adapter object on dvb usb device
 * @props: pointer to adapter properties
 * @stream: adapter the usb data stream
 * @id: index of this adapter (starting with 0)
 * @ts_type: transport stream, input stream, type
 * @suspend_resume_active: set when there is ongoing suspend / resume
 * @pid_filtering: is hardware pid_filtering used or not
 * @feed_count: current feed count
 * @max_feed_count: maimum feed count device can handle
 * @dvb_adap: adapter dvb_adapter
 * @dmxdev: adapter dmxdev
 * @demux: adapter software demuxer
 * @dvb_net: adapter dvb_net interfaces
 * @sync_mutex: mutex used to sync control and streaming of the adapter
 * @fe: adapter frontends
 * @fe_init: rerouted frontend-init function
 * @fe_sleep: rerouted frontend-sleep function
 */
struct dvb_usb_adapter {
	const struct dvb_usb_adapter_properties *props;
	struct usb_data_stream stream;
	u8 id;
	u8 ts_type;
	bool suspend_resume_active;
	bool pid_filtering;
	u8 feed_count;
	u8 max_feed_count;
	s8 active_fe;
#define ADAP_INIT                0
#define ADAP_SLEEP               1
#define ADAP_STREAMING           2
	unsigned long state_bits;

	/* dvb */
	struct dvb_adapter   dvb_adap;
	struct dmxdev        dmxdev;
	struct dvb_demux     demux;
	struct dvb_net       dvb_net;

	struct dvb_frontend *fe[MAX_NO_OF_FE_PER_ADAP];
	int (*fe_init[MAX_NO_OF_FE_PER_ADAP]) (struct dvb_frontend *);
	int (*fe_sleep[MAX_NO_OF_FE_PER_ADAP]) (struct dvb_frontend *);
};

/**
 * dvb usb device object
 * @props: device properties
 * @name: device name
 * @rc_map: name of rc codes table
 * @rc_polling_active: set when RC polling is active
 * @intf: pointer to the device's struct usb_interface
 * @udev: pointer to the device's struct usb_device
 * @rc: remote controller configuration
 * @powered: indicated whether the device is power or not
 * @usb_mutex: mutex for usb control messages
 * @i2c_mutex: mutex for i2c-transfers
 * @i2c_adap: device's i2c-adapter
 * @rc_dev: rc device for the remote control
 * @rc_query_work: work for polling remote
 * @priv: private data of the actual driver (allocate by dvb usb, size defined
 *  in size_of_priv of dvb_usb_properties).
 */
struct dvb_usb_device {
	const struct dvb_usb_device_properties *props;
	const char *name;
	const char *rc_map;
	bool rc_polling_active;
	struct usb_interface *intf;
	struct usb_device *udev;
	struct dvb_usb_rc rc;
	int powered;

	/* locking */
	struct mutex usb_mutex;

	/* i2c */
	struct mutex i2c_mutex;
	struct i2c_adapter i2c_adap;

	struct dvb_usb_adapter adapter[MAX_NO_OF_ADAPTER_PER_DEVICE];

	/* remote control */
	struct rc_dev *rc_dev;
	char rc_phys[64];
	struct delayed_work rc_query_work;

	void *priv;
};

extern int dvb_usbv2_probe(struct usb_interface *,
		const struct usb_device_id *);
extern void dvb_usbv2_disconnect(struct usb_interface *);
extern int dvb_usbv2_suspend(struct usb_interface *, pm_message_t);
extern int dvb_usbv2_resume(struct usb_interface *);
extern int dvb_usbv2_reset_resume(struct usb_interface *);

/* the generic read/write method for device control */
extern int dvb_usbv2_generic_rw(struct dvb_usb_device *, u8 *, u16, u8 *, u16);
extern int dvb_usbv2_generic_write(struct dvb_usb_device *, u8 *, u16);
/* caller must hold lock when locked versions are called */
extern int dvb_usbv2_generic_rw_locked(struct dvb_usb_device *,
		u8 *, u16, u8 *, u16);
extern int dvb_usbv2_generic_write_locked(struct dvb_usb_device *, u8 *, u16);

#endif
