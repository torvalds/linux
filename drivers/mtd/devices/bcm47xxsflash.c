#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/platform_device.h>
#include <linux/bcma/bcma.h>

#include "bcm47xxsflash.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Serial flash driver for BCMA bus");

static const char * const probes[] = { "bcm47xxpart", NULL };

static int bcm47xxsflash_read(struct mtd_info *mtd, loff_t from, size_t len,
			      size_t *retlen, u_char *buf)
{
	struct bcm47xxsflash *b47s = mtd->priv;

	/* Check address range */
	if ((from + len) > mtd->size)
		return -EINVAL;

	memcpy_fromio(buf, (void __iomem *)KSEG0ADDR(b47s->window + from),
		      len);
	*retlen = len;

	return len;
}

static void bcm47xxsflash_fill_mtd(struct bcm47xxsflash *b47s)
{
	struct mtd_info *mtd = &b47s->mtd;

	mtd->priv = b47s;
	mtd->name = "bcm47xxsflash";
	mtd->owner = THIS_MODULE;
	mtd->type = MTD_ROM;
	mtd->size = b47s->size;
	mtd->_read = bcm47xxsflash_read;

	/* TODO: implement writing support and verify/change following code */
	mtd->flags = MTD_CAP_ROM;
	mtd->writebufsize = mtd->writesize = 1;
}

/**************************************************
 * BCMA
 **************************************************/

static int bcm47xxsflash_bcma_probe(struct platform_device *pdev)
{
	struct bcma_sflash *sflash = dev_get_platdata(&pdev->dev);
	struct bcm47xxsflash *b47s;
	int err;

	b47s = kzalloc(sizeof(*b47s), GFP_KERNEL);
	if (!b47s) {
		err = -ENOMEM;
		goto out;
	}
	sflash->priv = b47s;

	b47s->bcma_cc = container_of(sflash, struct bcma_drv_cc, sflash);

	switch (b47s->bcma_cc->capabilities & BCMA_CC_CAP_FLASHT) {
	case BCMA_CC_FLASHT_STSER:
		b47s->type = BCM47XXSFLASH_TYPE_ST;
		break;
	case BCMA_CC_FLASHT_ATSER:
		b47s->type = BCM47XXSFLASH_TYPE_ATMEL;
		break;
	}

	b47s->window = sflash->window;
	b47s->blocksize = sflash->blocksize;
	b47s->numblocks = sflash->numblocks;
	b47s->size = sflash->size;
	bcm47xxsflash_fill_mtd(b47s);

	err = mtd_device_parse_register(&b47s->mtd, probes, NULL, NULL, 0);
	if (err) {
		pr_err("Failed to register MTD device: %d\n", err);
		goto err_dev_reg;
	}

	return 0;

err_dev_reg:
	kfree(&b47s->mtd);
out:
	return err;
}

static int bcm47xxsflash_bcma_remove(struct platform_device *pdev)
{
	struct bcma_sflash *sflash = dev_get_platdata(&pdev->dev);
	struct bcm47xxsflash *b47s = sflash->priv;

	mtd_device_unregister(&b47s->mtd);
	kfree(b47s);

	return 0;
}

static struct platform_driver bcma_sflash_driver = {
	.probe	= bcm47xxsflash_bcma_probe,
	.remove = bcm47xxsflash_bcma_remove,
	.driver = {
		.name = "bcma_sflash",
		.owner = THIS_MODULE,
	},
};

/**************************************************
 * Init
 **************************************************/

static int __init bcm47xxsflash_init(void)
{
	int err;

	err = platform_driver_register(&bcma_sflash_driver);
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
