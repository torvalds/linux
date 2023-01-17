// SPDX-License-Identifier: GPL-2.0-only
/*
 * Windfarm PowerMac thermal control. AD7417 sensors
 *
 * Copyright 2012 Benjamin Herrenschmidt, IBM Corp.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/i2c.h>

#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/sections.h>

#include "windfarm.h"
#include "windfarm_mpu.h"

#define VERSION "1.0"

struct wf_ad7417_priv {
	struct kref		ref;
	struct i2c_client	*i2c;
	u8			config;
	u8			cpu;
	const struct mpu_data	*mpu;
	struct wf_sensor	sensors[5];
	struct mutex		lock;
};

static int wf_ad7417_temp_get(struct wf_sensor *sr, s32 *value)
{
	struct wf_ad7417_priv *pv = sr->priv;
	u8 buf[2];
	s16 raw;
	int rc;

	*value = 0;
	mutex_lock(&pv->lock);

	/* Read temp register */
	buf[0] = 0;
	rc = i2c_master_send(pv->i2c, buf, 1);
	if (rc < 0)
		goto error;
	rc = i2c_master_recv(pv->i2c, buf, 2);
	if (rc < 0)
		goto error;

	/* Read a a 16-bit signed value */
	raw = be16_to_cpup((__le16 *)buf);

	/* Convert 8.8-bit to 16.16 fixed point */
	*value = ((s32)raw) << 8;

	mutex_unlock(&pv->lock);
	return 0;

error:
	mutex_unlock(&pv->lock);
	return -1;
}

/*
 * Scaling factors for the AD7417 ADC converters (except
 * for the CPU diode which is obtained from the EEPROM).
 * Those values are obtained from the property list of
 * the darwin driver
 */
#define ADC_12V_CURRENT_SCALE	0x0320	/* _AD2 */
#define ADC_CPU_VOLTAGE_SCALE	0x00a0	/* _AD3 */
#define ADC_CPU_CURRENT_SCALE	0x1f40	/* _AD4 */

static void wf_ad7417_adc_convert(struct wf_ad7417_priv *pv,
				  int chan, s32 raw, s32 *value)
{
	switch(chan) {
	case 1: /* Diode */
		*value = (raw * (s32)pv->mpu->mdiode +
			((s32)pv->mpu->bdiode << 12)) >> 2;
		break;
	case 2: /* 12v current */
		*value = raw * ADC_12V_CURRENT_SCALE;
		break;
	case 3: /* core voltage */
		*value = raw * ADC_CPU_VOLTAGE_SCALE;
		break;
	case 4: /* core current */
		*value = raw * ADC_CPU_CURRENT_SCALE;
		break;
	}
}

static int wf_ad7417_adc_get(struct wf_sensor *sr, s32 *value)
{
	struct wf_ad7417_priv *pv = sr->priv;
	int chan = sr - pv->sensors;
	int i, rc;
	u8 buf[2];
	u16 raw;

	*value = 0;
	mutex_lock(&pv->lock);
	for (i = 0; i < 10; i++) {
		/* Set channel */
		buf[0] = 1;
		buf[1] = (pv->config & 0x1f) | (chan << 5);
		rc = i2c_master_send(pv->i2c, buf, 2);
		if (rc < 0)
			goto error;

		/* Wait for conversion */
		msleep(1);

		/* Switch to data register */
		buf[0] = 4;
		rc = i2c_master_send(pv->i2c, buf, 1);
		if (rc < 0)
			goto error;

		/* Read result */
		rc = i2c_master_recv(pv->i2c, buf, 2);
		if (rc < 0)
			goto error;

		/* Read a a 16-bit signed value */
		raw = be16_to_cpup((__le16 *)buf) >> 6;
		wf_ad7417_adc_convert(pv, chan, raw, value);

		dev_vdbg(&pv->i2c->dev, "ADC chan %d [%s]"
			 " raw value: 0x%x, conv to: 0x%08x\n",
			 chan, sr->name, raw, *value);

		mutex_unlock(&pv->lock);
		return 0;

	error:
		dev_dbg(&pv->i2c->dev,
			  "Error reading ADC, try %d...\n", i);
		if (i < 9)
			msleep(10);
	}
	mutex_unlock(&pv->lock);
	return -1;
}

static void wf_ad7417_release(struct kref *ref)
{
	struct wf_ad7417_priv *pv = container_of(ref,
						 struct wf_ad7417_priv, ref);
	kfree(pv);
}

static void wf_ad7417_sensor_release(struct wf_sensor *sr)
{
	struct wf_ad7417_priv *pv = sr->priv;

	kfree(sr->name);
	kref_put(&pv->ref, wf_ad7417_release);
}

static const struct wf_sensor_ops wf_ad7417_temp_ops = {
	.get_value	= wf_ad7417_temp_get,
	.release	= wf_ad7417_sensor_release,
	.owner		= THIS_MODULE,
};

static const struct wf_sensor_ops wf_ad7417_adc_ops = {
	.get_value	= wf_ad7417_adc_get,
	.release	= wf_ad7417_sensor_release,
	.owner		= THIS_MODULE,
};

static void wf_ad7417_add_sensor(struct wf_ad7417_priv *pv,
				 int index, const char *name,
				 const struct wf_sensor_ops *ops)
{
	pv->sensors[index].name = kasprintf(GFP_KERNEL, "%s-%d", name, pv->cpu);
	pv->sensors[index].priv = pv;
	pv->sensors[index].ops = ops;
	if (!wf_register_sensor(&pv->sensors[index]))
		kref_get(&pv->ref);
}

static void wf_ad7417_init_chip(struct wf_ad7417_priv *pv)
{
	int rc;
	u8 buf[2];
	u8 config = 0;

	/*
	 * Read ADC the configuration register and cache it. We
	 * also make sure Config2 contains proper values, I've seen
	 * cases where we got stale grabage in there, thus preventing
	 * proper reading of conv. values
	 */

	/* Clear Config2 */
	buf[0] = 5;
	buf[1] = 0;
	i2c_master_send(pv->i2c, buf, 2);

	/* Read & cache Config1 */
	buf[0] = 1;
	rc = i2c_master_send(pv->i2c, buf, 1);
	if (rc > 0) {
		rc = i2c_master_recv(pv->i2c, buf, 1);
		if (rc > 0) {
			config = buf[0];

			dev_dbg(&pv->i2c->dev, "ADC config reg: %02x\n",
				config);

			/* Disable shutdown mode */
			config &= 0xfe;
			buf[0] = 1;
			buf[1] = config;
			rc = i2c_master_send(pv->i2c, buf, 2);
		}
	}
	if (rc <= 0)
		dev_err(&pv->i2c->dev, "Error reading ADC config\n");

	pv->config = config;
}

static int wf_ad7417_probe(struct i2c_client *client)
{
	struct wf_ad7417_priv *pv;
	const struct mpu_data *mpu;
	const char *loc;
	int cpu_nr;

	loc = of_get_property(client->dev.of_node, "hwsensor-location", NULL);
	if (!loc) {
		dev_warn(&client->dev, "Missing hwsensor-location property!\n");
		return -ENXIO;
	}

	/*
	 * Identify which CPU we belong to by looking at the first entry
	 * in the hwsensor-location list
	 */
	if (!strncmp(loc, "CPU A", 5))
		cpu_nr = 0;
	else if (!strncmp(loc, "CPU B", 5))
		cpu_nr = 1;
	else {
		pr_err("wf_ad7417: Can't identify location %s\n", loc);
		return -ENXIO;
	}
	mpu = wf_get_mpu(cpu_nr);
	if (!mpu) {
		dev_err(&client->dev, "Failed to retrieve MPU data\n");
		return -ENXIO;
	}

	pv = kzalloc(sizeof(struct wf_ad7417_priv), GFP_KERNEL);
	if (pv == NULL)
		return -ENODEV;

	kref_init(&pv->ref);
	mutex_init(&pv->lock);
	pv->i2c = client;
	pv->cpu = cpu_nr;
	pv->mpu = mpu;
	dev_set_drvdata(&client->dev, pv);

	/* Initialize the chip */
	wf_ad7417_init_chip(pv);

	/*
	 * We cannot rely on Apple device-tree giving us child
	 * node with the names of the individual sensors so we
	 * just hard code what we know about them
	 */
	wf_ad7417_add_sensor(pv, 0, "cpu-amb-temp", &wf_ad7417_temp_ops);
	wf_ad7417_add_sensor(pv, 1, "cpu-diode-temp", &wf_ad7417_adc_ops);
	wf_ad7417_add_sensor(pv, 2, "cpu-12v-current", &wf_ad7417_adc_ops);
	wf_ad7417_add_sensor(pv, 3, "cpu-voltage", &wf_ad7417_adc_ops);
	wf_ad7417_add_sensor(pv, 4, "cpu-current", &wf_ad7417_adc_ops);

	return 0;
}

static void wf_ad7417_remove(struct i2c_client *client)
{
	struct wf_ad7417_priv *pv = dev_get_drvdata(&client->dev);
	int i;

	/* Mark client detached */
	pv->i2c = NULL;

	/* Release sensor */
	for (i = 0; i < 5; i++)
		wf_unregister_sensor(&pv->sensors[i]);

	kref_put(&pv->ref, wf_ad7417_release);
}

static const struct i2c_device_id wf_ad7417_id[] = {
	{ "MAC,ad7417", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wf_ad7417_id);

static const struct of_device_id wf_ad7417_of_id[] = {
	{ .compatible = "ad7417", },
	{ }
};
MODULE_DEVICE_TABLE(of, wf_ad7417_of_id);

static struct i2c_driver wf_ad7417_driver = {
	.driver = {
		.name	= "wf_ad7417",
		.of_match_table = wf_ad7417_of_id,
	},
	.probe_new	= wf_ad7417_probe,
	.remove		= wf_ad7417_remove,
	.id_table	= wf_ad7417_id,
};

static int wf_ad7417_init(void)
{
	/* This is only supported on these machines */
	if (!of_machine_is_compatible("PowerMac7,2") &&
	    !of_machine_is_compatible("PowerMac7,3") &&
	    !of_machine_is_compatible("RackMac3,1"))
		return -ENODEV;

	return i2c_add_driver(&wf_ad7417_driver);
}

static void wf_ad7417_exit(void)
{
	i2c_del_driver(&wf_ad7417_driver);
}

module_init(wf_ad7417_init);
module_exit(wf_ad7417_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("ad7417 sensor driver for PowerMacs");
MODULE_LICENSE("GPL");

