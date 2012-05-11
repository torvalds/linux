/*
 * arch/sparc/kernel/ksyms.c: Sparc specific ksyms support.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 */

#include <linux/module.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/head.h>
#include <asm/dma.h>

struct poll {
	int fd;
	short events;
	short revents;
};

/* from entry.S */
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);

/* from head_32.S */
EXPORT_SYMBOL(__ret_efault);
EXPORT_SYMBOL(empty_zero_page);

/* Defined using magic */
#ifndef CONFIG_SMP
EXPORT_SYMBOL(BTFIXUP_CALL(___xchg32));
#else
EXPORT_SYMBOL(BTFIXUP_CALL(__hard_smp_processor_id));
#endif
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_unlockarea));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_lockarea));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_get_scsi_sgl));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_get_scsi_one));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_release_scsi_sgl));
EXPORT_SYMBOL(BTFIXUP_CALL(mmu_release_scsi_one));

/* Exporting a symbol from /init/main.c */
EXPORT_SYMBOL(saved_command_line);
