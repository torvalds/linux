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
#include <linux/of_address.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/platform_data/clk-ux500.h>
#include <linux/platform_data/arm-ux500-pm.h>

#include <asm/mach/map.h>

#include "setup.h"

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
	struct device_node *np;
	struct resource r;

	irqchip_init();
	np = of_find_compatible_node(NULL, NULL, "stericsson,db8500-prcmu");
	of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (!r.start) {
		pr_err("could not find PRCMU base resource\n");
		return;
	}
	prcmu_early_init(r.start, r.end-r.start);
	ux500_pm_init(r.start, r.end-r.start);

	/*
	 * Init clocks here so that they are available for system timer
	 * initialization.
	 */
	if (cpu_is_u8500_family())
		u8500_clk_init();
	else if (cpu_is_u9540())
		u9540_clk_init();
	else if (cpu_is_u8540())
		u8540_clk_init();
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

static const struct device_attribute ux500_soc_attr =
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
