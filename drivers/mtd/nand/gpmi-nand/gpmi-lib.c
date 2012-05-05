/*
 * Freescale GPMI NAND Flash Driver
 *
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <linux/mtd/gpmi-nand.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include "gpmi-nand.h"
#include "gpmi-regs.h"
#include "bch-regs.h"

struct timing_threshod timing_default_threshold = {
	.max_data_setup_cycles       = (BM_GPMI_TIMING0_DATA_SETUP >>
						BP_GPMI_TIMING0_DATA_SETUP),
	.internal_data_setup_in_ns   = 0,
	.max_sample_delay_factor     = (BM_GPMI_CTRL1_RDN_DELAY >>
						BP_GPMI_CTRL1_RDN_DELAY),
	.max_dll_clock_period_in_ns  = 32,
	.max_dll_delay_in_ns         = 16,
};

#define MXS_SET_ADDR		0x4
#define MXS_CLR_ADDR		0x8
/*
 * Clear the bit and poll it cleared.  This is usually called with
 * a reset address and mask being either SFTRST(bit 31) or CLKGATE
 * (bit 30).
 */
static int clear_poll_bit(void __iomem *addr, u32 mask)
{
	int timeout = 0x400;

	/* clear the bit */
	writel(mask, addr + MXS_CLR_ADDR);

	/*
	 * SFTRST needs 3 GPMI clocks to settle, the reference manual
	 * recommends to wait 1us.
	 */
	udelay(1);

	/* poll the bit becoming clear */
	while ((readl(addr) & mask) && --timeout)
		/* nothing */;

	return !timeout;
}

#define MODULE_CLKGATE		(1 << 30)
#define MODULE_SFTRST		(1 << 31)
/*
 * The current mxs_reset_block() will do two things:
 *  [1] enable the module.
 *  [2] reset the module.
 *
 * In most of the cases, it's ok.
 * But in MX23, there is a hardware bug in the BCH block (see erratum #2847).
 * If you try to soft reset the BCH block, it becomes unusable until
 * the next hard reset. This case occurs in the NAND boot mode. When the board
 * boots by NAND, the ROM of the chip will initialize the BCH blocks itself.
 * So If the driver tries to reset the BCH again, the BCH will not work anymore.
 * You will see a DMA timeout in this case. The bug has been fixed
 * in the following chips, such as MX28.
 *
 * To avoid this bug, just add a new parameter `just_enable` for
 * the mxs_reset_block(), and rewrite it here.
 */
static int gpmi_reset_block(void __iomem *reset_addr, bool just_enable)
{
	int ret;
	int timeout = 0x400;

	/* clear and poll SFTRST */
	ret = clear_poll_bit(reset_addr, MODULE_SFTRST);
	if (unlikely(ret))
		goto error;

	/* clear CLKGATE */
	writel(MODULE_CLKGATE, reset_addr + MXS_CLR_ADDR);

	if (!just_enable) {
		/* set SFTRST to reset the block */
		writel(MODULE_SFTRST, reset_addr + MXS_SET_ADDR);
		udelay(1);

		/* poll CLKGATE becoming set */
		while ((!(readl(reset_addr) & MODULE_CLKGATE)) && --timeout)
			/* nothing */;
		if (unlikely(!timeout))
			goto error;
	}

	/* clear and poll SFTRST */
	ret = clear_poll_bit(reset_addr, MODULE_SFTRST);
	if (unlikely(ret))
		goto error;

	/* clear and poll CLKGATE */
	ret = clear_poll_bit(reset_addr, MODULE_CLKGATE);
	if (unlikely(ret))
		goto error;

	return 0;

error:
	pr_err("%s(%p): module reset timeout\n", __func__, reset_addr);
	return -ETIMEDOUT;
}

int gpmi_init(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	int ret;

	ret = clk_prepare_enable(r->clock);
	if (ret)
		goto err_out;
	ret = gpmi_reset_block(r->gpmi_regs, false);
	if (ret)
		goto err_out;

	/* Choose NAND mode. */
	writel(BM_GPMI_CTRL1_GPMI_MODE, r->gpmi_regs + HW_GPMI_CTRL1_CLR);

	/* Set the IRQ polarity. */
	writel(BM_GPMI_CTRL1_ATA_IRQRDY_POLARITY,
				r->gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Disable Write-Protection. */
	writel(BM_GPMI_CTRL1_DEV_RESET, r->gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Select BCH ECC. */
	writel(BM_GPMI_CTRL1_BCH_MODE, r->gpmi_regs + HW_GPMI_CTRL1_SET);

	clk_disable_unprepare(r->clock);
	return 0;
err_out:
	return ret;
}

/* This function is very useful. It is called only when the bug occur. */
void gpmi_dump_info(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	struct bch_geometry *geo = &this->bch_geometry;
	u32 reg;
	int i;

	pr_err("Show GPMI registers :\n");
	for (i = 0; i <= HW_GPMI_DEBUG / 0x10 + 1; i++) {
		reg = readl(r->gpmi_regs + i * 0x10);
		pr_err("offset 0x%.3x : 0x%.8x\n", i * 0x10, reg);
	}

	/* start to print out the BCH info */
	pr_err("BCH Geometry :\n");
	pr_err("GF length              : %u\n", geo->gf_len);
	pr_err("ECC Strength           : %u\n", geo->ecc_strength);
	pr_err("Page Size in Bytes     : %u\n", geo->page_size);
	pr_err("Metadata Size in Bytes : %u\n", geo->metadata_size);
	pr_err("ECC Chunk Size in Bytes: %u\n", geo->ecc_chunk_size);
	pr_err("ECC Chunk Count        : %u\n", geo->ecc_chunk_count);
	pr_err("Payload Size in Bytes  : %u\n", geo->payload_size);
	pr_err("Auxiliary Size in Bytes: %u\n", geo->auxiliary_size);
	pr_err("Auxiliary Status Offset: %u\n", geo->auxiliary_status_offset);
	pr_err("Block Mark Byte Offset : %u\n", geo->block_mark_byte_offset);
	pr_err("Block Mark Bit Offset  : %u\n", geo->block_mark_bit_offset);
}

/* Configures the geometry for BCH.  */
int bch_set_geometry(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	struct bch_geometry *bch_geo = &this->bch_geometry;
	unsigned int block_count;
	unsigned int block_size;
	unsigned int metadata_size;
	unsigned int ecc_strength;
	unsigned int page_size;
	int ret;

	if (common_nfc_set_geometry(this))
		return !0;

	block_count   = bch_geo->ecc_chunk_count - 1;
	block_size    = bch_geo->ecc_chunk_size;
	metadata_size = bch_geo->metadata_size;
	ecc_strength  = bch_geo->ecc_strength >> 1;
	page_size     = bch_geo->page_size;

	ret = clk_prepare_enable(r->clock);
	if (ret)
		goto err_out;

	/*
	* Due to erratum #2847 of the MX23, the BCH cannot be soft reset on this
	* chip, otherwise it will lock up. So we skip resetting BCH on the MX23.
	* On the other hand, the MX28 needs the reset, because one case has been
	* seen where the BCH produced ECC errors constantly after 10000
	* consecutive reboots. The latter case has not been seen on the MX23 yet,
	* still we don't know if it could happen there as well.
	*/
	ret = gpmi_reset_block(r->bch_regs, GPMI_IS_MX23(this));
	if (ret)
		goto err_out;

	/* Configure layout 0. */
	writel(BF_BCH_FLASH0LAYOUT0_NBLOCKS(block_count)
			| BF_BCH_FLASH0LAYOUT0_META_SIZE(metadata_size)
			| BF_BCH_FLASH0LAYOUT0_ECC0(ecc_strength)
			| BF_BCH_FLASH0LAYOUT0_DATA0_SIZE(block_size),
			r->bch_regs + HW_BCH_FLASH0LAYOUT0);

	writel(BF_BCH_FLASH0LAYOUT1_PAGE_SIZE(page_size)
			| BF_BCH_FLASH0LAYOUT1_ECCN(ecc_strength)
			| BF_BCH_FLASH0LAYOUT1_DATAN_SIZE(block_size),
			r->bch_regs + HW_BCH_FLASH0LAYOUT1);

	/* Set *all* chip selects to use layout 0. */
	writel(0, r->bch_regs + HW_BCH_LAYOUTSELECT);

	/* Enable interrupts. */
	writel(BM_BCH_CTRL_COMPLETE_IRQ_EN,
				r->bch_regs + HW_BCH_CTRL_SET);

	clk_disable_unprepare(r->clock);
	return 0;
err_out:
	return ret;
}

/* Converts time in nanoseconds to cycles. */
static unsigned int ns_to_cycles(unsigned int time,
			unsigned int period, unsigned int min)
{
	unsigned int k;

	k = (time + period - 1) / period;
	return max(k, min);
}

#define DEF_MIN_PROP_DELAY	5
#define DEF_MAX_PROP_DELAY	9
/* Apply timing to current hardware conditions. */
static int gpmi_nfc_compute_hardware_timing(struct gpmi_nand_data *this,
					struct gpmi_nfc_hardware_timing *hw)
{
	struct timing_threshod *nfc = &timing_default_threshold;
	struct nand_chip *nand = &this->nand;
	struct nand_timing target = this->timing;
	bool improved_timing_is_available;
	unsigned long clock_frequency_in_hz;
	unsigned int clock_period_in_ns;
	bool dll_use_half_periods;
	unsigned int dll_delay_shift;
	unsigned int max_sample_delay_in_ns;
	unsigned int address_setup_in_cycles;
	unsigned int data_setup_in_ns;
	unsigned int data_setup_in_cycles;
	unsigned int data_hold_in_cycles;
	int ideal_sample_delay_in_ns;
	unsigned int sample_delay_factor;
	int tEYE;
	unsigned int min_prop_delay_in_ns = DEF_MIN_PROP_DELAY;
	unsigned int max_prop_delay_in_ns = DEF_MAX_PROP_DELAY;

	/*
	 * If there are multiple chips, we need to relax the timings to allow
	 * for signal distortion due to higher capacitance.
	 */
	if (nand->numchips > 2) {
		target.data_setup_in_ns    += 10;
		target.data_hold_in_ns     += 10;
		target.address_setup_in_ns += 10;
	} else if (nand->numchips > 1) {
		target.data_setup_in_ns    += 5;
		target.data_hold_in_ns     += 5;
		target.address_setup_in_ns += 5;
	}

	/* Check if improved timing information is available. */
	improved_timing_is_available =
		(target.tREA_in_ns  >= 0) &&
		(target.tRLOH_in_ns >= 0) &&
		(target.tRHOH_in_ns >= 0) ;

	/* Inspect the clock. */
	clock_frequency_in_hz = nfc->clock_frequency_in_hz;
	clock_period_in_ns    = 1000000000 / clock_frequency_in_hz;

	/*
	 * The NFC quantizes setup and hold parameters in terms of clock cycles.
	 * Here, we quantize the setup and hold timing parameters to the
	 * next-highest clock period to make sure we apply at least the
	 * specified times.
	 *
	 * For data setup and data hold, the hardware interprets a value of zero
	 * as the largest possible delay. This is not what's intended by a zero
	 * in the input parameter, so we impose a minimum of one cycle.
	 */
	data_setup_in_cycles    = ns_to_cycles(target.data_setup_in_ns,
							clock_period_in_ns, 1);
	data_hold_in_cycles     = ns_to_cycles(target.data_hold_in_ns,
							clock_period_in_ns, 1);
	address_setup_in_cycles = ns_to_cycles(target.address_setup_in_ns,
							clock_period_in_ns, 0);

	/*
	 * The clock's period affects the sample delay in a number of ways:
	 *
	 * (1) The NFC HAL tells us the maximum clock period the sample delay
	 *     DLL can tolerate. If the clock period is greater than half that
	 *     maximum, we must configure the DLL to be driven by half periods.
	 *
	 * (2) We need to convert from an ideal sample delay, in ns, to a
	 *     "sample delay factor," which the NFC uses. This factor depends on
	 *     whether we're driving the DLL with full or half periods.
	 *     Paraphrasing the reference manual:
	 *
	 *         AD = SDF x 0.125 x RP
	 *
	 * where:
	 *
	 *     AD   is the applied delay, in ns.
	 *     SDF  is the sample delay factor, which is dimensionless.
	 *     RP   is the reference period, in ns, which is a full clock period
	 *          if the DLL is being driven by full periods, or half that if
	 *          the DLL is being driven by half periods.
	 *
	 * Let's re-arrange this in a way that's more useful to us:
	 *
	 *                        8
	 *         SDF  =  AD x ----
	 *                       RP
	 *
	 * The reference period is either the clock period or half that, so this
	 * is:
	 *
	 *                        8       AD x DDF
	 *         SDF  =  AD x -----  =  --------
	 *                      f x P        P
	 *
	 * where:
	 *
	 *       f  is 1 or 1/2, depending on how we're driving the DLL.
	 *       P  is the clock period.
	 *     DDF  is the DLL Delay Factor, a dimensionless value that
	 *          incorporates all the constants in the conversion.
	 *
	 * DDF will be either 8 or 16, both of which are powers of two. We can
	 * reduce the cost of this conversion by using bit shifts instead of
	 * multiplication or division. Thus:
	 *
	 *                 AD << DDS
	 *         SDF  =  ---------
	 *                     P
	 *
	 *     or
	 *
	 *         AD  =  (SDF >> DDS) x P
	 *
	 * where:
	 *
	 *     DDS  is the DLL Delay Shift, the logarithm to base 2 of the DDF.
	 */
	if (clock_period_in_ns > (nfc->max_dll_clock_period_in_ns >> 1)) {
		dll_use_half_periods = true;
		dll_delay_shift      = 3 + 1;
	} else {
		dll_use_half_periods = false;
		dll_delay_shift      = 3;
	}

	/*
	 * Compute the maximum sample delay the NFC allows, under current
	 * conditions. If the clock is running too slowly, no sample delay is
	 * possible.
	 */
	if (clock_period_in_ns > nfc->max_dll_clock_period_in_ns)
		max_sample_delay_in_ns = 0;
	else {
		/*
		 * Compute the delay implied by the largest sample delay factor
		 * the NFC allows.
		 */
		max_sample_delay_in_ns =
			(nfc->max_sample_delay_factor * clock_period_in_ns) >>
								dll_delay_shift;

		/*
		 * Check if the implied sample delay larger than the NFC
		 * actually allows.
		 */
		if (max_sample_delay_in_ns > nfc->max_dll_delay_in_ns)
			max_sample_delay_in_ns = nfc->max_dll_delay_in_ns;
	}

	/*
	 * Check if improved timing information is available. If not, we have to
	 * use a less-sophisticated algorithm.
	 */
	if (!improved_timing_is_available) {
		/*
		 * Fold the read setup time required by the NFC into the ideal
		 * sample delay.
		 */
		ideal_sample_delay_in_ns = target.gpmi_sample_delay_in_ns +
						nfc->internal_data_setup_in_ns;

		/*
		 * The ideal sample delay may be greater than the maximum
		 * allowed by the NFC. If so, we can trade off sample delay time
		 * for more data setup time.
		 *
		 * In each iteration of the following loop, we add a cycle to
		 * the data setup time and subtract a corresponding amount from
		 * the sample delay until we've satisified the constraints or
		 * can't do any better.
		 */
		while ((ideal_sample_delay_in_ns > max_sample_delay_in_ns) &&
			(data_setup_in_cycles < nfc->max_data_setup_cycles)) {

			data_setup_in_cycles++;
			ideal_sample_delay_in_ns -= clock_period_in_ns;

			if (ideal_sample_delay_in_ns < 0)
				ideal_sample_delay_in_ns = 0;

		}

		/*
		 * Compute the sample delay factor that corresponds most closely
		 * to the ideal sample delay. If the result is too large for the
		 * NFC, use the maximum value.
		 *
		 * Notice that we use the ns_to_cycles function to compute the
		 * sample delay factor. We do this because the form of the
		 * computation is the same as that for calculating cycles.
		 */
		sample_delay_factor =
			ns_to_cycles(
				ideal_sample_delay_in_ns << dll_delay_shift,
							clock_period_in_ns, 0);

		if (sample_delay_factor > nfc->max_sample_delay_factor)
			sample_delay_factor = nfc->max_sample_delay_factor;

		/* Skip to the part where we return our results. */
		goto return_results;
	}

	/*
	 * If control arrives here, we have more detailed timing information,
	 * so we can use a better algorithm.
	 */

	/*
	 * Fold the read setup time required by the NFC into the maximum
	 * propagation delay.
	 */
	max_prop_delay_in_ns += nfc->internal_data_setup_in_ns;

	/*
	 * Earlier, we computed the number of clock cycles required to satisfy
	 * the data setup time. Now, we need to know the actual nanoseconds.
	 */
	data_setup_in_ns = clock_period_in_ns * data_setup_in_cycles;

	/*
	 * Compute tEYE, the width of the data eye when reading from the NAND
	 * Flash. The eye width is fundamentally determined by the data setup
	 * time, perturbed by propagation delays and some characteristics of the
	 * NAND Flash device.
	 *
	 * start of the eye = max_prop_delay + tREA
	 * end of the eye   = min_prop_delay + tRHOH + data_setup
	 */
	tEYE = (int)min_prop_delay_in_ns + (int)target.tRHOH_in_ns +
							(int)data_setup_in_ns;

	tEYE -= (int)max_prop_delay_in_ns + (int)target.tREA_in_ns;

	/*
	 * The eye must be open. If it's not, we can try to open it by
	 * increasing its main forcer, the data setup time.
	 *
	 * In each iteration of the following loop, we increase the data setup
	 * time by a single clock cycle. We do this until either the eye is
	 * open or we run into NFC limits.
	 */
	while ((tEYE <= 0) &&
			(data_setup_in_cycles < nfc->max_data_setup_cycles)) {
		/* Give a cycle to data setup. */
		data_setup_in_cycles++;
		/* Synchronize the data setup time with the cycles. */
		data_setup_in_ns += clock_period_in_ns;
		/* Adjust tEYE accordingly. */
		tEYE += clock_period_in_ns;
	}

	/*
	 * When control arrives here, the eye is open. The ideal time to sample
	 * the data is in the center of the eye:
	 *
	 *     end of the eye + start of the eye
	 *     ---------------------------------  -  data_setup
	 *                    2
	 *
	 * After some algebra, this simplifies to the code immediately below.
	 */
	ideal_sample_delay_in_ns =
		((int)max_prop_delay_in_ns +
			(int)target.tREA_in_ns +
				(int)min_prop_delay_in_ns +
					(int)target.tRHOH_in_ns -
						(int)data_setup_in_ns) >> 1;

	/*
	 * The following figure illustrates some aspects of a NAND Flash read:
	 *
	 *
	 *           __                   _____________________________________
	 * RDN         \_________________/
	 *
	 *                                         <---- tEYE ----->
	 *                                        /-----------------\
	 * Read Data ----------------------------<                   >---------
	 *                                        \-----------------/
	 *             ^                 ^                 ^              ^
	 *             |                 |                 |              |
	 *             |<--Data Setup -->|<--Delay Time -->|              |
	 *             |                 |                 |              |
	 *             |                 |                                |
	 *             |                 |<--   Quantized Delay Time   -->|
	 *             |                 |                                |
	 *
	 *
	 * We have some issues we must now address:
	 *
	 * (1) The *ideal* sample delay time must not be negative. If it is, we
	 *     jam it to zero.
	 *
	 * (2) The *ideal* sample delay time must not be greater than that
	 *     allowed by the NFC. If it is, we can increase the data setup
	 *     time, which will reduce the delay between the end of the data
	 *     setup and the center of the eye. It will also make the eye
	 *     larger, which might help with the next issue...
	 *
	 * (3) The *quantized* sample delay time must not fall either before the
	 *     eye opens or after it closes (the latter is the problem
	 *     illustrated in the above figure).
	 */

	/* Jam a negative ideal sample delay to zero. */
	if (ideal_sample_delay_in_ns < 0)
		ideal_sample_delay_in_ns = 0;

	/*
	 * Extend the data setup as needed to reduce the ideal sample delay
	 * below the maximum permitted by the NFC.
	 */
	while ((ideal_sample_delay_in_ns > max_sample_delay_in_ns) &&
			(data_setup_in_cycles < nfc->max_data_setup_cycles)) {

		/* Give a cycle to data setup. */
		data_setup_in_cycles++;
		/* Synchronize the data setup time with the cycles. */
		data_setup_in_ns += clock_period_in_ns;
		/* Adjust tEYE accordingly. */
		tEYE += clock_period_in_ns;

		/*
		 * Decrease the ideal sample delay by one half cycle, to keep it
		 * in the middle of the eye.
		 */
		ideal_sample_delay_in_ns -= (clock_period_in_ns >> 1);

		/* Jam a negative ideal sample delay to zero. */
		if (ideal_sample_delay_in_ns < 0)
			ideal_sample_delay_in_ns = 0;
	}

	/*
	 * Compute the sample delay factor that corresponds to the ideal sample
	 * delay. If the result is too large, then use the maximum allowed
	 * value.
	 *
	 * Notice that we use the ns_to_cycles function to compute the sample
	 * delay factor. We do this because the form of the computation is the
	 * same as that for calculating cycles.
	 */
	sample_delay_factor =
		ns_to_cycles(ideal_sample_delay_in_ns << dll_delay_shift,
							clock_period_in_ns, 0);

	if (sample_delay_factor > nfc->max_sample_delay_factor)
		sample_delay_factor = nfc->max_sample_delay_factor;

	/*
	 * These macros conveniently encapsulate a computation we'll use to
	 * continuously evaluate whether or not the data sample delay is inside
	 * the eye.
	 */
	#define IDEAL_DELAY  ((int) ideal_sample_delay_in_ns)

	#define QUANTIZED_DELAY  \
		((int) ((sample_delay_factor * clock_period_in_ns) >> \
							dll_delay_shift))

	#define DELAY_ERROR  (abs(QUANTIZED_DELAY - IDEAL_DELAY))

	#define SAMPLE_IS_NOT_WITHIN_THE_EYE  (DELAY_ERROR > (tEYE >> 1))

	/*
	 * While the quantized sample time falls outside the eye, reduce the
	 * sample delay or extend the data setup to move the sampling point back
	 * toward the eye. Do not allow the number of data setup cycles to
	 * exceed the maximum allowed by the NFC.
	 */
	while (SAMPLE_IS_NOT_WITHIN_THE_EYE &&
			(data_setup_in_cycles < nfc->max_data_setup_cycles)) {
		/*
		 * If control arrives here, the quantized sample delay falls
		 * outside the eye. Check if it's before the eye opens, or after
		 * the eye closes.
		 */
		if (QUANTIZED_DELAY > IDEAL_DELAY) {
			/*
			 * If control arrives here, the quantized sample delay
			 * falls after the eye closes. Decrease the quantized
			 * delay time and then go back to re-evaluate.
			 */
			if (sample_delay_factor != 0)
				sample_delay_factor--;
			continue;
		}

		/*
		 * If control arrives here, the quantized sample delay falls
		 * before the eye opens. Shift the sample point by increasing
		 * data setup time. This will also make the eye larger.
		 */

		/* Give a cycle to data setup. */
		data_setup_in_cycles++;
		/* Synchronize the data setup time with the cycles. */
		data_setup_in_ns += clock_period_in_ns;
		/* Adjust tEYE accordingly. */
		tEYE += clock_period_in_ns;

		/*
		 * Decrease the ideal sample delay by one half cycle, to keep it
		 * in the middle of the eye.
		 */
		ideal_sample_delay_in_ns -= (clock_period_in_ns >> 1);

		/* ...and one less period for the delay time. */
		ideal_sample_delay_in_ns -= clock_period_in_ns;

		/* Jam a negative ideal sample delay to zero. */
		if (ideal_sample_delay_in_ns < 0)
			ideal_sample_delay_in_ns = 0;

		/*
		 * We have a new ideal sample delay, so re-compute the quantized
		 * delay.
		 */
		sample_delay_factor =
			ns_to_cycles(
				ideal_sample_delay_in_ns << dll_delay_shift,
							clock_period_in_ns, 0);

		if (sample_delay_factor > nfc->max_sample_delay_factor)
			sample_delay_factor = nfc->max_sample_delay_factor;
	}

	/* Control arrives here when we're ready to return our results. */
return_results:
	hw->data_setup_in_cycles    = data_setup_in_cycles;
	hw->data_hold_in_cycles     = data_hold_in_cycles;
	hw->address_setup_in_cycles = address_setup_in_cycles;
	hw->use_half_periods        = dll_use_half_periods;
	hw->sample_delay_factor     = sample_delay_factor;

	/* Return success. */
	return 0;
}

/* Begin the I/O */
void gpmi_begin(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	struct timing_threshod *nfc = &timing_default_threshold;
	unsigned char  *gpmi_regs = r->gpmi_regs;
	unsigned int   clock_period_in_ns;
	uint32_t       reg;
	unsigned int   dll_wait_time_in_us;
	struct gpmi_nfc_hardware_timing  hw;
	int ret;

	/* Enable the clock. */
	ret = clk_prepare_enable(r->clock);
	if (ret) {
		pr_err("We failed in enable the clk\n");
		goto err_out;
	}

	/* set ready/busy timeout */
	writel(0x500 << BP_GPMI_TIMING1_BUSY_TIMEOUT,
		gpmi_regs + HW_GPMI_TIMING1);

	/* Get the timing information we need. */
	nfc->clock_frequency_in_hz = clk_get_rate(r->clock);
	clock_period_in_ns = 1000000000 / nfc->clock_frequency_in_hz;

	gpmi_nfc_compute_hardware_timing(this, &hw);

	/* Set up all the simple timing parameters. */
	reg = BF_GPMI_TIMING0_ADDRESS_SETUP(hw.address_setup_in_cycles) |
		BF_GPMI_TIMING0_DATA_HOLD(hw.data_hold_in_cycles)         |
		BF_GPMI_TIMING0_DATA_SETUP(hw.data_setup_in_cycles)       ;

	writel(reg, gpmi_regs + HW_GPMI_TIMING0);

	/*
	 * DLL_ENABLE must be set to 0 when setting RDN_DELAY or HALF_PERIOD.
	 */
	writel(BM_GPMI_CTRL1_DLL_ENABLE, gpmi_regs + HW_GPMI_CTRL1_CLR);

	/* Clear out the DLL control fields. */
	writel(BM_GPMI_CTRL1_RDN_DELAY,   gpmi_regs + HW_GPMI_CTRL1_CLR);
	writel(BM_GPMI_CTRL1_HALF_PERIOD, gpmi_regs + HW_GPMI_CTRL1_CLR);

	/* If no sample delay is called for, return immediately. */
	if (!hw.sample_delay_factor)
		return;

	/* Configure the HALF_PERIOD flag. */
	if (hw.use_half_periods)
		writel(BM_GPMI_CTRL1_HALF_PERIOD,
						gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Set the delay factor. */
	writel(BF_GPMI_CTRL1_RDN_DELAY(hw.sample_delay_factor),
						gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Enable the DLL. */
	writel(BM_GPMI_CTRL1_DLL_ENABLE, gpmi_regs + HW_GPMI_CTRL1_SET);

	/*
	 * After we enable the GPMI DLL, we have to wait 64 clock cycles before
	 * we can use the GPMI.
	 *
	 * Calculate the amount of time we need to wait, in microseconds.
	 */
	dll_wait_time_in_us = (clock_period_in_ns * 64) / 1000;

	if (!dll_wait_time_in_us)
		dll_wait_time_in_us = 1;

	/* Wait for the DLL to settle. */
	udelay(dll_wait_time_in_us);

err_out:
	return;
}

void gpmi_end(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	clk_disable_unprepare(r->clock);
}

/* Clears a BCH interrupt. */
void gpmi_clear_bch(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	writel(BM_BCH_CTRL_COMPLETE_IRQ, r->bch_regs + HW_BCH_CTRL_CLR);
}

/* Returns the Ready/Busy status of the given chip. */
int gpmi_is_ready(struct gpmi_nand_data *this, unsigned chip)
{
	struct resources *r = &this->resources;
	uint32_t mask = 0;
	uint32_t reg = 0;

	if (GPMI_IS_MX23(this)) {
		mask = MX23_BM_GPMI_DEBUG_READY0 << chip;
		reg = readl(r->gpmi_regs + HW_GPMI_DEBUG);
	} else if (GPMI_IS_MX28(this)) {
		mask = MX28_BF_GPMI_STAT_READY_BUSY(1 << chip);
		reg = readl(r->gpmi_regs + HW_GPMI_STAT);
	} else
		pr_err("unknow arch.\n");
	return reg & mask;
}

static inline void set_dma_type(struct gpmi_nand_data *this,
					enum dma_ops_type type)
{
	this->last_dma_type = this->dma_type;
	this->dma_type = type;
}

int gpmi_send_command(struct gpmi_nand_data *this)
{
	struct dma_chan *channel = get_dma_chan(this);
	struct dma_async_tx_descriptor *desc;
	struct scatterlist *sgl;
	int chip = this->current_chip;
	u32 pio[3];

	/* [1] send out the PIO words */
	pio[0] = BF_GPMI_CTRL0_COMMAND_MODE(BV_GPMI_CTRL0_COMMAND_MODE__WRITE)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(BV_GPMI_CTRL0_ADDRESS__NAND_CLE)
		| BM_GPMI_CTRL0_ADDRESS_INCREMENT
		| BF_GPMI_CTRL0_XFER_COUNT(this->command_length);
	pio[1] = pio[2] = 0;
	desc = dmaengine_prep_slave_sg(channel,
					(struct scatterlist *)pio,
					ARRAY_SIZE(pio), DMA_TRANS_NONE, 0);
	if (!desc) {
		pr_err("step 1 error\n");
		return -1;
	}

	/* [2] send out the COMMAND + ADDRESS string stored in @buffer */
	sgl = &this->cmd_sgl;

	sg_init_one(sgl, this->cmd_buffer, this->command_length);
	dma_map_sg(this->dev, sgl, 1, DMA_TO_DEVICE);
	desc = dmaengine_prep_slave_sg(channel,
				sgl, 1, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!desc) {
		pr_err("step 2 error\n");
		return -1;
	}

	/* [3] submit the DMA */
	set_dma_type(this, DMA_FOR_COMMAND);
	return start_dma_without_bch_irq(this, desc);
}

int gpmi_send_data(struct gpmi_nand_data *this)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *channel = get_dma_chan(this);
	int chip = this->current_chip;
	uint32_t command_mode;
	uint32_t address;
	u32 pio[2];

	/* [1] PIO */
	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WRITE;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	pio[0] = BF_GPMI_CTRL0_COMMAND_MODE(command_mode)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(address)
		| BF_GPMI_CTRL0_XFER_COUNT(this->upper_len);
	pio[1] = 0;
	desc = dmaengine_prep_slave_sg(channel, (struct scatterlist *)pio,
					ARRAY_SIZE(pio), DMA_TRANS_NONE, 0);
	if (!desc) {
		pr_err("step 1 error\n");
		return -1;
	}

	/* [2] send DMA request */
	prepare_data_dma(this, DMA_TO_DEVICE);
	desc = dmaengine_prep_slave_sg(channel, &this->data_sgl,
					1, DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		pr_err("step 2 error\n");
		return -1;
	}
	/* [3] submit the DMA */
	set_dma_type(this, DMA_FOR_WRITE_DATA);
	return start_dma_without_bch_irq(this, desc);
}

int gpmi_read_data(struct gpmi_nand_data *this)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *channel = get_dma_chan(this);
	int chip = this->current_chip;
	u32 pio[2];

	/* [1] : send PIO */
	pio[0] = BF_GPMI_CTRL0_COMMAND_MODE(BV_GPMI_CTRL0_COMMAND_MODE__READ)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(BV_GPMI_CTRL0_ADDRESS__NAND_DATA)
		| BF_GPMI_CTRL0_XFER_COUNT(this->upper_len);
	pio[1] = 0;
	desc = dmaengine_prep_slave_sg(channel,
					(struct scatterlist *)pio,
					ARRAY_SIZE(pio), DMA_TRANS_NONE, 0);
	if (!desc) {
		pr_err("step 1 error\n");
		return -1;
	}

	/* [2] : send DMA request */
	prepare_data_dma(this, DMA_FROM_DEVICE);
	desc = dmaengine_prep_slave_sg(channel, &this->data_sgl,
					1, DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		pr_err("step 2 error\n");
		return -1;
	}

	/* [3] : submit the DMA */
	set_dma_type(this, DMA_FOR_READ_DATA);
	return start_dma_without_bch_irq(this, desc);
}

int gpmi_send_page(struct gpmi_nand_data *this,
			dma_addr_t payload, dma_addr_t auxiliary)
{
	struct bch_geometry *geo = &this->bch_geometry;
	uint32_t command_mode;
	uint32_t address;
	uint32_t ecc_command;
	uint32_t buffer_mask;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *channel = get_dma_chan(this);
	int chip = this->current_chip;
	u32 pio[6];

	/* A DMA descriptor that does an ECC page read. */
	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WRITE;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;
	ecc_command  = BV_GPMI_ECCCTRL_ECC_CMD__BCH_ENCODE;
	buffer_mask  = BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_PAGE |
				BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_AUXONLY;

	pio[0] = BF_GPMI_CTRL0_COMMAND_MODE(command_mode)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(address)
		| BF_GPMI_CTRL0_XFER_COUNT(0);
	pio[1] = 0;
	pio[2] = BM_GPMI_ECCCTRL_ENABLE_ECC
		| BF_GPMI_ECCCTRL_ECC_CMD(ecc_command)
		| BF_GPMI_ECCCTRL_BUFFER_MASK(buffer_mask);
	pio[3] = geo->page_size;
	pio[4] = payload;
	pio[5] = auxiliary;

	desc = dmaengine_prep_slave_sg(channel,
					(struct scatterlist *)pio,
					ARRAY_SIZE(pio), DMA_TRANS_NONE,
					DMA_CTRL_ACK);
	if (!desc) {
		pr_err("step 2 error\n");
		return -1;
	}
	set_dma_type(this, DMA_FOR_WRITE_ECC_PAGE);
	return start_dma_with_bch_irq(this, desc);
}

int gpmi_read_page(struct gpmi_nand_data *this,
				dma_addr_t payload, dma_addr_t auxiliary)
{
	struct bch_geometry *geo = &this->bch_geometry;
	uint32_t command_mode;
	uint32_t address;
	uint32_t ecc_command;
	uint32_t buffer_mask;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *channel = get_dma_chan(this);
	int chip = this->current_chip;
	u32 pio[6];

	/* [1] Wait for the chip to report ready. */
	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WAIT_FOR_READY;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	pio[0] =  BF_GPMI_CTRL0_COMMAND_MODE(command_mode)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(address)
		| BF_GPMI_CTRL0_XFER_COUNT(0);
	pio[1] = 0;
	desc = dmaengine_prep_slave_sg(channel,
				(struct scatterlist *)pio, 2,
				DMA_TRANS_NONE, 0);
	if (!desc) {
		pr_err("step 1 error\n");
		return -1;
	}

	/* [2] Enable the BCH block and read. */
	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__READ;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;
	ecc_command  = BV_GPMI_ECCCTRL_ECC_CMD__BCH_DECODE;
	buffer_mask  = BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_PAGE
			| BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_AUXONLY;

	pio[0] =  BF_GPMI_CTRL0_COMMAND_MODE(command_mode)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(address)
		| BF_GPMI_CTRL0_XFER_COUNT(geo->page_size);

	pio[1] = 0;
	pio[2] =  BM_GPMI_ECCCTRL_ENABLE_ECC
		| BF_GPMI_ECCCTRL_ECC_CMD(ecc_command)
		| BF_GPMI_ECCCTRL_BUFFER_MASK(buffer_mask);
	pio[3] = geo->page_size;
	pio[4] = payload;
	pio[5] = auxiliary;
	desc = dmaengine_prep_slave_sg(channel,
					(struct scatterlist *)pio,
					ARRAY_SIZE(pio), DMA_TRANS_NONE,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		pr_err("step 2 error\n");
		return -1;
	}

	/* [3] Disable the BCH block */
	command_mode = BV_GPMI_CTRL0_COMMAND_MODE__WAIT_FOR_READY;
	address      = BV_GPMI_CTRL0_ADDRESS__NAND_DATA;

	pio[0] = BF_GPMI_CTRL0_COMMAND_MODE(command_mode)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(address)
		| BF_GPMI_CTRL0_XFER_COUNT(geo->page_size);
	pio[1] = 0;
	pio[2] = 0; /* clear GPMI_HW_GPMI_ECCCTRL, disable the BCH. */
	desc = dmaengine_prep_slave_sg(channel,
				(struct scatterlist *)pio, 3,
				DMA_TRANS_NONE,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		pr_err("step 3 error\n");
		return -1;
	}

	/* [4] submit the DMA */
	set_dma_type(this, DMA_FOR_READ_ECC_PAGE);
	return start_dma_with_bch_irq(this, desc);
}
