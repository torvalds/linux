/* SPDX-License-Identifier: GPL-2.0 */
SECTIONS {
	/* Group unwind sections into a single section: */
	.IA_64.unwind_info : { *(.IA_64.unwind_info*) }
	.IA_64.unwind : { *(.IA_64.unwind*) }
	/*
	 * Create place-holder sections to hold the PLTs, GOT, and
	 * official procedure-descriptors (.opd).
	 */
	.core.plt : { BYTE(0) }
	.init.plt : { BYTE(0) }
	.got : { BYTE(0) }
	.opd : { BYTE(0) }
}
