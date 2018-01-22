/* SPDX-License-Identifier: GPL-2.0 */
	.global cris_flush_cache_range
	.type	cris_flush_cache_range, @function
cris_flush_cache_range:
	move.d 1024, $r12
	cmp.d $r11, $r12
	bhi cris_flush_1KB
	nop
	add.d $r10, $r11
	ftagd [$r10]
cris_flush_last:
	addq 32, $r10
	cmp.d $r11, $r10
	blt cris_flush_last
	ftagd [$r10]
	ret
	nop
cris_flush_1KB:
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ftagd [$r10]
	addq 32, $r10
	ba cris_flush_cache_range
	sub.d $r12, $r11
	.size	cris_flush_cache_range, . - cris_flush_cache_range

	.global cris_flush_cache
	.type	cris_flush_cache, @function
cris_flush_cache:
	moveq 0, $r10
cris_flush_line:
	move.d 16*1024, $r11
	addq 16, $r10
	cmp.d $r10, $r11
	blt cris_flush_line
	fidxd [$r10]
	ret
	nop
	.size	cris_flush_cache, . - cris_flush_cache

