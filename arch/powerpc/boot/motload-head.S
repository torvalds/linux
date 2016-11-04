#include "ppc_asm.h"

	.text
	.globl _zimage_start
_zimage_start:
	mfmsr   r10
	rlwinm  r10,r10,0,~(1<<15)        /* Clear MSR_EE */
	sync
	mtmsr   r10
	isync
	b	_zimage_start_lib
