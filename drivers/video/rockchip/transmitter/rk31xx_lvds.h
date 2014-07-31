#ifndef _RK31XX_LVDS_H_
#define _RK31XX_LVDS_H_

#include <linux/rk_screen.h>


#define BITS(x, bit)            ((x) << (bit))
#define BITS_MASK(x, mask, bit)  BITS((x) & (mask), bit)
#define BITS_EN(mask, bit)       BITS(mask, bit + 16)

/* RK312X_GRF_LVDS_CON0 */
#define v_LVDS_DATA_SEL(x)      (BITS_MASK(x, 1, 0) | BITS_EN(1, 0))
#define v_LVDS_OUTPUT_FORMAT(x) (BITS_MASK(x, 3, 1) | BITS_EN(3, 1))
#define v_LVDS_MSBSEL(x)        (BITS_MASK(x, 1, 3) | BITS_EN(1, 3))
#define v_LVDSMODE_EN(x)        (BITS_MASK(x, 1, 6) | BITS_EN(1, 6))
#define v_MIPIPHY_TTL_EN(x)     (BITS_MASK(x, 1, 7) | BITS_EN(1, 7))

enum {
        LVDS_DATA_FROM_LCDC = 0,
        LVDS_DATA_FORM_EBC,
};

enum {
        LVDS_MSB_D0 = 0,
        LVDS_MSB_D7,
};

#define MIPIPHY_REG0		0x0000
#define v_LANE0_EN(x)           BITS_MASK(x, 1, 2)
#define v_LANE1_EN(x)           BITS_MASK(x, 1, 3)
#define v_LANE2_EN(x)           BITS_MASK(x, 1, 4)
#define v_LANE3_EN(x)           BITS_MASK(x, 1, 5)
#define v_LANECLK_EN(x)         BITS_MASK(x, 1, 6)

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

#define MIPIPHY_REGEA		0x03A8
#define m_BG_POWER_DOWN         BITS(1, 0)
#define m_PLL_POWER_DOWN        BITS(1, 2)
#define v_BG_POWER_DOWN(x)      BITS_MASK(x, 1, 0)
#define v_PLL_POWER_DOWN(x)     BITS_MASK(x, 1, 2)

#define MIPIPHY_REGE2		0x0388
#define MIPIPHY_REGE7		0x039C


struct rk_lvds_device {
	struct device 		*dev;
	void __iomem  		*regbase;
	struct clk    		*pclk;  /*phb clk*/
	struct rk_screen	screen;
	bool			clk_on;
        bool                    sys_state;
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

#endif

