// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus Audio Sound SoC helper APIs
 */

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include "audio_helper.h"

#define gbaudio_dapm_for_each_direction(dir) \
	for ((dir) = SND_SOC_DAPM_DIR_IN; (dir) <= SND_SOC_DAPM_DIR_OUT; \
		(dir)++)

static void gbaudio_dapm_link_dai_widget(struct snd_soc_dapm_widget *dai_w,
					 struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_widget *src, *sink;
	struct snd_soc_dai *dai = dai_w->priv;

	/* ...find all widgets with the same stream and link them */
	list_for_each_entry(w, &card->widgets, list) {
		if (w->dapm != dai_w->dapm)
			continue;

		switch (w->id) {
		case snd_soc_dapm_dai_in:
		case snd_soc_dapm_dai_out:
			continue;
		default:
			break;
		}

		if (!w->sname || !strstr(w->sname, dai_w->sname))
			continue;

		/*
		 * check if widget is already linked,
		 * if (w->linked)
		 *	return;
		 */

		if (dai_w->id == snd_soc_dapm_dai_in) {
			src = dai_w;
			sink = w;
		} else {
			src = w;
			sink = dai_w;
		}
		dev_dbg(dai->dev, "%s -> %s\n", src->name, sink->name);
		/* Add the DAPM path and set widget's linked status
		 * snd_soc_dapm_add_path(w->dapm, src, sink, NULL, NULL);
		 * w->linked = 1;
		 */
	}
}

int gbaudio_dapm_link_component_dai_widgets(struct snd_soc_card *card,
					    struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_widget *dai_w;

	/* For each DAI widget... */
	list_for_each_entry(dai_w, &card->widgets, list) {
		if (dai_w->dapm != dapm)
			continue;
		switch (dai_w->id) {
		case snd_soc_dapm_dai_in:
		case snd_soc_dapm_dai_out:
			break;
		default:
			continue;
		}
		gbaudio_dapm_link_dai_widget(dai_w, card);
	}

	return 0;
}

static void gbaudio_dapm_free_path(struct snd_soc_dapm_path *path)
{
	list_del(&path->list_node[SND_SOC_DAPM_DIR_IN]);
	list_del(&path->list_node[SND_SOC_DAPM_DIR_OUT]);
	list_del(&path->list_kcontrol);
	list_del(&path->list);
	kfree(path);
}

static void gbaudio_dapm_free_widget(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p, *next_p;
	enum snd_soc_dapm_direction dir;

	list_del(&w->list);
	/*
	 * remove source and sink paths associated to this widget.
	 * While removing the path, remove reference to it from both
	 * source and sink widgets so that path is removed only once.
	 */
	gbaudio_dapm_for_each_direction(dir) {
		snd_soc_dapm_widget_for_each_path_safe(w, dir, p, next_p)
			gbaudio_dapm_free_path(p);
	}

	kfree(w->kcontrols);
	kfree_const(w->name);
	kfree_const(w->sname);
	kfree(w);
}

int gbaudio_dapm_free_controls(struct snd_soc_dapm_context *dapm,
			       const struct snd_soc_dapm_widget *widget,
			       int num)
{
	int i;
	struct snd_soc_dapm_widget *w, *tmp_w;

	mutex_lock(&dapm->card->dapm_mutex);
	for (i = 0; i < num; i++) {
		/* below logic can be optimized to identify widget pointer */
		w = NULL;
		list_for_each_entry(tmp_w, &dapm->card->widgets, list) {
			if (tmp_w->dapm == dapm &&
			    !strcmp(tmp_w->name, widget->name)) {
				w = tmp_w;
				break;
			}
		}
		if (!w) {
			dev_err(dapm->dev, "%s: widget not found\n",
				widget->name);
			widget++;
			continue;
		}
		widget++;
		gbaudio_dapm_free_widget(w);
	}
	mutex_unlock(&dapm->card->dapm_mutex);
	return 0;
}

static int gbaudio_remove_controls(struct snd_card *card, struct device *dev,
				   const struct snd_kcontrol_new *controls,
				   int num_controls, const char *prefix)
{
	int i, err;

	for (i = 0; i < num_controls; i++) {
		const struct snd_kcontrol_new *control = &controls[i];
		struct snd_ctl_elem_id id;
		struct snd_kcontrol *kctl;

		if (prefix)
			snprintf(id.name, sizeof(id.name), "%s %s", prefix,
				 control->name);
		else
			strscpy(id.name, control->name, sizeof(id.name));
		id.numid = 0;
		id.iface = control->iface;
		id.device = control->device;
		id.subdevice = control->subdevice;
		id.index = control->index;
		kctl = snd_ctl_find_id(card, &id);
		if (!kctl) {
			dev_err(dev, "Failed to find %s\n", control->name);
			continue;
		}
		err = snd_ctl_remove(card, kctl);
		if (err < 0) {
			dev_err(dev, "%d: Failed to remove %s\n", err,
				control->name);
			continue;
		}
	}
	return 0;
}

int gbaudio_remove_component_controls(struct snd_soc_component *component,
				      const struct snd_kcontrol_new *controls,
				      unsigned int num_controls)
{
	struct snd_card *card = component->card->snd_card;
	int err;

	down_write(&card->controls_rwsem);
	err = gbaudio_remove_controls(card, component->dev, controls,
				      num_controls, component->name_prefix);
	up_write(&card->controls_rwsem);
	return err;
}
