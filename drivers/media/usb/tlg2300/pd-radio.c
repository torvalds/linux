#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <media/v4l2-dev.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <linux/sched.h>

#include "pd-common.h"
#include "vendorcmds.h"

static int set_frequency(struct poseidon *p, __u32 frequency);
static int poseidon_fm_close(struct file *filp);
static int poseidon_fm_open(struct file *filp);

#define TUNER_FREQ_MIN_FM 76000000U
#define TUNER_FREQ_MAX_FM 108000000U

#define MAX_PREEMPHASIS (V4L2_PREEMPHASIS_75_uS + 1)
static int preemphasis[MAX_PREEMPHASIS] = {
	TLG_TUNE_ASTD_NONE,   /* V4L2_PREEMPHASIS_DISABLED */
	TLG_TUNE_ASTD_FM_EUR, /* V4L2_PREEMPHASIS_50_uS    */
	TLG_TUNE_ASTD_FM_US,  /* V4L2_PREEMPHASIS_75_uS    */
};

static int poseidon_check_mode_radio(struct poseidon *p)
{
	int ret;
	u32 status;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ/2);
	ret = usb_set_interface(p->udev, 0, BULK_ALTERNATE_IFACE);
	if (ret < 0)
		goto out;

	ret = set_tuner_mode(p, TLG_MODE_FM_RADIO);
	if (ret != 0)
		goto out;

	ret = send_set_req(p, SGNL_SRC_SEL, TLG_SIG_SRC_ANTENNA, &status);
	ret = send_set_req(p, TUNER_AUD_ANA_STD,
				p->radio_data.pre_emphasis, &status);
	ret |= send_set_req(p, TUNER_AUD_MODE,
				TLG_TUNE_TVAUDIO_MODE_STEREO, &status);
	ret |= send_set_req(p, AUDIO_SAMPLE_RATE_SEL,
				ATV_AUDIO_RATE_48K, &status);
	ret |= send_set_req(p, TUNE_FREQ_SELECT, TUNER_FREQ_MIN_FM, &status);
out:
	return ret;
}

#ifdef CONFIG_PM
static int pm_fm_suspend(struct poseidon *p)
{
	logpm(p);
	pm_alsa_suspend(p);
	usb_set_interface(p->udev, 0, 0);
	msleep(300);
	return 0;
}

static int pm_fm_resume(struct poseidon *p)
{
	logpm(p);
	poseidon_check_mode_radio(p);
	set_frequency(p, p->radio_data.fm_freq);
	pm_alsa_resume(p);
	return 0;
}
#endif

static int poseidon_fm_open(struct file *filp)
{
	struct poseidon *p = video_drvdata(filp);
	int ret = 0;

	mutex_lock(&p->lock);
	if (p->state & POSEIDON_STATE_DISCONNECT) {
		ret = -ENODEV;
		goto out;
	}

	if (p->state && !(p->state & POSEIDON_STATE_FM)) {
		ret = -EBUSY;
		goto out;
	}
	ret = v4l2_fh_open(filp);
	if (ret)
		goto out;

	usb_autopm_get_interface(p->interface);
	if (0 == p->state) {
		struct video_device *vfd = &p->radio_data.fm_dev;

		/* default pre-emphasis */
		if (p->radio_data.pre_emphasis == 0)
			p->radio_data.pre_emphasis = TLG_TUNE_ASTD_FM_EUR;
		set_debug_mode(vfd, debug_mode);

		ret = poseidon_check_mode_radio(p);
		if (ret < 0) {
			usb_autopm_put_interface(p->interface);
			goto out;
		}
		p->state |= POSEIDON_STATE_FM;
	}
	kref_get(&p->kref);
out:
	mutex_unlock(&p->lock);
	return ret;
}

static int poseidon_fm_close(struct file *filp)
{
	struct poseidon *p = video_drvdata(filp);
	struct radio_data *fm = &p->radio_data;
	uint32_t status;

	mutex_lock(&p->lock);
	if (v4l2_fh_is_singular_file(filp))
		p->state &= ~POSEIDON_STATE_FM;

	if (fm->is_radio_streaming && filp == p->file_for_stream) {
		fm->is_radio_streaming = 0;
		send_set_req(p, PLAY_SERVICE, TLG_TUNE_PLAY_SVC_STOP, &status);
	}
	usb_autopm_put_interface(p->interface);
	mutex_unlock(&p->lock);

	kref_put(&p->kref, poseidon_delete);
	return v4l2_fh_release(filp);
}

static int vidioc_querycap(struct file *file, void *priv,
			struct v4l2_capability *v)
{
	struct poseidon *p = video_drvdata(file);

	strlcpy(v->driver, "tele-radio", sizeof(v->driver));
	strlcpy(v->card, "Telegent Poseidon", sizeof(v->card));
	usb_make_path(p->udev, v->bus_info, sizeof(v->bus_info));
	v->device_caps = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	/* Report all capabilities of the USB device */
	v->capabilities = v->device_caps | V4L2_CAP_DEVICE_CAPS |
			V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VBI_CAPTURE |
			V4L2_CAP_AUDIO | V4L2_CAP_STREAMING |
			V4L2_CAP_READWRITE;
	return 0;
}

static const struct v4l2_file_operations poseidon_fm_fops = {
	.owner         = THIS_MODULE,
	.open          = poseidon_fm_open,
	.release       = poseidon_fm_close,
	.poll		= v4l2_ctrl_poll,
	.unlocked_ioctl = video_ioctl2,
};

static int tlg_fm_vidioc_g_tuner(struct file *file, void *priv,
				 struct v4l2_tuner *vt)
{
	struct poseidon *p = video_drvdata(file);
	struct tuner_fm_sig_stat_s fm_stat = {};
	int ret, status, count = 5;

	if (vt->index != 0)
		return -EINVAL;

	vt->type	= V4L2_TUNER_RADIO;
	vt->capability	= V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LOW;
	vt->rangelow	= TUNER_FREQ_MIN_FM * 2 / 125;
	vt->rangehigh	= TUNER_FREQ_MAX_FM * 2 / 125;
	vt->rxsubchans	= V4L2_TUNER_SUB_STEREO;
	vt->audmode	= V4L2_TUNER_MODE_STEREO;
	vt->signal	= 0;
	vt->afc 	= 0;
	strlcpy(vt->name, "Radio", sizeof(vt->name));

	mutex_lock(&p->lock);
	ret = send_get_req(p, TUNER_STATUS, TLG_MODE_FM_RADIO,
			      &fm_stat, &status, sizeof(fm_stat));

	while (fm_stat.sig_lock_busy && count-- && !ret) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);

		ret = send_get_req(p, TUNER_STATUS, TLG_MODE_FM_RADIO,
				  &fm_stat, &status, sizeof(fm_stat));
	}
	mutex_unlock(&p->lock);

	if (ret || status) {
		vt->signal = 0;
	} else if ((fm_stat.sig_present || fm_stat.sig_locked)
			&& fm_stat.sig_strength == 0) {
		vt->signal = 0xffff;
	} else
		vt->signal = (fm_stat.sig_strength * 255 / 10) << 8;

	return 0;
}

static int fm_get_freq(struct file *file, void *priv,
		       struct v4l2_frequency *argp)
{
	struct poseidon *p = video_drvdata(file);

	if (argp->tuner)
		return -EINVAL;
	argp->frequency = p->radio_data.fm_freq;
	return 0;
}

static int set_frequency(struct poseidon *p, __u32 frequency)
{
	__u32 freq ;
	int ret, status;

	mutex_lock(&p->lock);

	ret = send_set_req(p, TUNER_AUD_ANA_STD,
				p->radio_data.pre_emphasis, &status);

	freq = (frequency * 125) / 2; /* Hz */
	freq = clamp(freq, TUNER_FREQ_MIN_FM, TUNER_FREQ_MAX_FM);

	ret = send_set_req(p, TUNE_FREQ_SELECT, freq, &status);
	if (ret < 0)
		goto error ;
	ret = send_set_req(p, TAKE_REQUEST, 0, &status);

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ/4);
	if (!p->radio_data.is_radio_streaming) {
		ret = send_set_req(p, TAKE_REQUEST, 0, &status);
		ret = send_set_req(p, PLAY_SERVICE,
				TLG_TUNE_PLAY_SVC_START, &status);
		p->radio_data.is_radio_streaming = 1;
	}
	p->radio_data.fm_freq = freq * 2 / 125;
error:
	mutex_unlock(&p->lock);
	return ret;
}

static int fm_set_freq(struct file *file, void *priv,
		       const struct v4l2_frequency *argp)
{
	struct poseidon *p = video_drvdata(file);

	if (argp->tuner)
		return -EINVAL;
	p->file_for_stream = file;
#ifdef CONFIG_PM
	p->pm_suspend = pm_fm_suspend;
	p->pm_resume  = pm_fm_resume;
#endif
	return set_frequency(p, argp->frequency);
}

static int tlg_fm_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct poseidon *p = container_of(ctrl->handler, struct poseidon,
						radio_data.ctrl_handler);
	int pre_emphasis;
	u32 status;

	switch (ctrl->id) {
	case V4L2_CID_TUNE_PREEMPHASIS:
		pre_emphasis = preemphasis[ctrl->val];
		send_set_req(p, TUNER_AUD_ANA_STD, pre_emphasis, &status);
		p->radio_data.pre_emphasis = pre_emphasis;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_tuner(struct file *file, void *priv, const struct v4l2_tuner *vt)
{
	return vt->index > 0 ? -EINVAL : 0;
}

static const struct v4l2_ctrl_ops tlg_fm_ctrl_ops = {
	.s_ctrl = tlg_fm_s_ctrl,
};

static const struct v4l2_ioctl_ops poseidon_fm_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_tuner     = tlg_fm_vidioc_g_tuner,
	.vidioc_g_frequency = fm_get_freq,
	.vidioc_s_frequency = fm_set_freq,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device poseidon_fm_template = {
	.name       = "Telegent-Radio",
	.fops       = &poseidon_fm_fops,
	.minor      = -1,
	.release    = video_device_release_empty,
	.ioctl_ops  = &poseidon_fm_ioctl_ops,
};

int poseidon_fm_init(struct poseidon *p)
{
	struct video_device *vfd = &p->radio_data.fm_dev;
	struct v4l2_ctrl_handler *hdl = &p->radio_data.ctrl_handler;

	*vfd = poseidon_fm_template;

	set_frequency(p, TUNER_FREQ_MIN_FM);
	v4l2_ctrl_handler_init(hdl, 1);
	v4l2_ctrl_new_std_menu(hdl, &tlg_fm_ctrl_ops, V4L2_CID_TUNE_PREEMPHASIS,
			V4L2_PREEMPHASIS_75_uS, 0, V4L2_PREEMPHASIS_50_uS);
	if (hdl->error) {
		v4l2_ctrl_handler_free(hdl);
		return hdl->error;
	}
	vfd->v4l2_dev = &p->v4l2_dev;
	vfd->ctrl_handler = hdl;
	video_set_drvdata(vfd, p);
	return video_register_device(vfd, VFL_TYPE_RADIO, -1);
}

int poseidon_fm_exit(struct poseidon *p)
{
	video_unregister_device(&p->radio_data.fm_dev);
	v4l2_ctrl_handler_free(&p->radio_data.ctrl_handler);
	return 0;
}
