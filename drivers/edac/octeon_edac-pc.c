/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
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

#include "edac_core.h"
#include "edac_module.h"

#include <asm/octeon/cvmx.h>
#include <asm/mipsregs.h>

#define EDAC_MOD_STR "octeon"

extern int register_co_cache_error_notifier(struct notifier_block *nb);
extern int unregister_co_cache_error_notifier(struct notifier_block *nb);

extern unsigned long long cache_err_dcache[NR_CPUS];

static struct edac_device_ctl_info *ed_cavium;

/*
 * EDAC CPU cache error callback
 *
 */

static int  co_cache_error_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	unsigned int core = cvmx_get_core_num();
	unsigned int cpu = smp_processor_id();
	uint64_t icache_err = read_octeon_c0_icacheerr();
	struct edac_device_ctl_info *ed = ed_cavium;

	edac_device_printk(ed, KERN_ERR,
			   "Cache error exception on core %d / processor %d:\n",
			   core, cpu);
	edac_device_printk(ed, KERN_ERR,
			   "cp0_errorepc == %lx\n", read_c0_errorepc());
	if (icache_err & 1) {
		edac_device_printk(ed, KERN_ERR, "CacheErr (Icache) == %llx\n",
				   (unsigned long long)icache_err);
		write_octeon_c0_icacheerr(0);
		edac_device_handle_ce(ed, 0, 0, ed->ctl_name);
	}
	if (cache_err_dcache[core] & 1) {
		edac_device_printk(ed, KERN_ERR, "CacheErr (Dcache) == %llx\n",
				   (unsigned long long)cache_err_dcache[core]);
		cache_err_dcache[core] = 0;
		edac_device_handle_ue(ed, 0, 0, ed->ctl_name);
	}

	return NOTIFY_DONE;
}

static struct notifier_block co_cache_error_notifier = {
	.notifier_call = co_cache_error_event,
};

static int __devinit co_cache_error_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *ed;
	int res = 0;

	ed = edac_device_alloc_ctl_info(0, "cpu", 1, NULL, 0, 0, NULL, 0,
					edac_device_alloc_index());

	ed->dev = &pdev->dev;
	platform_set_drvdata(pdev, ed);
	ed->dev_name = dev_name(&pdev->dev);

	ed->mod_name = "octeon-cpu";
	ed->ctl_name = "co_cpu_err";

	if (edac_device_add_device(ed) > 0) {
		pr_err("%s: edac_device_add_device() failed\n", __func__);
		goto err;
	}

	register_co_cache_error_notifier(&co_cache_error_notifier);
	ed_cavium = ed;

	return 0;

err:
	edac_device_free_ctl_info(ed);

	return res;
}

static int co_cache_error_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *ed = platform_get_drvdata(pdev);

	unregister_co_cache_error_notifier(&co_cache_error_notifier);
	ed_cavium = NULL;
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(ed);

	return 0;
}

static struct platform_driver co_cache_error_driver = {
	.probe = co_cache_error_probe,
	.remove = co_cache_error_remove,
	.driver = {
		   .name = "co_pc_edac",
	}
};

static int __init co_edac_init(void)
{
	int ret;

	ret = platform_driver_register(&co_cache_error_driver);
	if (ret)
		pr_warning(EDAC_MOD_STR "CPU err failed to register\n");

	return ret;
}

static void __exit co_edac_exit(void)
{
	platform_driver_unregister(&co_cache_error_driver);
}

module_init(co_edac_init);
module_exit(co_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
