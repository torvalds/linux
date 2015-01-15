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

#ifdef CONFIG_COMMON_CLK
#include <linux/clk.h>
#include <linux/clk-provider.h>
#endif

#include "hdmi.h"

struct hdmi_phy_8960 {
	struct hdmi_phy base;
	struct hdmi *hdmi;
#ifdef CONFIG_COMMON_CLK
	struct clk_hw pll_hw;
	struct clk *pll;
	unsigned long pixclk;
#endif
};
#define to_hdmi_phy_8960(x) container_of(x, struct hdmi_phy_8960, base)

#ifdef CONFIG_COMMON_CLK
#define clk_to_phy(x) container_of(x, struct hdmi_phy_8960, pll_hw)

/*
 * HDMI PLL:
 *
 * To get the parent clock setup properly, we need to plug in hdmi pll
 * configuration into common-clock-framework.
 */

struct pll_rate {
	unsigned long rate;
	struct {
		uint32_t val;
		uint32_t reg;
	} conf[32];
};

/* NOTE: keep sorted highest freq to lowest: */
static const struct pll_rate freqtbl[] = {
	/* 1080p60/1080p50 case */
	{ 148500000, {
		{ 0x02, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x02, REG_HDMI_8960_PHY_PLL_CHRG_PUMP_CFG },
		{ 0x01, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0x33, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x2c, REG_HDMI_8960_PHY_PLL_IDAC_ADJ_CFG  },
		{ 0x06, REG_HDMI_8960_PHY_PLL_I_VI_KVCO_CFG },
		{ 0x0a, REG_HDMI_8960_PHY_PLL_PWRDN_B       },
		{ 0x76, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0x01, REG_HDMI_8960_PHY_PLL_SDM_CFG1      },
		{ 0x4c, REG_HDMI_8960_PHY_PLL_SDM_CFG2      },
		{ 0xc0, REG_HDMI_8960_PHY_PLL_SDM_CFG3      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG4      },
		{ 0x9a, REG_HDMI_8960_PHY_PLL_SSC_CFG0      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SSC_CFG1      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SSC_CFG2      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SSC_CFG3      },
		{ 0x10, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG0  },
		{ 0x1a, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG1  },
		{ 0x0d, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2  },
		{ 0xe6, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x02, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG3   },
		{ 0x86, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG4   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG5   },
		{ 0x33, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG6   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG7   },
		{ 0, 0 } }
	},
	{ 108000000, {
		{ 0x08, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x21, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0xf9, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x1c, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x02, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0x86, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG4   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG5   },
		{ 0x49, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0x49, REG_HDMI_8960_PHY_PLL_SDM_CFG1      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG2      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG3      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG4      },
		{ 0, 0 } }
	},
	/* 720p60/720p50/1080i60/1080i50/1080p24/1080p30/1080p25 */
	{ 74250000, {
		{ 0x0a, REG_HDMI_8960_PHY_PLL_PWRDN_B       },
		{ 0x12, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x01, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0x33, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x76, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0xe6, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x02, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0, 0 } }
	},
	{ 65000000, {
		{ 0x18, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x20, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0xf9, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x8a, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x02, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG3   },
		{ 0x86, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG4   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG5   },
		{ 0x0b, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0x4b, REG_HDMI_8960_PHY_PLL_SDM_CFG1      },
		{ 0x7b, REG_HDMI_8960_PHY_PLL_SDM_CFG2      },
		{ 0x09, REG_HDMI_8960_PHY_PLL_SDM_CFG3      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG4      },
		{ 0, 0 } }
	},
	/* 480p60/480i60 */
	{ 27030000, {
		{ 0x0a, REG_HDMI_8960_PHY_PLL_PWRDN_B       },
		{ 0x38, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x02, REG_HDMI_8960_PHY_PLL_CHRG_PUMP_CFG },
		{ 0x20, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0xff, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0x4e, REG_HDMI_8960_PHY_PLL_SDM_CFG1      },
		{ 0xd7, REG_HDMI_8960_PHY_PLL_SDM_CFG2      },
		{ 0x03, REG_HDMI_8960_PHY_PLL_SDM_CFG3      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG4      },
		{ 0x2a, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x03, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG3   },
		{ 0x86, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG4   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG5   },
		{ 0x33, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG6   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG7   },
		{ 0, 0 } }
	},
	/* 576p50/576i50 */
	{ 27000000, {
		{ 0x32, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x02, REG_HDMI_8960_PHY_PLL_CHRG_PUMP_CFG },
		{ 0x01, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0x33, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x2c, REG_HDMI_8960_PHY_PLL_IDAC_ADJ_CFG  },
		{ 0x06, REG_HDMI_8960_PHY_PLL_I_VI_KVCO_CFG },
		{ 0x0a, REG_HDMI_8960_PHY_PLL_PWRDN_B       },
		{ 0x7b, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0x01, REG_HDMI_8960_PHY_PLL_SDM_CFG1      },
		{ 0x4c, REG_HDMI_8960_PHY_PLL_SDM_CFG2      },
		{ 0xc0, REG_HDMI_8960_PHY_PLL_SDM_CFG3      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG4      },
		{ 0x9a, REG_HDMI_8960_PHY_PLL_SSC_CFG0      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SSC_CFG1      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SSC_CFG2      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SSC_CFG3      },
		{ 0x10, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG0  },
		{ 0x1a, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG1  },
		{ 0x0d, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2  },
		{ 0x2a, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x03, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG3   },
		{ 0x86, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG4   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG5   },
		{ 0x33, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG6   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG7   },
		{ 0, 0 } }
	},
	/* 640x480p60 */
	{ 25200000, {
		{ 0x32, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x02, REG_HDMI_8960_PHY_PLL_CHRG_PUMP_CFG },
		{ 0x01, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0x33, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x2c, REG_HDMI_8960_PHY_PLL_IDAC_ADJ_CFG  },
		{ 0x06, REG_HDMI_8960_PHY_PLL_I_VI_KVCO_CFG },
		{ 0x0a, REG_HDMI_8960_PHY_PLL_PWRDN_B       },
		{ 0x77, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0x4c, REG_HDMI_8960_PHY_PLL_SDM_CFG1      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG2      },
		{ 0xc0, REG_HDMI_8960_PHY_PLL_SDM_CFG3      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG4      },
		{ 0x9a, REG_HDMI_8960_PHY_PLL_SSC_CFG0      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SSC_CFG1      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SSC_CFG2      },
		{ 0x20, REG_HDMI_8960_PHY_PLL_SSC_CFG3      },
		{ 0x10, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG0  },
		{ 0x1a, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG1  },
		{ 0x0d, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2  },
		{ 0xf4, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x02, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG3   },
		{ 0x86, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG4   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG5   },
		{ 0x33, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG6   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG7   },
		{ 0, 0 } }
	},
};

static int hdmi_pll_enable(struct clk_hw *hw)
{
	struct hdmi_phy_8960 *phy_8960 = clk_to_phy(hw);
	struct hdmi *hdmi = phy_8960->hdmi;
	int timeout_count, pll_lock_retry = 10;
	unsigned int val;

	DBG("");

	/* Assert PLL S/W reset */
	hdmi_write(hdmi, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2, 0x8d);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG0, 0x10);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG1, 0x1a);

	/* Wait for a short time before de-asserting
	 * to allow the hardware to complete its job.
	 * This much of delay should be fine for hardware
	 * to assert and de-assert.
	 */
	udelay(10);

	/* De-assert PLL S/W reset */
	hdmi_write(hdmi, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2, 0x0d);

	val = hdmi_read(hdmi, REG_HDMI_8960_PHY_REG12);
	val |= HDMI_8960_PHY_REG12_SW_RESET;
	/* Assert PHY S/W reset */
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG12, val);
	val &= ~HDMI_8960_PHY_REG12_SW_RESET;
	/* Wait for a short time before de-asserting
	   to allow the hardware to complete its job.
	   This much of delay should be fine for hardware
	   to assert and de-assert. */
	udelay(10);
	/* De-assert PHY S/W reset */
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG12, val);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG2,  0x3f);

	val = hdmi_read(hdmi, REG_HDMI_8960_PHY_REG12);
	val |= HDMI_8960_PHY_REG12_PWRDN_B;
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG12, val);
	/* Wait 10 us for enabling global power for PHY */
	mb();
	udelay(10);

	val = hdmi_read(hdmi, REG_HDMI_8960_PHY_PLL_PWRDN_B);
	val |= HDMI_8960_PHY_PLL_PWRDN_B_PLL_PWRDN_B;
	val &= ~HDMI_8960_PHY_PLL_PWRDN_B_PD_PLL;
	hdmi_write(hdmi, REG_HDMI_8960_PHY_PLL_PWRDN_B, val);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG2, 0x80);

	timeout_count = 1000;
	while (--pll_lock_retry > 0) {

		/* are we there yet? */
		val = hdmi_read(hdmi, REG_HDMI_8960_PHY_PLL_STATUS0);
		if (val & HDMI_8960_PHY_PLL_STATUS0_PLL_LOCK)
			break;

		udelay(1);

		if (--timeout_count > 0)
			continue;

		/*
		 * PLL has still not locked.
		 * Do a software reset and try again
		 * Assert PLL S/W reset first
		 */
		hdmi_write(hdmi, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2, 0x8d);
		udelay(10);
		hdmi_write(hdmi, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2, 0x0d);

		/*
		 * Wait for a short duration for the PLL calibration
		 * before checking if the PLL gets locked
		 */
		udelay(350);

		timeout_count = 1000;
	}

	return 0;
}

static void hdmi_pll_disable(struct clk_hw *hw)
{
	struct hdmi_phy_8960 *phy_8960 = clk_to_phy(hw);
	struct hdmi *hdmi = phy_8960->hdmi;
	unsigned int val;

	DBG("");

	val = hdmi_read(hdmi, REG_HDMI_8960_PHY_REG12);
	val &= ~HDMI_8960_PHY_REG12_PWRDN_B;
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG12, val);

	val = hdmi_read(hdmi, REG_HDMI_8960_PHY_PLL_PWRDN_B);
	val |= HDMI_8960_PHY_REG12_SW_RESET;
	val &= ~HDMI_8960_PHY_REG12_PWRDN_B;
	hdmi_write(hdmi, REG_HDMI_8960_PHY_PLL_PWRDN_B, val);
	/* Make sure HDMI PHY/PLL are powered down */
	mb();
}

static const struct pll_rate *find_rate(unsigned long rate)
{
	int i;
	for (i = 1; i < ARRAY_SIZE(freqtbl); i++)
		if (rate > freqtbl[i].rate)
			return &freqtbl[i-1];
	return &freqtbl[i-1];
}

static unsigned long hdmi_pll_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct hdmi_phy_8960 *phy_8960 = clk_to_phy(hw);
	return phy_8960->pixclk;
}

static long hdmi_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	const struct pll_rate *pll_rate = find_rate(rate);
	return pll_rate->rate;
}

static int hdmi_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct hdmi_phy_8960 *phy_8960 = clk_to_phy(hw);
	struct hdmi *hdmi = phy_8960->hdmi;
	const struct pll_rate *pll_rate = find_rate(rate);
	int i;

	DBG("rate=%lu", rate);

	for (i = 0; pll_rate->conf[i].reg; i++)
		hdmi_write(hdmi, pll_rate->conf[i].reg, pll_rate->conf[i].val);

	phy_8960->pixclk = rate;

	return 0;
}


static const struct clk_ops hdmi_pll_ops = {
	.enable = hdmi_pll_enable,
	.disable = hdmi_pll_disable,
	.recalc_rate = hdmi_pll_recalc_rate,
	.round_rate = hdmi_pll_round_rate,
	.set_rate = hdmi_pll_set_rate,
};

static const char *hdmi_pll_parents[] = {
	"pxo",
};

static struct clk_init_data pll_init = {
	.name = "hdmi_pll",
	.ops = &hdmi_pll_ops,
	.parent_names = hdmi_pll_parents,
	.num_parents = ARRAY_SIZE(hdmi_pll_parents),
};
#endif

/*
 * HDMI Phy:
 */

static void hdmi_phy_8960_destroy(struct hdmi_phy *phy)
{
	struct hdmi_phy_8960 *phy_8960 = to_hdmi_phy_8960(phy);
	kfree(phy_8960);
}

static void hdmi_phy_8960_reset(struct hdmi_phy *phy)
{
	struct hdmi_phy_8960 *phy_8960 = to_hdmi_phy_8960(phy);
	struct hdmi *hdmi = phy_8960->hdmi;
	unsigned int val;

	val = hdmi_read(hdmi, REG_HDMI_PHY_CTRL);

	if (val & HDMI_PHY_CTRL_SW_RESET_LOW) {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET);
	} else {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET);
	}

	if (val & HDMI_PHY_CTRL_SW_RESET_PLL_LOW) {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET_PLL);
	} else {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET_PLL);
	}

	msleep(100);

	if (val & HDMI_PHY_CTRL_SW_RESET_LOW) {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET);
	} else {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET);
	}

	if (val & HDMI_PHY_CTRL_SW_RESET_PLL_LOW) {
		/* pull high */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val | HDMI_PHY_CTRL_SW_RESET_PLL);
	} else {
		/* pull low */
		hdmi_write(hdmi, REG_HDMI_PHY_CTRL,
				val & ~HDMI_PHY_CTRL_SW_RESET_PLL);
	}
}

static void hdmi_phy_8960_powerup(struct hdmi_phy *phy,
		unsigned long int pixclock)
{
	struct hdmi_phy_8960 *phy_8960 = to_hdmi_phy_8960(phy);
	struct hdmi *hdmi = phy_8960->hdmi;

	DBG("pixclock: %lu", pixclock);

	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG2, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG0, 0x1b);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG1, 0xf2);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG4, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG5, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG6, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG7, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG8, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG9, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG10, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG11, 0x00);
	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG3, 0x20);
}

static void hdmi_phy_8960_powerdown(struct hdmi_phy *phy)
{
	struct hdmi_phy_8960 *phy_8960 = to_hdmi_phy_8960(phy);
	struct hdmi *hdmi = phy_8960->hdmi;

	DBG("");

	hdmi_write(hdmi, REG_HDMI_8960_PHY_REG2, 0x7f);
}

static const struct hdmi_phy_funcs hdmi_phy_8960_funcs = {
		.destroy = hdmi_phy_8960_destroy,
		.reset = hdmi_phy_8960_reset,
		.powerup = hdmi_phy_8960_powerup,
		.powerdown = hdmi_phy_8960_powerdown,
};

struct hdmi_phy *hdmi_phy_8960_init(struct hdmi *hdmi)
{
	struct hdmi_phy_8960 *phy_8960;
	struct hdmi_phy *phy = NULL;
	int ret;
#ifdef CONFIG_COMMON_CLK
	int i;

	/* sanity check: */
	for (i = 0; i < (ARRAY_SIZE(freqtbl) - 1); i++)
		if (WARN_ON(freqtbl[i].rate < freqtbl[i+1].rate))
			return ERR_PTR(-EINVAL);
#endif

	phy_8960 = kzalloc(sizeof(*phy_8960), GFP_KERNEL);
	if (!phy_8960) {
		ret = -ENOMEM;
		goto fail;
	}

	phy = &phy_8960->base;

	phy->funcs = &hdmi_phy_8960_funcs;

	phy_8960->hdmi = hdmi;

#ifdef CONFIG_COMMON_CLK
	phy_8960->pll_hw.init = &pll_init;
	phy_8960->pll = devm_clk_register(&hdmi->pdev->dev, &phy_8960->pll_hw);
	if (IS_ERR(phy_8960->pll)) {
		ret = PTR_ERR(phy_8960->pll);
		phy_8960->pll = NULL;
		goto fail;
	}
#endif

	return phy;

fail:
	if (phy)
		hdmi_phy_8960_destroy(phy);
	return ERR_PTR(ret);
}
