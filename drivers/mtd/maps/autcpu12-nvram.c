/*
 * NV-RAM memory access on autcpu12
 * (C) 2002 Thomas Gleixner (gleixner@autronix.de)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/sizes.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>

struct autcpu12_nvram_priv {
	struct mtd_info *mtd;
	struct map_info map;
};

static int __devinit autcpu12_nvram_probe(struct platform_device *pdev)
{
	map_word tmp, save0, save1;
	struct resource *res;
	struct autcpu12_nvram_priv *priv;

	priv = devm_kzalloc(&pdev->dev,
			    sizeof(struct autcpu12_nvram_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		return -ENOENT;
	}

	priv->map.bankwidth	= 4;
	priv->map.phys		= res->start;
	priv->map.size		= resource_size(res);
	priv->map.virt		= devm_request_and_ioremap(&pdev->dev, res);
	strcpy((char *)priv->map.name, res->name);
	if (!priv->map.virt) {
		dev_err(&pdev->dev, "failed to remap mem resource\n");
		return -EBUSY;
	}

	simple_map_init(&priv->map);

	/*
	 * Check for 32K/128K
	 * read ofs 0
	 * read ofs 0x10000
	 * Write complement to ofs 0x100000
	 * Read	and check result on ofs 0x0
	 * Restore contents
	 */
	save0 = map_read(&priv->map, 0);
	save1 = map_read(&priv->map, 0x10000);
	tmp.x[0] = ~save0.x[0];
	map_write(&priv->map, tmp, 0x10000);
	tmp = map_read(&priv->map, 0);
	/* if we find this pattern on 0x0, we have 32K size */
	if (!map_word_equal(&priv->map, tmp, save0)) {
		map_write(&priv->map, save0, 0x0);
		priv->map.size = SZ_32K;
	} else
		map_write(&priv->map, save1, 0x10000);

	priv->mtd = do_map_probe("map_ram", &priv->map);
	if (!priv->mtd) {
		dev_err(&pdev->dev, "probing failed\n");
		return -ENXIO;
	}

	priv->mtd->owner	= THIS_MODULE;
	priv->mtd->erasesize	= 16;
	priv->mtd->dev.parent	= &pdev->dev;
	if (!mtd_device_register(priv->mtd, NULL, 0)) {
		dev_info(&pdev->dev,
			 "NV-RAM device size %ldKiB registered on AUTCPU12\n",
			 priv->map.size / SZ_1K);
		return 0;
	}

	map_destroy(priv->mtd);
	dev_err(&pdev->dev, "NV-RAM device addition failed\n");
	return -ENOMEM;
}

static int __devexit autcpu12_nvram_remove(struct platform_device *pdev)
{
	struct autcpu12_nvram_priv *priv = platform_get_drvdata(pdev);

	mtd_device_unregister(priv->mtd);
	map_destroy(priv->mtd);

	return 0;
}

static struct platform_driver autcpu12_nvram_driver = {
	.driver		= {
		.name	= "autcpu12_nvram",
		.owner	= THIS_MODULE,
	},
	.probe		= autcpu12_nvram_probe,
	.remove		= autcpu12_nvram_remove,
};
module_platform_driver(autcpu12_nvram_driver);

MODULE_AUTHOR("Thomas Gleixner");
MODULE_DESCRIPTION("autcpu12 NVRAM map driver");
MODULE_LICENSE("GPL");
