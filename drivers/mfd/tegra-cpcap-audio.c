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
static struct cpcap_audio_stream current_output = {
	.id	= CPCAP_AUDIO_OUT_SPEAKER,
};
static struct cpcap_audio_stream current_input = {
	.id	= CPCAP_AUDIO_IN_MIC1,
};
static int codec_rate;
static int stdac_rate;
static bool dock_connected;
static bool bluetooth_byp;

static int cpcap_audio_ctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int cpcap_audio_ctl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static DEFINE_MUTEX(cpcap_lock);

static void tegra_setup_audio_output_off(void)
{
	/* turn off the amplifier */
	gpio_direction_output(pdata->speaker_gpio, 0);
	gpio_direction_output(pdata->headset_gpio, 0);

	pdata->state->stdac_mute = CPCAP_AUDIO_STDAC_MUTE;
	pdata->state->stdac_mode = CPCAP_AUDIO_STDAC_OFF;

	pdata->state->stdac_primary_speaker = CPCAP_AUDIO_OUT_NONE;
	pdata->state->stdac_secondary_speaker = CPCAP_AUDIO_OUT_NONE;
	cpcap_audio_set_audio_state(pdata->state);
}

static void tegra_setup_audio_out_speaker_on(void)
{
	/* turn off the amplifier */
	gpio_direction_output(pdata->speaker_gpio, 0);
	gpio_direction_output(pdata->headset_gpio, 0);

	pdata->state->stdac_mode = CPCAP_AUDIO_STDAC_ON;
	/* Using an external amp, lineout is the loudspeaker. */
	pdata->state->stdac_primary_speaker = CPCAP_AUDIO_OUT_LINEOUT;
	pdata->state->stdac_secondary_speaker = CPCAP_AUDIO_OUT_NONE;
	cpcap_audio_set_audio_state(pdata->state);

	/* turn on the amplifier */
	gpio_direction_output(pdata->speaker_gpio, 1);
	gpio_direction_output(pdata->headset_gpio, 0);
}

static void tegra_setup_audio_out_headset_on(void)
{
	/* turn off the amplifier */
	gpio_direction_output(pdata->speaker_gpio, 0);
	gpio_direction_output(pdata->headset_gpio, 0);

	pdata->state->stdac_mode = CPCAP_AUDIO_STDAC_ON;
	pdata->state->stdac_primary_speaker = CPCAP_AUDIO_OUT_STEREO_HEADSET;
	pdata->state->stdac_secondary_speaker = CPCAP_AUDIO_OUT_NONE;
	cpcap_audio_set_audio_state(pdata->state);

	/* turn on the amplifier */
	gpio_direction_output(pdata->speaker_gpio, 0);
	gpio_direction_output(pdata->headset_gpio, 1);
}

static void tegra_setup_audio_out_headset_and_speaker_on(void)
{
	/* turn off the amplifier */
	gpio_direction_output(pdata->speaker_gpio, 0);
	gpio_direction_output(pdata->headset_gpio, 0);

	pdata->state->stdac_mode = CPCAP_AUDIO_STDAC_ON;
	pdata->state->stdac_primary_speaker = CPCAP_AUDIO_OUT_STEREO_HEADSET;
	pdata->state->stdac_secondary_speaker = CPCAP_AUDIO_OUT_LINEOUT;
	cpcap_audio_set_audio_state(pdata->state);

	/* turn on the amplifier */
	gpio_direction_output(pdata->speaker_gpio, 1);
	gpio_direction_output(pdata->headset_gpio, 1);
}

static void tegra_setup_audio_out_dock_headset_on(void)
{
	pdata->state->stdac_mode = CPCAP_AUDIO_STDAC_ON;
	pdata->state->stdac_primary_speaker = CPCAP_AUDIO_OUT_EMU_STEREO;
	pdata->state->stdac_secondary_speaker = CPCAP_AUDIO_OUT_EMU_STEREO;
	cpcap_audio_set_audio_state(pdata->state);
}

static void tegra_setup_audio_in_mute(void)
{
	pdata->state->codec_mute = CPCAP_AUDIO_CODEC_MUTE;
	pdata->state->codec_mode = CPCAP_AUDIO_CODEC_OFF;

	pdata->state->microphone = CPCAP_AUDIO_IN_NONE;

	cpcap_audio_set_audio_state(pdata->state);
}

static void tegra_setup_audio_in_handset_on(void)
{
	pdata->state->codec_mute = CPCAP_AUDIO_CODEC_UNMUTE;
	pdata->state->codec_mode = CPCAP_AUDIO_CODEC_ON;

	pdata->state->microphone = CPCAP_AUDIO_IN_HANDSET;
	cpcap_audio_set_audio_state(pdata->state);
}

static void tegra_setup_audio_in_headset_on(void)
{
	pdata->state->codec_mute = CPCAP_AUDIO_CODEC_UNMUTE;
	pdata->state->codec_mode = CPCAP_AUDIO_CODEC_ON;

	pdata->state->microphone = CPCAP_AUDIO_IN_HEADSET;
	cpcap_audio_set_audio_state(pdata->state);
}

static int rate_to_cpcap_codec_rate(int rate)
{
	return
	    rate == 8000  ? CPCAP_AUDIO_CODEC_RATE_8000_HZ :
	    rate == 11025 ? CPCAP_AUDIO_CODEC_RATE_11025_HZ :
	    rate == 12000 ? CPCAP_AUDIO_CODEC_RATE_12000_HZ :
	    rate == 16000 ? CPCAP_AUDIO_CODEC_RATE_16000_HZ :
	    rate == 22050 ? CPCAP_AUDIO_CODEC_RATE_22050_HZ :
	    rate == 24000 ? CPCAP_AUDIO_CODEC_RATE_24000_HZ :
	    rate == 32000 ? CPCAP_AUDIO_CODEC_RATE_32000_HZ :
	    rate == 44100 ? CPCAP_AUDIO_CODEC_RATE_44100_HZ :
	    rate == 48000 ? CPCAP_AUDIO_CODEC_RATE_48000_HZ :
	    /*default*/     CPCAP_AUDIO_CODEC_RATE_8000_HZ;
}

static int rate_to_cpcap_stdac_rate(int rate)
{
	return
	    rate == 8000  ? CPCAP_AUDIO_STDAC_RATE_8000_HZ :
	    rate == 11025 ? CPCAP_AUDIO_STDAC_RATE_11025_HZ :
	    rate == 12000 ? CPCAP_AUDIO_STDAC_RATE_12000_HZ :
	    rate == 16000 ? CPCAP_AUDIO_STDAC_RATE_16000_HZ :
	    rate == 22050 ? CPCAP_AUDIO_STDAC_RATE_22050_HZ :
	    rate == 24000 ? CPCAP_AUDIO_STDAC_RATE_24000_HZ :
	    rate == 32000 ? CPCAP_AUDIO_STDAC_RATE_32000_HZ :
	    rate == 44100 ? CPCAP_AUDIO_STDAC_RATE_44100_HZ :
	    rate == 48000 ? CPCAP_AUDIO_STDAC_RATE_48000_HZ :
	    /*default*/     CPCAP_AUDIO_STDAC_RATE_44100_HZ;
}

static void tegra_setup_audio_out_rate(int rate)
{
	pdata->state->stdac_rate = rate_to_cpcap_stdac_rate(rate);
	stdac_rate = rate;
	cpcap_audio_set_audio_state(pdata->state);
}

static void tegra_setup_audio_in_rate(int rate)
{
	pdata->state->codec_rate = rate_to_cpcap_codec_rate(rate);
	codec_rate = rate;
	cpcap_audio_set_audio_state(pdata->state);
}

static long cpcap_audio_ctl_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int rc = 0;
	struct cpcap_audio_stream in, out;
	int rate;
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
			pr_debug("%s: setting output path to speaker\n",
					__func__);
			if (out.on)
				tegra_setup_audio_out_speaker_on();
			else
				tegra_setup_audio_output_off();
			current_output = out;
			break;
		case CPCAP_AUDIO_OUT_HEADSET:
			pr_debug("%s: setting output path to headset\n",
					__func__);
			if (out.on)
				tegra_setup_audio_out_headset_on();
			else
				tegra_setup_audio_output_off();
			current_output = out;
			break;
		case CPCAP_AUDIO_OUT_HEADSET_AND_SPEAKER:
			pr_debug("%s: setting output path to "
					"headset + speaker\n", __func__);
			if (out.on)
				tegra_setup_audio_out_headset_and_speaker_on();
			else
				tegra_setup_audio_output_off();

			current_output = out;
			break;
		case CPCAP_AUDIO_OUT_ANLG_DOCK_HEADSET:
			pr_err("%s: setting output path to basic dock\n",
					__func__);
			if (out.on)
				tegra_setup_audio_out_dock_headset_on();
			else
				tegra_setup_audio_output_off();
			current_output = out;
			break;
		case CPCAP_AUDIO_OUT_STANDBY:
			current_output.on = !out.on;
			if (out.on) {
				pr_debug("%s: standby mode\n", __func__);
				tegra_setup_audio_output_off();
				break;
			}

			switch (current_output.id) {
			case CPCAP_AUDIO_OUT_SPEAKER:
				pr_debug("%s: standby off (speaker)", __func__);
				tegra_setup_audio_out_speaker_on();
				break;
			case CPCAP_AUDIO_OUT_HEADSET:
				pr_debug("%s: standby off (headset)", __func__);
				tegra_setup_audio_out_headset_on();
				break;
			case CPCAP_AUDIO_OUT_HEADSET_AND_SPEAKER:
				pr_debug("%s: standby off (speaker + headset)",
					__func__);
				tegra_setup_audio_out_headset_and_speaker_on();
				break;
			case CPCAP_AUDIO_OUT_ANLG_DOCK_HEADSET:
				pr_err("%s: standby off (dock headset)",
					__func__);
				tegra_setup_audio_out_dock_headset_on();
				break;
			}
			break;
		}
		break;
	case CPCAP_AUDIO_OUT_GET_OUTPUT:
		if (copy_to_user((void __user *)arg, &current_output,
					sizeof(current_output)))
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
		switch (in.id) {
		case CPCAP_AUDIO_IN_MIC1:
			if (in.on) {
				pr_debug("%s: setting input path to on-board mic\n",
					__func__);
				tegra_setup_audio_in_handset_on();
			} else {
				pr_debug("%s: mute on-board mic\n", __func__);
				tegra_setup_audio_in_mute();
			}

			current_input = in;
			break;
		case CPCAP_AUDIO_IN_MIC2:
			if (in.on) {
				pr_debug("%s: setting input path to headset mic\n",
					__func__);
				tegra_setup_audio_in_headset_on();
			} else {
				pr_debug("%s: mute headset mic\n", __func__);
				tegra_setup_audio_in_mute();
			}

			current_input = in;
			break;
		case CPCAP_AUDIO_IN_STANDBY:
			current_input.on = !in.on;
			if (in.on) {
				pr_debug("%s: microphone in standby mode\n",
					__func__);
				tegra_setup_audio_in_mute();
				break;
			}
			switch (current_input.id) {
			case CPCAP_AUDIO_IN_MIC1:
				tegra_setup_audio_in_handset_on();
				break;
			case CPCAP_AUDIO_IN_MIC2:
				tegra_setup_audio_in_headset_on();
				break;
			}
			break;
		}
		break;
	case CPCAP_AUDIO_IN_GET_INPUT:
		if (copy_to_user((void __user *)arg, &current_input,
					sizeof(current_input)))
			rc = -EFAULT;
		break;
	case CPCAP_AUDIO_OUT_SET_VOLUME:
		if (arg > CPCAP_AUDIO_OUT_VOL_MAX) {
			pr_err("%s: invalid audio volume %ld\n",
				__func__, arg);
			rc = -EINVAL;
			goto done;
		}
		pdata->state->output_gain = arg;
		cpcap_audio_set_audio_state(pdata->state);
		break;
	case CPCAP_AUDIO_IN_SET_VOLUME:
		if (arg > CPCAP_AUDIO_IN_VOL_MAX) {
			pr_err("%s: invalid audio-input volume %ld\n",
				__func__, arg);
			rc = -EINVAL;
			goto done;
		}
		pdata->state->input_gain = (unsigned)arg;
		cpcap_audio_set_audio_state(pdata->state);
		break;
	case CPCAP_AUDIO_OUT_GET_VOLUME:
		if (copy_to_user((void __user *)arg, &pdata->state->output_gain,
					sizeof(unsigned int))) {
			rc = -EFAULT;
			goto done;
		}
		break;
	case CPCAP_AUDIO_IN_GET_VOLUME:
		if (copy_to_user((void __user *)arg, &pdata->state->input_gain,
					sizeof(unsigned int))) {
			rc = -EFAULT;
			goto done;
		}
		break;
	case CPCAP_AUDIO_OUT_GET_RATE:
		if (copy_to_user((void __user *)arg, &stdac_rate,
					sizeof(int))) {
			rc = -EFAULT;
			goto done;
		}
		break;
	case CPCAP_AUDIO_OUT_SET_RATE:
		rate = (int)arg;
		if (rate < 8000 || rate > 48000) {
			pr_err("%s: invalid rate %d\n",	__func__, rate);
			rc = -EFAULT;
			goto done;
		}
		pr_debug("%s: setting output rate to %dHz\n", __func__, rate);
		tegra_setup_audio_out_rate(rate);
		break;
	case CPCAP_AUDIO_IN_GET_RATE:
		if (copy_to_user((void __user *)arg, &codec_rate,
					sizeof(int))) {
			rc = -EFAULT;
			goto done;
		}
		break;
	case CPCAP_AUDIO_IN_SET_RATE:
		rate = (int)arg;
		if (rate < 8000 || rate > 48000) {
			pr_err("%s: invalid in rate %d\n", __func__, rate);
			rc = -EFAULT;
			goto done;
		}
		pr_debug("%s: setting input rate to %dHz\n", __func__, rate);
		tegra_setup_audio_in_rate(rate);
		break;
	case CPCAP_AUDIO_SET_BLUETOOTH_BYPASS:
		bluetooth_byp = (bool)arg;
		if (pdata->bluetooth_bypass)
			pdata->bluetooth_bypass(bluetooth_byp);
		else
			pr_err("%s: no bluetooth bypass handler\n", __func__);
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

/* Couple the CPCAP and Dock audio state, to avoid pops */
void tegra_cpcap_audio_dock_state(bool connected)
{
	pr_debug("%s: %s", __func__, connected ? "connected" : "disconnected");

	mutex_lock(&cpcap_lock);
	dock_connected = connected;
	/* Borrow (unused) "ext output" to keep dock speaker amplifier on. */
	pdata->state->ext_primary_speaker = dock_connected ?
			CPCAP_AUDIO_OUT_EMU_STEREO : CPCAP_AUDIO_OUT_NONE;
	cpcap_audio_set_audio_state(pdata->state);
	mutex_unlock(&cpcap_lock);
}
EXPORT_SYMBOL(tegra_cpcap_audio_dock_state);

static void cpcap_audio_callback(int status)
{
	mutex_lock(&cpcap_lock);
	if (status == 1 || status == 2)	{
		if (pdata->state->stdac_primary_speaker ==
					CPCAP_AUDIO_OUT_STEREO_HEADSET)
			tegra_setup_audio_out_headset_on();
		if (pdata->state->microphone ==
					CPCAP_AUDIO_IN_HEADSET)
			tegra_setup_audio_in_headset_on();
	}
	if (status == 0) {
		if (pdata->state->stdac_primary_speaker ==
					CPCAP_AUDIO_OUT_STEREO_HEADSET)
			tegra_setup_audio_output_off();
		if (pdata->state->microphone ==
					CPCAP_AUDIO_IN_HEADSET)
			tegra_setup_audio_in_mute();
	}

	mutex_unlock(&cpcap_lock);
}

static int cpcap_audio_probe(struct platform_device *pdev)
{
	int rc;

	pr_debug("%s\n", __func__);

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

	cpcap->h2w_new_state = &cpcap_audio_callback;

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

#ifdef CONFIG_PM
static int tegra_audio_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);
	return 0;
}

static int tegra_audio_resume(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);
	/* initialize DAC/DAP connections */
	if (pdata->bluetooth_bypass)
		pdata->bluetooth_bypass(bluetooth_byp);
	else
		pr_warn("No function for setting up DAC/DAP.");
	return 0;
}
#endif

static struct platform_driver cpcap_audio_driver = {
	.probe = cpcap_audio_probe,
	.driver = {
		.name = "cpcap_audio",
		.owner = THIS_MODULE,
	},
#ifdef CONFIG_PM
	.suspend = tegra_audio_suspend,
	.resume = tegra_audio_resume,
#endif
};

static int __init tegra_cpcap_audio_init(void)
{
	return cpcap_driver_register(&cpcap_audio_driver);
}

module_init(tegra_cpcap_audio_init);
MODULE_LICENSE("GPL");
