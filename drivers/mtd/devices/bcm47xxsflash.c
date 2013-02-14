#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/platform_device.h>
#include <linux/bcma/bcma.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Serial flash driver for BCMA bus");

static const char *probes[] = { "bcm47xxpart", NULL };

static int bcm47xxsflash_read(struct mtd_info *mtd, loff_t from, size_t len,
			      size_t *retlen, u_char *buf)
{
	struct bcma_sflash *sflash = mtd->priv;

	/* Check address range */
	if ((from + len) > mtd->size)
		return -EINVAL;

	memcpy_fromio(buf, (void __iomem *)KSEG0ADDR(sflash->window + from),
		      len);

	return len;
}

static void bcm47xxsflash_fill_mtd(struct bcma_sflash *sflash,
				   struct mtd_info *mtd)
{
	mtd->priv = sflash;
	mtd->name = "bcm47xxsflash";
	mtd->owner = THIS_MODULE;
	mtd->type = MTD_ROM;
	mtd->size = sflash->size;
	mtd->_read = bcm47xxsflash_read;

	/* TODO: implement writing support and verify/change following code */
	mtd->flags = MTD_CAP_ROM;
	mtd->writebufsize = mtd->writesize = 1;
}

static int bcm47xxsflash_probe(struct platform_device *pdev)
{
	struct bcma_sflash *sflash = dev_get_platdata(&pdev->dev);
	int err;

	sflash->mtd = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!sflash->mtd) {
		err = -ENOMEM;
		goto out;
	}
	bcm47xxsflash_fill_mtd(sflash, sflash->mtd);

	err = mtd_device_parse_register(sflash->mtd, probes, NULL, NULL, 0);
	if (err) {
		pr_err("Failed to register MTD device: %d\n", err);
		goto err_dev_reg;
	}

	return 0;

err_dev_reg:
	kfree(sflash->mtd);
out:
	return err;
}

static int bcm47xxsflash_remove(struct platform_device *pdev)
{
	struct bcma_sflash *sflash = dev_get_platdata(&pdev->dev);

	mtd_device_unregister(sflash->mtd);
	kfree(sflash->mtd);

	return 0;
}

static struct platform_driver bcma_sflash_driver = {
	.remove = bcm47xxsflash_remove,
	.driver = {
		.name = "bcma_sflash",
		.owner = THIS_MODULE,
	},
};

static int __init bcm47xxsflash_init(void)
{
	int err;

	err = platform_driver_probe(&bcma_sflash_driver, bcm47xxsflash_probe);
	if (err)
		pr_err("Failed to register BCMA serial flash driver: %d\n",
		       err);

	return err;
}

static void __exit bcm47xxsflash_exit(void)
{
	platform_driver_unregister(&bcma_sflash_driver);
}

module_init(bcm47xxsflash_init);
module_exit(bcm47xxsflash_exit);
