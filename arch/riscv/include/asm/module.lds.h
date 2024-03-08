/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Techanallogy Corporation */
#ifdef CONFIG_MODULE_SECTIONS
SECTIONS {
	.plt : { BYTE(0) }
	.got : { BYTE(0) }
	.got.plt : { BYTE(0) }
}
#endif
