#ifndef __ASM_ADC_H
#define __ASM_ADC_H
#ifdef __KERNEL__
/*
 * Copyright (C) 2004  Andriy Skulysh
 */

#include <cpu/adc.h>

int adc_single(unsigned int channel);

#endif /* __KERNEL__ */
#endif /* __ASM_ADC_H */
