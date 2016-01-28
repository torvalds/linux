/*
 * Greybus audio driver
 * Copyright 2015-2016 Google Inc.
 * Copyright 2015-2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "audio_codec.h"
#include "greybus_protocols.h"

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

static const char *gbaudio_map_controlid(struct gbaudio_codec_info *gbcodec,
					   __u8 control_id, __u8 index)
{
	struct gbaudio_control *control;

	if (control_id == GBAUDIO_INVALID_ID)
		return NULL;

	list_for_each_entry(control, &gbcodec->codec_ctl_list, list) {
		if (control->id == control_id) {
			if (index == GBAUDIO_INVALID_ID)
				return control->name;
			return control->texts[index];
		}
	}
	list_for_each_entry(control, &gbcodec->widget_ctl_list, list) {
		if (control->id == control_id) {
			if (index == GBAUDIO_INVALID_ID)
				return control->name;
			return control->texts[index];
		}
	}
	return NULL;
}

static int gbaudio_map_widgetname(struct gbaudio_codec_info *gbcodec,
					  const char *name)
{
	struct gbaudio_widget *widget;
	char widget_name[NAME_SIZE];
	char prefix_name[NAME_SIZE];

	snprintf(prefix_name, NAME_SIZE, "GB %d ", gbcodec->dev_id);
	if (strncmp(name, prefix_name, strlen(prefix_name)))
		return -EINVAL;

	strlcpy(widget_name, name+strlen(prefix_name), NAME_SIZE);
	dev_dbg(gbcodec->dev, "widget_name:%s, truncated widget_name:%s\n",
		name, widget_name);

	list_for_each_entry(widget, &gbcodec->widget_list, list) {
		if (!strncmp(widget->name, widget_name, NAME_SIZE))
			return widget->id;
	}
	return -EINVAL;
}

static const char *gbaudio_map_widgetid(struct gbaudio_codec_info *gbcodec,
					  __u8 widget_id)
{
	struct gbaudio_widget *widget;

	list_for_each_entry(widget, &gbcodec->widget_list, list) {
		if (widget->id == widget_id)
			return widget->name;
	}
	return NULL;
}

static int gbcodec_mixer_ctl_info(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_info *uinfo)
{
	unsigned int max;
	const char *name;
	struct gbaudio_ctl_pvt *data;
	struct gb_audio_ctl_elem_info *info;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct gbaudio_codec_info *gbcodec = snd_soc_codec_get_drvdata(codec);

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;

	if (!info) {
		dev_err(gbcodec->dev, "NULL info for %s\n", uinfo->id.name);
		return -EINVAL;
	}

	/* update uinfo */
	uinfo->access = data->access;
	uinfo->count = data->vcount;
	uinfo->type = (snd_ctl_elem_type_t)info->type;

	switch (info->type) {
	case GB_AUDIO_CTL_ELEM_TYPE_BOOLEAN:
	case GB_AUDIO_CTL_ELEM_TYPE_INTEGER:
		uinfo->value.integer.min = info->value.integer.min;
		uinfo->value.integer.max = info->value.integer.max;
		break;
	case GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED:
		max = info->value.enumerated.items;
		uinfo->value.enumerated.items = max;
		if (uinfo->value.enumerated.item > max - 1)
			uinfo->value.enumerated.item = max - 1;
		name = gbaudio_map_controlid(gbcodec, data->ctl_id,
					     uinfo->value.enumerated.item);
		strlcpy(uinfo->value.enumerated.name, name, NAME_SIZE);
		break;
	default:
		dev_err(codec->dev, "Invalid type: %d for %s:kcontrol\n",
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
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct gbaudio_codec_info *gb = snd_soc_codec_get_drvdata(codec);

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;

	ret = gb_audio_gb_get_control(gb->mgmt_connection, data->ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);
	if (ret) {
		dev_err(codec->dev, "%d:Error in %s for %s\n", ret, __func__,
			kcontrol->id.name);
		return ret;
	}

	/* update ucontrol */
	switch (info->type) {
	case GB_AUDIO_CTL_ELEM_TYPE_BOOLEAN:
	case GB_AUDIO_CTL_ELEM_TYPE_INTEGER:
		ucontrol->value.integer.value[0] =
			gbvalue.value.integer_value[0];
		if (data->vcount == 2)
			ucontrol->value.integer.value[1] =
				gbvalue.value.integer_value[1];
		break;
	case GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED:
		ucontrol->value.enumerated.item[0] =
			gbvalue.value.enumerated_item[0];
		if (data->vcount == 2)
			ucontrol->value.enumerated.item[1] =
				gbvalue.value.enumerated_item[1];
		break;
	default:
		dev_err(codec->dev, "Invalid type: %d for %s:kcontrol\n",
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
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct gbaudio_codec_info *gb = snd_soc_codec_get_drvdata(codec);

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;

	/* update ucontrol */
	switch (info->type) {
	case GB_AUDIO_CTL_ELEM_TYPE_BOOLEAN:
	case GB_AUDIO_CTL_ELEM_TYPE_INTEGER:
		gbvalue.value.integer_value[0] =
			ucontrol->value.integer.value[0];
		if (data->vcount == 2)
			gbvalue.value.integer_value[1] =
				ucontrol->value.integer.value[1];
		break;
	case GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED:
		gbvalue.value.enumerated_item[0] =
			ucontrol->value.enumerated.item[0];
		if (data->vcount == 2)
			gbvalue.value.enumerated_item[1] =
				ucontrol->value.enumerated.item[1];
		break;
	default:
		dev_err(codec->dev, "Invalid type: %d for %s:kcontrol\n",
			info->type, kcontrol->id.name);
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	ret = gb_audio_gb_set_control(gb->mgmt_connection, data->ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);
	if (ret) {
		dev_err(codec->dev, "%d:Error in %s for %s\n", ret, __func__,
			kcontrol->id.name);
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
	platform_max = info->value.integer.max;
	platform_min = info->value.integer.min;

	if (platform_max == 1 &&
	    !strnstr(kcontrol->id.name, " Volume", NAME_SIZE))
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = data->vcount;
	uinfo->value.integer.min = 0;
	if (info->value.integer.min < 0 &&
	    (uinfo->type == SNDRV_CTL_ELEM_TYPE_INTEGER))
		uinfo->value.integer.max = platform_max - platform_min;
	else
		uinfo->value.integer.max = platform_max;

	return 0;
}

static int gbcodec_mixer_dapm_ctl_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int ret;
	struct gb_audio_ctl_elem_info *info;
	struct gbaudio_ctl_pvt *data;
	struct gb_audio_ctl_elem_value gbvalue;
	struct snd_soc_dapm_widget_list *wlist = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget *widget = wlist->widgets[0];
	struct snd_soc_codec *codec = widget->codec;
	struct gbaudio_codec_info *gb = snd_soc_codec_get_drvdata(codec);

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;

	if (data->vcount == 2)
		dev_warn(widget->dapm->dev,
			 "GB: Control '%s' is stereo, which is not supported\n",
			 kcontrol->id.name);

	ret = gb_audio_gb_get_control(gb->mgmt_connection, data->ctl_id,
				      GB_AUDIO_INVALID_INDEX, &gbvalue);
	if (ret) {
		dev_err(codec->dev, "%d:Error in %s for %s\n", ret, __func__,
			kcontrol->id.name);
		return ret;
	}
	/* update ucontrol */
	ucontrol->value.integer.value[0] = gbvalue.value.integer_value[0];

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
	struct snd_soc_dapm_widget_list *wlist = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget *widget = wlist->widgets[0];
	struct snd_soc_codec *codec = widget->codec;
	struct gbaudio_codec_info *gb = snd_soc_codec_get_drvdata(codec);

	data = (struct gbaudio_ctl_pvt *)kcontrol->private_value;
	info = (struct gb_audio_ctl_elem_info *)data->info;

	if (data->vcount == 2)
		dev_warn(widget->dapm->dev,
			 "GB: Control '%s' is stereo, which is not supported\n",
			 kcontrol->id.name);

	max = info->value.integer.max;
	mask = (1 << fls(max)) - 1;
	val = (ucontrol->value.integer.value[0] & mask);
	connect = !!val;

	/* update ucontrol */
	if (gbvalue.value.integer_value[0] != val) {
		for (wi = 0; wi < wlist->num_widgets; wi++) {
			widget = wlist->widgets[wi];

			widget->value = val;
			widget->dapm->update = NULL;
			snd_soc_dapm_mixer_update_power(widget, kcontrol,
							connect);
		}
		gbvalue.value.integer_value[0] =
			ucontrol->value.integer.value[0];
		ret = gb_audio_gb_set_control(gb->mgmt_connection,
					      data->ctl_id,
					      GB_AUDIO_INVALID_INDEX, &gbvalue);
		if (ret) {
			dev_err(codec->dev,
				"%d:Error in %s for %s\n", ret, __func__,
				kcontrol->id.name);
		}
	}

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

static int gbaudio_tplg_create_kcontrol(struct gbaudio_codec_info *gb,
					struct snd_kcontrol_new *kctl,
					struct gb_audio_control *ctl)
{
	struct gbaudio_ctl_pvt *ctldata;

	switch (ctl->iface) {
	case SNDRV_CTL_ELEM_IFACE_MIXER:
		ctldata = devm_kzalloc(gb->dev, sizeof(struct gbaudio_ctl_pvt),
				       GFP_KERNEL);
		if (!ctldata)
			return -ENOMEM;
		ctldata->ctl_id = ctl->id;
		ctldata->data_cport = ctl->data_cport;
		ctldata->access = ctl->access;
		ctldata->vcount = ctl->count_values;
		ctldata->info = &ctl->info;
		*kctl = (struct snd_kcontrol_new)
			SOC_MIXER_GB(ctl->name, ctl->count, ctldata);
		ctldata = NULL;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(gb->dev, "%s:%d control created\n", ctl->name, ctl->id);
	return 0;
}

static const char * const gbtexts[] = {"Stereo", "Left", "Right"};

static const SOC_ENUM_SINGLE_DECL(
	gbcodec_apb1_rx_enum, GBCODEC_APB1_MUX_REG, 0, gbtexts);

static const SOC_ENUM_SINGLE_DECL(
	gbcodec_mic_enum, GBCODEC_APB1_MUX_REG, 4, gbtexts);

static int gbaudio_tplg_create_enum_ctl(struct gbaudio_codec_info *gb,
					struct snd_kcontrol_new *kctl,
					struct gb_audio_control *ctl)
{
	switch (ctl->id) {
	case 8:
		*kctl = (struct snd_kcontrol_new)
			SOC_DAPM_ENUM(ctl->name, gbcodec_apb1_rx_enum);
		break;
	case 9:
		*kctl = (struct snd_kcontrol_new)
			SOC_DAPM_ENUM(ctl->name, gbcodec_mic_enum);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int gbaudio_tplg_create_mixer_ctl(struct gbaudio_codec_info *gb,
					     struct snd_kcontrol_new *kctl,
					     struct gb_audio_control *ctl)
{
	struct gbaudio_ctl_pvt *ctldata;

	ctldata = devm_kzalloc(gb->dev, sizeof(struct gbaudio_ctl_pvt),
			       GFP_KERNEL);
	if (!ctldata)
		return -ENOMEM;
	ctldata->ctl_id = ctl->id;
	ctldata->data_cport = ctl->data_cport;
	ctldata->access = ctl->access;
	ctldata->vcount = ctl->count_values;
	ctldata->info = &ctl->info;
	*kctl = (struct snd_kcontrol_new)
		SOC_DAPM_MIXER_GB(ctl->name, ctl->count, ctldata);

	return 0;
}

static int gbaudio_tplg_create_wcontrol(struct gbaudio_codec_info *gb,
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
	struct snd_soc_codec *codec = w->codec;
	struct gbaudio_codec_info *gbcodec = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s %s %d\n", __func__, w->name, event);

	/* map name to widget id */
	wid = gbaudio_map_widgetname(gbcodec, w->name);
	if (wid < 0) {
		dev_err(codec->dev, "Invalid widget name:%s\n", w->name);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = gb_audio_gb_enable_widget(gbcodec->mgmt_connection, wid);
		break;
	case SND_SOC_DAPM_POST_PMD:
		ret = gb_audio_gb_disable_widget(gbcodec->mgmt_connection, wid);
		break;
	}
	if (ret)
		dev_err(codec->dev, "%d: widget, event:%d failed:%d\n", wid,
			event, ret);
	return ret;
}

static int gbaudio_tplg_create_widget(struct gbaudio_codec_info *gbcodec,
				      struct snd_soc_dapm_widget *dw,
				      struct gb_audio_widget *w)
{
	int i, ret;
	struct snd_kcontrol_new *widget_kctls;
	struct gb_audio_control *curr;
	struct gbaudio_control *control, *_control;
	size_t size;

	ret = gbaudio_validate_kcontrol_count(w);
	if (ret) {
		dev_err(gbcodec->dev, "Inavlid kcontrol count=%d for %s\n",
			w->ncontrols, w->name);
		return ret;
	}

	/* allocate memory for kcontrol */
	if (w->ncontrols) {
		size = sizeof(struct snd_kcontrol_new) * w->ncontrols;
		widget_kctls = devm_kzalloc(gbcodec->dev, size, GFP_KERNEL);
		if (!widget_kctls)
			return -ENOMEM;
	}

	/* create relevant kcontrols */
	for (i = 0; i < w->ncontrols; i++) {
		curr = &w->ctl[i];
		ret = gbaudio_tplg_create_wcontrol(gbcodec, &widget_kctls[i],
						   curr);
		if (ret) {
			dev_err(gbcodec->dev,
				"%s:%d type widget_ctl not supported\n",
				curr->name, curr->iface);
			goto error;
		}
		control = devm_kzalloc(gbcodec->dev,
				       sizeof(struct gbaudio_control),
				       GFP_KERNEL);
		if (!control) {
			ret = -ENOMEM;
			goto error;
		}
		control->id = curr->id;
		control->name = curr->name;
		if (curr->info.type == GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED)
			control->texts = (const char * const *)
				curr->info.value.enumerated.names;
		list_add(&control->list, &gbcodec->widget_ctl_list);
		dev_dbg(gbcodec->dev, "%s: control of type %d created\n",
			widget_kctls[i].name, widget_kctls[i].iface);
	}

	switch (w->type) {
	case snd_soc_dapm_spk:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_SPK(w->name, gbcodec_event_spk);
		break;
	case snd_soc_dapm_hp:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_HP(w->name, gbcodec_event_hp);
		break;
	case snd_soc_dapm_mic:
		*dw = (struct snd_soc_dapm_widget)
			SND_SOC_DAPM_MIC(w->name, gbcodec_event_int_mic);
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
					    widget_kctls, gbaudio_widget_event,
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

	dev_dbg(gbcodec->dev, "%s: widget of type %d created\n", dw->name,
		dw->id);
	return 0;
error:
	list_for_each_entry_safe(control, _control, &gbcodec->widget_ctl_list,
				 list) {
		list_del(&control->list);
		devm_kfree(gbcodec->dev, control);
	}
	return ret;
}

static int gbaudio_tplg_create_dai(struct gbaudio_codec_info *gbcodec,
				   struct snd_soc_dai_driver *gb_dai,
				   struct gb_audio_dai *dai)
{
	/*
	 * do not update name here,
	 * append dev_id before assigning it here
	 */

	gb_dai->playback.stream_name = dai->playback.stream_name;
	gb_dai->playback.channels_min = dai->playback.chan_min;
	gb_dai->playback.channels_max = dai->playback.chan_max;
	gb_dai->playback.formats = dai->playback.formats;
	gb_dai->playback.rates = dai->playback.rates;
	gb_dai->playback.sig_bits = dai->playback.sig_bits;

	gb_dai->capture.stream_name = dai->capture.stream_name;
	gb_dai->capture.channels_min = dai->capture.chan_min;
	gb_dai->capture.channels_max = dai->capture.chan_max;
	gb_dai->capture.formats = dai->capture.formats;
	gb_dai->capture.rates = dai->capture.rates;
	gb_dai->capture.sig_bits = dai->capture.sig_bits;

	return 0;
}

static int gbaudio_tplg_process_kcontrols(struct gbaudio_codec_info *gbcodec,
				   struct gb_audio_control *controls)
{
	int i, ret;
	struct snd_kcontrol_new *dapm_kctls;
	struct gb_audio_control *curr;
	struct gbaudio_control *control, *_control;
	size_t size;

	size = sizeof(struct snd_kcontrol_new) * gbcodec->num_kcontrols;
	dapm_kctls = devm_kzalloc(gbcodec->dev, size, GFP_KERNEL);
	if (!dapm_kctls)
		return -ENOMEM;

	curr = controls;
	for (i = 0; i < gbcodec->num_kcontrols; i++) {
		ret = gbaudio_tplg_create_kcontrol(gbcodec, &dapm_kctls[i],
						   curr);
		if (ret) {
			dev_err(gbcodec->dev, "%s:%d type not supported\n",
				curr->name, curr->iface);
			goto error;
		}
		control = devm_kzalloc(gbcodec->dev, sizeof(struct
							   gbaudio_control),
				      GFP_KERNEL);
		if (!control) {
			ret = -ENOMEM;
			goto error;
		}
		control->id = curr->id;
		control->name = curr->name;
		if (curr->info.type == GB_AUDIO_CTL_ELEM_TYPE_ENUMERATED)
			control->texts = (const char * const *)
				curr->info.value.enumerated.names;
		list_add(&control->list, &gbcodec->codec_ctl_list);
		dev_dbg(gbcodec->dev, "%d:%s created of type %d\n", curr->id,
			curr->name, curr->info.type);
		curr++;
	}
	gbcodec->kctls = dapm_kctls;

	return 0;
error:
	list_for_each_entry_safe(control, _control, &gbcodec->codec_ctl_list,
				 list) {
		list_del(&control->list);
		devm_kfree(gbcodec->dev, control);
	}
	devm_kfree(gbcodec->dev, dapm_kctls);
	return ret;
}

static int gbaudio_tplg_process_widgets(struct gbaudio_codec_info *gbcodec,
				   struct gb_audio_widget *widgets)
{
	int i, ret, ncontrols;
	struct snd_soc_dapm_widget *dapm_widgets;
	struct gb_audio_widget *curr;
	struct gbaudio_widget *widget, *_widget;
	size_t size;

	size = sizeof(struct snd_soc_dapm_widget) * gbcodec->num_dapm_widgets;
	dapm_widgets = devm_kzalloc(gbcodec->dev, size, GFP_KERNEL);
	if (!dapm_widgets)
		return -ENOMEM;

	curr = widgets;
	for (i = 0; i < gbcodec->num_dapm_widgets; i++) {
		ret = gbaudio_tplg_create_widget(gbcodec, &dapm_widgets[i],
						 curr);
		if (ret) {
			dev_err(gbcodec->dev, "%s:%d type not supported\n",
				curr->name, curr->type);
			goto error;
		}
		widget = devm_kzalloc(gbcodec->dev, sizeof(struct
							   gbaudio_widget),
				      GFP_KERNEL);
		if (!widget) {
			ret = -ENOMEM;
			goto error;
		}
		widget->id = curr->id;
		widget->name = curr->name;
		list_add(&widget->list, &gbcodec->widget_list);
		ncontrols = curr->ncontrols;
		curr++;
		curr += ncontrols * sizeof(struct gb_audio_control);
	}
	gbcodec->widgets = dapm_widgets;

	return 0;

error:
	list_for_each_entry_safe(widget, _widget, &gbcodec->widget_list,
				 list) {
		list_del(&widget->list);
		devm_kfree(gbcodec->dev, widget);
	}
	devm_kfree(gbcodec->dev, dapm_widgets);
	return ret;
}

static int gbaudio_tplg_process_dais(struct gbaudio_codec_info *gbcodec,
				   struct gb_audio_dai *dais)
{
	int i, ret;
	struct snd_soc_dai_driver *gb_dais;
	struct gb_audio_dai *curr;
	size_t size;
	char dai_name[NAME_SIZE];
	struct gbaudio_dai *dai;

	size = sizeof(struct snd_soc_dai_driver) * gbcodec->num_dais;
	gb_dais = devm_kzalloc(gbcodec->dev, size, GFP_KERNEL);
	if (!gb_dais)
		return -ENOMEM;

	curr = dais;
	for (i = 0; i < gbcodec->num_dais; i++) {
		ret = gbaudio_tplg_create_dai(gbcodec, &gb_dais[i], curr);
		if (ret) {
			dev_err(gbcodec->dev, "%s failed to create\n",
				curr->name);
			goto error;
		}
		/* append dev_id to dai_name */
		snprintf(dai_name, NAME_SIZE, "%s.%d", curr->name,
			 gbcodec->dev_id);
		dai = gbaudio_find_dai(gbcodec, curr->data_cport, NULL);
		if (!dai)
			goto error;
		strlcpy(dai->name, dai_name, NAME_SIZE);
		dev_dbg(gbcodec->dev, "%s:DAI added\n", dai->name);
		gb_dais[i].name = dai->name;
		curr++;
	}
	gbcodec->dais = gb_dais;

	return 0;

error:
	devm_kfree(gbcodec->dev, gb_dais);
	return ret;
}

static int gbaudio_tplg_process_routes(struct gbaudio_codec_info *gbcodec,
				   struct gb_audio_route *routes)
{
	int i, ret;
	struct snd_soc_dapm_route *dapm_routes;
	struct gb_audio_route *curr;
	size_t size;

	size = sizeof(struct snd_soc_dapm_route) * gbcodec->num_dapm_routes;
	dapm_routes = devm_kzalloc(gbcodec->dev, size, GFP_KERNEL);
	if (!dapm_routes)
		return -ENOMEM;


	gbcodec->routes = dapm_routes;
	curr = routes;

	for (i = 0; i < gbcodec->num_dapm_routes; i++) {
		dapm_routes->sink =
			gbaudio_map_widgetid(gbcodec, curr->destination_id);
		if (!dapm_routes->sink) {
			dev_err(gbcodec->dev, "%d:%d:%d:%d - Invalid sink\n",
				curr->source_id, curr->destination_id,
				curr->control_id, curr->index);
			ret = -EINVAL;
			goto error;
		}
		dapm_routes->source =
			gbaudio_map_widgetid(gbcodec, curr->source_id);
		if (!dapm_routes->source) {
			dev_err(gbcodec->dev, "%d:%d:%d:%d - Invalid source\n",
				curr->source_id, curr->destination_id,
				curr->control_id, curr->index);
			ret = -EINVAL;
			goto error;
		}
		dapm_routes->control =
			gbaudio_map_controlid(gbcodec,
						      curr->control_id,
						      curr->index);
		if ((curr->control_id !=  GBAUDIO_INVALID_ID) &&
		    !dapm_routes->control) {
			dev_err(gbcodec->dev, "%d:%d:%d:%d - Invalid control\n",
				curr->source_id, curr->destination_id,
				curr->control_id, curr->index);
			ret = -EINVAL;
			goto error;
		}
		dev_dbg(gbcodec->dev, "Route {%s, %s, %s}\n", dapm_routes->sink,
			(dapm_routes->control) ? dapm_routes->control:"NULL",
			dapm_routes->source);
		dapm_routes++;
		curr++;
	}

	return 0;

error:
	devm_kfree(gbcodec->dev, dapm_routes);
	return ret;
}

static int gbaudio_tplg_process_header(struct gbaudio_codec_info *gbcodec,
				 struct gb_audio_topology *tplg_data)
{
	/* fetch no. of kcontrols, widgets & routes */
	gbcodec->num_dais = tplg_data->num_dais;
	gbcodec->num_kcontrols = tplg_data->num_controls;
	gbcodec->num_dapm_widgets = tplg_data->num_widgets;
	gbcodec->num_dapm_routes = tplg_data->num_routes;

	/* update block offset */
	gbcodec->dai_offset = (unsigned long)&tplg_data->data;
	gbcodec->control_offset = gbcodec->dai_offset + tplg_data->size_dais;
	gbcodec->widget_offset = gbcodec->control_offset +
		tplg_data->size_controls;
	gbcodec->route_offset = gbcodec->widget_offset +
		tplg_data->size_widgets;

	dev_dbg(gbcodec->dev, "DAI offset is 0x%lx\n", gbcodec->dai_offset);
	dev_dbg(gbcodec->dev, "control offset is %lx\n",
		gbcodec->control_offset);
	dev_dbg(gbcodec->dev, "widget offset is %lx\n", gbcodec->widget_offset);
	dev_dbg(gbcodec->dev, "route offset is %lx\n", gbcodec->route_offset);

	return 0;
}

int gbaudio_tplg_parse_data(struct gbaudio_codec_info *gbcodec,
			       struct gb_audio_topology *tplg_data)
{
	int ret;
	struct gb_audio_dai *dais;
	struct gb_audio_control *controls;
	struct gb_audio_widget *widgets;
	struct gb_audio_route *routes;

	if (!tplg_data)
		return -EINVAL;

	ret = gbaudio_tplg_process_header(gbcodec, tplg_data);
	if (ret) {
		dev_err(gbcodec->dev, "%d: Error in parsing topology header\n",
			ret);
		return ret;
	}

	/* process control */
	controls = (struct gb_audio_control *)gbcodec->control_offset;
	ret = gbaudio_tplg_process_kcontrols(gbcodec, controls);
	if (ret) {
		dev_err(gbcodec->dev,
			"%d: Error in parsing controls data\n", ret);
		return ret;
	}
	dev_dbg(gbcodec->dev, "Control parsing finished\n");

	/* process DAI */
	dais = (struct gb_audio_dai *)gbcodec->dai_offset;
	ret = gbaudio_tplg_process_dais(gbcodec, dais);
	if (ret) {
		dev_err(gbcodec->dev,
			"%d: Error in parsing DAIs data\n", ret);
		return ret;
	}
	dev_dbg(gbcodec->dev, "DAI parsing finished\n");

	/* process widgets */
	widgets = (struct gb_audio_widget *)gbcodec->widget_offset;
	ret = gbaudio_tplg_process_widgets(gbcodec, widgets);
	if (ret) {
		dev_err(gbcodec->dev,
			"%d: Error in parsing widgets data\n", ret);
		return ret;
	}
	dev_dbg(gbcodec->dev, "Widget parsing finished\n");

	/* process route */
	routes = (struct gb_audio_route *)gbcodec->route_offset;
	ret = gbaudio_tplg_process_routes(gbcodec, routes);
	if (ret) {
		dev_err(gbcodec->dev,
			"%d: Error in parsing routes data\n", ret);
		return ret;
	}
	dev_dbg(gbcodec->dev, "Route parsing finished\n");

	return ret;
}

void gbaudio_tplg_release(struct gbaudio_codec_info *gbcodec)
{
	struct gbaudio_control *control, *_control;
	struct gbaudio_widget *widget, *_widget;

	if (!gbcodec->topology)
		return;

	/* release kcontrols */
	list_for_each_entry_safe(control, _control, &gbcodec->codec_ctl_list,
				 list) {
		list_del(&control->list);
		devm_kfree(gbcodec->dev, control);
	}
	if (gbcodec->kctls)
		devm_kfree(gbcodec->dev, gbcodec->kctls);

	/* release widget controls */
	list_for_each_entry_safe(control, _control, &gbcodec->widget_ctl_list,
				 list) {
		list_del(&control->list);
		devm_kfree(gbcodec->dev, control);
	}

	/* release widgets */
	list_for_each_entry_safe(widget, _widget, &gbcodec->widget_list,
				 list) {
		list_del(&widget->list);
		devm_kfree(gbcodec->dev, widget);
	}
	if (gbcodec->widgets)
		devm_kfree(gbcodec->dev, gbcodec->widgets);

	/* release routes */
	if (gbcodec->routes)
		devm_kfree(gbcodec->dev, gbcodec->routes);
}
