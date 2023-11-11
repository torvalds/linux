// SPDX-License-Identifier: GPL-2.0-or-later
/*
 DVB device driver for cx231xx

 Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
		Based on em28xx driver

 */

#include "cx231xx.h"
#include <linux/kernel.h>
#include <linux/slab.h>

#include <media/dvbdev.h>
#include <media/dmxdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_net.h>
#include <media/dvb_frontend.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>

#include "xc5000.h"
#include "s5h1432.h"
#include "tda18271.h"
#include "s5h1411.h"
#include "lgdt3305.h"
#include "si2165.h"
#include "si2168.h"
#include "mb86a20s.h"
#include "si2157.h"
#include "lgdt3306a.h"
#include "r820t.h"
#include "mn88473.h"

MODULE_DESCRIPTION("driver for cx231xx based DVB cards");
MODULE_AUTHOR("Srinivasa Deevi <srinivasa.deevi@conexant.com>");
MODULE_LICENSE("GPL");

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages [dvb]");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define CX231XX_DVB_NUM_BUFS 5
#define CX231XX_DVB_MAX_PACKETSIZE 564
#define CX231XX_DVB_MAX_PACKETS 64
#define CX231XX_DVB_MAX_FRONTENDS 2

struct cx231xx_dvb {
	struct dvb_frontend *frontend[CX231XX_DVB_MAX_FRONTENDS];

	/* feed count management */
	struct mutex lock;
	int nfeeds;

	/* general boilerplate stuff */
	struct dvb_adapter adapter;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	struct dmx_frontend fe_hw;
	struct dmx_frontend fe_mem;
	struct dvb_net net;
	struct i2c_client *i2c_client_demod[2];
	struct i2c_client *i2c_client_tuner;
};

static struct s5h1432_config dvico_s5h1432_config = {
	.output_mode   = S5H1432_SERIAL_OUTPUT,
	.gpio          = S5H1432_GPIO_ON,
	.qam_if        = S5H1432_IF_4000,
	.vsb_if        = S5H1432_IF_4000,
	.inversion     = S5H1432_INVERSION_OFF,
	.status_mode   = S5H1432_DEMODLOCKING,
	.mpeg_timing   = S5H1432_MPEGTIMING_CONTINUOUS_NONINVERTING_CLOCK,
};

static struct tda18271_std_map cnxt_rde253s_tda18271_std_map = {
	.dvbt_6   = { .if_freq = 4000, .agc_mode = 3, .std = 4,
		      .if_lvl = 1, .rfagc_top = 0x37, },
	.dvbt_7   = { .if_freq = 4000, .agc_mode = 3, .std = 5,
		      .if_lvl = 1, .rfagc_top = 0x37, },
	.dvbt_8   = { .if_freq = 4000, .agc_mode = 3, .std = 6,
		      .if_lvl = 1, .rfagc_top = 0x37, },
};

static struct tda18271_std_map mb86a20s_tda18271_config = {
	.dvbt_6   = { .if_freq = 4000, .agc_mode = 3, .std = 4,
		      .if_lvl = 0, .rfagc_top = 0x37, },
};

static struct tda18271_config cnxt_rde253s_tunerconfig = {
	.std_map = &cnxt_rde253s_tda18271_std_map,
	.gate    = TDA18271_GATE_ANALOG,
};

static struct s5h1411_config tda18271_s5h1411_config = {
	.output_mode   = S5H1411_SERIAL_OUTPUT,
	.gpio          = S5H1411_GPIO_OFF,
	.vsb_if        = S5H1411_IF_3250,
	.qam_if        = S5H1411_IF_4000,
	.inversion     = S5H1411_INVERSION_ON,
	.status_mode   = S5H1411_DEMODLOCKING,
	.mpeg_timing   = S5H1411_MPEGTIMING_CONTINUOUS_NONINVERTING_CLOCK,
};
static struct s5h1411_config xc5000_s5h1411_config = {
	.output_mode   = S5H1411_SERIAL_OUTPUT,
	.gpio          = S5H1411_GPIO_OFF,
	.vsb_if        = S5H1411_IF_3250,
	.qam_if        = S5H1411_IF_3250,
	.inversion     = S5H1411_INVERSION_OFF,
	.status_mode   = S5H1411_DEMODLOCKING,
	.mpeg_timing   = S5H1411_MPEGTIMING_CONTINUOUS_NONINVERTING_CLOCK,
};

static struct lgdt3305_config hcw_lgdt3305_config = {
	.i2c_addr           = 0x0e,
	.mpeg_mode          = LGDT3305_MPEG_SERIAL,
	.tpclk_edge         = LGDT3305_TPCLK_FALLING_EDGE,
	.tpvalid_polarity   = LGDT3305_TP_VALID_HIGH,
	.deny_i2c_rptr      = 1,
	.spectral_inversion = 1,
	.qam_if_khz         = 4000,
	.vsb_if_khz         = 3250,
};

static struct tda18271_std_map hauppauge_tda18271_std_map = {
	.atsc_6   = { .if_freq = 3250, .agc_mode = 3, .std = 4,
		      .if_lvl = 1, .rfagc_top = 0x58, },
	.qam_6    = { .if_freq = 4000, .agc_mode = 3, .std = 5,
		      .if_lvl = 1, .rfagc_top = 0x58, },
};

static struct tda18271_config hcw_tda18271_config = {
	.std_map = &hauppauge_tda18271_std_map,
	.gate    = TDA18271_GATE_DIGITAL,
};

static const struct mb86a20s_config pv_mb86a20s_config = {
	.demod_address = 0x10,
	.is_serial = true,
};

static struct tda18271_config pv_tda18271_config = {
	.std_map = &mb86a20s_tda18271_config,
	.gate    = TDA18271_GATE_DIGITAL,
	.small_i2c = TDA18271_03_BYTE_CHUNK_INIT,
};

static const struct lgdt3306a_config hauppauge_955q_lgdt3306a_config = {
	.qam_if_khz         = 4000,
	.vsb_if_khz         = 3250,
	.spectral_inversion = 1,
	.mpeg_mode          = LGDT3306A_MPEG_SERIAL,
	.tpclk_edge         = LGDT3306A_TPCLK_RISING_EDGE,
	.tpvalid_polarity   = LGDT3306A_TP_VALID_HIGH,
	.xtalMHz            = 25,
};

static struct r820t_config astrometa_t2hybrid_r820t_config = {
	.i2c_addr		= 0x3a, /* 0x74 >> 1 */
	.xtal			= 16000000,
	.rafael_chip		= CHIP_R828D,
	.max_i2c_msg_len	= 2,
};

static inline void print_err_status(struct cx231xx *dev, int packet, int status)
{
	char *errmsg = "Unknown";

	switch (status) {
	case -ENOENT:
		errmsg = "unlinked synchronously";
		break;
	case -ECONNRESET:
		errmsg = "unlinked asynchronously";
		break;
	case -ENOSR:
		errmsg = "Buffer error (overrun)";
		break;
	case -EPIPE:
		errmsg = "Stalled (device not responding)";
		break;
	case -EOVERFLOW:
		errmsg = "Babble (bad cable?)";
		break;
	case -EPROTO:
		errmsg = "Bit-stuff error (bad cable?)";
		break;
	case -EILSEQ:
		errmsg = "CRC/Timeout (could be anything)";
		break;
	case -ETIME:
		errmsg = "Device does not respond";
		break;
	}
	if (packet < 0) {
		dev_dbg(dev->dev,
			"URB status %d [%s].\n", status, errmsg);
	} else {
		dev_dbg(dev->dev,
			"URB packet %d, status %d [%s].\n",
			packet, status, errmsg);
	}
}

static inline int dvb_isoc_copy(struct cx231xx *dev, struct urb *urb)
{
	int i;

	if (!dev)
		return 0;

	if (dev->state & DEV_DISCONNECTED)
		return 0;

	if (urb->status < 0) {
		print_err_status(dev, -1, urb->status);
		if (urb->status == -ENOENT)
			return 0;
	}

	for (i = 0; i < urb->number_of_packets; i++) {
		int status = urb->iso_frame_desc[i].status;

		if (status < 0) {
			print_err_status(dev, i, status);
			if (urb->iso_frame_desc[i].status != -EPROTO)
				continue;
		}

		dvb_dmx_swfilter(&dev->dvb->demux,
				 urb->transfer_buffer +
				urb->iso_frame_desc[i].offset,
				urb->iso_frame_desc[i].actual_length);
	}

	return 0;
}

static inline int dvb_bulk_copy(struct cx231xx *dev, struct urb *urb)
{
	if (!dev)
		return 0;

	if (dev->state & DEV_DISCONNECTED)
		return 0;

	if (urb->status < 0) {
		print_err_status(dev, -1, urb->status);
		if (urb->status == -ENOENT)
			return 0;
	}

	/* Feed the transport payload into the kernel demux */
	dvb_dmx_swfilter(&dev->dvb->demux,
		urb->transfer_buffer, urb->actual_length);

	return 0;
}

static int start_streaming(struct cx231xx_dvb *dvb)
{
	int rc;
	struct cx231xx *dev = dvb->adapter.priv;

	if (dev->USE_ISO) {
		dev_dbg(dev->dev, "DVB transfer mode is ISO.\n");
		cx231xx_set_alt_setting(dev, INDEX_TS1, 5);
		rc = cx231xx_set_mode(dev, CX231XX_DIGITAL_MODE);
		if (rc < 0)
			return rc;
		dev->mode_tv = 1;
		return cx231xx_init_isoc(dev, CX231XX_DVB_MAX_PACKETS,
					CX231XX_DVB_NUM_BUFS,
					dev->ts1_mode.max_pkt_size,
					dvb_isoc_copy);
	} else {
		dev_dbg(dev->dev, "DVB transfer mode is BULK.\n");
		cx231xx_set_alt_setting(dev, INDEX_TS1, 0);
		rc = cx231xx_set_mode(dev, CX231XX_DIGITAL_MODE);
		if (rc < 0)
			return rc;
		dev->mode_tv = 1;
		return cx231xx_init_bulk(dev, CX231XX_DVB_MAX_PACKETS,
					CX231XX_DVB_NUM_BUFS,
					dev->ts1_mode.max_pkt_size,
					dvb_bulk_copy);
	}

}

static int stop_streaming(struct cx231xx_dvb *dvb)
{
	struct cx231xx *dev = dvb->adapter.priv;

	if (dev->USE_ISO)
		cx231xx_uninit_isoc(dev);
	else
		cx231xx_uninit_bulk(dev);

	cx231xx_set_mode(dev, CX231XX_SUSPEND);

	return 0;
}

static int start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct cx231xx_dvb *dvb = demux->priv;
	int rc, ret;

	if (!demux->dmx.frontend)
		return -EINVAL;

	mutex_lock(&dvb->lock);
	dvb->nfeeds++;
	rc = dvb->nfeeds;

	if (dvb->nfeeds == 1) {
		ret = start_streaming(dvb);
		if (ret < 0)
			rc = ret;
	}

	mutex_unlock(&dvb->lock);
	return rc;
}

static int stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct cx231xx_dvb *dvb = demux->priv;
	int err = 0;

	mutex_lock(&dvb->lock);
	dvb->nfeeds--;

	if (0 == dvb->nfeeds)
		err = stop_streaming(dvb);

	mutex_unlock(&dvb->lock);
	return err;
}

/* ------------------------------------------------------------------ */
static int cx231xx_dvb_bus_ctrl(struct dvb_frontend *fe, int acquire)
{
	struct cx231xx *dev = fe->dvb->priv;

	if (acquire)
		return cx231xx_set_mode(dev, CX231XX_DIGITAL_MODE);
	else
		return cx231xx_set_mode(dev, CX231XX_SUSPEND);
}

/* ------------------------------------------------------------------ */

static struct xc5000_config cnxt_rde250_tunerconfig = {
	.i2c_address = 0x61,
	.if_khz = 4000,
};
static struct xc5000_config cnxt_rdu250_tunerconfig = {
	.i2c_address = 0x61,
	.if_khz = 3250,
};

/* ------------------------------------------------------------------ */
#if 0
static int attach_xc5000(u8 addr, struct cx231xx *dev)
{

	struct dvb_frontend *fe;
	struct xc5000_config cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.i2c_adap = cx231xx_get_i2c_adap(dev, dev->board.tuner_i2c_master);
	cfg.i2c_addr = addr;

	if (!dev->dvb->frontend[0]) {
		dev_err(dev->dev, "%s/2: dvb frontend not attached. Can't attach xc5000\n",
			dev->name);
		return -EINVAL;
	}

	fe = dvb_attach(xc5000_attach, dev->dvb->frontend[0], &cfg);
	if (!fe) {
		dev_err(dev->dev, "%s/2: xc5000 attach failed\n", dev->name);
		dvb_frontend_detach(dev->dvb->frontend[0]);
		dev->dvb->frontend[0] = NULL;
		return -EINVAL;
	}

	dev_info(dev->dev, "%s/2: xc5000 attached\n", dev->name);

	return 0;
}
#endif

int cx231xx_set_analog_freq(struct cx231xx *dev, u32 freq)
{
	if (dev->dvb && dev->dvb->frontend[0]) {

		struct dvb_tuner_ops *dops = &dev->dvb->frontend[0]->ops.tuner_ops;

		if (dops->set_analog_params != NULL) {
			struct analog_parameters params;

			params.frequency = freq;
			params.std = dev->norm;
			params.mode = 0;	/* 0- Air; 1 - cable */
			/*params.audmode = ;       */

			/* Set the analog parameters to set the frequency */
			dops->set_analog_params(dev->dvb->frontend[0], &params);
		}

	}

	return 0;
}

int cx231xx_reset_analog_tuner(struct cx231xx *dev)
{
	int status = 0;

	if (dev->dvb && dev->dvb->frontend[0]) {

		struct dvb_tuner_ops *dops = &dev->dvb->frontend[0]->ops.tuner_ops;

		if (dops->init != NULL && !dev->xc_fw_load_done) {

			dev_dbg(dev->dev,
				"Reloading firmware for XC5000\n");
			status = dops->init(dev->dvb->frontend[0]);
			if (status == 0) {
				dev->xc_fw_load_done = 1;
				dev_dbg(dev->dev,
					"XC5000 firmware download completed\n");
			} else {
				dev->xc_fw_load_done = 0;
				dev_dbg(dev->dev,
					"XC5000 firmware download failed !!!\n");
			}
		}

	}

	return status;
}

/* ------------------------------------------------------------------ */

static int register_dvb(struct cx231xx_dvb *dvb,
			struct module *module,
			struct cx231xx *dev, struct device *device)
{
	int result;

	mutex_init(&dvb->lock);


	/* register adapter */
	result = dvb_register_adapter(&dvb->adapter, dev->name, module, device,
				      adapter_nr);
	if (result < 0) {
		dev_warn(dev->dev,
		       "%s: dvb_register_adapter failed (errno = %d)\n",
		       dev->name, result);
		goto fail_adapter;
	}
	dvb_register_media_controller(&dvb->adapter, dev->media_dev);

	/* Ensure all frontends negotiate bus access */
	dvb->frontend[0]->ops.ts_bus_ctrl = cx231xx_dvb_bus_ctrl;
	if (dvb->frontend[1])
		dvb->frontend[1]->ops.ts_bus_ctrl = cx231xx_dvb_bus_ctrl;

	dvb->adapter.priv = dev;

	/* register frontend */
	result = dvb_register_frontend(&dvb->adapter, dvb->frontend[0]);
	if (result < 0) {
		dev_warn(dev->dev,
		       "%s: dvb_register_frontend failed (errno = %d)\n",
		       dev->name, result);
		goto fail_frontend0;
	}

	if (dvb->frontend[1]) {
		result = dvb_register_frontend(&dvb->adapter, dvb->frontend[1]);
		if (result < 0) {
			dev_warn(dev->dev,
				 "%s: 2nd dvb_register_frontend failed (errno = %d)\n",
				dev->name, result);
			goto fail_frontend1;
		}

		/* MFE lock */
		dvb->adapter.mfe_shared = 1;
	}

	/* register demux stuff */
	dvb->demux.dmx.capabilities =
	    DMX_TS_FILTERING | DMX_SECTION_FILTERING |
	    DMX_MEMORY_BASED_FILTERING;
	dvb->demux.priv = dvb;
	dvb->demux.filternum = 256;
	dvb->demux.feednum = 256;
	dvb->demux.start_feed = start_feed;
	dvb->demux.stop_feed = stop_feed;

	result = dvb_dmx_init(&dvb->demux);
	if (result < 0) {
		dev_warn(dev->dev,
			 "%s: dvb_dmx_init failed (errno = %d)\n",
		       dev->name, result);
		goto fail_dmx;
	}

	dvb->dmxdev.filternum = 256;
	dvb->dmxdev.demux = &dvb->demux.dmx;
	dvb->dmxdev.capabilities = 0;
	result = dvb_dmxdev_init(&dvb->dmxdev, &dvb->adapter);
	if (result < 0) {
		dev_warn(dev->dev,
			 "%s: dvb_dmxdev_init failed (errno = %d)\n",
			 dev->name, result);
		goto fail_dmxdev;
	}

	dvb->fe_hw.source = DMX_FRONTEND_0;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		dev_warn(dev->dev,
		       "%s: add_frontend failed (DMX_FRONTEND_0, errno = %d)\n",
		       dev->name, result);
		goto fail_fe_hw;
	}

	dvb->fe_mem.source = DMX_MEMORY_FE;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	if (result < 0) {
		dev_warn(dev->dev,
			 "%s: add_frontend failed (DMX_MEMORY_FE, errno = %d)\n",
			 dev->name, result);
		goto fail_fe_mem;
	}

	result = dvb->demux.dmx.connect_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		dev_warn(dev->dev,
			 "%s: connect_frontend failed (errno = %d)\n",
			 dev->name, result);
		goto fail_fe_conn;
	}

	/* register network adapter */
	dvb_net_init(&dvb->adapter, &dvb->net, &dvb->demux.dmx);
	result = dvb_create_media_graph(&dvb->adapter,
					dev->tuner_type == TUNER_ABSENT);
	if (result < 0)
		goto fail_create_graph;

	return 0;

fail_create_graph:
	dvb_net_release(&dvb->net);
fail_fe_conn:
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
fail_fe_mem:
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
fail_fe_hw:
	dvb_dmxdev_release(&dvb->dmxdev);
fail_dmxdev:
	dvb_dmx_release(&dvb->demux);
fail_dmx:
	if (dvb->frontend[1])
		dvb_unregister_frontend(dvb->frontend[1]);
	dvb_unregister_frontend(dvb->frontend[0]);
fail_frontend1:
	if (dvb->frontend[1])
		dvb_frontend_detach(dvb->frontend[1]);
fail_frontend0:
	dvb_frontend_detach(dvb->frontend[0]);
	dvb_unregister_adapter(&dvb->adapter);
fail_adapter:
	return result;
}

static void unregister_dvb(struct cx231xx_dvb *dvb)
{
	dvb_net_release(&dvb->net);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	dvb_dmxdev_release(&dvb->dmxdev);
	dvb_dmx_release(&dvb->demux);
	if (dvb->frontend[1])
		dvb_unregister_frontend(dvb->frontend[1]);
	dvb_unregister_frontend(dvb->frontend[0]);
	if (dvb->frontend[1])
		dvb_frontend_detach(dvb->frontend[1]);
	dvb_frontend_detach(dvb->frontend[0]);
	dvb_unregister_adapter(&dvb->adapter);

	/* remove I2C tuner */
	dvb_module_release(dvb->i2c_client_tuner);
	dvb->i2c_client_tuner = NULL;
	/* remove I2C demod(s) */
	dvb_module_release(dvb->i2c_client_demod[1]);
	dvb->i2c_client_demod[1] = NULL;
	dvb_module_release(dvb->i2c_client_demod[0]);
	dvb->i2c_client_demod[0] = NULL;
}

static int dvb_init(struct cx231xx *dev)
{
	int result;
	struct cx231xx_dvb *dvb;
	struct i2c_adapter *tuner_i2c;
	struct i2c_adapter *demod_i2c;
	struct i2c_client *client;
	struct i2c_adapter *adapter;

	if (!dev->board.has_dvb) {
		/* This device does not support the extension */
		return 0;
	}

	dvb = kzalloc(sizeof(struct cx231xx_dvb), GFP_KERNEL);

	if (dvb == NULL) {
		dev_info(dev->dev,
			 "cx231xx_dvb: memory allocation failed\n");
		return -ENOMEM;
	}
	dev->dvb = dvb;
	dev->cx231xx_set_analog_freq = cx231xx_set_analog_freq;
	dev->cx231xx_reset_analog_tuner = cx231xx_reset_analog_tuner;

	tuner_i2c = cx231xx_get_i2c_adap(dev, dev->board.tuner_i2c_master);
	demod_i2c = cx231xx_get_i2c_adap(dev, dev->board.demod_i2c_master);
	mutex_lock(&dev->lock);
	cx231xx_set_mode(dev, CX231XX_DIGITAL_MODE);
	cx231xx_demod_reset(dev);
	/* init frontend */
	switch (dev->model) {
	case CX231XX_BOARD_CNXT_CARRAERA:
	case CX231XX_BOARD_CNXT_RDE_250:

		dev->dvb->frontend[0] = dvb_attach(s5h1432_attach,
					&dvico_s5h1432_config,
					demod_i2c);

		if (!dev->dvb->frontend[0]) {
			dev_err(dev->dev,
				"Failed to attach s5h1432 front end\n");
			result = -EINVAL;
			goto out_free;
		}

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		if (!dvb_attach(xc5000_attach, dev->dvb->frontend[0],
			       tuner_i2c,
			       &cnxt_rde250_tunerconfig)) {
			result = -EINVAL;
			goto out_free;
		}

		break;
	case CX231XX_BOARD_CNXT_SHELBY:
	case CX231XX_BOARD_CNXT_RDU_250:

		dev->dvb->frontend[0] = dvb_attach(s5h1411_attach,
					       &xc5000_s5h1411_config,
					       demod_i2c);

		if (!dev->dvb->frontend[0]) {
			dev_err(dev->dev,
				"Failed to attach s5h1411 front end\n");
			result = -EINVAL;
			goto out_free;
		}

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		if (!dvb_attach(xc5000_attach, dev->dvb->frontend[0],
			       tuner_i2c,
			       &cnxt_rdu250_tunerconfig)) {
			result = -EINVAL;
			goto out_free;
		}
		break;
	case CX231XX_BOARD_CNXT_RDE_253S:

		dev->dvb->frontend[0] = dvb_attach(s5h1432_attach,
					&dvico_s5h1432_config,
					demod_i2c);

		if (!dev->dvb->frontend[0]) {
			dev_err(dev->dev,
				"Failed to attach s5h1432 front end\n");
			result = -EINVAL;
			goto out_free;
		}

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		if (!dvb_attach(tda18271_attach, dev->dvb->frontend[0],
			       dev->board.tuner_addr, tuner_i2c,
			       &cnxt_rde253s_tunerconfig)) {
			result = -EINVAL;
			goto out_free;
		}
		break;
	case CX231XX_BOARD_CNXT_RDU_253S:
	case CX231XX_BOARD_KWORLD_UB445_USB_HYBRID:

		dev->dvb->frontend[0] = dvb_attach(s5h1411_attach,
					       &tda18271_s5h1411_config,
					       demod_i2c);

		if (!dev->dvb->frontend[0]) {
			dev_err(dev->dev,
				"Failed to attach s5h1411 front end\n");
			result = -EINVAL;
			goto out_free;
		}

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		if (!dvb_attach(tda18271_attach, dev->dvb->frontend[0],
			       dev->board.tuner_addr, tuner_i2c,
			       &cnxt_rde253s_tunerconfig)) {
			result = -EINVAL;
			goto out_free;
		}
		break;
	case CX231XX_BOARD_HAUPPAUGE_EXETER:

		dev_info(dev->dev,
			 "%s: looking for tuner / demod on i2c bus: %d\n",
		       __func__, i2c_adapter_id(tuner_i2c));

		dev->dvb->frontend[0] = dvb_attach(lgdt3305_attach,
						&hcw_lgdt3305_config,
						demod_i2c);

		if (!dev->dvb->frontend[0]) {
			dev_err(dev->dev,
				"Failed to attach LG3305 front end\n");
			result = -EINVAL;
			goto out_free;
		}

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		dvb_attach(tda18271_attach, dev->dvb->frontend[0],
			   dev->board.tuner_addr, tuner_i2c,
			   &hcw_tda18271_config);
		break;

	case CX231XX_BOARD_HAUPPAUGE_930C_HD_1113xx:
	{
		struct si2165_platform_data si2165_pdata = {};

		/* attach demod */
		si2165_pdata.fe = &dev->dvb->frontend[0];
		si2165_pdata.chip_mode = SI2165_MODE_PLL_XTAL;
		si2165_pdata.ref_freq_hz = 16000000;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2165", NULL, demod_i2c,
						dev->board.demod_addr,
						&si2165_pdata);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dvb->i2c_client_demod[0] = client;

		dev->dvb->frontend[0]->ops.i2c_gate_ctrl = NULL;

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		dvb_attach(tda18271_attach, dev->dvb->frontend[0],
			dev->board.tuner_addr, tuner_i2c,
			&hcw_tda18271_config);

		dev->cx231xx_reset_analog_tuner = NULL;
		break;
	}
	case CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx:
	{
		struct si2165_platform_data si2165_pdata = {};
		struct si2157_config si2157_config = {};

		/* attach demod */
		si2165_pdata.fe = &dev->dvb->frontend[0];
		si2165_pdata.chip_mode = SI2165_MODE_PLL_EXT;
		si2165_pdata.ref_freq_hz = 24000000;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2165", NULL, demod_i2c,
						dev->board.demod_addr,
						&si2165_pdata);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dvb->i2c_client_demod[0] = client;

		dev->dvb->frontend[0]->ops.i2c_gate_ctrl = NULL;

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		/* attach tuner */
		si2157_config.fe = dev->dvb->frontend[0];
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		si2157_config.mdev = dev->media_dev;
#endif
		si2157_config.if_port = 1;
		si2157_config.inversion = true;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2157", NULL, tuner_i2c,
						dev->board.tuner_addr,
						&si2157_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dev->cx231xx_reset_analog_tuner = NULL;

		dev->dvb->i2c_client_tuner = client;
		break;
	}
	case CX231XX_BOARD_HAUPPAUGE_955Q:
	{
		struct si2157_config si2157_config = {};
		struct lgdt3306a_config lgdt3306a_config = {};

		lgdt3306a_config = hauppauge_955q_lgdt3306a_config;
		lgdt3306a_config.fe = &dev->dvb->frontend[0];
		lgdt3306a_config.i2c_adapter = &adapter;

		/* perform probe/init/attach */
		client = dvb_module_probe("lgdt3306a", NULL, demod_i2c,
						dev->board.demod_addr,
						&lgdt3306a_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dvb->i2c_client_demod[0] = client;

		dev->dvb->frontend[0]->ops.i2c_gate_ctrl = NULL;

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		/* attach tuner */
		si2157_config.fe = dev->dvb->frontend[0];
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		si2157_config.mdev = dev->media_dev;
#endif
		si2157_config.if_port = 1;
		si2157_config.inversion = true;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2157", NULL, tuner_i2c,
						dev->board.tuner_addr,
						&si2157_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dev->cx231xx_reset_analog_tuner = NULL;

		dev->dvb->i2c_client_tuner = client;
		break;
	}
	case CX231XX_BOARD_PV_PLAYTV_USB_HYBRID:
	case CX231XX_BOARD_KWORLD_UB430_USB_HYBRID:

		dev_info(dev->dev,
			 "%s: looking for demod on i2c bus: %d\n",
			 __func__, i2c_adapter_id(tuner_i2c));

		dev->dvb->frontend[0] = dvb_attach(mb86a20s_attach,
						&pv_mb86a20s_config,
						demod_i2c);

		if (!dev->dvb->frontend[0]) {
			dev_err(dev->dev,
				"Failed to attach mb86a20s demod\n");
			result = -EINVAL;
			goto out_free;
		}

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		dvb_attach(tda18271_attach, dev->dvb->frontend[0],
			   dev->board.tuner_addr, tuner_i2c,
			   &pv_tda18271_config);
		break;

	case CX231XX_BOARD_EVROMEDIA_FULL_HYBRID_FULLHD:
	{
		struct si2157_config si2157_config = {};
		struct si2168_config si2168_config = {};

		/* attach demodulator chip */
		si2168_config.ts_mode = SI2168_TS_SERIAL; /* from *.inf file */
		si2168_config.fe = &dev->dvb->frontend[0];
		si2168_config.i2c_adapter = &adapter;
		si2168_config.ts_clock_inv = true;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2168", NULL, demod_i2c,
						dev->board.demod_addr,
						&si2168_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dvb->i2c_client_demod[0] = client;

		/* attach tuner chip */
		si2157_config.fe = dev->dvb->frontend[0];
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		si2157_config.mdev = dev->media_dev;
#endif
		si2157_config.if_port = 1;
		si2157_config.inversion = false;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2157", NULL, tuner_i2c,
						dev->board.tuner_addr,
						&si2157_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dev->cx231xx_reset_analog_tuner = NULL;
		dev->dvb->i2c_client_tuner = client;
		break;
	}
	case CX231XX_BOARD_ASTROMETA_T2HYBRID:
	{
		struct mn88473_config mn88473_config = {};

		/* attach demodulator chip */
		mn88473_config.i2c_wr_max = 16;
		mn88473_config.xtal = 25000000;
		mn88473_config.fe = &dev->dvb->frontend[0];

		/* perform probe/init/attach */
		client = dvb_module_probe("mn88473", NULL, demod_i2c,
						dev->board.demod_addr,
						&mn88473_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dvb->i2c_client_demod[0] = client;

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		/* attach tuner chip */
		dvb_attach(r820t_attach, dev->dvb->frontend[0],
			   tuner_i2c,
			   &astrometa_t2hybrid_r820t_config);
		break;
	}
	case CX231XX_BOARD_HAUPPAUGE_935C:
	{
		struct si2157_config si2157_config = {};
		struct si2168_config si2168_config = {};

		/* attach demodulator chip */
		si2168_config.ts_mode = SI2168_TS_SERIAL;
		si2168_config.fe = &dev->dvb->frontend[0];
		si2168_config.i2c_adapter = &adapter;
		si2168_config.ts_clock_inv = true;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2168", NULL, demod_i2c,
						dev->board.demod_addr,
						&si2168_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dvb->i2c_client_demod[0] = client;
		dev->dvb->frontend[0]->ops.i2c_gate_ctrl = NULL;

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;

		/* attach tuner */
		si2157_config.fe = dev->dvb->frontend[0];
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		si2157_config.mdev = dev->media_dev;
#endif
		si2157_config.if_port = 1;
		si2157_config.inversion = true;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2157", NULL, tuner_i2c,
						dev->board.tuner_addr,
						&si2157_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dev->cx231xx_reset_analog_tuner = NULL;
		dev->dvb->i2c_client_tuner = client;
		break;
	}
	case CX231XX_BOARD_HAUPPAUGE_975:
	{
		struct i2c_adapter *adapter2;
		struct si2157_config si2157_config = {};
		struct lgdt3306a_config lgdt3306a_config = {};
		struct si2168_config si2168_config = {};

		/* attach first demodulator chip */
		lgdt3306a_config = hauppauge_955q_lgdt3306a_config;
		lgdt3306a_config.fe = &dev->dvb->frontend[0];
		lgdt3306a_config.i2c_adapter = &adapter;

		/* perform probe/init/attach */
		client = dvb_module_probe("lgdt3306a", NULL, demod_i2c,
						dev->board.demod_addr,
						&lgdt3306a_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dvb->i2c_client_demod[0] = client;

		/* attach second demodulator chip */
		si2168_config.ts_mode = SI2168_TS_SERIAL;
		si2168_config.fe = &dev->dvb->frontend[1];
		si2168_config.i2c_adapter = &adapter2;
		si2168_config.ts_clock_inv = true;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2168", NULL, adapter,
						dev->board.demod_addr2,
						&si2168_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dvb->i2c_client_demod[1] = client;
		dvb->frontend[1]->id = 1;

		/* define general-purpose callback pointer */
		dvb->frontend[0]->callback = cx231xx_tuner_callback;
		dvb->frontend[1]->callback = cx231xx_tuner_callback;

		/* attach tuner */
		si2157_config.fe = dev->dvb->frontend[0];
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		si2157_config.mdev = dev->media_dev;
#endif
		si2157_config.if_port = 1;
		si2157_config.inversion = true;

		/* perform probe/init/attach */
		client = dvb_module_probe("si2157", NULL, adapter,
						dev->board.tuner_addr,
						&si2157_config);
		if (!client) {
			result = -ENODEV;
			goto out_free;
		}
		dev->cx231xx_reset_analog_tuner = NULL;
		dvb->i2c_client_tuner = client;

		dvb->frontend[1]->tuner_priv = dvb->frontend[0]->tuner_priv;

		memcpy(&dvb->frontend[1]->ops.tuner_ops,
			&dvb->frontend[0]->ops.tuner_ops,
			sizeof(struct dvb_tuner_ops));
		break;
	}
	default:
		dev_err(dev->dev,
			"%s/2: The frontend of your DVB/ATSC card isn't supported yet\n",
			dev->name);
		break;
	}
	if (!dvb->frontend[0]) {
		dev_err(dev->dev,
		       "%s/2: frontend initialization failed\n", dev->name);
		result = -EINVAL;
		goto out_free;
	}

	/* register everything */
	result = register_dvb(dvb, THIS_MODULE, dev, dev->dev);

	if (result < 0)
		goto out_free;


	dev_info(dev->dev, "Successfully loaded cx231xx-dvb\n");

ret:
	cx231xx_set_mode(dev, CX231XX_SUSPEND);
	mutex_unlock(&dev->lock);
	return result;

out_free:
	/* remove I2C tuner */
	dvb_module_release(dvb->i2c_client_tuner);
	dvb->i2c_client_tuner = NULL;
	/* remove I2C demod(s) */
	dvb_module_release(dvb->i2c_client_demod[1]);
	dvb->i2c_client_demod[1] = NULL;
	dvb_module_release(dvb->i2c_client_demod[0]);
	dvb->i2c_client_demod[0] = NULL;
	kfree(dvb);
	dev->dvb = NULL;
	goto ret;
}

static int dvb_fini(struct cx231xx *dev)
{
	if (!dev->board.has_dvb) {
		/* This device does not support the extension */
		return 0;
	}

	if (dev->dvb) {
		unregister_dvb(dev->dvb);
		kfree(dev->dvb);
		dev->dvb = NULL;
	}

	return 0;
}

static struct cx231xx_ops dvb_ops = {
	.id = CX231XX_DVB,
	.name = "Cx231xx dvb Extension",
	.init = dvb_init,
	.fini = dvb_fini,
};

static int __init cx231xx_dvb_register(void)
{
	return cx231xx_register_extension(&dvb_ops);
}

static void __exit cx231xx_dvb_unregister(void)
{
	cx231xx_unregister_extension(&dvb_ops);
}

module_init(cx231xx_dvb_register);
module_exit(cx231xx_dvb_unregister);
