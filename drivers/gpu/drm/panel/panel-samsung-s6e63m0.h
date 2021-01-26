/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _PANEL_SAMSUNG_S6E63M0_H
#define _PANEL_SAMSUNG_S6E63M0_H

int s6e63m0_probe(struct device *dev,
		  int (*dcs_read)(struct device *dev, const u8 cmd, u8 *val),
		  int (*dcs_write)(struct device *dev, const u8 *data,
				   size_t len),
		  bool dsi_mode);
int s6e63m0_remove(struct device *dev);

#endif /* _PANEL_SAMSUNG_S6E63M0_H */
