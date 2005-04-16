/* Linker script for the Midas labs RTE-NB85E-CB evaluation board
   (CONFIG_RTE_CB_NB85E).  */

MEMORY {
	LOW   : ORIGIN = 0x0,	     LENGTH = 0x00100000
	/* 1MB of SRAM  This memory is mirrored 4 times.  */
	SRAM  : ORIGIN = SRAM_ADDR,  LENGTH = SRAM_SIZE
	/* 16MB of SDRAM.  */
	SDRAM : ORIGIN = SDRAM_ADDR, LENGTH = SDRAM_SIZE
}

#ifdef CONFIG_RTE_CB_NB85E_KSRAM
# define KRAM SRAM
#else
# define KRAM SDRAM
#endif

SECTIONS {
	.intv : { INTV_CONTENTS } > LOW
	.sram : { RAMK_KRAM_CONTENTS } > KRAM
	.root : { ROOT_FS_CONTENTS } > SDRAM
}
