/*
 * Copyright (C) 2012 Thomas Petazzoni
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/irqchip.h>
#include <linux/platform_device.h>

/*
 * This special of_device_id is the sentinel at the end of the
 * of_device_id[] array of all irqchips. It is automatically placed at
 * the end of the array by the linker, thanks to being part of a
 * special section.
 */
static const struct of_device_id
irqchip_of_match_end __used __section("__irqchip_of_table_end");

extern struct of_device_id __irqchip_of_table[];

void __init irqchip_init(void)
{
	of_irq_init(__irqchip_of_table);
	acpi_probe_device_table(irqchip);
}

int platform_irqchip_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *par_np = of_irq_find_parent(np);
	of_irq_init_cb_t irq_init_cb = of_device_get_match_data(&pdev->dev);

	if (!irq_init_cb) {
		of_node_put(par_np);
		return -EINVAL;
	}

	if (par_np == np)
		par_np = NULL;

	/*
	 * If there's a parent interrupt controller and  none of the parent irq
	 * domains have been registered, that means the parent interrupt
	 * controller has not been initialized yet.  it's not time for this
	 * interrupt controller to initialize. So, defer probe of this
	 * interrupt controller. The actual initialization callback of this
	 * interrupt controller can check for specific domains as necessary.
	 */
	if (par_np && !irq_find_matching_host(par_np, DOMAIN_BUS_ANY)) {
		of_node_put(par_np);
		return -EPROBE_DEFER;
	}

	return irq_init_cb(np, par_np);
}
EXPORT_SYMBOL_GPL(platform_irqchip_probe);
