/*
 *  linux/arch/arm/kernel/arch.c
 *
 *  Architecture specific fixups.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>

#include <asm/elf.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/mach/arch.h>

unsigned int vram_size;

#ifdef CONFIG_ARCH_ACORN

unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;

static int __init parse_tag_acorn(const struct tag *tag)
{
	memc_ctrl_reg = tag->u.acorn.memc_control_reg;
	number_mfm_drives = tag->u.acorn.adfsdrives;

	switch (tag->u.acorn.vram_pages) {
	case 512:
		vram_size += PAGE_SIZE * 256;
	case 256:
		vram_size += PAGE_SIZE * 256;
	default:
		break;
	}
#if 0
	if (vram_size) {
		desc->video_start = 0x02000000;
		desc->video_end   = 0x02000000 + vram_size;
	}
#endif
	return 0;
}

__tagtable(ATAG_ACORN, parse_tag_acorn);

#endif
