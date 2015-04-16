/*
 *  linux/arch/xtensa/boot/boot-elf/boot.lds.S
 *
 *  Copyright (C) 2008 - 2013 by Tensilica Inc.
 *
 *  Chris Zankel <chris@zankel.net>
 *  Marc Gauthier <marc@tensilica.com
 *  Pete Delaney <piet@tensilica.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/vectors.h>
OUTPUT_ARCH(xtensa)
ENTRY(_ResetVector)

SECTIONS
{
	.ResetVector.text XCHAL_RESET_VECTOR_VADDR :
	{
		*(.ResetVector.text)
	}

	.image KERNELOFFSET: AT (LOAD_MEMORY_ADDRESS)
	{
		_image_start = .;
		*(image)
		. = (. + 3) & ~ 3;
		_image_end = .	;
	}

	.bss ((LOADADDR(.image) + SIZEOF(.image) + 3) & ~ 3):
	{
		__bss_start = .;
		*(.sbss)
		*(.scommon)
		*(.dynbss)
		*(.bss)
		__bss_end = .;
	}

#ifdef CONFIG_MMU
	/*
	 * This is a remapped copy of the Reset Vector Code.
	 * It keeps gdb in sync with the PC after switching
	 * to the temporary mapping used while setting up
	 * the V2 MMU mappings for Linux.
	 */
	.ResetVector.remapped_text 0x46000000 (INFO):
	{
		*(.ResetVector.remapped_text)
	}
#endif
}
