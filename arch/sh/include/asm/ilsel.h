#ifndef __ASM_SH_ILSEL_H
#define __ASM_SH_ILSEL_H

typedef enum {
	ILSEL_NONE,
	ILSEL_LAN,
	ILSEL_USBH_I,
	ILSEL_USBH_S,
	ILSEL_USBH_V,
	ILSEL_RTC,
	ILSEL_USBP_I,
	ILSEL_USBP_S,
	ILSEL_USBP_V,
	ILSEL_KEY,

	/*
	 * ILSEL Aliases - corner cases for interleaved level tables.
	 *
	 * Someone thought this was a good idea and less hassle than
	 * demuxing a shared vector, really.
	 */

	/* ILSEL0 and 2 */
	ILSEL_FPGA0,
	ILSEL_FPGA1,
	ILSEL_EX1,
	ILSEL_EX2,
	ILSEL_EX3,
	ILSEL_EX4,

	/* ILSEL1 and 3 */
	ILSEL_FPGA2 = ILSEL_FPGA0,
	ILSEL_FPGA3 = ILSEL_FPGA1,
	ILSEL_EX5 = ILSEL_EX1,
	ILSEL_EX6 = ILSEL_EX2,
	ILSEL_EX7 = ILSEL_EX3,
	ILSEL_EX8 = ILSEL_EX4,
} ilsel_source_t;

/* arch/sh/boards/renesas/x3proto/ilsel.c */
int ilsel_enable(ilsel_source_t set);
int ilsel_enable_fixed(ilsel_source_t set, unsigned int level);
void ilsel_disable(unsigned int irq);

#endif /* __ASM_SH_ILSEL_H */
