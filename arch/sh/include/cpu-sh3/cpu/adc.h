/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_CPU_SH3_ADC_H
#define __ASM_CPU_SH3_ADC_H

/*
 * Copyright (C) 2004  Andriy Skulysh
 */


#define ADDRAH	0xa4000080
#define ADDRAL	0xa4000082
#define ADDRBH	0xa4000084
#define ADDRBL	0xa4000086
#define ADDRCH	0xa4000088
#define ADDRCL	0xa400008a
#define ADDRDH	0xa400008c
#define ADDRDL	0xa400008e
#define ADCSR	0xa4000090

#define ADCSR_ADF	0x80
#define ADCSR_ADIE	0x40
#define ADCSR_ADST	0x20
#define ADCSR_MULTI	0x10
#define ADCSR_CKS	0x08
#define ADCSR_CH_MASK	0x07

#define ADCR	0xa4000092

#endif /* __ASM_CPU_SH3_ADC_H */
