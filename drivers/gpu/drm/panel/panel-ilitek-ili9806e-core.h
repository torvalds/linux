/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _PANEL_ILITEK_ILI9806E_CORE_H
#define _PANEL_ILITEK_ILI9806E_CORE_H

void *ili9806e_get_transport(struct drm_panel *panel);
int ili9806e_power_off(struct device *dev);
int ili9806e_power_on(struct device *dev);

int ili9806e_probe(struct device *dev, void *transport,
		   const struct drm_panel_funcs *funcs,
		   int connector_type);
void ili9806e_remove(struct device *dev);

#endif /* _PANEL_ILITEK_ILI9806E_CORE_H */
