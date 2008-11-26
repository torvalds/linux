/*
 * include/asm-mips/vr41xx/vr41xx.h
 *
 * Include file for NEC VR4100 series.
 *
 * Copyright (C) 1999 Michael Klar
 * Copyright (C) 2001, 2002 Paul Mundt
 * Copyright (C) 2002 MontaVista Software, Inc.
 * Copyright (C) 2002 TimeSys Corp.
 * Copyright (C) 2003-2008 Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#ifndef __NEC_VR41XX_H
#define __NEC_VR41XX_H

#include <linux/interrupt.h>

/*
 * CPU Revision
 */
/* VR4122 0x00000c70-0x00000c72 */
#define PRID_VR4122_REV1_0	0x00000c70
#define PRID_VR4122_REV2_0	0x00000c70
#define PRID_VR4122_REV2_1	0x00000c70
#define PRID_VR4122_REV3_0	0x00000c71
#define PRID_VR4122_REV3_1	0x00000c72

/* VR4181A 0x00000c73-0x00000c7f */
#define PRID_VR4181A_REV1_0	0x00000c73
#define PRID_VR4181A_REV1_1	0x00000c74

/* VR4131 0x00000c80-0x00000c83 */
#define PRID_VR4131_REV1_2	0x00000c80
#define PRID_VR4131_REV2_0	0x00000c81
#define PRID_VR4131_REV2_1	0x00000c82
#define PRID_VR4131_REV2_2	0x00000c83

/* VR4133 0x00000c84- */
#define PRID_VR4133		0x00000c84

/*
 * Bus Control Uint
 */
extern unsigned long vr41xx_calculate_clock_frequency(void);
extern unsigned long vr41xx_get_vtclock_frequency(void);
extern unsigned long vr41xx_get_tclock_frequency(void);

/*
 * Clock Mask Unit
 */
typedef enum {
	PIU_CLOCK,
	SIU_CLOCK,
	AIU_CLOCK,
	KIU_CLOCK,
	FIR_CLOCK,
	DSIU_CLOCK,
	CSI_CLOCK,
	PCIU_CLOCK,
	HSP_CLOCK,
	PCI_CLOCK,
	CEU_CLOCK,
	ETHER0_CLOCK,
	ETHER1_CLOCK
} vr41xx_clock_t;

extern void vr41xx_supply_clock(vr41xx_clock_t clock);
extern void vr41xx_mask_clock(vr41xx_clock_t clock);

/*
 * Interrupt Control Unit
 */
extern int vr41xx_set_intassign(unsigned int irq, unsigned char intassign);
extern int cascade_irq(unsigned int irq, int (*get_irq)(unsigned int));

#define PIUINT_COMMAND		0x0040
#define PIUINT_DATA		0x0020
#define PIUINT_PAGE1		0x0010
#define PIUINT_PAGE0		0x0008
#define PIUINT_DATALOST		0x0004
#define PIUINT_STATUSCHANGE	0x0001

extern void vr41xx_enable_piuint(uint16_t mask);
extern void vr41xx_disable_piuint(uint16_t mask);

#define AIUINT_INPUT_DMAEND	0x0800
#define AIUINT_INPUT_DMAHALT	0x0400
#define AIUINT_INPUT_DATALOST	0x0200
#define AIUINT_INPUT_DATA	0x0100
#define AIUINT_OUTPUT_DMAEND	0x0008
#define AIUINT_OUTPUT_DMAHALT	0x0004
#define AIUINT_OUTPUT_NODATA	0x0002

extern void vr41xx_enable_aiuint(uint16_t mask);
extern void vr41xx_disable_aiuint(uint16_t mask);

#define KIUINT_DATALOST		0x0004
#define KIUINT_DATAREADY	0x0002
#define KIUINT_SCAN		0x0001

extern void vr41xx_enable_kiuint(uint16_t mask);
extern void vr41xx_disable_kiuint(uint16_t mask);

#define DSIUINT_CTS		0x0800
#define DSIUINT_RXERR		0x0400
#define DSIUINT_RX		0x0200
#define DSIUINT_TX		0x0100
#define DSIUINT_ALL		0x0f00

extern void vr41xx_enable_dsiuint(uint16_t mask);
extern void vr41xx_disable_dsiuint(uint16_t mask);

#define FIRINT_UNIT		0x0010
#define FIRINT_RX_DMAEND	0x0008
#define FIRINT_RX_DMAHALT	0x0004
#define FIRINT_TX_DMAEND	0x0002
#define FIRINT_TX_DMAHALT	0x0001

extern void vr41xx_enable_firint(uint16_t mask);
extern void vr41xx_disable_firint(uint16_t mask);

extern void vr41xx_enable_pciint(void);
extern void vr41xx_disable_pciint(void);

extern void vr41xx_enable_scuint(void);
extern void vr41xx_disable_scuint(void);

#define CSIINT_TX_DMAEND	0x0040
#define CSIINT_TX_DMAHALT	0x0020
#define CSIINT_TX_DATA		0x0010
#define CSIINT_TX_FIFOEMPTY	0x0008
#define CSIINT_RX_DMAEND	0x0004
#define CSIINT_RX_DMAHALT	0x0002
#define CSIINT_RX_FIFOEMPTY	0x0001

extern void vr41xx_enable_csiint(uint16_t mask);
extern void vr41xx_disable_csiint(uint16_t mask);

extern void vr41xx_enable_bcuint(void);
extern void vr41xx_disable_bcuint(void);

#ifdef CONFIG_SERIAL_VR41XX_CONSOLE
extern void vr41xx_siu_setup(void);
#else
static inline void vr41xx_siu_setup(void) {}
#endif

#endif /* __NEC_VR41XX_H */
