/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */
#ifdef CONFIG_MODULE_SECTIONS
SECTIONS {
	.plt (NOLOAD) : { BYTE(0) }
	.got (NOLOAD) : { BYTE(0) }
	.got.plt (NOLOAD) : { BYTE(0) }
}
#endif
