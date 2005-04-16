/* Linker script for the Midas labs RTE-V850E/ME2-CB evaluation board
   (CONFIG_RTE_CB_ME2), with kernel in SDRAM.  */

MEMORY {
	/* 128Kbyte of IRAM */
	IRAM : ORIGIN = 0x00000000, LENGTH = 0x00020000

	/* 32MB of SDRAM.  */
	SDRAM : ORIGIN = SDRAM_ADDR, LENGTH = SDRAM_SIZE
}

#define KRAM SDRAM

SECTIONS {
	.text : {
		__kram_start = . ;
		TEXT_CONTENTS
		INTV_CONTENTS	/* copy to iRAM (0x0-0x620) */
	} > KRAM

	.data : {
		DATA_CONTENTS
		BSS_CONTENTS
		RAMK_INIT_CONTENTS
		__kram_end = . ;
		BOOTMAP_CONTENTS
	} > KRAM
	
	.root ALIGN (4096) : { ROOT_FS_CONTENTS } > SDRAM
}
