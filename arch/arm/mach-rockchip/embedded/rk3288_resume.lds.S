MEMORY {
	pmu_sram_code  : ORIGIN = 0xff720000, LENGTH = 0xf00
	pmu_sram_stack : ORIGIN = 0xff720f00, LENGTH = 0x100
}

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)

SECTIONS
{
	/* Don't need unwind tables */
	/DISCARD/ : {
		*(.ARM.exidx*)
		*(.ARM.extab*)
	}

	/* Kernel code finds params because it knows they are first */
	.params : { *(.resume_params*) } > pmu_sram_code
	. = ALIGN(4);

	.text : { *(.text*) } > pmu_sram_code
	. = ALIGN(4);

	.rodata : { *(SORT_BY_ALIGNMENT(.rodata*)) } > pmu_sram_code
	. = ALIGN(4);

	.data : {
		*(SORT_BY_ALIGNMENT(.data*))
		. = ALIGN(4);

		/* We purposely put bss as part of data to avoid initting */
		*(SORT_BY_ALIGNMENT(.bss*))
		. = ALIGN(4);
	} > pmu_sram_code

	.stack : {
		. += LENGTH(pmu_sram_stack) - 8;
		__stack_start = .;
	} > pmu_sram_stack
}
