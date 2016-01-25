#ifndef _RK31XX_LVDS_H_
#define _RK31XX_LVDS_H_

#include <linux/rk_screen.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define BITS(x, bit)            ((x) << (bit))
#define BITS_MASK(x, mask, bit)  BITS((x) & (mask), bit)
#define BITS_EN(mask, bit)       BITS(mask, bit + 16)

/* RK312X_GRF_LVDS_CON0 */
#define v_LVDS_DATA_SEL(x)      (BITS_MASK(x, 1, 0) | BITS_EN(1, 0))
#define v_LVDS_OUTPUT_FORMAT(x) (BITS_MASK(x, 3, 1) | BITS_EN(3, 1))
#define v_LVDS_MSBSEL(x)        (BITS_MASK(x, 1, 3) | BITS_EN(1, 3))
#define v_LVDSMODE_EN(x)        (BITS_MASK(x, 1, 6) | BITS_EN(1, 6))
#define v_MIPIPHY_TTL_EN(x)     (BITS_MASK(x, 1, 7) | BITS_EN(1, 7))
#define v_MIPIPHY_LANE0_EN(x)   (BITS_MASK(x, 1, 8) | BITS_EN(1, 8))
#define v_MIPIDPI_FORCEX_EN(x)  (BITS_MASK(x, 1, 9) | BITS_EN(1, 9))

/* RK3368_GRF_SOC_CON7 0x41c*/
/* RK3366_GRF_SOC_CON5 0x414*/
#define v_RK3368_LVDS_OUTPUT_FORMAT(x) (BITS_MASK(x, 3, 13) | BITS_EN(3, 13))
#define v_RK3368_LVDS_MSBSEL(x)        (BITS_MASK(x, 1, 11) | BITS_EN(1, 11))
#define v_RK3368_LVDSMODE_EN(x)        (BITS_MASK(x, 1, 12) | BITS_EN(1, 12))
#define v_RK3368_MIPIPHY_TTL_EN(x)     (BITS_MASK(x, 1, 15) | BITS_EN(1, 15))
#define v_RK3368_MIPIPHY_LANE0_EN(x)   (BITS_MASK(x, 1, 5) | BITS_EN(1, 5))
#define v_RK3368_MIPIDPI_FORCEX_EN(x)  (BITS_MASK(x, 1, 6) | BITS_EN(1, 6))
enum {
        LVDS_DATA_FROM_LCDC = 0,
        LVDS_DATA_FORM_EBC,
};

enum {
        LVDS_MSB_D0 = 0,
        LVDS_MSB_D7,
};

/* RK312X_GRF_SOC_CON1 */
#define v_MIPITTL_CLK_EN(x)     (BITS_MASK(x, 1, 7) | BITS_EN(1, 7))
#define v_MIPITTL_LANE0_EN(x)   (BITS_MASK(x, 1, 11) | BITS_EN(1, 11))
#define v_MIPITTL_LANE1_EN(x)   (BITS_MASK(x, 1, 12) | BITS_EN(1, 12))
#define v_MIPITTL_LANE2_EN(x)   (BITS_MASK(x, 1, 13) | BITS_EN(1, 13))
#define v_MIPITTL_LANE3_EN(x)   (BITS_MASK(x, 1, 14) | BITS_EN(1, 14))


#define MIPIPHY_REG0            0x0000
#define m_LANE_EN_0             BITS(1, 2)
#define m_LANE_EN_1             BITS(1, 3)
#define m_LANE_EN_2             BITS(1, 4)
#define m_LANE_EN_3             BITS(1, 5)
#define m_LANE_EN_CLK           BITS(1, 5)
#define v_LANE_EN_0(x)          BITS(1, 2)
#define v_LANE_EN_1(x)          BITS(1, 3)
#define v_LANE_EN_2(x)          BITS(1, 4)
#define v_LANE_EN_3(x)          BITS(1, 5)
#define v_LANE_EN_CLK(x)        BITS(1, 5)

#define MIPIPHY_REG1            0x0004
#define m_SYNC_RST              BITS(1, 0)
#define m_LDO_PWR_DOWN          BITS(1, 1)
#define m_PLL_PWR_DOWN          BITS(1, 2)
#define v_SYNC_RST(x)           BITS_MASK(x, 1, 0)
#define v_LDO_PWR_DOWN(x)       BITS_MASK(x, 1, 1)
#define v_PLL_PWR_DOWN(x)       BITS_MASK(x, 1, 2)

#define MIPIPHY_REG3		0x000c
#define m_PREDIV                BITS(0x1f, 0)
#define m_FBDIV_MSB             BITS(1, 5)
#define v_PREDIV(x)             BITS_MASK(x, 0x1f, 0)
#define v_FBDIV_MSB(x)          BITS_MASK(x, 1, 5)

#define MIPIPHY_REG4		0x0010
#define v_FBDIV_LSB(x)          BITS_MASK(x, 0xff, 0)

#define MIPIPHY_REGE0		0x0380
#define m_MSB_SEL               BITS(1, 0)
#define m_DIG_INTER_RST         BITS(1, 2)
#define m_LVDS_MODE_EN          BITS(1, 5)
#define m_TTL_MODE_EN           BITS(1, 6)
#define m_MIPI_MODE_EN          BITS(1, 7)
#define v_MSB_SEL(x)            BITS_MASK(x, 1, 0)
#define v_DIG_INTER_RST(x)      BITS_MASK(x, 1, 2)
#define v_LVDS_MODE_EN(x)       BITS_MASK(x, 1, 5)
#define v_TTL_MODE_EN(x)        BITS_MASK(x, 1, 6)
#define v_MIPI_MODE_EN(x)       BITS_MASK(x, 1, 7)

#define MIPIPHY_REGE1           0x0384
#define m_DIG_INTER_EN          BITS(1, 7)
#define v_DIG_INTER_EN(x)       BITS_MASK(x, 1, 7)

#define MIPIPHY_REGE3           0x038c
#define m_MIPI_EN               BITS(1, 0)
#define m_LVDS_EN               BITS(1, 1)
#define m_TTL_EN                BITS(1, 2)
#define v_MIPI_EN(x)            BITS_MASK(x, 1, 0)
#define v_LVDS_EN(x)            BITS_MASK(x, 1, 1)
#define v_TTL_EN(x)             BITS_MASK(x, 1, 2)

#define MIPIPHY_REGE4		0x0390
#define m_VOCM			BITS(3, 4)
#define m_DIFF_V		BITS(3, 6)

#define v_VOCM(x)		BITS_MASK(x, 3, 4)
#define v_DIFF_V(x)		BITS_MASK(x, 3, 6)

#define MIPIPHY_REGE8           0x03a0

#define MIPIPHY_REGEB           0x03ac
#define v_PLL_PWR_OFF(x)        BITS_MASK(x, 1, 2)
#define v_LANECLK_EN(x)         BITS_MASK(x, 1, 3)
#define v_LANE3_EN(x)           BITS_MASK(x, 1, 4)
#define v_LANE2_EN(x)           BITS_MASK(x, 1, 5)
#define v_LANE1_EN(x)           BITS_MASK(x, 1, 6)
#define v_LANE0_EN(x)           BITS_MASK(x, 1, 7)

#define RK3368_GRF_SOC_CON7_LVDS	0x041c
#define RK3368_GRF_SOC_CON15_LVDS	0x043c
#define RK3366_GRF_SOC_CON5_LVDS	0x0414
#define RK3366_GRF_SOC_CON6_LVDS	0x0418
#define v_RK3368_FORCE_JETAG(x) (BITS_MASK(x, 1, 13) | BITS_EN(1, 13))

enum {
	LVDS_SOC_RK312X,
	LVDS_SOC_RK3368,
	LVDS_SOC_RK3366
};

struct rk_lvds_drvdata  {
	u8 soc_type;
	u32 reversed;
};

struct rk_lvds_device {
	struct rk_lvds_drvdata *data;
	struct device 		*dev;
	void __iomem  		*regbase;
	void __iomem		*ctrl_reg;
	struct regmap		*grf_lvds_base;
	struct clk    		*pd;  /*power domain*/
	struct clk    		*pclk;  /*phb clk*/
	struct clk    		*ctrl_pclk;	/* mipi ctrl pclk*/
	struct clk    		*ctrl_hclk;	/* mipi ctrl hclk*/
	struct rk_screen	screen;
	bool			clk_on;
        bool                    sys_state;
#ifdef CONFIG_PINCTRL
	struct dev_pin_info	*pins;
#endif
};

static inline int lvds_writel(struct rk_lvds_device *lvds, u32 offset, u32 val)
{
	writel_relaxed(val, lvds->regbase + offset);
	return 0;
}

static inline int lvds_msk_reg(struct rk_lvds_device *lvds, u32 offset,
			       u32 msk, u32 val)
{
	u32 temp;

	temp = readl_relaxed(lvds->regbase + offset) & (0xFF - (msk));
	writel_relaxed(temp | ((val) & (msk)), lvds->regbase + offset);
	return 0;
}

static inline u32 lvds_readl(struct rk_lvds_device *lvds, u32 offset)
{
	return readl_relaxed(lvds->regbase + offset);
}

static inline int lvds_grf_writel(struct rk_lvds_device *lvds,
				  u32 offset, u32 val)
{
	regmap_write(lvds->grf_lvds_base, offset, val);
	dsb(sy);

	return 0;
}

static inline int lvds_dsi_writel(struct rk_lvds_device *lvds,
				  u32 offset, u32 val)
{
	writel_relaxed(val, lvds->ctrl_reg + offset);
	dsb(sy);

	return 0;
}

static inline u32 lvds_phy_lockon(struct rk_lvds_device *lvds)
{
	u32 val = 0;
	if (lvds->data->soc_type == LVDS_SOC_RK312X)
		val = readl_relaxed(lvds->ctrl_reg);
	else
		val = readl_relaxed(lvds->ctrl_reg + 0x10);
	return (val & 0x01);
}

#endif

