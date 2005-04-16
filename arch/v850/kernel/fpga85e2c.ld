/* Linker script for the FPGA implementation of the V850E2 NA85E2C cpu core
   (CONFIG_V850E2_FPGA85E2C).  */

MEMORY {
	/* Reset vector.  */
	RESET	 : ORIGIN = 0, LENGTH = 0x10
	/* Interrupt vectors.  */
	INTV      : ORIGIN = 0x10, LENGTH = 0x470
	/* The `window' in RAM were we're allowed to load stuff.  */
	RAM_LOW   : ORIGIN = 0x480, LENGTH = 0x0005FB80
	/* Some more ram above the window were we can put bss &c.  */
	RAM_HIGH  : ORIGIN = 0x00060000, LENGTH = 0x000A0000
	/* This is the area visible from the outside world (we can use
	   this only for uninitialized data).  */
	VISIBLE   : ORIGIN = 0x00200000, LENGTH = 0x00060000
}

SECTIONS {
	.reset : {
		__kram_start = . ;
		__intv_start = . ;
	        	*(.intv.reset)	/* Reset vector */
	} > RESET

	.ram_low : {
		__r0_ram = . ;		/* Must be near address 0.  */
		. = . + 32 ;

		TEXT_CONTENTS
		DATA_CONTENTS
		ROOT_FS_CONTENTS
		RAMK_INIT_CONTENTS_NO_END
		INITRAMFS_CONTENTS
	} > RAM_LOW

        /* Where the interrupt vectors are initially loaded.  */
	__intv_load_start = . ;

	.intv : {
			*(.intv.common)	/* Vectors common to all v850e proc. */
			*(.intv.mach)	/* Machine-specific int. vectors.  */
		__intv_end = . ;
	} > INTV  AT> RAM_LOW

	.ram_high : {
		/* This is here so that when we free init memory the
		   load-time copy of the interrupt vectors and any empty
		   space at the end of the `RAM_LOW' area is freed too.  */
		. = ALIGN (4096);
		__init_end = . ;

		BSS_CONTENTS
		__kram_end = . ;
		BOOTMAP_CONTENTS
	} > RAM_HIGH

	.visible : {
		_memcons_output = . ;
		. = . + 0x8000 ;
		_memcons_output_end = . ;
	} > VISIBLE
}
