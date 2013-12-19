/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Tilera TILE Processor hypervisor console
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <asm/setup.h>
#include <arch/sim_def.h>

#include <hv/hypervisor.h>

#include "hvc_console.h"

static int use_sim_console;
static int __init sim_console(char *str)
{
	use_sim_console = 1;
	return 0;
}
early_param("sim_console", sim_console);

int tile_console_write(const char *buf, int count)
{
	if (unlikely(use_sim_console)) {
		int i;
		for (i = 0; i < count; ++i)
			__insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
				     (buf[i] << _SIM_CONTROL_OPERATOR_BITS));
		__insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
			     (SIM_PUTC_FLUSH_BINARY <<
			      _SIM_CONTROL_OPERATOR_BITS));
		return 0;
	} else {
		return hv_console_write((HV_VirtAddr)buf, count);
	}
}

static int hvc_tile_put_chars(uint32_t vt, const char *buf, int count)
{
	return tile_console_write(buf, count);
}

static int hvc_tile_get_chars(uint32_t vt, char *buf, int count)
{
	int i, c;

	for (i = 0; i < count; ++i) {
		c = hv_console_read_if_ready();
		if (c < 0)
			break;
		buf[i] = c;
	}

	return i;
}

#ifdef __tilegx__
/*
 * IRQ based callbacks.
 */
static int hvc_tile_notifier_add_irq(struct hvc_struct *hp, int irq)
{
	int rc;
	int cpu = raw_smp_processor_id();  /* Choose an arbitrary cpu */
	HV_Coord coord = { .x = cpu_x(cpu), .y = cpu_y(cpu) };

	rc = notifier_add_irq(hp, irq);
	if (rc)
		return rc;

	/*
	 * Request that the hypervisor start sending us interrupts.
	 * If the hypervisor returns an error, we still return 0, so that
	 * we can fall back to polling.
	 */
	if (hv_console_set_ipi(KERNEL_PL, irq, coord) < 0)
		notifier_del_irq(hp, irq);

	return 0;
}

static void hvc_tile_notifier_del_irq(struct hvc_struct *hp, int irq)
{
	HV_Coord coord = { 0, 0 };

	/* Tell the hypervisor to stop sending us interrupts. */
	hv_console_set_ipi(KERNEL_PL, -1, coord);

	notifier_del_irq(hp, irq);
}

static void hvc_tile_notifier_hangup_irq(struct hvc_struct *hp, int irq)
{
	hvc_tile_notifier_del_irq(hp, irq);
}
#endif

static const struct hv_ops hvc_tile_get_put_ops = {
	.get_chars = hvc_tile_get_chars,
	.put_chars = hvc_tile_put_chars,
#ifdef __tilegx__
	.notifier_add = hvc_tile_notifier_add_irq,
	.notifier_del = hvc_tile_notifier_del_irq,
	.notifier_hangup = hvc_tile_notifier_hangup_irq,
#endif
};


#ifdef __tilegx__
static int hvc_tile_probe(struct platform_device *pdev)
{
	struct hvc_struct *hp;
	int tile_hvc_irq;

	/* Create our IRQ and register it. */
	tile_hvc_irq = create_irq();
	if (tile_hvc_irq < 0)
		return -ENXIO;

	tile_irq_activate(tile_hvc_irq, TILE_IRQ_PERCPU);
	hp = hvc_alloc(0, tile_hvc_irq, &hvc_tile_get_put_ops, 128);
	if (IS_ERR(hp)) {
		destroy_irq(tile_hvc_irq);
		return PTR_ERR(hp);
	}
	dev_set_drvdata(&pdev->dev, hp);

	return 0;
}

static int hvc_tile_remove(struct platform_device *pdev)
{
	int rc;
	struct hvc_struct *hp = dev_get_drvdata(&pdev->dev);

	rc = hvc_remove(hp);
	if (rc == 0)
		destroy_irq(hp->data);

	return rc;
}

static void hvc_tile_shutdown(struct platform_device *pdev)
{
	struct hvc_struct *hp = dev_get_drvdata(&pdev->dev);

	hvc_tile_notifier_del_irq(hp, hp->data);
}

static struct platform_device hvc_tile_pdev = {
	.name           = "hvc-tile",
	.id             = 0,
};

static struct platform_driver hvc_tile_driver = {
	.probe          = hvc_tile_probe,
	.remove         = hvc_tile_remove,
	.shutdown	= hvc_tile_shutdown,
	.driver         = {
		.name   = "hvc-tile",
		.owner  = THIS_MODULE,
	}
};
#endif

static int __init hvc_tile_console_init(void)
{
	hvc_instantiate(0, 0, &hvc_tile_get_put_ops);
	add_preferred_console("hvc", 0, NULL);
	return 0;
}
console_initcall(hvc_tile_console_init);

static int __init hvc_tile_init(void)
{
#ifndef __tilegx__
	struct hvc_struct *hp;
	hp = hvc_alloc(0, 0, &hvc_tile_get_put_ops, 128);
	return IS_ERR(hp) ? PTR_ERR(hp) : 0;
#else
	platform_device_register(&hvc_tile_pdev);
	return platform_driver_register(&hvc_tile_driver);
#endif
}
device_initcall(hvc_tile_init);
