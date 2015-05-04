#define EARLY_SHARED_BUF_PA (0x27FFFFFF)

.macro	addruart, rp, rv, tmp
	mov	\rp, #EARLY_SHARED_BUF_PA
	ldr	\rv, =(EARLY_SHARED_BUF_PA + CONFIG_PAGE_OFFSET - CONFIG_PHYS_OFFSET)
.endm

.macro	senduart,rd,rx
	strb	\rd, [\rx]
	# Clean and Invalidate Entire Data Cache
	# Data Synchronization Barrier
	mov	\rd, #0
	mcr	p15, 0, \rd, c7, c14, 0
	mcr 	p15, 0, \rd, c7, c10, 4
.endm

.macro	busyuart,rd,rx
1:	# Invalidate Entire Data Cache
	mov	\rd, #0
	mcr	p15, 0, \rd, c7, c6, 0

	ldrb	\rd, [\rx]
	cmp	\rd, #0
	bne	1b
.endm

.macro	waituart,rd,rx
	busyuart \rd, \rx
.endm
