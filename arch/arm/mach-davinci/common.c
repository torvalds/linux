/*
 * Code commons to all DaVinci SoCs.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/etherdevice.h>
#include <linux/davinci_emac.h>
#include <linux/dma-mapping.h>

#include <asm/tlb.h>
#include <asm/mach/map.h>

#include "common.h"
#include "cputype.h"

struct davinci_soc_info davinci_soc_info;
EXPORT_SYMBOL(davinci_soc_info);

static int __init davinci_init_id(struct davinci_soc_info *soc_info)
{
	int			i;
	struct davinci_id	*dip;
	u8			variant;
	u16			part_no;
	void __iomem		*base;

	base = ioremap(soc_info->jtag_id_reg, SZ_4K);
	if (!base) {
		pr_err("Unable to map JTAG ID register\n");
		return -ENOMEM;
	}

	soc_info->jtag_id = __raw_readl(base);
	iounmap(base);

	variant = (soc_info->jtag_id & 0xf0000000) >> 28;
	part_no = (soc_info->jtag_id & 0x0ffff000) >> 12;

	for (i = 0, dip = soc_info->ids; i < soc_info->ids_num;
			i++, dip++)
		/* Don't care about the manufacturer right now */
		if ((dip->part_no == part_no) && (dip->variant == variant)) {
			soc_info->cpu_id = dip->cpu_id;
			pr_info("DaVinci %s variant 0x%x\n", dip->name,
					dip->variant);
			return 0;
		}

	pr_err("Unknown DaVinci JTAG ID 0x%x\n", soc_info->jtag_id);
	return -EINVAL;
}

void __init davinci_common_init(const struct davinci_soc_info *soc_info)
{
	int ret;

	if (!soc_info) {
		ret = -EINVAL;
		goto err;
	}

	memcpy(&davinci_soc_info, soc_info, sizeof(struct davinci_soc_info));

	if (davinci_soc_info.io_desc && (davinci_soc_info.io_desc_num > 0))
		iotable_init(davinci_soc_info.io_desc,
				davinci_soc_info.io_desc_num);

	/*
	 * Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but we must also do it here because of the CPU
	 * revision check below.
	 */
	local_flush_tlb_all();
	flush_cache_all();

	/*
	 * We want to check CPU revision early for cpu_is_xxxx() macros.
	 * IO space mapping must be initialized before we can do that.
	 */
	ret = davinci_init_id(&davinci_soc_info);
	if (ret < 0)
		goto err;


	return;

err:
	panic("davinci_common_init: SoC Initialization failed\n");
}

void __init davinci_init_late(void)
{
	davinci_cpufreq_init();
}
