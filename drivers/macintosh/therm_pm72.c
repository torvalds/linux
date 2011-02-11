/*
 * Device driver for the thermostats & fan controller of  the
 * Apple G5 "PowerMac7,2" desktop machines.
 *
 * (c) Copyright IBM Corp. 2003-2004
 *
 * Maintained by: Benjamin Herrenschmidt
 *                <benh@kernel.crashing.org>
 * 
 *
 * The algorithm used is the PID control algorithm, used the same
 * way the published Darwin code does, using the same values that
 * are present in the Darwin 7.0 snapshot property lists.
 *
 * As far as the CPUs control loops are concerned, I use the
 * calibration & PID constants provided by the EEPROM,
 * I do _not_ embed any value from the property lists, as the ones
 * provided by Darwin 7.0 seem to always have an older version that
 * what I've seen on the actual computers.
 * It would be interesting to verify that though. Darwin has a
 * version code of 1.0.0d11 for all control loops it seems, while
 * so far, the machines EEPROMs contain a dataset versioned 1.0.0f
 *
 * Darwin doesn't provide source to all parts, some missing
 * bits like the AppleFCU driver or the actual scale of some
 * of the values returned by sensors had to be "guessed" some
 * way... or based on what Open Firmware does.
 *
 * I didn't yet figure out how to get the slots power consumption
 * out of the FCU, so that part has not been implemented yet and
 * the slots fan is set to a fixed 50% PWM, hoping this value is
 * safe enough ...
 *
 * Note: I have observed strange oscillations of the CPU control
 * loop on a dual G5 here. When idle, the CPU exhaust fan tend to
 * oscillates slowly (over several minutes) between the minimum
 * of 300RPMs and approx. 1000 RPMs. I don't know what is causing
 * this, it could be some incorrect constant or an error in the
 * way I ported the algorithm, or it could be just normal. I
 * don't have full understanding on the way Apple tweaked the PID
 * algorithm for the CPU control, it is definitely not a standard
 * implementation...
 *
 * TODO:  - Check MPU structure version/signature
 *        - Add things like /sbin/overtemp for non-critical
 *          overtemp conditions so userland can take some policy
 *          decisions, like slewing down CPUs
 *	  - Deal with fan and i2c failures in a better way
 *	  - Maybe do a generic PID based on params used for
 *	    U3 and Drives ? Definitely need to factor code a bit
 *          bettter... also make sensor detection more robust using
 *          the device-tree to probe for them
 *        - Figure out how to get the slots consumption and set the
 *          slots fan accordingly
 *
 * History:
 *
 *  Nov. 13, 2003 : 0.5
 *	- First release
 *
 *  Nov. 14, 2003 : 0.6
 *	- Read fan speed from FCU, low level fan routines now deal
 *	  with errors & check fan status, though higher level don't
 *	  do much.
 *	- Move a bunch of definitions to .h file
 *
 *  Nov. 18, 2003 : 0.7
 *	- Fix build on ppc64 kernel
 *	- Move back statics definitions to .c file
 *	- Avoid calling schedule_timeout with a negative number
 *
 *  Dec. 18, 2003 : 0.8
 *	- Fix typo when reading back fan speed on 2 CPU machines
 *
 *  Mar. 11, 2004 : 0.9
 *	- Rework code accessing the ADC chips, make it more robust and
 *	  closer to the chip spec. Also make sure it is configured properly,
 *        I've seen yet unexplained cases where on startup, I would have stale
 *        values in the configuration register
 *	- Switch back to use of target fan speed for PID, thus lowering
 *        pressure on i2c
 *
 *  Oct. 20, 2004 : 1.1
 *	- Add device-tree lookup for fan IDs, should detect liquid cooling
 *        pumps when present
 *	- Enable driver for PowerMac7,3 machines
 *	- Split the U3/Backside cooling on U3 & U3H versions as Darwin does
 *	- Add new CPU cooling algorithm for machines with liquid cooling
 *	- Workaround for some PowerMac7,3 with empty "fan" node in the devtree
 *	- Fix a signed/unsigned compare issue in some PID loops
 *
 *  Mar. 10, 2005 : 1.2
 *	- Add basic support for Xserve G5
 *	- Retreive pumps min/max from EEPROM image in device-tree (broken)
 *	- Use min/max macros here or there
 *	- Latest darwin updated U3H min fan speed to 20% PWM
 *
 *  July. 06, 2006 : 1.3
 *	- Fix setting of RPM fans on Xserve G5 (they were going too fast)
 *      - Add missing slots fan control loop for Xserve G5
 *	- Lower fixed slots fan speed from 50% to 40% on desktop G5s. We
 *        still can't properly implement the control loop for these, so let's
 *        reduce the noise a little bit, it appears that 40% still gives us
 *        a pretty good air flow
 *	- Add code to "tickle" the FCU regulary so it doesn't think that
 *        we are gone while in fact, the machine just didn't need any fan
 *        speed change lately
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/reboot.h>
#include <linux/kmod.h>
#include <linux/i2c.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <asm/macio.h>

#include "therm_pm72.h"

#define VERSION "1.3"

#undef DEBUG

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif


/*
 * Driver statics
 */

static struct platform_device *		of_dev;
static struct i2c_adapter *		u3_0;
static struct i2c_adapter *		u3_1;
static struct i2c_adapter *		k2;
static struct i2c_client *		fcu;
static struct cpu_pid_state		cpu_state[2];
static struct basckside_pid_params	backside_params;
static struct backside_pid_state	backside_state;
static struct drives_pid_state		drives_state;
static struct dimm_pid_state		dimms_state;
static struct slots_pid_state		slots_state;
static int				state;
static int				cpu_count;
static int				cpu_pid_type;
static struct task_struct		*ctrl_task;
static struct completion		ctrl_complete;
static int				critical_state;
static int				rackmac;
static s32				dimm_output_clamp;
static int 				fcu_rpm_shift;
static int				fcu_tickle_ticks;
static DEFINE_MUTEX(driver_lock);

/*
 * We have 3 types of CPU PID control. One is "split" old style control
 * for intake & exhaust fans, the other is "combined" control for both
 * CPUs that also deals with the pumps when present. To be "compatible"
 * with OS X at this point, we only use "COMBINED" on the machines that
 * are identified as having the pumps (though that identification is at
 * least dodgy). Ultimately, we could probably switch completely to this
 * algorithm provided we hack it to deal with the UP case
 */
#define CPU_PID_TYPE_SPLIT	0
#define CPU_PID_TYPE_COMBINED	1
#define CPU_PID_TYPE_RACKMAC	2

/*
 * This table describes all fans in the FCU. The "id" and "type" values
 * are defaults valid for all earlier machines. Newer machines will
 * eventually override the table content based on the device-tree
 */
struct fcu_fan_table
{
	char*	loc;	/* location code */
	int	type;	/* 0 = rpm, 1 = pwm, 2 = pump */
	int	id;	/* id or -1 */
};

#define FCU_FAN_RPM		0
#define FCU_FAN_PWM		1

#define FCU_FAN_ABSENT_ID	-1

#define FCU_FAN_COUNT		ARRAY_SIZE(fcu_fans)

struct fcu_fan_table	fcu_fans[] = {
	[BACKSIDE_FAN_PWM_INDEX] = {
		.loc	= "BACKSIDE,SYS CTRLR FAN",
		.type	= FCU_FAN_PWM,
		.id	= BACKSIDE_FAN_PWM_DEFAULT_ID,
	},
	[DRIVES_FAN_RPM_INDEX] = {
		.loc	= "DRIVE BAY",
		.type	= FCU_FAN_RPM,
		.id	= DRIVES_FAN_RPM_DEFAULT_ID,
	},
	[SLOTS_FAN_PWM_INDEX] = {
		.loc	= "SLOT,PCI FAN",
		.type	= FCU_FAN_PWM,
		.id	= SLOTS_FAN_PWM_DEFAULT_ID,
	},
	[CPUA_INTAKE_FAN_RPM_INDEX] = {
		.loc	= "CPU A INTAKE",
		.type	= FCU_FAN_RPM,
		.id	= CPUA_INTAKE_FAN_RPM_DEFAULT_ID,
	},
	[CPUA_EXHAUST_FAN_RPM_INDEX] = {
		.loc	= "CPU A EXHAUST",
		.type	= FCU_FAN_RPM,
		.id	= CPUA_EXHAUST_FAN_RPM_DEFAULT_ID,
	},
	[CPUB_INTAKE_FAN_RPM_INDEX] = {
		.loc	= "CPU B INTAKE",
		.type	= FCU_FAN_RPM,
		.id	= CPUB_INTAKE_FAN_RPM_DEFAULT_ID,
	},
	[CPUB_EXHAUST_FAN_RPM_INDEX] = {
		.loc	= "CPU B EXHAUST",
		.type	= FCU_FAN_RPM,
		.id	= CPUB_EXHAUST_FAN_RPM_DEFAULT_ID,
	},
	/* pumps aren't present by default, have to be looked up in the
	 * device-tree
	 */
	[CPUA_PUMP_RPM_INDEX] = {
		.loc	= "CPU A PUMP",
		.type	= FCU_FAN_RPM,		
		.id	= FCU_FAN_ABSENT_ID,
	},
	[CPUB_PUMP_RPM_INDEX] = {
		.loc	= "CPU B PUMP",
		.type	= FCU_FAN_RPM,
		.id	= FCU_FAN_ABSENT_ID,
	},
	/* Xserve fans */
	[CPU_A1_FAN_RPM_INDEX] = {
		.loc	= "CPU A 1",
		.type	= FCU_FAN_RPM,
		.id	= FCU_FAN_ABSENT_ID,
	},
	[CPU_A2_FAN_RPM_INDEX] = {
		.loc	= "CPU A 2",
		.type	= FCU_FAN_RPM,
		.id	= FCU_FAN_ABSENT_ID,
	},
	[CPU_A3_FAN_RPM_INDEX] = {
		.loc	= "CPU A 3",
		.type	= FCU_FAN_RPM,
		.id	= FCU_FAN_ABSENT_ID,
	},
	[CPU_B1_FAN_RPM_INDEX] = {
		.loc	= "CPU B 1",
		.type	= FCU_FAN_RPM,
		.id	= FCU_FAN_ABSENT_ID,
	},
	[CPU_B2_FAN_RPM_INDEX] = {
		.loc	= "CPU B 2",
		.type	= FCU_FAN_RPM,
		.id	= FCU_FAN_ABSENT_ID,
	},
	[CPU_B3_FAN_RPM_INDEX] = {
		.loc	= "CPU B 3",
		.type	= FCU_FAN_RPM,
		.id	= FCU_FAN_ABSENT_ID,
	},
};

static struct i2c_driver therm_pm72_driver;

/*
 * Utility function to create an i2c_client structure and
 * attach it to one of u3 adapters
 */
static struct i2c_client *attach_i2c_chip(int id, const char *name)
{
	struct i2c_client *clt;
	struct i2c_adapter *adap;
	struct i2c_board_info info;

	if (id & 0x200)
		adap = k2;
	else if (id & 0x100)
		adap = u3_1;
	else
		adap = u3_0;
	if (adap == NULL)
		return NULL;

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = (id >> 1) & 0x7f;
	strlcpy(info.type, "therm_pm72", I2C_NAME_SIZE);
	clt = i2c_new_device(adap, &info);
	if (!clt) {
		printk(KERN_ERR "therm_pm72: Failed to attach to i2c ID 0x%x\n", id);
		return NULL;
	}

	/*
	 * Let i2c-core delete that device on driver removal.
	 * This is safe because i2c-core holds the core_lock mutex for us.
	 */
	list_add_tail(&clt->detected, &therm_pm72_driver.clients);
	return clt;
}

/*
 * Here are the i2c chip access wrappers
 */

static void initialize_adc(struct cpu_pid_state *state)
{
	int rc;
	u8 buf[2];

	/* Read ADC the configuration register and cache it. We
	 * also make sure Config2 contains proper values, I've seen
	 * cases where we got stale grabage in there, thus preventing
	 * proper reading of conv. values
	 */

	/* Clear Config2 */
	buf[0] = 5;
	buf[1] = 0;
	i2c_master_send(state->monitor, buf, 2);

	/* Read & cache Config1 */
	buf[0] = 1;
	rc = i2c_master_send(state->monitor, buf, 1);
	if (rc > 0) {
		rc = i2c_master_recv(state->monitor, buf, 1);
		if (rc > 0) {
			state->adc_config = buf[0];
			DBG("ADC config reg: %02x\n", state->adc_config);
			/* Disable shutdown mode */
		       	state->adc_config &= 0xfe;
			buf[0] = 1;
			buf[1] = state->adc_config;
			rc = i2c_master_send(state->monitor, buf, 2);
		}
	}
	if (rc <= 0)
		printk(KERN_ERR "therm_pm72: Error reading ADC config"
		       " register !\n");
}

static int read_smon_adc(struct cpu_pid_state *state, int chan)
{
	int rc, data, tries = 0;
	u8 buf[2];

	for (;;) {
		/* Set channel */
		buf[0] = 1;
		buf[1] = (state->adc_config & 0x1f) | (chan << 5);
		rc = i2c_master_send(state->monitor, buf, 2);
		if (rc <= 0)
			goto error;
		/* Wait for convertion */
		msleep(1);
		/* Switch to data register */
		buf[0] = 4;
		rc = i2c_master_send(state->monitor, buf, 1);
		if (rc <= 0)
			goto error;
		/* Read result */
		rc = i2c_master_recv(state->monitor, buf, 2);
		if (rc < 0)
			goto error;
		data = ((u16)buf[0]) << 8 | (u16)buf[1];
		return data >> 6;
	error:
		DBG("Error reading ADC, retrying...\n");
		if (++tries > 10) {
			printk(KERN_ERR "therm_pm72: Error reading ADC !\n");
			return -1;
		}
		msleep(10);
	}
}

static int read_lm87_reg(struct i2c_client * chip, int reg)
{
	int rc, tries = 0;
	u8 buf;

	for (;;) {
		/* Set address */
		buf = (u8)reg;
		rc = i2c_master_send(chip, &buf, 1);
		if (rc <= 0)
			goto error;
		rc = i2c_master_recv(chip, &buf, 1);
		if (rc <= 0)
			goto error;
		return (int)buf;
	error:
		DBG("Error reading LM87, retrying...\n");
		if (++tries > 10) {
			printk(KERN_ERR "therm_pm72: Error reading LM87 !\n");
			return -1;
		}
		msleep(10);
	}
}

static int fan_read_reg(int reg, unsigned char *buf, int nb)
{
	int tries, nr, nw;

	buf[0] = reg;
	tries = 0;
	for (;;) {
		nw = i2c_master_send(fcu, buf, 1);
		if (nw > 0 || (nw < 0 && nw != -EIO) || tries >= 100)
			break;
		msleep(10);
		++tries;
	}
	if (nw <= 0) {
		printk(KERN_ERR "Failure writing address to FCU: %d", nw);
		return -EIO;
	}
	tries = 0;
	for (;;) {
		nr = i2c_master_recv(fcu, buf, nb);
		if (nr > 0 || (nr < 0 && nr != -ENODEV) || tries >= 100)
			break;
		msleep(10);
		++tries;
	}
	if (nr <= 0)
		printk(KERN_ERR "Failure reading data from FCU: %d", nw);
	return nr;
}

static int fan_write_reg(int reg, const unsigned char *ptr, int nb)
{
	int tries, nw;
	unsigned char buf[16];

	buf[0] = reg;
	memcpy(buf+1, ptr, nb);
	++nb;
	tries = 0;
	for (;;) {
		nw = i2c_master_send(fcu, buf, nb);
		if (nw > 0 || (nw < 0 && nw != -EIO) || tries >= 100)
			break;
		msleep(10);
		++tries;
	}
	if (nw < 0)
		printk(KERN_ERR "Failure writing to FCU: %d", nw);
	return nw;
}

static int start_fcu(void)
{
	unsigned char buf = 0xff;
	int rc;

	rc = fan_write_reg(0xe, &buf, 1);
	if (rc < 0)
		return -EIO;
	rc = fan_write_reg(0x2e, &buf, 1);
	if (rc < 0)
		return -EIO;
	rc = fan_read_reg(0, &buf, 1);
	if (rc < 0)
		return -EIO;
	fcu_rpm_shift = (buf == 1) ? 2 : 3;
	printk(KERN_DEBUG "FCU Initialized, RPM fan shift is %d\n",
	       fcu_rpm_shift);

	return 0;
}

static int set_rpm_fan(int fan_index, int rpm)
{
	unsigned char buf[2];
	int rc, id, min, max;

	if (fcu_fans[fan_index].type != FCU_FAN_RPM)
		return -EINVAL;
	id = fcu_fans[fan_index].id; 
	if (id == FCU_FAN_ABSENT_ID)
		return -EINVAL;

	min = 2400 >> fcu_rpm_shift;
	max = 56000 >> fcu_rpm_shift;

	if (rpm < min)
		rpm = min;
	else if (rpm > max)
		rpm = max;
	buf[0] = rpm >> (8 - fcu_rpm_shift);
	buf[1] = rpm << fcu_rpm_shift;
	rc = fan_write_reg(0x10 + (id * 2), buf, 2);
	if (rc < 0)
		return -EIO;
	return 0;
}

static int get_rpm_fan(int fan_index, int programmed)
{
	unsigned char failure;
	unsigned char active;
	unsigned char buf[2];
	int rc, id, reg_base;

	if (fcu_fans[fan_index].type != FCU_FAN_RPM)
		return -EINVAL;
	id = fcu_fans[fan_index].id; 
	if (id == FCU_FAN_ABSENT_ID)
		return -EINVAL;

	rc = fan_read_reg(0xb, &failure, 1);
	if (rc != 1)
		return -EIO;
	if ((failure & (1 << id)) != 0)
		return -EFAULT;
	rc = fan_read_reg(0xd, &active, 1);
	if (rc != 1)
		return -EIO;
	if ((active & (1 << id)) == 0)
		return -ENXIO;

	/* Programmed value or real current speed */
	reg_base = programmed ? 0x10 : 0x11;
	rc = fan_read_reg(reg_base + (id * 2), buf, 2);
	if (rc != 2)
		return -EIO;

	return (buf[0] << (8 - fcu_rpm_shift)) | buf[1] >> fcu_rpm_shift;
}

static int set_pwm_fan(int fan_index, int pwm)
{
	unsigned char buf[2];
	int rc, id;

	if (fcu_fans[fan_index].type != FCU_FAN_PWM)
		return -EINVAL;
	id = fcu_fans[fan_index].id; 
	if (id == FCU_FAN_ABSENT_ID)
		return -EINVAL;

	if (pwm < 10)
		pwm = 10;
	else if (pwm > 100)
		pwm = 100;
	pwm = (pwm * 2559) / 1000;
	buf[0] = pwm;
	rc = fan_write_reg(0x30 + (id * 2), buf, 1);
	if (rc < 0)
		return rc;
	return 0;
}

static int get_pwm_fan(int fan_index)
{
	unsigned char failure;
	unsigned char active;
	unsigned char buf[2];
	int rc, id;

	if (fcu_fans[fan_index].type != FCU_FAN_PWM)
		return -EINVAL;
	id = fcu_fans[fan_index].id; 
	if (id == FCU_FAN_ABSENT_ID)
		return -EINVAL;

	rc = fan_read_reg(0x2b, &failure, 1);
	if (rc != 1)
		return -EIO;
	if ((failure & (1 << id)) != 0)
		return -EFAULT;
	rc = fan_read_reg(0x2d, &active, 1);
	if (rc != 1)
		return -EIO;
	if ((active & (1 << id)) == 0)
		return -ENXIO;

	/* Programmed value or real current speed */
	rc = fan_read_reg(0x30 + (id * 2), buf, 1);
	if (rc != 1)
		return -EIO;

	return (buf[0] * 1000) / 2559;
}

static void tickle_fcu(void)
{
	int pwm;

	pwm = get_pwm_fan(SLOTS_FAN_PWM_INDEX);

	DBG("FCU Tickle, slots fan is: %d\n", pwm);
	if (pwm < 0)
		pwm = 100;

	if (!rackmac) {
		pwm = SLOTS_FAN_DEFAULT_PWM;
	} else if (pwm < SLOTS_PID_OUTPUT_MIN)
		pwm = SLOTS_PID_OUTPUT_MIN;

	/* That is hopefully enough to make the FCU happy */
	set_pwm_fan(SLOTS_FAN_PWM_INDEX, pwm);
}


/*
 * Utility routine to read the CPU calibration EEPROM data
 * from the device-tree
 */
static int read_eeprom(int cpu, struct mpu_data *out)
{
	struct device_node *np;
	char nodename[64];
	const u8 *data;
	int len;

	/* prom.c routine for finding a node by path is a bit brain dead
	 * and requires exact @xxx unit numbers. This is a bit ugly but
	 * will work for these machines
	 */
	sprintf(nodename, "/u3@0,f8000000/i2c@f8001000/cpuid@a%d", cpu ? 2 : 0);
	np = of_find_node_by_path(nodename);
	if (np == NULL) {
		printk(KERN_ERR "therm_pm72: Failed to retrieve cpuid node from device-tree\n");
		return -ENODEV;
	}
	data = of_get_property(np, "cpuid", &len);
	if (data == NULL) {
		printk(KERN_ERR "therm_pm72: Failed to retrieve cpuid property from device-tree\n");
		of_node_put(np);
		return -ENODEV;
	}
	memcpy(out, data, sizeof(struct mpu_data));
	of_node_put(np);
	
	return 0;
}

static void fetch_cpu_pumps_minmax(void)
{
	struct cpu_pid_state *state0 = &cpu_state[0];
	struct cpu_pid_state *state1 = &cpu_state[1];
	u16 pump_min = 0, pump_max = 0xffff;
	u16 tmp[4];

	/* Try to fetch pumps min/max infos from eeprom */

	memcpy(&tmp, &state0->mpu.processor_part_num, 8);
	if (tmp[0] != 0xffff && tmp[1] != 0xffff) {
		pump_min = max(pump_min, tmp[0]);
		pump_max = min(pump_max, tmp[1]);
	}
	if (tmp[2] != 0xffff && tmp[3] != 0xffff) {
		pump_min = max(pump_min, tmp[2]);
		pump_max = min(pump_max, tmp[3]);
	}

	/* Double check the values, this _IS_ needed as the EEPROM on
	 * some dual 2.5Ghz G5s seem, at least, to have both min & max
	 * same to the same value ... (grrrr)
	 */
	if (pump_min == pump_max || pump_min == 0 || pump_max == 0xffff) {
		pump_min = CPU_PUMP_OUTPUT_MIN;
		pump_max = CPU_PUMP_OUTPUT_MAX;
	}

	state0->pump_min = state1->pump_min = pump_min;
	state0->pump_max = state1->pump_max = pump_max;
}

/* 
 * Now, unfortunately, sysfs doesn't give us a nice void * we could
 * pass around to the attribute functions, so we don't really have
 * choice but implement a bunch of them...
 *
 * That sucks a bit, we take the lock because FIX32TOPRINT evaluates
 * the input twice... I accept patches :)
 */
#define BUILD_SHOW_FUNC_FIX(name, data)				\
static ssize_t show_##name(struct device *dev, struct device_attribute *attr, char *buf)	\
{								\
	ssize_t r;						\
	mutex_lock(&driver_lock);					\
	r = sprintf(buf, "%d.%03d", FIX32TOPRINT(data));	\
	mutex_unlock(&driver_lock);					\
	return r;						\
}
#define BUILD_SHOW_FUNC_INT(name, data)				\
static ssize_t show_##name(struct device *dev, struct device_attribute *attr, char *buf)	\
{								\
	return sprintf(buf, "%d", data);			\
}

BUILD_SHOW_FUNC_FIX(cpu0_temperature, cpu_state[0].last_temp)
BUILD_SHOW_FUNC_FIX(cpu0_voltage, cpu_state[0].voltage)
BUILD_SHOW_FUNC_FIX(cpu0_current, cpu_state[0].current_a)
BUILD_SHOW_FUNC_INT(cpu0_exhaust_fan_rpm, cpu_state[0].rpm)
BUILD_SHOW_FUNC_INT(cpu0_intake_fan_rpm, cpu_state[0].intake_rpm)

BUILD_SHOW_FUNC_FIX(cpu1_temperature, cpu_state[1].last_temp)
BUILD_SHOW_FUNC_FIX(cpu1_voltage, cpu_state[1].voltage)
BUILD_SHOW_FUNC_FIX(cpu1_current, cpu_state[1].current_a)
BUILD_SHOW_FUNC_INT(cpu1_exhaust_fan_rpm, cpu_state[1].rpm)
BUILD_SHOW_FUNC_INT(cpu1_intake_fan_rpm, cpu_state[1].intake_rpm)

BUILD_SHOW_FUNC_FIX(backside_temperature, backside_state.last_temp)
BUILD_SHOW_FUNC_INT(backside_fan_pwm, backside_state.pwm)

BUILD_SHOW_FUNC_FIX(drives_temperature, drives_state.last_temp)
BUILD_SHOW_FUNC_INT(drives_fan_rpm, drives_state.rpm)

BUILD_SHOW_FUNC_FIX(slots_temperature, slots_state.last_temp)
BUILD_SHOW_FUNC_INT(slots_fan_pwm, slots_state.pwm)

BUILD_SHOW_FUNC_FIX(dimms_temperature, dimms_state.last_temp)

static DEVICE_ATTR(cpu0_temperature,S_IRUGO,show_cpu0_temperature,NULL);
static DEVICE_ATTR(cpu0_voltage,S_IRUGO,show_cpu0_voltage,NULL);
static DEVICE_ATTR(cpu0_current,S_IRUGO,show_cpu0_current,NULL);
static DEVICE_ATTR(cpu0_exhaust_fan_rpm,S_IRUGO,show_cpu0_exhaust_fan_rpm,NULL);
static DEVICE_ATTR(cpu0_intake_fan_rpm,S_IRUGO,show_cpu0_intake_fan_rpm,NULL);

static DEVICE_ATTR(cpu1_temperature,S_IRUGO,show_cpu1_temperature,NULL);
static DEVICE_ATTR(cpu1_voltage,S_IRUGO,show_cpu1_voltage,NULL);
static DEVICE_ATTR(cpu1_current,S_IRUGO,show_cpu1_current,NULL);
static DEVICE_ATTR(cpu1_exhaust_fan_rpm,S_IRUGO,show_cpu1_exhaust_fan_rpm,NULL);
static DEVICE_ATTR(cpu1_intake_fan_rpm,S_IRUGO,show_cpu1_intake_fan_rpm,NULL);

static DEVICE_ATTR(backside_temperature,S_IRUGO,show_backside_temperature,NULL);
static DEVICE_ATTR(backside_fan_pwm,S_IRUGO,show_backside_fan_pwm,NULL);

static DEVICE_ATTR(drives_temperature,S_IRUGO,show_drives_temperature,NULL);
static DEVICE_ATTR(drives_fan_rpm,S_IRUGO,show_drives_fan_rpm,NULL);

static DEVICE_ATTR(slots_temperature,S_IRUGO,show_slots_temperature,NULL);
static DEVICE_ATTR(slots_fan_pwm,S_IRUGO,show_slots_fan_pwm,NULL);

static DEVICE_ATTR(dimms_temperature,S_IRUGO,show_dimms_temperature,NULL);

/*
 * CPUs fans control loop
 */

static int do_read_one_cpu_values(struct cpu_pid_state *state, s32 *temp, s32 *power)
{
	s32 ltemp, volts, amps;
	int index, rc = 0;

	/* Default (in case of error) */
	*temp = state->cur_temp;
	*power = state->cur_power;

	if (cpu_pid_type == CPU_PID_TYPE_RACKMAC)
		index = (state->index == 0) ?
			CPU_A1_FAN_RPM_INDEX : CPU_B1_FAN_RPM_INDEX;
	else
		index = (state->index == 0) ?
			CPUA_EXHAUST_FAN_RPM_INDEX : CPUB_EXHAUST_FAN_RPM_INDEX;

	/* Read current fan status */
	rc = get_rpm_fan(index, !RPM_PID_USE_ACTUAL_SPEED);
	if (rc < 0) {
		/* XXX What do we do now ? Nothing for now, keep old value, but
		 * return error upstream
		 */
		DBG("  cpu %d, fan reading error !\n", state->index);
	} else {
		state->rpm = rc;
		DBG("  cpu %d, exhaust RPM: %d\n", state->index, state->rpm);
	}

	/* Get some sensor readings and scale it */
	ltemp = read_smon_adc(state, 1);
	if (ltemp == -1) {
		/* XXX What do we do now ? */
		state->overtemp++;
		if (rc == 0)
			rc = -EIO;
		DBG("  cpu %d, temp reading error !\n", state->index);
	} else {
		/* Fixup temperature according to diode calibration
		 */
		DBG("  cpu %d, temp raw: %04x, m_diode: %04x, b_diode: %04x\n",
		    state->index,
		    ltemp, state->mpu.mdiode, state->mpu.bdiode);
		*temp = ((s32)ltemp * (s32)state->mpu.mdiode + ((s32)state->mpu.bdiode << 12)) >> 2;
		state->last_temp = *temp;
		DBG("  temp: %d.%03d\n", FIX32TOPRINT((*temp)));
	}

	/*
	 * Read voltage & current and calculate power
	 */
	volts = read_smon_adc(state, 3);
	amps = read_smon_adc(state, 4);

	/* Scale voltage and current raw sensor values according to fixed scales
	 * obtained in Darwin and calculate power from I and V
	 */
	volts *= ADC_CPU_VOLTAGE_SCALE;
	amps *= ADC_CPU_CURRENT_SCALE;
	*power = (((u64)volts) * ((u64)amps)) >> 16;
	state->voltage = volts;
	state->current_a = amps;
	state->last_power = *power;

	DBG("  cpu %d, current: %d.%03d, voltage: %d.%03d, power: %d.%03d W\n",
	    state->index, FIX32TOPRINT(state->current_a),
	    FIX32TOPRINT(state->voltage), FIX32TOPRINT(*power));

	return 0;
}

static void do_cpu_pid(struct cpu_pid_state *state, s32 temp, s32 power)
{
	s32 power_target, integral, derivative, proportional, adj_in_target, sval;
	s64 integ_p, deriv_p, prop_p, sum; 
	int i;

	/* Calculate power target value (could be done once for all)
	 * and convert to a 16.16 fp number
	 */
	power_target = ((u32)(state->mpu.pmaxh - state->mpu.padjmax)) << 16;
	DBG("  power target: %d.%03d, error: %d.%03d\n",
	    FIX32TOPRINT(power_target), FIX32TOPRINT(power_target - power));

	/* Store temperature and power in history array */
	state->cur_temp = (state->cur_temp + 1) % CPU_TEMP_HISTORY_SIZE;
	state->temp_history[state->cur_temp] = temp;
	state->cur_power = (state->cur_power + 1) % state->count_power;
	state->power_history[state->cur_power] = power;
	state->error_history[state->cur_power] = power_target - power;
	
	/* If first loop, fill the history table */
	if (state->first) {
		for (i = 0; i < (state->count_power - 1); i++) {
			state->cur_power = (state->cur_power + 1) % state->count_power;
			state->power_history[state->cur_power] = power;
			state->error_history[state->cur_power] = power_target - power;
		}
		for (i = 0; i < (CPU_TEMP_HISTORY_SIZE - 1); i++) {
			state->cur_temp = (state->cur_temp + 1) % CPU_TEMP_HISTORY_SIZE;
			state->temp_history[state->cur_temp] = temp;			
		}
		state->first = 0;
	}

	/* Calculate the integral term normally based on the "power" values */
	sum = 0;
	integral = 0;
	for (i = 0; i < state->count_power; i++)
		integral += state->error_history[i];
	integral *= CPU_PID_INTERVAL;
	DBG("  integral: %08x\n", integral);

	/* Calculate the adjusted input (sense value).
	 *   G_r is 12.20
	 *   integ is 16.16
	 *   so the result is 28.36
	 *
	 * input target is mpu.ttarget, input max is mpu.tmax
	 */
	integ_p = ((s64)state->mpu.pid_gr) * (s64)integral;
	DBG("   integ_p: %d\n", (int)(integ_p >> 36));
	sval = (state->mpu.tmax << 16) - ((integ_p >> 20) & 0xffffffff);
	adj_in_target = (state->mpu.ttarget << 16);
	if (adj_in_target > sval)
		adj_in_target = sval;
	DBG("   adj_in_target: %d.%03d, ttarget: %d\n", FIX32TOPRINT(adj_in_target),
	    state->mpu.ttarget);

	/* Calculate the derivative term */
	derivative = state->temp_history[state->cur_temp] -
		state->temp_history[(state->cur_temp + CPU_TEMP_HISTORY_SIZE - 1)
				    % CPU_TEMP_HISTORY_SIZE];
	derivative /= CPU_PID_INTERVAL;
	deriv_p = ((s64)state->mpu.pid_gd) * (s64)derivative;
	DBG("   deriv_p: %d\n", (int)(deriv_p >> 36));
	sum += deriv_p;

	/* Calculate the proportional term */
	proportional = temp - adj_in_target;
	prop_p = ((s64)state->mpu.pid_gp) * (s64)proportional;
	DBG("   prop_p: %d\n", (int)(prop_p >> 36));
	sum += prop_p;

	/* Scale sum */
	sum >>= 36;

	DBG("   sum: %d\n", (int)sum);
	state->rpm += (s32)sum;
}

static void do_monitor_cpu_combined(void)
{
	struct cpu_pid_state *state0 = &cpu_state[0];
	struct cpu_pid_state *state1 = &cpu_state[1];
	s32 temp0, power0, temp1, power1;
	s32 temp_combi, power_combi;
	int rc, intake, pump;

	rc = do_read_one_cpu_values(state0, &temp0, &power0);
	if (rc < 0) {
		/* XXX What do we do now ? */
	}
	state1->overtemp = 0;
	rc = do_read_one_cpu_values(state1, &temp1, &power1);
	if (rc < 0) {
		/* XXX What do we do now ? */
	}
	if (state1->overtemp)
		state0->overtemp++;

	temp_combi = max(temp0, temp1);
	power_combi = max(power0, power1);

	/* Check tmax, increment overtemp if we are there. At tmax+8, we go
	 * full blown immediately and try to trigger a shutdown
	 */
	if (temp_combi >= ((state0->mpu.tmax + 8) << 16)) {
		printk(KERN_WARNING "Warning ! Temperature way above maximum (%d) !\n",
		       temp_combi >> 16);
		state0->overtemp += CPU_MAX_OVERTEMP / 4;
	} else if (temp_combi > (state0->mpu.tmax << 16)) {
		state0->overtemp++;
		printk(KERN_WARNING "Temperature %d above max %d. overtemp %d\n",
		       temp_combi >> 16, state0->mpu.tmax, state0->overtemp);
	} else {
		if (state0->overtemp)
			printk(KERN_WARNING "Temperature back down to %d\n",
			       temp_combi >> 16);
		state0->overtemp = 0;
	}
	if (state0->overtemp >= CPU_MAX_OVERTEMP)
		critical_state = 1;
	if (state0->overtemp > 0) {
		state0->rpm = state0->mpu.rmaxn_exhaust_fan;
		state0->intake_rpm = intake = state0->mpu.rmaxn_intake_fan;
		pump = state0->pump_max;
		goto do_set_fans;
	}

	/* Do the PID */
	do_cpu_pid(state0, temp_combi, power_combi);

	/* Range check */
	state0->rpm = max(state0->rpm, (int)state0->mpu.rminn_exhaust_fan);
	state0->rpm = min(state0->rpm, (int)state0->mpu.rmaxn_exhaust_fan);

	/* Calculate intake fan speed */
	intake = (state0->rpm * CPU_INTAKE_SCALE) >> 16;
	intake = max(intake, (int)state0->mpu.rminn_intake_fan);
	intake = min(intake, (int)state0->mpu.rmaxn_intake_fan);
	state0->intake_rpm = intake;

	/* Calculate pump speed */
	pump = (state0->rpm * state0->pump_max) /
		state0->mpu.rmaxn_exhaust_fan;
	pump = min(pump, state0->pump_max);
	pump = max(pump, state0->pump_min);
	
 do_set_fans:
	/* We copy values from state 0 to state 1 for /sysfs */
	state1->rpm = state0->rpm;
	state1->intake_rpm = state0->intake_rpm;

	DBG("** CPU %d RPM: %d Ex, %d, Pump: %d, In, overtemp: %d\n",
	    state1->index, (int)state1->rpm, intake, pump, state1->overtemp);

	/* We should check for errors, shouldn't we ? But then, what
	 * do we do once the error occurs ? For FCU notified fan
	 * failures (-EFAULT) we probably want to notify userland
	 * some way...
	 */
	set_rpm_fan(CPUA_INTAKE_FAN_RPM_INDEX, intake);
	set_rpm_fan(CPUA_EXHAUST_FAN_RPM_INDEX, state0->rpm);
	set_rpm_fan(CPUB_INTAKE_FAN_RPM_INDEX, intake);
	set_rpm_fan(CPUB_EXHAUST_FAN_RPM_INDEX, state0->rpm);

	if (fcu_fans[CPUA_PUMP_RPM_INDEX].id != FCU_FAN_ABSENT_ID)
		set_rpm_fan(CPUA_PUMP_RPM_INDEX, pump);
	if (fcu_fans[CPUB_PUMP_RPM_INDEX].id != FCU_FAN_ABSENT_ID)
		set_rpm_fan(CPUB_PUMP_RPM_INDEX, pump);
}

static void do_monitor_cpu_split(struct cpu_pid_state *state)
{
	s32 temp, power;
	int rc, intake;

	/* Read current fan status */
	rc = do_read_one_cpu_values(state, &temp, &power);
	if (rc < 0) {
		/* XXX What do we do now ? */
	}

	/* Check tmax, increment overtemp if we are there. At tmax+8, we go
	 * full blown immediately and try to trigger a shutdown
	 */
	if (temp >= ((state->mpu.tmax + 8) << 16)) {
		printk(KERN_WARNING "Warning ! CPU %d temperature way above maximum"
		       " (%d) !\n",
		       state->index, temp >> 16);
		state->overtemp += CPU_MAX_OVERTEMP / 4;
	} else if (temp > (state->mpu.tmax << 16)) {
		state->overtemp++;
		printk(KERN_WARNING "CPU %d temperature %d above max %d. overtemp %d\n",
		       state->index, temp >> 16, state->mpu.tmax, state->overtemp);
	} else {
		if (state->overtemp)
			printk(KERN_WARNING "CPU %d temperature back down to %d\n",
			       state->index, temp >> 16);
		state->overtemp = 0;
	}
	if (state->overtemp >= CPU_MAX_OVERTEMP)
		critical_state = 1;
	if (state->overtemp > 0) {
		state->rpm = state->mpu.rmaxn_exhaust_fan;
		state->intake_rpm = intake = state->mpu.rmaxn_intake_fan;
		goto do_set_fans;
	}

	/* Do the PID */
	do_cpu_pid(state, temp, power);

	/* Range check */
	state->rpm = max(state->rpm, (int)state->mpu.rminn_exhaust_fan);
	state->rpm = min(state->rpm, (int)state->mpu.rmaxn_exhaust_fan);

	/* Calculate intake fan */
	intake = (state->rpm * CPU_INTAKE_SCALE) >> 16;
	intake = max(intake, (int)state->mpu.rminn_intake_fan);
	intake = min(intake, (int)state->mpu.rmaxn_intake_fan);
	state->intake_rpm = intake;

 do_set_fans:
	DBG("** CPU %d RPM: %d Ex, %d In, overtemp: %d\n",
	    state->index, (int)state->rpm, intake, state->overtemp);

	/* We should check for errors, shouldn't we ? But then, what
	 * do we do once the error occurs ? For FCU notified fan
	 * failures (-EFAULT) we probably want to notify userland
	 * some way...
	 */
	if (state->index == 0) {
		set_rpm_fan(CPUA_INTAKE_FAN_RPM_INDEX, intake);
		set_rpm_fan(CPUA_EXHAUST_FAN_RPM_INDEX, state->rpm);
	} else {
		set_rpm_fan(CPUB_INTAKE_FAN_RPM_INDEX, intake);
		set_rpm_fan(CPUB_EXHAUST_FAN_RPM_INDEX, state->rpm);
	}
}

static void do_monitor_cpu_rack(struct cpu_pid_state *state)
{
	s32 temp, power, fan_min;
	int rc;

	/* Read current fan status */
	rc = do_read_one_cpu_values(state, &temp, &power);
	if (rc < 0) {
		/* XXX What do we do now ? */
	}

	/* Check tmax, increment overtemp if we are there. At tmax+8, we go
	 * full blown immediately and try to trigger a shutdown
	 */
	if (temp >= ((state->mpu.tmax + 8) << 16)) {
		printk(KERN_WARNING "Warning ! CPU %d temperature way above maximum"
		       " (%d) !\n",
		       state->index, temp >> 16);
		state->overtemp = CPU_MAX_OVERTEMP / 4;
	} else if (temp > (state->mpu.tmax << 16)) {
		state->overtemp++;
		printk(KERN_WARNING "CPU %d temperature %d above max %d. overtemp %d\n",
		       state->index, temp >> 16, state->mpu.tmax, state->overtemp);
	} else {
		if (state->overtemp)
			printk(KERN_WARNING "CPU %d temperature back down to %d\n",
			       state->index, temp >> 16);
		state->overtemp = 0;
	}
	if (state->overtemp >= CPU_MAX_OVERTEMP)
		critical_state = 1;
	if (state->overtemp > 0) {
		state->rpm = state->intake_rpm = state->mpu.rmaxn_intake_fan;
		goto do_set_fans;
	}

	/* Do the PID */
	do_cpu_pid(state, temp, power);

	/* Check clamp from dimms */
	fan_min = dimm_output_clamp;
	fan_min = max(fan_min, (int)state->mpu.rminn_intake_fan);

	DBG(" CPU min mpu = %d, min dimm = %d\n",
	    state->mpu.rminn_intake_fan, dimm_output_clamp);

	state->rpm = max(state->rpm, (int)fan_min);
	state->rpm = min(state->rpm, (int)state->mpu.rmaxn_intake_fan);
	state->intake_rpm = state->rpm;

 do_set_fans:
	DBG("** CPU %d RPM: %d overtemp: %d\n",
	    state->index, (int)state->rpm, state->overtemp);

	/* We should check for errors, shouldn't we ? But then, what
	 * do we do once the error occurs ? For FCU notified fan
	 * failures (-EFAULT) we probably want to notify userland
	 * some way...
	 */
	if (state->index == 0) {
		set_rpm_fan(CPU_A1_FAN_RPM_INDEX, state->rpm);
		set_rpm_fan(CPU_A2_FAN_RPM_INDEX, state->rpm);
		set_rpm_fan(CPU_A3_FAN_RPM_INDEX, state->rpm);
	} else {
		set_rpm_fan(CPU_B1_FAN_RPM_INDEX, state->rpm);
		set_rpm_fan(CPU_B2_FAN_RPM_INDEX, state->rpm);
		set_rpm_fan(CPU_B3_FAN_RPM_INDEX, state->rpm);
	}
}

/*
 * Initialize the state structure for one CPU control loop
 */
static int init_cpu_state(struct cpu_pid_state *state, int index)
{
	int err;

	state->index = index;
	state->first = 1;
	state->rpm = (cpu_pid_type == CPU_PID_TYPE_RACKMAC) ? 4000 : 1000;
	state->overtemp = 0;
	state->adc_config = 0x00;


	if (index == 0)
		state->monitor = attach_i2c_chip(SUPPLY_MONITOR_ID, "CPU0_monitor");
	else if (index == 1)
		state->monitor = attach_i2c_chip(SUPPLY_MONITORB_ID, "CPU1_monitor");
	if (state->monitor == NULL)
		goto fail;

	if (read_eeprom(index, &state->mpu))
		goto fail;

	state->count_power = state->mpu.tguardband;
	if (state->count_power > CPU_POWER_HISTORY_SIZE) {
		printk(KERN_WARNING "Warning ! too many power history slots\n");
		state->count_power = CPU_POWER_HISTORY_SIZE;
	}
	DBG("CPU %d Using %d power history entries\n", index, state->count_power);

	if (index == 0) {
		err = device_create_file(&of_dev->dev, &dev_attr_cpu0_temperature);
		err |= device_create_file(&of_dev->dev, &dev_attr_cpu0_voltage);
		err |= device_create_file(&of_dev->dev, &dev_attr_cpu0_current);
		err |= device_create_file(&of_dev->dev, &dev_attr_cpu0_exhaust_fan_rpm);
		err |= device_create_file(&of_dev->dev, &dev_attr_cpu0_intake_fan_rpm);
	} else {
		err = device_create_file(&of_dev->dev, &dev_attr_cpu1_temperature);
		err |= device_create_file(&of_dev->dev, &dev_attr_cpu1_voltage);
		err |= device_create_file(&of_dev->dev, &dev_attr_cpu1_current);
		err |= device_create_file(&of_dev->dev, &dev_attr_cpu1_exhaust_fan_rpm);
		err |= device_create_file(&of_dev->dev, &dev_attr_cpu1_intake_fan_rpm);
	}
	if (err)
		printk(KERN_WARNING "Failed to create some of the atribute"
			"files for CPU %d\n", index);

	return 0;
 fail:
	state->monitor = NULL;
	
	return -ENODEV;
}

/*
 * Dispose of the state data for one CPU control loop
 */
static void dispose_cpu_state(struct cpu_pid_state *state)
{
	if (state->monitor == NULL)
		return;

	if (state->index == 0) {
		device_remove_file(&of_dev->dev, &dev_attr_cpu0_temperature);
		device_remove_file(&of_dev->dev, &dev_attr_cpu0_voltage);
		device_remove_file(&of_dev->dev, &dev_attr_cpu0_current);
		device_remove_file(&of_dev->dev, &dev_attr_cpu0_exhaust_fan_rpm);
		device_remove_file(&of_dev->dev, &dev_attr_cpu0_intake_fan_rpm);
	} else {
		device_remove_file(&of_dev->dev, &dev_attr_cpu1_temperature);
		device_remove_file(&of_dev->dev, &dev_attr_cpu1_voltage);
		device_remove_file(&of_dev->dev, &dev_attr_cpu1_current);
		device_remove_file(&of_dev->dev, &dev_attr_cpu1_exhaust_fan_rpm);
		device_remove_file(&of_dev->dev, &dev_attr_cpu1_intake_fan_rpm);
	}

	state->monitor = NULL;
}

/*
 * Motherboard backside & U3 heatsink fan control loop
 */
static void do_monitor_backside(struct backside_pid_state *state)
{
	s32 temp, integral, derivative, fan_min;
	s64 integ_p, deriv_p, prop_p, sum; 
	int i, rc;

	if (--state->ticks != 0)
		return;
	state->ticks = backside_params.interval;

	DBG("backside:\n");

	/* Check fan status */
	rc = get_pwm_fan(BACKSIDE_FAN_PWM_INDEX);
	if (rc < 0) {
		printk(KERN_WARNING "Error %d reading backside fan !\n", rc);
		/* XXX What do we do now ? */
	} else
		state->pwm = rc;
	DBG("  current pwm: %d\n", state->pwm);

	/* Get some sensor readings */
	temp = i2c_smbus_read_byte_data(state->monitor, MAX6690_EXT_TEMP) << 16;
	state->last_temp = temp;
	DBG("  temp: %d.%03d, target: %d.%03d\n", FIX32TOPRINT(temp),
	    FIX32TOPRINT(backside_params.input_target));

	/* Store temperature and error in history array */
	state->cur_sample = (state->cur_sample + 1) % BACKSIDE_PID_HISTORY_SIZE;
	state->sample_history[state->cur_sample] = temp;
	state->error_history[state->cur_sample] = temp - backside_params.input_target;
	
	/* If first loop, fill the history table */
	if (state->first) {
		for (i = 0; i < (BACKSIDE_PID_HISTORY_SIZE - 1); i++) {
			state->cur_sample = (state->cur_sample + 1) %
				BACKSIDE_PID_HISTORY_SIZE;
			state->sample_history[state->cur_sample] = temp;
			state->error_history[state->cur_sample] =
				temp - backside_params.input_target;
		}
		state->first = 0;
	}

	/* Calculate the integral term */
	sum = 0;
	integral = 0;
	for (i = 0; i < BACKSIDE_PID_HISTORY_SIZE; i++)
		integral += state->error_history[i];
	integral *= backside_params.interval;
	DBG("  integral: %08x\n", integral);
	integ_p = ((s64)backside_params.G_r) * (s64)integral;
	DBG("   integ_p: %d\n", (int)(integ_p >> 36));
	sum += integ_p;

	/* Calculate the derivative term */
	derivative = state->error_history[state->cur_sample] -
		state->error_history[(state->cur_sample + BACKSIDE_PID_HISTORY_SIZE - 1)
				    % BACKSIDE_PID_HISTORY_SIZE];
	derivative /= backside_params.interval;
	deriv_p = ((s64)backside_params.G_d) * (s64)derivative;
	DBG("   deriv_p: %d\n", (int)(deriv_p >> 36));
	sum += deriv_p;

	/* Calculate the proportional term */
	prop_p = ((s64)backside_params.G_p) * (s64)(state->error_history[state->cur_sample]);
	DBG("   prop_p: %d\n", (int)(prop_p >> 36));
	sum += prop_p;

	/* Scale sum */
	sum >>= 36;

	DBG("   sum: %d\n", (int)sum);
	if (backside_params.additive)
		state->pwm += (s32)sum;
	else
		state->pwm = sum;

	/* Check for clamp */
	fan_min = (dimm_output_clamp * 100) / 14000;
	fan_min = max(fan_min, backside_params.output_min);

	state->pwm = max(state->pwm, fan_min);
	state->pwm = min(state->pwm, backside_params.output_max);

	DBG("** BACKSIDE PWM: %d\n", (int)state->pwm);
	set_pwm_fan(BACKSIDE_FAN_PWM_INDEX, state->pwm);
}

/*
 * Initialize the state structure for the backside fan control loop
 */
static int init_backside_state(struct backside_pid_state *state)
{
	struct device_node *u3;
	int u3h = 1; /* conservative by default */
	int err;

	/*
	 * There are different PID params for machines with U3 and machines
	 * with U3H, pick the right ones now
	 */
	u3 = of_find_node_by_path("/u3@0,f8000000");
	if (u3 != NULL) {
		const u32 *vers = of_get_property(u3, "device-rev", NULL);
		if (vers)
			if (((*vers) & 0x3f) < 0x34)
				u3h = 0;
		of_node_put(u3);
	}

	if (rackmac) {
		backside_params.G_d = BACKSIDE_PID_RACK_G_d;
		backside_params.input_target = BACKSIDE_PID_RACK_INPUT_TARGET;
		backside_params.output_min = BACKSIDE_PID_U3H_OUTPUT_MIN;
		backside_params.interval = BACKSIDE_PID_RACK_INTERVAL;
		backside_params.G_p = BACKSIDE_PID_RACK_G_p;
		backside_params.G_r = BACKSIDE_PID_G_r;
		backside_params.output_max = BACKSIDE_PID_OUTPUT_MAX;
		backside_params.additive = 0;
	} else if (u3h) {
		backside_params.G_d = BACKSIDE_PID_U3H_G_d;
		backside_params.input_target = BACKSIDE_PID_U3H_INPUT_TARGET;
		backside_params.output_min = BACKSIDE_PID_U3H_OUTPUT_MIN;
		backside_params.interval = BACKSIDE_PID_INTERVAL;
		backside_params.G_p = BACKSIDE_PID_G_p;
		backside_params.G_r = BACKSIDE_PID_G_r;
		backside_params.output_max = BACKSIDE_PID_OUTPUT_MAX;
		backside_params.additive = 1;
	} else {
		backside_params.G_d = BACKSIDE_PID_U3_G_d;
		backside_params.input_target = BACKSIDE_PID_U3_INPUT_TARGET;
		backside_params.output_min = BACKSIDE_PID_U3_OUTPUT_MIN;
		backside_params.interval = BACKSIDE_PID_INTERVAL;
		backside_params.G_p = BACKSIDE_PID_G_p;
		backside_params.G_r = BACKSIDE_PID_G_r;
		backside_params.output_max = BACKSIDE_PID_OUTPUT_MAX;
		backside_params.additive = 1;
	}

	state->ticks = 1;
	state->first = 1;
	state->pwm = 50;

	state->monitor = attach_i2c_chip(BACKSIDE_MAX_ID, "backside_temp");
	if (state->monitor == NULL)
		return -ENODEV;

	err = device_create_file(&of_dev->dev, &dev_attr_backside_temperature);
	err |= device_create_file(&of_dev->dev, &dev_attr_backside_fan_pwm);
	if (err)
		printk(KERN_WARNING "Failed to create attribute file(s)"
			" for backside fan\n");

	return 0;
}

/*
 * Dispose of the state data for the backside control loop
 */
static void dispose_backside_state(struct backside_pid_state *state)
{
	if (state->monitor == NULL)
		return;

	device_remove_file(&of_dev->dev, &dev_attr_backside_temperature);
	device_remove_file(&of_dev->dev, &dev_attr_backside_fan_pwm);

	state->monitor = NULL;
}
 
/*
 * Drives bay fan control loop
 */
static void do_monitor_drives(struct drives_pid_state *state)
{
	s32 temp, integral, derivative;
	s64 integ_p, deriv_p, prop_p, sum; 
	int i, rc;

	if (--state->ticks != 0)
		return;
	state->ticks = DRIVES_PID_INTERVAL;

	DBG("drives:\n");

	/* Check fan status */
	rc = get_rpm_fan(DRIVES_FAN_RPM_INDEX, !RPM_PID_USE_ACTUAL_SPEED);
	if (rc < 0) {
		printk(KERN_WARNING "Error %d reading drives fan !\n", rc);
		/* XXX What do we do now ? */
	} else
		state->rpm = rc;
	DBG("  current rpm: %d\n", state->rpm);

	/* Get some sensor readings */
	temp = le16_to_cpu(i2c_smbus_read_word_data(state->monitor,
						    DS1775_TEMP)) << 8;
	state->last_temp = temp;
	DBG("  temp: %d.%03d, target: %d.%03d\n", FIX32TOPRINT(temp),
	    FIX32TOPRINT(DRIVES_PID_INPUT_TARGET));

	/* Store temperature and error in history array */
	state->cur_sample = (state->cur_sample + 1) % DRIVES_PID_HISTORY_SIZE;
	state->sample_history[state->cur_sample] = temp;
	state->error_history[state->cur_sample] = temp - DRIVES_PID_INPUT_TARGET;
	
	/* If first loop, fill the history table */
	if (state->first) {
		for (i = 0; i < (DRIVES_PID_HISTORY_SIZE - 1); i++) {
			state->cur_sample = (state->cur_sample + 1) %
				DRIVES_PID_HISTORY_SIZE;
			state->sample_history[state->cur_sample] = temp;
			state->error_history[state->cur_sample] =
				temp - DRIVES_PID_INPUT_TARGET;
		}
		state->first = 0;
	}

	/* Calculate the integral term */
	sum = 0;
	integral = 0;
	for (i = 0; i < DRIVES_PID_HISTORY_SIZE; i++)
		integral += state->error_history[i];
	integral *= DRIVES_PID_INTERVAL;
	DBG("  integral: %08x\n", integral);
	integ_p = ((s64)DRIVES_PID_G_r) * (s64)integral;
	DBG("   integ_p: %d\n", (int)(integ_p >> 36));
	sum += integ_p;

	/* Calculate the derivative term */
	derivative = state->error_history[state->cur_sample] -
		state->error_history[(state->cur_sample + DRIVES_PID_HISTORY_SIZE - 1)
				    % DRIVES_PID_HISTORY_SIZE];
	derivative /= DRIVES_PID_INTERVAL;
	deriv_p = ((s64)DRIVES_PID_G_d) * (s64)derivative;
	DBG("   deriv_p: %d\n", (int)(deriv_p >> 36));
	sum += deriv_p;

	/* Calculate the proportional term */
	prop_p = ((s64)DRIVES_PID_G_p) * (s64)(state->error_history[state->cur_sample]);
	DBG("   prop_p: %d\n", (int)(prop_p >> 36));
	sum += prop_p;

	/* Scale sum */
	sum >>= 36;

	DBG("   sum: %d\n", (int)sum);
	state->rpm += (s32)sum;

	state->rpm = max(state->rpm, DRIVES_PID_OUTPUT_MIN);
	state->rpm = min(state->rpm, DRIVES_PID_OUTPUT_MAX);

	DBG("** DRIVES RPM: %d\n", (int)state->rpm);
	set_rpm_fan(DRIVES_FAN_RPM_INDEX, state->rpm);
}

/*
 * Initialize the state structure for the drives bay fan control loop
 */
static int init_drives_state(struct drives_pid_state *state)
{
	int err;

	state->ticks = 1;
	state->first = 1;
	state->rpm = 1000;

	state->monitor = attach_i2c_chip(DRIVES_DALLAS_ID, "drives_temp");
	if (state->monitor == NULL)
		return -ENODEV;

	err = device_create_file(&of_dev->dev, &dev_attr_drives_temperature);
	err |= device_create_file(&of_dev->dev, &dev_attr_drives_fan_rpm);
	if (err)
		printk(KERN_WARNING "Failed to create attribute file(s)"
			" for drives bay fan\n");

	return 0;
}

/*
 * Dispose of the state data for the drives control loop
 */
static void dispose_drives_state(struct drives_pid_state *state)
{
	if (state->monitor == NULL)
		return;

	device_remove_file(&of_dev->dev, &dev_attr_drives_temperature);
	device_remove_file(&of_dev->dev, &dev_attr_drives_fan_rpm);

	state->monitor = NULL;
}

/*
 * DIMMs temp control loop
 */
static void do_monitor_dimms(struct dimm_pid_state *state)
{
	s32 temp, integral, derivative, fan_min;
	s64 integ_p, deriv_p, prop_p, sum;
	int i;

	if (--state->ticks != 0)
		return;
	state->ticks = DIMM_PID_INTERVAL;

	DBG("DIMM:\n");

	DBG("  current value: %d\n", state->output);

	temp = read_lm87_reg(state->monitor, LM87_INT_TEMP);
	if (temp < 0)
		return;
	temp <<= 16;
	state->last_temp = temp;
	DBG("  temp: %d.%03d, target: %d.%03d\n", FIX32TOPRINT(temp),
	    FIX32TOPRINT(DIMM_PID_INPUT_TARGET));

	/* Store temperature and error in history array */
	state->cur_sample = (state->cur_sample + 1) % DIMM_PID_HISTORY_SIZE;
	state->sample_history[state->cur_sample] = temp;
	state->error_history[state->cur_sample] = temp - DIMM_PID_INPUT_TARGET;

	/* If first loop, fill the history table */
	if (state->first) {
		for (i = 0; i < (DIMM_PID_HISTORY_SIZE - 1); i++) {
			state->cur_sample = (state->cur_sample + 1) %
				DIMM_PID_HISTORY_SIZE;
			state->sample_history[state->cur_sample] = temp;
			state->error_history[state->cur_sample] =
				temp - DIMM_PID_INPUT_TARGET;
		}
		state->first = 0;
	}

	/* Calculate the integral term */
	sum = 0;
	integral = 0;
	for (i = 0; i < DIMM_PID_HISTORY_SIZE; i++)
		integral += state->error_history[i];
	integral *= DIMM_PID_INTERVAL;
	DBG("  integral: %08x\n", integral);
	integ_p = ((s64)DIMM_PID_G_r) * (s64)integral;
	DBG("   integ_p: %d\n", (int)(integ_p >> 36));
	sum += integ_p;

	/* Calculate the derivative term */
	derivative = state->error_history[state->cur_sample] -
		state->error_history[(state->cur_sample + DIMM_PID_HISTORY_SIZE - 1)
				    % DIMM_PID_HISTORY_SIZE];
	derivative /= DIMM_PID_INTERVAL;
	deriv_p = ((s64)DIMM_PID_G_d) * (s64)derivative;
	DBG("   deriv_p: %d\n", (int)(deriv_p >> 36));
	sum += deriv_p;

	/* Calculate the proportional term */
	prop_p = ((s64)DIMM_PID_G_p) * (s64)(state->error_history[state->cur_sample]);
	DBG("   prop_p: %d\n", (int)(prop_p >> 36));
	sum += prop_p;

	/* Scale sum */
	sum >>= 36;

	DBG("   sum: %d\n", (int)sum);
	state->output = (s32)sum;
	state->output = max(state->output, DIMM_PID_OUTPUT_MIN);
	state->output = min(state->output, DIMM_PID_OUTPUT_MAX);
	dimm_output_clamp = state->output;

	DBG("** DIMM clamp value: %d\n", (int)state->output);

	/* Backside PID is only every 5 seconds, force backside fan clamping now */
	fan_min = (dimm_output_clamp * 100) / 14000;
	fan_min = max(fan_min, backside_params.output_min);
	if (backside_state.pwm < fan_min) {
		backside_state.pwm = fan_min;
		DBG(" -> applying clamp to backside fan now: %d  !\n", fan_min);
		set_pwm_fan(BACKSIDE_FAN_PWM_INDEX, fan_min);
	}
}

/*
 * Initialize the state structure for the DIMM temp control loop
 */
static int init_dimms_state(struct dimm_pid_state *state)
{
	state->ticks = 1;
	state->first = 1;
	state->output = 4000;

	state->monitor = attach_i2c_chip(XSERVE_DIMMS_LM87, "dimms_temp");
	if (state->monitor == NULL)
		return -ENODEV;

	if (device_create_file(&of_dev->dev, &dev_attr_dimms_temperature))
		printk(KERN_WARNING "Failed to create attribute file"
			" for DIMM temperature\n");

	return 0;
}

/*
 * Dispose of the state data for the DIMM control loop
 */
static void dispose_dimms_state(struct dimm_pid_state *state)
{
	if (state->monitor == NULL)
		return;

	device_remove_file(&of_dev->dev, &dev_attr_dimms_temperature);

	state->monitor = NULL;
}

/*
 * Slots fan control loop
 */
static void do_monitor_slots(struct slots_pid_state *state)
{
	s32 temp, integral, derivative;
	s64 integ_p, deriv_p, prop_p, sum;
	int i, rc;

	if (--state->ticks != 0)
		return;
	state->ticks = SLOTS_PID_INTERVAL;

	DBG("slots:\n");

	/* Check fan status */
	rc = get_pwm_fan(SLOTS_FAN_PWM_INDEX);
	if (rc < 0) {
		printk(KERN_WARNING "Error %d reading slots fan !\n", rc);
		/* XXX What do we do now ? */
	} else
		state->pwm = rc;
	DBG("  current pwm: %d\n", state->pwm);

	/* Get some sensor readings */
	temp = le16_to_cpu(i2c_smbus_read_word_data(state->monitor,
						    DS1775_TEMP)) << 8;
	state->last_temp = temp;
	DBG("  temp: %d.%03d, target: %d.%03d\n", FIX32TOPRINT(temp),
	    FIX32TOPRINT(SLOTS_PID_INPUT_TARGET));

	/* Store temperature and error in history array */
	state->cur_sample = (state->cur_sample + 1) % SLOTS_PID_HISTORY_SIZE;
	state->sample_history[state->cur_sample] = temp;
	state->error_history[state->cur_sample] = temp - SLOTS_PID_INPUT_TARGET;

	/* If first loop, fill the history table */
	if (state->first) {
		for (i = 0; i < (SLOTS_PID_HISTORY_SIZE - 1); i++) {
			state->cur_sample = (state->cur_sample + 1) %
				SLOTS_PID_HISTORY_SIZE;
			state->sample_history[state->cur_sample] = temp;
			state->error_history[state->cur_sample] =
				temp - SLOTS_PID_INPUT_TARGET;
		}
		state->first = 0;
	}

	/* Calculate the integral term */
	sum = 0;
	integral = 0;
	for (i = 0; i < SLOTS_PID_HISTORY_SIZE; i++)
		integral += state->error_history[i];
	integral *= SLOTS_PID_INTERVAL;
	DBG("  integral: %08x\n", integral);
	integ_p = ((s64)SLOTS_PID_G_r) * (s64)integral;
	DBG("   integ_p: %d\n", (int)(integ_p >> 36));
	sum += integ_p;

	/* Calculate the derivative term */
	derivative = state->error_history[state->cur_sample] -
		state->error_history[(state->cur_sample + SLOTS_PID_HISTORY_SIZE - 1)
				    % SLOTS_PID_HISTORY_SIZE];
	derivative /= SLOTS_PID_INTERVAL;
	deriv_p = ((s64)SLOTS_PID_G_d) * (s64)derivative;
	DBG("   deriv_p: %d\n", (int)(deriv_p >> 36));
	sum += deriv_p;

	/* Calculate the proportional term */
	prop_p = ((s64)SLOTS_PID_G_p) * (s64)(state->error_history[state->cur_sample]);
	DBG("   prop_p: %d\n", (int)(prop_p >> 36));
	sum += prop_p;

	/* Scale sum */
	sum >>= 36;

	DBG("   sum: %d\n", (int)sum);
	state->pwm = (s32)sum;

	state->pwm = max(state->pwm, SLOTS_PID_OUTPUT_MIN);
	state->pwm = min(state->pwm, SLOTS_PID_OUTPUT_MAX);

	DBG("** DRIVES PWM: %d\n", (int)state->pwm);
	set_pwm_fan(SLOTS_FAN_PWM_INDEX, state->pwm);
}

/*
 * Initialize the state structure for the slots bay fan control loop
 */
static int init_slots_state(struct slots_pid_state *state)
{
	int err;

	state->ticks = 1;
	state->first = 1;
	state->pwm = 50;

	state->monitor = attach_i2c_chip(XSERVE_SLOTS_LM75, "slots_temp");
	if (state->monitor == NULL)
		return -ENODEV;

	err = device_create_file(&of_dev->dev, &dev_attr_slots_temperature);
	err |= device_create_file(&of_dev->dev, &dev_attr_slots_fan_pwm);
	if (err)
		printk(KERN_WARNING "Failed to create attribute file(s)"
			" for slots bay fan\n");

	return 0;
}

/*
 * Dispose of the state data for the slots control loop
 */
static void dispose_slots_state(struct slots_pid_state *state)
{
	if (state->monitor == NULL)
		return;

	device_remove_file(&of_dev->dev, &dev_attr_slots_temperature);
	device_remove_file(&of_dev->dev, &dev_attr_slots_fan_pwm);

	state->monitor = NULL;
}


static int call_critical_overtemp(void)
{
	char *argv[] = { critical_overtemp_path, NULL };
	static char *envp[] = { "HOME=/",
				"TERM=linux",
				"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
				NULL };

	return call_usermodehelper(critical_overtemp_path,
				   argv, envp, UMH_WAIT_EXEC);
}


/*
 * Here's the kernel thread that calls the various control loops
 */
static int main_control_loop(void *x)
{
	DBG("main_control_loop started\n");

	mutex_lock(&driver_lock);

	if (start_fcu() < 0) {
		printk(KERN_ERR "kfand: failed to start FCU\n");
		mutex_unlock(&driver_lock);
		goto out;
	}

	/* Set the PCI fan once for now on non-RackMac */
	if (!rackmac)
		set_pwm_fan(SLOTS_FAN_PWM_INDEX, SLOTS_FAN_DEFAULT_PWM);

	/* Initialize ADCs */
	initialize_adc(&cpu_state[0]);
	if (cpu_state[1].monitor != NULL)
		initialize_adc(&cpu_state[1]);

	fcu_tickle_ticks = FCU_TICKLE_TICKS;

	mutex_unlock(&driver_lock);

	while (state == state_attached) {
		unsigned long elapsed, start;

		start = jiffies;

		mutex_lock(&driver_lock);

		/* Tickle the FCU just in case */
		if (--fcu_tickle_ticks < 0) {
			fcu_tickle_ticks = FCU_TICKLE_TICKS;
			tickle_fcu();
		}

		/* First, we always calculate the new DIMMs state on an Xserve */
		if (rackmac)
			do_monitor_dimms(&dimms_state);

		/* Then, the CPUs */
		if (cpu_pid_type == CPU_PID_TYPE_COMBINED)
			do_monitor_cpu_combined();
		else if (cpu_pid_type == CPU_PID_TYPE_RACKMAC) {
			do_monitor_cpu_rack(&cpu_state[0]);
			if (cpu_state[1].monitor != NULL)
				do_monitor_cpu_rack(&cpu_state[1]);
			// better deal with UP
		} else {
			do_monitor_cpu_split(&cpu_state[0]);
			if (cpu_state[1].monitor != NULL)
				do_monitor_cpu_split(&cpu_state[1]);
			// better deal with UP
		}
		/* Then, the rest */
		do_monitor_backside(&backside_state);
		if (rackmac)
			do_monitor_slots(&slots_state);
		else
			do_monitor_drives(&drives_state);
		mutex_unlock(&driver_lock);

		if (critical_state == 1) {
			printk(KERN_WARNING "Temperature control detected a critical condition\n");
			printk(KERN_WARNING "Attempting to shut down...\n");
			if (call_critical_overtemp()) {
				printk(KERN_WARNING "Can't call %s, power off now!\n",
				       critical_overtemp_path);
				machine_power_off();
			}
		}
		if (critical_state > 0)
			critical_state++;
		if (critical_state > MAX_CRITICAL_STATE) {
			printk(KERN_WARNING "Shutdown timed out, power off now !\n");
			machine_power_off();
		}

		// FIXME: Deal with signals
		elapsed = jiffies - start;
		if (elapsed < HZ)
			schedule_timeout_interruptible(HZ - elapsed);
	}

 out:
	DBG("main_control_loop ended\n");

	ctrl_task = 0;
	complete_and_exit(&ctrl_complete, 0);
}

/*
 * Dispose the control loops when tearing down
 */
static void dispose_control_loops(void)
{
	dispose_cpu_state(&cpu_state[0]);
	dispose_cpu_state(&cpu_state[1]);
	dispose_backside_state(&backside_state);
	dispose_drives_state(&drives_state);
	dispose_slots_state(&slots_state);
	dispose_dimms_state(&dimms_state);
}

/*
 * Create the control loops. U3-0 i2c bus is up, so we can now
 * get to the various sensors
 */
static int create_control_loops(void)
{
	struct device_node *np;

	/* Count CPUs from the device-tree, we don't care how many are
	 * actually used by Linux
	 */
	cpu_count = 0;
	for (np = NULL; NULL != (np = of_find_node_by_type(np, "cpu"));)
		cpu_count++;

	DBG("counted %d CPUs in the device-tree\n", cpu_count);

	/* Decide the type of PID algorithm to use based on the presence of
	 * the pumps, though that may not be the best way, that is good enough
	 * for now
	 */
	if (rackmac)
		cpu_pid_type = CPU_PID_TYPE_RACKMAC;
	else if (of_machine_is_compatible("PowerMac7,3")
	    && (cpu_count > 1)
	    && fcu_fans[CPUA_PUMP_RPM_INDEX].id != FCU_FAN_ABSENT_ID
	    && fcu_fans[CPUB_PUMP_RPM_INDEX].id != FCU_FAN_ABSENT_ID) {
		printk(KERN_INFO "Liquid cooling pumps detected, using new algorithm !\n");
		cpu_pid_type = CPU_PID_TYPE_COMBINED;
	} else
		cpu_pid_type = CPU_PID_TYPE_SPLIT;

	/* Create control loops for everything. If any fail, everything
	 * fails
	 */
	if (init_cpu_state(&cpu_state[0], 0))
		goto fail;
	if (cpu_pid_type == CPU_PID_TYPE_COMBINED)
		fetch_cpu_pumps_minmax();

	if (cpu_count > 1 && init_cpu_state(&cpu_state[1], 1))
		goto fail;
	if (init_backside_state(&backside_state))
		goto fail;
	if (rackmac && init_dimms_state(&dimms_state))
		goto fail;
	if (rackmac && init_slots_state(&slots_state))
		goto fail;
	if (!rackmac && init_drives_state(&drives_state))
		goto fail;

	DBG("all control loops up !\n");

	return 0;
	
 fail:
	DBG("failure creating control loops, disposing\n");

	dispose_control_loops();

	return -ENODEV;
}

/*
 * Start the control loops after everything is up, that is create
 * the thread that will make them run
 */
static void start_control_loops(void)
{
	init_completion(&ctrl_complete);

	ctrl_task = kthread_run(main_control_loop, NULL, "kfand");
}

/*
 * Stop the control loops when tearing down
 */
static void stop_control_loops(void)
{
	if (ctrl_task)
		wait_for_completion(&ctrl_complete);
}

/*
 * Attach to the i2c FCU after detecting U3-1 bus
 */
static int attach_fcu(void)
{
	fcu = attach_i2c_chip(FAN_CTRLER_ID, "fcu");
	if (fcu == NULL)
		return -ENODEV;

	DBG("FCU attached\n");

	return 0;
}

/*
 * Detach from the i2c FCU when tearing down
 */
static void detach_fcu(void)
{
	fcu = NULL;
}

/*
 * Attach to the i2c controller. We probe the various chips based
 * on the device-tree nodes and build everything for the driver to
 * run, we then kick the driver monitoring thread
 */
static int therm_pm72_attach(struct i2c_adapter *adapter)
{
	mutex_lock(&driver_lock);

	/* Check state */
	if (state == state_detached)
		state = state_attaching;
	if (state != state_attaching) {
		mutex_unlock(&driver_lock);
		return 0;
	}

	/* Check if we are looking for one of these */
	if (u3_0 == NULL && !strcmp(adapter->name, "u3 0")) {
		u3_0 = adapter;
		DBG("found U3-0\n");
		if (k2 || !rackmac)
			if (create_control_loops())
				u3_0 = NULL;
	} else if (u3_1 == NULL && !strcmp(adapter->name, "u3 1")) {
		u3_1 = adapter;
		DBG("found U3-1, attaching FCU\n");
		if (attach_fcu())
			u3_1 = NULL;
	} else if (k2 == NULL && !strcmp(adapter->name, "mac-io 0")) {
		k2 = adapter;
		DBG("Found K2\n");
		if (u3_0 && rackmac)
			if (create_control_loops())
				k2 = NULL;
	}
	/* We got all we need, start control loops */
	if (u3_0 != NULL && u3_1 != NULL && (k2 || !rackmac)) {
		DBG("everything up, starting control loops\n");
		state = state_attached;
		start_control_loops();
	}
	mutex_unlock(&driver_lock);

	return 0;
}

static int therm_pm72_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	/* Always succeed, the real work was done in therm_pm72_attach() */
	return 0;
}

/*
 * Called when any of the devices which participates into thermal management
 * is going away.
 */
static int therm_pm72_remove(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;

	mutex_lock(&driver_lock);

	if (state != state_detached)
		state = state_detaching;

	/* Stop control loops if any */
	DBG("stopping control loops\n");
	mutex_unlock(&driver_lock);
	stop_control_loops();
	mutex_lock(&driver_lock);

	if (u3_0 != NULL && !strcmp(adapter->name, "u3 0")) {
		DBG("lost U3-0, disposing control loops\n");
		dispose_control_loops();
		u3_0 = NULL;
	}
	
	if (u3_1 != NULL && !strcmp(adapter->name, "u3 1")) {
		DBG("lost U3-1, detaching FCU\n");
		detach_fcu();
		u3_1 = NULL;
	}
	if (u3_0 == NULL && u3_1 == NULL)
		state = state_detached;

	mutex_unlock(&driver_lock);

	return 0;
}

/*
 * i2c_driver structure to attach to the host i2c controller
 */

static const struct i2c_device_id therm_pm72_id[] = {
	/*
	 * Fake device name, thermal management is done by several
	 * chips but we don't need to differentiate between them at
	 * this point.
	 */
	{ "therm_pm72", 0 },
	{ }
};

static struct i2c_driver therm_pm72_driver = {
	.driver = {
		.name	= "therm_pm72",
	},
	.attach_adapter	= therm_pm72_attach,
	.probe		= therm_pm72_probe,
	.remove		= therm_pm72_remove,
	.id_table	= therm_pm72_id,
};

static int fan_check_loc_match(const char *loc, int fan)
{
	char	tmp[64];
	char	*c, *e;

	strlcpy(tmp, fcu_fans[fan].loc, 64);

	c = tmp;
	for (;;) {
		e = strchr(c, ',');
		if (e)
			*e = 0;
		if (strcmp(loc, c) == 0)
			return 1;
		if (e == NULL)
			break;
		c = e + 1;
	}
	return 0;
}

static void fcu_lookup_fans(struct device_node *fcu_node)
{
	struct device_node *np = NULL;
	int i;

	/* The table is filled by default with values that are suitable
	 * for the old machines without device-tree informations. We scan
	 * the device-tree and override those values with whatever is
	 * there
	 */

	DBG("Looking up FCU controls in device-tree...\n");

	while ((np = of_get_next_child(fcu_node, np)) != NULL) {
		int type = -1;
		const char *loc;
		const u32 *reg;

		DBG(" control: %s, type: %s\n", np->name, np->type);

		/* Detect control type */
		if (!strcmp(np->type, "fan-rpm-control") ||
		    !strcmp(np->type, "fan-rpm"))
			type = FCU_FAN_RPM;
		if (!strcmp(np->type, "fan-pwm-control") ||
		    !strcmp(np->type, "fan-pwm"))
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

		for (i = 0; i < FCU_FAN_COUNT; i++) {
			int fan_id;

			if (!fan_check_loc_match(loc, i))
				continue;
			DBG(" location match, index: %d\n", i);
			fcu_fans[i].id = FCU_FAN_ABSENT_ID;
			if (type != fcu_fans[i].type) {
				printk(KERN_WARNING "therm_pm72: Fan type mismatch "
				       "in device-tree for %s\n", np->full_name);
				break;
			}
			if (type == FCU_FAN_RPM)
				fan_id = ((*reg) - 0x10) / 2;
			else
				fan_id = ((*reg) - 0x30) / 2;
			if (fan_id > 7) {
				printk(KERN_WARNING "therm_pm72: Can't parse "
				       "fan ID in device-tree for %s\n", np->full_name);
				break;
			}
			DBG(" fan id -> %d, type -> %d\n", fan_id, type);
			fcu_fans[i].id = fan_id;
		}
	}

	/* Now dump the array */
	printk(KERN_INFO "Detected fan controls:\n");
	for (i = 0; i < FCU_FAN_COUNT; i++) {
		if (fcu_fans[i].id == FCU_FAN_ABSENT_ID)
			continue;
		printk(KERN_INFO "  %d: %s fan, id %d, location: %s\n", i,
		       fcu_fans[i].type == FCU_FAN_RPM ? "RPM" : "PWM",
		       fcu_fans[i].id, fcu_fans[i].loc);
	}
}

static int fcu_of_probe(struct platform_device* dev, const struct of_device_id *match)
{
	state = state_detached;
	of_dev = dev;

	dev_info(&dev->dev, "PowerMac G5 Thermal control driver %s\n", VERSION);

	/* Lookup the fans in the device tree */
	fcu_lookup_fans(dev->dev.of_node);

	/* Add the driver */
	return i2c_add_driver(&therm_pm72_driver);
}

static int fcu_of_remove(struct platform_device* dev)
{
	i2c_del_driver(&therm_pm72_driver);

	return 0;
}

static const struct of_device_id fcu_match[] = 
{
	{
	.type		= "fcu",
	},
	{},
};
MODULE_DEVICE_TABLE(of, fcu_match);

static struct of_platform_driver fcu_of_platform_driver = 
{
	.driver = {
		.name = "temperature",
		.owner = THIS_MODULE,
		.of_match_table = fcu_match,
	},
	.probe		= fcu_of_probe,
	.remove		= fcu_of_remove
};

/*
 * Check machine type, attach to i2c controller
 */
static int __init therm_pm72_init(void)
{
	rackmac = of_machine_is_compatible("RackMac3,1");

	if (!of_machine_is_compatible("PowerMac7,2") &&
	    !of_machine_is_compatible("PowerMac7,3") &&
	    !rackmac)
	    	return -ENODEV;

	return of_register_platform_driver(&fcu_of_platform_driver);
}

static void __exit therm_pm72_exit(void)
{
	of_unregister_platform_driver(&fcu_of_platform_driver);
}

module_init(therm_pm72_init);
module_exit(therm_pm72_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("Driver for Apple's PowerMac G5 thermal control");
MODULE_LICENSE("GPL");

