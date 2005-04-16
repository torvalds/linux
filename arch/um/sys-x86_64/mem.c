/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include "linux/mm.h"
#include "asm/page.h"
#include "asm/mman.h"

unsigned long vm_stack_flags = __VM_STACK_FLAGS;
unsigned long vm_stack_flags32 = __VM_STACK_FLAGS;
unsigned long vm_data_default_flags = __VM_DATA_DEFAULT_FLAGS;
unsigned long vm_data_default_flags32 = __VM_DATA_DEFAULT_FLAGS;
unsigned long vm_force_exec32 = PROT_EXEC;

/* Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
