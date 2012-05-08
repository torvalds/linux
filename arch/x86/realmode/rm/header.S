/*
 * Real-mode blob header; this should match realmode.h and be
 * readonly; for mutable data instead add pointers into the .data
 * or .bss sections as appropriate.
 */

#include <linux/linkage.h>
#include <asm/page_types.h>

	.section ".header", "a"

GLOBAL(real_mode_header)
	.long	pa_text_start
	.long	pa_ro_end
	/* SMP trampoline */
	.long	pa_trampoline_start
	.long	pa_trampoline_status
	.long	pa_trampoline_header
#ifdef CONFIG_X86_64
	.long	pa_trampoline_pgd;
#endif
	/* ACPI S3 wakeup */
#ifdef CONFIG_ACPI_SLEEP
	.long	pa_wakeup_start
	.long	pa_wakeup_header
#endif
	/* APM/BIOS reboot */
#ifdef CONFIG_X86_32
	.long	pa_machine_real_restart_asm
#endif
END(real_mode_header)
