#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/traps.h>

/* Says whether we're using A/UX interrupts or not */
extern int via_alt_mapping;

EXPORT_SYMBOL(via_alt_mapping);
