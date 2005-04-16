/* Linker script for the sim85e2c simulator, which is a verilog simulation of
   the V850E2 NA85E2C cpu core (CONFIG_V850E2_SIM85E2C).  */

MEMORY {
	/* 1MB of `instruction RAM', starting at 0.
	   Instruction fetches are much faster from IRAM than from DRAM.  */
	IRAM : ORIGIN = IRAM_ADDR, LENGTH = IRAM_SIZE

	/* 1MB of `data RAM', below and contiguous with the I/O space.
	   Data fetches are much faster from DRAM than from IRAM.  */
	DRAM : ORIGIN = DRAM_ADDR, LENGTH = DRAM_SIZE

	/* `external ram' (CS1 area), comes after IRAM.  */
	ERAM : ORIGIN = ERAM_ADDR, LENGTH = ERAM_SIZE

	/* Dynamic RAM; uses memory controller.  */
	SDRAM : ORIGIN = SDRAM_ADDR, LENGTH = SDRAM_SIZE
}

SECTIONS {
	.iram : {
		INTV_CONTENTS
		*arch/v850/kernel/head.o
		*(.early.text)
	} > IRAM
	.dram : {
		_memcons_output = . ;
		. = . + 0x8000 ;
		_memcons_output_end = . ;
	} > DRAM
	.sdram : {
		/* We stick console output into a buffer here.  */
		RAMK_KRAM_CONTENTS
		ROOT_FS_CONTENTS
	} > SDRAM
}
