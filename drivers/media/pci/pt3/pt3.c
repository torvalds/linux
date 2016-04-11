/*
 * Earthsoft PT3 driver
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/freezer.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"

#include "pt3.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static bool one_adapter;
module_param(one_adapter, bool, 0444);
MODULE_PARM_DESC(one_adapter, "Place FE's together under one adapter.");

static int num_bufs = 4;
module_param(num_bufs, int, 0444);
MODULE_PARM_DESC(num_bufs, "Number of DMA buffer (188KiB) per FE.");


static const struct i2c_algorithm pt3_i2c_algo = {
	.master_xfer   = &pt3_i2c_master_xfer,
	.functionality = &pt3_i2c_functionality,
};

static const struct pt3_adap_config adap_conf[PT3_NUM_FE] = {
	{
		.demod_info = {
			I2C_BOARD_INFO(TC90522_I2C_DEV_SAT, 0x11),
		},
		.tuner_info = {
			I2C_BOARD_INFO("qm1d1c0042", 0x63),
		},
		.tuner_cfg.qm1d1c0042 = {
			.lpf = 1,
		},
		.init_freq = 1049480 - 300,
	},
	{
		.demod_info = {
			I2C_BOARD_INFO(TC90522_I2C_DEV_TER, 0x10),
		},
		.tuner_info = {
			I2C_BOARD_INFO("mxl301rf", 0x62),
		},
		.init_freq = 515142857,
	},
	{
		.demod_info = {
			I2C_BOARD_INFO(TC90522_I2C_DEV_SAT, 0x13),
		},
		.tuner_info = {
			I2C_BOARD_INFO("qm1d1c0042", 0x60),
		},
		.tuner_cfg.qm1d1c0042 = {
			.lpf = 1,
		},
		.init_freq = 1049480 + 300,
	},
	{
		.demod_info = {
			I2C_BOARD_INFO(TC90522_I2C_DEV_TER, 0x12),
		},
		.tuner_info = {
			I2C_BOARD_INFO("mxl301rf", 0x61),
		},
		.init_freq = 521142857,
	},
};


struct reg_val {
	u8 reg;
	u8 val;
};

static int
pt3_demod_write(struct pt3_adapter *adap, const struct reg_val *data, int num)
{
	struct i2c_msg msg;
	int i, ret;

	ret = 0;
	msg.addr = adap->i2c_demod->addr;
	msg.flags = 0;
	msg.len = 2;
	for (i = 0; i < num; i++) {
		msg.buf = (u8 *)&data[i];
		ret = i2c_transfer(adap->i2c_demod->adapter, &msg, 1);
		if (ret == 0)
			ret = -EREMOTE;
		if (ret < 0)
			return ret;
	}
	return 0;
}

static inline void pt3_lnb_ctrl(struct pt3_board *pt3, bool on)
{
	iowrite32((on ? 0x0f : 0x0c), pt3->regs[0] + REG_SYSTEM_W);
}

static inline struct pt3_adapter *pt3_find_adapter(struct dvb_frontend *fe)
{
	struct pt3_board *pt3;
	int i;

	if (one_adapter) {
		pt3 = fe->dvb->priv;
		for (i = 0; i < PT3_NUM_FE; i++)
			if (pt3->adaps[i]->fe == fe)
				return pt3->adaps[i];
	}
	return container_of(fe->dvb, struct pt3_adapter, dvb_adap);
}

/*
 * all 4 tuners in PT3 are packaged in a can module (Sharp VA4M6JC2103).
 * it seems that they share the power lines and Amp power line and
 * adaps[3] controls those powers.
 */
static int
pt3_set_tuner_power(struct pt3_board *pt3, bool tuner_on, bool amp_on)
{
	struct reg_val rv = { 0x1e, 0x99 };

	if (tuner_on)
		rv.val |= 0x40;
	if (amp_on)
		rv.val |= 0x04;
	return pt3_demod_write(pt3->adaps[PT3_NUM_FE - 1], &rv, 1);
}

static int pt3_set_lna(struct dvb_frontend *fe)
{
	struct pt3_adapter *adap;
	struct pt3_board *pt3;
	u32 val;
	int ret;

	/* LNA is shared btw. 2 TERR-tuners */

	adap = pt3_find_adapter(fe);
	val = fe->dtv_property_cache.lna;
	if (val == LNA_AUTO || val == adap->cur_lna)
		return 0;

	pt3 = adap->dvb_adap.priv;
	if (mutex_lock_interruptible(&pt3->lock))
		return -ERESTARTSYS;
	if (val)
		pt3->lna_on_cnt++;
	else
		pt3->lna_on_cnt--;

	if (val && pt3->lna_on_cnt <= 1) {
		pt3->lna_on_cnt = 1;
		ret = pt3_set_tuner_power(pt3, true, true);
	} else if (!val && pt3->lna_on_cnt <= 0) {
		pt3->lna_on_cnt = 0;
		ret = pt3_set_tuner_power(pt3, true, false);
	} else
		ret = 0;
	mutex_unlock(&pt3->lock);
	adap->cur_lna = (val != 0);
	return ret;
}

static int pt3_set_voltage(struct dvb_frontend *fe, enum fe_sec_voltage volt)
{
	struct pt3_adapter *adap;
	struct pt3_board *pt3;
	bool on;

	/* LNB power is shared btw. 2 SAT-tuners */

	adap = pt3_find_adapter(fe);
	on = (volt != SEC_VOLTAGE_OFF);
	if (on == adap->cur_lnb)
		return 0;
	adap->cur_lnb = on;
	pt3 = adap->dvb_adap.priv;
	if (mutex_lock_interruptible(&pt3->lock))
		return -ERESTARTSYS;
	if (on)
		pt3->lnb_on_cnt++;
	else
		pt3->lnb_on_cnt--;

	if (on && pt3->lnb_on_cnt <= 1) {
		pt3->lnb_on_cnt = 1;
		pt3_lnb_ctrl(pt3, true);
	} else if (!on && pt3->lnb_on_cnt <= 0) {
		pt3->lnb_on_cnt = 0;
		pt3_lnb_ctrl(pt3, false);
	}
	mutex_unlock(&pt3->lock);
	return 0;
}

/* register values used in pt3_fe_init() */

static const struct reg_val init0_sat[] = {
	{ 0x03, 0x01 },
	{ 0x1e, 0x10 },
};
static const struct reg_val init0_ter[] = {
	{ 0x01, 0x40 },
	{ 0x1c, 0x10 },
};
static const struct reg_val cfg_sat[] = {
	{ 0x1c, 0x15 },
	{ 0x1f, 0x04 },
};
static const struct reg_val cfg_ter[] = {
	{ 0x1d, 0x01 },
};

/*
 * pt3_fe_init: initialize demod sub modules and ISDB-T tuners all at once.
 *
 * As for demod IC (TC90522) and ISDB-T tuners (MxL301RF),
 * the i2c sequences for init'ing them are not public and hidden in a ROM,
 * and include the board specific configurations as well.
 * They are stored in a lump and cannot be taken out / accessed separately,
 * thus cannot be moved to the FE/tuner driver.
 */
static int pt3_fe_init(struct pt3_board *pt3)
{
	int i, ret;
	struct dvb_frontend *fe;

	pt3_i2c_reset(pt3);
	ret = pt3_init_all_demods(pt3);
	if (ret < 0) {
		dev_warn(&pt3->pdev->dev, "Failed to init demod chips\n");
		return ret;
	}

	/* additional config? */
	for (i = 0; i < PT3_NUM_FE; i++) {
		fe = pt3->adaps[i]->fe;

		if (fe->ops.delsys[0] == SYS_ISDBS)
			ret = pt3_demod_write(pt3->adaps[i],
					      init0_sat, ARRAY_SIZE(init0_sat));
		else
			ret = pt3_demod_write(pt3->adaps[i],
					      init0_ter, ARRAY_SIZE(init0_ter));
		if (ret < 0) {
			dev_warn(&pt3->pdev->dev,
				 "demod[%d] failed in init sequence0\n", i);
			return ret;
		}
		ret = fe->ops.init(fe);
		if (ret < 0)
			return ret;
	}

	usleep_range(2000, 4000);
	ret = pt3_set_tuner_power(pt3, true, false);
	if (ret < 0) {
		dev_warn(&pt3->pdev->dev, "Failed to control tuner module\n");
		return ret;
	}

	/* output pin configuration */
	for (i = 0; i < PT3_NUM_FE; i++) {
		fe = pt3->adaps[i]->fe;
		if (fe->ops.delsys[0] == SYS_ISDBS)
			ret = pt3_demod_write(pt3->adaps[i],
						cfg_sat, ARRAY_SIZE(cfg_sat));
		else
			ret = pt3_demod_write(pt3->adaps[i],
						cfg_ter, ARRAY_SIZE(cfg_ter));
		if (ret < 0) {
			dev_warn(&pt3->pdev->dev,
				 "demod[%d] failed in init sequence1\n", i);
			return ret;
		}
	}
	usleep_range(4000, 6000);

	for (i = 0; i < PT3_NUM_FE; i++) {
		fe = pt3->adaps[i]->fe;
		if (fe->ops.delsys[0] != SYS_ISDBS)
			continue;
		/* init and wake-up ISDB-S tuners */
		ret = fe->ops.tuner_ops.init(fe);
		if (ret < 0) {
			dev_warn(&pt3->pdev->dev,
				 "Failed to init SAT-tuner[%d]\n", i);
			return ret;
		}
	}
	ret = pt3_init_all_mxl301rf(pt3);
	if (ret < 0) {
		dev_warn(&pt3->pdev->dev, "Failed to init TERR-tuners\n");
		return ret;
	}

	ret = pt3_set_tuner_power(pt3, true, true);
	if (ret < 0) {
		dev_warn(&pt3->pdev->dev, "Failed to control tuner module\n");
		return ret;
	}

	/* Wake up all tuners and make an initial tuning,
	 * in order to avoid interference among the tuners in the module,
	 * according to the doc from the manufacturer.
	 */
	for (i = 0; i < PT3_NUM_FE; i++) {
		fe = pt3->adaps[i]->fe;
		ret = 0;
		if (fe->ops.delsys[0] == SYS_ISDBT)
			ret = fe->ops.tuner_ops.init(fe);
		/* set only when called from pt3_probe(), not resume() */
		if (ret == 0 && fe->dtv_property_cache.frequency == 0) {
			fe->dtv_property_cache.frequency =
						adap_conf[i].init_freq;
			ret = fe->ops.tuner_ops.set_params(fe);
		}
		if (ret < 0) {
			dev_warn(&pt3->pdev->dev,
				 "Failed in initial tuning of tuner[%d]\n", i);
			return ret;
		}
	}

	/* and sleep again, waiting to be opened by users. */
	for (i = 0; i < PT3_NUM_FE; i++) {
		fe = pt3->adaps[i]->fe;
		if (fe->ops.tuner_ops.sleep)
			ret = fe->ops.tuner_ops.sleep(fe);
		if (ret < 0)
			break;
		if (fe->ops.sleep)
			ret = fe->ops.sleep(fe);
		if (ret < 0)
			break;
		if (fe->ops.delsys[0] == SYS_ISDBS)
			fe->ops.set_voltage = &pt3_set_voltage;
		else
			fe->ops.set_lna = &pt3_set_lna;
	}
	if (i < PT3_NUM_FE) {
		dev_warn(&pt3->pdev->dev, "FE[%d] failed to standby\n", i);
		return ret;
	}
	return 0;
}


static int pt3_attach_fe(struct pt3_board *pt3, int i)
{
	struct i2c_board_info info;
	struct tc90522_config cfg;
	struct i2c_client *cl;
	struct dvb_adapter *dvb_adap;
	int ret;

	info = adap_conf[i].demod_info;
	cfg = adap_conf[i].demod_cfg;
	cfg.tuner_i2c = NULL;
	info.platform_data = &cfg;

	ret = -ENODEV;
	request_module("tc90522");
	cl = i2c_new_device(&pt3->i2c_adap, &info);
	if (!cl || !cl->dev.driver)
		return -ENODEV;
	pt3->adaps[i]->i2c_demod = cl;
	if (!try_module_get(cl->dev.driver->owner))
		goto err_demod_i2c_unregister_device;

	if (!strncmp(cl->name, TC90522_I2C_DEV_SAT,
		     strlen(TC90522_I2C_DEV_SAT))) {
		struct qm1d1c0042_config tcfg;

		tcfg = adap_conf[i].tuner_cfg.qm1d1c0042;
		tcfg.fe = cfg.fe;
		info = adap_conf[i].tuner_info;
		info.platform_data = &tcfg;
		request_module("qm1d1c0042");
		cl = i2c_new_device(cfg.tuner_i2c, &info);
	} else {
		struct mxl301rf_config tcfg;

		tcfg = adap_conf[i].tuner_cfg.mxl301rf;
		tcfg.fe = cfg.fe;
		info = adap_conf[i].tuner_info;
		info.platform_data = &tcfg;
		request_module("mxl301rf");
		cl = i2c_new_device(cfg.tuner_i2c, &info);
	}
	if (!cl || !cl->dev.driver)
		goto err_demod_module_put;
	pt3->adaps[i]->i2c_tuner = cl;
	if (!try_module_get(cl->dev.driver->owner))
		goto err_tuner_i2c_unregister_device;

	dvb_adap = &pt3->adaps[one_adapter ? 0 : i]->dvb_adap;
	ret = dvb_register_frontend(dvb_adap, cfg.fe);
	if (ret < 0)
		goto err_tuner_module_put;
	pt3->adaps[i]->fe = cfg.fe;
	return 0;

err_tuner_module_put:
	module_put(pt3->adaps[i]->i2c_tuner->dev.driver->owner);
err_tuner_i2c_unregister_device:
	i2c_unregister_device(pt3->adaps[i]->i2c_tuner);
err_demod_module_put:
	module_put(pt3->adaps[i]->i2c_demod->dev.driver->owner);
err_demod_i2c_unregister_device:
	i2c_unregister_device(pt3->adaps[i]->i2c_demod);

	return ret;
}


static int pt3_fetch_thread(void *data)
{
	struct pt3_adapter *adap = data;
	ktime_t delay;
	bool was_frozen;

#define PT3_INITIAL_BUF_DROPS 4
#define PT3_FETCH_DELAY 10
#define PT3_FETCH_DELAY_DELTA 2

	pt3_init_dmabuf(adap);
	adap->num_discard = PT3_INITIAL_BUF_DROPS;

	dev_dbg(adap->dvb_adap.device, "PT3: [%s] started\n",
		adap->thread->comm);
	set_freezable();
	while (!kthread_freezable_should_stop(&was_frozen)) {
		if (was_frozen)
			adap->num_discard = PT3_INITIAL_BUF_DROPS;

		pt3_proc_dma(adap);

		delay = ktime_set(0, PT3_FETCH_DELAY * NSEC_PER_MSEC);
		set_current_state(TASK_UNINTERRUPTIBLE);
		freezable_schedule_hrtimeout_range(&delay,
					PT3_FETCH_DELAY_DELTA * NSEC_PER_MSEC,
					HRTIMER_MODE_REL);
	}
	dev_dbg(adap->dvb_adap.device, "PT3: [%s] exited\n",
		adap->thread->comm);
	adap->thread = NULL;
	return 0;
}

static int pt3_start_streaming(struct pt3_adapter *adap)
{
	struct task_struct *thread;

	/* start fetching thread */
	thread = kthread_run(pt3_fetch_thread, adap, "pt3-ad%i-dmx%i",
				adap->dvb_adap.num, adap->dmxdev.dvbdev->id);
	if (IS_ERR(thread)) {
		int ret = PTR_ERR(thread);

		dev_warn(adap->dvb_adap.device,
			 "PT3 (adap:%d, dmx:%d): failed to start kthread\n",
			 adap->dvb_adap.num, adap->dmxdev.dvbdev->id);
		return ret;
	}
	adap->thread = thread;

	return pt3_start_dma(adap);
}

static int pt3_stop_streaming(struct pt3_adapter *adap)
{
	int ret;

	ret = pt3_stop_dma(adap);
	if (ret)
		dev_warn(adap->dvb_adap.device,
			 "PT3: failed to stop streaming of adap:%d/FE:%d\n",
			 adap->dvb_adap.num, adap->fe->id);

	/* kill the fetching thread */
	ret = kthread_stop(adap->thread);
	return ret;
}

static int pt3_start_feed(struct dvb_demux_feed *feed)
{
	struct pt3_adapter *adap;

	if (signal_pending(current))
		return -EINTR;

	adap = container_of(feed->demux, struct pt3_adapter, demux);
	adap->num_feeds++;
	if (adap->thread)
		return 0;
	if (adap->num_feeds != 1) {
		dev_warn(adap->dvb_adap.device,
			 "%s: unmatched start/stop_feed in adap:%i/dmx:%i\n",
			 __func__, adap->dvb_adap.num, adap->dmxdev.dvbdev->id);
		adap->num_feeds = 1;
	}

	return pt3_start_streaming(adap);

}

static int pt3_stop_feed(struct dvb_demux_feed *feed)
{
	struct pt3_adapter *adap;

	adap = container_of(feed->demux, struct pt3_adapter, demux);

	adap->num_feeds--;
	if (adap->num_feeds > 0 || !adap->thread)
		return 0;
	adap->num_feeds = 0;

	return pt3_stop_streaming(adap);
}


static int pt3_alloc_adapter(struct pt3_board *pt3, int index)
{
	int ret;
	struct pt3_adapter *adap;
	struct dvb_adapter *da;

	adap = kzalloc(sizeof(*adap), GFP_KERNEL);
	if (!adap)
		return -ENOMEM;

	pt3->adaps[index] = adap;
	adap->adap_idx = index;

	if (index == 0 || !one_adapter) {
		ret = dvb_register_adapter(&adap->dvb_adap, "PT3 DVB",
				THIS_MODULE, &pt3->pdev->dev, adapter_nr);
		if (ret < 0) {
			dev_err(&pt3->pdev->dev,
				"failed to register adapter dev\n");
			goto err_mem;
		}
		da = &adap->dvb_adap;
	} else
		da = &pt3->adaps[0]->dvb_adap;

	adap->dvb_adap.priv = pt3;
	adap->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	adap->demux.priv = adap;
	adap->demux.feednum = 256;
	adap->demux.filternum = 256;
	adap->demux.start_feed = pt3_start_feed;
	adap->demux.stop_feed = pt3_stop_feed;
	ret = dvb_dmx_init(&adap->demux);
	if (ret < 0) {
		dev_err(&pt3->pdev->dev, "failed to init dmx dev\n");
		goto err_adap;
	}

	adap->dmxdev.filternum = 256;
	adap->dmxdev.demux = &adap->demux.dmx;
	ret = dvb_dmxdev_init(&adap->dmxdev, da);
	if (ret < 0) {
		dev_err(&pt3->pdev->dev, "failed to init dmxdev\n");
		goto err_demux;
	}

	ret = pt3_alloc_dmabuf(adap);
	if (ret) {
		dev_err(&pt3->pdev->dev, "failed to alloc DMA buffers\n");
		goto err_dmabuf;
	}

	return 0;

err_dmabuf:
	pt3_free_dmabuf(adap);
	dvb_dmxdev_release(&adap->dmxdev);
err_demux:
	dvb_dmx_release(&adap->demux);
err_adap:
	if (index == 0 || !one_adapter)
		dvb_unregister_adapter(da);
err_mem:
	kfree(adap);
	pt3->adaps[index] = NULL;
	return ret;
}

static void pt3_cleanup_adapter(struct pt3_board *pt3, int index)
{
	struct pt3_adapter *adap;
	struct dmx_demux *dmx;

	adap = pt3->adaps[index];
	if (adap == NULL)
		return;

	/* stop demux kthread */
	if (adap->thread)
		pt3_stop_streaming(adap);

	dmx = &adap->demux.dmx;
	dmx->close(dmx);
	if (adap->fe) {
		adap->fe->callback = NULL;
		if (adap->fe->frontend_priv)
			dvb_unregister_frontend(adap->fe);
		if (adap->i2c_tuner) {
			module_put(adap->i2c_tuner->dev.driver->owner);
			i2c_unregister_device(adap->i2c_tuner);
		}
		if (adap->i2c_demod) {
			module_put(adap->i2c_demod->dev.driver->owner);
			i2c_unregister_device(adap->i2c_demod);
		}
	}
	pt3_free_dmabuf(adap);
	dvb_dmxdev_release(&adap->dmxdev);
	dvb_dmx_release(&adap->demux);
	if (index == 0 || !one_adapter)
		dvb_unregister_adapter(&adap->dvb_adap);
	kfree(adap);
	pt3->adaps[index] = NULL;
}

#ifdef CONFIG_PM_SLEEP

static int pt3_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pt3_board *pt3 = pci_get_drvdata(pdev);
	int i;
	struct pt3_adapter *adap;

	for (i = 0; i < PT3_NUM_FE; i++) {
		adap = pt3->adaps[i];
		if (adap->num_feeds > 0)
			pt3_stop_dma(adap);
		dvb_frontend_suspend(adap->fe);
		pt3_free_dmabuf(adap);
	}

	pt3_lnb_ctrl(pt3, false);
	pt3_set_tuner_power(pt3, false, false);
	return 0;
}

static int pt3_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pt3_board *pt3 = pci_get_drvdata(pdev);
	int i, ret;
	struct pt3_adapter *adap;

	ret = pt3_fe_init(pt3);
	if (ret)
		return ret;

	if (pt3->lna_on_cnt > 0)
		pt3_set_tuner_power(pt3, true, true);
	if (pt3->lnb_on_cnt > 0)
		pt3_lnb_ctrl(pt3, true);

	for (i = 0; i < PT3_NUM_FE; i++) {
		adap = pt3->adaps[i];
		dvb_frontend_resume(adap->fe);
		ret = pt3_alloc_dmabuf(adap);
		if (ret) {
			dev_err(&pt3->pdev->dev, "failed to alloc DMA bufs\n");
			continue;
		}
		if (adap->num_feeds > 0)
			pt3_start_dma(adap);
	}

	return 0;
}

#endif /* CONFIG_PM_SLEEP */


static void pt3_remove(struct pci_dev *pdev)
{
	struct pt3_board *pt3;
	int i;

	pt3 = pci_get_drvdata(pdev);
	for (i = PT3_NUM_FE - 1; i >= 0; i--)
		pt3_cleanup_adapter(pt3, i);
	i2c_del_adapter(&pt3->i2c_adap);
	kfree(pt3->i2c_buf);
	pci_iounmap(pt3->pdev, pt3->regs[0]);
	pci_iounmap(pt3->pdev, pt3->regs[1]);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(pt3);
}

static int pt3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	u8 rev;
	u32 ver;
	int i, ret;
	struct pt3_board *pt3;
	struct i2c_adapter *i2c;

	if (pci_read_config_byte(pdev, PCI_REVISION_ID, &rev) || rev != 1)
		return -ENODEV;

	ret = pci_enable_device(pdev);
	if (ret < 0)
		return -ENODEV;
	pci_set_master(pdev);

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret < 0)
		goto err_disable_device;

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret == 0)
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	else {
		ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (ret == 0)
			dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
		else {
			dev_err(&pdev->dev, "Failed to set DMA mask\n");
			goto err_release_regions;
		}
		dev_info(&pdev->dev, "Use 32bit DMA\n");
	}

	pt3 = kzalloc(sizeof(*pt3), GFP_KERNEL);
	if (!pt3) {
		ret = -ENOMEM;
		goto err_release_regions;
	}
	pci_set_drvdata(pdev, pt3);
	pt3->pdev = pdev;
	mutex_init(&pt3->lock);
	pt3->regs[0] = pci_ioremap_bar(pdev, 0);
	pt3->regs[1] = pci_ioremap_bar(pdev, 2);
	if (pt3->regs[0] == NULL || pt3->regs[1] == NULL) {
		dev_err(&pdev->dev, "Failed to ioremap\n");
		ret = -ENOMEM;
		goto err_kfree;
	}

	ver = ioread32(pt3->regs[0] + REG_VERSION);
	if ((ver >> 16) != 0x0301) {
		dev_warn(&pdev->dev, "PT%d, I/F-ver.:%d not supported\n",
			 ver >> 24, (ver & 0x00ff0000) >> 16);
		ret = -ENODEV;
		goto err_iounmap;
	}

	pt3->num_bufs = clamp_val(num_bufs, MIN_DATA_BUFS, MAX_DATA_BUFS);

	pt3->i2c_buf = kmalloc(sizeof(*pt3->i2c_buf), GFP_KERNEL);
	if (pt3->i2c_buf == NULL) {
		ret = -ENOMEM;
		goto err_iounmap;
	}
	i2c = &pt3->i2c_adap;
	i2c->owner = THIS_MODULE;
	i2c->algo = &pt3_i2c_algo;
	i2c->algo_data = NULL;
	i2c->dev.parent = &pdev->dev;
	strlcpy(i2c->name, DRV_NAME, sizeof(i2c->name));
	i2c_set_adapdata(i2c, pt3);
	ret = i2c_add_adapter(i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add i2c adapter\n");
		goto err_i2cbuf;
	}

	for (i = 0; i < PT3_NUM_FE; i++) {
		ret = pt3_alloc_adapter(pt3, i);
		if (ret < 0)
			break;

		ret = pt3_attach_fe(pt3, i);
		if (ret < 0)
			break;
	}
	if (i < PT3_NUM_FE) {
		dev_err(&pdev->dev, "Failed to create FE%d\n", i);
		goto err_cleanup_adapters;
	}

	ret = pt3_fe_init(pt3);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to init frontends\n");
		i = PT3_NUM_FE - 1;
		goto err_cleanup_adapters;
	}

	dev_info(&pdev->dev,
		 "successfully init'ed PT%d (fw:0x%02x, I/F:0x%02x)\n",
		 ver >> 24, (ver >> 8) & 0xff, (ver >> 16) & 0xff);
	return 0;

err_cleanup_adapters:
	while (i >= 0)
		pt3_cleanup_adapter(pt3, i--);
	i2c_del_adapter(i2c);
err_i2cbuf:
	kfree(pt3->i2c_buf);
err_iounmap:
	if (pt3->regs[0])
		pci_iounmap(pdev, pt3->regs[0]);
	if (pt3->regs[1])
		pci_iounmap(pdev, pt3->regs[1]);
err_kfree:
	kfree(pt3);
err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
	return ret;

}

static const struct pci_device_id pt3_id_table[] = {
	{ PCI_DEVICE_SUB(0x1172, 0x4c15, 0xee8d, 0x0368) },
	{ },
};
MODULE_DEVICE_TABLE(pci, pt3_id_table);

static SIMPLE_DEV_PM_OPS(pt3_pm_ops, pt3_suspend, pt3_resume);

static struct pci_driver pt3_driver = {
	.name		= DRV_NAME,
	.probe		= pt3_probe,
	.remove		= pt3_remove,
	.id_table	= pt3_id_table,

	.driver.pm	= &pt3_pm_ops,
};

module_pci_driver(pt3_driver);

MODULE_DESCRIPTION("Earthsoft PT3 Driver");
MODULE_AUTHOR("Akihiro TSUKADA");
MODULE_LICENSE("GPL");
