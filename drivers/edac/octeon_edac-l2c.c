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
#include <linux/io.h>
#include <linux/edac.h>

#include <asm/octeon/cvmx.h>

#include "edac_core.h"
#include "edac_module.h"

#define EDAC_MOD_STR "octeon-l2c"

static void octeon_l2c_poll_oct1(struct edac_device_ctl_info *l2c)
{
	union cvmx_l2t_err l2t_err, l2t_err_reset;
	union cvmx_l2d_err l2d_err, l2d_err_reset;

	l2t_err_reset.u64 = 0;
	l2t_err.u64 = cvmx_read_csr(CVMX_L2T_ERR);
	if (l2t_err.s.sec_err) {
		edac_device_handle_ce(l2c, 0, 0,
				      "Tag Single bit error (corrected)");
		l2t_err_reset.s.sec_err = 1;
	}
	if (l2t_err.s.ded_err) {
		edac_device_handle_ue(l2c, 0, 0,
				      "Tag Double bit error (detected)");
		l2t_err_reset.s.ded_err = 1;
	}
	if (l2t_err_reset.u64)
		cvmx_write_csr(CVMX_L2T_ERR, l2t_err_reset.u64);

	l2d_err_reset.u64 = 0;
	l2d_err.u64 = cvmx_read_csr(CVMX_L2D_ERR);
	if (l2d_err.s.sec_err) {
		edac_device_handle_ce(l2c, 0, 1,
				      "Data Single bit error (corrected)");
		l2d_err_reset.s.sec_err = 1;
	}
	if (l2d_err.s.ded_err) {
		edac_device_handle_ue(l2c, 0, 1,
				      "Data Double bit error (detected)");
		l2d_err_reset.s.ded_err = 1;
	}
	if (l2d_err_reset.u64)
		cvmx_write_csr(CVMX_L2D_ERR, l2d_err_reset.u64);

}

static void _octeon_l2c_poll_oct2(struct edac_device_ctl_info *l2c, int tad)
{
	union cvmx_l2c_err_tdtx err_tdtx, err_tdtx_reset;
	union cvmx_l2c_err_ttgx err_ttgx, err_ttgx_reset;
	char buf1[64];
	char buf2[80];

	err_tdtx_reset.u64 = 0;
	err_tdtx.u64 = cvmx_read_csr(CVMX_L2C_ERR_TDTX(tad));
	if (err_tdtx.s.dbe || err_tdtx.s.sbe ||
	    err_tdtx.s.vdbe || err_tdtx.s.vsbe)
		snprintf(buf1, sizeof(buf1),
			 "type:%d, syn:0x%x, way:%d",
			 err_tdtx.s.type, err_tdtx.s.syn, err_tdtx.s.wayidx);

	if (err_tdtx.s.dbe) {
		snprintf(buf2, sizeof(buf2),
			 "L2D Double bit error (detected):%s", buf1);
		err_tdtx_reset.s.dbe = 1;
		edac_device_handle_ue(l2c, tad, 1, buf2);
	}
	if (err_tdtx.s.sbe) {
		snprintf(buf2, sizeof(buf2),
			 "L2D Single bit error (corrected):%s", buf1);
		err_tdtx_reset.s.sbe = 1;
		edac_device_handle_ce(l2c, tad, 1, buf2);
	}
	if (err_tdtx.s.vdbe) {
		snprintf(buf2, sizeof(buf2),
			 "VBF Double bit error (detected):%s", buf1);
		err_tdtx_reset.s.vdbe = 1;
		edac_device_handle_ue(l2c, tad, 1, buf2);
	}
	if (err_tdtx.s.vsbe) {
		snprintf(buf2, sizeof(buf2),
			 "VBF Single bit error (corrected):%s", buf1);
		err_tdtx_reset.s.vsbe = 1;
		edac_device_handle_ce(l2c, tad, 1, buf2);
	}
	if (err_tdtx_reset.u64)
		cvmx_write_csr(CVMX_L2C_ERR_TDTX(tad), err_tdtx_reset.u64);

	err_ttgx_reset.u64 = 0;
	err_ttgx.u64 = cvmx_read_csr(CVMX_L2C_ERR_TTGX(tad));

	if (err_ttgx.s.dbe || err_ttgx.s.sbe)
		snprintf(buf1, sizeof(buf1),
			 "type:%d, syn:0x%x, way:%d",
			 err_ttgx.s.type, err_ttgx.s.syn, err_ttgx.s.wayidx);

	if (err_ttgx.s.dbe) {
		snprintf(buf2, sizeof(buf2),
			 "Tag Double bit error (detected):%s", buf1);
		err_ttgx_reset.s.dbe = 1;
		edac_device_handle_ue(l2c, tad, 0, buf2);
	}
	if (err_ttgx.s.sbe) {
		snprintf(buf2, sizeof(buf2),
			 "Tag Single bit error (corrected):%s", buf1);
		err_ttgx_reset.s.sbe = 1;
		edac_device_handle_ce(l2c, tad, 0, buf2);
	}
	if (err_ttgx_reset.u64)
		cvmx_write_csr(CVMX_L2C_ERR_TTGX(tad), err_ttgx_reset.u64);
}

static void octeon_l2c_poll_oct2(struct edac_device_ctl_info *l2c)
{
	int i;
	for (i = 0; i < l2c->nr_instances; i++)
		_octeon_l2c_poll_oct2(l2c, i);
}

static int octeon_l2c_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *l2c;

	int num_tads = OCTEON_IS_MODEL(OCTEON_CN68XX) ? 4 : 1;

	/* 'Tags' are block 0, 'Data' is block 1*/
	l2c = edac_device_alloc_ctl_info(0, "l2c", num_tads, "l2c", 2, 0,
					 NULL, 0, edac_device_alloc_index());
	if (!l2c)
		return -ENOMEM;

	l2c->dev = &pdev->dev;
	platform_set_drvdata(pdev, l2c);
	l2c->dev_name = dev_name(&pdev->dev);

	l2c->mod_name = "octeon-l2c";
	l2c->ctl_name = "octeon_l2c_err";


	if (OCTEON_IS_OCTEON1PLUS()) {
		union cvmx_l2t_err l2t_err;
		union cvmx_l2d_err l2d_err;

		l2t_err.u64 = cvmx_read_csr(CVMX_L2T_ERR);
		l2t_err.s.sec_intena = 0;	/* We poll */
		l2t_err.s.ded_intena = 0;
		cvmx_write_csr(CVMX_L2T_ERR, l2t_err.u64);

		l2d_err.u64 = cvmx_read_csr(CVMX_L2D_ERR);
		l2d_err.s.sec_intena = 0;	/* We poll */
		l2d_err.s.ded_intena = 0;
		cvmx_write_csr(CVMX_L2T_ERR, l2d_err.u64);

		l2c->edac_check = octeon_l2c_poll_oct1;
	} else {
		/* OCTEON II */
		l2c->edac_check = octeon_l2c_poll_oct2;
	}

	if (edac_device_add_device(l2c) > 0) {
		pr_err("%s: edac_device_add_device() failed\n", __func__);
		goto err;
	}


	return 0;

err:
	edac_device_free_ctl_info(l2c);

	return -ENXIO;
}

static int octeon_l2c_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *l2c = platform_get_drvdata(pdev);

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(l2c);

	return 0;
}

static struct platform_driver octeon_l2c_driver = {
	.probe = octeon_l2c_probe,
	.remove = octeon_l2c_remove,
	.driver = {
		   .name = "octeon_l2c_edac",
	}
};
module_platform_driver(octeon_l2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
