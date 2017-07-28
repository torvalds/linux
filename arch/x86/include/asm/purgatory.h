#ifndef _ASM_X86_PURGATORY_H
#define _ASM_X86_PURGATORY_H

#ifndef __ASSEMBLY__
#include <linux/purgatory.h>

extern void purgatory(void);
/*
 * These forward declarations serve two purposes:
 *
 * 1) Make sparse happy when checking arch/purgatory
 * 2) Document that these are required to be global so the symbol
 *    lookup in kexec works
 */
extern unsigned long purgatory_backup_dest;
extern unsigned long purgatory_backup_src;
extern unsigned long purgatory_backup_sz;
#endif	/* __ASSEMBLY__ */

#endif /* _ASM_PURGATORY_H */
