/*
 * FPGA Framework
 *
 *  Copyright (C) 2013 Altera Corporation
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/fpga.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include "fpga-mgr.h"

static DEFINE_IDA(fpga_mgr_ida);
static int fpga_mgr_major;
static struct class *fpga_mgr_class;

#define FPGA_MAX_MINORS	256

/*
 * class attributes
 */
static ssize_t fpga_mgr_name_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = dev_get_drvdata(dev);

	if (!mgr)
		return -ENODEV;

	return snprintf(buf, sizeof(mgr->name), "%s\n", mgr->name);
}

static ssize_t fpga_mgr_status_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = dev_get_drvdata(dev);

	if (!mgr || !mgr->mops || !mgr->mops->status)
		return -ENODEV;

	return mgr->mops->status(mgr, buf);
}

static struct device_attribute fpga_mgr_attrs[] = {
	__ATTR(name, S_IRUGO, fpga_mgr_name_show, NULL),
	__ATTR(status, S_IRUGO, fpga_mgr_status_show, NULL),
	__ATTR_NULL
};

static int fpga_mgr_get_new_minor(struct fpga_manager *mgr, int request_nr)
{
	int nr, start;

	/* check specified minor number */
	if (request_nr >= FPGA_MAX_MINORS) {
		dev_err(mgr->parent, "Out of device minors (%d)\n", request_nr);
		return -ENODEV;
	}

	/*
	 * If request_nr == -1, dynamically allocate number.
	 * If request_nr >= 0, attempt to get specific number.
	 */
	if (request_nr == -1)
		start = 0;
	else
		start = request_nr;

	nr = ida_simple_get(&fpga_mgr_ida, start, FPGA_MAX_MINORS, GFP_KERNEL);

	/* return error code */
	if (nr < 0)
		return nr;

	if ((request_nr != -1) && (request_nr != nr)) {
		dev_err(mgr->parent,
			"Could not get requested device minor (%d)\n", nr);
		ida_simple_remove(&fpga_mgr_ida, nr);
		return -ENODEV;
	}

	mgr->nr = nr;

	return 0;
}

static void fpga_mgr_free_minor(int nr)
{
	ida_simple_remove(&fpga_mgr_ida, nr);
}

int register_fpga_manager(struct platform_device *pdev,
			struct fpga_manager_ops *mops, char *name, void *priv)
{
	struct fpga_manager *mgr;
	int ret;

	if (!mops) {
		dev_err(&pdev->dev,
			"Attempt to register with no fpga_manager_ops\n");
		return -EINVAL;
	}
	if (!name || (name[0] == '\0')) {
		dev_err(&pdev->dev, "Attempt to register with no name!\n");
		return -EINVAL;
	}

	mgr = kzalloc(sizeof(struct fpga_manager), GFP_KERNEL);
	if (!mgr)
		return -ENOMEM;

	platform_set_drvdata(pdev, mgr);
	mgr->mops = mops;
	mgr->np = pdev->dev.of_node;
	mgr->parent = get_device(&pdev->dev);
	mgr->priv = priv;
	strlcpy(mgr->name, name, sizeof(mgr->name));
	init_completion(&mgr->status_complete);

	ret = fpga_mgr_get_new_minor(mgr, pdev->id);
	if (ret)
		goto error_kfree;

	ret = fpga_mgr_attach_transport(mgr);
	if (ret)
		goto error_attach;

	if (mops->isr) {
		mgr->irq = irq_of_parse_and_map(mgr->np, 0);
		if (mgr->irq == NO_IRQ) {
			dev_err(mgr->parent, "failed to map interrupt\n");
			goto error_irq_map;
		}

		ret = request_irq(mgr->irq, mops->isr, 0, "fpga-mgr", mgr);
		if (ret < 0) {
			dev_err(mgr->parent, "error requesting interrupt\n");
			goto error_irq_req;
		}
	}

	cdev_init(&mgr->cdev, &fpga_mgr_fops);
	ret = cdev_add(&mgr->cdev, MKDEV(fpga_mgr_major, mgr->nr), 1);
	if (ret)
		goto error_cdev;

	mgr->dev = device_create(fpga_mgr_class, mgr->parent,
				 MKDEV(fpga_mgr_major, mgr->nr), mgr,
				 "fpga%d", mgr->nr);
	if (IS_ERR(mgr->dev)) {
		ret = PTR_ERR(mgr->dev);
		goto error_device;
	}

	dev_info(mgr->parent, "fpga manager [%s] registered as minor %d\n",
		 mgr->name, mgr->nr);

	return 0;

error_device:
	cdev_del(&mgr->cdev);
error_cdev:
	free_irq(mgr->irq, mgr);
error_irq_req:
	irq_dispose_mapping(mgr->irq);
error_irq_map:
	fpga_mgr_detach_transport(mgr);
error_attach:
	fpga_mgr_free_minor(mgr->nr);
error_kfree:
	put_device(mgr->parent);
	kfree(mgr);
	return ret;
}
EXPORT_SYMBOL_GPL(register_fpga_manager);

void remove_fpga_manager(struct platform_device *pdev)
{
	struct fpga_manager *mgr = platform_get_drvdata(pdev);

	if (mgr && mgr->mops && mgr->mops->fpga_remove)
		mgr->mops->fpga_remove(mgr);

	device_destroy(fpga_mgr_class, MKDEV(fpga_mgr_major, mgr->nr));
	cdev_del(&mgr->cdev);
	free_irq(mgr->irq, mgr);
	irq_dispose_mapping(mgr->irq);
	fpga_mgr_detach_transport(mgr);
	fpga_mgr_free_minor(mgr->nr);
	put_device(mgr->parent);
	kfree(mgr);
}
EXPORT_SYMBOL_GPL(remove_fpga_manager);

static int __init fpga_mgr_dev_init(void)
{
	dev_t fpga_mgr_dev;
	int ret;

	pr_info("FPGA Mangager framework driver\n");

	fpga_mgr_class = class_create(THIS_MODULE, "fpga");
	if (IS_ERR(fpga_mgr_class))
		return PTR_ERR(fpga_mgr_class);

	fpga_mgr_class->dev_attrs = fpga_mgr_attrs;

	ret = alloc_chrdev_region(&fpga_mgr_dev, 0, FPGA_MAX_MINORS, "fpga");
	if (ret) {
		class_destroy(fpga_mgr_class);
		return ret;
	}

	fpga_mgr_major = MAJOR(fpga_mgr_dev);

	return 0;
}

static void __exit fpga_mgr_dev_exit(void)
{
	unregister_chrdev_region(MKDEV(fpga_mgr_major, 0), FPGA_MAX_MINORS);
	class_destroy(fpga_mgr_class);
	ida_destroy(&fpga_mgr_ida);
}

MODULE_DESCRIPTION("FPGA Manager framework driver");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_mgr_dev_init);
module_exit(fpga_mgr_dev_exit);
