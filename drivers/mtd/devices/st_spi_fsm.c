/*
 * st_spi_fsm.c	- ST Fast Sequence Mode (FSM) Serial Flash Controller
 *
 * Author: Angus Clark <angus.clark@st.com>
 *
 * Copyright (C) 2010-2014 STicroelectronics Limited
 *
 * JEDEC probe based on drivers/mtd/devices/m25p80.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>

/*
 * FSM SPI Controller Registers
 */
#define SPI_CLOCKDIV			0x0010
#define SPI_MODESELECT			0x0018
#define SPI_CONFIGDATA			0x0020
#define SPI_STA_MODE_CHANGE		0x0028
#define SPI_FAST_SEQ_TRANSFER_SIZE	0x0100
#define SPI_FAST_SEQ_ADD1		0x0104
#define SPI_FAST_SEQ_ADD2		0x0108
#define SPI_FAST_SEQ_ADD_CFG		0x010c
#define SPI_FAST_SEQ_OPC1		0x0110
#define SPI_FAST_SEQ_OPC2		0x0114
#define SPI_FAST_SEQ_OPC3		0x0118
#define SPI_FAST_SEQ_OPC4		0x011c
#define SPI_FAST_SEQ_OPC5		0x0120
#define SPI_MODE_BITS			0x0124
#define SPI_DUMMY_BITS			0x0128
#define SPI_FAST_SEQ_FLASH_STA_DATA	0x012c
#define SPI_FAST_SEQ_1			0x0130
#define SPI_FAST_SEQ_2			0x0134
#define SPI_FAST_SEQ_3			0x0138
#define SPI_FAST_SEQ_4			0x013c
#define SPI_FAST_SEQ_CFG		0x0140
#define SPI_FAST_SEQ_STA		0x0144
#define SPI_QUAD_BOOT_SEQ_INIT_1	0x0148
#define SPI_QUAD_BOOT_SEQ_INIT_2	0x014c
#define SPI_QUAD_BOOT_READ_SEQ_1	0x0150
#define SPI_QUAD_BOOT_READ_SEQ_2	0x0154
#define SPI_PROGRAM_ERASE_TIME		0x0158
#define SPI_MULT_PAGE_REPEAT_SEQ_1	0x015c
#define SPI_MULT_PAGE_REPEAT_SEQ_2	0x0160
#define SPI_STATUS_WR_TIME_REG		0x0164
#define SPI_FAST_SEQ_DATA_REG		0x0300

/*
 * Register: SPI_MODESELECT
 */
#define SPI_MODESELECT_CONTIG		0x01
#define SPI_MODESELECT_FASTREAD		0x02
#define SPI_MODESELECT_DUALIO		0x04
#define SPI_MODESELECT_FSM		0x08
#define SPI_MODESELECT_QUADBOOT		0x10

/*
 * Register: SPI_CONFIGDATA
 */
#define SPI_CFG_DEVICE_ST		0x1
#define SPI_CFG_DEVICE_ATMEL		0x4
#define SPI_CFG_MIN_CS_HIGH(x)		(((x) & 0xfff) << 4)
#define SPI_CFG_CS_SETUPHOLD(x)		(((x) & 0xff) << 16)
#define SPI_CFG_DATA_HOLD(x)		(((x) & 0xff) << 24)

#define SPI_CFG_DEFAULT_MIN_CS_HIGH    SPI_CFG_MIN_CS_HIGH(0x0AA)
#define SPI_CFG_DEFAULT_CS_SETUPHOLD   SPI_CFG_CS_SETUPHOLD(0xA0)
#define SPI_CFG_DEFAULT_DATA_HOLD      SPI_CFG_DATA_HOLD(0x00)

/*
 * Register: SPI_FAST_SEQ_TRANSFER_SIZE
 */
#define TRANSFER_SIZE(x)		((x) * 8)

/*
 * Register: SPI_FAST_SEQ_ADD_CFG
 */
#define ADR_CFG_CYCLES_ADD1(x)		((x) << 0)
#define ADR_CFG_PADS_1_ADD1		(0x0 << 6)
#define ADR_CFG_PADS_2_ADD1		(0x1 << 6)
#define ADR_CFG_PADS_4_ADD1		(0x3 << 6)
#define ADR_CFG_CSDEASSERT_ADD1		(1   << 8)
#define ADR_CFG_CYCLES_ADD2(x)		((x) << (0+16))
#define ADR_CFG_PADS_1_ADD2		(0x0 << (6+16))
#define ADR_CFG_PADS_2_ADD2		(0x1 << (6+16))
#define ADR_CFG_PADS_4_ADD2		(0x3 << (6+16))
#define ADR_CFG_CSDEASSERT_ADD2		(1   << (8+16))

/*
 * Register: SPI_FAST_SEQ_n
 */
#define SEQ_OPC_OPCODE(x)		((x) << 0)
#define SEQ_OPC_CYCLES(x)		((x) << 8)
#define SEQ_OPC_PADS_1			(0x0 << 14)
#define SEQ_OPC_PADS_2			(0x1 << 14)
#define SEQ_OPC_PADS_4			(0x3 << 14)
#define SEQ_OPC_CSDEASSERT		(1   << 16)

/*
 * Register: SPI_FAST_SEQ_CFG
 */
#define SEQ_CFG_STARTSEQ		(1 << 0)
#define SEQ_CFG_SWRESET			(1 << 5)
#define SEQ_CFG_CSDEASSERT		(1 << 6)
#define SEQ_CFG_READNOTWRITE		(1 << 7)
#define SEQ_CFG_ERASE			(1 << 8)
#define SEQ_CFG_PADS_1			(0x0 << 16)
#define SEQ_CFG_PADS_2			(0x1 << 16)
#define SEQ_CFG_PADS_4			(0x3 << 16)

/*
 * Register: SPI_MODE_BITS
 */
#define MODE_DATA(x)			(x & 0xff)
#define MODE_CYCLES(x)			((x & 0x3f) << 16)
#define MODE_PADS_1			(0x0 << 22)
#define MODE_PADS_2			(0x1 << 22)
#define MODE_PADS_4			(0x3 << 22)
#define DUMMY_CSDEASSERT		(1   << 24)

/*
 * Register: SPI_DUMMY_BITS
 */
#define DUMMY_CYCLES(x)			((x & 0x3f) << 16)
#define DUMMY_PADS_1			(0x0 << 22)
#define DUMMY_PADS_2			(0x1 << 22)
#define DUMMY_PADS_4			(0x3 << 22)
#define DUMMY_CSDEASSERT		(1   << 24)

/*
 * Register: SPI_FAST_SEQ_FLASH_STA_DATA
 */
#define STA_DATA_BYTE1(x)		((x & 0xff) << 0)
#define STA_DATA_BYTE2(x)		((x & 0xff) << 8)
#define STA_PADS_1			(0x0 << 16)
#define STA_PADS_2			(0x1 << 16)
#define STA_PADS_4			(0x3 << 16)
#define STA_CSDEASSERT			(0x1 << 20)
#define STA_RDNOTWR			(0x1 << 21)

/*
 * FSM SPI Instruction Opcodes
 */
#define STFSM_OPC_CMD			0x1
#define STFSM_OPC_ADD			0x2
#define STFSM_OPC_STA			0x3
#define STFSM_OPC_MODE			0x4
#define STFSM_OPC_DUMMY		0x5
#define STFSM_OPC_DATA			0x6
#define STFSM_OPC_WAIT			0x7
#define STFSM_OPC_JUMP			0x8
#define STFSM_OPC_GOTO			0x9
#define STFSM_OPC_STOP			0xF

/*
 * FSM SPI Instructions (== opcode + operand).
 */
#define STFSM_INSTR(cmd, op)		((cmd) | ((op) << 4))

#define STFSM_INST_CMD1			STFSM_INSTR(STFSM_OPC_CMD,	1)
#define STFSM_INST_CMD2			STFSM_INSTR(STFSM_OPC_CMD,	2)
#define STFSM_INST_CMD3			STFSM_INSTR(STFSM_OPC_CMD,	3)
#define STFSM_INST_CMD4			STFSM_INSTR(STFSM_OPC_CMD,	4)
#define STFSM_INST_CMD5			STFSM_INSTR(STFSM_OPC_CMD,	5)
#define STFSM_INST_ADD1			STFSM_INSTR(STFSM_OPC_ADD,	1)
#define STFSM_INST_ADD2			STFSM_INSTR(STFSM_OPC_ADD,	2)

#define STFSM_INST_DATA_WRITE		STFSM_INSTR(STFSM_OPC_DATA,	1)
#define STFSM_INST_DATA_READ		STFSM_INSTR(STFSM_OPC_DATA,	2)

#define STFSM_INST_STA_RD1		STFSM_INSTR(STFSM_OPC_STA,	0x1)
#define STFSM_INST_STA_WR1		STFSM_INSTR(STFSM_OPC_STA,	0x1)
#define STFSM_INST_STA_RD2		STFSM_INSTR(STFSM_OPC_STA,	0x2)
#define STFSM_INST_STA_WR1_2		STFSM_INSTR(STFSM_OPC_STA,	0x3)

#define STFSM_INST_MODE			STFSM_INSTR(STFSM_OPC_MODE,	0)
#define STFSM_INST_DUMMY		STFSM_INSTR(STFSM_OPC_DUMMY,	0)
#define STFSM_INST_WAIT			STFSM_INSTR(STFSM_OPC_WAIT,	0)
#define STFSM_INST_STOP			STFSM_INSTR(STFSM_OPC_STOP,	0)

#define STFSM_DEFAULT_EMI_FREQ 100000000UL                        /* 100 MHz */
#define STFSM_DEFAULT_WR_TIME  (STFSM_DEFAULT_EMI_FREQ * (15/1000)) /* 15ms */

#define STFSM_FLASH_SAFE_FREQ  10000000UL                         /* 10 MHz */

#define STFSM_MAX_WAIT_SEQ_MS  1000     /* FSM execution time */

struct stfsm {
	struct device		*dev;
	void __iomem		*base;
	struct resource		*region;
	struct mtd_info		mtd;
	struct mutex		lock;

	uint32_t                fifo_dir_delay;
};

struct stfsm_seq {
	uint32_t data_size;
	uint32_t addr1;
	uint32_t addr2;
	uint32_t addr_cfg;
	uint32_t seq_opc[5];
	uint32_t mode;
	uint32_t dummy;
	uint32_t status;
	uint8_t  seq[16];
	uint32_t seq_cfg;
} __packed __aligned(4);

static struct stfsm_seq stfsm_seq_read_jedec = {
	.data_size = TRANSFER_SIZE(8),
	.seq_opc[0] = (SEQ_OPC_PADS_1 |
		       SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(FLASH_CMD_RDID)),
	.seq = {
		STFSM_INST_CMD1,
		STFSM_INST_DATA_READ,
		STFSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static inline int stfsm_is_idle(struct stfsm *fsm)
{
	return readl(fsm->base + SPI_FAST_SEQ_STA) & 0x10;
}

static inline uint32_t stfsm_fifo_available(struct stfsm *fsm)
{
	return (readl(fsm->base + SPI_FAST_SEQ_STA) >> 5) & 0x7f;
}

static void stfsm_clear_fifo(struct stfsm *fsm)
{
	uint32_t avail;

	for (;;) {
		avail = stfsm_fifo_available(fsm);
		if (!avail)
			break;

		while (avail) {
			readl(fsm->base + SPI_FAST_SEQ_DATA_REG);
			avail--;
		}
	}
}

static inline void stfsm_load_seq(struct stfsm *fsm,
				  const struct stfsm_seq *seq)
{
	void __iomem *dst = fsm->base + SPI_FAST_SEQ_TRANSFER_SIZE;
	const uint32_t *src = (const uint32_t *)seq;
	int words = sizeof(*seq) / sizeof(*src);

	BUG_ON(!stfsm_is_idle(fsm));

	while (words--) {
		writel(*src, dst);
		src++;
		dst += 4;
	}
}

static void stfsm_wait_seq(struct stfsm *fsm)
{
	unsigned long deadline;
	int timeout = 0;

	deadline = jiffies + msecs_to_jiffies(STFSM_MAX_WAIT_SEQ_MS);

	while (!timeout) {
		if (time_after_eq(jiffies, deadline))
			timeout = 1;

		if (stfsm_is_idle(fsm))
			return;

		cond_resched();
	}

	dev_err(fsm->dev, "timeout on sequence completion\n");
}

static void stfsm_read_fifo(struct stfsm *fsm, uint32_t *buf,
			    const uint32_t size)
{
	uint32_t remaining = size >> 2;
	uint32_t avail;
	uint32_t words;

	dev_dbg(fsm->dev, "Reading %d bytes from FIFO\n", size);

	BUG_ON((((uint32_t)buf) & 0x3) || (size & 0x3));

	while (remaining) {
		for (;;) {
			avail = stfsm_fifo_available(fsm);
			if (avail)
				break;
			udelay(1);
		}
		words = min(avail, remaining);
		remaining -= words;

		readsl(fsm->base + SPI_FAST_SEQ_DATA_REG, buf, words);
		buf += words;
	}
}

static void stfsm_read_jedec(struct stfsm *fsm, uint8_t *const jedec)
{
	const struct stfsm_seq *seq = &stfsm_seq_read_jedec;
	uint32_t tmp[2];

	stfsm_load_seq(fsm, seq);

	stfsm_read_fifo(fsm, tmp, 8);

	memcpy(jedec, tmp, 5);

	stfsm_wait_seq(fsm);
}

static struct flash_info *stfsm_jedec_probe(struct stfsm *fsm)
{
	u16                     ext_jedec;
	u32			jedec;
	u8			id[5];

	stfsm_read_jedec(fsm, id);

	jedec     = id[0] << 16 | id[1] << 8 | id[2];
	/*
	 * JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here. Supporting some chips might require using it.
	 */
	ext_jedec = id[3] << 8  | id[4];

	dev_dbg(fsm->dev, "JEDEC =  0x%08x [%02x %02x %02x %02x %02x]\n",
		jedec, id[0], id[1], id[2], id[3], id[4]);

	return NULL;
}

static int stfsm_set_mode(struct stfsm *fsm, uint32_t mode)
{
	int ret, timeout = 10;

	/* Wait for controller to accept mode change */
	while (--timeout) {
		ret = readl(fsm->base + SPI_STA_MODE_CHANGE);
		if (ret & 0x1)
			break;
		udelay(1);
	}

	if (!timeout)
		return -EBUSY;

	writel(mode, fsm->base + SPI_MODESELECT);

	return 0;
}

static void stfsm_set_freq(struct stfsm *fsm, uint32_t spi_freq)
{
	uint32_t emi_freq;
	uint32_t clk_div;

	/* TODO: Make this dynamic */
	emi_freq = STFSM_DEFAULT_EMI_FREQ;

	/*
	 * Calculate clk_div - values between 2 and 128
	 * Multiple of 2, rounded up
	 */
	clk_div = 2 * DIV_ROUND_UP(emi_freq, 2 * spi_freq);
	if (clk_div < 2)
		clk_div = 2;
	else if (clk_div > 128)
		clk_div = 128;

	/*
	 * Determine a suitable delay for the IP to complete a change of
	 * direction of the FIFO. The required delay is related to the clock
	 * divider used. The following heuristics are based on empirical tests,
	 * using a 100MHz EMI clock.
	 */
	if (clk_div <= 4)
		fsm->fifo_dir_delay = 0;
	else if (clk_div <= 10)
		fsm->fifo_dir_delay = 1;
	else
		fsm->fifo_dir_delay = DIV_ROUND_UP(clk_div, 10);

	dev_dbg(fsm->dev, "emi_clk = %uHZ, spi_freq = %uHZ, clk_div = %u\n",
		emi_freq, spi_freq, clk_div);

	writel(clk_div, fsm->base + SPI_CLOCKDIV);
}

static int stfsm_init(struct stfsm *fsm)
{
	int ret;

	/* Perform a soft reset of the FSM controller */
	writel(SEQ_CFG_SWRESET, fsm->base + SPI_FAST_SEQ_CFG);
	udelay(1);
	writel(0, fsm->base + SPI_FAST_SEQ_CFG);

	/* Set clock to 'safe' frequency initially */
	stfsm_set_freq(fsm, STFSM_FLASH_SAFE_FREQ);

	/* Switch to FSM */
	ret = stfsm_set_mode(fsm, SPI_MODESELECT_FSM);
	if (ret)
		return ret;

	/* Set timing parameters */
	writel(SPI_CFG_DEVICE_ST            |
	       SPI_CFG_DEFAULT_MIN_CS_HIGH  |
	       SPI_CFG_DEFAULT_CS_SETUPHOLD |
	       SPI_CFG_DEFAULT_DATA_HOLD,
	       fsm->base + SPI_CONFIGDATA);
	writel(STFSM_DEFAULT_WR_TIME, fsm->base + SPI_STATUS_WR_TIME_REG);

	/* Clear FIFO, just in case */
	stfsm_clear_fifo(fsm);

	return 0;
}

static int stfsm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct stfsm *fsm;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}

	fsm = devm_kzalloc(&pdev->dev, sizeof(*fsm), GFP_KERNEL);
	if (!fsm)
		return -ENOMEM;

	fsm->dev = &pdev->dev;

	platform_set_drvdata(pdev, fsm);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Resource not found\n");
		return -ENODEV;
	}

	fsm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(fsm->base)) {
		dev_err(&pdev->dev,
			"Failed to reserve memory region %pR\n", res);
		return PTR_ERR(fsm->base);
	}

	mutex_init(&fsm->lock);

	ret = stfsm_init(fsm);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialise FSM Controller\n");
		return ret;
	}

	/* Detect SPI FLASH device */
	stfsm_jedec_probe(fsm);

	fsm->mtd.dev.parent	= &pdev->dev;
	fsm->mtd.type		= MTD_NORFLASH;
	fsm->mtd.writesize	= 4;
	fsm->mtd.writebufsize	= fsm->mtd.writesize;
	fsm->mtd.flags		= MTD_CAP_NORFLASH;

	return mtd_device_parse_register(&fsm->mtd, NULL, NULL, NULL, 0);
}

static int stfsm_remove(struct platform_device *pdev)
{
	struct stfsm *fsm = platform_get_drvdata(pdev);
	int err;

	err = mtd_device_unregister(&fsm->mtd);
	if (err)
		return err;

	return 0;
}

static struct of_device_id stfsm_match[] = {
	{ .compatible = "st,spi-fsm", },
	{},
};
MODULE_DEVICE_TABLE(of, stfsm_match);

static struct platform_driver stfsm_driver = {
	.probe		= stfsm_probe,
	.remove		= stfsm_remove,
	.driver		= {
		.name	= "st-spi-fsm",
		.owner	= THIS_MODULE,
		.of_match_table = stfsm_match,
	},
};
module_platform_driver(stfsm_driver);

MODULE_AUTHOR("Angus Clark <angus.clark@st.com>");
MODULE_DESCRIPTION("ST SPI FSM driver");
MODULE_LICENSE("GPL");
