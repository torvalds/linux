// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read-only MTD access to the system firmware flash behind the ACPI
 * INT0800 "Intel 82802 firmware hub" device.
 *
 * On x86 systems the boot flash is decoded into the physical address
 * space below 4 GB, so a plain ioremap() is sufficient to read it -
 * no SPI or LPC controller access is required.  The declared _CRS
 * window may be larger than the real flash (the whole top-16MiB
 * decode range is commonly claimed); undecoded holes read as 0xff.
 *
 * The device is exposed read-only via the ROM chip driver; there is
 * deliberately no write or erase support.
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* top-of-4GB firmware decode, used when _CRS reports no window */
#define INT0800_DEFAULT_PHYS	0xffe00000UL
#define INT0800_DEFAULT_SIZE	SZ_2M

struct int0800 {
	struct map_info		map;
	struct mtd_info		*mtd;
};

static int int0800_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct int0800 *fw;
	resource_size_t end;
	int ret;

	fw = devm_kzalloc(&pdev->dev, sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return -ENOMEM;

	fw->map.name = dev_name(&pdev->dev);
	fw->map.bankwidth = 1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		fw->map.phys = res->start;
		fw->map.size = resource_size(res);
	} else {
		fw->map.phys = INT0800_DEFAULT_PHYS;
		fw->map.size = INT0800_DEFAULT_SIZE;
	}

	/*
	 * Plain ioremap on purpose: the window is already claimed by the
	 * ACPI/pnp resource reservation, so devm_ioremap_resource() would
	 * fail with -EBUSY.
	 */
	fw->map.virt = devm_ioremap(&pdev->dev, fw->map.phys, fw->map.size);
	if (!fw->map.virt)
		return -ENOMEM;

	simple_map_init(&fw->map);
	fw->mtd = do_map_probe("map_rom", &fw->map);
	if (!fw->mtd)
		return -ENODEV;
	fw->mtd->dev.parent = &pdev->dev;
	platform_set_drvdata(pdev, fw);

	end = fw->map.phys + fw->map.size - 1;
	dev_info(&pdev->dev, "mapped firmware window %pa-%pa\n",
		 &fw->map.phys, &end);

	ret = mtd_device_register(fw->mtd, NULL, 0);
	if (ret)
		map_destroy(fw->mtd);
	return ret;
}

static void int0800_remove(struct platform_device *pdev)
{
	struct int0800 *fw = platform_get_drvdata(pdev);

	mtd_device_unregister(fw->mtd);
	map_destroy(fw->mtd);
}

static const struct acpi_device_id int0800_ids[] = {
	{ "INT0800", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, int0800_ids);

static struct platform_driver int0800_driver = {
	.probe	= int0800_probe,
	.remove	= int0800_remove,
	.driver	= {
		.name			= "int0800",
		.acpi_match_table	= int0800_ids,
		/*
		 * remove() frees the mtd_info and unmaps the window even
		 * while /dev/mtdX fds are still open; the only safe
		 * teardown path is module unload, which open fds block
		 * via mtd->owner.
		 */
		.suppress_bind_attrs	= true,
	},
};
module_platform_driver(int0800_driver);

MODULE_AUTHOR("Stephen Bancroft <stevereaver@gmail.com>");
MODULE_DESCRIPTION("Read-only MTD map over the INT0800 firmware flash window");
MODULE_LICENSE("GPL");
