#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "amba-clcd-nomadik.h"

static struct gpio_desc *grestb;
static struct gpio_desc *scen;
static struct gpio_desc *scl;
static struct gpio_desc *sda;

static u8 tpg110_readwrite_reg(bool write, u8 address, u8 outval)
{
	int i;
	u8 inval = 0;

	/* Assert SCEN */
	gpiod_set_value_cansleep(scen, 1);
	ndelay(150);
	/* Hammer out the address */
	for (i = 5; i >= 0; i--) {
		if (address & BIT(i))
			gpiod_set_value_cansleep(sda, 1);
		else
			gpiod_set_value_cansleep(sda, 0);
		ndelay(150);
		/* Send an SCL pulse */
		gpiod_set_value_cansleep(scl, 1);
		ndelay(160);
		gpiod_set_value_cansleep(scl, 0);
		ndelay(160);
	}

	if (write) {
		/* WRITE */
		gpiod_set_value_cansleep(sda, 0);
	} else {
		/* READ */
		gpiod_set_value_cansleep(sda, 1);
	}
	ndelay(150);
	/* Send an SCL pulse */
	gpiod_set_value_cansleep(scl, 1);
	ndelay(160);
	gpiod_set_value_cansleep(scl, 0);
	ndelay(160);

	if (!write)
		/* HiZ turn-around cycle */
		gpiod_direction_input(sda);
	ndelay(150);
	/* Send an SCL pulse */
	gpiod_set_value_cansleep(scl, 1);
	ndelay(160);
	gpiod_set_value_cansleep(scl, 0);
	ndelay(160);

	/* Hammer in/out the data */
	for (i = 7; i >= 0; i--) {
		int value;

		if (write) {
			value = !!(outval & BIT(i));
			gpiod_set_value_cansleep(sda, value);
		} else {
			value = gpiod_get_value(sda);
			if (value)
				inval |= BIT(i);
		}
		ndelay(150);
		/* Send an SCL pulse */
		gpiod_set_value_cansleep(scl, 1);
		ndelay(160);
		gpiod_set_value_cansleep(scl, 0);
		ndelay(160);
	}

	gpiod_direction_output(sda, 0);
	/* Deassert SCEN */
	gpiod_set_value_cansleep(scen, 0);
	/* Satisfies SCEN pulse width */
	udelay(1);

	return inval;
}

static u8 tpg110_read_reg(u8 address)
{
	return tpg110_readwrite_reg(false, address, 0);
}

static void tpg110_write_reg(u8 address, u8 outval)
{
	tpg110_readwrite_reg(true, address, outval);
}

static void tpg110_startup(struct device *dev)
{
	u8 val;

	dev_info(dev, "TPG110 display enable\n");
	/* De-assert the reset signal */
	gpiod_set_value_cansleep(grestb, 0);
	mdelay(1);
	dev_info(dev, "de-asserted GRESTB\n");

	/* Test display communication */
	tpg110_write_reg(0x00, 0x55);
	val = tpg110_read_reg(0x00);
	if (val == 0x55)
		dev_info(dev, "passed communication test\n");
	val = tpg110_read_reg(0x01);
	dev_info(dev, "TPG110 chip ID: %d version: %d\n",
		val>>4, val&0x0f);

	/* Show display resolution */
	val = tpg110_read_reg(0x02);
	val &= 7;
	switch (val) {
	case 0x0:
		dev_info(dev, "IN 400x240 RGB -> OUT 800x480 RGB (dual scan)");
		break;
	case 0x1:
		dev_info(dev, "IN 480x272 RGB -> OUT 800x480 RGB (dual scan)");
		break;
	case 0x4:
		dev_info(dev, "480x640 RGB");
		break;
	case 0x5:
		dev_info(dev, "480x272 RGB");
		break;
	case 0x6:
		dev_info(dev, "640x480 RGB");
		break;
	case 0x7:
		dev_info(dev, "800x480 RGB");
		break;
	default:
		dev_info(dev, "ILLEGAL RESOLUTION");
		break;
	}

	val = tpg110_read_reg(0x03);
	dev_info(dev, "resolution is controlled by %s\n",
		(val & BIT(7)) ? "software" : "hardware");
}

static void tpg110_enable(struct clcd_fb *fb)
{
	struct device *dev = &fb->dev->dev;
	static bool startup;
	u8 val;

	if (!startup) {
		tpg110_startup(dev);
		startup = true;
	}

	/* Take chip out of standby */
	val = tpg110_read_reg(0x03);
	val |= BIT(0);
	tpg110_write_reg(0x03, val);
}

static void tpg110_disable(struct clcd_fb *fb)
{
	u8 val;

	dev_info(&fb->dev->dev, "TPG110 display disable\n");
	val = tpg110_read_reg(0x03);
	/* Put into standby */
	val &= ~BIT(0);
	tpg110_write_reg(0x03, val);
}

static void tpg110_init(struct device *dev, struct device_node *np,
			struct clcd_board *board)
{
	dev_info(dev, "TPG110 display init\n");

	/* This asserts the GRESTB signal, putting the display into reset */
	grestb = devm_fwnode_get_gpiod_from_child(dev, "grestb", &np->fwnode,
						  GPIOD_OUT_HIGH, "grestb");
	if (IS_ERR(grestb)) {
		dev_err(dev, "no GRESTB GPIO\n");
		return;
	}
	scen = devm_fwnode_get_gpiod_from_child(dev, "scen", &np->fwnode,
						GPIOD_OUT_LOW, "scen");
	if (IS_ERR(scen)) {
		dev_err(dev, "no SCEN GPIO\n");
		return;
	}
	scl = devm_fwnode_get_gpiod_from_child(dev, "scl", &np->fwnode,
					       GPIOD_OUT_LOW, "scl");
	if (IS_ERR(scl)) {
		dev_err(dev, "no SCL GPIO\n");
		return;
	}
	sda = devm_fwnode_get_gpiod_from_child(dev, "sda", &np->fwnode,
					       GPIOD_OUT_LOW, "sda");
	if (IS_ERR(sda)) {
		dev_err(dev, "no SDA GPIO\n");
		return;
	}
	board->enable = tpg110_enable;
	board->disable = tpg110_disable;
}

int nomadik_clcd_init_panel(struct clcd_fb *fb, struct device_node *panel)
{
	if (of_device_is_compatible(panel, "tpo,tpg110"))
		tpg110_init(&fb->dev->dev, panel, fb->board);
	else
		dev_info(&fb->dev->dev, "unknown panel\n");

	/* Unknown panel, fall through */
	return 0;
}
EXPORT_SYMBOL_GPL(nomadik_clcd_init_panel);

#define PMU_CTRL_OFFSET 0x0000
#define PMU_CTRL_LCDNDIF BIT(26)

int nomadik_clcd_init_board(struct amba_device *adev,
			    struct clcd_board *board)
{
	struct regmap *pmu_regmap;

	dev_info(&adev->dev, "Nomadik CLCD board init\n");
	pmu_regmap =
		syscon_regmap_lookup_by_compatible("stericsson,nomadik-pmu");
	if (IS_ERR(pmu_regmap)) {
		dev_err(&adev->dev, "could not find PMU syscon regmap\n");
		return PTR_ERR(pmu_regmap);
	}
	regmap_update_bits(pmu_regmap,
			   PMU_CTRL_OFFSET,
			   PMU_CTRL_LCDNDIF,
			   0);
	dev_info(&adev->dev, "set PMU mux to CLCD mode\n");

	return 0;
}
EXPORT_SYMBOL_GPL(nomadik_clcd_init_board);
