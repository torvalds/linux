// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus audio driver
 * Copyright 2015-2016 Google Inc.
 * Copyright 2015-2016 Linaro Ltd.
 */

#include <linux/greybus.h>
#include "audio_codec.h"

#define GBAUDIO_INVALID_ID	0xFF

/* mixer control */
struct gb_mixer_control {
	int min, max;
	unsigned int reg, rreg, shift, rshift, invert;
};

struct gbaudio_ctl_pvt {
	unsigned int ctl_id;
	unsigned int data_cport;
	unsigned int access;
	unsigned int vcount;
	struct gb_audio_ctl_elem_info *info;
};

static struct gbaudio_module_info *find_gb_module(
					struct gbaudio_codec_info *codec,
					char const *name)
{
	int dev_id;
	char begin[NAME_SIZE];
	struct gbaudio_module_info *module;

	if (!name)
		return NULL;

	if (sscanf(name, "%s %d", begin, &dev_id) != 2)
		return NULL;

	dev_dbg(codec->dev, "%s:Find module#%d\n", __func__, dev_id);

	mutex_lock(&codec->lock);
	list_for_each_entry(module, &codec->module_list, list) {
		if (module->dev_id == dev_id) {
			mutex_unlock(&codec->lock);
			return module;
		}
	}
	mutex_unlock(&codec->lock);
	dev_warn(codec->dev, "%s: module#%d missing in codec list\n", name,
		 dev_id);
	return NULL;
}

static const char *gbaudio_map_controlid(struct gbaudio_module_info *module,
					 __u8 control_id, __u8 index)
{
	struct gbaudio_control *control;

	if (control_id == GBAUDIO_INVALID_ID)
		return NULL;

	list_for_each_entry(control, &module->ctl_list, list) {
		if (control->id == control_id) {
			if (index == GBAUDIO_INVALID_ID)
				return control->name;
			if (index >= control->items)
				return NULL;
			return control->texts[index];
		}
	}
	list_for_each_entry(control, &module->widget_ctl_list, list) {
		if (control->id == control_id) {
			if (index == GBAUDIO_INVALID_ID)
				return control->name;
			if (index >= control->items)
				return NULL;
			return control->texts[index];
		}
	}
	return NULL;
}

static int gbaudio_map_controlname(struct gbaudio_module_info *module,
				   const char *name)
{
	struct gbaudio_control *control;

	list_for_each_entry(control, &module->ctl_list, list) {
		if (!strncmp(control->name, name, NAME_SIZE))
			return control->id;
	}

	dev_warn(module->dev, "%s: missing in modules controls list\n", name);

	return -EINVAL;
}

static int gbaudio_map_wcontrolname(struct gbaudio_module_info *module,
				    const char *name)
{
	struct gbaudio_control *control;

	list_for_each_entry(control, &module->widget_ctl_list, list) {
		if (!strncmp(control->wname, name, NAME_SIZE))
			return control->id;
	}
	dev_warn(module->dev, "%s: missing in modules controls list\n", name);

	return -EINVAL;
}

static int gbaudio_map_widgetname(struct gbaudio_module_info *module,
				  const char *name)
{
	struct gbaudio_widget *widget;

	list_for_each_entry(widget, &module->widget_list, list) {
		if (!strncmp(widget->name, name, NAME_SIZE))
			return widget->id;
	}
	dev_warn(module->dev, "%s: missing in modules widgets list\n", name);

	return -EINVAL;
}

static const char *gbaudio_map_widgetid(struct gbaudio_module_info *module,
					__u8 widget_id)
{
	struct gbaudio_widget *widget;

	list_for_each_entry(widget, &module->widget_list, list) {
		if (widget->id == widget_id)
			return widget->name;
	}
	return NULL;
}

static const char **gb_generate_enum_strings(struct gbaudio_module_info *gb,
					     struct gb_audio_enumerated *gbenum)
{
	const char **strings;
	int i;
	unsigned int items;
	__u8 *data;

	items = le32_to_cpu(gbenum->items);
	strings = devm_kcalloc(gb->dev, items, sizeof(char *), GFP_KERNEL);
	data = gbenum->names;

	for (i = 0; i < items; i++) {
		strings[i] = (const char *)data;
		while (*data != '\0')
			data++;
		data++;
	}

	return strings;
}

static int gbcodec_mixer_ctl_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	unsigned int max;
	const char *name;
	struct gbaudio_ctl_pvt *data;
	struct gb_audio_ctl_elem_info *info;
	struct gbaudio_module_info *module;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct gbaudio_codec_info *gbcodec = snd_soc_component_get_drvdata(comp);

	dev_dbg(comp->dev, "Entered %s:%s\n", __func__, kcontrol->id.name);
	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;

	if (!info) {
		dev_err(comp->dev, "NULL info for %s\n", uinfo->id.name);
		return -EINVAL;
	}

	/* update uinfo */
	uinfo->access = data->access;
	uinfo->count = data->vcount;
	uinfo->type = (snd_ctl_elem_type_t)info->type;

	switch (info->type) {
	case GB_AUDIO_CTL_ELEM_TYPE_BOOLEAN:
	case GB_AUDIO_CTL_ELEM_TYPE_INTEGER:
		uinfo->value.integer.min = le32_to_cpu(info->value.integer.min);
		uinfo->value.integer.max = le32_to_cpu(info->value.integer.max);
		break;
	case GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED:
		max = le32_to_cpu(info->value.enumerated.items);
		uinfo->value.enumerated.items = max;
		if (uinfo->value.enumerated.item > max - 1)
			uinfo->value.enumerated.item = max - 1;
		module = find_gb_module(gbcodec, kcontrol->id.name);
		if (!module)
			return -EINVAL;
		name = gbaudio_map_controlid(module, data->ctl_id,
					     uinfo->value.enumerated.item);
		strlcpy(uinfo->value.enumerated.name, name, NAME_SIZE);
		break;
	default:
		dev_err(comp->dev, "Invalid type: %d for %s:kcontrol\n",
			info->type, kcontrol->id.name);
		break;
	}
	return 0;
}

static int gbcodec_mixer_ctl_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int ret;
	struct gb_audio_ctl_elem_info *info;
	struct gbaudio_ctl_pvt *data;
	struct gb_audio_ctl_elem_value gbvalue;
	struct gbaudio_module_info *module;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct gbaudio_codec_info *gb = snd_soc_component_get_drvdata(comp);
	struct gb_bundle *bundle;

	dev_dbg(comp->dev, "Entered %s:%s\n", __func__, kcontrol->id.name);
	module = find_gb_module(gb, kcontrol->id.name);
	if (!module)
		return -EINVAL;

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;
	bundle = to_gb_bundle(module->dev);

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	ret = gb_audio_gb_get_control(module->mgmt_connection, data->ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);

	gb_pm_runtime_put_autosuspend(bundle);

	if (ret) {
		dev_err_ratelimited(comp->dev, "%d:Error in %s for %s\n", ret,
				    __func__, kcontrol->id.name);
		return ret;
	}

	/* update ucontrol */
	switch (info->type) {
	case GB_AUDIO_CTL_ELEM_TYPE_BOOLEAN:
	case GB_AUDIO_CTL_ELEM_TYPE_INTEGER:
		ucontrol->value.integer.value[0] =
			le32_to_cpu(gbvalue.value.integer_value[0]);
		if (data->vcount == 2)
			ucontrol->value.integer.value[1] =
				le32_to_cpu(gbvalue.value.integer_value[1]);
		break;
	case GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED:
		ucontrol->value.enumerated.item[0] =
			le32_to_cpu(gbvalue.value.enumerated_item[0]);
		if (data->vcount == 2)
			ucontrol->value.enumerated.item[1] =
				le32_to_cpu(gbvalue.value.enumerated_item[1]);
		break;
	default:
		dev_err(comp->dev, "Invalid type: %d for %s:kcontrol\n",
			info->type, kcontrol->id.name);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int gbcodec_mixer_ctl_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct gb_audio_ctl_elem_info *info;
	struct gbaudio_ctl_pvt *data;
	struct gb_audio_ctl_elem_value gbvalue;
	struct gbaudio_module_info *module;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct gbaudio_codec_info *gb = snd_soc_component_get_drvdata(comp);
	struct gb_bundle *bundle;

	dev_dbg(comp->dev, "Entered %s:%s\n", __func__, kcontrol->id.name);
	module = find_gb_module(gb, kcontrol->id.name);
	if (!module)
		return -EINVAL;

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;
	bundle = to_gb_bundle(module->dev);

	/* update ucontrol */
	switch (info->type) {
	case GB_AUDIO_CTL_ELEM_TYPE_BOOLEAN:
	case GB_AUDIO_CTL_ELEM_TYPE_INTEGER:
		gbvalue.value.integer_value[0] =
			cpu_to_le32(ucontrol->value.integer.value[0]);
		if (data->vcount == 2)
			gbvalue.value.integer_value[1] =
				cpu_to_le32(ucontrol->value.integer.value[1]);
		break;
	case GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED:
		gbvalue.value.enumerated_item[0] =
			cpu_to_le32(ucontrol->value.enumerated.item[0]);
		if (data->vcount == 2)
			gbvalue.value.enumerated_item[1] =
				cpu_to_le32(ucontrol->value.enumerated.item[1]);
		break;
	default:
		dev_err(comp->dev, "Invalid type: %d for %s:kcontrol\n",
			info->type, kcontrol->id.name);
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	ret = gb_audio_gb_set_control(module->mgmt_connection, data->ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);

	gb_pm_runtime_put_autosuspend(bundle);

	if (ret) {
		dev_err_ratelimited(comp->dev, "%d:Error in %s for %s\n", ret,
				    __func__, kcontrol->id.name);
	}

	return ret;
}

#define SOC_MIXER_GB(xname, kcount, data) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.count = kcount, .info = gbcodec_mixer_ctl_info, \
	.get = gbcodec_mixer_ctl_get, .put = gbcodec_mixer_ctl_put, \
	.private_value = (unsigned long)data }

/*
 * although below callback functions seems redundant to above functions.
 * same are kept to allow provision for different handling in case
 * of DAPM related sequencing, etc.
 */
static int gbcodec_mixer_dapm_ctl_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	int platform_max, platform_min;
	struct gbaudio_ctl_pvt *data;
	struct gb_audio_ctl_elem_info *info;

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;

	/* update uinfo */
	platform_max = le32_to_cpu(info->value.integer.max);
	platform_min = le32_to_cpu(info->value.integer.min);

	if (platform_max == 1 &&
	    !strnstr(kcontrol->id.name, " Volume", NAME_SIZE))
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = data->vcount;
	uinfo->value.integer.min = platform_min;
	uinfo->value.integer.max = platform_max;

	return 0;
}

static int gbcodec_mixer_dapm_ctl_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int ret;
	struct gbaudio_ctl_pvt *data;
	struct gb_audio_ctl_elem_value gbvalue;
	struct gbaudio_module_info *module;
	struct snd_soc_dapm_widget_list *wlist = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget *widget = wlist->widgets[0];
	struct device *codec_dev = widget->dapm->dev;
	struct gbaudio_codec_info *gb = dev_get_drvdata(codec_dev);
	struct gb_bundle *bundle;

	dev_dbg(codec_dev, "Entered %s:%s\n", __func__, kcontrol->id.name);
	module = find_gb_module(gb, kcontrol->id.name);
	if (!module)
		return -EINVAL;

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	bundle = to_gb_bundle(module->dev);

	if (data->vcount == 2)
		dev_warn(widget->dapm->dev,
			 "GB: Control '%s' is stereo, which is not supported\n",
			 kcontrol->id.name);

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	ret = gb_audio_gb_get_control(module->mgmt_connection, data->ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);

	gb_pm_runtime_put_autosuspend(bundle);

	if (ret) {
		dev_err_ratelimited(codec_dev, "%d:Error in %s for %s\n", ret,
				    __func__, kcontrol->id.name);
		return ret;
	}
	/* update ucontrol */
	ucontrol->value.integer.value[0] =
		le32_to_cpu(gbvalue.value.integer_value[0]);

	return ret;
}

static int gbcodec_mixer_dapm_ctl_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int ret, wi, max, connect;
	unsigned int mask, val;
	struct gb_audio_ctl_elem_info *info;
	struct gbaudio_ctl_pvt *data;
	struct gb_audio_ctl_elem_value gbvalue;
	struct gbaudio_module_info *module;
	struct snd_soc_dapm_widget_list *wlist = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget *widget = wlist->widgets[0];
	struct device *codec_dev = widget->dapm->dev;
	struct gbaudio_codec_info *gb = dev_get_drvdata(codec_dev);
	struct gb_bundle *bundle;

	dev_dbg(codec_dev, "Entered %s:%s\n", __func__, kcontrol->id.name);
	module = find_gb_module(gb, kcontrol->id.name);
	if (!module)
		return -EINVAL;

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;
	bundle = to_gb_bundle(module->dev);

	if (data->vcount == 2)
		dev_warn(widget->dapm->dev,
			 "GB: Control '%s' is stereo, which is not supported\n",
			 kcontrol->id.name);

	max = le32_to_cpu(info->value.integer.max);
	mask = (1 << fls(max)) - 1;
	val = ucontrol->value.integer.value[0] & mask;
	connect = !!val;

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	ret = gb_audio_gb_get_control(module->mgmt_connection, data->ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);
	if (ret)
		goto exit;

	/* update ucontrol */
	if (gbvalue.value.integer_value[0] != val) {
		for (wi = 0; wi < wlist->num_widgets; wi++) {
			widget = wlist->widgets[wi];
			snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol,
							connect, NULL);
		}
		gbvalue.value.integer_value[0] =
			cpu_to_le32(ucontrol->value.integer.value[0]);

		ret = gb_audio_gb_set_control(module->mgmt_connection,
					      data->ctl_id,
					      GB_AUDIO_INVALID_INDEX, &gbvalue);
	}

exit:
	gb_pm_runtime_put_autosuspend(bundle);
	if (ret)
		dev_err_ratelimited(codec_dev, "%d:Error in %s for %s\n", ret,
				    __func__, kcontrol->id.name);
	return ret;
}

#define SOC_DAPM_MIXER_GB(xname, kcount, data) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.count = kcount, .info = gbcodec_mixer_dapm_ctl_info, \
	.get = gbcodec_mixer_dapm_ctl_get, .put = gbcodec_mixer_dapm_ctl_put, \
	.private_value = (unsigned long)data}

static int gbcodec_event_spk(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *k, int event)
{
	/* Ensure GB speaker is connected */

	return 0;
}

static int gbcodec_event_hp(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *k, int event)
{
	/* Ensure GB module supports jack slot */

	return 0;
}

static int gbcodec_event_int_mic(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *k, int event)
{
	/* Ensure GB module supports jack slot */

	return 0;
}

static int gbaudio_validate_kcontrol_count(struct gb_audio_widget *w)
{
	int ret = 0;

	switch (w->type) {
	case snd_soc_dapm_spk:
	case snd_soc_dapm_hp:
	case snd_soc_dapm_mic:
	case snd_soc_dapm_output:
	case snd_soc_dapm_input:
		if (w->ncontrols)
			ret = -EINVAL;
		break;
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mux:
		if (w->ncontrols != 1)
			ret = -EINVAL;
		break;
	default:
		break;
	}

	return ret;
}

static int gbcodec_enum_ctl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int ret, ctl_id;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct gbaudio_codec_info *gb = snd_soc_component_get_drvdata(comp);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct gb_audio_ctl_elem_value gbvalue;
	struct gbaudio_module_info *module;
	struct gb_bundle *bundle;

	module = find_gb_module(gb, kcontrol->id.name);
	if (!module)
		return -EINVAL;

	ctl_id = gbaudio_map_controlname(module, kcontrol->id.name);
	if (ctl_id < 0)
		return -EINVAL;

	bundle = to_gb_bundle(module->dev);

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	ret = gb_audio_gb_get_control(module->mgmt_connection, ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);

	gb_pm_runtime_put_autosuspend(bundle);

	if (ret) {
		dev_err_ratelimited(comp->dev, "%d:Error in %s for %s\n", ret,
				    __func__, kcontrol->id.name);
		return ret;
	}

	ucontrol->value.enumerated.item[0] =
		le32_to_cpu(gbvalue.value.enumerated_item[0]);
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] =
			le32_to_cpu(gbvalue.value.enumerated_item[1]);

	return 0;
}

static int gbcodec_enum_ctl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int ret, ctl_id;
	struct snd_soc_component *comp = snd_soc_kcontrol_component(kcontrol);
	struct gbaudio_codec_info *gb = snd_soc_component_get_drvdata(comp);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct gb_audio_ctl_elem_value gbvalue;
	struct gbaudio_module_info *module;
	struct gb_bundle *bundle;

	module = find_gb_module(gb, kcontrol->id.name);
	if (!module)
		return -EINVAL;

	ctl_id = gbaudio_map_controlname(module, kcontrol->id.name);
	if (ctl_id < 0)
		return -EINVAL;

	if (ucontrol->value.enumerated.item[0] > e->items - 1)
		return -EINVAL;
	gbvalue.value.enumerated_item[0] =
		cpu_to_le32(ucontrol->value.enumerated.item[0]);

	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->items - 1)
			return -EINVAL;
		gbvalue.value.enumerated_item[1] =
			cpu_to_le32(ucontrol->value.enumerated.item[1]);
	}

	bundle = to_gb_bundle(module->dev);

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	ret = gb_audio_gb_set_control(module->mgmt_connection, ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);

	gb_pm_runtime_put_autosuspend(bundle);

	if (ret) {
		dev_err_ratelimited(comp->dev, "%d:Error in %s for %s\n",
				    ret, __func__, kcontrol->id.name);
	}

	return ret;
}

static int gbaudio_tplg_create_enum_kctl(struct gbaudio_module_info *gb,
					 struct snd_kcontrol_new *kctl,
					 struct gb_audio_control *ctl)
{
	struct soc_enum *gbe;
	struct gb_audio_enumerated *gb_enum;
	int i;

	gbe = devm_kzalloc(gb->dev, sizeof(*gbe), GFP_KERNEL);
	if (!gbe)
		return -ENOMEM;

	gb_enum = &ctl->info.value.enumerated;

	/* since count=1, and reg is dummy */
	gbe->items = le32_to_cpu(gb_enum->items);
	gbe->texts = gb_generate_enum_strings(gb, gb_enum);

	/* debug enum info */
	dev_dbg(gb->dev, "Max:%d, name_length:%d\n", gbe->items,
		le16_to_cpu(gb_enum->names_length));
	for (i = 0; i < gbe->items; i++)
		dev_dbg(gb->dev, "src[%d]: %s\n", i, gbe->texts[i]);

	*kctl = (struct snd_kcontrol_new)
		SOC_ENUM_EXT(ctl->name, *gbe, gbcodec_enum_ctl_get,
			     gbcodec_enum_ctl_put);
	return 0;
}

static int gbaudio_tplg_create_kcontrol(struct gbaudio_module_info *gb,
					struct snd_kcontrol_new *kctl,
					struct gb_audio_control *ctl)
{
	int ret = 0;
	struct gbaudio_ctl_pvt *ctldata;

	switch (ctl->iface) {
	case SNDRV_CTL_ELEM_IFACE_MIXER:
		switch (ctl->info.type) {
		case GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED:
			ret = gbaudio_tplg_create_enum_kctl(gb, kctl, ctl);
			break;
		default:
			ctldata = devm_kzalloc(gb->dev,
					       sizeof(struct gbaudio_ctl_pvt),
					       GFP_KERNEL);
			if (!ctldata)
				return -ENOMEM;
			ctldata->ctl_id = ctl->id;
			ctldata->data_cport = le16_to_cpu(ctl->data_cport);
			ctldata->access = ctl->access;
			ctldata->vcount = ctl->count_values;
			ctldata->info = &ctl->info;
			*kctl = (struct snd_kcontrol_new)
				SOC_MIXER_GB(ctl->name, ctl->count, ctldata);
			ctldata = NULL;
			break;
		}
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(gb->dev, "%s:%d control created\n", ctl->name, ctl->id);
	return ret;
}

static int gbcodec_enum_dapm_ctl_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	int ret, ctl_id;
	struct snd_soc_dapm_widget_list *wlist = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget *widget = wlist->widgets[0];
	struct gbaudio_module_info *module;
	struct gb_audio_ctl_elem_value gbvalue;
	struct device *codec_dev = widget->dapm->dev;
	struct gbaudio_codec_info *gb = dev_get_drvdata(codec_dev);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct gb_bundle *bundle;

	module = find_gb_module(gb, kcontrol->id.name);
	if (!module)
		return -EINVAL;

	ctl_id = gbaudio_map_wcontrolname(module, kcontrol->id.name);
	if (ctl_id < 0)
		return -EINVAL;

	bundle = to_gb_bundle(module->dev);

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	ret = gb_audio_gb_get_control(module->mgmt_connection, ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);

	gb_pm_runtime_put_autosuspend(bundle);

	if (ret) {
		dev_err_ratelimited(codec_dev, "%d:Error in %s for %s\n", ret,
				    __func__, kcontrol->id.name);
		return ret;
	}

	ucontrol->value.enumerated.item[0] = gbvalue.value.enumerated_item[0];
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] =
			gbvalue.value.enumerated_item[1];

	return 0;
}

static int gbcodec_enum_dapm_ctl_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	int ret, wi, ctl_id;
	unsigned int val, mux, change;
	unsigned int mask;
	struct snd_soc_dapm_widget_list *wlist = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget *widget = wlist->widgets[0];
	struct gb_audio_ctl_elem_value gbvalue;
	struct gbaudio_module_info *module;
	struct device *codec_dev = widget->dapm->dev;
	struct gbaudio_codec_info *gb = dev_get_drvdata(codec_dev);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct gb_bundle *bundle;

	if (ucontrol->value.enumerated.item[0] > e->items - 1)
		return -EINVAL;

	module = find_gb_module(gb, kcontrol->id.name);
	if (!module)
		return -EINVAL;

	ctl_id = gbaudio_map_wcontrolname(module, kcontrol->id.name);
	if (ctl_id < 0)
		return -EINVAL;

	change = 0;
	bundle = to_gb_bundle(module->dev);

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	ret = gb_audio_gb_get_control(module->mgmt_connection, ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);

	gb_pm_runtime_put_autosuspend(bundle);

	if (ret) {
		dev_err_ratelimited(codec_dev, "%d:Error in %s for %s\n", ret,
				    __func__, kcontrol->id.name);
		return ret;
	}

	mux = ucontrol->value.enumerated.item[0];
	val = mux << e->shift_l;
	mask = e->mask << e->shift_l;

	if (gbvalue.value.enumerated_item[0] !=
	    ucontrol->value.enumerated.item[0]) {
		change = 1;
		gbvalue.value.enumerated_item[0] =
			ucontrol->value.enumerated.item[0];
	}

	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->items - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= e->mask << e->shift_r;
		if (gbvalue.value.enumerated_item[1] !=
		    ucontrol->value.enumerated.item[1]) {
			change = 1;
			gbvalue.value.enumerated_item[1] =
				ucontrol->value.enumerated.item[1];
		}
	}

	if (change) {
		ret = gb_pm_runtime_get_sync(bundle);
		if (ret)
			return ret;

		ret = gb_audio_gb_set_control(module->mgmt_connection, ctl_id,
					      GB_AUDIO_INVALID_INDEX, &gbvalue);

		gb_pm_runtime_put_autosuspend(bundle);

		if (ret) {
			dev_err_ratelimited(codec_dev,
					    "%d:Error in %s for %s\n", ret,
					    __func__, kcontrol->id.name);
		}
		for (wi = 0; wi < wlist->num_widgets; wi++) {
			widget = wlist->widgets[wi];
			snd_soc_dapm_mux_update_power(widget->dapm, kcontrol,
						      val, e, NULL);
		}
	}

	return change;
}

static int gbaudio_tplg_create_enum_ctl(struct gbaudio_module_info *gb,
					struct snd_kcontrol_new *kctl,
					struct gb_audio_control *ctl)
{
	struct soc_enum *gbe;
	struct gb_audio_enumerated *gb_enum;
	int i;

	gbe = devm_kzalloc(gb->dev, sizeof(*gbe), GFP_KERNEL);
	if (!gbe)
		return -ENOMEM;

	gb_enum = &ctl->info.value.enumerated;

	/* since count=1, and reg is dummy */
	gbe->items = le32_to_cpu(gb_enum->items);
	gbe->texts = gb_generate_enum_strings(gb, gb_enum);

	/* debug enum info */
	dev_dbg(gb->dev, "Max:%d, name_length:%d\n", gbe->items,
		le16_to_cpu(gb_enum->names_length));
	for (i = 0; i < gbe->items; i++)
		dev_dbg(gb->dev, "src[%d]: %s\n", i, gbe->texts[i]);

	*kctl = (struct snd_kcontrol_new)
		SOC_DAPM_ENUM_EXT(ctl->name, *gbe, gbcodec_enum_dapm_ctl_get,
				  gbcodec_enum_dapm_ctl_put);
	return 0;
}

static int gbaudio_tplg_create_mixer_ctl(struct gbaudio_module_info *gb,
					 struct snd_kcontrol_new *kctl,
					 struct gb_audio_control *ctl)
{
	struct gbaudio_ctl_pvt *ctldata;

	ctldata = devm_kzalloc(gb->dev, sizeof(struct gbaudio_ctl_pvt),
			       GFP_KERNEL);
	if (!ctldata)
		return -ENOMEM;
	ctldata->ctl_id = ctl->id;
	ctldata->data_cport = le16_to_cpu(ctl->data_cport);
	ctldata->access = ctl->access;
	ctldata->vcount = ctl->count_values;
	ctldata->info = &ctl->info;
	*kctl = (struct snd_kcontrol_new)
		SOC_DAPM_MIXER_GB(ctl->name, ctl->count, ctldata);

	return 0;
}

static int gbaudio_tplg_create_wcontrol(struct gbaudio_module_info *gb,
					struct snd_kcontrol_new *kctl,
					struct gb_audio_control *ctl)
{
	int ret;

	switch (ctl->iface) {
	case SNDRV_CTL_ELEM_IFACE_MIXER:
		switch (ctl->info.type) {
		case GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED:
			ret = gbaudio_tplg_create_enum_ctl(gb, kctl, ctl);
			break;
		default:
			ret = gbaudio_tplg_create_mixer_ctl(gb, kctl, ctl);
			break;
		}
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(gb->dev, "%s:%d DAPM control created, ret:%d\n", ctl->name,
		ctl->id, ret);
	return ret;
}

static int gbaudio_widget_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	int wid;
	int ret;
	struct device *codec_dev = w->dapm->dev;
	struct gbaudio_codec_info *gbcodec = dev_get_drvdata(codec_dev);
	struct gbaudio_module_info *module;
	struct gb_bundle *bundle;

	dev_dbg(codec_dev, "%s %s %d\n", __func__, w->name, event);

	/* Find relevant module */
	module = find_gb_module(gbcodec, w->name);
	if (!module)
		return -EINVAL;

	/* map name to widget id */
	wid = gbaudio_map_widgetname(module, w->name);
	if (wid < 0) {
		dev_err(codec_dev, "Invalid widget name:%s\n", w->name);
		return -EINVAL;
	}

	bundle = to_gb_bundle(module->dev);

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = gb_audio_gb_enable_widget(module->mgmt_connection, wid);
		if (!ret)
			ret = gbaudio_module_update(gbcodec, w, module, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		ret = gb_audio_gb_disable_widget(module->mgmt_connection, wid);
		if (!ret)
			ret = gbaudio_module_update(gbcodec, w, module, 0);
		break;
	}
	if (ret)
		dev_err_ratelimited(codec_dev,
				    "%d: widget, event:%d failed:%d\n", wid,
				    event, ret);

	gb_pm_runtime_put_autosuspend(bundle);

	return ret;
}

static int gbaudio_tplg_create_widget(struct gbaudio_module_info *module,
				      struct snd_soc_dapm_widget *dw,
				      struct gb_audio_widget *w, int *w_size)
{
	int i, ret, csize;
	struct snd_kcontrol_new *widget_kctls;
	struct gb_audio_control *curr;
	struct gbaudio_control *control, *_control;
	size_t size;
	char temp_name[NAME_SIZE];

	ret = gbaudio_validate_kcontrol_count(w);
	if (ret) {
		dev_err(module->dev, "Invalid kcontrol count=%d for %s\n",
			w->ncontrols, w->name);
		return ret;
	}

	/* allocate memory for kcontrol */
	if (w->ncontrols) {
		size = sizeof(struct snd_kcontrol_new) * w->ncontrols;
		widget_kctls = devm_kzalloc(module->dev, size, GFP_KERNEL);
		if (!widget_kctls)
			return -ENOMEM;
	}

	*w_size = sizeof(struct gb_audio_widget);

	/* create relevant kcontrols */
	curr = w->ctl;
	for (i = 0; i < w->ncontrols; i++) {
		ret = gbaudio_tplg_create_wcontrol(module, &widget_kctls[i],
						   curr);
		if (ret) {
			dev_err(module->dev,
				"%s:%d type widget_ctl not supported\n",
				curr->name, curr->iface);
			goto error;
		}
		control = devm_kzalloc(module->dev,
				       sizeof(struct gbaudio_control),
				       GFP_KERNEL);
		if (!control) {
			ret = -ENOMEM;
			goto error;
		}
		control->id = curr->id;
		control->name = curr->name;
		control->wname = w->name;

		if (curr->info.type == GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED) {
			struct gb_audio_enumerated *gbenum =
				&curr->info.value.enumerated;

			csize = offsetof(struct gb_audio_control, info);
			csize += offsetof(struct gb_audio_ctl_elem_info, value);
			csize += offsetof(struct gb_audio_enumerated, names);
			csize += le16_to_cpu(gbenum->names_length);
			control->texts = (const char * const *)
				gb_generate_enum_strings(module, gbenum);
			control->items = le32_to_cpu(gbenum->items);
		} else {
			csize = sizeof(struct gb_audio_control);
		}

		*w_size += csize;
		curr = (void *)curr + csize;
		list_add(&control->list, &module->widget_ctl_list);
		dev_dbg(module->dev, "%s: control of type %d created\n",
			widget_kctls[i].name, widget_kctls[i].iface);
	}

	/* Prefix dev_id to widget control_name */
	strlcpy(temp_name, w->name, NAME_SIZE);
	snprintf(w->name, NAME_SIZE, "GB %d %s", module->dev_id, temp_name);

	switch (w->type) {
	case snd_soc_dapm_spk:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_SPK(w->name, gbcodec_event_spk);
		module->op_devices |= GBAUDIO_DEVICE_OUT_SPEAKER;
		break;
	case snd_soc_dapm_hp:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_HP(w->name, gbcodec_event_hp);
		module->op_devices |= (GBAUDIO_DEVICE_OUT_WIRED_HEADSET
					| GBAUDIO_DEVICE_OUT_WIRED_HEADPHONE);
		module->ip_devices |= GBAUDIO_DEVICE_IN_WIRED_HEADSET;
		break;
	case snd_soc_dapm_mic:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_MIC(w->name, gbcodec_event_int_mic);
		module->ip_devices |= GBAUDIO_DEVICE_IN_BUILTIN_MIC;
		break;
	case snd_soc_dapm_output:
		*dw = (struct snd_soc_dapm_widget)SND_SOC_DAPM_OUTPUT(w->name);
		break;
	case snd_soc_dapm_input:
		*dw = (struct snd_soc_dapm_widget)SND_SOC_DAPM_INPUT(w->name);
		break;
	case snd_soc_dapm_switch:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_SWITCH_E(w->name, SND_SOC_NOPM, 0, 0,
					      widget_kctls,
					      gbaudio_widget_event,
					      SND_SOC_DAPM_PRE_PMU |
					      SND_SOC_DAPM_POST_PMD);
		break;
	case snd_soc_dapm_pga:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_PGA_E(w->name, SND_SOC_NOPM, 0, 0, NULL, 0,
					   gbaudio_widget_event,
					   SND_SOC_DAPM_PRE_PMU |
					   SND_SOC_DAPM_POST_PMD);
		break;
	case snd_soc_dapm_mixer:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_MIXER_E(w->name, SND_SOC_NOPM, 0, 0, NULL,
					     0, gbaudio_widget_event,
					     SND_SOC_DAPM_PRE_PMU |
					     SND_SOC_DAPM_POST_PMD);
		break;
	case snd_soc_dapm_mux:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_MUX_E(w->name, SND_SOC_NOPM, 0, 0,
					   widget_kctls, gbaudio_widget_event,
					   SND_SOC_DAPM_PRE_PMU |
					   SND_SOC_DAPM_POST_PMD);
		break;
	case snd_soc_dapm_aif_in:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_AIF_IN_E(w->name, w->sname, 0,
					      SND_SOC_NOPM,
					      0, 0, gbaudio_widget_event,
					      SND_SOC_DAPM_PRE_PMU |
					      SND_SOC_DAPM_POST_PMD);
		break;
	case snd_soc_dapm_aif_out:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_AIF_OUT_E(w->name, w->sname, 0,
					       SND_SOC_NOPM,
					       0, 0, gbaudio_widget_event,
					       SND_SOC_DAPM_PRE_PMU |
					       SND_SOC_DAPM_POST_PMD);
		break;
	default:
		ret = -EINVAL;
		goto error;
	}

	dev_dbg(module->dev, "%s: widget of type %d created\n", dw->name,
		dw->id);
	return 0;
error:
	list_for_each_entry_safe(control, _control, &module->widget_ctl_list,
				 list) {
		list_del(&control->list);
		devm_kfree(module->dev, control);
	}
	return ret;
}

static int gbaudio_tplg_process_kcontrols(struct gbaudio_module_info *module,
					  struct gb_audio_control *controls)
{
	int i, csize, ret;
	struct snd_kcontrol_new *dapm_kctls;
	struct gb_audio_control *curr;
	struct gbaudio_control *control, *_control;
	size_t size;
	char temp_name[NAME_SIZE];

	size = sizeof(struct snd_kcontrol_new) * module->num_controls;
	dapm_kctls = devm_kzalloc(module->dev, size, GFP_KERNEL);
	if (!dapm_kctls)
		return -ENOMEM;

	curr = controls;
	for (i = 0; i < module->num_controls; i++) {
		ret = gbaudio_tplg_create_kcontrol(module, &dapm_kctls[i],
						   curr);
		if (ret) {
			dev_err(module->dev, "%s:%d type not supported\n",
				curr->name, curr->iface);
			goto error;
		}
		control = devm_kzalloc(module->dev, sizeof(struct
							   gbaudio_control),
				      GFP_KERNEL);
		if (!control) {
			ret = -ENOMEM;
			goto error;
		}
		control->id = curr->id;
		/* Prefix dev_id to widget_name */
		strlcpy(temp_name, curr->name, NAME_SIZE);
		snprintf(curr->name, NAME_SIZE, "GB %d %s", module->dev_id,
			 temp_name);
		control->name = curr->name;
		if (curr->info.type == GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED) {
			struct gb_audio_enumerated *gbenum =
				&curr->info.value.enumerated;

			csize = offsetof(struct gb_audio_control, info);
			csize += offsetof(struct gb_audio_ctl_elem_info, value);
			csize += offsetof(struct gb_audio_enumerated, names);
			csize += le16_to_cpu(gbenum->names_length);
			control->texts = (const char * const *)
				gb_generate_enum_strings(module, gbenum);
			control->items = le32_to_cpu(gbenum->items);
		} else {
			csize = sizeof(struct gb_audio_control);
		}

		list_add(&control->list, &module->ctl_list);
		dev_dbg(module->dev, "%d:%s created of type %d\n", curr->id,
			curr->name, curr->info.type);
		curr = (void *)curr + csize;
	}
	module->controls = dapm_kctls;

	return 0;
error:
	list_for_each_entry_safe(control, _control, &module->ctl_list,
				 list) {
		list_del(&control->list);
		devm_kfree(module->dev, control);
	}
	devm_kfree(module->dev, dapm_kctls);
	return ret;
}

static int gbaudio_tplg_process_widgets(struct gbaudio_module_info *module,
					struct gb_audio_widget *widgets)
{
	int i, ret, w_size;
	struct snd_soc_dapm_widget *dapm_widgets;
	struct gb_audio_widget *curr;
	struct gbaudio_widget *widget, *_widget;
	size_t size;

	size = sizeof(struct snd_soc_dapm_widget) * module->num_dapm_widgets;
	dapm_widgets = devm_kzalloc(module->dev, size, GFP_KERNEL);
	if (!dapm_widgets)
		return -ENOMEM;

	curr = widgets;
	for (i = 0; i < module->num_dapm_widgets; i++) {
		ret = gbaudio_tplg_create_widget(module, &dapm_widgets[i],
						 curr, &w_size);
		if (ret) {
			dev_err(module->dev, "%s:%d type not supported\n",
				curr->name, curr->type);
			goto error;
		}
		widget = devm_kzalloc(module->dev, sizeof(struct
							   gbaudio_widget),
				      GFP_KERNEL);
		if (!widget) {
			ret = -ENOMEM;
			goto error;
		}
		widget->id = curr->id;
		widget->name = curr->name;
		list_add(&widget->list, &module->widget_list);
		curr = (void *)curr + w_size;
	}
	module->dapm_widgets = dapm_widgets;

	return 0;

error:
	list_for_each_entry_safe(widget, _widget, &module->widget_list,
				 list) {
		list_del(&widget->list);
		devm_kfree(module->dev, widget);
	}
	devm_kfree(module->dev, dapm_widgets);
	return ret;
}

static int gbaudio_tplg_process_routes(struct gbaudio_module_info *module,
				       struct gb_audio_route *routes)
{
	int i, ret;
	struct snd_soc_dapm_route *dapm_routes;
	struct gb_audio_route *curr;
	size_t size;

	size = sizeof(struct snd_soc_dapm_route) * module->num_dapm_routes;
	dapm_routes = devm_kzalloc(module->dev, size, GFP_KERNEL);
	if (!dapm_routes)
		return -ENOMEM;

	module->dapm_routes = dapm_routes;
	curr = routes;

	for (i = 0; i < module->num_dapm_routes; i++) {
		dapm_routes->sink =
			gbaudio_map_widgetid(module, curr->destination_id);
		if (!dapm_routes->sink) {
			dev_err(module->dev, "%d:%d:%d:%d - Invalid sink\n",
				curr->source_id, curr->destination_id,
				curr->control_id, curr->index);
			ret = -EINVAL;
			goto error;
		}
		dapm_routes->source =
			gbaudio_map_widgetid(module, curr->source_id);
		if (!dapm_routes->source) {
			dev_err(module->dev, "%d:%d:%d:%d - Invalid source\n",
				curr->source_id, curr->destination_id,
				curr->control_id, curr->index);
			ret = -EINVAL;
			goto error;
		}
		dapm_routes->control =
			gbaudio_map_controlid(module,
					      curr->control_id,
					      curr->index);
		if ((curr->control_id !=  GBAUDIO_INVALID_ID) &&
		    !dapm_routes->control) {
			dev_err(module->dev, "%d:%d:%d:%d - Invalid control\n",
				curr->source_id, curr->destination_id,
				curr->control_id, curr->index);
			ret = -EINVAL;
			goto error;
		}
		dev_dbg(module->dev, "Route {%s, %s, %s}\n", dapm_routes->sink,
			(dapm_routes->control) ? dapm_routes->control : "NULL",
			dapm_routes->source);
		dapm_routes++;
		curr++;
	}

	return 0;

error:
	devm_kfree(module->dev, module->dapm_routes);
	return ret;
}

static int gbaudio_tplg_process_header(struct gbaudio_module_info *module,
				       struct gb_audio_topology *tplg_data)
{
	/* fetch no. of kcontrols, widgets & routes */
	module->num_controls = tplg_data->num_controls;
	module->num_dapm_widgets = tplg_data->num_widgets;
	module->num_dapm_routes = tplg_data->num_routes;

	/* update block offset */
	module->dai_offset = (unsigned long)&tplg_data->data;
	module->control_offset = module->dai_offset +
					le32_to_cpu(tplg_data->size_dais);
	module->widget_offset = module->control_offset +
					le32_to_cpu(tplg_data->size_controls);
	module->route_offset = module->widget_offset +
					le32_to_cpu(tplg_data->size_widgets);

	dev_dbg(module->dev, "DAI offset is 0x%lx\n", module->dai_offset);
	dev_dbg(module->dev, "control offset is %lx\n",
		module->control_offset);
	dev_dbg(module->dev, "widget offset is %lx\n", module->widget_offset);
	dev_dbg(module->dev, "route offset is %lx\n", module->route_offset);

	return 0;
}

int gbaudio_tplg_parse_data(struct gbaudio_module_info *module,
			    struct gb_audio_topology *tplg_data)
{
	int ret;
	struct gb_audio_control *controls;
	struct gb_audio_widget *widgets;
	struct gb_audio_route *routes;
	unsigned int jack_type;

	if (!tplg_data)
		return -EINVAL;

	ret = gbaudio_tplg_process_header(module, tplg_data);
	if (ret) {
		dev_err(module->dev, "%d: Error in parsing topology header\n",
			ret);
		return ret;
	}

	/* process control */
	controls = (struct gb_audio_control *)module->control_offset;
	ret = gbaudio_tplg_process_kcontrols(module, controls);
	if (ret) {
		dev_err(module->dev,
			"%d: Error in parsing controls data\n", ret);
		return ret;
	}
	dev_dbg(module->dev, "Control parsing finished\n");

	/* process widgets */
	widgets = (struct gb_audio_widget *)module->widget_offset;
	ret = gbaudio_tplg_process_widgets(module, widgets);
	if (ret) {
		dev_err(module->dev,
			"%d: Error in parsing widgets data\n", ret);
		return ret;
	}
	dev_dbg(module->dev, "Widget parsing finished\n");

	/* process route */
	routes = (struct gb_audio_route *)module->route_offset;
	ret = gbaudio_tplg_process_routes(module, routes);
	if (ret) {
		dev_err(module->dev,
			"%d: Error in parsing routes data\n", ret);
		return ret;
	}
	dev_dbg(module->dev, "Route parsing finished\n");

	/* parse jack capabilities */
	jack_type = le32_to_cpu(tplg_data->jack_type);
	if (jack_type) {
		module->jack_mask = jack_type & GBCODEC_JACK_MASK;
		module->button_mask = jack_type & GBCODEC_JACK_BUTTON_MASK;
	}

	return ret;
}

void gbaudio_tplg_release(struct gbaudio_module_info *module)
{
	struct gbaudio_control *control, *_control;
	struct gbaudio_widget *widget, *_widget;

	if (!module->topology)
		return;

	/* release kcontrols */
	list_for_each_entry_safe(control, _control, &module->ctl_list,
				 list) {
		list_del(&control->list);
		devm_kfree(module->dev, control);
	}
	if (module->controls)
		devm_kfree(module->dev, module->controls);

	/* release widget controls */
	list_for_each_entry_safe(control, _control, &module->widget_ctl_list,
				 list) {
		list_del(&control->list);
		devm_kfree(module->dev, control);
	}

	/* release widgets */
	list_for_each_entry_safe(widget, _widget, &module->widget_list,
				 list) {
		list_del(&widget->list);
		devm_kfree(module->dev, widget);
	}
	if (module->dapm_widgets)
		devm_kfree(module->dev, module->dapm_widgets);

	/* release routes */
	if (module->dapm_routes)
		devm_kfree(module->dev, module->dapm_routes);
}
