/*
 * SMI (Serial Memory Controller) device driver for Serial NOR Flash on
 * SPEAr platform
 * The serial nor interface is largely based on drivers/mtd/m25p80.c,
 * however the SPI interface has been replaced by SMI.
 *
 * Copyright © 2010 STMicroelectronics.
 * Ashish Priyadarshi
 * Shiraz Hashim <shiraz.hashim@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spear_smi.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* SMI clock rate */
#define SMI_MAX_CLOCK_FREQ	50000000 /* 50 MHz */

/* MAX time out to safely come out of a erase or write busy conditions */
#define SMI_PROBE_TIMEOUT	(HZ / 10)
#define SMI_MAX_TIME_OUT	(3 * HZ)

/* timeout for command completion */
#define SMI_CMD_TIMEOUT		(HZ / 10)

/* registers of smi */
#define SMI_CR1		0x0	/* SMI control register 1 */
#define SMI_CR2		0x4	/* SMI control register 2 */
#define SMI_SR		0x8	/* SMI status register */
#define SMI_TR		0xC	/* SMI transmit register */
#define SMI_RR		0x10	/* SMI receive register */

/* defines for control_reg 1 */
#define BANK_EN		(0xF << 0)	/* enables all banks */
#define DSEL_TIME	(0x6 << 4)	/* Deselect time 6 + 1 SMI_CK periods */
#define SW_MODE		(0x1 << 28)	/* enables SW Mode */
#define WB_MODE		(0x1 << 29)	/* Write Burst Mode */
#define FAST_MODE	(0x1 << 15)	/* Fast Mode */
#define HOLD1		(0x1 << 16)	/* Clock Hold period selection */

/* defines for control_reg 2 */
#define SEND		(0x1 << 7)	/* Send data */
#define TFIE		(0x1 << 8)	/* Transmission Flag Interrupt Enable */
#define WCIE		(0x1 << 9)	/* Write Complete Interrupt Enable */
#define RD_STATUS_REG	(0x1 << 10)	/* reads status reg */
#define WE		(0x1 << 11)	/* Write Enable */

#define TX_LEN_SHIFT	0
#define RX_LEN_SHIFT	4
#define BANK_SHIFT	12

/* defines for status register */
#define SR_WIP		0x1	/* Write in progress */
#define SR_WEL		0x2	/* Write enable latch */
#define SR_BP0		0x4	/* Block protect 0 */
#define SR_BP1		0x8	/* Block protect 1 */
#define SR_BP2		0x10	/* Block protect 2 */
#define SR_SRWD		0x80	/* SR write protect */
#define TFF		0x100	/* Transfer Finished Flag */
#define WCF		0x200	/* Transfer Finished Flag */
#define ERF1		0x400	/* Forbidden Write Request */
#define ERF2		0x800	/* Forbidden Access */

#define WM_SHIFT	12

/* flash opcodes */
#define OPCODE_RDID	0x9f	/* Read JEDEC ID */

/* Flash Device Ids maintenance section */

/* data structure to maintain flash ids from different vendors */
struct flash_device {
	char *name;
	u8 erase_cmd;
	u32 device_id;
	u32 pagesize;
	unsigned long sectorsize;
	unsigned long size_in_bytes;
};

#define FLASH_ID(n, es, id, psize, ssize, size)	\
{				\
	.name = n,		\
	.erase_cmd = es,	\
	.device_id = id,	\
	.pagesize = psize,	\
	.sectorsize = ssize,	\
	.size_in_bytes = size	\
}

static struct flash_device flash_devices[] = {
	FLASH_ID("st m25p16"     , 0xd8, 0x00152020, 0x100, 0x10000, 0x200000),
	FLASH_ID("st m25p32"     , 0xd8, 0x00162020, 0x100, 0x10000, 0x400000),
	FLASH_ID("st m25p64"     , 0xd8, 0x00172020, 0x100, 0x10000, 0x800000),
	FLASH_ID("st m25p128"    , 0xd8, 0x00182020, 0x100, 0x40000, 0x1000000),
	FLASH_ID("st m25p05"     , 0xd8, 0x00102020, 0x80 , 0x8000 , 0x10000),
	FLASH_ID("st m25p10"     , 0xd8, 0x00112020, 0x80 , 0x8000 , 0x20000),
	FLASH_ID("st m25p20"     , 0xd8, 0x00122020, 0x100, 0x10000, 0x40000),
	FLASH_ID("st m25p40"     , 0xd8, 0x00132020, 0x100, 0x10000, 0x80000),
	FLASH_ID("st m25p80"     , 0xd8, 0x00142020, 0x100, 0x10000, 0x100000),
	FLASH_ID("st m45pe10"    , 0xd8, 0x00114020, 0x100, 0x10000, 0x20000),
	FLASH_ID("st m45pe20"    , 0xd8, 0x00124020, 0x100, 0x10000, 0x40000),
	FLASH_ID("st m45pe40"    , 0xd8, 0x00134020, 0x100, 0x10000, 0x80000),
	FLASH_ID("st m45pe80"    , 0xd8, 0x00144020, 0x100, 0x10000, 0x100000),
	FLASH_ID("sp s25fl004"   , 0xd8, 0x00120201, 0x100, 0x10000, 0x80000),
	FLASH_ID("sp s25fl008"   , 0xd8, 0x00130201, 0x100, 0x10000, 0x100000),
	FLASH_ID("sp s25fl016"   , 0xd8, 0x00140201, 0x100, 0x10000, 0x200000),
	FLASH_ID("sp s25fl032"   , 0xd8, 0x00150201, 0x100, 0x10000, 0x400000),
	FLASH_ID("sp s25fl064"   , 0xd8, 0x00160201, 0x100, 0x10000, 0x800000),
	FLASH_ID("atmel 25f512"  , 0x52, 0x0065001F, 0x80 , 0x8000 , 0x10000),
	FLASH_ID("atmel 25f1024" , 0x52, 0x0060001F, 0x100, 0x8000 , 0x20000),
	FLASH_ID("atmel 25f2048" , 0x52, 0x0063001F, 0x100, 0x10000, 0x40000),
	FLASH_ID("atmel 25f4096" , 0x52, 0x0064001F, 0x100, 0x10000, 0x80000),
	FLASH_ID("atmel 25fs040" , 0xd7, 0x0004661F, 0x100, 0x10000, 0x80000),
	FLASH_ID("mac 25l512"    , 0xd8, 0x001020C2, 0x010, 0x10000, 0x10000),
	FLASH_ID("mac 25l1005"   , 0xd8, 0x001120C2, 0x010, 0x10000, 0x20000),
	FLASH_ID("mac 25l2005"   , 0xd8, 0x001220C2, 0x010, 0x10000, 0x40000),
	FLASH_ID("mac 25l4005"   , 0xd8, 0x001320C2, 0x010, 0x10000, 0x80000),
	FLASH_ID("mac 25l4005a"  , 0xd8, 0x001320C2, 0x010, 0x10000, 0x80000),
	FLASH_ID("mac 25l8005"   , 0xd8, 0x001420C2, 0x010, 0x10000, 0x100000),
	FLASH_ID("mac 25l1605"   , 0xd8, 0x001520C2, 0x100, 0x10000, 0x200000),
	FLASH_ID("mac 25l1605a"  , 0xd8, 0x001520C2, 0x010, 0x10000, 0x200000),
	FLASH_ID("mac 25l3205"   , 0xd8, 0x001620C2, 0x100, 0x10000, 0x400000),
	FLASH_ID("mac 25l3205a"  , 0xd8, 0x001620C2, 0x100, 0x10000, 0x400000),
	FLASH_ID("mac 25l6405"   , 0xd8, 0x001720C2, 0x100, 0x10000, 0x800000),
};

/* Define spear specific structures */

struct spear_snor_flash;

/**
 * struct spear_smi - Structure for SMI Device
 *
 * @clk: functional clock
 * @status: current status register of SMI.
 * @clk_rate: functional clock rate of SMI (default: SMI_MAX_CLOCK_FREQ)
 * @lock: lock to prevent parallel access of SMI.
 * @io_base: base address for registers of SMI.
 * @pdev: platform device
 * @cmd_complete: queue to wait for command completion of NOR-flash.
 * @num_flashes: number of flashes actually present on board.
 * @flash: separate structure for each Serial NOR-flash attached to SMI.
 */
struct spear_smi {
	struct clk *clk;
	u32 status;
	unsigned long clk_rate;
	struct mutex lock;
	void __iomem *io_base;
	struct platform_device *pdev;
	wait_queue_head_t cmd_complete;
	u32 num_flashes;
	struct spear_snor_flash *flash[MAX_NUM_FLASH_CHIP];
};

/**
 * struct spear_snor_flash - Structure for Serial NOR Flash
 *
 * @bank: Bank number(0, 1, 2, 3) for each NOR-flash.
 * @dev_id: Device ID of NOR-flash.
 * @lock: lock to manage flash read, write and erase operations
 * @mtd: MTD info for each NOR-flash.
 * @num_parts: Total number of partition in each bank of NOR-flash.
 * @parts: Partition info for each bank of NOR-flash.
 * @page_size: Page size of NOR-flash.
 * @base_addr: Base address of NOR-flash.
 * @erase_cmd: erase command may vary on different flash types
 * @fast_mode: flash supports read in fast mode
 */
struct spear_snor_flash {
	u32 bank;
	u32 dev_id;
	struct mutex lock;
	struct mtd_info mtd;
	u32 num_parts;
	struct mtd_partition *parts;
	u32 page_size;
	void __iomem *base_addr;
	u8 erase_cmd;
	u8 fast_mode;
};

static inline struct spear_snor_flash *get_flash_data(struct mtd_info *mtd)
{
	return container_of(mtd, struct spear_snor_flash, mtd);
}

/**
 * spear_smi_read_sr - Read status register of flash through SMI
 * @dev: structure of SMI information.
 * @bank: bank to which flash is connected
 *
 * This routine will return the status register of the flash chip present at the
 * given bank.
 */
static int spear_smi_read_sr(struct spear_smi *dev, u32 bank)
{
	int ret;
	u32 ctrlreg1;

	mutex_lock(&dev->lock);
	dev->status = 0; /* Will be set in interrupt handler */

	ctrlreg1 = readl(dev->io_base + SMI_CR1);
	/* program smi in hw mode */
	writel(ctrlreg1 & ~(SW_MODE | WB_MODE), dev->io_base + SMI_CR1);

	/* performing a rsr instruction in hw mode */
	writel((bank << BANK_SHIFT) | RD_STATUS_REG | TFIE,
			dev->io_base + SMI_CR2);

	/* wait for tff */
	ret = wait_event_interruptible_timeout(dev->cmd_complete,
			dev->status & TFF, SMI_CMD_TIMEOUT);

	/* copy dev->status (lower 16 bits) in order to release lock */
	if (ret > 0)
		ret = dev->status & 0xffff;
	else if (ret == 0)
		ret = -ETIMEDOUT;

	/* restore the ctrl regs state */
	writel(ctrlreg1, dev->io_base + SMI_CR1);
	writel(0, dev->io_base + SMI_CR2);
	mutex_unlock(&dev->lock);

	return ret;
}

/**
 * spear_smi_wait_till_ready - wait till flash is ready
 * @dev: structure of SMI information.
 * @bank: flash corresponding to this bank
 * @timeout: timeout for busy wait condition
 *
 * This routine checks for WIP (write in progress) bit in Status register
 * If successful the routine returns 0 else -EBUSY
 */
static int spear_smi_wait_till_ready(struct spear_smi *dev, u32 bank,
		unsigned long timeout)
{
	unsigned long finish;
	int status;

	finish = jiffies + timeout;
	do {
		status = spear_smi_read_sr(dev, bank);
		if (status < 0) {
			if (status == -ETIMEDOUT)
				continue; /* try till finish */
			return status;
		} else if (!(status & SR_WIP)) {
			return 0;
		}

		cond_resched();
	} while (!time_after_eq(jiffies, finish));

	dev_err(&dev->pdev->dev, "smi controller is busy, timeout\n");
	return -EBUSY;
}

/**
 * spear_smi_int_handler - SMI Interrupt Handler.
 * @irq: irq number
 * @dev_id: structure of SMI device, embedded in dev_id.
 *
 * The handler clears all interrupt conditions and records the status in
 * dev->status which is used by the driver later.
 */
static irqreturn_t spear_smi_int_handler(int irq, void *dev_id)
{
	u32 status = 0;
	struct spear_smi *dev = dev_id;

	status = readl(dev->io_base + SMI_SR);

	if (unlikely(!status))
		return IRQ_NONE;

	/* clear all interrupt conditions */
	writel(0, dev->io_base + SMI_SR);

	/* copy the status register in dev->status */
	dev->status |= status;

	/* send the completion */
	wake_up_interruptible(&dev->cmd_complete);

	return IRQ_HANDLED;
}

/**
 * spear_smi_hw_init - initializes the smi controller.
 * @dev: structure of smi device
 *
 * this routine initializes the smi controller wit the default values
 */
static void spear_smi_hw_init(struct spear_smi *dev)
{
	unsigned long rate = 0;
	u32 prescale = 0;
	u32 val;

	rate = clk_get_rate(dev->clk);

	/* functional clock of smi */
	prescale = DIV_ROUND_UP(rate, dev->clk_rate);

	/*
	 * setting the standard values, fast mode, prescaler for
	 * SMI_MAX_CLOCK_FREQ (50MHz) operation and bank enable
	 */
	val = HOLD1 | BANK_EN | DSEL_TIME | (prescale << 8);

	mutex_lock(&dev->lock);
	/* clear all interrupt conditions */
	writel(0, dev->io_base + SMI_SR);

	writel(val, dev->io_base + SMI_CR1);
	mutex_unlock(&dev->lock);
}

/**
 * get_flash_index - match chip id from a flash list.
 * @flash_id: a valid nor flash chip id obtained from board.
 *
 * try to validate the chip id by matching from a list, if not found then simply
 * returns negative. In case of success returns index in to the flash devices
 * array.
 */
static int get_flash_index(u32 flash_id)
{
	int index;

	/* Matches chip-id to entire list of 'serial-nor flash' ids */
	for (index = 0; index < ARRAY_SIZE(flash_devices); index++) {
		if (flash_devices[index].device_id == flash_id)
			return index;
	}

	/* Memory chip is not listed and not supported */
	return -ENODEV;
}

/**
 * spear_smi_write_enable - Enable the flash to do write operation
 * @dev: structure of SMI device
 * @bank: enable write for flash connected to this bank
 *
 * Set write enable latch with Write Enable command.
 * Returns 0 on success.
 */
static int spear_smi_write_enable(struct spear_smi *dev, u32 bank)
{
	int ret;
	u32 ctrlreg1;

	mutex_lock(&dev->lock);
	dev->status = 0; /* Will be set in interrupt handler */

	ctrlreg1 = readl(dev->io_base + SMI_CR1);
	/* program smi in h/w mode */
	writel(ctrlreg1 & ~SW_MODE, dev->io_base + SMI_CR1);

	/* give the flash, write enable command */
	writel((bank << BANK_SHIFT) | WE | TFIE, dev->io_base + SMI_CR2);

	ret = wait_event_interruptible_timeout(dev->cmd_complete,
			dev->status & TFF, SMI_CMD_TIMEOUT);

	/* restore the ctrl regs state */
	writel(ctrlreg1, dev->io_base + SMI_CR1);
	writel(0, dev->io_base + SMI_CR2);

	if (ret == 0) {
		ret = -EIO;
		dev_err(&dev->pdev->dev,
			"smi controller failed on write enable\n");
	} else if (ret > 0) {
		/* check whether write mode status is set for required bank */
		if (dev->status & (1 << (bank + WM_SHIFT)))
			ret = 0;
		else {
			dev_err(&dev->pdev->dev, "couldn't enable write\n");
			ret = -EIO;
		}
	}

	mutex_unlock(&dev->lock);
	return ret;
}

static inline u32
get_sector_erase_cmd(struct spear_snor_flash *flash, u32 offset)
{
	u32 cmd;
	u8 *x = (u8 *)&cmd;

	x[0] = flash->erase_cmd;
	x[1] = offset >> 16;
	x[2] = offset >> 8;
	x[3] = offset;

	return cmd;
}

/**
 * spear_smi_erase_sector - erase one sector of flash
 * @dev: structure of SMI information
 * @command: erase command to be send
 * @bank: bank to which this command needs to be send
 * @bytes: size of command
 *
 * Erase one sector of flash memory at offset ``offset'' which is any
 * address within the sector which should be erased.
 * Returns 0 if successful, non-zero otherwise.
 */
static int spear_smi_erase_sector(struct spear_smi *dev,
		u32 bank, u32 command, u32 bytes)
{
	u32 ctrlreg1 = 0;
	int ret;

	ret = spear_smi_wait_till_ready(dev, bank, SMI_MAX_TIME_OUT);
	if (ret)
		return ret;

	ret = spear_smi_write_enable(dev, bank);
	if (ret)
		return ret;

	mutex_lock(&dev->lock);

	ctrlreg1 = readl(dev->io_base + SMI_CR1);
	writel((ctrlreg1 | SW_MODE) & ~WB_MODE, dev->io_base + SMI_CR1);

	/* send command in sw mode */
	writel(command, dev->io_base + SMI_TR);

	writel((bank << BANK_SHIFT) | SEND | TFIE | (bytes << TX_LEN_SHIFT),
			dev->io_base + SMI_CR2);

	ret = wait_event_interruptible_timeout(dev->cmd_complete,
			dev->status & TFF, SMI_CMD_TIMEOUT);

	if (ret == 0) {
		ret = -EIO;
		dev_err(&dev->pdev->dev, "sector erase failed\n");
	} else if (ret > 0)
		ret = 0; /* success */

	/* restore ctrl regs */
	writel(ctrlreg1, dev->io_base + SMI_CR1);
	writel(0, dev->io_base + SMI_CR2);

	mutex_unlock(&dev->lock);
	return ret;
}

/**
 * spear_mtd_erase - perform flash erase operation as requested by user
 * @mtd: Provides the memory characteristics
 * @e_info: Provides the erase information
 *
 * Erase an address range on the flash chip. The address range may extend
 * one or more erase sectors. Return an error is there is a problem erasing.
 */
static int spear_mtd_erase(struct mtd_info *mtd, struct erase_info *e_info)
{
	struct spear_snor_flash *flash = get_flash_data(mtd);
	struct spear_smi *dev = mtd->priv;
	u32 addr, command, bank;
	int len, ret;

	if (!flash || !dev)
		return -ENODEV;

	bank = flash->bank;
	if (bank > dev->num_flashes - 1) {
		dev_err(&dev->pdev->dev, "Invalid Bank Num");
		return -EINVAL;
	}

	addr = e_info->addr;
	len = e_info->len;

	mutex_lock(&flash->lock);

	/* now erase sectors in loop */
	while (len) {
		command = get_sector_erase_cmd(flash, addr);
		/* preparing the command for flash */
		ret = spear_smi_erase_sector(dev, bank, command, 4);
		if (ret) {
			e_info->state = MTD_ERASE_FAILED;
			mutex_unlock(&flash->lock);
			return ret;
		}
		addr += mtd->erasesize;
		len -= mtd->erasesize;
	}

	mutex_unlock(&flash->lock);
	e_info->state = MTD_ERASE_DONE;
	mtd_erase_callback(e_info);

	return 0;
}

/**
 * spear_mtd_read - performs flash read operation as requested by the user
 * @mtd: MTD information of the memory bank
 * @from: Address from which to start read
 * @len: Number of bytes to be read
 * @retlen: Fills the Number of bytes actually read
 * @buf: Fills this after reading
 *
 * Read an address range from the flash chip. The address range
 * may be any size provided it is within the physical boundaries.
 * Returns 0 on success, non zero otherwise
 */
static int spear_mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u8 *buf)
{
	struct spear_snor_flash *flash = get_flash_data(mtd);
	struct spear_smi *dev = mtd->priv;
	void *src;
	u32 ctrlreg1, val;
	int ret;

	if (!flash || !dev)
		return -ENODEV;

	if (flash->bank > dev->num_flashes - 1) {
		dev_err(&dev->pdev->dev, "Invalid Bank Num");
		return -EINVAL;
	}

	/* select address as per bank number */
	src = flash->base_addr + from;

	mutex_lock(&flash->lock);

	/* wait till previous write/erase is done. */
	ret = spear_smi_wait_till_ready(dev, flash->bank, SMI_MAX_TIME_OUT);
	if (ret) {
		mutex_unlock(&flash->lock);
		return ret;
	}

	mutex_lock(&dev->lock);
	/* put smi in hw mode not wbt mode */
	ctrlreg1 = val = readl(dev->io_base + SMI_CR1);
	val &= ~(SW_MODE | WB_MODE);
	if (flash->fast_mode)
		val |= FAST_MODE;

	writel(val, dev->io_base + SMI_CR1);

	memcpy_fromio(buf, (u8 *)src, len);

	/* restore ctrl reg1 */
	writel(ctrlreg1, dev->io_base + SMI_CR1);
	mutex_unlock(&dev->lock);

	*retlen = len;
	mutex_unlock(&flash->lock);

	return 0;
}

static inline int spear_smi_cpy_toio(struct spear_smi *dev, u32 bank,
		void *dest, const void *src, size_t len)
{
	int ret;
	u32 ctrlreg1;

	/* wait until finished previous write command. */
	ret = spear_smi_wait_till_ready(dev, bank, SMI_MAX_TIME_OUT);
	if (ret)
		return ret;

	/* put smi in write enable */
	ret = spear_smi_write_enable(dev, bank);
	if (ret)
		return ret;

	/* put smi in hw, write burst mode */
	mutex_lock(&dev->lock);

	ctrlreg1 = readl(dev->io_base + SMI_CR1);
	writel((ctrlreg1 | WB_MODE) & ~SW_MODE, dev->io_base + SMI_CR1);

	memcpy_toio(dest, src, len);

	writel(ctrlreg1, dev->io_base + SMI_CR1);

	mutex_unlock(&dev->lock);
	return 0;
}

/**
 * spear_mtd_write - performs write operation as requested by the user.
 * @mtd: MTD information of the memory bank.
 * @to:	Address to write.
 * @len: Number of bytes to be written.
 * @retlen: Number of bytes actually wrote.
 * @buf: Buffer from which the data to be taken.
 *
 * Write an address range to the flash chip. Data must be written in
 * flash_page_size chunks. The address range may be any size provided
 * it is within the physical boundaries.
 * Returns 0 on success, non zero otherwise
 */
static int spear_mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u8 *buf)
{
	struct spear_snor_flash *flash = get_flash_data(mtd);
	struct spear_smi *dev = mtd->priv;
	void *dest;
	u32 page_offset, page_size;
	int ret;

	if (!flash || !dev)
		return -ENODEV;

	if (flash->bank > dev->num_flashes - 1) {
		dev_err(&dev->pdev->dev, "Invalid Bank Num");
		return -EINVAL;
	}

	/* select address as per bank number */
	dest = flash->base_addr + to;
	mutex_lock(&flash->lock);

	page_offset = (u32)to % flash->page_size;

	/* do if all the bytes fit onto one page */
	if (page_offset + len <= flash->page_size) {
		ret = spear_smi_cpy_toio(dev, flash->bank, dest, buf, len);
		if (!ret)
			*retlen += len;
	} else {
		u32 i;

		/* the size of data remaining on the first page */
		page_size = flash->page_size - page_offset;

		ret = spear_smi_cpy_toio(dev, flash->bank, dest, buf,
				page_size);
		if (ret)
			goto err_write;
		else
			*retlen += page_size;

		/* write everything in pagesize chunks */
		for (i = page_size; i < len; i += page_size) {
			page_size = len - i;
			if (page_size > flash->page_size)
				page_size = flash->page_size;

			ret = spear_smi_cpy_toio(dev, flash->bank, dest + i,
					buf + i, page_size);
			if (ret)
				break;
			else
				*retlen += page_size;
		}
	}

err_write:
	mutex_unlock(&flash->lock);

	return ret;
}

/**
 * spear_smi_probe_flash - Detects the NOR Flash chip.
 * @dev: structure of SMI information.
 * @bank: bank on which flash must be probed
 *
 * This routine will check whether there exists a flash chip on a given memory
 * bank ID.
 * Return index of the probed flash in flash devices structure
 */
static int spear_smi_probe_flash(struct spear_smi *dev, u32 bank)
{
	int ret;
	u32 val = 0;

	ret = spear_smi_wait_till_ready(dev, bank, SMI_PROBE_TIMEOUT);
	if (ret)
		return ret;

	mutex_lock(&dev->lock);

	dev->status = 0; /* Will be set in interrupt handler */
	/* put smi in sw mode */
	val = readl(dev->io_base + SMI_CR1);
	writel(val | SW_MODE, dev->io_base + SMI_CR1);

	/* send readid command in sw mode */
	writel(OPCODE_RDID, dev->io_base + SMI_TR);

	val = (bank << BANK_SHIFT) | SEND | (1 << TX_LEN_SHIFT) |
		(3 << RX_LEN_SHIFT) | TFIE;
	writel(val, dev->io_base + SMI_CR2);

	/* wait for TFF */
	ret = wait_event_interruptible_timeout(dev->cmd_complete,
			dev->status & TFF, SMI_CMD_TIMEOUT);
	if (ret <= 0) {
		ret = -ENODEV;
		goto err_probe;
	}

	/* get memory chip id */
	val = readl(dev->io_base + SMI_RR);
	val &= 0x00ffffff;
	ret = get_flash_index(val);

err_probe:
	/* clear sw mode */
	val = readl(dev->io_base + SMI_CR1);
	writel(val & ~SW_MODE, dev->io_base + SMI_CR1);

	mutex_unlock(&dev->lock);
	return ret;
}


#ifdef CONFIG_OF
static int spear_smi_probe_config_dt(struct platform_device *pdev,
				     struct device_node *np)
{
	struct spear_smi_plat_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *pp = NULL;
	const __be32 *addr;
	u32 val;
	int len;
	int i = 0;

	if (!np)
		return -ENODEV;

	of_property_read_u32(np, "clock-rate", &val);
	pdata->clk_rate = val;

	pdata->board_flash_info = devm_kzalloc(&pdev->dev,
					       sizeof(*pdata->board_flash_info),
					       GFP_KERNEL);

	/* Fill structs for each subnode (flash device) */
	while ((pp = of_get_next_child(np, pp))) {
		struct spear_smi_flash_info *flash_info;

		flash_info = &pdata->board_flash_info[i];
		pdata->np[i] = pp;

		/* Read base-addr and size from DT */
		addr = of_get_property(pp, "reg", &len);
		pdata->board_flash_info->mem_base = be32_to_cpup(&addr[0]);
		pdata->board_flash_info->size = be32_to_cpup(&addr[1]);

		if (of_get_property(pp, "st,smi-fast-mode", NULL))
			pdata->board_flash_info->fast_mode = 1;

		i++;
	}

	pdata->num_flashes = i;

	return 0;
}
#else
static int spear_smi_probe_config_dt(struct platform_device *pdev,
				     struct device_node *np)
{
	return -ENOSYS;
}
#endif

static int spear_smi_setup_banks(struct platform_device *pdev,
				 u32 bank, struct device_node *np)
{
	struct spear_smi *dev = platform_get_drvdata(pdev);
	struct mtd_part_parser_data ppdata = {};
	struct spear_smi_flash_info *flash_info;
	struct spear_smi_plat_data *pdata;
	struct spear_snor_flash *flash;
	struct mtd_partition *parts = NULL;
	int count = 0;
	int flash_index;
	int ret = 0;

	pdata = dev_get_platdata(&pdev->dev);
	if (bank > pdata->num_flashes - 1)
		return -EINVAL;

	flash_info = &pdata->board_flash_info[bank];
	if (!flash_info)
		return -ENODEV;

	flash = devm_kzalloc(&pdev->dev, sizeof(*flash), GFP_ATOMIC);
	if (!flash)
		return -ENOMEM;
	flash->bank = bank;
	flash->fast_mode = flash_info->fast_mode ? 1 : 0;
	mutex_init(&flash->lock);

	/* verify whether nor flash is really present on board */
	flash_index = spear_smi_probe_flash(dev, bank);
	if (flash_index < 0) {
		dev_info(&dev->pdev->dev, "smi-nor%d not found\n", bank);
		return flash_index;
	}
	/* map the memory for nor flash chip */
	flash->base_addr = devm_ioremap(&pdev->dev, flash_info->mem_base,
					flash_info->size);
	if (!flash->base_addr)
		return -EIO;

	dev->flash[bank] = flash;
	flash->mtd.priv = dev;

	if (flash_info->name)
		flash->mtd.name = flash_info->name;
	else
		flash->mtd.name = flash_devices[flash_index].name;

	flash->mtd.type = MTD_NORFLASH;
	flash->mtd.writesize = 1;
	flash->mtd.flags = MTD_CAP_NORFLASH;
	flash->mtd.size = flash_info->size;
	flash->mtd.erasesize = flash_devices[flash_index].sectorsize;
	flash->page_size = flash_devices[flash_index].pagesize;
	flash->mtd.writebufsize = flash->page_size;
	flash->erase_cmd = flash_devices[flash_index].erase_cmd;
	flash->mtd._erase = spear_mtd_erase;
	flash->mtd._read = spear_mtd_read;
	flash->mtd._write = spear_mtd_write;
	flash->dev_id = flash_devices[flash_index].device_id;

	dev_info(&dev->pdev->dev, "mtd .name=%s .size=%llx(%lluM)\n",
			flash->mtd.name, flash->mtd.size,
			flash->mtd.size / (1024 * 1024));

	dev_info(&dev->pdev->dev, ".erasesize = 0x%x(%uK)\n",
			flash->mtd.erasesize, flash->mtd.erasesize / 1024);

#ifndef CONFIG_OF
	if (flash_info->partitions) {
		parts = flash_info->partitions;
		count = flash_info->nr_partitions;
	}
#endif
	ppdata.of_node = np;

	ret = mtd_device_parse_register(&flash->mtd, NULL, &ppdata, parts,
					count);
	if (ret) {
		dev_err(&dev->pdev->dev, "Err MTD partition=%d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * spear_smi_probe - Entry routine
 * @pdev: platform device structure
 *
 * This is the first routine which gets invoked during booting and does all
 * initialization/allocation work. The routine looks for available memory banks,
 * and do proper init for any found one.
 * Returns 0 on success, non zero otherwise
 */
static int spear_smi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spear_smi_plat_data *pdata = NULL;
	struct spear_smi *dev;
	struct resource *smi_base;
	int irq, ret = 0;
	int i;

	if (np) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			pr_err("%s: ERROR: no memory", __func__);
			ret = -ENOMEM;
			goto err;
		}
		pdev->dev.platform_data = pdata;
		ret = spear_smi_probe_config_dt(pdev, np);
		if (ret) {
			ret = -ENODEV;
			dev_err(&pdev->dev, "no platform data\n");
			goto err;
		}
	} else {
		pdata = dev_get_platdata(&pdev->dev);
		if (!pdata) {
			ret = -ENODEV;
			dev_err(&pdev->dev, "no platform data\n");
			goto err;
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "invalid smi irq\n");
		goto err;
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_ATOMIC);
	if (!dev) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "mem alloc fail\n");
		goto err;
	}

	smi_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	dev->io_base = devm_ioremap_resource(&pdev->dev, smi_base);
	if (IS_ERR(dev->io_base)) {
		ret = PTR_ERR(dev->io_base);
		goto err;
	}

	dev->pdev = pdev;
	dev->clk_rate = pdata->clk_rate;

	if (dev->clk_rate > SMI_MAX_CLOCK_FREQ)
		dev->clk_rate = SMI_MAX_CLOCK_FREQ;

	dev->num_flashes = pdata->num_flashes;

	if (dev->num_flashes > MAX_NUM_FLASH_CHIP) {
		dev_err(&pdev->dev, "exceeding max number of flashes\n");
		dev->num_flashes = MAX_NUM_FLASH_CHIP;
	}

	dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		ret = PTR_ERR(dev->clk);
		goto err;
	}

	ret = clk_prepare_enable(dev->clk);
	if (ret)
		goto err;

	ret = devm_request_irq(&pdev->dev, irq, spear_smi_int_handler, 0,
			       pdev->name, dev);
	if (ret) {
		dev_err(&dev->pdev->dev, "SMI IRQ allocation failed\n");
		goto err_irq;
	}

	mutex_init(&dev->lock);
	init_waitqueue_head(&dev->cmd_complete);
	spear_smi_hw_init(dev);
	platform_set_drvdata(pdev, dev);

	/* loop for each serial nor-flash which is connected to smi */
	for (i = 0; i < dev->num_flashes; i++) {
		ret = spear_smi_setup_banks(pdev, i, pdata->np[i]);
		if (ret) {
			dev_err(&dev->pdev->dev, "bank setup failed\n");
			goto err_bank_setup;
		}
	}

	return 0;

err_bank_setup:
	platform_set_drvdata(pdev, NULL);
err_irq:
	clk_disable_unprepare(dev->clk);
err:
	return ret;
}

/**
 * spear_smi_remove - Exit routine
 * @pdev: platform device structure
 *
 * free all allocations and delete the partitions.
 */
static int spear_smi_remove(struct platform_device *pdev)
{
	struct spear_smi *dev;
	struct spear_snor_flash *flash;
	int ret, i;

	dev = platform_get_drvdata(pdev);
	if (!dev) {
		dev_err(&pdev->dev, "dev is null\n");
		return -ENODEV;
	}

	/* clean up for all nor flash */
	for (i = 0; i < dev->num_flashes; i++) {
		flash = dev->flash[i];
		if (!flash)
			continue;

		/* clean up mtd stuff */
		ret = mtd_device_unregister(&flash->mtd);
		if (ret)
			dev_err(&pdev->dev, "error removing mtd\n");
	}

	clk_disable_unprepare(dev->clk);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int spear_smi_suspend(struct device *dev)
{
	struct spear_smi *sdev = dev_get_drvdata(dev);

	if (sdev && sdev->clk)
		clk_disable_unprepare(sdev->clk);

	return 0;
}

static int spear_smi_resume(struct device *dev)
{
	struct spear_smi *sdev = dev_get_drvdata(dev);
	int ret = -EPERM;

	if (sdev && sdev->clk)
		ret = clk_prepare_enable(sdev->clk);

	if (!ret)
		spear_smi_hw_init(sdev);
	return ret;
}

static SIMPLE_DEV_PM_OPS(spear_smi_pm_ops, spear_smi_suspend, spear_smi_resume);
#endif

#ifdef CONFIG_OF
static const struct of_device_id spear_smi_id_table[] = {
	{ .compatible = "st,spear600-smi" },
	{}
};
MODULE_DEVICE_TABLE(of, spear_smi_id_table);
#endif

static struct platform_driver spear_smi_driver = {
	.driver = {
		.name = "smi",
		.bus = &platform_bus_type,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(spear_smi_id_table),
#ifdef CONFIG_PM
		.pm = &spear_smi_pm_ops,
#endif
	},
	.probe = spear_smi_probe,
	.remove = spear_smi_remove,
};
module_platform_driver(spear_smi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ashish Priyadarshi, Shiraz Hashim <shiraz.hashim@st.com>");
MODULE_DESCRIPTION("MTD SMI driver for serial nor flash chips");
