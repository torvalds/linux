/*
 * Wrapper script for the realmode binary as a transport object
 * before copying to low memory.
 */
#include <asm/page_types.h>

	.section ".x86_trampoline","a"
	.balign PAGE_SIZE
	.globl	acpi_wakeup_code
acpi_wakeup_code:
	.incbin	"arch/x86/kernel/acpi/realmode/wakeup.bin"
	.size	acpi_wakeup_code, .-acpi_wakeup_code
