

.macro  get_irqnr_preamble, base, tmp
ldr	\base, =0
.endm

.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
ldr	\irqstat, [\base, #0]
clz	\irqnr, \irqstat
rsb	\irqnr, \irqnr, #31
teq	\irqstat, #0
.endm
