/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include <linux/clk-provider.h>
#include "hdmi.h"

struct hdmi_pll_8960 {
	struct platform_device *pdev;
	struct clk_hw clk_hw;
	void __iomem *mmio;

	unsigned long pixclk;
};

#define hw_clk_to_pll(x) container_of(x, struct hdmi_pll_8960, clk_hw)

/*
 * HDMI PLL:
 *
 * To get the parent clock setup properly, we need to plug in hdmi pll
 * configuration into common-clock-framework.
 */

struct pll_rate {
	unsigned long rate;
	int num_reg;
	struct {
		u32 val;
		u32 reg;
	} conf[32];
};

/* NOTE: keep sorted highest freq to lowest: */
static const struct pll_rate freqtbl[] = {
	{ 154000000, 14, {
		{ 0x08, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x20, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0xf9, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x02, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x03, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG3   },
		{ 0x86, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG4   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG5   },
		{ 0x0d, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0x4d, REG_HDMI_8960_PHY_PLL_SDM_CFG1      },
		{ 0x5e, REG_HDMI_8960_PHY_PLL_SDM_CFG2      },
		{ 0x42, REG_HDMI_8960_PHY_PLL_SDM_CFG3      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG4      },
			}
	},
	/* 1080p60/1080p50 case */
	{ 148500000, 27, {
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
			}
	},
	{ 108000000, 13, {
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
			}
	},
	/* 720p60/720p50/1080i60/1080i50/1080p24/1080p30/1080p25 */
	{ 74250000, 8, {
		{ 0x0a, REG_HDMI_8960_PHY_PLL_PWRDN_B       },
		{ 0x12, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x01, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0x33, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0x76, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0xe6, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x02, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
			}
	},
	{ 74176000, 14, {
		{ 0x18, REG_HDMI_8960_PHY_PLL_REFCLK_CFG    },
		{ 0x20, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG0 },
		{ 0xf9, REG_HDMI_8960_PHY_PLL_LOOP_FLT_CFG1 },
		{ 0xe5, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG0   },
		{ 0x02, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG1   },
		{ 0x3b, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG2   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG3   },
		{ 0x86, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG4   },
		{ 0x00, REG_HDMI_8960_PHY_PLL_VCOCAL_CFG5   },
		{ 0x0c, REG_HDMI_8960_PHY_PLL_SDM_CFG0      },
		{ 0x4c, REG_HDMI_8960_PHY_PLL_SDM_CFG1      },
		{ 0x7d, REG_HDMI_8960_PHY_PLL_SDM_CFG2      },
		{ 0xbc, REG_HDMI_8960_PHY_PLL_SDM_CFG3      },
		{ 0x00, REG_HDMI_8960_PHY_PLL_SDM_CFG4      },
			}
	},
	{ 65000000, 14, {
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
			}
	},
	/* 480p60/480i60 */
	{ 27030000, 18, {
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
			}
	},
	/* 576p50/576i50 */
	{ 27000000, 27, {
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
			}
	},
	/* 640x480p60 */
	{ 25200000, 27, {
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
			}
	},
};

static inline void pll_write(struct hdmi_pll_8960 *pll, u32 reg, u32 data)
{
	msm_writel(data, pll->mmio + reg);
}

static inline u32 pll_read(struct hdmi_pll_8960 *pll, u32 reg)
{
	return msm_readl(pll->mmio + reg);
}

static inline struct hdmi_phy *pll_get_phy(struct hdmi_pll_8960 *pll)
{
	return platform_get_drvdata(pll->pdev);
}

static int hdmi_pll_enable(struct clk_hw *hw)
{
	struct hdmi_pll_8960 *pll = hw_clk_to_pll(hw);
	struct hdmi_phy *phy = pll_get_phy(pll);
	int timeout_count, pll_lock_retry = 10;
	unsigned int val;

	DBG("");

	/* Assert PLL S/W reset */
	pll_write(pll, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2, 0x8d);
	pll_write(pll, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG0, 0x10);
	pll_write(pll, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG1, 0x1a);

	/* Wait for a short time before de-asserting
	 * to allow the hardware to complete its job.
	 * This much of delay should be fine for hardware
	 * to assert and de-assert.
	 */
	udelay(10);

	/* De-assert PLL S/W reset */
	pll_write(pll, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2, 0x0d);

	val = hdmi_phy_read(phy, REG_HDMI_8960_PHY_REG12);
	val |= HDMI_8960_PHY_REG12_SW_RESET;
	/* Assert PHY S/W reset */
	hdmi_phy_write(phy, REG_HDMI_8960_PHY_REG12, val);
	val &= ~HDMI_8960_PHY_REG12_SW_RESET;
	/*
	 * Wait for a short time before de-asserting to allow the hardware to
	 * complete its job. This much of delay should be fine for hardware to
	 * assert and de-assert.
	 */
	udelay(10);
	/* De-assert PHY S/W reset */
	hdmi_phy_write(phy, REG_HDMI_8960_PHY_REG12, val);
	hdmi_phy_write(phy, REG_HDMI_8960_PHY_REG2,  0x3f);

	val = hdmi_phy_read(phy, REG_HDMI_8960_PHY_REG12);
	val |= HDMI_8960_PHY_REG12_PWRDN_B;
	hdmi_phy_write(phy, REG_HDMI_8960_PHY_REG12, val);
	/* Wait 10 us for enabling global power for PHY */
	mb();
	udelay(10);

	val = pll_read(pll, REG_HDMI_8960_PHY_PLL_PWRDN_B);
	val |= HDMI_8960_PHY_PLL_PWRDN_B_PLL_PWRDN_B;
	val &= ~HDMI_8960_PHY_PLL_PWRDN_B_PD_PLL;
	pll_write(pll, REG_HDMI_8960_PHY_PLL_PWRDN_B, val);
	hdmi_phy_write(phy, REG_HDMI_8960_PHY_REG2, 0x80);

	timeout_count = 1000;
	while (--pll_lock_retry > 0) {
		/* are we there yet? */
		val = pll_read(pll, REG_HDMI_8960_PHY_PLL_STATUS0);
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
		pll_write(pll, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2, 0x8d);
		udelay(10);
		pll_write(pll, REG_HDMI_8960_PHY_PLL_LOCKDET_CFG2, 0x0d);

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
	struct hdmi_pll_8960 *pll = hw_clk_to_pll(hw);
	struct hdmi_phy *phy = pll_get_phy(pll);
	unsigned int val;

	DBG("");

	val = hdmi_phy_read(phy, REG_HDMI_8960_PHY_REG12);
	val &= ~HDMI_8960_PHY_REG12_PWRDN_B;
	hdmi_phy_write(phy, REG_HDMI_8960_PHY_REG12, val);

	val = pll_read(pll, REG_HDMI_8960_PHY_PLL_PWRDN_B);
	val |= HDMI_8960_PHY_REG12_SW_RESET;
	val &= ~HDMI_8960_PHY_REG12_PWRDN_B;
	pll_write(pll, REG_HDMI_8960_PHY_PLL_PWRDN_B, val);
	/* Make sure HDMI PHY/PLL are powered down */
	mb();
}

static const struct pll_rate *find_rate(unsigned long rate)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(freqtbl); i++)
		if (rate > freqtbl[i].rate)
			return &freqtbl[i - 1];

	return &freqtbl[i - 1];
}

static unsigned long hdmi_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct hdmi_pll_8960 *pll = hw_clk_to_pll(hw);

	return pll->pixclk;
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
	struct hdmi_pll_8960 *pll = hw_clk_to_pll(hw);
	const struct pll_rate *pll_rate = find_rate(rate);
	int i;

	DBG("rate=%lu", rate);

	for (i = 0; i < pll_rate->num_reg; i++)
		pll_write(pll, pll_rate->conf[i].reg, pll_rate->conf[i].val);

	pll->pixclk = rate;

	return 0;
}

static const struct clk_ops hdmi_pll_ops = {
	.enable = hdmi_pll_enable,
	.disable = hdmi_pll_disable,
	.recalc_rate = hdmi_pll_recalc_rate,
	.round_rate = hdmi_pll_round_rate,
	.set_rate = hdmi_pll_set_rate,
};

static const char * const hdmi_pll_parents[] = {
	"pxo",
};

static struct clk_init_data pll_init = {
	.name = "hdmi_pll",
	.ops = &hdmi_pll_ops,
	.parent_names = hdmi_pll_parents,
	.num_parents = ARRAY_SIZE(hdmi_pll_parents),
	.flags = CLK_IGNORE_UNUSED,
};

int msm_hdmi_pll_8960_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdmi_pll_8960 *pll;
	struct clk *clk;
	int i;

	/* sanity check: */
	for (i = 0; i < (ARRAY_SIZE(freqtbl) - 1); i++)
		if (WARN_ON(freqtbl[i].rate < freqtbl[i + 1].rate))
			return -EINVAL;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return -ENOMEM;

	pll->mmio = msm_ioremap(pdev, "hdmi_pll", "HDMI_PLL");
	if (IS_ERR(pll->mmio)) {
		dev_err(dev, "failed to map pll base\n");
		return -ENOMEM;
	}

	pll->pdev = pdev;
	pll->clk_hw.init = &pll_init;

	clk = devm_clk_register(dev, &pll->clk_hw);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to register pll clock\n");
		return -EINVAL;
	}

	return 0;
}
