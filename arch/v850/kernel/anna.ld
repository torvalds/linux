/* Linker script for the Midas labs Anna V850E2 evaluation board
   (CONFIG_V850E2_ANNA).  */

MEMORY {
	/* 256KB of internal memory (followed by one mirror).  */
	iMEM0 : ORIGIN = 0,	     LENGTH = 0x00040000
	/* 256KB of internal memory (followed by one mirror).  */
	iMEM1 : ORIGIN = 0x00040000, LENGTH = 0x00040000

	/* 1MB of static RAM.  This memory is mirrored 64 times.  */
	SRAM  : ORIGIN = SRAM_ADDR,  LENGTH = SRAM_SIZE
	/* 64MB of DRAM.  */
	SDRAM : ORIGIN = SDRAM_ADDR, LENGTH = SDRAM_SIZE
}

SECTIONS {
	.intv : { INTV_CONTENTS } > iMEM0
	.sram : { RAMK_KRAM_CONTENTS } > SRAM
	.root : { ROOT_FS_CONTENTS } > SDRAM
}
