/*
 * Windfarm PowerMac thermal control. iMac G5
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 * Released under the term of the GNU GPL v2.
 *
 * The algorithm used is the PID control algorithm, used the same
 * way the published Darwin code does, using the same values that
 * are present in the Darwin 8.2 snapshot property lists (note however
 * that none of the code has been re-used, it's a complete re-implementation
 *
 * The various control loops found in Darwin config file are:
 *
 * PowerMac8,1 and PowerMac8,2
 * ===========================
 *
 * System Fans control loop. Different based on models. In addition to the
 * usual PID algorithm, the control loop gets 2 additional pairs of linear
 * scaling factors (scale/offsets) expressed as 4.12 fixed point values
 * signed offset, unsigned scale)
 *
 * The targets are modified such as:
 *  - the linked control (second control) gets the target value as-is
 *    (typically the drive fan)
 *  - the main control (first control) gets the target value scaled with
 *    the first pair of factors, and is then modified as below
 *  - the value of the target of the CPU Fan control loop is retrieved,
 *    scaled with the second pair of factors, and the max of that and
 *    the scaled target is applied to the main control.
 *
 * # model_id: 2
 *   controls       : system-fan, drive-bay-fan
 *   sensors        : hd-temp
 *   PID params     : G_d = 0x15400000
 *                    G_p = 0x00200000
 *                    G_r = 0x000002fd
 *                    History = 2 entries
 *                    Input target = 0x3a0000
 *                    Interval = 5s
 *   linear-factors : offset = 0xff38 scale  = 0x0ccd
 *                    offset = 0x0208 scale  = 0x07ae
 *
 * # model_id: 3
 *   controls       : system-fan, drive-bay-fan
 *   sensors        : hd-temp
 *   PID params     : G_d = 0x08e00000
 *                    G_p = 0x00566666
 *                    G_r = 0x0000072b
 *                    History = 2 entries
 *                    Input target = 0x350000
 *                    Interval = 5s
 *   linear-factors : offset = 0xff38 scale  = 0x0ccd
 *                    offset = 0x0000 scale  = 0x0000
 *
 * # model_id: 5
 *   controls       : system-fan
 *   sensors        : hd-temp
 *   PID params     : G_d = 0x15400000
 *                    G_p = 0x00233333
 *                    G_r = 0x000002fd
 *                    History = 2 entries
 *                    Input target = 0x3a0000
 *                    Interval = 5s
 *   linear-factors : offset = 0x0000 scale  = 0x1000
 *                    offset = 0x0091 scale  = 0x0bae
 *
 * CPU Fan control loop. The loop is identical for all models. it
 * has an additional pair of scaling factor. This is used to scale the
 * systems fan control loop target result (the one before it gets scaled
 * by the System Fans control loop itself). Then, the max value of the
 * calculated target value and system fan value is sent to the fans
 *
 *   controls       : cpu-fan
 *   sensors        : cpu-temp cpu-power
 *   PID params     : From SMU sdb partition
 *   linear-factors : offset = 0xfb50 scale  = 0x1000
 *
 * CPU Slew control loop. Not implemented. The cpufreq driver in linux is
 * completely separate for now, though we could find a way to link it, either
 * as a client reacting to overtemp notifications, or directling monitoring
 * the CPU temperature
 *
 * WARNING ! The CPU control loop requires the CPU tmax for the current
 * operating point. However, we currently are completely separated from
 * the cpufreq driver and thus do not know what the current operating
 * point is. Fortunately, we also do not have any hardware supporting anything
 * but operating point 0 at the moment, thus we just peek that value directly
 * from the SDB partition. If we ever end up with actually slewing the system
 * clock and thus changing operating points, we'll have to find a way to
 * communicate with the CPU freq driver;
 *
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
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/system.h>
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

static int wf_smu_mach_model;	/* machine model id */

static struct device *wf_smu_dev;

/* Controls & sensors */
static struct wf_sensor	*sensor_cpu_power;
static struct wf_sensor	*sensor_cpu_temp;
static struct wf_sensor	*sensor_hd_temp;
static struct wf_control *fan_cpu_main;
static struct wf_control *fan_hd;
static struct wf_control *fan_system;
static struct wf_control *cpufreq_clamp;

/* Set to kick the control loop into life */
static int wf_smu_all_controls_ok, wf_smu_all_sensors_ok, wf_smu_started;

/* Failure handling.. could be nicer */
#define FAILURE_FAN		0x01
#define FAILURE_SENSOR		0x02
#define FAILURE_OVERTEMP	0x04

static unsigned int wf_smu_failure_state;
static int wf_smu_readjust, wf_smu_skipping;

/*
 * ****** System Fans Control Loop ******
 *
 */

/* Parameters for the System Fans control loop. Parameters
 * not in this table such as interval, history size, ...
 * are common to all versions and thus hard coded for now.
 */
struct wf_smu_sys_fans_param {
	int	model_id;
	s32	itarget;
	s32	gd, gp, gr;

	s16	offset0;
	u16	scale0;
	s16	offset1;
	u16	scale1;
};

#define WF_SMU_SYS_FANS_INTERVAL	5
#define WF_SMU_SYS_FANS_HISTORY_SIZE	2

/* State data used by the system fans control loop
 */
struct wf_smu_sys_fans_state {
	int			ticks;
	s32			sys_setpoint;
	s32			hd_setpoint;
	s16			offset0;
	u16			scale0;
	s16			offset1;
	u16			scale1;
	struct wf_pid_state	pid;
};

/*
 * Configs for SMU Sytem Fan control loop
 */
static struct wf_smu_sys_fans_param wf_smu_sys_all_params[] = {
	/* Model ID 2 */
	{
		.model_id	= 2,
		.itarget	= 0x3a0000,
		.gd		= 0x15400000,
		.gp		= 0x00200000,
		.gr		= 0x000002fd,
		.offset0	= 0xff38,
		.scale0		= 0x0ccd,
		.offset1	= 0x0208,
		.scale1		= 0x07ae,
	},
	/* Model ID 3 */
	{
		.model_id	= 3,
		.itarget	= 0x350000,
		.gd		= 0x08e00000,
		.gp		= 0x00566666,
		.gr		= 0x0000072b,
		.offset0	= 0xff38,
		.scale0		= 0x0ccd,
		.offset1	= 0x0000,
		.scale1		= 0x0000,
	},
	/* Model ID 5 */
	{
		.model_id	= 5,
		.itarget	= 0x3a0000,
		.gd		= 0x15400000,
		.gp		= 0x00233333,
		.gr		= 0x000002fd,
		.offset0	= 0x0000,
		.scale0		= 0x1000,
		.offset1	= 0x0091,
		.scale1		= 0x0bae,
	},
};
#define WF_SMU_SYS_FANS_NUM_CONFIGS ARRAY_SIZE(wf_smu_sys_all_params)

static struct wf_smu_sys_fans_state *wf_smu_sys_fans;

/*
 * ****** CPU Fans Control Loop ******
 *
 */


#define WF_SMU_CPU_FANS_INTERVAL	1
#define WF_SMU_CPU_FANS_MAX_HISTORY	16
#define WF_SMU_CPU_FANS_SIBLING_SCALE	0x00001000
#define WF_SMU_CPU_FANS_SIBLING_OFFSET	0xfffffb50

/* State data used by the cpu fans control loop
 */
struct wf_smu_cpu_fans_state {
	int			ticks;
	s32			cpu_setpoint;
	s32			scale;
	s32			offset;
	struct wf_cpu_pid_state	pid;
};

static struct wf_smu_cpu_fans_state *wf_smu_cpu_fans;



/*
 * ***** Implementation *****
 *
 */

static void wf_smu_create_sys_fans(void)
{
	struct wf_smu_sys_fans_param *param = NULL;
	struct wf_pid_param pid_param;
	int i;

	/* First, locate the params for this model */
	for (i = 0; i < WF_SMU_SYS_FANS_NUM_CONFIGS; i++)
		if (wf_smu_sys_all_params[i].model_id == wf_smu_mach_model) {
			param = &wf_smu_sys_all_params[i];
			break;
		}

	/* No params found, put fans to max */
	if (param == NULL) {
		printk(KERN_WARNING "windfarm: System fan config not found "
		       "for this machine model, max fan speed\n");
		goto fail;
	}

	/* Alloc & initialize state */
	wf_smu_sys_fans = kmalloc(sizeof(struct wf_smu_sys_fans_state),
				  GFP_KERNEL);
	if (wf_smu_sys_fans == NULL) {
		printk(KERN_WARNING "windfarm: Memory allocation error"
		       " max fan speed\n");
		goto fail;
	}
	wf_smu_sys_fans->ticks = 1;
	wf_smu_sys_fans->scale0 = param->scale0;
	wf_smu_sys_fans->offset0 = param->offset0;
	wf_smu_sys_fans->scale1 = param->scale1;
	wf_smu_sys_fans->offset1 = param->offset1;

	/* Fill PID params */
	pid_param.gd = param->gd;
	pid_param.gp = param->gp;
	pid_param.gr = param->gr;
	pid_param.interval = WF_SMU_SYS_FANS_INTERVAL;
	pid_param.history_len = WF_SMU_SYS_FANS_HISTORY_SIZE;
	pid_param.itarget = param->itarget;
	pid_param.min = fan_system->ops->get_min(fan_system);
	pid_param.max = fan_system->ops->get_max(fan_system);
	if (fan_hd) {
		pid_param.min =
			max(pid_param.min,fan_hd->ops->get_min(fan_hd));
		pid_param.max =
			min(pid_param.max,fan_hd->ops->get_max(fan_hd));
	}
	wf_pid_init(&wf_smu_sys_fans->pid, &pid_param);

	DBG("wf: System Fan control initialized.\n");
	DBG("    itarged=%d.%03d, min=%d RPM, max=%d RPM\n",
	    FIX32TOPRINT(pid_param.itarget), pid_param.min, pid_param.max);
	return;

 fail:

	if (fan_system)
		wf_control_set_max(fan_system);
	if (fan_hd)
		wf_control_set_max(fan_hd);
}

static void wf_smu_sys_fans_tick(struct wf_smu_sys_fans_state *st)
{
	s32 new_setpoint, temp, scaled, cputarget;
	int rc;

	if (--st->ticks != 0) {
		if (wf_smu_readjust)
			goto readjust;
		return;
	}
	st->ticks = WF_SMU_SYS_FANS_INTERVAL;

	rc = sensor_hd_temp->ops->get_value(sensor_hd_temp, &temp);
	if (rc) {
		printk(KERN_WARNING "windfarm: HD temp sensor error %d\n",
		       rc);
		wf_smu_failure_state |= FAILURE_SENSOR;
		return;
	}

	DBG("wf_smu: System Fans tick ! HD temp: %d.%03d\n",
	    FIX32TOPRINT(temp));

	if (temp > (st->pid.param.itarget + 0x50000))
		wf_smu_failure_state |= FAILURE_OVERTEMP;

	new_setpoint = wf_pid_run(&st->pid, temp);

	DBG("wf_smu: new_setpoint: %d RPM\n", (int)new_setpoint);

	scaled = ((((s64)new_setpoint) * (s64)st->scale0) >> 12) + st->offset0;

	DBG("wf_smu: scaled setpoint: %d RPM\n", (int)scaled);

	cputarget = wf_smu_cpu_fans ? wf_smu_cpu_fans->pid.target : 0;
	cputarget = ((((s64)cputarget) * (s64)st->scale1) >> 12) + st->offset1;
	scaled = max(scaled, cputarget);
	scaled = max(scaled, st->pid.param.min);
	scaled = min(scaled, st->pid.param.max);

	DBG("wf_smu: adjusted setpoint: %d RPM\n", (int)scaled);

	if (st->sys_setpoint == scaled && new_setpoint == st->hd_setpoint)
		return;
	st->sys_setpoint = scaled;
	st->hd_setpoint = new_setpoint;
 readjust:
	if (fan_system && wf_smu_failure_state == 0) {
		rc = fan_system->ops->set_value(fan_system, st->sys_setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: Sys fan error %d\n",
			       rc);
			wf_smu_failure_state |= FAILURE_FAN;
		}
	}
	if (fan_hd && wf_smu_failure_state == 0) {
		rc = fan_hd->ops->set_value(fan_hd, st->hd_setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: HD fan error %d\n",
			       rc);
			wf_smu_failure_state |= FAILURE_FAN;
		}
	}
}

static void wf_smu_create_cpu_fans(void)
{
	struct wf_cpu_pid_param pid_param;
	struct smu_sdbp_header *hdr;
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

	wf_smu_cpu_fans->scale = WF_SMU_CPU_FANS_SIBLING_SCALE;
	wf_smu_cpu_fans->offset = WF_SMU_CPU_FANS_SIBLING_OFFSET;

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

	pid_param.min = fan_cpu_main->ops->get_min(fan_cpu_main);
	pid_param.max = fan_cpu_main->ops->get_max(fan_cpu_main);

	wf_cpu_pid_init(&wf_smu_cpu_fans->pid, &pid_param);

	DBG("wf: CPU Fan control initialized.\n");
	DBG("    ttarged=%d.%03d, tmax=%d.%03d, min=%d RPM, max=%d RPM\n",
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
	s32 new_setpoint, temp, power, systarget;
	int rc;

	if (--st->ticks != 0) {
		if (wf_smu_readjust)
			goto readjust;
		return;
	}
	st->ticks = WF_SMU_CPU_FANS_INTERVAL;

	rc = sensor_cpu_temp->ops->get_value(sensor_cpu_temp, &temp);
	if (rc) {
		printk(KERN_WARNING "windfarm: CPU temp sensor error %d\n",
		       rc);
		wf_smu_failure_state |= FAILURE_SENSOR;
		return;
	}

	rc = sensor_cpu_power->ops->get_value(sensor_cpu_power, &power);
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

	systarget = wf_smu_sys_fans ? wf_smu_sys_fans->pid.target : 0;
	systarget = ((((s64)systarget) * (s64)st->scale) >> 12)
		+ st->offset;
	new_setpoint = max(new_setpoint, systarget);
	new_setpoint = max(new_setpoint, st->pid.param.min);
	new_setpoint = min(new_setpoint, st->pid.param.max);

	DBG("wf_smu: adjusted setpoint: %d RPM\n", (int)new_setpoint);

	if (st->cpu_setpoint == new_setpoint)
		return;
	st->cpu_setpoint = new_setpoint;
 readjust:
	if (fan_cpu_main && wf_smu_failure_state == 0) {
		rc = fan_cpu_main->ops->set_value(fan_cpu_main,
						  st->cpu_setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: CPU main fan"
			       " error %d\n", rc);
			wf_smu_failure_state |= FAILURE_FAN;
		}
	}
}


/*
 * ****** Attributes ******
 *
 */

#define BUILD_SHOW_FUNC_FIX(name, data)				\
static ssize_t show_##name(struct device *dev,                  \
			   struct device_attribute *attr,       \
			   char *buf)	                        \
{								\
	ssize_t r;						\
	s32 val = 0;                                            \
	data->ops->get_value(data, &val);                       \
	r = sprintf(buf, "%d.%03d", FIX32TOPRINT(val)); 	\
	return r;						\
}                                                               \
static DEVICE_ATTR(name,S_IRUGO,show_##name, NULL);


#define BUILD_SHOW_FUNC_INT(name, data)				\
static ssize_t show_##name(struct device *dev,                  \
			   struct device_attribute *attr,       \
			   char *buf)	                        \
{								\
	s32 val = 0;                                            \
	data->ops->get_value(data, &val);                       \
	return sprintf(buf, "%d", val);  			\
}                                                               \
static DEVICE_ATTR(name,S_IRUGO,show_##name, NULL);

BUILD_SHOW_FUNC_INT(cpu_fan, fan_cpu_main);
BUILD_SHOW_FUNC_INT(sys_fan, fan_system);
BUILD_SHOW_FUNC_INT(hd_fan, fan_hd);

BUILD_SHOW_FUNC_FIX(cpu_temp, sensor_cpu_temp);
BUILD_SHOW_FUNC_FIX(cpu_power, sensor_cpu_power);
BUILD_SHOW_FUNC_FIX(hd_temp, sensor_hd_temp);

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
		wf_smu_create_sys_fans();
		wf_smu_create_cpu_fans();
		wf_smu_started = 1;
	}

	/* Skipping ticks */
	if (wf_smu_skipping && --wf_smu_skipping)
		return;

	wf_smu_failure_state = 0;
	if (wf_smu_sys_fans)
		wf_smu_sys_fans_tick(wf_smu_sys_fans);
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
		if (fan_system)
			wf_control_set_max(fan_system);
		if (fan_cpu_main)
			wf_control_set_max(fan_cpu_main);
		if (fan_hd)
			wf_control_set_max(fan_hd);
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
	}

	/* We only clear the overtemp condition if overtemp is cleared
	 * _and_ no other failure is present. Since a sensor error will
	 * clear the overtemp condition (can't measure temperature) at
	 * the control loop levels, but we don't want to keep it clear
	 * here in this case
	 */
	if (new_failure == 0 && last_failure & FAILURE_OVERTEMP)
		wf_clear_overtemp();
}

static void wf_smu_new_control(struct wf_control *ct)
{
	if (wf_smu_all_controls_ok)
		return;

	if (fan_cpu_main == NULL && !strcmp(ct->name, "cpu-fan")) {
		if (wf_get_control(ct) == 0) {
			fan_cpu_main = ct;
			device_create_file(wf_smu_dev, &dev_attr_cpu_fan);
		}
	}

	if (fan_system == NULL && !strcmp(ct->name, "system-fan")) {
		if (wf_get_control(ct) == 0) {
			fan_system = ct;
			device_create_file(wf_smu_dev, &dev_attr_sys_fan);
		}
	}

	if (cpufreq_clamp == NULL && !strcmp(ct->name, "cpufreq-clamp")) {
		if (wf_get_control(ct) == 0)
			cpufreq_clamp = ct;
	}

	/* Darwin property list says the HD fan is only for model ID
	 * 0, 1, 2 and 3
	 */

	if (wf_smu_mach_model > 3) {
		if (fan_system && fan_cpu_main && cpufreq_clamp)
			wf_smu_all_controls_ok = 1;
		return;
	}

	if (fan_hd == NULL && !strcmp(ct->name, "drive-bay-fan")) {
		if (wf_get_control(ct) == 0) {
			fan_hd = ct;
			device_create_file(wf_smu_dev, &dev_attr_hd_fan);
		}
	}

	if (fan_system && fan_hd && fan_cpu_main && cpufreq_clamp)
		wf_smu_all_controls_ok = 1;
}

static void wf_smu_new_sensor(struct wf_sensor *sr)
{
	if (wf_smu_all_sensors_ok)
		return;

	if (sensor_cpu_power == NULL && !strcmp(sr->name, "cpu-power")) {
		if (wf_get_sensor(sr) == 0) {
			sensor_cpu_power = sr;
			device_create_file(wf_smu_dev, &dev_attr_cpu_power);
		}
	}

	if (sensor_cpu_temp == NULL && !strcmp(sr->name, "cpu-temp")) {
		if (wf_get_sensor(sr) == 0) {
			sensor_cpu_temp = sr;
			device_create_file(wf_smu_dev, &dev_attr_cpu_temp);
		}
	}

	if (sensor_hd_temp == NULL && !strcmp(sr->name, "hd-temp")) {
		if (wf_get_sensor(sr) == 0) {
			sensor_hd_temp = sr;
			device_create_file(wf_smu_dev, &dev_attr_hd_temp);
		}
	}

	if (sensor_cpu_power && sensor_cpu_temp && sensor_hd_temp)
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
	struct smu_sdbp_header *hdr;

	hdr = smu_get_sdb_partition(SMU_SDB_SENSORTREE_ID, NULL);
	if (hdr != 0) {
		struct smu_sdbp_sensortree *st =
			(struct smu_sdbp_sensortree *)&hdr[1];
		wf_smu_mach_model = st->model_id;
	}

	printk(KERN_INFO "windfarm: Initializing for iMacG5 model ID %d\n",
	       wf_smu_mach_model);

	return 0;
}

static int wf_smu_probe(struct device *ddev)
{
	wf_smu_dev = ddev;

	wf_register_client(&wf_smu_events);

	return 0;
}

static int wf_smu_remove(struct device *ddev)
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
	if (sensor_cpu_power) {
		device_remove_file(wf_smu_dev, &dev_attr_cpu_power);
		wf_put_sensor(sensor_cpu_power);
	}
	if (sensor_cpu_temp) {
		device_remove_file(wf_smu_dev, &dev_attr_cpu_temp);
		wf_put_sensor(sensor_cpu_temp);
	}
	if (sensor_hd_temp) {
		device_remove_file(wf_smu_dev, &dev_attr_hd_temp);
		wf_put_sensor(sensor_hd_temp);
	}

	/* Release all controls */
	if (fan_cpu_main) {
		device_remove_file(wf_smu_dev, &dev_attr_cpu_fan);
		wf_put_control(fan_cpu_main);
	}
	if (fan_hd) {
		device_remove_file(wf_smu_dev, &dev_attr_hd_fan);
		wf_put_control(fan_hd);
	}
	if (fan_system) {
		device_remove_file(wf_smu_dev, &dev_attr_sys_fan);
		wf_put_control(fan_system);
	}
	if (cpufreq_clamp)
		wf_put_control(cpufreq_clamp);

	/* Destroy control loops state structures */
	if (wf_smu_sys_fans)
		kfree(wf_smu_sys_fans);
	if (wf_smu_cpu_fans)
		kfree(wf_smu_cpu_fans);

	wf_smu_dev = NULL;

	return 0;
}

static struct device_driver wf_smu_driver = {
        .name = "windfarm",
        .bus = &platform_bus_type,
        .probe = wf_smu_probe,
        .remove = wf_smu_remove,
};


static int __init wf_smu_init(void)
{
	int rc = -ENODEV;

	if (machine_is_compatible("PowerMac8,1") ||
	    machine_is_compatible("PowerMac8,2"))
		rc = wf_init_pm();

	if (rc == 0) {
#ifdef MODULE
		request_module("windfarm_smu_controls");
		request_module("windfarm_smu_sensors");
		request_module("windfarm_lm75_sensor");

#endif /* MODULE */
		driver_register(&wf_smu_driver);
	}

	return rc;
}

static void __exit wf_smu_exit(void)
{

	driver_unregister(&wf_smu_driver);
}


module_init(wf_smu_init);
module_exit(wf_smu_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Thermal control logic for iMac G5");
MODULE_LICENSE("GPL");

