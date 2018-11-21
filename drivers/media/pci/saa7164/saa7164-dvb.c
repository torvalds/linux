/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010-2015 Steven Toth <stoth@kernellabs.com>
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
 */

#include "saa7164.h"

#include "tda10048.h"
#include "tda18271.h"
#include "s5h1411.h"
#include "si2157.h"
#include "si2168.h"
#include "lgdt3306a.h"

#define DRIVER_NAME "saa7164"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* addr is in the card struct, get it from there */
static struct tda10048_config hauppauge_hvr2200_1_config = {
	.demod_address    = 0x10 >> 1,
	.output_mode      = TDA10048_SERIAL_OUTPUT,
	.fwbulkwritelen   = TDA10048_BULKWRITE_200,
	.inversion        = TDA10048_INVERSION_ON,
	.dtv6_if_freq_khz = TDA10048_IF_3300,
	.dtv7_if_freq_khz = TDA10048_IF_3500,
	.dtv8_if_freq_khz = TDA10048_IF_4000,
	.clk_freq_khz     = TDA10048_CLK_16000,
};
static struct tda10048_config hauppauge_hvr2200_2_config = {
	.demod_address    = 0x12 >> 1,
	.output_mode      = TDA10048_SERIAL_OUTPUT,
	.fwbulkwritelen   = TDA10048_BULKWRITE_200,
	.inversion        = TDA10048_INVERSION_ON,
	.dtv6_if_freq_khz = TDA10048_IF_3300,
	.dtv7_if_freq_khz = TDA10048_IF_3500,
	.dtv8_if_freq_khz = TDA10048_IF_4000,
	.clk_freq_khz     = TDA10048_CLK_16000,
};

static struct tda18271_std_map hauppauge_tda18271_std_map = {
	.atsc_6   = { .if_freq = 3250, .agc_mode = 3, .std = 3,
		      .if_lvl = 6, .rfagc_top = 0x37 },
	.qam_6    = { .if_freq = 4000, .agc_mode = 3, .std = 0,
		      .if_lvl = 6, .rfagc_top = 0x37 },
};

static struct tda18271_config hauppauge_hvr22x0_tuner_config = {
	.std_map	= &hauppauge_tda18271_std_map,
	.gate		= TDA18271_GATE_ANALOG,
	.role		= TDA18271_MASTER,
};

static struct tda18271_config hauppauge_hvr22x0s_tuner_config = {
	.std_map	= &hauppauge_tda18271_std_map,
	.gate		= TDA18271_GATE_ANALOG,
	.role		= TDA18271_SLAVE,
	.output_opt     = TDA18271_OUTPUT_LT_OFF,
	.rf_cal_on_startup = 1
};

static struct s5h1411_config hauppauge_s5h1411_config = {
	.output_mode   = S5H1411_SERIAL_OUTPUT,
	.gpio          = S5H1411_GPIO_ON,
	.qam_if        = S5H1411_IF_4000,
	.vsb_if        = S5H1411_IF_3250,
	.inversion     = S5H1411_INVERSION_ON,
	.status_mode   = S5H1411_DEMODLOCKING,
	.mpeg_timing   = S5H1411_MPEGTIMING_CONTINUOUS_NONINVERTING_CLOCK,
};

static struct lgdt3306a_config hauppauge_hvr2255a_config = {
	.i2c_addr               = 0xb2 >> 1,
	.qam_if_khz             = 4000,
	.vsb_if_khz             = 3250,
	.deny_i2c_rptr          = 1, /* Disabled */
	.spectral_inversion     = 0, /* Disabled */
	.mpeg_mode              = LGDT3306A_MPEG_SERIAL,
	.tpclk_edge             = LGDT3306A_TPCLK_RISING_EDGE,
	.tpvalid_polarity       = LGDT3306A_TP_VALID_HIGH,
	.xtalMHz                = 25, /* 24 or 25 */
};

static struct lgdt3306a_config hauppauge_hvr2255b_config = {
	.i2c_addr               = 0x1c >> 1,
	.qam_if_khz             = 4000,
	.vsb_if_khz             = 3250,
	.deny_i2c_rptr          = 1, /* Disabled */
	.spectral_inversion     = 0, /* Disabled */
	.mpeg_mode              = LGDT3306A_MPEG_SERIAL,
	.tpclk_edge             = LGDT3306A_TPCLK_RISING_EDGE,
	.tpvalid_polarity       = LGDT3306A_TP_VALID_HIGH,
	.xtalMHz                = 25, /* 24 or 25 */
};

static struct si2157_config hauppauge_hvr2255_tuner_config = {
	.inversion = 1,
	.if_port = 1,
};

static int si2157_attach(struct saa7164_port *port, struct i2c_adapter *adapter,
	struct dvb_frontend *fe, u8 addr8bit, struct si2157_config *cfg)
{
	struct i2c_board_info bi;
	struct i2c_client *tuner;

	cfg->fe = fe;

	memset(&bi, 0, sizeof(bi));

	strlcpy(bi.type, "si2157", I2C_NAME_SIZE);
	bi.platform_data = cfg;
	bi.addr = addr8bit >> 1;

	request_module(bi.type);

	tuner = i2c_new_device(adapter, &bi);
	if (tuner == NULL || tuner->dev.driver == NULL)
		return -ENODEV;

	if (!try_module_get(tuner->dev.driver->owner)) {
		i2c_unregister_device(tuner);
		return -ENODEV;
	}

	port->i2c_client_tuner = tuner;

	return 0;
}

static int saa7164_dvb_stop_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() stop transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_DVB, "%s()    Stopped\n", __func__);
		ret = 0;
	}

	return ret;
}

static int saa7164_dvb_acquire_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_ACQUIRE);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() acquire transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_DVB, "%s() Acquired\n", __func__);
		ret = 0;
	}

	return ret;
}

static int saa7164_dvb_pause_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_PAUSE);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() pause transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_DVB, "%s()   Paused\n", __func__);
		ret = 0;
	}

	return ret;
}

/* Firmware is very windows centric, meaning you have to transition
 * the part through AVStream / KS Windows stages, forwards or backwards.
 * States are: stopped, acquired (h/w), paused, started.
 */
static int saa7164_dvb_stop_streaming(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct list_head *p, *q;
	int ret;

	dprintk(DBGLVL_DVB, "%s(port=%d)\n", __func__, port->nr);

	ret = saa7164_dvb_pause_port(port);
	ret = saa7164_dvb_acquire_port(port);
	ret = saa7164_dvb_stop_port(port);

	/* Mark the hardware buffers as free */
	mutex_lock(&port->dmaqueue_lock);
	list_for_each_safe(p, q, &port->dmaqueue.list) {
		buf = list_entry(p, struct saa7164_buffer, list);
		buf->flags = SAA7164_BUFFER_FREE;
	}
	mutex_unlock(&port->dmaqueue_lock);

	return ret;
}

static int saa7164_dvb_start_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret = 0, result;

	dprintk(DBGLVL_DVB, "%s(port=%d)\n", __func__, port->nr);

	saa7164_buffer_cfg_port(port);

	/* Acquire the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_ACQUIRE);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() acquire transition failed, res = 0x%x\n",
			__func__, result);

		/* Stop the hardware, regardless */
		result = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
		if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
			printk(KERN_ERR "%s() acquire/forced stop transition failed, res = 0x%x\n",
			       __func__, result);
		}
		ret = -EIO;
		goto out;
	} else
		dprintk(DBGLVL_DVB, "%s()   Acquired\n", __func__);

	/* Pause the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_PAUSE);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() pause transition failed, res = 0x%x\n",
				__func__, result);

		/* Stop the hardware, regardless */
		result = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
		if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
			printk(KERN_ERR "%s() pause/forced stop transition failed, res = 0x%x\n",
			       __func__, result);
		}

		ret = -EIO;
		goto out;
	} else
		dprintk(DBGLVL_DVB, "%s()   Paused\n", __func__);

	/* Start the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_RUN);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() run transition failed, result = 0x%x\n",
				__func__, result);

		/* Stop the hardware, regardless */
		result = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
		if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
			printk(KERN_ERR "%s() run/forced stop transition failed, res = 0x%x\n",
			       __func__, result);
		}

		ret = -EIO;
	} else
		dprintk(DBGLVL_DVB, "%s()   Running\n", __func__);

out:
	return ret;
}

static int saa7164_dvb_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct saa7164_port *port = (struct saa7164_port *) demux->priv;
	struct saa7164_dvb *dvb = &port->dvb;
	struct saa7164_dev *dev = port->dev;
	int ret = 0;

	dprintk(DBGLVL_DVB, "%s(port=%d)\n", __func__, port->nr);

	if (!demux->dmx.frontend)
		return -EINVAL;

	if (dvb) {
		mutex_lock(&dvb->lock);
		if (dvb->feeding++ == 0) {
			/* Start transport */
			ret = saa7164_dvb_start_port(port);
		}
		mutex_unlock(&dvb->lock);
		dprintk(DBGLVL_DVB, "%s(port=%d) now feeding = %d\n",
			__func__, port->nr, dvb->feeding);
	}

	return ret;
}

static int saa7164_dvb_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct saa7164_port *port = (struct saa7164_port *) demux->priv;
	struct saa7164_dvb *dvb = &port->dvb;
	struct saa7164_dev *dev = port->dev;
	int ret = 0;

	dprintk(DBGLVL_DVB, "%s(port=%d)\n", __func__, port->nr);

	if (dvb) {
		mutex_lock(&dvb->lock);
		if (--dvb->feeding == 0) {
			/* Stop transport */
			ret = saa7164_dvb_stop_streaming(port);
		}
		mutex_unlock(&dvb->lock);
		dprintk(DBGLVL_DVB, "%s(port=%d) now feeding = %d\n",
			__func__, port->nr, dvb->feeding);
	}

	return ret;
}

static int dvb_register(struct saa7164_port *port)
{
	struct saa7164_dvb *dvb = &port->dvb;
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	int result, i;

	dprintk(DBGLVL_DVB, "%s(port=%d)\n", __func__, port->nr);

	if (port->type != SAA7164_MPEG_DVB)
		BUG();

	/* Sanity check that the PCI configuration space is active */
	if (port->hwcfg.BARLocation == 0) {
		result = -ENOMEM;
		printk(KERN_ERR "%s: dvb_register_adapter failed (errno = %d), NO PCI configuration\n",
			DRIVER_NAME, result);
		goto fail_adapter;
	}

	/* Init and establish defaults */
	port->hw_streamingparams.bitspersample = 8;
	port->hw_streamingparams.samplesperline = 188;
	port->hw_streamingparams.numberoflines =
		(SAA7164_TS_NUMBER_OF_LINES * 188) / 188;

	port->hw_streamingparams.pitch = 188;
	port->hw_streamingparams.linethreshold = 0;
	port->hw_streamingparams.pagetablelistvirt = NULL;
	port->hw_streamingparams.pagetablelistphys = NULL;
	port->hw_streamingparams.numpagetables = 2 +
		((SAA7164_TS_NUMBER_OF_LINES * 188) / PAGE_SIZE);

	port->hw_streamingparams.numpagetableentries = port->hwcfg.buffercount;

	/* Allocate the PCI resources */
	for (i = 0; i < port->hwcfg.buffercount; i++) {
		buf = saa7164_buffer_alloc(port,
			port->hw_streamingparams.numberoflines *
			port->hw_streamingparams.pitch);

		if (!buf) {
			result = -ENOMEM;
			printk(KERN_ERR "%s: dvb_register_adapter failed (errno = %d), unable to allocate buffers\n",
				DRIVER_NAME, result);
			goto fail_adapter;
		}

		mutex_lock(&port->dmaqueue_lock);
		list_add_tail(&buf->list, &port->dmaqueue.list);
		mutex_unlock(&port->dmaqueue_lock);
	}

	/* register adapter */
	result = dvb_register_adapter(&dvb->adapter, DRIVER_NAME, THIS_MODULE,
			&dev->pci->dev, adapter_nr);
	if (result < 0) {
		printk(KERN_ERR "%s: dvb_register_adapter failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_adapter;
	}
	dvb->adapter.priv = port;

	/* register frontend */
	result = dvb_register_frontend(&dvb->adapter, dvb->frontend);
	if (result < 0) {
		printk(KERN_ERR "%s: dvb_register_frontend failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_frontend;
	}

	/* register demux stuff */
	dvb->demux.dmx.capabilities =
		DMX_TS_FILTERING | DMX_SECTION_FILTERING |
		DMX_MEMORY_BASED_FILTERING;
	dvb->demux.priv       = port;
	dvb->demux.filternum  = 256;
	dvb->demux.feednum    = 256;
	dvb->demux.start_feed = saa7164_dvb_start_feed;
	dvb->demux.stop_feed  = saa7164_dvb_stop_feed;
	result = dvb_dmx_init(&dvb->demux);
	if (result < 0) {
		printk(KERN_ERR "%s: dvb_dmx_init failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_dmx;
	}

	dvb->dmxdev.filternum    = 256;
	dvb->dmxdev.demux        = &dvb->demux.dmx;
	dvb->dmxdev.capabilities = 0;
	result = dvb_dmxdev_init(&dvb->dmxdev, &dvb->adapter);
	if (result < 0) {
		printk(KERN_ERR "%s: dvb_dmxdev_init failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_dmxdev;
	}

	dvb->fe_hw.source = DMX_FRONTEND_0;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		printk(KERN_ERR "%s: add_frontend failed (DMX_FRONTEND_0, errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_fe_hw;
	}

	dvb->fe_mem.source = DMX_MEMORY_FE;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	if (result < 0) {
		printk(KERN_ERR "%s: add_frontend failed (DMX_MEMORY_FE, errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_fe_mem;
	}

	result = dvb->demux.dmx.connect_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		printk(KERN_ERR "%s: connect_frontend failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_fe_conn;
	}

	/* register network adapter */
	dvb_net_init(&dvb->adapter, &dvb->net, &dvb->demux.dmx);
	return 0;

fail_fe_conn:
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
fail_fe_mem:
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
fail_fe_hw:
	dvb_dmxdev_release(&dvb->dmxdev);
fail_dmxdev:
	dvb_dmx_release(&dvb->demux);
fail_dmx:
	dvb_unregister_frontend(dvb->frontend);
fail_frontend:
	dvb_frontend_detach(dvb->frontend);
	dvb_unregister_adapter(&dvb->adapter);
fail_adapter:
	return result;
}

int saa7164_dvb_unregister(struct saa7164_port *port)
{
	struct saa7164_dvb *dvb = &port->dvb;
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *b;
	struct list_head *c, *n;
	struct i2c_client *client;

	dprintk(DBGLVL_DVB, "%s()\n", __func__);

	if (port->type != SAA7164_MPEG_DVB)
		BUG();

	/* Remove any allocated buffers */
	mutex_lock(&port->dmaqueue_lock);
	list_for_each_safe(c, n, &port->dmaqueue.list) {
		b = list_entry(c, struct saa7164_buffer, list);
		list_del(c);
		saa7164_buffer_dealloc(b);
	}
	mutex_unlock(&port->dmaqueue_lock);

	if (dvb->frontend == NULL)
		return 0;

	/* remove I2C client for tuner */
	client = port->i2c_client_tuner;
	if (client) {
		module_put(client->dev.driver->owner);
		i2c_unregister_device(client);
	}

	/* remove I2C client for demodulator */
	client = port->i2c_client_demod;
	if (client) {
		module_put(client->dev.driver->owner);
		i2c_unregister_device(client);
	}

	dvb_net_release(&dvb->net);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	dvb_dmxdev_release(&dvb->dmxdev);
	dvb_dmx_release(&dvb->demux);
	dvb_unregister_frontend(dvb->frontend);
	dvb_frontend_detach(dvb->frontend);
	dvb_unregister_adapter(&dvb->adapter);
	return 0;
}

/* All the DVB attach calls go here, this function get's modified
 * for each new card.
 */
int saa7164_dvb_register(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_dvb *dvb = &port->dvb;
	struct saa7164_i2c *i2c_bus = NULL;
	struct si2168_config si2168_config;
	struct si2157_config si2157_config;
	struct i2c_adapter *adapter;
	struct i2c_board_info info;
	struct i2c_client *client_demod;
	struct i2c_client *client_tuner;
	int ret;

	dprintk(DBGLVL_DVB, "%s()\n", __func__);

	/* init frontend */
	switch (dev->board) {
	case SAA7164_BOARD_HAUPPAUGE_HVR2200:
	case SAA7164_BOARD_HAUPPAUGE_HVR2200_2:
	case SAA7164_BOARD_HAUPPAUGE_HVR2200_3:
	case SAA7164_BOARD_HAUPPAUGE_HVR2200_4:
	case SAA7164_BOARD_HAUPPAUGE_HVR2200_5:
		i2c_bus = &dev->i2c_bus[port->nr + 1];
		switch (port->nr) {
		case 0:
			port->dvb.frontend = dvb_attach(tda10048_attach,
				&hauppauge_hvr2200_1_config,
				&i2c_bus->i2c_adap);

			if (port->dvb.frontend != NULL) {
				/* TODO: addr is in the card struct */
				dvb_attach(tda18271_attach, port->dvb.frontend,
					0xc0 >> 1, &i2c_bus->i2c_adap,
					&hauppauge_hvr22x0_tuner_config);
			}

			break;
		case 1:
			port->dvb.frontend = dvb_attach(tda10048_attach,
				&hauppauge_hvr2200_2_config,
				&i2c_bus->i2c_adap);

			if (port->dvb.frontend != NULL) {
				/* TODO: addr is in the card struct */
				dvb_attach(tda18271_attach, port->dvb.frontend,
					0xc0 >> 1, &i2c_bus->i2c_adap,
					&hauppauge_hvr22x0s_tuner_config);
			}

			break;
		}
		break;
	case SAA7164_BOARD_HAUPPAUGE_HVR2250:
	case SAA7164_BOARD_HAUPPAUGE_HVR2250_2:
	case SAA7164_BOARD_HAUPPAUGE_HVR2250_3:
		i2c_bus = &dev->i2c_bus[port->nr + 1];

		port->dvb.frontend = dvb_attach(s5h1411_attach,
			&hauppauge_s5h1411_config,
			&i2c_bus->i2c_adap);

		if (port->dvb.frontend != NULL) {
			if (port->nr == 0) {
				/* Master TDA18271 */
				/* TODO: addr is in the card struct */
				dvb_attach(tda18271_attach, port->dvb.frontend,
					0xc0 >> 1, &i2c_bus->i2c_adap,
					&hauppauge_hvr22x0_tuner_config);
			} else {
				/* Slave TDA18271 */
				dvb_attach(tda18271_attach, port->dvb.frontend,
					0xc0 >> 1, &i2c_bus->i2c_adap,
					&hauppauge_hvr22x0s_tuner_config);
			}
		}

		break;
	case SAA7164_BOARD_HAUPPAUGE_HVR2255proto:
	case SAA7164_BOARD_HAUPPAUGE_HVR2255:
		i2c_bus = &dev->i2c_bus[2];

		if (port->nr == 0) {
			port->dvb.frontend = dvb_attach(lgdt3306a_attach,
				&hauppauge_hvr2255a_config, &i2c_bus->i2c_adap);
		} else {
			port->dvb.frontend = dvb_attach(lgdt3306a_attach,
				&hauppauge_hvr2255b_config, &i2c_bus->i2c_adap);
		}

		if (port->dvb.frontend != NULL) {

			if (port->nr == 0) {
				si2157_attach(port, &dev->i2c_bus[0].i2c_adap,
					      port->dvb.frontend, 0xc0,
					      &hauppauge_hvr2255_tuner_config);
			} else {
				si2157_attach(port, &dev->i2c_bus[1].i2c_adap,
					      port->dvb.frontend, 0xc0,
					      &hauppauge_hvr2255_tuner_config);
			}
		}
		break;
	case SAA7164_BOARD_HAUPPAUGE_HVR2205:

		if (port->nr == 0) {
			/* attach frontend */
			memset(&si2168_config, 0, sizeof(si2168_config));
			si2168_config.i2c_adapter = &adapter;
			si2168_config.fe = &port->dvb.frontend;
			si2168_config.ts_mode = SI2168_TS_SERIAL;
			memset(&info, 0, sizeof(struct i2c_board_info));
			strlcpy(info.type, "si2168", I2C_NAME_SIZE);
			info.addr = 0xc8 >> 1;
			info.platform_data = &si2168_config;
			request_module(info.type);
			client_demod = i2c_new_device(&dev->i2c_bus[2].i2c_adap,
						      &info);
			if (!client_demod || !client_demod->dev.driver)
				goto frontend_detach;

			if (!try_module_get(client_demod->dev.driver->owner)) {
				i2c_unregister_device(client_demod);
				goto frontend_detach;
			}
			port->i2c_client_demod = client_demod;

			/* attach tuner */
			memset(&si2157_config, 0, sizeof(si2157_config));
			si2157_config.if_port = 1;
			si2157_config.fe = port->dvb.frontend;
			memset(&info, 0, sizeof(struct i2c_board_info));
			strlcpy(info.type, "si2157", I2C_NAME_SIZE);
			info.addr = 0xc0 >> 1;
			info.platform_data = &si2157_config;
			request_module(info.type);
			client_tuner = i2c_new_device(&dev->i2c_bus[0].i2c_adap,
						      &info);
			if (!client_tuner || !client_tuner->dev.driver) {
				module_put(client_demod->dev.driver->owner);
				i2c_unregister_device(client_demod);
				goto frontend_detach;
			}
			if (!try_module_get(client_tuner->dev.driver->owner)) {
				i2c_unregister_device(client_tuner);
				module_put(client_demod->dev.driver->owner);
				i2c_unregister_device(client_demod);
				goto frontend_detach;
			}
			port->i2c_client_tuner = client_tuner;
		} else {
			/* attach frontend */
			memset(&si2168_config, 0, sizeof(si2168_config));
			si2168_config.i2c_adapter = &adapter;
			si2168_config.fe = &port->dvb.frontend;
			si2168_config.ts_mode = SI2168_TS_SERIAL;
			memset(&info, 0, sizeof(struct i2c_board_info));
			strlcpy(info.type, "si2168", I2C_NAME_SIZE);
			info.addr = 0xcc >> 1;
			info.platform_data = &si2168_config;
			request_module(info.type);
			client_demod = i2c_new_device(&dev->i2c_bus[2].i2c_adap,
						      &info);
			if (!client_demod || !client_demod->dev.driver)
				goto frontend_detach;

			if (!try_module_get(client_demod->dev.driver->owner)) {
				i2c_unregister_device(client_demod);
				goto frontend_detach;
			}
			port->i2c_client_demod = client_demod;

			/* attach tuner */
			memset(&si2157_config, 0, sizeof(si2157_config));
			si2157_config.fe = port->dvb.frontend;
			si2157_config.if_port = 1;
			memset(&info, 0, sizeof(struct i2c_board_info));
			strlcpy(info.type, "si2157", I2C_NAME_SIZE);
			info.addr = 0xc0 >> 1;
			info.platform_data = &si2157_config;
			request_module(info.type);
			client_tuner = i2c_new_device(&dev->i2c_bus[1].i2c_adap,
						      &info);
			if (!client_tuner || !client_tuner->dev.driver) {
				module_put(client_demod->dev.driver->owner);
				i2c_unregister_device(client_demod);
				goto frontend_detach;
			}
			if (!try_module_get(client_tuner->dev.driver->owner)) {
				i2c_unregister_device(client_tuner);
				module_put(client_demod->dev.driver->owner);
				i2c_unregister_device(client_demod);
				goto frontend_detach;
			}
			port->i2c_client_tuner = client_tuner;
		}

		break;
	default:
		printk(KERN_ERR "%s: The frontend isn't supported\n",
		       dev->name);
		break;
	}
	if (NULL == dvb->frontend) {
		printk(KERN_ERR "%s() Frontend initialization failed\n",
		       __func__);
		return -1;
	}

	/* register everything */
	ret = dvb_register(port);
	if (ret < 0) {
		if (dvb->frontend->ops.release)
			dvb->frontend->ops.release(dvb->frontend);
		return ret;
	}

	return 0;

frontend_detach:
	printk(KERN_ERR "%s() Frontend/I2C initialization failed\n", __func__);
	return -1;
}

