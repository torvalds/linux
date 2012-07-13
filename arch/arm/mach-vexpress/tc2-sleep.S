#include <linux/linkage.h>

#define SPC_PHYS_BASE           0x7FFF0000
#define A15_CONF          	0x400

ENTRY(tc2_cpu_resume)
	mrc	p15, 0, r0, c0, c0, 5
	ands	r0, r0, #0xff00
	ldr	r1, =SPC_PHYS_BASE
	mov	r2, #A15_CONF
	add	r1, r1, r2
	ldr	r1, [r1]
	and	r1, r1, #0x7
	cmp	r1, r0, lsr #8
	adr	r0, value
	addne	r0, r0, #16
	ldmia	r0, {r1, r2, r3, r4}	@ CCI address, SCC snoop control & val
	mvn	r3, r3			@ undo actions done at shutdown
	ldr	r0, [r2]
	and	r5, r0, r3
	str	r5, [r2]
	mov	r0, #3			@ enable CCI for the cluster
	str	r0, [r1]
	adr	r1, cci_ctrl
	ldr	r1, [r1]
loop:
	ldr	r0, [r1]
	ands	r0, r0, #1
	bne	loop
	mov	r0, #0			@ disable power down enable
	str	r0, [r4]
	b	cpu_resume
ENDPROC(tc2_cpu_resume)

ENTRY(disable_snoops)
	mrc	p15, 0, r0, c0, c0, 5
	ands	r0, r0, #0xff00
	ldr	r1, scc_ptr
	ldr	r1, [r1]
	mov	r2, #A15_CONF
	add	r1, r1, r2
	ldr	r1, [r1]
	and	r1, r1, #0x7
	cmp	r1, r0, lsr #8
	adr	r0, vvalue
	addne	r0, r0, #8
	ldmia	r0, {r2, r3}	@ CCI address, SCC snoop control & val
	ldr	r1, scc_ptr
	ldr	r1, [r1]
	add	r2, r1, r2
	ldr	r0, [r2]
	orr	r0, r0, r3
	dsb
	isb
	str	r0, [r2]
	wfi
ENDPROC(disable_snoops)

cci_ctrl:
	.long	0x2c09000c
value:
	.long 	0x2c094000
	.long 	0x7fff0404
	.long 	0x180
	.long 	0x7fff0b30
	.long 	0x2c095000
	.long 	0x7fff0504
	.long 	0x2000
	.long 	0x7fff0b34
vvalue:
	.long 	0x404
	.long 	0x180
	.long 	0x504
	.long 	0x2000
scc_ptr:
	.long vscc
