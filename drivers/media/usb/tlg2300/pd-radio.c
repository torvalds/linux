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
#include <linux/sched.h>

#include "pd-common.h"
#include "vendorcmds.h"

static int set_frequency(struct poseidon *p, __u32 frequency);
static int poseidon_fm_close(struct file *filp);
static int poseidon_fm_open(struct file *filp);

#define TUNER_FREQ_MIN_FM 76000000
#define TUNER_FREQ_MAX_FM 108000000

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
	struct video_device *vfd = video_devdata(filp);
	struct poseidon *p = video_get_drvdata(vfd);
	int ret = 0;

	if (!p)
		return -1;

	mutex_lock(&p->lock);
	if (p->state & POSEIDON_STATE_DISCONNECT) {
		ret = -ENODEV;
		goto out;
	}

	if (p->state && !(p->state & POSEIDON_STATE_FM)) {
		ret = -EBUSY;
		goto out;
	}

	usb_autopm_get_interface(p->interface);
	if (0 == p->state) {
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
	p->radio_data.users++;
	kref_get(&p->kref);
	filp->private_data = p;
out:
	mutex_unlock(&p->lock);
	return ret;
}

static int poseidon_fm_close(struct file *filp)
{
	struct poseidon *p = filp->private_data;
	struct radio_data *fm = &p->radio_data;
	uint32_t status;

	mutex_lock(&p->lock);
	fm->users--;
	if (0 == fm->users)
		p->state &= ~POSEIDON_STATE_FM;

	if (fm->is_radio_streaming && filp == p->file_for_stream) {
		fm->is_radio_streaming = 0;
		send_set_req(p, PLAY_SERVICE, TLG_TUNE_PLAY_SVC_STOP, &status);
	}
	usb_autopm_put_interface(p->interface);
	mutex_unlock(&p->lock);

	kref_put(&p->kref, poseidon_delete);
	filp->private_data = NULL;
	return 0;
}

static int vidioc_querycap(struct file *file, void *priv,
			struct v4l2_capability *v)
{
	struct poseidon *p = file->private_data;

	strlcpy(v->driver, "tele-radio", sizeof(v->driver));
	strlcpy(v->card, "Telegent Poseidon", sizeof(v->card));
	usb_make_path(p->udev, v->bus_info, sizeof(v->bus_info));
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static const struct v4l2_file_operations poseidon_fm_fops = {
	.owner         = THIS_MODULE,
	.open          = poseidon_fm_open,
	.release       = poseidon_fm_close,
	.ioctl	       = video_ioctl2,
};

static int tlg_fm_vidioc_g_tuner(struct file *file, void *priv,
				 struct v4l2_tuner *vt)
{
	struct tuner_fm_sig_stat_s fm_stat = {};
	int ret, status, count = 5;
	struct poseidon *p = file->private_data;

	if (vt->index != 0)
		return -EINVAL;

	vt->type	= V4L2_TUNER_RADIO;
	vt->capability	= V4L2_TUNER_CAP_STEREO;
	vt->rangelow	= TUNER_FREQ_MIN_FM / 62500;
	vt->rangehigh	= TUNER_FREQ_MAX_FM / 62500;
	vt->rxsubchans	= V4L2_TUNER_SUB_STEREO;
	vt->audmode	= V4L2_TUNER_MODE_STEREO;
	vt->signal	= 0;
	vt->afc 	= 0;

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
	struct poseidon *p = file->private_data;

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

	freq =  (frequency * 125) * 500 / 1000;/* kHZ */
	if (freq < TUNER_FREQ_MIN_FM/1000 || freq > TUNER_FREQ_MAX_FM/1000) {
		ret = -EINVAL;
		goto error;
	}

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
	p->radio_data.fm_freq = frequency;
error:
	mutex_unlock(&p->lock);
	return ret;
}

static int fm_set_freq(struct file *file, void *priv,
		       struct v4l2_frequency *argp)
{
	struct poseidon *p = file->private_data;

	p->file_for_stream  = file;
#ifdef CONFIG_PM
	p->pm_suspend = pm_fm_suspend;
	p->pm_resume  = pm_fm_resume;
#endif
	return set_frequency(p, argp->frequency);
}

static int tlg_fm_vidioc_g_ctrl(struct file *file, void *priv,
		struct v4l2_control *arg)
{
	return 0;
}

static int tlg_fm_vidioc_g_exts_ctrl(struct file *file, void *fh,
				struct v4l2_ext_controls *ctrls)
{
	struct poseidon *p = file->private_data;
	int i;

	if (ctrls->ctrl_class != V4L2_CTRL_CLASS_FM_TX)
		return -EINVAL;

	for (i = 0; i < ctrls->count; i++) {
		struct v4l2_ext_control *ctrl = ctrls->controls + i;

		if (ctrl->id != V4L2_CID_TUNE_PREEMPHASIS)
			continue;

		if (i < MAX_PREEMPHASIS)
			ctrl->value = p->radio_data.pre_emphasis;
	}
	return 0;
}

static int tlg_fm_vidioc_s_exts_ctrl(struct file *file, void *fh,
			struct v4l2_ext_controls *ctrls)
{
	int i;

	if (ctrls->ctrl_class != V4L2_CTRL_CLASS_FM_TX)
		return -EINVAL;

	for (i = 0; i < ctrls->count; i++) {
		struct v4l2_ext_control *ctrl = ctrls->controls + i;

		if (ctrl->id != V4L2_CID_TUNE_PREEMPHASIS)
			continue;

		if (ctrl->value >= 0 && ctrl->value < MAX_PREEMPHASIS) {
			struct poseidon *p = file->private_data;
			int pre_emphasis = preemphasis[ctrl->value];
			u32 status;

			send_set_req(p, TUNER_AUD_ANA_STD,
						pre_emphasis, &status);
			p->radio_data.pre_emphasis = pre_emphasis;
		}
	}
	return 0;
}

static int tlg_fm_vidioc_s_ctrl(struct file *file, void *priv,
		struct v4l2_control *ctrl)
{
	return 0;
}

static int tlg_fm_vidioc_queryctrl(struct file *file, void *priv,
		struct v4l2_queryctrl *ctrl)
{
	if (!(ctrl->id & V4L2_CTRL_FLAG_NEXT_CTRL))
		return -EINVAL;

	ctrl->id &= ~V4L2_CTRL_FLAG_NEXT_CTRL;
	if (ctrl->id != V4L2_CID_TUNE_PREEMPHASIS) {
		/* return the next supported control */
		ctrl->id = V4L2_CID_TUNE_PREEMPHASIS;
		v4l2_ctrl_query_fill(ctrl, V4L2_PREEMPHASIS_DISABLED,
					V4L2_PREEMPHASIS_75_uS, 1,
					V4L2_PREEMPHASIS_50_uS);
		ctrl->flags = V4L2_CTRL_FLAG_UPDATE;
		return 0;
	}
	return -EINVAL;
}

static int tlg_fm_vidioc_querymenu(struct file *file, void *fh,
				struct v4l2_querymenu *qmenu)
{
	return v4l2_ctrl_query_menu(qmenu, NULL, NULL);
}

static int vidioc_s_tuner(struct file *file, void *priv, struct v4l2_tuner *vt)
{
	return vt->index > 0 ? -EINVAL : 0;
}
static int vidioc_s_audio(struct file *file, void *priv, const struct v4l2_audio *va)
{
	return (va->index != 0) ? -EINVAL : 0;
}

static int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	a->index    = 0;
	a->mode    = 0;
	a->capability = V4L2_AUDCAP_STEREO;
	strcpy(a->name, "Radio");
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, u32 i)
{
	return (i != 0) ? -EINVAL : 0;
}

static int vidioc_g_input(struct file *filp, void *priv, u32 *i)
{
	return (*i != 0) ? -EINVAL : 0;
}

static const struct v4l2_ioctl_ops poseidon_fm_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
	.vidioc_queryctrl   = tlg_fm_vidioc_queryctrl,
	.vidioc_querymenu   = tlg_fm_vidioc_querymenu,
	.vidioc_g_ctrl      = tlg_fm_vidioc_g_ctrl,
	.vidioc_s_ctrl      = tlg_fm_vidioc_s_ctrl,
	.vidioc_s_ext_ctrls = tlg_fm_vidioc_s_exts_ctrl,
	.vidioc_g_ext_ctrls = tlg_fm_vidioc_g_exts_ctrl,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_tuner     = tlg_fm_vidioc_g_tuner,
	.vidioc_g_frequency = fm_get_freq,
	.vidioc_s_frequency = fm_set_freq,
};

static struct video_device poseidon_fm_template = {
	.name       = "Telegent-Radio",
	.fops       = &poseidon_fm_fops,
	.minor      = -1,
	.release    = video_device_release,
	.ioctl_ops  = &poseidon_fm_ioctl_ops,
};

int poseidon_fm_init(struct poseidon *p)
{
	struct video_device *fm_dev;

	fm_dev = vdev_init(p, &poseidon_fm_template);
	if (fm_dev == NULL)
		return -1;

	if (video_register_device(fm_dev, VFL_TYPE_RADIO, -1) < 0) {
		video_device_release(fm_dev);
		return -1;
	}
	p->radio_data.fm_dev = fm_dev;
	return 0;
}

int poseidon_fm_exit(struct poseidon *p)
{
	destroy_video_device(&p->radio_data.fm_dev);
	return 0;
}
