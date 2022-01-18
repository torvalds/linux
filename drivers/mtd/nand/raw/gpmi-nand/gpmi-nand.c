// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale GPMI NAND Flash Driver
 *
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/sched/task_stack.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma/mxs-dma.h>
#include "gpmi-nand.h"
#include "gpmi-regs.h"
#include "bch-regs.h"

/* Resource names for the GPMI NAND driver. */
#define GPMI_NAND_GPMI_REGS_ADDR_RES_NAME  "gpmi-nand"
#define GPMI_NAND_BCH_REGS_ADDR_RES_NAME   "bch"
#define GPMI_NAND_BCH_INTERRUPT_RES_NAME   "bch"

/* Converts time to clock cycles */
#define TO_CYCLES(duration, period) DIV_ROUND_UP_ULL(duration, period)

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

static int __gpmi_enable_clk(struct gpmi_nand_data *this, bool v)
{
	struct clk *clk;
	int ret;
	int i;

	for (i = 0; i < GPMI_CLK_MAX; i++) {
		clk = this->resources.clock[i];
		if (!clk)
			break;

		if (v) {
			ret = clk_prepare_enable(clk);
			if (ret)
				goto err_clk;
		} else {
			clk_disable_unprepare(clk);
		}
	}
	return 0;

err_clk:
	for (; i > 0; i--)
		clk_disable_unprepare(this->resources.clock[i - 1]);
	return ret;
}

static int gpmi_init(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	int ret;

	ret = pm_runtime_get_sync(this->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(this->dev);
		return ret;
	}

	ret = gpmi_reset_block(r->gpmi_regs, false);
	if (ret)
		goto err_out;

	/*
	 * Reset BCH here, too. We got failures otherwise :(
	 * See later BCH reset for explanation of MX23 and MX28 handling
	 */
	ret = gpmi_reset_block(r->bch_regs, GPMI_IS_MXS(this));
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

	/*
	 * Decouple the chip select from dma channel. We use dma0 for all
	 * the chips.
	 */
	writel(BM_GPMI_CTRL1_DECOUPLE_CS, r->gpmi_regs + HW_GPMI_CTRL1_SET);

err_out:
	pm_runtime_mark_last_busy(this->dev);
	pm_runtime_put_autosuspend(this->dev);
	return ret;
}

/* This function is very useful. It is called only when the bug occur. */
static void gpmi_dump_info(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	struct bch_geometry *geo = &this->bch_geometry;
	u32 reg;
	int i;

	dev_err(this->dev, "Show GPMI registers :\n");
	for (i = 0; i <= HW_GPMI_DEBUG / 0x10 + 1; i++) {
		reg = readl(r->gpmi_regs + i * 0x10);
		dev_err(this->dev, "offset 0x%.3x : 0x%.8x\n", i * 0x10, reg);
	}

	/* start to print out the BCH info */
	dev_err(this->dev, "Show BCH registers :\n");
	for (i = 0; i <= HW_BCH_VERSION / 0x10 + 1; i++) {
		reg = readl(r->bch_regs + i * 0x10);
		dev_err(this->dev, "offset 0x%.3x : 0x%.8x\n", i * 0x10, reg);
	}
	dev_err(this->dev, "BCH Geometry :\n"
		"GF length              : %u\n"
		"ECC Strength           : %u\n"
		"Page Size in Bytes     : %u\n"
		"Metadata Size in Bytes : %u\n"
		"ECC Chunk Size in Bytes: %u\n"
		"ECC Chunk Count        : %u\n"
		"Payload Size in Bytes  : %u\n"
		"Auxiliary Size in Bytes: %u\n"
		"Auxiliary Status Offset: %u\n"
		"Block Mark Byte Offset : %u\n"
		"Block Mark Bit Offset  : %u\n",
		geo->gf_len,
		geo->ecc_strength,
		geo->page_size,
		geo->metadata_size,
		geo->ecc_chunk_size,
		geo->ecc_chunk_count,
		geo->payload_size,
		geo->auxiliary_size,
		geo->auxiliary_status_offset,
		geo->block_mark_byte_offset,
		geo->block_mark_bit_offset);
}

static inline bool gpmi_check_ecc(struct gpmi_nand_data *this)
{
	struct bch_geometry *geo = &this->bch_geometry;

	/* Do the sanity check. */
	if (GPMI_IS_MXS(this)) {
		/* The mx23/mx28 only support the GF13. */
		if (geo->gf_len == 14)
			return false;
	}
	return geo->ecc_strength <= this->devdata->bch_max_ecc_strength;
}

/*
 * If we can get the ECC information from the nand chip, we do not
 * need to calculate them ourselves.
 *
 * We may have available oob space in this case.
 */
static int set_geometry_by_ecc_info(struct gpmi_nand_data *this,
				    unsigned int ecc_strength,
				    unsigned int ecc_step)
{
	struct bch_geometry *geo = &this->bch_geometry;
	struct nand_chip *chip = &this->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int block_mark_bit_offset;

	switch (ecc_step) {
	case SZ_512:
		geo->gf_len = 13;
		break;
	case SZ_1K:
		geo->gf_len = 14;
		break;
	default:
		dev_err(this->dev,
			"unsupported nand chip. ecc bits : %d, ecc size : %d\n",
			nanddev_get_ecc_requirements(&chip->base)->strength,
			nanddev_get_ecc_requirements(&chip->base)->step_size);
		return -EINVAL;
	}
	geo->ecc_chunk_size = ecc_step;
	geo->ecc_strength = round_up(ecc_strength, 2);
	if (!gpmi_check_ecc(this))
		return -EINVAL;

	/* Keep the C >= O */
	if (geo->ecc_chunk_size < mtd->oobsize) {
		dev_err(this->dev,
			"unsupported nand chip. ecc size: %d, oob size : %d\n",
			ecc_step, mtd->oobsize);
		return -EINVAL;
	}

	/* The default value, see comment in the legacy_set_geometry(). */
	geo->metadata_size = 10;

	geo->ecc_chunk_count = mtd->writesize / geo->ecc_chunk_size;

	/*
	 * Now, the NAND chip with 2K page(data chunk is 512byte) shows below:
	 *
	 *    |                          P                            |
	 *    |<----------------------------------------------------->|
	 *    |                                                       |
	 *    |                                        (Block Mark)   |
	 *    |                      P'                      |      | |     |
	 *    |<-------------------------------------------->|  D   | |  O' |
	 *    |                                              |<---->| |<--->|
	 *    V                                              V      V V     V
	 *    +---+----------+-+----------+-+----------+-+----------+-+-----+
	 *    | M |   data   |E|   data   |E|   data   |E|   data   |E|     |
	 *    +---+----------+-+----------+-+----------+-+----------+-+-----+
	 *                                                   ^              ^
	 *                                                   |      O       |
	 *                                                   |<------------>|
	 *                                                   |              |
	 *
	 *	P : the page size for BCH module.
	 *	E : The ECC strength.
	 *	G : the length of Galois Field.
	 *	N : The chunk count of per page.
	 *	M : the metasize of per page.
	 *	C : the ecc chunk size, aka the "data" above.
	 *	P': the nand chip's page size.
	 *	O : the nand chip's oob size.
	 *	O': the free oob.
	 *
	 *	The formula for P is :
	 *
	 *	            E * G * N
	 *	       P = ------------ + P' + M
	 *                      8
	 *
	 * The position of block mark moves forward in the ECC-based view
	 * of page, and the delta is:
	 *
	 *                   E * G * (N - 1)
	 *             D = (---------------- + M)
	 *                          8
	 *
	 * Please see the comment in legacy_set_geometry().
	 * With the condition C >= O , we still can get same result.
	 * So the bit position of the physical block mark within the ECC-based
	 * view of the page is :
	 *             (P' - D) * 8
	 */
	geo->page_size = mtd->writesize + geo->metadata_size +
		(geo->gf_len * geo->ecc_strength * geo->ecc_chunk_count) / 8;

	geo->payload_size = mtd->writesize;

	geo->auxiliary_status_offset = ALIGN(geo->metadata_size, 4);
	geo->auxiliary_size = ALIGN(geo->metadata_size, 4)
				+ ALIGN(geo->ecc_chunk_count, 4);

	if (!this->swap_block_mark)
		return 0;

	/* For bit swap. */
	block_mark_bit_offset = mtd->writesize * 8 -
		(geo->ecc_strength * geo->gf_len * (geo->ecc_chunk_count - 1)
				+ geo->metadata_size * 8);

	geo->block_mark_byte_offset = block_mark_bit_offset / 8;
	geo->block_mark_bit_offset  = block_mark_bit_offset % 8;
	return 0;
}

/*
 *  Calculate the ECC strength by hand:
 *	E : The ECC strength.
 *	G : the length of Galois Field.
 *	N : The chunk count of per page.
 *	O : the oobsize of the NAND chip.
 *	M : the metasize of per page.
 *
 *	The formula is :
 *		E * G * N
 *	      ------------ <= (O - M)
 *                  8
 *
 *      So, we get E by:
 *                    (O - M) * 8
 *              E <= -------------
 *                       G * N
 */
static inline int get_ecc_strength(struct gpmi_nand_data *this)
{
	struct bch_geometry *geo = &this->bch_geometry;
	struct mtd_info	*mtd = nand_to_mtd(&this->nand);
	int ecc_strength;

	ecc_strength = ((mtd->oobsize - geo->metadata_size) * 8)
			/ (geo->gf_len * geo->ecc_chunk_count);

	/* We need the minor even number. */
	return round_down(ecc_strength, 2);
}

static int legacy_set_geometry(struct gpmi_nand_data *this)
{
	struct bch_geometry *geo = &this->bch_geometry;
	struct mtd_info *mtd = nand_to_mtd(&this->nand);
	unsigned int metadata_size;
	unsigned int status_size;
	unsigned int block_mark_bit_offset;

	/*
	 * The size of the metadata can be changed, though we set it to 10
	 * bytes now. But it can't be too large, because we have to save
	 * enough space for BCH.
	 */
	geo->metadata_size = 10;

	/* The default for the length of Galois Field. */
	geo->gf_len = 13;

	/* The default for chunk size. */
	geo->ecc_chunk_size = 512;
	while (geo->ecc_chunk_size < mtd->oobsize) {
		geo->ecc_chunk_size *= 2; /* keep C >= O */
		geo->gf_len = 14;
	}

	geo->ecc_chunk_count = mtd->writesize / geo->ecc_chunk_size;

	/* We use the same ECC strength for all chunks. */
	geo->ecc_strength = get_ecc_strength(this);
	if (!gpmi_check_ecc(this)) {
		dev_err(this->dev,
			"ecc strength: %d cannot be supported by the controller (%d)\n"
			"try to use minimum ecc strength that NAND chip required\n",
			geo->ecc_strength,
			this->devdata->bch_max_ecc_strength);
		return -EINVAL;
	}

	geo->page_size = mtd->writesize + geo->metadata_size +
		(geo->gf_len * geo->ecc_strength * geo->ecc_chunk_count) / 8;
	geo->payload_size = mtd->writesize;

	/*
	 * The auxiliary buffer contains the metadata and the ECC status. The
	 * metadata is padded to the nearest 32-bit boundary. The ECC status
	 * contains one byte for every ECC chunk, and is also padded to the
	 * nearest 32-bit boundary.
	 */
	metadata_size = ALIGN(geo->metadata_size, 4);
	status_size   = ALIGN(geo->ecc_chunk_count, 4);

	geo->auxiliary_size = metadata_size + status_size;
	geo->auxiliary_status_offset = metadata_size;

	if (!this->swap_block_mark)
		return 0;

	/*
	 * We need to compute the byte and bit offsets of
	 * the physical block mark within the ECC-based view of the page.
	 *
	 * NAND chip with 2K page shows below:
	 *                                             (Block Mark)
	 *                                                   |      |
	 *                                                   |  D   |
	 *                                                   |<---->|
	 *                                                   V      V
	 *    +---+----------+-+----------+-+----------+-+----------+-+
	 *    | M |   data   |E|   data   |E|   data   |E|   data   |E|
	 *    +---+----------+-+----------+-+----------+-+----------+-+
	 *
	 * The position of block mark moves forward in the ECC-based view
	 * of page, and the delta is:
	 *
	 *                   E * G * (N - 1)
	 *             D = (---------------- + M)
	 *                          8
	 *
	 * With the formula to compute the ECC strength, and the condition
	 *       : C >= O         (C is the ecc chunk size)
	 *
	 * It's easy to deduce to the following result:
	 *
	 *         E * G       (O - M)      C - M         C - M
	 *      ----------- <= ------- <=  --------  <  ---------
	 *           8            N           N          (N - 1)
	 *
	 *  So, we get:
	 *
	 *                   E * G * (N - 1)
	 *             D = (---------------- + M) < C
	 *                          8
	 *
	 *  The above inequality means the position of block mark
	 *  within the ECC-based view of the page is still in the data chunk,
	 *  and it's NOT in the ECC bits of the chunk.
	 *
	 *  Use the following to compute the bit position of the
	 *  physical block mark within the ECC-based view of the page:
	 *          (page_size - D) * 8
	 *
	 *  --Huang Shijie
	 */
	block_mark_bit_offset = mtd->writesize * 8 -
		(geo->ecc_strength * geo->gf_len * (geo->ecc_chunk_count - 1)
				+ geo->metadata_size * 8);

	geo->block_mark_byte_offset = block_mark_bit_offset / 8;
	geo->block_mark_bit_offset  = block_mark_bit_offset % 8;
	return 0;
}

static int common_nfc_set_geometry(struct gpmi_nand_data *this)
{
	struct nand_chip *chip = &this->nand;
	const struct nand_ecc_props *requirements =
		nanddev_get_ecc_requirements(&chip->base);

	if (chip->ecc.strength > 0 && chip->ecc.size > 0)
		return set_geometry_by_ecc_info(this, chip->ecc.strength,
						chip->ecc.size);

	if ((of_property_read_bool(this->dev->of_node, "fsl,use-minimum-ecc"))
				|| legacy_set_geometry(this)) {
		if (!(requirements->strength > 0 && requirements->step_size > 0))
			return -EINVAL;

		return set_geometry_by_ecc_info(this,
						requirements->strength,
						requirements->step_size);
	}

	return 0;
}

/* Configures the geometry for BCH.  */
static int bch_set_geometry(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	int ret;

	ret = common_nfc_set_geometry(this);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(this->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(this->dev);
		return ret;
	}

	/*
	* Due to erratum #2847 of the MX23, the BCH cannot be soft reset on this
	* chip, otherwise it will lock up. So we skip resetting BCH on the MX23.
	* and MX28.
	*/
	ret = gpmi_reset_block(r->bch_regs, GPMI_IS_MXS(this));
	if (ret)
		goto err_out;

	/* Set *all* chip selects to use layout 0. */
	writel(0, r->bch_regs + HW_BCH_LAYOUTSELECT);

	ret = 0;
err_out:
	pm_runtime_mark_last_busy(this->dev);
	pm_runtime_put_autosuspend(this->dev);

	return ret;
}

/*
 * <1> Firstly, we should know what's the GPMI-clock means.
 *     The GPMI-clock is the internal clock in the gpmi nand controller.
 *     If you set 100MHz to gpmi nand controller, the GPMI-clock's period
 *     is 10ns. Mark the GPMI-clock's period as GPMI-clock-period.
 *
 * <2> Secondly, we should know what's the frequency on the nand chip pins.
 *     The frequency on the nand chip pins is derived from the GPMI-clock.
 *     We can get it from the following equation:
 *
 *         F = G / (DS + DH)
 *
 *         F  : the frequency on the nand chip pins.
 *         G  : the GPMI clock, such as 100MHz.
 *         DS : GPMI_HW_GPMI_TIMING0:DATA_SETUP
 *         DH : GPMI_HW_GPMI_TIMING0:DATA_HOLD
 *
 * <3> Thirdly, when the frequency on the nand chip pins is above 33MHz,
 *     the nand EDO(extended Data Out) timing could be applied.
 *     The GPMI implements a feedback read strobe to sample the read data.
 *     The feedback read strobe can be delayed to support the nand EDO timing
 *     where the read strobe may deasserts before the read data is valid, and
 *     read data is valid for some time after read strobe.
 *
 *     The following figure illustrates some aspects of a NAND Flash read:
 *
 *                   |<---tREA---->|
 *                   |             |
 *                   |         |   |
 *                   |<--tRP-->|   |
 *                   |         |   |
 *                  __          ___|__________________________________
 *     RDN            \________/   |
 *                                 |
 *                                 /---------\
 *     Read Data    --------------<           >---------
 *                                 \---------/
 *                                |     |
 *                                |<-D->|
 *     FeedbackRDN  ________             ____________
 *                          \___________/
 *
 *          D stands for delay, set in the HW_GPMI_CTRL1:RDN_DELAY.
 *
 *
 * <4> Now, we begin to describe how to compute the right RDN_DELAY.
 *
 *  4.1) From the aspect of the nand chip pins:
 *        Delay = (tREA + C - tRP)               {1}
 *
 *        tREA : the maximum read access time.
 *        C    : a constant to adjust the delay. default is 4000ps.
 *        tRP  : the read pulse width, which is exactly:
 *                   tRP = (GPMI-clock-period) * DATA_SETUP
 *
 *  4.2) From the aspect of the GPMI nand controller:
 *         Delay = RDN_DELAY * 0.125 * RP        {2}
 *
 *         RP   : the DLL reference period.
 *            if (GPMI-clock-period > DLL_THRETHOLD)
 *                   RP = GPMI-clock-period / 2;
 *            else
 *                   RP = GPMI-clock-period;
 *
 *            Set the HW_GPMI_CTRL1:HALF_PERIOD if GPMI-clock-period
 *            is greater DLL_THRETHOLD. In other SOCs, the DLL_THRETHOLD
 *            is 16000ps, but in mx6q, we use 12000ps.
 *
 *  4.3) since {1} equals {2}, we get:
 *
 *                     (tREA + 4000 - tRP) * 8
 *         RDN_DELAY = -----------------------     {3}
 *                           RP
 */
static void gpmi_nfc_compute_timings(struct gpmi_nand_data *this,
				     const struct nand_sdr_timings *sdr)
{
	struct gpmi_nfc_hardware_timing *hw = &this->hw;
	struct resources *r = &this->resources;
	unsigned int dll_threshold_ps = this->devdata->max_chain_delay;
	unsigned int period_ps, reference_period_ps;
	unsigned int data_setup_cycles, data_hold_cycles, addr_setup_cycles;
	unsigned int tRP_ps;
	bool use_half_period;
	int sample_delay_ps, sample_delay_factor;
	u16 busy_timeout_cycles;
	u8 wrn_dly_sel;

	if (sdr->tRC_min >= 30000) {
		/* ONFI non-EDO modes [0-3] */
		hw->clk_rate = 22000000;
		wrn_dly_sel = BV_GPMI_CTRL1_WRN_DLY_SEL_4_TO_8NS;
	} else if (sdr->tRC_min >= 25000) {
		/* ONFI EDO mode 4 */
		hw->clk_rate = 80000000;
		wrn_dly_sel = BV_GPMI_CTRL1_WRN_DLY_SEL_NO_DELAY;
	} else {
		/* ONFI EDO mode 5 */
		hw->clk_rate = 100000000;
		wrn_dly_sel = BV_GPMI_CTRL1_WRN_DLY_SEL_NO_DELAY;
	}

	hw->clk_rate = clk_round_rate(r->clock[0], hw->clk_rate);

	/* SDR core timings are given in picoseconds */
	period_ps = div_u64((u64)NSEC_PER_SEC * 1000, hw->clk_rate);

	addr_setup_cycles = TO_CYCLES(sdr->tALS_min, period_ps);
	data_setup_cycles = TO_CYCLES(sdr->tDS_min, period_ps);
	data_hold_cycles = TO_CYCLES(sdr->tDH_min, period_ps);
	busy_timeout_cycles = TO_CYCLES(sdr->tWB_max + sdr->tR_max, period_ps);

	hw->timing0 = BF_GPMI_TIMING0_ADDRESS_SETUP(addr_setup_cycles) |
		      BF_GPMI_TIMING0_DATA_HOLD(data_hold_cycles) |
		      BF_GPMI_TIMING0_DATA_SETUP(data_setup_cycles);
	hw->timing1 = BF_GPMI_TIMING1_BUSY_TIMEOUT(busy_timeout_cycles * 4096);

	/*
	 * Derive NFC ideal delay from {3}:
	 *
	 *                     (tREA + 4000 - tRP) * 8
	 *         RDN_DELAY = -----------------------
	 *                                RP
	 */
	if (period_ps > dll_threshold_ps) {
		use_half_period = true;
		reference_period_ps = period_ps / 2;
	} else {
		use_half_period = false;
		reference_period_ps = period_ps;
	}

	tRP_ps = data_setup_cycles * period_ps;
	sample_delay_ps = (sdr->tREA_max + 4000 - tRP_ps) * 8;
	if (sample_delay_ps > 0)
		sample_delay_factor = sample_delay_ps / reference_period_ps;
	else
		sample_delay_factor = 0;

	hw->ctrl1n = BF_GPMI_CTRL1_WRN_DLY_SEL(wrn_dly_sel);
	if (sample_delay_factor)
		hw->ctrl1n |= BF_GPMI_CTRL1_RDN_DELAY(sample_delay_factor) |
			      BM_GPMI_CTRL1_DLL_ENABLE |
			      (use_half_period ? BM_GPMI_CTRL1_HALF_PERIOD : 0);
}

static int gpmi_nfc_apply_timings(struct gpmi_nand_data *this)
{
	struct gpmi_nfc_hardware_timing *hw = &this->hw;
	struct resources *r = &this->resources;
	void __iomem *gpmi_regs = r->gpmi_regs;
	unsigned int dll_wait_time_us;
	int ret;

	/* Clock dividers do NOT guarantee a clean clock signal on its output
	 * during the change of the divide factor on i.MX6Q/UL/SX. On i.MX7/8,
	 * all clock dividers provide these guarantee.
	 */
	if (GPMI_IS_MX6Q(this) || GPMI_IS_MX6SX(this))
		clk_disable_unprepare(r->clock[0]);

	ret = clk_set_rate(r->clock[0], hw->clk_rate);
	if (ret) {
		dev_err(this->dev, "cannot set clock rate to %lu Hz: %d\n", hw->clk_rate, ret);
		return ret;
	}

	if (GPMI_IS_MX6Q(this) || GPMI_IS_MX6SX(this)) {
		ret = clk_prepare_enable(r->clock[0]);
		if (ret)
			return ret;
	}

	writel(hw->timing0, gpmi_regs + HW_GPMI_TIMING0);
	writel(hw->timing1, gpmi_regs + HW_GPMI_TIMING1);

	/*
	 * Clear several CTRL1 fields, DLL must be disabled when setting
	 * RDN_DELAY or HALF_PERIOD.
	 */
	writel(BM_GPMI_CTRL1_CLEAR_MASK, gpmi_regs + HW_GPMI_CTRL1_CLR);
	writel(hw->ctrl1n, gpmi_regs + HW_GPMI_CTRL1_SET);

	/* Wait 64 clock cycles before using the GPMI after enabling the DLL */
	dll_wait_time_us = USEC_PER_SEC / hw->clk_rate * 64;
	if (!dll_wait_time_us)
		dll_wait_time_us = 1;

	/* Wait for the DLL to settle. */
	udelay(dll_wait_time_us);

	return 0;
}

static int gpmi_setup_interface(struct nand_chip *chip, int chipnr,
				const struct nand_interface_config *conf)
{
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	const struct nand_sdr_timings *sdr;

	/* Retrieve required NAND timings */
	sdr = nand_get_sdr_timings(conf);
	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	/* Only MX6 GPMI controller can reach EDO timings */
	if (sdr->tRC_min <= 25000 && !GPMI_IS_MX6(this))
		return -ENOTSUPP;

	/* Stop here if this call was just a check */
	if (chipnr < 0)
		return 0;

	/* Do the actual derivation of the controller timings */
	gpmi_nfc_compute_timings(this, sdr);

	this->hw.must_apply_timings = true;

	return 0;
}

/* Clears a BCH interrupt. */
static void gpmi_clear_bch(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	writel(BM_BCH_CTRL_COMPLETE_IRQ, r->bch_regs + HW_BCH_CTRL_CLR);
}

static struct dma_chan *get_dma_chan(struct gpmi_nand_data *this)
{
	/* We use the DMA channel 0 to access all the nand chips. */
	return this->dma_chans[0];
}

/* This will be called after the DMA operation is finished. */
static void dma_irq_callback(void *param)
{
	struct gpmi_nand_data *this = param;
	struct completion *dma_c = &this->dma_done;

	complete(dma_c);
}

static irqreturn_t bch_irq(int irq, void *cookie)
{
	struct gpmi_nand_data *this = cookie;

	gpmi_clear_bch(this);
	complete(&this->bch_done);
	return IRQ_HANDLED;
}

static int gpmi_raw_len_to_len(struct gpmi_nand_data *this, int raw_len)
{
	/*
	 * raw_len is the length to read/write including bch data which
	 * we are passed in exec_op. Calculate the data length from it.
	 */
	if (this->bch)
		return ALIGN_DOWN(raw_len, this->bch_geometry.ecc_chunk_size);
	else
		return raw_len;
}

/* Can we use the upper's buffer directly for DMA? */
static bool prepare_data_dma(struct gpmi_nand_data *this, const void *buf,
			     int raw_len, struct scatterlist *sgl,
			     enum dma_data_direction dr)
{
	int ret;
	int len = gpmi_raw_len_to_len(this, raw_len);

	/* first try to map the upper buffer directly */
	if (virt_addr_valid(buf) && !object_is_on_stack(buf)) {
		sg_init_one(sgl, buf, len);
		ret = dma_map_sg(this->dev, sgl, 1, dr);
		if (ret == 0)
			goto map_fail;

		return true;
	}

map_fail:
	/* We have to use our own DMA buffer. */
	sg_init_one(sgl, this->data_buffer_dma, len);

	if (dr == DMA_TO_DEVICE && buf != this->data_buffer_dma)
		memcpy(this->data_buffer_dma, buf, len);

	dma_map_sg(this->dev, sgl, 1, dr);

	return false;
}

/* add our owner bbt descriptor */
static uint8_t scan_ff_pattern[] = { 0xff };
static struct nand_bbt_descr gpmi_bbt_descr = {
	.options	= 0,
	.offs		= 0,
	.len		= 1,
	.pattern	= scan_ff_pattern
};

/*
 * We may change the layout if we can get the ECC info from the datasheet,
 * else we will use all the (page + OOB).
 */
static int gpmi_ooblayout_ecc(struct mtd_info *mtd, int section,
			      struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct bch_geometry *geo = &this->bch_geometry;

	if (section)
		return -ERANGE;

	oobregion->offset = 0;
	oobregion->length = geo->page_size - mtd->writesize;

	return 0;
}

static int gpmi_ooblayout_free(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct bch_geometry *geo = &this->bch_geometry;

	if (section)
		return -ERANGE;

	/* The available oob size we have. */
	if (geo->page_size < mtd->writesize + mtd->oobsize) {
		oobregion->offset = geo->page_size - mtd->writesize;
		oobregion->length = mtd->oobsize - oobregion->offset;
	}

	return 0;
}

static const char * const gpmi_clks_for_mx2x[] = {
	"gpmi_io",
};

static const struct mtd_ooblayout_ops gpmi_ooblayout_ops = {
	.ecc = gpmi_ooblayout_ecc,
	.free = gpmi_ooblayout_free,
};

static const struct gpmi_devdata gpmi_devdata_imx23 = {
	.type = IS_MX23,
	.bch_max_ecc_strength = 20,
	.max_chain_delay = 16000,
	.clks = gpmi_clks_for_mx2x,
	.clks_count = ARRAY_SIZE(gpmi_clks_for_mx2x),
};

static const struct gpmi_devdata gpmi_devdata_imx28 = {
	.type = IS_MX28,
	.bch_max_ecc_strength = 20,
	.max_chain_delay = 16000,
	.clks = gpmi_clks_for_mx2x,
	.clks_count = ARRAY_SIZE(gpmi_clks_for_mx2x),
};

static const char * const gpmi_clks_for_mx6[] = {
	"gpmi_io", "gpmi_apb", "gpmi_bch", "gpmi_bch_apb", "per1_bch",
};

static const struct gpmi_devdata gpmi_devdata_imx6q = {
	.type = IS_MX6Q,
	.bch_max_ecc_strength = 40,
	.max_chain_delay = 12000,
	.clks = gpmi_clks_for_mx6,
	.clks_count = ARRAY_SIZE(gpmi_clks_for_mx6),
};

static const struct gpmi_devdata gpmi_devdata_imx6sx = {
	.type = IS_MX6SX,
	.bch_max_ecc_strength = 62,
	.max_chain_delay = 12000,
	.clks = gpmi_clks_for_mx6,
	.clks_count = ARRAY_SIZE(gpmi_clks_for_mx6),
};

static const char * const gpmi_clks_for_mx7d[] = {
	"gpmi_io", "gpmi_bch_apb",
};

static const struct gpmi_devdata gpmi_devdata_imx7d = {
	.type = IS_MX7D,
	.bch_max_ecc_strength = 62,
	.max_chain_delay = 12000,
	.clks = gpmi_clks_for_mx7d,
	.clks_count = ARRAY_SIZE(gpmi_clks_for_mx7d),
};

static int acquire_register_block(struct gpmi_nand_data *this,
				  const char *res_name)
{
	struct platform_device *pdev = this->pdev;
	struct resources *res = &this->resources;
	struct resource *r;
	void __iomem *p;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, res_name);
	p = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(p))
		return PTR_ERR(p);

	if (!strcmp(res_name, GPMI_NAND_GPMI_REGS_ADDR_RES_NAME))
		res->gpmi_regs = p;
	else if (!strcmp(res_name, GPMI_NAND_BCH_REGS_ADDR_RES_NAME))
		res->bch_regs = p;
	else
		dev_err(this->dev, "unknown resource name : %s\n", res_name);

	return 0;
}

static int acquire_bch_irq(struct gpmi_nand_data *this, irq_handler_t irq_h)
{
	struct platform_device *pdev = this->pdev;
	const char *res_name = GPMI_NAND_BCH_INTERRUPT_RES_NAME;
	struct resource *r;
	int err;

	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ, res_name);
	if (!r) {
		dev_err(this->dev, "Can't get resource for %s\n", res_name);
		return -ENODEV;
	}

	err = devm_request_irq(this->dev, r->start, irq_h, 0, res_name, this);
	if (err)
		dev_err(this->dev, "error requesting BCH IRQ\n");

	return err;
}

static void release_dma_channels(struct gpmi_nand_data *this)
{
	unsigned int i;
	for (i = 0; i < DMA_CHANS; i++)
		if (this->dma_chans[i]) {
			dma_release_channel(this->dma_chans[i]);
			this->dma_chans[i] = NULL;
		}
}

static int acquire_dma_channels(struct gpmi_nand_data *this)
{
	struct platform_device *pdev = this->pdev;
	struct dma_chan *dma_chan;
	int ret = 0;

	/* request dma channel */
	dma_chan = dma_request_chan(&pdev->dev, "rx-tx");
	if (IS_ERR(dma_chan)) {
		ret = dev_err_probe(this->dev, PTR_ERR(dma_chan),
				    "DMA channel request failed\n");
		release_dma_channels(this);
	} else {
		this->dma_chans[0] = dma_chan;
	}

	return ret;
}

static int gpmi_get_clks(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	struct clk *clk;
	int err, i;

	for (i = 0; i < this->devdata->clks_count; i++) {
		clk = devm_clk_get(this->dev, this->devdata->clks[i]);
		if (IS_ERR(clk)) {
			err = PTR_ERR(clk);
			goto err_clock;
		}

		r->clock[i] = clk;
	}

	return 0;

err_clock:
	dev_dbg(this->dev, "failed in finding the clocks.\n");
	return err;
}

static int acquire_resources(struct gpmi_nand_data *this)
{
	int ret;

	ret = acquire_register_block(this, GPMI_NAND_GPMI_REGS_ADDR_RES_NAME);
	if (ret)
		goto exit_regs;

	ret = acquire_register_block(this, GPMI_NAND_BCH_REGS_ADDR_RES_NAME);
	if (ret)
		goto exit_regs;

	ret = acquire_bch_irq(this, bch_irq);
	if (ret)
		goto exit_regs;

	ret = acquire_dma_channels(this);
	if (ret)
		goto exit_regs;

	ret = gpmi_get_clks(this);
	if (ret)
		goto exit_clock;
	return 0;

exit_clock:
	release_dma_channels(this);
exit_regs:
	return ret;
}

static void release_resources(struct gpmi_nand_data *this)
{
	release_dma_channels(this);
}

static void gpmi_free_dma_buffer(struct gpmi_nand_data *this)
{
	struct device *dev = this->dev;
	struct bch_geometry *geo = &this->bch_geometry;

	if (this->auxiliary_virt && virt_addr_valid(this->auxiliary_virt))
		dma_free_coherent(dev, geo->auxiliary_size,
					this->auxiliary_virt,
					this->auxiliary_phys);
	kfree(this->data_buffer_dma);
	kfree(this->raw_buffer);

	this->data_buffer_dma	= NULL;
	this->raw_buffer	= NULL;
}

/* Allocate the DMA buffers */
static int gpmi_alloc_dma_buffer(struct gpmi_nand_data *this)
{
	struct bch_geometry *geo = &this->bch_geometry;
	struct device *dev = this->dev;
	struct mtd_info *mtd = nand_to_mtd(&this->nand);

	/*
	 * [2] Allocate a read/write data buffer.
	 *     The gpmi_alloc_dma_buffer can be called twice.
	 *     We allocate a PAGE_SIZE length buffer if gpmi_alloc_dma_buffer
	 *     is called before the NAND identification; and we allocate a
	 *     buffer of the real NAND page size when the gpmi_alloc_dma_buffer
	 *     is called after.
	 */
	this->data_buffer_dma = kzalloc(mtd->writesize ?: PAGE_SIZE,
					GFP_DMA | GFP_KERNEL);
	if (this->data_buffer_dma == NULL)
		goto error_alloc;

	this->auxiliary_virt = dma_alloc_coherent(dev, geo->auxiliary_size,
					&this->auxiliary_phys, GFP_DMA);
	if (!this->auxiliary_virt)
		goto error_alloc;

	this->raw_buffer = kzalloc((mtd->writesize ?: PAGE_SIZE) + mtd->oobsize, GFP_KERNEL);
	if (!this->raw_buffer)
		goto error_alloc;

	return 0;

error_alloc:
	gpmi_free_dma_buffer(this);
	return -ENOMEM;
}

/*
 * Handles block mark swapping.
 * It can be called in swapping the block mark, or swapping it back,
 * because the the operations are the same.
 */
static void block_mark_swapping(struct gpmi_nand_data *this,
				void *payload, void *auxiliary)
{
	struct bch_geometry *nfc_geo = &this->bch_geometry;
	unsigned char *p;
	unsigned char *a;
	unsigned int  bit;
	unsigned char mask;
	unsigned char from_data;
	unsigned char from_oob;

	if (!this->swap_block_mark)
		return;

	/*
	 * If control arrives here, we're swapping. Make some convenience
	 * variables.
	 */
	bit = nfc_geo->block_mark_bit_offset;
	p   = payload + nfc_geo->block_mark_byte_offset;
	a   = auxiliary;

	/*
	 * Get the byte from the data area that overlays the block mark. Since
	 * the ECC engine applies its own view to the bits in the page, the
	 * physical block mark won't (in general) appear on a byte boundary in
	 * the data.
	 */
	from_data = (p[0] >> bit) | (p[1] << (8 - bit));

	/* Get the byte from the OOB. */
	from_oob = a[0];

	/* Swap them. */
	a[0] = from_data;

	mask = (0x1 << bit) - 1;
	p[0] = (p[0] & mask) | (from_oob << bit);

	mask = ~0 << bit;
	p[1] = (p[1] & mask) | (from_oob >> (8 - bit));
}

static int gpmi_count_bitflips(struct nand_chip *chip, void *buf, int first,
			       int last, int meta)
{
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct bch_geometry *nfc_geo = &this->bch_geometry;
	struct mtd_info *mtd = nand_to_mtd(chip);
	int i;
	unsigned char *status;
	unsigned int max_bitflips = 0;

	/* Loop over status bytes, accumulating ECC status. */
	status = this->auxiliary_virt + ALIGN(meta, 4);

	for (i = first; i < last; i++, status++) {
		if ((*status == STATUS_GOOD) || (*status == STATUS_ERASED))
			continue;

		if (*status == STATUS_UNCORRECTABLE) {
			int eccbits = nfc_geo->ecc_strength * nfc_geo->gf_len;
			u8 *eccbuf = this->raw_buffer;
			int offset, bitoffset;
			int eccbytes;
			int flips;

			/* Read ECC bytes into our internal raw_buffer */
			offset = nfc_geo->metadata_size * 8;
			offset += ((8 * nfc_geo->ecc_chunk_size) + eccbits) * (i + 1);
			offset -= eccbits;
			bitoffset = offset % 8;
			eccbytes = DIV_ROUND_UP(offset + eccbits, 8);
			offset /= 8;
			eccbytes -= offset;
			nand_change_read_column_op(chip, offset, eccbuf,
						   eccbytes, false);

			/*
			 * ECC data are not byte aligned and we may have
			 * in-band data in the first and last byte of
			 * eccbuf. Set non-eccbits to one so that
			 * nand_check_erased_ecc_chunk() does not count them
			 * as bitflips.
			 */
			if (bitoffset)
				eccbuf[0] |= GENMASK(bitoffset - 1, 0);

			bitoffset = (bitoffset + eccbits) % 8;
			if (bitoffset)
				eccbuf[eccbytes - 1] |= GENMASK(7, bitoffset);

			/*
			 * The ECC hardware has an uncorrectable ECC status
			 * code in case we have bitflips in an erased page. As
			 * nothing was written into this subpage the ECC is
			 * obviously wrong and we can not trust it. We assume
			 * at this point that we are reading an erased page and
			 * try to correct the bitflips in buffer up to
			 * ecc_strength bitflips. If this is a page with random
			 * data, we exceed this number of bitflips and have a
			 * ECC failure. Otherwise we use the corrected buffer.
			 */
			if (i == 0) {
				/* The first block includes metadata */
				flips = nand_check_erased_ecc_chunk(
						buf + i * nfc_geo->ecc_chunk_size,
						nfc_geo->ecc_chunk_size,
						eccbuf, eccbytes,
						this->auxiliary_virt,
						nfc_geo->metadata_size,
						nfc_geo->ecc_strength);
			} else {
				flips = nand_check_erased_ecc_chunk(
						buf + i * nfc_geo->ecc_chunk_size,
						nfc_geo->ecc_chunk_size,
						eccbuf, eccbytes,
						NULL, 0,
						nfc_geo->ecc_strength);
			}

			if (flips > 0) {
				max_bitflips = max_t(unsigned int, max_bitflips,
						     flips);
				mtd->ecc_stats.corrected += flips;
				continue;
			}

			mtd->ecc_stats.failed++;
			continue;
		}

		mtd->ecc_stats.corrected += *status;
		max_bitflips = max_t(unsigned int, max_bitflips, *status);
	}

	return max_bitflips;
}

static void gpmi_bch_layout_std(struct gpmi_nand_data *this)
{
	struct bch_geometry *geo = &this->bch_geometry;
	unsigned int ecc_strength = geo->ecc_strength >> 1;
	unsigned int gf_len = geo->gf_len;
	unsigned int block_size = geo->ecc_chunk_size;

	this->bch_flashlayout0 =
		BF_BCH_FLASH0LAYOUT0_NBLOCKS(geo->ecc_chunk_count - 1) |
		BF_BCH_FLASH0LAYOUT0_META_SIZE(geo->metadata_size) |
		BF_BCH_FLASH0LAYOUT0_ECC0(ecc_strength, this) |
		BF_BCH_FLASH0LAYOUT0_GF(gf_len, this) |
		BF_BCH_FLASH0LAYOUT0_DATA0_SIZE(block_size, this);

	this->bch_flashlayout1 =
		BF_BCH_FLASH0LAYOUT1_PAGE_SIZE(geo->page_size) |
		BF_BCH_FLASH0LAYOUT1_ECCN(ecc_strength, this) |
		BF_BCH_FLASH0LAYOUT1_GF(gf_len, this) |
		BF_BCH_FLASH0LAYOUT1_DATAN_SIZE(block_size, this);
}

static int gpmi_ecc_read_page(struct nand_chip *chip, uint8_t *buf,
			      int oob_required, int page)
{
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct bch_geometry *geo = &this->bch_geometry;
	unsigned int max_bitflips;
	int ret;

	gpmi_bch_layout_std(this);
	this->bch = true;

	ret = nand_read_page_op(chip, page, 0, buf, geo->page_size);
	if (ret)
		return ret;

	max_bitflips = gpmi_count_bitflips(chip, buf, 0,
					   geo->ecc_chunk_count,
					   geo->auxiliary_status_offset);

	/* handle the block mark swapping */
	block_mark_swapping(this, buf, this->auxiliary_virt);

	if (oob_required) {
		/*
		 * It's time to deliver the OOB bytes. See gpmi_ecc_read_oob()
		 * for details about our policy for delivering the OOB.
		 *
		 * We fill the caller's buffer with set bits, and then copy the
		 * block mark to th caller's buffer. Note that, if block mark
		 * swapping was necessary, it has already been done, so we can
		 * rely on the first byte of the auxiliary buffer to contain
		 * the block mark.
		 */
		memset(chip->oob_poi, ~0, mtd->oobsize);
		chip->oob_poi[0] = ((uint8_t *)this->auxiliary_virt)[0];
	}

	return max_bitflips;
}

/* Fake a virtual small page for the subpage read */
static int gpmi_ecc_read_subpage(struct nand_chip *chip, uint32_t offs,
				 uint32_t len, uint8_t *buf, int page)
{
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct bch_geometry *geo = &this->bch_geometry;
	int size = chip->ecc.size; /* ECC chunk size */
	int meta, n, page_size;
	unsigned int max_bitflips;
	unsigned int ecc_strength;
	int first, last, marker_pos;
	int ecc_parity_size;
	int col = 0;
	int ret;

	/* The size of ECC parity */
	ecc_parity_size = geo->gf_len * geo->ecc_strength / 8;

	/* Align it with the chunk size */
	first = offs / size;
	last = (offs + len - 1) / size;

	if (this->swap_block_mark) {
		/*
		 * Find the chunk which contains the Block Marker.
		 * If this chunk is in the range of [first, last],
		 * we have to read out the whole page.
		 * Why? since we had swapped the data at the position of Block
		 * Marker to the metadata which is bound with the chunk 0.
		 */
		marker_pos = geo->block_mark_byte_offset / size;
		if (last >= marker_pos && first <= marker_pos) {
			dev_dbg(this->dev,
				"page:%d, first:%d, last:%d, marker at:%d\n",
				page, first, last, marker_pos);
			return gpmi_ecc_read_page(chip, buf, 0, page);
		}
	}

	meta = geo->metadata_size;
	if (first) {
		col = meta + (size + ecc_parity_size) * first;
		meta = 0;
		buf = buf + first * size;
	}

	ecc_parity_size = geo->gf_len * geo->ecc_strength / 8;

	n = last - first + 1;
	page_size = meta + (size + ecc_parity_size) * n;
	ecc_strength = geo->ecc_strength >> 1;

	this->bch_flashlayout0 = BF_BCH_FLASH0LAYOUT0_NBLOCKS(n - 1) |
		BF_BCH_FLASH0LAYOUT0_META_SIZE(meta) |
		BF_BCH_FLASH0LAYOUT0_ECC0(ecc_strength, this) |
		BF_BCH_FLASH0LAYOUT0_GF(geo->gf_len, this) |
		BF_BCH_FLASH0LAYOUT0_DATA0_SIZE(geo->ecc_chunk_size, this);

	this->bch_flashlayout1 = BF_BCH_FLASH0LAYOUT1_PAGE_SIZE(page_size) |
		BF_BCH_FLASH0LAYOUT1_ECCN(ecc_strength, this) |
		BF_BCH_FLASH0LAYOUT1_GF(geo->gf_len, this) |
		BF_BCH_FLASH0LAYOUT1_DATAN_SIZE(geo->ecc_chunk_size, this);

	this->bch = true;

	ret = nand_read_page_op(chip, page, col, buf, page_size);
	if (ret)
		return ret;

	dev_dbg(this->dev, "page:%d(%d:%d)%d, chunk:(%d:%d), BCH PG size:%d\n",
		page, offs, len, col, first, n, page_size);

	max_bitflips = gpmi_count_bitflips(chip, buf, first, last, meta);

	return max_bitflips;
}

static int gpmi_ecc_write_page(struct nand_chip *chip, const uint8_t *buf,
			       int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct bch_geometry *nfc_geo = &this->bch_geometry;
	int ret;

	dev_dbg(this->dev, "ecc write page.\n");

	gpmi_bch_layout_std(this);
	this->bch = true;

	memcpy(this->auxiliary_virt, chip->oob_poi, nfc_geo->auxiliary_size);

	if (this->swap_block_mark) {
		/*
		 * When doing bad block marker swapping we must always copy the
		 * input buffer as we can't modify the const buffer.
		 */
		memcpy(this->data_buffer_dma, buf, mtd->writesize);
		buf = this->data_buffer_dma;
		block_mark_swapping(this, this->data_buffer_dma,
				    this->auxiliary_virt);
	}

	ret = nand_prog_page_op(chip, page, 0, buf, nfc_geo->page_size);

	return ret;
}

/*
 * There are several places in this driver where we have to handle the OOB and
 * block marks. This is the function where things are the most complicated, so
 * this is where we try to explain it all. All the other places refer back to
 * here.
 *
 * These are the rules, in order of decreasing importance:
 *
 * 1) Nothing the caller does can be allowed to imperil the block mark.
 *
 * 2) In read operations, the first byte of the OOB we return must reflect the
 *    true state of the block mark, no matter where that block mark appears in
 *    the physical page.
 *
 * 3) ECC-based read operations return an OOB full of set bits (since we never
 *    allow ECC-based writes to the OOB, it doesn't matter what ECC-based reads
 *    return).
 *
 * 4) "Raw" read operations return a direct view of the physical bytes in the
 *    page, using the conventional definition of which bytes are data and which
 *    are OOB. This gives the caller a way to see the actual, physical bytes
 *    in the page, without the distortions applied by our ECC engine.
 *
 *
 * What we do for this specific read operation depends on two questions:
 *
 * 1) Are we doing a "raw" read, or an ECC-based read?
 *
 * 2) Are we using block mark swapping or transcription?
 *
 * There are four cases, illustrated by the following Karnaugh map:
 *
 *                    |           Raw           |         ECC-based       |
 *       -------------+-------------------------+-------------------------+
 *                    | Read the conventional   |                         |
 *                    | OOB at the end of the   |                         |
 *       Swapping     | page and return it. It  |                         |
 *                    | contains exactly what   |                         |
 *                    | we want.                | Read the block mark and |
 *       -------------+-------------------------+ return it in a buffer   |
 *                    | Read the conventional   | full of set bits.       |
 *                    | OOB at the end of the   |                         |
 *                    | page and also the block |                         |
 *       Transcribing | mark in the metadata.   |                         |
 *                    | Copy the block mark     |                         |
 *                    | into the first byte of  |                         |
 *                    | the OOB.                |                         |
 *       -------------+-------------------------+-------------------------+
 *
 * Note that we break rule #4 in the Transcribing/Raw case because we're not
 * giving an accurate view of the actual, physical bytes in the page (we're
 * overwriting the block mark). That's OK because it's more important to follow
 * rule #2.
 *
 * It turns out that knowing whether we want an "ECC-based" or "raw" read is not
 * easy. When reading a page, for example, the NAND Flash MTD code calls our
 * ecc.read_page or ecc.read_page_raw function. Thus, the fact that MTD wants an
 * ECC-based or raw view of the page is implicit in which function it calls
 * (there is a similar pair of ECC-based/raw functions for writing).
 */
static int gpmi_ecc_read_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	int ret;

	/* clear the OOB buffer */
	memset(chip->oob_poi, ~0, mtd->oobsize);

	/* Read out the conventional OOB. */
	ret = nand_read_page_op(chip, page, mtd->writesize, chip->oob_poi,
				mtd->oobsize);
	if (ret)
		return ret;

	/*
	 * Now, we want to make sure the block mark is correct. In the
	 * non-transcribing case (!GPMI_IS_MX23()), we already have it.
	 * Otherwise, we need to explicitly read it.
	 */
	if (GPMI_IS_MX23(this)) {
		/* Read the block mark into the first byte of the OOB buffer. */
		ret = nand_read_page_op(chip, page, 0, chip->oob_poi, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int gpmi_ecc_write_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct mtd_oob_region of = { };

	/* Do we have available oob area? */
	mtd_ooblayout_free(mtd, 0, &of);
	if (!of.length)
		return -EPERM;

	if (!nand_is_slc(chip))
		return -EPERM;

	return nand_prog_page_op(chip, page, mtd->writesize + of.offset,
				 chip->oob_poi + of.offset, of.length);
}

/*
 * This function reads a NAND page without involving the ECC engine (no HW
 * ECC correction).
 * The tricky part in the GPMI/BCH controller is that it stores ECC bits
 * inline (interleaved with payload DATA), and do not align data chunk on
 * byte boundaries.
 * We thus need to take care moving the payload data and ECC bits stored in the
 * page into the provided buffers, which is why we're using nand_extract_bits().
 *
 * See set_geometry_by_ecc_info inline comments to have a full description
 * of the layout used by the GPMI controller.
 */
static int gpmi_ecc_read_page_raw(struct nand_chip *chip, uint8_t *buf,
				  int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct bch_geometry *nfc_geo = &this->bch_geometry;
	int eccsize = nfc_geo->ecc_chunk_size;
	int eccbits = nfc_geo->ecc_strength * nfc_geo->gf_len;
	u8 *tmp_buf = this->raw_buffer;
	size_t src_bit_off;
	size_t oob_bit_off;
	size_t oob_byte_off;
	uint8_t *oob = chip->oob_poi;
	int step;
	int ret;

	ret = nand_read_page_op(chip, page, 0, tmp_buf,
				mtd->writesize + mtd->oobsize);
	if (ret)
		return ret;

	/*
	 * If required, swap the bad block marker and the data stored in the
	 * metadata section, so that we don't wrongly consider a block as bad.
	 *
	 * See the layout description for a detailed explanation on why this
	 * is needed.
	 */
	if (this->swap_block_mark)
		swap(tmp_buf[0], tmp_buf[mtd->writesize]);

	/*
	 * Copy the metadata section into the oob buffer (this section is
	 * guaranteed to be aligned on a byte boundary).
	 */
	if (oob_required)
		memcpy(oob, tmp_buf, nfc_geo->metadata_size);

	oob_bit_off = nfc_geo->metadata_size * 8;
	src_bit_off = oob_bit_off;

	/* Extract interleaved payload data and ECC bits */
	for (step = 0; step < nfc_geo->ecc_chunk_count; step++) {
		if (buf)
			nand_extract_bits(buf, step * eccsize * 8, tmp_buf,
					  src_bit_off, eccsize * 8);
		src_bit_off += eccsize * 8;

		/* Align last ECC block to align a byte boundary */
		if (step == nfc_geo->ecc_chunk_count - 1 &&
		    (oob_bit_off + eccbits) % 8)
			eccbits += 8 - ((oob_bit_off + eccbits) % 8);

		if (oob_required)
			nand_extract_bits(oob, oob_bit_off, tmp_buf,
					  src_bit_off, eccbits);

		src_bit_off += eccbits;
		oob_bit_off += eccbits;
	}

	if (oob_required) {
		oob_byte_off = oob_bit_off / 8;

		if (oob_byte_off < mtd->oobsize)
			memcpy(oob + oob_byte_off,
			       tmp_buf + mtd->writesize + oob_byte_off,
			       mtd->oobsize - oob_byte_off);
	}

	return 0;
}

/*
 * This function writes a NAND page without involving the ECC engine (no HW
 * ECC generation).
 * The tricky part in the GPMI/BCH controller is that it stores ECC bits
 * inline (interleaved with payload DATA), and do not align data chunk on
 * byte boundaries.
 * We thus need to take care moving the OOB area at the right place in the
 * final page, which is why we're using nand_extract_bits().
 *
 * See set_geometry_by_ecc_info inline comments to have a full description
 * of the layout used by the GPMI controller.
 */
static int gpmi_ecc_write_page_raw(struct nand_chip *chip, const uint8_t *buf,
				   int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct bch_geometry *nfc_geo = &this->bch_geometry;
	int eccsize = nfc_geo->ecc_chunk_size;
	int eccbits = nfc_geo->ecc_strength * nfc_geo->gf_len;
	u8 *tmp_buf = this->raw_buffer;
	uint8_t *oob = chip->oob_poi;
	size_t dst_bit_off;
	size_t oob_bit_off;
	size_t oob_byte_off;
	int step;

	/*
	 * Initialize all bits to 1 in case we don't have a buffer for the
	 * payload or oob data in order to leave unspecified bits of data
	 * to their initial state.
	 */
	if (!buf || !oob_required)
		memset(tmp_buf, 0xff, mtd->writesize + mtd->oobsize);

	/*
	 * First copy the metadata section (stored in oob buffer) at the
	 * beginning of the page, as imposed by the GPMI layout.
	 */
	memcpy(tmp_buf, oob, nfc_geo->metadata_size);
	oob_bit_off = nfc_geo->metadata_size * 8;
	dst_bit_off = oob_bit_off;

	/* Interleave payload data and ECC bits */
	for (step = 0; step < nfc_geo->ecc_chunk_count; step++) {
		if (buf)
			nand_extract_bits(tmp_buf, dst_bit_off, buf,
					  step * eccsize * 8, eccsize * 8);
		dst_bit_off += eccsize * 8;

		/* Align last ECC block to align a byte boundary */
		if (step == nfc_geo->ecc_chunk_count - 1 &&
		    (oob_bit_off + eccbits) % 8)
			eccbits += 8 - ((oob_bit_off + eccbits) % 8);

		if (oob_required)
			nand_extract_bits(tmp_buf, dst_bit_off, oob,
					  oob_bit_off, eccbits);

		dst_bit_off += eccbits;
		oob_bit_off += eccbits;
	}

	oob_byte_off = oob_bit_off / 8;

	if (oob_required && oob_byte_off < mtd->oobsize)
		memcpy(tmp_buf + mtd->writesize + oob_byte_off,
		       oob + oob_byte_off, mtd->oobsize - oob_byte_off);

	/*
	 * If required, swap the bad block marker and the first byte of the
	 * metadata section, so that we don't modify the bad block marker.
	 *
	 * See the layout description for a detailed explanation on why this
	 * is needed.
	 */
	if (this->swap_block_mark)
		swap(tmp_buf[0], tmp_buf[mtd->writesize]);

	return nand_prog_page_op(chip, page, 0, tmp_buf,
				 mtd->writesize + mtd->oobsize);
}

static int gpmi_ecc_read_oob_raw(struct nand_chip *chip, int page)
{
	return gpmi_ecc_read_page_raw(chip, NULL, 1, page);
}

static int gpmi_ecc_write_oob_raw(struct nand_chip *chip, int page)
{
	return gpmi_ecc_write_page_raw(chip, NULL, 1, page);
}

static int gpmi_block_markbad(struct nand_chip *chip, loff_t ofs)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	int ret = 0;
	uint8_t *block_mark;
	int column, page, chipnr;

	chipnr = (int)(ofs >> chip->chip_shift);
	nand_select_target(chip, chipnr);

	column = !GPMI_IS_MX23(this) ? mtd->writesize : 0;

	/* Write the block mark. */
	block_mark = this->data_buffer_dma;
	block_mark[0] = 0; /* bad block marker */

	/* Shift to get page */
	page = (int)(ofs >> chip->page_shift);

	ret = nand_prog_page_op(chip, page, column, block_mark, 1);

	nand_deselect_target(chip);

	return ret;
}

static int nand_boot_set_geometry(struct gpmi_nand_data *this)
{
	struct boot_rom_geometry *geometry = &this->rom_geometry;

	/*
	 * Set the boot block stride size.
	 *
	 * In principle, we should be reading this from the OTP bits, since
	 * that's where the ROM is going to get it. In fact, we don't have any
	 * way to read the OTP bits, so we go with the default and hope for the
	 * best.
	 */
	geometry->stride_size_in_pages = 64;

	/*
	 * Set the search area stride exponent.
	 *
	 * In principle, we should be reading this from the OTP bits, since
	 * that's where the ROM is going to get it. In fact, we don't have any
	 * way to read the OTP bits, so we go with the default and hope for the
	 * best.
	 */
	geometry->search_area_stride_exponent = 2;
	return 0;
}

static const char  *fingerprint = "STMP";
static int mx23_check_transcription_stamp(struct gpmi_nand_data *this)
{
	struct boot_rom_geometry *rom_geo = &this->rom_geometry;
	struct device *dev = this->dev;
	struct nand_chip *chip = &this->nand;
	unsigned int search_area_size_in_strides;
	unsigned int stride;
	unsigned int page;
	u8 *buffer = nand_get_data_buf(chip);
	int found_an_ncb_fingerprint = false;
	int ret;

	/* Compute the number of strides in a search area. */
	search_area_size_in_strides = 1 << rom_geo->search_area_stride_exponent;

	nand_select_target(chip, 0);

	/*
	 * Loop through the first search area, looking for the NCB fingerprint.
	 */
	dev_dbg(dev, "Scanning for an NCB fingerprint...\n");

	for (stride = 0; stride < search_area_size_in_strides; stride++) {
		/* Compute the page addresses. */
		page = stride * rom_geo->stride_size_in_pages;

		dev_dbg(dev, "Looking for a fingerprint in page 0x%x\n", page);

		/*
		 * Read the NCB fingerprint. The fingerprint is four bytes long
		 * and starts in the 12th byte of the page.
		 */
		ret = nand_read_page_op(chip, page, 12, buffer,
					strlen(fingerprint));
		if (ret)
			continue;

		/* Look for the fingerprint. */
		if (!memcmp(buffer, fingerprint, strlen(fingerprint))) {
			found_an_ncb_fingerprint = true;
			break;
		}

	}

	nand_deselect_target(chip);

	if (found_an_ncb_fingerprint)
		dev_dbg(dev, "\tFound a fingerprint\n");
	else
		dev_dbg(dev, "\tNo fingerprint found\n");
	return found_an_ncb_fingerprint;
}

/* Writes a transcription stamp. */
static int mx23_write_transcription_stamp(struct gpmi_nand_data *this)
{
	struct device *dev = this->dev;
	struct boot_rom_geometry *rom_geo = &this->rom_geometry;
	struct nand_chip *chip = &this->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int block_size_in_pages;
	unsigned int search_area_size_in_strides;
	unsigned int search_area_size_in_pages;
	unsigned int search_area_size_in_blocks;
	unsigned int block;
	unsigned int stride;
	unsigned int page;
	u8 *buffer = nand_get_data_buf(chip);
	int status;

	/* Compute the search area geometry. */
	block_size_in_pages = mtd->erasesize / mtd->writesize;
	search_area_size_in_strides = 1 << rom_geo->search_area_stride_exponent;
	search_area_size_in_pages = search_area_size_in_strides *
					rom_geo->stride_size_in_pages;
	search_area_size_in_blocks =
		  (search_area_size_in_pages + (block_size_in_pages - 1)) /
				    block_size_in_pages;

	dev_dbg(dev, "Search Area Geometry :\n");
	dev_dbg(dev, "\tin Blocks : %u\n", search_area_size_in_blocks);
	dev_dbg(dev, "\tin Strides: %u\n", search_area_size_in_strides);
	dev_dbg(dev, "\tin Pages  : %u\n", search_area_size_in_pages);

	nand_select_target(chip, 0);

	/* Loop over blocks in the first search area, erasing them. */
	dev_dbg(dev, "Erasing the search area...\n");

	for (block = 0; block < search_area_size_in_blocks; block++) {
		/* Erase this block. */
		dev_dbg(dev, "\tErasing block 0x%x\n", block);
		status = nand_erase_op(chip, block);
		if (status)
			dev_err(dev, "[%s] Erase failed.\n", __func__);
	}

	/* Write the NCB fingerprint into the page buffer. */
	memset(buffer, ~0, mtd->writesize);
	memcpy(buffer + 12, fingerprint, strlen(fingerprint));

	/* Loop through the first search area, writing NCB fingerprints. */
	dev_dbg(dev, "Writing NCB fingerprints...\n");
	for (stride = 0; stride < search_area_size_in_strides; stride++) {
		/* Compute the page addresses. */
		page = stride * rom_geo->stride_size_in_pages;

		/* Write the first page of the current stride. */
		dev_dbg(dev, "Writing an NCB fingerprint in page 0x%x\n", page);

		status = chip->ecc.write_page_raw(chip, buffer, 0, page);
		if (status)
			dev_err(dev, "[%s] Write failed.\n", __func__);
	}

	nand_deselect_target(chip);

	return 0;
}

static int mx23_boot_init(struct gpmi_nand_data  *this)
{
	struct device *dev = this->dev;
	struct nand_chip *chip = &this->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int block_count;
	unsigned int block;
	int     chipnr;
	int     page;
	loff_t  byte;
	uint8_t block_mark;
	int     ret = 0;

	/*
	 * If control arrives here, we can't use block mark swapping, which
	 * means we're forced to use transcription. First, scan for the
	 * transcription stamp. If we find it, then we don't have to do
	 * anything -- the block marks are already transcribed.
	 */
	if (mx23_check_transcription_stamp(this))
		return 0;

	/*
	 * If control arrives here, we couldn't find a transcription stamp, so
	 * so we presume the block marks are in the conventional location.
	 */
	dev_dbg(dev, "Transcribing bad block marks...\n");

	/* Compute the number of blocks in the entire medium. */
	block_count = nanddev_eraseblocks_per_target(&chip->base);

	/*
	 * Loop over all the blocks in the medium, transcribing block marks as
	 * we go.
	 */
	for (block = 0; block < block_count; block++) {
		/*
		 * Compute the chip, page and byte addresses for this block's
		 * conventional mark.
		 */
		chipnr = block >> (chip->chip_shift - chip->phys_erase_shift);
		page = block << (chip->phys_erase_shift - chip->page_shift);
		byte = block <<  chip->phys_erase_shift;

		/* Send the command to read the conventional block mark. */
		nand_select_target(chip, chipnr);
		ret = nand_read_page_op(chip, page, mtd->writesize, &block_mark,
					1);
		nand_deselect_target(chip);

		if (ret)
			continue;

		/*
		 * Check if the block is marked bad. If so, we need to mark it
		 * again, but this time the result will be a mark in the
		 * location where we transcribe block marks.
		 */
		if (block_mark != 0xff) {
			dev_dbg(dev, "Transcribing mark in block %u\n", block);
			ret = chip->legacy.block_markbad(chip, byte);
			if (ret)
				dev_err(dev,
					"Failed to mark block bad with ret %d\n",
					ret);
		}
	}

	/* Write the stamp that indicates we've transcribed the block marks. */
	mx23_write_transcription_stamp(this);
	return 0;
}

static int nand_boot_init(struct gpmi_nand_data  *this)
{
	nand_boot_set_geometry(this);

	/* This is ROM arch-specific initilization before the BBT scanning. */
	if (GPMI_IS_MX23(this))
		return mx23_boot_init(this);
	return 0;
}

static int gpmi_set_geometry(struct gpmi_nand_data *this)
{
	int ret;

	/* Free the temporary DMA memory for reading ID. */
	gpmi_free_dma_buffer(this);

	/* Set up the NFC geometry which is used by BCH. */
	ret = bch_set_geometry(this);
	if (ret) {
		dev_err(this->dev, "Error setting BCH geometry : %d\n", ret);
		return ret;
	}

	/* Alloc the new DMA buffers according to the pagesize and oobsize */
	return gpmi_alloc_dma_buffer(this);
}

static int gpmi_init_last(struct gpmi_nand_data *this)
{
	struct nand_chip *chip = &this->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	struct bch_geometry *bch_geo = &this->bch_geometry;
	int ret;

	/* Set up the medium geometry */
	ret = gpmi_set_geometry(this);
	if (ret)
		return ret;

	/* Init the nand_ecc_ctrl{} */
	ecc->read_page	= gpmi_ecc_read_page;
	ecc->write_page	= gpmi_ecc_write_page;
	ecc->read_oob	= gpmi_ecc_read_oob;
	ecc->write_oob	= gpmi_ecc_write_oob;
	ecc->read_page_raw = gpmi_ecc_read_page_raw;
	ecc->write_page_raw = gpmi_ecc_write_page_raw;
	ecc->read_oob_raw = gpmi_ecc_read_oob_raw;
	ecc->write_oob_raw = gpmi_ecc_write_oob_raw;
	ecc->engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;
	ecc->size	= bch_geo->ecc_chunk_size;
	ecc->strength	= bch_geo->ecc_strength;
	mtd_set_ooblayout(mtd, &gpmi_ooblayout_ops);

	/*
	 * We only enable the subpage read when:
	 *  (1) the chip is imx6, and
	 *  (2) the size of the ECC parity is byte aligned.
	 */
	if (GPMI_IS_MX6(this) &&
		((bch_geo->gf_len * bch_geo->ecc_strength) % 8) == 0) {
		ecc->read_subpage = gpmi_ecc_read_subpage;
		chip->options |= NAND_SUBPAGE_READ;
	}

	return 0;
}

static int gpmi_nand_attach_chip(struct nand_chip *chip)
{
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	int ret;

	if (chip->bbt_options & NAND_BBT_USE_FLASH) {
		chip->bbt_options |= NAND_BBT_NO_OOB;

		if (of_property_read_bool(this->dev->of_node,
					  "fsl,no-blockmark-swap"))
			this->swap_block_mark = false;
	}
	dev_dbg(this->dev, "Blockmark swapping %sabled\n",
		this->swap_block_mark ? "en" : "dis");

	ret = gpmi_init_last(this);
	if (ret)
		return ret;

	chip->options |= NAND_SKIP_BBTSCAN;

	return 0;
}

static struct gpmi_transfer *get_next_transfer(struct gpmi_nand_data *this)
{
	struct gpmi_transfer *transfer = &this->transfers[this->ntransfers];

	this->ntransfers++;

	if (this->ntransfers == GPMI_MAX_TRANSFERS)
		return NULL;

	return transfer;
}

static struct dma_async_tx_descriptor *gpmi_chain_command(
	struct gpmi_nand_data *this, u8 cmd, const u8 *addr, int naddr)
{
	struct dma_chan *channel = get_dma_chan(this);
	struct dma_async_tx_descriptor *desc;
	struct gpmi_transfer *transfer;
	int chip = this->nand.cur_cs;
	u32 pio[3];

	/* [1] send out the PIO words */
	pio[0] = BF_GPMI_CTRL0_COMMAND_MODE(BV_GPMI_CTRL0_COMMAND_MODE__WRITE)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(BV_GPMI_CTRL0_ADDRESS__NAND_CLE)
		| BM_GPMI_CTRL0_ADDRESS_INCREMENT
		| BF_GPMI_CTRL0_XFER_COUNT(naddr + 1);
	pio[1] = 0;
	pio[2] = 0;
	desc = mxs_dmaengine_prep_pio(channel, pio, ARRAY_SIZE(pio),
				      DMA_TRANS_NONE, 0);
	if (!desc)
		return NULL;

	transfer = get_next_transfer(this);
	if (!transfer)
		return NULL;

	transfer->cmdbuf[0] = cmd;
	if (naddr)
		memcpy(&transfer->cmdbuf[1], addr, naddr);

	sg_init_one(&transfer->sgl, transfer->cmdbuf, naddr + 1);
	dma_map_sg(this->dev, &transfer->sgl, 1, DMA_TO_DEVICE);

	transfer->direction = DMA_TO_DEVICE;

	desc = dmaengine_prep_slave_sg(channel, &transfer->sgl, 1, DMA_MEM_TO_DEV,
				       MXS_DMA_CTRL_WAIT4END);
	return desc;
}

static struct dma_async_tx_descriptor *gpmi_chain_wait_ready(
	struct gpmi_nand_data *this)
{
	struct dma_chan *channel = get_dma_chan(this);
	u32 pio[2];

	pio[0] =  BF_GPMI_CTRL0_COMMAND_MODE(BV_GPMI_CTRL0_COMMAND_MODE__WAIT_FOR_READY)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(this->nand.cur_cs, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(BV_GPMI_CTRL0_ADDRESS__NAND_DATA)
		| BF_GPMI_CTRL0_XFER_COUNT(0);
	pio[1] = 0;

	return mxs_dmaengine_prep_pio(channel, pio, 2, DMA_TRANS_NONE,
				MXS_DMA_CTRL_WAIT4END | MXS_DMA_CTRL_WAIT4RDY);
}

static struct dma_async_tx_descriptor *gpmi_chain_data_read(
	struct gpmi_nand_data *this, void *buf, int raw_len, bool *direct)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *channel = get_dma_chan(this);
	struct gpmi_transfer *transfer;
	u32 pio[6] = {};

	transfer = get_next_transfer(this);
	if (!transfer)
		return NULL;

	transfer->direction = DMA_FROM_DEVICE;

	*direct = prepare_data_dma(this, buf, raw_len, &transfer->sgl,
				   DMA_FROM_DEVICE);

	pio[0] =  BF_GPMI_CTRL0_COMMAND_MODE(BV_GPMI_CTRL0_COMMAND_MODE__READ)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(this->nand.cur_cs, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(BV_GPMI_CTRL0_ADDRESS__NAND_DATA)
		| BF_GPMI_CTRL0_XFER_COUNT(raw_len);

	if (this->bch) {
		pio[2] =  BM_GPMI_ECCCTRL_ENABLE_ECC
			| BF_GPMI_ECCCTRL_ECC_CMD(BV_GPMI_ECCCTRL_ECC_CMD__BCH_DECODE)
			| BF_GPMI_ECCCTRL_BUFFER_MASK(BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_PAGE
				| BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_AUXONLY);
		pio[3] = raw_len;
		pio[4] = transfer->sgl.dma_address;
		pio[5] = this->auxiliary_phys;
	}

	desc = mxs_dmaengine_prep_pio(channel, pio, ARRAY_SIZE(pio),
				      DMA_TRANS_NONE, 0);
	if (!desc)
		return NULL;

	if (!this->bch)
		desc = dmaengine_prep_slave_sg(channel, &transfer->sgl, 1,
					     DMA_DEV_TO_MEM,
					     MXS_DMA_CTRL_WAIT4END);

	return desc;
}

static struct dma_async_tx_descriptor *gpmi_chain_data_write(
	struct gpmi_nand_data *this, const void *buf, int raw_len)
{
	struct dma_chan *channel = get_dma_chan(this);
	struct dma_async_tx_descriptor *desc;
	struct gpmi_transfer *transfer;
	u32 pio[6] = {};

	transfer = get_next_transfer(this);
	if (!transfer)
		return NULL;

	transfer->direction = DMA_TO_DEVICE;

	prepare_data_dma(this, buf, raw_len, &transfer->sgl, DMA_TO_DEVICE);

	pio[0] = BF_GPMI_CTRL0_COMMAND_MODE(BV_GPMI_CTRL0_COMMAND_MODE__WRITE)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(this->nand.cur_cs, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(BV_GPMI_CTRL0_ADDRESS__NAND_DATA)
		| BF_GPMI_CTRL0_XFER_COUNT(raw_len);

	if (this->bch) {
		pio[2] = BM_GPMI_ECCCTRL_ENABLE_ECC
			| BF_GPMI_ECCCTRL_ECC_CMD(BV_GPMI_ECCCTRL_ECC_CMD__BCH_ENCODE)
			| BF_GPMI_ECCCTRL_BUFFER_MASK(BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_PAGE |
					BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_AUXONLY);
		pio[3] = raw_len;
		pio[4] = transfer->sgl.dma_address;
		pio[5] = this->auxiliary_phys;
	}

	desc = mxs_dmaengine_prep_pio(channel, pio, ARRAY_SIZE(pio),
				      DMA_TRANS_NONE,
				      (this->bch ? MXS_DMA_CTRL_WAIT4END : 0));
	if (!desc)
		return NULL;

	if (!this->bch)
		desc = dmaengine_prep_slave_sg(channel, &transfer->sgl, 1,
					       DMA_MEM_TO_DEV,
					       MXS_DMA_CTRL_WAIT4END);

	return desc;
}

static int gpmi_nfc_exec_op(struct nand_chip *chip,
			     const struct nand_operation *op,
			     bool check_only)
{
	const struct nand_op_instr *instr;
	struct gpmi_nand_data *this = nand_get_controller_data(chip);
	struct dma_async_tx_descriptor *desc = NULL;
	int i, ret, buf_len = 0, nbufs = 0;
	u8 cmd = 0;
	void *buf_read = NULL;
	const void *buf_write = NULL;
	bool direct = false;
	struct completion *dma_completion, *bch_completion;
	unsigned long to;

	if (check_only)
		return 0;

	this->ntransfers = 0;
	for (i = 0; i < GPMI_MAX_TRANSFERS; i++)
		this->transfers[i].direction = DMA_NONE;

	ret = pm_runtime_get_sync(this->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(this->dev);
		return ret;
	}

	/*
	 * This driver currently supports only one NAND chip. Plus, dies share
	 * the same configuration. So once timings have been applied on the
	 * controller side, they will not change anymore. When the time will
	 * come, the check on must_apply_timings will have to be dropped.
	 */
	if (this->hw.must_apply_timings) {
		this->hw.must_apply_timings = false;
		ret = gpmi_nfc_apply_timings(this);
		if (ret)
			goto out_pm;
	}

	dev_dbg(this->dev, "%s: %d instructions\n", __func__, op->ninstrs);

	for (i = 0; i < op->ninstrs; i++) {
		instr = &op->instrs[i];

		nand_op_trace("  ", instr);

		switch (instr->type) {
		case NAND_OP_WAITRDY_INSTR:
			desc = gpmi_chain_wait_ready(this);
			break;
		case NAND_OP_CMD_INSTR:
			cmd = instr->ctx.cmd.opcode;

			/*
			 * When this command has an address cycle chain it
			 * together with the address cycle
			 */
			if (i + 1 != op->ninstrs &&
			    op->instrs[i + 1].type == NAND_OP_ADDR_INSTR)
				continue;

			desc = gpmi_chain_command(this, cmd, NULL, 0);

			break;
		case NAND_OP_ADDR_INSTR:
			desc = gpmi_chain_command(this, cmd, instr->ctx.addr.addrs,
						  instr->ctx.addr.naddrs);
			break;
		case NAND_OP_DATA_OUT_INSTR:
			buf_write = instr->ctx.data.buf.out;
			buf_len = instr->ctx.data.len;
			nbufs++;

			desc = gpmi_chain_data_write(this, buf_write, buf_len);

			break;
		case NAND_OP_DATA_IN_INSTR:
			if (!instr->ctx.data.len)
				break;
			buf_read = instr->ctx.data.buf.in;
			buf_len = instr->ctx.data.len;
			nbufs++;

			desc = gpmi_chain_data_read(this, buf_read, buf_len,
						   &direct);
			break;
		}

		if (!desc) {
			ret = -ENXIO;
			goto unmap;
		}
	}

	dev_dbg(this->dev, "%s setup done\n", __func__);

	if (nbufs > 1) {
		dev_err(this->dev, "Multiple data instructions not supported\n");
		ret = -EINVAL;
		goto unmap;
	}

	if (this->bch) {
		writel(this->bch_flashlayout0,
		       this->resources.bch_regs + HW_BCH_FLASH0LAYOUT0);
		writel(this->bch_flashlayout1,
		       this->resources.bch_regs + HW_BCH_FLASH0LAYOUT1);
	}

	desc->callback = dma_irq_callback;
	desc->callback_param = this;
	dma_completion = &this->dma_done;
	bch_completion = NULL;

	init_completion(dma_completion);

	if (this->bch && buf_read) {
		writel(BM_BCH_CTRL_COMPLETE_IRQ_EN,
		       this->resources.bch_regs + HW_BCH_CTRL_SET);
		bch_completion = &this->bch_done;
		init_completion(bch_completion);
	}

	dmaengine_submit(desc);
	dma_async_issue_pending(get_dma_chan(this));

	to = wait_for_completion_timeout(dma_completion, msecs_to_jiffies(1000));
	if (!to) {
		dev_err(this->dev, "DMA timeout, last DMA\n");
		gpmi_dump_info(this);
		ret = -ETIMEDOUT;
		goto unmap;
	}

	if (this->bch && buf_read) {
		to = wait_for_completion_timeout(bch_completion, msecs_to_jiffies(1000));
		if (!to) {
			dev_err(this->dev, "BCH timeout, last DMA\n");
			gpmi_dump_info(this);
			ret = -ETIMEDOUT;
			goto unmap;
		}
	}

	writel(BM_BCH_CTRL_COMPLETE_IRQ_EN,
	       this->resources.bch_regs + HW_BCH_CTRL_CLR);
	gpmi_clear_bch(this);

	ret = 0;

unmap:
	for (i = 0; i < this->ntransfers; i++) {
		struct gpmi_transfer *transfer = &this->transfers[i];

		if (transfer->direction != DMA_NONE)
			dma_unmap_sg(this->dev, &transfer->sgl, 1,
				     transfer->direction);
	}

	if (!ret && buf_read && !direct)
		memcpy(buf_read, this->data_buffer_dma,
		       gpmi_raw_len_to_len(this, buf_len));

	this->bch = false;

out_pm:
	pm_runtime_mark_last_busy(this->dev);
	pm_runtime_put_autosuspend(this->dev);

	return ret;
}

static const struct nand_controller_ops gpmi_nand_controller_ops = {
	.attach_chip = gpmi_nand_attach_chip,
	.setup_interface = gpmi_setup_interface,
	.exec_op = gpmi_nfc_exec_op,
};

static int gpmi_nand_init(struct gpmi_nand_data *this)
{
	struct nand_chip *chip = &this->nand;
	struct mtd_info  *mtd = nand_to_mtd(chip);
	int ret;

	/* init the MTD data structures */
	mtd->name		= "gpmi-nand";
	mtd->dev.parent		= this->dev;

	/* init the nand_chip{}, we don't support a 16-bit NAND Flash bus. */
	nand_set_controller_data(chip, this);
	nand_set_flash_node(chip, this->pdev->dev.of_node);
	chip->legacy.block_markbad = gpmi_block_markbad;
	chip->badblock_pattern	= &gpmi_bbt_descr;
	chip->options		|= NAND_NO_SUBPAGE_WRITE;

	/* Set up swap_block_mark, must be set before the gpmi_set_geometry() */
	this->swap_block_mark = !GPMI_IS_MX23(this);

	/*
	 * Allocate a temporary DMA buffer for reading ID in the
	 * nand_scan_ident().
	 */
	this->bch_geometry.payload_size = 1024;
	this->bch_geometry.auxiliary_size = 128;
	ret = gpmi_alloc_dma_buffer(this);
	if (ret)
		return ret;

	nand_controller_init(&this->base);
	this->base.ops = &gpmi_nand_controller_ops;
	chip->controller = &this->base;

	ret = nand_scan(chip, GPMI_IS_MX6(this) ? 2 : 1);
	if (ret)
		goto err_out;

	ret = nand_boot_init(this);
	if (ret)
		goto err_nand_cleanup;
	ret = nand_create_bbt(chip);
	if (ret)
		goto err_nand_cleanup;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret)
		goto err_nand_cleanup;
	return 0;

err_nand_cleanup:
	nand_cleanup(chip);
err_out:
	gpmi_free_dma_buffer(this);
	return ret;
}

static const struct of_device_id gpmi_nand_id_table[] = {
	{
		.compatible = "fsl,imx23-gpmi-nand",
		.data = &gpmi_devdata_imx23,
	}, {
		.compatible = "fsl,imx28-gpmi-nand",
		.data = &gpmi_devdata_imx28,
	}, {
		.compatible = "fsl,imx6q-gpmi-nand",
		.data = &gpmi_devdata_imx6q,
	}, {
		.compatible = "fsl,imx6sx-gpmi-nand",
		.data = &gpmi_devdata_imx6sx,
	}, {
		.compatible = "fsl,imx7d-gpmi-nand",
		.data = &gpmi_devdata_imx7d,
	}, {}
};
MODULE_DEVICE_TABLE(of, gpmi_nand_id_table);

static int gpmi_nand_probe(struct platform_device *pdev)
{
	struct gpmi_nand_data *this;
	const struct of_device_id *of_id;
	int ret;

	this = devm_kzalloc(&pdev->dev, sizeof(*this), GFP_KERNEL);
	if (!this)
		return -ENOMEM;

	of_id = of_match_device(gpmi_nand_id_table, &pdev->dev);
	if (of_id) {
		this->devdata = of_id->data;
	} else {
		dev_err(&pdev->dev, "Failed to find the right device id.\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, this);
	this->pdev  = pdev;
	this->dev   = &pdev->dev;

	ret = acquire_resources(this);
	if (ret)
		goto exit_acquire_resources;

	ret = __gpmi_enable_clk(this, true);
	if (ret)
		goto exit_acquire_resources;

	pm_runtime_set_autosuspend_delay(&pdev->dev, 500);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	ret = gpmi_init(this);
	if (ret)
		goto exit_nfc_init;

	ret = gpmi_nand_init(this);
	if (ret)
		goto exit_nfc_init;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	dev_info(this->dev, "driver registered.\n");

	return 0;

exit_nfc_init:
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	release_resources(this);
exit_acquire_resources:

	return ret;
}

static int gpmi_nand_remove(struct platform_device *pdev)
{
	struct gpmi_nand_data *this = platform_get_drvdata(pdev);
	struct nand_chip *chip = &this->nand;
	int ret;

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);
	gpmi_free_dma_buffer(this);
	release_resources(this);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gpmi_pm_suspend(struct device *dev)
{
	struct gpmi_nand_data *this = dev_get_drvdata(dev);

	release_dma_channels(this);
	return 0;
}

static int gpmi_pm_resume(struct device *dev)
{
	struct gpmi_nand_data *this = dev_get_drvdata(dev);
	int ret;

	ret = acquire_dma_channels(this);
	if (ret < 0)
		return ret;

	/* re-init the GPMI registers */
	ret = gpmi_init(this);
	if (ret) {
		dev_err(this->dev, "Error setting GPMI : %d\n", ret);
		return ret;
	}

	/* Set flag to get timing setup restored for next exec_op */
	if (this->hw.clk_rate)
		this->hw.must_apply_timings = true;

	/* re-init the BCH registers */
	ret = bch_set_geometry(this);
	if (ret) {
		dev_err(this->dev, "Error setting BCH : %d\n", ret);
		return ret;
	}

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static int __maybe_unused gpmi_runtime_suspend(struct device *dev)
{
	struct gpmi_nand_data *this = dev_get_drvdata(dev);

	return __gpmi_enable_clk(this, false);
}

static int __maybe_unused gpmi_runtime_resume(struct device *dev)
{
	struct gpmi_nand_data *this = dev_get_drvdata(dev);

	return __gpmi_enable_clk(this, true);
}

static const struct dev_pm_ops gpmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(gpmi_pm_suspend, gpmi_pm_resume)
	SET_RUNTIME_PM_OPS(gpmi_runtime_suspend, gpmi_runtime_resume, NULL)
};

static struct platform_driver gpmi_nand_driver = {
	.driver = {
		.name = "gpmi-nand",
		.pm = &gpmi_pm_ops,
		.of_match_table = gpmi_nand_id_table,
	},
	.probe   = gpmi_nand_probe,
	.remove  = gpmi_nand_remove,
};
module_platform_driver(gpmi_nand_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("i.MX GPMI NAND Flash Controller Driver");
MODULE_LICENSE("GPL");
