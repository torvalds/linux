/*
 * twl6030-irq.c - TWL6030 irq support
 *
 * Copyright (C) 2005-2009 Texas Instruments, Inc.
 *
 * Modifications to defer interrupt handling to a kernel thread:
 * Copyright (C) 2006 MontaVista Software, Inc.
 *
 * Based on tlv320aic23.c:
 * Copyright (c) by Kai Svahn <kai.svahn@nokia.com>
 *
 * Code cleanup and modifications to IRQ handler.
 * by syed khasim <x0khasim@ti.com>
 *
 * TWL6030 specific code and IRQ handling changes by
 * Jagadeesh Bhaskar Pakaravoor <j-pakaravoor@ti.com>
 * Balaji T K <balajitk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/i2c/twl.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/reboot.h>

#include "twl-core.h"

/*
 * TWL6030 (unlike its predecessors, which had two level interrupt handling)
 * three interrupt registers INT_STS_A, INT_STS_B and INT_STS_C.
 * It exposes status bits saying who has raised an interrupt. There are
 * three mask registers that corresponds to these status registers, that
 * enables/disables these interrupts.
 *
 * We set up IRQs starting at a platform-specified base. An interrupt map table,
 * specifies mapping between interrupt number and the associated module.
 *
 */

static int twl6030_interrupt_mapping_table[24] = {
	PWR_INTR_OFFSET,	/* Bit 0	PWRON			*/
	PWR_INTR_OFFSET,	/* Bit 1	RPWRON			*/
	TWL_VLOW_INTR_OFFSET,	/* Bit 2	BAT_VLOW		*/
	RTC_INTR_OFFSET,	/* Bit 3	RTC_ALARM		*/
	RTC_INTR_OFFSET,	/* Bit 4	RTC_PERIOD		*/
	HOTDIE_INTR_OFFSET,	/* Bit 5	HOT_DIE			*/
	SMPSLDO_INTR_OFFSET,	/* Bit 6	VXXX_SHORT		*/
	SMPSLDO_INTR_OFFSET,	/* Bit 7	VMMC_SHORT		*/

	SMPSLDO_INTR_OFFSET,	/* Bit 8	VUSIM_SHORT		*/
	BATDETECT_INTR_OFFSET,	/* Bit 9	BAT			*/
	SIMDETECT_INTR_OFFSET,	/* Bit 10	SIM			*/
	MMCDETECT_INTR_OFFSET,	/* Bit 11	MMC			*/
	RSV_INTR_OFFSET,  	/* Bit 12	Reserved		*/
	MADC_INTR_OFFSET,	/* Bit 13	GPADC_RT_EOC		*/
	GPADCSW_INTR_OFFSET,	/* Bit 14	GPADC_SW_EOC		*/
	GASGAUGE_INTR_OFFSET,	/* Bit 15	CC_AUTOCAL		*/

	USBOTG_INTR_OFFSET,	/* Bit 16	ID_WKUP			*/
	USBOTG_INTR_OFFSET,	/* Bit 17	VBUS_WKUP		*/
	USBOTG_INTR_OFFSET,	/* Bit 18	ID			*/
	USB_PRES_INTR_OFFSET,	/* Bit 19	VBUS			*/
	CHARGER_INTR_OFFSET,	/* Bit 20	CHRG_CTRL		*/
	CHARGERFAULT_INTR_OFFSET,	/* Bit 21	EXT_CHRG	*/
	CHARGERFAULT_INTR_OFFSET,	/* Bit 22	INT_CHRG	*/
	RSV_INTR_OFFSET,	/* Bit 23	Reserved		*/
};

static int twl6032_interrupt_mapping_table[24] = {
	PWR_INTR_OFFSET,	/* Bit 0	PWRON			*/
	PWR_INTR_OFFSET,	/* Bit 1	RPWRON			*/
	TWL_VLOW_INTR_OFFSET,	/* Bit 2	SYS_VLOW		*/
	RTC_INTR_OFFSET,	/* Bit 3	RTC_ALARM		*/
	RTC_INTR_OFFSET,	/* Bit 4	RTC_PERIOD		*/
	HOTDIE_INTR_OFFSET,	/* Bit 5	HOT_DIE			*/
	SMPSLDO_INTR_OFFSET,	/* Bit 6	VXXX_SHORT		*/
	PWR_INTR_OFFSET,	/* Bit 7	SPDURATION		*/

	PWR_INTR_OFFSET,	/* Bit 8	WATCHDOG		*/
	BATDETECT_INTR_OFFSET,	/* Bit 9	BAT			*/
	SIMDETECT_INTR_OFFSET,	/* Bit 10	SIM			*/
	MMCDETECT_INTR_OFFSET,	/* Bit 11	MMC			*/
	MADC_INTR_OFFSET,	/* Bit 12	GPADC_RT_EOC		*/
	GPADCSW_INTR_OFFSET,	/* Bit 13	GPADC_SW_EOC		*/
	GASGAUGE_INTR_OFFSET,	/* Bit 14	CC_EOC		*/
	GASGAUGE_INTR_OFFSET,	/* Bit 15	CC_AUTOCAL		*/

	USBOTG_INTR_OFFSET,	/* Bit 16	ID_WKUP			*/
	USBOTG_INTR_OFFSET,	/* Bit 17	VBUS_WKUP		*/
	USBOTG_INTR_OFFSET,	/* Bit 18	ID			*/
	USB_PRES_INTR_OFFSET,	/* Bit 19	VBUS			*/
	CHARGER_INTR_OFFSET,	/* Bit 20	CHRG_CTRL		*/
	CHARGERFAULT_INTR_OFFSET,	/* Bit 21	EXT_CHRG	*/
	CHARGERFAULT_INTR_OFFSET,	/* Bit 22	INT_CHRG	*/
	RSV_INTR_OFFSET,	/* Bit 23	Reserved		*/
};

static int *twl6030_interrupt_mapping = twl6030_interrupt_mapping_table;
/*----------------------------------------------------------------------*/

static unsigned twl6030_irq_base, twl6030_irq_end;
static int twl_irq;
static bool twl_irq_wake_enabled;

static struct task_struct *task;
static struct completion irq_event;
static atomic_t twl6030_wakeirqs = ATOMIC_INIT(0);

static u8 vbatmin_hi_threshold;

static int twl6030_irq_pm_notifier(struct notifier_block *notifier,
				   unsigned long pm_event, void *unused)
{
	int chained_wakeups;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		chained_wakeups = atomic_read(&twl6030_wakeirqs);

		if (chained_wakeups && !twl_irq_wake_enabled) {
			if (enable_irq_wake(twl_irq))
				pr_err("twl6030 IRQ wake enable failed\n");
			else
				twl_irq_wake_enabled = true;
		} else if (!chained_wakeups && twl_irq_wake_enabled) {
			disable_irq_wake(twl_irq);
			twl_irq_wake_enabled = false;
		}

		disable_irq(twl_irq);
		break;

	case PM_POST_SUSPEND:
		enable_irq(twl_irq);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block twl6030_irq_pm_notifier_block = {
	.notifier_call = twl6030_irq_pm_notifier,
};

/*
 * This thread processes interrupts reported by the Primary Interrupt Handler.
 */
static int twl6030_irq_thread(void *data)
{
	long irq = (long)data;
	static unsigned i2c_errors;
	static const unsigned max_i2c_errors = 100;
	int ret;

	current->flags |= PF_NOFREEZE;

	while (!kthread_should_stop()) {
		int i;
		union {
		u8 bytes[4];
		u32 int_sts;
		} sts;
		u32 int_sts; /* sts.int_sts converted to CPU endianness */

		/* Wait for IRQ, then read PIH irq status (also blocking) */
		wait_for_completion_interruptible(&irq_event);

		/* read INT_STS_A, B and C in one shot using a burst read */
		ret = twl_i2c_read(TWL_MODULE_PIH, sts.bytes,
				REG_INT_STS_A, 3);
		if (ret) {
			pr_warning("twl6030: I2C error %d reading PIH ISR\n",
					ret);
			if (++i2c_errors >= max_i2c_errors) {
				printk(KERN_ERR "Maximum I2C error count"
						" exceeded.  Terminating %s.\n",
						__func__);
				break;
			}
			complete(&irq_event);
			continue;
		}



		sts.bytes[3] = 0; /* Only 24 bits are valid*/

		/*
		 * Since VBUS status bit is not reliable for VBUS disconnect
		 * use CHARGER VBUS detection status bit instead.
		 */
		if (sts.bytes[2] & 0x10)
			sts.bytes[2] |= 0x08;

		int_sts = le32_to_cpu(sts.int_sts);
		for (i = 0; int_sts; int_sts >>= 1, i++) {
			local_irq_disable();
			if (int_sts & 0x1) {
				int module_irq = twl6030_irq_base +
					twl6030_interrupt_mapping[i];
				generic_handle_irq(module_irq);

			}
		local_irq_enable();
		}

		/*
		 * NOTE:
		 * Simulation confirms that documentation is wrong w.r.t the
		 * interrupt status clear operation. A single *byte* write to
		 * any one of STS_A to STS_C register results in all three
		 * STS registers being reset. Since it does not matter which
		 * value is written, all three registers are cleared on a
		 * single byte write, so we just use 0x0 to clear.
		 */
		ret = twl_i2c_write_u8(TWL_MODULE_PIH, 0x00, REG_INT_STS_A);
		if (ret)
			pr_warning("twl6030: I2C error in clearing PIH ISR\n");

		enable_irq(irq);
	}

	return 0;
}

/*
 * handle_twl6030_int() is the desc->handle method for the twl6030 interrupt.
 * This is a chained interrupt, so there is no desc->action method for it.
 * Now we need to query the interrupt controller in the twl6030 to determine
 * which module is generating the interrupt request.  However, we can't do i2c
 * transactions in interrupt context, so we must defer that work to a kernel
 * thread.  All we do here is acknowledge and mask the interrupt and wakeup
 * the kernel thread.
 */
static irqreturn_t handle_twl6030_pih(int irq, void *devid)
{
	disable_irq_nosync(irq);
	complete(devid);
	return IRQ_HANDLED;
}

/*
 * handle_twl6030_vlow() is a threaded BAT_VLOW interrupt handler. BAT_VLOW
 * is a secondary interrupt generated in twl6030_irq_thread().
 */
static irqreturn_t handle_twl6030_vlow(int irq, void *unused)
{
	pr_err("twl6030: BAT_VLOW interrupt; threshold=%dmV\n",
	       2300 + (vbatmin_hi_threshold - 0b110) * 50);

#if 1 /* temporary */
	pr_err("%s: disabling BAT_VLOW interrupt\n", __func__);
	disable_irq_nosync(twl6030_irq_base + TWL_VLOW_INTR_OFFSET);
//	WARN_ON(1);
#else
	pr_emerg("handle_twl6030_vlow: kernel_power_off()\n");
	kernel_power_off();
#endif
	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

static inline void activate_irq(int irq)
{
#ifdef CONFIG_ARM
	/* ARM requires an extra step to clear IRQ_NOREQUEST, which it
	 * sets on behalf of every irq_chip.  Also sets IRQ_NOPROBE.
	 */
	set_irq_flags(irq, IRQF_VALID);
#else
	/* same effect on other architectures */
	irq_set_noprobe(irq);
#endif
}

int twl6030_irq_set_wake(struct irq_data *d, unsigned int on)
{
	if (on)
		atomic_inc(&twl6030_wakeirqs);
	else
		atomic_dec(&twl6030_wakeirqs);

	return 0;
}

/*----------------------------------------------------------------------*/

static unsigned twl6030_irq_next;

/*----------------------------------------------------------------------*/
int twl6030_interrupt_unmask(u8 bit_mask, u8 offset)
{
	int ret;
	u8 unmask_value;
	ret = twl_i2c_read_u8(TWL_MODULE_PIH, &unmask_value,
			REG_INT_STS_A + offset);
	unmask_value &= (~(bit_mask));
	ret |= twl_i2c_write_u8(TWL_MODULE_PIH, unmask_value,
			REG_INT_STS_A + offset); /* unmask INT_MSK_A/B/C */
	return ret;
}
EXPORT_SYMBOL(twl6030_interrupt_unmask);

int twl6030_interrupt_mask(u8 bit_mask, u8 offset)
{
	int ret;
	u8 mask_value;
	ret = twl_i2c_read_u8(TWL_MODULE_PIH, &mask_value,
			REG_INT_STS_A + offset);
	mask_value |= (bit_mask);
	ret |= twl_i2c_write_u8(TWL_MODULE_PIH, mask_value,
			REG_INT_STS_A + offset); /* mask INT_MSK_A/B/C */
	return ret;
}
EXPORT_SYMBOL(twl6030_interrupt_mask);

int twl6030_mmc_card_detect_config(void)
{
	int ret;
	u8 reg_val = 0;

	/* Unmasking the Card detect Interrupt line for MMC1 from Phoenix */
	twl6030_interrupt_unmask(TWL6030_MMCDETECT_INT_MASK,
						REG_INT_MSK_LINE_B);
	twl6030_interrupt_unmask(TWL6030_MMCDETECT_INT_MASK,
						REG_INT_MSK_STS_B);
	/*
	 * Initially Configuring MMC_CTRL for receiving interrupts &
	 * Card status on TWL6030 for MMC1
	 */
	ret = twl_i2c_read_u8(TWL6030_MODULE_ID0, &reg_val, TWL6030_MMCCTRL);
	if (ret < 0) {
		pr_err("twl6030: Failed to read MMCCTRL, error %d\n", ret);
		return ret;
	}
	reg_val &= ~VMMC_AUTO_OFF;
	reg_val |= SW_FC;
	ret = twl_i2c_write_u8(TWL6030_MODULE_ID0, reg_val, TWL6030_MMCCTRL);
	if (ret < 0) {
		pr_err("twl6030: Failed to write MMCCTRL, error %d\n", ret);
		return ret;
	}

	/* Configuring PullUp-PullDown register */
	ret = twl_i2c_read_u8(TWL6030_MODULE_ID0, &reg_val,
						TWL6030_CFG_INPUT_PUPD3);
	if (ret < 0) {
		pr_err("twl6030: Failed to read CFG_INPUT_PUPD3, error %d\n",
									ret);
		return ret;
	}
	reg_val &= ~(MMC_PU | MMC_PD);
	ret = twl_i2c_write_u8(TWL6030_MODULE_ID0, reg_val,
						TWL6030_CFG_INPUT_PUPD3);
	if (ret < 0) {
		pr_err("twl6030: Failed to write CFG_INPUT_PUPD3, error %d\n",
									ret);
		return ret;
	}

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID0,
			(MMC_MINS_DEB_MASK | MMC_MEXT_DEB_MASK),
			TWL6030_MMCDEBOUNCING);
	if (ret < 0){
		pr_err("twl6030: Failed to write MMC_MEXT_DEB_MASK %d\n",
									ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(twl6030_mmc_card_detect_config);

int twl6030_mmc_card_detect(struct device *dev, int slot)
{
	int ret = -EIO;
	u8 read_reg = 0;
	struct platform_device *pdev = to_platform_device(dev);

	if (pdev->id) {
		/* TWL6030 provide's Card detect support for
		 * only MMC1 controller.
		 */
		pr_err("Unknown MMC controller %d in %s\n", pdev->id, __func__);
		return ret;
	}
	/*
	 * BIT0 of MMC_CTRL on TWL6030 provides card status for MMC1
	 * 0 - Card not present ,1 - Card present
	 */
	ret = twl_i2c_read_u8(TWL6030_MODULE_ID0, &read_reg,
						TWL6030_MMCCTRL);
	if (ret >= 0)
		ret = read_reg & STS_MMC;
	return ret;
}
EXPORT_SYMBOL(twl6030_mmc_card_detect);

int twl6030_vlow_init(int vlow_irq)
{
	int status;
	u8 val;

	status = twl_i2c_read_u8(TWL_MODULE_PM_SLAVE_RES, &val,
			REG_VBATMIN_HI_CFG_STATE);
	if (status < 0) {
		pr_err("twl6030: I2C err reading REG_VBATMIN_HI_CFG_STATE: %d\n",
				status);
		return status;
	}

	status = twl_i2c_write_u8(TWL_MODULE_PM_SLAVE_RES,
			val | VBATMIN_VLOW_EN, REG_VBATMIN_HI_CFG_STATE);
	if (status < 0) {
		pr_err("twl6030: I2C err writing REG_VBATMIN_HI_CFG_STATE: %d\n",
				status);
		return status;
	}

	status = twl_i2c_read_u8(TWL_MODULE_PIH, &val, REG_INT_MSK_LINE_A);
	if (status < 0) {
		pr_err("twl6030: I2C err reading REG_INT_MSK_LINE_A: %d\n",
				status);
		return status;
	}

	status = twl_i2c_write_u8(TWL_MODULE_PIH, val & ~VLOW_INT_MASK,
			REG_INT_MSK_LINE_A);
	if (status < 0) {
		pr_err("twl6030: I2C err writing REG_INT_MSK_LINE_A: %d\n",
				status);
		return status;
	}

	status = twl_i2c_read_u8(TWL_MODULE_PIH, &val, REG_INT_MSK_STS_A);
	if (status < 0) {
		pr_err("twl6030: I2C err reading REG_INT_MSK_STS_A: %d\n",
				status);
		return status;
	}

	status = twl_i2c_write_u8(TWL_MODULE_PIH, val & ~VLOW_INT_MASK,
		REG_INT_MSK_STS_A);
	if (status < 0) {
		pr_err("twl6030: I2C err writing REG_INT_MSK_STS_A: %d\n",
				status);
		return status;
	}

	twl_i2c_read_u8(TWL_MODULE_PM_MASTER, &vbatmin_hi_threshold,
			TWL6030_VBATMIN_HI_THRESHOLD);

	/* install an irq handler for vlow */
	status = request_threaded_irq(vlow_irq, NULL, handle_twl6030_vlow,
			IRQF_ONESHOT,
			"TWL6030-VLOW", handle_twl6030_vlow);
	if (status < 0) {
		pr_err("twl6030: could not claim vlow irq %d: %d\n", vlow_irq,
				status);
		return status;
	}

	return 0;
}

int twl6030_init_irq(int irq_num, unsigned irq_base, unsigned irq_end,
			unsigned long features)
{

	int	status = 0;
	int	i;
	int ret;
	u8 mask[4];

	static struct irq_chip	twl6030_irq_chip;

	if (features & TWL6032_SUBCLASS)
		twl6030_interrupt_mapping = twl6032_interrupt_mapping_table;

	mask[1] = 0xFF;
	mask[2] = 0xFF;
	mask[3] = 0xFF;
	ret = twl_i2c_write(TWL_MODULE_PIH, &mask[0],
			REG_INT_MSK_LINE_A, 3); /* MASK ALL INT LINES */
	ret = twl_i2c_write(TWL_MODULE_PIH, &mask[0],
			REG_INT_MSK_STS_A, 3); /* MASK ALL INT STS */
	ret = twl_i2c_write(TWL_MODULE_PIH, &mask[0],
			REG_INT_STS_A, 3); /* clear INT_STS_A,B,C */

	twl6030_irq_base = irq_base;
	twl6030_irq_end = irq_end;

	/* install an irq handler for each of the modules;
	 * clone dummy irq_chip since PIH can't *do* anything
	 */
	twl6030_irq_chip = dummy_irq_chip;
	twl6030_irq_chip.name = "twl6030";
	twl6030_irq_chip.irq_set_type = NULL;
	twl6030_irq_chip.irq_set_wake = twl6030_irq_set_wake;

	for (i = irq_base; i < irq_end; i++) {
		irq_set_chip_and_handler(i, &twl6030_irq_chip,
					 handle_simple_irq);
		irq_set_chip_data(i, (void *)irq_num);
		activate_irq(i);
	}

	twl6030_irq_next = i;
	pr_info("twl6030: %s (irq %d) chaining IRQs %d..%d\n", "PIH",
			irq_num, irq_base, twl6030_irq_next - 1);

	/* install an irq handler to demultiplex the TWL6030 interrupt */
	init_completion(&irq_event);
	task = kthread_run(twl6030_irq_thread, (void *)irq_num, "twl6030-irq");
	if (IS_ERR(task)) {
		pr_err("twl6030: could not create irq %d thread!\n", irq_num);
		status = PTR_ERR(task);
		goto fail_kthread;
	}

	status = request_irq(irq_num, handle_twl6030_pih, IRQF_DISABLED,
				"TWL6030-PIH", &irq_event);
	if (status < 0) {
		pr_err("twl6030: could not claim irq%d: %d\n", irq_num, status);
		goto fail_irq;
	}

	twl_irq = irq_num;
	register_pm_notifier(&twl6030_irq_pm_notifier_block);

	status = twl6030_vlow_init(twl6030_irq_base + TWL_VLOW_INTR_OFFSET);
	if (status < 0)
		goto fail_vlow;

	return status;

fail_vlow:
	free_irq(irq_num, &irq_event);

fail_irq:
	kthread_stop(task);

fail_kthread:
	for (i = irq_base; i < irq_end; i++)
		irq_set_chip_and_handler(i, NULL, NULL);
	return status;
}

int twl6030_exit_irq(void)
{
	int i;
	unregister_pm_notifier(&twl6030_irq_pm_notifier_block);

	if (task)
		kthread_stop(task);

	if (!twl6030_irq_base || !twl6030_irq_end) {
		pr_err("twl6030: can't yet clean up IRQs?\n");
		return -ENOSYS;
	}

	free_irq(twl6030_irq_base + TWL_VLOW_INTR_OFFSET,
		handle_twl6030_vlow);

	free_irq(twl_irq, &irq_event);

	for (i = twl6030_irq_base; i < twl6030_irq_end; i++)
		irq_set_chip_and_handler(i, NULL, NULL);

	return 0;
}

