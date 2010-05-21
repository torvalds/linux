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
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/mtd/mtd.h>
#include <linux/module.h>

#include "denali.h"

MODULE_LICENSE("GPL");

/* We define a module parameter that allows the user to override 
 * the hardware and decide what timing mode should be used.
 */
#define NAND_DEFAULT_TIMINGS	-1

static int onfi_timing_mode = NAND_DEFAULT_TIMINGS;
module_param(onfi_timing_mode, int, S_IRUGO);
MODULE_PARM_DESC(onfi_timing_mode, "Overrides default ONFI setting. -1 indicates"
					" use default timings");

#define DENALI_NAND_NAME    "denali-nand"

/* We define a macro here that combines all interrupts this driver uses into
 * a single constant value, for convenience. */
#define DENALI_IRQ_ALL	(INTR_STATUS0__DMA_CMD_COMP | \
			INTR_STATUS0__ECC_TRANSACTION_DONE | \
			INTR_STATUS0__ECC_ERR | \
			INTR_STATUS0__PROGRAM_FAIL | \
			INTR_STATUS0__LOAD_COMP | \
			INTR_STATUS0__PROGRAM_COMP | \
			INTR_STATUS0__TIME_OUT | \
			INTR_STATUS0__ERASE_FAIL | \
			INTR_STATUS0__RST_COMP | \
			INTR_STATUS0__ERASE_COMP)

/* indicates whether or not the internal value for the flash bank is 
   valid or not */
#define CHIP_SELECT_INVALID 	-1

#define SUPPORT_8BITECC		1

/* This macro divides two integers and rounds fractional values up 
 * to the nearest integer value. */
#define CEIL_DIV(X, Y) (((X)%(Y)) ? ((X)/(Y)+1) : ((X)/(Y)))

/* this macro allows us to convert from an MTD structure to our own
 * device context (denali) structure.
 */
#define mtd_to_denali(m) container_of(m, struct denali_nand_info, mtd)

/* These constants are defined by the driver to enable common driver
   configuration options. */
#define SPARE_ACCESS		0x41
#define MAIN_ACCESS		0x42
#define MAIN_SPARE_ACCESS	0x43

#define DENALI_READ	0
#define DENALI_WRITE	0x100

/* types of device accesses. We can issue commands and get status */
#define COMMAND_CYCLE	0
#define ADDR_CYCLE	1
#define STATUS_CYCLE	2

/* this is a helper macro that allows us to 
 * format the bank into the proper bits for the controller */
#define BANK(x) ((x) << 24)

/* List of platforms this NAND controller has be integrated into */
static const struct pci_device_id denali_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x0701), INTEL_CE4100 },
	{ PCI_VDEVICE(INTEL, 0x0809), INTEL_MRST },
	{ /* end: all zeroes */ }
};


/* these are static lookup tables that give us easy access to 
   registers in the NAND controller.  
 */
static const uint32_t intr_status_addresses[4] = {INTR_STATUS0, 
						  INTR_STATUS1, 
					     	  INTR_STATUS2, 
						  INTR_STATUS3};

static const uint32_t device_reset_banks[4] = {DEVICE_RESET__BANK0,
                                               DEVICE_RESET__BANK1,
                                               DEVICE_RESET__BANK2,
                                               DEVICE_RESET__BANK3};

static const uint32_t operation_timeout[4] = {INTR_STATUS0__TIME_OUT,
                        		      INTR_STATUS1__TIME_OUT,
		                              INTR_STATUS2__TIME_OUT,
		                              INTR_STATUS3__TIME_OUT};

static const uint32_t reset_complete[4] = {INTR_STATUS0__RST_COMP,
                		           INTR_STATUS1__RST_COMP,
		                           INTR_STATUS2__RST_COMP,
		                           INTR_STATUS3__RST_COMP};

/* specifies the debug level of the driver */
static int nand_debug_level = 0;

/* forward declarations */
static void clear_interrupts(struct denali_nand_info *denali);
static uint32_t wait_for_irq(struct denali_nand_info *denali, uint32_t irq_mask);
static void denali_irq_enable(struct denali_nand_info *denali, uint32_t int_mask);
static uint32_t read_interrupt_status(struct denali_nand_info *denali);

#define DEBUG_DENALI 0

/* This is a wrapper for writing to the denali registers.
 * this allows us to create debug information so we can
 * observe how the driver is programming the device. 
 * it uses standard linux convention for (val, addr) */
static void denali_write32(uint32_t value, void *addr)
{
	iowrite32(value, addr);	

#if DEBUG_DENALI
	printk(KERN_ERR "wrote: 0x%x -> 0x%x\n", value, (uint32_t)((uint32_t)addr & 0x1fff));
#endif
} 

/* Certain operations for the denali NAND controller use an indexed mode to read/write 
   data. The operation is performed by writing the address value of the command to 
   the device memory followed by the data. This function abstracts this common 
   operation. 
*/
static void index_addr(struct denali_nand_info *denali, uint32_t address, uint32_t data)
{
	denali_write32(address, denali->flash_mem);
	denali_write32(data, denali->flash_mem + 0x10);
}

/* Perform an indexed read of the device */
static void index_addr_read_data(struct denali_nand_info *denali,
				 uint32_t address, uint32_t *pdata)
{
	denali_write32(address, denali->flash_mem);
	*pdata = ioread32(denali->flash_mem + 0x10);
}

/* We need to buffer some data for some of the NAND core routines. 
 * The operations manage buffering that data. */
static void reset_buf(struct denali_nand_info *denali)
{
	denali->buf.head = denali->buf.tail = 0;
}

static void write_byte_to_buf(struct denali_nand_info *denali, uint8_t byte)
{
	BUG_ON(denali->buf.tail >= sizeof(denali->buf.buf));
	denali->buf.buf[denali->buf.tail++] = byte;
}

/* reads the status of the device */
static void read_status(struct denali_nand_info *denali)
{
	uint32_t cmd = 0x0;

	/* initialize the data buffer to store status */
	reset_buf(denali);

	/* initiate a device status read */
	cmd = MODE_11 | BANK(denali->flash_bank); 
	index_addr(denali, cmd | COMMAND_CYCLE, 0x70);
	denali_write32(cmd | STATUS_CYCLE, denali->flash_mem);

	/* update buffer with status value */
	write_byte_to_buf(denali, ioread32(denali->flash_mem + 0x10));

#if DEBUG_DENALI
	printk("device reporting status value of 0x%2x\n", denali->buf.buf[0]);
#endif
}

/* resets a specific device connected to the core */
static void reset_bank(struct denali_nand_info *denali)
{
	uint32_t irq_status = 0;
	uint32_t irq_mask = reset_complete[denali->flash_bank] | 
			    operation_timeout[denali->flash_bank];
	int bank = 0;

	clear_interrupts(denali);

	bank = device_reset_banks[denali->flash_bank];
	denali_write32(bank, denali->flash_reg + DEVICE_RESET);

	irq_status = wait_for_irq(denali, irq_mask);
	
	if (irq_status & operation_timeout[denali->flash_bank])
	{
		printk(KERN_ERR "reset bank failed.\n");
	}
}

/* Reset the flash controller */
static uint16_t NAND_Flash_Reset(struct denali_nand_info *denali)
{
	uint32_t i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	for (i = 0 ; i < LLD_MAX_FLASH_BANKS; i++)
		denali_write32(reset_complete[i] | operation_timeout[i],
		denali->flash_reg + intr_status_addresses[i]);

	for (i = 0 ; i < LLD_MAX_FLASH_BANKS; i++) {
		denali_write32(device_reset_banks[i], denali->flash_reg + DEVICE_RESET);
		while (!(ioread32(denali->flash_reg + intr_status_addresses[i]) &
			(reset_complete[i] | operation_timeout[i])))
			;
		if (ioread32(denali->flash_reg + intr_status_addresses[i]) &
			operation_timeout[i])
			nand_dbg_print(NAND_DBG_WARN,
			"NAND Reset operation timed out on bank %d\n", i);
	}

	for (i = 0; i < LLD_MAX_FLASH_BANKS; i++)
		denali_write32(reset_complete[i] | operation_timeout[i],
			denali->flash_reg + intr_status_addresses[i]);

	return PASS;
}

/* this routine calculates the ONFI timing values for a given mode and programs
 * the clocking register accordingly. The mode is determined by the get_onfi_nand_para
   routine.
 */
static void NAND_ONFi_Timing_Mode(struct denali_nand_info *denali, uint16_t mode)
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

	uint16_t TclsRising = 1;
	uint16_t data_invalid_rhoh, data_invalid_rloh, data_invalid;
	uint16_t dv_window = 0;
	uint16_t en_lo, en_hi;
	uint16_t acc_clks;
	uint16_t addr_2_data, re_2_we, re_2_re, we_2_re, cs_cnt;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

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

		data_invalid =
		    data_invalid_rhoh <
		    data_invalid_rloh ? data_invalid_rhoh : data_invalid_rloh;

		dv_window = data_invalid - Trea[mode];

		if (dv_window < 8)
			en_lo++;
	}

	acc_clks = CEIL_DIV(Trea[mode], CLK_X);

	while (((acc_clks * CLK_X) - Trea[mode]) < 3)
		acc_clks++;

	if ((data_invalid - acc_clks * CLK_X) < 2)
		nand_dbg_print(NAND_DBG_WARN, "%s, Line %d: Warning!\n",
			__FILE__, __LINE__);

	addr_2_data = CEIL_DIV(Tadl[mode], CLK_X);
	re_2_we = CEIL_DIV(Trhw[mode], CLK_X);
	re_2_re = CEIL_DIV(Trhz[mode], CLK_X);
	we_2_re = CEIL_DIV(Twhr[mode], CLK_X);
	cs_cnt = CEIL_DIV((Tcs[mode] - Trp[mode]), CLK_X);
	if (!TclsRising)
		cs_cnt = CEIL_DIV(Tcs[mode], CLK_X);
	if (cs_cnt == 0)
		cs_cnt = 1;

	if (Tcea[mode]) {
		while (((cs_cnt * CLK_X) + Trea[mode]) < Tcea[mode])
			cs_cnt++;
	}

#if MODE5_WORKAROUND
	if (mode == 5)
		acc_clks = 5;
#endif

	/* Sighting 3462430: Temporary hack for MT29F128G08CJABAWP:B */
	if ((ioread32(denali->flash_reg + MANUFACTURER_ID) == 0) &&
		(ioread32(denali->flash_reg + DEVICE_ID) == 0x88))
		acc_clks = 6;

	denali_write32(acc_clks, denali->flash_reg + ACC_CLKS);
	denali_write32(re_2_we, denali->flash_reg + RE_2_WE);
	denali_write32(re_2_re, denali->flash_reg + RE_2_RE);
	denali_write32(we_2_re, denali->flash_reg + WE_2_RE);
	denali_write32(addr_2_data, denali->flash_reg + ADDR_2_DATA);
	denali_write32(en_lo, denali->flash_reg + RDWR_EN_LO_CNT);
	denali_write32(en_hi, denali->flash_reg + RDWR_EN_HI_CNT);
	denali_write32(cs_cnt, denali->flash_reg + CS_SETUP_CNT);
}

/* configures the initial ECC settings for the controller */
static void set_ecc_config(struct denali_nand_info *denali)
{
#if SUPPORT_8BITECC
	if ((ioread32(denali->flash_reg + DEVICE_MAIN_AREA_SIZE) < 4096) ||
		(ioread32(denali->flash_reg + DEVICE_SPARE_AREA_SIZE) <= 128))
		denali_write32(8, denali->flash_reg + ECC_CORRECTION);
#endif

	if ((ioread32(denali->flash_reg + ECC_CORRECTION) & ECC_CORRECTION__VALUE)
		== 1) {
		denali->dev_info.wECCBytesPerSector = 4;
		denali->dev_info.wECCBytesPerSector *= denali->dev_info.wDevicesConnected;
		denali->dev_info.wNumPageSpareFlag =
			denali->dev_info.wPageSpareSize -
			denali->dev_info.wPageDataSize /
			(ECC_SECTOR_SIZE * denali->dev_info.wDevicesConnected) *
			denali->dev_info.wECCBytesPerSector
			- denali->dev_info.wSpareSkipBytes;
	} else {
		denali->dev_info.wECCBytesPerSector =
			(ioread32(denali->flash_reg + ECC_CORRECTION) &
			ECC_CORRECTION__VALUE) * 13 / 8;
		if ((denali->dev_info.wECCBytesPerSector) % 2 == 0)
			denali->dev_info.wECCBytesPerSector += 2;
		else
			denali->dev_info.wECCBytesPerSector += 1;

		denali->dev_info.wECCBytesPerSector *= denali->dev_info.wDevicesConnected;
		denali->dev_info.wNumPageSpareFlag = denali->dev_info.wPageSpareSize -
			denali->dev_info.wPageDataSize /
			(ECC_SECTOR_SIZE * denali->dev_info.wDevicesConnected) *
			denali->dev_info.wECCBytesPerSector
			- denali->dev_info.wSpareSkipBytes;
	}
}

/* queries the NAND device to see what ONFI modes it supports. */
static uint16_t get_onfi_nand_para(struct denali_nand_info *denali)
{
	int i;
	uint16_t blks_lun_l, blks_lun_h, n_of_luns;
	uint32_t blockperlun, id;

	denali_write32(DEVICE_RESET__BANK0, denali->flash_reg + DEVICE_RESET);

	while (!((ioread32(denali->flash_reg + INTR_STATUS0) &
		INTR_STATUS0__RST_COMP) |
		(ioread32(denali->flash_reg + INTR_STATUS0) &
		INTR_STATUS0__TIME_OUT)))
		;

	if (ioread32(denali->flash_reg + INTR_STATUS0) & INTR_STATUS0__RST_COMP) {
		denali_write32(DEVICE_RESET__BANK1, denali->flash_reg + DEVICE_RESET);
		while (!((ioread32(denali->flash_reg + INTR_STATUS1) &
			INTR_STATUS1__RST_COMP) |
			(ioread32(denali->flash_reg + INTR_STATUS1) &
			INTR_STATUS1__TIME_OUT)))
			;

		if (ioread32(denali->flash_reg + INTR_STATUS1) &
			INTR_STATUS1__RST_COMP) {
			denali_write32(DEVICE_RESET__BANK2,
				denali->flash_reg + DEVICE_RESET);
			while (!((ioread32(denali->flash_reg + INTR_STATUS2) &
				INTR_STATUS2__RST_COMP) |
				(ioread32(denali->flash_reg + INTR_STATUS2) &
				INTR_STATUS2__TIME_OUT)))
				;

			if (ioread32(denali->flash_reg + INTR_STATUS2) &
				INTR_STATUS2__RST_COMP) {
				denali_write32(DEVICE_RESET__BANK3,
					denali->flash_reg + DEVICE_RESET);
				while (!((ioread32(denali->flash_reg + INTR_STATUS3) &
					INTR_STATUS3__RST_COMP) |
					(ioread32(denali->flash_reg + INTR_STATUS3) &
					INTR_STATUS3__TIME_OUT)))
					;
			} else {
				printk(KERN_ERR "Getting a time out for bank 2!\n");
			}
		} else {
			printk(KERN_ERR "Getting a time out for bank 1!\n");
		}
	}

	denali_write32(INTR_STATUS0__TIME_OUT, denali->flash_reg + INTR_STATUS0);
	denali_write32(INTR_STATUS1__TIME_OUT, denali->flash_reg + INTR_STATUS1);
	denali_write32(INTR_STATUS2__TIME_OUT, denali->flash_reg + INTR_STATUS2);
	denali_write32(INTR_STATUS3__TIME_OUT, denali->flash_reg + INTR_STATUS3);

	denali->dev_info.wONFIDevFeatures =
		ioread32(denali->flash_reg + ONFI_DEVICE_FEATURES);
	denali->dev_info.wONFIOptCommands =
		ioread32(denali->flash_reg + ONFI_OPTIONAL_COMMANDS);
	denali->dev_info.wONFITimingMode =
		ioread32(denali->flash_reg + ONFI_TIMING_MODE);
	denali->dev_info.wONFIPgmCacheTimingMode =
		ioread32(denali->flash_reg + ONFI_PGM_CACHE_TIMING_MODE);

	n_of_luns = ioread32(denali->flash_reg + ONFI_DEVICE_NO_OF_LUNS) &
		ONFI_DEVICE_NO_OF_LUNS__NO_OF_LUNS;
	blks_lun_l = ioread32(denali->flash_reg + ONFI_DEVICE_NO_OF_BLOCKS_PER_LUN_L);
	blks_lun_h = ioread32(denali->flash_reg + ONFI_DEVICE_NO_OF_BLOCKS_PER_LUN_U);

	blockperlun = (blks_lun_h << 16) | blks_lun_l;

	denali->dev_info.wTotalBlocks = n_of_luns * blockperlun;

	if (!(ioread32(denali->flash_reg + ONFI_TIMING_MODE) &
		ONFI_TIMING_MODE__VALUE))
		return FAIL;

	for (i = 5; i > 0; i--) {
		if (ioread32(denali->flash_reg + ONFI_TIMING_MODE) & (0x01 << i))
			break;
	}

	NAND_ONFi_Timing_Mode(denali, i);

	index_addr(denali, MODE_11 | 0, 0x90);
	index_addr(denali, MODE_11 | 1, 0);

	for (i = 0; i < 3; i++)
		index_addr_read_data(denali, MODE_11 | 2, &id);

	nand_dbg_print(NAND_DBG_DEBUG, "3rd ID: 0x%x\n", id);

	denali->dev_info.MLCDevice = id & 0x0C;

	/* By now, all the ONFI devices we know support the page cache */
	/* rw feature. So here we enable the pipeline_rw_ahead feature */
	/* iowrite32(1, denali->flash_reg + CACHE_WRITE_ENABLE); */
	/* iowrite32(1, denali->flash_reg + CACHE_READ_ENABLE);  */

	return PASS;
}

static void get_samsung_nand_para(struct denali_nand_info *denali)
{
	uint8_t no_of_planes;
	uint32_t blk_size;
	uint64_t plane_size, capacity;
	uint32_t id_bytes[5];
	int i;

	index_addr(denali, (uint32_t)(MODE_11 | 0), 0x90);
	index_addr(denali, (uint32_t)(MODE_11 | 1), 0);
	for (i = 0; i < 5; i++)
		index_addr_read_data(denali, (uint32_t)(MODE_11 | 2), &id_bytes[i]);

	nand_dbg_print(NAND_DBG_DEBUG,
		"ID bytes: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
		id_bytes[0], id_bytes[1], id_bytes[2],
		id_bytes[3], id_bytes[4]);

	if ((id_bytes[1] & 0xff) == 0xd3) { /* Samsung K9WAG08U1A */
		/* Set timing register values according to datasheet */
		denali_write32(5, denali->flash_reg + ACC_CLKS);
		denali_write32(20, denali->flash_reg + RE_2_WE);
		denali_write32(12, denali->flash_reg + WE_2_RE);
		denali_write32(14, denali->flash_reg + ADDR_2_DATA);
		denali_write32(3, denali->flash_reg + RDWR_EN_LO_CNT);
		denali_write32(2, denali->flash_reg + RDWR_EN_HI_CNT);
		denali_write32(2, denali->flash_reg + CS_SETUP_CNT);
	}

	no_of_planes = 1 << ((id_bytes[4] & 0x0c) >> 2);
	plane_size  = (uint64_t)64 << ((id_bytes[4] & 0x70) >> 4);
	blk_size = 64 << ((ioread32(denali->flash_reg + DEVICE_PARAM_1) & 0x30) >> 4);
	capacity = (uint64_t)128 * plane_size * no_of_planes;

	do_div(capacity, blk_size);
	denali->dev_info.wTotalBlocks = capacity;
}

static void get_toshiba_nand_para(struct denali_nand_info *denali)
{
	void __iomem *scratch_reg;
	uint32_t tmp;

	/* Workaround to fix a controller bug which reports a wrong */
	/* spare area size for some kind of Toshiba NAND device */
	if ((ioread32(denali->flash_reg + DEVICE_MAIN_AREA_SIZE) == 4096) &&
		(ioread32(denali->flash_reg + DEVICE_SPARE_AREA_SIZE) == 64)) {
		denali_write32(216, denali->flash_reg + DEVICE_SPARE_AREA_SIZE);
		tmp = ioread32(denali->flash_reg + DEVICES_CONNECTED) *
			ioread32(denali->flash_reg + DEVICE_SPARE_AREA_SIZE);
		denali_write32(tmp, denali->flash_reg + LOGICAL_PAGE_SPARE_SIZE);
#if SUPPORT_15BITECC
		denali_write32(15, denali->flash_reg + ECC_CORRECTION);
#elif SUPPORT_8BITECC
		denali_write32(8, denali->flash_reg + ECC_CORRECTION);
#endif
	}

	/* As Toshiba NAND can not provide it's block number, */
	/* so here we need user to provide the correct block */
	/* number in a scratch register before the Linux NAND */
	/* driver is loaded. If no valid value found in the scratch */
	/* register, then we use default block number value */
	scratch_reg = ioremap_nocache(SCRATCH_REG_ADDR, SCRATCH_REG_SIZE);
	if (!scratch_reg) {
		printk(KERN_ERR "Spectra: ioremap failed in %s, Line %d",
			__FILE__, __LINE__);
		denali->dev_info.wTotalBlocks = GLOB_HWCTL_DEFAULT_BLKS;
	} else {
		nand_dbg_print(NAND_DBG_WARN,
			"Spectra: ioremap reg address: 0x%p\n", scratch_reg);
		denali->dev_info.wTotalBlocks = 1 << ioread8(scratch_reg);
		if (denali->dev_info.wTotalBlocks < 512)
			denali->dev_info.wTotalBlocks = GLOB_HWCTL_DEFAULT_BLKS;
		iounmap(scratch_reg);
	}
}

static void get_hynix_nand_para(struct denali_nand_info *denali)
{
	void __iomem *scratch_reg;
	uint32_t main_size, spare_size;

	switch (denali->dev_info.wDeviceID) {
	case 0xD5: /* Hynix H27UAG8T2A, H27UBG8U5A or H27UCG8VFA */
	case 0xD7: /* Hynix H27UDG8VEM, H27UCG8UDM or H27UCG8V5A */
		denali_write32(128, denali->flash_reg + PAGES_PER_BLOCK);
		denali_write32(4096, denali->flash_reg + DEVICE_MAIN_AREA_SIZE);
		denali_write32(224, denali->flash_reg + DEVICE_SPARE_AREA_SIZE);
		main_size = 4096 * ioread32(denali->flash_reg + DEVICES_CONNECTED);
		spare_size = 224 * ioread32(denali->flash_reg + DEVICES_CONNECTED);
		denali_write32(main_size, denali->flash_reg + LOGICAL_PAGE_DATA_SIZE);
		denali_write32(spare_size, denali->flash_reg + LOGICAL_PAGE_SPARE_SIZE);
		denali_write32(0, denali->flash_reg + DEVICE_WIDTH);
#if SUPPORT_15BITECC
		denali_write32(15, denali->flash_reg + ECC_CORRECTION);
#elif SUPPORT_8BITECC
		denali_write32(8, denali->flash_reg + ECC_CORRECTION);
#endif
		denali->dev_info.MLCDevice  = 1;
		break;
	default:
		nand_dbg_print(NAND_DBG_WARN,
			"Spectra: Unknown Hynix NAND (Device ID: 0x%x)."
			"Will use default parameter values instead.\n",
			denali->dev_info.wDeviceID);
	}

	scratch_reg = ioremap_nocache(SCRATCH_REG_ADDR, SCRATCH_REG_SIZE);
	if (!scratch_reg) {
		printk(KERN_ERR "Spectra: ioremap failed in %s, Line %d",
			__FILE__, __LINE__);
		denali->dev_info.wTotalBlocks = GLOB_HWCTL_DEFAULT_BLKS;
	} else {
		nand_dbg_print(NAND_DBG_WARN,
			"Spectra: ioremap reg address: 0x%p\n", scratch_reg);
		denali->dev_info.wTotalBlocks = 1 << ioread8(scratch_reg);
		if (denali->dev_info.wTotalBlocks < 512)
			denali->dev_info.wTotalBlocks = GLOB_HWCTL_DEFAULT_BLKS;
		iounmap(scratch_reg);
	}
}

/* determines how many NAND chips are connected to the controller. Note for
   Intel CE4100 devices we don't support more than one device. 
 */
static void find_valid_banks(struct denali_nand_info *denali)
{
	uint32_t id[LLD_MAX_FLASH_BANKS];
	int i;

	denali->total_used_banks = 1;
	for (i = 0; i < LLD_MAX_FLASH_BANKS; i++) {
		index_addr(denali, (uint32_t)(MODE_11 | (i << 24) | 0), 0x90);
		index_addr(denali, (uint32_t)(MODE_11 | (i << 24) | 1), 0);
		index_addr_read_data(denali, (uint32_t)(MODE_11 | (i << 24) | 2), &id[i]);

		nand_dbg_print(NAND_DBG_DEBUG,
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

	if (denali->platform == INTEL_CE4100)
	{
		/* Platform limitations of the CE4100 device limit
		 * users to a single chip solution for NAND.
                 * Multichip support is not enabled. 
		 */ 
		if (denali->total_used_banks != 1)
		{
			printk(KERN_ERR "Sorry, Intel CE4100 only supports "
					"a single NAND device.\n");
			BUG();
		}
	}
	nand_dbg_print(NAND_DBG_DEBUG,
		"denali->total_used_banks: %d\n", denali->total_used_banks);
}

static void detect_partition_feature(struct denali_nand_info *denali)
{
	if (ioread32(denali->flash_reg + FEATURES) & FEATURES__PARTITION) {
		if ((ioread32(denali->flash_reg + PERM_SRC_ID_1) &
			PERM_SRC_ID_1__SRCID) == SPECTRA_PARTITION_ID) {
			denali->dev_info.wSpectraStartBlock =
			    ((ioread32(denali->flash_reg + MIN_MAX_BANK_1) &
			      MIN_MAX_BANK_1__MIN_VALUE) *
			     denali->dev_info.wTotalBlocks)
			    +
			    (ioread32(denali->flash_reg + MIN_BLK_ADDR_1) &
			    MIN_BLK_ADDR_1__VALUE);

			denali->dev_info.wSpectraEndBlock =
			    (((ioread32(denali->flash_reg + MIN_MAX_BANK_1) &
			       MIN_MAX_BANK_1__MAX_VALUE) >> 2) *
			     denali->dev_info.wTotalBlocks)
			    +
			    (ioread32(denali->flash_reg + MAX_BLK_ADDR_1) &
			    MAX_BLK_ADDR_1__VALUE);

			denali->dev_info.wTotalBlocks *= denali->total_used_banks;

			if (denali->dev_info.wSpectraEndBlock >=
			    denali->dev_info.wTotalBlocks) {
				denali->dev_info.wSpectraEndBlock =
				    denali->dev_info.wTotalBlocks - 1;
			}

			denali->dev_info.wDataBlockNum =
				denali->dev_info.wSpectraEndBlock -
				denali->dev_info.wSpectraStartBlock + 1;
		} else {
			denali->dev_info.wTotalBlocks *= denali->total_used_banks;
			denali->dev_info.wSpectraStartBlock = SPECTRA_START_BLOCK;
			denali->dev_info.wSpectraEndBlock =
				denali->dev_info.wTotalBlocks - 1;
			denali->dev_info.wDataBlockNum =
				denali->dev_info.wSpectraEndBlock -
				denali->dev_info.wSpectraStartBlock + 1;
		}
	} else {
		denali->dev_info.wTotalBlocks *= denali->total_used_banks;
		denali->dev_info.wSpectraStartBlock = SPECTRA_START_BLOCK;
		denali->dev_info.wSpectraEndBlock = denali->dev_info.wTotalBlocks - 1;
		denali->dev_info.wDataBlockNum =
			denali->dev_info.wSpectraEndBlock -
			denali->dev_info.wSpectraStartBlock + 1;
	}
}

static void dump_device_info(struct denali_nand_info *denali)
{
	nand_dbg_print(NAND_DBG_DEBUG, "denali->dev_info:\n");
	nand_dbg_print(NAND_DBG_DEBUG, "DeviceMaker: 0x%x\n",
		denali->dev_info.wDeviceMaker);
	nand_dbg_print(NAND_DBG_DEBUG, "DeviceID: 0x%x\n",
		denali->dev_info.wDeviceID);
	nand_dbg_print(NAND_DBG_DEBUG, "DeviceType: 0x%x\n",
		denali->dev_info.wDeviceType);
	nand_dbg_print(NAND_DBG_DEBUG, "SpectraStartBlock: %d\n",
		denali->dev_info.wSpectraStartBlock);
	nand_dbg_print(NAND_DBG_DEBUG, "SpectraEndBlock: %d\n",
		denali->dev_info.wSpectraEndBlock);
	nand_dbg_print(NAND_DBG_DEBUG, "TotalBlocks: %d\n",
		denali->dev_info.wTotalBlocks);
	nand_dbg_print(NAND_DBG_DEBUG, "PagesPerBlock: %d\n",
		denali->dev_info.wPagesPerBlock);
	nand_dbg_print(NAND_DBG_DEBUG, "PageSize: %d\n",
		denali->dev_info.wPageSize);
	nand_dbg_print(NAND_DBG_DEBUG, "PageDataSize: %d\n",
		denali->dev_info.wPageDataSize);
	nand_dbg_print(NAND_DBG_DEBUG, "PageSpareSize: %d\n",
		denali->dev_info.wPageSpareSize);
	nand_dbg_print(NAND_DBG_DEBUG, "NumPageSpareFlag: %d\n",
		denali->dev_info.wNumPageSpareFlag);
	nand_dbg_print(NAND_DBG_DEBUG, "ECCBytesPerSector: %d\n",
		denali->dev_info.wECCBytesPerSector);
	nand_dbg_print(NAND_DBG_DEBUG, "BlockSize: %d\n",
		denali->dev_info.wBlockSize);
	nand_dbg_print(NAND_DBG_DEBUG, "BlockDataSize: %d\n",
		denali->dev_info.wBlockDataSize);
	nand_dbg_print(NAND_DBG_DEBUG, "DataBlockNum: %d\n",
		denali->dev_info.wDataBlockNum);
	nand_dbg_print(NAND_DBG_DEBUG, "PlaneNum: %d\n",
		denali->dev_info.bPlaneNum);
	nand_dbg_print(NAND_DBG_DEBUG, "DeviceMainAreaSize: %d\n",
		denali->dev_info.wDeviceMainAreaSize);
	nand_dbg_print(NAND_DBG_DEBUG, "DeviceSpareAreaSize: %d\n",
		denali->dev_info.wDeviceSpareAreaSize);
	nand_dbg_print(NAND_DBG_DEBUG, "DevicesConnected: %d\n",
		denali->dev_info.wDevicesConnected);
	nand_dbg_print(NAND_DBG_DEBUG, "DeviceWidth: %d\n",
		denali->dev_info.wDeviceWidth);
	nand_dbg_print(NAND_DBG_DEBUG, "HWRevision: 0x%x\n",
		denali->dev_info.wHWRevision);
	nand_dbg_print(NAND_DBG_DEBUG, "HWFeatures: 0x%x\n",
		denali->dev_info.wHWFeatures);
	nand_dbg_print(NAND_DBG_DEBUG, "ONFIDevFeatures: 0x%x\n",
		denali->dev_info.wONFIDevFeatures);
	nand_dbg_print(NAND_DBG_DEBUG, "ONFIOptCommands: 0x%x\n",
		denali->dev_info.wONFIOptCommands);
	nand_dbg_print(NAND_DBG_DEBUG, "ONFITimingMode: 0x%x\n",
		denali->dev_info.wONFITimingMode);
	nand_dbg_print(NAND_DBG_DEBUG, "ONFIPgmCacheTimingMode: 0x%x\n",
		denali->dev_info.wONFIPgmCacheTimingMode);
	nand_dbg_print(NAND_DBG_DEBUG, "MLCDevice: %s\n",
		denali->dev_info.MLCDevice ? "Yes" : "No");
	nand_dbg_print(NAND_DBG_DEBUG, "SpareSkipBytes: %d\n",
		denali->dev_info.wSpareSkipBytes);
	nand_dbg_print(NAND_DBG_DEBUG, "BitsInPageNumber: %d\n",
		denali->dev_info.nBitsInPageNumber);
	nand_dbg_print(NAND_DBG_DEBUG, "BitsInPageDataSize: %d\n",
		denali->dev_info.nBitsInPageDataSize);
	nand_dbg_print(NAND_DBG_DEBUG, "BitsInBlockDataSize: %d\n",
		denali->dev_info.nBitsInBlockDataSize);
}

static uint16_t NAND_Read_Device_ID(struct denali_nand_info *denali)
{
	uint16_t status = PASS;
	uint8_t no_of_planes;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	denali->dev_info.wDeviceMaker = ioread32(denali->flash_reg + MANUFACTURER_ID);
	denali->dev_info.wDeviceID = ioread32(denali->flash_reg + DEVICE_ID);
	denali->dev_info.bDeviceParam0 = ioread32(denali->flash_reg + DEVICE_PARAM_0);
	denali->dev_info.bDeviceParam1 = ioread32(denali->flash_reg + DEVICE_PARAM_1);
	denali->dev_info.bDeviceParam2 = ioread32(denali->flash_reg + DEVICE_PARAM_2);

	denali->dev_info.MLCDevice = ioread32(denali->flash_reg + DEVICE_PARAM_0) & 0x0c;

	if (ioread32(denali->flash_reg + ONFI_DEVICE_NO_OF_LUNS) &
		ONFI_DEVICE_NO_OF_LUNS__ONFI_DEVICE) { /* ONFI 1.0 NAND */
		if (FAIL == get_onfi_nand_para(denali))
			return FAIL;
	} else if (denali->dev_info.wDeviceMaker == 0xEC) { /* Samsung NAND */
		get_samsung_nand_para(denali);
	} else if (denali->dev_info.wDeviceMaker == 0x98) { /* Toshiba NAND */
		get_toshiba_nand_para(denali);
	} else if (denali->dev_info.wDeviceMaker == 0xAD) { /* Hynix NAND */
		get_hynix_nand_para(denali);
	} else {
		denali->dev_info.wTotalBlocks = GLOB_HWCTL_DEFAULT_BLKS;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Dump timing register values:"
			"acc_clks: %d, re_2_we: %d, we_2_re: %d,"
			"addr_2_data: %d, rdwr_en_lo_cnt: %d, "
			"rdwr_en_hi_cnt: %d, cs_setup_cnt: %d\n",
			ioread32(denali->flash_reg + ACC_CLKS),
			ioread32(denali->flash_reg + RE_2_WE),
			ioread32(denali->flash_reg + WE_2_RE),
			ioread32(denali->flash_reg + ADDR_2_DATA),
			ioread32(denali->flash_reg + RDWR_EN_LO_CNT),
			ioread32(denali->flash_reg + RDWR_EN_HI_CNT),
			ioread32(denali->flash_reg + CS_SETUP_CNT));

	denali->dev_info.wHWRevision = ioread32(denali->flash_reg + REVISION);
	denali->dev_info.wHWFeatures = ioread32(denali->flash_reg + FEATURES);

	denali->dev_info.wDeviceMainAreaSize =
		ioread32(denali->flash_reg + DEVICE_MAIN_AREA_SIZE);
	denali->dev_info.wDeviceSpareAreaSize =
		ioread32(denali->flash_reg + DEVICE_SPARE_AREA_SIZE);

	denali->dev_info.wPageDataSize =
		ioread32(denali->flash_reg + LOGICAL_PAGE_DATA_SIZE);

	/* Note: When using the Micon 4K NAND device, the controller will report
	 * Page Spare Size as 216 bytes. But Micron's Spec say it's 218 bytes.
	 * And if force set it to 218 bytes, the controller can not work
	 * correctly. So just let it be. But keep in mind that this bug may
	 * cause
	 * other problems in future.       - Yunpeng  2008-10-10
	 */
	denali->dev_info.wPageSpareSize =
		ioread32(denali->flash_reg + LOGICAL_PAGE_SPARE_SIZE);

	denali->dev_info.wPagesPerBlock = ioread32(denali->flash_reg + PAGES_PER_BLOCK);

	denali->dev_info.wPageSize =
	    denali->dev_info.wPageDataSize + denali->dev_info.wPageSpareSize;
	denali->dev_info.wBlockSize =
	    denali->dev_info.wPageSize * denali->dev_info.wPagesPerBlock;
	denali->dev_info.wBlockDataSize =
	    denali->dev_info.wPagesPerBlock * denali->dev_info.wPageDataSize;

	denali->dev_info.wDeviceWidth = ioread32(denali->flash_reg + DEVICE_WIDTH);
	denali->dev_info.wDeviceType =
		((ioread32(denali->flash_reg + DEVICE_WIDTH) > 0) ? 16 : 8);

	denali->dev_info.wDevicesConnected = ioread32(denali->flash_reg + DEVICES_CONNECTED);

	denali->dev_info.wSpareSkipBytes =
		ioread32(denali->flash_reg + SPARE_AREA_SKIP_BYTES) *
		denali->dev_info.wDevicesConnected;

	denali->dev_info.nBitsInPageNumber =
		ilog2(denali->dev_info.wPagesPerBlock);
	denali->dev_info.nBitsInPageDataSize =
		ilog2(denali->dev_info.wPageDataSize);
	denali->dev_info.nBitsInBlockDataSize =
		ilog2(denali->dev_info.wBlockDataSize);

	set_ecc_config(denali);

	no_of_planes = ioread32(denali->flash_reg + NUMBER_OF_PLANES) &
		NUMBER_OF_PLANES__VALUE;

	switch (no_of_planes) {
	case 0:
	case 1:
	case 3:
	case 7:
		denali->dev_info.bPlaneNum = no_of_planes + 1;
		break;
	default:
		status = FAIL;
		break;
	}

	find_valid_banks(denali);

	detect_partition_feature(denali);

	dump_device_info(denali);

	/* If the user specified to override the default timings
	 * with a specific ONFI mode, we apply those changes here. 
	 */
	if (onfi_timing_mode != NAND_DEFAULT_TIMINGS)
	{
		NAND_ONFi_Timing_Mode(denali, onfi_timing_mode);
	}

	return status;
}

static void NAND_LLD_Enable_Disable_Interrupts(struct denali_nand_info *denali,
					uint16_t INT_ENABLE)
{
	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	if (INT_ENABLE)
		denali_write32(1, denali->flash_reg + GLOBAL_INT_ENABLE);
	else
		denali_write32(0, denali->flash_reg + GLOBAL_INT_ENABLE);
}

/* validation function to verify that the controlling software is making
   a valid request
 */
static inline bool is_flash_bank_valid(int flash_bank)
{
	return (flash_bank >= 0 && flash_bank < 4); 
}

static void denali_irq_init(struct denali_nand_info *denali)
{
	uint32_t int_mask = 0;

	/* Disable global interrupts */
	NAND_LLD_Enable_Disable_Interrupts(denali, false);

	int_mask = DENALI_IRQ_ALL;

	/* Clear all status bits */
	denali_write32(0xFFFF, denali->flash_reg + INTR_STATUS0);
	denali_write32(0xFFFF, denali->flash_reg + INTR_STATUS1);
	denali_write32(0xFFFF, denali->flash_reg + INTR_STATUS2);
	denali_write32(0xFFFF, denali->flash_reg + INTR_STATUS3);

	denali_irq_enable(denali, int_mask);
}

static void denali_irq_cleanup(int irqnum, struct denali_nand_info *denali)
{
	NAND_LLD_Enable_Disable_Interrupts(denali, false);
	free_irq(irqnum, denali);
}

static void denali_irq_enable(struct denali_nand_info *denali, uint32_t int_mask)
{
	denali_write32(int_mask, denali->flash_reg + INTR_EN0);
	denali_write32(int_mask, denali->flash_reg + INTR_EN1);
	denali_write32(int_mask, denali->flash_reg + INTR_EN2);
	denali_write32(int_mask, denali->flash_reg + INTR_EN3);
}

/* This function only returns when an interrupt that this driver cares about
 * occurs. This is to reduce the overhead of servicing interrupts 
 */
static inline uint32_t denali_irq_detected(struct denali_nand_info *denali)
{
	return (read_interrupt_status(denali) & DENALI_IRQ_ALL);
}

/* Interrupts are cleared by writing a 1 to the appropriate status bit */
static inline void clear_interrupt(struct denali_nand_info *denali, uint32_t irq_mask)
{
	uint32_t intr_status_reg = 0;

	intr_status_reg = intr_status_addresses[denali->flash_bank];

	denali_write32(irq_mask, denali->flash_reg + intr_status_reg);
}

static void clear_interrupts(struct denali_nand_info *denali)
{
	uint32_t status = 0x0;
	spin_lock_irq(&denali->irq_lock);

	status = read_interrupt_status(denali);

#if DEBUG_DENALI
	denali->irq_debug_array[denali->idx++] = 0x30000000 | status;
	denali->idx %= 32;
#endif

	denali->irq_status = 0x0;
	spin_unlock_irq(&denali->irq_lock);
}

static uint32_t read_interrupt_status(struct denali_nand_info *denali)
{
	uint32_t intr_status_reg = 0;

	intr_status_reg = intr_status_addresses[denali->flash_bank];

	return ioread32(denali->flash_reg + intr_status_reg);
}

#if DEBUG_DENALI
static void print_irq_log(struct denali_nand_info *denali)
{
	int i = 0;

	printk("ISR debug log index = %X\n", denali->idx);
	for (i = 0; i < 32; i++)
	{
		printk("%08X: %08X\n", i, denali->irq_debug_array[i]);
	}
}
#endif

/* This is the interrupt service routine. It handles all interrupts 
 * sent to this device. Note that on CE4100, this is a shared 
 * interrupt. 
 */
static irqreturn_t denali_isr(int irq, void *dev_id)
{
	struct denali_nand_info *denali = dev_id;
	uint32_t irq_status = 0x0;
	irqreturn_t result = IRQ_NONE;

	spin_lock(&denali->irq_lock);

	/* check to see if a valid NAND chip has 
         * been selected. 
	 */
	if (is_flash_bank_valid(denali->flash_bank))
	{
		/* check to see if controller generated 
		 * the interrupt, since this is a shared interrupt */
		if ((irq_status = denali_irq_detected(denali)) != 0)
		{
#if DEBUG_DENALI
			denali->irq_debug_array[denali->idx++] = 0x10000000 | irq_status;
			denali->idx %= 32;

			printk("IRQ status = 0x%04x\n", irq_status);
#endif
			/* handle interrupt */
			/* first acknowledge it */
			clear_interrupt(denali, irq_status);
			/* store the status in the device context for someone
			   to read */
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
#define BANK(x) ((x) << 24)

static uint32_t wait_for_irq(struct denali_nand_info *denali, uint32_t irq_mask)
{
	unsigned long comp_res = 0;
	uint32_t intr_status = 0;
	bool retry = false;
	unsigned long timeout = msecs_to_jiffies(1000);

	do
	{
#if DEBUG_DENALI
		printk("waiting for 0x%x\n", irq_mask);
#endif
		comp_res = wait_for_completion_timeout(&denali->complete, timeout);
		spin_lock_irq(&denali->irq_lock);
		intr_status = denali->irq_status;

#if DEBUG_DENALI
		denali->irq_debug_array[denali->idx++] = 0x20000000 | (irq_mask << 16) | intr_status;
		denali->idx %= 32;
#endif

		if (intr_status & irq_mask)
		{
			denali->irq_status &= ~irq_mask;
			spin_unlock_irq(&denali->irq_lock);
#if DEBUG_DENALI
			if (retry) printk("status on retry = 0x%x\n", intr_status);
#endif
			/* our interrupt was detected */
			break;
		}
		else 
		{
			/* these are not the interrupts you are looking for - 
		           need to wait again */
			spin_unlock_irq(&denali->irq_lock);
#if DEBUG_DENALI
			print_irq_log(denali);
			printk("received irq nobody cared: irq_status = 0x%x,"
				" irq_mask = 0x%x, timeout = %ld\n", intr_status, irq_mask, comp_res);
#endif
			retry = true;
		}
	} while (comp_res != 0);

	if (comp_res == 0)
	{
		/* timeout */
		printk(KERN_ERR "timeout occurred, status = 0x%x, mask = 0x%x\n", 
	       			intr_status, irq_mask);

		intr_status = 0;
	}
	return intr_status;
}

/* This helper function setups the registers for ECC and whether or not 
   the spare area will be transfered. */
static void setup_ecc_for_xfer(struct denali_nand_info *denali, bool ecc_en, 
				bool transfer_spare)
{
	int ecc_en_flag = 0, transfer_spare_flag = 0; 

	/* set ECC, transfer spare bits if needed */
	ecc_en_flag = ecc_en ? ECC_ENABLE__FLAG : 0;
	transfer_spare_flag = transfer_spare ? TRANSFER_SPARE_REG__FLAG : 0;

	/* Enable spare area/ECC per user's request. */
	denali_write32(ecc_en_flag, denali->flash_reg + ECC_ENABLE);
	denali_write32(transfer_spare_flag, denali->flash_reg + TRANSFER_SPARE_REG);
}

/* sends a pipeline command operation to the controller. See the Denali NAND 
   controller's user guide for more information (section 4.2.3.6). 
 */
static int denali_send_pipeline_cmd(struct denali_nand_info *denali, bool ecc_en, 
					bool transfer_spare, int access_type, 
					int op)
{
	int status = PASS;
	uint32_t addr = 0x0, cmd = 0x0, page_count = 1, irq_status = 0, 
		 irq_mask = 0;

	if (op == DENALI_READ) irq_mask = INTR_STATUS0__LOAD_COMP;
	else if (op == DENALI_WRITE) irq_mask = 0;
	else BUG();

	setup_ecc_for_xfer(denali, ecc_en, transfer_spare);

#if DEBUG_DENALI
	spin_lock_irq(&denali->irq_lock);
	denali->irq_debug_array[denali->idx++] = 0x40000000 | ioread32(denali->flash_reg + ECC_ENABLE) | (access_type << 4);
	denali->idx %= 32;
	spin_unlock_irq(&denali->irq_lock);
#endif


	/* clear interrupts */
	clear_interrupts(denali);	

	addr = BANK(denali->flash_bank) | denali->page;

	if (op == DENALI_WRITE && access_type != SPARE_ACCESS)
	{
		cmd = MODE_01 | addr; 
		denali_write32(cmd, denali->flash_mem);
	}
	else if (op == DENALI_WRITE && access_type == SPARE_ACCESS)
	{
		/* read spare area */
		cmd = MODE_10 | addr; 
		index_addr(denali, (uint32_t)cmd, access_type);

		cmd = MODE_01 | addr; 
		denali_write32(cmd, denali->flash_mem);
	}
	else if (op == DENALI_READ)
	{
		/* setup page read request for access type */
		cmd = MODE_10 | addr; 
		index_addr(denali, (uint32_t)cmd, access_type);

		/* page 33 of the NAND controller spec indicates we should not
		   use the pipeline commands in Spare area only mode. So we 
		   don't.
		 */
		if (access_type == SPARE_ACCESS)
		{
			cmd = MODE_01 | addr;
			denali_write32(cmd, denali->flash_mem);
		}
		else
		{
			index_addr(denali, (uint32_t)cmd, 0x2000 | op | page_count);
	
			/* wait for command to be accepted  
			 * can always use status0 bit as the mask is identical for each
			 * bank. */
			irq_status = wait_for_irq(denali, irq_mask);

			if (irq_status == 0)
			{
				printk(KERN_ERR "cmd, page, addr on timeout "
					"(0x%x, 0x%x, 0x%x)\n", cmd, denali->page, addr);
				status = FAIL;
			}
			else
			{
				cmd = MODE_01 | addr;
				denali_write32(cmd, denali->flash_mem);
			}
		}
	}
	return status;
}

/* helper function that simply writes a buffer to the flash */
static int write_data_to_flash_mem(struct denali_nand_info *denali, const uint8_t *buf, 
					int len) 
{
	uint32_t i = 0, *buf32;

	/* verify that the len is a multiple of 4. see comment in 
	 * read_data_from_flash_mem() */	
	BUG_ON((len % 4) != 0);

	/* write the data to the flash memory */
	buf32 = (uint32_t *)buf;
	for (i = 0; i < len / 4; i++)
	{
		denali_write32(*buf32++, denali->flash_mem + 0x10);
	}
	return i*4; /* intent is to return the number of bytes read */ 
}

/* helper function that simply reads a buffer from the flash */
static int read_data_from_flash_mem(struct denali_nand_info *denali, uint8_t *buf, 
					int len)
{
	uint32_t i = 0, *buf32;

	/* we assume that len will be a multiple of 4, if not
	 * it would be nice to know about it ASAP rather than
	 * have random failures... 
         *	
	 * This assumption is based on the fact that this 
	 * function is designed to be used to read flash pages, 
	 * which are typically multiples of 4...
	 */

	BUG_ON((len % 4) != 0);

	/* transfer the data from the flash */
	buf32 = (uint32_t *)buf;
	for (i = 0; i < len / 4; i++)
	{
		*buf32++ = ioread32(denali->flash_mem + 0x10);
	}
	return i*4; /* intent is to return the number of bytes read */ 
}

/* writes OOB data to the device */
static int write_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t irq_status = 0;
	uint32_t irq_mask = INTR_STATUS0__PROGRAM_COMP | 
						INTR_STATUS0__PROGRAM_FAIL;
	int status = 0;

	denali->page = page;

	if (denali_send_pipeline_cmd(denali, false, false, SPARE_ACCESS, 
							DENALI_WRITE) == PASS) 
	{
		write_data_to_flash_mem(denali, buf, mtd->oobsize);

#if DEBUG_DENALI
		spin_lock_irq(&denali->irq_lock);
		denali->irq_debug_array[denali->idx++] = 0x80000000 | mtd->oobsize;
		denali->idx %= 32;
		spin_unlock_irq(&denali->irq_lock);
#endif

	
		/* wait for operation to complete */
		irq_status = wait_for_irq(denali, irq_mask);

		if (irq_status == 0)
		{
			printk(KERN_ERR "OOB write failed\n");
			status = -EIO;
		}
	}
	else 
	{ 	
		printk(KERN_ERR "unable to send pipeline command\n");
		status = -EIO; 
	}
	return status;
}

/* reads OOB data from the device */
static void read_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t irq_mask = INTR_STATUS0__LOAD_COMP, irq_status = 0, addr = 0x0, cmd = 0x0;

	denali->page = page;

#if DEBUG_DENALI
	printk("read_oob %d\n", page);
#endif
	if (denali_send_pipeline_cmd(denali, false, true, SPARE_ACCESS, 
							DENALI_READ) == PASS) 
	{
		read_data_from_flash_mem(denali, buf, mtd->oobsize);	

		/* wait for command to be accepted  
		 * can always use status0 bit as the mask is identical for each
		 * bank. */
		irq_status = wait_for_irq(denali, irq_mask);

		if (irq_status == 0)
		{
			printk(KERN_ERR "page on OOB timeout %d\n", denali->page);
		}

		/* We set the device back to MAIN_ACCESS here as I observed
		 * instability with the controller if you do a block erase
		 * and the last transaction was a SPARE_ACCESS. Block erase
		 * is reliable (according to the MTD test infrastructure)
		 * if you are in MAIN_ACCESS. 
		 */
		addr = BANK(denali->flash_bank) | denali->page;
		cmd = MODE_10 | addr; 
		index_addr(denali, (uint32_t)cmd, MAIN_ACCESS);

#if DEBUG_DENALI
		spin_lock_irq(&denali->irq_lock);
		denali->irq_debug_array[denali->idx++] = 0x60000000 | mtd->oobsize;
		denali->idx %= 32;
		spin_unlock_irq(&denali->irq_lock);
#endif
	}
}

/* this function examines buffers to see if they contain data that 
 * indicate that the buffer is part of an erased region of flash.
 */
bool is_erased(uint8_t *buf, int len)
{
	int i = 0;
	for (i = 0; i < len; i++)
	{	
		if (buf[i] != 0xFF)
		{
			return false;
		}
	}
	return true;
}
#define ECC_SECTOR_SIZE 512

#define ECC_SECTOR(x)	(((x) & ECC_ERROR_ADDRESS__SECTOR_NR) >> 12)
#define ECC_BYTE(x)	(((x) & ECC_ERROR_ADDRESS__OFFSET))
#define ECC_CORRECTION_VALUE(x) ((x) & ERR_CORRECTION_INFO__BYTEMASK)
#define ECC_ERROR_CORRECTABLE(x) (!((x) & ERR_CORRECTION_INFO))
#define ECC_ERR_DEVICE(x)	((x) & ERR_CORRECTION_INFO__DEVICE_NR >> 8)
#define ECC_LAST_ERR(x)		((x) & ERR_CORRECTION_INFO__LAST_ERR_INFO)

static bool handle_ecc(struct denali_nand_info *denali, uint8_t *buf, 
			uint8_t *oobbuf, uint32_t irq_status)
{
	bool check_erased_page = false;

	if (irq_status & INTR_STATUS0__ECC_ERR)
	{
		/* read the ECC errors. we'll ignore them for now */
		uint32_t err_address = 0, err_correction_info = 0;
		uint32_t err_byte = 0, err_sector = 0, err_device = 0;
		uint32_t err_correction_value = 0;

		do 
		{
			err_address = ioread32(denali->flash_reg + 
						ECC_ERROR_ADDRESS);
			err_sector = ECC_SECTOR(err_address);
			err_byte = ECC_BYTE(err_address);


			err_correction_info = ioread32(denali->flash_reg + 
						ERR_CORRECTION_INFO);
			err_correction_value = 
				ECC_CORRECTION_VALUE(err_correction_info);
			err_device = ECC_ERR_DEVICE(err_correction_info);

			if (ECC_ERROR_CORRECTABLE(err_correction_info))
			{
				/* offset in our buffer is computed as:
				   sector number * sector size + offset in 
				   sector
				 */
				int offset = err_sector * ECC_SECTOR_SIZE + 
								err_byte;
				if (offset < denali->mtd.writesize)
				{
					/* correct the ECC error */
					buf[offset] ^= err_correction_value;
					denali->mtd.ecc_stats.corrected++;
				}
				else
				{
					/* bummer, couldn't correct the error */
					printk(KERN_ERR "ECC offset invalid\n");
					denali->mtd.ecc_stats.failed++;
				}
			}
			else
			{
				/* if the error is not correctable, need to 
				 * look at the page to see if it is an erased page.
				 * if so, then it's not a real ECC error */	
				check_erased_page = true;
			}

#if DEBUG_DENALI 
			printk("Detected ECC error in page %d: err_addr = 0x%08x,"
				" info to fix is 0x%08x\n", denali->page, err_address, 
				err_correction_info);
#endif
		} while (!ECC_LAST_ERR(err_correction_info));
	}
	return check_erased_page;
}

/* programs the controller to either enable/disable DMA transfers */
static void denali_enable_dma(struct denali_nand_info *denali, bool en)
{
	uint32_t reg_val = 0x0;

	if (en) reg_val = DMA_ENABLE__FLAG;

	denali_write32(reg_val, denali->flash_reg + DMA_ENABLE);
	ioread32(denali->flash_reg + DMA_ENABLE);
}

/* setups the HW to perform the data DMA */
static void denali_setup_dma(struct denali_nand_info *denali, int op)
{
	uint32_t mode = 0x0;
	const int page_count = 1;
	dma_addr_t addr = denali->buf.dma_buf;

	mode = MODE_10 | BANK(denali->flash_bank);

	/* DMA is a four step process */

	/* 1. setup transfer type and # of pages */
	index_addr(denali, mode | denali->page, 0x2000 | op | page_count);

	/* 2. set memory high address bits 23:8 */
	index_addr(denali, mode | ((uint16_t)(addr >> 16) << 8), 0x2200);

	/* 3. set memory low address bits 23:8 */
	index_addr(denali, mode | ((uint16_t)addr << 8), 0x2300);

	/* 4.  interrupt when complete, burst len = 64 bytes*/
	index_addr(denali, mode | 0x14000, 0x2400);
}

/* writes a page. user specifies type, and this function handles the 
   configuration details. */
static void write_page(struct mtd_info *mtd, struct nand_chip *chip, 
			const uint8_t *buf, bool raw_xfer)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	struct pci_dev *pci_dev = denali->dev;

	dma_addr_t addr = denali->buf.dma_buf;
	size_t size = denali->mtd.writesize + denali->mtd.oobsize;

	uint32_t irq_status = 0;
	uint32_t irq_mask = INTR_STATUS0__DMA_CMD_COMP | 
						INTR_STATUS0__PROGRAM_FAIL;

	/* if it is a raw xfer, we want to disable ecc, and send
	 * the spare area.
	 * !raw_xfer - enable ecc
	 * raw_xfer - transfer spare
	 */
	setup_ecc_for_xfer(denali, !raw_xfer, raw_xfer);

	/* copy buffer into DMA buffer */
	memcpy(denali->buf.buf, buf, mtd->writesize);

	if (raw_xfer)
	{
		/* transfer the data to the spare area */
		memcpy(denali->buf.buf + mtd->writesize, 
			chip->oob_poi, 
			mtd->oobsize); 
	}

	pci_dma_sync_single_for_device(pci_dev, addr, size, PCI_DMA_TODEVICE);

	clear_interrupts(denali);
	denali_enable_dma(denali, true);	

	denali_setup_dma(denali, DENALI_WRITE);

	/* wait for operation to complete */
	irq_status = wait_for_irq(denali, irq_mask);

	if (irq_status == 0)
	{
		printk(KERN_ERR "timeout on write_page (type = %d)\n", raw_xfer);
		denali->status = 
	   	   (irq_status & INTR_STATUS0__PROGRAM_FAIL) ? NAND_STATUS_FAIL : 
						   	     PASS;
	}

	denali_enable_dma(denali, false);	
	pci_dma_sync_single_for_cpu(pci_dev, addr, size, PCI_DMA_TODEVICE);
}

/* NAND core entry points */

/* this is the callback that the NAND core calls to write a page. Since 
   writing a page with ECC or without is similar, all the work is done 
   by write_page above.   */
static void denali_write_page(struct mtd_info *mtd, struct nand_chip *chip, 
				const uint8_t *buf)
{
	/* for regular page writes, we let HW handle all the ECC
         * data written to the device. */
	write_page(mtd, chip, buf, false);
}

/* This is the callback that the NAND core calls to write a page without ECC. 
   raw access is similiar to ECC page writes, so all the work is done in the
   write_page() function above. 
 */
static void denali_write_page_raw(struct mtd_info *mtd, struct nand_chip *chip, 
					const uint8_t *buf)
{
	/* for raw page writes, we want to disable ECC and simply write 
	   whatever data is in the buffer. */
	write_page(mtd, chip, buf, true);
}

static int denali_write_oob(struct mtd_info *mtd, struct nand_chip *chip, 
			    int page)
{
	return write_oob_data(mtd, chip->oob_poi, page);	
}

static int denali_read_oob(struct mtd_info *mtd, struct nand_chip *chip, 
			   int page, int sndcmd)
{
	read_oob_data(mtd, chip->oob_poi, page);

	return 0; /* notify NAND core to send command to 
                   * NAND device. */
}

static int denali_read_page(struct mtd_info *mtd, struct nand_chip *chip,
			    uint8_t *buf, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	struct pci_dev *pci_dev = denali->dev;

	dma_addr_t addr = denali->buf.dma_buf;
	size_t size = denali->mtd.writesize + denali->mtd.oobsize;

	uint32_t irq_status = 0;
	uint32_t irq_mask = INTR_STATUS0__ECC_TRANSACTION_DONE | 
			    INTR_STATUS0__ECC_ERR;
	bool check_erased_page = false;

	setup_ecc_for_xfer(denali, true, false);

	denali_enable_dma(denali, true);
	pci_dma_sync_single_for_device(pci_dev, addr, size, PCI_DMA_FROMDEVICE);

	clear_interrupts(denali);
	denali_setup_dma(denali, DENALI_READ);

	/* wait for operation to complete */
	irq_status = wait_for_irq(denali, irq_mask);

	pci_dma_sync_single_for_cpu(pci_dev, addr, size, PCI_DMA_FROMDEVICE);

	memcpy(buf, denali->buf.buf, mtd->writesize);
	
	check_erased_page = handle_ecc(denali, buf, chip->oob_poi, irq_status);
	denali_enable_dma(denali, false);

	if (check_erased_page)
	{
		read_oob_data(&denali->mtd, chip->oob_poi, denali->page);

		/* check ECC failures that may have occurred on erased pages */
		if (check_erased_page)
		{
			if (!is_erased(buf, denali->mtd.writesize))
			{
				denali->mtd.ecc_stats.failed++;
			}
			if (!is_erased(buf, denali->mtd.oobsize))
			{
				denali->mtd.ecc_stats.failed++;
			}
		}	
	}
	return 0;
}

static int denali_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	struct pci_dev *pci_dev = denali->dev;

	dma_addr_t addr = denali->buf.dma_buf;
	size_t size = denali->mtd.writesize + denali->mtd.oobsize;

	uint32_t irq_status = 0;
	uint32_t irq_mask = INTR_STATUS0__DMA_CMD_COMP;
						
	setup_ecc_for_xfer(denali, false, true);
	denali_enable_dma(denali, true);

	pci_dma_sync_single_for_device(pci_dev, addr, size, PCI_DMA_FROMDEVICE);

	clear_interrupts(denali);
	denali_setup_dma(denali, DENALI_READ);

	/* wait for operation to complete */
	irq_status = wait_for_irq(denali, irq_mask);

	pci_dma_sync_single_for_cpu(pci_dev, addr, size, PCI_DMA_FROMDEVICE);

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
	{
		result = denali->buf.buf[denali->buf.head++];
	}

#if DEBUG_DENALI
	printk("read byte -> 0x%02x\n", result);
#endif
	return result;
}

static void denali_select_chip(struct mtd_info *mtd, int chip)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
#if DEBUG_DENALI
	printk("denali select chip %d\n", chip);
#endif
	spin_lock_irq(&denali->irq_lock);
	denali->flash_bank = chip;
	spin_unlock_irq(&denali->irq_lock);
}

static int denali_waitfunc(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	int status = denali->status;
	denali->status = 0;

#if DEBUG_DENALI
	printk("waitfunc %d\n", status);
#endif
	return status;
}

static void denali_erase(struct mtd_info *mtd, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	uint32_t cmd = 0x0, irq_status = 0;

#if DEBUG_DENALI
	printk("erase page: %d\n", page);
#endif
	/* clear interrupts */
	clear_interrupts(denali);	

	/* setup page read request for access type */
	cmd = MODE_10 | BANK(denali->flash_bank) | page;
	index_addr(denali, (uint32_t)cmd, 0x1);

	/* wait for erase to complete or failure to occur */
	irq_status = wait_for_irq(denali, INTR_STATUS0__ERASE_COMP | 
					INTR_STATUS0__ERASE_FAIL);

	denali->status = (irq_status & INTR_STATUS0__ERASE_FAIL) ? NAND_STATUS_FAIL : 
								 PASS;
}

static void denali_cmdfunc(struct mtd_info *mtd, unsigned int cmd, int col, 
			   int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

#if DEBUG_DENALI
	printk("cmdfunc: 0x%x %d %d\n", cmd, col, page);
#endif
	switch (cmd)
	{ 
		case NAND_CMD_PAGEPROG:
			break;
		case NAND_CMD_STATUS:
			read_status(denali);
			break;
		case NAND_CMD_READID:
			reset_buf(denali);
			if (denali->flash_bank < denali->total_used_banks)
			{
				/* write manufacturer information into nand 
				   buffer for NAND subsystem to fetch.
  			         */ 
	                        write_byte_to_buf(denali, denali->dev_info.wDeviceMaker);
	                        write_byte_to_buf(denali, denali->dev_info.wDeviceID);
	                        write_byte_to_buf(denali, denali->dev_info.bDeviceParam0);
	                        write_byte_to_buf(denali, denali->dev_info.bDeviceParam1);
	                        write_byte_to_buf(denali, denali->dev_info.bDeviceParam2);
			}
			else 
			{
				int i;
				for (i = 0; i < 5; i++) 
					write_byte_to_buf(denali, 0xff);
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
			printk(KERN_ERR ": unsupported command received 0x%x\n", cmd);
			break;
	}
}

/* stubs for ECC functions not used by the NAND core */
static int denali_ecc_calculate(struct mtd_info *mtd, const uint8_t *data, 
				uint8_t *ecc_code)
{
	printk(KERN_ERR "denali_ecc_calculate called unexpectedly\n");
	BUG();
	return -EIO;
}

static int denali_ecc_correct(struct mtd_info *mtd, uint8_t *data, 
				uint8_t *read_ecc, uint8_t *calc_ecc)
{
	printk(KERN_ERR "denali_ecc_correct called unexpectedly\n");
	BUG();
	return -EIO;
}

static void denali_ecc_hwctl(struct mtd_info *mtd, int mode)
{
	printk(KERN_ERR "denali_ecc_hwctl called unexpectedly\n");
	BUG();
}
/* end NAND core entry points */

/* Initialization code to bring the device up to a known good state */
static void denali_hw_init(struct denali_nand_info *denali)
{
	denali_irq_init(denali);
	NAND_Flash_Reset(denali);
	denali_write32(0x0F, denali->flash_reg + RB_PIN_ENABLED);
	denali_write32(CHIP_EN_DONT_CARE__FLAG, denali->flash_reg + CHIP_ENABLE_DONT_CARE);

	denali_write32(0x0, denali->flash_reg + SPARE_AREA_SKIP_BYTES);
	denali_write32(0xffff, denali->flash_reg + SPARE_AREA_MARKER);

	/* Should set value for these registers when init */
	denali_write32(0, denali->flash_reg + TWO_ROW_ADDR_CYCLES);
	denali_write32(1, denali->flash_reg + ECC_ENABLE);
}

/* ECC layout for SLC devices. Denali spec indicates SLC fixed at 4 bytes */
#define ECC_BYTES_SLC   4 * (2048 / ECC_SECTOR_SIZE)
static struct nand_ecclayout nand_oob_slc = {
	.eccbytes = 4,
	.eccpos = { 0, 1, 2, 3 }, /* not used */
	.oobfree = {{ 
			.offset = ECC_BYTES_SLC, 
			.length = 64 - ECC_BYTES_SLC  
		   }}
};

#define ECC_BYTES_MLC   14 * (2048 / ECC_SECTOR_SIZE)
static struct nand_ecclayout nand_oob_mlc_14bit = {
	.eccbytes = 14,
	.eccpos = { 0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13 }, /* not used */
	.oobfree = {{ 
			.offset = ECC_BYTES_MLC, 
			.length = 64 - ECC_BYTES_MLC  
		   }}
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

/* initalize driver data structures */
void denali_drv_init(struct denali_nand_info *denali)
{
	denali->idx = 0;

	/* setup interrupt handler */
	/* the completion object will be used to notify 
	 * the callee that the interrupt is done */
	init_completion(&denali->complete);

	/* the spinlock will be used to synchronize the ISR
	 * with any element that might be access shared 
	 * data (interrupt status) */
	spin_lock_init(&denali->irq_lock);

	/* indicate that MTD has not selected a valid bank yet */
	denali->flash_bank = CHIP_SELECT_INVALID;

	/* initialize our irq_status variable to indicate no interrupts */
	denali->irq_status = 0;
}

/* driver entry point */
static int denali_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int ret = -ENODEV;
	resource_size_t csr_base, mem_base;
	unsigned long csr_len, mem_len;
	struct denali_nand_info *denali;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	denali = kzalloc(sizeof(*denali), GFP_KERNEL);
	if (!denali)
		return -ENOMEM;

	ret = pci_enable_device(dev);
	if (ret) {
		printk(KERN_ERR "Spectra: pci_enable_device failed.\n");
		goto failed_enable;
	}

	if (id->driver_data == INTEL_CE4100) {
		/* Due to a silicon limitation, we can only support 
		 * ONFI timing mode 1 and below. 
		 */ 
		if (onfi_timing_mode < -1 || onfi_timing_mode > 1)
		{
			printk("Intel CE4100 only supports ONFI timing mode 1 "
				"or below\n");
			ret = -EINVAL;
			goto failed_enable;
		}
		denali->platform = INTEL_CE4100;
		mem_base = pci_resource_start(dev, 0);
		mem_len = pci_resource_len(dev, 1);
		csr_base = pci_resource_start(dev, 1);
		csr_len = pci_resource_len(dev, 1);
	} else {
		denali->platform = INTEL_MRST;
		csr_base = pci_resource_start(dev, 0);
		csr_len = pci_resource_start(dev, 0);
		mem_base = pci_resource_start(dev, 1);
		mem_len = pci_resource_len(dev, 1);
		if (!mem_len) {
			mem_base = csr_base + csr_len;
			mem_len = csr_len;
			nand_dbg_print(NAND_DBG_WARN,
				       "Spectra: No second BAR for PCI device; assuming %08Lx\n",
				       (uint64_t)csr_base);
		}
	}

	/* Is 32-bit DMA supported? */
	ret = pci_set_dma_mask(dev, DMA_BIT_MASK(32));

	if (ret)
	{
		printk(KERN_ERR "Spectra: no usable DMA configuration\n");
		goto failed_enable;
	}
	denali->buf.dma_buf = pci_map_single(dev, denali->buf.buf, DENALI_BUF_SIZE, 
					 PCI_DMA_BIDIRECTIONAL);

	if (pci_dma_mapping_error(dev, denali->buf.dma_buf))
	{
		printk(KERN_ERR "Spectra: failed to map DMA buffer\n");
		goto failed_enable;
	}

	pci_set_master(dev);
	denali->dev = dev;

	ret = pci_request_regions(dev, DENALI_NAND_NAME);
	if (ret) {
		printk(KERN_ERR "Spectra: Unable to request memory regions\n");
		goto failed_req_csr;
	}

	denali->flash_reg = ioremap_nocache(csr_base, csr_len);
	if (!denali->flash_reg) {
		printk(KERN_ERR "Spectra: Unable to remap memory region\n");
		ret = -ENOMEM;
		goto failed_remap_csr;
	}
	nand_dbg_print(NAND_DBG_DEBUG, "Spectra: CSR 0x%08Lx -> 0x%p (0x%lx)\n",
		       (uint64_t)csr_base, denali->flash_reg, csr_len);

	denali->flash_mem = ioremap_nocache(mem_base, mem_len);
	if (!denali->flash_mem) {
		printk(KERN_ERR "Spectra: ioremap_nocache failed!");
		iounmap(denali->flash_reg);
		ret = -ENOMEM;
		goto failed_remap_csr;
	}

	nand_dbg_print(NAND_DBG_WARN,
		"Spectra: Remapped flash base address: "
		"0x%p, len: %ld\n",
		denali->flash_mem, csr_len);

	denali_hw_init(denali);
	denali_drv_init(denali);

	nand_dbg_print(NAND_DBG_DEBUG, "Spectra: IRQ %d\n", dev->irq);
	if (request_irq(dev->irq, denali_isr, IRQF_SHARED,
			DENALI_NAND_NAME, denali)) {
		printk(KERN_ERR "Spectra: Unable to allocate IRQ\n");
		ret = -ENODEV;
		goto failed_request_irq;
	}

	/* now that our ISR is registered, we can enable interrupts */
	NAND_LLD_Enable_Disable_Interrupts(denali, true);

	pci_set_drvdata(dev, denali);

	NAND_Read_Device_ID(denali);

	/* MTD supported page sizes vary by kernel. We validate our 
           kernel supports the device here.
	 */
	if (denali->dev_info.wPageSize > NAND_MAX_PAGESIZE + NAND_MAX_OOBSIZE)
	{
		ret = -ENODEV;
		printk(KERN_ERR "Spectra: device size not supported by this "
			"version of MTD.");
		goto failed_nand;
	}

	nand_dbg_print(NAND_DBG_DEBUG, "Dump timing register values:"
			"acc_clks: %d, re_2_we: %d, we_2_re: %d,"
			"addr_2_data: %d, rdwr_en_lo_cnt: %d, "
			"rdwr_en_hi_cnt: %d, cs_setup_cnt: %d\n",
			ioread32(denali->flash_reg + ACC_CLKS),
			ioread32(denali->flash_reg + RE_2_WE),
			ioread32(denali->flash_reg + WE_2_RE),
			ioread32(denali->flash_reg + ADDR_2_DATA),
			ioread32(denali->flash_reg + RDWR_EN_LO_CNT),
			ioread32(denali->flash_reg + RDWR_EN_HI_CNT),
			ioread32(denali->flash_reg + CS_SETUP_CNT));

	denali->mtd.name = "Denali NAND";
	denali->mtd.owner = THIS_MODULE;
	denali->mtd.priv = &denali->nand;

	/* register the driver with the NAND core subsystem */
	denali->nand.select_chip = denali_select_chip;
	denali->nand.cmdfunc = denali_cmdfunc;
	denali->nand.read_byte = denali_read_byte;
	denali->nand.waitfunc = denali_waitfunc;

	/* scan for NAND devices attached to the controller 
	 * this is the first stage in a two step process to register
	 * with the nand subsystem */	
	if (nand_scan_ident(&denali->mtd, LLD_MAX_FLASH_BANKS, NULL))
	{
		ret = -ENXIO;
		goto failed_nand;
	}
	
	/* second stage of the NAND scan 
	 * this stage requires information regarding ECC and 
         * bad block management. */

	/* Bad block management */
	denali->nand.bbt_td = &bbt_main_descr;
	denali->nand.bbt_md = &bbt_mirror_descr;

	/* skip the scan for now until we have OOB read and write support */
	denali->nand.options |= NAND_USE_FLASH_BBT | NAND_SKIP_BBTSCAN;
	denali->nand.ecc.mode = NAND_ECC_HW_SYNDROME;

	if (denali->dev_info.MLCDevice)
	{
		denali->nand.ecc.layout = &nand_oob_mlc_14bit;
		denali->nand.ecc.bytes = ECC_BYTES_MLC;
	}
	else /* SLC */
	{
		denali->nand.ecc.layout = &nand_oob_slc;
		denali->nand.ecc.bytes = ECC_BYTES_SLC;
	}

	/* These functions are required by the NAND core framework, otherwise, 
           the NAND core will assert. However, we don't need them, so we'll stub 
           them out. */
	denali->nand.ecc.calculate = denali_ecc_calculate;
	denali->nand.ecc.correct = denali_ecc_correct;
	denali->nand.ecc.hwctl = denali_ecc_hwctl;

	/* override the default read operations */
	denali->nand.ecc.size = denali->mtd.writesize;
	denali->nand.ecc.read_page = denali_read_page;
	denali->nand.ecc.read_page_raw = denali_read_page_raw;
	denali->nand.ecc.write_page = denali_write_page;
	denali->nand.ecc.write_page_raw = denali_write_page_raw;
	denali->nand.ecc.read_oob = denali_read_oob;
	denali->nand.ecc.write_oob = denali_write_oob;
	denali->nand.erase_cmd = denali_erase;

	if (nand_scan_tail(&denali->mtd))
	{
		ret = -ENXIO;
		goto failed_nand;
	}

	ret = add_mtd_device(&denali->mtd);
	if (ret) {
		printk(KERN_ERR "Spectra: Failed to register MTD device: %d\n", ret);
		goto failed_nand;
	}
	return 0;

 failed_nand:
	denali_irq_cleanup(dev->irq, denali);
 failed_request_irq:
	iounmap(denali->flash_reg);
	iounmap(denali->flash_mem);
 failed_remap_csr:
	pci_release_regions(dev);
 failed_req_csr:
	pci_unmap_single(dev, denali->buf.dma_buf, DENALI_BUF_SIZE, 
							PCI_DMA_BIDIRECTIONAL);
 failed_enable:
	kfree(denali);
	return ret;
}

/* driver exit point */
static void denali_pci_remove(struct pci_dev *dev)
{
	struct denali_nand_info *denali = pci_get_drvdata(dev);

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	nand_release(&denali->mtd);
	del_mtd_device(&denali->mtd);

	denali_irq_cleanup(dev->irq, denali);

	iounmap(denali->flash_reg);
	iounmap(denali->flash_mem);
	pci_release_regions(dev);
	pci_disable_device(dev);
	pci_unmap_single(dev, denali->buf.dma_buf, DENALI_BUF_SIZE, 
							PCI_DMA_BIDIRECTIONAL);
	pci_set_drvdata(dev, NULL);
	kfree(denali);
}

MODULE_DEVICE_TABLE(pci, denali_pci_ids);

static struct pci_driver denali_pci_driver = {
	.name = DENALI_NAND_NAME,
	.id_table = denali_pci_ids,
	.probe = denali_pci_probe,
	.remove = denali_pci_remove,
};

static int __devinit denali_init(void)
{
	printk(KERN_INFO "Spectra MTD driver built on %s @ %s\n", __DATE__, __TIME__);
	return pci_register_driver(&denali_pci_driver);
}

/* Free memory */
static void __devexit denali_exit(void)
{
	pci_unregister_driver(&denali_pci_driver);
}

module_init(denali_init);
module_exit(denali_exit);
