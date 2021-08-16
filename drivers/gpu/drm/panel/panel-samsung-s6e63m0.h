/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _PANEL_SAMSUNG_S6E63M0_H
#define _PANEL_SAMSUNG_S6E63M0_H

/* Manufacturer Command Set */
#define MCS_ELVSS_ON		0xb1
#define MCS_TEMP_SWIRE		0xb2
#define MCS_PENTILE_1		0xb3
#define MCS_PENTILE_2		0xb4
#define MCS_GAMMA_DELTA_Y_RED	0xb5
#define MCS_GAMMA_DELTA_X_RED	0xb6
#define MCS_GAMMA_DELTA_Y_GREEN	0xb7
#define MCS_GAMMA_DELTA_X_GREEN	0xb8
#define MCS_GAMMA_DELTA_Y_BLUE	0xb9
#define MCS_GAMMA_DELTA_X_BLUE	0xba
#define MCS_MIECTL1		0xc0
#define MCS_BCMODE		0xc1
#define MCS_ERROR_CHECK		0xd5
#define MCS_READ_ID1		0xda
#define MCS_READ_ID2		0xdb
#define MCS_READ_ID3		0xdc
#define MCS_LEVEL_2_KEY		0xf0
#define MCS_MTP_KEY		0xf1
#define MCS_DISCTL		0xf2
#define MCS_SRCCTL		0xf6
#define MCS_IFCTL		0xf7
#define MCS_PANELCTL		0xf8
#define MCS_PGAMMACTL		0xfa

int s6e63m0_probe(struct device *dev, void *trsp,
		  int (*dcs_read)(struct device *dev, void *trsp,
				  const u8 cmd, u8 *val),
		  int (*dcs_write)(struct device *dev, void *trsp,
				   const u8 *data,
				   size_t len),
		  bool dsi_mode);
int s6e63m0_remove(struct device *dev);

#endif /* _PANEL_SAMSUNG_S6E63M0_H */
