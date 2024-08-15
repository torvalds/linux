/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The Virtual DTV test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * When this module is loaded, it will attempt to modprobe 'dvb_vidtv_tuner' and 'dvb_vidtv_demod'.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_BRIDGE_H
#define VIDTV_BRIDGE_H

/*
 * For now, only one frontend is supported. See vidtv_start_streaming()
 */
#define NUM_FE 1
#define VIDTV_PDEV_NAME "vidtv"

#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <media/dmxdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/media-device.h>

#include "vidtv_mux.h"

/**
 * struct vidtv_dvb - Vidtv bridge state
 * @pdev: The platform device. Obtained when the bridge is probed.
 * @fe: The frontends. Obtained when probing the demodulator modules.
 * @adapter: Represents a DTV adapter. See 'dvb_register_adapter'.
 * @demux: The demux used by the dvb_dmx_swfilter_packets() call.
 * @dmx_dev: Represents a demux device.
 * @dmx_fe: The frontends associated with the demux.
 * @i2c_adapter: The i2c_adapter associated with the bridge driver.
 * @i2c_client_demod: The i2c_clients associated with the demodulator modules.
 * @i2c_client_tuner: The i2c_clients associated with the tuner modules.
 * @nfeeds: The number of feeds active.
 * @feed_lock: Protects access to the start/stop stream logic/data.
 * @streaming: Whether we are streaming now.
 * @mux: The abstraction responsible for delivering MPEG TS packets to the bridge.
 * @mdev: The media_device struct for media controller support.
 */
struct vidtv_dvb {
	struct platform_device *pdev;
	struct dvb_frontend *fe[NUM_FE];
	struct dvb_adapter adapter;
	struct dvb_demux demux;
	struct dmxdev dmx_dev;
	struct dmx_frontend dmx_fe[NUM_FE];
	struct i2c_adapter i2c_adapter;
	struct i2c_client *i2c_client_demod[NUM_FE];
	struct i2c_client *i2c_client_tuner[NUM_FE];

	u32 nfeeds;
	struct mutex feed_lock; /* Protects access to the start/stop stream logic/data. */

	bool streaming;

	struct vidtv_mux *mux;

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	struct media_device mdev;
#endif /* CONFIG_MEDIA_CONTROLLER_DVB */
};

#endif // VIDTV_BRIDG_H
