/* arch/sparc64/kernel/sparc64_ksyms.c: Sparc64 specific ksyms support.
 *
 * Copyright (C) 1996, 2007 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#include <linux/export.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/cpudata.h>
#include <asm/uaccess.h>
#include <asm/spitfire.h>
#include <asm/oplib.h>
#include <asm/hypervisor.h>
#include <asm/cacheflush.h>

struct poll {
	int fd;
	short events;
	short revents;
};

/* from helpers.S */
EXPORT_SYMBOL(__flushw_user);
EXPORT_SYMBOL_GPL(real_hard_smp_processor_id);

/* from head_64.S */
EXPORT_SYMBOL(__ret_efault);
EXPORT_SYMBOL(tlb_type);
EXPORT_SYMBOL(sun4v_chip_type);
EXPORT_SYMBOL(prom_root_node);

/* from hvcalls.S */
EXPORT_SYMBOL(sun4v_niagara_getperf);
EXPORT_SYMBOL(sun4v_niagara_setperf);
EXPORT_SYMBOL(sun4v_niagara2_getperf);
EXPORT_SYMBOL(sun4v_niagara2_setperf);

/* from hweight.S */
EXPORT_SYMBOL(__arch_hweight8);
EXPORT_SYMBOL(__arch_hweight16);
EXPORT_SYMBOL(__arch_hweight32);
EXPORT_SYMBOL(__arch_hweight64);

/* from ffs_ffz.S */
EXPORT_SYMBOL(ffs);
EXPORT_SYMBOL(__ffs);

/* Exporting a symbol from /init/main.c */
EXPORT_SYMBOL(saved_command_line);
