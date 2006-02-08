/*
 * Windfarm PowerMac thermal control.
 * Control loops for machines with SMU and PPC970MP processors.
 *
 * Copyright (C) 2005 Paul Mackerras, IBM Corp. <paulus@samba.org>
 * Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *
 * Use and redistribute under the terms of the GNU GPL v2.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <asm/prom.h>
#include <asm/smu.h>

#include "windfarm.h"
#include "windfarm_pid.h"

#define VERSION "0.2"

#define DEBUG
#undef LOTSA_DEBUG

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif

#ifdef LOTSA_DEBUG
#define DBG_LOTS(args...)	printk(args)
#else
#define DBG_LOTS(args...)	do { } while(0)
#endif

/* define this to force CPU overtemp to 60 degree, useful for testing
 * the overtemp code
 */
#undef HACKED_OVERTEMP

/* We currently only handle 2 chips, 4 cores... */
#define NR_CHIPS	2
#define NR_CORES	4
#define NR_CPU_FANS	3 * NR_CHIPS

/* Controls and sensors */
static struct wf_sensor *sens_cpu_temp[NR_CORES];
static struct wf_sensor *sens_cpu_power[NR_CORES];
static struct wf_sensor *hd_temp;
static struct wf_sensor *slots_power;
static struct wf_sensor *u4_temp;

static struct wf_control *cpu_fans[NR_CPU_FANS];
static char *cpu_fan_names[NR_CPU_FANS] = {
	"cpu-rear-fan-0",
	"cpu-rear-fan-1",
	"cpu-front-fan-0",
	"cpu-front-fan-1",
	"cpu-pump-0",
	"cpu-pump-1",
};
static struct wf_control *cpufreq_clamp;

/* Second pump isn't required (and isn't actually present) */
#define CPU_FANS_REQD		(NR_CPU_FANS - 2)
#define FIRST_PUMP		4
#define LAST_PUMP		5

/* We keep a temperature history for average calculation of 180s */
#define CPU_TEMP_HIST_SIZE	180

/* Scale factor for fan speed, *100 */
static int cpu_fan_scale[NR_CPU_FANS] = {
	100,
	100,
	97,		/* inlet fans run at 97% of exhaust fan */
	97,
	100,		/* updated later */
	100,		/* updated later */
};

static struct wf_control *backside_fan;
static struct wf_control *slots_fan;
static struct wf_control *drive_bay_fan;

/* PID loop state */
static struct wf_cpu_pid_state cpu_pid[NR_CORES];
static u32 cpu_thist[CPU_TEMP_HIST_SIZE];
static int cpu_thist_pt;
static s64 cpu_thist_total;
static s32 cpu_all_tmax = 100 << 16;
static int cpu_last_target;
static struct wf_pid_state backside_pid;
static int backside_tick;
static struct wf_pid_state slots_pid;
static int slots_started;
static struct wf_pid_state drive_bay_pid;
static int drive_bay_tick;

static int nr_cores;
static int have_all_controls;
static int have_all_sensors;
static int started;

static int failure_state;
#define FAILURE_SENSOR		1
#define FAILURE_FAN		2
#define FAILURE_PERM		4
#define FAILURE_LOW_OVERTEMP	8
#define FAILURE_HIGH_OVERTEMP	16

/* Overtemp values */
#define LOW_OVER_AVERAGE	0
#define LOW_OVER_IMMEDIATE	(10 << 16)
#define LOW_OVER_CLEAR		((-10) << 16)
#define HIGH_OVER_IMMEDIATE	(14 << 16)
#define HIGH_OVER_AVERAGE	(10 << 16)
#define HIGH_OVER_IMMEDIATE	(14 << 16)


/* Implementation... */
static int create_cpu_loop(int cpu)
{
	int chip = cpu / 2;
	int core = cpu & 1;
	struct smu_sdbp_header *hdr;
	struct smu_sdbp_cpupiddata *piddata;
	struct wf_cpu_pid_param pid;
	struct wf_control *main_fan = cpu_fans[0];
	s32 tmax;
	int fmin;

	/* Get PID params from the appropriate SAT */
	hdr = smu_sat_get_sdb_partition(chip, 0xC8 + core, NULL);
	if (hdr == NULL) {
		printk(KERN_WARNING"windfarm: can't get CPU PID fan config\n");
		return -EINVAL;
	}
	piddata = (struct smu_sdbp_cpupiddata *)&hdr[1];

	/* Get FVT params to get Tmax; if not found, assume default */
	hdr = smu_sat_get_sdb_partition(chip, 0xC4 + core, NULL);
	if (hdr) {
		struct smu_sdbp_fvt *fvt = (struct smu_sdbp_fvt *)&hdr[1];
		tmax = fvt->maxtemp << 16;
	} else
		tmax = 95 << 16;	/* default to 95 degrees C */

	/* We keep a global tmax for overtemp calculations */
	if (tmax < cpu_all_tmax)
		cpu_all_tmax = tmax;

	/*
	 * Darwin has a minimum fan speed of 1000 rpm for the 4-way and
	 * 515 for the 2-way.  That appears to be overkill, so for now,
	 * impose a minimum of 750 or 515.
	 */
	fmin = (nr_cores > 2) ? 750 : 515;

	/* Initialize PID loop */
	pid.interval = 1;	/* seconds */
	pid.history_len = piddata->history_len;
	pid.gd = piddata->gd;
	pid.gp = piddata->gp;
	pid.gr = piddata->gr / piddata->history_len;
	pid.pmaxadj = (piddata->max_power << 16) - (piddata->power_adj << 8);
	pid.ttarget = tmax - (piddata->target_temp_delta << 16);
	pid.tmax = tmax;
	pid.min = main_fan->ops->get_min(main_fan);
	pid.max = main_fan->ops->get_max(main_fan);
	if (pid.min < fmin)
		pid.min = fmin;

	wf_cpu_pid_init(&cpu_pid[cpu], &pid);
	return 0;
}

static void cpu_max_all_fans(void)
{
	int i;

	/* We max all CPU fans in case of a sensor error. We also do the
	 * cpufreq clamping now, even if it's supposedly done later by the
	 * generic code anyway, we do it earlier here to react faster
	 */
	if (cpufreq_clamp)
		wf_control_set_max(cpufreq_clamp);
	for (i = 0; i < NR_CPU_FANS; ++i)
		if (cpu_fans[i])
			wf_control_set_max(cpu_fans[i]);
}

static int cpu_check_overtemp(s32 temp)
{
	int new_state = 0;
	s32 t_avg, t_old;

	/* First check for immediate overtemps */
	if (temp >= (cpu_all_tmax + LOW_OVER_IMMEDIATE)) {
		new_state |= FAILURE_LOW_OVERTEMP;
		if ((failure_state & FAILURE_LOW_OVERTEMP) == 0)
			printk(KERN_ERR "windfarm: Overtemp due to immediate CPU"
			       " temperature !\n");
	}
	if (temp >= (cpu_all_tmax + HIGH_OVER_IMMEDIATE)) {
		new_state |= FAILURE_HIGH_OVERTEMP;
		if ((failure_state & FAILURE_HIGH_OVERTEMP) == 0)
			printk(KERN_ERR "windfarm: Critical overtemp due to"
			       " immediate CPU temperature !\n");
	}

	/* We calculate a history of max temperatures and use that for the
	 * overtemp management
	 */
	t_old = cpu_thist[cpu_thist_pt];
	cpu_thist[cpu_thist_pt] = temp;
	cpu_thist_pt = (cpu_thist_pt + 1) % CPU_TEMP_HIST_SIZE;
	cpu_thist_total -= t_old;
	cpu_thist_total += temp;
	t_avg = cpu_thist_total / CPU_TEMP_HIST_SIZE;

	DBG_LOTS("t_avg = %d.%03d (out: %d.%03d, in: %d.%03d)\n",
		 FIX32TOPRINT(t_avg), FIX32TOPRINT(t_old), FIX32TOPRINT(temp));

	/* Now check for average overtemps */
	if (t_avg >= (cpu_all_tmax + LOW_OVER_AVERAGE)) {
		new_state |= FAILURE_LOW_OVERTEMP;
		if ((failure_state & FAILURE_LOW_OVERTEMP) == 0)
			printk(KERN_ERR "windfarm: Overtemp due to average CPU"
			       " temperature !\n");
	}
	if (t_avg >= (cpu_all_tmax + HIGH_OVER_AVERAGE)) {
		new_state |= FAILURE_HIGH_OVERTEMP;
		if ((failure_state & FAILURE_HIGH_OVERTEMP) == 0)
			printk(KERN_ERR "windfarm: Critical overtemp due to"
			       " average CPU temperature !\n");
	}

	/* Now handle overtemp conditions. We don't currently use the windfarm
	 * overtemp handling core as it's not fully suited to the needs of those
	 * new machine. This will be fixed later.
	 */
	if (new_state) {
		/* High overtemp -> immediate shutdown */
		if (new_state & FAILURE_HIGH_OVERTEMP)
			machine_power_off();
		if ((failure_state & new_state) != new_state)
			cpu_max_all_fans();
		failure_state |= new_state;
	} else if ((failure_state & FAILURE_LOW_OVERTEMP) &&
		   (temp < (cpu_all_tmax + LOW_OVER_CLEAR))) {
		printk(KERN_ERR "windfarm: Overtemp condition cleared !\n");
		failure_state &= ~FAILURE_LOW_OVERTEMP;
	}

	return failure_state & (FAILURE_LOW_OVERTEMP | FAILURE_HIGH_OVERTEMP);
}

static void cpu_fans_tick(void)
{
	int err, cpu;
	s32 greatest_delta = 0;
	s32 temp, power, t_max = 0;
	int i, t, target = 0;
	struct wf_sensor *sr;
	struct wf_control *ct;
	struct wf_cpu_pid_state *sp;

	DBG_LOTS(KERN_DEBUG);
	for (cpu = 0; cpu < nr_cores; ++cpu) {
		/* Get CPU core temperature */
		sr = sens_cpu_temp[cpu];
		err = sr->ops->get_value(sr, &temp);
		if (err) {
			DBG("\n");
			printk(KERN_WARNING "windfarm: CPU %d temperature "
			       "sensor error %d\n", cpu, err);
			failure_state |= FAILURE_SENSOR;
			cpu_max_all_fans();
			return;
		}

		/* Keep track of highest temp */
		t_max = max(t_max, temp);

		/* Get CPU power */
		sr = sens_cpu_power[cpu];
		err = sr->ops->get_value(sr, &power);
		if (err) {
			DBG("\n");
			printk(KERN_WARNING "windfarm: CPU %d power "
			       "sensor error %d\n", cpu, err);
			failure_state |= FAILURE_SENSOR;
			cpu_max_all_fans();
			return;
		}

		/* Run PID */
		sp = &cpu_pid[cpu];
		t = wf_cpu_pid_run(sp, power, temp);

		if (cpu == 0 || sp->last_delta > greatest_delta) {
			greatest_delta = sp->last_delta;
			target = t;
		}
		DBG_LOTS("[%d] P=%d.%.3d T=%d.%.3d ",
		    cpu, FIX32TOPRINT(power), FIX32TOPRINT(temp));
	}
	DBG_LOTS("fans = %d, t_max = %d.%03d\n", target, FIX32TOPRINT(t_max));

	/* Darwin limits decrease to 20 per iteration */
	if (target < (cpu_last_target - 20))
		target = cpu_last_target - 20;
	cpu_last_target = target;
	for (cpu = 0; cpu < nr_cores; ++cpu)
		cpu_pid[cpu].target = target;

	/* Handle possible overtemps */
	if (cpu_check_overtemp(t_max))
		return;

	/* Set fans */
	for (i = 0; i < NR_CPU_FANS; ++i) {
		ct = cpu_fans[i];
		if (ct == NULL)
			continue;
		err = ct->ops->set_value(ct, target * cpu_fan_scale[i] / 100);
		if (err) {
			printk(KERN_WARNING "windfarm: fan %s reports "
			       "error %d\n", ct->name, err);
			failure_state |= FAILURE_FAN;
			break;
		}
	}
}

/* Backside/U4 fan */
static struct wf_pid_param backside_param = {
	.interval	= 5,
	.history_len	= 2,
	.gd		= 48 << 20,
	.gp		= 5 << 20,
	.gr		= 0,
	.itarget	= 64 << 16,
	.additive	= 1,
};

static void backside_fan_tick(void)
{
	s32 temp;
	int speed;
	int err;

	if (!backside_fan || !u4_temp)
		return;
	if (!backside_tick) {
		/* first time; initialize things */
		backside_param.min = backside_fan->ops->get_min(backside_fan);
		backside_param.max = backside_fan->ops->get_max(backside_fan);
		wf_pid_init(&backside_pid, &backside_param);
		backside_tick = 1;
	}
	if (--backside_tick > 0)
		return;
	backside_tick = backside_pid.param.interval;

	err = u4_temp->ops->get_value(u4_temp, &temp);
	if (err) {
		printk(KERN_WARNING "windfarm: U4 temp sensor error %d\n",
		       err);
		failure_state |= FAILURE_SENSOR;
		wf_control_set_max(backside_fan);
		return;
	}
	speed = wf_pid_run(&backside_pid, temp);
	DBG_LOTS("backside PID temp=%d.%.3d speed=%d\n",
		 FIX32TOPRINT(temp), speed);

	err = backside_fan->ops->set_value(backside_fan, speed);
	if (err) {
		printk(KERN_WARNING "windfarm: backside fan error %d\n", err);
		failure_state |= FAILURE_FAN;
	}
}

/* Drive bay fan */
static struct wf_pid_param drive_bay_prm = {
	.interval	= 5,
	.history_len	= 2,
	.gd		= 30 << 20,
	.gp		= 5 << 20,
	.gr		= 0,
	.itarget	= 40 << 16,
	.additive	= 1,
};

static void drive_bay_fan_tick(void)
{
	s32 temp;
	int speed;
	int err;

	if (!drive_bay_fan || !hd_temp)
		return;
	if (!drive_bay_tick) {
		/* first time; initialize things */
		drive_bay_prm.min = drive_bay_fan->ops->get_min(drive_bay_fan);
		drive_bay_prm.max = drive_bay_fan->ops->get_max(drive_bay_fan);
		wf_pid_init(&drive_bay_pid, &drive_bay_prm);
		drive_bay_tick = 1;
	}
	if (--drive_bay_tick > 0)
		return;
	drive_bay_tick = drive_bay_pid.param.interval;

	err = hd_temp->ops->get_value(hd_temp, &temp);
	if (err) {
		printk(KERN_WARNING "windfarm: drive bay temp sensor "
		       "error %d\n", err);
		failure_state |= FAILURE_SENSOR;
		wf_control_set_max(drive_bay_fan);
		return;
	}
	speed = wf_pid_run(&drive_bay_pid, temp);
	DBG_LOTS("drive_bay PID temp=%d.%.3d speed=%d\n",
		 FIX32TOPRINT(temp), speed);

	err = drive_bay_fan->ops->set_value(drive_bay_fan, speed);
	if (err) {
		printk(KERN_WARNING "windfarm: drive bay fan error %d\n", err);
		failure_state |= FAILURE_FAN;
	}
}

/* PCI slots area fan */
/* This makes the fan speed proportional to the power consumed */
static struct wf_pid_param slots_param = {
	.interval	= 1,
	.history_len	= 2,
	.gd		= 0,
	.gp		= 0,
	.gr		= 0x1277952,
	.itarget	= 0,
	.min		= 1560,
	.max		= 3510,
};

static void slots_fan_tick(void)
{
	s32 power;
	int speed;
	int err;

	if (!slots_fan || !slots_power)
		return;
	if (!slots_started) {
		/* first time; initialize things */
		wf_pid_init(&slots_pid, &slots_param);
		slots_started = 1;
	}

	err = slots_power->ops->get_value(slots_power, &power);
	if (err) {
		printk(KERN_WARNING "windfarm: slots power sensor error %d\n",
		       err);
		failure_state |= FAILURE_SENSOR;
		wf_control_set_max(slots_fan);
		return;
	}
	speed = wf_pid_run(&slots_pid, power);
	DBG_LOTS("slots PID power=%d.%.3d speed=%d\n",
		 FIX32TOPRINT(power), speed);

	err = slots_fan->ops->set_value(slots_fan, speed);
	if (err) {
		printk(KERN_WARNING "windfarm: slots fan error %d\n", err);
		failure_state |= FAILURE_FAN;
	}
}

static void set_fail_state(void)
{
	int i;

	if (cpufreq_clamp)
		wf_control_set_max(cpufreq_clamp);
	for (i = 0; i < NR_CPU_FANS; ++i)
		if (cpu_fans[i])
			wf_control_set_max(cpu_fans[i]);
	if (backside_fan)
		wf_control_set_max(backside_fan);
	if (slots_fan)
		wf_control_set_max(slots_fan);
	if (drive_bay_fan)
		wf_control_set_max(drive_bay_fan);
}

static void pm112_tick(void)
{
	int i, last_failure;

	if (!started) {
		started = 1;
		for (i = 0; i < nr_cores; ++i) {
			if (create_cpu_loop(i) < 0) {
				failure_state = FAILURE_PERM;
				set_fail_state();
				break;
			}
		}
		DBG_LOTS("cpu_all_tmax=%d.%03d\n", FIX32TOPRINT(cpu_all_tmax));

#ifdef HACKED_OVERTEMP
		cpu_all_tmax = 60 << 16;
#endif
	}

	/* Permanent failure, bail out */
	if (failure_state & FAILURE_PERM)
		return;
	/* Clear all failure bits except low overtemp which will be eventually
	 * cleared by the control loop itself
	 */
	last_failure = failure_state;
	failure_state &= FAILURE_LOW_OVERTEMP;
	cpu_fans_tick();
	backside_fan_tick();
	slots_fan_tick();
	drive_bay_fan_tick();

	DBG_LOTS("last_failure: 0x%x, failure_state: %x\n",
		 last_failure, failure_state);

	/* Check for failures. Any failure causes cpufreq clamping */
	if (failure_state && last_failure == 0 && cpufreq_clamp)
		wf_control_set_max(cpufreq_clamp);
	if (failure_state == 0 && last_failure && cpufreq_clamp)
		wf_control_set_min(cpufreq_clamp);

	/* That's it for now, we might want to deal with other failures
	 * differently in the future though
	 */
}

static void pm112_new_control(struct wf_control *ct)
{
	int i, max_exhaust;

	if (cpufreq_clamp == NULL && !strcmp(ct->name, "cpufreq-clamp")) {
		if (wf_get_control(ct) == 0)
			cpufreq_clamp = ct;
	}

	for (i = 0; i < NR_CPU_FANS; ++i) {
		if (!strcmp(ct->name, cpu_fan_names[i])) {
			if (cpu_fans[i] == NULL && wf_get_control(ct) == 0)
				cpu_fans[i] = ct;
			break;
		}
	}
	if (i >= NR_CPU_FANS) {
		/* not a CPU fan, try the others */
		if (!strcmp(ct->name, "backside-fan")) {
			if (backside_fan == NULL && wf_get_control(ct) == 0)
				backside_fan = ct;
		} else if (!strcmp(ct->name, "slots-fan")) {
			if (slots_fan == NULL && wf_get_control(ct) == 0)
				slots_fan = ct;
		} else if (!strcmp(ct->name, "drive-bay-fan")) {
			if (drive_bay_fan == NULL && wf_get_control(ct) == 0)
				drive_bay_fan = ct;
		}
		return;
	}

	for (i = 0; i < CPU_FANS_REQD; ++i)
		if (cpu_fans[i] == NULL)
			return;

	/* work out pump scaling factors */
	max_exhaust = cpu_fans[0]->ops->get_max(cpu_fans[0]);
	for (i = FIRST_PUMP; i <= LAST_PUMP; ++i)
		if ((ct = cpu_fans[i]) != NULL)
			cpu_fan_scale[i] =
				ct->ops->get_max(ct) * 100 / max_exhaust;

	have_all_controls = 1;
}

static void pm112_new_sensor(struct wf_sensor *sr)
{
	unsigned int i;

	if (have_all_sensors)
		return;
	if (!strncmp(sr->name, "cpu-temp-", 9)) {
		i = sr->name[9] - '0';
		if (sr->name[10] == 0 && i < NR_CORES &&
		    sens_cpu_temp[i] == NULL && wf_get_sensor(sr) == 0)
			sens_cpu_temp[i] = sr;

	} else if (!strncmp(sr->name, "cpu-power-", 10)) {
		i = sr->name[10] - '0';
		if (sr->name[11] == 0 && i < NR_CORES &&
		    sens_cpu_power[i] == NULL && wf_get_sensor(sr) == 0)
			sens_cpu_power[i] = sr;
	} else if (!strcmp(sr->name, "hd-temp")) {
		if (hd_temp == NULL && wf_get_sensor(sr) == 0)
			hd_temp = sr;
	} else if (!strcmp(sr->name, "slots-power")) {
		if (slots_power == NULL && wf_get_sensor(sr) == 0)
			slots_power = sr;
	} else if (!strcmp(sr->name, "u4-temp")) {
		if (u4_temp == NULL && wf_get_sensor(sr) == 0)
			u4_temp = sr;
	} else
		return;

	/* check if we have all the sensors we need */
	for (i = 0; i < nr_cores; ++i)
		if (sens_cpu_temp[i] == NULL || sens_cpu_power[i] == NULL)
			return;

	have_all_sensors = 1;
}

static int pm112_wf_notify(struct notifier_block *self,
			   unsigned long event, void *data)
{
	switch (event) {
	case WF_EVENT_NEW_SENSOR:
		pm112_new_sensor(data);
		break;
	case WF_EVENT_NEW_CONTROL:
		pm112_new_control(data);
		break;
	case WF_EVENT_TICK:
		if (have_all_controls && have_all_sensors)
			pm112_tick();
	}
	return 0;
}

static struct notifier_block pm112_events = {
	.notifier_call = pm112_wf_notify,
};

static int wf_pm112_probe(struct device *dev)
{
	wf_register_client(&pm112_events);
	return 0;
}

static int wf_pm112_remove(struct device *dev)
{
	wf_unregister_client(&pm112_events);
	/* should release all sensors and controls */
	return 0;
}

static struct device_driver wf_pm112_driver = {
	.name = "windfarm",
	.bus = &platform_bus_type,
	.probe = wf_pm112_probe,
	.remove = wf_pm112_remove,
};

static int __init wf_pm112_init(void)
{
	struct device_node *cpu;

	if (!machine_is_compatible("PowerMac11,2"))
		return -ENODEV;

	/* Count the number of CPU cores */
	nr_cores = 0;
	for (cpu = NULL; (cpu = of_find_node_by_type(cpu, "cpu")) != NULL; )
		++nr_cores;

	printk(KERN_INFO "windfarm: initializing for dual-core desktop G5\n");
	driver_register(&wf_pm112_driver);
	return 0;
}

static void __exit wf_pm112_exit(void)
{
	driver_unregister(&wf_pm112_driver);
}

module_init(wf_pm112_init);
module_exit(wf_pm112_exit);

MODULE_AUTHOR("Paul Mackerras <paulus@samba.org>");
MODULE_DESCRIPTION("Thermal control for PowerMac11,2");
MODULE_LICENSE("GPL");
