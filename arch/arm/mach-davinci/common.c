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

#include <asm/tlb.h>
#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/cputype.h>

#include "clock.h"

struct davinci_soc_info davinci_soc_info;
EXPORT_SYMBOL(davinci_soc_info);

void __iomem *davinci_intc_base;
int davinci_intc_type;

void davinci_get_mac_addr(struct memory_accessor *mem_acc, void *context)
{
	char *mac_addr = davinci_soc_info.emac_pdata->mac_addr;
	off_t offset = (off_t)context;

	/* Read MAC addr from EEPROM */
	if (mem_acc->read(mem_acc, mac_addr, offset, ETH_ALEN) == ETH_ALEN)
		pr_info("Read MAC addr from EEPROM: %pM\n", mac_addr);
}

static struct davinci_id * __init davinci_get_id(u32 jtag_id)
{
	int i;
	struct davinci_id *dip;
	u8 variant = (jtag_id & 0xf0000000) >> 28;
	u16 part_no = (jtag_id & 0x0ffff000) >> 12;

	for (i = 0, dip = davinci_soc_info.ids; i < davinci_soc_info.ids_num;
			i++, dip++)
		/* Don't care about the manufacturer right now */
		if ((dip->part_no == part_no) && (dip->variant == variant))
			return dip;

	return NULL;
}

void __init davinci_common_init(struct davinci_soc_info *soc_info)
{
	int ret;
	struct davinci_id *dip;

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
	davinci_soc_info.jtag_id = __raw_readl(davinci_soc_info.jtag_id_base);

	dip = davinci_get_id(davinci_soc_info.jtag_id);
	if (!dip) {
		ret = -EINVAL;
		pr_err("Unknown DaVinci JTAG ID 0x%x\n",
						davinci_soc_info.jtag_id);
		goto err;
	}

	davinci_soc_info.cpu_id = dip->cpu_id;
	pr_info("DaVinci %s variant 0x%x\n", dip->name, dip->variant);

	if (davinci_soc_info.cpu_clks) {
		ret = davinci_clk_init(davinci_soc_info.cpu_clks);

		if (ret != 0)
			goto err;
	}

	davinci_intc_base = davinci_soc_info.intc_base;
	davinci_intc_type = davinci_soc_info.intc_type;
	return;

err:
	panic("davinci_common_init: SoC Initialization failed\n");
}
