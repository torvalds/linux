#ifdef CONFIG_DEBUG_LL_NINTENDO3DS_ARM9

#define EARLY_SHARED_BUF_PA (0x20000000)

.macro	addruart,rp,rv,tmp
	mov	\rp, #EARLY_SHARED_BUF_PA
	mov	\rv, #(EARLY_SHARED_BUF_PA - CONFIG_PHYS_OFFSET + CONFIG_PAGE_OFFSET)
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
99:
	# Invalidate Entire Data Cache
	mov	\rd, #0
	mcr	p15, 0, \rd, c7, c6, 0

	ldrb	\rd, [\rx]
	cmp	\rd, #0
	bne	99b
.endm

.macro	waituart,rd,rx
.endm

#else

.macro	addruart,rp,rv,tmp
.endm

.macro	senduart,rd,rx
.endm

.macro	busyuart,rd,rx
.endm

.macro	waituart,rd,rx
.endm

#endif
