/*
 * Linux driver for digital TV devices equipped with B2C2 FlexcopII(b)/III
 * flexcop.c - main module part
 * Copyright (C) 2004-9 Patrick Boettcher <patrick.boettcher@posteo.de>
 * based on skystar2-driver Copyright (C) 2003 Vadim Catana, skystar@moldova.cc
 *
 * Acknowledgements:
 *   John Jurrius from BBTI, Inc. for extensive support
 *                    with code examples and data books
 *   Bjarne Steinsbo, bjarne at steinsbo.com (some ideas for rewriting)
 *
 * Contributions to the skystar2-driver have been done by
 *   Vincenzo Di Massa, hawk.it at tiscalinet.it (several DiSEqC fixes)
 *   Roberto Ragusa, r.ragusa at libero.it (polishing, restyling the code)
 *   Uwe Bugla, uwe.bugla at gmx.de (doing tests, restyling code, writing docu)
 *   Niklas Peinecke, peinecke at gdv.uni-hannover.de (hardware pid/mac
 *               filtering)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "flexcop.h"

#define DRIVER_NAME "B2C2 FlexcopII/II(b)/III digital TV receiver chip"
#define DRIVER_AUTHOR "Patrick Boettcher <patrick.boettcher@posteo.de"

#ifdef CONFIG_DVB_B2C2_FLEXCOP_DEBUG
#define DEBSTATUS ""
#else
#define DEBSTATUS " (debugging is not enabled)"
#endif

int b2c2_flexcop_debug;
EXPORT_SYMBOL_GPL(b2c2_flexcop_debug);
module_param_named(debug, b2c2_flexcop_debug,  int, 0644);
MODULE_PARM_DESC(debug,
		"set debug level (1=info,2=tuner,4=i2c,8=ts,"
		"16=sram,32=reg (|-able))."
		DEBSTATUS);
#undef DEBSTATUS

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* global zero for ibi values */
flexcop_ibi_value ibi_zero;

static int flexcop_dvb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct flexcop_device *fc = dvbdmxfeed->demux->priv;
	return flexcop_pid_feed_control(fc, dvbdmxfeed, 1);
}

static int flexcop_dvb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct flexcop_device *fc = dvbdmxfeed->demux->priv;
	return flexcop_pid_feed_control(fc, dvbdmxfeed, 0);
}

static int flexcop_dvb_init(struct flexcop_device *fc)
{
	int ret = dvb_register_adapter(&fc->dvb_adapter,
			"FlexCop Digital TV device", fc->owner,
			fc->dev, adapter_nr);
	if (ret < 0) {
		err("error registering DVB adapter");
		return ret;
	}
	fc->dvb_adapter.priv = fc;

	fc->demux.dmx.capabilities = (DMX_TS_FILTERING | DMX_SECTION_FILTERING
			| DMX_MEMORY_BASED_FILTERING);
	fc->demux.priv = fc;
	fc->demux.filternum = fc->demux.feednum = FC_MAX_FEED;
	fc->demux.start_feed = flexcop_dvb_start_feed;
	fc->demux.stop_feed = flexcop_dvb_stop_feed;
	fc->demux.write_to_decoder = NULL;

	ret = dvb_dmx_init(&fc->demux);
	if (ret < 0) {
		err("dvb_dmx failed: error %d", ret);
		goto err_dmx;
	}

	fc->hw_frontend.source = DMX_FRONTEND_0;

	fc->dmxdev.filternum = fc->demux.feednum;
	fc->dmxdev.demux = &fc->demux.dmx;
	fc->dmxdev.capabilities = 0;
	ret = dvb_dmxdev_init(&fc->dmxdev, &fc->dvb_adapter);
	if (ret < 0) {
		err("dvb_dmxdev_init failed: error %d", ret);
		goto err_dmx_dev;
	}

	ret = fc->demux.dmx.add_frontend(&fc->demux.dmx, &fc->hw_frontend);
	if (ret < 0) {
		err("adding hw_frontend to dmx failed: error %d", ret);
		goto err_dmx_add_hw_frontend;
	}

	fc->mem_frontend.source = DMX_MEMORY_FE;
	ret = fc->demux.dmx.add_frontend(&fc->demux.dmx, &fc->mem_frontend);
	if (ret < 0) {
		err("adding mem_frontend to dmx failed: error %d", ret);
		goto err_dmx_add_mem_frontend;
	}

	ret = fc->demux.dmx.connect_frontend(&fc->demux.dmx, &fc->hw_frontend);
	if (ret < 0) {
		err("connect frontend failed: error %d", ret);
		goto err_connect_frontend;
	}

	ret = dvb_net_init(&fc->dvb_adapter, &fc->dvbnet, &fc->demux.dmx);
	if (ret < 0) {
		err("dvb_net_init failed: error %d", ret);
		goto err_net;
	}

	fc->init_state |= FC_STATE_DVB_INIT;
	return 0;

err_net:
	fc->demux.dmx.disconnect_frontend(&fc->demux.dmx);
err_connect_frontend:
	fc->demux.dmx.remove_frontend(&fc->demux.dmx, &fc->mem_frontend);
err_dmx_add_mem_frontend:
	fc->demux.dmx.remove_frontend(&fc->demux.dmx, &fc->hw_frontend);
err_dmx_add_hw_frontend:
	dvb_dmxdev_release(&fc->dmxdev);
err_dmx_dev:
	dvb_dmx_release(&fc->demux);
err_dmx:
	dvb_unregister_adapter(&fc->dvb_adapter);
	return ret;
}

static void flexcop_dvb_exit(struct flexcop_device *fc)
{
	if (fc->init_state & FC_STATE_DVB_INIT) {
		dvb_net_release(&fc->dvbnet);

		fc->demux.dmx.close(&fc->demux.dmx);
		fc->demux.dmx.remove_frontend(&fc->demux.dmx,
			&fc->mem_frontend);
		fc->demux.dmx.remove_frontend(&fc->demux.dmx,
			&fc->hw_frontend);
		dvb_dmxdev_release(&fc->dmxdev);
		dvb_dmx_release(&fc->demux);
		dvb_unregister_adapter(&fc->dvb_adapter);
		deb_info("deinitialized dvb stuff\n");
	}
	fc->init_state &= ~FC_STATE_DVB_INIT;
}

/* these methods are necessary to achieve the long-term-goal of hiding the
 * struct flexcop_device from the bus-parts */
void flexcop_pass_dmx_data(struct flexcop_device *fc, u8 *buf, u32 len)
{
	dvb_dmx_swfilter(&fc->demux, buf, len);
}
EXPORT_SYMBOL(flexcop_pass_dmx_data);

void flexcop_pass_dmx_packets(struct flexcop_device *fc, u8 *buf, u32 no)
{
	dvb_dmx_swfilter_packets(&fc->demux, buf, no);
}
EXPORT_SYMBOL(flexcop_pass_dmx_packets);

static void flexcop_reset(struct flexcop_device *fc)
{
	flexcop_ibi_value v210, v204;

	/* reset the flexcop itself */
	fc->write_ibi_reg(fc,ctrl_208,ibi_zero);

	v210.raw = 0;
	v210.sw_reset_210.reset_block_000 = 1;
	v210.sw_reset_210.reset_block_100 = 1;
	v210.sw_reset_210.reset_block_200 = 1;
	v210.sw_reset_210.reset_block_300 = 1;
	v210.sw_reset_210.reset_block_400 = 1;
	v210.sw_reset_210.reset_block_500 = 1;
	v210.sw_reset_210.reset_block_600 = 1;
	v210.sw_reset_210.reset_block_700 = 1;
	v210.sw_reset_210.Block_reset_enable = 0xb2;
	v210.sw_reset_210.Special_controls = 0xc259;
	fc->write_ibi_reg(fc,sw_reset_210,v210);
	msleep(1);

	/* reset the periphical devices */

	v204 = fc->read_ibi_reg(fc,misc_204);
	v204.misc_204.Per_reset_sig = 0;
	fc->write_ibi_reg(fc,misc_204,v204);
	msleep(1);
	v204.misc_204.Per_reset_sig = 1;
	fc->write_ibi_reg(fc,misc_204,v204);
}

void flexcop_reset_block_300(struct flexcop_device *fc)
{
	flexcop_ibi_value v208_save = fc->read_ibi_reg(fc, ctrl_208),
			  v210 = fc->read_ibi_reg(fc, sw_reset_210);

	deb_rdump("208: %08x, 210: %08x\n", v208_save.raw, v210.raw);
	fc->write_ibi_reg(fc,ctrl_208,ibi_zero);

	v210.sw_reset_210.reset_block_300 = 1;
	v210.sw_reset_210.Block_reset_enable = 0xb2;

	fc->write_ibi_reg(fc,sw_reset_210,v210);
	fc->write_ibi_reg(fc,ctrl_208,v208_save);
}

struct flexcop_device *flexcop_device_kmalloc(size_t bus_specific_len)
{
	void *bus;
	struct flexcop_device *fc = kzalloc(sizeof(struct flexcop_device),
				GFP_KERNEL);
	if (!fc) {
		err("no memory");
		return NULL;
	}

	bus = kzalloc(bus_specific_len, GFP_KERNEL);
	if (!bus) {
		err("no memory");
		kfree(fc);
		return NULL;
	}

	fc->bus_specific = bus;

	return fc;
}
EXPORT_SYMBOL(flexcop_device_kmalloc);

void flexcop_device_kfree(struct flexcop_device *fc)
{
	kfree(fc->bus_specific);
	kfree(fc);
}
EXPORT_SYMBOL(flexcop_device_kfree);

int flexcop_device_initialize(struct flexcop_device *fc)
{
	int ret;
	ibi_zero.raw = 0;

	flexcop_reset(fc);
	flexcop_determine_revision(fc);
	flexcop_sram_init(fc);
	flexcop_hw_filter_init(fc);
	flexcop_smc_ctrl(fc, 0);

	ret = flexcop_dvb_init(fc);
	if (ret)
		goto error;

	/* i2c has to be done before doing EEProm stuff -
	 * because the EEProm is accessed via i2c */
	ret = flexcop_i2c_init(fc);
	if (ret)
		goto error;

	/* do the MAC address reading after initializing the dvb_adapter */
	if (fc->get_mac_addr(fc, 0) == 0) {
		u8 *b = fc->dvb_adapter.proposed_mac;
		info("MAC address = %pM", b);
		flexcop_set_mac_filter(fc,b);
		flexcop_mac_filter_ctrl(fc,1);
	} else
		warn("reading of MAC address failed.\n");

	ret = flexcop_frontend_init(fc);
	if (ret)
		goto error;

	flexcop_device_name(fc,"initialization of","complete");
	return 0;

error:
	flexcop_device_exit(fc);
	return ret;
}
EXPORT_SYMBOL(flexcop_device_initialize);

void flexcop_device_exit(struct flexcop_device *fc)
{
	flexcop_frontend_exit(fc);
	flexcop_i2c_exit(fc);
	flexcop_dvb_exit(fc);
}
EXPORT_SYMBOL(flexcop_device_exit);

static int flexcop_module_init(void)
{
	info(DRIVER_NAME " loaded successfully");
	return 0;
}

static void flexcop_module_cleanup(void)
{
	info(DRIVER_NAME " unloaded successfully");
}

module_init(flexcop_module_init);
module_exit(flexcop_module_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_NAME);
MODULE_LICENSE("GPL");
