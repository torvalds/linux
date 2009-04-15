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

#include <asm/tlb.h>
#include <asm/mach/map.h>

#include <mach/common.h>

struct davinci_soc_info davinci_soc_info;
EXPORT_SYMBOL(davinci_soc_info);

void __init davinci_common_init(struct davinci_soc_info *soc_info)
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
	davinci_check_revision();

	return;

err:
	pr_err("davinci_common_init: SoC Initialization failed\n");
}
