/*
 * arch/arm/plat-orion/addr-map.c
 *
 * Address map functions for Marvell Orion based SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include <plat/addr-map.h>

struct mbus_dram_target_info orion_mbus_dram_info;

const struct mbus_dram_target_info *mv_mbus_dram_info(void)
{
	return &orion_mbus_dram_info;
}
EXPORT_SYMBOL_GPL(mv_mbus_dram_info);

/*
 * DDR target is the same on all Orion platforms.
 */
#define TARGET_DDR		0

/*
 * Helpers to get DDR bank info
 */
#define DDR_BASE_CS_OFF(n)	(0x0000 + ((n) << 3))
#define DDR_SIZE_CS_OFF(n)	(0x0004 + ((n) << 3))

/*
 * CPU Address Decode Windows registers
 */
#define WIN_CTRL_OFF		0x0000
#define WIN_BASE_OFF		0x0004
#define WIN_REMAP_LO_OFF	0x0008
#define WIN_REMAP_HI_OFF	0x000c

#define ATTR_HW_COHERENCY	(0x1 << 4)

/*
 * Default implementation
 */
static void __init __iomem *
orion_win_cfg_base(const struct orion_addr_map_cfg *cfg, int win)
{
	return cfg->bridge_virt_base + (win << 4);
}

/*
 * Default implementation
 */
static int __init orion_cpu_win_can_remap(const struct orion_addr_map_cfg *cfg,
					  const int win)
{
	if (win < cfg->remappable_wins)
		return 1;

	return 0;
}

void __init orion_setup_cpu_win(const struct orion_addr_map_cfg *cfg,
				const int win, const u32 base,
				const u32 size, const u8 target,
				const u8 attr, const int remap)
{
	void __iomem *addr = cfg->win_cfg_base(cfg, win);
	u32 ctrl, base_high, remap_addr;

	if (win >= cfg->num_wins) {
		printk(KERN_ERR "setup_cpu_win: trying to allocate window "
		       "%d when only %d allowed\n", win, cfg->num_wins);
	}

	base_high = base & 0xffff0000;
	ctrl = ((size - 1) & 0xffff0000) | (attr << 8) | (target << 4) | 1;

	writel(base_high, addr + WIN_BASE_OFF);
	writel(ctrl, addr + WIN_CTRL_OFF);
	if (cfg->cpu_win_can_remap(cfg, win)) {
		if (remap < 0)
			remap_addr = base;
		else
			remap_addr = remap;
		writel(remap_addr & 0xffff0000, addr + WIN_REMAP_LO_OFF);
		writel(0, addr + WIN_REMAP_HI_OFF);
	}
}

/*
 * Configure a number of windows.
 */
static void __init orion_setup_cpu_wins(const struct orion_addr_map_cfg * cfg,
					const struct orion_addr_map_info *info)
{
	while (info->win != -1) {
		orion_setup_cpu_win(cfg, info->win, info->base, info->size,
				    info->target, info->attr, info->remap);
		info++;
	}
}

static void __init orion_disable_wins(const struct orion_addr_map_cfg * cfg)
{
	void __iomem *addr;
	int i;

	for (i = 0; i < cfg->num_wins; i++) {
		addr = cfg->win_cfg_base(cfg, i);

		writel(0, addr + WIN_BASE_OFF);
		writel(0, addr + WIN_CTRL_OFF);
		if (cfg->cpu_win_can_remap(cfg, i)) {
			writel(0, addr + WIN_REMAP_LO_OFF);
			writel(0, addr + WIN_REMAP_HI_OFF);
		}
	}
}

/*
 * Disable, clear and configure windows.
 */
void __init orion_config_wins(struct orion_addr_map_cfg * cfg,
			      const struct orion_addr_map_info *info)
{
	if (!cfg->cpu_win_can_remap)
		cfg->cpu_win_can_remap = orion_cpu_win_can_remap;

	if (!cfg->win_cfg_base)
		cfg->win_cfg_base = orion_win_cfg_base;

	orion_disable_wins(cfg);

	if (info)
		orion_setup_cpu_wins(cfg, info);
}

/*
 * Setup MBUS dram target info.
 */
void __init orion_setup_cpu_mbus_target(const struct orion_addr_map_cfg *cfg,
					const void __iomem *ddr_window_cpu_base)
{
	int i;
	int cs;

	orion_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	for (i = 0, cs = 0; i < 4; i++) {
		u32 base = readl(ddr_window_cpu_base + DDR_BASE_CS_OFF(i));
		u32 size = readl(ddr_window_cpu_base + DDR_SIZE_CS_OFF(i));

		/*
		 * Chip select enabled?
		 */
		if (size & 1) {
			struct mbus_dram_window *w;

			w = &orion_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0xf & ~(1 << i);
			if (cfg->hw_io_coherency)
				w->mbus_attr |= ATTR_HW_COHERENCY;
			w->base = base & 0xffff0000;
			w->size = (size | 0x0000ffff) + 1;
		}
	}
	orion_mbus_dram_info.num_cs = cs;
}
