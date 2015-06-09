/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Atheros Communications, Inc.,  All Rights Reserved.
 * Copyright (C) 2006 FON Technology, SL.
 * Copyright (C) 2006 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2006-2009 Felix Fietkau <nbd@openwrt.org>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/irq_cpu.h>
#include <asm/reboot.h>
#include <asm/bootinfo.h>
#include <asm/time.h>

#include <ath25_platform.h>
#include "devices.h"
#include "ar5312.h"
#include "ar2315.h"

void (*ath25_irq_dispatch)(void);

static inline bool check_radio_magic(const void __iomem *addr)
{
	addr += 0x7a; /* offset for flash magic */
	return (__raw_readb(addr) == 0x5a) && (__raw_readb(addr + 1) == 0xa5);
}

static inline bool check_notempty(const void __iomem *addr)
{
	return __raw_readl(addr) != 0xffffffff;
}

static inline bool check_board_data(const void __iomem *addr, bool broken)
{
	/* config magic found */
	if (__raw_readl(addr) == ATH25_BD_MAGIC)
		return true;

	if (!broken)
		return false;

	/* broken board data detected, use radio data to find the
	 * offset, user will fix this */

	if (check_radio_magic(addr + 0x1000))
		return true;
	if (check_radio_magic(addr + 0xf8))
		return true;

	return false;
}

static const void __iomem * __init find_board_config(const void __iomem *limit,
						     const bool broken)
{
	const void __iomem *addr;
	const void __iomem *begin = limit - 0x1000;
	const void __iomem *end = limit - 0x30000;

	for (addr = begin; addr >= end; addr -= 0x1000)
		if (check_board_data(addr, broken))
			return addr;

	return NULL;
}

static const void __iomem * __init find_radio_config(const void __iomem *limit,
						     const void __iomem *bcfg)
{
	const void __iomem *rcfg, *begin, *end;

	/*
	 * Now find the start of Radio Configuration data, using heuristics:
	 * Search forward from Board Configuration data by 0x1000 bytes
	 * at a time until we find non-0xffffffff.
	 */
	begin = bcfg + 0x1000;
	end = limit;
	for (rcfg = begin; rcfg < end; rcfg += 0x1000)
		if (check_notempty(rcfg) && check_radio_magic(rcfg))
			return rcfg;

	/* AR2316 relocates radio config to new location */
	begin = bcfg + 0xf8;
	end = limit - 0x1000 + 0xf8;
	for (rcfg = begin; rcfg < end; rcfg += 0x1000)
		if (check_notempty(rcfg) && check_radio_magic(rcfg))
			return rcfg;

	return NULL;
}

/*
 * NB: Search region size could be larger than the actual flash size,
 * but this shouldn't be a problem here, because the flash
 * will simply be mapped multiple times.
 */
int __init ath25_find_config(phys_addr_t base, unsigned long size)
{
	const void __iomem *flash_base, *flash_limit;
	struct ath25_boarddata *config;
	unsigned int rcfg_size;
	int broken_boarddata = 0;
	const void __iomem *bcfg, *rcfg;
	u8 *board_data;
	u8 *radio_data;
	u8 *mac_addr;
	u32 offset;

	flash_base = ioremap_nocache(base, size);
	flash_limit = flash_base + size;

	ath25_board.config = NULL;
	ath25_board.radio = NULL;

	/* Copy the board and radio data to RAM, because accessing the mapped
	 * memory of the flash directly after booting is not safe */

	/* Try to find valid board and radio data */
	bcfg = find_board_config(flash_limit, false);

	/* If that fails, try to at least find valid radio data */
	if (!bcfg) {
		bcfg = find_board_config(flash_limit, true);
		broken_boarddata = 1;
	}

	if (!bcfg) {
		pr_warn("WARNING: No board configuration data found!\n");
		goto error;
	}

	board_data = kzalloc(BOARD_CONFIG_BUFSZ, GFP_KERNEL);
	ath25_board.config = (struct ath25_boarddata *)board_data;
	memcpy_fromio(board_data, bcfg, 0x100);
	if (broken_boarddata) {
		pr_warn("WARNING: broken board data detected\n");
		config = ath25_board.config;
		if (is_zero_ether_addr(config->enet0_mac)) {
			pr_info("Fixing up empty mac addresses\n");
			config->reset_config_gpio = 0xffff;
			config->sys_led_gpio = 0xffff;
			random_ether_addr(config->wlan0_mac);
			config->wlan0_mac[0] &= ~0x06;
			random_ether_addr(config->enet0_mac);
			random_ether_addr(config->enet1_mac);
		}
	}

	/* Radio config starts 0x100 bytes after board config, regardless
	 * of what the physical layout on the flash chip looks like */

	rcfg = find_radio_config(flash_limit, bcfg);
	if (!rcfg) {
		pr_warn("WARNING: Could not find Radio Configuration data\n");
		goto error;
	}

	radio_data = board_data + 0x100 + ((rcfg - bcfg) & 0xfff);
	ath25_board.radio = radio_data;
	offset = radio_data - board_data;
	pr_info("Radio config found at offset 0x%x (0x%x)\n", rcfg - bcfg,
		offset);
	rcfg_size = BOARD_CONFIG_BUFSZ - offset;
	memcpy_fromio(radio_data, rcfg, rcfg_size);

	mac_addr = &radio_data[0x1d * 2];
	if (is_broadcast_ether_addr(mac_addr)) {
		pr_info("Radio MAC is blank; using board-data\n");
		ether_addr_copy(mac_addr, ath25_board.config->wlan0_mac);
	}

	iounmap(flash_base);

	return 0;

error:
	iounmap(flash_base);
	return -ENODEV;
}

static void ath25_halt(void)
{
	local_irq_disable();
	unreachable();
}

void __init plat_mem_setup(void)
{
	_machine_halt = ath25_halt;
	pm_power_off = ath25_halt;

	if (is_ar5312())
		ar5312_plat_mem_setup();
	else
		ar2315_plat_mem_setup();

	/* Disable data watchpoints */
	write_c0_watchlo0(0);
}

asmlinkage void plat_irq_dispatch(void)
{
	ath25_irq_dispatch();
}

void __init plat_time_init(void)
{
	if (is_ar5312())
		ar5312_plat_time_init();
	else
		ar2315_plat_time_init();
}

unsigned int __cpuinit get_c0_compare_int(void)
{
	return CP0_LEGACY_COMPARE_IRQ;
}

void __init arch_init_irq(void)
{
	clear_c0_status(ST0_IM);
	mips_cpu_irq_init();

	/* Initialize interrupt controllers */
	if (is_ar5312())
		ar5312_arch_init_irq();
	else
		ar2315_arch_init_irq();
}
