/* Linker script for the NEC AS85EP1 V850E evaluation board
   (CONFIG_V850E_AS85EP1).  */

MEMORY {
	/* 1MB of internal instruction memory. */
	iMEM0 : ORIGIN = 0,	     LENGTH = 0x00100000

	/* 1MB of static RAM.  */
	SRAM  : ORIGIN = SRAM_ADDR,  LENGTH = SRAM_SIZE

	/* About 58MB of DRAM.  This can actually be at one of two
	   positions, determined by jump JP3; we have to use the first
	   position because the second is partially out of processor
	   instruction addressing range (though in the second position
	   there's actually 64MB available).  */
	SDRAM : ORIGIN = SDRAM_ADDR, LENGTH = SDRAM_SIZE
}

SECTIONS {
	.resetv : {
		__intv_start = . ;
			*(.intv.reset)	/* Reset vector */
	} > iMEM0

	.sram : {
		RAMK_KRAM_CONTENTS

		/* We stick most of the interrupt vectors here; they'll be
		   copied into the proper location by the early init code (we
		   can't put them directly in the right place because of
		   hardware bugs).  The vectors shouldn't need to be
		   relocated, so we don't have to use `> ...  AT> ...' to
		   split the load/vm addresses (and we can't because of
		   problems with the loader).  */
		. = ALIGN (0x10) ;
		__intv_copy_src_start = . ;
			*(.intv.common)	/* Vectors common to all v850e proc. */
			*(.intv.mach)	/* Machine-specific int. vectors.  */
		. = ALIGN (0x10) ;
		__intv_copy_src_end = . ;
	} > SRAM

	/* Where we end up putting the vectors.  */
	__intv_copy_dst_start = 0x10 ;
	__intv_copy_dst_end = __intv_copy_dst_start + (__intv_copy_src_end - __intv_copy_src_start) ;
	__intv_end = __intv_copy_dst_end ;

	.root : { ROOT_FS_CONTENTS } > SDRAM
}
