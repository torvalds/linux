/* dvb-usb.h is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * the headerfile, all dvb-usb-drivers have to include.
 *
 * TODO: clean-up the structures for unused fields and update the comments
 */
#ifndef DVB_USB_H
#define DVB_USB_H

#include <linux/input.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <media/rc-core.h>

#include "dvb_frontend.h"
#include "dvb_demux.h"
#include "dvb_net.h"
#include "dmxdev.h"
#include "dvb-usb-ids.h"

/* helper macros for every DVB USB driver use */
#define adap_to_d(adap) (container_of(adap, struct dvb_usb_device, \
		adapter[adap->id]))
#define adap_to_priv(adap) (adap_to_d(adap)->priv)
#define fe_to_adap(fe) ((struct dvb_usb_adapter *) ((fe)->dvb->priv))
#define fe_to_d(fe) (adap_to_d(fe_to_adap(fe)))
#define fe_to_priv(fe) (fe_to_d(fe)->priv)
#define d_to_priv(d) (d->priv)

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

struct dvb_usb_driver_info {
	const char *name;
	const char *rc_map;
	const struct dvb_usb_device_properties *props;
};

struct dvb_usb_device;
struct dvb_usb_adapter;
struct usb_data_stream;

/**
 * Properties of USB streaming - TODO this structure should be somewhere else
 * describes the kind of USB transfer used for data-streaming.
 *  (BULK or ISOC)
 */
struct usb_data_stream_properties {
#define USB_BULK  1
#define USB_ISOC  2
	int type;
	int count;
	int endpoint;

	union {
		struct {
			int buffersize; /* per URB */
		} bulk;
		struct {
			int framesperurb;
			int framesize;
			int interval;
		} isoc;
	} u;
};

/**
 * struct dvb_usb_adapter_properties - properties of a dvb-usb-adapter.
 * A DVB-USB-Adapter is basically a dvb_adapter which is present on a
 * USB-device.
 * @caps: capabilities of the DVB USB device.
 * @pid_filter_count: number of PID filter position in the optional hardware
 *  PID-filter.
 * @num_frontends: number of frontends of the DVB USB adapter.
 * @frontend_ctrl: called to power on/off active frontend.
 * @streaming_ctrl: called to start and stop the MPEG2-TS streaming of the
 *  device (not URB submitting/killing).
 * @pid_filter_ctrl: called to en/disable the PID filter, if any.
 * @pid_filter: called to set/unset a PID for filtering.
 * @frontend_attach: called to attach the possible frontends (fill fe-field
 *  of struct dvb_usb_device).
 * @tuner_attach: called to attach the correct tuner and to fill pll_addr,
 *  pll_desc and pll_init_buf of struct dvb_usb_device).
 * @stream: configuration of the USB streaming
 */

#define MAX_NO_OF_FE_PER_ADAP 3
struct dvb_usb_adapter_properties {
#define DVB_USB_ADAP_HAS_PID_FILTER               0x01
#define DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF 0x02
#define DVB_USB_ADAP_NEED_PID_FILTERING           0x04
	int caps;
	int size_of_priv;

	int pid_filter_count;
	int (*pid_filter_ctrl) (struct dvb_usb_adapter *, int);
	int (*pid_filter) (struct dvb_usb_adapter *, int, u16, int);

	struct usb_data_stream_properties stream;
};

/**
 * struct dvb_rc properties of remote controller, using rc-core
 * @rc_codes: name of rc codes table
 * @protocol: type of protocol(s) currently used by the driver
 * @allowed_protos: protocol(s) supported by the driver
 * @driver_type: Used to point if a device supports raw mode
 * @change_protocol: callback to change protocol
 * @rc_query: called to query an event event.
 * @rc_interval: time in ms between two queries.
 * @bulk_mode: device supports bulk mode for RC (disable polling mode)
 */
struct dvb_usb_rc {
	char *map_name;
	u64 allowed_protos;
	int (*change_protocol)(struct rc_dev *dev, u64 rc_type);
	int (*query) (struct dvb_usb_device *d);
	int interval;
	const enum rc_driver_type driver_type;
	bool bulk_mode;
};

/**
 * struct dvb_usb_device_properties - properties of a dvb-usb-device
 * @owner: owner of the dvb_adapter
 * @usb_ctrl: which USB device-side controller is in use. Needed for firmware
 *  download.
 * @firmware: name of the firmware file.
 * @download_firmware: called to download the firmware when the usb_ctrl is
 *  DEVICE_SPECIFIC.
 * @no_reconnect: device doesn't do a reconnect after downloading the firmware,
 *  so do the warm initialization right after it
 *
 * @size_of_priv: how many bytes shall be allocated for the private field
 *  of struct dvb_usb_device.
 *
 * @power_ctrl: called to enable/disable power of the device.
 * @read_mac_address: called to read the MAC address of the device.
 * @identify_state: called to determine the state (cold or warm), when it
 *  is not distinguishable by the USB IDs.
 * @init: called after adapters are created in order to finalize device
 *  configuration.
 *
 * @rc: remote controller properties
 *
 * @i2c_algo: i2c_algorithm if the device has I2CoverUSB.
 *
 * @generic_bulk_ctrl_endpoint: most of the DVB USB devices have a generic
 *  endpoint which received control messages with bulk transfers. When this
 *  is non-zero, one can use dvb_usb_generic_rw and dvb_usb_generic_write-
 *  helper functions.
 *
 * @generic_bulk_ctrl_endpoint_response: some DVB USB devices use a separate
 *  endpoint for responses to control messages sent with bulk transfers via
 *  the generic_bulk_ctrl_endpoint. When this is non-zero, this will be used
 *  instead of the generic_bulk_ctrl_endpoint when reading usb responses in
 *  the dvb_usb_generic_rw helper function.
 */
#define MAX_NO_OF_ADAPTER_PER_DEVICE 2
struct dvb_usb_device_properties {
	const char *driver_name;
	struct module *owner;
	short *adapter_nr;
	u8 bInterfaceNumber;

	int size_of_priv;

	const char *firmware;
	int (*get_firmware_name) (struct dvb_usb_device *, const char **);
#define RECONNECTS_USB                  1
	int (*download_firmware) (struct dvb_usb_device *,
			const struct firmware *);

	int num_adapters;
	int (*get_adapter_count) (struct dvb_usb_device *);
	struct dvb_usb_adapter_properties adapter[MAX_NO_OF_ADAPTER_PER_DEVICE];

	int (*power_ctrl)       (struct dvb_usb_device *, int);
	int (*read_config) (struct dvb_usb_device *d);
	int (*read_mac_address) (struct dvb_usb_adapter *, u8 []);
	int (*frontend_attach) (struct dvb_usb_adapter *);
	int (*tuner_attach) (struct dvb_usb_adapter *);
	int (*frontend_ctrl) (struct dvb_frontend *, int);
	int (*streaming_ctrl) (struct dvb_usb_adapter *, int);
	int (*fe_ioctl_override) (struct dvb_frontend *,
			unsigned int, void *, unsigned int);

#define WARM                  0
#define COLD                  1
	int (*identify_state) (struct dvb_usb_device *);
	int (*init) (struct dvb_usb_device *);
	void (*disconnect) (struct dvb_usb_device *);
	int (*get_rc_config) (struct dvb_usb_device *, struct dvb_usb_rc *);
#define DVB_USB_FE_TS_TYPE_188        0
#define DVB_USB_FE_TS_TYPE_204        1
#define DVB_USB_FE_TS_TYPE_RAW        2
	int (*get_stream_config) (struct dvb_frontend *,  u8 *,
			struct usb_data_stream_properties *);

	struct i2c_algorithm *i2c_algo;

	int generic_bulk_ctrl_endpoint;
	int generic_bulk_ctrl_endpoint_response;
};

/**
 * struct usb_data_stream - generic object of an USB stream
 * @buf_num: number of buffer allocated.
 * @buf_size: size of each buffer in buf_list.
 * @buf_list: array containing all allocate buffers for streaming.
 * @dma_addr: list of dma_addr_t for each buffer in buf_list.
 *
 * @urbs_initialized: number of URBs initialized.
 * @urbs_submitted: number of URBs submitted.
 */
#define MAX_NO_URBS_FOR_DATA_STREAM 10
struct usb_data_stream {
	struct usb_device                 *udev;
	struct usb_data_stream_properties  props;

#define USB_STATE_INIT    0x00
#define USB_STATE_URB_BUF 0x01
	int state;

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
 * struct dvb_usb_adapter - a DVB adapter on a USB device
 * @id: index of this adapter (starting with 0).
 *
 * @feedcount: number of reqested feeds (used for streaming-activation)
 * @pid_filtering: is hardware pid_filtering used or not.
 *
 * @pll_addr: I2C address of the tuner for programming
 * @pll_init: array containing the initialization buffer
 * @pll_desc: pointer to the appropriate struct dvb_pll_desc
 * @tuner_pass_ctrl: called to (de)activate tuner passthru of the demod or
 * the board
 *
 * @dvb_adap: device's dvb_adapter.
 * @dmxdev: device's dmxdev.
 * @demux: device's software demuxer.
 * @dvb_net: device's dvb_net interfaces.
 * @dvb_frontend: device's frontend.
 * @max_feed_count: how many feeds can be handled simultaneously by this
 *  device
 *
 * @fe_init:  rerouted frontend-init (wakeup) function.
 * @fe_sleep: rerouted frontend-sleep function.
 *
 * @stream: the usb data stream.
 */
struct dvb_usb_adapter {
	const struct dvb_usb_adapter_properties *props;
	struct usb_data_stream stream;
	u8  id;
	u8 ts_type;
	int pid_filtering;
	int feedcount;
	int max_feed_count;

	/* sync frontend and streaming as those are different tasks */
	struct mutex sync_mutex;

	/* dvb */
	struct dvb_adapter   dvb_adap;
	struct dmxdev        dmxdev;
	struct dvb_demux     demux;
	struct dvb_net       dvb_net;

	struct dvb_frontend *fe[MAX_NO_OF_FE_PER_ADAP];
	int (*fe_init[MAX_NO_OF_FE_PER_ADAP]) (struct dvb_frontend *);
	int (*fe_sleep[MAX_NO_OF_FE_PER_ADAP]) (struct dvb_frontend *);

	int active_fe;
};

/**
 * struct dvb_usb_device - object of a DVB USB device
 * @props: copy of the struct dvb_usb_properties this device belongs to.
 * @desc: pointer to the device's struct dvb_usb_device_description.
 * @state: initialization and runtime state of the device.
 *
 * @powered: indicated whether the device is power or not.
 *  Powered is in/decremented for each call to modify the state.
 * @udev: pointer to the device's struct usb_device.
 *
 * @usb_mutex: semaphore of USB control messages (reading needs two messages)
 * @i2c_mutex: semaphore for i2c-transfers
 *
 * @i2c_adap: device's i2c_adapter if it uses I2CoverUSB
 *
 * @rc_dev: rc device for the remote control (rc-core mode)
 * @input_dev: input device for the remote control (legacy mode)
 * @rc_query_work: struct work_struct frequent rc queries
 * @last_event: last triggered event
 * @last_state: last state (no, pressed, repeat)
 * @priv: private data of the actual driver (allocate by dvb-usb, size defined
 *  in size_of_priv of dvb_usb_properties).
 */
struct dvb_usb_device {
	const struct dvb_usb_device_properties *props;
	const char *name;
	const char *rc_map;
	struct dvb_usb_rc rc;
	struct usb_device *udev;
	struct work_struct probe_work;
	pid_t work_pid;
	struct usb_interface *intf;
	int powered;

	/* locking */
	struct mutex usb_mutex;

	/* i2c */
	struct mutex i2c_mutex;
	struct i2c_adapter i2c_adap;

	int num_adapters_initialized;
	struct dvb_usb_adapter adapter[MAX_NO_OF_ADAPTER_PER_DEVICE];

	/* remote control */
	struct rc_dev *rc_dev;
	struct input_dev *input_dev;
	char rc_phys[64];
	struct delayed_work rc_query_work;

	void *priv;
};

extern int dvb_usbv2_probe(struct usb_interface *,
		const struct usb_device_id *);
extern void dvb_usbv2_disconnect(struct usb_interface *);
extern int dvb_usbv2_suspend(struct usb_interface *, pm_message_t);
extern int dvb_usbv2_resume(struct usb_interface *);

/* the generic read/write method for device control */
extern int dvb_usbv2_generic_rw(struct dvb_usb_device *, u8 *, u16, u8 *, u16,
		int);
extern int dvb_usbv2_generic_write(struct dvb_usb_device *, u8 *, u16);

#endif
