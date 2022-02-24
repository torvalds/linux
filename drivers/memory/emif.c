// SPDX-License-Identifier: GPL-2.0-only
/*
 * EMIF driver
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Aneesh V <aneesh@ti.com>
 * Santosh Shilimkar <santosh.shilimkar@ti.com>
 */
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/platform_data/emif_plat.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/pm.h>

#include "emif.h"
#include "jedec_ddr.h"
#include "of_memory.h"

/**
 * struct emif_data - Per device static data for driver's use
 * @duplicate:			Whether the DDR devices attached to this EMIF
 *				instance are exactly same as that on EMIF1. In
 *				this case we can save some memory and processing
 * @temperature_level:		Maximum temperature of LPDDR2 devices attached
 *				to this EMIF - read from MR4 register. If there
 *				are two devices attached to this EMIF, this
 *				value is the maximum of the two temperature
 *				levels.
 * @node:			node in the device list
 * @base:			base address of memory-mapped IO registers.
 * @dev:			device pointer.
 * @regs_cache:			An array of 'struct emif_regs' that stores
 *				calculated register values for different
 *				frequencies, to avoid re-calculating them on
 *				each DVFS transition.
 * @curr_regs:			The set of register values used in the last
 *				frequency change (i.e. corresponding to the
 *				frequency in effect at the moment)
 * @plat_data:			Pointer to saved platform data.
 * @debugfs_root:		dentry to the root folder for EMIF in debugfs
 * @np_ddr:			Pointer to ddr device tree node
 */
struct emif_data {
	u8				duplicate;
	u8				temperature_level;
	u8				lpmode;
	struct list_head		node;
	unsigned long			irq_state;
	void __iomem			*base;
	struct device			*dev;
	struct emif_regs		*regs_cache[EMIF_MAX_NUM_FREQUENCIES];
	struct emif_regs		*curr_regs;
	struct emif_platform_data	*plat_data;
	struct dentry			*debugfs_root;
	struct device_node		*np_ddr;
};

static struct emif_data *emif1;
static DEFINE_SPINLOCK(emif_lock);
static unsigned long	irq_state;
static LIST_HEAD(device_list);

#ifdef CONFIG_DEBUG_FS
static void do_emif_regdump_show(struct seq_file *s, struct emif_data *emif,
	struct emif_regs *regs)
{
	u32 type = emif->plat_data->device_info->type;
	u32 ip_rev = emif->plat_data->ip_rev;

	seq_printf(s, "EMIF register cache dump for %dMHz\n",
		regs->freq/1000000);

	seq_printf(s, "ref_ctrl_shdw\t: 0x%08x\n", regs->ref_ctrl_shdw);
	seq_printf(s, "sdram_tim1_shdw\t: 0x%08x\n", regs->sdram_tim1_shdw);
	seq_printf(s, "sdram_tim2_shdw\t: 0x%08x\n", regs->sdram_tim2_shdw);
	seq_printf(s, "sdram_tim3_shdw\t: 0x%08x\n", regs->sdram_tim3_shdw);

	if (ip_rev == EMIF_4D) {
		seq_printf(s, "read_idle_ctrl_shdw_normal\t: 0x%08x\n",
			regs->read_idle_ctrl_shdw_normal);
		seq_printf(s, "read_idle_ctrl_shdw_volt_ramp\t: 0x%08x\n",
			regs->read_idle_ctrl_shdw_volt_ramp);
	} else if (ip_rev == EMIF_4D5) {
		seq_printf(s, "dll_calib_ctrl_shdw_normal\t: 0x%08x\n",
			regs->dll_calib_ctrl_shdw_normal);
		seq_printf(s, "dll_calib_ctrl_shdw_volt_ramp\t: 0x%08x\n",
			regs->dll_calib_ctrl_shdw_volt_ramp);
	}

	if (type == DDR_TYPE_LPDDR2_S2 || type == DDR_TYPE_LPDDR2_S4) {
		seq_printf(s, "ref_ctrl_shdw_derated\t: 0x%08x\n",
			regs->ref_ctrl_shdw_derated);
		seq_printf(s, "sdram_tim1_shdw_derated\t: 0x%08x\n",
			regs->sdram_tim1_shdw_derated);
		seq_printf(s, "sdram_tim3_shdw_derated\t: 0x%08x\n",
			regs->sdram_tim3_shdw_derated);
	}
}

static int emif_regdump_show(struct seq_file *s, void *unused)
{
	struct emif_data	*emif	= s->private;
	struct emif_regs	**regs_cache;
	int			i;

	if (emif->duplicate)
		regs_cache = emif1->regs_cache;
	else
		regs_cache = emif->regs_cache;

	for (i = 0; i < EMIF_MAX_NUM_FREQUENCIES && regs_cache[i]; i++) {
		do_emif_regdump_show(s, emif, regs_cache[i]);
		seq_putc(s, '\n');
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(emif_regdump);

static int emif_mr4_show(struct seq_file *s, void *unused)
{
	struct emif_data *emif = s->private;

	seq_printf(s, "MR4=%d\n", emif->temperature_level);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(emif_mr4);

static int __init_or_module emif_debugfs_init(struct emif_data *emif)
{
	emif->debugfs_root = debugfs_create_dir(dev_name(emif->dev), NULL);
	debugfs_create_file("regcache_dump", S_IRUGO, emif->debugfs_root, emif,
			    &emif_regdump_fops);
	debugfs_create_file("mr4", S_IRUGO, emif->debugfs_root, emif,
			    &emif_mr4_fops);
	return 0;
}

static void __exit emif_debugfs_exit(struct emif_data *emif)
{
	debugfs_remove_recursive(emif->debugfs_root);
	emif->debugfs_root = NULL;
}
#else
static inline int __init_or_module emif_debugfs_init(struct emif_data *emif)
{
	return 0;
}

static inline void __exit emif_debugfs_exit(struct emif_data *emif)
{
}
#endif

/*
 * Get bus width used by EMIF. Note that this may be different from the
 * bus width of the DDR devices used. For instance two 16-bit DDR devices
 * may be connected to a given CS of EMIF. In this case bus width as far
 * as EMIF is concerned is 32, where as the DDR bus width is 16 bits.
 */
static u32 get_emif_bus_width(struct emif_data *emif)
{
	u32		width;
	void __iomem	*base = emif->base;

	width = (readl(base + EMIF_SDRAM_CONFIG) & NARROW_MODE_MASK)
			>> NARROW_MODE_SHIFT;
	width = width == 0 ? 32 : 16;

	return width;
}

static void set_lpmode(struct emif_data *emif, u8 lpmode)
{
	u32 temp;
	void __iomem *base = emif->base;

	/*
	 * Workaround for errata i743 - LPDDR2 Power-Down State is Not
	 * Efficient
	 *
	 * i743 DESCRIPTION:
	 * The EMIF supports power-down state for low power. The EMIF
	 * automatically puts the SDRAM into power-down after the memory is
	 * not accessed for a defined number of cycles and the
	 * EMIF_PWR_MGMT_CTRL[10:8] REG_LP_MODE bit field is set to 0x4.
	 * As the EMIF supports automatic output impedance calibration, a ZQ
	 * calibration long command is issued every time it exits active
	 * power-down and precharge power-down modes. The EMIF waits and
	 * blocks any other command during this calibration.
	 * The EMIF does not allow selective disabling of ZQ calibration upon
	 * exit of power-down mode. Due to very short periods of power-down
	 * cycles, ZQ calibration overhead creates bandwidth issues and
	 * increases overall system power consumption. On the other hand,
	 * issuing ZQ calibration long commands when exiting self-refresh is
	 * still required.
	 *
	 * WORKAROUND
	 * Because there is no power consumption benefit of the power-down due
	 * to the calibration and there is a performance risk, the guideline
	 * is to not allow power-down state and, therefore, to not have set
	 * the EMIF_PWR_MGMT_CTRL[10:8] REG_LP_MODE bit field to 0x4.
	 */
	if ((emif->plat_data->ip_rev == EMIF_4D) &&
	    (lpmode == EMIF_LP_MODE_PWR_DN)) {
		WARN_ONCE(1,
			  "REG_LP_MODE = LP_MODE_PWR_DN(4) is prohibited by erratum i743 switch to LP_MODE_SELF_REFRESH(2)\n");
		/* rollback LP_MODE to Self-refresh mode */
		lpmode = EMIF_LP_MODE_SELF_REFRESH;
	}

	temp = readl(base + EMIF_POWER_MANAGEMENT_CONTROL);
	temp &= ~LP_MODE_MASK;
	temp |= (lpmode << LP_MODE_SHIFT);
	writel(temp, base + EMIF_POWER_MANAGEMENT_CONTROL);
}

static void do_freq_update(void)
{
	struct emif_data *emif;

	/*
	 * Workaround for errata i728: Disable LPMODE during FREQ_UPDATE
	 *
	 * i728 DESCRIPTION:
	 * The EMIF automatically puts the SDRAM into self-refresh mode
	 * after the EMIF has not performed accesses during
	 * EMIF_PWR_MGMT_CTRL[7:4] REG_SR_TIM number of DDR clock cycles
	 * and the EMIF_PWR_MGMT_CTRL[10:8] REG_LP_MODE bit field is set
	 * to 0x2. If during a small window the following three events
	 * occur:
	 * - The SR_TIMING counter expires
	 * - And frequency change is requested
	 * - And OCP access is requested
	 * Then it causes instable clock on the DDR interface.
	 *
	 * WORKAROUND
	 * To avoid the occurrence of the three events, the workaround
	 * is to disable the self-refresh when requesting a frequency
	 * change. Before requesting a frequency change the software must
	 * program EMIF_PWR_MGMT_CTRL[10:8] REG_LP_MODE to 0x0. When the
	 * frequency change has been done, the software can reprogram
	 * EMIF_PWR_MGMT_CTRL[10:8] REG_LP_MODE to 0x2
	 */
	list_for_each_entry(emif, &device_list, node) {
		if (emif->lpmode == EMIF_LP_MODE_SELF_REFRESH)
			set_lpmode(emif, EMIF_LP_MODE_DISABLE);
	}

	/*
	 * TODO: Do FREQ_UPDATE here when an API
	 * is available for this as part of the new
	 * clock framework
	 */

	list_for_each_entry(emif, &device_list, node) {
		if (emif->lpmode == EMIF_LP_MODE_SELF_REFRESH)
			set_lpmode(emif, EMIF_LP_MODE_SELF_REFRESH);
	}
}

/* Find addressing table entry based on the device's type and density */
static const struct lpddr2_addressing *get_addressing_table(
	const struct ddr_device_info *device_info)
{
	u32		index, type, density;

	type = device_info->type;
	density = device_info->density;

	switch (type) {
	case DDR_TYPE_LPDDR2_S4:
		index = density - 1;
		break;
	case DDR_TYPE_LPDDR2_S2:
		switch (density) {
		case DDR_DENSITY_1Gb:
		case DDR_DENSITY_2Gb:
			index = density + 3;
			break;
		default:
			index = density - 1;
		}
		break;
	default:
		return NULL;
	}

	return &lpddr2_jedec_addressing_table[index];
}

static u32 get_zq_config_reg(const struct lpddr2_addressing *addressing,
		bool cs1_used, bool cal_resistors_per_cs)
{
	u32 zq = 0, val = 0;

	val = EMIF_ZQCS_INTERVAL_US * 1000 / addressing->tREFI_ns;
	zq |= val << ZQ_REFINTERVAL_SHIFT;

	val = DIV_ROUND_UP(T_ZQCL_DEFAULT_NS, T_ZQCS_DEFAULT_NS) - 1;
	zq |= val << ZQ_ZQCL_MULT_SHIFT;

	val = DIV_ROUND_UP(T_ZQINIT_DEFAULT_NS, T_ZQCL_DEFAULT_NS) - 1;
	zq |= val << ZQ_ZQINIT_MULT_SHIFT;

	zq |= ZQ_SFEXITEN_ENABLE << ZQ_SFEXITEN_SHIFT;

	if (cal_resistors_per_cs)
		zq |= ZQ_DUALCALEN_ENABLE << ZQ_DUALCALEN_SHIFT;
	else
		zq |= ZQ_DUALCALEN_DISABLE << ZQ_DUALCALEN_SHIFT;

	zq |= ZQ_CS0EN_MASK; /* CS0 is used for sure */

	val = cs1_used ? 1 : 0;
	zq |= val << ZQ_CS1EN_SHIFT;

	return zq;
}

static u32 get_temp_alert_config(const struct lpddr2_addressing *addressing,
		const struct emif_custom_configs *custom_configs, bool cs1_used,
		u32 sdram_io_width, u32 emif_bus_width)
{
	u32 alert = 0, interval, devcnt;

	if (custom_configs && (custom_configs->mask &
				EMIF_CUSTOM_CONFIG_TEMP_ALERT_POLL_INTERVAL))
		interval = custom_configs->temp_alert_poll_interval_ms;
	else
		interval = TEMP_ALERT_POLL_INTERVAL_DEFAULT_MS;

	interval *= 1000000;			/* Convert to ns */
	interval /= addressing->tREFI_ns;	/* Convert to refresh cycles */
	alert |= (interval << TA_REFINTERVAL_SHIFT);

	/*
	 * sdram_io_width is in 'log2(x) - 1' form. Convert emif_bus_width
	 * also to this form and subtract to get TA_DEVCNT, which is
	 * in log2(x) form.
	 */
	emif_bus_width = __fls(emif_bus_width) - 1;
	devcnt = emif_bus_width - sdram_io_width;
	alert |= devcnt << TA_DEVCNT_SHIFT;

	/* DEVWDT is in 'log2(x) - 3' form */
	alert |= (sdram_io_width - 2) << TA_DEVWDT_SHIFT;

	alert |= 1 << TA_SFEXITEN_SHIFT;
	alert |= 1 << TA_CS0EN_SHIFT;
	alert |= (cs1_used ? 1 : 0) << TA_CS1EN_SHIFT;

	return alert;
}

static u32 get_pwr_mgmt_ctrl(u32 freq, struct emif_data *emif, u32 ip_rev)
{
	u32 pwr_mgmt_ctrl	= 0, timeout;
	u32 lpmode		= EMIF_LP_MODE_SELF_REFRESH;
	u32 timeout_perf	= EMIF_LP_MODE_TIMEOUT_PERFORMANCE;
	u32 timeout_pwr		= EMIF_LP_MODE_TIMEOUT_POWER;
	u32 freq_threshold	= EMIF_LP_MODE_FREQ_THRESHOLD;
	u32 mask;
	u8 shift;

	struct emif_custom_configs *cust_cfgs = emif->plat_data->custom_configs;

	if (cust_cfgs && (cust_cfgs->mask & EMIF_CUSTOM_CONFIG_LPMODE)) {
		lpmode		= cust_cfgs->lpmode;
		timeout_perf	= cust_cfgs->lpmode_timeout_performance;
		timeout_pwr	= cust_cfgs->lpmode_timeout_power;
		freq_threshold  = cust_cfgs->lpmode_freq_threshold;
	}

	/* Timeout based on DDR frequency */
	timeout = freq >= freq_threshold ? timeout_perf : timeout_pwr;

	/*
	 * The value to be set in register is "log2(timeout) - 3"
	 * if timeout < 16 load 0 in register
	 * if timeout is not a power of 2, round to next highest power of 2
	 */
	if (timeout < 16) {
		timeout = 0;
	} else {
		if (timeout & (timeout - 1))
			timeout <<= 1;
		timeout = __fls(timeout) - 3;
	}

	switch (lpmode) {
	case EMIF_LP_MODE_CLOCK_STOP:
		shift = CS_TIM_SHIFT;
		mask = CS_TIM_MASK;
		break;
	case EMIF_LP_MODE_SELF_REFRESH:
		/* Workaround for errata i735 */
		if (timeout < 6)
			timeout = 6;

		shift = SR_TIM_SHIFT;
		mask = SR_TIM_MASK;
		break;
	case EMIF_LP_MODE_PWR_DN:
		shift = PD_TIM_SHIFT;
		mask = PD_TIM_MASK;
		break;
	case EMIF_LP_MODE_DISABLE:
	default:
		mask = 0;
		shift = 0;
		break;
	}
	/* Round to maximum in case of overflow, BUT warn! */
	if (lpmode != EMIF_LP_MODE_DISABLE && timeout > mask >> shift) {
		pr_err("TIMEOUT Overflow - lpmode=%d perf=%d pwr=%d freq=%d\n",
		       lpmode,
		       timeout_perf,
		       timeout_pwr,
		       freq_threshold);
		WARN(1, "timeout=0x%02x greater than 0x%02x. Using max\n",
		     timeout, mask >> shift);
		timeout = mask >> shift;
	}

	/* Setup required timing */
	pwr_mgmt_ctrl = (timeout << shift) & mask;
	/* setup a default mask for rest of the modes */
	pwr_mgmt_ctrl |= (SR_TIM_MASK | CS_TIM_MASK | PD_TIM_MASK) &
			  ~mask;

	/* No CS_TIM in EMIF_4D5 */
	if (ip_rev == EMIF_4D5)
		pwr_mgmt_ctrl &= ~CS_TIM_MASK;

	pwr_mgmt_ctrl |= lpmode << LP_MODE_SHIFT;

	return pwr_mgmt_ctrl;
}

/*
 * Get the temperature level of the EMIF instance:
 * Reads the MR4 register of attached SDRAM parts to find out the temperature
 * level. If there are two parts attached(one on each CS), then the temperature
 * level for the EMIF instance is the higher of the two temperatures.
 */
static void get_temperature_level(struct emif_data *emif)
{
	u32		temp, temperature_level;
	void __iomem	*base;

	base = emif->base;

	/* Read mode register 4 */
	writel(DDR_MR4, base + EMIF_LPDDR2_MODE_REG_CONFIG);
	temperature_level = readl(base + EMIF_LPDDR2_MODE_REG_DATA);
	temperature_level = (temperature_level & MR4_SDRAM_REF_RATE_MASK) >>
				MR4_SDRAM_REF_RATE_SHIFT;

	if (emif->plat_data->device_info->cs1_used) {
		writel(DDR_MR4 | CS_MASK, base + EMIF_LPDDR2_MODE_REG_CONFIG);
		temp = readl(base + EMIF_LPDDR2_MODE_REG_DATA);
		temp = (temp & MR4_SDRAM_REF_RATE_MASK)
				>> MR4_SDRAM_REF_RATE_SHIFT;
		temperature_level = max(temp, temperature_level);
	}

	/* treat everything less than nominal(3) in MR4 as nominal */
	if (unlikely(temperature_level < SDRAM_TEMP_NOMINAL))
		temperature_level = SDRAM_TEMP_NOMINAL;

	/* if we get reserved value in MR4 persist with the existing value */
	if (likely(temperature_level != SDRAM_TEMP_RESERVED_4))
		emif->temperature_level = temperature_level;
}

/*
 * setup_temperature_sensitive_regs() - set the timings for temperature
 * sensitive registers. This happens once at initialisation time based
 * on the temperature at boot time and subsequently based on the temperature
 * alert interrupt. Temperature alert can happen when the temperature
 * increases or drops. So this function can have the effect of either
 * derating the timings or going back to nominal values.
 */
static void setup_temperature_sensitive_regs(struct emif_data *emif,
		struct emif_regs *regs)
{
	u32		tim1, tim3, ref_ctrl, type;
	void __iomem	*base = emif->base;
	u32		temperature;

	type = emif->plat_data->device_info->type;

	tim1 = regs->sdram_tim1_shdw;
	tim3 = regs->sdram_tim3_shdw;
	ref_ctrl = regs->ref_ctrl_shdw;

	/* No de-rating for non-lpddr2 devices */
	if (type != DDR_TYPE_LPDDR2_S2 && type != DDR_TYPE_LPDDR2_S4)
		goto out;

	temperature = emif->temperature_level;
	if (temperature == SDRAM_TEMP_HIGH_DERATE_REFRESH) {
		ref_ctrl = regs->ref_ctrl_shdw_derated;
	} else if (temperature == SDRAM_TEMP_HIGH_DERATE_REFRESH_AND_TIMINGS) {
		tim1 = regs->sdram_tim1_shdw_derated;
		tim3 = regs->sdram_tim3_shdw_derated;
		ref_ctrl = regs->ref_ctrl_shdw_derated;
	}

out:
	writel(tim1, base + EMIF_SDRAM_TIMING_1_SHDW);
	writel(tim3, base + EMIF_SDRAM_TIMING_3_SHDW);
	writel(ref_ctrl, base + EMIF_SDRAM_REFRESH_CTRL_SHDW);
}

static irqreturn_t handle_temp_alert(void __iomem *base, struct emif_data *emif)
{
	u32		old_temp_level;
	irqreturn_t	ret = IRQ_HANDLED;
	struct emif_custom_configs *custom_configs;

	spin_lock_irqsave(&emif_lock, irq_state);
	old_temp_level = emif->temperature_level;
	get_temperature_level(emif);

	if (unlikely(emif->temperature_level == old_temp_level)) {
		goto out;
	} else if (!emif->curr_regs) {
		dev_err(emif->dev, "temperature alert before registers are calculated, not de-rating timings\n");
		goto out;
	}

	custom_configs = emif->plat_data->custom_configs;

	/*
	 * IF we detect higher than "nominal rating" from DDR sensor
	 * on an unsupported DDR part, shutdown system
	 */
	if (custom_configs && !(custom_configs->mask &
				EMIF_CUSTOM_CONFIG_EXTENDED_TEMP_PART)) {
		if (emif->temperature_level >= SDRAM_TEMP_HIGH_DERATE_REFRESH) {
			dev_err(emif->dev,
				"%s:NOT Extended temperature capable memory. Converting MR4=0x%02x as shutdown event\n",
				__func__, emif->temperature_level);
			/*
			 * Temperature far too high - do kernel_power_off()
			 * from thread context
			 */
			emif->temperature_level = SDRAM_TEMP_VERY_HIGH_SHUTDOWN;
			ret = IRQ_WAKE_THREAD;
			goto out;
		}
	}

	if (emif->temperature_level < old_temp_level ||
		emif->temperature_level == SDRAM_TEMP_VERY_HIGH_SHUTDOWN) {
		/*
		 * Temperature coming down - defer handling to thread OR
		 * Temperature far too high - do kernel_power_off() from
		 * thread context
		 */
		ret = IRQ_WAKE_THREAD;
	} else {
		/* Temperature is going up - handle immediately */
		setup_temperature_sensitive_regs(emif, emif->curr_regs);
		do_freq_update();
	}

out:
	spin_unlock_irqrestore(&emif_lock, irq_state);
	return ret;
}

static irqreturn_t emif_interrupt_handler(int irq, void *dev_id)
{
	u32			interrupts;
	struct emif_data	*emif = dev_id;
	void __iomem		*base = emif->base;
	struct device		*dev = emif->dev;
	irqreturn_t		ret = IRQ_HANDLED;

	/* Save the status and clear it */
	interrupts = readl(base + EMIF_SYSTEM_OCP_INTERRUPT_STATUS);
	writel(interrupts, base + EMIF_SYSTEM_OCP_INTERRUPT_STATUS);

	/*
	 * Handle temperature alert
	 * Temperature alert should be same for all ports
	 * So, it's enough to process it only for one of the ports
	 */
	if (interrupts & TA_SYS_MASK)
		ret = handle_temp_alert(base, emif);

	if (interrupts & ERR_SYS_MASK)
		dev_err(dev, "Access error from SYS port - %x\n", interrupts);

	if (emif->plat_data->hw_caps & EMIF_HW_CAPS_LL_INTERFACE) {
		/* Save the status and clear it */
		interrupts = readl(base + EMIF_LL_OCP_INTERRUPT_STATUS);
		writel(interrupts, base + EMIF_LL_OCP_INTERRUPT_STATUS);

		if (interrupts & ERR_LL_MASK)
			dev_err(dev, "Access error from LL port - %x\n",
				interrupts);
	}

	return ret;
}

static irqreturn_t emif_threaded_isr(int irq, void *dev_id)
{
	struct emif_data	*emif = dev_id;

	if (emif->temperature_level == SDRAM_TEMP_VERY_HIGH_SHUTDOWN) {
		dev_emerg(emif->dev, "SDRAM temperature exceeds operating limit.. Needs shut down!!!\n");

		/* If we have Power OFF ability, use it, else try restarting */
		if (pm_power_off) {
			kernel_power_off();
		} else {
			WARN(1, "FIXME: NO pm_power_off!!! trying restart\n");
			kernel_restart("SDRAM Over-temp Emergency restart");
		}
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&emif_lock, irq_state);

	if (emif->curr_regs) {
		setup_temperature_sensitive_regs(emif, emif->curr_regs);
		do_freq_update();
	} else {
		dev_err(emif->dev, "temperature alert before registers are calculated, not de-rating timings\n");
	}

	spin_unlock_irqrestore(&emif_lock, irq_state);

	return IRQ_HANDLED;
}

static void clear_all_interrupts(struct emif_data *emif)
{
	void __iomem	*base = emif->base;

	writel(readl(base + EMIF_SYSTEM_OCP_INTERRUPT_STATUS),
		base + EMIF_SYSTEM_OCP_INTERRUPT_STATUS);
	if (emif->plat_data->hw_caps & EMIF_HW_CAPS_LL_INTERFACE)
		writel(readl(base + EMIF_LL_OCP_INTERRUPT_STATUS),
			base + EMIF_LL_OCP_INTERRUPT_STATUS);
}

static void disable_and_clear_all_interrupts(struct emif_data *emif)
{
	void __iomem		*base = emif->base;

	/* Disable all interrupts */
	writel(readl(base + EMIF_SYSTEM_OCP_INTERRUPT_ENABLE_SET),
		base + EMIF_SYSTEM_OCP_INTERRUPT_ENABLE_CLEAR);
	if (emif->plat_data->hw_caps & EMIF_HW_CAPS_LL_INTERFACE)
		writel(readl(base + EMIF_LL_OCP_INTERRUPT_ENABLE_SET),
			base + EMIF_LL_OCP_INTERRUPT_ENABLE_CLEAR);

	/* Clear all interrupts */
	clear_all_interrupts(emif);
}

static int __init_or_module setup_interrupts(struct emif_data *emif, u32 irq)
{
	u32		interrupts, type;
	void __iomem	*base = emif->base;

	type = emif->plat_data->device_info->type;

	clear_all_interrupts(emif);

	/* Enable interrupts for SYS interface */
	interrupts = EN_ERR_SYS_MASK;
	if (type == DDR_TYPE_LPDDR2_S2 || type == DDR_TYPE_LPDDR2_S4)
		interrupts |= EN_TA_SYS_MASK;
	writel(interrupts, base + EMIF_SYSTEM_OCP_INTERRUPT_ENABLE_SET);

	/* Enable interrupts for LL interface */
	if (emif->plat_data->hw_caps & EMIF_HW_CAPS_LL_INTERFACE) {
		/* TA need not be enabled for LL */
		interrupts = EN_ERR_LL_MASK;
		writel(interrupts, base + EMIF_LL_OCP_INTERRUPT_ENABLE_SET);
	}

	/* setup IRQ handlers */
	return devm_request_threaded_irq(emif->dev, irq,
				    emif_interrupt_handler,
				    emif_threaded_isr,
				    0, dev_name(emif->dev),
				    emif);

}

static void __init_or_module emif_onetime_settings(struct emif_data *emif)
{
	u32				pwr_mgmt_ctrl, zq, temp_alert_cfg;
	void __iomem			*base = emif->base;
	const struct lpddr2_addressing	*addressing;
	const struct ddr_device_info	*device_info;

	device_info = emif->plat_data->device_info;
	addressing = get_addressing_table(device_info);

	/*
	 * Init power management settings
	 * We don't know the frequency yet. Use a high frequency
	 * value for a conservative timeout setting
	 */
	pwr_mgmt_ctrl = get_pwr_mgmt_ctrl(1000000000, emif,
			emif->plat_data->ip_rev);
	emif->lpmode = (pwr_mgmt_ctrl & LP_MODE_MASK) >> LP_MODE_SHIFT;
	writel(pwr_mgmt_ctrl, base + EMIF_POWER_MANAGEMENT_CONTROL);

	/* Init ZQ calibration settings */
	zq = get_zq_config_reg(addressing, device_info->cs1_used,
		device_info->cal_resistors_per_cs);
	writel(zq, base + EMIF_SDRAM_OUTPUT_IMPEDANCE_CALIBRATION_CONFIG);

	/* Check temperature level temperature level*/
	get_temperature_level(emif);
	if (emif->temperature_level == SDRAM_TEMP_VERY_HIGH_SHUTDOWN)
		dev_emerg(emif->dev, "SDRAM temperature exceeds operating limit.. Needs shut down!!!\n");

	/* Init temperature polling */
	temp_alert_cfg = get_temp_alert_config(addressing,
		emif->plat_data->custom_configs, device_info->cs1_used,
		device_info->io_width, get_emif_bus_width(emif));
	writel(temp_alert_cfg, base + EMIF_TEMPERATURE_ALERT_CONFIG);

	/*
	 * Program external PHY control registers that are not frequency
	 * dependent
	 */
	if (emif->plat_data->phy_type != EMIF_PHY_TYPE_INTELLIPHY)
		return;
	writel(EMIF_EXT_PHY_CTRL_1_VAL, base + EMIF_EXT_PHY_CTRL_1_SHDW);
	writel(EMIF_EXT_PHY_CTRL_5_VAL, base + EMIF_EXT_PHY_CTRL_5_SHDW);
	writel(EMIF_EXT_PHY_CTRL_6_VAL, base + EMIF_EXT_PHY_CTRL_6_SHDW);
	writel(EMIF_EXT_PHY_CTRL_7_VAL, base + EMIF_EXT_PHY_CTRL_7_SHDW);
	writel(EMIF_EXT_PHY_CTRL_8_VAL, base + EMIF_EXT_PHY_CTRL_8_SHDW);
	writel(EMIF_EXT_PHY_CTRL_9_VAL, base + EMIF_EXT_PHY_CTRL_9_SHDW);
	writel(EMIF_EXT_PHY_CTRL_10_VAL, base + EMIF_EXT_PHY_CTRL_10_SHDW);
	writel(EMIF_EXT_PHY_CTRL_11_VAL, base + EMIF_EXT_PHY_CTRL_11_SHDW);
	writel(EMIF_EXT_PHY_CTRL_12_VAL, base + EMIF_EXT_PHY_CTRL_12_SHDW);
	writel(EMIF_EXT_PHY_CTRL_13_VAL, base + EMIF_EXT_PHY_CTRL_13_SHDW);
	writel(EMIF_EXT_PHY_CTRL_14_VAL, base + EMIF_EXT_PHY_CTRL_14_SHDW);
	writel(EMIF_EXT_PHY_CTRL_15_VAL, base + EMIF_EXT_PHY_CTRL_15_SHDW);
	writel(EMIF_EXT_PHY_CTRL_16_VAL, base + EMIF_EXT_PHY_CTRL_16_SHDW);
	writel(EMIF_EXT_PHY_CTRL_17_VAL, base + EMIF_EXT_PHY_CTRL_17_SHDW);
	writel(EMIF_EXT_PHY_CTRL_18_VAL, base + EMIF_EXT_PHY_CTRL_18_SHDW);
	writel(EMIF_EXT_PHY_CTRL_19_VAL, base + EMIF_EXT_PHY_CTRL_19_SHDW);
	writel(EMIF_EXT_PHY_CTRL_20_VAL, base + EMIF_EXT_PHY_CTRL_20_SHDW);
	writel(EMIF_EXT_PHY_CTRL_21_VAL, base + EMIF_EXT_PHY_CTRL_21_SHDW);
	writel(EMIF_EXT_PHY_CTRL_22_VAL, base + EMIF_EXT_PHY_CTRL_22_SHDW);
	writel(EMIF_EXT_PHY_CTRL_23_VAL, base + EMIF_EXT_PHY_CTRL_23_SHDW);
	writel(EMIF_EXT_PHY_CTRL_24_VAL, base + EMIF_EXT_PHY_CTRL_24_SHDW);
}

static void get_default_timings(struct emif_data *emif)
{
	struct emif_platform_data *pd = emif->plat_data;

	pd->timings		= lpddr2_jedec_timings;
	pd->timings_arr_size	= ARRAY_SIZE(lpddr2_jedec_timings);

	dev_warn(emif->dev, "%s: using default timings\n", __func__);
}

static int is_dev_data_valid(u32 type, u32 density, u32 io_width, u32 phy_type,
		u32 ip_rev, struct device *dev)
{
	int valid;

	valid = (type == DDR_TYPE_LPDDR2_S4 ||
			type == DDR_TYPE_LPDDR2_S2)
		&& (density >= DDR_DENSITY_64Mb
			&& density <= DDR_DENSITY_8Gb)
		&& (io_width >= DDR_IO_WIDTH_8
			&& io_width <= DDR_IO_WIDTH_32);

	/* Combinations of EMIF and PHY revisions that we support today */
	switch (ip_rev) {
	case EMIF_4D:
		valid = valid && (phy_type == EMIF_PHY_TYPE_ATTILAPHY);
		break;
	case EMIF_4D5:
		valid = valid && (phy_type == EMIF_PHY_TYPE_INTELLIPHY);
		break;
	default:
		valid = 0;
	}

	if (!valid)
		dev_err(dev, "%s: invalid DDR details\n", __func__);
	return valid;
}

static int is_custom_config_valid(struct emif_custom_configs *cust_cfgs,
		struct device *dev)
{
	int valid = 1;

	if ((cust_cfgs->mask & EMIF_CUSTOM_CONFIG_LPMODE) &&
		(cust_cfgs->lpmode != EMIF_LP_MODE_DISABLE))
		valid = cust_cfgs->lpmode_freq_threshold &&
			cust_cfgs->lpmode_timeout_performance &&
			cust_cfgs->lpmode_timeout_power;

	if (cust_cfgs->mask & EMIF_CUSTOM_CONFIG_TEMP_ALERT_POLL_INTERVAL)
		valid = valid && cust_cfgs->temp_alert_poll_interval_ms;

	if (!valid)
		dev_warn(dev, "%s: invalid custom configs\n", __func__);

	return valid;
}

#if defined(CONFIG_OF)
static void __init_or_module of_get_custom_configs(struct device_node *np_emif,
		struct emif_data *emif)
{
	struct emif_custom_configs	*cust_cfgs = NULL;
	int				len;
	const __be32			*lpmode, *poll_intvl;

	lpmode = of_get_property(np_emif, "low-power-mode", &len);
	poll_intvl = of_get_property(np_emif, "temp-alert-poll-interval", &len);

	if (lpmode || poll_intvl)
		cust_cfgs = devm_kzalloc(emif->dev, sizeof(*cust_cfgs),
			GFP_KERNEL);

	if (!cust_cfgs)
		return;

	if (lpmode) {
		cust_cfgs->mask |= EMIF_CUSTOM_CONFIG_LPMODE;
		cust_cfgs->lpmode = be32_to_cpup(lpmode);
		of_property_read_u32(np_emif,
				"low-power-mode-timeout-performance",
				&cust_cfgs->lpmode_timeout_performance);
		of_property_read_u32(np_emif,
				"low-power-mode-timeout-power",
				&cust_cfgs->lpmode_timeout_power);
		of_property_read_u32(np_emif,
				"low-power-mode-freq-threshold",
				&cust_cfgs->lpmode_freq_threshold);
	}

	if (poll_intvl) {
		cust_cfgs->mask |=
				EMIF_CUSTOM_CONFIG_TEMP_ALERT_POLL_INTERVAL;
		cust_cfgs->temp_alert_poll_interval_ms =
						be32_to_cpup(poll_intvl);
	}

	if (of_find_property(np_emif, "extended-temp-part", &len))
		cust_cfgs->mask |= EMIF_CUSTOM_CONFIG_EXTENDED_TEMP_PART;

	if (!is_custom_config_valid(cust_cfgs, emif->dev)) {
		devm_kfree(emif->dev, cust_cfgs);
		return;
	}

	emif->plat_data->custom_configs = cust_cfgs;
}

static void __init_or_module of_get_ddr_info(struct device_node *np_emif,
		struct device_node *np_ddr,
		struct ddr_device_info *dev_info)
{
	u32 density = 0, io_width = 0;
	int len;

	if (of_find_property(np_emif, "cs1-used", &len))
		dev_info->cs1_used = true;

	if (of_find_property(np_emif, "cal-resistor-per-cs", &len))
		dev_info->cal_resistors_per_cs = true;

	if (of_device_is_compatible(np_ddr, "jedec,lpddr2-s4"))
		dev_info->type = DDR_TYPE_LPDDR2_S4;
	else if (of_device_is_compatible(np_ddr, "jedec,lpddr2-s2"))
		dev_info->type = DDR_TYPE_LPDDR2_S2;

	of_property_read_u32(np_ddr, "density", &density);
	of_property_read_u32(np_ddr, "io-width", &io_width);

	/* Convert from density in Mb to the density encoding in jedc_ddr.h */
	if (density & (density - 1))
		dev_info->density = 0;
	else
		dev_info->density = __fls(density) - 5;

	/* Convert from io_width in bits to io_width encoding in jedc_ddr.h */
	if (io_width & (io_width - 1))
		dev_info->io_width = 0;
	else
		dev_info->io_width = __fls(io_width) - 1;
}

static struct emif_data * __init_or_module of_get_memory_device_details(
		struct device_node *np_emif, struct device *dev)
{
	struct emif_data		*emif = NULL;
	struct ddr_device_info		*dev_info = NULL;
	struct emif_platform_data	*pd = NULL;
	struct device_node		*np_ddr;
	int				len;

	np_ddr = of_parse_phandle(np_emif, "device-handle", 0);
	if (!np_ddr)
		goto error;
	emif	= devm_kzalloc(dev, sizeof(struct emif_data), GFP_KERNEL);
	pd	= devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	dev_info = devm_kzalloc(dev, sizeof(*dev_info), GFP_KERNEL);

	if (!emif || !pd || !dev_info) {
		dev_err(dev, "%s: Out of memory!!\n",
			__func__);
		goto error;
	}

	emif->plat_data		= pd;
	pd->device_info		= dev_info;
	emif->dev		= dev;
	emif->np_ddr		= np_ddr;
	emif->temperature_level	= SDRAM_TEMP_NOMINAL;

	if (of_device_is_compatible(np_emif, "ti,emif-4d"))
		emif->plat_data->ip_rev = EMIF_4D;
	else if (of_device_is_compatible(np_emif, "ti,emif-4d5"))
		emif->plat_data->ip_rev = EMIF_4D5;

	of_property_read_u32(np_emif, "phy-type", &pd->phy_type);

	if (of_find_property(np_emif, "hw-caps-ll-interface", &len))
		pd->hw_caps |= EMIF_HW_CAPS_LL_INTERFACE;

	of_get_ddr_info(np_emif, np_ddr, dev_info);
	if (!is_dev_data_valid(pd->device_info->type, pd->device_info->density,
			pd->device_info->io_width, pd->phy_type, pd->ip_rev,
			emif->dev)) {
		dev_err(dev, "%s: invalid device data!!\n", __func__);
		goto error;
	}
	/*
	 * For EMIF instances other than EMIF1 see if the devices connected
	 * are exactly same as on EMIF1(which is typically the case). If so,
	 * mark it as a duplicate of EMIF1. This will save some memory and
	 * computation.
	 */
	if (emif1 && emif1->np_ddr == np_ddr) {
		emif->duplicate = true;
		goto out;
	} else if (emif1) {
		dev_warn(emif->dev, "%s: Non-symmetric DDR geometry\n",
			__func__);
	}

	of_get_custom_configs(np_emif, emif);
	emif->plat_data->timings = of_get_ddr_timings(np_ddr, emif->dev,
					emif->plat_data->device_info->type,
					&emif->plat_data->timings_arr_size);

	emif->plat_data->min_tck = of_get_min_tck(np_ddr, emif->dev);
	goto out;

error:
	return NULL;
out:
	return emif;
}

#else

static struct emif_data * __init_or_module of_get_memory_device_details(
		struct device_node *np_emif, struct device *dev)
{
	return NULL;
}
#endif

static struct emif_data *__init_or_module get_device_details(
		struct platform_device *pdev)
{
	u32				size;
	struct emif_data		*emif = NULL;
	struct ddr_device_info		*dev_info;
	struct emif_custom_configs	*cust_cfgs;
	struct emif_platform_data	*pd;
	struct device			*dev;
	void				*temp;

	pd = pdev->dev.platform_data;
	dev = &pdev->dev;

	if (!(pd && pd->device_info && is_dev_data_valid(pd->device_info->type,
			pd->device_info->density, pd->device_info->io_width,
			pd->phy_type, pd->ip_rev, dev))) {
		dev_err(dev, "%s: invalid device data\n", __func__);
		goto error;
	}

	emif	= devm_kzalloc(dev, sizeof(*emif), GFP_KERNEL);
	temp	= devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	dev_info = devm_kzalloc(dev, sizeof(*dev_info), GFP_KERNEL);

	if (!emif || !pd || !dev_info) {
		dev_err(dev, "%s:%d: allocation error\n", __func__, __LINE__);
		goto error;
	}

	memcpy(temp, pd, sizeof(*pd));
	pd = temp;
	memcpy(dev_info, pd->device_info, sizeof(*dev_info));

	pd->device_info		= dev_info;
	emif->plat_data		= pd;
	emif->dev		= dev;
	emif->temperature_level	= SDRAM_TEMP_NOMINAL;

	/*
	 * For EMIF instances other than EMIF1 see if the devices connected
	 * are exactly same as on EMIF1(which is typically the case). If so,
	 * mark it as a duplicate of EMIF1 and skip copying timings data.
	 * This will save some memory and some computation later.
	 */
	emif->duplicate = emif1 && (memcmp(dev_info,
		emif1->plat_data->device_info,
		sizeof(struct ddr_device_info)) == 0);

	if (emif->duplicate) {
		pd->timings = NULL;
		pd->min_tck = NULL;
		goto out;
	} else if (emif1) {
		dev_warn(emif->dev, "%s: Non-symmetric DDR geometry\n",
			__func__);
	}

	/*
	 * Copy custom configs - ignore allocation error, if any, as
	 * custom_configs is not very critical
	 */
	cust_cfgs = pd->custom_configs;
	if (cust_cfgs && is_custom_config_valid(cust_cfgs, dev)) {
		temp = devm_kzalloc(dev, sizeof(*cust_cfgs), GFP_KERNEL);
		if (temp)
			memcpy(temp, cust_cfgs, sizeof(*cust_cfgs));
		else
			dev_warn(dev, "%s:%d: allocation error\n", __func__,
				__LINE__);
		pd->custom_configs = temp;
	}

	/*
	 * Copy timings and min-tck values from platform data. If it is not
	 * available or if memory allocation fails, use JEDEC defaults
	 */
	size = sizeof(struct lpddr2_timings) * pd->timings_arr_size;
	if (pd->timings) {
		temp = devm_kzalloc(dev, size, GFP_KERNEL);
		if (temp) {
			memcpy(temp, pd->timings, size);
			pd->timings = temp;
		} else {
			dev_warn(dev, "%s:%d: allocation error\n", __func__,
				__LINE__);
			get_default_timings(emif);
		}
	} else {
		get_default_timings(emif);
	}

	if (pd->min_tck) {
		temp = devm_kzalloc(dev, sizeof(*pd->min_tck), GFP_KERNEL);
		if (temp) {
			memcpy(temp, pd->min_tck, sizeof(*pd->min_tck));
			pd->min_tck = temp;
		} else {
			dev_warn(dev, "%s:%d: allocation error\n", __func__,
				__LINE__);
			pd->min_tck = &lpddr2_jedec_min_tck;
		}
	} else {
		pd->min_tck = &lpddr2_jedec_min_tck;
	}

out:
	return emif;

error:
	return NULL;
}

static int __init_or_module emif_probe(struct platform_device *pdev)
{
	struct emif_data	*emif;
	struct resource		*res;
	int			irq, ret;

	if (pdev->dev.of_node)
		emif = of_get_memory_device_details(pdev->dev.of_node, &pdev->dev);
	else
		emif = get_device_details(pdev);

	if (!emif) {
		pr_err("%s: error getting device data\n", __func__);
		goto error;
	}

	list_add(&emif->node, &device_list);

	/* Save pointers to each other in emif and device structures */
	emif->dev = &pdev->dev;
	platform_set_drvdata(pdev, emif);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	emif->base = devm_ioremap_resource(emif->dev, res);
	if (IS_ERR(emif->base))
		goto error;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		goto error;

	emif_onetime_settings(emif);
	emif_debugfs_init(emif);
	disable_and_clear_all_interrupts(emif);
	ret = setup_interrupts(emif, irq);
	if (ret)
		goto error;

	/* One-time actions taken on probing the first device */
	if (!emif1) {
		emif1 = emif;

		/*
		 * TODO: register notifiers for frequency and voltage
		 * change here once the respective frameworks are
		 * available
		 */
	}

	dev_info(&pdev->dev, "%s: device configured with addr = %p and IRQ%d\n",
		__func__, emif->base, irq);

	return 0;
error:
	return -ENODEV;
}

static int __exit emif_remove(struct platform_device *pdev)
{
	struct emif_data *emif = platform_get_drvdata(pdev);

	emif_debugfs_exit(emif);

	return 0;
}

static void emif_shutdown(struct platform_device *pdev)
{
	struct emif_data	*emif = platform_get_drvdata(pdev);

	disable_and_clear_all_interrupts(emif);
}

#if defined(CONFIG_OF)
static const struct of_device_id emif_of_match[] = {
		{ .compatible = "ti,emif-4d" },
		{ .compatible = "ti,emif-4d5" },
		{},
};
MODULE_DEVICE_TABLE(of, emif_of_match);
#endif

static struct platform_driver emif_driver = {
	.remove		= __exit_p(emif_remove),
	.shutdown	= emif_shutdown,
	.driver = {
		.name = "emif",
		.of_match_table = of_match_ptr(emif_of_match),
	},
};

module_platform_driver_probe(emif_driver, emif_probe);

MODULE_DESCRIPTION("TI EMIF SDRAM Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:emif");
MODULE_AUTHOR("Texas Instruments Inc");
