// SPDX-License-Identifier: GPL-2.0
// SPI to CAN driver for the Texas Instruments TCAN4x5x
// Copyright (C) 2018-19 Texas Instruments Incorporated - http://www.ti.com/

#include "tcan4x5x.h"

#define TCAN4X5X_EXT_CLK_DEF 40000000

#define TCAN4X5X_DEV_ID0 0x00
#define TCAN4X5X_DEV_ID1 0x04
#define TCAN4X5X_REV 0x08
#define TCAN4X5X_STATUS 0x0C
#define TCAN4X5X_ERROR_STATUS_MASK 0x10
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

#define TCAN4X5X_CLEAR_ALL_INT 0xffffffff
#define TCAN4X5X_SET_ALL_INT 0xffffffff

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

static inline struct tcan4x5x_priv *cdev_to_priv(struct m_can_classdev *cdev)
{
	return container_of(cdev, struct tcan4x5x_priv, cdev);
}

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

static u32 tcan4x5x_read_reg(struct m_can_classdev *cdev, int reg)
{
	struct tcan4x5x_priv *priv = cdev_to_priv(cdev);
	u32 val;

	regmap_read(priv->regmap, TCAN4X5X_MCAN_OFFSET + reg, &val);

	return val;
}

static int tcan4x5x_read_fifo(struct m_can_classdev *cdev, int addr_offset,
			      void *val, size_t val_count)
{
	struct tcan4x5x_priv *priv = cdev_to_priv(cdev);

	return regmap_bulk_read(priv->regmap, TCAN4X5X_MRAM_START + addr_offset, val, val_count);
}

static int tcan4x5x_write_reg(struct m_can_classdev *cdev, int reg, int val)
{
	struct tcan4x5x_priv *priv = cdev_to_priv(cdev);

	return regmap_write(priv->regmap, TCAN4X5X_MCAN_OFFSET + reg, val);
}

static int tcan4x5x_write_fifo(struct m_can_classdev *cdev,
			       int addr_offset, const void *val, size_t val_count)
{
	struct tcan4x5x_priv *priv = cdev_to_priv(cdev);

	return regmap_bulk_write(priv->regmap, TCAN4X5X_MRAM_START + addr_offset, val, val_count);
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
	struct tcan4x5x_priv *priv = cdev_to_priv(cdev);

	return regmap_write(priv->regmap, reg, val);
}

static int tcan4x5x_clear_interrupts(struct m_can_classdev *cdev)
{
	int ret;

	ret = tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_STATUS,
				      TCAN4X5X_CLEAR_ALL_INT);
	if (ret)
		return ret;

	return tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_INT_FLAGS,
				       TCAN4X5X_CLEAR_ALL_INT);
}

static int tcan4x5x_init(struct m_can_classdev *cdev)
{
	struct tcan4x5x_priv *tcan4x5x = cdev_to_priv(cdev);
	int ret;

	tcan4x5x_check_wake(tcan4x5x);

	ret = tcan4x5x_clear_interrupts(cdev);
	if (ret)
		return ret;

	ret = tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_INT_EN,
				      TCAN4X5X_ENABLE_TCAN_INT);
	if (ret)
		return ret;

	ret = tcan4x5x_write_tcan_reg(cdev, TCAN4X5X_ERROR_STATUS_MASK,
				      TCAN4X5X_CLEAR_ALL_INT);
	if (ret)
		return ret;

	ret = regmap_update_bits(tcan4x5x->regmap, TCAN4X5X_CONFIG,
				 TCAN4X5X_MODE_SEL_MASK, TCAN4X5X_MODE_NORMAL);
	if (ret)
		return ret;

	return ret;
}

static int tcan4x5x_disable_wake(struct m_can_classdev *cdev)
{
	struct tcan4x5x_priv *tcan4x5x = cdev_to_priv(cdev);

	return regmap_update_bits(tcan4x5x->regmap, TCAN4X5X_CONFIG,
				  TCAN4X5X_DISABLE_WAKE_MSK, 0x00);
}

static int tcan4x5x_disable_state(struct m_can_classdev *cdev)
{
	struct tcan4x5x_priv *tcan4x5x = cdev_to_priv(cdev);

	return regmap_update_bits(tcan4x5x->regmap, TCAN4X5X_CONFIG,
				  TCAN4X5X_DISABLE_INH_MSK, 0x01);
}

static int tcan4x5x_get_gpios(struct m_can_classdev *cdev)
{
	struct tcan4x5x_priv *tcan4x5x = cdev_to_priv(cdev);
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

	mcan_class = m_can_class_allocate_dev(&spi->dev,
					      sizeof(struct tcan4x5x_priv));
	if (!mcan_class)
		return -ENOMEM;

	priv = cdev_to_priv(mcan_class);

	priv->power = devm_regulator_get_optional(&spi->dev, "vsup");
	if (PTR_ERR(priv->power) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto out_m_can_class_free_dev;
	} else {
		priv->power = NULL;
	}

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

	priv->spi = spi;

	mcan_class->pm_clock_support = 0;
	mcan_class->can.clock.freq = freq;
	mcan_class->dev = &spi->dev;
	mcan_class->ops = &tcan4x5x_ops;
	mcan_class->is_peripheral = true;
	mcan_class->net->irq = spi->irq;

	spi_set_drvdata(spi, priv);

	/* Configure the SPI bus */
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		goto out_m_can_class_free_dev;

	ret = tcan4x5x_regmap_init(priv);
	if (ret)
		goto out_m_can_class_free_dev;

	ret = tcan4x5x_power_enable(priv->power, 1);
	if (ret)
		goto out_m_can_class_free_dev;

	ret = tcan4x5x_get_gpios(mcan_class);
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
	return ret;
}

static void tcan4x5x_can_remove(struct spi_device *spi)
{
	struct tcan4x5x_priv *priv = spi_get_drvdata(spi);

	m_can_class_unregister(&priv->cdev);

	tcan4x5x_power_enable(priv->power, 0);

	m_can_class_free_dev(priv->cdev.net);
}

static const struct of_device_id tcan4x5x_of_match[] = {
	{
		.compatible = "ti,tcan4x5x",
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, tcan4x5x_of_match);

static const struct spi_device_id tcan4x5x_id_table[] = {
	{
		.name = "tcan4x5x",
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(spi, tcan4x5x_id_table);

static struct spi_driver tcan4x5x_can_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
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
