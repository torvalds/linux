/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_ARM_MODULE_PLTS
SECTIONS {
	.plt : { BYTE(0) }
	.init.plt : { BYTE(0) }
}
#endif
