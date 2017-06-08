/*
 * Windfarm PowerMac thermal control.
 * Control loops for PowerMac7,2 and 7,3
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
static struct wf_sensor *drives_temp;

static struct wf_control *cpu_front_fans[NR_CHIPS];
static struct wf_control *cpu_rear_fans[NR_CHIPS];
static struct wf_control *cpu_pumps[NR_CHIPS];
static struct wf_control *backside_fan;
static struct wf_control *drives_fan;
static struct wf_control *slots_fan;
static struct wf_control *cpufreq_clamp;

/* We keep a temperature history for average calculation of 180s */
#define CPU_TEMP_HIST_SIZE	180

/* Fixed speed for slot fan */
#define	SLOTS_FAN_DEFAULT_PWM	40

/* Scale value for CPU intake fans */
#define CPU_INTAKE_SCALE	0x0000f852

/* PID loop state */
static const struct mpu_data *cpu_mpu_data[NR_CHIPS];
static struct wf_cpu_pid_state cpu_pid[NR_CHIPS];
static bool cpu_pid_combined;
static u32 cpu_thist[CPU_TEMP_HIST_SIZE];
static int cpu_thist_pt;
static s64 cpu_thist_total;
static s32 cpu_all_tmax = 100 << 16;
static struct wf_pid_state backside_pid;
static int backside_tick;
static struct wf_pid_state drives_pid;
static int drives_tick;

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
		if (cpu_front_fans[i])
			wf_control_set_max(cpu_front_fans[i]);
		if (cpu_rear_fans[i])
			wf_control_set_max(cpu_rear_fans[i]);
		if (cpu_pumps[i])
			wf_control_set_max(cpu_pumps[i]);
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

static void cpu_fans_tick_split(void)
{
	int err, cpu;
	s32 intake, temp, power, t_max = 0;

	DBG_LOTS("* cpu fans_tick_split()\n");

	for (cpu = 0; cpu < nr_chips; ++cpu) {
		struct wf_cpu_pid_state *sp = &cpu_pid[cpu];

		/* Read current speed */
		wf_control_get(cpu_rear_fans[cpu], &sp->target);

		DBG_LOTS("  CPU%d: cur_target = %d RPM\n", cpu, sp->target);

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

		/* Apply result directly to exhaust fan */
		err = wf_control_set(cpu_rear_fans[cpu], sp->target);
		if (err) {
			pr_warning("wf_pm72: Fan %s reports error %d\n",
			       cpu_rear_fans[cpu]->name, err);
			failure_state |= FAILURE_FAN;
			break;
		}

		/* Scale result for intake fan */
		intake = (sp->target * CPU_INTAKE_SCALE) >> 16;
		DBG_LOTS("  CPU%d: intake = %d RPM\n", cpu, intake);
		err = wf_control_set(cpu_front_fans[cpu], intake);
		if (err) {
			pr_warning("wf_pm72: Fan %s reports error %d\n",
			       cpu_front_fans[cpu]->name, err);
			failure_state |= FAILURE_FAN;
			break;
		}
	}
}

static void cpu_fans_tick_combined(void)
{
	s32 temp0, power0, temp1, power1, t_max = 0;
	s32 temp, power, intake, pump;
	struct wf_control *pump0, *pump1;
	struct wf_cpu_pid_state *sp = &cpu_pid[0];
	int err, cpu;

	DBG_LOTS("* cpu fans_tick_combined()\n");

	/* Read current speed from cpu 0 */
	wf_control_get(cpu_rear_fans[0], &sp->target);

	DBG_LOTS("  CPUs: cur_target = %d RPM\n", sp->target);

	/* Read values for both CPUs */
	err = read_one_cpu_vals(0, &temp0, &power0);
	if (err) {
		failure_state |= FAILURE_SENSOR;
		cpu_max_all_fans();
		return;
	}
	err = read_one_cpu_vals(1, &temp1, &power1);
	if (err) {
		failure_state |= FAILURE_SENSOR;
		cpu_max_all_fans();
		return;
	}

	/* Keep track of highest temp */
	t_max = max(t_max, max(temp0, temp1));

	/* Handle possible overtemps */
	if (cpu_check_overtemp(t_max))
		return;

	/* Use the max temp & power of both */
	temp = max(temp0, temp1);
	power = max(power0, power1);

	/* Run PID */
	wf_cpu_pid_run(sp, power, temp);

	/* Scale result for intake fan */
	intake = (sp->target * CPU_INTAKE_SCALE) >> 16;

	/* Same deal with pump speed */
	pump0 = cpu_pumps[0];
	pump1 = cpu_pumps[1];
	if (!pump0) {
		pump0 = pump1;
		pump1 = NULL;
	}
	pump = (sp->target * wf_control_get_max(pump0)) /
		cpu_mpu_data[0]->rmaxn_exhaust_fan;

	DBG_LOTS("  CPUs: target = %d RPM\n", sp->target);
	DBG_LOTS("  CPUs: intake = %d RPM\n", intake);
	DBG_LOTS("  CPUs: pump   = %d RPM\n", pump);

	for (cpu = 0; cpu < nr_chips; cpu++) {
		err = wf_control_set(cpu_rear_fans[cpu], sp->target);
		if (err) {
			pr_warning("wf_pm72: Fan %s reports error %d\n",
				   cpu_rear_fans[cpu]->name, err);
			failure_state |= FAILURE_FAN;
		}
		err = wf_control_set(cpu_front_fans[cpu], intake);
		if (err) {
			pr_warning("wf_pm72: Fan %s reports error %d\n",
				   cpu_front_fans[cpu]->name, err);
			failure_state |= FAILURE_FAN;
		}
		err = 0;
		if (cpu_pumps[cpu])
			err = wf_control_set(cpu_pumps[cpu], pump);
		if (err) {
			pr_warning("wf_pm72: Pump %s reports error %d\n",
				   cpu_pumps[cpu]->name, err);
			failure_state |= FAILURE_FAN;
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
	fmin = wf_control_get_min(cpu_rear_fans[cpu]);
	fmax = wf_control_get_max(cpu_rear_fans[cpu]);
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
	cpu_pid[cpu].target = 1000;

	return 0;
}

/* Backside/U3 fan */
static struct wf_pid_param backside_u3_param = {
	.interval	= 5,
	.history_len	= 2,
	.gd		= 40 << 20,
	.gp		= 5 << 20,
	.gr		= 0,
	.itarget	= 65 << 16,
	.additive	= 1,
	.min		= 20,
	.max		= 100,
};

static struct wf_pid_param backside_u3h_param = {
	.interval	= 5,
	.history_len	= 2,
	.gd		= 20 << 20,
	.gp		= 5 << 20,
	.gr		= 0,
	.itarget	= 75 << 16,
	.additive	= 1,
	.min		= 20,
	.max		= 100,
};

static void backside_fan_tick(void)
{
	s32 temp;
	int speed;
	int err;

	if (!backside_fan || !backside_temp || !backside_tick)
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
		printk(KERN_WARNING "windfarm: U4 temp sensor error %d\n",
		       err);
		failure_state |= FAILURE_SENSOR;
		wf_control_set_max(backside_fan);
		return;
	}
	speed = wf_pid_run(&backside_pid, temp);

	DBG_LOTS("backside PID temp=%d.%.3d speed=%d\n",
		 FIX32TOPRINT(temp), speed);

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
	struct device_node *u3;
	int u3h = 1; /* conservative by default */

	u3 = of_find_node_by_path("/u3@0,f8000000");
	if (u3 != NULL) {
		const u32 *vers = of_get_property(u3, "device-rev", NULL);
		if (vers)
			if (((*vers) & 0x3f) < 0x34)
				u3h = 0;
		of_node_put(u3);
	}

	param = u3h ? backside_u3h_param : backside_u3_param;

	param.min = max(param.min, fmin);
	param.max = min(param.max, fmax);
	wf_pid_init(&backside_pid, &param);
	backside_tick = 1;

	pr_info("wf_pm72: Backside control loop started.\n");
}

/* Drive bay fan */
static const struct wf_pid_param drives_param = {
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

static void drives_fan_tick(void)
{
	s32 temp;
	int speed;
	int err;

	if (!drives_fan || !drives_temp || !drives_tick)
		return;
	if (--drives_tick > 0)
		return;
	drives_tick = drives_pid.param.interval;

	DBG_LOTS("* drives fans tick\n");

	/* Update fan speed from actual fans */
	err = wf_control_get(drives_fan, &speed);
	if (!err)
		drives_pid.target = speed;

	err = wf_sensor_get(drives_temp, &temp);
	if (err) {
		pr_warning("wf_pm72: drive bay temp sensor error %d\n", err);
		failure_state |= FAILURE_SENSOR;
		wf_control_set_max(drives_fan);
		return;
	}
	speed = wf_pid_run(&drives_pid, temp);

	DBG_LOTS("drives PID temp=%d.%.3d speed=%d\n",
		 FIX32TOPRINT(temp), speed);

	err = wf_control_set(drives_fan, speed);
	if (err) {
		printk(KERN_WARNING "windfarm: drive bay fan error %d\n", err);
		failure_state |= FAILURE_FAN;
	}
}

static void drives_setup_pid(void)
{
	/* first time initialize things */
	s32 fmin = wf_control_get_min(drives_fan);
	s32 fmax = wf_control_get_max(drives_fan);
	struct wf_pid_param param = drives_param;

	param.min = max(param.min, fmin);
	param.max = min(param.max, fmax);
	wf_pid_init(&drives_pid, &param);
	drives_tick = 1;

	pr_info("wf_pm72: Drive bay control loop started.\n");
}

static void set_fail_state(void)
{
	cpu_max_all_fans();

	if (backside_fan)
		wf_control_set_max(backside_fan);
	if (slots_fan)
		wf_control_set_max(slots_fan);
	if (drives_fan)
		wf_control_set_max(drives_fan);
}

static void pm72_tick(void)
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
		drives_setup_pid();

		/*
		 * We don't have the right stuff to drive the PCI fan
		 * so we fix it to a default value
		 */
		wf_control_set(slots_fan, SLOTS_FAN_DEFAULT_PWM);

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
	if (cpu_pid_combined)
		cpu_fans_tick_combined();
	else
		cpu_fans_tick_split();
	backside_fan_tick();
	drives_fan_tick();

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

static void pm72_new_control(struct wf_control *ct)
{
	bool all_controls;
	bool had_pump = cpu_pumps[0] || cpu_pumps[1];

	if (!strcmp(ct->name, "cpu-front-fan-0"))
		cpu_front_fans[0] = ct;
	else if (!strcmp(ct->name, "cpu-front-fan-1"))
		cpu_front_fans[1] = ct;
	else if (!strcmp(ct->name, "cpu-rear-fan-0"))
		cpu_rear_fans[0] = ct;
	else if (!strcmp(ct->name, "cpu-rear-fan-1"))
		cpu_rear_fans[1] = ct;
	else if (!strcmp(ct->name, "cpu-pump-0"))
		cpu_pumps[0] = ct;
	else if (!strcmp(ct->name, "cpu-pump-1"))
		cpu_pumps[1] = ct;
	else if (!strcmp(ct->name, "backside-fan"))
		backside_fan = ct;
	else if (!strcmp(ct->name, "slots-fan"))
		slots_fan = ct;
	else if (!strcmp(ct->name, "drive-bay-fan"))
		drives_fan = ct;
	else if (!strcmp(ct->name, "cpufreq-clamp"))
		cpufreq_clamp = ct;

	all_controls =
		cpu_front_fans[0] &&
		cpu_rear_fans[0] &&
		backside_fan &&
		slots_fan &&
		drives_fan;
	if (nr_chips > 1)
		all_controls &=
			cpu_front_fans[1] &&
			cpu_rear_fans[1];
	have_all_controls = all_controls;

	if ((cpu_pumps[0] || cpu_pumps[1]) && !had_pump) {
		pr_info("wf_pm72: Liquid cooling pump(s) detected,"
			" using new algorithm !\n");
		cpu_pid_combined = true;
	}
}


static void pm72_new_sensor(struct wf_sensor *sr)
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
	else if (!strcmp(sr->name, "hd-temp"))
		drives_temp = sr;

	all_sensors =
		sens_cpu_temp[0] &&
		sens_cpu_volts[0] &&
		sens_cpu_amps[0] &&
		backside_temp &&
		drives_temp;
	if (nr_chips > 1)
		all_sensors &=
			sens_cpu_temp[1] &&
			sens_cpu_volts[1] &&
			sens_cpu_amps[1];

	have_all_sensors = all_sensors;
}

static int pm72_wf_notify(struct notifier_block *self,
			  unsigned long event, void *data)
{
	switch (event) {
	case WF_EVENT_NEW_SENSOR:
		pm72_new_sensor(data);
		break;
	case WF_EVENT_NEW_CONTROL:
		pm72_new_control(data);
		break;
	case WF_EVENT_TICK:
		if (have_all_controls && have_all_sensors)
			pm72_tick();
	}
	return 0;
}

static struct notifier_block pm72_events = {
	.notifier_call = pm72_wf_notify,
};

static int wf_pm72_probe(struct platform_device *dev)
{
	wf_register_client(&pm72_events);
	return 0;
}

static int wf_pm72_remove(struct platform_device *dev)
{
	wf_unregister_client(&pm72_events);

	/* should release all sensors and controls */
	return 0;
}

static struct platform_driver wf_pm72_driver = {
	.probe	= wf_pm72_probe,
	.remove	= wf_pm72_remove,
	.driver	= {
		.name = "windfarm",
	},
};

static int __init wf_pm72_init(void)
{
	struct device_node *cpu;
	int i;

	if (!of_machine_is_compatible("PowerMac7,2") &&
	    !of_machine_is_compatible("PowerMac7,3"))
		return -ENODEV;

	/* Count the number of CPU cores */
	nr_chips = 0;
	for_each_node_by_type(cpu, "cpu")
		++nr_chips;
	if (nr_chips > NR_CHIPS)
		nr_chips = NR_CHIPS;

	pr_info("windfarm: Initializing for desktop G5 with %d chips\n",
		nr_chips);

	/* Get MPU data for each CPU */
	for (i = 0; i < nr_chips; i++) {
		cpu_mpu_data[i] = wf_get_mpu(i);
		if (!cpu_mpu_data[i]) {
			pr_err("wf_pm72: Failed to find MPU data for CPU %d\n", i);
			return -ENXIO;
		}
	}

#ifdef MODULE
	request_module("windfarm_fcu_controls");
	request_module("windfarm_lm75_sensor");
	request_module("windfarm_ad7417_sensor");
	request_module("windfarm_max6690_sensor");
	request_module("windfarm_cpufreq_clamp");
#endif /* MODULE */

	platform_driver_register(&wf_pm72_driver);
	return 0;
}

static void __exit wf_pm72_exit(void)
{
	platform_driver_unregister(&wf_pm72_driver);
}

module_init(wf_pm72_init);
module_exit(wf_pm72_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Thermal control for AGP PowerMac G5s");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:windfarm");
