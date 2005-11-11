/*
 * Flash memory support for various TI OMAP boards
 *
 * Copyright (C) 2001-2002 MontaVista Software Inc.
 * Copyright (C) 2003-2004 Texas Instruments
 * Copyright (C) 2004 Nokia Corporation
 *
 *	Assembled using driver code copyright the companies above
 *	and written by David Brownell, Jian Zhang <jzhang@ti.com>,
 * 	Tony Lindgren <tony@atomide.com> and others.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/mach/flash.h>
#include <asm/arch/tc.h>

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { /* "RedBoot", */ "cmdlinepart", NULL };
#endif

struct omapflash_info {
	struct mtd_partition	*parts;
	struct mtd_info		*mtd;
	struct map_info		map;
};

static void omap_set_vpp(struct map_info *map, int enable)
{
	static int	count;

	if (enable) {
		if (count++ == 0)
			OMAP_EMIFS_CONFIG_REG |= OMAP_EMIFS_CONFIG_WP;
	} else {
		if (count && (--count == 0))
			OMAP_EMIFS_CONFIG_REG &= ~OMAP_EMIFS_CONFIG_WP;
	}
}

static int __devinit omapflash_probe(struct device *dev)
{
	int err;
	struct omapflash_info *info;
	struct platform_device *pdev = to_platform_device(dev);
	struct flash_platform_data *pdata = pdev->dev.platform_data;
	struct resource *res = pdev->resource;
	unsigned long size = res->end - res->start + 1;

	info = kmalloc(sizeof(struct omapflash_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	memset(info, 0, sizeof(struct omapflash_info));

	if (!request_mem_region(res->start, size, "flash")) {
		err = -EBUSY;
		goto out_free_info;
	}

	info->map.virt		= ioremap(res->start, size);
	if (!info->map.virt) {
		err = -ENOMEM;
		goto out_release_mem_region;
	}
	info->map.name		= pdev->dev.bus_id;
	info->map.phys		= res->start;
	info->map.size		= size;
	info->map.bankwidth	= pdata->width;
	info->map.set_vpp	= omap_set_vpp;

	simple_map_init(&info->map);
	info->mtd = do_map_probe(pdata->map_name, &info->map);
	if (!info->mtd) {
		err = -EIO;
		goto out_iounmap;
	}
	info->mtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
	err = parse_mtd_partitions(info->mtd, part_probes, &info->parts, 0);
	if (err > 0)
		add_mtd_partitions(info->mtd, info->parts, err);
	else if (err < 0 && pdata->parts)
		add_mtd_partitions(info->mtd, pdata->parts, pdata->nr_parts);
	else
#endif
		add_mtd_device(info->mtd);

	dev_set_drvdata(&pdev->dev, info);

	return 0;

out_iounmap:
	iounmap(info->map.virt);
out_release_mem_region:
	release_mem_region(res->start, size);
out_free_info:
	kfree(info);

	return err;
}

static int __devexit omapflash_remove(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omapflash_info *info = dev_get_drvdata(&pdev->dev);

	dev_set_drvdata(&pdev->dev, NULL);

	if (info) {
		if (info->parts) {
			del_mtd_partitions(info->mtd);
			kfree(info->parts);
		} else
			del_mtd_device(info->mtd);
		map_destroy(info->mtd);
		release_mem_region(info->map.phys, info->map.size);
		iounmap((void __iomem *) info->map.virt);
		kfree(info);
	}

	return 0;
}

static struct device_driver omapflash_driver = {
	.name	= "omapflash",
	.bus	= &platform_bus_type,
	.probe	= omapflash_probe,
	.remove	= __devexit_p(omapflash_remove),
};

static int __init omapflash_init(void)
{
	return driver_register(&omapflash_driver);
}

static void __exit omapflash_exit(void)
{
	driver_unregister(&omapflash_driver);
}

module_init(omapflash_init);
module_exit(omapflash_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD NOR map driver for TI OMAP boards");

