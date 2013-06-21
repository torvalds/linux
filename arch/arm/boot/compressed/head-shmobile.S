/*
 * The head-file for SH-Mobile ARM platforms
 *
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 * Simon Horman <horms@verge.net.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef CONFIG_ZBOOT_ROM

	.section	".start", "ax"

	/* load board-specific initialization code */
#include <mach/zboot.h>

#if defined(CONFIG_ZBOOT_ROM_MMCIF) || defined(CONFIG_ZBOOT_ROM_SH_MOBILE_SDHI)
	/* Load image from MMC/SD */
	adr	sp, __tmp_stack + 256
	ldr	r0, __image_start
	ldr	r1, __image_end
	subs	r1, r1, r0
	ldr	r0, __load_base
	bl	mmc_loader

	/* Jump to loaded code */
	ldr	r0, __loaded
	ldr	r1, __image_start
	sub	r0, r0, r1
	ldr	r1, __load_base
	add	pc, r0, r1

__image_start:
	.long	_start
__image_end:
	.long	_got_end
__load_base:
	.long	MEMORY_START + 0x02000000 @ Load at 32Mb into SDRAM
__loaded:
	.long	__continue
	.align
__tmp_stack:
	.space	256
__continue:
#endif /* CONFIG_ZBOOT_ROM_MMC || CONFIG_ZBOOT_ROM_SH_MOBILE_SDHI */

	/* Set board ID necessary for boot */
	ldr	r7, 1f				@ Set machine type register
	mov	r8, #0				@ pass null pointer as atag
	b	2f

1 :	.long MACH_TYPE
2 :

#endif /* CONFIG_ZBOOT_ROM */
