;; Core of the spinlock implementation
;;
;; Copyright (C) 2004 Axis Communications AB.
;;
;; Author: Mikael Starvik


	.global cris_spin_lock
	.global cris_spin_trylock

	.text

cris_spin_lock:
	clearf	p
1:	test.b	[$r10]
	beq	1b
	clearf	p
	ax
	clear.b [$r10]
	bcs     1b
	clearf	p
	ret
	nop

cris_spin_trylock:
	clearf	p
1:	move.b	[$r10], $r11
	ax
	clear.b [$r10]
        bcs	1b
        clearf	p
	ret
	movu.b	$r11,$r10
