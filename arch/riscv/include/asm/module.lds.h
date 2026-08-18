/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */
#ifdef CONFIG_MODULE_SECTIONS
SECTIONS {
	.plt 0 : { BYTE(0) }
	.got 0 : { BYTE(0) }
	.got.plt 0 : { BYTE(0) }
}
#endif
