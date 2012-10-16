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
#include <linux/io.h>
#include <linux/edac.h>

#include <asm/octeon/cvmx.h>

#include "edac_core.h"
#include "edac_module.h"

#define EDAC_MOD_STR "octeon-l2c"

static void co_l2c_poll(struct edac_device_ctl_info *l2c)
{
	union cvmx_l2t_err l2t_err;

	l2t_err.u64 = cvmx_read_csr(CVMX_L2T_ERR);
	if (l2t_err.s.sec_err) {
		edac_device_handle_ce(l2c, 0, 0,
				      "Single bit error (corrected)");
		l2t_err.s.sec_err = 1;		/* Reset */
		cvmx_write_csr(CVMX_L2T_ERR, l2t_err.u64);
	}
	if (l2t_err.s.ded_err) {
		edac_device_handle_ue(l2c, 0, 0,
				      "Double bit error (corrected)");
		l2t_err.s.ded_err = 1;		/* Reset */
		cvmx_write_csr(CVMX_L2T_ERR, l2t_err.u64);
	}
}

static int __devinit co_l2c_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *l2c;
	union cvmx_l2t_err l2t_err;
	int res = 0;

	l2c = edac_device_alloc_ctl_info(0, "l2c", 1, NULL, 0, 0,
					 NULL, 0, edac_device_alloc_index());
	if (!l2c)
		return -ENOMEM;

	l2c->dev = &pdev->dev;
	platform_set_drvdata(pdev, l2c);
	l2c->dev_name = dev_name(&pdev->dev);

	l2c->mod_name = "octeon-l2c";
	l2c->ctl_name = "octeon_l2c_err";
	l2c->edac_check = co_l2c_poll;

	if (edac_device_add_device(l2c) > 0) {
		pr_err("%s: edac_device_add_device() failed\n", __func__);
		goto err;
	}

	l2t_err.u64 = cvmx_read_csr(CVMX_L2T_ERR);
	l2t_err.s.sec_intena = 0;	/* We poll */
	l2t_err.s.ded_intena = 0;
	l2t_err.s.sec_err = 1;		/* Clear, just in case */
	l2t_err.s.ded_err = 1;
	cvmx_write_csr(CVMX_L2T_ERR, l2t_err.u64);

	return 0;

err:
	edac_device_free_ctl_info(l2c);

	return res;
}

static int co_l2c_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *l2c = platform_get_drvdata(pdev);

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(l2c);

	return 0;
}

static struct platform_driver co_l2c_driver = {
	.probe = co_l2c_probe,
	.remove = co_l2c_remove,
	.driver = {
		   .name = "co_l2c_edac",
	}
};

static int __init co_edac_init(void)
{
	int ret;

	ret = platform_driver_register(&co_l2c_driver);
	if (ret)
		pr_warning(EDAC_MOD_STR " EDAC failed to register\n");

	return ret;
}

static void __exit co_edac_exit(void)
{
	platform_driver_unregister(&co_l2c_driver);
}

module_init(co_edac_init);
module_exit(co_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
