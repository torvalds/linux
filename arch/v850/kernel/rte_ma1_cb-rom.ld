/* Linker script for the Midas labs RTE-V850E/MA1-CB evaluation board
   (CONFIG_RTE_CB_MA1), with kernel in ROM.  */

MEMORY {
	ROM   : ORIGIN = 0x00000000, LENGTH = 0x00100000
	/* 1MB of SRAM.  This memory is mirrored 4 times.  */
	SRAM  : ORIGIN = SRAM_ADDR,  LENGTH = SRAM_SIZE
	/* 32MB of SDRAM.  */
	SDRAM : ORIGIN = SDRAM_ADDR, LENGTH = SDRAM_SIZE
}

SECTIONS {
	ROMK_SECTIONS(ROM, SRAM)
}
