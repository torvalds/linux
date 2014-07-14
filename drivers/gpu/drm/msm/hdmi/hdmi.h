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
#include <linux/hdmi.h>

#include "msm_drv.h"
#include "hdmi.xml.h"


struct hdmi_phy;
struct hdmi_platform_config;

struct hdmi_audio {
	bool enabled;
	struct hdmi_audio_infoframe infoframe;
	int rate;
};

struct hdmi {
	struct kref refcount;

	struct drm_device *dev;
	struct platform_device *pdev;

	const struct hdmi_platform_config *config;

	/* audio state: */
	struct hdmi_audio audio;

	/* video state: */
	bool power_on;
	unsigned long int pixclock;

	void __iomem *mmio;

	struct regulator *hpd_regs[2];
	struct regulator *pwr_regs[2];
	struct clk *hpd_clks[3];
	struct clk *pwr_clks[2];

	struct hdmi_phy *phy;
	struct i2c_adapter *i2c;
	struct drm_connector *connector;
	struct drm_bridge *bridge;

	/* the encoder we are hooked to (outside of hdmi block) */
	struct drm_encoder *encoder;

	bool hdmi_mode;               /* are we in hdmi mode? */

	int irq;
};

/* platform config data (ie. from DT, or pdata) */
struct hdmi_platform_config {
	struct hdmi_phy *(*phy_init)(struct hdmi *hdmi);
	const char *mmio_name;

	/* regulators that need to be on for hpd: */
	const char **hpd_reg_names;
	int hpd_reg_cnt;

	/* regulators that need to be on for screen pwr: */
	const char **pwr_reg_names;
	int pwr_reg_cnt;

	/* clks that need to be on for hpd: */
	const char **hpd_clk_names;
	const long unsigned *hpd_freq;
	int hpd_clk_cnt;

	/* clks that need to be on for screen pwr (ie pixel clk): */
	const char **pwr_clk_names;
	int pwr_clk_cnt;

	/* gpio's: */
	int ddc_clk_gpio, ddc_data_gpio, hpd_gpio, mux_en_gpio, mux_sel_gpio;

	/* older devices had their own irq, mdp5+ it is shared w/ mdp: */
	bool shared_irq;
};

void hdmi_set_mode(struct hdmi *hdmi, bool power_on);
void hdmi_destroy(struct kref *kref);

static inline void hdmi_write(struct hdmi *hdmi, u32 reg, u32 data)
{
	msm_writel(data, hdmi->mmio + reg);
}

static inline u32 hdmi_read(struct hdmi *hdmi, u32 reg)
{
	return msm_readl(hdmi->mmio + reg);
}

static inline struct hdmi * hdmi_reference(struct hdmi *hdmi)
{
	kref_get(&hdmi->refcount);
	return hdmi;
}

static inline void hdmi_unreference(struct hdmi *hdmi)
{
	kref_put(&hdmi->refcount, hdmi_destroy);
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

struct hdmi_phy *hdmi_phy_8960_init(struct hdmi *hdmi);
struct hdmi_phy *hdmi_phy_8x60_init(struct hdmi *hdmi);
struct hdmi_phy *hdmi_phy_8x74_init(struct hdmi *hdmi);

/*
 * audio:
 */

int hdmi_audio_update(struct hdmi *hdmi);
int hdmi_audio_info_setup(struct hdmi *hdmi, bool enabled,
	uint32_t num_of_channels, uint32_t channel_allocation,
	uint32_t level_shift, bool down_mix);
void hdmi_audio_set_sample_rate(struct hdmi *hdmi, int rate);


/*
 * hdmi bridge:
 */

struct drm_bridge *hdmi_bridge_init(struct hdmi *hdmi);

/*
 * hdmi connector:
 */

void hdmi_connector_irq(struct drm_connector *connector);
struct drm_connector *hdmi_connector_init(struct hdmi *hdmi);

/*
 * i2c adapter for ddc:
 */

void hdmi_i2c_irq(struct i2c_adapter *i2c);
void hdmi_i2c_destroy(struct i2c_adapter *i2c);
struct i2c_adapter *hdmi_i2c_init(struct hdmi *hdmi);

#endif /* __HDMI_CONNECTOR_H__ */
