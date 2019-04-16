/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/of.h>
#include <linux/mtd/map.h>

#ifdef CONFIG_MTD_PHYSMAP_GEMINI
int of_flash_probe_gemini(struct platform_device *pdev,
			  struct device_node *np,
			  struct map_info *map);
#else
static inline
int of_flash_probe_gemini(struct platform_device *pdev,
			  struct device_node *np,
			  struct map_info *map)
{
	return 0;
}
#endif
