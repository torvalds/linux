// SPDX-License-Identifier: GPL-2.0-only
/*
 * Windfarm PowerMac thermal control. FCU fan control
 *
 * Copyright 2012 Benjamin Herrenschmidt, IBM Corp.
 */
#undef DEBUG

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/sections.h>

#include "windfarm.h"
#include "windfarm_mpu.h"

#define VERSION "1.0"

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif

/*
 * This option is "weird" :) Basically, if you define this to 1
 * the control loop for the RPMs fans (not PWMs) will apply the
 * correction factor obtained from the PID to the actual RPM
 * speed read from the FCU.
 *
 * If you define the below constant to 0, then it will be
 * applied to the setpoint RPM speed, that is basically the
 * speed we proviously "asked" for.
 *
 * I'm using 0 for now which is what therm_pm72 used to do and
 * what Darwin -apparently- does based on observed behaviour.
 */
#define RPM_PID_USE_ACTUAL_SPEED	0

/* Default min/max for pumps */
#define CPU_PUMP_OUTPUT_MAX		3200
#define CPU_PUMP_OUTPUT_MIN		1250

#define FCU_FAN_RPM		0
#define FCU_FAN_PWM		1

struct wf_fcu_priv {
	struct kref		ref;
	struct i2c_client	*i2c;
	struct mutex		lock;
	struct list_head	fan_list;
	int			rpm_shift;
};

struct wf_fcu_fan {
	struct list_head	link;
	int			id;
	s32			min, max, target;
	struct wf_fcu_priv	*fcu_priv;
	struct wf_control	ctrl;
};

static void wf_fcu_release(struct kref *ref)
{
	struct wf_fcu_priv *pv = container_of(ref, struct wf_fcu_priv, ref);

	kfree(pv);
}

static void wf_fcu_fan_release(struct wf_control *ct)
{
	struct wf_fcu_fan *fan = ct->priv;

	kref_put(&fan->fcu_priv->ref, wf_fcu_release);
	kfree(fan);
}

static int wf_fcu_read_reg(struct wf_fcu_priv *pv, int reg,
			   unsigned char *buf, int nb)
{
	int tries, nr, nw;

	mutex_lock(&pv->lock);

	buf[0] = reg;
	tries = 0;
	for (;;) {
		nw = i2c_master_send(pv->i2c, buf, 1);
		if (nw > 0 || (nw < 0 && nw != -EIO) || tries >= 100)
			break;
		msleep(10);
		++tries;
	}
	if (nw <= 0) {
		pr_err("Failure writing address to FCU: %d", nw);
		nr = nw;
		goto bail;
	}
	tries = 0;
	for (;;) {
		nr = i2c_master_recv(pv->i2c, buf, nb);
		if (nr > 0 || (nr < 0 && nr != -ENODEV) || tries >= 100)
			break;
		msleep(10);
		++tries;
	}
	if (nr <= 0)
		pr_err("wf_fcu: Failure reading data from FCU: %d", nw);
 bail:
	mutex_unlock(&pv->lock);
	return nr;
}

static int wf_fcu_write_reg(struct wf_fcu_priv *pv, int reg,
			    const unsigned char *ptr, int nb)
{
	int tries, nw;
	unsigned char buf[16];

	buf[0] = reg;
	memcpy(buf+1, ptr, nb);
	++nb;
	tries = 0;
	for (;;) {
		nw = i2c_master_send(pv->i2c, buf, nb);
		if (nw > 0 || (nw < 0 && nw != -EIO) || tries >= 100)
			break;
		msleep(10);
		++tries;
	}
	if (nw < 0)
		pr_err("wf_fcu: Failure writing to FCU: %d", nw);
	return nw;
}

static int wf_fcu_fan_set_rpm(struct wf_control *ct, s32 value)
{
	struct wf_fcu_fan *fan = ct->priv;
	struct wf_fcu_priv *pv = fan->fcu_priv;
	int rc, shift = pv->rpm_shift;
	unsigned char buf[2];

	if (value < fan->min)
		value = fan->min;
	if (value > fan->max)
		value = fan->max;

	fan->target = value;

	buf[0] = value >> (8 - shift);
	buf[1] = value << shift;
	rc = wf_fcu_write_reg(pv, 0x10 + (fan->id * 2), buf, 2);
	if (rc < 0)
		return -EIO;
	return 0;
}

static int wf_fcu_fan_get_rpm(struct wf_control *ct, s32 *value)
{
	struct wf_fcu_fan *fan = ct->priv;
	struct wf_fcu_priv *pv = fan->fcu_priv;
	int rc, reg_base, shift = pv->rpm_shift;
	unsigned char failure;
	unsigned char active;
	unsigned char buf[2];

	rc = wf_fcu_read_reg(pv, 0xb, &failure, 1);
	if (rc != 1)
		return -EIO;
	if ((failure & (1 << fan->id)) != 0)
		return -EFAULT;
	rc = wf_fcu_read_reg(pv, 0xd, &active, 1);
	if (rc != 1)
		return -EIO;
	if ((active & (1 << fan->id)) == 0)
		return -ENXIO;

	/* Programmed value or real current speed */
#if RPM_PID_USE_ACTUAL_SPEED
	reg_base = 0x11;
#else
	reg_base = 0x10;
#endif
	rc = wf_fcu_read_reg(pv, reg_base + (fan->id * 2), buf, 2);
	if (rc != 2)
		return -EIO;

	*value = (buf[0] << (8 - shift)) | buf[1] >> shift;

	return 0;
}

static int wf_fcu_fan_set_pwm(struct wf_control *ct, s32 value)
{
	struct wf_fcu_fan *fan = ct->priv;
	struct wf_fcu_priv *pv = fan->fcu_priv;
	unsigned char buf[2];
	int rc;

	if (value < fan->min)
		value = fan->min;
	if (value > fan->max)
		value = fan->max;

	fan->target = value;

	value = (value * 2559) / 1000;
	buf[0] = value;
	rc = wf_fcu_write_reg(pv, 0x30 + (fan->id * 2), buf, 1);
	if (rc < 0)
		return -EIO;
	return 0;
}

static int wf_fcu_fan_get_pwm(struct wf_control *ct, s32 *value)
{
	struct wf_fcu_fan *fan = ct->priv;
	struct wf_fcu_priv *pv = fan->fcu_priv;
	unsigned char failure;
	unsigned char active;
	unsigned char buf[2];
	int rc;

	rc = wf_fcu_read_reg(pv, 0x2b, &failure, 1);
	if (rc != 1)
		return -EIO;
	if ((failure & (1 << fan->id)) != 0)
		return -EFAULT;
	rc = wf_fcu_read_reg(pv, 0x2d, &active, 1);
	if (rc != 1)
		return -EIO;
	if ((active & (1 << fan->id)) == 0)
		return -ENXIO;

	rc = wf_fcu_read_reg(pv, 0x30 + (fan->id * 2), buf, 1);
	if (rc != 1)
		return -EIO;

	*value = (((s32)buf[0]) * 1000) / 2559;

	return 0;
}

static s32 wf_fcu_fan_min(struct wf_control *ct)
{
	struct wf_fcu_fan *fan = ct->priv;

	return fan->min;
}

static s32 wf_fcu_fan_max(struct wf_control *ct)
{
	struct wf_fcu_fan *fan = ct->priv;

	return fan->max;
}

static const struct wf_control_ops wf_fcu_fan_rpm_ops = {
	.set_value	= wf_fcu_fan_set_rpm,
	.get_value	= wf_fcu_fan_get_rpm,
	.get_min	= wf_fcu_fan_min,
	.get_max	= wf_fcu_fan_max,
	.release	= wf_fcu_fan_release,
	.owner		= THIS_MODULE,
};

static const struct wf_control_ops wf_fcu_fan_pwm_ops = {
	.set_value	= wf_fcu_fan_set_pwm,
	.get_value	= wf_fcu_fan_get_pwm,
	.get_min	= wf_fcu_fan_min,
	.get_max	= wf_fcu_fan_max,
	.release	= wf_fcu_fan_release,
	.owner		= THIS_MODULE,
};

static void wf_fcu_get_pump_minmax(struct wf_fcu_fan *fan)
{
	const struct mpu_data *mpu = wf_get_mpu(0);
	u16 pump_min = 0, pump_max = 0xffff;
	u16 tmp[4];

	/* Try to fetch pumps min/max infos from eeprom */
	if (mpu) {
		memcpy(&tmp, mpu->processor_part_num, 8);
		if (tmp[0] != 0xffff && tmp[1] != 0xffff) {
			pump_min = max(pump_min, tmp[0]);
			pump_max = min(pump_max, tmp[1]);
		}
		if (tmp[2] != 0xffff && tmp[3] != 0xffff) {
			pump_min = max(pump_min, tmp[2]);
			pump_max = min(pump_max, tmp[3]);
		}
	}

	/* Double check the values, this _IS_ needed as the EEPROM on
	 * some dual 2.5Ghz G5s seem, at least, to have both min & max
	 * same to the same value ... (grrrr)
	 */
	if (pump_min == pump_max || pump_min == 0 || pump_max == 0xffff) {
		pump_min = CPU_PUMP_OUTPUT_MIN;
		pump_max = CPU_PUMP_OUTPUT_MAX;
	}

	fan->min = pump_min;
	fan->max = pump_max;

	DBG("wf_fcu: pump min/max for %s set to: [%d..%d] RPM\n",
	    fan->ctrl.name, pump_min, pump_max);
}

static void wf_fcu_get_rpmfan_minmax(struct wf_fcu_fan *fan)
{
	struct wf_fcu_priv *pv = fan->fcu_priv;
	const struct mpu_data *mpu0 = wf_get_mpu(0);
	const struct mpu_data *mpu1 = wf_get_mpu(1);

	/* Default */
	fan->min = 2400 >> pv->rpm_shift;
	fan->max = 56000 >> pv->rpm_shift;

	/* CPU fans have min/max in MPU */
	if (mpu0 && !strcmp(fan->ctrl.name, "cpu-front-fan-0")) {
		fan->min = max(fan->min, (s32)mpu0->rminn_intake_fan);
		fan->max = min(fan->max, (s32)mpu0->rmaxn_intake_fan);
		goto bail;
	}
	if (mpu1 && !strcmp(fan->ctrl.name, "cpu-front-fan-1")) {
		fan->min = max(fan->min, (s32)mpu1->rminn_intake_fan);
		fan->max = min(fan->max, (s32)mpu1->rmaxn_intake_fan);
		goto bail;
	}
	if (mpu0 && !strcmp(fan->ctrl.name, "cpu-rear-fan-0")) {
		fan->min = max(fan->min, (s32)mpu0->rminn_exhaust_fan);
		fan->max = min(fan->max, (s32)mpu0->rmaxn_exhaust_fan);
		goto bail;
	}
	if (mpu1 && !strcmp(fan->ctrl.name, "cpu-rear-fan-1")) {
		fan->min = max(fan->min, (s32)mpu1->rminn_exhaust_fan);
		fan->max = min(fan->max, (s32)mpu1->rmaxn_exhaust_fan);
		goto bail;
	}
	/* Rackmac variants, we just use mpu0 intake */
	if (!strncmp(fan->ctrl.name, "cpu-fan", 7)) {
		fan->min = max(fan->min, (s32)mpu0->rminn_intake_fan);
		fan->max = min(fan->max, (s32)mpu0->rmaxn_intake_fan);
		goto bail;
	}
 bail:
	DBG("wf_fcu: fan min/max for %s set to: [%d..%d] RPM\n",
	    fan->ctrl.name, fan->min, fan->max);
}

static void wf_fcu_add_fan(struct wf_fcu_priv *pv, const char *name,
			   int type, int id)
{
	struct wf_fcu_fan *fan;

	fan = kzalloc(sizeof(*fan), GFP_KERNEL);
	if (!fan)
		return;
	fan->fcu_priv = pv;
	fan->id = id;
	fan->ctrl.name = name;
	fan->ctrl.priv = fan;

	/* min/max is oddball but the code comes from
	 * therm_pm72 which seems to work so ...
	 */
	if (type == FCU_FAN_RPM) {
		if (!strncmp(name, "cpu-pump", strlen("cpu-pump")))
			wf_fcu_get_pump_minmax(fan);
		else
			wf_fcu_get_rpmfan_minmax(fan);
		fan->ctrl.type = WF_CONTROL_RPM_FAN;
		fan->ctrl.ops = &wf_fcu_fan_rpm_ops;
	} else {
		fan->min = 10;
		fan->max = 100;
		fan->ctrl.type = WF_CONTROL_PWM_FAN;
		fan->ctrl.ops = &wf_fcu_fan_pwm_ops;
	}

	if (wf_register_control(&fan->ctrl)) {
		pr_err("wf_fcu: Failed to register fan %s\n", name);
		kfree(fan);
		return;
	}
	list_add(&fan->link, &pv->fan_list);
	kref_get(&pv->ref);
}

static void wf_fcu_lookup_fans(struct wf_fcu_priv *pv)
{
	/* Translation of device-tree location properties to
	 * windfarm fan names
	 */
	static const struct {
		const char *dt_name;	/* Device-tree name */
		const char *ct_name;	/* Control name */
	} loc_trans[] = {
		{ "BACKSIDE",		"backside-fan",		},
		{ "SYS CTRLR FAN",	"backside-fan",		},
		{ "DRIVE BAY",		"drive-bay-fan",	},
		{ "SLOT",		"slots-fan",		},
		{ "PCI FAN",		"slots-fan",		},
		{ "CPU A INTAKE",	"cpu-front-fan-0",	},
		{ "CPU A EXHAUST",	"cpu-rear-fan-0",	},
		{ "CPU B INTAKE",	"cpu-front-fan-1",	},
		{ "CPU B EXHAUST",	"cpu-rear-fan-1",	},
		{ "CPU A PUMP",		"cpu-pump-0",		},
		{ "CPU B PUMP",		"cpu-pump-1",		},
		{ "CPU A 1",		"cpu-fan-a-0",		},
		{ "CPU A 2",		"cpu-fan-b-0",		},
		{ "CPU A 3",		"cpu-fan-c-0",		},
		{ "CPU B 1",		"cpu-fan-a-1",		},
		{ "CPU B 2",		"cpu-fan-b-1",		},
		{ "CPU B 3",		"cpu-fan-c-1",		},
	};
	struct device_node *np, *fcu = pv->i2c->dev.of_node;
	int i;

	DBG("Looking up FCU controls in device-tree...\n");

	for_each_child_of_node(fcu, np) {
		int id, type = -1;
		const char *loc;
		const char *name;
		const u32 *reg;

		DBG(" control: %pOFn, type: %s\n", np, of_node_get_device_type(np));

		/* Detect control type */
		if (of_node_is_type(np, "fan-rpm-control") ||
		    of_node_is_type(np, "fan-rpm"))
			type = FCU_FAN_RPM;
		if (of_node_is_type(np, "fan-pwm-control") ||
		    of_node_is_type(np, "fan-pwm"))
			type = FCU_FAN_PWM;
		/* Only care about fans for now */
		if (type == -1)
			continue;

		/* Lookup for a matching location */
		loc = of_get_property(np, "location", NULL);
		reg = of_get_property(np, "reg", NULL);
		if (loc == NULL || reg == NULL)
			continue;
		DBG(" matching location: %s, reg: 0x%08x\n", loc, *reg);

		for (i = 0; i < ARRAY_SIZE(loc_trans); i++) {
			if (strncmp(loc, loc_trans[i].dt_name,
				    strlen(loc_trans[i].dt_name)))
				continue;
			name = loc_trans[i].ct_name;

			DBG(" location match, name: %s\n", name);

			if (type == FCU_FAN_RPM)
				id = ((*reg) - 0x10) / 2;
			else
				id = ((*reg) - 0x30) / 2;
			if (id > 7) {
				pr_warning("wf_fcu: Can't parse "
				       "fan ID in device-tree for %pOF\n",
					   np);
				break;
			}
			wf_fcu_add_fan(pv, name, type, id);
			break;
		}
	}
}

static void wf_fcu_default_fans(struct wf_fcu_priv *pv)
{
	/* We only support the default fans for PowerMac7,2 */
	if (!of_machine_is_compatible("PowerMac7,2"))
		return;

	wf_fcu_add_fan(pv, "backside-fan",	FCU_FAN_PWM, 1);
	wf_fcu_add_fan(pv, "drive-bay-fan",	FCU_FAN_RPM, 2);
	wf_fcu_add_fan(pv, "slots-fan",		FCU_FAN_PWM, 2);
	wf_fcu_add_fan(pv, "cpu-front-fan-0",	FCU_FAN_RPM, 3);
	wf_fcu_add_fan(pv, "cpu-rear-fan-0",	FCU_FAN_RPM, 4);
	wf_fcu_add_fan(pv, "cpu-front-fan-1",	FCU_FAN_RPM, 5);
	wf_fcu_add_fan(pv, "cpu-rear-fan-1",	FCU_FAN_RPM, 6);
}

static int wf_fcu_init_chip(struct wf_fcu_priv *pv)
{
	unsigned char buf = 0xff;
	int rc;

	rc = wf_fcu_write_reg(pv, 0xe, &buf, 1);
	if (rc < 0)
		return -EIO;
	rc = wf_fcu_write_reg(pv, 0x2e, &buf, 1);
	if (rc < 0)
		return -EIO;
	rc = wf_fcu_read_reg(pv, 0, &buf, 1);
	if (rc < 0)
		return -EIO;
	pv->rpm_shift = (buf == 1) ? 2 : 3;

	pr_debug("wf_fcu: FCU Initialized, RPM fan shift is %d\n",
		 pv->rpm_shift);

	return 0;
}

static int wf_fcu_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct wf_fcu_priv *pv;

	pv = kzalloc(sizeof(*pv), GFP_KERNEL);
	if (!pv)
		return -ENOMEM;

	kref_init(&pv->ref);
	mutex_init(&pv->lock);
	INIT_LIST_HEAD(&pv->fan_list);
	pv->i2c = client;

	/*
	 * First we must start the FCU which will query the
	 * shift value to apply to RPMs
	 */
	if (wf_fcu_init_chip(pv)) {
		pr_err("wf_fcu: Initialization failed !\n");
		kfree(pv);
		return -ENXIO;
	}

	/* First lookup fans in the device-tree */
	wf_fcu_lookup_fans(pv);

	/*
	 * Older machines don't have the device-tree entries
	 * we are looking for, just hard code the list
	 */
	if (list_empty(&pv->fan_list))
		wf_fcu_default_fans(pv);

	/* Still no fans ? FAIL */
	if (list_empty(&pv->fan_list)) {
		pr_err("wf_fcu: Failed to find fans for your machine\n");
		kfree(pv);
		return -ENODEV;
	}

	dev_set_drvdata(&client->dev, pv);

	return 0;
}

static int wf_fcu_remove(struct i2c_client *client)
{
	struct wf_fcu_priv *pv = dev_get_drvdata(&client->dev);
	struct wf_fcu_fan *fan;

	while (!list_empty(&pv->fan_list)) {
		fan = list_first_entry(&pv->fan_list, struct wf_fcu_fan, link);
		list_del(&fan->link);
		wf_unregister_control(&fan->ctrl);
	}
	kref_put(&pv->ref, wf_fcu_release);
	return 0;
}

static const struct i2c_device_id wf_fcu_id[] = {
	{ "MAC,fcu", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wf_fcu_id);

static struct i2c_driver wf_fcu_driver = {
	.driver = {
		.name	= "wf_fcu",
	},
	.probe		= wf_fcu_probe,
	.remove		= wf_fcu_remove,
	.id_table	= wf_fcu_id,
};

module_i2c_driver(wf_fcu_driver);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("FCU control objects for PowerMacs thermal control");
MODULE_LICENSE("GPL");

