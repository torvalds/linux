/* drivers/mfd/cpcap-audio.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *      Iliyan Malchev <malchev@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/cpcap-regbits.h>
#include <linux/spi/cpcap.h>
#include <linux/regulator/consumer.h>
#include <linux/cpcap_audio.h>
#include <linux/spi/cpcap.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/cpcap_audio.h>
#include <linux/uaccess.h>

#include <mach/cpcap_audio.h>

static struct cpcap_device *cpcap;
static struct cpcap_audio_platform_data *pdata;
static unsigned current_output = CPCAP_AUDIO_OUT_SPEAKER;
static unsigned current_input  = -1U; /* none */
static unsigned current_volume = CPCAP_AUDIO_OUT_VOL_MAX;
static unsigned current_in_volume = CPCAP_AUDIO_IN_VOL_MAX;

static int cpcap_set_volume(struct cpcap_device *cpcap, unsigned volume)
{
	pr_info("%s\n", __func__);
	volume &= 0xF;
	volume = volume << 12 | volume << 8;
	return cpcap_regacc_write(cpcap, CPCAP_REG_RXVC, volume, 0xFF00);
}


static int cpcap_set_mic_volume(struct cpcap_device *cpcap, unsigned volume)
{
	pr_info("%s\n", __func__);
	volume &= 0x1F;
	/* set the same volume for mic1 and mic2 */
	volume = volume << 5 | volume;
	return cpcap_regacc_write(cpcap, CPCAP_REG_TXMP, volume, 0x3FF);
}

static int cpcap_audio_ctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int cpcap_audio_ctl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static DEFINE_MUTEX(cpcap_lock);

static long cpcap_audio_ctl_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int rc = 0;
	struct cpcap_audio_stream in, out;

	mutex_lock(&cpcap_lock);

	switch (cmd) {
	case CPCAP_AUDIO_OUT_SET_OUTPUT:
		if (copy_from_user(&out, (const void __user *)arg,
				sizeof(out))) {
			rc = -EFAULT;
			goto done;
		}
		if (out.id > CPCAP_AUDIO_OUT_MAX) {
			pr_err("%s: invalid audio-output selector %d\n",
				__func__, out.id);
			rc = -EINVAL;
			goto done;
		}
		switch (out.id) {
		case CPCAP_AUDIO_OUT_SPEAKER:
			pr_info("%s: setting output path to speaker\n",
					__func__);
			pdata->state->stdac_primary_speaker =
					CPCAP_AUDIO_OUT_NONE;
			cpcap_audio_set_audio_state(pdata->state);
			if (!out.on) {
				if (pdata->speaker_gpio >= 0)
					gpio_direction_output(
						pdata->speaker_gpio, 0);
				break;
			}

			pr_info("%s: enable speaker\n", __func__);

			pdata->state->stdac_primary_speaker =
					CPCAP_AUDIO_OUT_LOUDSPEAKER;
			gpio_direction_output(pdata->headset_gpio, 0);
			gpio_direction_output(pdata->speaker_gpio, 1);
			break;
		case CPCAP_AUDIO_OUT_HEADSET:
			pr_info("%s: setting output path to headset\n",
					__func__);
			pdata->state->stdac_primary_speaker =
					CPCAP_AUDIO_OUT_NONE;
			cpcap_audio_set_audio_state(pdata->state);
			if (!out.on) {
				if (pdata->headset_gpio >= 0)
					gpio_direction_output(
						pdata->headset_gpio, 0);
				break;
			}
			pdata->state->stdac_primary_speaker =
					CPCAP_AUDIO_OUT_STEREO_HEADSET;
			cpcap_audio_set_audio_state(pdata->state);
			gpio_direction_output(pdata->speaker_gpio, 0);
			gpio_direction_output(pdata->headset_gpio, 1);
			break;
		case CPCAP_AUDIO_OUT_HEADSET_AND_SPEAKER:
			pr_info("%s: setting output path to "
					"headset + speaker\n", __func__);
			pdata->state->stdac_primary_speaker =
					CPCAP_AUDIO_OUT_NONE;
			cpcap_audio_set_audio_state(pdata->state);
			if (!out.on) {
				if (pdata->headset_gpio >= 0)
					gpio_direction_output(
						pdata->headset_gpio, 0);
					gpio_direction_output(
						pdata->speaker_gpio, 0);
				break;
			}
			pdata->state->stdac_primary_speaker =
					CPCAP_AUDIO_OUT_STEREO_HEADSET;
			cpcap_audio_set_audio_state(pdata->state);
			gpio_direction_output(pdata->speaker_gpio, 1);
			gpio_direction_output(pdata->headset_gpio, 1);
			break;
		}
		current_output = out.id;
		break;
	case CPCAP_AUDIO_OUT_GET_OUTPUT:
		if (copy_to_user((void __user *)arg, &current_output,
					sizeof(unsigned int)))
			rc = -EFAULT;
		break;
	case CPCAP_AUDIO_IN_SET_INPUT:
		if (copy_from_user(&in, (const void __user *)arg,
				sizeof(in))) {
			rc = -EFAULT;
			goto done;
		}

		if (in.id > CPCAP_AUDIO_IN_MAX) {
			pr_err("%s: invalid audio input selector %d\n",
				__func__, in.id);
			rc = -EINVAL;
			goto done;
		}

		pr_info("%s: muting current input before switch\n", __func__);

		pdata->state->microphone = CPCAP_AUDIO_IN_NONE;
		pdata->state->codec_mute = CPCAP_AUDIO_CODEC_MUTE;
		pdata->state->stdac_mute = CPCAP_AUDIO_STDAC_MUTE;
		pdata->state->codec_mode = CPCAP_AUDIO_CODEC_OFF;
		pdata->state->stdac_mode = CPCAP_AUDIO_STDAC_OFF;
		cpcap_audio_set_audio_state(pdata->state);

		if (!in.on)
			break;

		pdata->state->codec_mute = CPCAP_AUDIO_CODEC_UNMUTE;
		pdata->state->stdac_mute = CPCAP_AUDIO_STDAC_UNMUTE;
		pdata->state->codec_mode = CPCAP_AUDIO_CODEC_ON;
		pdata->state->stdac_mode = CPCAP_AUDIO_STDAC_ON;

		switch (in.id) {
		case CPCAP_AUDIO_IN_MIC1:
			pr_info("%s: setting input path to on-board mic\n",
					__func__);
			pdata->state->microphone = CPCAP_AUDIO_IN_HANDSET;
			cpcap_audio_set_audio_state(pdata->state);
			cpcap_audio_register_dump(pdata->state);
			break;
		case CPCAP_AUDIO_IN_MIC2:
			pr_info("%s: setting input path to headset mic\n",
					__func__);
			pdata->state->microphone = CPCAP_AUDIO_IN_HEADSET;
			cpcap_audio_set_audio_state(pdata->state);
			cpcap_audio_register_dump(pdata->state);
			break;
		}
		current_input = arg;
		break;
	case CPCAP_AUDIO_IN_GET_INPUT:
		if (copy_to_user((void __user *)arg, &current_input,
					sizeof(unsigned int)))
			rc = -EFAULT;
		break;
	case CPCAP_AUDIO_OUT_SET_VOLUME:
		if (arg > CPCAP_AUDIO_OUT_VOL_MAX) {
			pr_err("%s: invalid audio volume %ld\n",
				__func__, arg);
			rc = -EINVAL;
			goto done;
		}
		rc = cpcap_set_volume(cpcap, (unsigned)arg);
		if (rc < 0) {
			pr_err("%s: could not set audio volume to %ld: %d\n",
				__func__, arg, rc);
			goto done;
		}
		current_volume = arg;
		break;
	case CPCAP_AUDIO_IN_SET_VOLUME:
		if (arg > CPCAP_AUDIO_IN_VOL_MAX) {
			pr_err("%s: invalid audio-input volume %ld\n",
				__func__, arg);
			rc = -EINVAL;
			goto done;
		}
		rc = cpcap_set_mic_volume(cpcap, (unsigned)arg);
		if (rc < 0) {
			pr_err("%s: could not set audio-input"\
				" volume to %ld: %d\n", __func__, arg, rc);
			goto done;
		}
		current_in_volume = arg;
		break;
	case CPCAP_AUDIO_OUT_GET_VOLUME:
		if (copy_to_user((void __user *)arg, &current_volume,
					sizeof(unsigned int))) {
			rc = -EFAULT;
			goto done;
		}
		break;
	case CPCAP_AUDIO_IN_GET_VOLUME:
		if (copy_to_user((void __user *)arg, &current_in_volume,
					sizeof(unsigned int))) {
			rc = -EFAULT;
			goto done;
		}
		break;
	}

done:
	mutex_unlock(&cpcap_lock);
	return rc;
}

static const struct file_operations cpcap_audio_ctl_fops = {
	.open = cpcap_audio_ctl_open,
	.release = cpcap_audio_ctl_release,
	.unlocked_ioctl = cpcap_audio_ctl_ioctl,
};

static struct miscdevice cpcap_audio_ctl = {
	.name = "audio_ctl",
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &cpcap_audio_ctl_fops,
};

static int cpcap_audio_probe(struct platform_device *pdev)
{
	int rc;

	pr_info("%s\n", __func__);

	cpcap = platform_get_drvdata(pdev);
	BUG_ON(!cpcap);

	pdata = pdev->dev.platform_data;
	BUG_ON(!pdata);

	if (pdata->speaker_gpio >= 0) {
		tegra_gpio_enable(pdata->speaker_gpio);
		rc = gpio_request(pdata->speaker_gpio, "speaker");
		if (rc) {
			pr_err("%s: could not get speaker GPIO %d: %d\n",
				__func__, pdata->speaker_gpio, rc);
			goto fail1;
		}
	}

	if (pdata->headset_gpio >= 0) {
		tegra_gpio_enable(pdata->headset_gpio);
		rc = gpio_request(pdata->headset_gpio, "headset");
		if (rc) {
			pr_err("%s: could not get headset GPIO %d: %d\n",
				__func__, pdata->headset_gpio, rc);
			goto fail2;
		}
	}

	pdata->state->cpcap = cpcap;
	if (cpcap_audio_init(pdata->state, pdata->regulator))
		goto fail3;
	cpcap_audio_register_dump(pdata->state);

	pdata->state->stdac_mode = CPCAP_AUDIO_STDAC_ON;
	cpcap_audio_set_audio_state(pdata->state);
	cpcap_audio_register_dump(pdata->state);

	rc = misc_register(&cpcap_audio_ctl);
	if (rc < 0) {
		pr_err("%s: failed to register misc device: %d\n", __func__,
				rc);
		goto fail3;
	}

	return rc;

fail3:
	if (pdata->headset_gpio >= 0)
		gpio_free(pdata->headset_gpio);
fail2:
	if (pdata->headset_gpio >= 0)
		tegra_gpio_disable(pdata->headset_gpio);
	if (pdata->speaker_gpio >= 0)
		gpio_free(pdata->speaker_gpio);
fail1:
	if (pdata->speaker_gpio >= 0)
		tegra_gpio_disable(pdata->speaker_gpio);
	return rc;
}

static struct platform_driver cpcap_audio_driver = {
	.probe = cpcap_audio_probe,
	.driver = {
		.name = "cpcap_audio",
		.owner = THIS_MODULE,
	},
};

static int __init tegra_cpcap_audio_init(void)
{
	return cpcap_driver_register(&cpcap_audio_driver);
}

module_init(tegra_cpcap_audio_init);
MODULE_LICENSE("GPL");
