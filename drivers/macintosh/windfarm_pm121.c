// SPDX-License-Identifier: GPL-2.0-only
/*
 * Windfarm PowerMac thermal control. iMac G5 iSight
 *
 * (c) Copyright 2007 Étienne Bersac <bersace@gmail.com>
 *
 * Bits & pieces from windfarm_pm81.c by (c) Copyright 2005 Benjamin
 * Herrenschmidt, IBM Corp. <benh@kernel.crashing.org>
 *
 * PowerMac12,1
 * ============
 *
 * The algorithm used is the PID control algorithm, used the same way
 * the published Darwin code does, using the same values that are
 * present in the Darwin 8.10 snapshot property lists (note however
 * that none of the code has been re-used, it's a complete
 * re-implementation
 *
 * There is two models using PowerMac12,1. Model 2 is iMac G5 iSight
 * 17" while Model 3 is iMac G5 20". They do have both the same
 * controls with a tiny difference. The control-ids of hard-drive-fan
 * and cpu-fan is swapped.
 *
 * Target Correction :
 *
 * controls have a target correction calculated as :
 *
 * new_min = ((((average_power * slope) >> 16) + offset) >> 16) + min_value
 * new_value = max(new_value, max(new_min, 0))
 *
 * OD Fan control correction.
 *
 * # model_id: 2
 *   offset		: -19563152
 *   slope		:  1956315
 *
 * # model_id: 3
 *   offset		: -15650652
 *   slope		:  1565065
 *
 * HD Fan control correction.
 *
 * # model_id: 2
 *   offset		: -15650652
 *   slope		:  1565065
 *
 * # model_id: 3
 *   offset		: -19563152
 *   slope		:  1956315
 *
 * CPU Fan control correction.
 *
 * # model_id: 2
 *   offset		: -25431900
 *   slope		:  2543190
 *
 * # model_id: 3
 *   offset		: -15650652
 *   slope		:  1565065
 *
 * Target rubber-banding :
 *
 * Some controls have a target correction which depends on another
 * control value. The correction is computed in the following way :
 *
 * new_min = ref_value * slope + offset
 *
 * ref_value is the value of the reference control. If new_min is
 * greater than 0, then we correct the target value using :
 *
 * new_target = max (new_target, new_min >> 16)
 *
 * # model_id : 2
 *   control	: cpu-fan
 *   ref	: optical-drive-fan
 *   offset	: -15650652
 *   slope	: 1565065
 *
 * # model_id : 3
 *   control	: optical-drive-fan
 *   ref	: hard-drive-fan
 *   offset	: -32768000
 *   slope	: 65536
 *
 * In order to have the moste efficient correction with those
 * dependencies, we must trigger HD loop before OD loop before CPU
 * loop.
 *
 * The various control loops found in Darwin config file are:
 *
 * HD Fan control loop.
 *
 * # model_id: 2
 *   control        : hard-drive-fan
 *   sensor         : hard-drive-temp
 *   PID params     : G_d = 0x00000000
 *                    G_p = 0x002D70A3
 *                    G_r = 0x00019999
 *                    History = 2 entries
 *                    Input target = 0x370000
 *                    Interval = 5s
 *
 * # model_id: 3
 *   control        : hard-drive-fan
 *   sensor         : hard-drive-temp
 *   PID params     : G_d = 0x00000000
 *                    G_p = 0x002170A3
 *                    G_r = 0x00019999
 *                    History = 2 entries
 *                    Input target = 0x370000
 *                    Interval = 5s
 *
 * OD Fan control loop.
 *
 * # model_id: 2
 *   control        : optical-drive-fan
 *   sensor         : optical-drive-temp
 *   PID params     : G_d = 0x00000000
 *                    G_p = 0x001FAE14
 *                    G_r = 0x00019999
 *                    History = 2 entries
 *                    Input target = 0x320000
 *                    Interval = 5s
 *
 * # model_id: 3
 *   control        : optical-drive-fan
 *   sensor         : optical-drive-temp
 *   PID params     : G_d = 0x00000000
 *                    G_p = 0x001FAE14
 *                    G_r = 0x00019999
 *                    History = 2 entries
 *                    Input target = 0x320000
 *                    Interval = 5s
 *
 * GPU Fan control loop.
 *
 * # model_id: 2
 *   control        : hard-drive-fan
 *   sensor         : gpu-temp
 *   PID params     : G_d = 0x00000000
 *                    G_p = 0x002A6666
 *                    G_r = 0x00019999
 *                    History = 2 entries
 *                    Input target = 0x5A0000
 *                    Interval = 5s
 *
 * # model_id: 3
 *   control        : cpu-fan
 *   sensor         : gpu-temp
 *   PID params     : G_d = 0x00000000
 *                    G_p = 0x0010CCCC
 *                    G_r = 0x00019999
 *                    History = 2 entries
 *                    Input target = 0x500000
 *                    Interval = 5s
 *
 * KODIAK (aka northbridge) Fan control loop.
 *
 * # model_id: 2
 *   control        : optical-drive-fan
 *   sensor         : north-bridge-temp
 *   PID params     : G_d = 0x00000000
 *                    G_p = 0x003BD70A
 *                    G_r = 0x00019999
 *                    History = 2 entries
 *                    Input target = 0x550000
 *                    Interval = 5s
 *
 * # model_id: 3
 *   control        : hard-drive-fan
 *   sensor         : north-bridge-temp
 *   PID params     : G_d = 0x00000000
 *                    G_p = 0x0030F5C2
 *                    G_r = 0x00019999
 *                    History = 2 entries
 *                    Input target = 0x550000
 *                    Interval = 5s
 *
 * CPU Fan control loop.
 *
 *   control        : cpu-fan
 *   sensors        : cpu-temp, cpu-power
 *   PID params     : from SDB partition
 *
 * CPU Slew control loop.
 *
 *   control        : cpufreq-clamp
 *   sensor         : cpu-temp
 */

#undef	DEBUG

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
#include <asm/sections.h>
#include <asm/smu.h>

#include "windfarm.h"
#include "windfarm_pid.h"

#define VERSION "0.3"

static int pm121_mach_model;	/* machine model id */

/* Controls & sensors */
static struct wf_sensor	*sensor_cpu_power;
static struct wf_sensor	*sensor_cpu_temp;
static struct wf_sensor	*sensor_cpu_voltage;
static struct wf_sensor	*sensor_cpu_current;
static struct wf_sensor	*sensor_gpu_temp;
static struct wf_sensor	*sensor_north_bridge_temp;
static struct wf_sensor	*sensor_hard_drive_temp;
static struct wf_sensor	*sensor_optical_drive_temp;
static struct wf_sensor	*sensor_incoming_air_temp; /* unused ! */

enum {
	FAN_CPU,
	FAN_HD,
	FAN_OD,
	CPUFREQ,
	N_CONTROLS
};
static struct wf_control *controls[N_CONTROLS] = {};

/* Set to kick the control loop into life */
static int pm121_all_controls_ok, pm121_all_sensors_ok;
static bool pm121_started;

enum {
	FAILURE_FAN		= 1 << 0,
	FAILURE_SENSOR		= 1 << 1,
	FAILURE_OVERTEMP	= 1 << 2
};

/* All sys loops. Note the HD before the OD loop in order to have it
   run before. */
enum {
	LOOP_GPU,		/* control = hd or cpu, but luckily,
				   it doesn't matter */
	LOOP_HD,		/* control = hd */
	LOOP_KODIAK,		/* control = hd or od */
	LOOP_OD,		/* control = od */
	N_LOOPS
};

static const char *loop_names[N_LOOPS] = {
	"GPU",
	"HD",
	"KODIAK",
	"OD",
};

#define	PM121_NUM_CONFIGS	2

static unsigned int pm121_failure_state;
static int pm121_readjust, pm121_skipping;
static bool pm121_overtemp;
static s32 average_power;

struct pm121_correction {
	int	offset;
	int	slope;
};

static struct pm121_correction corrections[N_CONTROLS][PM121_NUM_CONFIGS] = {
	/* FAN_OD */
	{
		/* MODEL 2 */
		{ .offset	= -19563152,
		  .slope	=  1956315
		},
		/* MODEL 3 */
		{ .offset	= -15650652,
		  .slope	=  1565065
		},
	},
	/* FAN_HD */
	{
		/* MODEL 2 */
		{ .offset	= -15650652,
		  .slope	=  1565065
		},
		/* MODEL 3 */
		{ .offset	= -19563152,
		  .slope	=  1956315
		},
	},
	/* FAN_CPU */
	{
		/* MODEL 2 */
		{ .offset	= -25431900,
		  .slope	=  2543190
		},
		/* MODEL 3 */
		{ .offset	= -15650652,
		  .slope	=  1565065
		},
	},
	/* CPUFREQ has no correction (and is not implemented at all) */
};

struct pm121_connection {
	unsigned int	control_id;
	unsigned int	ref_id;
	struct pm121_correction	correction;
};

static struct pm121_connection pm121_connections[] = {
	/* MODEL 2 */
	{ .control_id	= FAN_CPU,
	  .ref_id	= FAN_OD,
	  { .offset	= -32768000,
	    .slope	=  65536
	  }
	},
	/* MODEL 3 */
	{ .control_id	= FAN_OD,
	  .ref_id	= FAN_HD,
	  { .offset	= -32768000,
	    .slope	=  65536
	  }
	},
};

/* pointer to the current model connection */
static struct pm121_connection *pm121_connection;

/*
 * ****** System Fans Control Loop ******
 *
 */

/* Since each loop handles only one control and we want to avoid
 * writing virtual control, we store the control correction with the
 * loop params. Some data are not set, there are common to all loop
 * and thus, hardcoded.
 */
struct pm121_sys_param {
	/* purely informative since we use mach_model-2 as index */
	int			model_id;
	struct wf_sensor	**sensor; /* use sensor_id instead ? */
	s32			gp, itarget;
	unsigned int		control_id;
};

static struct pm121_sys_param
pm121_sys_all_params[N_LOOPS][PM121_NUM_CONFIGS] = {
	/* GPU Fan control loop */
	{
		{ .model_id	= 2,
		  .sensor	= &sensor_gpu_temp,
		  .gp		= 0x002A6666,
		  .itarget	= 0x5A0000,
		  .control_id	= FAN_HD,
		},
		{ .model_id	= 3,
		  .sensor	= &sensor_gpu_temp,
		  .gp		= 0x0010CCCC,
		  .itarget	= 0x500000,
		  .control_id	= FAN_CPU,
		},
	},
	/* HD Fan control loop */
	{
		{ .model_id	= 2,
		  .sensor	= &sensor_hard_drive_temp,
		  .gp		= 0x002D70A3,
		  .itarget	= 0x370000,
		  .control_id	= FAN_HD,
		},
		{ .model_id	= 3,
		  .sensor	= &sensor_hard_drive_temp,
		  .gp		= 0x002170A3,
		  .itarget	= 0x370000,
		  .control_id	= FAN_HD,
		},
	},
	/* KODIAK Fan control loop */
	{
		{ .model_id	= 2,
		  .sensor	= &sensor_north_bridge_temp,
		  .gp		= 0x003BD70A,
		  .itarget	= 0x550000,
		  .control_id	= FAN_OD,
		},
		{ .model_id	= 3,
		  .sensor	= &sensor_north_bridge_temp,
		  .gp		= 0x0030F5C2,
		  .itarget	= 0x550000,
		  .control_id	= FAN_HD,
		},
	},
	/* OD Fan control loop */
	{
		{ .model_id	= 2,
		  .sensor	= &sensor_optical_drive_temp,
		  .gp		= 0x001FAE14,
		  .itarget	= 0x320000,
		  .control_id	= FAN_OD,
		},
		{ .model_id	= 3,
		  .sensor	= &sensor_optical_drive_temp,
		  .gp		= 0x001FAE14,
		  .itarget	= 0x320000,
		  .control_id	= FAN_OD,
		},
	},
};

/* the hardcoded values */
#define	PM121_SYS_GD		0x00000000
#define	PM121_SYS_GR		0x00019999
#define	PM121_SYS_HISTORY_SIZE	2
#define	PM121_SYS_INTERVAL	5

/* State data used by the system fans control loop
 */
struct pm121_sys_state {
	int			ticks;
	s32			setpoint;
	struct wf_pid_state	pid;
};

struct pm121_sys_state *pm121_sys_state[N_LOOPS] = {};

/*
 * ****** CPU Fans Control Loop ******
 *
 */

#define PM121_CPU_INTERVAL	1

/* State data used by the cpu fans control loop
 */
struct pm121_cpu_state {
	int			ticks;
	s32			setpoint;
	struct wf_cpu_pid_state	pid;
};

static struct pm121_cpu_state *pm121_cpu_state;



/*
 * ***** Implementation *****
 *
 */

/* correction the value using the output-low-bound correction algo */
static s32 pm121_correct(s32 new_setpoint,
			 unsigned int control_id,
			 s32 min)
{
	s32 new_min;
	struct pm121_correction *correction;
	correction = &corrections[control_id][pm121_mach_model - 2];

	new_min = (average_power * correction->slope) >> 16;
	new_min += correction->offset;
	new_min = (new_min >> 16) + min;

	return max3(new_setpoint, new_min, 0);
}

static s32 pm121_connect(unsigned int control_id, s32 setpoint)
{
	s32 new_min, value, new_setpoint;

	if (pm121_connection->control_id == control_id) {
		controls[control_id]->ops->get_value(controls[control_id],
						     &value);
		new_min = value * pm121_connection->correction.slope;
		new_min += pm121_connection->correction.offset;
		if (new_min > 0) {
			new_setpoint = max(setpoint, (new_min >> 16));
			if (new_setpoint != setpoint) {
				pr_debug("pm121: %s depending on %s, "
					 "corrected from %d to %d RPM\n",
					 controls[control_id]->name,
					 controls[pm121_connection->ref_id]->name,
					 (int) setpoint, (int) new_setpoint);
			}
		} else
			new_setpoint = setpoint;
	}
	/* no connection */
	else
		new_setpoint = setpoint;

	return new_setpoint;
}

/* FAN LOOPS */
static void pm121_create_sys_fans(int loop_id)
{
	struct pm121_sys_param *param = NULL;
	struct wf_pid_param pid_param;
	struct wf_control *control = NULL;
	int i;

	/* First, locate the params for this model */
	for (i = 0; i < PM121_NUM_CONFIGS; i++) {
		if (pm121_sys_all_params[loop_id][i].model_id == pm121_mach_model) {
			param = &(pm121_sys_all_params[loop_id][i]);
			break;
		}
	}

	/* No params found, put fans to max */
	if (param == NULL) {
		printk(KERN_WARNING "pm121: %s fan config not found "
		       " for this machine model\n",
		       loop_names[loop_id]);
		goto fail;
	}

	control = controls[param->control_id];

	/* Alloc & initialize state */
	pm121_sys_state[loop_id] = kmalloc(sizeof(struct pm121_sys_state),
					   GFP_KERNEL);
	if (pm121_sys_state[loop_id] == NULL) {
		printk(KERN_WARNING "pm121: Memory allocation error\n");
		goto fail;
	}
	pm121_sys_state[loop_id]->ticks = 1;

	/* Fill PID params */
	pid_param.gd		= PM121_SYS_GD;
	pid_param.gp		= param->gp;
	pid_param.gr		= PM121_SYS_GR;
	pid_param.interval	= PM121_SYS_INTERVAL;
	pid_param.history_len	= PM121_SYS_HISTORY_SIZE;
	pid_param.itarget	= param->itarget;
	if(control)
	{
		pid_param.min		= control->ops->get_min(control);
		pid_param.max		= control->ops->get_max(control);
	} else {
		/*
		 * This is probably not the right!?
		 * Perhaps goto fail  if control == NULL  above?
		 */
		pid_param.min		= 0;
		pid_param.max		= 0;
	}

	wf_pid_init(&pm121_sys_state[loop_id]->pid, &pid_param);

	pr_debug("pm121: %s Fan control loop initialized.\n"
		 "       itarged=%d.%03d, min=%d RPM, max=%d RPM\n",
		 loop_names[loop_id], FIX32TOPRINT(pid_param.itarget),
		 pid_param.min, pid_param.max);
	return;

 fail:
	/* note that this is not optimal since another loop may still
	   control the same control */
	printk(KERN_WARNING "pm121: failed to set up %s loop "
	       "setting \"%s\" to max speed.\n",
	       loop_names[loop_id], control ? control->name : "uninitialized value");

	if (control)
		wf_control_set_max(control);
}

static void pm121_sys_fans_tick(int loop_id)
{
	struct pm121_sys_param *param;
	struct pm121_sys_state *st;
	struct wf_sensor *sensor;
	struct wf_control *control;
	s32 temp, new_setpoint;
	int rc;

	param = &(pm121_sys_all_params[loop_id][pm121_mach_model-2]);
	st = pm121_sys_state[loop_id];
	sensor = *(param->sensor);
	control = controls[param->control_id];

	if (--st->ticks != 0) {
		if (pm121_readjust)
			goto readjust;
		return;
	}
	st->ticks = PM121_SYS_INTERVAL;

	rc = sensor->ops->get_value(sensor, &temp);
	if (rc) {
		printk(KERN_WARNING "windfarm: %s sensor error %d\n",
		       sensor->name, rc);
		pm121_failure_state |= FAILURE_SENSOR;
		return;
	}

	pr_debug("pm121: %s Fan tick ! %s: %d.%03d\n",
		 loop_names[loop_id], sensor->name,
		 FIX32TOPRINT(temp));

	new_setpoint = wf_pid_run(&st->pid, temp);

	/* correction */
	new_setpoint = pm121_correct(new_setpoint,
				     param->control_id,
				     st->pid.param.min);
	/* linked corretion */
	new_setpoint = pm121_connect(param->control_id, new_setpoint);

	if (new_setpoint == st->setpoint)
		return;
	st->setpoint = new_setpoint;
	pr_debug("pm121: %s corrected setpoint: %d RPM\n",
		 control->name, (int)new_setpoint);
 readjust:
	if (control && pm121_failure_state == 0) {
		rc = control->ops->set_value(control, st->setpoint);
		if (rc) {
			printk(KERN_WARNING "windfarm: %s fan error %d\n",
			       control->name, rc);
			pm121_failure_state |= FAILURE_FAN;
		}
	}
}


/* CPU LOOP */
static void pm121_create_cpu_fans(void)
{
	struct wf_cpu_pid_param pid_param;
	const struct smu_sdbp_header *hdr;
	struct smu_sdbp_cpupiddata *piddata;
	struct smu_sdbp_fvt *fvt;
	struct wf_control *fan_cpu;
	s32 tmax, tdelta, maxpow, powadj;

	fan_cpu = controls[FAN_CPU];

	/* First, locate the PID params in SMU SBD */
	hdr = smu_get_sdb_partition(SMU_SDB_CPUPIDDATA_ID, NULL);
	if (hdr == 0) {
		printk(KERN_WARNING "pm121: CPU PID fan config not found.\n");
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
	pm121_cpu_state = kmalloc(sizeof(struct pm121_cpu_state),
				  GFP_KERNEL);
	if (pm121_cpu_state == NULL)
		goto fail;
	pm121_cpu_state->ticks = 1;

	/* Fill PID params */
	pid_param.interval = PM121_CPU_INTERVAL;
	pid_param.history_len = piddata->history_len;
	if (pid_param.history_len > WF_CPU_PID_MAX_HISTORY) {
		printk(KERN_WARNING "pm121: History size overflow on "
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

	pid_param.min = fan_cpu->ops->get_min(fan_cpu);
	pid_param.max = fan_cpu->ops->get_max(fan_cpu);

	wf_cpu_pid_init(&pm121_cpu_state->pid, &pid_param);

	pr_debug("pm121: CPU Fan control initialized.\n");
	pr_debug("       ttarget=%d.%03d, tmax=%d.%03d, min=%d RPM, max=%d RPM,\n",
		 FIX32TOPRINT(pid_param.ttarget), FIX32TOPRINT(pid_param.tmax),
		 pid_param.min, pid_param.max);

	return;

 fail:
	printk(KERN_WARNING "pm121: CPU fan config not found, max fan speed\n");

	if (controls[CPUFREQ])
		wf_control_set_max(controls[CPUFREQ]);
	if (fan_cpu)
		wf_control_set_max(fan_cpu);
}


static void pm121_cpu_fans_tick(struct pm121_cpu_state *st)
{
	s32 new_setpoint, temp, power;
	struct wf_control *fan_cpu = NULL;
	int rc;

	if (--st->ticks != 0) {
		if (pm121_readjust)
			goto readjust;
		return;
	}
	st->ticks = PM121_CPU_INTERVAL;

	fan_cpu = controls[FAN_CPU];

	rc = sensor_cpu_temp->ops->get_value(sensor_cpu_temp, &temp);
	if (rc) {
		printk(KERN_WARNING "pm121: CPU temp sensor error %d\n",
		       rc);
		pm121_failure_state |= FAILURE_SENSOR;
		return;
	}

	rc = sensor_cpu_power->ops->get_value(sensor_cpu_power, &power);
	if (rc) {
		printk(KERN_WARNING "pm121: CPU power sensor error %d\n",
		       rc);
		pm121_failure_state |= FAILURE_SENSOR;
		return;
	}

	pr_debug("pm121: CPU Fans tick ! CPU temp: %d.%03d°C, power: %d.%03d\n",
		 FIX32TOPRINT(temp), FIX32TOPRINT(power));

	if (temp > st->pid.param.tmax)
		pm121_failure_state |= FAILURE_OVERTEMP;

	new_setpoint = wf_cpu_pid_run(&st->pid, power, temp);

	/* correction */
	new_setpoint = pm121_correct(new_setpoint,
				     FAN_CPU,
				     st->pid.param.min);

	/* connected correction */
	new_setpoint = pm121_connect(FAN_CPU, new_setpoint);

	if (st->setpoint == new_setpoint)
		return;
	st->setpoint = new_setpoint;
	pr_debug("pm121: CPU corrected setpoint: %d RPM\n", (int)new_setpoint);

 readjust:
	if (fan_cpu && pm121_failure_state == 0) {
		rc = fan_cpu->ops->set_value(fan_cpu, st->setpoint);
		if (rc) {
			printk(KERN_WARNING "pm121: %s fan error %d\n",
			       fan_cpu->name, rc);
			pm121_failure_state |= FAILURE_FAN;
		}
	}
}

/*
 * ****** Common ******
 *
 */

static void pm121_tick(void)
{
	unsigned int last_failure = pm121_failure_state;
	unsigned int new_failure;
	s32 total_power;
	int i;

	if (!pm121_started) {
		pr_debug("pm121: creating control loops !\n");
		for (i = 0; i < N_LOOPS; i++)
			pm121_create_sys_fans(i);

		pm121_create_cpu_fans();
		pm121_started = true;
	}

	/* skipping ticks */
	if (pm121_skipping && --pm121_skipping)
		return;

	/* compute average power */
	total_power = 0;
	for (i = 0; i < pm121_cpu_state->pid.param.history_len; i++)
		total_power += pm121_cpu_state->pid.powers[i];

	average_power = total_power / pm121_cpu_state->pid.param.history_len;


	pm121_failure_state = 0;
	for (i = 0 ; i < N_LOOPS; i++) {
		if (pm121_sys_state[i])
			pm121_sys_fans_tick(i);
	}

	if (pm121_cpu_state)
		pm121_cpu_fans_tick(pm121_cpu_state);

	pm121_readjust = 0;
	new_failure = pm121_failure_state & ~last_failure;

	/* If entering failure mode, clamp cpufreq and ramp all
	 * fans to full speed.
	 */
	if (pm121_failure_state && !last_failure) {
		for (i = 0; i < N_CONTROLS; i++) {
			if (controls[i])
				wf_control_set_max(controls[i]);
		}
	}

	/* If leaving failure mode, unclamp cpufreq and readjust
	 * all fans on next iteration
	 */
	if (!pm121_failure_state && last_failure) {
		if (controls[CPUFREQ])
			wf_control_set_min(controls[CPUFREQ]);
		pm121_readjust = 1;
	}

	/* Overtemp condition detected, notify and start skipping a couple
	 * ticks to let the temperature go down
	 */
	if (new_failure & FAILURE_OVERTEMP) {
		wf_set_overtemp();
		pm121_skipping = 2;
		pm121_overtemp = true;
	}

	/* We only clear the overtemp condition if overtemp is cleared
	 * _and_ no other failure is present. Since a sensor error will
	 * clear the overtemp condition (can't measure temperature) at
	 * the control loop levels, but we don't want to keep it clear
	 * here in this case
	 */
	if (!pm121_failure_state && pm121_overtemp) {
		wf_clear_overtemp();
		pm121_overtemp = false;
	}
}


static struct wf_control* pm121_register_control(struct wf_control *ct,
						 const char *match,
						 unsigned int id)
{
	if (controls[id] == NULL && !strcmp(ct->name, match)) {
		if (wf_get_control(ct) == 0)
			controls[id] = ct;
	}
	return controls[id];
}

static void pm121_new_control(struct wf_control *ct)
{
	int all = 1;

	if (pm121_all_controls_ok)
		return;

	all = pm121_register_control(ct, "optical-drive-fan", FAN_OD) && all;
	all = pm121_register_control(ct, "hard-drive-fan", FAN_HD) && all;
	all = pm121_register_control(ct, "cpu-fan", FAN_CPU) && all;
	all = pm121_register_control(ct, "cpufreq-clamp", CPUFREQ) && all;

	if (all)
		pm121_all_controls_ok = 1;
}




static struct wf_sensor* pm121_register_sensor(struct wf_sensor *sensor,
					       const char *match,
					       struct wf_sensor **var)
{
	if (*var == NULL && !strcmp(sensor->name, match)) {
		if (wf_get_sensor(sensor) == 0)
			*var = sensor;
	}
	return *var;
}

static void pm121_new_sensor(struct wf_sensor *sr)
{
	int all = 1;

	if (pm121_all_sensors_ok)
		return;

	all = pm121_register_sensor(sr, "cpu-temp",
				    &sensor_cpu_temp) && all;
	all = pm121_register_sensor(sr, "cpu-current",
				    &sensor_cpu_current) && all;
	all = pm121_register_sensor(sr, "cpu-voltage",
				    &sensor_cpu_voltage) && all;
	all = pm121_register_sensor(sr, "cpu-power",
				    &sensor_cpu_power) && all;
	all = pm121_register_sensor(sr, "hard-drive-temp",
				    &sensor_hard_drive_temp) && all;
	all = pm121_register_sensor(sr, "optical-drive-temp",
				    &sensor_optical_drive_temp) && all;
	all = pm121_register_sensor(sr, "incoming-air-temp",
				    &sensor_incoming_air_temp) && all;
	all = pm121_register_sensor(sr, "north-bridge-temp",
				    &sensor_north_bridge_temp) && all;
	all = pm121_register_sensor(sr, "gpu-temp",
				    &sensor_gpu_temp) && all;

	if (all)
		pm121_all_sensors_ok = 1;
}



static int pm121_notify(struct notifier_block *self,
			unsigned long event, void *data)
{
	switch (event) {
	case WF_EVENT_NEW_CONTROL:
		pr_debug("pm121: new control %s detected\n",
			 ((struct wf_control *)data)->name);
		pm121_new_control(data);
		break;
	case WF_EVENT_NEW_SENSOR:
		pr_debug("pm121: new sensor %s detected\n",
			 ((struct wf_sensor *)data)->name);
		pm121_new_sensor(data);
		break;
	case WF_EVENT_TICK:
		if (pm121_all_controls_ok && pm121_all_sensors_ok)
			pm121_tick();
		break;
	}

	return 0;
}

static struct notifier_block pm121_events = {
	.notifier_call	= pm121_notify,
};

static int pm121_init_pm(void)
{
	const struct smu_sdbp_header *hdr;

	hdr = smu_get_sdb_partition(SMU_SDB_SENSORTREE_ID, NULL);
	if (hdr != 0) {
		struct smu_sdbp_sensortree *st =
			(struct smu_sdbp_sensortree *)&hdr[1];
		pm121_mach_model = st->model_id;
	}

	pm121_connection = &pm121_connections[pm121_mach_model - 2];

	printk(KERN_INFO "pm121: Initializing for iMac G5 iSight model ID %d\n",
	       pm121_mach_model);

	return 0;
}


static int pm121_probe(struct platform_device *ddev)
{
	wf_register_client(&pm121_events);

	return 0;
}

static int pm121_remove(struct platform_device *ddev)
{
	wf_unregister_client(&pm121_events);
	return 0;
}

static struct platform_driver pm121_driver = {
	.probe = pm121_probe,
	.remove = pm121_remove,
	.driver = {
		.name = "windfarm",
		.bus = &platform_bus_type,
	},
};


static int __init pm121_init(void)
{
	int rc = -ENODEV;

	if (of_machine_is_compatible("PowerMac12,1"))
		rc = pm121_init_pm();

	if (rc == 0) {
		request_module("windfarm_smu_controls");
		request_module("windfarm_smu_sensors");
		request_module("windfarm_smu_sat");
		request_module("windfarm_lm75_sensor");
		request_module("windfarm_max6690_sensor");
		request_module("windfarm_cpufreq_clamp");
		platform_driver_register(&pm121_driver);
	}

	return rc;
}

static void __exit pm121_exit(void)
{

	platform_driver_unregister(&pm121_driver);
}


module_init(pm121_init);
module_exit(pm121_exit);

MODULE_AUTHOR("Étienne Bersac <bersace@gmail.com>");
MODULE_DESCRIPTION("Thermal control logic for iMac G5 (iSight)");
MODULE_LICENSE("GPL");

