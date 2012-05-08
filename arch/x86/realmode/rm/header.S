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
		/* SMP trampoline */
		.long	pa_trampoline_data
		.long	pa_trampoline_status
#ifdef CONFIG_X86_32
		.long	pa_startup_32_smp
		.long	pa_boot_gdt
#else
		.long	pa_startup_64_smp
		.long	pa_level3_ident_pgt
		.long	pa_level3_kernel_pgt
#endif
		/* ACPI sleep */
#ifdef CONFIG_ACPI_SLEEP
		.long	pa_wakeup_start
		.long	pa_wakeup_header
#endif
END(real_mode_header)
