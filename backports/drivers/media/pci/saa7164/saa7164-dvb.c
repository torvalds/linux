/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010 Steven Toth <stoth@kernellabs.com>
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "saa7164.h"

#include "tda10048.h"
#include "tda18271.h"
#include "s5h1411.h"

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
	.mpeg_timing   = S5H1411_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK,
};

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
			printk(KERN_ERR "%s() acquire/forced stop transition "
				"failed, res = 0x%x\n", __func__, result);
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
			printk(KERN_ERR "%s() pause/forced stop transition "
				"failed, res = 0x%x\n", __func__, result);
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
			printk(KERN_ERR "%s() run/forced stop transition "
				"failed, res = 0x%x\n", __func__, result);
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

	mutex_lock(&dvb->lock);
	if (dvb->feeding++ == 0) {
		/* Start transport */
		ret = saa7164_dvb_start_port(port);
	}
	mutex_unlock(&dvb->lock);
	dprintk(DBGLVL_DVB, "%s(port=%d) now feeding = %d\n",
		__func__, port->nr, dvb->feeding);

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

	mutex_lock(&dvb->lock);
	if (--dvb->feeding == 0) {
		/* Stop transport */
		ret = saa7164_dvb_stop_streaming(port);
	}
	mutex_unlock(&dvb->lock);
	dprintk(DBGLVL_DVB, "%s(port=%d) now feeding = %d\n",
		__func__, port->nr, dvb->feeding);

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
		printk(KERN_ERR "%s: dvb_register_adapter failed "
		       "(errno = %d), NO PCI configuration\n",
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
			printk(KERN_ERR "%s: dvb_register_adapter failed "
			       "(errno = %d), unable to allocate buffers\n",
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
		printk(KERN_ERR "%s: dvb_register_adapter failed "
		       "(errno = %d)\n", DRIVER_NAME, result);
		goto fail_adapter;
	}
	dvb->adapter.priv = port;

	/* register frontend */
	result = dvb_register_frontend(&dvb->adapter, dvb->frontend);
	if (result < 0) {
		printk(KERN_ERR "%s: dvb_register_frontend failed "
		       "(errno = %d)\n", DRIVER_NAME, result);
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
		printk(KERN_ERR "%s: add_frontend failed "
		       "(DMX_FRONTEND_0, errno = %d)\n", DRIVER_NAME, result);
		goto fail_fe_hw;
	}

	dvb->fe_mem.source = DMX_MEMORY_FE;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	if (result < 0) {
		printk(KERN_ERR "%s: add_frontend failed "
		       "(DMX_MEMORY_FE, errno = %d)\n", DRIVER_NAME, result);
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
}

