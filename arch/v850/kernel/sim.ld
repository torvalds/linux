/* Linker script for the gdb v850e simulator (CONFIG_V850E_SIM).  */

MEMORY {
	/* Interrupt vectors.  */
	INTV  : ORIGIN = 0x0, LENGTH = 0xe0
	/* Main RAM.  */
	RAM   : ORIGIN = RAM_ADDR, LENGTH = RAM_SIZE
}

SECTIONS {
	.intv : { INTV_CONTENTS } > INTV
	.ram : { RAMK_KRAM_CONTENTS } > RAM
}
