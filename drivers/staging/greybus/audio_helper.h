/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Greybus Audio Sound SoC helper APIs
 */

#ifndef __LINUX_GBAUDIO_HELPER_H
#define __LINUX_GBAUDIO_HELPER_H

int gbaudio_dapm_link_component_dai_widgets(struct snd_soc_card *card,
					    struct snd_soc_dapm_context *dapm);
int gbaudio_dapm_free_controls(struct snd_soc_dapm_context *dapm,
			       const struct snd_soc_dapm_widget *widget,
			       int num);
int gbaudio_remove_component_controls(struct snd_soc_component *component,
				      const struct snd_kcontrol_new *controls,
				      unsigned int num_controls);
#endif
