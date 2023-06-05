// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * acerhdf - A driver which monitors the temperature
 *           of the aspire one netbook, turns on/off the fan
 *           as soon as the upper/lower threshold is reached.
 *
 * (C) 2009 - Peter Kaestle     peter (a) piie.net
 *                              https://piie.net
 *     2009 Borislav Petkov	bp (a) alien8.de
 *
 * Inspired by and many thanks to:
 *  o acerfand   - Rachel Greenham
 *  o acer_ec.pl - Michael Kurz     michi.kurz (at) googlemail.com
 *               - Petr Tomasek     tomasek (#) etf,cuni,cz
 *               - Carlos Corbacho  cathectic (at) gmail.com
 *  o lkml       - Matthew Garrett
 *               - Borislav Petkov
 *               - Andreas Mohr
 */

#define pr_fmt(fmt) "acerhdf: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>

/*
 * The driver is started with "kernel mode off" by default. That means, the BIOS
 * is still in control of the fan. In this mode the driver allows to read the
 * temperature of the cpu and a userspace tool may take over control of the fan.
 * If the driver is switched to "kernel mode" (e.g. via module parameter) the
 * driver is in full control of the fan. If you want the module to be started in
 * kernel mode by default, define the following:
 */
#undef START_IN_KERNEL_MODE

#define DRV_VER "0.7.0"

/*
 * According to the Atom N270 datasheet,
 * (http://download.intel.com/design/processor/datashts/320032.pdf) the
 * CPU's optimal operating limits denoted in junction temperature as
 * measured by the on-die thermal monitor are within 0 <= Tj <= 90. So,
 * assume 89°C is critical temperature.
 */
#define ACERHDF_DEFAULT_TEMP_FANON 60000
#define ACERHDF_DEFAULT_TEMP_FANOFF 53000
#define ACERHDF_TEMP_CRIT 89000
#define ACERHDF_FAN_OFF 0
#define ACERHDF_FAN_AUTO 1

/*
 * No matter what value the user puts into the fanon variable, turn on the fan
 * at 80 degree Celsius to prevent hardware damage
 */
#define ACERHDF_MAX_FANON 80000

/*
 * Maximum interval between two temperature checks is 15 seconds, as the die
 * can get hot really fast under heavy load (plus we shouldn't forget about
 * possible impact of _external_ aggressive sources such as heaters, sun etc.)
 */
#define ACERHDF_MAX_INTERVAL 15

#ifdef START_IN_KERNEL_MODE
static int kernelmode = 1;
#else
static int kernelmode;
#endif

static unsigned int interval = 10;
static unsigned int fanon = ACERHDF_DEFAULT_TEMP_FANON;
static unsigned int fanoff = ACERHDF_DEFAULT_TEMP_FANOFF;
static unsigned int verbose;
static unsigned int list_supported;
static unsigned int fanstate = ACERHDF_FAN_AUTO;
static char force_bios[16];
static char force_product[16];
static struct thermal_zone_device *thz_dev;
static struct thermal_cooling_device *cl_dev;
static struct platform_device *acerhdf_dev;

module_param(kernelmode, uint, 0);
MODULE_PARM_DESC(kernelmode, "Kernel mode fan control on / off");
module_param(fanon, uint, 0600);
MODULE_PARM_DESC(fanon, "Turn the fan on above this temperature");
module_param(fanoff, uint, 0600);
MODULE_PARM_DESC(fanoff, "Turn the fan off below this temperature");
module_param(verbose, uint, 0600);
MODULE_PARM_DESC(verbose, "Enable verbose dmesg output");
module_param(list_supported, uint, 0600);
MODULE_PARM_DESC(list_supported, "List supported models and BIOS versions");
module_param_string(force_bios, force_bios, 16, 0);
MODULE_PARM_DESC(force_bios, "Pretend system has this known supported BIOS version");
module_param_string(force_product, force_product, 16, 0);
MODULE_PARM_DESC(force_product, "Pretend system is this known supported model");

/*
 * cmd_off: to switch the fan completely off and check if the fan is off
 *	cmd_auto: to set the BIOS in control of the fan. The BIOS regulates then
 *		the fan speed depending on the temperature
 */
struct fancmd {
	u8 cmd_off;
	u8 cmd_auto;
};

struct manualcmd {
	u8 mreg;
	u8 moff;
};

/* default register and command to disable fan in manual mode */
static const struct manualcmd mcmd = {
	.mreg = 0x94,
	.moff = 0xff,
};

/* BIOS settings - only used during probe */
struct bios_settings {
	const char *vendor;
	const char *product;
	const char *version;
	u8 fanreg;
	u8 tempreg;
	struct fancmd cmd;
	int mcmd_enable;
};

/* This could be a daughter struct in the above, but not worth the redirect */
struct ctrl_settings {
	u8 fanreg;
	u8 tempreg;
	struct fancmd cmd;
	int mcmd_enable;
};

static struct thermal_trip trips[] = {
	[0] = { .temperature = ACERHDF_DEFAULT_TEMP_FANON,
		.hysteresis = ACERHDF_DEFAULT_TEMP_FANON - ACERHDF_DEFAULT_TEMP_FANOFF,
		.type = THERMAL_TRIP_ACTIVE },

	[1] = { .temperature = ACERHDF_TEMP_CRIT,
		.type = THERMAL_TRIP_CRITICAL }
};

static struct ctrl_settings ctrl_cfg __read_mostly;

/* Register addresses and values for different BIOS versions */
static const struct bios_settings bios_tbl[] __initconst = {
	/* AOA110 */
	{"Acer", "AOA110", "v0.3109", 0x55, 0x58, {0x1f, 0x00}, 0},
	{"Acer", "AOA110", "v0.3114", 0x55, 0x58, {0x1f, 0x00}, 0},
	{"Acer", "AOA110", "v0.3301", 0x55, 0x58, {0xaf, 0x00}, 0},
	{"Acer", "AOA110", "v0.3304", 0x55, 0x58, {0xaf, 0x00}, 0},
	{"Acer", "AOA110", "v0.3305", 0x55, 0x58, {0xaf, 0x00}, 0},
	{"Acer", "AOA110", "v0.3307", 0x55, 0x58, {0xaf, 0x00}, 0},
	{"Acer", "AOA110", "v0.3308", 0x55, 0x58, {0x21, 0x00}, 0},
	{"Acer", "AOA110", "v0.3309", 0x55, 0x58, {0x21, 0x00}, 0},
	{"Acer", "AOA110", "v0.3310", 0x55, 0x58, {0x21, 0x00}, 0},
	/* AOA150 */
	{"Acer", "AOA150", "v0.3114", 0x55, 0x58, {0x1f, 0x00}, 0},
	{"Acer", "AOA150", "v0.3301", 0x55, 0x58, {0x20, 0x00}, 0},
	{"Acer", "AOA150", "v0.3304", 0x55, 0x58, {0x20, 0x00}, 0},
	{"Acer", "AOA150", "v0.3305", 0x55, 0x58, {0x20, 0x00}, 0},
	{"Acer", "AOA150", "v0.3307", 0x55, 0x58, {0x20, 0x00}, 0},
	{"Acer", "AOA150", "v0.3308", 0x55, 0x58, {0x20, 0x00}, 0},
	{"Acer", "AOA150", "v0.3309", 0x55, 0x58, {0x20, 0x00}, 0},
	{"Acer", "AOA150", "v0.3310", 0x55, 0x58, {0x20, 0x00}, 0},
	/* LT1005u */
	{"Acer", "LT-10Q", "v0.3310", 0x55, 0x58, {0x20, 0x00}, 0},
	/* Acer 1410 */
	{"Acer", "Aspire 1410", "v0.3108", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v0.3113", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v0.3115", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v0.3117", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v0.3119", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v0.3120", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v1.3204", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v1.3303", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v1.3308", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v1.3310", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1410", "v1.3314", 0x55, 0x58, {0x9e, 0x00}, 0},
	/* Acer 1810xx */
	{"Acer", "Aspire 1810TZ", "v0.3108", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v0.3108", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v0.3113", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v0.3113", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v0.3115", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v0.3115", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v0.3117", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v0.3117", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v0.3119", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v0.3119", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v0.3120", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v0.3120", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v1.3204", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v1.3204", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v1.3303", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v1.3303", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v1.3308", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v1.3308", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v1.3310", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v1.3310", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810TZ", "v1.3314", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1810T",  "v1.3314", 0x55, 0x58, {0x9e, 0x00}, 0},
	/* Acer 5755G */
	{"Acer", "Aspire 5755G",  "V1.20",   0xab, 0xb4, {0x00, 0x08}, 0},
	{"Acer", "Aspire 5755G",  "V1.21",   0xab, 0xb3, {0x00, 0x08}, 0},
	/* Acer 521 */
	{"Acer", "AO521", "V1.11", 0x55, 0x58, {0x1f, 0x00}, 0},
	/* Acer 531 */
	{"Acer", "AO531h", "v0.3104", 0x55, 0x58, {0x20, 0x00}, 0},
	{"Acer", "AO531h", "v0.3201", 0x55, 0x58, {0x20, 0x00}, 0},
	{"Acer", "AO531h", "v0.3304", 0x55, 0x58, {0x20, 0x00}, 0},
	/* Acer 751 */
	{"Acer", "AO751h", "V0.3206", 0x55, 0x58, {0x21, 0x00}, 0},
	{"Acer", "AO751h", "V0.3212", 0x55, 0x58, {0x21, 0x00}, 0},
	/* Acer 753 */
	{"Acer", "Aspire One 753", "V1.24", 0x93, 0xac, {0x14, 0x04}, 1},
	/* Acer 1825 */
	{"Acer", "Aspire 1825PTZ", "V1.3118", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Acer", "Aspire 1825PTZ", "V1.3127", 0x55, 0x58, {0x9e, 0x00}, 0},
	/* Acer Extensa 5420 */
	{"Acer", "Extensa 5420", "V1.17", 0x93, 0xac, {0x14, 0x04}, 1},
	/* Acer Aspire 5315 */
	{"Acer", "Aspire 5315", "V1.19", 0x93, 0xac, {0x14, 0x04}, 1},
	/* Acer Aspire 5739 */
	{"Acer", "Aspire 5739G", "V1.3311", 0x55, 0x58, {0x20, 0x00}, 0},
	/* Acer TravelMate 7730 */
	{"Acer", "TravelMate 7730G", "v0.3509", 0x55, 0x58, {0xaf, 0x00}, 0},
	/* Acer Aspire 7551 */
	{"Acer", "Aspire 7551", "V1.18", 0x93, 0xa8, {0x14, 0x04}, 1},
	/* Acer TravelMate TM8573T */
	{"Acer", "TM8573T", "V1.13", 0x93, 0xa8, {0x14, 0x04}, 1},
	/* Gateway */
	{"Gateway", "AOA110", "v0.3103",  0x55, 0x58, {0x21, 0x00}, 0},
	{"Gateway", "AOA150", "v0.3103",  0x55, 0x58, {0x20, 0x00}, 0},
	{"Gateway", "LT31",   "v1.3103",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Gateway", "LT31",   "v1.3201",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Gateway", "LT31",   "v1.3302",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Gateway", "LT31",   "v1.3303t", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Gateway", "LT31",   "v1.3307",  0x55, 0x58, {0x9e, 0x00}, 0},
	/* Packard Bell */
	{"Packard Bell", "DOA150",  "v0.3104",  0x55, 0x58, {0x21, 0x00}, 0},
	{"Packard Bell", "DOA150",  "v0.3105",  0x55, 0x58, {0x20, 0x00}, 0},
	{"Packard Bell", "AOA110",  "v0.3105",  0x55, 0x58, {0x21, 0x00}, 0},
	{"Packard Bell", "AOA150",  "v0.3105",  0x55, 0x58, {0x20, 0x00}, 0},
	{"Packard Bell", "ENBFT",   "V1.3118",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "ENBFT",   "V1.3127",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMU",   "v1.3303",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMU",   "v0.3120",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMU",   "v0.3108",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMU",   "v0.3113",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMU",   "v0.3115",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMU",   "v0.3117",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMU",   "v0.3119",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMU",   "v1.3204",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMA",   "v1.3201",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMA",   "v1.3302",  0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTMA",   "v1.3303t", 0x55, 0x58, {0x9e, 0x00}, 0},
	{"Packard Bell", "DOTVR46", "v1.3308",  0x55, 0x58, {0x9e, 0x00}, 0},
	/* pewpew-terminator */
	{"", "", "", 0, 0, {0, 0}, 0}
};

/*
 * this struct is used to instruct thermal layer to use bang_bang instead of
 * default governor for acerhdf
 */
static struct thermal_zone_params acerhdf_zone_params = {
	.governor_name = "bang_bang",
};

static int acerhdf_get_temp(int *temp)
{
	u8 read_temp;

	if (ec_read(ctrl_cfg.tempreg, &read_temp))
		return -EINVAL;

	*temp = read_temp * 1000;

	return 0;
}

static int acerhdf_get_fanstate(int *state)
{
	u8 fan;

	if (ec_read(ctrl_cfg.fanreg, &fan))
		return -EINVAL;

	if (fan != ctrl_cfg.cmd.cmd_off)
		*state = ACERHDF_FAN_AUTO;
	else
		*state = ACERHDF_FAN_OFF;

	return 0;
}

static void acerhdf_change_fanstate(int state)
{
	unsigned char cmd;

	if (verbose)
		pr_notice("fan %s\n", state == ACERHDF_FAN_OFF ? "OFF" : "ON");

	if ((state != ACERHDF_FAN_OFF) && (state != ACERHDF_FAN_AUTO)) {
		pr_err("invalid fan state %d requested, setting to auto!\n",
		       state);
		state = ACERHDF_FAN_AUTO;
	}

	cmd = (state == ACERHDF_FAN_OFF) ? ctrl_cfg.cmd.cmd_off
					 : ctrl_cfg.cmd.cmd_auto;
	fanstate = state;

	ec_write(ctrl_cfg.fanreg, cmd);

	if (ctrl_cfg.mcmd_enable && state == ACERHDF_FAN_OFF) {
		if (verbose)
			pr_notice("turning off fan manually\n");
		ec_write(mcmd.mreg, mcmd.moff);
	}
}

static void acerhdf_check_param(struct thermal_zone_device *thermal)
{
	if (fanon > ACERHDF_MAX_FANON) {
		pr_err("fanon temperature too high, set to %d\n",
		       ACERHDF_MAX_FANON);
		fanon = ACERHDF_MAX_FANON;
	}

	if (fanon < fanoff) {
		pr_err("fanoff temperature (%d) is above fanon temperature (%d), clamping to %d\n",
		       fanoff, fanon, fanon);
		fanoff = fanon;
	}

	trips[0].temperature = fanon;
	trips[0].hysteresis  = fanon - fanoff;

	if (kernelmode) {
		if (interval > ACERHDF_MAX_INTERVAL) {
			pr_err("interval too high, set to %d\n",
			       ACERHDF_MAX_INTERVAL);
			interval = ACERHDF_MAX_INTERVAL;
		}

		if (verbose)
			pr_notice("interval changed to: %d\n", interval);
	}
}

/*
 * This is the thermal zone callback which does the delayed polling of the fan
 * state. We do check /sysfs-originating settings here in acerhdf_check_param()
 * as late as the polling interval is since we can't do that in the respective
 * accessors of the module parameters.
 */
static int acerhdf_get_ec_temp(struct thermal_zone_device *thermal, int *t)
{
	int temp, err = 0;

	err = acerhdf_get_temp(&temp);
	if (err)
		return err;

	if (verbose)
		pr_notice("temp %d\n", temp);

	*t = temp;
	return 0;
}

static int acerhdf_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	/* if the cooling device is the one from acerhdf bind it */
	if (cdev != cl_dev)
		return 0;

	if (thermal_zone_bind_cooling_device(thermal, 0, cdev,
			THERMAL_NO_LIMIT, THERMAL_NO_LIMIT,
			THERMAL_WEIGHT_DEFAULT)) {
		pr_err("error binding cooling dev\n");
		return -EINVAL;
	}
	return 0;
}

static int acerhdf_unbind(struct thermal_zone_device *thermal,
			  struct thermal_cooling_device *cdev)
{
	if (cdev != cl_dev)
		return 0;

	if (thermal_zone_unbind_cooling_device(thermal, 0, cdev)) {
		pr_err("error unbinding cooling dev\n");
		return -EINVAL;
	}
	return 0;
}

static inline void acerhdf_revert_to_bios_mode(void)
{
	acerhdf_change_fanstate(ACERHDF_FAN_AUTO);
	kernelmode = 0;

	pr_notice("kernel mode fan control OFF\n");
}
static inline void acerhdf_enable_kernelmode(void)
{
	kernelmode = 1;

	pr_notice("kernel mode fan control ON\n");
}

/*
 * set operation mode;
 * enabled: the thermal layer of the kernel takes care about
 *          the temperature and the fan.
 * disabled: the BIOS takes control of the fan.
 */
static int acerhdf_change_mode(struct thermal_zone_device *thermal,
			       enum thermal_device_mode mode)
{
	if (mode == THERMAL_DEVICE_DISABLED && kernelmode)
		acerhdf_revert_to_bios_mode();
	else if (mode == THERMAL_DEVICE_ENABLED && !kernelmode)
		acerhdf_enable_kernelmode();

	return 0;
}

static int acerhdf_get_crit_temp(struct thermal_zone_device *thermal,
				 int *temperature)
{
	*temperature = ACERHDF_TEMP_CRIT;
	return 0;
}

/* bind callback functions to thermalzone */
static struct thermal_zone_device_ops acerhdf_dev_ops = {
	.bind = acerhdf_bind,
	.unbind = acerhdf_unbind,
	.get_temp = acerhdf_get_ec_temp,
	.change_mode = acerhdf_change_mode,
	.get_crit_temp = acerhdf_get_crit_temp,
};

/*
 * cooling device callback functions
 * get maximal fan cooling state
 */
static int acerhdf_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = 1;

	return 0;
}

static int acerhdf_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	int err = 0, tmp;

	err = acerhdf_get_fanstate(&tmp);
	if (err)
		return err;

	*state = (tmp == ACERHDF_FAN_AUTO) ? 1 : 0;
	return 0;
}

/* change current fan state - is overwritten when running in kernel mode */
static int acerhdf_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	int cur_temp, cur_state, err = 0;

	if (!kernelmode)
		return 0;

	err = acerhdf_get_temp(&cur_temp);
	if (err) {
		pr_err("error reading temperature, hand off control to BIOS\n");
		goto err_out;
	}

	err = acerhdf_get_fanstate(&cur_state);
	if (err) {
		pr_err("error reading fan state, hand off control to BIOS\n");
		goto err_out;
	}

	if (state == 0) {
		if (cur_state == ACERHDF_FAN_AUTO)
			acerhdf_change_fanstate(ACERHDF_FAN_OFF);
	} else {
		if (cur_state == ACERHDF_FAN_OFF)
			acerhdf_change_fanstate(ACERHDF_FAN_AUTO);
	}
	return 0;

err_out:
	acerhdf_revert_to_bios_mode();
	return -EINVAL;
}

/* bind fan callbacks to fan device */
static const struct thermal_cooling_device_ops acerhdf_cooling_ops = {
	.get_max_state = acerhdf_get_max_state,
	.get_cur_state = acerhdf_get_cur_state,
	.set_cur_state = acerhdf_set_cur_state,
};

/* suspend / resume functionality */
static int acerhdf_suspend(struct device *dev)
{
	if (kernelmode)
		acerhdf_change_fanstate(ACERHDF_FAN_AUTO);

	if (verbose)
		pr_notice("going suspend\n");

	return 0;
}

static int acerhdf_probe(struct platform_device *device)
{
	return 0;
}

static const struct dev_pm_ops acerhdf_pm_ops = {
	.suspend = acerhdf_suspend,
	.freeze  = acerhdf_suspend,
};

static struct platform_driver acerhdf_driver = {
	.driver = {
		.name  = "acerhdf",
		.pm    = &acerhdf_pm_ops,
	},
	.probe = acerhdf_probe,
};

/* check hardware */
static int __init acerhdf_check_hardware(void)
{
	char const *vendor, *version, *product;
	const struct bios_settings *bt = NULL;
	int found = 0;

	/* get BIOS data */
	vendor  = dmi_get_system_info(DMI_SYS_VENDOR);
	version = dmi_get_system_info(DMI_BIOS_VERSION);
	product = dmi_get_system_info(DMI_PRODUCT_NAME);

	if (!vendor || !version || !product) {
		pr_err("error getting hardware information\n");
		return -EINVAL;
	}

	pr_info("Acer Aspire One Fan driver, v.%s\n", DRV_VER);

	if (list_supported) {
		pr_info("List of supported Manufacturer/Model/BIOS:\n");
		pr_info("---------------------------------------------------\n");
		for (bt = bios_tbl; bt->vendor[0]; bt++) {
			pr_info("%-13s | %-17s | %-10s\n", bt->vendor,
				bt->product, bt->version);
		}
		pr_info("---------------------------------------------------\n");
		return -ECANCELED;
	}

	if (force_bios[0]) {
		version = force_bios;
		pr_info("forcing BIOS version: %s\n", version);
		kernelmode = 0;
	}

	if (force_product[0]) {
		product = force_product;
		pr_info("forcing BIOS product: %s\n", product);
		kernelmode = 0;
	}

	if (verbose)
		pr_info("BIOS info: %s %s, product: %s\n",
			vendor, version, product);

	/* search BIOS version and vendor in BIOS settings table */
	for (bt = bios_tbl; bt->vendor[0]; bt++) {
		/*
		 * check if actual hardware BIOS vendor, product and version
		 * IDs start with the strings of BIOS table entry
		 */
		if (strstarts(vendor, bt->vendor) &&
		    strstarts(product, bt->product) &&
		    strstarts(version, bt->version)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		pr_err("unknown (unsupported) BIOS version %s/%s/%s, please report, aborting!\n",
		       vendor, product, version);
		return -EINVAL;
	}

	/* Copy control settings from BIOS table before we free it. */
	ctrl_cfg.fanreg = bt->fanreg;
	ctrl_cfg.tempreg = bt->tempreg;
	memcpy(&ctrl_cfg.cmd, &bt->cmd, sizeof(struct fancmd));
	ctrl_cfg.mcmd_enable = bt->mcmd_enable;

	/*
	 * if started with kernel mode off, prevent the kernel from switching
	 * off the fan
	 */
	if (!kernelmode) {
		pr_notice("Fan control off, to enable do:\n");
		pr_notice("echo -n \"enabled\" > /sys/class/thermal/thermal_zoneN/mode # N=0,1,2...\n");
	}

	return 0;
}

static int __init acerhdf_register_platform(void)
{
	int err = 0;

	err = platform_driver_register(&acerhdf_driver);
	if (err)
		return err;

	acerhdf_dev = platform_device_alloc("acerhdf", PLATFORM_DEVID_NONE);
	if (!acerhdf_dev) {
		err = -ENOMEM;
		goto err_device_alloc;
	}
	err = platform_device_add(acerhdf_dev);
	if (err)
		goto err_device_add;

	return 0;

err_device_add:
	platform_device_put(acerhdf_dev);
err_device_alloc:
	platform_driver_unregister(&acerhdf_driver);
	return err;
}

static void acerhdf_unregister_platform(void)
{
	platform_device_unregister(acerhdf_dev);
	platform_driver_unregister(&acerhdf_driver);
}

static int __init acerhdf_register_thermal(void)
{
	int ret;

	cl_dev = thermal_cooling_device_register("acerhdf-fan", NULL,
						 &acerhdf_cooling_ops);

	if (IS_ERR(cl_dev))
		return -EINVAL;

	thz_dev = thermal_zone_device_register_with_trips("acerhdf", trips, ARRAY_SIZE(trips),
							  0, NULL, &acerhdf_dev_ops,
							  &acerhdf_zone_params, 0,
							  (kernelmode) ? interval*1000 : 0);
	if (IS_ERR(thz_dev))
		return -EINVAL;

	if (kernelmode)
		ret = thermal_zone_device_enable(thz_dev);
	else
		ret = thermal_zone_device_disable(thz_dev);
	if (ret)
		return ret;

	return 0;
}

static void acerhdf_unregister_thermal(void)
{
	if (cl_dev) {
		thermal_cooling_device_unregister(cl_dev);
		cl_dev = NULL;
	}

	if (thz_dev) {
		thermal_zone_device_unregister(thz_dev);
		thz_dev = NULL;
	}
}

static int __init acerhdf_init(void)
{
	int err = 0;

	err = acerhdf_check_hardware();
	if (err)
		goto out_err;

	err = acerhdf_register_platform();
	if (err)
		goto out_err;

	err = acerhdf_register_thermal();
	if (err)
		goto err_unreg;

	return 0;

err_unreg:
	acerhdf_unregister_thermal();
	acerhdf_unregister_platform();

out_err:
	return err;
}

static void __exit acerhdf_exit(void)
{
	acerhdf_change_fanstate(ACERHDF_FAN_AUTO);
	acerhdf_unregister_thermal();
	acerhdf_unregister_platform();
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Kaestle");
MODULE_DESCRIPTION("Aspire One temperature and fan driver");
MODULE_ALIAS("dmi:*:*Acer*:pnAOA*:");
MODULE_ALIAS("dmi:*:*Acer*:pnAO751h*:");
MODULE_ALIAS("dmi:*:*Acer*:pnAspire*1410*:");
MODULE_ALIAS("dmi:*:*Acer*:pnAspire*1810*:");
MODULE_ALIAS("dmi:*:*Acer*:pnAspire*5755G:");
MODULE_ALIAS("dmi:*:*Acer*:pnAspire*1825PTZ:");
MODULE_ALIAS("dmi:*:*Acer*:pnAO521*:");
MODULE_ALIAS("dmi:*:*Acer*:pnAO531*:");
MODULE_ALIAS("dmi:*:*Acer*:pnAspire*5739G:");
MODULE_ALIAS("dmi:*:*Acer*:pnAspire*One*753:");
MODULE_ALIAS("dmi:*:*Acer*:pnAspire*5315:");
MODULE_ALIAS("dmi:*:*Acer*:TravelMate*7730G:");
MODULE_ALIAS("dmi:*:*Acer*:pnAspire*7551:");
MODULE_ALIAS("dmi:*:*Acer*:TM8573T:");
MODULE_ALIAS("dmi:*:*Gateway*:pnAOA*:");
MODULE_ALIAS("dmi:*:*Gateway*:pnLT31*:");
MODULE_ALIAS("dmi:*:*Packard*Bell*:pnAOA*:");
MODULE_ALIAS("dmi:*:*Packard*Bell*:pnDOA*:");
MODULE_ALIAS("dmi:*:*Packard*Bell*:pnDOTMU*:");
MODULE_ALIAS("dmi:*:*Packard*Bell*:pnENBFT*:");
MODULE_ALIAS("dmi:*:*Packard*Bell*:pnDOTMA*:");
MODULE_ALIAS("dmi:*:*Packard*Bell*:pnDOTVR46*:");
MODULE_ALIAS("dmi:*:*Acer*:pnExtensa*5420*:");

module_init(acerhdf_init);
module_exit(acerhdf_exit);

static int interval_set_uint(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_uint(val, kp);
	if (ret)
		return ret;

	acerhdf_check_param(thz_dev);

	return 0;
}

static const struct kernel_param_ops interval_ops = {
	.set = interval_set_uint,
	.get = param_get_uint,
};

module_param_cb(interval, &interval_ops, &interval, 0000);
MODULE_PARM_DESC(interval, "Polling interval of temperature check");
