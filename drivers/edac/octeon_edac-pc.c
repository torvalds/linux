/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Cavium, Inc.
 *
 * Copyright (C) 2009 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/edac.h>

#include "edac_module.h"

#include <asm/octeon/cvmx.h>
#include <asm/mipsregs.h>

extern int register_co_cache_error_notifier(struct notifier_block *nb);
extern int unregister_co_cache_error_notifier(struct notifier_block *nb);

extern unsigned long long cache_err_dcache[NR_CPUS];

struct co_cache_error {
	struct notifier_block notifier;
	struct edac_device_ctl_info *ed;
};

/**
 * EDAC CPU cache error callback
 *
 * @event: non-zero if unrecoverable.
 */
static int  co_cache_error_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	struct co_cache_error *p = container_of(this, struct co_cache_error,
						notifier);

	unsigned int core = cvmx_get_core_num();
	unsigned int cpu = smp_processor_id();
	u64 icache_err = read_octeon_c0_icacheerr();
	u64 dcache_err;

	if (event) {
		dcache_err = cache_err_dcache[core];
		cache_err_dcache[core] = 0;
	} else {
		dcache_err = read_octeon_c0_dcacheerr();
	}

	if (icache_err & 1) {
		edac_device_printk(p->ed, KERN_ERR,
				   "CacheErr (Icache):%llx, core %d/cpu %d, cp0_errorepc == %lx\n",
				   (unsigned long long)icache_err, core, cpu,
				   read_c0_errorepc());
		write_octeon_c0_icacheerr(0);
		edac_device_handle_ce(p->ed, cpu, 1, "icache");
	}
	if (dcache_err & 1) {
		edac_device_printk(p->ed, KERN_ERR,
				   "CacheErr (Dcache):%llx, core %d/cpu %d, cp0_errorepc == %lx\n",
				   (unsigned long long)dcache_err, core, cpu,
				   read_c0_errorepc());
		if (event)
			edac_device_handle_ue(p->ed, cpu, 0, "dcache");
		else
			edac_device_handle_ce(p->ed, cpu, 0, "dcache");

		/* Clear the error indication */
		if (OCTEON_IS_OCTEON2())
			write_octeon_c0_dcacheerr(1);
		else
			write_octeon_c0_dcacheerr(0);
	}

	return NOTIFY_STOP;
}

static int co_cache_error_probe(struct platform_device *pdev)
{
	struct co_cache_error *p = devm_kzalloc(&pdev->dev, sizeof(*p),
						GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->notifier.notifier_call = co_cache_error_event;
	platform_set_drvdata(pdev, p);

	p->ed = edac_device_alloc_ctl_info(0, "cpu", num_possible_cpus(),
					   "cache", 2, 0,
					   edac_device_alloc_index());
	if (!p->ed)
		goto err;

	p->ed->dev = &pdev->dev;

	p->ed->dev_name = dev_name(&pdev->dev);

	p->ed->mod_name = "octeon-cpu";
	p->ed->ctl_name = "cache";

	if (edac_device_add_device(p->ed)) {
		pr_err("%s: edac_device_add_device() failed\n", __func__);
		goto err1;
	}

	register_co_cache_error_notifier(&p->notifier);

	return 0;

err1:
	edac_device_free_ctl_info(p->ed);
err:
	return -ENXIO;
}

static void co_cache_error_remove(struct platform_device *pdev)
{
	struct co_cache_error *p = platform_get_drvdata(pdev);

	unregister_co_cache_error_notifier(&p->notifier);
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(p->ed);
}

static struct platform_driver co_cache_error_driver = {
	.probe = co_cache_error_probe,
	.remove_new = co_cache_error_remove,
	.driver = {
		   .name = "octeon_pc_edac",
	}
};
module_platform_driver(co_cache_error_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
