// SPDX-License-Identifier: GPL-2.0-only
/*
 * Windfarm PowerMac thermal control. SMU based sensors
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/smu.h>

#include "windfarm.h"

#define VERSION "0.2"

#undef DEBUG

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif

/*
 * Various SMU "partitions" calibration objects for which we
 * keep pointers here for use by bits & pieces of the driver
 */
static struct smu_sdbp_cpuvcp *cpuvcp;
static int  cpuvcp_version;
static struct smu_sdbp_cpudiode *cpudiode;
static struct smu_sdbp_slotspow *slotspow;
static u8 *debugswitches;

/*
 * SMU basic sensors objects
 */

static LIST_HEAD(smu_ads);

struct smu_ad_sensor {
	struct list_head	link;
	u32			reg;		/* index in SMU */
	struct wf_sensor	sens;
};
#define to_smu_ads(c) container_of(c, struct smu_ad_sensor, sens)

static void smu_ads_release(struct wf_sensor *sr)
{
	struct smu_ad_sensor *ads = to_smu_ads(sr);

	kfree(ads);
}

static int smu_read_adc(u8 id, s32 *value)
{
	struct smu_simple_cmd	cmd;
	DECLARE_COMPLETION_ONSTACK(comp);
	int rc;

	rc = smu_queue_simple(&cmd, SMU_CMD_READ_ADC, 1,
			      smu_done_complete, &comp, id);
	if (rc)
		return rc;
	wait_for_completion(&comp);
	if (cmd.cmd.status != 0)
		return cmd.cmd.status;
	if (cmd.cmd.reply_len != 2) {
		printk(KERN_ERR "winfarm: read ADC 0x%x returned %d bytes !\n",
		       id, cmd.cmd.reply_len);
		return -EIO;
	}
	*value = *((u16 *)cmd.buffer);
	return 0;
}

static int smu_cputemp_get(struct wf_sensor *sr, s32 *value)
{
	struct smu_ad_sensor *ads = to_smu_ads(sr);
	int rc;
	s32 val;
	s64 scaled;

	rc = smu_read_adc(ads->reg, &val);
	if (rc) {
		printk(KERN_ERR "windfarm: read CPU temp failed, err %d\n",
		       rc);
		return rc;
	}

	/* Ok, we have to scale & adjust, taking units into account */
	scaled = (s64)(((u64)val) * (u64)cpudiode->m_value);
	scaled >>= 3;
	scaled += ((s64)cpudiode->b_value) << 9;
	*value = (s32)(scaled << 1);

	return 0;
}

static int smu_cpuamp_get(struct wf_sensor *sr, s32 *value)
{
	struct smu_ad_sensor *ads = to_smu_ads(sr);
	s32 val, scaled;
	int rc;

	rc = smu_read_adc(ads->reg, &val);
	if (rc) {
		printk(KERN_ERR "windfarm: read CPU current failed, err %d\n",
		       rc);
		return rc;
	}

	/* Ok, we have to scale & adjust, taking units into account */
	scaled = (s32)(val * (u32)cpuvcp->curr_scale);
	scaled += (s32)cpuvcp->curr_offset;
	*value = scaled << 4;

	return 0;
}

static int smu_cpuvolt_get(struct wf_sensor *sr, s32 *value)
{
	struct smu_ad_sensor *ads = to_smu_ads(sr);
	s32 val, scaled;
	int rc;

	rc = smu_read_adc(ads->reg, &val);
	if (rc) {
		printk(KERN_ERR "windfarm: read CPU voltage failed, err %d\n",
		       rc);
		return rc;
	}

	/* Ok, we have to scale & adjust, taking units into account */
	scaled = (s32)(val * (u32)cpuvcp->volt_scale);
	scaled += (s32)cpuvcp->volt_offset;
	*value = scaled << 4;

	return 0;
}

static int smu_slotspow_get(struct wf_sensor *sr, s32 *value)
{
	struct smu_ad_sensor *ads = to_smu_ads(sr);
	s32 val, scaled;
	int rc;

	rc = smu_read_adc(ads->reg, &val);
	if (rc) {
		printk(KERN_ERR "windfarm: read slots power failed, err %d\n",
		       rc);
		return rc;
	}

	/* Ok, we have to scale & adjust, taking units into account */
	scaled = (s32)(val * (u32)slotspow->pow_scale);
	scaled += (s32)slotspow->pow_offset;
	*value = scaled << 4;

	return 0;
}


static const struct wf_sensor_ops smu_cputemp_ops = {
	.get_value	= smu_cputemp_get,
	.release	= smu_ads_release,
	.owner		= THIS_MODULE,
};
static const struct wf_sensor_ops smu_cpuamp_ops = {
	.get_value	= smu_cpuamp_get,
	.release	= smu_ads_release,
	.owner		= THIS_MODULE,
};
static const struct wf_sensor_ops smu_cpuvolt_ops = {
	.get_value	= smu_cpuvolt_get,
	.release	= smu_ads_release,
	.owner		= THIS_MODULE,
};
static const struct wf_sensor_ops smu_slotspow_ops = {
	.get_value	= smu_slotspow_get,
	.release	= smu_ads_release,
	.owner		= THIS_MODULE,
};


static struct smu_ad_sensor *smu_ads_create(struct device_node *node)
{
	struct smu_ad_sensor *ads;
	const char *l;
	const u32 *v;

	ads = kmalloc(sizeof(struct smu_ad_sensor), GFP_KERNEL);
	if (ads == NULL)
		return NULL;
	l = of_get_property(node, "location", NULL);
	if (l == NULL)
		goto fail;

	/* We currently pick the sensors based on the OF name and location
	 * properties, while Darwin uses the sensor-id's.
	 * The problem with the IDs is that they are model specific while it
	 * looks like apple has been doing a reasonably good job at keeping
	 * the names and locations consistents so I'll stick with the names
	 * and locations for now.
	 */
	if (of_node_is_type(node, "temp-sensor") &&
	    !strcmp(l, "CPU T-Diode")) {
		ads->sens.ops = &smu_cputemp_ops;
		ads->sens.name = "cpu-temp";
		if (cpudiode == NULL) {
			DBG("wf: cpudiode partition (%02x) not found\n",
			    SMU_SDB_CPUDIODE_ID);
			goto fail;
		}
	} else if (of_node_is_type(node, "current-sensor") &&
		   !strcmp(l, "CPU Current")) {
		ads->sens.ops = &smu_cpuamp_ops;
		ads->sens.name = "cpu-current";
		if (cpuvcp == NULL) {
			DBG("wf: cpuvcp partition (%02x) not found\n",
			    SMU_SDB_CPUVCP_ID);
			goto fail;
		}
	} else if (of_node_is_type(node, "voltage-sensor") &&
		   !strcmp(l, "CPU Voltage")) {
		ads->sens.ops = &smu_cpuvolt_ops;
		ads->sens.name = "cpu-voltage";
		if (cpuvcp == NULL) {
			DBG("wf: cpuvcp partition (%02x) not found\n",
			    SMU_SDB_CPUVCP_ID);
			goto fail;
		}
	} else if (of_node_is_type(node, "power-sensor") &&
		   !strcmp(l, "Slots Power")) {
		ads->sens.ops = &smu_slotspow_ops;
		ads->sens.name = "slots-power";
		if (slotspow == NULL) {
			DBG("wf: slotspow partition (%02x) not found\n",
			    SMU_SDB_SLOTSPOW_ID);
			goto fail;
		}
	} else
		goto fail;

	v = of_get_property(node, "reg", NULL);
	if (v == NULL)
		goto fail;
	ads->reg = *v;

	if (wf_register_sensor(&ads->sens))
		goto fail;
	return ads;
 fail:
	kfree(ads);
	return NULL;
}

/*
 * SMU Power combo sensor object
 */

struct smu_cpu_power_sensor {
	struct list_head	link;
	struct wf_sensor	*volts;
	struct wf_sensor	*amps;
	int			fake_volts : 1;
	int			quadratic : 1;
	struct wf_sensor	sens;
};
#define to_smu_cpu_power(c) container_of(c, struct smu_cpu_power_sensor, sens)

static struct smu_cpu_power_sensor *smu_cpu_power;

static void smu_cpu_power_release(struct wf_sensor *sr)
{
	struct smu_cpu_power_sensor *pow = to_smu_cpu_power(sr);

	if (pow->volts)
		wf_put_sensor(pow->volts);
	if (pow->amps)
		wf_put_sensor(pow->amps);
	kfree(pow);
}

static int smu_cpu_power_get(struct wf_sensor *sr, s32 *value)
{
	struct smu_cpu_power_sensor *pow = to_smu_cpu_power(sr);
	s32 volts, amps, power;
	u64 tmps, tmpa, tmpb;
	int rc;

	rc = pow->amps->ops->get_value(pow->amps, &amps);
	if (rc)
		return rc;

	if (pow->fake_volts) {
		*value = amps * 12 - 0x30000;
		return 0;
	}

	rc = pow->volts->ops->get_value(pow->volts, &volts);
	if (rc)
		return rc;

	power = (s32)((((u64)volts) * ((u64)amps)) >> 16);
	if (!pow->quadratic) {
		*value = power;
		return 0;
	}
	tmps = (((u64)power) * ((u64)power)) >> 16;
	tmpa = ((u64)cpuvcp->power_quads[0]) * tmps;
	tmpb = ((u64)cpuvcp->power_quads[1]) * ((u64)power);
	*value = (tmpa >> 28) + (tmpb >> 28) + (cpuvcp->power_quads[2] >> 12);

	return 0;
}

static const struct wf_sensor_ops smu_cpu_power_ops = {
	.get_value	= smu_cpu_power_get,
	.release	= smu_cpu_power_release,
	.owner		= THIS_MODULE,
};


static struct smu_cpu_power_sensor *
smu_cpu_power_create(struct wf_sensor *volts, struct wf_sensor *amps)
{
	struct smu_cpu_power_sensor *pow;

	pow = kmalloc(sizeof(struct smu_cpu_power_sensor), GFP_KERNEL);
	if (pow == NULL)
		return NULL;
	pow->sens.ops = &smu_cpu_power_ops;
	pow->sens.name = "cpu-power";

	wf_get_sensor(volts);
	pow->volts = volts;
	wf_get_sensor(amps);
	pow->amps = amps;

	/* Some early machines need a faked voltage */
	if (debugswitches && ((*debugswitches) & 0x80)) {
		printk(KERN_INFO "windfarm: CPU Power sensor using faked"
		       " voltage !\n");
		pow->fake_volts = 1;
	} else
		pow->fake_volts = 0;

	/* Try to use quadratic transforms on PowerMac8,1 and 9,1 for now,
	 * I yet have to figure out what's up with 8,2 and will have to
	 * adjust for later, unless we can 100% trust the SDB partition...
	 */
	if ((of_machine_is_compatible("PowerMac8,1") ||
	     of_machine_is_compatible("PowerMac8,2") ||
	     of_machine_is_compatible("PowerMac9,1")) &&
	    cpuvcp_version >= 2) {
		pow->quadratic = 1;
		DBG("windfarm: CPU Power using quadratic transform\n");
	} else
		pow->quadratic = 0;

	if (wf_register_sensor(&pow->sens))
		goto fail;
	return pow;
 fail:
	kfree(pow);
	return NULL;
}

static void smu_fetch_param_partitions(void)
{
	const struct smu_sdbp_header *hdr;

	/* Get CPU voltage/current/power calibration data */
	hdr = smu_get_sdb_partition(SMU_SDB_CPUVCP_ID, NULL);
	if (hdr != NULL) {
		cpuvcp = (struct smu_sdbp_cpuvcp *)&hdr[1];
		/* Keep version around */
		cpuvcp_version = hdr->version;
	}

	/* Get CPU diode calibration data */
	hdr = smu_get_sdb_partition(SMU_SDB_CPUDIODE_ID, NULL);
	if (hdr != NULL)
		cpudiode = (struct smu_sdbp_cpudiode *)&hdr[1];

	/* Get slots power calibration data if any */
	hdr = smu_get_sdb_partition(SMU_SDB_SLOTSPOW_ID, NULL);
	if (hdr != NULL)
		slotspow = (struct smu_sdbp_slotspow *)&hdr[1];

	/* Get debug switches if any */
	hdr = smu_get_sdb_partition(SMU_SDB_DEBUG_SWITCHES_ID, NULL);
	if (hdr != NULL)
		debugswitches = (u8 *)&hdr[1];
}

static int __init smu_sensors_init(void)
{
	struct device_node *smu, *sensors, *s;
	struct smu_ad_sensor *volt_sensor = NULL, *curr_sensor = NULL;

	if (!smu_present())
		return -ENODEV;

	/* Get parameters partitions */
	smu_fetch_param_partitions();

	smu = of_find_node_by_type(NULL, "smu");
	if (smu == NULL)
		return -ENODEV;

	/* Look for sensors subdir */
	for_each_child_of_node(smu, sensors)
		if (of_node_name_eq(sensors, "sensors"))
			break;

	of_node_put(smu);

	/* Create basic sensors */
	for (s = NULL;
	     sensors && (s = of_get_next_child(sensors, s)) != NULL;) {
		struct smu_ad_sensor *ads;

		ads = smu_ads_create(s);
		if (ads == NULL)
			continue;
		list_add(&ads->link, &smu_ads);
		/* keep track of cpu voltage & current */
		if (!strcmp(ads->sens.name, "cpu-voltage"))
			volt_sensor = ads;
		else if (!strcmp(ads->sens.name, "cpu-current"))
			curr_sensor = ads;
	}

	of_node_put(sensors);

	/* Create CPU power sensor if possible */
	if (volt_sensor && curr_sensor)
		smu_cpu_power = smu_cpu_power_create(&volt_sensor->sens,
						     &curr_sensor->sens);

	return 0;
}

static void __exit smu_sensors_exit(void)
{
	struct smu_ad_sensor *ads;

	/* dispose of power sensor */
	if (smu_cpu_power)
		wf_unregister_sensor(&smu_cpu_power->sens);

	/* dispose of basic sensors */
	while (!list_empty(&smu_ads)) {
		ads = list_entry(smu_ads.next, struct smu_ad_sensor, link);
		list_del(&ads->link);
		wf_unregister_sensor(&ads->sens);
	}
}


module_init(smu_sensors_init);
module_exit(smu_sensors_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("SMU sensor objects for PowerMacs thermal control");
MODULE_LICENSE("GPL");

