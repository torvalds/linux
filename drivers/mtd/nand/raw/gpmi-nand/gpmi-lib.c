// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale GPMI NAND Flash Driver
 *
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc.
 */
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/slab.h>

#include "gpmi-nand.h"
#include "gpmi-regs.h"
#include "bch-regs.h"

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

int gpmi_enable_clk(struct gpmi_nand_data *this)
{
	return __gpmi_enable_clk(this, true);
}

int gpmi_disable_clk(struct gpmi_nand_data *this)
{
	return __gpmi_enable_clk(this, false);
}

int gpmi_init(struct gpmi_nand_data *this)
{
	struct resources *r = &this->resources;
	int ret;

	ret = gpmi_enable_clk(this);
	if (ret)
		return ret;
	ret = gpmi_reset_block(r->gpmi_regs, false);
	if (ret)
		goto err_out;

	/*
	 * Reset BCH here, too. We got failures otherwise :(
	 * See later BCH reset for explanation of MX23 handling
	 */
	ret = gpmi_reset_block(r->bch_regs, GPMI_IS_MX23(this));
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

	gpmi_disable_clk(this);
	return 0;
err_out:
	gpmi_disable_clk(this);
	return ret;
}

/* This function is very useful. It is called only when the bug occur. */
void gpmi_dump_info(struct gpmi_nand_data *this)
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
	unsigned int gf_len;
	int ret;

	ret = common_nfc_set_geometry(this);
	if (ret)
		return ret;

	block_count   = bch_geo->ecc_chunk_count - 1;
	block_size    = bch_geo->ecc_chunk_size;
	metadata_size = bch_geo->metadata_size;
	ecc_strength  = bch_geo->ecc_strength >> 1;
	page_size     = bch_geo->page_size;
	gf_len        = bch_geo->gf_len;

	ret = gpmi_enable_clk(this);
	if (ret)
		return ret;

	/*
	* Due to erratum #2847 of the MX23, the BCH cannot be soft reset on this
	* chip, otherwise it will lock up. So we skip resetting BCH on the MX23.
	* On the other hand, the MX28 needs the reset, because one case has been
	* seen where the BCH produced ECC errors constantly after 10000
	* consecutive reboots. The latter case has not been seen on the MX23
	* yet, still we don't know if it could happen there as well.
	*/
	ret = gpmi_reset_block(r->bch_regs, GPMI_IS_MX23(this));
	if (ret)
		goto err_out;

	/* Configure layout 0. */
	writel(BF_BCH_FLASH0LAYOUT0_NBLOCKS(block_count)
			| BF_BCH_FLASH0LAYOUT0_META_SIZE(metadata_size)
			| BF_BCH_FLASH0LAYOUT0_ECC0(ecc_strength, this)
			| BF_BCH_FLASH0LAYOUT0_GF(gf_len, this)
			| BF_BCH_FLASH0LAYOUT0_DATA0_SIZE(block_size, this),
			r->bch_regs + HW_BCH_FLASH0LAYOUT0);

	writel(BF_BCH_FLASH0LAYOUT1_PAGE_SIZE(page_size)
			| BF_BCH_FLASH0LAYOUT1_ECCN(ecc_strength, this)
			| BF_BCH_FLASH0LAYOUT1_GF(gf_len, this)
			| BF_BCH_FLASH0LAYOUT1_DATAN_SIZE(block_size, this),
			r->bch_regs + HW_BCH_FLASH0LAYOUT1);

	/* Set *all* chip selects to use layout 0. */
	writel(0, r->bch_regs + HW_BCH_LAYOUTSELECT);

	/* Enable interrupts. */
	writel(BM_BCH_CTRL_COMPLETE_IRQ_EN,
				r->bch_regs + HW_BCH_CTRL_SET);

	gpmi_disable_clk(this);
	return 0;
err_out:
	gpmi_disable_clk(this);
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

void gpmi_nfc_apply_timings(struct gpmi_nand_data *this)
{
	struct gpmi_nfc_hardware_timing *hw = &this->hw;
	struct resources *r = &this->resources;
	void __iomem *gpmi_regs = r->gpmi_regs;
	unsigned int dll_wait_time_us;

	clk_set_rate(r->clock[0], hw->clk_rate);

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
}

int gpmi_setup_data_interface(struct nand_chip *chip, int chipnr,
			      const struct nand_data_interface *conf)
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
	} else if (GPMI_IS_MX28(this) || GPMI_IS_MX6(this)) {
		/*
		 * In the imx6, all the ready/busy pins are bound
		 * together. So we only need to check chip 0.
		 */
		if (GPMI_IS_MX6(this))
			chip = 0;

		/* MX28 shares the same R/B register as MX6Q. */
		mask = MX28_BF_GPMI_STAT_READY_BUSY(1 << chip);
		reg = readl(r->gpmi_regs + HW_GPMI_STAT);
	} else
		dev_err(this->dev, "unknown arch.\n");
	return reg & mask;
}

int gpmi_send_command(struct gpmi_nand_data *this)
{
	struct dma_chan *channel = get_dma_chan(this);
	struct dma_async_tx_descriptor *desc;
	struct scatterlist *sgl;
	int chip = this->current_chip;
	int ret;
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
	if (!desc)
		return -EINVAL;

	/* [2] send out the COMMAND + ADDRESS string stored in @buffer */
	sgl = &this->cmd_sgl;

	sg_init_one(sgl, this->cmd_buffer, this->command_length);
	dma_map_sg(this->dev, sgl, 1, DMA_TO_DEVICE);
	desc = dmaengine_prep_slave_sg(channel,
				sgl, 1, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return -EINVAL;

	/* [3] submit the DMA */
	ret = start_dma_without_bch_irq(this, desc);

	dma_unmap_sg(this->dev, sgl, 1, DMA_TO_DEVICE);

	return ret;
}

int gpmi_send_data(struct gpmi_nand_data *this, const void *buf, int len)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *channel = get_dma_chan(this);
	int chip = this->current_chip;
	int ret;
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
		| BF_GPMI_CTRL0_XFER_COUNT(len);
	pio[1] = 0;
	desc = dmaengine_prep_slave_sg(channel, (struct scatterlist *)pio,
					ARRAY_SIZE(pio), DMA_TRANS_NONE, 0);
	if (!desc)
		return -EINVAL;

	/* [2] send DMA request */
	prepare_data_dma(this, buf, len, DMA_TO_DEVICE);
	desc = dmaengine_prep_slave_sg(channel, &this->data_sgl,
					1, DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return -EINVAL;

	/* [3] submit the DMA */
	ret = start_dma_without_bch_irq(this, desc);

	dma_unmap_sg(this->dev, &this->data_sgl, 1, DMA_TO_DEVICE);

	return ret;
}

int gpmi_read_data(struct gpmi_nand_data *this, void *buf, int len)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *channel = get_dma_chan(this);
	int chip = this->current_chip;
	int ret;
	u32 pio[2];
	bool direct;

	/* [1] : send PIO */
	pio[0] = BF_GPMI_CTRL0_COMMAND_MODE(BV_GPMI_CTRL0_COMMAND_MODE__READ)
		| BM_GPMI_CTRL0_WORD_LENGTH
		| BF_GPMI_CTRL0_CS(chip, this)
		| BF_GPMI_CTRL0_LOCK_CS(LOCK_CS_ENABLE, this)
		| BF_GPMI_CTRL0_ADDRESS(BV_GPMI_CTRL0_ADDRESS__NAND_DATA)
		| BF_GPMI_CTRL0_XFER_COUNT(len);
	pio[1] = 0;
	desc = dmaengine_prep_slave_sg(channel,
					(struct scatterlist *)pio,
					ARRAY_SIZE(pio), DMA_TRANS_NONE, 0);
	if (!desc)
		return -EINVAL;

	/* [2] : send DMA request */
	direct = prepare_data_dma(this, buf, len, DMA_FROM_DEVICE);
	desc = dmaengine_prep_slave_sg(channel, &this->data_sgl,
					1, DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return -EINVAL;

	/* [3] : submit the DMA */

	ret = start_dma_without_bch_irq(this, desc);

	dma_unmap_sg(this->dev, &this->data_sgl, 1, DMA_FROM_DEVICE);
	if (!direct)
		memcpy(buf, this->data_buffer_dma, len);

	return ret;
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
	if (!desc)
		return -EINVAL;

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
	if (!desc)
		return -EINVAL;

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
	if (!desc)
		return -EINVAL;

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
	if (!desc)
		return -EINVAL;

	/* [4] submit the DMA */
	return start_dma_with_bch_irq(this, desc);
}

/**
 * gpmi_copy_bits - copy bits from one memory region to another
 * @dst: destination buffer
 * @dst_bit_off: bit offset we're starting to write at
 * @src: source buffer
 * @src_bit_off: bit offset we're starting to read from
 * @nbits: number of bits to copy
 *
 * This functions copies bits from one memory region to another, and is used by
 * the GPMI driver to copy ECC sections which are not guaranteed to be byte
 * aligned.
 *
 * src and dst should not overlap.
 *
 */
void gpmi_copy_bits(u8 *dst, size_t dst_bit_off,
		    const u8 *src, size_t src_bit_off,
		    size_t nbits)
{
	size_t i;
	size_t nbytes;
	u32 src_buffer = 0;
	size_t bits_in_src_buffer = 0;

	if (!nbits)
		return;

	/*
	 * Move src and dst pointers to the closest byte pointer and store bit
	 * offsets within a byte.
	 */
	src += src_bit_off / 8;
	src_bit_off %= 8;

	dst += dst_bit_off / 8;
	dst_bit_off %= 8;

	/*
	 * Initialize the src_buffer value with bits available in the first
	 * byte of data so that we end up with a byte aligned src pointer.
	 */
	if (src_bit_off) {
		src_buffer = src[0] >> src_bit_off;
		if (nbits >= (8 - src_bit_off)) {
			bits_in_src_buffer += 8 - src_bit_off;
		} else {
			src_buffer &= GENMASK(nbits - 1, 0);
			bits_in_src_buffer += nbits;
		}
		nbits -= bits_in_src_buffer;
		src++;
	}

	/* Calculate the number of bytes that can be copied from src to dst. */
	nbytes = nbits / 8;

	/* Try to align dst to a byte boundary. */
	if (dst_bit_off) {
		if (bits_in_src_buffer < (8 - dst_bit_off) && nbytes) {
			src_buffer |= src[0] << bits_in_src_buffer;
			bits_in_src_buffer += 8;
			src++;
			nbytes--;
		}

		if (bits_in_src_buffer >= (8 - dst_bit_off)) {
			dst[0] &= GENMASK(dst_bit_off - 1, 0);
			dst[0] |= src_buffer << dst_bit_off;
			src_buffer >>= (8 - dst_bit_off);
			bits_in_src_buffer -= (8 - dst_bit_off);
			dst_bit_off = 0;
			dst++;
			if (bits_in_src_buffer > 7) {
				bits_in_src_buffer -= 8;
				dst[0] = src_buffer;
				dst++;
				src_buffer >>= 8;
			}
		}
	}

	if (!bits_in_src_buffer && !dst_bit_off) {
		/*
		 * Both src and dst pointers are byte aligned, thus we can
		 * just use the optimized memcpy function.
		 */
		if (nbytes)
			memcpy(dst, src, nbytes);
	} else {
		/*
		 * src buffer is not byte aligned, hence we have to copy each
		 * src byte to the src_buffer variable before extracting a byte
		 * to store in dst.
		 */
		for (i = 0; i < nbytes; i++) {
			src_buffer |= src[i] << bits_in_src_buffer;
			dst[i] = src_buffer;
			src_buffer >>= 8;
		}
	}
	/* Update dst and src pointers */
	dst += nbytes;
	src += nbytes;

	/*
	 * nbits is the number of remaining bits. It should not exceed 8 as
	 * we've already copied as much bytes as possible.
	 */
	nbits %= 8;

	/*
	 * If there's no more bits to copy to the destination and src buffer
	 * was already byte aligned, then we're done.
	 */
	if (!nbits && !bits_in_src_buffer)
		return;

	/* Copy the remaining bits to src_buffer */
	if (nbits)
		src_buffer |= (*src & GENMASK(nbits - 1, 0)) <<
			      bits_in_src_buffer;
	bits_in_src_buffer += nbits;

	/*
	 * In case there were not enough bits to get a byte aligned dst buffer
	 * prepare the src_buffer variable to match the dst organization (shift
	 * src_buffer by dst_bit_off and retrieve the least significant bits
	 * from dst).
	 */
	if (dst_bit_off)
		src_buffer = (src_buffer << dst_bit_off) |
			     (*dst & GENMASK(dst_bit_off - 1, 0));
	bits_in_src_buffer += dst_bit_off;

	/*
	 * Keep most significant bits from dst if we end up with an unaligned
	 * number of bits.
	 */
	nbytes = bits_in_src_buffer / 8;
	if (bits_in_src_buffer % 8) {
		src_buffer |= (dst[nbytes] &
			       GENMASK(7, bits_in_src_buffer % 8)) <<
			      (nbytes * 8);
		nbytes++;
	}

	/* Copy the remaining bytes to dst */
	for (i = 0; i < nbytes; i++) {
		dst[i] = src_buffer;
		src_buffer >>= 8;
	}
}
