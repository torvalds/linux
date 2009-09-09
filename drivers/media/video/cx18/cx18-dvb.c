/*
 *  cx18 functions for DVB support
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
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

#include "cx18-version.h"
#include "cx18-dvb.h"
#include "cx18-io.h"
#include "cx18-queue.h"
#include "cx18-streams.h"
#include "cx18-cards.h"
#include "cx18-gpio.h"
#include "s5h1409.h"
#include "mxl5005s.h"
#include "zl10353.h"
#include "tuner-xc2028.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define CX18_REG_DMUX_NUM_PORT_0_CONTROL 0xd5a000
#define CX18_CLOCK_ENABLE2		 0xc71024
#define CX18_DMUX_CLK_MASK		 0x0080

static struct mxl5005s_config hauppauge_hvr1600_tuner = {
	.i2c_address     = 0xC6 >> 1,
	.if_freq         = IF_FREQ_5380000HZ,
	.xtal_freq       = CRYSTAL_FREQ_16000000HZ,
	.agc_mode        = MXL_SINGLE_AGC,
	.tracking_filter = MXL_TF_C_H,
	.rssi_enable     = MXL_RSSI_ENABLE,
	.cap_select      = MXL_CAP_SEL_ENABLE,
	.div_out         = MXL_DIV_OUT_4,
	.clock_out       = MXL_CLOCK_OUT_DISABLE,
	.output_load     = MXL5005S_IF_OUTPUT_LOAD_200_OHM,
	.top		 = MXL5005S_TOP_25P2,
	.mod_mode        = MXL_DIGITAL_MODE,
	.if_mode         = MXL_ZERO_IF,
	.AgcMasterByte   = 0x00,
};

static struct s5h1409_config hauppauge_hvr1600_config = {
	.demod_address = 0x32 >> 1,
	.output_mode   = S5H1409_SERIAL_OUTPUT,
	.gpio          = S5H1409_GPIO_ON,
	.qam_if        = 44000,
	.inversion     = S5H1409_INVERSION_OFF,
	.status_mode   = S5H1409_DEMODLOCKING,
	.mpeg_timing   = S5H1409_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK
};

/* Information/confirmation of proper config values provided by Terry Wu */
static struct zl10353_config leadtek_dvr3100h_demod = {
	.demod_address         = 0x1e >> 1, /* Datasheet suggested straps */
	.if2                   = 45600,     /* 4.560 MHz IF from the XC3028 */
	.parallel_ts           = 1,         /* Not a serial TS */
	.no_tuner              = 1,         /* XC3028 is not behind the gate */
	.disable_i2c_gate_ctrl = 1,         /* Disable the I2C gate */
};

static int dvb_register(struct cx18_stream *stream);

/* Kernel DVB framework calls this when the feed needs to start.
 * The CX18 framework should enable the transport DMA handling
 * and queue processing.
 */
static int cx18_dvb_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct cx18_stream *stream = (struct cx18_stream *) demux->priv;
	struct cx18 *cx = stream->cx;
	int ret;
	u32 v;

	CX18_DEBUG_INFO("Start feed: pid = 0x%x index = %d\n",
			feed->pid, feed->index);

	mutex_lock(&cx->serialize_lock);
	ret = cx18_init_on_first_open(cx);
	mutex_unlock(&cx->serialize_lock);
	if (ret) {
		CX18_ERR("Failed to initialize firmware starting DVB feed\n");
		return ret;
	}
	ret = -EINVAL;

	switch (cx->card->type) {
	case CX18_CARD_HVR_1600_ESMT:
	case CX18_CARD_HVR_1600_SAMSUNG:
		v = cx18_read_reg(cx, CX18_REG_DMUX_NUM_PORT_0_CONTROL);
		v |= 0x00400000; /* Serial Mode */
		v |= 0x00002000; /* Data Length - Byte */
		v |= 0x00010000; /* Error - Polarity */
		v |= 0x00020000; /* Error - Passthru */
		v |= 0x000c0000; /* Error - Ignore */
		cx18_write_reg(cx, v, CX18_REG_DMUX_NUM_PORT_0_CONTROL);
		break;

	case CX18_CARD_LEADTEK_DVR3100H:
	default:
		/* Assumption - Parallel transport - Signalling
		 * undefined or default.
		 */
		break;
	}

	if (!demux->dmx.frontend)
		return -EINVAL;

	if (!stream)
		return -EINVAL;

	mutex_lock(&stream->dvb.feedlock);
	if (stream->dvb.feeding++ == 0) {
		CX18_DEBUG_INFO("Starting Transport DMA\n");
		set_bit(CX18_F_S_STREAMING, &stream->s_flags);
		ret = cx18_start_v4l2_encode_stream(stream);
		if (ret < 0) {
			CX18_DEBUG_INFO("Failed to start Transport DMA\n");
			stream->dvb.feeding--;
			if (stream->dvb.feeding == 0)
				clear_bit(CX18_F_S_STREAMING, &stream->s_flags);
		}
	} else
		ret = 0;
	mutex_unlock(&stream->dvb.feedlock);

	return ret;
}

/* Kernel DVB framework calls this when the feed needs to stop. */
static int cx18_dvb_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct cx18_stream *stream = (struct cx18_stream *)demux->priv;
	struct cx18 *cx = stream->cx;
	int ret = -EINVAL;

	CX18_DEBUG_INFO("Stop feed: pid = 0x%x index = %d\n",
			feed->pid, feed->index);

	if (stream) {
		mutex_lock(&stream->dvb.feedlock);
		if (--stream->dvb.feeding == 0) {
			CX18_DEBUG_INFO("Stopping Transport DMA\n");
			ret = cx18_stop_v4l2_encode_stream(stream, 0);
		} else
			ret = 0;
		mutex_unlock(&stream->dvb.feedlock);
	}

	return ret;
}

int cx18_dvb_register(struct cx18_stream *stream)
{
	struct cx18 *cx = stream->cx;
	struct cx18_dvb *dvb = &stream->dvb;
	struct dvb_adapter *dvb_adapter;
	struct dvb_demux *dvbdemux;
	struct dmx_demux *dmx;
	int ret;

	if (!dvb)
		return -EINVAL;

	ret = dvb_register_adapter(&dvb->dvb_adapter,
			CX18_DRIVER_NAME,
			THIS_MODULE, &cx->pci_dev->dev, adapter_nr);
	if (ret < 0)
		goto err_out;

	dvb_adapter = &dvb->dvb_adapter;

	dvbdemux = &dvb->demux;

	dvbdemux->priv = (void *)stream;

	dvbdemux->filternum = 256;
	dvbdemux->feednum = 256;
	dvbdemux->start_feed = cx18_dvb_start_feed;
	dvbdemux->stop_feed = cx18_dvb_stop_feed;
	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING |
		DMX_SECTION_FILTERING | DMX_MEMORY_BASED_FILTERING);
	ret = dvb_dmx_init(dvbdemux);
	if (ret < 0)
		goto err_dvb_unregister_adapter;

	dmx = &dvbdemux->dmx;

	dvb->hw_frontend.source = DMX_FRONTEND_0;
	dvb->mem_frontend.source = DMX_MEMORY_FE;
	dvb->dmxdev.filternum = 256;
	dvb->dmxdev.demux = dmx;

	ret = dvb_dmxdev_init(&dvb->dmxdev, dvb_adapter);
	if (ret < 0)
		goto err_dvb_dmx_release;

	ret = dmx->add_frontend(dmx, &dvb->hw_frontend);
	if (ret < 0)
		goto err_dvb_dmxdev_release;

	ret = dmx->add_frontend(dmx, &dvb->mem_frontend);
	if (ret < 0)
		goto err_remove_hw_frontend;

	ret = dmx->connect_frontend(dmx, &dvb->hw_frontend);
	if (ret < 0)
		goto err_remove_mem_frontend;

	ret = dvb_register(stream);
	if (ret < 0)
		goto err_disconnect_frontend;

	dvb_net_init(dvb_adapter, &dvb->dvbnet, dmx);

	CX18_INFO("DVB Frontend registered\n");
	CX18_INFO("Registered DVB adapter%d for %s (%d x %d kB)\n",
		  stream->dvb.dvb_adapter.num, stream->name,
		  stream->buffers, stream->buf_size/1024);

	mutex_init(&dvb->feedlock);
	dvb->enabled = 1;
	return ret;

err_disconnect_frontend:
	dmx->disconnect_frontend(dmx);
err_remove_mem_frontend:
	dmx->remove_frontend(dmx, &dvb->mem_frontend);
err_remove_hw_frontend:
	dmx->remove_frontend(dmx, &dvb->hw_frontend);
err_dvb_dmxdev_release:
	dvb_dmxdev_release(&dvb->dmxdev);
err_dvb_dmx_release:
	dvb_dmx_release(dvbdemux);
err_dvb_unregister_adapter:
	dvb_unregister_adapter(dvb_adapter);
err_out:
	return ret;
}

void cx18_dvb_unregister(struct cx18_stream *stream)
{
	struct cx18 *cx = stream->cx;
	struct cx18_dvb *dvb = &stream->dvb;
	struct dvb_adapter *dvb_adapter;
	struct dvb_demux *dvbdemux;
	struct dmx_demux *dmx;

	CX18_INFO("unregister DVB\n");

	dvb_adapter = &dvb->dvb_adapter;
	dvbdemux = &dvb->demux;
	dmx = &dvbdemux->dmx;

	dmx->close(dmx);
	dvb_net_release(&dvb->dvbnet);
	dmx->remove_frontend(dmx, &dvb->mem_frontend);
	dmx->remove_frontend(dmx, &dvb->hw_frontend);
	dvb_dmxdev_release(&dvb->dmxdev);
	dvb_dmx_release(dvbdemux);
	dvb_unregister_frontend(dvb->fe);
	dvb_frontend_detach(dvb->fe);
	dvb_unregister_adapter(dvb_adapter);
}

/* All the DVB attach calls go here, this function get's modified
 * for each new card. cx18_dvb_start_feed() will also need changes.
 */
static int dvb_register(struct cx18_stream *stream)
{
	struct cx18_dvb *dvb = &stream->dvb;
	struct cx18 *cx = stream->cx;
	int ret = 0;

	switch (cx->card->type) {
	case CX18_CARD_HVR_1600_ESMT:
	case CX18_CARD_HVR_1600_SAMSUNG:
		dvb->fe = dvb_attach(s5h1409_attach,
			&hauppauge_hvr1600_config,
			&cx->i2c_adap[0]);
		if (dvb->fe != NULL) {
			dvb_attach(mxl5005s_attach, dvb->fe,
				&cx->i2c_adap[0],
				&hauppauge_hvr1600_tuner);
			ret = 0;
		}
		break;
	case CX18_CARD_LEADTEK_DVR3100H:
		dvb->fe = dvb_attach(zl10353_attach,
				     &leadtek_dvr3100h_demod,
				     &cx->i2c_adap[1]);
		if (dvb->fe != NULL) {
			struct dvb_frontend *fe;
			struct xc2028_config cfg = {
				.i2c_adap = &cx->i2c_adap[1],
				.i2c_addr = 0xc2 >> 1,
				.ctrl = NULL,
			};
			static struct xc2028_ctrl ctrl = {
				.fname   = XC2028_DEFAULT_FIRMWARE,
				.max_len = 64,
				.demod   = XC3028_FE_ZARLINK456,
				.type    = XC2028_AUTO,
			};

			fe = dvb_attach(xc2028_attach, dvb->fe, &cfg);
			if (fe != NULL && fe->ops.tuner_ops.set_config != NULL)
				fe->ops.tuner_ops.set_config(fe, &ctrl);
		}
		break;
	default:
		/* No Digital Tv Support */
		break;
	}

	if (dvb->fe == NULL) {
		CX18_ERR("frontend initialization failed\n");
		return -1;
	}

	dvb->fe->callback = cx18_reset_tuner_gpio;

	ret = dvb_register_frontend(&dvb->dvb_adapter, dvb->fe);
	if (ret < 0) {
		if (dvb->fe->ops.release)
			dvb->fe->ops.release(dvb->fe);
		return ret;
	}

	/*
	 * The firmware seems to enable the TS DMUX clock
	 * under various circumstances.  However, since we know we
	 * might use it, let's just turn it on ourselves here.
	 */
	cx18_write_reg_expect(cx,
			      (CX18_DMUX_CLK_MASK << 16) | CX18_DMUX_CLK_MASK,
			      CX18_CLOCK_ENABLE2,
			      CX18_DMUX_CLK_MASK,
			      (CX18_DMUX_CLK_MASK << 16) | CX18_DMUX_CLK_MASK);

	return ret;
}
