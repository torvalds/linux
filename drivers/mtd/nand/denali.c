/*
 * NAND Flash Controller Device Driver
 * Copyright © 2009-2010, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/module.h>

#include "denali.h"

MODULE_LICENSE("GPL");

/*
 * We define a module parameter that allows the user to override
 * the hardware and decide what timing mode should be used.
 */
#define NAND_DEFAULT_TIMINGS	-1

static int onfi_timing_mode = NAND_DEFAULT_TIMINGS;
module_param(onfi_timing_mode, int, S_IRUGO);
MODULE_PARM_DESC(onfi_timing_mode,
	   "Overrides default ONFI setting. -1 indicates use default timings");

#define DENALI_NAND_NAME    "denali-nand"

/*
 * We define a macro here that combines all interrupts this driver uses into
 * a single constant value, for convenience.
 */
#define DENALI_IRQ_ALL	(INTR__DMA_CMD_COMP | \
			INTR__ECC_TRANSACTION_DONE | \
			INTR__ECC_ERR | \
			INTR__PROGRAM_FAIL | \
			INTR__LOAD_COMP | \
			INTR__PROGRAM_COMP | \
			INTR__TIME_OUT | \
			INTR__ERASE_FAIL | \
			INTR__RST_COMP | \
			INTR__ERASE_COMP)

/*
 * indicates whether or not the internal value for the flash bank is
 * valid or not
 */
#define CHIP_SELECT_INVALID	-1

/*
 * This macro divides two integers and rounds fractional values up
 * to the nearest integer value.
 */
#define CEIL_DIV(X, Y) (((X)%(Y)) ? ((X)/(Y)+1) : ((X)/(Y)))

/*
 * this macro allows us to convert from an MTD structure to our own
 * device context (denali) structure.
 */
static inline struct denali_nand_info *mtd_to_denali(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct denali_nand_info, nand);
}

/*
 * These constants are defined by the driver to enable common driver
 * configuration options.
 */
#define SPARE_ACCESS		0x41
#define MAIN_ACCESS		0x42
#define MAIN_SPARE_ACCESS	0x43

#define DENALI_READ	0
#define DENALI_WRITE	0x100

/*
 * this is a helper macro that allows us to
 * format the bank into the proper bits for the controller
 */
#define BANK(x) ((x) << 24)

/* forward declarations */
static void clear_interrupts(struct denali_nand_info *denali);
static uint32_t wait_for_irq(struct denali_nand_info *denali,
							uint32_t irq_mask);
static void denali_irq_enable(struct denali_nand_info *denali,
							uint32_t int_mask);
static uint32_t read_interrupt_status(struct denali_nand_info *denali);

/*
 * Certain operations for the denali NAND controller use an indexed mode to
 * read/write data. The operation is performed by writing the address value
 * of the command to the device memory followed by the data. This function
 * abstracts this common operation.
 */
static void index_addr(struct denali_nand_info *denali,
				uint32_t address, uint32_t data)
{
	iowrite32(address, denali->flash_mem);
	iowrite32(data, denali->flash_mem + 0x10);
}

/* Perform an indexed read of the device */
static void index_addr_read_data(struct denali_nand_info *denali,
				 uint32_t address, uint32_t *pdata)
{
	iowrite32(address, denali->flash_mem);
	*pdata = ioread32(denali->flash_mem + 0x10);
}

/*
 * We need to buffer some data for some of the NAND core routines.
 * The operations manage buffering that data.
 */
static void reset_buf(struct denali_nand_info *denali)
{
	denali->buf.head = denali->buf.tail = 0;
}

static void write_byte_to_buf(struct denali_nand_info *denali, uint8_t byte)
{
	denali->buf.buf[denali->buf.tail++] = byte;
}

/* reads the status of the device */
static void read_status(struct denali_nand_info *denali)
{
	uint32_t cmd;

	/* initialize the data buffer to store status */
	reset_buf(denali);

	cmd = ioread32(denali->flash_reg + WRITE_PROTECT);
	if (cmd)
		write_byte_to_buf(denali, NAND_STATUS_WP);
	else
		write_byte_to_buf(denali, 0);
}

/* resets a specific device connected to the core */
static void reset_bank(struct denali_nand_info *denali)
{
	uint32_t irq_status;
	uint32_t irq_mask = INTR__RST_COMP | INTR__TIME_OUT;

	clear_interrupts(denali);

	iowrite32(1 << denali->flash_bank, denali->flash_reg + DEVICE_RESET);

	irq_status = wait_for_irq(denali, irq_mask);

	if (irq_status & INTR__TIME_OUT)
		dev_err(denali->dev, "reset bank failed.\n");
}

/* Reset the flash controller */
static uint16_t denali_nand_reset(struct denali_nand_info *denali)
{
	int i;

	for (i = 0; i < denali->max_banks; i++)
		iowrite32(INTR__RST_COMP | INTR__TIME_OUT,
		denali->flash_reg + INTR_STATUS(i));

	for (i = 0; i < denali->max_banks; i++) {
		iowrite32(1 << i, denali->flash_reg + DEVICE_RESET);
		while (!(ioread32(denali->flash_reg + INTR_STATUS(i)) &
			(INTR__RST_COMP | INTR__TIME_OUT)))
			cpu_relax();
		if (ioread32(denali->flash_reg + INTR_STATUS(i)) &
			INTR__TIME_OUT)
			dev_dbg(denali->dev,
			"NAND Reset operation timed out on bank %d\n", i);
	}

	for (i = 0; i < denali->max_banks; i++)
		iowrite32(INTR__RST_COMP | INTR__TIME_OUT,
			  denali->flash_reg + INTR_STATUS(i));

	return PASS;
}

/*
 * this routine calculates the ONFI timing values for a given mode and
 * programs the clocking register accordingly. The mode is determined by
 * the get_onfi_nand_para routine.
 */
static void nand_onfi_timing_set(struct denali_nand_info *denali,
								uint16_t mode)
{
	uint16_t Trea[6] = {40, 30, 25, 20, 20, 16};
	uint16_t Trp[6] = {50, 25, 17, 15, 12, 10};
	uint16_t Treh[6] = {30, 15, 15, 10, 10, 7};
	uint16_t Trc[6] = {100, 50, 35, 30, 25, 20};
	uint16_t Trhoh[6] = {0, 15, 15, 15, 15, 15};
	uint16_t Trloh[6] = {0, 0, 0, 0, 5, 5};
	uint16_t Tcea[6] = {100, 45, 30, 25, 25, 25};
	uint16_t Tadl[6] = {200, 100, 100, 100, 70, 70};
	uint16_t Trhw[6] = {200, 100, 100, 100, 100, 100};
	uint16_t Trhz[6] = {200, 100, 100, 100, 100, 100};
	uint16_t Twhr[6] = {120, 80, 80, 60, 60, 60};
	uint16_t Tcs[6] = {70, 35, 25, 25, 20, 15};

	uint16_t data_invalid_rhoh, data_invalid_rloh, data_invalid;
	uint16_t dv_window = 0;
	uint16_t en_lo, en_hi;
	uint16_t acc_clks;
	uint16_t addr_2_data, re_2_we, re_2_re, we_2_re, cs_cnt;

	en_lo = CEIL_DIV(Trp[mode], CLK_X);
	en_hi = CEIL_DIV(Treh[mode], CLK_X);
#if ONFI_BLOOM_TIME
	if ((en_hi * CLK_X) < (Treh[mode] + 2))
		en_hi++;
#endif

	if ((en_lo + en_hi) * CLK_X < Trc[mode])
		en_lo += CEIL_DIV((Trc[mode] - (en_lo + en_hi) * CLK_X), CLK_X);

	if ((en_lo + en_hi) < CLK_MULTI)
		en_lo += CLK_MULTI - en_lo - en_hi;

	while (dv_window < 8) {
		data_invalid_rhoh = en_lo * CLK_X + Trhoh[mode];

		data_invalid_rloh = (en_lo + en_hi) * CLK_X + Trloh[mode];

		data_invalid = data_invalid_rhoh < data_invalid_rloh ?
					data_invalid_rhoh : data_invalid_rloh;

		dv_window = data_invalid - Trea[mode];

		if (dv_window < 8)
			en_lo++;
	}

	acc_clks = CEIL_DIV(Trea[mode], CLK_X);

	while (acc_clks * CLK_X - Trea[mode] < 3)
		acc_clks++;

	if (data_invalid - acc_clks * CLK_X < 2)
		dev_warn(denali->dev, "%s, Line %d: Warning!\n",
			 __FILE__, __LINE__);

	addr_2_data = CEIL_DIV(Tadl[mode], CLK_X);
	re_2_we = CEIL_DIV(Trhw[mode], CLK_X);
	re_2_re = CEIL_DIV(Trhz[mode], CLK_X);
	we_2_re = CEIL_DIV(Twhr[mode], CLK_X);
	cs_cnt = CEIL_DIV((Tcs[mode] - Trp[mode]), CLK_X);
	if (cs_cnt == 0)
		cs_cnt = 1;

	if (Tcea[mode]) {
		while (cs_cnt * CLK_X + Trea[mode] < Tcea[mode])
			cs_cnt++;
	}

#if MODE5_WORKAROUND
	if (mode == 5)
		acc_clks = 5;
#endif

	/* Sighting 3462430: Temporary hack for MT29F128G08CJABAWP:B */
	if (ioread32(denali->flash_reg + MANUFACTURER_ID) == 0 &&
		ioread32(denali->flash_reg + DEVICE_ID) == 0x88)
		acc_clks = 6;

	iowrite32(acc_clks, denali->flash_reg + ACC_CLKS);
	iowrite32(re_2_we, denali->flash_reg + RE_2_WE);
	iowrite32(re_2_re, denali->flash_reg + RE_2_RE);
	iowrite32(we_2_re, denali->flash_reg + WE_2_RE);
	iowrite32(addr_2_data, denali->flash_reg + ADDR_2_DATA);
	iowrite32(en_lo, denali->flash_reg + RDWR_EN_LO_CNT);
	iowrite32(en_hi, denali->flash_reg + RDWR_EN_HI_CNT);
	iowrite32(cs_cnt, denali->flash_reg + CS_SETUP_CNT);
}

/* queries the NAND device to see what ONFI modes it supports. */
static uint16_t get_onfi_nand_para(struct denali_nand_info *denali)
{
	int i;

	/*
	 * we needn't to do a reset here because driver has already
	 * reset all the banks before
	 */
	if (!(ioread32(denali->flash_reg + ONFI_TIMING_MODE) &
		ONFI_TIMING_MODE__VALUE))
		return FAIL;

	for (i = 5; i > 0; i--) {
		if (ioread32(denali->flash_reg + ONFI_TIMING_MODE) &
			(0x01 << i))
			break;
	}

	nand_onfi_timing_set(denali, i);

	/*
	 * By now, all the ONFI devices we know support the page cache
	 * rw feature. So here we enable the pipeline_rw_ahead feature
	 */
	/* iowrite32(1, denali->flash_reg + CACHE_WRITE_ENABLE); */
	/* iowrite32(1, denali->flash_reg + CACHE_READ_ENABLE);  */

	return PASS;
}

static void get_samsung_nand_para(struct denali_nand_info *denali,
							uint8_t device_id)
{
	if (device_id == 0xd3) { /* Samsung K9WAG08U1A */
		/* Set timing register values according to datasheet */
		iowrite32(5, denali->flash_reg + ACC_CLKS);
		iowrite32(20, denali->flash_reg + RE_2_WE);
		iowrite32(12, denali->flash_reg + WE_2_RE);
		iowrite32(14, denali->flash_reg + ADDR_2_DATA);
		iowrite32(3, denali->flash_reg + RDWR_EN_LO_CNT);
		iowrite32(2, denali->flash_reg + RDWR_EN_HI_CNT);
		iowrite32(2, denali->flash_reg + CS_SETUP_CNT);
	}
}

static void get_toshiba_nand_para(struct denali_nand_info *denali)
{
	/*
	 * Workaround to fix a controller bug which reports a wrong
	 * spare area size for some kind of Toshiba NAND device
	 */
	if ((ioread32(denali->flash_reg + DEVICE_MAIN_AREA_SIZE) == 4096) &&
		(ioread32(denali->flash_reg + DEVICE_SPARE_AREA_SIZE) == 64))
		iowrite32(216, denali->flash_reg + DEVICE_SPARE_AREA_SIZE);
}

static void get_hynix_nand_para(struct denali_nand_info *denali,
							uint8_t device_id)
{
	switch (device_id) {
	case 0xD5: /* Hynix H27UAG8T2A, H27UBG8U5A or H27UCG8VFA */
	case 0xD7: /* Hynix H27UDG8VEM, H27UCG8UDM or H27UCG8V5A */
		iowrite32(128, denali->flash_reg + PAGES_PER_BLOCK);
		iowrite32(4096, denali->flash_reg + DEVICE_MAIN_AREA_SIZE);
		iowrite32(224, denali->flash_reg + DEVICE_SPARE_AREA_SIZE);
		iowrite32(0, denali->flash_reg + DEVICE_WIDTH);
		break;
	default:
		dev_warn(denali->dev,
			 "Unknown Hynix NAND (Device ID: 0x%x).\n"
			 "Will use default parameter values instead.\n",
			 device_id);
	}
}

/*
 * determines how many NAND chips are connected to the controller. Note for
 * Intel CE4100 devices we don't support more than one device.
 */
static void find_valid_banks(struct denali_nand_info *denali)
{
	uint32_t id[denali->max_banks];
	int i;

	denali->total_used_banks = 1;
	for (i = 0; i < denali->max_banks; i++) {
		index_addr(denali, MODE_11 | (i << 24) | 0, 0x90);
		index_addr(denali, MODE_11 | (i << 24) | 1, 0);
		index_addr_read_data(denali, MODE_11 | (i << 24) | 2, &id[i]);

		dev_dbg(denali->dev,
			"Return 1st ID for bank[%d]: %x\n", i, id[i]);

		if (i == 0) {
			if (!(id[i] & 0x0ff))
				break; /* WTF? */
		} else {
			if ((id[i] & 0x0ff) == (id[0] & 0x0ff))
				denali->total_used_banks++;
			else
				break;
		}
	}

	if (denali->platform == INTEL_CE4100) {
		/*
		 * Platform limitations of the CE4100 device limit
		 * users to a single chip solution for NAND.
		 * Multichip support is not enabled.
		 */
		if (denali->total_used_banks != 1) {
			dev_err(denali->dev,
				"Sorry, Intel CE4100 only supports a single NAND device.\n");
			BUG();
		}
	}
	dev_dbg(denali->dev,
		"denali->total_used_banks: %d\n", denali->total_used_banks);
}

/*
 * Use the configuration feature register to determine the maximum number of
 * banks that the hardware supports.
 */
static void detect_max_banks(struct denali_nand_info *denali)
{
	uint32_t features = ioread32(denali->flash_reg + FEATURES);

	denali->max_banks = 1 << (features & FEATURES__N_BANKS);

	/* the encoding changed from rev 5.0 to 5.1 */
	if (denali->revision < 0x0501)
		denali->max_banks <<= 1;
}

static uint16_t denali_nand_timing_set(struct denali_nand_info *denali)
{
	uint16_t status = PASS;
	uint32_t id_bytes[8], addr;
	uint8_t maf_id, device_id;
	int i;

	/*
	 * Use read id method to get device ID and other params.
	 * For some NAND chips, controller can't report the correct
	 * device ID by reading from DEVICE_ID register
	 */
	addr = MODE_11 | BANK(denali->flash_bank);
	index_addr(denali, addr | 0, 0x90);
	index_addr(denali, addr | 1, 0);
	for (i = 0; i < 8; i++)
		index_addr_read_data(denali, addr | 2, &id_bytes[i]);
	maf_id = id_bytes[0];
	device_id = id_bytes[1];

	if (ioread32(denali->flash_reg + ONFI_DEVICE_NO_OF_LUNS) &
		ONFI_DEVICE_NO_OF_LUNS__ONFI_DEVICE) { /* ONFI 1.0 NAND */
		if (FAIL == get_onfi_nand_para(denali))
			return FAIL;
	} else if (maf_id == 0xEC) { /* Samsung NAND */
		get_samsung_nand_para(denali, device_id);
	} else if (maf_id == 0x98) { /* Toshiba NAND */
		get_toshiba_nand_para(denali);
	} else if (maf_id == 0xAD) { /* Hynix NAND */
		get_hynix_nand_para(denali, device_id);
	}

	dev_info(denali->dev,
			"Dump timing register values:\n"
			"acc_clks: %d, re_2_we: %d, re_2_re: %d\n"
			"we_2_re: %d, addr_2_data: %d, rdwr_en_lo_cnt: %d\n"
			"rdwr_en_hi_cnt: %d, cs_setup_cnt: %d\n",
			ioread32(denali->flash_reg + ACC_CLKS),
			ioread32(denali->flash_reg + RE_2_WE),
			ioread32(denali->flash_reg + RE_2_RE),
			ioread32(denali->flash_reg + WE_2_RE),
			ioread32(denali->flash_reg + ADDR_2_DATA),
			ioread32(denali->flash_reg + RDWR_EN_LO_CNT),
			ioread32(denali->flash_reg + RDWR_EN_HI_CNT),
			ioread32(denali->flash_reg + CS_SETUP_CNT));

	find_valid_banks(denali);

	/*
	 * If the user specified to override the default timings
	 * with a specific ONFI mode, we apply those changes here.
	 */
	if (onfi_timing_mode != NAND_DEFAULT_TIMINGS)
		nand_onfi_timing_set(denali, onfi_timing_mode);

	return status;
}

static void denali_set_intr_modes(struct denali_nand_info *denali,
					uint16_t INT_ENABLE)
{
	if (INT_ENABLE)
		iowrite32(1, denali->flash_reg + GLOBAL_INT_ENABLE);
	else
		iowrite32(0, denali->flash_reg + GLOBAL_INT_ENABLE);
}

/*
 * validation function to verify that the controlling software is making
 * a valid request
 */
static inline bool is_flash_bank_valid(int flash_bank)
{
	return flash_bank >= 0 && flash_bank < 4;
}

static void denali_irq_init(struct denali_nand_info *denali)
{
	uint32_t int_mask;
	int i;

	/* Disable global interrupts */
	denali_set_intr_modes(denali, false);

	int_mask = DENALI_IRQ_ALL;

	/* Clear all status bits */
	for (i = 0; i < denali->max_banks; ++i)
		iowrite32(0xFFFF, denali->flash_reg + INTR_STATUS(i));

	denali_irq_enable(denali, int_mask);
}

static void denali_irq_cleanup(int irqnum, struct denali_nand_info *denali)
{
	denali_set_intr_modes(denali, false);
}

static void denali_irq_enable(struct denali_nand_info *denali,
							uint32_t int_mask)
{
	int i;

	for (i = 0; i < denali->max_banks; ++i)
		iowrite32(int_mask, denali->flash_reg + INTR_EN(i));
}

/*
 * This function only returns when an interrupt that this driver cares about
 * occurs. This is to reduce the overhead of servicing interrupts
 */
static inline uint32_t denali_irq_detected(struct denali_nand_info *denali)
{
	return read_interrupt_status(denali) & DENALI_IRQ_ALL;
}

/* Interrupts are cleared by writing a 1 to the appropriate status bit */
static inline void clear_interrupt(struct denali_nand_info *denali,
							uint32_t irq_mask)
{
	uint32_t intr_status_reg;

	intr_status_reg = INTR_STATUS(denali->flash_bank);

	iowrite32(irq_mask, denali->flash_reg + intr_status_reg);
}

static void clear_interrupts(struct denali_nand_info *denali)
{
	uint32_t status;

	spin_lock_irq(&denali->irq_lock);

	status = read_interrupt_status(denali);
	clear_interrupt(denali, status);

	denali->irq_status = 0x0;
	spin_unlock_irq(&denali->irq_lock);
}

static uint32_t read_interrupt_status(struct denali_nand_info *denali)
{
	uint32_t intr_status_reg;

	intr_status_reg = INTR_STATUS(denali->flash_bank);

	return ioread32(denali->flash_reg + intr_status_reg);
}

/*
 * This is the interrupt service routine. It handles all interrupts
 * sent to this device. Note that on CE4100, this is a shared interrupt.
 */
static irqreturn_t denali_isr(int irq, void *dev_id)
{
	struct denali_nand_info *denali = dev_id;
	uint32_t irq_status;
	irqreturn_t result = IRQ_NONE;

	spin_lock(&denali->irq_lock);

	/* check to see if a valid NAND chip has been selected. */
	if (is_flash_bank_valid(denali->flash_bank)) {
		/*
		 * check to see if controller generated the interrupt,
		 * since this is a shared interrupt
		 */
		irq_status = denali_irq_detected(denali);
		if (irq_status != 0) {
			/* handle interrupt */
			/* first acknowledge it */
			clear_interrupt(denali, irq_status);
			/*
			 * store the status in the device context for someone
			 * to read
			 */
			denali->irq_status |= irq_status;
			/* notify anyone who cares that it happened */
			complete(&denali->complete);
			/* tell the OS that we've handled this */
			result = IRQ_HANDLED;
		}
	}
	spin_unlock(&denali->irq_lock);
	return result;
}

static uint32_t wait_for_irq(struct denali_nand_info *denali, uint32_t irq_mask)
{
	unsigned long comp_res;
	uint32_t intr_status;
	unsigned long timeout = msecs_to_jiffies(1000);

	do {
		comp_res =
			wait_for_completion_timeout(&denali->complete, timeout);
		spin_lock_irq(&denali->irq_lock);
		intr_status = denali->irq_status;

		if (intr_status & irq_mask) {
			denali->irq_status &= ~irq_mask;
			spin_unlock_irq(&denali->irq_lock);
			/* our interrupt was detected */
			break;
		}

		/*
		 * these are not the interrupts you are looking for -
		 * need to wait again
		 */
		spin_unlock_irq(&denali->irq_lock);
	} while (comp_res != 0);

	if (comp_res == 0) {
		/* timeout */
		pr_err("timeout occurred, status = 0x%x, mask = 0x%x\n",
				intr_status, irq_mask);

		intr_status = 0;
	}
	return intr_status;
}

/*
 * This helper function setups the registers for ECC and whether or not
 * the spare area will be transferred.
 */
static void setup_ecc_for_xfer(struct denali_nand_info *denali, bool ecc_en,
				bool transfer_spare)
{
	int ecc_en_flag, transfer_spare_flag;

	/* set ECC, transfer spare bits if needed */
	ecc_en_flag = ecc_en ? ECC_ENABLE__FLAG : 0;
	transfer_spare_flag = transfer_spare ? TRANSFER_SPARE_REG__FLAG : 0;

	/* Enable spare area/ECC per user's request. */
	iowrite32(ecc_en_flag, denali->flash_reg + ECC_ENABLE);
	iowrite32(transfer_spare_flag, denali->flash_reg + TRANSFER_SPARE_REG);
}

/*
 * sends a pipeline command operation to the controller. See the Denali NAND
 * controller's user guide for more information (section 4.2.3.6).
 */
static int denali_send_pipeline_cmd(struct denali_nand_info *denali,
				    bool ecc_en, bool transfer_spare,
				    int access_type, int op)
{
	int status = PASS;
	uint32_t addr, cmd;

	setup_ecc_for_xfer(denali, ecc_en, transfer_spare);

	clear_interrupts(denali);

	addr = BANK(denali->flash_bank) | denali->page;

	if (op == DENALI_WRITE && access_type != SPARE_ACCESS) {
		cmd = MODE_01 | addr;
		iowrite32(cmd, denali->flash_mem);
	} else if (op == DENALI_WRITE && access_type == SPARE_ACCESS) {
		/* read spare area */
		cmd = MODE_10 | addr;
		index_addr(denali, cmd, access_type);

		cmd = MODE_01 | addr;
		iowrite32(cmd, denali->flash_mem);
	} else if (op == DENALI_READ) {
		/* setup page read request for access type */
		cmd = MODE_10 | addr;
		index_addr(denali, cmd, access_type);

		cmd = MODE_01 | addr;
		iowrite32(cmd, denali->flash_mem);
	}
	return status;
}

/* helper function that simply writes a buffer to the flash */
static int write_data_to_flash_mem(struct denali_nand_info *denali,
				   const uint8_t *buf, int len)
{
	uint32_t *buf32;
	int i;

	/*
	 * verify that the len is a multiple of 4.
	 * see comment in read_data_from_flash_mem()
	 */
	BUG_ON((len % 4) != 0);

	/* write the data to the flash memory */
	buf32 = (uint32_t *)buf;
	for (i = 0; i < len / 4; i++)
		iowrite32(*buf32++, denali->flash_mem + 0x10);
	return i * 4; /* intent is to return the number of bytes read */
}

/* helper function that simply reads a buffer from the flash */
static int read_data_from_flash_mem(struct denali_nand_info *denali,
				    uint8_t *buf, int len)
{
	uint32_t *buf32;
	int i;

	/*
	 * we assume that len will be a multiple of 4, if not it would be nice
	 * to know about it ASAP rather than have random failures...
	 * This assumption is based on the fact that this function is designed
	 * to be used to read flash pages, which are typically multiples of 4.
	 */
	BUG_ON((len % 4) != 0);

	/* transfer the data from the flash */
	buf32 = (uint32_t *)buf;
	for (i = 0; i < len / 4; i++)
		*buf32++ = ioread32(denali->flash_mem + 0x10);
	return i * 4; /* intent is to return the number of bytes read */
}

/* writes OOB data to the device */
static int write_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t irq_status;
	uint32_t irq_mask = INTR__PROGRAM_COMP | INTR__PROGRAM_FAIL;
	int status = 0;

	denali->page = page;

	if (denali_send_pipeline_cmd(denali, false, false, SPARE_ACCESS,
							DENALI_WRITE) == PASS) {
		write_data_to_flash_mem(denali, buf, mtd->oobsize);

		/* wait for operation to complete */
		irq_status = wait_for_irq(denali, irq_mask);

		if (irq_status == 0) {
			dev_err(denali->dev, "OOB write failed\n");
			status = -EIO;
		}
	} else {
		dev_err(denali->dev, "unable to send pipeline command\n");
		status = -EIO;
	}
	return status;
}

/* reads OOB data from the device */
static void read_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t irq_mask = INTR__LOAD_COMP;
	uint32_t irq_status, addr, cmd;

	denali->page = page;

	if (denali_send_pipeline_cmd(denali, false, true, SPARE_ACCESS,
							DENALI_READ) == PASS) {
		read_data_from_flash_mem(denali, buf, mtd->oobsize);

		/*
		 * wait for command to be accepted
		 * can always use status0 bit as the
		 * mask is identical for each bank.
		 */
		irq_status = wait_for_irq(denali, irq_mask);

		if (irq_status == 0)
			dev_err(denali->dev, "page on OOB timeout %d\n",
					denali->page);

		/*
		 * We set the device back to MAIN_ACCESS here as I observed
		 * instability with the controller if you do a block erase
		 * and the last transaction was a SPARE_ACCESS. Block erase
		 * is reliable (according to the MTD test infrastructure)
		 * if you are in MAIN_ACCESS.
		 */
		addr = BANK(denali->flash_bank) | denali->page;
		cmd = MODE_10 | addr;
		index_addr(denali, cmd, MAIN_ACCESS);
	}
}

static int denali_check_erased_page(struct mtd_info *mtd,
				    struct nand_chip *chip, uint8_t *buf,
				    unsigned long uncor_ecc_flags,
				    unsigned int max_bitflips)
{
	uint8_t *ecc_code = chip->buffers->ecccode;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	int i, ret, stat;

	ret = mtd_ooblayout_get_eccbytes(mtd, ecc_code, chip->oob_poi, 0,
					 chip->ecc.total);
	if (ret)
		return ret;

	for (i = 0; i < ecc_steps; i++) {
		if (!(uncor_ecc_flags & BIT(i)))
			continue;

		stat = nand_check_erased_ecc_chunk(buf, ecc_size,
						  ecc_code, ecc_bytes,
						  NULL, 0,
						  chip->ecc.strength);
		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}

		buf += ecc_size;
		ecc_code += ecc_bytes;
	}

	return max_bitflips;
}

static int denali_hw_ecc_fixup(struct mtd_info *mtd,
			       struct denali_nand_info *denali,
			       unsigned long *uncor_ecc_flags)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	int bank = denali->flash_bank;
	uint32_t ecc_cor;
	unsigned int max_bitflips;

	ecc_cor = ioread32(denali->flash_reg + ECC_COR_INFO(bank));
	ecc_cor >>= ECC_COR_INFO__SHIFT(bank);

	if (ecc_cor & ECC_COR_INFO__UNCOR_ERR) {
		/*
		 * This flag is set when uncorrectable error occurs at least in
		 * one ECC sector.  We can not know "how many sectors", or
		 * "which sector(s)".  We need erase-page check for all sectors.
		 */
		*uncor_ecc_flags = GENMASK(chip->ecc.steps - 1, 0);
		return 0;
	}

	max_bitflips = ecc_cor & ECC_COR_INFO__MAX_ERRORS;

	/*
	 * The register holds the maximum of per-sector corrected bitflips.
	 * This is suitable for the return value of the ->read_page() callback.
	 * Unfortunately, we can not know the total number of corrected bits in
	 * the page.  Increase the stats by max_bitflips. (compromised solution)
	 */
	mtd->ecc_stats.corrected += max_bitflips;

	return max_bitflips;
}

#define ECC_SECTOR_SIZE 512

#define ECC_SECTOR(x)	(((x) & ECC_ERROR_ADDRESS__SECTOR_NR) >> 12)
#define ECC_BYTE(x)	(((x) & ECC_ERROR_ADDRESS__OFFSET))
#define ECC_CORRECTION_VALUE(x) ((x) & ERR_CORRECTION_INFO__BYTEMASK)
#define ECC_ERROR_UNCORRECTABLE(x) ((x) & ERR_CORRECTION_INFO__ERROR_TYPE)
#define ECC_ERR_DEVICE(x)	(((x) & ERR_CORRECTION_INFO__DEVICE_NR) >> 8)
#define ECC_LAST_ERR(x)		((x) & ERR_CORRECTION_INFO__LAST_ERR_INFO)

static int denali_sw_ecc_fixup(struct mtd_info *mtd,
			       struct denali_nand_info *denali,
			       unsigned long *uncor_ecc_flags, uint8_t *buf)
{
	unsigned int bitflips = 0;
	unsigned int max_bitflips = 0;
	uint32_t err_addr, err_cor_info;
	unsigned int err_byte, err_sector, err_device;
	uint8_t err_cor_value;
	unsigned int prev_sector = 0;

	/* read the ECC errors. we'll ignore them for now */
	denali_set_intr_modes(denali, false);

	do {
		err_addr = ioread32(denali->flash_reg + ECC_ERROR_ADDRESS);
		err_sector = ECC_SECTOR(err_addr);
		err_byte = ECC_BYTE(err_addr);

		err_cor_info = ioread32(denali->flash_reg + ERR_CORRECTION_INFO);
		err_cor_value = ECC_CORRECTION_VALUE(err_cor_info);
		err_device = ECC_ERR_DEVICE(err_cor_info);

		/* reset the bitflip counter when crossing ECC sector */
		if (err_sector != prev_sector)
			bitflips = 0;

		if (ECC_ERROR_UNCORRECTABLE(err_cor_info)) {
			/*
			 * Check later if this is a real ECC error, or
			 * an erased sector.
			 */
			*uncor_ecc_flags |= BIT(err_sector);
		} else if (err_byte < ECC_SECTOR_SIZE) {
			/*
			 * If err_byte is larger than ECC_SECTOR_SIZE, means error
			 * happened in OOB, so we ignore it. It's no need for
			 * us to correct it err_device is represented the NAND
			 * error bits are happened in if there are more than
			 * one NAND connected.
			 */
			int offset;
			unsigned int flips_in_byte;

			offset = (err_sector * ECC_SECTOR_SIZE + err_byte) *
						denali->devnum + err_device;

			/* correct the ECC error */
			flips_in_byte = hweight8(buf[offset] ^ err_cor_value);
			buf[offset] ^= err_cor_value;
			mtd->ecc_stats.corrected += flips_in_byte;
			bitflips += flips_in_byte;

			max_bitflips = max(max_bitflips, bitflips);
		}

		prev_sector = err_sector;
	} while (!ECC_LAST_ERR(err_cor_info));

	/*
	 * Once handle all ecc errors, controller will trigger a
	 * ECC_TRANSACTION_DONE interrupt, so here just wait for
	 * a while for this interrupt
	 */
	while (!(read_interrupt_status(denali) & INTR__ECC_TRANSACTION_DONE))
		cpu_relax();
	clear_interrupts(denali);
	denali_set_intr_modes(denali, true);

	return max_bitflips;
}

/* programs the controller to either enable/disable DMA transfers */
static void denali_enable_dma(struct denali_nand_info *denali, bool en)
{
	iowrite32(en ? DMA_ENABLE__FLAG : 0, denali->flash_reg + DMA_ENABLE);
	ioread32(denali->flash_reg + DMA_ENABLE);
}

static void denali_setup_dma64(struct denali_nand_info *denali, int op)
{
	uint32_t mode;
	const int page_count = 1;
	uint64_t addr = denali->buf.dma_buf;

	mode = MODE_10 | BANK(denali->flash_bank) | denali->page;

	/* DMA is a three step process */

	/*
	 * 1. setup transfer type, interrupt when complete,
	 *    burst len = 64 bytes, the number of pages
	 */
	index_addr(denali, mode, 0x01002000 | (64 << 16) | op | page_count);

	/* 2. set memory low address */
	index_addr(denali, mode, addr);

	/* 3. set memory high address */
	index_addr(denali, mode, addr >> 32);
}

static void denali_setup_dma32(struct denali_nand_info *denali, int op)
{
	uint32_t mode;
	const int page_count = 1;
	uint32_t addr = denali->buf.dma_buf;

	mode = MODE_10 | BANK(denali->flash_bank);

	/* DMA is a four step process */

	/* 1. setup transfer type and # of pages */
	index_addr(denali, mode | denali->page, 0x2000 | op | page_count);

	/* 2. set memory high address bits 23:8 */
	index_addr(denali, mode | ((addr >> 16) << 8), 0x2200);

	/* 3. set memory low address bits 23:8 */
	index_addr(denali, mode | ((addr & 0xffff) << 8), 0x2300);

	/* 4. interrupt when complete, burst len = 64 bytes */
	index_addr(denali, mode | 0x14000, 0x2400);
}

static void denali_setup_dma(struct denali_nand_info *denali, int op)
{
	if (denali->caps & DENALI_CAP_DMA_64BIT)
		denali_setup_dma64(denali, op);
	else
		denali_setup_dma32(denali, op);
}

/*
 * writes a page. user specifies type, and this function handles the
 * configuration details.
 */
static int write_page(struct mtd_info *mtd, struct nand_chip *chip,
			const uint8_t *buf, bool raw_xfer)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	dma_addr_t addr = denali->buf.dma_buf;
	size_t size = mtd->writesize + mtd->oobsize;
	uint32_t irq_status;
	uint32_t irq_mask = INTR__DMA_CMD_COMP | INTR__PROGRAM_FAIL;

	/*
	 * if it is a raw xfer, we want to disable ecc and send the spare area.
	 * !raw_xfer - enable ecc
	 * raw_xfer - transfer spare
	 */
	setup_ecc_for_xfer(denali, !raw_xfer, raw_xfer);

	/* copy buffer into DMA buffer */
	memcpy(denali->buf.buf, buf, mtd->writesize);

	if (raw_xfer) {
		/* transfer the data to the spare area */
		memcpy(denali->buf.buf + mtd->writesize,
			chip->oob_poi,
			mtd->oobsize);
	}

	dma_sync_single_for_device(denali->dev, addr, size, DMA_TO_DEVICE);

	clear_interrupts(denali);
	denali_enable_dma(denali, true);

	denali_setup_dma(denali, DENALI_WRITE);

	/* wait for operation to complete */
	irq_status = wait_for_irq(denali, irq_mask);

	if (irq_status == 0) {
		dev_err(denali->dev, "timeout on write_page (type = %d)\n",
			raw_xfer);
		denali->status = NAND_STATUS_FAIL;
	}

	denali_enable_dma(denali, false);
	dma_sync_single_for_cpu(denali->dev, addr, size, DMA_TO_DEVICE);

	return 0;
}

/* NAND core entry points */

/*
 * this is the callback that the NAND core calls to write a page. Since
 * writing a page with ECC or without is similar, all the work is done
 * by write_page above.
 */
static int denali_write_page(struct mtd_info *mtd, struct nand_chip *chip,
				const uint8_t *buf, int oob_required, int page)
{
	/*
	 * for regular page writes, we let HW handle all the ECC
	 * data written to the device.
	 */
	return write_page(mtd, chip, buf, false);
}

/*
 * This is the callback that the NAND core calls to write a page without ECC.
 * raw access is similar to ECC page writes, so all the work is done in the
 * write_page() function above.
 */
static int denali_write_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
				 const uint8_t *buf, int oob_required,
				 int page)
{
	/*
	 * for raw page writes, we want to disable ECC and simply write
	 * whatever data is in the buffer.
	 */
	return write_page(mtd, chip, buf, true);
}

static int denali_write_oob(struct mtd_info *mtd, struct nand_chip *chip,
			    int page)
{
	return write_oob_data(mtd, chip->oob_poi, page);
}

static int denali_read_oob(struct mtd_info *mtd, struct nand_chip *chip,
			   int page)
{
	read_oob_data(mtd, chip->oob_poi, page);

	return 0;
}

static int denali_read_page(struct mtd_info *mtd, struct nand_chip *chip,
			    uint8_t *buf, int oob_required, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	dma_addr_t addr = denali->buf.dma_buf;
	size_t size = mtd->writesize + mtd->oobsize;
	uint32_t irq_status;
	uint32_t irq_mask = denali->caps & DENALI_CAP_HW_ECC_FIXUP ?
				INTR__DMA_CMD_COMP | INTR__ECC_UNCOR_ERR :
				INTR__ECC_TRANSACTION_DONE | INTR__ECC_ERR;
	unsigned long uncor_ecc_flags = 0;
	int stat = 0;

	if (page != denali->page) {
		dev_err(denali->dev,
			"IN %s: page %d is not equal to denali->page %d",
			__func__, page, denali->page);
		BUG();
	}

	setup_ecc_for_xfer(denali, true, false);

	denali_enable_dma(denali, true);
	dma_sync_single_for_device(denali->dev, addr, size, DMA_FROM_DEVICE);

	clear_interrupts(denali);
	denali_setup_dma(denali, DENALI_READ);

	/* wait for operation to complete */
	irq_status = wait_for_irq(denali, irq_mask);

	dma_sync_single_for_cpu(denali->dev, addr, size, DMA_FROM_DEVICE);

	memcpy(buf, denali->buf.buf, mtd->writesize);

	if (denali->caps & DENALI_CAP_HW_ECC_FIXUP)
		stat = denali_hw_ecc_fixup(mtd, denali, &uncor_ecc_flags);
	else if (irq_status & INTR__ECC_ERR)
		stat = denali_sw_ecc_fixup(mtd, denali, &uncor_ecc_flags, buf);
	denali_enable_dma(denali, false);

	if (stat < 0)
		return stat;

	if (uncor_ecc_flags) {
		read_oob_data(mtd, chip->oob_poi, denali->page);

		stat = denali_check_erased_page(mtd, chip, buf,
						uncor_ecc_flags, stat);
	}

	return stat;
}

static int denali_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf, int oob_required, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	dma_addr_t addr = denali->buf.dma_buf;
	size_t size = mtd->writesize + mtd->oobsize;
	uint32_t irq_mask = INTR__DMA_CMD_COMP;

	if (page != denali->page) {
		dev_err(denali->dev,
			"IN %s: page %d is not equal to denali->page %d",
			__func__, page, denali->page);
		BUG();
	}

	setup_ecc_for_xfer(denali, false, true);
	denali_enable_dma(denali, true);

	dma_sync_single_for_device(denali->dev, addr, size, DMA_FROM_DEVICE);

	clear_interrupts(denali);
	denali_setup_dma(denali, DENALI_READ);

	/* wait for operation to complete */
	wait_for_irq(denali, irq_mask);

	dma_sync_single_for_cpu(denali->dev, addr, size, DMA_FROM_DEVICE);

	denali_enable_dma(denali, false);

	memcpy(buf, denali->buf.buf, mtd->writesize);
	memcpy(chip->oob_poi, denali->buf.buf + mtd->writesize, mtd->oobsize);

	return 0;
}

static uint8_t denali_read_byte(struct mtd_info *mtd)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint8_t result = 0xff;

	if (denali->buf.head < denali->buf.tail)
		result = denali->buf.buf[denali->buf.head++];

	return result;
}

static void denali_select_chip(struct mtd_info *mtd, int chip)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	spin_lock_irq(&denali->irq_lock);
	denali->flash_bank = chip;
	spin_unlock_irq(&denali->irq_lock);
}

static int denali_waitfunc(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	int status = denali->status;

	denali->status = 0;

	return status;
}

static int denali_erase(struct mtd_info *mtd, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	uint32_t cmd, irq_status;

	clear_interrupts(denali);

	/* setup page read request for access type */
	cmd = MODE_10 | BANK(denali->flash_bank) | page;
	index_addr(denali, cmd, 0x1);

	/* wait for erase to complete or failure to occur */
	irq_status = wait_for_irq(denali, INTR__ERASE_COMP | INTR__ERASE_FAIL);

	return irq_status & INTR__ERASE_FAIL ? NAND_STATUS_FAIL : PASS;
}

static void denali_cmdfunc(struct mtd_info *mtd, unsigned int cmd, int col,
			   int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t addr, id;
	int i;

	switch (cmd) {
	case NAND_CMD_PAGEPROG:
		break;
	case NAND_CMD_STATUS:
		read_status(denali);
		break;
	case NAND_CMD_READID:
	case NAND_CMD_PARAM:
		reset_buf(denali);
		/*
		 * sometimes ManufactureId read from register is not right
		 * e.g. some of Micron MT29F32G08QAA MLC NAND chips
		 * So here we send READID cmd to NAND insteand
		 */
		addr = MODE_11 | BANK(denali->flash_bank);
		index_addr(denali, addr | 0, 0x90);
		index_addr(denali, addr | 1, col);
		for (i = 0; i < 8; i++) {
			index_addr_read_data(denali, addr | 2, &id);
			write_byte_to_buf(denali, id);
		}
		break;
	case NAND_CMD_READ0:
	case NAND_CMD_SEQIN:
		denali->page = page;
		break;
	case NAND_CMD_RESET:
		reset_bank(denali);
		break;
	case NAND_CMD_READOOB:
		/* TODO: Read OOB data */
		break;
	default:
		pr_err(": unsupported command received 0x%x\n", cmd);
		break;
	}
}
/* end NAND core entry points */

/* Initialization code to bring the device up to a known good state */
static void denali_hw_init(struct denali_nand_info *denali)
{
	/*
	 * The REVISION register may not be reliable.  Platforms are allowed to
	 * override it.
	 */
	if (!denali->revision)
		denali->revision =
				swab16(ioread32(denali->flash_reg + REVISION));

	/*
	 * tell driver how many bit controller will skip before
	 * writing ECC code in OOB, this register may be already
	 * set by firmware. So we read this value out.
	 * if this value is 0, just let it be.
	 */
	denali->bbtskipbytes = ioread32(denali->flash_reg +
						SPARE_AREA_SKIP_BYTES);
	detect_max_banks(denali);
	denali_nand_reset(denali);
	iowrite32(0x0F, denali->flash_reg + RB_PIN_ENABLED);
	iowrite32(CHIP_EN_DONT_CARE__FLAG,
			denali->flash_reg + CHIP_ENABLE_DONT_CARE);

	iowrite32(0xffff, denali->flash_reg + SPARE_AREA_MARKER);

	/* Should set value for these registers when init */
	iowrite32(0, denali->flash_reg + TWO_ROW_ADDR_CYCLES);
	iowrite32(1, denali->flash_reg + ECC_ENABLE);
	denali_nand_timing_set(denali);
	denali_irq_init(denali);
}

/*
 * Althogh controller spec said SLC ECC is forceb to be 4bit,
 * but denali controller in MRST only support 15bit and 8bit ECC
 * correction
 */
#define ECC_8BITS	14
#define ECC_15BITS	26

static int denali_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = denali->bbtskipbytes;
	oobregion->length = chip->ecc.total;

	return 0;
}

static int denali_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = chip->ecc.total + denali->bbtskipbytes;
	oobregion->length = mtd->oobsize - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops denali_ooblayout_ops = {
	.ecc = denali_ooblayout_ecc,
	.free = denali_ooblayout_free,
};

static uint8_t bbt_pattern[] = {'B', 'b', 't', '0' };
static uint8_t mirror_pattern[] = {'1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs =	8,
	.len = 4,
	.veroffs = 12,
	.maxblocks = 4,
	.pattern = bbt_pattern,
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs =	8,
	.len = 4,
	.veroffs = 12,
	.maxblocks = 4,
	.pattern = mirror_pattern,
};

/* initialize driver data structures */
static void denali_drv_init(struct denali_nand_info *denali)
{
	/*
	 * the completion object will be used to notify
	 * the callee that the interrupt is done
	 */
	init_completion(&denali->complete);

	/*
	 * the spinlock will be used to synchronize the ISR with any
	 * element that might be access shared data (interrupt status)
	 */
	spin_lock_init(&denali->irq_lock);

	/* indicate that MTD has not selected a valid bank yet */
	denali->flash_bank = CHIP_SELECT_INVALID;

	/* initialize our irq_status variable to indicate no interrupts */
	denali->irq_status = 0;
}

static int denali_multidev_fixup(struct denali_nand_info *denali)
{
	struct nand_chip *chip = &denali->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);

	/*
	 * Support for multi device:
	 * When the IP configuration is x16 capable and two x8 chips are
	 * connected in parallel, DEVICES_CONNECTED should be set to 2.
	 * In this case, the core framework knows nothing about this fact,
	 * so we should tell it the _logical_ pagesize and anything necessary.
	 */
	denali->devnum = ioread32(denali->flash_reg + DEVICES_CONNECTED);

	/*
	 * On some SoCs, DEVICES_CONNECTED is not auto-detected.
	 * For those, DEVICES_CONNECTED is left to 0.  Set 1 if it is the case.
	 */
	if (denali->devnum == 0) {
		denali->devnum = 1;
		iowrite32(1, denali->flash_reg + DEVICES_CONNECTED);
	}

	if (denali->devnum == 1)
		return 0;

	if (denali->devnum != 2) {
		dev_err(denali->dev, "unsupported number of devices %d\n",
			denali->devnum);
		return -EINVAL;
	}

	/* 2 chips in parallel */
	mtd->size <<= 1;
	mtd->erasesize <<= 1;
	mtd->writesize <<= 1;
	mtd->oobsize <<= 1;
	chip->chipsize <<= 1;
	chip->page_shift += 1;
	chip->phys_erase_shift += 1;
	chip->bbt_erase_shift += 1;
	chip->chip_shift += 1;
	chip->pagemask <<= 1;
	chip->ecc.size <<= 1;
	chip->ecc.bytes <<= 1;
	chip->ecc.strength <<= 1;
	denali->bbtskipbytes <<= 1;

	return 0;
}

int denali_init(struct denali_nand_info *denali)
{
	struct nand_chip *chip = &denali->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	if (denali->platform == INTEL_CE4100) {
		/*
		 * Due to a silicon limitation, we can only support
		 * ONFI timing mode 1 and below.
		 */
		if (onfi_timing_mode < -1 || onfi_timing_mode > 1) {
			pr_err("Intel CE4100 only supports ONFI timing mode 1 or below\n");
			return -EINVAL;
		}
	}

	/* allocate a temporary buffer for nand_scan_ident() */
	denali->buf.buf = devm_kzalloc(denali->dev, PAGE_SIZE,
					GFP_DMA | GFP_KERNEL);
	if (!denali->buf.buf)
		return -ENOMEM;

	mtd->dev.parent = denali->dev;
	denali_hw_init(denali);
	denali_drv_init(denali);

	/* Request IRQ after all the hardware initialization is finished */
	ret = devm_request_irq(denali->dev, denali->irq, denali_isr,
			       IRQF_SHARED, DENALI_NAND_NAME, denali);
	if (ret) {
		dev_err(denali->dev, "Unable to request IRQ\n");
		return ret;
	}

	/* now that our ISR is registered, we can enable interrupts */
	denali_set_intr_modes(denali, true);
	nand_set_flash_node(chip, denali->dev->of_node);
	/* Fallback to the default name if DT did not give "label" property */
	if (!mtd->name)
		mtd->name = "denali-nand";

	/* register the driver with the NAND core subsystem */
	chip->select_chip = denali_select_chip;
	chip->cmdfunc = denali_cmdfunc;
	chip->read_byte = denali_read_byte;
	chip->waitfunc = denali_waitfunc;
	chip->onfi_set_features = nand_onfi_get_set_features_notsupp;
	chip->onfi_get_features = nand_onfi_get_set_features_notsupp;

	/*
	 * scan for NAND devices attached to the controller
	 * this is the first stage in a two step process to register
	 * with the nand subsystem
	 */
	ret = nand_scan_ident(mtd, denali->max_banks, NULL);
	if (ret)
		goto failed_req_irq;

	/* allocate the right size buffer now */
	devm_kfree(denali->dev, denali->buf.buf);
	denali->buf.buf = devm_kzalloc(denali->dev,
			     mtd->writesize + mtd->oobsize,
			     GFP_KERNEL);
	if (!denali->buf.buf) {
		ret = -ENOMEM;
		goto failed_req_irq;
	}

	ret = dma_set_mask(denali->dev,
			   DMA_BIT_MASK(denali->caps & DENALI_CAP_DMA_64BIT ?
					64 : 32));
	if (ret) {
		dev_err(denali->dev, "No usable DMA configuration\n");
		goto failed_req_irq;
	}

	denali->buf.dma_buf = dma_map_single(denali->dev, denali->buf.buf,
			     mtd->writesize + mtd->oobsize,
			     DMA_BIDIRECTIONAL);
	if (dma_mapping_error(denali->dev, denali->buf.dma_buf)) {
		dev_err(denali->dev, "Failed to map DMA buffer\n");
		ret = -EIO;
		goto failed_req_irq;
	}

	/*
	 * second stage of the NAND scan
	 * this stage requires information regarding ECC and
	 * bad block management.
	 */

	/* Bad block management */
	chip->bbt_td = &bbt_main_descr;
	chip->bbt_md = &bbt_mirror_descr;

	/* skip the scan for now until we have OOB read and write support */
	chip->bbt_options |= NAND_BBT_USE_FLASH;
	chip->options |= NAND_SKIP_BBTSCAN;
	chip->ecc.mode = NAND_ECC_HW_SYNDROME;

	/* no subpage writes on denali */
	chip->options |= NAND_NO_SUBPAGE_WRITE;

	/*
	 * Denali Controller only support 15bit and 8bit ECC in MRST,
	 * so just let controller do 15bit ECC for MLC and 8bit ECC for
	 * SLC if possible.
	 * */
	if (!nand_is_slc(chip) &&
			(mtd->oobsize > (denali->bbtskipbytes +
			ECC_15BITS * (mtd->writesize /
			ECC_SECTOR_SIZE)))) {
		/* if MLC OOB size is large enough, use 15bit ECC*/
		chip->ecc.strength = 15;
		chip->ecc.bytes = ECC_15BITS;
		iowrite32(15, denali->flash_reg + ECC_CORRECTION);
	} else if (mtd->oobsize < (denali->bbtskipbytes +
			ECC_8BITS * (mtd->writesize /
			ECC_SECTOR_SIZE))) {
		pr_err("Your NAND chip OOB is not large enough to contain 8bit ECC correction codes");
		goto failed_req_irq;
	} else {
		chip->ecc.strength = 8;
		chip->ecc.bytes = ECC_8BITS;
		iowrite32(8, denali->flash_reg + ECC_CORRECTION);
	}

	mtd_set_ooblayout(mtd, &denali_ooblayout_ops);

	/* override the default read operations */
	chip->ecc.size = ECC_SECTOR_SIZE;
	chip->ecc.read_page = denali_read_page;
	chip->ecc.read_page_raw = denali_read_page_raw;
	chip->ecc.write_page = denali_write_page;
	chip->ecc.write_page_raw = denali_write_page_raw;
	chip->ecc.read_oob = denali_read_oob;
	chip->ecc.write_oob = denali_write_oob;
	chip->erase = denali_erase;

	ret = denali_multidev_fixup(denali);
	if (ret)
		goto failed_req_irq;

	ret = nand_scan_tail(mtd);
	if (ret)
		goto failed_req_irq;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(denali->dev, "Failed to register MTD: %d\n", ret);
		goto failed_req_irq;
	}
	return 0;

failed_req_irq:
	denali_irq_cleanup(denali->irq, denali);

	return ret;
}
EXPORT_SYMBOL(denali_init);

/* driver exit point */
void denali_remove(struct denali_nand_info *denali)
{
	struct mtd_info *mtd = nand_to_mtd(&denali->nand);
	/*
	 * Pre-compute DMA buffer size to avoid any problems in case
	 * nand_release() ever changes in a way that mtd->writesize and
	 * mtd->oobsize are not reliable after this call.
	 */
	int bufsize = mtd->writesize + mtd->oobsize;

	nand_release(mtd);
	denali_irq_cleanup(denali->irq, denali);
	dma_unmap_single(denali->dev, denali->buf.dma_buf, bufsize,
			 DMA_BIDIRECTIONAL);
}
EXPORT_SYMBOL(denali_remove);
