/*
 * Copyright 2014 Andy Lutomirski
 * Subject to the GNU Public License, v.2
 *
 * String table for loadable section headers.  See vdso2c.h for why
 * this exists.
 */

const char fake_shstrtab[] __attribute__((section(".fake_shstrtab"))) =
	".hash\0"
	".dynsym\0"
	".dynstr\0"
	".gnu.version\0"
	".gnu.version_d\0"
	".dynamic\0"
	".rodata\0"
	".fake_shstrtab\0"  /* Yay, self-referential code. */
	".note\0"
	".eh_frame_hdr\0"
	".eh_frame\0"
	".text";
