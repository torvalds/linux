/* romfs move to __ebss */

#include <asm/linkage.h>

#if defined(__H8300H__) 
	.h8300h
#endif
#if defined(__H8300S__) 
	.h8300s
#endif

#define BLKOFFSET 512

	.text
.globl	__move_romfs
_romfs_sig_len = 8

__move_romfs:	
	mov.l	#__sbss,er0
	mov.l	#_romfs_sig,er1
	mov.b	#_romfs_sig_len,r3l
1:					/* check romfs image */
	mov.b	@er0+,r2l
	mov.b	@er1+,r2h
	cmp.b	r2l,r2h
	bne	2f
	dec.b	r3l
	bne	1b

	/* find romfs image */
	mov.l	@__sbss+8,er0		/* romfs length(be) */
	mov.l	#__sbss,er1
	add.l	er0,er1			/* romfs image end */
	mov.l	#__ebss,er2
	add.l	er0,er2			/* distination address */
#if defined(CONFIG_INTELFLASH)
	add.l	#BLKOFFSET,er2
#endif
	adds	#2,er0
	adds	#1,er0
	shlr	er0
	shlr	er0			/* transfer length */
1:
	mov.l	@er1,er3		/* copy image */
	mov.l	er3,@er2
	subs	#4,er1
	subs	#4,er2
	dec.l	#1,er0
	bpl	1b
2:
	rts

	.section	.rodata
_romfs_sig:	
	.ascii	"-rom1fs-"

	.end
