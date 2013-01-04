/*
 * Windfarm PowerMac thermal control.
 * Control loops for RackMack3,1 (Xserve G5)
 *
 * Copyright (C) 2012 Benjamin Herrenschmidt, IBM Corp.
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
#include "windfarm_mpu.h"

#define VERSION "1.0"

#undef DEBUG
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

/* We currently only handle 2 chips */
#define NR_CHIPS	2
#define NR_CPU_FANS	3 * NR_CHIPS

/* Controls and sensors */
static struct wf_sensor *sens_cpu_temp[NR_CHIPS];
static struct wf_sensor *sens_cpu_volts[NR_CHIPS];
static struct wf_sensor *sens_cpu_amps[NR_CHIPS];
static struct wf_sensor *backside_temp;
static struct wf_sensor *slots_temp;
static struct wf_sensor *dimms_temp;

static struct wf_control *cpu_fans[NR_CHIPS][3];
static struct wf_control *backside_fan;
static struct wf_control *slots_fan;
static struct wf_control *cpufreq_clamp;

/* We keep a temperature history for average calculation of 180s */
#define CPU_TEMP_HIST_SIZE	180

/* PID loop state */
static const struct mpu_data *cpu_mpu_data[NR_CHIPS];
static struct wf_cpu_pid_state cpu_pid[NR_CHIPS];
static u32 cpu_thist[CPU_TEMP_HIST_SIZE];
static int cpu_thist_pt;
static s64 cpu_thist_total;
static s32 cpu_all_tmax = 100 << 16;
static struct wf_pid_state backside_pid;
static int backside_tick;
static struct wf_pid_state slots_pid;
static int slots_tick;
static int slots_speed;
static struct wf_pid_state dimms_pid;
static int dimms_output_clamp;

static int nr_chips;
static bool have_all_controls;
static bool have_all_sensors;
static bool started;

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


static void cpu_max_all_fans(void)
{
	int i;

	/* We max all CPU fans in case of a sensor error. We also do the
	 * cpufreq clamping now, even if it's supposedly done later by the
	 * generic code anyway, we do it earlier here to react faster
	 */
	if (cpufreq_clamp)
		wf_control_set_max(cpufreq_clamp);
	for (i = 0; i < nr_chips; i++) {
		if (cpu_fans[i][0])
			wf_control_set_max(cpu_fans[i][0]);
		if (cpu_fans[i][1])
			wf_control_set_max(cpu_fans[i][1]);
		if (cpu_fans[i][2])
			wf_control_set_max(cpu_fans[i][2]);
	}
}

static int cpu_check_overtemp(s32 temp)
{
	int new_state = 0;
	s32 t_avg, t_old;
	static bool first = true;

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

	/*
	 * The first time around, initialize the array with the first
	 * temperature reading
	 */
	if (first) {
		int i;

		cpu_thist_total = 0;
		for (i = 0; i < CPU_TEMP_HIST_SIZE; i++) {
			cpu_thist[i] = temp;
			cpu_thist_total += temp;
		}
		first = false;
	}

	/*
	 * We calculate a history of max temperatures and use that for the
	 * overtemp management
	 */
	t_old = cpu_thist[cpu_thist_pt];
	cpu_thist[cpu_thist_pt] = temp;
	cpu_thist_pt = (cpu_thist_pt + 1) % CPU_TEMP_HIST_SIZE;
	cpu_thist_total -= t_old;
	cpu_thist_total += temp;
	t_avg = cpu_thist_total / CPU_TEMP_HIST_SIZE;

	DBG_LOTS("  t_avg = %d.%03d (out: %d.%03d, in: %d.%03d)\n",
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

static int read_one_cpu_vals(int cpu, s32 *temp, s32 *power)
{
	s32 dtemp, volts, amps;
	int rc;

	/* Get diode temperature */
	rc = wf_sensor_get(sens_cpu_temp[cpu], &dtemp);
	if (rc) {
		DBG("  CPU%d: temp reading error !\n", cpu);
		return -EIO;
	}
	DBG_LOTS("  CPU%d: temp   = %d.%03d\n", cpu, FIX32TOPRINT((dtemp)));
	*temp = dtemp;

	/* Get voltage */
	rc = wf_sensor_get(sens_cpu_volts[cpu], &volts);
	if (rc) {
		DBG("  CPU%d, volts reading error !\n", cpu);
		return -EIO;
	}
	DBG_LOTS("  CPU%d: volts  = %d.%03d\n", cpu, FIX32TOPRINT((volts)));

	/* Get current */
	rc = wf_sensor_get(sens_cpu_amps[cpu], &amps);
	if (rc) {
		DBG("  CPU%d, current reading error !\n", cpu);
		return -EIO;
	}
	DBG_LOTS("  CPU%d: amps   = %d.%03d\n", cpu, FIX32TOPRINT((amps)));

	/* Calculate power */

	/* Scale voltage and current raw sensor values according to fixed scales
	 * obtained in Darwin and calculate power from I and V
	 */
	*power = (((u64)volts) * ((u64)amps)) >> 16;

	DBG_LOTS("  CPU%d: power  = %d.%03d\n", cpu, FIX32TOPRINT((*power)));

	return 0;

}

static void cpu_fans_tick(void)
{
	int err, cpu, i;
	s32 speed, temp, power, t_max = 0;

	DBG_LOTS("* cpu fans_tick_split()\n");

	for (cpu = 0; cpu < nr_chips; ++cpu) {
		struct wf_cpu_pid_state *sp = &cpu_pid[cpu];

		/* Read current speed */
		wf_control_get(cpu_fans[cpu][0], &sp->target);

		err = read_one_cpu_vals(cpu, &temp, &power);
		if (err) {
			failure_state |= FAILURE_SENSOR;
			cpu_max_all_fans();
			return;
		}

		/* Keep track of highest temp */
		t_max = max(t_max, temp);

		/* Handle possible overtemps */
		if (cpu_check_overtemp(t_max))
			return;

		/* Run PID */
		wf_cpu_pid_run(sp, power, temp);

		DBG_LOTS("  CPU%d: target = %d RPM\n", cpu, sp->target);

		/* Apply DIMMs clamp */
		speed = max(sp->target, dimms_output_clamp);

		/* Apply result to all cpu fans */
		for (i = 0; i < 3; i++) {
			err = wf_control_set(cpu_fans[cpu][i], speed);
			if (err) {
				pr_warning("wf_rm31: Fan %s reports error %d\n",
					   cpu_fans[cpu][i]->name, err);
				failure_state |= FAILURE_FAN;
			}
		}
	}
}

/* Implementation... */
static int cpu_setup_pid(int cpu)
{
	struct wf_cpu_pid_param pid;
	const struct mpu_data *mpu = cpu_mpu_data[cpu];
	s32 tmax, ttarget, ptarget;
	int fmin, fmax, hsize;

	/* Get PID params from the appropriate MPU EEPROM */
	tmax = mpu->tmax << 16;
	ttarget = mpu->ttarget << 16;
	ptarget = ((s32)(mpu->pmaxh - mpu->padjmax)) << 16;

	DBG("wf_72: CPU%d ttarget = %d.%03d, tmax = %d.%03d\n",
	    cpu, FIX32TOPRINT(ttarget), FIX32TOPRINT(tmax));

	/* We keep a global tmax for overtemp calculations */
	if (tmax < cpu_all_tmax)
		cpu_all_tmax = tmax;

	/* Set PID min/max by using the rear fan min/max */
	fmin = wf_control_get_min(cpu_fans[cpu][0]);
	fmax = wf_control_get_max(cpu_fans[cpu][0]);
	DBG("wf_72: CPU%d max RPM range = [%d..%d]\n", cpu, fmin, fmax);

	/* History size */
	hsize = min_t(int, mpu->tguardband, WF_PID_MAX_HISTORY);
	DBG("wf_72: CPU%d history size = %d\n", cpu, hsize);

	/* Initialize PID loop */
	pid.interval	= 1;	/* seconds */
	pid.history_len = hsize;
	pid.gd		= mpu->pid_gd;
	pid.gp		= mpu->pid_gp;
	pid.gr		= mpu->pid_gr;
	pid.tmax	= tmax;
	pid.ttarget	= ttarget;
	pid.pmaxadj	= ptarget;
	pid.min		= fmin;
	pid.max		= fmax;

	wf_cpu_pid_init(&cpu_pid[cpu], &pid);
	cpu_pid[cpu].target = 4000;
	
	return 0;
}

/* Backside/U3 fan */
static struct wf_pid_param backside_param = {
	.interval	= 1,
	.history_len	= 2,
	.gd		= 0x00500000,
	.gp		= 0x0004cccc,
	.gr		= 0,
	.itarget	= 70 << 16,
	.additive	= 0,
	.min		= 20,
	.max		= 100,
};

/* DIMMs temperature (clamp the backside fan) */
static struct wf_pid_param dimms_param = {
	.interval	= 1,
	.history_len	= 20,
	.gd		= 0,
	.gp		= 0,
	.gr		= 0x06553600,
	.itarget	= 50 << 16,
	.additive	= 0,
	.min		= 4000,
	.max		= 14000,
};

static void backside_fan_tick(void)
{
	s32 temp, dtemp;
	int speed, dspeed, fan_min;
	int err;

	if (!backside_fan || !backside_temp || !dimms_temp || !backside_tick)
		return;
	if (--backside_tick > 0)
		return;
	backside_tick = backside_pid.param.interval;

	DBG_LOTS("* backside fans tick\n");

	/* Update fan speed from actual fans */
	err = wf_control_get(backside_fan, &speed);
	if (!err)
		backside_pid.target = speed;

	err = wf_sensor_get(backside_temp, &temp);
	if (err) {
		printk(KERN_WARNING "windfarm: U3 temp sensor error %d\n",
		       err);
		failure_state |= FAILURE_SENSOR;
		wf_control_set_max(backside_fan);
		return;
	}
	speed = wf_pid_run(&backside_pid, temp);

	DBG_LOTS("backside PID temp=%d.%.3d speed=%d\n",
		 FIX32TOPRINT(temp), speed);

	err = wf_sensor_get(dimms_temp, &dtemp);
	if (err) {
		printk(KERN_WARNING "windfarm: DIMMs temp sensor error %d\n",
		       err);
		failure_state |= FAILURE_SENSOR;
		wf_control_set_max(backside_fan);
		return;
	}
	dspeed = wf_pid_run(&dimms_pid, dtemp);
	dimms_output_clamp = dspeed;

	fan_min = (dspeed * 100) / 14000;
	fan_min = max(fan_min, backside_param.min);
	speed = max(speed, fan_min);

	err = wf_control_set(backside_fan, speed);
	if (err) {
		printk(KERN_WARNING "windfarm: backside fan error %d\n", err);
		failure_state |= FAILURE_FAN;
	}
}

static void backside_setup_pid(void)
{
	/* first time initialize things */
	s32 fmin = wf_control_get_min(backside_fan);
	s32 fmax = wf_control_get_max(backside_fan);
	struct wf_pid_param param;

	param = backside_param;
	param.min = max(param.min, fmin);
	param.max = min(param.max, fmax);
	wf_pid_init(&backside_pid, &param);

	param = dimms_param;
	wf_pid_init(&dimms_pid, &param);

	backside_tick = 1;

	pr_info("wf_rm31: Backside control loop started.\n");
}

/* Slots fan */
static const struct wf_pid_param slots_param = {
	.interval	= 5,
	.history_len	= 2,
	.gd		= 30 << 20,
	.gp		= 5 << 20,
	.gr		= 0,
	.itarget	= 40 << 16,
	.additive	= 1,
	.min		= 300,
	.max		= 4000,
};

static void slots_fan_tick(void)
{
	s32 temp;
	int speed;
	int err;

	if (!slots_fan || !slots_temp || !slots_tick)
		return;
	if (--slots_tick > 0)
		return;
	slots_tick = slots_pid.param.interval;

	DBG_LOTS("* slots fans tick\n");

	err = wf_sensor_get(slots_temp, &temp);
	if (err) {
		pr_warning("wf_rm31: slots temp sensor error %d\n", err);
		failure_state |= FAILURE_SENSOR;
		wf_control_set_max(slots_fan);
		return;
	}
	speed = wf_pid_run(&slots_pid, temp);

	DBG_LOTS("slots PID temp=%d.%.3d speed=%d\n",
		 FIX32TOPRINT(temp), speed);

	slots_speed = speed;
	err = wf_control_set(slots_fan, speed);
	if (err) {
		printk(KERN_WARNING "windfarm: slots bay fan error %d\n", err);
		failure_state |= FAILURE_FAN;
	}
}

static void slots_setup_pid(void)
{
	/* first time initialize things */
	s32 fmin = wf_control_get_min(slots_fan);
	s32 fmax = wf_control_get_max(slots_fan);
	struct wf_pid_param param = slots_param;

	param.min = max(param.min, fmin);
	param.max = min(param.max, fmax);
	wf_pid_init(&slots_pid, &param);
	slots_tick = 1;

	pr_info("wf_rm31: Slots control loop started.\n");
}

static void set_fail_state(void)
{
	cpu_max_all_fans();

	if (backside_fan)
		wf_control_set_max(backside_fan);
	if (slots_fan)
		wf_control_set_max(slots_fan);
}

static void rm31_tick(void)
{
	int i, last_failure;

	if (!started) {
		started = 1;
		printk(KERN_INFO "windfarm: CPUs control loops started.\n");
		for (i = 0; i < nr_chips; ++i) {
			if (cpu_setup_pid(i) < 0) {
				failure_state = FAILURE_PERM;
				set_fail_state();
				break;
			}
		}
		DBG_LOTS("cpu_all_tmax=%d.%03d\n", FIX32TOPRINT(cpu_all_tmax));

		backside_setup_pid();
		slots_setup_pid();

#ifdef HACKED_OVERTEMP
		cpu_all_tmax = 60 << 16;
#endif
	}

	/* Permanent failure, bail out */
	if (failure_state & FAILURE_PERM)
		return;

	/*
	 * Clear all failure bits except low overtemp which will be eventually
	 * cleared by the control loop itself
	 */
	last_failure = failure_state;
	failure_state &= FAILURE_LOW_OVERTEMP;
	backside_fan_tick();
	slots_fan_tick();

	/* We do CPUs last because they can be clamped high by
	 * DIMM temperature
	 */
	cpu_fans_tick();

	DBG_LOTS("  last_failure: 0x%x, failure_state: %x\n",
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

static void rm31_new_control(struct wf_control *ct)
{
	bool all_controls;

	if (!strcmp(ct->name, "cpu-fan-a-0"))
		cpu_fans[0][0] = ct;
	else if (!strcmp(ct->name, "cpu-fan-b-0"))
		cpu_fans[0][1] = ct;
	else if (!strcmp(ct->name, "cpu-fan-c-0"))
		cpu_fans[0][2] = ct;
	else if (!strcmp(ct->name, "cpu-fan-a-1"))
		cpu_fans[1][0] = ct;
	else if (!strcmp(ct->name, "cpu-fan-b-1"))
		cpu_fans[1][1] = ct;
	else if (!strcmp(ct->name, "cpu-fan-c-1"))
		cpu_fans[1][2] = ct;
	else if (!strcmp(ct->name, "backside-fan"))
		backside_fan = ct;
	else if (!strcmp(ct->name, "slots-fan"))
		slots_fan = ct;
	else if (!strcmp(ct->name, "cpufreq-clamp"))
		cpufreq_clamp = ct;

	all_controls =
		cpu_fans[0][0] &&
		cpu_fans[0][1] &&
		cpu_fans[0][2] &&
		backside_fan &&
		slots_fan;
	if (nr_chips > 1)
		all_controls &=
			cpu_fans[1][0] &&
			cpu_fans[1][1] &&
			cpu_fans[1][2];
	have_all_controls = all_controls;
}


static void rm31_new_sensor(struct wf_sensor *sr)
{
	bool all_sensors;

	if (!strcmp(sr->name, "cpu-diode-temp-0"))
		sens_cpu_temp[0] = sr;
	else if (!strcmp(sr->name, "cpu-diode-temp-1"))
		sens_cpu_temp[1] = sr;
	else if (!strcmp(sr->name, "cpu-voltage-0"))
		sens_cpu_volts[0] = sr;
	else if (!strcmp(sr->name, "cpu-voltage-1"))
		sens_cpu_volts[1] = sr;
	else if (!strcmp(sr->name, "cpu-current-0"))
		sens_cpu_amps[0] = sr;
	else if (!strcmp(sr->name, "cpu-current-1"))
		sens_cpu_amps[1] = sr;
	else if (!strcmp(sr->name, "backside-temp"))
		backside_temp = sr;
	else if (!strcmp(sr->name, "slots-temp"))
		slots_temp = sr;
	else if (!strcmp(sr->name, "dimms-temp"))
		dimms_temp = sr;

	all_sensors =
		sens_cpu_temp[0] &&
		sens_cpu_volts[0] &&
		sens_cpu_amps[0] &&
		backside_temp &&
		slots_temp &&
		dimms_temp;
	if (nr_chips > 1)
		all_sensors &=
			sens_cpu_temp[1] &&
			sens_cpu_volts[1] &&
			sens_cpu_amps[1];

	have_all_sensors = all_sensors;
}

static int rm31_wf_notify(struct notifier_block *self,
			  unsigned long event, void *data)
{
	switch (event) {
	case WF_EVENT_NEW_SENSOR:
		rm31_new_sensor(data);
		break;
	case WF_EVENT_NEW_CONTROL:
		rm31_new_control(data);
		break;
	case WF_EVENT_TICK:
		if (have_all_controls && have_all_sensors)
			rm31_tick();
	}
	return 0;
}

static struct notifier_block rm31_events = {
	.notifier_call = rm31_wf_notify,
};

static int wf_rm31_probe(struct platform_device *dev)
{
	wf_register_client(&rm31_events);
	return 0;
}

static int wf_rm31_remove(struct platform_device *dev)
{
	wf_unregister_client(&rm31_events);

	/* should release all sensors and controls */
	return 0;
}

static struct platform_driver wf_rm31_driver = {
	.probe	= wf_rm31_probe,
	.remove	= wf_rm31_remove,
	.driver	= {
		.name = "windfarm",
		.owner	= THIS_MODULE,
	},
};

static int __init wf_rm31_init(void)
{
	struct device_node *cpu;
	int i;

	if (!of_machine_is_compatible("RackMac3,1"))
		return -ENODEV;

	/* Count the number of CPU cores */
	nr_chips = 0;
	for (cpu = NULL; (cpu = of_find_node_by_type(cpu, "cpu")) != NULL; )
		++nr_chips;
	if (nr_chips > NR_CHIPS)
		nr_chips = NR_CHIPS;

	pr_info("windfarm: Initializing for desktop G5 with %d chips\n",
		nr_chips);

	/* Get MPU data for each CPU */
	for (i = 0; i < nr_chips; i++) {
		cpu_mpu_data[i] = wf_get_mpu(i);
		if (!cpu_mpu_data[i]) {
			pr_err("wf_rm31: Failed to find MPU data for CPU %d\n", i);
			return -ENXIO;
		}
	}

#ifdef MODULE
	request_module("windfarm_fcu_controls");
	request_module("windfarm_lm75_sensor");
	request_module("windfarm_lm87_sensor");
	request_module("windfarm_ad7417_sensor");
	request_module("windfarm_max6690_sensor");
	request_module("windfarm_cpufreq_clamp");
#endif /* MODULE */

	platform_driver_register(&wf_rm31_driver);
	return 0;
}

static void __exit wf_rm31_exit(void)
{
	platform_driver_unregister(&wf_rm31_driver);
}

module_init(wf_rm31_init);
module_exit(wf_rm31_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Thermal control for Xserve G5");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:windfarm");
