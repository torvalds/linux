// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

/* PMIC global control registers definition */
#define SC27XX_MODULE_EN0		0xc08
#define SC27XX_CLK_EN0			0xc18
#define SC27XX_FGU_EN			BIT(7)
#define SC27XX_FGU_RTC_EN		BIT(6)

/* FGU registers definition */
#define SC27XX_FGU_START		0x0
#define SC27XX_FGU_CONFIG		0x4
#define SC27XX_FGU_ADC_CONFIG		0x8
#define SC27XX_FGU_STATUS		0xc
#define SC27XX_FGU_INT_EN		0x10
#define SC27XX_FGU_INT_CLR		0x14
#define SC27XX_FGU_INT_STS		0x1c
#define SC27XX_FGU_VOLTAGE		0x20
#define SC27XX_FGU_OCV			0x24
#define SC27XX_FGU_POCV			0x28
#define SC27XX_FGU_CURRENT		0x2c
#define SC27XX_FGU_CLBCNT_SETH		0x50
#define SC27XX_FGU_CLBCNT_SETL		0x54
#define SC27XX_FGU_CLBCNT_VALH		0x68
#define SC27XX_FGU_CLBCNT_VALL		0x6c
#define SC27XX_FGU_CLBCNT_QMAXL		0x74

#define SC27XX_WRITE_SELCLB_EN		BIT(0)
#define SC27XX_FGU_CLBCNT_MASK		GENMASK(15, 0)
#define SC27XX_FGU_CLBCNT_SHIFT		16

#define SC27XX_FGU_1000MV_ADC		686
#define SC27XX_FGU_1000MA_ADC		1372
#define SC27XX_FGU_CUR_BASIC_ADC	8192
#define SC27XX_FGU_SAMPLE_HZ		2

/*
 * struct sc27xx_fgu_data: describe the FGU device
 * @regmap: regmap for register access
 * @dev: platform device
 * @battery: battery power supply
 * @base: the base offset for the controller
 * @lock: protect the structure
 * @gpiod: GPIO for battery detection
 * @channel: IIO channel to get battery temperature
 * @internal_resist: the battery internal resistance in mOhm
 * @total_cap: the total capacity of the battery in mAh
 * @init_cap: the initial capacity of the battery in mAh
 * @init_clbcnt: the initial coulomb counter
 * @max_volt: the maximum constant input voltage in millivolt
 * @table_len: the capacity table length
 * @cap_table: capacity table with corresponding ocv
 */
struct sc27xx_fgu_data {
	struct regmap *regmap;
	struct device *dev;
	struct power_supply *battery;
	u32 base;
	struct mutex lock;
	struct gpio_desc *gpiod;
	struct iio_channel *channel;
	bool bat_present;
	int internal_resist;
	int total_cap;
	int init_cap;
	int init_clbcnt;
	int max_volt;
	int table_len;
	struct power_supply_battery_ocv_table *cap_table;
};

static const char * const sc27xx_charger_supply_name[] = {
	"sc2731_charger",
	"sc2720_charger",
	"sc2721_charger",
	"sc2723_charger",
};

static int sc27xx_fgu_adc_to_current(int adc)
{
	return DIV_ROUND_CLOSEST(adc * 1000, SC27XX_FGU_1000MA_ADC);
}

static int sc27xx_fgu_adc_to_voltage(int adc)
{
	return DIV_ROUND_CLOSEST(adc * 1000, SC27XX_FGU_1000MV_ADC);
}

/*
 * When system boots on, we can not read battery capacity from coulomb
 * registers, since now the coulomb registers are invalid. So we should
 * calculate the battery open circuit voltage, and get current battery
 * capacity according to the capacity table.
 */
static int sc27xx_fgu_get_boot_capacity(struct sc27xx_fgu_data *data, int *cap)
{
	int volt, cur, oci, ocv, ret;

	/*
	 * After system booting on, the SC27XX_FGU_CLBCNT_QMAXL register saved
	 * the first sampled open circuit current.
	 */
	ret = regmap_read(data->regmap, data->base + SC27XX_FGU_CLBCNT_QMAXL,
			  &cur);
	if (ret)
		return ret;

	cur <<= 1;
	oci = sc27xx_fgu_adc_to_current(cur - SC27XX_FGU_CUR_BASIC_ADC);

	/*
	 * Should get the OCV from SC27XX_FGU_POCV register at the system
	 * beginning. It is ADC values reading from registers which need to
	 * convert the corresponding voltage.
	 */
	ret = regmap_read(data->regmap, data->base + SC27XX_FGU_POCV, &volt);
	if (ret)
		return ret;

	volt = sc27xx_fgu_adc_to_voltage(volt);
	ocv = volt * 1000 - oci * data->internal_resist;

	/*
	 * Parse the capacity table to look up the correct capacity percent
	 * according to current battery's corresponding OCV values.
	 */
	*cap = power_supply_ocv2cap_simple(data->cap_table, data->table_len,
					   ocv);

	return 0;
}

static int sc27xx_fgu_set_clbcnt(struct sc27xx_fgu_data *data, int clbcnt)
{
	int ret;

	clbcnt *= SC27XX_FGU_SAMPLE_HZ;

	ret = regmap_update_bits(data->regmap,
				 data->base + SC27XX_FGU_CLBCNT_SETL,
				 SC27XX_FGU_CLBCNT_MASK, clbcnt);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap,
				 data->base + SC27XX_FGU_CLBCNT_SETH,
				 SC27XX_FGU_CLBCNT_MASK,
				 clbcnt >> SC27XX_FGU_CLBCNT_SHIFT);
	if (ret)
		return ret;

	return regmap_update_bits(data->regmap, data->base + SC27XX_FGU_START,
				 SC27XX_WRITE_SELCLB_EN,
				 SC27XX_WRITE_SELCLB_EN);
}

static int sc27xx_fgu_get_clbcnt(struct sc27xx_fgu_data *data, int *clb_cnt)
{
	int ccl, cch, ret;

	ret = regmap_read(data->regmap, data->base + SC27XX_FGU_CLBCNT_VALL,
			  &ccl);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, data->base + SC27XX_FGU_CLBCNT_VALH,
			  &cch);
	if (ret)
		return ret;

	*clb_cnt = ccl & SC27XX_FGU_CLBCNT_MASK;
	*clb_cnt |= (cch & SC27XX_FGU_CLBCNT_MASK) << SC27XX_FGU_CLBCNT_SHIFT;
	*clb_cnt /= SC27XX_FGU_SAMPLE_HZ;

	return 0;
}

static int sc27xx_fgu_get_capacity(struct sc27xx_fgu_data *data, int *cap)
{
	int ret, cur_clbcnt, delta_clbcnt, delta_cap, temp;

	/* Get current coulomb counters firstly */
	ret = sc27xx_fgu_get_clbcnt(data, &cur_clbcnt);
	if (ret)
		return ret;

	delta_clbcnt = cur_clbcnt - data->init_clbcnt;

	/*
	 * Convert coulomb counter to delta capacity (mAh), and set multiplier
	 * as 100 to improve the precision.
	 */
	temp = DIV_ROUND_CLOSEST(delta_clbcnt, 360);
	temp = sc27xx_fgu_adc_to_current(temp);

	/*
	 * Convert to capacity percent of the battery total capacity,
	 * and multiplier is 100 too.
	 */
	delta_cap = DIV_ROUND_CLOSEST(temp * 100, data->total_cap);
	*cap = delta_cap + data->init_cap;

	return 0;
}

static int sc27xx_fgu_get_vbat_vol(struct sc27xx_fgu_data *data, int *val)
{
	int ret, vol;

	ret = regmap_read(data->regmap, data->base + SC27XX_FGU_VOLTAGE, &vol);
	if (ret)
		return ret;

	/*
	 * It is ADC values reading from registers which need to convert to
	 * corresponding voltage values.
	 */
	*val = sc27xx_fgu_adc_to_voltage(vol);

	return 0;
}

static int sc27xx_fgu_get_current(struct sc27xx_fgu_data *data, int *val)
{
	int ret, cur;

	ret = regmap_read(data->regmap, data->base + SC27XX_FGU_CURRENT, &cur);
	if (ret)
		return ret;

	/*
	 * It is ADC values reading from registers which need to convert to
	 * corresponding current values.
	 */
	*val = sc27xx_fgu_adc_to_current(cur - SC27XX_FGU_CUR_BASIC_ADC);

	return 0;
}

static int sc27xx_fgu_get_vbat_ocv(struct sc27xx_fgu_data *data, int *val)
{
	int vol, cur, ret;

	ret = sc27xx_fgu_get_vbat_vol(data, &vol);
	if (ret)
		return ret;

	ret = sc27xx_fgu_get_current(data, &cur);
	if (ret)
		return ret;

	/* Return the battery OCV in micro volts. */
	*val = vol * 1000 - cur * data->internal_resist;

	return 0;
}

static int sc27xx_fgu_get_temp(struct sc27xx_fgu_data *data, int *temp)
{
	return iio_read_channel_processed(data->channel, temp);
}

static int sc27xx_fgu_get_health(struct sc27xx_fgu_data *data, int *health)
{
	int ret, vol;

	ret = sc27xx_fgu_get_vbat_vol(data, &vol);
	if (ret)
		return ret;

	if (vol > data->max_volt)
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else
		*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int sc27xx_fgu_get_status(struct sc27xx_fgu_data *data, int *status)
{
	union power_supply_propval val;
	struct power_supply *psy;
	int i, ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(sc27xx_charger_supply_name); i++) {
		psy = power_supply_get_by_name(sc27xx_charger_supply_name[i]);
		if (!psy)
			continue;

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_STATUS,
						&val);
		power_supply_put(psy);
		if (ret)
			return ret;

		*status = val.intval;
	}

	return ret;
}

static int sc27xx_fgu_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct sc27xx_fgu_data *data = power_supply_get_drvdata(psy);
	int ret = 0;
	int value;

	mutex_lock(&data->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = sc27xx_fgu_get_status(data, &value);
		if (ret)
			goto error;

		val->intval = value;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		ret = sc27xx_fgu_get_health(data, &value);
		if (ret)
			goto error;

		val->intval = value;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = data->bat_present;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		ret = sc27xx_fgu_get_temp(data, &value);
		if (ret)
			goto error;

		val->intval = value;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		ret = sc27xx_fgu_get_capacity(data, &value);
		if (ret)
			goto error;

		val->intval = value;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sc27xx_fgu_get_vbat_vol(data, &value);
		if (ret)
			goto error;

		val->intval = value * 1000;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		ret = sc27xx_fgu_get_vbat_ocv(data, &value);
		if (ret)
			goto error;

		val->intval = value;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = sc27xx_fgu_get_current(data, &value);
		if (ret)
			goto error;

		val->intval = value * 1000;
		break;

	default:
		ret = -EINVAL;
		break;
	}

error:
	mutex_unlock(&data->lock);
	return ret;
}

static void sc27xx_fgu_external_power_changed(struct power_supply *psy)
{
	struct sc27xx_fgu_data *data = power_supply_get_drvdata(psy);

	power_supply_changed(data->battery);
}

static enum power_supply_property sc27xx_fgu_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
};

static const struct power_supply_desc sc27xx_fgu_desc = {
	.name			= "sc27xx-fgu",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= sc27xx_fgu_props,
	.num_properties		= ARRAY_SIZE(sc27xx_fgu_props),
	.get_property		= sc27xx_fgu_get_property,
	.external_power_changed	= sc27xx_fgu_external_power_changed,
};

static irqreturn_t sc27xx_fgu_bat_detection(int irq, void *dev_id)
{
	struct sc27xx_fgu_data *data = dev_id;
	int state;

	mutex_lock(&data->lock);

	state = gpiod_get_value_cansleep(data->gpiod);
	if (state < 0) {
		dev_err(data->dev, "failed to get gpio state\n");
		mutex_unlock(&data->lock);
		return IRQ_RETVAL(state);
	}

	data->bat_present = !!state;

	mutex_unlock(&data->lock);

	power_supply_changed(data->battery);
	return IRQ_HANDLED;
}

static void sc27xx_fgu_disable(void *_data)
{
	struct sc27xx_fgu_data *data = _data;

	regmap_update_bits(data->regmap, SC27XX_CLK_EN0, SC27XX_FGU_RTC_EN, 0);
	regmap_update_bits(data->regmap, SC27XX_MODULE_EN0, SC27XX_FGU_EN, 0);
}

static int sc27xx_fgu_cap_to_clbcnt(struct sc27xx_fgu_data *data, int capacity)
{
	/*
	 * Get current capacity (mAh) = battery total capacity (mAh) *
	 * current capacity percent (capacity / 100).
	 */
	int cur_cap = DIV_ROUND_CLOSEST(data->total_cap * capacity, 100);

	/*
	 * Convert current capacity (mAh) to coulomb counter according to the
	 * formula: 1 mAh =3.6 coulomb.
	 */
	return DIV_ROUND_CLOSEST(cur_cap * 36, 10);
}

static int sc27xx_fgu_hw_init(struct sc27xx_fgu_data *data)
{
	struct power_supply_battery_info info = { };
	struct power_supply_battery_ocv_table *table;
	int ret;

	ret = power_supply_get_battery_info(data->battery, &info);
	if (ret) {
		dev_err(data->dev, "failed to get battery information\n");
		return ret;
	}

	data->total_cap = info.charge_full_design_uah / 1000;
	data->max_volt = info.constant_charge_voltage_max_uv / 1000;
	data->internal_resist = info.factory_internal_resistance_uohm / 1000;

	/*
	 * For SC27XX fuel gauge device, we only use one ocv-capacity
	 * table in normal temperature 20 Celsius.
	 */
	table = power_supply_find_ocv2cap_table(&info, 20, &data->table_len);
	if (!table)
		return -EINVAL;

	data->cap_table = devm_kmemdup(data->dev, table,
				       data->table_len * sizeof(*table),
				       GFP_KERNEL);
	if (!data->cap_table) {
		power_supply_put_battery_info(data->battery, &info);
		return -ENOMEM;
	}

	power_supply_put_battery_info(data->battery, &info);

	/* Enable the FGU module */
	ret = regmap_update_bits(data->regmap, SC27XX_MODULE_EN0,
				 SC27XX_FGU_EN, SC27XX_FGU_EN);
	if (ret) {
		dev_err(data->dev, "failed to enable fgu\n");
		return ret;
	}

	/* Enable the FGU RTC clock to make it work */
	ret = regmap_update_bits(data->regmap, SC27XX_CLK_EN0,
				 SC27XX_FGU_RTC_EN, SC27XX_FGU_RTC_EN);
	if (ret) {
		dev_err(data->dev, "failed to enable fgu RTC clock\n");
		goto disable_fgu;
	}

	/*
	 * Get the boot battery capacity when system powers on, which is used to
	 * initialize the coulomb counter. After that, we can read the coulomb
	 * counter to measure the battery capacity.
	 */
	ret = sc27xx_fgu_get_boot_capacity(data, &data->init_cap);
	if (ret) {
		dev_err(data->dev, "failed to get boot capacity\n");
		goto disable_clk;
	}

	/*
	 * Convert battery capacity to the corresponding initial coulomb counter
	 * and set into coulomb counter registers.
	 */
	data->init_clbcnt = sc27xx_fgu_cap_to_clbcnt(data, data->init_cap);
	ret = sc27xx_fgu_set_clbcnt(data, data->init_clbcnt);
	if (ret) {
		dev_err(data->dev, "failed to initialize coulomb counter\n");
		goto disable_clk;
	}

	return 0;

disable_clk:
	regmap_update_bits(data->regmap, SC27XX_CLK_EN0, SC27XX_FGU_RTC_EN, 0);
disable_fgu:
	regmap_update_bits(data->regmap, SC27XX_MODULE_EN0, SC27XX_FGU_EN, 0);

	return ret;
}

static int sc27xx_fgu_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct power_supply_config fgu_cfg = { };
	struct sc27xx_fgu_data *data;
	int ret, irq;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!data->regmap) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return -ENODEV;
	}

	ret = device_property_read_u32(&pdev->dev, "reg", &data->base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get fgu address\n");
		return ret;
	}

	data->channel = devm_iio_channel_get(&pdev->dev, "bat-temp");
	if (IS_ERR(data->channel)) {
		dev_err(&pdev->dev, "failed to get IIO channel\n");
		return PTR_ERR(data->channel);
	}

	data->gpiod = devm_gpiod_get(&pdev->dev, "bat-detect", GPIOD_IN);
	if (IS_ERR(data->gpiod)) {
		dev_err(&pdev->dev, "failed to get battery detection GPIO\n");
		return PTR_ERR(data->gpiod);
	}

	ret = gpiod_get_value_cansleep(data->gpiod);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get gpio state\n");
		return ret;
	}

	data->bat_present = !!ret;
	mutex_init(&data->lock);
	data->dev = &pdev->dev;

	fgu_cfg.drv_data = data;
	fgu_cfg.of_node = np;
	data->battery = devm_power_supply_register(&pdev->dev, &sc27xx_fgu_desc,
						   &fgu_cfg);
	if (IS_ERR(data->battery)) {
		dev_err(&pdev->dev, "failed to register power supply\n");
		return PTR_ERR(data->battery);
	}

	ret = sc27xx_fgu_hw_init(data);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize fgu hardware\n");
		return ret;
	}

	ret = devm_add_action(&pdev->dev, sc27xx_fgu_disable, data);
	if (ret) {
		sc27xx_fgu_disable(data);
		dev_err(&pdev->dev, "failed to add fgu disable action\n");
		return ret;
	}

	irq = gpiod_to_irq(data->gpiod);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to translate GPIO to IRQ\n");
		return irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					sc27xx_fgu_bat_detection,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
					pdev->name, data);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id sc27xx_fgu_of_match[] = {
	{ .compatible = "sprd,sc2731-fgu", },
	{ }
};

static struct platform_driver sc27xx_fgu_driver = {
	.probe = sc27xx_fgu_probe,
	.driver = {
		.name = "sc27xx-fgu",
		.of_match_table = sc27xx_fgu_of_match,
	}
};

module_platform_driver(sc27xx_fgu_driver);

MODULE_DESCRIPTION("Spreadtrum SC27XX PMICs Fual Gauge Unit Driver");
MODULE_LICENSE("GPL v2");
