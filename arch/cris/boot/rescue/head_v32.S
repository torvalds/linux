/*
 * Just get started by jumping to CONFIG_ETRAX_PTABLE_SECTOR to start
 * kernel decompressor.
 *
 * In practice, this only works for NOR flash (or some convoluted RAM boot)
 * and hence is not really useful for Artpec-3, so it's Etrax FS / NOR only.
 *
 */

#include <mach/startup.inc>

#ifdef CONFIG_ETRAX_AXISFLASHMAP

;; Code

	.text
start:

	;; Start clocks for used blocks.
	START_CLOCKS

	move.d	CONFIG_ETRAX_PTABLE_SECTOR, $r10
	jump	$r10			; Jump to decompressor
	nop

#endif
