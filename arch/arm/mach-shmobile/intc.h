#ifndef __ASM_MACH_INTC_H
#define __ASM_MACH_INTC_H
#include <linux/sh_intc.h>

#define INTC_IRQ_PINS_ENUM_16L(p)				\
	p ## _IRQ0, p ## _IRQ1, p ## _IRQ2, p ## _IRQ3,		\
	p ## _IRQ4, p ## _IRQ5, p ## _IRQ6, p ## _IRQ7,		\
	p ## _IRQ8, p ## _IRQ9, p ## _IRQ10, p ## _IRQ11,	\
	p ## _IRQ12, p ## _IRQ13, p ## _IRQ14, p ## _IRQ15

#define INTC_IRQ_PINS_ENUM_16H(p)				\
	p ## _IRQ16, p ## _IRQ17, p ## _IRQ18, p ## _IRQ19,	\
	p ## _IRQ20, p ## _IRQ21, p ## _IRQ22, p ## _IRQ23,	\
	p ## _IRQ24, p ## _IRQ25, p ## _IRQ26, p ## _IRQ27,	\
	p ## _IRQ28, p ## _IRQ29, p ## _IRQ30, p ## _IRQ31

#define INTC_IRQ_PINS_VECT_16L(p, vect)				\
	vect(p ## _IRQ0, 0x0200), vect(p ## _IRQ1, 0x0220),	\
	vect(p ## _IRQ2, 0x0240), vect(p ## _IRQ3, 0x0260),	\
	vect(p ## _IRQ4, 0x0280), vect(p ## _IRQ5, 0x02a0),	\
	vect(p ## _IRQ6, 0x02c0), vect(p ## _IRQ7, 0x02e0),	\
	vect(p ## _IRQ8, 0x0300), vect(p ## _IRQ9, 0x0320),	\
	vect(p ## _IRQ10, 0x0340), vect(p ## _IRQ11, 0x0360),	\
	vect(p ## _IRQ12, 0x0380), vect(p ## _IRQ13, 0x03a0),	\
	vect(p ## _IRQ14, 0x03c0), vect(p ## _IRQ15, 0x03e0)

#define INTC_IRQ_PINS_VECT_16H(p, vect)				\
	vect(p ## _IRQ16, 0x3200), vect(p ## _IRQ17, 0x3220),	\
	vect(p ## _IRQ18, 0x3240), vect(p ## _IRQ19, 0x3260),	\
	vect(p ## _IRQ20, 0x3280), vect(p ## _IRQ21, 0x32a0),	\
	vect(p ## _IRQ22, 0x32c0), vect(p ## _IRQ23, 0x32e0),	\
	vect(p ## _IRQ24, 0x3300), vect(p ## _IRQ25, 0x3320),	\
	vect(p ## _IRQ26, 0x3340), vect(p ## _IRQ27, 0x3360),	\
	vect(p ## _IRQ28, 0x3380), vect(p ## _IRQ29, 0x33a0),	\
	vect(p ## _IRQ30, 0x33c0), vect(p ## _IRQ31, 0x33e0)

#define INTC_IRQ_PINS_MASK_16L(p, base)					\
	{ base + 0x40, base + 0x60, 8, /* INTMSK00A / INTMSKCLR00A */	\
	  { p ## _IRQ0, p ## _IRQ1, p ## _IRQ2, p ## _IRQ3,		\
	    p ## _IRQ4, p ## _IRQ5, p ## _IRQ6, p ## _IRQ7 } },		\
	{ base + 0x44, base + 0x64, 8, /* INTMSK10A / INTMSKCLR10A */	\
	  { p ## _IRQ8, p ## _IRQ9, p ## _IRQ10, p ## _IRQ11,		\
	    p ## _IRQ12, p ## _IRQ13, p ## _IRQ14, p ## _IRQ15 } }

#define INTC_IRQ_PINS_MASK_16H(p, base)					\
	{ base + 0x48, base + 0x68, 8, /* INTMSK20A / INTMSKCLR20A */	\
	  { p ## _IRQ16, p ## _IRQ17, p ## _IRQ18, p ## _IRQ19,		\
	    p ## _IRQ20, p ## _IRQ21, p ## _IRQ22, p ## _IRQ23 } },	\
	{ base + 0x4c, base + 0x6c, 8, /* INTMSK30A / INTMSKCLR30A */	\
	  { p ## _IRQ24, p ## _IRQ25, p ## _IRQ26, p ## _IRQ27,		\
	    p ## _IRQ28, p ## _IRQ29, p ## _IRQ30, p ## _IRQ31 } }

#define INTC_IRQ_PINS_PRIO_16L(p, base)					\
	{ base + 0x10, 0, 32, 4, /* INTPRI00A */			\
	  { p ## _IRQ0, p ## _IRQ1, p ## _IRQ2, p ## _IRQ3,		\
	    p ## _IRQ4, p ## _IRQ5, p ## _IRQ6, p ## _IRQ7 } },		\
	{ base + 0x14, 0, 32, 4, /* INTPRI10A */			\
	  { p ## _IRQ8, p ## _IRQ9, p ## _IRQ10, p ## _IRQ11,		\
	    p ## _IRQ12, p ## _IRQ13, p ## _IRQ14, p ## _IRQ15 } }

#define INTC_IRQ_PINS_PRIO_16H(p, base)					\
	{ base + 0x18, 0, 32, 4, /* INTPRI20A */			\
	  { p ## _IRQ16, p ## _IRQ17, p ## _IRQ18, p ## _IRQ19,		\
	    p ## _IRQ20, p ## _IRQ21, p ## _IRQ22, p ## _IRQ23 } },	\
	{ base + 0x1c, 0, 32, 4, /* INTPRI30A */			\
	  { p ## _IRQ24, p ## _IRQ25, p ## _IRQ26, p ## _IRQ27,		\
	    p ## _IRQ28, p ## _IRQ29, p ## _IRQ30, p ## _IRQ31 } }

#define INTC_IRQ_PINS_SENSE_16L(p, base)				\
	{ base + 0x00, 32, 4, /* ICR1A */				\
	  { p ## _IRQ0, p ## _IRQ1, p ## _IRQ2, p ## _IRQ3,		\
	    p ## _IRQ4, p ## _IRQ5, p ## _IRQ6, p ## _IRQ7 } },		\
	{ base + 0x04, 32, 4, /* ICR2A */				\
	  { p ## _IRQ8, p ## _IRQ9, p ## _IRQ10, p ## _IRQ11,		\
	    p ## _IRQ12, p ## _IRQ13, p ## _IRQ14, p ## _IRQ15 } }

#define INTC_IRQ_PINS_SENSE_16H(p, base)				\
	{ base + 0x08, 32, 4, /* ICR3A */				\
	  { p ## _IRQ16, p ## _IRQ17, p ## _IRQ18, p ## _IRQ19,		\
	    p ## _IRQ20, p ## _IRQ21, p ## _IRQ22, p ## _IRQ23 } },	\
	{ base + 0x0c, 32, 4, /* ICR4A */				\
	  { p ## _IRQ24, p ## _IRQ25, p ## _IRQ26, p ## _IRQ27,		\
	    p ## _IRQ28, p ## _IRQ29, p ## _IRQ30, p ## _IRQ31 } }

#define INTC_IRQ_PINS_ACK_16L(p, base)					\
	{ base + 0x20, 0, 8, /* INTREQ00A */				\
	  { p ## _IRQ0, p ## _IRQ1, p ## _IRQ2, p ## _IRQ3,		\
	    p ## _IRQ4, p ## _IRQ5, p ## _IRQ6, p ## _IRQ7 } },		\
	{ base + 0x24, 0, 8, /* INTREQ10A */				\
	  { p ## _IRQ8, p ## _IRQ9, p ## _IRQ10, p ## _IRQ11,		\
	    p ## _IRQ12, p ## _IRQ13, p ## _IRQ14, p ## _IRQ15 } }

#define INTC_IRQ_PINS_ACK_16H(p, base)					\
	{ base + 0x28, 0, 8, /* INTREQ20A */				\
	  { p ## _IRQ16, p ## _IRQ17, p ## _IRQ18, p ## _IRQ19,		\
	    p ## _IRQ20, p ## _IRQ21, p ## _IRQ22, p ## _IRQ23 } },	\
	{ base + 0x2c, 0, 8, /* INTREQ30A */				\
	  { p ## _IRQ24, p ## _IRQ25, p ## _IRQ26, p ## _IRQ27,		\
	    p ## _IRQ28, p ## _IRQ29, p ## _IRQ30, p ## _IRQ31 } }

#define INTC_IRQ_PINS_16(p, base, vect, str)				\
									\
static struct resource p ## _resources[] __initdata = {			\
	[0] = {								\
		.start	= base,						\
		.end	= base + 0x64,					\
		.flags	= IORESOURCE_MEM,				\
	},								\
};									\
									\
enum {									\
	p ## _UNUSED = 0,						\
	INTC_IRQ_PINS_ENUM_16L(p),					\
};									\
									\
static struct intc_vect p ## _vectors[] __initdata = {			\
	INTC_IRQ_PINS_VECT_16L(p, vect),				\
};									\
									\
static struct intc_mask_reg p ## _mask_registers[] __initdata = {	\
	INTC_IRQ_PINS_MASK_16L(p, base),				\
};									\
									\
static struct intc_prio_reg p ## _prio_registers[] __initdata = {	\
	INTC_IRQ_PINS_PRIO_16L(p, base),				\
};									\
									\
static struct intc_sense_reg p ## _sense_registers[] __initdata = {	\
	INTC_IRQ_PINS_SENSE_16L(p, base),				\
};									\
									\
static struct intc_mask_reg p ## _ack_registers[] __initdata = {	\
	INTC_IRQ_PINS_ACK_16L(p, base),					\
};									\
									\
static struct intc_desc p ## _desc __initdata = {			\
	.name = str,							\
	.resource = p ## _resources,					\
	.num_resources = ARRAY_SIZE(p ## _resources),			\
	.hw = INTC_HW_DESC(p ## _vectors, NULL,				\
			     p ## _mask_registers, p ## _prio_registers, \
			     p ## _sense_registers, p ## _ack_registers) \
}

#define INTC_IRQ_PINS_16H(p, base, vect, str)				\
									\
static struct resource p ## _resources[] __initdata = {			\
	[0] = {								\
		.start	= base,						\
		.end	= base + 0x64,					\
		.flags	= IORESOURCE_MEM,				\
	},								\
};									\
									\
enum {									\
	p ## _UNUSED = 0,						\
	INTC_IRQ_PINS_ENUM_16H(p),					\
};									\
									\
static struct intc_vect p ## _vectors[] __initdata = {			\
	INTC_IRQ_PINS_VECT_16H(p, vect),				\
};									\
									\
static struct intc_mask_reg p ## _mask_registers[] __initdata = {	\
	INTC_IRQ_PINS_MASK_16H(p, base),				\
};									\
									\
static struct intc_prio_reg p ## _prio_registers[] __initdata = {	\
	INTC_IRQ_PINS_PRIO_16H(p, base),				\
};									\
									\
static struct intc_sense_reg p ## _sense_registers[] __initdata = {	\
	INTC_IRQ_PINS_SENSE_16H(p, base),				\
};									\
									\
static struct intc_mask_reg p ## _ack_registers[] __initdata = {	\
	INTC_IRQ_PINS_ACK_16H(p, base),					\
};									\
									\
static struct intc_desc p ## _desc __initdata = {			\
	.name = str,							\
	.resource = p ## _resources,					\
	.num_resources = ARRAY_SIZE(p ## _resources),			\
	.hw = INTC_HW_DESC(p ## _vectors, NULL,				\
			     p ## _mask_registers, p ## _prio_registers, \
			     p ## _sense_registers, p ## _ack_registers) \
}

#define INTC_IRQ_PINS_32(p, base, vect, str)				\
									\
static struct resource p ## _resources[] __initdata = {			\
	[0] = {								\
		.start	= base,						\
		.end	= base + 0x6c,					\
		.flags	= IORESOURCE_MEM,				\
	},								\
};									\
									\
enum {									\
	p ## _UNUSED = 0,						\
	INTC_IRQ_PINS_ENUM_16L(p),					\
	INTC_IRQ_PINS_ENUM_16H(p),					\
};									\
									\
static struct intc_vect p ## _vectors[] __initdata = {			\
	INTC_IRQ_PINS_VECT_16L(p, vect),				\
	INTC_IRQ_PINS_VECT_16H(p, vect),				\
};									\
									\
static struct intc_mask_reg p ## _mask_registers[] __initdata = {	\
	INTC_IRQ_PINS_MASK_16L(p, base),				\
	INTC_IRQ_PINS_MASK_16H(p, base),				\
};									\
									\
static struct intc_prio_reg p ## _prio_registers[] __initdata = {	\
	INTC_IRQ_PINS_PRIO_16L(p, base),				\
	INTC_IRQ_PINS_PRIO_16H(p, base),				\
};									\
									\
static struct intc_sense_reg p ## _sense_registers[] __initdata = {	\
	INTC_IRQ_PINS_SENSE_16L(p, base),				\
	INTC_IRQ_PINS_SENSE_16H(p, base),				\
};									\
									\
static struct intc_mask_reg p ## _ack_registers[] __initdata = {	\
	INTC_IRQ_PINS_ACK_16L(p, base),					\
	INTC_IRQ_PINS_ACK_16H(p, base),					\
};									\
									\
static struct intc_desc p ## _desc __initdata = {			\
	.name = str,							\
	.resource = p ## _resources,					\
	.num_resources = ARRAY_SIZE(p ## _resources),			\
	.hw = INTC_HW_DESC(p ## _vectors, NULL,				\
			     p ## _mask_registers, p ## _prio_registers, \
			     p ## _sense_registers, p ## _ack_registers) \
}

#define INTC_PINT_E_EMPTY
#define INTC_PINT_E_NONE 0, 0, 0, 0, 0, 0, 0, 0,
#define INTC_PINT_E(p)							\
	PINT ## p ## 0, PINT ## p ## 1, PINT ## p ## 2, PINT ## p ## 3,	\
	PINT ## p ## 4, PINT ## p ## 5, PINT ## p ## 6, PINT ## p ## 7,

#define INTC_PINT_V_NONE
#define INTC_PINT_V(p, vect)					\
	vect(PINT ## p ## 0, 0), vect(PINT ## p ## 1, 1),	\
	vect(PINT ## p ## 2, 2), vect(PINT ## p ## 3, 3),	\
	vect(PINT ## p ## 4, 4), vect(PINT ## p ## 5, 5),	\
	vect(PINT ## p ## 6, 6), vect(PINT ## p ## 7, 7),

#define INTC_PINT(p, mask_reg, sense_base, str,				\
	enums_1, enums_2, enums_3, enums_4,				\
	vect_1, vect_2, vect_3, vect_4,					\
	mask_a, mask_b, mask_c, mask_d,					\
	sense_a, sense_b, sense_c, sense_d)				\
									\
enum {									\
	PINT ## p ## _UNUSED = 0,					\
	enums_1 enums_2 enums_3 enums_4 				\
};									\
									\
static struct intc_vect p ## _vectors[] __initdata = {			\
	vect_1 vect_2 vect_3 vect_4					\
};									\
									\
static struct intc_mask_reg p ## _mask_registers[] __initdata = {	\
	{ mask_reg, 0, 32, /* PINTER */					\
	  { mask_a mask_b mask_c mask_d } }				\
};									\
									\
static struct intc_sense_reg p ## _sense_registers[] __initdata = {	\
	{ sense_base + 0x00, 16, 2, /* PINTCR */			\
	  { sense_a } },						\
	{ sense_base + 0x04, 16, 2, /* PINTCR */			\
	  { sense_b } },						\
	{ sense_base + 0x08, 16, 2, /* PINTCR */			\
	  { sense_c } },						\
	{ sense_base + 0x0c, 16, 2, /* PINTCR */			\
	  { sense_d } },						\
};									\
									\
static struct intc_desc p ## _desc __initdata = {			\
	.name = str,							\
	.hw = INTC_HW_DESC(p ## _vectors, NULL,				\
			     p ## _mask_registers, NULL,		\
			     p ## _sense_registers, NULL),		\
}

/* INTCS */
#define INTCS_VECT_BASE		0x3400
#define INTCS_VECT(n, vect)	INTC_VECT((n), INTCS_VECT_BASE + (vect))
#define intcs_evt2irq(evt)	evt2irq(INTCS_VECT_BASE + (evt))

#endif  /* __ASM_MACH_INTC_H */
