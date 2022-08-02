// SPDX-License-Identifier: GPL-2.0-only
/*
 * Windfarm PowerMac thermal control. SMU based 1 CPU desktop control loops
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 * The algorithm used is the PID control algorithm, used the same
 * way the published Darwin code does, using the same values that
 * are present in the Darwin 8.2 snapshot property lists (note however
 * that none of the code has been re-used, it's a complete re-implementation
 *
 * The various control loops found in Darwin config file are:
 *
 * PowerMac9,1
 * ===========
 *
 * Has 3 control loops: CPU fans is similar to PowerMac8,1 (though it doesn't
 * try to play with other control loops fans). Drive bay is rather basic PID
 * with one sensor and one fan. Slots area is a bit different as the Darwin
 * driver is supposed to be capable of working in a special "AGP" mode which
 * involves the presence of an AGP sensor and an AGP fan (possibly on the
 * AGP card itself). I can't deal with that special mode as I don't have
 * access to those additional sensor/fans for now (though ultimately, it would
 * be possible to add sensor objects for them) so I'm only implementing the
 * basic PCI slot control loop
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/kmod.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/smu.h>

#include "windfarm.h"
#include "windfarm_pid.h"

#define VERSION "0.4"

#undef DEBUG

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif

/* define this to force CPU overtemp to 74 degree, useful for testing
 * the overtemp code
 */
#undef HACKED_OVERTEMP

/* Controls & sensors */
static struct wf_sensor	*sensor_cpu_power;
static struct wf_sensor	*sensor_cpu_temp;
static struct wf_sensor	*sensor_hd_temp;
static struct wf_sensor	*sensor_slots_power;
static struct wf_control *fan_cpu_main;
static struct wf_control *fan_cpu_second;
static struct wf_control *fan_cpu_third;
static struct wf_control *fan_hd;
static struct wf_control *fan_slots;
static struct wf_control *cpufreq_clamp;

/* Set to kick the control loop into life */
static int wf_smu_all_controls_ok, wf_smu_all_sensors_ok;
static bool wf_smu_started;
static bool wf_smu_overtemp;

/* Failure handling.. could be nicer */
#define FAILURE_FAN		0x01
#define FAILURE_SENSOR		0x02
#define FAILURE_OVERTEMP	0x04

static unsigned int wf_smu_failure_state;
static int wf_smu_readjust, wf_smu_skipping;

/*
 * ****** CPU Fans Control Loop ******
 *
 */


#define WF_SMU_CPU_FANS_INTERVAL	1
#define WF_SMU_CPU_FANS_MAX_HISTORY	16

/* State data used by the cpu fans control loop
 */
struct wf_smu_cpu_fans_state {
	int			ticks;
	s32			cpu_setpoint;
	struct wf_cpu_pid_state	pid;
};

static struct wf_smu_cpu_fans_state *wf_smu_cpu_fans;



/*
 * ****** Drive Fan Control Loop ******
 *
 */

struct wf_smu_drive_fans_state {
	int			ticks;
	s32			setpoint;
	struct wf_pid_state	pid;
};

static struct wf_smu_drive_fans_state *wf_smu_drive_fans;

/*
 * ****** Slots Fan Control Loop ******
 *
 */

struct wf_smu_slots_fans_state {
	int			ticks;
	s32			setpoint;
	struct wf_pid_state	pid;
};

static struct wf_smu_slots_fans_state *wf_smu_slots_fans;

/*
 * ***** Implementation *****
 *
 */


static void wf_smu_create_cpu_fans(void)
{
	struct wf_cpu_pid_param pid_param;
	const struct smu_sdbp_header *hdr;
	struct smu_sdbp_cpupiddata *piddata;
	struct smu_sdbp_fvt *fvt;
	s32 tmax, tdelta, maxpow, powadj;

	/* First, locate the PID params in SMU SBD */
	hdr = smu_get_sdb_partition(SMU_SDB_CPUPIDDATA_ID, NULL);
	if (hdr == 0) {
		printk(KERN_WARNING "windfarm: CPU PID fan config not found "
		       "max fan speed\n");
		goto fail;
	}
	piddata = (struct smu_sdbp_cpupiddata *)&hdr[1];

	/* Get the FVT params for operating point 0 (the only supported one
	 * for now) in order to get tmax
	 */
	hdr = smu_get_sdb_partition(SMU_SDB_FVT_ID, NULL);
	if (hdr) {
		fvt = (struct smu_sdbp_fvt *)&hdr[1];
		tmax = ((s32)fvt->maxtemp) << 16;
	} else
		tmax = 0x5e0000; /* 94 degree default */

	/* Alloc & initialize state */
	wf_smu_cpu_fans = kmalloc(sizeof(struct wf_smu_cpu_fans_state),
				  GFP_KERNEL);
	if (wf_smu_cpu_fans == NULL)
		goto fail;
       	wf_smu_cpu_fans->ticks = 1;

	/* Fill PID params */
	pid_param.interval = WF_SMU_CPU_FANS_INTERVAL;
	pid_param.history_len = piddata->history_len;
	if (pid_param.history_len > WF_CPU_PID_MAX_HISTORY) {
		printk(KERN_WARNING "windfarm: History size overflow on "
		       "CPU control loop (%d)\n", piddata->history_len);
		pid_param.history_len = WF_CPU_PID_MAX_HISTORY;
	}
	pid_param.gd = piddata->gd;
	pid_param.gp = piddata->gp;
	pid_param.gr = piddata->gr / pid_param.history_len;

	tdelta = ((s32)piddata->target_temp_delta) << 16;
	maxpow = ((s32)piddata->max_power) << 16;
	powadj = ((s32)piddata->power_adj) << 16;

	pid_param.tmax = tmax;
	pid_param.ttarget = tmax - tdelta;
	pid_param.pmaxadj = maxpow - powadj;

	pid_param.min = wf_control_get_min(fan_cpu_main);
	pid_param.max = wf_control_get_max(fan_cpu_main);

	wf_cpu_pid_init(&wf_smu_cpu_fans->pid, &pid_param);

	DBG("wf: CPU Fan control initialized.\n");
	DBG("    ttarget=%d.%03d, tmax=%d.%03d, min=%d RPM, max=%d RPM\n",
	    FIX32TOPRINT(pid_param.ttarget), FIX32TOPRINT(pid_param.tmax),
	    pid_param.min, pid_param.max);

	return;

 fail:
	printk(KERN_WARNING "windfarm: CPU fan config not found\n"
	       "for this machine model, max fan speed\n");

	if (cpufreq_clamp)
		wf_control_set_max(cpufreq_clamp);
	if (fan_cpu_main)
		wf_control_set_max(fan_cpu_main);
}

static void wf_smu_cpu_fans_tick(struct wf_smu_cpu_fans_state *st)
{
	s32 new_setpoint, temp, power;
	int rc;

	if (--st->ticks != 0) {
		if (wf_smu_readjust)
			goto readjust;
		return;
	}
	st->ticks = WF_SMU_CPU_FANS_INTERVAL;

	rc = wf_sensor_get(sensor_cpu_temp, &temp);
	if (rc) {
		printk(KERN_WARNING "windfarm: CPU temp sensor error %d\n",
		       rc);
		wf_smu_failure_state |= FAILURE_SENSOR;
		return;
	}

	rc = wf_sensor_get(sensor_cpu_power, &power);
	if (rc) {
		printk(KERN_WARNING "windfarm: CPU power sensor error %d\n",
		       rc);
		wf_smu_failure_state |= FAILURE_SENSOR;
		return;
	}

	DBG("wf_smu: CPU Fans tick ! CPU temp: %d.%03d, power: %d.%03d\n",
	    FIX32TOPRINT(temp), FIX32TOPRINT(power));

#ifdef HACKED_OVERTEMP
	if (temp > 0x4a0000)
		wf_smu_failure_state |= FAILURE_OVERTEMP;
#else
	if (temp > st->pid.param.tmax)
		wf_smu_failure_state |= FAILURE_OVERTEMP;
#endif
	new_setpoint = wf_cpu_pid_run(&st->pid, power, temp);

	DBG("wf_smu: new_setpoint: %d RPM\n", (int)new_setpoint);

	if (st->cpu_setpoint == new_setpoint)
		return;
	st->cpu_setpoint = new_setpoint;
 readjust:
	if (fan_cpu_main && wf_smu_failure_state == 0) {
		rc = wf_control_set(fan_cpu_main, st->cpu_setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: CPU main fan"
			       " error %d\n", rc);
			wf_smu_failure_state |= FAILURE_FAN;
		}
	}
	if (fan_cpu_second && wf_smu_failure_state == 0) {
		rc = wf_control_set(fan_cpu_second, st->cpu_setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: CPU second fan"
			       " error %d\n", rc);
			wf_smu_failure_state |= FAILURE_FAN;
		}
	}
	if (fan_cpu_third && wf_smu_failure_state == 0) {
		rc = wf_control_set(fan_cpu_third, st->cpu_setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: CPU third fan"
			       " error %d\n", rc);
			wf_smu_failure_state |= FAILURE_FAN;
		}
	}
}

static void wf_smu_create_drive_fans(void)
{
	struct wf_pid_param param = {
		.interval	= 5,
		.history_len	= 2,
		.gd		= 0x01e00000,
		.gp		= 0x00500000,
		.gr		= 0x00000000,
		.itarget	= 0x00200000,
	};

	/* Alloc & initialize state */
	wf_smu_drive_fans = kmalloc(sizeof(struct wf_smu_drive_fans_state),
					GFP_KERNEL);
	if (wf_smu_drive_fans == NULL) {
		printk(KERN_WARNING "windfarm: Memory allocation error"
		       " max fan speed\n");
		goto fail;
	}
       	wf_smu_drive_fans->ticks = 1;

	/* Fill PID params */
	param.additive = (fan_hd->type == WF_CONTROL_RPM_FAN);
	param.min = wf_control_get_min(fan_hd);
	param.max = wf_control_get_max(fan_hd);
	wf_pid_init(&wf_smu_drive_fans->pid, &param);

	DBG("wf: Drive Fan control initialized.\n");
	DBG("    itarged=%d.%03d, min=%d RPM, max=%d RPM\n",
	    FIX32TOPRINT(param.itarget), param.min, param.max);
	return;

 fail:
	if (fan_hd)
		wf_control_set_max(fan_hd);
}

static void wf_smu_drive_fans_tick(struct wf_smu_drive_fans_state *st)
{
	s32 new_setpoint, temp;
	int rc;

	if (--st->ticks != 0) {
		if (wf_smu_readjust)
			goto readjust;
		return;
	}
	st->ticks = st->pid.param.interval;

	rc = wf_sensor_get(sensor_hd_temp, &temp);
	if (rc) {
		printk(KERN_WARNING "windfarm: HD temp sensor error %d\n",
		       rc);
		wf_smu_failure_state |= FAILURE_SENSOR;
		return;
	}

	DBG("wf_smu: Drive Fans tick ! HD temp: %d.%03d\n",
	    FIX32TOPRINT(temp));

	if (temp > (st->pid.param.itarget + 0x50000))
		wf_smu_failure_state |= FAILURE_OVERTEMP;

	new_setpoint = wf_pid_run(&st->pid, temp);

	DBG("wf_smu: new_setpoint: %d\n", (int)new_setpoint);

	if (st->setpoint == new_setpoint)
		return;
	st->setpoint = new_setpoint;
 readjust:
	if (fan_hd && wf_smu_failure_state == 0) {
		rc = wf_control_set(fan_hd, st->setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: HD fan error %d\n",
			       rc);
			wf_smu_failure_state |= FAILURE_FAN;
		}
	}
}

static void wf_smu_create_slots_fans(void)
{
	struct wf_pid_param param = {
		.interval	= 1,
		.history_len	= 8,
		.gd		= 0x00000000,
		.gp		= 0x00000000,
		.gr		= 0x00020000,
		.itarget	= 0x00000000
	};

	/* Alloc & initialize state */
	wf_smu_slots_fans = kmalloc(sizeof(struct wf_smu_slots_fans_state),
					GFP_KERNEL);
	if (wf_smu_slots_fans == NULL) {
		printk(KERN_WARNING "windfarm: Memory allocation error"
		       " max fan speed\n");
		goto fail;
	}
       	wf_smu_slots_fans->ticks = 1;

	/* Fill PID params */
	param.additive = (fan_slots->type == WF_CONTROL_RPM_FAN);
	param.min = wf_control_get_min(fan_slots);
	param.max = wf_control_get_max(fan_slots);
	wf_pid_init(&wf_smu_slots_fans->pid, &param);

	DBG("wf: Slots Fan control initialized.\n");
	DBG("    itarged=%d.%03d, min=%d RPM, max=%d RPM\n",
	    FIX32TOPRINT(param.itarget), param.min, param.max);
	return;

 fail:
	if (fan_slots)
		wf_control_set_max(fan_slots);
}

static void wf_smu_slots_fans_tick(struct wf_smu_slots_fans_state *st)
{
	s32 new_setpoint, power;
	int rc;

	if (--st->ticks != 0) {
		if (wf_smu_readjust)
			goto readjust;
		return;
	}
	st->ticks = st->pid.param.interval;

	rc = wf_sensor_get(sensor_slots_power, &power);
	if (rc) {
		printk(KERN_WARNING "windfarm: Slots power sensor error %d\n",
		       rc);
		wf_smu_failure_state |= FAILURE_SENSOR;
		return;
	}

	DBG("wf_smu: Slots Fans tick ! Slots power: %d.%03d\n",
	    FIX32TOPRINT(power));

#if 0 /* Check what makes a good overtemp condition */
	if (power > (st->pid.param.itarget + 0x50000))
		wf_smu_failure_state |= FAILURE_OVERTEMP;
#endif

	new_setpoint = wf_pid_run(&st->pid, power);

	DBG("wf_smu: new_setpoint: %d\n", (int)new_setpoint);

	if (st->setpoint == new_setpoint)
		return;
	st->setpoint = new_setpoint;
 readjust:
	if (fan_slots && wf_smu_failure_state == 0) {
		rc = wf_control_set(fan_slots, st->setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: Slots fan error %d\n",
			       rc);
			wf_smu_failure_state |= FAILURE_FAN;
		}
	}
}


/*
 * ****** Setup / Init / Misc ... ******
 *
 */

static void wf_smu_tick(void)
{
	unsigned int last_failure = wf_smu_failure_state;
	unsigned int new_failure;

	if (!wf_smu_started) {
		DBG("wf: creating control loops !\n");
		wf_smu_create_drive_fans();
		wf_smu_create_slots_fans();
		wf_smu_create_cpu_fans();
		wf_smu_started = true;
	}

	/* Skipping ticks */
	if (wf_smu_skipping && --wf_smu_skipping)
		return;

	wf_smu_failure_state = 0;
	if (wf_smu_drive_fans)
		wf_smu_drive_fans_tick(wf_smu_drive_fans);
	if (wf_smu_slots_fans)
		wf_smu_slots_fans_tick(wf_smu_slots_fans);
	if (wf_smu_cpu_fans)
		wf_smu_cpu_fans_tick(wf_smu_cpu_fans);

	wf_smu_readjust = 0;
	new_failure = wf_smu_failure_state & ~last_failure;

	/* If entering failure mode, clamp cpufreq and ramp all
	 * fans to full speed.
	 */
	if (wf_smu_failure_state && !last_failure) {
		if (cpufreq_clamp)
			wf_control_set_max(cpufreq_clamp);
		if (fan_cpu_main)
			wf_control_set_max(fan_cpu_main);
		if (fan_cpu_second)
			wf_control_set_max(fan_cpu_second);
		if (fan_cpu_third)
			wf_control_set_max(fan_cpu_third);
		if (fan_hd)
			wf_control_set_max(fan_hd);
		if (fan_slots)
			wf_control_set_max(fan_slots);
	}

	/* If leaving failure mode, unclamp cpufreq and readjust
	 * all fans on next iteration
	 */
	if (!wf_smu_failure_state && last_failure) {
		if (cpufreq_clamp)
			wf_control_set_min(cpufreq_clamp);
		wf_smu_readjust = 1;
	}

	/* Overtemp condition detected, notify and start skipping a couple
	 * ticks to let the temperature go down
	 */
	if (new_failure & FAILURE_OVERTEMP) {
		wf_set_overtemp();
		wf_smu_skipping = 2;
		wf_smu_overtemp = true;
	}

	/* We only clear the overtemp condition if overtemp is cleared
	 * _and_ no other failure is present. Since a sensor error will
	 * clear the overtemp condition (can't measure temperature) at
	 * the control loop levels, but we don't want to keep it clear
	 * here in this case
	 */
	if (!wf_smu_failure_state && wf_smu_overtemp) {
		wf_clear_overtemp();
		wf_smu_overtemp = false;
	}
}


static void wf_smu_new_control(struct wf_control *ct)
{
	if (wf_smu_all_controls_ok)
		return;

	if (fan_cpu_main == NULL && !strcmp(ct->name, "cpu-rear-fan-0")) {
		if (wf_get_control(ct) == 0)
			fan_cpu_main = ct;
	}

	if (fan_cpu_second == NULL && !strcmp(ct->name, "cpu-rear-fan-1")) {
		if (wf_get_control(ct) == 0)
			fan_cpu_second = ct;
	}

	if (fan_cpu_third == NULL && !strcmp(ct->name, "cpu-front-fan-0")) {
		if (wf_get_control(ct) == 0)
			fan_cpu_third = ct;
	}

	if (cpufreq_clamp == NULL && !strcmp(ct->name, "cpufreq-clamp")) {
		if (wf_get_control(ct) == 0)
			cpufreq_clamp = ct;
	}

	if (fan_hd == NULL && !strcmp(ct->name, "drive-bay-fan")) {
		if (wf_get_control(ct) == 0)
			fan_hd = ct;
	}

	if (fan_slots == NULL && !strcmp(ct->name, "slots-fan")) {
		if (wf_get_control(ct) == 0)
			fan_slots = ct;
	}

	if (fan_cpu_main && (fan_cpu_second || fan_cpu_third) && fan_hd &&
	    fan_slots && cpufreq_clamp)
		wf_smu_all_controls_ok = 1;
}

static void wf_smu_new_sensor(struct wf_sensor *sr)
{
	if (wf_smu_all_sensors_ok)
		return;

	if (sensor_cpu_power == NULL && !strcmp(sr->name, "cpu-power")) {
		if (wf_get_sensor(sr) == 0)
			sensor_cpu_power = sr;
	}

	if (sensor_cpu_temp == NULL && !strcmp(sr->name, "cpu-temp")) {
		if (wf_get_sensor(sr) == 0)
			sensor_cpu_temp = sr;
	}

	if (sensor_hd_temp == NULL && !strcmp(sr->name, "hd-temp")) {
		if (wf_get_sensor(sr) == 0)
			sensor_hd_temp = sr;
	}

	if (sensor_slots_power == NULL && !strcmp(sr->name, "slots-power")) {
		if (wf_get_sensor(sr) == 0)
			sensor_slots_power = sr;
	}

	if (sensor_cpu_power && sensor_cpu_temp &&
	    sensor_hd_temp && sensor_slots_power)
		wf_smu_all_sensors_ok = 1;
}


static int wf_smu_notify(struct notifier_block *self,
			       unsigned long event, void *data)
{
	switch(event) {
	case WF_EVENT_NEW_CONTROL:
		DBG("wf: new control %s detected\n",
		    ((struct wf_control *)data)->name);
		wf_smu_new_control(data);
		wf_smu_readjust = 1;
		break;
	case WF_EVENT_NEW_SENSOR:
		DBG("wf: new sensor %s detected\n",
		    ((struct wf_sensor *)data)->name);
		wf_smu_new_sensor(data);
		break;
	case WF_EVENT_TICK:
		if (wf_smu_all_controls_ok && wf_smu_all_sensors_ok)
			wf_smu_tick();
	}

	return 0;
}

static struct notifier_block wf_smu_events = {
	.notifier_call	= wf_smu_notify,
};

static int wf_init_pm(void)
{
	printk(KERN_INFO "windfarm: Initializing for Desktop G5 model\n");

	return 0;
}

static int wf_smu_probe(struct platform_device *ddev)
{
	wf_register_client(&wf_smu_events);

	return 0;
}

static int wf_smu_remove(struct platform_device *ddev)
{
	wf_unregister_client(&wf_smu_events);

	/* XXX We don't have yet a guarantee that our callback isn't
	 * in progress when returning from wf_unregister_client, so
	 * we add an arbitrary delay. I'll have to fix that in the core
	 */
	msleep(1000);

	/* Release all sensors */
	/* One more crappy race: I don't think we have any guarantee here
	 * that the attribute callback won't race with the sensor beeing
	 * disposed of, and I'm not 100% certain what best way to deal
	 * with that except by adding locks all over... I'll do that
	 * eventually but heh, who ever rmmod this module anyway ?
	 */
	if (sensor_cpu_power)
		wf_put_sensor(sensor_cpu_power);
	if (sensor_cpu_temp)
		wf_put_sensor(sensor_cpu_temp);
	if (sensor_hd_temp)
		wf_put_sensor(sensor_hd_temp);
	if (sensor_slots_power)
		wf_put_sensor(sensor_slots_power);

	/* Release all controls */
	if (fan_cpu_main)
		wf_put_control(fan_cpu_main);
	if (fan_cpu_second)
		wf_put_control(fan_cpu_second);
	if (fan_cpu_third)
		wf_put_control(fan_cpu_third);
	if (fan_hd)
		wf_put_control(fan_hd);
	if (fan_slots)
		wf_put_control(fan_slots);
	if (cpufreq_clamp)
		wf_put_control(cpufreq_clamp);

	/* Destroy control loops state structures */
	kfree(wf_smu_slots_fans);
	kfree(wf_smu_drive_fans);
	kfree(wf_smu_cpu_fans);

	return 0;
}

static struct platform_driver wf_smu_driver = {
        .probe = wf_smu_probe,
        .remove = wf_smu_remove,
	.driver = {
		.name = "windfarm",
	},
};


static int __init wf_smu_init(void)
{
	int rc = -ENODEV;

	if (of_machine_is_compatible("PowerMac9,1"))
		rc = wf_init_pm();

	if (rc == 0) {
#ifdef MODULE
		request_module("windfarm_smu_controls");
		request_module("windfarm_smu_sensors");
		request_module("windfarm_lm75_sensor");
		request_module("windfarm_cpufreq_clamp");

#endif /* MODULE */
		platform_driver_register(&wf_smu_driver);
	}

	return rc;
}

static void __exit wf_smu_exit(void)
{

	platform_driver_unregister(&wf_smu_driver);
}


module_init(wf_smu_init);
module_exit(wf_smu_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Thermal control logic for PowerMac9,1");
MODULE_LICENSE("GPL");

MODULE_ALIAS("platform:windfarm");
