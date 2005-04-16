/* Linker script for the Midas labs Anna V850E2 evaluation board
   (CONFIG_V850E2_ANNA), with kernel in ROM (CONFIG_ROM_KERNEL).  */

MEMORY {
	/* 8MB of flash ROM.  */
	ROM   : ORIGIN = 0,          LENGTH = 0x00800000

	/* 1MB of static RAM.  This memory is mirrored 64 times.  */
	SRAM  : ORIGIN = SRAM_ADDR,  LENGTH = SRAM_SIZE
	/* 64MB of DRAM.  */
	SDRAM : ORIGIN = SDRAM_ADDR, LENGTH = SDRAM_SIZE
}

SECTIONS {
	ROMK_SECTIONS(ROM, SRAM)
}
