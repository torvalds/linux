/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020-2022 Loongson Technology Corporation Limited */
SECTIONS {
	. = ALIGN(4);
	.got 0 : { BYTE(0) }
	.plt 0 : { BYTE(0) }
	.plt.idx 0 : { BYTE(0) }
	.ftrace_trampoline 0 : { BYTE(0) }
}
