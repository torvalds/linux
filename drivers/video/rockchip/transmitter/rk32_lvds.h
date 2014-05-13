#ifndef __RK32_LVDS__
#define __RK32_LVDS__

#define LVDS_CH0_REG_0			0x00
#define LVDS_CH0_REG_1			0x04
#define LVDS_CH0_REG_2			0x08
#define LVDS_CH0_REG_3			0x0c
#define LVDS_CH0_REG_4			0x10
#define LVDS_CH0_REG_5			0x14
#define LVDS_CH0_REG_9			0x24
#define LVDS_CFG_REG_c			0x30
#define LVDS_CH0_REG_d			0x34
#define LVDS_CH0_REG_f			0x3c
#define LVDS_CH0_REG_20			0x80
#define LVDS_CFG_REG_21			0x84

#define LVDS_SEL_VOP_LIT		(1 << 3)

#define LVDS_FMT_MASK			(0x07 << 16)
#define LVDS_MSB			(0x01 << 3)
#define LVDS_DUAL			(0x01 << 4)
#define LVDS_FMT_1			(0x01 << 5)
#define LVDS_TTL_EN			(0x01 << 6)
#define LVDS_START_PHASE_RST_1		(0x01 << 7)
#define LVDS_DCLK_INV			(0x01 << 8)
#define LVDS_CH0_EN			(0x01 << 11)
#define LVDS_CH1_EN			(0x01 << 12)
#define LVDS_PWRDN			(0x01 << 15)

struct rk32_lvds {
	struct device 		*dev;
	void __iomem  		*regs;
	struct clk    		*pclk; /*phb clk*/
	struct clk              *pd;
	struct rk_screen	screen;
	bool			clk_on;
};

static int inline lvds_writel(struct rk32_lvds *lvds, u32 offset, u32 val)
{
	writel_relaxed(val, lvds->regs + offset);
	//if (lvds->screen.type == SCREEN_DUAL_LVDS)
		writel_relaxed(val, lvds->regs + offset + 0x100);
	return 0;
}
#endif
