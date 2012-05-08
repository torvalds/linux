/*
 * Real-mode blob header; this should match realmode.h and be
 * readonly; for mutable data instead add pointers into the .data
 * or .bss sections as appropriate.
 */

#include <linux/linkage.h>
#include <asm/page_types.h>

		.section ".header", "a"

ENTRY(real_mode_header)
		.long	pa_text_start
		.long	pa_ro_end
		.long	pa_end
#ifdef CONFIG_X86_32
		.long	pa_machine_real_restart_asm
#endif
END(real_mode_header)
