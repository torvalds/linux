/*
 * Hardware Random Number Generator support for Cavium Networks
 * Octeon processor family.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009 Cavium Networks
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/gfp.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-rnm-defs.h>

struct octeon_rng {
	struct hwrng ops;
	void __iomem *control_status;
	void __iomem *result;
};

static int octeon_rng_init(struct hwrng *rng)
{
	union cvmx_rnm_ctl_status ctl;
	struct octeon_rng *p = container_of(rng, struct octeon_rng, ops);

	ctl.u64 = 0;
	ctl.s.ent_en = 1; /* Enable the entropy source.  */
	ctl.s.rng_en = 1; /* Enable the RNG hardware.  */
	cvmx_write_csr((u64)p->control_status, ctl.u64);
	return 0;
}

static void octeon_rng_cleanup(struct hwrng *rng)
{
	union cvmx_rnm_ctl_status ctl;
	struct octeon_rng *p = container_of(rng, struct octeon_rng, ops);

	ctl.u64 = 0;
	/* Disable everything.  */
	cvmx_write_csr((u64)p->control_status, ctl.u64);
}

static int octeon_rng_data_read(struct hwrng *rng, u32 *data)
{
	struct octeon_rng *p = container_of(rng, struct octeon_rng, ops);

	*data = cvmx_read64_uint32((u64)p->result);
	return sizeof(u32);
}

static int __devinit octeon_rng_probe(struct platform_device *pdev)
{
	struct resource *res_ports;
	struct resource *res_result;
	struct octeon_rng *rng;
	int ret;
	struct hwrng ops = {
		.name = "octeon",
		.init = octeon_rng_init,
		.cleanup = octeon_rng_cleanup,
		.data_read = octeon_rng_data_read
	};

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	res_ports = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_ports)
		goto err_ports;

	res_result = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res_result)
		goto err_ports;


	rng->control_status = devm_ioremap_nocache(&pdev->dev,
						   res_ports->start,
						   sizeof(u64));
	if (!rng->control_status)
		goto err_ports;

	rng->result = devm_ioremap_nocache(&pdev->dev,
					   res_result->start,
					   sizeof(u64));
	if (!rng->result)
		goto err_r;

	rng->ops = ops;

	dev_set_drvdata(&pdev->dev, &rng->ops);
	ret = hwrng_register(&rng->ops);
	if (ret)
		goto err;

	dev_info(&pdev->dev, "Octeon Random Number Generator\n");

	return 0;
err:
	devm_iounmap(&pdev->dev, rng->control_status);
err_r:
	devm_iounmap(&pdev->dev, rng->result);
err_ports:
	devm_kfree(&pdev->dev, rng);
	return -ENOENT;
}

static int __exit octeon_rng_remove(struct platform_device *pdev)
{
	struct hwrng *rng = dev_get_drvdata(&pdev->dev);

	hwrng_unregister(rng);

	return 0;
}

static struct platform_driver octeon_rng_driver = {
	.driver = {
		.name		= "octeon_rng",
		.owner		= THIS_MODULE,
	},
	.probe		= octeon_rng_probe,
	.remove		= __exit_p(octeon_rng_remove),
};

static int __init octeon_rng_mod_init(void)
{
	return platform_driver_register(&octeon_rng_driver);
}

static void __exit octeon_rng_mod_exit(void)
{
	platform_driver_unregister(&octeon_rng_driver);
}

module_init(octeon_rng_mod_init);
module_exit(octeon_rng_mod_exit);

MODULE_AUTHOR("David Daney");
MODULE_LICENSE("GPL");
