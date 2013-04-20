#include <asm/lasat/head.h>

	.text
	.section .text..start, "ax"
	.set noreorder
	.set mips3

	/* Magic words identifying a software image */
	.word	LASAT_K_MAGIC0_VAL
	.word	LASAT_K_MAGIC1_VAL

	/* Image header version */
	.word	0x00000002

	/* image start and size */
	.word	_image_start
	.word	_image_size

	/* start of kernel and entrypoint in uncompressed image */
	.word	_kernel_start
	.word	_kernel_entry

	/* Here we have room for future flags */

	.org	0x40
reldate:
	.word	TIMESTAMP

	.org	0x50
release:
	.string VERSION
