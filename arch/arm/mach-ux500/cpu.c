/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * Author: Lee Jones <lee.jones@linaro.org> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/clksrc-dbx500-prcmu.h>
#include <linux/sys_soc.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/platform_data/clk-ux500.h>
#include <linux/platform_data/arm-ux500-pm.h>

#include <asm/mach/map.h>

#include "setup.h"
#include "devices.h"

#include "board-mop500.h"
#include "db8500-regs.h"
#include "id.h"

void ux500_restart(enum reboot_mode mode, const char *cmd)
{
	local_irq_disable();
	local_fiq_disable();

	prcmu_system_reset(0);
}

/*
 * FIXME: Should we set up the GPIO domain here?
 *
 * The problem is that we cannot put the interrupt resources into the platform
 * device until the irqdomain has been added. Right now, we set the GIC interrupt
 * domain from init_irq(), then load the gpio driver from
 * core_initcall(nmk_gpio_init) and add the platform devices from
 * arch_initcall(customize_machine).
 *
 * This feels fragile because it depends on the gpio device getting probed
 * _before_ any device uses the gpio interrupts.
*/
void __init ux500_init_irq(void)
{
	void __iomem *dist_base;
	void __iomem *cpu_base;

	gic_arch_extn.flags = IRQCHIP_SKIP_SET_WAKE | IRQCHIP_MASK_ON_SUSPEND;

	if (cpu_is_u8500_family() || cpu_is_ux540_family()) {
		dist_base = __io_address(U8500_GIC_DIST_BASE);
		cpu_base = __io_address(U8500_GIC_CPU_BASE);
	} else
		ux500_unknown_soc();

#ifdef CONFIG_OF
	if (of_have_populated_dt())
		irqchip_init();
	else
#endif
		gic_init(0, 29, dist_base, cpu_base);

	/*
	 * Init clocks here so that they are available for system timer
	 * initialization.
	 */
	if (cpu_is_u8500_family()) {
		prcmu_early_init(U8500_PRCMU_BASE, SZ_8K - 1);
		ux500_pm_init(U8500_PRCMU_BASE, SZ_8K - 1);

		if (of_have_populated_dt())
			u8500_of_clk_init(U8500_CLKRST1_BASE,
					  U8500_CLKRST2_BASE,
					  U8500_CLKRST3_BASE,
					  U8500_CLKRST5_BASE,
					  U8500_CLKRST6_BASE);
		else
			u8500_clk_init(U8500_CLKRST1_BASE, U8500_CLKRST2_BASE,
				       U8500_CLKRST3_BASE, U8500_CLKRST5_BASE,
				       U8500_CLKRST6_BASE);
	} else if (cpu_is_u9540()) {
		prcmu_early_init(U8500_PRCMU_BASE, SZ_8K - 1);
		ux500_pm_init(U8500_PRCMU_BASE, SZ_8K - 1);
		u9540_clk_init(U8500_CLKRST1_BASE, U8500_CLKRST2_BASE,
			       U8500_CLKRST3_BASE, U8500_CLKRST5_BASE,
			       U8500_CLKRST6_BASE);
	} else if (cpu_is_u8540()) {
		prcmu_early_init(U8500_PRCMU_BASE, SZ_8K + SZ_4K - 1);
		ux500_pm_init(U8500_PRCMU_BASE, SZ_8K + SZ_4K - 1);
		u8540_clk_init(U8500_CLKRST1_BASE, U8500_CLKRST2_BASE,
			       U8500_CLKRST3_BASE, U8500_CLKRST5_BASE,
			       U8500_CLKRST6_BASE);
	}
}

static const char * __init ux500_get_machine(void)
{
	return kasprintf(GFP_KERNEL, "DB%4x", dbx500_partnumber());
}

static const char * __init ux500_get_family(void)
{
	return kasprintf(GFP_KERNEL, "ux500");
}

static const char * __init ux500_get_revision(void)
{
	unsigned int rev = dbx500_revision();

	if (rev == 0x01)
		return kasprintf(GFP_KERNEL, "%s", "ED");
	else if (rev >= 0xA0)
		return kasprintf(GFP_KERNEL, "%d.%d",
				 (rev >> 4) - 0xA + 1, rev & 0xf);

	return kasprintf(GFP_KERNEL, "%s", "Unknown");
}

static ssize_t ux500_get_process(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	if (dbx500_id.process == 0x00)
		return sprintf(buf, "Standard\n");

	return sprintf(buf, "%02xnm\n", dbx500_id.process);
}

static void __init soc_info_populate(struct soc_device_attribute *soc_dev_attr,
				     const char *soc_id)
{
	soc_dev_attr->soc_id   = soc_id;
	soc_dev_attr->machine  = ux500_get_machine();
	soc_dev_attr->family   = ux500_get_family();
	soc_dev_attr->revision = ux500_get_revision();
}

struct device_attribute ux500_soc_attr =
	__ATTR(process,  S_IRUGO, ux500_get_process,  NULL);

struct device * __init ux500_soc_device_init(const char *soc_id)
{
	struct device *parent;
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return ERR_PTR(-ENOMEM);

	soc_info_populate(soc_dev_attr, soc_id);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
	        kfree(soc_dev_attr);
		return NULL;
	}

	parent = soc_device_to_device(soc_dev);
	device_create_file(parent, &ux500_soc_attr);

	return parent;
}
