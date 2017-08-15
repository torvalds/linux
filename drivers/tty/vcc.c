/* vcc.c: sun4v virtual channel concentrator
 *
 * Copyright (C) 2017 Oracle. All rights reserved.
 */

#include <linux/module.h>

#define DRV_MODULE_NAME		"vcc"
#define DRV_MODULE_VERSION	"1.1"
#define DRV_MODULE_RELDATE	"July 1, 2017"

MODULE_DESCRIPTION("Sun LDOM virtual console concentrator driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

int vcc_dbg;
int vcc_dbg_ldc;
int vcc_dbg_vio;

module_param(vcc_dbg, uint, 0664);
module_param(vcc_dbg_ldc, uint, 0664);
module_param(vcc_dbg_vio, uint, 0664);

#define VCC_DBG_DRV	0x1
#define VCC_DBG_LDC	0x2
#define VCC_DBG_PKT	0x4

#define vccdbg(f, a...)						\
	do {							\
		if (vcc_dbg & VCC_DBG_DRV)			\
			pr_info(f, ## a);			\
	} while (0)						\

#define vccdbgl(l)						\
	do {							\
		if (vcc_dbg & VCC_DBG_LDC)			\
			ldc_print(l);				\
	} while (0)						\

#define vccdbgp(pkt)						\
	do {							\
		if (vcc_dbg & VCC_DBG_PKT) {			\
			int i;					\
			for (i = 0; i < pkt.tag.stype; i++)	\
				pr_info("[%c]", pkt.data[i]);	\
		}						\
	} while (0)						\

static int __init vcc_init(void)
{
	return 0;
}

static void __exit vcc_exit(void)
{
}

module_init(vcc_init);
module_exit(vcc_exit);
