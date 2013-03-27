/*
 * Copyright (C) 2010 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <media/v4l2-device.h>
#include <media/davinci/vpbe_types.h>
#include <media/davinci/vpbe.h>
#include <media/davinci/vpss.h>
#include <media/davinci/vpbe_venc.h>

#define VPBE_DEFAULT_OUTPUT	"Composite"
#define VPBE_DEFAULT_MODE	"ntsc"

static char *def_output = VPBE_DEFAULT_OUTPUT;
static char *def_mode = VPBE_DEFAULT_MODE;
static int debug;

module_param(def_output, charp, S_IRUGO);
module_param(def_mode, charp, S_IRUGO);
module_param(debug, int, 0644);

MODULE_PARM_DESC(def_output, "vpbe output name (default:Composite)");
MODULE_PARM_DESC(def_mode, "vpbe output mode name (default:ntsc");
MODULE_PARM_DESC(debug, "Debug level 0-1");

MODULE_DESCRIPTION("TI DMXXX VPBE Display controller");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments");

/**
 * vpbe_current_encoder_info - Get config info for current encoder
 * @vpbe_dev - vpbe device ptr
 *
 * Return ptr to current encoder config info
 */
static struct encoder_config_info*
vpbe_current_encoder_info(struct vpbe_device *vpbe_dev)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	int index = vpbe_dev->current_sd_index;

	return ((index == 0) ? &cfg->venc :
				&cfg->ext_encoders[index-1]);
}

/**
 * vpbe_find_encoder_sd_index - Given a name find encoder sd index
 *
 * @vpbe_config - ptr to vpbe cfg
 * @output_index - index used by application
 *
 * Return sd index of the encoder
 */
static int vpbe_find_encoder_sd_index(struct vpbe_config *cfg,
			     int index)
{
	char *encoder_name = cfg->outputs[index].subdev_name;
	int i;

	/* Venc is always first	*/
	if (!strcmp(encoder_name, cfg->venc.module_name))
		return 0;

	for (i = 0; i < cfg->num_ext_encoders; i++) {
		if (!strcmp(encoder_name,
		     cfg->ext_encoders[i].module_name))
			return i+1;
	}

	return -EINVAL;
}

/**
 * vpbe_g_cropcap - Get crop capabilities of the display
 * @vpbe_dev - vpbe device ptr
 * @cropcap - cropcap is a ptr to struct v4l2_cropcap
 *
 * Update the crop capabilities in crop cap for current
 * mode
 */
static int vpbe_g_cropcap(struct vpbe_device *vpbe_dev,
			  struct v4l2_cropcap *cropcap)
{
	if (NULL == cropcap)
		return -EINVAL;
	cropcap->bounds.left = 0;
	cropcap->bounds.top = 0;
	cropcap->bounds.width = vpbe_dev->current_timings.xres;
	cropcap->bounds.height = vpbe_dev->current_timings.yres;
	cropcap->defrect = cropcap->bounds;

	return 0;
}

/**
 * vpbe_enum_outputs - enumerate outputs
 * @vpbe_dev - vpbe device ptr
 * @output - ptr to v4l2_output structure
 *
 * Enumerates the outputs available at the vpbe display
 * returns the status, -EINVAL if end of output list
 */
static int vpbe_enum_outputs(struct vpbe_device *vpbe_dev,
			     struct v4l2_output *output)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	int temp_index = output->index;

	if (temp_index >= cfg->num_outputs)
		return -EINVAL;

	*output = cfg->outputs[temp_index].output;
	output->index = temp_index;

	return 0;
}

static int vpbe_get_mode_info(struct vpbe_device *vpbe_dev, char *mode,
			      int output_index)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	struct vpbe_enc_mode_info var;
	int curr_output = output_index;
	int i;

	if (NULL == mode)
		return -EINVAL;

	for (i = 0; i < cfg->outputs[curr_output].num_modes; i++) {
		var = cfg->outputs[curr_output].modes[i];
		if (!strcmp(mode, var.name)) {
			vpbe_dev->current_timings = var;
			return 0;
		}
	}

	return -EINVAL;
}

static int vpbe_get_current_mode_info(struct vpbe_device *vpbe_dev,
				      struct vpbe_enc_mode_info *mode_info)
{
	if (NULL == mode_info)
		return -EINVAL;

	*mode_info = vpbe_dev->current_timings;

	return 0;
}

/* Get std by std id */
static int vpbe_get_std_info(struct vpbe_device *vpbe_dev,
			     v4l2_std_id std_id)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	struct vpbe_enc_mode_info var;
	int curr_output = vpbe_dev->current_out_index;
	int i;

	for (i = 0; i < vpbe_dev->cfg->outputs[curr_output].num_modes; i++) {
		var = cfg->outputs[curr_output].modes[i];
		if ((var.timings_type & VPBE_ENC_STD) &&
		  (var.std_id & std_id)) {
			vpbe_dev->current_timings = var;
			return 0;
		}
	}

	return -EINVAL;
}

static int vpbe_get_std_info_by_name(struct vpbe_device *vpbe_dev,
				char *std_name)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	struct vpbe_enc_mode_info var;
	int curr_output = vpbe_dev->current_out_index;
	int i;

	for (i = 0; i < vpbe_dev->cfg->outputs[curr_output].num_modes; i++) {
		var = cfg->outputs[curr_output].modes[i];
		if (!strcmp(var.name, std_name)) {
			vpbe_dev->current_timings = var;
			return 0;
		}
	}

	return -EINVAL;
}

/**
 * vpbe_set_output - Set output
 * @vpbe_dev - vpbe device ptr
 * @index - index of output
 *
 * Set vpbe output to the output specified by the index
 */
static int vpbe_set_output(struct vpbe_device *vpbe_dev, int index)
{
	struct encoder_config_info *curr_enc_info =
			vpbe_current_encoder_info(vpbe_dev);
	struct vpbe_config *cfg = vpbe_dev->cfg;
	struct venc_platform_data *venc_device = vpbe_dev->venc_device;
	enum v4l2_mbus_pixelcode if_params;
	int enc_out_index;
	int sd_index;
	int ret = 0;

	if (index >= cfg->num_outputs)
		return -EINVAL;

	mutex_lock(&vpbe_dev->lock);

	sd_index = vpbe_dev->current_sd_index;
	enc_out_index = cfg->outputs[index].output.index;
	/*
	 * Currently we switch the encoder based on output selected
	 * by the application. If media controller is implemented later
	 * there is will be an API added to setup_link between venc
	 * and external encoder. So in that case below comparison always
	 * match and encoder will not be switched. But if application
	 * chose not to use media controller, then this provides current
	 * way of switching encoder at the venc output.
	 */
	if (strcmp(curr_enc_info->module_name,
		   cfg->outputs[index].subdev_name)) {
		/* Need to switch the encoder at the output */
		sd_index = vpbe_find_encoder_sd_index(cfg, index);
		if (sd_index < 0) {
			ret = -EINVAL;
			goto out;
		}

		if_params = cfg->outputs[index].if_params;
		venc_device->setup_if_config(if_params);
		if (ret)
			goto out;
	}

	/* Set output at the encoder */
	ret = v4l2_subdev_call(vpbe_dev->encoders[sd_index], video,
				       s_routing, 0, enc_out_index, 0);
	if (ret)
		goto out;

	/*
	 * It is assumed that venc or extenal encoder will set a default
	 * mode in the sub device. For external encoder or LCD pannel output,
	 * we also need to set up the lcd port for the required mode. So setup
	 * the lcd port for the default mode that is configured in the board
	 * arch/arm/mach-davinci/board-dm355-evm.setup file for the external
	 * encoder.
	 */
	ret = vpbe_get_mode_info(vpbe_dev,
				 cfg->outputs[index].default_mode, index);
	if (!ret) {
		struct osd_state *osd_device = vpbe_dev->osd_device;

		osd_device->ops.set_left_margin(osd_device,
			vpbe_dev->current_timings.left_margin);
		osd_device->ops.set_top_margin(osd_device,
		vpbe_dev->current_timings.upper_margin);
		vpbe_dev->current_sd_index = sd_index;
		vpbe_dev->current_out_index = index;
	}
out:
	mutex_unlock(&vpbe_dev->lock);
	return ret;
}

static int vpbe_set_default_output(struct vpbe_device *vpbe_dev)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	int ret = 0;
	int i;

	for (i = 0; i < cfg->num_outputs; i++) {
		if (!strcmp(def_output,
			    cfg->outputs[i].output.name)) {
			ret = vpbe_set_output(vpbe_dev, i);
			if (!ret)
				vpbe_dev->current_out_index = i;
			return ret;
		}
	}
	return ret;
}

/**
 * vpbe_get_output - Get output
 * @vpbe_dev - vpbe device ptr
 *
 * return current vpbe output to the the index
 */
static unsigned int vpbe_get_output(struct vpbe_device *vpbe_dev)
{
	return vpbe_dev->current_out_index;
}

/**
 * vpbe_s_dv_timings - Set the given preset timings in the encoder
 *
 * Sets the timings if supported by the current encoder. Return the status.
 * 0 - success & -EINVAL on error
 */
static int vpbe_s_dv_timings(struct vpbe_device *vpbe_dev,
		    struct v4l2_dv_timings *dv_timings)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	int out_index = vpbe_dev->current_out_index;
	struct vpbe_output *output = &cfg->outputs[out_index];
	int sd_index = vpbe_dev->current_sd_index;
	int ret, i;


	if (!(cfg->outputs[out_index].output.capabilities &
	    V4L2_OUT_CAP_DV_TIMINGS))
		return -EINVAL;

	for (i = 0; i < output->num_modes; i++) {
		if (output->modes[i].timings_type == VPBE_ENC_CUSTOM_TIMINGS &&
		    !memcmp(&output->modes[i].dv_timings,
				dv_timings, sizeof(*dv_timings)))
			break;
	}
	if (i >= output->num_modes)
		return -EINVAL;
	vpbe_dev->current_timings = output->modes[i];
	mutex_lock(&vpbe_dev->lock);

	ret = v4l2_subdev_call(vpbe_dev->encoders[sd_index], video,
					s_dv_timings, dv_timings);
	if (!ret && (vpbe_dev->amp != NULL)) {
		/* Call amplifier subdevice */
		ret = v4l2_subdev_call(vpbe_dev->amp, video,
				s_dv_timings, dv_timings);
	}
	/* set the lcd controller output for the given mode */
	if (!ret) {
		struct osd_state *osd_device = vpbe_dev->osd_device;

		osd_device->ops.set_left_margin(osd_device,
		vpbe_dev->current_timings.left_margin);
		osd_device->ops.set_top_margin(osd_device,
		vpbe_dev->current_timings.upper_margin);
	}
	mutex_unlock(&vpbe_dev->lock);

	return ret;
}

/**
 * vpbe_g_dv_timings - Get the timings in the current encoder
 *
 * Get the timings in the current encoder. Return the status. 0 - success
 * -EINVAL on error
 */
static int vpbe_g_dv_timings(struct vpbe_device *vpbe_dev,
		     struct v4l2_dv_timings *dv_timings)
{
	if (vpbe_dev->current_timings.timings_type &
	  VPBE_ENC_CUSTOM_TIMINGS) {
		*dv_timings = vpbe_dev->current_timings.dv_timings;
		return 0;
	}

	return -EINVAL;
}

/**
 * vpbe_enum_dv_timings - Enumerate the dv timings in the current encoder
 *
 * Get the timings in the current encoder. Return the status. 0 - success
 * -EINVAL on error
 */
static int vpbe_enum_dv_timings(struct vpbe_device *vpbe_dev,
			 struct v4l2_enum_dv_timings *timings)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	int out_index = vpbe_dev->current_out_index;
	struct vpbe_output *output = &cfg->outputs[out_index];
	int j = 0;
	int i;

	if (!(output->output.capabilities & V4L2_OUT_CAP_DV_TIMINGS))
		return -EINVAL;

	for (i = 0; i < output->num_modes; i++) {
		if (output->modes[i].timings_type == VPBE_ENC_CUSTOM_TIMINGS) {
			if (j == timings->index)
				break;
			j++;
		}
	}

	if (i == output->num_modes)
		return -EINVAL;
	timings->timings = output->modes[i].dv_timings;
	return 0;
}

/**
 * vpbe_s_std - Set the given standard in the encoder
 *
 * Sets the standard if supported by the current encoder. Return the status.
 * 0 - success & -EINVAL on error
 */
static int vpbe_s_std(struct vpbe_device *vpbe_dev, v4l2_std_id *std_id)
{
	struct vpbe_config *cfg = vpbe_dev->cfg;
	int out_index = vpbe_dev->current_out_index;
	int sd_index = vpbe_dev->current_sd_index;
	int ret;

	if (!(cfg->outputs[out_index].output.capabilities &
		V4L2_OUT_CAP_STD))
		return -EINVAL;

	ret = vpbe_get_std_info(vpbe_dev, *std_id);
	if (ret)
		return ret;

	mutex_lock(&vpbe_dev->lock);

	ret = v4l2_subdev_call(vpbe_dev->encoders[sd_index], video,
			       s_std_output, *std_id);
	/* set the lcd controller output for the given mode */
	if (!ret) {
		struct osd_state *osd_device = vpbe_dev->osd_device;

		osd_device->ops.set_left_margin(osd_device,
		vpbe_dev->current_timings.left_margin);
		osd_device->ops.set_top_margin(osd_device,
		vpbe_dev->current_timings.upper_margin);
	}
	mutex_unlock(&vpbe_dev->lock);

	return ret;
}

/**
 * vpbe_g_std - Get the standard in the current encoder
 *
 * Get the standard in the current encoder. Return the status. 0 - success
 * -EINVAL on error
 */
static int vpbe_g_std(struct vpbe_device *vpbe_dev, v4l2_std_id *std_id)
{
	struct vpbe_enc_mode_info *cur_timings = &vpbe_dev->current_timings;

	if (cur_timings->timings_type & VPBE_ENC_STD) {
		*std_id = cur_timings->std_id;
		return 0;
	}

	return -EINVAL;
}

/**
 * vpbe_set_mode - Set mode in the current encoder using mode info
 *
 * Use the mode string to decide what timings to set in the encoder
 * This is typically useful when fbset command is used to change the current
 * timings by specifying a string to indicate the timings.
 */
static int vpbe_set_mode(struct vpbe_device *vpbe_dev,
			 struct vpbe_enc_mode_info *mode_info)
{
	struct vpbe_enc_mode_info *preset_mode = NULL;
	struct vpbe_config *cfg = vpbe_dev->cfg;
	struct v4l2_dv_timings dv_timings;
	struct osd_state *osd_device;
	int out_index = vpbe_dev->current_out_index;
	int ret = 0;
	int i;

	if ((NULL == mode_info) || (NULL == mode_info->name))
		return -EINVAL;

	for (i = 0; i < cfg->outputs[out_index].num_modes; i++) {
		if (!strcmp(mode_info->name,
		     cfg->outputs[out_index].modes[i].name)) {
			preset_mode = &cfg->outputs[out_index].modes[i];
			/*
			 * it may be one of the 3 timings type. Check and
			 * invoke right API
			 */
			if (preset_mode->timings_type & VPBE_ENC_STD)
				return vpbe_s_std(vpbe_dev,
						 &preset_mode->std_id);
			if (preset_mode->timings_type &
						VPBE_ENC_CUSTOM_TIMINGS) {
				dv_timings =
					preset_mode->dv_timings;
				return vpbe_s_dv_timings(vpbe_dev, &dv_timings);
			}
		}
	}

	/* Only custom timing should reach here */
	if (preset_mode == NULL)
		return -EINVAL;

	mutex_lock(&vpbe_dev->lock);

	osd_device = vpbe_dev->osd_device;
	vpbe_dev->current_timings = *preset_mode;
	osd_device->ops.set_left_margin(osd_device,
		vpbe_dev->current_timings.left_margin);
	osd_device->ops.set_top_margin(osd_device,
		vpbe_dev->current_timings.upper_margin);

	mutex_unlock(&vpbe_dev->lock);

	return ret;
}

static int vpbe_set_default_mode(struct vpbe_device *vpbe_dev)
{
	int ret;

	ret = vpbe_get_std_info_by_name(vpbe_dev, def_mode);
	if (ret)
		return ret;

	/* set the default mode in the encoder */
	return vpbe_set_mode(vpbe_dev, &vpbe_dev->current_timings);
}

static int platform_device_get(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vpbe_device *vpbe_dev = data;

	if (strstr(pdev->name, "vpbe-osd") != NULL)
		vpbe_dev->osd_device = platform_get_drvdata(pdev);
	if (strstr(pdev->name, "vpbe-venc") != NULL)
		vpbe_dev->venc_device = dev_get_platdata(&pdev->dev);

	return 0;
}

/**
 * vpbe_initialize() - Initialize the vpbe display controller
 * @vpbe_dev - vpbe device ptr
 *
 * Master frame buffer device drivers calls this to initialize vpbe
 * display controller. This will then registers v4l2 device and the sub
 * devices and sets a current encoder sub device for display. v4l2 display
 * device driver is the master and frame buffer display device driver is
 * the slave. Frame buffer display driver checks the initialized during
 * probe and exit if not initialized. Returns status.
 */
static int vpbe_initialize(struct device *dev, struct vpbe_device *vpbe_dev)
{
	struct encoder_config_info *enc_info;
	struct amp_config_info *amp_info;
	struct v4l2_subdev **enc_subdev;
	struct osd_state *osd_device;
	struct i2c_adapter *i2c_adap;
	int num_encoders;
	int ret = 0;
	int err;
	int i;

	/*
	 * v4l2 abd FBDev frame buffer devices will get the vpbe_dev pointer
	 * from the platform device by iteration of platform drivers and
	 * matching with device name
	 */
	if (NULL == vpbe_dev || NULL == dev) {
		printk(KERN_ERR "Null device pointers.\n");
		return -ENODEV;
	}

	if (vpbe_dev->initialized)
		return 0;

	mutex_lock(&vpbe_dev->lock);

	if (strcmp(vpbe_dev->cfg->module_name, "dm644x-vpbe-display") != 0) {
		/* We have dac clock available for platform */
		vpbe_dev->dac_clk = clk_get(vpbe_dev->pdev, "vpss_dac");
		if (IS_ERR(vpbe_dev->dac_clk)) {
			ret =  PTR_ERR(vpbe_dev->dac_clk);
			goto fail_mutex_unlock;
		}
		if (clk_prepare_enable(vpbe_dev->dac_clk)) {
			ret =  -ENODEV;
			goto fail_mutex_unlock;
		}
	}

	/* first enable vpss clocks */
	vpss_enable_clock(VPSS_VPBE_CLOCK, 1);

	/* First register a v4l2 device */
	ret = v4l2_device_register(dev, &vpbe_dev->v4l2_dev);
	if (ret) {
		v4l2_err(dev->driver,
			"Unable to register v4l2 device.\n");
		goto fail_clk_put;
	}
	v4l2_info(&vpbe_dev->v4l2_dev, "vpbe v4l2 device registered\n");

	err = bus_for_each_dev(&platform_bus_type, NULL, vpbe_dev,
			       platform_device_get);
	if (err < 0) {
		ret = err;
		goto fail_dev_unregister;
	}

	vpbe_dev->venc = venc_sub_dev_init(&vpbe_dev->v4l2_dev,
					   vpbe_dev->cfg->venc.module_name);
	/* register venc sub device */
	if (vpbe_dev->venc == NULL) {
		v4l2_err(&vpbe_dev->v4l2_dev,
			"vpbe unable to init venc sub device\n");
		ret = -ENODEV;
		goto fail_dev_unregister;
	}
	/* initialize osd device */
	osd_device = vpbe_dev->osd_device;

	if (NULL != osd_device->ops.initialize) {
		err = osd_device->ops.initialize(osd_device);
		if (err) {
			v4l2_err(&vpbe_dev->v4l2_dev,
				 "unable to initialize the OSD device");
			err = -ENOMEM;
			goto fail_dev_unregister;
		}
	}

	/*
	 * Register any external encoders that are configured. At index 0 we
	 * store venc sd index.
	 */
	num_encoders = vpbe_dev->cfg->num_ext_encoders + 1;
	vpbe_dev->encoders = kmalloc(
				sizeof(struct v4l2_subdev *)*num_encoders,
				GFP_KERNEL);
	if (NULL == vpbe_dev->encoders) {
		v4l2_err(&vpbe_dev->v4l2_dev,
			"unable to allocate memory for encoders sub devices");
		ret = -ENOMEM;
		goto fail_dev_unregister;
	}

	i2c_adap = i2c_get_adapter(vpbe_dev->cfg->i2c_adapter_id);
	for (i = 0; i < (vpbe_dev->cfg->num_ext_encoders + 1); i++) {
		if (i == 0) {
			/* venc is at index 0 */
			enc_subdev = &vpbe_dev->encoders[i];
			*enc_subdev = vpbe_dev->venc;
			continue;
		}
		enc_info = &vpbe_dev->cfg->ext_encoders[i];
		if (enc_info->is_i2c) {
			enc_subdev = &vpbe_dev->encoders[i];
			*enc_subdev = v4l2_i2c_new_subdev_board(
						&vpbe_dev->v4l2_dev, i2c_adap,
						&enc_info->board_info, NULL);
			if (*enc_subdev)
				v4l2_info(&vpbe_dev->v4l2_dev,
					  "v4l2 sub device %s registered\n",
					  enc_info->module_name);
			else {
				v4l2_err(&vpbe_dev->v4l2_dev, "encoder %s"
					 " failed to register",
					 enc_info->module_name);
				ret = -ENODEV;
				goto fail_kfree_encoders;
			}
		} else
			v4l2_warn(&vpbe_dev->v4l2_dev, "non-i2c encoders"
				 " currently not supported");
	}
	/* Add amplifier subdevice for dm365 */
	if ((strcmp(vpbe_dev->cfg->module_name, "dm365-vpbe-display") == 0) &&
			vpbe_dev->cfg->amp != NULL) {
		amp_info = vpbe_dev->cfg->amp;
		if (amp_info->is_i2c) {
			vpbe_dev->amp = v4l2_i2c_new_subdev_board(
			&vpbe_dev->v4l2_dev, i2c_adap,
			&amp_info->board_info, NULL);
			if (!vpbe_dev->amp) {
				v4l2_err(&vpbe_dev->v4l2_dev,
					 "amplifier %s failed to register",
					 amp_info->module_name);
				ret = -ENODEV;
				goto fail_kfree_encoders;
			}
			v4l2_info(&vpbe_dev->v4l2_dev,
					  "v4l2 sub device %s registered\n",
					  amp_info->module_name);
		} else {
			    vpbe_dev->amp = NULL;
			    v4l2_warn(&vpbe_dev->v4l2_dev, "non-i2c amplifiers"
			    " currently not supported");
		}
	} else {
	    vpbe_dev->amp = NULL;
	}

	/* set the current encoder and output to that of venc by default */
	vpbe_dev->current_sd_index = 0;
	vpbe_dev->current_out_index = 0;

	mutex_unlock(&vpbe_dev->lock);

	printk(KERN_NOTICE "Setting default output to %s\n", def_output);
	ret = vpbe_set_default_output(vpbe_dev);
	if (ret) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Failed to set default output %s",
			 def_output);
		return ret;
	}

	printk(KERN_NOTICE "Setting default mode to %s\n", def_mode);
	ret = vpbe_set_default_mode(vpbe_dev);
	if (ret) {
		v4l2_err(&vpbe_dev->v4l2_dev, "Failed to set default mode %s",
			 def_mode);
		return ret;
	}
	vpbe_dev->initialized = 1;
	/* TBD handling of bootargs for default output and mode */
	return 0;

fail_kfree_encoders:
	kfree(vpbe_dev->encoders);
fail_dev_unregister:
	v4l2_device_unregister(&vpbe_dev->v4l2_dev);
fail_clk_put:
	if (strcmp(vpbe_dev->cfg->module_name, "dm644x-vpbe-display") != 0) {
		clk_disable_unprepare(vpbe_dev->dac_clk);
		clk_put(vpbe_dev->dac_clk);
	}
fail_mutex_unlock:
	mutex_unlock(&vpbe_dev->lock);
	return ret;
}

/**
 * vpbe_deinitialize() - de-initialize the vpbe display controller
 * @dev - Master and slave device ptr
 *
 * vpbe_master and slave frame buffer devices calls this to de-initialize
 * the display controller. It is called when master and slave device
 * driver modules are removed and no longer requires the display controller.
 */
static void vpbe_deinitialize(struct device *dev, struct vpbe_device *vpbe_dev)
{
	v4l2_device_unregister(&vpbe_dev->v4l2_dev);
	if (strcmp(vpbe_dev->cfg->module_name, "dm644x-vpbe-display") != 0) {
		clk_disable_unprepare(vpbe_dev->dac_clk);
		clk_put(vpbe_dev->dac_clk);
	}

	kfree(vpbe_dev->amp);
	kfree(vpbe_dev->encoders);
	vpbe_dev->initialized = 0;
	/* disable vpss clocks */
	vpss_enable_clock(VPSS_VPBE_CLOCK, 0);
}

static struct vpbe_device_ops vpbe_dev_ops = {
	.g_cropcap = vpbe_g_cropcap,
	.enum_outputs = vpbe_enum_outputs,
	.set_output = vpbe_set_output,
	.get_output = vpbe_get_output,
	.s_dv_timings = vpbe_s_dv_timings,
	.g_dv_timings = vpbe_g_dv_timings,
	.enum_dv_timings = vpbe_enum_dv_timings,
	.s_std = vpbe_s_std,
	.g_std = vpbe_g_std,
	.initialize = vpbe_initialize,
	.deinitialize = vpbe_deinitialize,
	.get_mode_info = vpbe_get_current_mode_info,
	.set_mode = vpbe_set_mode,
};

static int vpbe_probe(struct platform_device *pdev)
{
	struct vpbe_device *vpbe_dev;
	struct vpbe_config *cfg;
	int ret = -EINVAL;

	if (pdev->dev.platform_data == NULL) {
		v4l2_err(pdev->dev.driver, "No platform data\n");
		return -ENODEV;
	}
	cfg = pdev->dev.platform_data;

	if (!cfg->module_name[0] ||
	    !cfg->osd.module_name[0] ||
	    !cfg->venc.module_name[0]) {
		v4l2_err(pdev->dev.driver, "vpbe display module names not"
			 " defined\n");
		return ret;
	}

	vpbe_dev = kzalloc(sizeof(*vpbe_dev), GFP_KERNEL);
	if (vpbe_dev == NULL) {
		v4l2_err(pdev->dev.driver, "Unable to allocate memory"
			 " for vpbe_device\n");
		return -ENOMEM;
	}
	vpbe_dev->cfg = cfg;
	vpbe_dev->ops = vpbe_dev_ops;
	vpbe_dev->pdev = &pdev->dev;

	if (cfg->outputs->num_modes > 0)
		vpbe_dev->current_timings = vpbe_dev->cfg->outputs[0].modes[0];
	else {
		kfree(vpbe_dev);
		return -ENODEV;
	}

	/* set the driver data in platform device */
	platform_set_drvdata(pdev, vpbe_dev);
	mutex_init(&vpbe_dev->lock);

	return 0;
}

static int vpbe_remove(struct platform_device *device)
{
	struct vpbe_device *vpbe_dev = platform_get_drvdata(device);

	kfree(vpbe_dev);

	return 0;
}

static struct platform_driver vpbe_driver = {
	.driver	= {
		.name	= "vpbe_controller",
		.owner	= THIS_MODULE,
	},
	.probe = vpbe_probe,
	.remove = vpbe_remove,
};

module_platform_driver(vpbe_driver);
