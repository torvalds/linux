/*
 * OMAP Voltage Controller (VC) interface
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/io.h>

#include <asm/div64.h>

#include "iomap.h"
#include "soc.h"
#include "voltage.h"
#include "vc.h"
#include "prm-regbits-34xx.h"
#include "prm-regbits-44xx.h"
#include "prm44xx.h"
#include "pm.h"
#include "scrm44xx.h"
#include "control.h"

/**
 * struct omap_vc_channel_cfg - describe the cfg_channel bitfield
 * @sa: bit for slave address
 * @rav: bit for voltage configuration register
 * @rac: bit for command configuration register
 * @racen: enable bit for RAC
 * @cmd: bit for command value set selection
 *
 * Channel configuration bits, common for OMAP3+
 * OMAP3 register: PRM_VC_CH_CONF
 * OMAP4 register: PRM_VC_CFG_CHANNEL
 * OMAP5 register: PRM_VC_SMPS_<voltdm>_CONFIG
 */
struct omap_vc_channel_cfg {
	u8 sa;
	u8 rav;
	u8 rac;
	u8 racen;
	u8 cmd;
};

static struct omap_vc_channel_cfg vc_default_channel_cfg = {
	.sa    = BIT(0),
	.rav   = BIT(1),
	.rac   = BIT(2),
	.racen = BIT(3),
	.cmd   = BIT(4),
};

/*
 * On OMAP3+, all VC channels have the above default bitfield
 * configuration, except the OMAP4 MPU channel.  This appears
 * to be a freak accident as every other VC channel has the
 * default configuration, thus creating a mutant channel config.
 */
static struct omap_vc_channel_cfg vc_mutant_channel_cfg = {
	.sa    = BIT(0),
	.rav   = BIT(2),
	.rac   = BIT(3),
	.racen = BIT(4),
	.cmd   = BIT(1),
};

static struct omap_vc_channel_cfg *vc_cfg_bits;

/* Default I2C trace length on pcb, 6.3cm. Used for capacitance calculations. */
static u32 sr_i2c_pcb_length = 63;
#define CFG_CHANNEL_MASK 0x1f

/**
 * omap_vc_config_channel - configure VC channel to PMIC mappings
 * @voltdm: pointer to voltagdomain defining the desired VC channel
 *
 * Configures the VC channel to PMIC mappings for the following
 * PMIC settings
 * - i2c slave address (SA)
 * - voltage configuration address (RAV)
 * - command configuration address (RAC) and enable bit (RACEN)
 * - command values for ON, ONLP, RET and OFF (CMD)
 *
 * This function currently only allows flexible configuration of the
 * non-default channel.  Starting with OMAP4, there are more than 2
 * channels, with one defined as the default (on OMAP4, it's MPU.)
 * Only the non-default channel can be configured.
 */
static int omap_vc_config_channel(struct voltagedomain *voltdm)
{
	struct omap_vc_channel *vc = voltdm->vc;

	/*
	 * For default channel, the only configurable bit is RACEN.
	 * All others must stay at zero (see function comment above.)
	 */
	if (vc->flags & OMAP_VC_CHANNEL_DEFAULT)
		vc->cfg_channel &= vc_cfg_bits->racen;

	voltdm->rmw(CFG_CHANNEL_MASK << vc->cfg_channel_sa_shift,
		    vc->cfg_channel << vc->cfg_channel_sa_shift,
		    vc->cfg_channel_reg);

	return 0;
}

/* Voltage scale and accessory APIs */
int omap_vc_pre_scale(struct voltagedomain *voltdm,
		      unsigned long target_volt,
		      u8 *target_vsel, u8 *current_vsel)
{
	struct omap_vc_channel *vc = voltdm->vc;
	u32 vc_cmdval;

	/* Check if sufficient pmic info is available for this vdd */
	if (!voltdm->pmic) {
		pr_err("%s: Insufficient pmic info to scale the vdd_%s\n",
			__func__, voltdm->name);
		return -EINVAL;
	}

	if (!voltdm->pmic->uv_to_vsel) {
		pr_err("%s: PMIC function to convert voltage in uV to vsel not registered. Hence unable to scale voltage for vdd_%s\n",
		       __func__, voltdm->name);
		return -ENODATA;
	}

	if (!voltdm->read || !voltdm->write) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return -EINVAL;
	}

	*target_vsel = voltdm->pmic->uv_to_vsel(target_volt);
	*current_vsel = voltdm->pmic->uv_to_vsel(voltdm->nominal_volt);

	/* Setting the ON voltage to the new target voltage */
	vc_cmdval = voltdm->read(vc->cmdval_reg);
	vc_cmdval &= ~vc->common->cmd_on_mask;
	vc_cmdval |= (*target_vsel << vc->common->cmd_on_shift);
	voltdm->write(vc_cmdval, vc->cmdval_reg);

	voltdm->vc_param->on = target_volt;

	omap_vp_update_errorgain(voltdm, target_volt);

	return 0;
}

void omap_vc_post_scale(struct voltagedomain *voltdm,
			unsigned long target_volt,
			u8 target_vsel, u8 current_vsel)
{
	u32 smps_steps = 0, smps_delay = 0;

	smps_steps = abs(target_vsel - current_vsel);
	/* SMPS slew rate / step size. 2us added as buffer. */
	smps_delay = ((smps_steps * voltdm->pmic->step_size) /
			voltdm->pmic->slew_rate) + 2;
	udelay(smps_delay);
}

/* vc_bypass_scale - VC bypass method of voltage scaling */
int omap_vc_bypass_scale(struct voltagedomain *voltdm,
			 unsigned long target_volt)
{
	struct omap_vc_channel *vc = voltdm->vc;
	u32 loop_cnt = 0, retries_cnt = 0;
	u32 vc_valid, vc_bypass_val_reg, vc_bypass_value;
	u8 target_vsel, current_vsel;
	int ret;

	ret = omap_vc_pre_scale(voltdm, target_volt, &target_vsel, &current_vsel);
	if (ret)
		return ret;

	vc_valid = vc->common->valid;
	vc_bypass_val_reg = vc->common->bypass_val_reg;
	vc_bypass_value = (target_vsel << vc->common->data_shift) |
		(vc->volt_reg_addr << vc->common->regaddr_shift) |
		(vc->i2c_slave_addr << vc->common->slaveaddr_shift);

	voltdm->write(vc_bypass_value, vc_bypass_val_reg);
	voltdm->write(vc_bypass_value | vc_valid, vc_bypass_val_reg);

	vc_bypass_value = voltdm->read(vc_bypass_val_reg);
	/*
	 * Loop till the bypass command is acknowledged from the SMPS.
	 * NOTE: This is legacy code. The loop count and retry count needs
	 * to be revisited.
	 */
	while (!(vc_bypass_value & vc_valid)) {
		loop_cnt++;

		if (retries_cnt > 10) {
			pr_warn("%s: Retry count exceeded\n", __func__);
			return -ETIMEDOUT;
		}

		if (loop_cnt > 50) {
			retries_cnt++;
			loop_cnt = 0;
			udelay(10);
		}
		vc_bypass_value = voltdm->read(vc_bypass_val_reg);
	}

	omap_vc_post_scale(voltdm, target_volt, target_vsel, current_vsel);
	return 0;
}

/* Convert microsecond value to number of 32kHz clock cycles */
static inline u32 omap_usec_to_32k(u32 usec)
{
	return DIV_ROUND_UP_ULL(32768ULL * (u64)usec, 1000000ULL);
}

struct omap3_vc_timings {
	u32 voltsetup1;
	u32 voltsetup2;
};

struct omap3_vc {
	struct voltagedomain *vd;
	u32 voltctrl;
	u32 voltsetup1;
	u32 voltsetup2;
	struct omap3_vc_timings timings[2];
};
static struct omap3_vc vc;

void omap3_vc_set_pmic_signaling(int core_next_state)
{
	struct voltagedomain *vd = vc.vd;
	struct omap3_vc_timings *c = vc.timings;
	u32 voltctrl, voltsetup1, voltsetup2;

	voltctrl = vc.voltctrl;
	voltsetup1 = vc.voltsetup1;
	voltsetup2 = vc.voltsetup2;

	switch (core_next_state) {
	case PWRDM_POWER_OFF:
		voltctrl &= ~(OMAP3430_PRM_VOLTCTRL_AUTO_RET |
			      OMAP3430_PRM_VOLTCTRL_AUTO_SLEEP);
		voltctrl |= OMAP3430_PRM_VOLTCTRL_AUTO_OFF;
		if (voltctrl & OMAP3430_PRM_VOLTCTRL_SEL_OFF)
			voltsetup2 = c->voltsetup2;
		else
			voltsetup1 = c->voltsetup1;
		break;
	case PWRDM_POWER_RET:
	default:
		c++;
		voltctrl &= ~(OMAP3430_PRM_VOLTCTRL_AUTO_OFF |
			      OMAP3430_PRM_VOLTCTRL_AUTO_SLEEP);
		voltctrl |= OMAP3430_PRM_VOLTCTRL_AUTO_RET;
		voltsetup1 = c->voltsetup1;
		break;
	}

	if (voltctrl != vc.voltctrl) {
		vd->write(voltctrl, OMAP3_PRM_VOLTCTRL_OFFSET);
		vc.voltctrl = voltctrl;
	}
	if (voltsetup1 != vc.voltsetup1) {
		vd->write(c->voltsetup1,
			  OMAP3_PRM_VOLTSETUP1_OFFSET);
		vc.voltsetup1 = voltsetup1;
	}
	if (voltsetup2 != vc.voltsetup2) {
		vd->write(c->voltsetup2,
			  OMAP3_PRM_VOLTSETUP2_OFFSET);
		vc.voltsetup2 = voltsetup2;
	}
}

/*
 * Configure signal polarity for sys_clkreq and sys_off_mode pins
 * as the default values are wrong and can cause the system to hang
 * if any twl4030 scripts are loaded.
 */
static void __init omap3_vc_init_pmic_signaling(struct voltagedomain *voltdm)
{
	u32 val;

	if (vc.vd)
		return;

	vc.vd = voltdm;

	val = voltdm->read(OMAP3_PRM_POLCTRL_OFFSET);
	if (!(val & OMAP3430_PRM_POLCTRL_CLKREQ_POL) ||
	    (val & OMAP3430_PRM_POLCTRL_OFFMODE_POL)) {
		val |= OMAP3430_PRM_POLCTRL_CLKREQ_POL;
		val &= ~OMAP3430_PRM_POLCTRL_OFFMODE_POL;
		pr_debug("PM: fixing sys_clkreq and sys_off_mode polarity to 0x%x\n",
			 val);
		voltdm->write(val, OMAP3_PRM_POLCTRL_OFFSET);
	}

	/*
	 * By default let's use I2C4 signaling for retention idle
	 * and sys_off_mode pin signaling for off idle. This way we
	 * have sys_clk_req pin go down for retention and both
	 * sys_clk_req and sys_off_mode pins will go down for off
	 * idle. And we can also scale voltages to zero for off-idle.
	 * Note that no actual voltage scaling during off-idle will
	 * happen unless the board specific twl4030 PMIC scripts are
	 * loaded. See also omap_vc_i2c_init for comments regarding
	 * erratum i531.
	 */
	val = voltdm->read(OMAP3_PRM_VOLTCTRL_OFFSET);
	if (!(val & OMAP3430_PRM_VOLTCTRL_SEL_OFF)) {
		val |= OMAP3430_PRM_VOLTCTRL_SEL_OFF;
		pr_debug("PM: setting voltctrl sys_off_mode signaling to 0x%x\n",
			 val);
		voltdm->write(val, OMAP3_PRM_VOLTCTRL_OFFSET);
	}
	vc.voltctrl = val;

	omap3_vc_set_pmic_signaling(PWRDM_POWER_ON);
}

static void omap3_init_voltsetup1(struct voltagedomain *voltdm,
				  struct omap3_vc_timings *c, u32 idle)
{
	unsigned long val;

	val = (voltdm->vc_param->on - idle) / voltdm->pmic->slew_rate;
	val *= voltdm->sys_clk.rate / 8 / 1000000 + 1;
	val <<= __ffs(voltdm->vfsm->voltsetup_mask);
	c->voltsetup1 &= ~voltdm->vfsm->voltsetup_mask;
	c->voltsetup1 |= val;
}

/**
 * omap3_set_i2c_timings - sets i2c sleep timings for a channel
 * @voltdm: channel to configure
 * @off_mode: select whether retention or off mode values used
 *
 * Calculates and sets up voltage controller to use I2C based
 * voltage scaling for sleep modes. This can be used for either off mode
 * or retention. Off mode has additionally an option to use sys_off_mode
 * pad, which uses a global signal to program the whole power IC to
 * off-mode.
 *
 * Note that pmic is not controlling the voltage scaling during
 * retention signaled over I2C4, so we can keep voltsetup2 as 0.
 * And the oscillator is not shut off over I2C4, so no need to
 * set clksetup.
 */
static void omap3_set_i2c_timings(struct voltagedomain *voltdm)
{
	struct omap3_vc_timings *c = vc.timings;

	/* Configure PRWDM_POWER_OFF over I2C4 */
	omap3_init_voltsetup1(voltdm, c, voltdm->vc_param->off);
	c++;
	/* Configure PRWDM_POWER_RET over I2C4 */
	omap3_init_voltsetup1(voltdm, c, voltdm->vc_param->ret);
}

/**
 * omap3_set_off_timings - sets off-mode timings for a channel
 * @voltdm: channel to configure
 *
 * Calculates and sets up off-mode timings for a channel. Off-mode
 * can use either I2C based voltage scaling, or alternatively
 * sys_off_mode pad can be used to send a global command to power IC.n,
 * sys_off_mode has the additional benefit that voltages can be
 * scaled to zero volt level with TWL4030 / TWL5030, I2C can only
 * scale to 600mV.
 *
 * Note that omap is not controlling the voltage scaling during
 * off idle signaled by sys_off_mode, so we can keep voltsetup1
 * as 0.
 */
static void omap3_set_off_timings(struct voltagedomain *voltdm)
{
	struct omap3_vc_timings *c = vc.timings;
	u32 tstart, tshut, clksetup, voltoffset;

	if (c->voltsetup2)
		return;

	omap_pm_get_oscillator(&tstart, &tshut);
	if (tstart == ULONG_MAX) {
		pr_debug("PM: oscillator start-up time not initialized, using 10ms\n");
		clksetup = omap_usec_to_32k(10000);
	} else {
		clksetup = omap_usec_to_32k(tstart);
	}

	/*
	 * For twl4030 errata 27, we need to allow minimum ~488.32 us wait to
	 * switch from HFCLKIN to internal oscillator. That means timings
	 * have voltoffset fixed to 0xa in rounded up 32 KiHz cycles. And
	 * that means we can calculate the value based on the oscillator
	 * start-up time since voltoffset2 = clksetup - voltoffset.
	 */
	voltoffset = omap_usec_to_32k(488);
	c->voltsetup2 = clksetup - voltoffset;
	voltdm->write(clksetup, OMAP3_PRM_CLKSETUP_OFFSET);
	voltdm->write(voltoffset, OMAP3_PRM_VOLTOFFSET_OFFSET);
}

static void __init omap3_vc_init_channel(struct voltagedomain *voltdm)
{
	omap3_vc_init_pmic_signaling(voltdm);
	omap3_set_off_timings(voltdm);
	omap3_set_i2c_timings(voltdm);
}

/**
 * omap4_calc_volt_ramp - calculates voltage ramping delays on omap4
 * @voltdm: channel to calculate values for
 * @voltage_diff: voltage difference in microvolts
 *
 * Calculates voltage ramp prescaler + counter values for a voltage
 * difference on omap4. Returns a field value suitable for writing to
 * VOLTSETUP register for a channel in following format:
 * bits[8:9] prescaler ... bits[0:5] counter. See OMAP4 TRM for reference.
 */
static u32 omap4_calc_volt_ramp(struct voltagedomain *voltdm, u32 voltage_diff)
{
	u32 prescaler;
	u32 cycles;
	u32 time;

	time = voltage_diff / voltdm->pmic->slew_rate;

	cycles = voltdm->sys_clk.rate / 1000 * time / 1000;

	cycles /= 64;
	prescaler = 0;

	/* shift to next prescaler until no overflow */

	/* scale for div 256 = 64 * 4 */
	if (cycles > 63) {
		cycles /= 4;
		prescaler++;
	}

	/* scale for div 512 = 256 * 2 */
	if (cycles > 63) {
		cycles /= 2;
		prescaler++;
	}

	/* scale for div 2048 = 512 * 4 */
	if (cycles > 63) {
		cycles /= 4;
		prescaler++;
	}

	/* check for overflow => invalid ramp time */
	if (cycles > 63) {
		pr_warn("%s: invalid setuptime for vdd_%s\n", __func__,
			voltdm->name);
		return 0;
	}

	cycles++;

	return (prescaler << OMAP4430_RAMP_UP_PRESCAL_SHIFT) |
		(cycles << OMAP4430_RAMP_UP_COUNT_SHIFT);
}

/**
 * omap4_usec_to_val_scrm - convert microsecond value to SCRM module bitfield
 * @usec: microseconds
 * @shift: number of bits to shift left
 * @mask: bitfield mask
 *
 * Converts microsecond value to OMAP4 SCRM bitfield. Bitfield is
 * shifted to requested position, and checked agains the mask value.
 * If larger, forced to the max value of the field (i.e. the mask itself.)
 * Returns the SCRM bitfield value.
 */
static u32 omap4_usec_to_val_scrm(u32 usec, int shift, u32 mask)
{
	u32 val;

	val = omap_usec_to_32k(usec) << shift;

	/* Check for overflow, if yes, force to max value */
	if (val > mask)
		val = mask;

	return val;
}

/**
 * omap4_set_timings - set voltage ramp timings for a channel
 * @voltdm: channel to configure
 * @off_mode: whether off-mode values are used
 *
 * Calculates and sets the voltage ramp up / down values for a channel.
 */
static void omap4_set_timings(struct voltagedomain *voltdm, bool off_mode)
{
	u32 val;
	u32 ramp;
	int offset;
	u32 tstart, tshut;

	if (off_mode) {
		ramp = omap4_calc_volt_ramp(voltdm,
			voltdm->vc_param->on - voltdm->vc_param->off);
		offset = voltdm->vfsm->voltsetup_off_reg;
	} else {
		ramp = omap4_calc_volt_ramp(voltdm,
			voltdm->vc_param->on - voltdm->vc_param->ret);
		offset = voltdm->vfsm->voltsetup_reg;
	}

	if (!ramp)
		return;

	val = voltdm->read(offset);

	val |= ramp << OMAP4430_RAMP_DOWN_COUNT_SHIFT;

	val |= ramp << OMAP4430_RAMP_UP_COUNT_SHIFT;

	voltdm->write(val, offset);

	omap_pm_get_oscillator(&tstart, &tshut);

	val = omap4_usec_to_val_scrm(tstart, OMAP4_SETUPTIME_SHIFT,
		OMAP4_SETUPTIME_MASK);
	val |= omap4_usec_to_val_scrm(tshut, OMAP4_DOWNTIME_SHIFT,
		OMAP4_DOWNTIME_MASK);

	writel_relaxed(val, OMAP4_SCRM_CLKSETUPTIME);
}

/* OMAP4 specific voltage init functions */
static void __init omap4_vc_init_channel(struct voltagedomain *voltdm)
{
	omap4_set_timings(voltdm, true);
	omap4_set_timings(voltdm, false);
}

struct i2c_init_data {
	u8 loadbits;
	u8 load;
	u8 hsscll_38_4;
	u8 hsscll_26;
	u8 hsscll_19_2;
	u8 hsscll_16_8;
	u8 hsscll_12;
};

static const struct i2c_init_data omap4_i2c_timing_data[] __initconst = {
	{
		.load = 50,
		.loadbits = 0x3,
		.hsscll_38_4 = 13,
		.hsscll_26 = 11,
		.hsscll_19_2 = 9,
		.hsscll_16_8 = 9,
		.hsscll_12 = 8,
	},
	{
		.load = 25,
		.loadbits = 0x2,
		.hsscll_38_4 = 13,
		.hsscll_26 = 11,
		.hsscll_19_2 = 9,
		.hsscll_16_8 = 9,
		.hsscll_12 = 8,
	},
	{
		.load = 12,
		.loadbits = 0x1,
		.hsscll_38_4 = 11,
		.hsscll_26 = 10,
		.hsscll_19_2 = 9,
		.hsscll_16_8 = 9,
		.hsscll_12 = 8,
	},
	{
		.load = 0,
		.loadbits = 0x0,
		.hsscll_38_4 = 12,
		.hsscll_26 = 10,
		.hsscll_19_2 = 9,
		.hsscll_16_8 = 8,
		.hsscll_12 = 8,
	},
};

/**
 * omap4_vc_i2c_timing_init - sets up board I2C timing parameters
 * @voltdm: voltagedomain pointer to get data from
 *
 * Use PMIC + board supplied settings for calculating the total I2C
 * channel capacitance and set the timing parameters based on this.
 * Pre-calculated values are provided in data tables, as it is not
 * too straightforward to calculate these runtime.
 */
static void __init omap4_vc_i2c_timing_init(struct voltagedomain *voltdm)
{
	u32 capacitance;
	u32 val;
	u16 hsscll;
	const struct i2c_init_data *i2c_data;

	if (!voltdm->pmic->i2c_high_speed) {
		pr_warn("%s: only high speed supported!\n", __func__);
		return;
	}

	/* PCB trace capacitance, 0.125pF / mm => mm / 8 */
	capacitance = DIV_ROUND_UP(sr_i2c_pcb_length, 8);

	/* OMAP pad capacitance */
	capacitance += 4;

	/* PMIC pad capacitance */
	capacitance += voltdm->pmic->i2c_pad_load;

	/* Search for capacitance match in the table */
	i2c_data = omap4_i2c_timing_data;

	while (i2c_data->load > capacitance)
		i2c_data++;

	/* Select proper values based on sysclk frequency */
	switch (voltdm->sys_clk.rate) {
	case 38400000:
		hsscll = i2c_data->hsscll_38_4;
		break;
	case 26000000:
		hsscll = i2c_data->hsscll_26;
		break;
	case 19200000:
		hsscll = i2c_data->hsscll_19_2;
		break;
	case 16800000:
		hsscll = i2c_data->hsscll_16_8;
		break;
	case 12000000:
		hsscll = i2c_data->hsscll_12;
		break;
	default:
		pr_warn("%s: unsupported sysclk rate: %d!\n", __func__,
			voltdm->sys_clk.rate);
		return;
	}

	/* Loadbits define pull setup for the I2C channels */
	val = i2c_data->loadbits << 25 | i2c_data->loadbits << 29;

	/* Write to SYSCTRL_PADCONF_WKUP_CTRL_I2C_2 to setup I2C pull */
	writel_relaxed(val, OMAP2_L4_IO_ADDRESS(OMAP4_CTRL_MODULE_PAD_WKUP +
				OMAP4_CTRL_MODULE_PAD_WKUP_CONTROL_I2C_2));

	/* HSSCLH can always be zero */
	val = hsscll << OMAP4430_HSSCLL_SHIFT;
	val |= (0x28 << OMAP4430_SCLL_SHIFT | 0x2c << OMAP4430_SCLH_SHIFT);

	/* Write setup times to I2C config register */
	voltdm->write(val, OMAP4_PRM_VC_CFG_I2C_CLK_OFFSET);
}



/**
 * omap_vc_i2c_init - initialize I2C interface to PMIC
 * @voltdm: voltage domain containing VC data
 *
 * Use PMIC supplied settings for I2C high-speed mode and
 * master code (if set) and program the VC I2C configuration
 * register.
 *
 * The VC I2C configuration is common to all VC channels,
 * so this function only configures I2C for the first VC
 * channel registers.  All other VC channels will use the
 * same configuration.
 */
static void __init omap_vc_i2c_init(struct voltagedomain *voltdm)
{
	struct omap_vc_channel *vc = voltdm->vc;
	static bool initialized;
	static bool i2c_high_speed;
	u8 mcode;

	if (initialized) {
		if (voltdm->pmic->i2c_high_speed != i2c_high_speed)
			pr_warn("%s: I2C config for vdd_%s does not match other channels (%u).\n",
				__func__, voltdm->name, i2c_high_speed);
		return;
	}

	/*
	 * Note that for omap3 OMAP3430_SREN_MASK clears SREN to work around
	 * erratum i531 "Extra Power Consumed When Repeated Start Operation
	 * Mode Is Enabled on I2C Interface Dedicated for Smart Reflex (I2C4)".
	 * Otherwise I2C4 eventually leads into about 23mW extra power being
	 * consumed even during off idle using VMODE.
	 */
	i2c_high_speed = voltdm->pmic->i2c_high_speed;
	if (i2c_high_speed)
		voltdm->rmw(vc->common->i2c_cfg_clear_mask,
			    vc->common->i2c_cfg_hsen_mask,
			    vc->common->i2c_cfg_reg);

	mcode = voltdm->pmic->i2c_mcode;
	if (mcode)
		voltdm->rmw(vc->common->i2c_mcode_mask,
			    mcode << __ffs(vc->common->i2c_mcode_mask),
			    vc->common->i2c_cfg_reg);

	if (cpu_is_omap44xx())
		omap4_vc_i2c_timing_init(voltdm);

	initialized = true;
}

/**
 * omap_vc_calc_vsel - calculate vsel value for a channel
 * @voltdm: channel to calculate value for
 * @uvolt: microvolt value to convert to vsel
 *
 * Converts a microvolt value to vsel value for the used PMIC.
 * This checks whether the microvolt value is out of bounds, and
 * adjusts the value accordingly. If unsupported value detected,
 * warning is thrown.
 */
static u8 omap_vc_calc_vsel(struct voltagedomain *voltdm, u32 uvolt)
{
	if (voltdm->pmic->vddmin > uvolt)
		uvolt = voltdm->pmic->vddmin;
	if (voltdm->pmic->vddmax < uvolt) {
		WARN(1, "%s: voltage not supported by pmic: %u vs max %u\n",
			__func__, uvolt, voltdm->pmic->vddmax);
		/* Lets try maximum value anyway */
		uvolt = voltdm->pmic->vddmax;
	}

	return voltdm->pmic->uv_to_vsel(uvolt);
}

#ifdef CONFIG_PM
/**
 * omap_pm_setup_sr_i2c_pcb_length - set length of SR I2C traces on PCB
 * @mm: length of the PCB trace in millimetres
 *
 * Sets the PCB trace length for the I2C channel. By default uses 63mm.
 * This is needed for properly calculating the capacitance value for
 * the PCB trace, and for setting the SR I2C channel timing parameters.
 */
void __init omap_pm_setup_sr_i2c_pcb_length(u32 mm)
{
	sr_i2c_pcb_length = mm;
}
#endif

void __init omap_vc_init_channel(struct voltagedomain *voltdm)
{
	struct omap_vc_channel *vc = voltdm->vc;
	u8 on_vsel, onlp_vsel, ret_vsel, off_vsel;
	u32 val;

	if (!voltdm->pmic || !voltdm->pmic->uv_to_vsel) {
		pr_err("%s: No PMIC info for vdd_%s\n", __func__, voltdm->name);
		return;
	}

	if (!voltdm->read || !voltdm->write) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return;
	}

	vc->cfg_channel = 0;
	if (vc->flags & OMAP_VC_CHANNEL_CFG_MUTANT)
		vc_cfg_bits = &vc_mutant_channel_cfg;
	else
		vc_cfg_bits = &vc_default_channel_cfg;

	/* get PMIC/board specific settings */
	vc->i2c_slave_addr = voltdm->pmic->i2c_slave_addr;
	vc->volt_reg_addr = voltdm->pmic->volt_reg_addr;
	vc->cmd_reg_addr = voltdm->pmic->cmd_reg_addr;

	/* Configure the i2c slave address for this VC */
	voltdm->rmw(vc->smps_sa_mask,
		    vc->i2c_slave_addr << __ffs(vc->smps_sa_mask),
		    vc->smps_sa_reg);
	vc->cfg_channel |= vc_cfg_bits->sa;

	/*
	 * Configure the PMIC register addresses.
	 */
	voltdm->rmw(vc->smps_volra_mask,
		    vc->volt_reg_addr << __ffs(vc->smps_volra_mask),
		    vc->smps_volra_reg);
	vc->cfg_channel |= vc_cfg_bits->rav;

	if (vc->cmd_reg_addr) {
		voltdm->rmw(vc->smps_cmdra_mask,
			    vc->cmd_reg_addr << __ffs(vc->smps_cmdra_mask),
			    vc->smps_cmdra_reg);
		vc->cfg_channel |= vc_cfg_bits->rac;
	}

	if (vc->cmd_reg_addr == vc->volt_reg_addr)
		vc->cfg_channel |= vc_cfg_bits->racen;

	/* Set up the on, inactive, retention and off voltage */
	on_vsel = omap_vc_calc_vsel(voltdm, voltdm->vc_param->on);
	onlp_vsel = omap_vc_calc_vsel(voltdm, voltdm->vc_param->onlp);
	ret_vsel = omap_vc_calc_vsel(voltdm, voltdm->vc_param->ret);
	off_vsel = omap_vc_calc_vsel(voltdm, voltdm->vc_param->off);

	val = ((on_vsel << vc->common->cmd_on_shift) |
	       (onlp_vsel << vc->common->cmd_onlp_shift) |
	       (ret_vsel << vc->common->cmd_ret_shift) |
	       (off_vsel << vc->common->cmd_off_shift));
	voltdm->write(val, vc->cmdval_reg);
	vc->cfg_channel |= vc_cfg_bits->cmd;

	/* Channel configuration */
	omap_vc_config_channel(voltdm);

	omap_vc_i2c_init(voltdm);

	if (cpu_is_omap34xx())
		omap3_vc_init_channel(voltdm);
	else if (cpu_is_omap44xx())
		omap4_vc_init_channel(voltdm);
}

