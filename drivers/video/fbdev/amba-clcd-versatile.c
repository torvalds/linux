#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/platform_data/video-clcd-versatile.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/bitops.h>
#include "amba-clcd-versatile.h"

static struct clcd_panel vga = {
	.mode		= {
		.name		= "VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 39721,
		.left_margin	= 40,
		.right_margin	= 24,
		.upper_margin	= 32,
		.lower_margin	= 11,
		.hsync_len	= 96,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_BGR | CNTL_LCDVCOMP(1),
	.caps		= CLCD_CAP_5551 | CLCD_CAP_565 | CLCD_CAP_888,
	.bpp		= 16,
};

static struct clcd_panel xvga = {
	.mode		= {
		.name		= "XVGA",
		.refresh	= 60,
		.xres		= 1024,
		.yres		= 768,
		.pixclock	= 15748,
		.left_margin	= 152,
		.right_margin	= 48,
		.upper_margin	= 23,
		.lower_margin	= 3,
		.hsync_len	= 104,
		.vsync_len	= 4,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_BGR | CNTL_LCDVCOMP(1),
	.caps		= CLCD_CAP_5551 | CLCD_CAP_565 | CLCD_CAP_888,
	.bpp		= 16,
};

/* Sanyo TM38QV67A02A - 3.8 inch QVGA (320x240) Color TFT */
static struct clcd_panel sanyo_tm38qv67a02a = {
	.mode		= {
		.name		= "Sanyo TM38QV67A02A",
		.refresh	= 116,
		.xres		= 320,
		.yres		= 240,
		.pixclock	= 100000,
		.left_margin	= 6,
		.right_margin	= 6,
		.upper_margin	= 5,
		.lower_margin	= 5,
		.hsync_len	= 6,
		.vsync_len	= 6,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD,
	.cntl		= CNTL_LCDTFT | CNTL_BGR | CNTL_LCDVCOMP(1),
	.caps		= CLCD_CAP_5551,
	.bpp		= 16,
};

static struct clcd_panel sanyo_2_5_in = {
	.mode		= {
		.name		= "Sanyo QVGA Portrait",
		.refresh	= 116,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 100000,
		.left_margin	= 20,
		.right_margin	= 10,
		.upper_margin	= 2,
		.lower_margin	= 2,
		.hsync_len	= 10,
		.vsync_len	= 2,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IVS | TIM2_IHS | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_BGR | CNTL_LCDVCOMP(1),
	.caps		= CLCD_CAP_5551,
	.bpp		= 16,
};

/* Epson L2F50113T00 - 2.2 inch 176x220 Color TFT */
static struct clcd_panel epson_l2f50113t00 = {
	.mode		= {
		.name		= "Epson L2F50113T00",
		.refresh	= 390,
		.xres		= 176,
		.yres		= 220,
		.pixclock	= 62500,
		.left_margin	= 3,
		.right_margin	= 2,
		.upper_margin	= 1,
		.lower_margin	= 0,
		.hsync_len	= 3,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_BGR | CNTL_LCDVCOMP(1),
	.caps		= CLCD_CAP_5551,
	.bpp		= 16,
};

static struct clcd_panel *panels[] = {
	&vga,
	&xvga,
	&sanyo_tm38qv67a02a,
	&sanyo_2_5_in,
	&epson_l2f50113t00,
};

struct clcd_panel *versatile_clcd_get_panel(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(panels); i++)
		if (strcmp(panels[i]->mode.name, name) == 0)
			break;

	if (i < ARRAY_SIZE(panels))
		return panels[i];

	pr_err("CLCD: couldn't get parameters for panel %s\n", name);

	return NULL;
}

int versatile_clcd_setup_dma(struct clcd_fb *fb, unsigned long framesize)
{
	dma_addr_t dma;

	fb->fb.screen_base = dma_alloc_wc(&fb->dev->dev, framesize, &dma,
					  GFP_KERNEL);
	if (!fb->fb.screen_base) {
		pr_err("CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

	fb->fb.fix.smem_start	= dma;
	fb->fb.fix.smem_len	= framesize;

	return 0;
}

int versatile_clcd_mmap_dma(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_wc(&fb->dev->dev, vma, fb->fb.screen_base,
			   fb->fb.fix.smem_start, fb->fb.fix.smem_len);
}

void versatile_clcd_remove_dma(struct clcd_fb *fb)
{
	dma_free_wc(&fb->dev->dev, fb->fb.fix.smem_len, fb->fb.screen_base,
		    fb->fb.fix.smem_start);
}

#ifdef CONFIG_OF

static struct regmap *versatile_syscon_map;
static struct regmap *versatile_ib2_map;

/*
 * We detect the different syscon types from the compatible strings.
 */
enum versatile_clcd {
	INTEGRATOR_CLCD_CM,
	VERSATILE_CLCD,
	REALVIEW_CLCD_EB,
	REALVIEW_CLCD_PB1176,
	REALVIEW_CLCD_PB11MP,
	REALVIEW_CLCD_PBA8,
	REALVIEW_CLCD_PBX,
};

static const struct of_device_id versatile_clcd_of_match[] = {
	{
		.compatible = "arm,core-module-integrator",
		.data = (void *)INTEGRATOR_CLCD_CM,
	},
	{
		.compatible = "arm,versatile-sysreg",
		.data = (void *)VERSATILE_CLCD,
	},
	{
		.compatible = "arm,realview-eb-syscon",
		.data = (void *)REALVIEW_CLCD_EB,
	},
	{
		.compatible = "arm,realview-pb1176-syscon",
		.data = (void *)REALVIEW_CLCD_PB1176,
	},
	{
		.compatible = "arm,realview-pb11mp-syscon",
		.data = (void *)REALVIEW_CLCD_PB11MP,
	},
	{
		.compatible = "arm,realview-pba8-syscon",
		.data = (void *)REALVIEW_CLCD_PBA8,
	},
	{
		.compatible = "arm,realview-pbx-syscon",
		.data = (void *)REALVIEW_CLCD_PBX,
	},
	{},
};

/*
 * Core module CLCD control on the Integrator/CP, bits
 * 8 thru 19 of the CM_CONTROL register controls a bunch
 * of CLCD settings.
 */
#define INTEGRATOR_HDR_CTRL_OFFSET	0x0C
#define INTEGRATOR_CLCD_LCDBIASEN	BIT(8)
#define INTEGRATOR_CLCD_LCDBIASUP	BIT(9)
#define INTEGRATOR_CLCD_LCDBIASDN	BIT(10)
/* Bits 11,12,13 controls the LCD type */
#define INTEGRATOR_CLCD_LCDMUX_MASK	(BIT(11)|BIT(12)|BIT(13))
#define INTEGRATOR_CLCD_LCDMUX_LCD24	BIT(11)
#define INTEGRATOR_CLCD_LCDMUX_VGA565	BIT(12)
#define INTEGRATOR_CLCD_LCDMUX_SHARP	(BIT(11)|BIT(12))
#define INTEGRATOR_CLCD_LCDMUX_VGA555	BIT(13)
#define INTEGRATOR_CLCD_LCDMUX_VGA24	(BIT(11)|BIT(12)|BIT(13))
#define INTEGRATOR_CLCD_LCD0_EN		BIT(14)
#define INTEGRATOR_CLCD_LCD1_EN		BIT(15)
/* R/L flip on Sharp */
#define INTEGRATOR_CLCD_LCD_STATIC1	BIT(16)
/* U/D flip on Sharp */
#define INTEGRATOR_CLCD_LCD_STATIC2	BIT(17)
/* No connection on Sharp */
#define INTEGRATOR_CLCD_LCD_STATIC	BIT(18)
/* 0 = 24bit VGA, 1 = 18bit VGA */
#define INTEGRATOR_CLCD_LCD_N24BITEN	BIT(19)

#define INTEGRATOR_CLCD_MASK		(INTEGRATOR_CLCD_LCDBIASEN | \
					 INTEGRATOR_CLCD_LCDBIASUP | \
					 INTEGRATOR_CLCD_LCDBIASDN | \
					 INTEGRATOR_CLCD_LCDMUX_MASK | \
					 INTEGRATOR_CLCD_LCD0_EN | \
					 INTEGRATOR_CLCD_LCD1_EN | \
					 INTEGRATOR_CLCD_LCD_STATIC1 | \
					 INTEGRATOR_CLCD_LCD_STATIC2 | \
					 INTEGRATOR_CLCD_LCD_STATIC | \
					 INTEGRATOR_CLCD_LCD_N24BITEN)

static void integrator_clcd_enable(struct clcd_fb *fb)
{
	struct fb_var_screeninfo *var = &fb->fb.var;
	u32 val;

	dev_info(&fb->dev->dev, "enable Integrator CLCD connectors\n");

	/* FIXME: really needed? */
	val = INTEGRATOR_CLCD_LCD_STATIC1 | INTEGRATOR_CLCD_LCD_STATIC2 |
		INTEGRATOR_CLCD_LCD0_EN | INTEGRATOR_CLCD_LCD1_EN;
	if (var->bits_per_pixel <= 8 ||
	    (var->bits_per_pixel == 16 && var->green.length == 5))
		/* Pseudocolor, RGB555, BGR555 */
		val |= INTEGRATOR_CLCD_LCDMUX_VGA555;
	else if (fb->fb.var.bits_per_pixel <= 16)
		/* truecolor RGB565 */
		val |= INTEGRATOR_CLCD_LCDMUX_VGA565;
	else
		val = 0; /* no idea for this, don't trust the docs */

	regmap_update_bits(versatile_syscon_map,
			   INTEGRATOR_HDR_CTRL_OFFSET,
			   INTEGRATOR_CLCD_MASK,
			   val);
}

/*
 * This configuration register in the Versatile and RealView
 * family is uniformly present but appears more and more
 * unutilized starting with the RealView series.
 */
#define SYS_CLCD			0x50
#define SYS_CLCD_MODE_MASK		(BIT(0)|BIT(1))
#define SYS_CLCD_MODE_888		0
#define SYS_CLCD_MODE_5551		BIT(0)
#define SYS_CLCD_MODE_565_R_LSB		BIT(1)
#define SYS_CLCD_MODE_565_B_LSB		(BIT(0)|BIT(1))
#define SYS_CLCD_CONNECTOR_MASK		(BIT(2)|BIT(3)|BIT(4)|BIT(5))
#define SYS_CLCD_NLCDIOON		BIT(2)
#define SYS_CLCD_VDDPOSSWITCH		BIT(3)
#define SYS_CLCD_PWR3V5SWITCH		BIT(4)
#define SYS_CLCD_VDDNEGSWITCH		BIT(5)
#define SYS_CLCD_TSNSS			BIT(6) /* touchscreen enable */
#define SYS_CLCD_SSPEXP			BIT(7) /* SSP expansion enable */

/* The Versatile can detect the connected panel type */
#define SYS_CLCD_CLCDID_MASK		(BIT(8)|BIT(9)|BIT(10)|BIT(11)|BIT(12))
#define SYS_CLCD_ID_SANYO_3_8		(0x00 << 8)
#define SYS_CLCD_ID_SHARP_8_4		(0x01 << 8)
#define SYS_CLCD_ID_EPSON_2_2		(0x02 << 8)
#define SYS_CLCD_ID_SANYO_2_5		(0x07 << 8)
#define SYS_CLCD_ID_VGA			(0x1f << 8)

#define SYS_CLCD_TSNDAV			BIT(13) /* data ready from TS */

/* IB2 control register for the Versatile daughterboard */
#define IB2_CTRL			0x00
#define IB2_CTRL_LCD_SD			BIT(1) /* 1 = shut down LCD */
#define IB2_CTRL_LCD_BL_ON		BIT(0)
#define IB2_CTRL_LCD_MASK		(BIT(0)|BIT(1))

static void versatile_clcd_disable(struct clcd_fb *fb)
{
	dev_info(&fb->dev->dev, "disable Versatile CLCD connectors\n");
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_CONNECTOR_MASK,
			   0);

	/* If we're on an IB2 daughterboard, turn off display */
	if (versatile_ib2_map) {
		dev_info(&fb->dev->dev, "disable IB2 display\n");
		regmap_update_bits(versatile_ib2_map,
				   IB2_CTRL,
				   IB2_CTRL_LCD_MASK,
				   IB2_CTRL_LCD_SD);
	}
}

static void versatile_clcd_enable(struct clcd_fb *fb)
{
	struct fb_var_screeninfo *var = &fb->fb.var;
	u32 val = 0;

	dev_info(&fb->dev->dev, "enable Versatile CLCD connectors\n");
	switch (var->green.length) {
	case 5:
		val |= SYS_CLCD_MODE_5551;
		break;
	case 6:
		if (var->red.offset == 0)
			val |= SYS_CLCD_MODE_565_R_LSB;
		else
			val |= SYS_CLCD_MODE_565_B_LSB;
		break;
	case 8:
		val |= SYS_CLCD_MODE_888;
		break;
	}

	/* Set up the MUX */
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_MODE_MASK,
			   val);

	/* Then enable the display */
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_CONNECTOR_MASK,
			   SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH);

	/* If we're on an IB2 daughterboard, turn on display */
	if (versatile_ib2_map) {
		dev_info(&fb->dev->dev, "enable IB2 display\n");
		regmap_update_bits(versatile_ib2_map,
				   IB2_CTRL,
				   IB2_CTRL_LCD_MASK,
				   IB2_CTRL_LCD_BL_ON);
	}
}

static void versatile_clcd_decode(struct clcd_fb *fb, struct clcd_regs *regs)
{
	clcdfb_decode(fb, regs);

	/* Always clear BGR for RGB565: we do the routing externally */
	if (fb->fb.var.green.length == 6)
		regs->cntl &= ~CNTL_BGR;
}

static void realview_clcd_disable(struct clcd_fb *fb)
{
	dev_info(&fb->dev->dev, "disable RealView CLCD connectors\n");
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_CONNECTOR_MASK,
			   0);
}

static void realview_clcd_enable(struct clcd_fb *fb)
{
	dev_info(&fb->dev->dev, "enable RealView CLCD connectors\n");
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_CONNECTOR_MASK,
			   SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH);
}

struct versatile_panel {
	u32 id;
	char *compatible;
	bool ib2;
};

static const struct versatile_panel versatile_panels[] = {
	{
		.id = SYS_CLCD_ID_VGA,
		.compatible = "VGA",
	},
	{
		.id = SYS_CLCD_ID_SANYO_3_8,
		.compatible = "sanyo,tm38qv67a02a",
	},
	{
		.id = SYS_CLCD_ID_SHARP_8_4,
		.compatible = "sharp,lq084v1dg21",
	},
	{
		.id = SYS_CLCD_ID_EPSON_2_2,
		.compatible = "epson,l2f50113t00",
	},
	{
		.id = SYS_CLCD_ID_SANYO_2_5,
		.compatible = "sanyo,alr252rgt",
		.ib2 = true,
	},
};

static void versatile_panel_probe(struct device *dev,
				  struct device_node *endpoint)
{
	struct versatile_panel const *vpanel = NULL;
	struct device_node *panel = NULL;
	u32 val;
	int ret;
	int i;

	/*
	 * The Versatile CLCD has a panel auto-detection mechanism.
	 * We use this and look for the compatible panel in the
	 * device tree.
	 */
	ret = regmap_read(versatile_syscon_map, SYS_CLCD, &val);
	if (ret) {
		dev_err(dev, "cannot read CLCD syscon register\n");
		return;
	}
	val &= SYS_CLCD_CLCDID_MASK;

	/* First find corresponding panel information */
	for (i = 0; i < ARRAY_SIZE(versatile_panels); i++) {
		vpanel = &versatile_panels[i];

		if (val == vpanel->id) {
			dev_err(dev, "autodetected panel \"%s\"\n",
				vpanel->compatible);
			break;
		}
	}
	if (i == ARRAY_SIZE(versatile_panels)) {
		dev_err(dev, "could not auto-detect panel\n");
		return;
	}

	panel = of_graph_get_remote_port_parent(endpoint);
	if (!panel) {
		dev_err(dev, "could not locate panel in DT\n");
		return;
	}
	if (!of_device_is_compatible(panel, vpanel->compatible))
		dev_err(dev, "panel in DT is not compatible with the "
			"auto-detected panel, continuing anyway\n");

	/*
	 * If we have a Sanyo 2.5" port
	 * that we're running on an IB2 and proceed to look for the
	 * IB2 syscon regmap.
	 */
	if (!vpanel->ib2)
		return;

	versatile_ib2_map = syscon_regmap_lookup_by_compatible(
		"arm,versatile-ib2-syscon");
	if (IS_ERR(versatile_ib2_map)) {
		dev_err(dev, "could not locate IB2 control register\n");
		versatile_ib2_map = NULL;
		return;
	}
}

int versatile_clcd_init_panel(struct clcd_fb *fb,
			      struct device_node *endpoint)
{
	const struct of_device_id *clcd_id;
	enum versatile_clcd versatile_clcd_type;
	struct device_node *np;
	struct regmap *map;
	struct device *dev = &fb->dev->dev;

	np = of_find_matching_node_and_match(NULL, versatile_clcd_of_match,
					     &clcd_id);
	if (!np) {
		/* Vexpress does not have this */
		return 0;
	}
	versatile_clcd_type = (enum versatile_clcd)clcd_id->data;

	map = syscon_node_to_regmap(np);
	if (IS_ERR(map)) {
		dev_err(dev, "no Versatile syscon regmap\n");
		return PTR_ERR(map);
	}

	switch (versatile_clcd_type) {
	case INTEGRATOR_CLCD_CM:
		versatile_syscon_map = map;
		fb->board->enable = integrator_clcd_enable;
		/* Override the caps, we have only these */
		fb->board->caps = CLCD_CAP_5551 | CLCD_CAP_RGB565 |
			CLCD_CAP_888;
		dev_info(dev, "set up callbacks for Integrator PL110\n");
		break;
	case VERSATILE_CLCD:
		versatile_syscon_map = map;
		fb->board->enable = versatile_clcd_enable;
		fb->board->disable = versatile_clcd_disable;
		fb->board->decode = versatile_clcd_decode;
		versatile_panel_probe(dev, endpoint);
		dev_info(dev, "set up callbacks for Versatile\n");
		break;
	case REALVIEW_CLCD_EB:
	case REALVIEW_CLCD_PB1176:
	case REALVIEW_CLCD_PB11MP:
	case REALVIEW_CLCD_PBA8:
	case REALVIEW_CLCD_PBX:
		versatile_syscon_map = map;
		fb->board->enable = realview_clcd_enable;
		fb->board->disable = realview_clcd_disable;
		dev_info(dev, "set up callbacks for RealView PL111\n");
		break;
	default:
		dev_info(dev, "unknown Versatile system controller\n");
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(versatile_clcd_init_panel);
#endif
