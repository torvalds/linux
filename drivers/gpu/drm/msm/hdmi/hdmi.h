/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __HDMI_CONNECTOR_H__
#define __HDMI_CONNECTOR_H__

#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "msm_drv.h"
#include "hdmi.xml.h"


struct hdmi_phy;

struct hdmi {
	struct drm_device *dev;
	struct platform_device *pdev;

	void __iomem *mmio;

	struct regulator *mvs;        /* HDMI_5V */
	struct regulator *mpp0;       /* External 5V */

	struct clk *clk;
	struct clk *m_pclk;
	struct clk *s_pclk;

	struct hdmi_phy *phy;
	struct i2c_adapter *i2c;
	struct drm_connector *connector;

	bool hdmi_mode;               /* are we in hdmi mode? */

	int irq;
};

/* platform config data (ie. from DT, or pdata) */
struct hdmi_platform_config {
	struct hdmi_phy *(*phy_init)(struct hdmi *hdmi);
	int ddc_clk_gpio, ddc_data_gpio, hpd_gpio, pmic_gpio;
};

void hdmi_set_mode(struct hdmi *hdmi, bool power_on);
void hdmi_destroy(struct hdmi *hdmi);
int hdmi_init(struct hdmi *hdmi, struct drm_device *dev,
		struct drm_connector *connector);

static inline void hdmi_write(struct hdmi *hdmi, u32 reg, u32 data)
{
	msm_writel(data, hdmi->mmio + reg);
}

static inline u32 hdmi_read(struct hdmi *hdmi, u32 reg)
{
	return msm_readl(hdmi->mmio + reg);
}

/*
 * The phy appears to be different, for example between 8960 and 8x60,
 * so split the phy related functions out and load the correct one at
 * runtime:
 */

struct hdmi_phy_funcs {
	void (*destroy)(struct hdmi_phy *phy);
	void (*reset)(struct hdmi_phy *phy);
	void (*powerup)(struct hdmi_phy *phy, unsigned long int pixclock);
	void (*powerdown)(struct hdmi_phy *phy);
};

struct hdmi_phy {
	const struct hdmi_phy_funcs *funcs;
};

/*
 * phy can be different on different generations:
 */
struct hdmi_phy *hdmi_phy_8960_init(struct hdmi *hdmi);
struct hdmi_phy *hdmi_phy_8x60_init(struct hdmi *hdmi);

/*
 * hdmi connector:
 */

void hdmi_connector_irq(struct drm_connector *connector);

/*
 * i2c adapter for ddc:
 */

void hdmi_i2c_irq(struct i2c_adapter *i2c);
void hdmi_i2c_destroy(struct i2c_adapter *i2c);
struct i2c_adapter *hdmi_i2c_init(struct hdmi *hdmi);

#endif /* __HDMI_CONNECTOR_H__ */
