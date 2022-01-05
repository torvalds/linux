// SPDX-License-Identifier: GPL-2.0
/*
 * The Virtual DTV test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * When this module is loaded, it will attempt to modprobe 'dvb_vidtv_tuner'
 * and 'dvb_vidtv_demod'.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#include <linux/dev_printk.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <media/dvbdev.h>
#include <media/media-device.h>

#include "vidtv_bridge.h"
#include "vidtv_common.h"
#include "vidtv_demod.h"
#include "vidtv_mux.h"
#include "vidtv_ts.h"
#include "vidtv_tuner.h"

#define MUX_BUF_MIN_SZ 90164
#define MUX_BUF_MAX_SZ (MUX_BUF_MIN_SZ * 10)
#define TUNER_DEFAULT_ADDR 0x68
#define DEMOD_DEFAULT_ADDR 0x60
#define VIDTV_DEFAULT_NETWORK_ID 0xff44
#define VIDTV_DEFAULT_NETWORK_NAME "LinuxTV.org"
#define VIDTV_DEFAULT_TS_ID 0x4081

/*
 * The LNBf fake parameters here are the ranges used by an
 * Universal (extended) European LNBf, which is likely the most common LNBf
 * found on Satellite digital TV system nowadays.
 */
#define LNB_CUT_FREQUENCY	11700000	/* high IF frequency */
#define LNB_LOW_FREQ		9750000		/* low IF frequency */
#define LNB_HIGH_FREQ		10600000	/* transition frequency */

static unsigned int drop_tslock_prob_on_low_snr;
module_param(drop_tslock_prob_on_low_snr, uint, 0);
MODULE_PARM_DESC(drop_tslock_prob_on_low_snr,
		 "Probability of losing the TS lock if the signal quality is bad");

static unsigned int recover_tslock_prob_on_good_snr;
module_param(recover_tslock_prob_on_good_snr, uint, 0);
MODULE_PARM_DESC(recover_tslock_prob_on_good_snr,
		 "Probability recovering the TS lock when the signal improves");

static unsigned int mock_power_up_delay_msec;
module_param(mock_power_up_delay_msec, uint, 0);
MODULE_PARM_DESC(mock_power_up_delay_msec, "Simulate a power up delay");

static unsigned int mock_tune_delay_msec;
module_param(mock_tune_delay_msec, uint, 0);
MODULE_PARM_DESC(mock_tune_delay_msec, "Simulate a tune delay");

static unsigned int vidtv_valid_dvb_t_freqs[NUM_VALID_TUNER_FREQS] = {
	474000000
};

module_param_array(vidtv_valid_dvb_t_freqs, uint, NULL, 0);
MODULE_PARM_DESC(vidtv_valid_dvb_t_freqs,
		 "Valid DVB-T frequencies to simulate, in Hz");

static unsigned int vidtv_valid_dvb_c_freqs[NUM_VALID_TUNER_FREQS] = {
	474000000
};

module_param_array(vidtv_valid_dvb_c_freqs, uint, NULL, 0);
MODULE_PARM_DESC(vidtv_valid_dvb_c_freqs,
		 "Valid DVB-C frequencies to simulate, in Hz");

static unsigned int vidtv_valid_dvb_s_freqs[NUM_VALID_TUNER_FREQS] = {
	11362000
};
module_param_array(vidtv_valid_dvb_s_freqs, uint, NULL, 0);
MODULE_PARM_DESC(vidtv_valid_dvb_s_freqs,
		 "Valid DVB-S/S2 frequencies to simulate at Ku-Band, in kHz");

static unsigned int max_frequency_shift_hz;
module_param(max_frequency_shift_hz, uint, 0);
MODULE_PARM_DESC(max_frequency_shift_hz,
		 "Maximum shift in HZ allowed when tuning in a channel");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nums);

/*
 * Influences the signal acquisition time. See ISO/IEC 13818-1 : 2000. p. 113.
 */
static unsigned int si_period_msec = 40;
module_param(si_period_msec, uint, 0);
MODULE_PARM_DESC(si_period_msec, "How often to send SI packets. Default: 40ms");

static unsigned int pcr_period_msec = 40;
module_param(pcr_period_msec, uint, 0);
MODULE_PARM_DESC(pcr_period_msec,
		 "How often to send PCR packets. Default: 40ms");

static unsigned int mux_rate_kbytes_sec = 4096;
module_param(mux_rate_kbytes_sec, uint, 0);
MODULE_PARM_DESC(mux_rate_kbytes_sec, "Mux rate: will pad stream if below");

static unsigned int pcr_pid = 0x200;
module_param(pcr_pid, uint, 0);
MODULE_PARM_DESC(pcr_pid, "PCR PID for all channels: defaults to 0x200");

static unsigned int mux_buf_sz_pkts;
module_param(mux_buf_sz_pkts, uint, 0);
MODULE_PARM_DESC(mux_buf_sz_pkts,
		 "Size for the internal mux buffer in multiples of 188 bytes");

static u32 vidtv_bridge_mux_buf_sz_for_mux_rate(void)
{
	u32 max_elapsed_time_msecs =  VIDTV_MAX_SLEEP_USECS / USEC_PER_MSEC;
	u32 mux_buf_sz = mux_buf_sz_pkts * TS_PACKET_LEN;
	u32 nbytes_expected;

	nbytes_expected = mux_rate_kbytes_sec;
	nbytes_expected *= max_elapsed_time_msecs;

	mux_buf_sz = roundup(nbytes_expected, TS_PACKET_LEN);
	mux_buf_sz += mux_buf_sz / 10;

	if (mux_buf_sz < MUX_BUF_MIN_SZ)
		mux_buf_sz = MUX_BUF_MIN_SZ;

	if (mux_buf_sz > MUX_BUF_MAX_SZ)
		mux_buf_sz = MUX_BUF_MAX_SZ;

	return mux_buf_sz;
}

static bool vidtv_bridge_check_demod_lock(struct vidtv_dvb *dvb, u32 n)
{
	enum fe_status status;

	dvb->fe[n]->ops.read_status(dvb->fe[n], &status);

	return status == (FE_HAS_SIGNAL  |
			  FE_HAS_CARRIER |
			  FE_HAS_VITERBI |
			  FE_HAS_SYNC    |
			  FE_HAS_LOCK);
}

/*
 * called on a separate thread by the mux when new packets become available
 */
static void vidtv_bridge_on_new_pkts_avail(void *priv, u8 *buf, u32 npkts)
{
	struct vidtv_dvb *dvb = priv;

	/* drop packets if we lose the lock */
	if (vidtv_bridge_check_demod_lock(dvb, 0))
		dvb_dmx_swfilter_packets(&dvb->demux, buf, npkts);
}

static int vidtv_start_streaming(struct vidtv_dvb *dvb)
{
	struct vidtv_mux_init_args mux_args = {
		.mux_rate_kbytes_sec         = mux_rate_kbytes_sec,
		.on_new_packets_available_cb = vidtv_bridge_on_new_pkts_avail,
		.pcr_period_usecs            = pcr_period_msec * USEC_PER_MSEC,
		.si_period_usecs             = si_period_msec * USEC_PER_MSEC,
		.pcr_pid                     = pcr_pid,
		.transport_stream_id         = VIDTV_DEFAULT_TS_ID,
		.network_id                  = VIDTV_DEFAULT_NETWORK_ID,
		.network_name                = VIDTV_DEFAULT_NETWORK_NAME,
		.priv                        = dvb,
	};
	struct device *dev = &dvb->pdev->dev;
	u32 mux_buf_sz;

	if (dvb->streaming) {
		dev_warn_ratelimited(dev, "Already streaming. Skipping.\n");
		return 0;
	}

	if (mux_buf_sz_pkts)
		mux_buf_sz = mux_buf_sz_pkts;
	else
		mux_buf_sz = vidtv_bridge_mux_buf_sz_for_mux_rate();

	mux_args.mux_buf_sz  = mux_buf_sz;

	dvb->streaming = true;
	dvb->mux = vidtv_mux_init(dvb->fe[0], dev, &mux_args);
	if (!dvb->mux)
		return -ENOMEM;
	vidtv_mux_start_thread(dvb->mux);

	dev_dbg_ratelimited(dev, "Started streaming\n");
	return 0;
}

static int vidtv_stop_streaming(struct vidtv_dvb *dvb)
{
	struct device *dev = &dvb->pdev->dev;

	dvb->streaming = false;
	vidtv_mux_stop_thread(dvb->mux);
	vidtv_mux_destroy(dvb->mux);
	dvb->mux = NULL;

	dev_dbg_ratelimited(dev, "Stopped streaming\n");
	return 0;
}

static int vidtv_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct vidtv_dvb *dvb   = demux->priv;
	int ret;
	int rc;

	if (!demux->dmx.frontend)
		return -EINVAL;

	mutex_lock(&dvb->feed_lock);

	dvb->nfeeds++;
	rc = dvb->nfeeds;

	if (dvb->nfeeds == 1) {
		ret = vidtv_start_streaming(dvb);
		if (ret < 0)
			rc = ret;
	}

	mutex_unlock(&dvb->feed_lock);
	return rc;
}

static int vidtv_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct vidtv_dvb *dvb   = demux->priv;
	int err = 0;

	mutex_lock(&dvb->feed_lock);
	dvb->nfeeds--;

	if (!dvb->nfeeds)
		err = vidtv_stop_streaming(dvb);

	mutex_unlock(&dvb->feed_lock);
	return err;
}

static struct dvb_frontend *vidtv_get_frontend_ptr(struct i2c_client *c)
{
	struct vidtv_demod_state *state = i2c_get_clientdata(c);

	/* the demod will set this when its probe function runs */
	return &state->frontend;
}

static int vidtv_master_xfer(struct i2c_adapter *i2c_adap,
			     struct i2c_msg msgs[],
			     int num)
{
	/*
	 * Right now, this virtual driver doesn't really send or receive
	 * messages from I2C. A real driver will require an implementation
	 * here.
	 */
	return 0;
}

static u32 vidtv_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm vidtv_i2c_algorithm = {
	.master_xfer   = vidtv_master_xfer,
	.functionality = vidtv_i2c_func,
};

static int vidtv_bridge_i2c_register_adap(struct vidtv_dvb *dvb)
{
	struct i2c_adapter *i2c_adapter = &dvb->i2c_adapter;

	strscpy(i2c_adapter->name, "vidtv_i2c", sizeof(i2c_adapter->name));
	i2c_adapter->owner      = THIS_MODULE;
	i2c_adapter->algo       = &vidtv_i2c_algorithm;
	i2c_adapter->algo_data  = NULL;
	i2c_adapter->timeout    = 500;
	i2c_adapter->retries    = 3;
	i2c_adapter->dev.parent = &dvb->pdev->dev;

	i2c_set_adapdata(i2c_adapter, dvb);
	return i2c_add_adapter(&dvb->i2c_adapter);
}

static int vidtv_bridge_register_adap(struct vidtv_dvb *dvb)
{
	int ret = 0;

	ret = dvb_register_adapter(&dvb->adapter,
				   KBUILD_MODNAME,
				   THIS_MODULE,
				   &dvb->i2c_adapter.dev,
				   adapter_nums);

	return ret;
}

static int vidtv_bridge_dmx_init(struct vidtv_dvb *dvb)
{
	dvb->demux.dmx.capabilities = DMX_TS_FILTERING |
				      DMX_SECTION_FILTERING;

	dvb->demux.priv       = dvb;
	dvb->demux.filternum  = 256;
	dvb->demux.feednum    = 256;
	dvb->demux.start_feed = vidtv_start_feed;
	dvb->demux.stop_feed  = vidtv_stop_feed;

	return dvb_dmx_init(&dvb->demux);
}

static int vidtv_bridge_dmxdev_init(struct vidtv_dvb *dvb)
{
	dvb->dmx_dev.filternum    = 256;
	dvb->dmx_dev.demux        = &dvb->demux.dmx;
	dvb->dmx_dev.capabilities = 0;

	return dvb_dmxdev_init(&dvb->dmx_dev, &dvb->adapter);
}

static int vidtv_bridge_probe_demod(struct vidtv_dvb *dvb, u32 n)
{
	struct vidtv_demod_config cfg = {
		.drop_tslock_prob_on_low_snr     = drop_tslock_prob_on_low_snr,
		.recover_tslock_prob_on_good_snr = recover_tslock_prob_on_good_snr,
	};
	dvb->i2c_client_demod[n] = dvb_module_probe("dvb_vidtv_demod",
						    NULL,
						    &dvb->i2c_adapter,
						    DEMOD_DEFAULT_ADDR,
						    &cfg);

	/* driver will not work anyways so bail out */
	if (!dvb->i2c_client_demod[n])
		return -ENODEV;

	/* retrieve a ptr to the frontend state */
	dvb->fe[n] = vidtv_get_frontend_ptr(dvb->i2c_client_demod[n]);

	return 0;
}

static int vidtv_bridge_probe_tuner(struct vidtv_dvb *dvb, u32 n)
{
	struct vidtv_tuner_config cfg = {
		.fe                       = dvb->fe[n],
		.mock_power_up_delay_msec = mock_power_up_delay_msec,
		.mock_tune_delay_msec     = mock_tune_delay_msec,
	};
	u32 freq;
	int i;

	/* TODO: check if the frequencies are at a valid range */

	memcpy(cfg.vidtv_valid_dvb_t_freqs,
	       vidtv_valid_dvb_t_freqs,
	       sizeof(vidtv_valid_dvb_t_freqs));

	memcpy(cfg.vidtv_valid_dvb_c_freqs,
	       vidtv_valid_dvb_c_freqs,
	       sizeof(vidtv_valid_dvb_c_freqs));

	/*
	 * Convert Satellite frequencies from Ku-band in kHZ into S-band
	 * frequencies in Hz.
	 */
	for (i = 0; i < ARRAY_SIZE(vidtv_valid_dvb_s_freqs); i++) {
		freq = vidtv_valid_dvb_s_freqs[i];
		if (freq) {
			if (freq < LNB_CUT_FREQUENCY)
				freq = abs(freq - LNB_LOW_FREQ);
			else
				freq = abs(freq - LNB_HIGH_FREQ);
		}
		cfg.vidtv_valid_dvb_s_freqs[i] = freq;
	}

	cfg.max_frequency_shift_hz = max_frequency_shift_hz;

	dvb->i2c_client_tuner[n] = dvb_module_probe("dvb_vidtv_tuner",
						    NULL,
						    &dvb->i2c_adapter,
						    TUNER_DEFAULT_ADDR,
						    &cfg);

	return (dvb->i2c_client_tuner[n]) ? 0 : -ENODEV;
}

static int vidtv_bridge_dvb_init(struct vidtv_dvb *dvb)
{
	int ret, i, j;

	ret = vidtv_bridge_i2c_register_adap(dvb);
	if (ret < 0)
		goto fail_i2c;

	ret = vidtv_bridge_register_adap(dvb);
	if (ret < 0)
		goto fail_adapter;
	dvb_register_media_controller(&dvb->adapter, &dvb->mdev);

	for (i = 0; i < NUM_FE; ++i) {
		ret = vidtv_bridge_probe_demod(dvb, i);
		if (ret < 0)
			goto fail_demod_probe;

		ret = vidtv_bridge_probe_tuner(dvb, i);
		if (ret < 0)
			goto fail_tuner_probe;

		ret = dvb_register_frontend(&dvb->adapter, dvb->fe[i]);
		if (ret < 0)
			goto fail_fe;
	}

	ret = vidtv_bridge_dmx_init(dvb);
	if (ret < 0)
		goto fail_dmx;

	ret = vidtv_bridge_dmxdev_init(dvb);
	if (ret < 0)
		goto fail_dmx_dev;

	for (j = 0; j < NUM_FE; ++j) {
		ret = dvb->demux.dmx.connect_frontend(&dvb->demux.dmx,
						      &dvb->dmx_fe[j]);
		if (ret < 0)
			goto fail_dmx_conn;

		/*
		 * The source of the demux is a frontend connected
		 * to the demux.
		 */
		dvb->dmx_fe[j].source = DMX_FRONTEND_0;
	}

	return ret;

fail_dmx_conn:
	for (j = j - 1; j >= 0; --j)
		dvb->demux.dmx.remove_frontend(&dvb->demux.dmx,
					       &dvb->dmx_fe[j]);
fail_dmx_dev:
	dvb_dmxdev_release(&dvb->dmx_dev);
fail_dmx:
	dvb_dmx_release(&dvb->demux);
fail_fe:
	for (j = i; j >= 0; --j)
		dvb_unregister_frontend(dvb->fe[j]);
fail_tuner_probe:
	for (j = i; j >= 0; --j)
		if (dvb->i2c_client_tuner[j])
			dvb_module_release(dvb->i2c_client_tuner[j]);

fail_demod_probe:
	for (j = i; j >= 0; --j)
		if (dvb->i2c_client_demod[j])
			dvb_module_release(dvb->i2c_client_demod[j]);

fail_adapter:
	dvb_unregister_adapter(&dvb->adapter);

fail_i2c:
	i2c_del_adapter(&dvb->i2c_adapter);

	return ret;
}

static int vidtv_bridge_probe(struct platform_device *pdev)
{
	struct vidtv_dvb *dvb;
	int ret;

	dvb = kzalloc(sizeof(*dvb), GFP_KERNEL);
	if (!dvb)
		return -ENOMEM;

	dvb->pdev = pdev;

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	dvb->mdev.dev = &pdev->dev;

	strscpy(dvb->mdev.model, "vidtv", sizeof(dvb->mdev.model));
	strscpy(dvb->mdev.bus_info, "platform:vidtv", sizeof(dvb->mdev.bus_info));

	media_device_init(&dvb->mdev);
#endif

	ret = vidtv_bridge_dvb_init(dvb);
	if (ret < 0)
		goto err_dvb;

	mutex_init(&dvb->feed_lock);

	platform_set_drvdata(pdev, dvb);

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	ret = media_device_register(&dvb->mdev);
	if (ret) {
		dev_err(dvb->mdev.dev,
			"media device register failed (err=%d)\n", ret);
		goto err_media_device_register;
	}
#endif /* CONFIG_MEDIA_CONTROLLER_DVB */

	dev_info(&pdev->dev, "Successfully initialized vidtv!\n");
	return ret;

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
err_media_device_register:
	media_device_cleanup(&dvb->mdev);
#endif /* CONFIG_MEDIA_CONTROLLER_DVB */
err_dvb:
	kfree(dvb);
	return ret;
}

static int vidtv_bridge_remove(struct platform_device *pdev)
{
	struct vidtv_dvb *dvb;
	u32 i;

	dvb = platform_get_drvdata(pdev);

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	media_device_unregister(&dvb->mdev);
	media_device_cleanup(&dvb->mdev);
#endif /* CONFIG_MEDIA_CONTROLLER_DVB */

	mutex_destroy(&dvb->feed_lock);

	for (i = 0; i < NUM_FE; ++i) {
		dvb_unregister_frontend(dvb->fe[i]);
		dvb_module_release(dvb->i2c_client_tuner[i]);
		dvb_module_release(dvb->i2c_client_demod[i]);
	}

	dvb_dmxdev_release(&dvb->dmx_dev);
	dvb_dmx_release(&dvb->demux);
	dvb_unregister_adapter(&dvb->adapter);
	dev_info(&pdev->dev, "Successfully removed vidtv\n");

	return 0;
}

static void vidtv_bridge_dev_release(struct device *dev)
{
	struct vidtv_dvb *dvb;

	dvb = dev_get_drvdata(dev);
	kfree(dvb);
}

static struct platform_device vidtv_bridge_dev = {
	.name		= VIDTV_PDEV_NAME,
	.dev.release	= vidtv_bridge_dev_release,
};

static struct platform_driver vidtv_bridge_driver = {
	.driver = {
		.name = VIDTV_PDEV_NAME,
	},
	.probe    = vidtv_bridge_probe,
	.remove   = vidtv_bridge_remove,
};

static void __exit vidtv_bridge_exit(void)
{
	platform_driver_unregister(&vidtv_bridge_driver);
	platform_device_unregister(&vidtv_bridge_dev);
}

static int __init vidtv_bridge_init(void)
{
	int ret;

	ret = platform_device_register(&vidtv_bridge_dev);
	if (ret)
		return ret;

	ret = platform_driver_register(&vidtv_bridge_driver);
	if (ret)
		platform_device_unregister(&vidtv_bridge_dev);

	return ret;
}

module_init(vidtv_bridge_init);
module_exit(vidtv_bridge_exit);

MODULE_DESCRIPTION("Virtual Digital TV Test Driver");
MODULE_AUTHOR("Daniel W. S. Almeida");
MODULE_LICENSE("GPL");
MODULE_ALIAS("vidtv");
MODULE_ALIAS("dvb_vidtv");
