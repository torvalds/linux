// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/sizes.h>

#include "ddk750_reg.h"
#include "ddk750_chip.h"
#include "ddk750_power.h"

#define MHz(x) ((x) * 1000000)

static enum logical_chip_type chip;

enum logical_chip_type sm750_get_chip_type(void)
{
	return chip;
}

void sm750_set_chip_type(unsigned short dev_id, u8 rev_id)
{
	if (dev_id == 0x718) {
		chip = SM718;
	} else if (dev_id == 0x750) {
		chip = SM750;
		/* SM750 and SM750LE are different in their revision ID only. */
		if (rev_id == SM750LE_REVISION_ID) {
			chip = SM750LE;
			pr_info("found sm750le\n");
		}
	} else {
		chip = SM_UNKNOWN;
	}
}

static unsigned int get_mxclk_freq(void)
{
	unsigned int pll_reg;
	unsigned int M, N, OD, POD;

	if (sm750_get_chip_type() == SM750LE)
		return MHz(130);

	pll_reg = peek32(MXCLK_PLL_CTRL);
	M = (pll_reg & PLL_CTRL_M_MASK) >> PLL_CTRL_M_SHIFT;
	N = (pll_reg & PLL_CTRL_N_MASK) >> PLL_CTRL_N_SHIFT;
	OD = (pll_reg & PLL_CTRL_OD_MASK) >> PLL_CTRL_OD_SHIFT;
	POD = (pll_reg & PLL_CTRL_POD_MASK) >> PLL_CTRL_POD_SHIFT;

	return DEFAULT_INPUT_CLOCK * M / N / BIT(OD) / BIT(POD);
}

/*
 * This function set up the main chip clock.
 *
 * Input: Frequency to be set.
 */
static void set_chip_clock(unsigned int frequency)
{
	struct pll_value pll;

	/* Cheok_0509: For SM750LE, the chip clock is fixed. Nothing to set. */
	if (sm750_get_chip_type() == SM750LE)
		return;

	if (frequency) {
		/*
		 * Set up PLL structure to hold the value to be set in clocks.
		 */
		pll.input_freq = DEFAULT_INPUT_CLOCK; /* Defined in CLOCK.H */
		pll.clock_type = MXCLK_PLL;

		/*
		 * Call sm750_calc_pll_value() to fill the other fields
		 * of the PLL structure. Sometimes, the chip cannot set
		 * up the exact clock required by the User.
		 * Return value of sm750_calc_pll_value gives the actual
		 * possible clock.
		 */
		sm750_calc_pll_value(frequency, &pll);

		/* Master Clock Control: MXCLK_PLL */
		poke32(MXCLK_PLL_CTRL, sm750_format_pll_reg(&pll));
	}
}

static void set_memory_clock(unsigned int frequency)
{
	unsigned int reg, divisor;

	/*
	 * Cheok_0509: For SM750LE, the memory clock is fixed.
	 * Nothing to set.
	 */
	if (sm750_get_chip_type() == SM750LE)
		return;

	if (frequency) {
		/*
		 * Set the frequency to the maximum frequency
		 * that the DDR Memory can take which is 336MHz.
		 */
		if (frequency > MHz(336))
			frequency = MHz(336);

		/* Calculate the divisor */
		divisor = DIV_ROUND_CLOSEST(get_mxclk_freq(), frequency);

		/* Set the corresponding divisor in the register. */
		reg = peek32(CURRENT_GATE) & ~CURRENT_GATE_M2XCLK_MASK;
		switch (divisor) {
		default:
		case 1:
			reg |= CURRENT_GATE_M2XCLK_DIV_1;
			break;
		case 2:
			reg |= CURRENT_GATE_M2XCLK_DIV_2;
			break;
		case 3:
			reg |= CURRENT_GATE_M2XCLK_DIV_3;
			break;
		case 4:
			reg |= CURRENT_GATE_M2XCLK_DIV_4;
			break;
		}

		sm750_set_current_gate(reg);
	}
}

/*
 * This function set up the master clock (MCLK).
 *
 * Input: Frequency to be set.
 *
 * NOTE:
 *      The maximum frequency the engine can run is 168MHz.
 */
static void set_master_clock(unsigned int frequency)
{
	unsigned int reg, divisor;

	/*
	 * Cheok_0509: For SM750LE, the memory clock is fixed.
	 * Nothing to set.
	 */
	if (sm750_get_chip_type() == SM750LE)
		return;

	if (frequency) {
		/*
		 * Set the frequency to the maximum frequency
		 * that the SM750 engine can run, which is about 190 MHz.
		 */
		if (frequency > MHz(190))
			frequency = MHz(190);

		/* Calculate the divisor */
		divisor = DIV_ROUND_CLOSEST(get_mxclk_freq(), frequency);

		/* Set the corresponding divisor in the register. */
		reg = peek32(CURRENT_GATE) & ~CURRENT_GATE_MCLK_MASK;
		switch (divisor) {
		default:
		case 3:
			reg |= CURRENT_GATE_MCLK_DIV_3;
			break;
		case 4:
			reg |= CURRENT_GATE_MCLK_DIV_4;
			break;
		case 6:
			reg |= CURRENT_GATE_MCLK_DIV_6;
			break;
		case 8:
			reg |= CURRENT_GATE_MCLK_DIV_8;
			break;
		}

		sm750_set_current_gate(reg);
	}
}

unsigned int ddk750_get_vm_size(void)
{
	unsigned int reg;
	unsigned int data;

	/* sm750le only use 64 mb memory*/
	if (sm750_get_chip_type() == SM750LE)
		return SZ_64M;

	/* for 750,always use power mode0*/
	reg = peek32(MODE0_GATE);
	reg |= MODE0_GATE_GPIO;
	poke32(MODE0_GATE, reg);

	/* get frame buffer size from GPIO */
	reg = peek32(MISC_CTRL) & MISC_CTRL_LOCALMEM_SIZE_MASK;
	switch (reg) {
	case MISC_CTRL_LOCALMEM_SIZE_8M:
		data = SZ_8M;  break; /* 8  Mega byte */
	case MISC_CTRL_LOCALMEM_SIZE_16M:
		data = SZ_16M; break; /* 16 Mega byte */
	case MISC_CTRL_LOCALMEM_SIZE_32M:
		data = SZ_32M; break; /* 32 Mega byte */
	case MISC_CTRL_LOCALMEM_SIZE_64M:
		data = SZ_64M; break; /* 64 Mega byte */
	default:
		data = 0;
		break;
	}
	return data;
}

int ddk750_init_hw(struct initchip_param *p_init_param)
{
	unsigned int reg;

	if (p_init_param->power_mode != 0)
		p_init_param->power_mode = 0;
	sm750_set_power_mode(p_init_param->power_mode);

	/* Enable display power gate & LOCALMEM power gate*/
	reg = peek32(CURRENT_GATE);
	reg |= (CURRENT_GATE_DISPLAY | CURRENT_GATE_LOCALMEM);
	sm750_set_current_gate(reg);

	if (sm750_get_chip_type() != SM750LE) {
		/* set panel pll and graphic mode via mmio_88 */
		reg = peek32(VGA_CONFIGURATION);
		reg |= (VGA_CONFIGURATION_PLL | VGA_CONFIGURATION_MODE);
		poke32(VGA_CONFIGURATION, reg);
	} else {
#if defined(__i386__) || defined(__x86_64__)
		/* set graphic mode via IO method */
		outb_p(0x88, 0x3d4);
		outb_p(0x06, 0x3d5);
#endif
	}

	/* Set the Main Chip Clock */
	set_chip_clock(MHz((unsigned int)p_init_param->chip_clock));

	/* Set up memory clock. */
	set_memory_clock(MHz(p_init_param->mem_clock));

	/* Set up master clock */
	set_master_clock(MHz(p_init_param->master_clock));

	/*
	 * Reset the memory controller.
	 * If the memory controller is not reset in SM750,
	 * the system might hang when sw accesses the memory.
	 * The memory should be resetted after changing the MXCLK.
	 */
	if (p_init_param->reset_memory == 1) {
		reg = peek32(MISC_CTRL);
		reg &= ~MISC_CTRL_LOCALMEM_RESET;
		poke32(MISC_CTRL, reg);

		reg |= MISC_CTRL_LOCALMEM_RESET;
		poke32(MISC_CTRL, reg);
	}

	if (p_init_param->set_all_eng_off == 1) {
		sm750_enable_2d_engine(0);

		/* Disable Overlay, if a former application left it on */
		reg = peek32(VIDEO_DISPLAY_CTRL);
		reg &= ~DISPLAY_CTRL_PLANE;
		poke32(VIDEO_DISPLAY_CTRL, reg);

		/* Disable video alpha, if a former application left it on */
		reg = peek32(VIDEO_ALPHA_DISPLAY_CTRL);
		reg &= ~DISPLAY_CTRL_PLANE;
		poke32(VIDEO_ALPHA_DISPLAY_CTRL, reg);

		/* Disable alpha plane, if a former application left it on */
		reg = peek32(ALPHA_DISPLAY_CTRL);
		reg &= ~DISPLAY_CTRL_PLANE;
		poke32(ALPHA_DISPLAY_CTRL, reg);

		/* Disable DMA Channel, if a former application left it on */
		reg = peek32(DMA_ABORT_INTERRUPT);
		reg |= DMA_ABORT_INTERRUPT_ABORT_1;
		poke32(DMA_ABORT_INTERRUPT, reg);

		/* Disable DMA Power, if a former application left it on */
		sm750_enable_dma(0);
	}

	/* We can add more initialization as needed. */

	return 0;
}

/*
 * monk liu @ 4/6/2011:
 *	re-write the calculatePLL function of ddk750.
 *	the original version function does not use
 *	some mathematics tricks and shortcut
 *	when it doing the calculation of the best N,M,D combination
 *	I think this version gives a little upgrade in speed
 *
 * 750 pll clock formular:
 * Request Clock = (Input Clock * M )/(N * X)
 *
 * Input Clock = 14318181 hz
 * X = 2 power D
 * D ={0,1,2,3,4,5,6}
 * M = {1,...,255}
 * N = {2,...,15}
 */
unsigned int sm750_calc_pll_value(unsigned int request_orig,
				  struct pll_value *pll)
{
	/*
	 * as sm750 register definition,
	 * N located in 2,15 and M located in 1,255
	 */
	int N, M, X, d;
	int mini_diff;
	unsigned int RN, quo, rem, fl_quo;
	unsigned int input, request;
	unsigned int tmp_clock, ret;
	const int max_OD = 3;
	int max_d = 6;

	if (sm750_get_chip_type() == SM750LE) {
		/*
		 * SM750LE don't have
		 * programmable PLL and M/N values to work on.
		 * Just return the requested clock.
		 */
		return request_orig;
	}

	ret = 0;
	mini_diff = ~0;
	request = request_orig / 1000;
	input = pll->input_freq / 1000;

	/*
	 * for MXCLK register,
	 * no POD provided, so need be treated differently
	 */
	if (pll->clock_type == MXCLK_PLL)
		max_d = 3;

	for (N = 15; N > 1; N--) {
		/*
		 * RN will not exceed maximum long
		 * if @request <= 285 MHZ (for 32bit cpu)
		 */
		RN = N * request;
		quo = RN / input;
		rem = RN % input;/* rem always small than 14318181 */
		fl_quo = rem * 10000 / input;

		for (d = max_d; d >= 0; d--) {
			X = BIT(d);
			M = quo * X;
			M += fl_quo * X / 10000;
			/* round step */
			M += (fl_quo * X % 10000) > 5000 ? 1 : 0;
			if (M < 256 && M > 0) {
				unsigned int diff;

				tmp_clock = pll->input_freq * M / N / X;
				diff = abs(tmp_clock - request_orig);
				if (diff < mini_diff) {
					pll->M = M;
					pll->N = N;
					pll->POD = 0;
					if (d > max_OD)
						pll->POD = d - max_OD;
					pll->OD = d - pll->POD;
					mini_diff = diff;
					ret = tmp_clock;
				}
			}
		}
	}
	return ret;
}

unsigned int sm750_format_pll_reg(struct pll_value *p_PLL)
{
#ifndef VALIDATION_CHIP
	unsigned int POD = p_PLL->POD;
#endif
	unsigned int OD = p_PLL->OD;
	unsigned int M = p_PLL->M;
	unsigned int N = p_PLL->N;

	/*
	 * Note that all PLL's have the same format. Here, we just use
	 * Panel PLL parameter to work out the bit fields in the
	 * register. On returning a 32 bit number, the value can be
	 * applied to any PLL in the calling function.
	 */
	return PLL_CTRL_POWER |
#ifndef VALIDATION_CHIP
		((POD << PLL_CTRL_POD_SHIFT) & PLL_CTRL_POD_MASK) |
#endif
		((OD << PLL_CTRL_OD_SHIFT) & PLL_CTRL_OD_MASK) |
		((N << PLL_CTRL_N_SHIFT) & PLL_CTRL_N_MASK) |
		((M << PLL_CTRL_M_SHIFT) & PLL_CTRL_M_MASK);
}
