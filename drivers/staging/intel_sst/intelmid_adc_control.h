#ifndef __INTELMID_ADC_CONTROL_H__
#define __INTELMID_ADC_CONTROL_H_
/*
 *  intelmid_adc_control.h - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corporation
 *  Authors:	R Durgadadoss <r.durgadoss@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  Common private ADC declarations for SST
 */


#define MSIC_ADC1CNTL1		0x1C0
#define MSIC_ADC_ENBL		0x10
#define MSIC_ADC_START		0x08

#define MSIC_ADC1CNTL3		0x1C2
#define MSIC_ADCTHERM_ENBL	0x04
#define MSIC_ADCRRDATA_ENBL	0x05

#define MSIC_STOPBIT_MASK	16
#define MSIC_ADCTHERM_MASK	4

#define ADC_CHANLS_MAX		15 /* Number of ADC channels */
#define ADC_LOOP_MAX		(ADC_CHANLS_MAX - 1)

/* ADC channel code values */
#define AUDIO_DETECT_CODE	0x06

/* ADC base addresses */
#define ADC_CHNL_START_ADDR	0x1C5	/* increments by 1 */
#define ADC_DATA_START_ADDR     0x1D4   /* increments by 2 */


/**
 * configure_adc - enables/disables the ADC for conversion
 * @val: zero: disables the ADC non-zero:enables the ADC
 *
 * Enable/Disable the ADC depending on the argument
 *
 * Can sleep
 */
static inline int configure_adc(int val)
{
	int ret;
	struct sc_reg_access sc_access = {0,};


	sc_access.reg_addr = MSIC_ADC1CNTL1;
	ret = sst_sc_reg_access(&sc_access, PMIC_READ, 1);
	if (ret)
		return ret;

	if (val)
		/* Enable and start the ADC */
		sc_access.value |= (MSIC_ADC_ENBL | MSIC_ADC_START);
	else
		/* Just stop the ADC */
		sc_access.value &= (~MSIC_ADC_START);
	sc_access.reg_addr = MSIC_ADC1CNTL1;
	return sst_sc_reg_access(&sc_access, PMIC_WRITE, 1);
}

/**
 * reset_stopbit - sets the stop bit to 0 on the given channel
 * @addr: address of the channel
 *
 * Can sleep
 */
static inline int reset_stopbit(uint16_t addr)
{
	int ret;
	struct sc_reg_access sc_access = {0,};
	sc_access.reg_addr = addr;
	ret = sst_sc_reg_access(&sc_access, PMIC_READ, 1);
	if (ret)
		return ret;
	/* Set the stop bit to zero */
	sc_access.reg_addr = addr;
	sc_access.value = (sc_access.value) & 0xEF;
	return sst_sc_reg_access(&sc_access, PMIC_WRITE, 1);
}

/**
 * find_free_channel - finds an empty channel for conversion
 *
 * If the ADC is not enabled then start using 0th channel
 * itself. Otherwise find an empty channel by looking for a
 * channel in which the stopbit is set to 1. returns the index
 * of the first free channel if succeeds or an error code.
 *
 * Context: can sleep
 *
 */
static inline int find_free_channel(void)
{
	int ret;
	int i;

	struct sc_reg_access sc_access = {0,};

	/* check whether ADC is enabled */
	sc_access.reg_addr = MSIC_ADC1CNTL1;
	ret = sst_sc_reg_access(&sc_access, PMIC_READ, 1);
	if (ret)
		return ret;

	if ((sc_access.value & MSIC_ADC_ENBL) == 0)
		return 0;

	/* ADC is already enabled; Looking for an empty channel */
	for (i = 0; i < ADC_CHANLS_MAX; i++) {

		sc_access.reg_addr = ADC_CHNL_START_ADDR + i;
		ret = sst_sc_reg_access(&sc_access, PMIC_READ, 1);
		if (ret)
			return ret;

		if (sc_access.value & MSIC_STOPBIT_MASK) {
			ret = i;
			break;
		}
	}
	return (ret > ADC_LOOP_MAX) ? (-EINVAL) : ret;
}

/**
 * mid_initialize_adc - initializing the ADC
 * @dev: our device structure
 *
 * Initialize the ADC for reading thermistor values. Can sleep.
 */
static inline int mid_initialize_adc(void)
{
	int base_addr, chnl_addr;
	int ret;
	static int channel_index;
	struct sc_reg_access sc_access = {0,};

	/* Index of the first channel in which the stop bit is set */
	channel_index = find_free_channel();
	if (channel_index < 0) {
		pr_err("No free ADC channels");
		return channel_index;
	}

	base_addr = ADC_CHNL_START_ADDR + channel_index;

	if (!(channel_index == 0 || channel_index == ADC_LOOP_MAX)) {
		/* Reset stop bit for channels other than 0 and 12 */
		ret = reset_stopbit(base_addr);
		if (ret)
			return ret;

		/* Index of the first free channel */
		base_addr++;
		channel_index++;
	}

	/* Since this is the last channel, set the stop bit
	   to 1 by ORing the DIE_SENSOR_CODE with 0x10 */
	sc_access.reg_addr = base_addr;
	sc_access.value = AUDIO_DETECT_CODE | 0x10;
	ret = sst_sc_reg_access(&sc_access, PMIC_WRITE, 1);
	if (ret) {
		pr_err("unable to enable ADC");
		return ret;
	}

	chnl_addr = ADC_DATA_START_ADDR + 2 * channel_index;
	pr_debug("mid_initialize : %x", chnl_addr);
	configure_adc(1);
	return chnl_addr;
}
#endif

