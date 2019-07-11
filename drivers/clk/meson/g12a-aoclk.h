/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __G12A_AOCLKC_H
#define __G12A_AOCLKC_H

/*
 * CLKID index values
 *
 * These indices are entirely contrived and do not map onto the hardware.
 * It has now been decided to expose everything by default in the DT header:
 * include/dt-bindings/clock/g12a-aoclkc.h. Only the clocks ids we don't want
 * to expose, such as the internal muxes and dividers of composite clocks,
 * will remain defined here.
 */
#define CLKID_AO_SAR_ADC_SEL	16
#define CLKID_AO_SAR_ADC_DIV	17
#define CLKID_AO_CTS_OSCIN	19
#define CLKID_AO_32K_PRE	20
#define CLKID_AO_32K_DIV	21
#define CLKID_AO_32K_SEL	22
#define CLKID_AO_CEC_PRE	24
#define CLKID_AO_CEC_DIV	25
#define CLKID_AO_CEC_SEL	26

#define NR_CLKS	29

#include <dt-bindings/clock/g12a-aoclkc.h>
#include <dt-bindings/reset/g12a-aoclkc.h>

#endif /* __G12A_AOCLKC_H */
