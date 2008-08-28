/*
 * arch/arm/mach-l7200/include/mach/aux_reg.h
 *
 * Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *
 * Changelog:
 *   08-02-2000	SJH	Created file
 */
#ifndef _ASM_ARCH_AUXREG_H
#define _ASM_ARCH_AUXREG_H

#include <mach/hardware.h>

#define l7200aux_reg	*((volatile unsigned int *) (AUX_BASE))

/*
 * Auxillary register values
 */
#define AUX_CLEAR		0x00000000
#define AUX_DIAG_LED_ON		0x00000002
#define AUX_RTS_UART1		0x00000004
#define AUX_DTR_UART1		0x00000008
#define AUX_KBD_COLUMN_12_HIGH	0x00000010
#define AUX_KBD_COLUMN_12_OFF	0x00000020
#define AUX_KBD_COLUMN_13_HIGH	0x00000040
#define AUX_KBD_COLUMN_13_OFF	0x00000080

#endif
