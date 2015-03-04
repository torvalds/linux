/* uio_fsl_elbc_gpcm: UIO driver for eLBC/GPCM peripherals

   Copyright (C) 2014 Linutronix GmbH
     Author: John Ogness <john.ogness@linutronix.de>

   This driver provides UIO access to memory of a peripheral connected
   to the Freescale enhanced local bus controller (eLBC) interface
   using the general purpose chip-select mode (GPCM).

   Here is an example of the device tree entries:

	localbus@ffe05000 {
		ranges = <0x2 0x0 0x0 0xff810000 0x10000>;

		dpm@2,0 {
			compatible = "fsl,elbc-gpcm-uio";
			reg = <0x2 0x0 0x10000>;
			elbc-gpcm-br = <0xff810800>;
			elbc-gpcm-or = <0xffff09f7>;
			interrupt-parent = <&mpic>;
			interrupts = <4 1>;
			device_type = "netx5152";
			uio_name = "netx_custom";
			netx5152,init-win0-offset = <0x0>;
		};
	};

   Only the entries reg (to identify bank) and elbc-gpcm-* (initial BR/OR
   values) are required. The entries interrupt*, device_type, and uio_name
   are optional (as well as any type-specific options such as
   netx5152,init-win0-offset). As long as no interrupt handler is needed,
   this driver can be used without any type-specific implementation.

   The netx5152 type has been tested to work with the netX 51/52 hardware
   from Hilscher using the Hilscher userspace netX stack.

   The netx5152 type should serve as a model to add new type-specific
   devices as needed.
*/

#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/uio_driver.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/fsl_lbc.h>

#define MAX_BANKS 8

struct fsl_elbc_gpcm {
	struct device *dev;
	struct fsl_lbc_regs __iomem *lbc;
	u32 bank;
	const char *name;

	void (*init)(struct uio_info *info);
	void (*shutdown)(struct uio_info *info, bool init_err);
	irqreturn_t (*irq_handler)(int irq, struct uio_info *info);
};

static ssize_t reg_show(struct device *dev, struct device_attribute *attr,
			char *buf);
static ssize_t reg_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);

DEVICE_ATTR(reg_br, S_IRUGO|S_IWUSR|S_IWGRP, reg_show, reg_store);
DEVICE_ATTR(reg_or, S_IRUGO|S_IWUSR|S_IWGRP, reg_show, reg_store);

static ssize_t reg_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct uio_info *info = platform_get_drvdata(pdev);
	struct fsl_elbc_gpcm *priv = info->priv;
	struct fsl_lbc_bank *bank = &priv->lbc->bank[priv->bank];

	if (attr == &dev_attr_reg_br) {
		return scnprintf(buf, PAGE_SIZE, "0x%08x\n",
				 in_be32(&bank->br));

	} else if (attr == &dev_attr_reg_or) {
		return scnprintf(buf, PAGE_SIZE, "0x%08x\n",
				 in_be32(&bank->or));
	}

	return 0;
}

static ssize_t reg_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct uio_info *info = platform_get_drvdata(pdev);
	struct fsl_elbc_gpcm *priv = info->priv;
	struct fsl_lbc_bank *bank = &priv->lbc->bank[priv->bank];
	unsigned long val;
	u32 reg_br_cur;
	u32 reg_or_cur;
	u32 reg_new;

	/* parse use input */
	if (kstrtoul(buf, 0, &val) != 0)
		return -EINVAL;
	reg_new = (u32)val;

	/* read current values */
	reg_br_cur = in_be32(&bank->br);
	reg_or_cur = in_be32(&bank->or);

	if (attr == &dev_attr_reg_br) {
		/* not allowed to change effective base address */
		if ((reg_br_cur & reg_or_cur & BR_BA) !=
		    (reg_new & reg_or_cur & BR_BA)) {
			return -EINVAL;
		}

		/* not allowed to change mode */
		if ((reg_new & BR_MSEL) != BR_MS_GPCM)
			return -EINVAL;

		/* write new value (force valid) */
		out_be32(&bank->br, reg_new | BR_V);

	} else if (attr == &dev_attr_reg_or) {
		/* not allowed to change access mask */
		if ((reg_or_cur & OR_GPCM_AM) != (reg_new & OR_GPCM_AM))
			return -EINVAL;

		/* write new value */
		out_be32(&bank->or, reg_new);

	} else {
		return -EINVAL;
	}

	return count;
}

#ifdef CONFIG_UIO_FSL_ELBC_GPCM_NETX5152
#define DPM_HOST_WIN0_OFFSET	0xff00
#define DPM_HOST_INT_STAT0	0xe0
#define DPM_HOST_INT_EN0	0xf0
#define DPM_HOST_INT_MASK	0xe600ffff
#define DPM_HOST_INT_GLOBAL_EN	0x80000000

static irqreturn_t netx5152_irq_handler(int irq, struct uio_info *info)
{
	void __iomem *reg_int_en = info->mem[0].internal_addr +
					DPM_HOST_WIN0_OFFSET +
					DPM_HOST_INT_EN0;
	void __iomem *reg_int_stat = info->mem[0].internal_addr +
					DPM_HOST_WIN0_OFFSET +
					DPM_HOST_INT_STAT0;

	/* check if an interrupt is enabled and active */
	if ((ioread32(reg_int_en) & ioread32(reg_int_stat) &
	     DPM_HOST_INT_MASK) == 0) {
		return IRQ_NONE;
	}

	/* disable interrupts */
	iowrite32(ioread32(reg_int_en) & ~DPM_HOST_INT_GLOBAL_EN, reg_int_en);

	return IRQ_HANDLED;
}

static void netx5152_init(struct uio_info *info)
{
	unsigned long win0_offset = DPM_HOST_WIN0_OFFSET;
	struct fsl_elbc_gpcm *priv = info->priv;
	const void *prop;

	/* get an optional initial win0 offset */
	prop = of_get_property(priv->dev->of_node,
			       "netx5152,init-win0-offset", NULL);
	if (prop)
		win0_offset = of_read_ulong(prop, 1);

	/* disable interrupts */
	iowrite32(0, info->mem[0].internal_addr + win0_offset +
		     DPM_HOST_INT_EN0);
}

static void netx5152_shutdown(struct uio_info *info, bool init_err)
{
	if (init_err)
		return;

	/* disable interrupts */
	iowrite32(0, info->mem[0].internal_addr + DPM_HOST_WIN0_OFFSET +
		     DPM_HOST_INT_EN0);
}
#endif

static void setup_periph(struct fsl_elbc_gpcm *priv,
				   const char *type)
{
#ifdef CONFIG_UIO_FSL_ELBC_GPCM_NETX5152
	if (strcmp(type, "netx5152") == 0) {
		priv->irq_handler = netx5152_irq_handler;
		priv->init = netx5152_init;
		priv->shutdown = netx5152_shutdown;
		priv->name = "netX 51/52";
		return;
	}
#endif
}

static int check_of_data(struct fsl_elbc_gpcm *priv,
				   struct resource *res,
				   u32 reg_br, u32 reg_or)
{
	/* check specified bank */
	if (priv->bank >= MAX_BANKS) {
		dev_err(priv->dev, "invalid bank\n");
		return -ENODEV;
	}

	/* check specified mode (BR_MS_GPCM is 0) */
	if ((reg_br & BR_MSEL) != BR_MS_GPCM) {
		dev_err(priv->dev, "unsupported mode\n");
		return -ENODEV;
	}

	/* check specified mask vs. resource size */
	if ((~(reg_or & OR_GPCM_AM) + 1) != resource_size(res)) {
		dev_err(priv->dev, "address mask / size mismatch\n");
		return -ENODEV;
	}

	/* check specified address */
	if ((reg_br & reg_or & BR_BA) != fsl_lbc_addr(res->start)) {
		dev_err(priv->dev, "base address mismatch\n");
		return -ENODEV;
	}

	return 0;
}

static int get_of_data(struct fsl_elbc_gpcm *priv, struct device_node *node,
		       struct resource *res, u32 *reg_br,
		       u32 *reg_or, unsigned int *irq, char **name)
{
	const char *dt_name;
	const char *type;
	int ret;

	/* get the memory resource */
	ret = of_address_to_resource(node, 0, res);
	if (ret) {
		dev_err(priv->dev, "failed to get resource\n");
		return ret;
	}

	/* get the bank number */
	ret = of_property_read_u32(node, "reg", &priv->bank);
	if (ret) {
		dev_err(priv->dev, "failed to get bank number\n");
		return ret;
	}

	/* get BR value to set */
	ret = of_property_read_u32(node, "elbc-gpcm-br", reg_br);
	if (ret) {
		dev_err(priv->dev, "missing elbc-gpcm-br value\n");
		return ret;
	}

	/* get OR value to set */
	ret = of_property_read_u32(node, "elbc-gpcm-or", reg_or);
	if (ret) {
		dev_err(priv->dev, "missing elbc-gpcm-or value\n");
		return ret;
	}

	/* get optional peripheral type */
	priv->name = "generic";
	if (of_property_read_string(node, "device_type", &type) == 0)
		setup_periph(priv, type);

	/* get optional irq value */
	*irq = irq_of_parse_and_map(node, 0);

	/* sanity check device tree data */
	ret = check_of_data(priv, res, *reg_br, *reg_or);
	if (ret)
		return ret;

	/* get optional uio name */
	if (of_property_read_string(node, "uio_name", &dt_name) != 0)
		dt_name = "eLBC_GPCM";
	*name = kstrdup(dt_name, GFP_KERNEL);
	if (!*name)
		return -ENOMEM;

	return 0;
}

static int uio_fsl_elbc_gpcm_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct fsl_elbc_gpcm *priv;
	struct uio_info *info;
	char *uio_name = NULL;
	struct resource res;
	unsigned int irq;
	u32 reg_br_cur;
	u32 reg_or_cur;
	u32 reg_br_new;
	u32 reg_or_new;
	int ret;

	if (!fsl_lbc_ctrl_dev || !fsl_lbc_ctrl_dev->regs)
		return -ENODEV;

	/* allocate private data */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;
	priv->lbc = fsl_lbc_ctrl_dev->regs;

	/* get device tree data */
	ret = get_of_data(priv, node, &res, &reg_br_new, &reg_or_new,
			  &irq, &uio_name);
	if (ret)
		goto out_err0;

	/* allocate UIO structure */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto out_err0;
	}

	/* get current BR/OR values */
	reg_br_cur = in_be32(&priv->lbc->bank[priv->bank].br);
	reg_or_cur = in_be32(&priv->lbc->bank[priv->bank].or);

	/* if bank already configured, make sure it matches */
	if ((reg_br_cur & BR_V)) {
		if ((reg_br_cur & BR_MSEL) != BR_MS_GPCM ||
		    (reg_br_cur & reg_or_cur & BR_BA)
		     != fsl_lbc_addr(res.start)) {
			dev_err(priv->dev,
				"bank in use by another peripheral\n");
			ret = -ENODEV;
			goto out_err1;
		}

		/* warn if behavior settings changing */
		if ((reg_br_cur & ~(BR_BA | BR_V)) !=
		    (reg_br_new & ~(BR_BA | BR_V))) {
			dev_warn(priv->dev,
				 "modifying BR settings: 0x%08x -> 0x%08x",
				 reg_br_cur, reg_br_new);
		}
		if ((reg_or_cur & ~OR_GPCM_AM) != (reg_or_new & ~OR_GPCM_AM)) {
			dev_warn(priv->dev,
				 "modifying OR settings: 0x%08x -> 0x%08x",
				 reg_or_cur, reg_or_new);
		}
	}

	/* configure the bank (force base address and GPCM) */
	reg_br_new &= ~(BR_BA | BR_MSEL);
	reg_br_new |= fsl_lbc_addr(res.start) | BR_MS_GPCM | BR_V;
	out_be32(&priv->lbc->bank[priv->bank].or, reg_or_new);
	out_be32(&priv->lbc->bank[priv->bank].br, reg_br_new);

	/* map the memory resource */
	info->mem[0].internal_addr = ioremap(res.start, resource_size(&res));
	if (!info->mem[0].internal_addr) {
		dev_err(priv->dev, "failed to map chip region\n");
		ret = -ENODEV;
		goto out_err1;
	}

	/* set all UIO data */
	if (node->name)
		info->mem[0].name = kstrdup(node->name, GFP_KERNEL);
	info->mem[0].addr = res.start;
	info->mem[0].size = resource_size(&res);
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->priv = priv;
	info->name = uio_name;
	info->version = "0.0.1";
	if (irq != NO_IRQ) {
		if (priv->irq_handler) {
			info->irq = irq;
			info->irq_flags = IRQF_SHARED;
			info->handler = priv->irq_handler;
		} else {
			irq = NO_IRQ;
			dev_warn(priv->dev, "ignoring irq, no handler\n");
		}
	}

	if (priv->init)
		priv->init(info);

	/* register UIO device */
	if (uio_register_device(priv->dev, info) != 0) {
		dev_err(priv->dev, "UIO registration failed\n");
		ret = -ENODEV;
		goto out_err2;
	}

	/* store private data */
	platform_set_drvdata(pdev, info);

	/* create sysfs files */
	ret = device_create_file(priv->dev, &dev_attr_reg_br);
	if (ret)
		goto out_err3;
	ret = device_create_file(priv->dev, &dev_attr_reg_or);
	if (ret)
		goto out_err4;

	dev_info(priv->dev,
		 "eLBC/GPCM device (%s) at 0x%llx, bank %d, irq=%d\n",
		 priv->name, (unsigned long long)res.start, priv->bank,
		 irq != NO_IRQ ? irq : -1);

	return 0;
out_err4:
	device_remove_file(priv->dev, &dev_attr_reg_br);
out_err3:
	platform_set_drvdata(pdev, NULL);
	uio_unregister_device(info);
out_err2:
	if (priv->shutdown)
		priv->shutdown(info, true);
	iounmap(info->mem[0].internal_addr);
out_err1:
	kfree(info->mem[0].name);
	kfree(info);
out_err0:
	kfree(uio_name);
	kfree(priv);
	return ret;
}

static int uio_fsl_elbc_gpcm_remove(struct platform_device *pdev)
{
	struct uio_info *info = platform_get_drvdata(pdev);
	struct fsl_elbc_gpcm *priv = info->priv;

	device_remove_file(priv->dev, &dev_attr_reg_or);
	device_remove_file(priv->dev, &dev_attr_reg_br);
	platform_set_drvdata(pdev, NULL);
	uio_unregister_device(info);
	if (priv->shutdown)
		priv->shutdown(info, false);
	iounmap(info->mem[0].internal_addr);
	kfree(info->mem[0].name);
	kfree(info->name);
	kfree(info);
	kfree(priv);

	return 0;

}

static const struct of_device_id uio_fsl_elbc_gpcm_match[] = {
	{ .compatible = "fsl,elbc-gpcm-uio", },
	{}
};

static struct platform_driver uio_fsl_elbc_gpcm_driver = {
	.driver = {
		.name = "fsl,elbc-gpcm-uio",
		.owner = THIS_MODULE,
		.of_match_table = uio_fsl_elbc_gpcm_match,
	},
	.probe = uio_fsl_elbc_gpcm_probe,
	.remove = uio_fsl_elbc_gpcm_remove,
};

static int __init uio_fsl_elbc_gpcm_init(void)
{
	return platform_driver_register(&uio_fsl_elbc_gpcm_driver);
}

static void __exit uio_fsl_elbc_gpcm_exit(void)
{
	platform_driver_unregister(&uio_fsl_elbc_gpcm_driver);
}

module_init(uio_fsl_elbc_gpcm_init);
module_exit(uio_fsl_elbc_gpcm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Ogness <john.ogness@linutronix.de>");
MODULE_DESCRIPTION("Freescale Enhanced Local Bus Controller GPCM driver");
