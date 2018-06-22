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

#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/mfd/twl.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/irqdomain.h>
#include <linux/of_device.h>

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
 */
#define TWL6030_NR_IRQS    20

static int twl6030_interrupt_mapping[24] = {
	PWR_INTR_OFFSET,	/* Bit 0	PWRON			*/
	PWR_INTR_OFFSET,	/* Bit 1	RPWRON			*/
	PWR_INTR_OFFSET,	/* Bit 2	BAT_VLOW		*/
	RTC_INTR_OFFSET,	/* Bit 3	RTC_ALARM		*/
	RTC_INTR_OFFSET,	/* Bit 4	RTC_PERIOD		*/
	HOTDIE_INTR_OFFSET,	/* Bit 5	HOT_DIE			*/
	SMPSLDO_INTR_OFFSET,	/* Bit 6	VXXX_SHORT		*/
	SMPSLDO_INTR_OFFSET,	/* Bit 7	VMMC_SHORT		*/

	SMPSLDO_INTR_OFFSET,	/* Bit 8	VUSIM_SHORT		*/
	BATDETECT_INTR_OFFSET,	/* Bit 9	BAT			*/
	SIMDETECT_INTR_OFFSET,	/* Bit 10	SIM			*/
	MMCDETECT_INTR_OFFSET,	/* Bit 11	MMC			*/
	RSV_INTR_OFFSET,	/* Bit 12	Reserved		*/
	MADC_INTR_OFFSET,	/* Bit 13	GPADC_RT_EOC		*/
	MADC_INTR_OFFSET,	/* Bit 14	GPADC_SW_EOC		*/
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

static int twl6032_interrupt_mapping[24] = {
	PWR_INTR_OFFSET,	/* Bit 0	PWRON			*/
	PWR_INTR_OFFSET,	/* Bit 1	RPWRON			*/
	PWR_INTR_OFFSET,	/* Bit 2	SYS_VLOW		*/
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
	MADC_INTR_OFFSET,	/* Bit 13	GPADC_SW_EOC		*/
	GASGAUGE_INTR_OFFSET,	/* Bit 14	CC_EOC			*/
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

/*----------------------------------------------------------------------*/

struct twl6030_irq {
	unsigned int		irq_base;
	int			twl_irq;
	bool			irq_wake_enabled;
	atomic_t		wakeirqs;
	struct notifier_block	pm_nb;
	struct irq_chip		irq_chip;
	struct irq_domain	*irq_domain;
	const int		*irq_mapping_tbl;
};

static struct twl6030_irq *twl6030_irq;

static int twl6030_irq_pm_notifier(struct notifier_block *notifier,
				   unsigned long pm_event, void *unused)
{
	int chained_wakeups;
	struct twl6030_irq *pdata = container_of(notifier, struct twl6030_irq,
						  pm_nb);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		chained_wakeups = atomic_read(&pdata->wakeirqs);

		if (chained_wakeups && !pdata->irq_wake_enabled) {
			if (enable_irq_wake(pdata->twl_irq))
				pr_err("twl6030 IRQ wake enable failed\n");
			else
				pdata->irq_wake_enabled = true;
		} else if (!chained_wakeups && pdata->irq_wake_enabled) {
			disable_irq_wake(pdata->twl_irq);
			pdata->irq_wake_enabled = false;
		}

		disable_irq(pdata->twl_irq);
		break;

	case PM_POST_SUSPEND:
		enable_irq(pdata->twl_irq);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

/*
* Threaded irq handler for the twl6030 interrupt.
* We query the interrupt controller in the twl6030 to determine
* which module is generating the interrupt request and call
* handle_nested_irq for that module.
*/
static irqreturn_t twl6030_irq_thread(int irq, void *data)
{
	int i, ret;
	union {
		u8 bytes[4];
		__le32 int_sts;
	} sts;
	u32 int_sts; /* sts.int_sts converted to CPU endianness */
	struct twl6030_irq *pdata = data;

	/* read INT_STS_A, B and C in one shot using a burst read */
	ret = twl_i2c_read(TWL_MODULE_PIH, sts.bytes, REG_INT_STS_A, 3);
	if (ret) {
		pr_warn("twl6030_irq: I2C error %d reading PIH ISR\n", ret);
		return IRQ_HANDLED;
	}

	sts.bytes[3] = 0; /* Only 24 bits are valid*/

	/*
	 * Since VBUS status bit is not reliable for VBUS disconnect
	 * use CHARGER VBUS detection status bit instead.
	 */
	if (sts.bytes[2] & 0x10)
		sts.bytes[2] |= 0x08;

	int_sts = le32_to_cpu(sts.int_sts);
	for (i = 0; int_sts; int_sts >>= 1, i++)
		if (int_sts & 0x1) {
			int module_irq =
				irq_find_mapping(pdata->irq_domain,
						 pdata->irq_mapping_tbl[i]);
			if (module_irq)
				handle_nested_irq(module_irq);
			else
				pr_err("twl6030_irq: Unmapped PIH ISR %u detected\n",
				       i);
			pr_debug("twl6030_irq: PIH ISR %u, virq%u\n",
				 i, module_irq);
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
		pr_warn("twl6030_irq: I2C error in clearing PIH ISR\n");

	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

static int twl6030_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct twl6030_irq *pdata = irq_data_get_irq_chip_data(d);

	if (on)
		atomic_inc(&pdata->wakeirqs);
	else
		atomic_dec(&pdata->wakeirqs);

	return 0;
}

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

	return irq_find_mapping(twl6030_irq->irq_domain,
				 MMCDETECT_INTR_OFFSET);
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

static int twl6030_irq_map(struct irq_domain *d, unsigned int virq,
			      irq_hw_number_t hwirq)
{
	struct twl6030_irq *pdata = d->host_data;

	irq_set_chip_data(virq, pdata);
	irq_set_chip_and_handler(virq,  &pdata->irq_chip, handle_simple_irq);
	irq_set_nested_thread(virq, true);
	irq_set_parent(virq, pdata->twl_irq);
	irq_set_noprobe(virq);

	return 0;
}

static void twl6030_irq_unmap(struct irq_domain *d, unsigned int virq)
{
	irq_set_chip_and_handler(virq, NULL, NULL);
	irq_set_chip_data(virq, NULL);
}

static const struct irq_domain_ops twl6030_irq_domain_ops = {
	.map	= twl6030_irq_map,
	.unmap	= twl6030_irq_unmap,
	.xlate	= irq_domain_xlate_onetwocell,
};

static const struct of_device_id twl6030_of_match[] = {
	{.compatible = "ti,twl6030", &twl6030_interrupt_mapping},
	{.compatible = "ti,twl6032", &twl6032_interrupt_mapping},
	{ },
};

int twl6030_init_irq(struct device *dev, int irq_num)
{
	struct			device_node *node = dev->of_node;
	int			nr_irqs;
	int			status;
	u8			mask[3];
	const struct of_device_id *of_id;

	of_id = of_match_device(twl6030_of_match, dev);
	if (!of_id || !of_id->data) {
		dev_err(dev, "Unknown TWL device model\n");
		return -EINVAL;
	}

	nr_irqs = TWL6030_NR_IRQS;

	twl6030_irq = devm_kzalloc(dev, sizeof(*twl6030_irq), GFP_KERNEL);
	if (!twl6030_irq)
		return -ENOMEM;

	mask[0] = 0xFF;
	mask[1] = 0xFF;
	mask[2] = 0xFF;

	/* mask all int lines */
	status = twl_i2c_write(TWL_MODULE_PIH, &mask[0], REG_INT_MSK_LINE_A, 3);
	/* mask all int sts */
	status |= twl_i2c_write(TWL_MODULE_PIH, &mask[0], REG_INT_MSK_STS_A, 3);
	/* clear INT_STS_A,B,C */
	status |= twl_i2c_write(TWL_MODULE_PIH, &mask[0], REG_INT_STS_A, 3);

	if (status < 0) {
		dev_err(dev, "I2C err writing TWL_MODULE_PIH: %d\n", status);
		return status;
	}

	/*
	 * install an irq handler for each of the modules;
	 * clone dummy irq_chip since PIH can't *do* anything
	 */
	twl6030_irq->irq_chip = dummy_irq_chip;
	twl6030_irq->irq_chip.name = "twl6030";
	twl6030_irq->irq_chip.irq_set_type = NULL;
	twl6030_irq->irq_chip.irq_set_wake = twl6030_irq_set_wake;

	twl6030_irq->pm_nb.notifier_call = twl6030_irq_pm_notifier;
	atomic_set(&twl6030_irq->wakeirqs, 0);
	twl6030_irq->irq_mapping_tbl = of_id->data;

	twl6030_irq->irq_domain =
		irq_domain_add_linear(node, nr_irqs,
				      &twl6030_irq_domain_ops, twl6030_irq);
	if (!twl6030_irq->irq_domain) {
		dev_err(dev, "Can't add irq_domain\n");
		return -ENOMEM;
	}

	dev_info(dev, "PIH (irq %d) nested IRQs\n", irq_num);

	/* install an irq handler to demultiplex the TWL6030 interrupt */
	status = request_threaded_irq(irq_num, NULL, twl6030_irq_thread,
				      IRQF_ONESHOT, "TWL6030-PIH", twl6030_irq);
	if (status < 0) {
		dev_err(dev, "could not claim irq %d: %d\n", irq_num, status);
		goto fail_irq;
	}

	twl6030_irq->twl_irq = irq_num;
	register_pm_notifier(&twl6030_irq->pm_nb);
	return 0;

fail_irq:
	irq_domain_remove(twl6030_irq->irq_domain);
	return status;
}

int twl6030_exit_irq(void)
{
	if (twl6030_irq && twl6030_irq->twl_irq) {
		unregister_pm_notifier(&twl6030_irq->pm_nb);
		free_irq(twl6030_irq->twl_irq, NULL);
		/*
		 * TODO: IRQ domain and allocated nested IRQ descriptors
		 * should be freed somehow here. Now It can't be done, because
		 * child devices will not be deleted during removing of
		 * TWL Core driver and they will still contain allocated
		 * virt IRQs in their Resources tables.
		 * The same prevents us from using devm_request_threaded_irq()
		 * in this module.
		 */
	}
	return 0;
}

