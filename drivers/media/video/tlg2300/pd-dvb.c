#include "pd-common.h"
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/dvb/dmx.h>
#include <linux/delay.h>
#include <linux/gfp.h>

#include "vendorcmds.h"
#include <linux/sched.h>
#include <linux/atomic.h>

static void dvb_urb_cleanup(struct pd_dvb_adapter *pd_dvb);

static int dvb_bandwidth[][2] = {
	{ TLG_BW_8, 8000000 },
	{ TLG_BW_7, 7000000 },
	{ TLG_BW_6, 6000000 }
};
static int dvb_bandwidth_length = ARRAY_SIZE(dvb_bandwidth);

static s32 dvb_start_streaming(struct pd_dvb_adapter *pd_dvb);
static int poseidon_check_mode_dvbt(struct poseidon *pd)
{
	s32 ret = 0, cmd_status = 0;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ/4);

	ret = usb_set_interface(pd->udev, 0, BULK_ALTERNATE_IFACE);
	if (ret != 0)
		return ret;

	ret = set_tuner_mode(pd, TLG_MODE_CAPS_DVB_T);
	if (ret)
		return ret;

	/* signal source */
	ret = send_set_req(pd, SGNL_SRC_SEL, TLG_SIG_SRC_ANTENNA, &cmd_status);
	if (ret|cmd_status)
		return ret;

	return 0;
}

/* acquire :
 * 	1 == open
 * 	0 == release
 */
static int poseidon_ts_bus_ctrl(struct dvb_frontend *fe, int acquire)
{
	struct poseidon *pd = fe->demodulator_priv;
	struct pd_dvb_adapter *pd_dvb;
	int ret = 0;

	if (!pd)
		return -ENODEV;

	pd_dvb = container_of(fe, struct pd_dvb_adapter, dvb_fe);
	if (acquire) {
		mutex_lock(&pd->lock);
		if (pd->state & POSEIDON_STATE_DISCONNECT) {
			ret = -ENODEV;
			goto open_out;
		}

		if (pd->state && !(pd->state & POSEIDON_STATE_DVBT)) {
			ret = -EBUSY;
			goto open_out;
		}

		usb_autopm_get_interface(pd->interface);
		if (0 == pd->state) {
			ret = poseidon_check_mode_dvbt(pd);
			if (ret < 0) {
				usb_autopm_put_interface(pd->interface);
				goto open_out;
			}
			pd->state |= POSEIDON_STATE_DVBT;
			pd_dvb->bandwidth = 0;
			pd_dvb->prev_freq = 0;
		}
		atomic_inc(&pd_dvb->users);
		kref_get(&pd->kref);
open_out:
		mutex_unlock(&pd->lock);
	} else {
		dvb_stop_streaming(pd_dvb);

		if (atomic_dec_and_test(&pd_dvb->users)) {
			mutex_lock(&pd->lock);
			pd->state &= ~POSEIDON_STATE_DVBT;
			mutex_unlock(&pd->lock);
		}
		kref_put(&pd->kref, poseidon_delete);
		usb_autopm_put_interface(pd->interface);
	}
	return ret;
}

#ifdef CONFIG_PM
static void poseidon_fe_release(struct dvb_frontend *fe)
{
	struct poseidon *pd = fe->demodulator_priv;

	pd->pm_suspend = NULL;
	pd->pm_resume  = NULL;
}
#else
#define poseidon_fe_release NULL
#endif

static s32 poseidon_fe_sleep(struct dvb_frontend *fe)
{
	return 0;
}

/*
 * return true if we can satisfy the conditions, else return false.
 */
static bool check_scan_ok(__u32 freq, int bandwidth,
			struct pd_dvb_adapter *adapter)
{
	if (bandwidth < 0)
		return false;

	if (adapter->prev_freq == freq
		&& adapter->bandwidth == bandwidth) {
		long nl = jiffies - adapter->last_jiffies;
		unsigned int msec ;

		msec = jiffies_to_msecs(abs(nl));
		return msec > 15000 ? true : false;
	}
	return true;
}

/*
 * Check if the firmware delays too long for an invalid frequency.
 */
static int fw_delay_overflow(struct pd_dvb_adapter *adapter)
{
	long nl = jiffies - adapter->last_jiffies;
	unsigned int msec ;

	msec = jiffies_to_msecs(abs(nl));
	return msec > 800 ? true : false;
}

static int poseidon_set_fe(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *fep = &fe->dtv_property_cache;
	s32 ret = 0, cmd_status = 0;
	s32 i, bandwidth = -1;
	struct poseidon *pd = fe->demodulator_priv;
	struct pd_dvb_adapter *pd_dvb = &pd->dvb_data;

	if (in_hibernation(pd))
		return -EBUSY;

	mutex_lock(&pd->lock);
	for (i = 0; i < dvb_bandwidth_length; i++)
		if (fep->bandwidth_hz == dvb_bandwidth[i][1])
			bandwidth = dvb_bandwidth[i][0];

	if (check_scan_ok(fep->frequency, bandwidth, pd_dvb)) {
		ret = send_set_req(pd, TUNE_FREQ_SELECT,
					fep->frequency / 1000, &cmd_status);
		if (ret | cmd_status) {
			log("error line");
			goto front_out;
		}

		ret = send_set_req(pd, DVBT_BANDW_SEL,
						bandwidth, &cmd_status);
		if (ret | cmd_status) {
			log("error line");
			goto front_out;
		}

		ret = send_set_req(pd, TAKE_REQUEST, 0, &cmd_status);
		if (ret | cmd_status) {
			log("error line");
			goto front_out;
		}

		/* save the context for future */
		memcpy(&pd_dvb->fe_param, fep, sizeof(*fep));
		pd_dvb->bandwidth = bandwidth;
		pd_dvb->prev_freq = fep->frequency;
		pd_dvb->last_jiffies = jiffies;
	}
front_out:
	mutex_unlock(&pd->lock);
	return ret;
}

#ifdef CONFIG_PM
static int pm_dvb_suspend(struct poseidon *pd)
{
	struct pd_dvb_adapter *pd_dvb = &pd->dvb_data;
	dvb_stop_streaming(pd_dvb);
	dvb_urb_cleanup(pd_dvb);
	msleep(500);
	return 0;
}

static int pm_dvb_resume(struct poseidon *pd)
{
	struct pd_dvb_adapter *pd_dvb = &pd->dvb_data;

	poseidon_check_mode_dvbt(pd);
	msleep(300);
	poseidon_set_fe(&pd_dvb->dvb_fe);

	dvb_start_streaming(pd_dvb);
	return 0;
}
#endif

static s32 poseidon_fe_init(struct dvb_frontend *fe)
{
	struct poseidon *pd = fe->demodulator_priv;
	struct pd_dvb_adapter *pd_dvb = &pd->dvb_data;

#ifdef CONFIG_PM
	pd->pm_suspend = pm_dvb_suspend;
	pd->pm_resume  = pm_dvb_resume;
#endif
	memset(&pd_dvb->fe_param, 0,
			sizeof(struct dtv_frontend_properties));
	return 0;
}

static int poseidon_get_fe(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *fep = &fe->dtv_property_cache;
	struct poseidon *pd = fe->demodulator_priv;
	struct pd_dvb_adapter *pd_dvb = &pd->dvb_data;

	memcpy(fep, &pd_dvb->fe_param, sizeof(*fep));
	return 0;
}

static int poseidon_fe_get_tune_settings(struct dvb_frontend *fe,
				struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static int poseidon_read_status(struct dvb_frontend *fe, fe_status_t *stat)
{
	struct poseidon *pd = fe->demodulator_priv;
	s32 ret = -1, cmd_status;
	struct tuner_dtv_sig_stat_s status = {};

	if (in_hibernation(pd))
		return -EBUSY;
	mutex_lock(&pd->lock);

	ret = send_get_req(pd, TUNER_STATUS, TLG_MODE_DVB_T,
				&status, &cmd_status, sizeof(status));
	if (ret | cmd_status) {
		log("get tuner status error");
		goto out;
	}

	if (debug_mode)
		log("P : %d, L %d, LB :%d", status.sig_present,
			status.sig_locked, status.sig_lock_busy);

	if (status.sig_lock_busy) {
		goto out;
	} else if (status.sig_present || status.sig_locked) {
		*stat |= FE_HAS_LOCK | FE_HAS_SIGNAL | FE_HAS_CARRIER
				| FE_HAS_SYNC | FE_HAS_VITERBI;
	} else {
		if (fw_delay_overflow(&pd->dvb_data))
			*stat |= FE_TIMEDOUT;
	}
out:
	mutex_unlock(&pd->lock);
	return ret;
}

static int poseidon_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct poseidon *pd = fe->demodulator_priv;
	struct tuner_ber_rate_s tlg_ber = {};
	s32 ret = -1, cmd_status;

	mutex_lock(&pd->lock);
	ret = send_get_req(pd, TUNER_BER_RATE, 0,
				&tlg_ber, &cmd_status, sizeof(tlg_ber));
	if (ret | cmd_status)
		goto out;
	*ber = tlg_ber.ber_rate;
out:
	mutex_unlock(&pd->lock);
	return ret;
}

static s32 poseidon_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct poseidon *pd = fe->demodulator_priv;
	struct tuner_dtv_sig_stat_s status = {};
	s32 ret = 0, cmd_status;

	mutex_lock(&pd->lock);
	ret = send_get_req(pd, TUNER_STATUS, TLG_MODE_DVB_T,
				&status, &cmd_status, sizeof(status));
	if (ret | cmd_status)
		goto out;
	if ((status.sig_present || status.sig_locked) && !status.sig_strength)
		*strength = 0xFFFF;
	else
		*strength = status.sig_strength;
out:
	mutex_unlock(&pd->lock);
	return ret;
}

static int poseidon_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	return 0;
}

static int poseidon_read_unc_blocks(struct dvb_frontend *fe, u32 *unc)
{
	*unc = 0;
	return 0;
}

static struct dvb_frontend_ops poseidon_frontend_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name		= "Poseidon DVB-T",
		.frequency_min	= 174000000,
		.frequency_max  = 862000000,
		.frequency_stepsize	  = 62500,/* FIXME */
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_HIERARCHY_AUTO,
	},

	.release = poseidon_fe_release,

	.init = poseidon_fe_init,
	.sleep = poseidon_fe_sleep,

	.set_frontend = poseidon_set_fe,
	.get_frontend = poseidon_get_fe,
	.get_tune_settings = poseidon_fe_get_tune_settings,

	.read_status	= poseidon_read_status,
	.read_ber	= poseidon_read_ber,
	.read_signal_strength = poseidon_read_signal_strength,
	.read_snr	= poseidon_read_snr,
	.read_ucblocks	= poseidon_read_unc_blocks,

	.ts_bus_ctrl = poseidon_ts_bus_ctrl,
};

static void dvb_urb_irq(struct urb *urb)
{
	struct pd_dvb_adapter *pd_dvb = urb->context;
	int len = urb->transfer_buffer_length;
	struct dvb_demux *demux = &pd_dvb->demux;
	s32 ret;

	if (!pd_dvb->is_streaming || urb->status) {
		if (urb->status == -EPROTO)
			goto resend;
		return;
	}

	if (urb->actual_length == len)
		dvb_dmx_swfilter(demux, urb->transfer_buffer, len);
	else if (urb->actual_length == len - 4) {
		int offset;
		u8 *buf = urb->transfer_buffer;

		/*
		 * The packet size is 512,
		 * last packet contains 456 bytes tsp data
		 */
		for (offset = 456; offset < len; offset += 512) {
			if (!strncmp(buf + offset, "DVHS", 4)) {
				dvb_dmx_swfilter(demux, buf, offset);
				if (len > offset + 52 + 4) {
					/*16 bytes trailer + 36 bytes padding */
					buf += offset + 52;
					len -= offset + 52 + 4;
					dvb_dmx_swfilter(demux, buf, len);
				}
				break;
			}
		}
	}

resend:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret)
		log(" usb_submit_urb failed: error %d", ret);
}

static int dvb_urb_init(struct pd_dvb_adapter *pd_dvb)
{
	if (pd_dvb->urb_array[0])
		return 0;

	alloc_bulk_urbs_generic(pd_dvb->urb_array, DVB_SBUF_NUM,
			pd_dvb->pd_device->udev, pd_dvb->ep_addr,
			DVB_URB_BUF_SIZE, GFP_KERNEL,
			dvb_urb_irq, pd_dvb);
	return 0;
}

static void dvb_urb_cleanup(struct pd_dvb_adapter *pd_dvb)
{
	free_all_urb_generic(pd_dvb->urb_array, DVB_SBUF_NUM);
}

static s32 dvb_start_streaming(struct pd_dvb_adapter *pd_dvb)
{
	struct poseidon *pd = pd_dvb->pd_device;
	int ret = 0;

	if (pd->state & POSEIDON_STATE_DISCONNECT)
		return -ENODEV;

	mutex_lock(&pd->lock);
	if (!pd_dvb->is_streaming) {
		s32 i, cmd_status = 0;
		/*
		 * Once upon a time, there was a difficult bug lying here.
		 * ret = send_set_req(pd, TAKE_REQUEST, 0, &cmd_status);
		 */

		ret = send_set_req(pd, PLAY_SERVICE, 1, &cmd_status);
		if (ret | cmd_status)
			goto out;

		ret = dvb_urb_init(pd_dvb);
		if (ret < 0)
			goto out;

		pd_dvb->is_streaming = 1;
		for (i = 0; i < DVB_SBUF_NUM; i++) {
			ret = usb_submit_urb(pd_dvb->urb_array[i],
						       GFP_KERNEL);
			if (ret) {
				log(" submit urb error %d", ret);
				goto out;
			}
		}
	}
out:
	mutex_unlock(&pd->lock);
	return ret;
}

void dvb_stop_streaming(struct pd_dvb_adapter *pd_dvb)
{
	struct poseidon *pd = pd_dvb->pd_device;

	mutex_lock(&pd->lock);
	if (pd_dvb->is_streaming) {
		s32 i, ret, cmd_status = 0;

		pd_dvb->is_streaming = 0;

		for (i = 0; i < DVB_SBUF_NUM; i++)
			if (pd_dvb->urb_array[i])
				usb_kill_urb(pd_dvb->urb_array[i]);

		ret = send_set_req(pd, PLAY_SERVICE, TLG_TUNE_PLAY_SVC_STOP,
					&cmd_status);
		if (ret | cmd_status)
			log("error");
	}
	mutex_unlock(&pd->lock);
}

static int pd_start_feed(struct dvb_demux_feed *feed)
{
	struct pd_dvb_adapter *pd_dvb = feed->demux->priv;
	int ret = 0;

	if (!pd_dvb)
		return -1;
	if (atomic_inc_return(&pd_dvb->active_feed) == 1)
		ret = dvb_start_streaming(pd_dvb);
	return ret;
}

static int pd_stop_feed(struct dvb_demux_feed *feed)
{
	struct pd_dvb_adapter *pd_dvb = feed->demux->priv;

	if (!pd_dvb)
		return -1;
	if (atomic_dec_and_test(&pd_dvb->active_feed))
		dvb_stop_streaming(pd_dvb);
	return 0;
}

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);
int pd_dvb_usb_device_init(struct poseidon *pd)
{
	struct pd_dvb_adapter *pd_dvb = &pd->dvb_data;
	struct dvb_demux *dvbdemux;
	int ret = 0;

	pd_dvb->ep_addr = 0x82;
	atomic_set(&pd_dvb->users, 0);
	atomic_set(&pd_dvb->active_feed, 0);
	pd_dvb->pd_device = pd;

	ret = dvb_register_adapter(&pd_dvb->dvb_adap,
				"Poseidon dvbt adapter",
				THIS_MODULE,
				NULL /* for hibernation correctly*/,
				adapter_nr);
	if (ret < 0)
		goto error1;

	/* register frontend */
	pd_dvb->dvb_fe.demodulator_priv = pd;
	memcpy(&pd_dvb->dvb_fe.ops, &poseidon_frontend_ops,
			sizeof(struct dvb_frontend_ops));
	ret = dvb_register_frontend(&pd_dvb->dvb_adap, &pd_dvb->dvb_fe);
	if (ret < 0)
		goto error2;

	/* register demux device */
	dvbdemux = &pd_dvb->demux;
	dvbdemux->dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	dvbdemux->priv = pd_dvb;
	dvbdemux->feednum = dvbdemux->filternum = 64;
	dvbdemux->start_feed = pd_start_feed;
	dvbdemux->stop_feed = pd_stop_feed;
	dvbdemux->write_to_decoder = NULL;

	ret = dvb_dmx_init(dvbdemux);
	if (ret < 0)
		goto error3;

	pd_dvb->dmxdev.filternum = pd_dvb->demux.filternum;
	pd_dvb->dmxdev.demux = &pd_dvb->demux.dmx;
	pd_dvb->dmxdev.capabilities = 0;

	ret = dvb_dmxdev_init(&pd_dvb->dmxdev, &pd_dvb->dvb_adap);
	if (ret < 0)
		goto error3;
	return 0;

error3:
	dvb_unregister_frontend(&pd_dvb->dvb_fe);
error2:
	dvb_unregister_adapter(&pd_dvb->dvb_adap);
error1:
	return ret;
}

void pd_dvb_usb_device_exit(struct poseidon *pd)
{
	struct pd_dvb_adapter *pd_dvb = &pd->dvb_data;

	while (atomic_read(&pd_dvb->users) != 0
		|| atomic_read(&pd_dvb->active_feed) != 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}
	dvb_dmxdev_release(&pd_dvb->dmxdev);
	dvb_unregister_frontend(&pd_dvb->dvb_fe);
	dvb_unregister_adapter(&pd_dvb->dvb_adap);
	pd_dvb_usb_device_cleanup(pd);
}

void pd_dvb_usb_device_cleanup(struct poseidon *pd)
{
	struct pd_dvb_adapter *pd_dvb = &pd->dvb_data;

	dvb_urb_cleanup(pd_dvb);
}

int pd_dvb_get_adapter_num(struct pd_dvb_adapter *pd_dvb)
{
	return pd_dvb->dvb_adap.num;
}
