/*
 * drivers/media/radio/radio-si4713.c
 *
 * Platform Driver for Silicon Labs Si4713 FM Radio Transmitter:
 *
 * Copyright (c) 2008 Instituto Nokia de Tecnologia - INdT
 * Contact: Eduardo Valentin <eduardo.valentin@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/radio-si4713.h>

/* module parameters */
static int radio_nr = -1;	/* radio device minor (-1 ==> auto assign) */
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(radio_nr,
		 "Minor number for radio device (-1 ==> auto assign)");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eduardo Valentin <eduardo.valentin@nokia.com>");
MODULE_DESCRIPTION("Platform driver for Si4713 FM Radio Transmitter");
MODULE_VERSION("0.0.1");

/* Driver state struct */
struct radio_si4713_device {
	struct v4l2_device		v4l2_dev;
	struct video_device		*radio_dev;
};

/* radio_si4713_fops - file operations interface */
static const struct v4l2_file_operations radio_si4713_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= video_ioctl2,
};

/* Video4Linux Interface */
static int radio_si4713_fill_audout(struct v4l2_audioout *vao)
{
	/* TODO: check presence of audio output */
	strlcpy(vao->name, "FM Modulator Audio Out", 32);

	return 0;
}

static int radio_si4713_enumaudout(struct file *file, void *priv,
						struct v4l2_audioout *vao)
{
	return radio_si4713_fill_audout(vao);
}

static int radio_si4713_g_audout(struct file *file, void *priv,
					struct v4l2_audioout *vao)
{
	int rval = radio_si4713_fill_audout(vao);

	vao->index = 0;

	return rval;
}

static int radio_si4713_s_audout(struct file *file, void *priv,
					struct v4l2_audioout *vao)
{
	return vao->index ? -EINVAL : 0;
}

/* radio_si4713_querycap - query device capabilities */
static int radio_si4713_querycap(struct file *file, void *priv,
					struct v4l2_capability *capability)
{
	struct radio_si4713_device *rsdev;

	rsdev = video_get_drvdata(video_devdata(file));

	strlcpy(capability->driver, "radio-si4713", sizeof(capability->driver));
	strlcpy(capability->card, "Silicon Labs Si4713 Modulator",
				sizeof(capability->card));
	capability->capabilities = V4L2_CAP_MODULATOR | V4L2_CAP_RDS_OUTPUT;

	return 0;
}

/* radio_si4713_queryctrl - enumerate control items */
static int radio_si4713_queryctrl(struct file *file, void *priv,
						struct v4l2_queryctrl *qc)
{
	/* Must be sorted from low to high control ID! */
	static const u32 user_ctrls[] = {
		V4L2_CID_USER_CLASS,
		V4L2_CID_AUDIO_MUTE,
		0
	};

	/* Must be sorted from low to high control ID! */
	static const u32 fmtx_ctrls[] = {
		V4L2_CID_FM_TX_CLASS,
		V4L2_CID_RDS_TX_DEVIATION,
		V4L2_CID_RDS_TX_PI,
		V4L2_CID_RDS_TX_PTY,
		V4L2_CID_RDS_TX_PS_NAME,
		V4L2_CID_RDS_TX_RADIO_TEXT,
		V4L2_CID_AUDIO_LIMITER_ENABLED,
		V4L2_CID_AUDIO_LIMITER_RELEASE_TIME,
		V4L2_CID_AUDIO_LIMITER_DEVIATION,
		V4L2_CID_AUDIO_COMPRESSION_ENABLED,
		V4L2_CID_AUDIO_COMPRESSION_GAIN,
		V4L2_CID_AUDIO_COMPRESSION_THRESHOLD,
		V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME,
		V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME,
		V4L2_CID_PILOT_TONE_ENABLED,
		V4L2_CID_PILOT_TONE_DEVIATION,
		V4L2_CID_PILOT_TONE_FREQUENCY,
		V4L2_CID_TUNE_PREEMPHASIS,
		V4L2_CID_TUNE_POWER_LEVEL,
		V4L2_CID_TUNE_ANTENNA_CAPACITOR,
		0
	};
	static const u32 *ctrl_classes[] = {
		user_ctrls,
		fmtx_ctrls,
		NULL
	};
	struct radio_si4713_device *rsdev;

	rsdev = video_get_drvdata(video_devdata(file));

	qc->id = v4l2_ctrl_next(ctrl_classes, qc->id);
	if (qc->id == 0)
		return -EINVAL;

	if (qc->id == V4L2_CID_USER_CLASS || qc->id == V4L2_CID_FM_TX_CLASS)
		return v4l2_ctrl_query_fill(qc, 0, 0, 0, 0);

	return v4l2_device_call_until_err(&rsdev->v4l2_dev, 0, core,
						queryctrl, qc);
}

/*
 * v4l2 ioctl call backs.
 * we are just a wrapper for v4l2_sub_devs.
 */
static inline struct v4l2_device *get_v4l2_dev(struct file *file)
{
	return &((struct radio_si4713_device *)video_drvdata(file))->v4l2_dev;
}

static int radio_si4713_g_ext_ctrls(struct file *file, void *p,
						struct v4l2_ext_controls *vecs)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, core,
							g_ext_ctrls, vecs);
}

static int radio_si4713_s_ext_ctrls(struct file *file, void *p,
						struct v4l2_ext_controls *vecs)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, core,
							s_ext_ctrls, vecs);
}

static int radio_si4713_g_ctrl(struct file *file, void *p,
						struct v4l2_control *vc)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, core,
							g_ctrl, vc);
}

static int radio_si4713_s_ctrl(struct file *file, void *p,
						struct v4l2_control *vc)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, core,
							s_ctrl, vc);
}

static int radio_si4713_g_modulator(struct file *file, void *p,
						struct v4l2_modulator *vm)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, tuner,
							g_modulator, vm);
}

static int radio_si4713_s_modulator(struct file *file, void *p,
						struct v4l2_modulator *vm)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, tuner,
							s_modulator, vm);
}

static int radio_si4713_g_frequency(struct file *file, void *p,
						struct v4l2_frequency *vf)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, tuner,
							g_frequency, vf);
}

static int radio_si4713_s_frequency(struct file *file, void *p,
						struct v4l2_frequency *vf)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, tuner,
							s_frequency, vf);
}

static long radio_si4713_default(struct file *file, void *p, int cmd, void *arg)
{
	return v4l2_device_call_until_err(get_v4l2_dev(file), 0, core,
							ioctl, cmd, arg);
}

static struct v4l2_ioctl_ops radio_si4713_ioctl_ops = {
	.vidioc_enumaudout	= radio_si4713_enumaudout,
	.vidioc_g_audout	= radio_si4713_g_audout,
	.vidioc_s_audout	= radio_si4713_s_audout,
	.vidioc_querycap	= radio_si4713_querycap,
	.vidioc_queryctrl	= radio_si4713_queryctrl,
	.vidioc_g_ext_ctrls	= radio_si4713_g_ext_ctrls,
	.vidioc_s_ext_ctrls	= radio_si4713_s_ext_ctrls,
	.vidioc_g_ctrl		= radio_si4713_g_ctrl,
	.vidioc_s_ctrl		= radio_si4713_s_ctrl,
	.vidioc_g_modulator	= radio_si4713_g_modulator,
	.vidioc_s_modulator	= radio_si4713_s_modulator,
	.vidioc_g_frequency	= radio_si4713_g_frequency,
	.vidioc_s_frequency	= radio_si4713_s_frequency,
	.vidioc_default		= radio_si4713_default,
};

/* radio_si4713_vdev_template - video device interface */
static struct video_device radio_si4713_vdev_template = {
	.fops			= &radio_si4713_fops,
	.name			= "radio-si4713",
	.release		= video_device_release,
	.ioctl_ops		= &radio_si4713_ioctl_ops,
};

/* Platform driver interface */
/* radio_si4713_pdriver_probe - probe for the device */
static int radio_si4713_pdriver_probe(struct platform_device *pdev)
{
	struct radio_si4713_platform_data *pdata = pdev->dev.platform_data;
	struct radio_si4713_device *rsdev;
	struct i2c_adapter *adapter;
	struct v4l2_subdev *sd;
	int rval = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "Cannot proceed without platform data.\n");
		rval = -EINVAL;
		goto exit;
	}

	rsdev = kzalloc(sizeof *rsdev, GFP_KERNEL);
	if (!rsdev) {
		dev_err(&pdev->dev, "Failed to alloc video device.\n");
		rval = -ENOMEM;
		goto exit;
	}

	rval = v4l2_device_register(&pdev->dev, &rsdev->v4l2_dev);
	if (rval) {
		dev_err(&pdev->dev, "Failed to register v4l2 device.\n");
		goto free_rsdev;
	}

	adapter = i2c_get_adapter(pdata->i2c_bus);
	if (!adapter) {
		dev_err(&pdev->dev, "Cannot get i2c adapter %d\n",
							pdata->i2c_bus);
		rval = -ENODEV;
		goto unregister_v4l2_dev;
	}

	sd = v4l2_i2c_new_subdev_board(&rsdev->v4l2_dev, adapter, "si4713_i2c",
					pdata->subdev_board_info, NULL);
	if (!sd) {
		dev_err(&pdev->dev, "Cannot get v4l2 subdevice\n");
		rval = -ENODEV;
		goto unregister_v4l2_dev;
	}

	rsdev->radio_dev = video_device_alloc();
	if (!rsdev->radio_dev) {
		dev_err(&pdev->dev, "Failed to alloc video device.\n");
		rval = -ENOMEM;
		goto unregister_v4l2_dev;
	}

	memcpy(rsdev->radio_dev, &radio_si4713_vdev_template,
			sizeof(radio_si4713_vdev_template));
	video_set_drvdata(rsdev->radio_dev, rsdev);
	if (video_register_device(rsdev->radio_dev, VFL_TYPE_RADIO, radio_nr)) {
		dev_err(&pdev->dev, "Could not register video device.\n");
		rval = -EIO;
		goto free_vdev;
	}
	dev_info(&pdev->dev, "New device successfully probed\n");

	goto exit;

free_vdev:
	video_device_release(rsdev->radio_dev);
unregister_v4l2_dev:
	v4l2_device_unregister(&rsdev->v4l2_dev);
free_rsdev:
	kfree(rsdev);
exit:
	return rval;
}

/* radio_si4713_pdriver_remove - remove the device */
static int __exit radio_si4713_pdriver_remove(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct radio_si4713_device *rsdev = container_of(v4l2_dev,
						struct radio_si4713_device,
						v4l2_dev);

	video_unregister_device(rsdev->radio_dev);
	v4l2_device_unregister(&rsdev->v4l2_dev);
	kfree(rsdev);

	return 0;
}

static struct platform_driver radio_si4713_pdriver = {
	.driver		= {
		.name	= "radio-si4713",
	},
	.probe		= radio_si4713_pdriver_probe,
	.remove         = __exit_p(radio_si4713_pdriver_remove),
};

/* Module Interface */
static int __init radio_si4713_module_init(void)
{
	return platform_driver_register(&radio_si4713_pdriver);
}

static void __exit radio_si4713_module_exit(void)
{
	platform_driver_unregister(&radio_si4713_pdriver);
}

module_init(radio_si4713_module_init);
module_exit(radio_si4713_module_exit);

