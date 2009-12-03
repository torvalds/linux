/*
	Mantis PCI bridge driver
	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/bitops.h>
#include "mantis_common.h"
#include "mantis_core.h"

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "mantis_vp1033.h"
#include "mantis_vp1034.h"
#include "mantis_vp2033.h"
#include "mantis_vp3030.h"

/*	Tuner power supply control	*/
void mantis_fe_powerup(struct mantis_pci *mantis)
{
	dprintk(verbose, MANTIS_DEBUG, 1, "Frontend Power ON");
	gpio_set_bits(mantis, 0x0c, 1);
	msleep_interruptible(100);
	gpio_set_bits(mantis, 0x0c, 1);
	msleep_interruptible(100);
}

void mantis_fe_powerdown(struct mantis_pci *mantis)
{
	dprintk(verbose, MANTIS_DEBUG, 1, "Frontend Power OFF");
	gpio_set_bits(mantis, 0x0c, 0);
}

static int mantis_fe_reset(struct dvb_frontend *fe)
{
	struct mantis_pci *mantis = fe->dvb->priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Frontend Reset");
	gpio_set_bits(mantis, 13, 0);
	msleep_interruptible(100);
	gpio_set_bits(mantis, 13, 0);
	msleep_interruptible(100);
	gpio_set_bits(mantis, 13, 1);
	msleep_interruptible(100);
	gpio_set_bits(mantis, 13, 1);

	return 0;
}

static int mantis_frontend_reset(struct mantis_pci *mantis)
{
	dprintk(verbose, MANTIS_DEBUG, 1, "Frontend Reset");
	gpio_set_bits(mantis, 13, 0);
	msleep_interruptible(100);
	gpio_set_bits(mantis, 13, 0);
	msleep_interruptible(100);
	gpio_set_bits(mantis, 13, 1);
	msleep_interruptible(100);
	gpio_set_bits(mantis, 13, 1);

	return 0;
}

static int mantis_dvb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct mantis_pci *mantis = dvbdmx->priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Mantis DVB Start feed");
	if (!dvbdmx->dmx.frontend) {
		dprintk(verbose, MANTIS_DEBUG, 1, "no frontend ?");
		return -EINVAL;
	}
	mantis->feeds++;
	dprintk(verbose, MANTIS_DEBUG, 1,
		"mantis start feed, feeds=%d",
		mantis->feeds);

	if (mantis->feeds == 1)	 {
		dprintk(verbose, MANTIS_DEBUG, 1, "mantis start feed & dma");
		printk("mantis start feed & dma\n");
		mantis_dma_start(mantis);
	}

	return mantis->feeds;
}

static int mantis_dvb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct mantis_pci *mantis = dvbdmx->priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Mantis DVB Stop feed");
	if (!dvbdmx->dmx.frontend) {
		dprintk(verbose, MANTIS_DEBUG, 1, "no frontend ?");
		return -EINVAL;
	}
	mantis->feeds--;
	if (mantis->feeds == 0) {
		dprintk(verbose, MANTIS_DEBUG, 1, "mantis stop feed and dma");
		printk("mantis stop feed and dma\n");
		mantis_dma_stop(mantis);
	}
	return 0;
}

int __devinit mantis_dvb_init(struct mantis_pci *mantis)
{
	int result;

	dprintk(verbose, MANTIS_DEBUG, 1, "dvb_register_adapter");
	if (dvb_register_adapter(&mantis->dvb_adapter,
				 "Mantis dvb adapter", THIS_MODULE,
				 &mantis->pdev->dev) < 0) {

		dprintk(verbose, MANTIS_ERROR, 1, "Error registering adapter");
		return -ENODEV;
	}
	mantis->dvb_adapter.priv = mantis;
	mantis->demux.dmx.capabilities = DMX_TS_FILTERING	|
					 DMX_SECTION_FILTERING	|
					 DMX_MEMORY_BASED_FILTERING;

	mantis->demux.priv = mantis;
	mantis->demux.filternum = 256;
	mantis->demux.feednum = 256;
	mantis->demux.start_feed = mantis_dvb_start_feed;
	mantis->demux.stop_feed = mantis_dvb_stop_feed;
	mantis->demux.write_to_decoder = NULL;
	dprintk(verbose, MANTIS_DEBUG, 1, "dvb_dmx_init");
	if ((result = dvb_dmx_init(&mantis->demux)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1,
			"dvb_dmx_init failed, ERROR=%d", result);

		goto err0;
	}
	mantis->dmxdev.filternum = 256;
	mantis->dmxdev.demux = &mantis->demux.dmx;
	mantis->dmxdev.capabilities = 0;
	dprintk(verbose, MANTIS_DEBUG, 1, "dvb_dmxdev_init");
	if ((result = dvb_dmxdev_init(&mantis->dmxdev,
				      &mantis->dvb_adapter)) < 0) {

		dprintk(verbose, MANTIS_ERROR, 1,
			"dvb_dmxdev_init failed, ERROR=%d", result);
		goto err1;
	}
	mantis->fe_hw.source = DMX_FRONTEND_0;
	if ((result = mantis->demux.dmx.add_frontend(&mantis->demux.dmx,
						     &mantis->fe_hw)) < 0) {

		dprintk(verbose, MANTIS_ERROR, 1,
			"dvb_dmx_init failed, ERROR=%d", result);

		goto err2;
	}
	mantis->fe_mem.source = DMX_MEMORY_FE;
	if ((result = mantis->demux.dmx.add_frontend(&mantis->demux.dmx,
						     &mantis->fe_mem)) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1,
			"dvb_dmx_init failed, ERROR=%d", result);

		goto err3;
	}
	if ((result = mantis->demux.dmx.connect_frontend(&mantis->demux.dmx,
							 &mantis->fe_hw)) < 0) {

		dprintk(verbose, MANTIS_ERROR, 1,
			"dvb_dmx_init failed, ERROR=%d", result);

		goto err4;
	}
	dvb_net_init(&mantis->dvb_adapter, &mantis->dvbnet, &mantis->demux.dmx);
	tasklet_init(&mantis->tasklet, mantis_dma_xfer, (unsigned long) mantis);
	mantis_frontend_init(mantis);
	return 0;

	/*	Error conditions ..	*/
err4:
	mantis->demux.dmx.remove_frontend(&mantis->demux.dmx, &mantis->fe_mem);
err3:
	mantis->demux.dmx.remove_frontend(&mantis->demux.dmx, &mantis->fe_hw);
err2:
	dvb_dmxdev_release(&mantis->dmxdev);
err1:
	dvb_dmx_release(&mantis->demux);
err0:
	dvb_unregister_adapter(&mantis->dvb_adapter);

	return result;
}

int __devinit mantis_frontend_init(struct mantis_pci *mantis)
{
	dprintk(verbose, MANTIS_DEBUG, 1, "Mantis frontend Init");
	mantis_fe_powerup(mantis);
	mantis_frontend_reset(mantis);
	dprintk(verbose, MANTIS_DEBUG, 1, "Device ID=%02x", mantis->subsystem_device);
	switch (mantis->subsystem_device) {
	case MANTIS_VP_1033_DVB_S:	// VP-1033
		dprintk(verbose, MANTIS_ERROR, 1, "Probing for STV0299 (DVB-S)");
		mantis->fe = stv0299_attach(&lgtdqcs001f_config,
					    &mantis->adapter);

		if (mantis->fe) {
			mantis->fe->ops.tuner_ops.set_params = lgtdqcs001f_tuner_set;
			dprintk(verbose, MANTIS_ERROR, 1,
				"found STV0299 DVB-S frontend @ 0x%02x",
				lgtdqcs001f_config.demod_address);

			dprintk(verbose, MANTIS_ERROR, 1,
				"Mantis DVB-S STV0299 frontend attach success");
		}
		break;
	case MANTIS_VP_1034_DVB_S:	// VP-1034
		dprintk(verbose, MANTIS_ERROR, 1, "Probing for MB86A16 (DVB-S/DSS)");
		mantis->fe = mb86a16_attach(&vp1034_config, &mantis->adapter);
		if (mantis->fe) {
			dprintk(verbose, MANTIS_ERROR, 1,
			"found MB86A16 DVB-S/DSS frontend @0x%02x",
			vp1034_config.demod_address);

		}
		break;
	case MANTIS_VP_2033_DVB_C:	// VP-2033
		dprintk(verbose, MANTIS_ERROR, 1, "Probing for CU1216 (DVB-C)");
		mantis->fe = cu1216_attach(&philips_cu1216_config, &mantis->adapter);
		if (mantis->fe) {
			mantis->fe->ops.tuner_ops.set_params = philips_cu1216_tuner_set;
			dprintk(verbose, MANTIS_ERROR, 1,
				"found Philips CU1216 DVB-C frontend @ 0x%02x",
				philips_cu1216_config.demod_address);

			dprintk(verbose, MANTIS_ERROR, 1,
				"Mantis DVB-C Philips CU1216 frontend attach success");

		}
		break;
	default:
		dprintk(verbose, MANTIS_DEBUG, 1, "Unknown frontend:[0x%02x]",
			mantis->sub_device_id);

		return -ENODEV;
	}
	if (mantis->fe == NULL) {
		dprintk(verbose, MANTIS_ERROR, 1, "!!! NO Frontends found !!!");
		return -ENODEV;
	} else {
		if (dvb_register_frontend(&mantis->dvb_adapter, mantis->fe)) {
			dprintk(verbose, MANTIS_ERROR, 1,
				"ERROR: Frontend registration failed");

			if (mantis->fe->ops.release)
				mantis->fe->ops.release(mantis->fe);

			mantis->fe = NULL;
			return -ENODEV;
		}
	}

	return 0;
}

int __devexit mantis_dvb_exit(struct mantis_pci *mantis)
{
	tasklet_kill(&mantis->tasklet);
	dvb_net_release(&mantis->dvbnet);
	mantis->demux.dmx.remove_frontend(&mantis->demux.dmx, &mantis->fe_mem);
	mantis->demux.dmx.remove_frontend(&mantis->demux.dmx, &mantis->fe_hw);
	dvb_dmxdev_release(&mantis->dmxdev);
	dvb_dmx_release(&mantis->demux);

	if (mantis->fe)
		dvb_unregister_frontend(mantis->fe);
	dprintk(verbose, MANTIS_DEBUG, 1, "dvb_unregister_adapter");
	dvb_unregister_adapter(&mantis->dvb_adapter);

	return 0;
}
