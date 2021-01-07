// SPDX-License-Identifier: GPL-2.0
// SPI to CAN driver for the Texas Instruments TCAN4x5x
// Copyright (C) 2018-19 Texas Instruments Incorporated - http://www.ti.com/

#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>

#include "m_can.h"

#define DEVICE_NAME "tcan4x5x"
#define TCAN4X5X_EXT_CLK_DEF 40000000

#define TCAN4X5X_DEV_ID0 0x00
#define TCAN4X5X_DEV_ID1 0x04
#define TCAN4X5X_REV 0x08
#define TCAN4X5X_STATUS 0x0C
#define TCAN4X5X_ERROR_STATUS 0x10
#define TCAN4X5X_CONTROL 0x14

#define TCAN4X5X_CONFIG 0x800
#define TCAN4X5X_TS_PRESCALE 0x804
#define TCAN4X5X_TEST_REG 0x808
#define TCAN4X5X_INT_FLAGS 0x820
#define TCAN4X5X_MCAN_INT_REG 0x824
#define TCAN4X5X_INT_EN 0x830

/* Interrupt bits */
#define TCAN4X5X_CANBUSTERMOPEN_INT_EN BIT(30)
#define TCAN4X5X_CANHCANL_INT_EN BIT(29)
#define TCAN4X5X_CANHBAT_INT_EN BIT(28)
#define TCAN4X5X_CANLGND_INT_EN BIT(27)
#define TCAN4X5X_CANBUSOPEN_INT_EN BIT(26)
#define TCAN4X5X_CANBUSGND_INT_EN BIT(25)
#define TCAN4X5X_CANBUSBAT_INT_EN BIT(24)
#define TCAN4X5X_UVSUP_INT_EN BIT(22)
#define TCAN4X5X_UVIO_INT_EN BIT(21)
#define TCAN4X5X_TSD_INT_EN BIT(19)
#define TCAN4X5X_ECCERR_INT_EN BIT(16)
#define TCAN4X5X_CANINT_INT_EN BIT(15)
#define TCAN4X5X_LWU_INT_EN BIT(14)
#define TCAN4X5X_CANSLNT_INT_EN BIT(10)
#define TCAN4X5X_CANDOM_INT_EN BIT(8)
#define TCAN4X5X_CANBUS_ERR_INT_EN BIT(5)
#define TCAN4X5X_BUS_FAULT BIT(4)
#define TCAN4X5X_MCAN_INT BIT(1)
#define TCAN4X5X_ENABLE_TCAN_INT \
	(TCAN4X5X_MCAN_INT | TCAN4X5X_BUS_FAULT | \
	 TCAN4X5X_CANBUS_ERR_INT_EN | TCAN4X5X_CANINT_INT_EN)

/* MCAN Interrupt bits */
#define TCAN4X5X_MCAN_IR_ARA BIT(29)
#define TCAN4X5X_MCAN_IR_PED BIT(28)
#define TCAN4X5X_MCAN_IR_PEA BIT(27)
#define TCAN4X5X_MCAN_IR_WD BIT(26)
#define TCAN4X5X_MCAN_IR_BO BIT(25)
#define TCAN4X5X_MCAN_IR_EW BIT(24)
#define TCAN4X5X_MCAN_IR_EP BIT(23)
#define TCAN4X5X_MCAN_IR_ELO BIT(22)
#define TCAN4X5X_MCAN_IR_BEU BIT(21)
#define TCAN4X5X_MCAN_IR_BEC BIT(20)
#define TCAN4X5X_MCAN_IR_DRX BIT(19)
#define TCAN4X5X_MCAN_IR_TOO BIT(18)
#define TCAN4X5X_MCAN_IR_MRAF BIT(17)
#define TCAN4X5X_MCAN_IR_TSW BIT(16)
#define TCAN4X5X_MCAN_IR_TEFL BIT(15)
#define TCAN4X5X_MCAN_IR_TEFF BIT(14)
#define TCAN4X5X_MCAN_IR_TEFW BIT(13)
#define TCAN4X5X_MCAN_IR_TEFN BIT(12)
#define TCAN4X5X_MCAN_IR_TFE BIT(11)
#define TCAN4X5X_MCAN_IR_TCF BIT(10)
#define TCAN4X5X_MCAN_IR_TC BIT(9)
#define TCAN4X5X_MCAN_IR_HPM BIT(8)
#define TCAN4X5X_MCAN_IR_RF1L BIT(7)
#define TCAN4X5X_MCAN_IR_RF1F BIT(6)
#define TCAN4X5X_MCAN_IR_RF1W BIT(5)
#define TCAN4X5X_MCAN_IR_RF1N BIT(4)
#define TCAN4X5X_MCAN_IR_RF0L BIT(3)
#define TCAN4X5X_MCAN_IR_RF0F BIT(2)
#define TCAN4X5X_MCAN_IR_RF0W BIT(1)
#define TCAN4X5X_MCAN_IR_RF0N BIT(0)
#define TCAN4X5X_ENABLE_MCAN_INT \
	(TCAN4X5X_MCAN_IR_TC | TCAN4X5X_MCAN_IR_RF0N | \
	 TCAN4X5X_MCAN_IR_RF1N | TCAN4X5X_MCAN_IR_RF0F | \
	 TCAN4X5X_MCAN_IR_RF1F)

#define TCAN4X5X_MRAM_START 0x8000
#define TCAN4X5X_MCAN_OFFSET 0x1000
#define TCAN4X5X_MAX_REGISTER 0x8fff

#define TCAN4X5X_CLEAR_ALL_INT 0xffffffff
#define TCAN4X5X_SET_ALL_INT 0xffffffff

#define TCAN4X5X_WRITE_CMD (0x61 << 24)
#define TCAN4X5X_READ_CMD (0x41 << 24)

#define TCAN4X5X_MODE_SEL_MASK (BIT(7) | BIT(6))
#define TCAN4X5X_MODE_SLEEP 0x00
#define TCAN4X5X_MODE_STANDBY BIT(6)
#define TCAN4X5X_MODE_NORMAL BIT(7)

#define TCAN4X5X_DISABLE_WAKE_MSK	(BIT(31) | BIT(30))
#define TCAN4X5X_DISABLE_INH_MSK	BIT(9)

#define TCAN4X5X_SW_RESET BIT(2)

#define TCAN4X5X_MCAN_CONFIGURED BIT(5)
#define TCAN4X5X_WATCHDOG_EN BIT(3)
#define TCAN4X5X_WD_60_MS_TIMER 0
#define TCAN4X5X_WD_600_MS_TIMER BIT(28)
#define TCAN4X5X_WD_3_S_TIMER BIT(29)
#define TCAN4X5X_WD_6_S_TIMER (BIT(28) | BIT(29))

struct tcan4x5x_priv {
	struct regmap *regmap;
	struct spi_device *spi;

	struct m_can_classdev *mcan_dev;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *device_wake_gpio;
	struct gpio_desc *device_state_gpio;
	struct regulator *power;

	/* Register based ip */
	int mram_start;
	int reg_offset;
};

static struct can_bittiming_const tcan4x5x_bittiming_const = {
	.name = DEVICE_NAME,
	.tseg1_min = 2,
	.tseg1_max = 31,
	.tseg2_min = 2,
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 32,
	.brp_inc = 1,
};

static struct can_bittiming_const tcan4x5x_data_bittiming_const = {
	.name = DEVICE_NAME,
	.tseg1_min = 1,
	.tseg1_max = 32,
	.tseg2_min = 1,
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 32,
	.brp_inc = 1,
};

static void tcan4x5x_check_wake(struct tcan4x5x_priv *priv)
{
	int wake_state = 0;

	if (priv->device_state_gpio)
		wake_state = gpiod_get_value(priv->device_state_gpio);

	if (priv->device_wake_gpio && wake_state) {
		gpiod_set_value(priv->device_wake_gpio, 0);
		usleep_range(5, 50);
		gpiod_set_value(priv->device_wake_gpio, 1);
	}
}

static int tcan4x5x_reset(struct tcan4x5x_priv *priv)
{
	int ret = 0;

	if (priv->reset_gpio) {
		gpiod_set_value(priv->reset_gpio, 1);

		/* tpulse_width minimum 30us */
		usleep_range(30, 100);
		gpiod_set_value(priv->reset_gpio, 0);
	} else {
		ret = regmap_write(priv->regmap, TCAN4X5X_CONFIG,
				   TCAN4X5X_SW_RESET);
		if (ret)
			return ret;
	}

	usleep_range(700, 1000);

	return ret;
}

static int regmap_spi_gather_write(void *context, const void *reg,
				   size_t reg_len, const void *val,
				   size_t val_len)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	struct spi_message m;
	u32 addr;
	struct spi_transfer t[2] = {
		{ .tx_buf = &addr, .len = reg_len, .cs_change = 0,},
		{ .tx_buf = val, .len = val_len, },
	};

	addr = TCAN4X5X_WRITE_CMD | (*((u16 *)reg) << 8) | val_len >> 2;

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	return spi_sync(spi, &m);
}

static int tcan4x5x_regmap_write(void *context, const void *data, size_t count)
{
	u16 *reg = (u16 *)(data);
	const u32 *val = data + 4;

	return regmap_spi_gather_write(context, reg, 4, val, count - 4);
}

static int regmap_spi_async_write(void *context,
				  const void *reg, size_t reg_len,
				  const void *val, size_t val_len,
				  struct regmap_async *a)
{
	return -ENOTSUPP;
}

static struct regmap_async *regmap_spi_async_alloc(void)
{
	return NULL;
}

static int tcan4x5x_regmap_read(void *context,
				const void *reg, size_t reg_size,
				void *val, size_t val_size)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);

	u32 addr = TCAN4X5X_READ_CMD | (*((u16 *)reg) << 8) | val_size >> 2;

	return spi_write_then_read(spi, &addr, reg_size, (u32 *)val, val_size);
}

static struct regmap_bus tcan4x5x_bus = {
	.write = tcan4x5x_regmap_write,
	.gather_write = regmap_spi_gather_write,
	.async_write = regmap_spi_async_write,
	.async_alloc = regmap_spi_async_alloc,
	.read = tcan4x5x_regmap_read,
	.read_flag_mask = 0x00,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

static u32 tcan4x5x_read_reg(struct m_can_classdev *cdev, int reg)
{
	struct tcan4x5x_priv *priv = cdev->device_data;
	u32 val;

	regmap_read(priv->regmap, priv->reg_offset + reg, &val);

	return val;
}

static u32 tcan4x5x_read_fifo(struct m_can_classdev *cdev, int addr_offset)
{
	struct tcan4x5x_priv *priv = cdev->device_data;
	u32 val;

	regmap_read(priv->regmap, priv->mram_start + addr_offset, &val);

	return val;
}

static int tcan4x5x_write_reg(struct m_can_classdev *cdev, int reg, int val)
{
	struct tcan4x5x_priv *priv = cdev->device_data;

	return regmap_write(priv->regmap, priv->reg_offset + reg, val);
}

static int tcan4x5x_write_fifo(struct m_can_classdev *cdev,
			       int addr_offset, int val)
{
	struct tcan4x5x_priv *priv = cdev->device_data;

	return regmap_write(priv->regmap, priv->mram_start + addr_offset, val);
}

static int tcan4x5x_power_enable(struct regulator *reg, int enable)
{
	if (IS_ERR_OR_NULL(reg))
		return 0;

	if (enable)
		return regulator_enable(reg);
	else
		return regulator_disable(reg);
}

static int tcan4x5x_write_tcan_reg(struct m_can_classdev *cdev,
				   int reg, int val)
{
	struct tcan4x5x_priv *priv = cdev->device_data;

	return regmap_write(priv->regmap, reg, val);
}

static int tcan4x5x_clear_interrupts(struct m_can_classdev *cdev)
{
	int ret;

	ret = tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_STATUS,
				      TCAN4X5X_CLEAR_ALL_INT);
	if (ret)
		return ret;

	ret = tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_MCAN_INT_REG,
				      TCAN4X5X_ENABLE_MCAN_INT);
	if (ret)
		return ret;

	ret = tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_INT_FLAGS,
				      TCAN4X5X_CLEAR_ALL_INT);
	if (ret)
		return ret;

	ret = tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_ERROR_STATUS,
				      TCAN4X5X_CLEAR_ALL_INT);
	if (ret)
		return ret;

	return ret;
}

static int tcan4x5x_init(struct m_can_classdev *cdev)
{
	struct tcan4x5x_priv *tcan4x5x = cdev->device_data;
	int ret;

	tcan4x5x_check_wake(tcan4x5x);

	ret = tcan4x5x_clear_interrupts(cdev);
	if (ret)
		return ret;

	ret = tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_INT_EN,
				      TCAN4X5X_ENABLE_TCAN_INT);
	if (ret)
		return ret;

	ret = regmap_update_bits(tcan4x5x->regmap, TCAN4X5X_CONFIG,
				 TCAN4X5X_MODE_SEL_MASK, TCAN4X5X_MODE_NORMAL);
	if (ret)
		return ret;

	/* Zero out the MCAN buffers */
	m_can_init_ram(cdev);

	return ret;
}

static int tcan4x5x_disable_wake(struct m_can_classdev *cdev)
{
	struct tcan4x5x_priv *tcan4x5x = cdev->device_data;

	return regmap_update_bits(tcan4x5x->regmap, TCAN4X5X_CONFIG,
				  TCAN4X5X_DISABLE_WAKE_MSK, 0x00);
}

static int tcan4x5x_disable_state(struct m_can_classdev *cdev)
{
	struct tcan4x5x_priv *tcan4x5x = cdev->device_data;

	return regmap_update_bits(tcan4x5x->regmap, TCAN4X5X_CONFIG,
				  TCAN4X5X_DISABLE_INH_MSK, 0x01);
}

static int tcan4x5x_parse_config(struct m_can_classdev *cdev)
{
	struct tcan4x5x_priv *tcan4x5x = cdev->device_data;
	int ret;

	tcan4x5x->device_wake_gpio = devm_gpiod_get(cdev->dev, "device-wake",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(tcan4x5x->device_wake_gpio)) {
		if (PTR_ERR(tcan4x5x->device_wake_gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		tcan4x5x_disable_wake(cdev);
	}

	tcan4x5x->reset_gpio = devm_gpiod_get_optional(cdev->dev, "reset",
						       GPIOD_OUT_LOW);
	if (IS_ERR(tcan4x5x->reset_gpio))
		tcan4x5x->reset_gpio = NULL;

	ret = tcan4x5x_reset(tcan4x5x);
	if (ret)
		return ret;

	tcan4x5x->device_state_gpio = devm_gpiod_get_optional(cdev->dev,
							      "device-state",
							      GPIOD_IN);
	if (IS_ERR(tcan4x5x->device_state_gpio)) {
		tcan4x5x->device_state_gpio = NULL;
		tcan4x5x_disable_state(cdev);
	}

	return 0;
}

static const struct regmap_config tcan4x5x_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
	.cache_type = REGCACHE_NONE,
	.max_register = TCAN4X5X_MAX_REGISTER,
};

static struct m_can_ops tcan4x5x_ops = {
	.init = tcan4x5x_init,
	.read_reg = tcan4x5x_read_reg,
	.write_reg = tcan4x5x_write_reg,
	.write_fifo = tcan4x5x_write_fifo,
	.read_fifo = tcan4x5x_read_fifo,
	.clear_interrupts = tcan4x5x_clear_interrupts,
};

static int tcan4x5x_can_probe(struct spi_device *spi)
{
	struct tcan4x5x_priv *priv;
	struct m_can_classdev *mcan_class;
	int freq, ret;

	mcan_class = m_can_class_allocate_dev(&spi->dev);
	if (!mcan_class)
		return -ENOMEM;

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto out_m_can_class_free_dev;
	}

	priv->power = devm_regulator_get_optional(&spi->dev, "vsup");
	if (PTR_ERR(priv->power) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto out_m_can_class_free_dev;
	} else {
		priv->power = NULL;
	}

	mcan_class->device_data = priv;

	m_can_class_get_clocks(mcan_class);
	if (IS_ERR(mcan_class->cclk)) {
		dev_err(&spi->dev, "no CAN clock source defined\n");
		freq = TCAN4X5X_EXT_CLK_DEF;
	} else {
		freq = clk_get_rate(mcan_class->cclk);
	}

	/* Sanity check */
	if (freq < 20000000 || freq > TCAN4X5X_EXT_CLK_DEF) {
		ret = -ERANGE;
		goto out_m_can_class_free_dev;
	}

	priv->reg_offset = TCAN4X5X_MCAN_OFFSET;
	priv->mram_start = TCAN4X5X_MRAM_START;
	priv->spi = spi;
	priv->mcan_dev = mcan_class;

	mcan_class->pm_clock_support = 0;
	mcan_class->can.clock.freq = freq;
	mcan_class->dev = &spi->dev;
	mcan_class->ops = &tcan4x5x_ops;
	mcan_class->is_peripheral = true;
	mcan_class->bit_timing = &tcan4x5x_bittiming_const;
	mcan_class->data_timing = &tcan4x5x_data_bittiming_const;
	mcan_class->net->irq = spi->irq;

	spi_set_drvdata(spi, priv);

	/* Configure the SPI bus */
	spi->bits_per_word = 32;
	ret = spi_setup(spi);
	if (ret)
		goto out_m_can_class_free_dev;

	priv->regmap = devm_regmap_init(&spi->dev, &tcan4x5x_bus,
					&spi->dev, &tcan4x5x_regmap);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		goto out_m_can_class_free_dev;
	}

	ret = tcan4x5x_power_enable(priv->power, 1);
	if (ret)
		goto out_m_can_class_free_dev;

	ret = tcan4x5x_parse_config(mcan_class);
	if (ret)
		goto out_power;

	ret = tcan4x5x_init(mcan_class);
	if (ret)
		goto out_power;

	ret = m_can_class_register(mcan_class);
	if (ret)
		goto out_power;

	netdev_info(mcan_class->net, "TCAN4X5X successfully initialized.\n");
	return 0;

out_power:
	tcan4x5x_power_enable(priv->power, 0);
 out_m_can_class_free_dev:
	m_can_class_free_dev(mcan_class->net);
	dev_err(&spi->dev, "Probe failed, err=%d\n", ret);

	return ret;
}

static int tcan4x5x_can_remove(struct spi_device *spi)
{
	struct tcan4x5x_priv *priv = spi_get_drvdata(spi);

	m_can_class_unregister(priv->mcan_dev);

	tcan4x5x_power_enable(priv->power, 0);

	m_can_class_free_dev(priv->mcan_dev->net);

	return 0;
}

static const struct of_device_id tcan4x5x_of_match[] = {
	{ .compatible = "ti,tcan4x5x", },
	{ }
};
MODULE_DEVICE_TABLE(of, tcan4x5x_of_match);

static const struct spi_device_id tcan4x5x_id_table[] = {
	{
		.name		= "tcan4x5x",
		.driver_data	= 0,
	},
	{ }
};
MODULE_DEVICE_TABLE(spi, tcan4x5x_id_table);

static struct spi_driver tcan4x5x_can_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = tcan4x5x_of_match,
		.pm = NULL,
	},
	.id_table = tcan4x5x_id_table,
	.probe = tcan4x5x_can_probe,
	.remove = tcan4x5x_can_remove,
};
module_spi_driver(tcan4x5x_can_driver);

MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_DESCRIPTION("Texas Instruments TCAN4x5x CAN driver");
MODULE_LICENSE("GPL v2");
