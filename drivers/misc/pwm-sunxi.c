/* pwm-sunxi.c 
 * 
 * pwm module for sun4i (and others) like cubieboard and pcduino 
 * 
 * (C) Copyright 2013 
 * David H. Wilkins  <dwil...@conecuh.com> 
 * 
 * CHANGELOG:
 * 10.08.2013 - Stefan Voit <stefan.voit@voit-consulting.com>
 * - Added script.bin support for [pwm0_para] and [pwm1_para]: pwm_used, pwm_period, pwm_duty_percent
 * - Added initial setup based on script.bin settings
 * - Removed bug that caused the PWM to pause quickly when changing parameters
 * - Dropped debug/dump functions
 *
 * TODO:
 * - Implement duty_percent=0 to set pwm line to 0 - right now it goes to 100%
 * - Change the script_bin settings loader for pwm_period to allow text based values (100ms, 10hz,...)
 * - Merge h & c file
 * - 
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of 
 * the License, or (at your option) any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.         See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 * MA 02111-1307 USA 
 */ 
 
#include <linux/kobject.h> 
#include <linux/slab.h> 
#include <linux/device.h> 
#include <linux/sysfs.h> 
#include <linux/module.h> 
#include <linux/kernel.h> 
#include <linux/platform_device.h> 
#include <linux/err.h> 
#include <asm/io.h> 
#include <asm/delay.h> 
#include <mach/platform.h> 
#include <linux/pwm.h> 
#include <linux/ctype.h> 
#include <linux/limits.h> 
#include <linux/pwm.h> 
#include <linux/kdev_t.h> 
#include <plat/system.h>
#include <plat/sys_config.h>

#include "pwm-sunxi.h" 
/* 
 * Forward Declarations 
 */ 


#define SUNXI_PWM_DEBUG

//comment to get debug messages
#undef SUNXI_PWM_DEBUG
 
void release_pwm_sunxi(struct kobject *kobj); 
void pwm_setup_available_channels(void ); 
ssize_t pwm_set_mode(unsigned int enable, struct sun4i_pwm_available_channel *chan); 
enum sun4i_pwm_prescale  pwm_get_best_prescale(unsigned long long period); 
unsigned int get_entire_cycles(struct sun4i_pwm_available_channel *chan); 
unsigned int get_active_cycles(struct sun4i_pwm_available_channel *chan); 
unsigned long convert_string_to_microseconds(const char *buf); 
int pwm_set_period_and_duty(struct sun4i_pwm_available_channel *chan); 
void fixup_duty(struct sun4i_pwm_available_channel *chan); 
 
 
static DEFINE_MUTEX(sysfs_lock); 
static struct class pwm_class; 
 
void *PWM_CTRL_REG_BASE = NULL; 
 
 
static struct class_attribute pwm_class_attrs[] = { 
	__ATTR_NULL 
}; 
 
 
static struct class pwm_class = { 
	.name =         "pwm-sunxi", 
	.owner =        THIS_MODULE, 
	.class_attrs =  pwm_class_attrs, 
}; 
 
 
/* 
 * sysfs store / show functions 
 */ 
 
static ssize_t pwm_polarity_show(struct device *dev, struct device_attribute *attr, char *buf); 
static ssize_t pwm_period_show(struct device *dev, struct device_attribute *attr, char *buf); 
static ssize_t pwm_duty_show(struct device *dev, struct device_attribute *attr, char *buf); 
static ssize_t pwm_run_show(struct device *dev,struct device_attribute *attr, char *buf); 
static ssize_t pwm_duty_percent_show(struct device *dev,struct device_attribute *attr, char *buf); 
static ssize_t pwm_pulse_show(struct device *dev,struct device_attribute *attr, char *buf); 
static ssize_t pwm_pin_show(struct device *dev,struct device_attribute *attr, char *buf); 
 
static ssize_t pwm_polarity_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size); 
static ssize_t pwm_period_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size); 
static ssize_t pwm_duty_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size); 
static ssize_t pwm_run_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size); 
static ssize_t pwm_duty_percent_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size); 
static ssize_t pwm_pulse_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size); 
 
static DEVICE_ATTR(polarity, 0644,pwm_polarity_show, pwm_polarity_store); 
static DEVICE_ATTR(period, 0644, pwm_period_show, pwm_period_store); 
static DEVICE_ATTR(duty, 0644,pwm_duty_show, pwm_duty_store); 
static DEVICE_ATTR(run, 0644, pwm_run_show, pwm_run_store); 
static DEVICE_ATTR(duty_percent, 0644, pwm_duty_percent_show, pwm_duty_percent_store); 
static DEVICE_ATTR(pulse, 0644, pwm_pulse_show, pwm_pulse_store); 
static DEVICE_ATTR(pin, 0644, pwm_pin_show, NULL); 
 
static const struct attribute *pwm_attrs[] = { 
	&dev_attr_polarity.attr, 
	&dev_attr_period.attr, 
	&dev_attr_duty.attr, 
	&dev_attr_run.attr, 
	&dev_attr_duty_percent.attr, 
	&dev_attr_pulse.attr, 
	&dev_attr_pin.attr, 
	NULL, 
}; 
 
static const struct attribute_group pwm_attr_group = { 
	.attrs = (struct attribute **) pwm_attrs 
}; 
 
struct device *pwm0; 
struct device *pwm1; 
 
 
static struct sun4i_pwm_available_channel pwm_available_chan[SUN4I_MAX_HARDWARE_PWM_CHANNELS]; 
static int sunxi_pwm_class_registered = 0;

static void __init sunxi_pwm_register_class(void)
{
	if (sunxi_pwm_class_registered)
		return;

	if (class_register(&pwm_class) != 0) {
		pr_err("pwm-sunxi: class_register failed\n");
		return;
	}
	sunxi_pwm_class_registered = 1;
}

static int __init sunxi_pwm_init(void) 
{ 
	int init_enable, init_duty_percent, init_period;
	struct sun4i_pwm_available_channel *chan;
	int err = 0;

	pwm_setup_available_channels(); 

	//PWM 0
	//printk("pwm-sunxi: configuring pwm0...\n");
	chan = &pwm_available_chan[0];
 	
	init_enable=0;
	init_period=0;
	init_duty_percent=100;

	err = script_parser_fetch("pwm0_para", "pwm_used", &init_enable,sizeof(init_enable)/sizeof(int));
	if (err == 0 && init_enable) {
		sunxi_pwm_register_class();
		pwm0 = device_create(&pwm_class, NULL,
				MKDEV(0, 0), &pwm_available_chan[0], "pwm0");
		err = sysfs_create_group(&pwm0->kobj, &pwm_attr_group);
		if (err)
			pr_err("pwm-sunxi: sysfs_create_group(pwm0) err %d\n",
			       err);

		err = script_parser_fetch("pwm0_para", "pwm_period", &init_period,sizeof(init_period)/sizeof(int));
		if (err) {
			pr_err("%s script_parser_fetch '[pwm0_para]' 'pwm_period' err - using 10000\n",	__func__);
			init_period=10000;
		}

		err = script_parser_fetch("pwm0_para", "pwm_duty_percent", &init_duty_percent,sizeof(init_duty_percent)/sizeof(int));
		if (err) {
			pr_err("%s script_parser_fetch '[pwm0_para]' 'pwm_duty_percent' err - using 100\n",	__func__);
			init_duty_percent=100;
		}

		chan->duty_percent=init_duty_percent;
		chan->period = init_period; 
		chan->prescale = pwm_get_best_prescale(init_period); 
		fixup_duty(chan); 
#ifdef SUNXI_PWM_DEBUG
		printk("pwm-sunxi: pwm0 set initial values\n");
#endif
		pwm_set_mode(init_enable,chan); 
		printk("pwm-sunxi: pwm0 configured - period: %ld, duty_percent: %d, duty: %ld\n",
			chan->period, chan->duty_percent, chan->duty);
	}

	if (sunxi_is_sun5i()) /* Only 1 pwm on the A13 / A10s */
		return 0;

	//PWM 1
	//printk("pwm-sunxi: configuring pwm1...\n");
	chan = &pwm_available_chan[1];
 	
	init_enable=0;
	init_period=0;
	init_duty_percent=100;

	err = script_parser_fetch("pwm1_para", "pwm_used", &init_enable,sizeof(init_enable)/sizeof(int));
	if (err == 0 && init_enable) {
		sunxi_pwm_register_class();
		pwm1 = device_create(&pwm_class, NULL,
				MKDEV(0, 0), &pwm_available_chan[1], "pwm1");
		err = sysfs_create_group(&pwm1->kobj, &pwm_attr_group);
		if (err)
			pr_err("pwm-sunxi: sysfs_create_group(pwm1) err %d\n",
			       err);

		err = script_parser_fetch("pwm1_para", "pwm_period", &init_period,sizeof(init_period)/sizeof(int));
		if (err) {
			pr_err("%s script_parser_fetch '[pwm1_para]' 'pwm_period' err - using 10000\n",	__func__);
			init_period=10000;
		}

		err = script_parser_fetch("pwm1_para", "pwm_duty_percent", &init_duty_percent,sizeof(init_duty_percent)/sizeof(int));
		if (err) {
			pr_err("%s script_parser_fetch '[pwm1_para]' 'pwm_duty_percent' err - using 100\n",	__func__);
			init_duty_percent=100;
		}

		chan->duty_percent=init_duty_percent;
		chan->period = init_period; 
		chan->prescale = pwm_get_best_prescale(init_period); 
		fixup_duty(chan); 
#ifdef SUNXI_PWM_DEBUG
		printk("pwm-sunxi: pwm0 set initial values\n");
#endif
		pwm_set_mode(init_enable,chan); 
		printk("pwm-sunxi: pwm1 configured - period: %ld, duty_percent: %d, duty: %ld\n",
			chan->period, chan->duty_percent, chan->duty);
	}
	return 0;
} 

void sunxi_pwm_exit(void) 
{ 
	void *timer_base = ioremap(SW_PA_TIMERC_IO_BASE, 0x400); 
	void *PWM_CTRL_REG_BASE = timer_base + 0x200; 

	if (pwm0) {
		device_destroy(&pwm_class, pwm0->devt);
		writel(pwm_available_chan[0].pin_backup.initializer,
		       pwm_available_chan[0].pin_addr);
	}
	if (pwm1) {
		device_destroy(&pwm_class, pwm1->devt);
		writel(pwm_available_chan[1].pin_backup.initializer,
		       pwm_available_chan[1].pin_addr);
	}

	if (sunxi_pwm_class_registered) {
		writel(0, PWM_CTRL_REG_BASE + 0); 
		class_unregister(&pwm_class);
	}
}

/* 
 * Functions to display the pwm variables currently set 
 */ 
 
static ssize_t pwm_polarity_show(struct device *dev, struct device_attribute *attr, char *buf) { 
	const struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status; 
	switch (chan->channel) { 
	case 0: 
		status = scnprintf(buf,PAGE_SIZE,"%d",chan->ctrl_current.s.ch0_act_state); 
		break; 
	case 1: 
		status = scnprintf(buf,PAGE_SIZE,"%d",chan->ctrl_current.s.ch1_act_state); 
		break; 
	default: 
		status = -EINVAL; 
		break; 
	} 
	return status; 
} 
static ssize_t pwm_period_show(struct device *dev, struct device_attribute *attr, char *buf) { 
	const struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status; 
	status = sprintf(buf,"%lu",chan->period); 
	return status; 
} 
static ssize_t pwm_duty_show(struct device *dev, struct device_attribute *attr, char *buf) { 
	const struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status; 
	status = sprintf(buf,"%lu",chan->duty); 
	return status; 
} 
static ssize_t pwm_run_show(struct device *dev,struct device_attribute *attr, char *buf) { 
	const struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status; 
	switch (chan->channel) { 
	case 0: 
		status = sprintf(buf,"%d",chan->ctrl_current.s.ch0_en); 
		break; 
	case 1: 
		status = sprintf(buf,"%d",chan->ctrl_current.s.ch1_en); 
		break; 
	default: 
		status = -EINVAL; 
		break; 
	} 
 
	return status; 
} 
static ssize_t pwm_duty_percent_show(struct device *dev,struct device_attribute *attr, char *buf) { 
	const struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	return sprintf(buf,"%u",chan->duty_percent); 
} 
static ssize_t pwm_pulse_show(struct device *dev,struct device_attribute *attr, char *buf) { 
	const struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status; 
	switch (chan->channel) { 
	case 0: 
		status = sprintf(buf,"%d",chan->ctrl_current.s.ch0_pulse_start); 
		break; 
	case 1: 
		status = sprintf(buf,"%d",chan->ctrl_current.s.ch1_pulse_start); 
		break; 
	default: 
		status = -EINVAL; 
		break; 
	} 
	return status; 
} 
 
static ssize_t pwm_pin_show(struct device *dev,struct device_attribute *attr, char *buf) { 
	const struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status; 
	status = sprintf(buf,"%s",chan->pin_name); 
 
	return status; 
} 
 
/* 
 * Functions to store values for pwm 
 */ 
 
static ssize_t pwm_polarity_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size) { 
	struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status = -EINVAL; 
	int act_state = 0; 
 
	sscanf(buf,"%d",&act_state); 
	if(act_state < 2) { 
		switch (chan->channel) { 
		case 0: 
			chan->ctrl_current.s.ch0_act_state = act_state; 
			break; 
		case 1: 
			chan->ctrl_current.s.ch1_act_state = act_state; 
			break; 
		default: 
			status = -EINVAL; 
			break; 
		} 
		status = size; 
	} 
	return status; 
} 
static ssize_t pwm_period_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size) { 
	unsigned long long period = 0; 
	struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
 
	period = convert_string_to_microseconds(buf); 
	if(!period || period > ULONG_MAX) { 
		size = -EINVAL; 
	} else { 
		if(period <= chan->duty) { 
			chan->duty = period; 
		} 
		chan->period = period; 
		chan->prescale = pwm_get_best_prescale(period); 
		fixup_duty(chan); 
		if(chan->duty) { 
			pwm_set_mode(NO_ENABLE_CHANGE,chan); 
		} 
	} 
	return size; 
} 
static ssize_t pwm_duty_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size) { 
	unsigned long long duty = 0; 
	struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
 
	/* sscanf(buf,"%Lu",&duty); */ /* L means long long pointer */ 
	duty = convert_string_to_microseconds(buf); 
	duty = duty > ULONG_MAX ? ULONG_MAX : duty; 
	duty = duty > chan->period ? chan->period : duty; 
	chan->duty_percent = -1; /* disable duty_percent if duty is set by hand */ 
	chan->duty = duty; 
	pwm_set_mode(NO_ENABLE_CHANGE,chan); 
	return size; 
} 
 
struct time_suffix suffixes[] = { 
	[0] = { .suffix = "hz",  .multiplier =	  1, .freq = true  }, /* f = 1/T */ 
	[1] = { .suffix = "khz", .multiplier =       1000, .freq = true  }, 
	[2] = { .suffix = "mhz", .multiplier =    1000000, .freq = true  }, 
	[3] = { .suffix = "ghz", .multiplier = 1000000000, .freq = true  }, 
	[4] = { .suffix = "ms",  .multiplier =       1000, .freq = false }, /* T = 1/f */ 
	[5] = { .suffix = "us",  .multiplier =	  1, .freq = false }, 
	[6] = { .suffix = "ns",  .multiplier =	  1, .freq = false }, 
	[7] = { .suffix = NULL,  .multiplier =	  0, .freq = false }, 
}; 
 
 
unsigned long convert_string_to_microseconds(const char *buf) { 
	unsigned char ch = 0; 
	char numbers[10]; 
	char letters[4]; 
	const char *bufptr = buf; 
	int i = 0; 
	unsigned long microseconds = 0; 
	unsigned long numeric_part = 0; 
	int found_suffix = -1; 
	int numbers_index = 0, letters_index = 0; 
	while(bufptr && *bufptr && (ch = *bufptr)  && isdigit(ch) && numbers_index < (sizeof(numbers)-1)) { 
		numbers[numbers_index++] = *bufptr++; 
	} 
	numbers[numbers_index] = 0; 
	while(bufptr && *bufptr && (ch = *bufptr)  && strchr("usmhznhzkg",tolower(ch)) && letters_index < (sizeof(letters)-1)) { 
		letters[letters_index++] = tolower(*bufptr); 
		bufptr++; 
	} 
	letters[letters_index] = 0; 
	sscanf(numbers,"%lu",&numeric_part); 
	while(suffixes[i].suffix) { 
		if(!strcmp(suffixes[i].suffix,letters)) { 
			found_suffix = i; 
			break; 
		} 
		i++; 
	} 
	if(found_suffix > -1) { 
		if(suffixes[found_suffix].freq) { 
			microseconds = 1000000 / (numeric_part * suffixes[found_suffix].multiplier); 
		} else { 
			microseconds = suffixes[found_suffix].multiplier * numeric_part; 
		} 
	} 
	return microseconds; 
} 
 
 
 
 
static const unsigned int prescale_divisor[13] = {120, 
						  180, 
						  240, 
						  360, 
						  480, 
						  480, /* Invalid Option */ 
						  480, /* Invalid Option */ 
						  480, /* Invalid Option */ 
						  12000, 
						  24000, 
						  36000, 
						  48000, 
						  72000}; 
 
/* 
 * Find the best prescale value for the period 
 * We want to get the highest period cycle count possible, so we look 
 * make a run through the prescale values looking for numbers over 
 * min_optimal_period_cycles.  If none are found then root though again 
 * taking anything that works 
 */ 
enum sun4i_pwm_prescale  pwm_get_best_prescale(unsigned long long period_in) { 
	int i; 
	unsigned long period = period_in; 
	const unsigned long min_optimal_period_cycles = MAX_CYCLES / 2; 
	const unsigned long min_period_cycles = 0x02; 
	enum sun4i_pwm_prescale best_prescale = 0; 
 
	best_prescale = -1; 
	for(i = 0 ; i < 13 ; i++) { 
		unsigned long int check_value = (prescale_divisor[i] /24); 
		if(check_value < 1 || check_value > period) { 
			break; 
		} 
		if(((period / check_value) >= min_optimal_period_cycles) && 
			((period / check_value) <= MAX_CYCLES)) { 
			best_prescale = i; 
			break; 
		} 
	} 
 
	if(best_prescale > 13) { 
		for(i = 0 ; i < 13 ; i++) { 
			unsigned long int check_value = (prescale_divisor[i] /24); 
			if(check_value < 1 || check_value > period) { 
				break; 
			} 
			if(((period / check_value) >= min_period_cycles) && 
				((period / check_value) <= MAX_CYCLES)) { 
				best_prescale = i; 
				break; 
			} 
		} 
	} 
	if(best_prescale > 13) { 
		best_prescale = PRESCALE_DIV480;  /* Something that's not zero - use invalid prescale value */ 
	} 
 
	return best_prescale; 
} 
 
/* 
 * return the number of cycles for the channel period computed from the microseconds 
 * for the period.  Allwinner docs call this "entire" cycles 
 */ 
unsigned int get_entire_cycles(struct sun4i_pwm_available_channel *chan) { 
	unsigned int entire_cycles = 0x01; 
	if ((2 * prescale_divisor[chan->prescale] * MAX_CYCLES) > 0) { 
		entire_cycles = chan->period / (prescale_divisor[chan->prescale] /24); 
	} 
	if(entire_cycles == 0) {entire_cycles = MAX_CYCLES;} 
	if(entire_cycles > MAX_CYCLES) {entire_cycles = MAX_CYCLES;} 
#ifdef SUNXI_PWM_DEBUG
	printk("Best prescale was %d, entire cycles was %u\n",chan->prescale, entire_cycles); 
#endif
 
	return entire_cycles; 
} 
 
/* 
 * return the number of cycles for the channel duty computed from the microseconds 
 * for the duty.  Allwinner docs call this "active" cycles 
 */ 
unsigned int get_active_cycles(struct sun4i_pwm_available_channel *chan) { 
	unsigned int active_cycles = 0x01; 
	unsigned int entire_cycles = get_entire_cycles(chan); 
	if(chan->duty < 0 && chan->period) { 
       		active_cycles = entire_cycles-1; 
	} else if ((2 * prescale_divisor[chan->prescale] * MAX_CYCLES) > 0) { 
		active_cycles = chan->duty / (prescale_divisor[chan->prescale] /24); 
	} 
/*	if(active_cycles == 0) {active_cycles = 0x0ff;} */ 
#ifdef SUNXI_PWM_DEBUG
	printk("Best prescale was %d, active cycles was %u (before entire check)\n",chan->prescale, active_cycles); 
#endif
	if(active_cycles > MAX_CYCLES) {active_cycles = entire_cycles-1;} 
#ifdef SUNXI_PWM_DEBUG
	printk("Best prescale was %d, active cycles was %u (after  entire check)\n",chan->prescale, active_cycles); 
#endif
	return active_cycles; 
} 
 
/* 
 * When the duty is set, compute the number of microseconds 
 * based on the period. 
 */ 
 
void fixup_duty(struct sun4i_pwm_available_channel *chan) { 
	if(chan->duty_percent >= 0) { 
		chan->duty = chan->period * chan->duty_percent / 100; 
	} 
} 
 
/* 
 * Stores the run (enable) bit. 
 */ 
 
static ssize_t pwm_run_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size) { 
	struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status = -EINVAL; 
	int enable = 0; 
 
	sscanf(buf,"%d",&enable); 
	if(enable < 2) { 
		status = pwm_set_mode(enable, chan); 
	} 
	return size; 
} 
 
static ssize_t pwm_duty_percent_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size) { 
	unsigned int duty_percent = 0; 
	struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
 
	sscanf(buf,"%u",&duty_percent); 
	if(duty_percent > 100) { 
		size = -EINVAL; 
	} else { 
		chan->duty_percent = duty_percent; 
		if(chan->period) { 
			fixup_duty(chan); 
			pwm_set_mode(NO_ENABLE_CHANGE,chan); 
		} 
	} 
 
	return size; 
} 
static ssize_t pwm_pulse_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size) { 
	struct sun4i_pwm_available_channel *chan = dev_get_drvdata(dev); 
	ssize_t status = -EINVAL; 
	int pulse = 0; 
	sscanf(buf,"%d",&pulse); 
	if(pulse < 2) { 
		switch (chan->channel) { 
		case 0: 
			chan->ctrl_current.s.ch0_pulse_start = pulse; 
			break; 
		case 1: 
			chan->ctrl_current.s.ch1_pulse_start = pulse; 
			break; 
		default: 
			status = -EINVAL; 
			break; 
		} 
		status = size; 
	} 
	return status; 
} 
 
int pwm_set_period_and_duty(struct sun4i_pwm_available_channel *chan) { 
	int return_val = -EINVAL; 
	unsigned int entire_cycles = get_entire_cycles(chan); 
	unsigned int active_cycles = get_active_cycles(chan); 
	chan->period_reg.initializer = 0; 
	if(entire_cycles >= active_cycles && active_cycles) { 
		chan->period_reg.s.pwm_entire_cycles = entire_cycles; 
		chan->period_reg.s.pwm_active_cycles = active_cycles; 
	} else { 
		chan->period_reg.s.pwm_entire_cycles = MAX_CYCLES; 
		chan->period_reg.s.pwm_active_cycles = MAX_CYCLES; 
	} 
	writel(chan->period_reg.initializer, chan->period_reg_addr); 
	return return_val; 
} 
 
 
ssize_t pwm_set_mode(unsigned int enable, struct sun4i_pwm_available_channel *chan) { 
	ssize_t status = 0; 
	if(enable == NO_ENABLE_CHANGE) { 
		switch (chan->channel) { 
		case 0: 
			enable = chan->ctrl_current.s.ch0_en; 
			break; 
		case 1: 
			enable = chan->ctrl_current.s.ch1_en; 
			break; 
		default: 
			status = -EINVAL; 
			break; 
		} 
	} 
	chan->ctrl_current.initializer = readl(chan->ctrl_addr); 
	if(enable == 1) { 
		switch (chan->channel) { 
		case 0: 
			chan->ctrl_current.s.ch0_prescaler = 0; 
			chan->ctrl_current.s.ch0_act_state = 0; 
			chan->ctrl_current.s.ch0_mode = 0; 
			chan->ctrl_current.s.ch0_pulse_start = 0; 
			chan->ctrl_current.s.ch0_en = 0; 
			chan->ctrl_current.s.ch0_clk_gating = 0; 
			break; 
		case 1: 
			chan->ctrl_current.s.ch1_prescaler = 0; 
			chan->ctrl_current.s.ch1_act_state = 0; 
			chan->ctrl_current.s.ch1_mode = 0; 
			chan->ctrl_current.s.ch1_pulse_start = 0; 
			chan->ctrl_current.s.ch1_en = 1; 
			chan->ctrl_current.s.ch1_clk_gating = 0; 
			break; 
		default: 
			status = -EINVAL; 
			break; 
		} 
		if(status) { 
			return status; 
		} 
		//writel(chan->ctrl_current.initializer,chan->ctrl_addr); 
		chan->pin_current.initializer = readl(chan->pin_addr); 
		if(chan->pin_mask.s0.pin0_select) { 
			chan->pin_current.s0.pin0_select = SELECT_PWM; 
		} 
		if(chan->pin_mask.s0.pin1_select) { 
			chan->pin_current.s0.pin1_select = SELECT_PWM; 
		} 
		if(chan->pin_mask.s0.pin2_select) { 
			chan->pin_current.s0.pin2_select = SELECT_PWM; 
		} 
		if(chan->pin_mask.s0.pin3_select) { 
			chan->pin_current.s0.pin3_select = SELECT_PWM; 
		} 
		if(chan->pin_mask.s0.pin4_select) { 
			chan->pin_current.s0.pin4_select = SELECT_PWM; 
		} 
		if(chan->pin_mask.s0.pin5_select) { 
			chan->pin_current.s0.pin5_select = SELECT_PWM; 
		} 
		if(chan->pin_mask.s0.pin6_select) { 
			chan->pin_current.s0.pin6_select = SELECT_PWM; 
		} 
		if(chan->pin_mask.s0.pin7_select) { 
			chan->pin_current.s0.pin7_select = SELECT_PWM; 
		} 
		if(chan->channel == 0) { 
			chan->ctrl_current.s.ch0_prescaler = chan->prescale; 
		} else { 
			chan->ctrl_current.s.ch1_prescaler = chan->prescale; 
		} 
		pwm_set_period_and_duty(chan); 
 
		writel(chan->pin_current.initializer,chan->pin_addr); 
		//writel(chan->ctrl_current.initializer,chan->ctrl_addr); 
		switch (chan->channel) { 
		case 0: 
			chan->ctrl_current.s.ch0_en = 1; 
			chan->ctrl_current.s.ch0_clk_gating = 1; 
			break; 
		case 1: 
			chan->ctrl_current.s.ch1_en = 1; 
			chan->ctrl_current.s.ch1_clk_gating = 1; 
			break; 
		} 
		writel(chan->ctrl_current.initializer,chan->ctrl_addr); 
 
	} else if (enable == 0) { 
		switch (chan->channel) { 
		case 0: 
			chan->ctrl_current.s.ch0_clk_gating = 0; 
			chan->ctrl_current.s.ch0_en = enable; 
			break; 
		case 1: 
			chan->ctrl_current.s.ch1_clk_gating = 0; 
			chan->ctrl_current.s.ch1_en = enable; 
			break; 
		default: 
			status = -EINVAL; 
			break; 
		} 
		if(!status) { 
			chan->pin_current.initializer &= ~chan->pin_mask.initializer; 
			chan->pin_current.initializer |= readl(chan->pin_addr) & chan->pin_mask.initializer; 
			writel(chan->pin_current.initializer,chan->pin_addr); 
			writel(chan->ctrl_current.initializer,chan->ctrl_addr); 
		} 
	} 
	return status; 
} 
 
 
 
void pwm_setup_available_channels( void ) { 
	void * timer_base = ioremap(SW_PA_TIMERC_IO_BASE, 0x400);  /* 0x01c20c00 */ 
	void * PWM_CTRL_REG_BASE = timer_base + 0x200;	     /* 0x01c20e00 */ 
	void * portc_io_base = ioremap(SW_PA_PORTC_IO_BASE,0x400); /* 0x01c20800 */ 
	void * PB_CFG0_REG = (portc_io_base + 0x24);	       /* 0x01C20824 */ 
	void * PI_CFG0_REG = (portc_io_base + 0x120);	      /* 0x01c20920 */ 
 
	/*void * PB_PULL0_REG = (portc_io_base + 0x040);*/	     /* 0x01c20840 */ 
	/*void * PI_PULL0_REG = (portc_io_base + 0x13c);*/	     /* 0x01c2091c */ 
	/*void * PH_CFG0_REG = (portc_io_base + 0xfc);*/	       /* 0x01c208fc */ 
	/*void * PH_CFG1_REG = (portc_io_base + 0x100);*/	      /* 0x01c20900 */ 
	/*void * PH_PULL0_REG = (portc_io_base + 0x118);*/	     /* 0x01c20918 */ 
 
	pwm_available_chan[0].use_count = 0; 
	pwm_available_chan[0].ctrl_addr = PWM_CTRL_REG_BASE; 
	pwm_available_chan[0].pin_addr = PB_CFG0_REG; 
	pwm_available_chan[0].period_reg_addr = pwm_available_chan[0].ctrl_addr + 0x04; 
	pwm_available_chan[0].channel = 0; 
	pwm_available_chan[0].ctrl_backup.initializer = readl(pwm_available_chan[0].ctrl_addr); 
	pwm_available_chan[0].ctrl_mask.initializer = 0; 
	pwm_available_chan[0].ctrl_mask.s.ch0_prescaler = 0x0f; 
	pwm_available_chan[0].ctrl_mask.s.ch0_en = 0x01; 
	pwm_available_chan[0].ctrl_mask.s.ch0_act_state = 0x01; 
	pwm_available_chan[0].ctrl_mask.s.ch0_clk_gating = 0x00; 
	pwm_available_chan[0].ctrl_mask.s.ch0_mode = 0x01; 
	pwm_available_chan[0].ctrl_mask.s.ch0_pulse_start = 0x01; 
	pwm_available_chan[0].ctrl_current.initializer = 0; 
	pwm_available_chan[0].pin_backup.initializer = readl(pwm_available_chan[0].pin_addr); 
/*	pwm_available_chan[0].pin_mask.initializer = 0xffffffff; */ 
	pwm_available_chan[0].pin_mask.s0.pin2_select = 0x07; 
	pwm_available_chan[0].pin_current.s0.pin2_select = 0x02; 
 
	pwm_available_chan[0].pin_name = "PB2"; 
	pwm_available_chan[0].period = 10000; 
	pwm_available_chan[0].duty_percent = 100; 
	*(unsigned int *)&pwm_available_chan[0].period_reg = 0; 
	pwm_available_chan[0].prescale = 0; 
 
 
	pwm_available_chan[1].use_count = 0; 
	pwm_available_chan[1].ctrl_addr = PWM_CTRL_REG_BASE; 
	pwm_available_chan[1].pin_addr = PI_CFG0_REG; 
	pwm_available_chan[1].period_reg_addr = pwm_available_chan[1].ctrl_addr + 0x08; 
	pwm_available_chan[1].channel = 1; 
	pwm_available_chan[1].ctrl_backup.initializer = readl(pwm_available_chan[1].ctrl_addr); 
	pwm_available_chan[1].ctrl_mask.initializer = 0; 
	pwm_available_chan[1].ctrl_mask.s.ch1_prescaler = 0x0f; 
	pwm_available_chan[1].ctrl_mask.s.ch1_en = 0x01; 
	pwm_available_chan[1].ctrl_mask.s.ch1_act_state = 0x01; 
	pwm_available_chan[1].ctrl_mask.s.ch1_clk_gating = 0x00; 
	pwm_available_chan[1].ctrl_mask.s.ch1_mode = 0x01; 
	pwm_available_chan[1].ctrl_mask.s.ch1_pulse_start = 0x01; 
	pwm_available_chan[1].ctrl_current.initializer = 0; 
	pwm_available_chan[1].pin_backup.initializer = readl(pwm_available_chan[1].pin_addr); 
	pwm_available_chan[1].pin_mask.initializer = 0; 
	pwm_available_chan[1].pin_mask.s0.pin3_select = 0x07; 
	pwm_available_chan[1].pin_current.s0.pin3_select = 0x02; 
	pwm_available_chan[1].pin_name = "PI3"; 
	pwm_available_chan[1].period = 10000; 
	pwm_available_chan[1].duty_percent = 50; 
	*(unsigned int *)&pwm_available_chan[1].period_reg = 0; 
	pwm_available_chan[1].prescale = 0; 
 
 
} 
 
struct pwm_device { 
	struct sun4i_pwm_available_channel *chan; 
}; 
 
struct pwm_device pwm_devices[2] = { 
	[0] = {.chan = &pwm_available_chan[0]}, 
	[1] = {.chan = &pwm_available_chan[1]} 
}; 
 
struct pwm_device *pwm_request(int pwm_id, const char *label) 
{ 
	struct pwm_device *pwm; 
	int found = 0; 
 
	if(pwm_id < 2 && pwm_id >= 0) { 
		pwm = &pwm_devices[pwm_id]; 
		found = 1; 
	} 
	if (found) { 
		if (pwm->chan->use_count == 0) { 
			pwm->chan->use_count++; 
			pwm->chan->name = label; 
		} else 
			pwm = ERR_PTR(-EBUSY); 
	} else 
		pwm = ERR_PTR(-ENOENT); 
 
	return pwm; 
} 
EXPORT_SYMBOL(pwm_request); 
 
 
int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns) 
{ 
	if (pwm == NULL || period_ns == 0 || duty_ns > period_ns) 
		return -EINVAL; 
 
	pwm->chan->period = period_ns / 1000; 
	pwm->chan->prescale = pwm_get_best_prescale(pwm->chan->period); 
	pwm->chan->duty = duty_ns / 1000; 
	fixup_duty(pwm->chan); 
	pwm_set_mode(NO_ENABLE_CHANGE,pwm->chan); 
	return 0; 
} 
EXPORT_SYMBOL(pwm_config); 
 
 
int pwm_enable(struct pwm_device *pwm) 
{ 
	if (pwm == NULL) { 
		return -EINVAL; 
	} 
	pwm_set_mode(PWM_CTRL_ENABLE,pwm->chan); 
	return 0; 
} 
EXPORT_SYMBOL(pwm_enable); 
 
void pwm_disable(struct pwm_device *pwm) 
{ 
	if (pwm == NULL) { 
		return; 
	} 
	pwm_set_mode(PWM_CTRL_DISABLE,pwm->chan); 
} 
EXPORT_SYMBOL(pwm_disable); 
 
void pwm_free(struct pwm_device *pwm) 
{ 
	if (pwm->chan->use_count) { 
		pwm->chan->use_count--; 
	} else 
		pr_warning("PWM device already freed\n"); 
} 
EXPORT_SYMBOL(pwm_free); 
 
 
module_init(sunxi_pwm_init); 
module_exit(sunxi_pwm_exit); 
 
 
 
 
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("David H. Wilkins <dwil...@conecuh.com>"); 
