// SPDX-License-Identifier: GPL-2.0+

/*
 * ASPEED FMC/SPI Memory Controller Driver
 *
 * Copyright (c) 2020, ASPEED Corporation.
 * Copyright (c) 2015-2016, IBM Corporation.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/* ASPEED FMC/SPI memory control register related */
#define OFFSET_CE_TYPE_SETTING		0x00
#define OFFSET_CE_ADDR_MODE_CTRL	0x04
#define OFFSET_INTR_CTRL_STATUS		0x08
#define OFFSET_ADDR_DATA_MASK		0x0c
#define OFFSET_CE0_CTRL_REG		0x10
#define OFFSET_CE0_DECODE_RANGE_REG	0x30
#define OFFSET_HOST_DIRECT_ACCESS_CMD_CTRL4	0x6c
#define OFFSET_HOST_DIRECT_ACCESS_CMD_CTRL2	0x74
#define OFFSET_DMA_CTRL			0x80
#define OFFSET_DMA_FLASH_ADDR_REG	0x84
#define OFFSET_DMA_RAM_ADDR_REG		0x88
#define OFFSET_DMA_LEN_REG		0x8c
#define OFFSET_DMA_CHECKSUM_RESULT	0x90
#define OFFSET_CE0_TIMING_COMPENSATION	0x94

#define CTRL_IO_SINGLE_DATA	0
#define CTRL_IO_DUAL_DATA	BIT(29)
#define CTRL_IO_QUAD_DATA	BIT(30)

#define CTRL_IO_MODE_USER	GENMASK(1, 0)
#define CTRL_IO_MODE_CMD_READ	BIT(0)
#define CTRL_IO_MODE_CMD_WRITE	BIT(1)
#define CTRL_STOP_ACTIVE	BIT(2)

#define CALIBRATION_LEN		0x400
#define SPI_DMA_IRQ_EN		BIT(3)
#define SPI_DAM_REQUEST		BIT(31)
#define SPI_DAM_GRANT		BIT(30)
#define SPI_DMA_CALIB_MODE	BIT(3)
#define SPI_DMA_CALC_CKSUM	BIT(2)
#define SPI_DMA_ENABLE		BIT(0)
#define SPI_DMA_STATUS		BIT(11)
#define DMA_GET_REQ_MAGIC	0xaeed0000
#define DMA_DISCARD_REQ_MAGIC	0xdeea0000
#define FMC_SPI_DMA_BUF_LEN	0x3400

enum aspeed_spi_ctl_reg_value {
	ASPEED_SPI_BASE,
	ASPEED_SPI_READ,
	ASPEED_SPI_WRITE,
	ASPEED_SPI_MAX,
};

#define ASPEED_SPI_MAX_CS 5

/* definition for controller flag */
#define SPI_MODE_USER	0x00000001
#define SPI_FIXED_LOW_W_CLK 0x00000002
#define SPI_DMA_WRITE	0x00000004
#define SPI_DMA_READ	0x00000008

#define MAX_READ_SZ_ONCE	0x3000 /* 12KB */
#define FIXED_REMAPPED_MEM_SZ	0x1000

static spinlock_t g_lock;

struct aspeed_spi_controller;
struct aspeed_spi_chip;

struct aspeed_spi_info {
	uint32_t cmd_io_ctrl_mask;
	uint32_t max_data_bus_width;
	uint32_t min_decode_sz;
	void (*set_4byte)(struct aspeed_spi_controller *ast_ctrl, uint32_t cs);
	int (*calibrate)(struct aspeed_spi_controller *ast_ctrl, uint32_t cs);
	void (*adjust_decode_sz)(uint32_t decode_sz_arr[], int len);
	uint32_t (*segment_start)(struct aspeed_spi_controller *ast_ctrl,
				  uint32_t reg);
	uint32_t (*segment_end)(struct aspeed_spi_controller *ast_ctrl,
				uint32_t reg);
	uint32_t (*segment_reg)(struct aspeed_spi_controller *ast_ctrl,
				uint32_t start, uint32_t end);
	void (*safs_support)(struct aspeed_spi_controller *ast_ctrl,
		enum spi_mem_data_dir dir, uint8_t cmd, uint8_t addr_len,
		uint8_t bus_width);
};

struct aspeed_spi_chip {
	void __iomem *ahb_base;
	void __iomem *ahb_base_phy;
	uint32_t ahb_window_sz;
	uint32_t ctrl_val[ASPEED_SPI_MAX];
	uint32_t max_clk_freq;
};

struct aspeed_spi_controller {
	struct device *dev;
	const struct aspeed_spi_info *info; /* controller info */
	void __iomem *regs; /* controller registers */
	void __iomem *ahb_base;
	uint8_t *op_buf;
	dma_addr_t dma_addr_phy;
	uint32_t ahb_base_phy; /* physical addr of AHB window */
	uint32_t ahb_window_sz; /* AHB window size */
	uint32_t num_cs;
	uint64_t ahb_clk;
	int irq; /* for dma write */
	struct completion dma_done;
	struct aspeed_spi_chip *chips; /* pointers to attached chips */
	uint32_t flag;
	bool disable_calib;
	spinlock_t lock;
};

static uint32_t
aspeed_2600_spi_segment_start(struct aspeed_spi_controller *ast_ctrl,
			      uint32_t reg)
{
	uint32_t start_offset = (reg << 16) & 0x0ff00000;

	return ast_ctrl->ahb_base_phy + start_offset;
}

static uint32_t
aspeed_2600_spi_segment_end(struct aspeed_spi_controller *ast_ctrl,
			    uint32_t reg)
{
	uint32_t end_offset = reg & 0x0ff00000;

	/* no decode range, set to physical ahb base */
	if (end_offset == 0)
		return ast_ctrl->ahb_base_phy;

	return ast_ctrl->ahb_base_phy + end_offset + 0x100000;
}

static uint32_t
aspeed_2600_spi_segment_reg(struct aspeed_spi_controller *ast_ctrl,
			    uint32_t start, uint32_t end)
{
	/* no decode range, assign zero value */
	if (start == end)
		return 0;

	return ((start & 0x0ff00000) >> 16) | ((end - 0x100000) & 0x0ff00000);
}

static void aspeed_spi_chip_set_4byte(struct aspeed_spi_controller *ast_ctrl,
				      uint32_t cs)
{
	uint32_t reg_val;

	reg_val = readl(ast_ctrl->regs + OFFSET_CE_ADDR_MODE_CTRL);
	reg_val |= 0x11 << cs;
	writel(reg_val, ast_ctrl->regs + OFFSET_CE_ADDR_MODE_CTRL);
}

uint32_t aspeed_spi_get_io_mode(uint32_t bus_width)
{
	switch (bus_width) {
	case 1:
		return CTRL_IO_SINGLE_DATA;
	case 2:
		return CTRL_IO_DUAL_DATA;
	case 4:
		return CTRL_IO_QUAD_DATA;
	default:
		return CTRL_IO_SINGLE_DATA;
	}
}

/*
 * Check whether the data is not all 0 or 1 in order to
 * avoid calibriate umount spi-flash.
 */
static bool aspeed_spi_calibriation_enable(const uint8_t *buf, uint32_t sz)
{
	const uint32_t *buf_32 = (const uint32_t *)buf;
	uint32_t i;
	uint32_t valid_count = 0;

	for (i = 0; i < (sz / 4); i++) {
		if (buf_32[i] != 0 && buf_32[i] != 0xffffffff)
			valid_count++;
		if (valid_count > 100)
			return true;
	}

	return false;
}

static uint32_t
aspeed_2600_spi_dma_checksum(struct aspeed_spi_controller *ast_ctrl,
			     uint32_t cs, uint32_t div, uint32_t delay)
{
	uint32_t ctrl_val;
	uint32_t checksum;

	writel(DMA_GET_REQ_MAGIC, ast_ctrl->regs + OFFSET_DMA_CTRL);
	if (readl(ast_ctrl->regs + OFFSET_DMA_CTRL) & SPI_DAM_REQUEST) {
		while (!(readl(ast_ctrl->regs + OFFSET_DMA_CTRL) &
			 SPI_DAM_GRANT))
			;
	}

	writel((uint32_t)ast_ctrl->chips[cs].ahb_base_phy,
	       ast_ctrl->regs + OFFSET_DMA_FLASH_ADDR_REG);
	writel(CALIBRATION_LEN, ast_ctrl->regs + OFFSET_DMA_LEN_REG);

	ctrl_val = SPI_DMA_ENABLE | SPI_DMA_CALC_CKSUM | SPI_DMA_CALIB_MODE |
		   (delay << 8) | ((div & 0xf) << 16);
	writel(ctrl_val, ast_ctrl->regs + OFFSET_DMA_CTRL);
	while (!(readl(ast_ctrl->regs + OFFSET_INTR_CTRL_STATUS) &
		 SPI_DMA_STATUS))
		;

	checksum = readl(ast_ctrl->regs + OFFSET_DMA_CHECKSUM_RESULT);

	writel(0x0, ast_ctrl->regs + OFFSET_DMA_CTRL);
	writel(DMA_DISCARD_REQ_MAGIC, ast_ctrl->regs + OFFSET_DMA_CTRL);

	return checksum;
}

static int get_mid_point_of_longest_one(uint8_t *buf, uint32_t len)
{
	int i;
	int start = 0, mid_point = 0;
	int max_cnt = 0, cnt = 0;

	for (i = 0; i < len; i++) {
		if (buf[i] == 1) {
			cnt++;
		} else {
			cnt = 0;
			start = i;
		}

		if (max_cnt < cnt) {
			max_cnt = cnt;
			mid_point = start + (cnt / 2);
		}
	}

	/*
	 * In order to get a stable SPI read timing,
	 * abandon the result if the length of longest
	 * consecutive good points is too short.
	 */
	if (max_cnt < 4)
		return -1;

	return mid_point;
}

/* Transfer maximum clock frequency to register setting */
static uint32_t
aspeed_2600_spi_clk_basic_setting(struct aspeed_spi_controller *ast_ctrl,
				  uint32_t *max_clk)
{
	struct device *dev = ast_ctrl->dev;
	uint32_t hclk_clk = ast_ctrl->ahb_clk;
	uint32_t hclk_div = 0x400; /* default value */
	uint32_t i = 0, j = 0;
	bool found = false;
	/* HCLK/1 ..	HCLK/16 */
	uint32_t hclk_masks[] = { 15, 7, 14, 6, 13, 5, 12, 4,
				  11, 3, 10, 2, 9,  1, 8,  0 };

	/* FMC/SPIR10[27:24] */
	for (j = 0; j < 0xf; j++) {
		/* FMC/SPIR10[11:8] */
		for (i = 0; i < ARRAY_SIZE(hclk_masks); i++) {
			if (i == 0 && j == 0)
				continue;

			if (hclk_clk / (i + 1 + (j * 16)) <= *max_clk) {
				found = 1;
				*max_clk = hclk_clk / (i + 1 + (j * 16));
				break;
			}
		}

		if (found) {
			hclk_div = ((j << 24) | hclk_masks[i] << 8);
			break;
		}
	}

	dev_dbg(dev, "found: %s, hclk: %d, max_clk: %d\n", found ? "yes" : "no",
		hclk_clk, *max_clk);
	dev_dbg(dev, "base_clk: %d, h_div: %d (mask %x), speed: %d\n", j, i + 1,
		hclk_masks[i], hclk_clk / (i + 1 + j * 16));

	return hclk_div;
}

/*
 * If SPI frequency is too high, timing compensation is needed,
 * otherwise, SPI controller will sample unready data. For AST2600
 * SPI memory controller, only the first four frequency levels
 * (HCLK/2, HCLK/3,..., HCKL/5) may need timing compensation.
 * Here, for each frequency, we will get a sequence of reading
 * result (pass or fail) compared to golden data. Then, getting the
 * middle point of the maximum pass widow. Besides, if the flash's
 * content is too monotonous, the frequency recorded in the device
 * tree will be adopted.
 */
int aspeed_2600_spi_timing_calibration(struct aspeed_spi_controller *ast_ctrl,
				       uint32_t cs)
{
	int ret = 0;
	struct device *dev = ast_ctrl->dev;
	struct aspeed_spi_chip *chip = &ast_ctrl->chips[cs];
	uint32_t max_freq = chip->max_clk_freq;
	/* HCLK/2, ..., HCKL/5 */
	uint32_t hclk_masks[] = { 7, 14, 6, 13 };
	uint8_t *calib_res = NULL;
	uint8_t *check_buf = NULL;
	uint32_t reg_val;
	uint32_t checksum, gold_checksum;
	uint32_t i, hcycle, delay_ns, final_delay = 0;
	uint32_t hclk_div;
	bool pass;
	int calib_point;

	reg_val =
		readl(ast_ctrl->regs + OFFSET_CE0_TIMING_COMPENSATION + cs * 4);
	if (reg_val != 0) {
		dev_dbg(dev, "has executed calibration.\n");
		goto no_calib;
	}

	dev_dbg(dev, "calculate timing compensation :\n");
	/*
	 * use the related low frequency to get check calibration data
	 * and get golden data.
	 */
	reg_val = chip->ctrl_val[ASPEED_SPI_READ] & 0xf0fff0ff;
	writel(reg_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);

	/*
	 * timing calibration should be skipped when
	 * "timing-calibration-disabled" property is configured
	 * in the device tree.
	 */
	if (ast_ctrl->disable_calib)
		goto no_calib;

	check_buf = kzalloc(CALIBRATION_LEN, GFP_KERNEL);
	if (!check_buf)
		return -ENOMEM;

	memcpy_fromio(check_buf, chip->ahb_base, CALIBRATION_LEN);
	if (!aspeed_spi_calibriation_enable(check_buf, CALIBRATION_LEN)) {
		dev_info(dev, "flash data is monotonous, skip calibration.");
		goto no_calib;
	}

	gold_checksum = aspeed_2600_spi_dma_checksum(ast_ctrl, cs, 0, 0);

	/*
	 * allocate a space to record calibration result for
	 * different timing compensation with fixed
	 * HCLK division.
	 */
	calib_res = kzalloc(6 * 17, GFP_KERNEL);
	if (!calib_res) {
		ret = -ENOMEM;
		goto no_calib;
	}

	/* From HCLK/2 to HCLK/5 */
	for (i = 0; i < ARRAY_SIZE(hclk_masks); i++) {
		if (max_freq < (uint32_t)ast_ctrl->ahb_clk / (i + 2)) {
			dev_dbg(dev, "skipping freq %d\n",
				(uint32_t)ast_ctrl->ahb_clk / (i + 2));
			continue;
		}

		max_freq = (uint32_t)ast_ctrl->ahb_clk / (i + 2);

		memset(calib_res, 0x0, 6 * 17);

		for (hcycle = 0; hcycle <= 5; hcycle++) {
			/* increase DI delay by the step of 0.5ns */
			dev_dbg(dev, "Delay Enable : hcycle %x\n", hcycle);
			for (delay_ns = 0; delay_ns <= 0xf; delay_ns++) {
				checksum = aspeed_2600_spi_dma_checksum(
					ast_ctrl, cs, hclk_masks[i],
					BIT(3) | hcycle | (delay_ns << 4));
				pass = (checksum == gold_checksum);
				calib_res[hcycle * 17 + delay_ns] = pass;
				dev_dbg(dev,
					"HCLK/%d, %d HCLK cycle, %d delay_ns : %s\n",
					i + 2, hcycle, delay_ns,
					pass ? "PASS" : "FAIL");
			}
		}

		calib_point = get_mid_point_of_longest_one(calib_res, 6 * 17);
		if (calib_point < 0) {
			dev_info(dev, "cannot get good calibration point.\n");
			continue;
		}

		hcycle = calib_point / 17;
		delay_ns = calib_point % 17;
		dev_dbg(dev, "final hcycle: %d, delay_ns: %d\n", hcycle,
			delay_ns);

		final_delay = (BIT(3) | hcycle | (delay_ns << 4)) << (i * 8);
		writel(final_delay, ast_ctrl->regs +
					    OFFSET_CE0_TIMING_COMPENSATION +
					    cs * 4);
		break;
	}

no_calib:

	hclk_div = aspeed_2600_spi_clk_basic_setting(ast_ctrl, &max_freq);

	/* configure SPI clock frequency */
	reg_val = readl(ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);
	reg_val = (reg_val & 0xf0fff0ff) | hclk_div;
	writel(reg_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);

	/* add clock setting info for CE ctrl setting */
	for (i = 0; i < ASPEED_SPI_MAX; i++)
		chip->ctrl_val[i] = (chip->ctrl_val[i] & 0xf0fff0ff) | hclk_div;

	dev_info(dev, "freq: %dMHz\n", max_freq / 1000000);

	kfree(check_buf);
	kfree(calib_res);

	return ret;
}

/*
 * AST2600 SPI memory controllers support multiple chip selects.
 * The start address of a decode range should be multiple
 * of its related flash size. Namely, the total decoded size
 * from flash 0 to flash N should be multiple of (N + 1) flash size.
 */
void aspeed_2600_adjust_decode_sz(uint32_t decode_sz_arr[], int len)
{
	int cs, j;
	uint32_t sz;

	for (cs = len - 1; cs >= 0; cs--) {
		sz = 0;
		for (j = 0; j < cs; j++)
			sz += decode_sz_arr[j];

		if (sz % decode_sz_arr[cs] != 0)
			decode_sz_arr[0] += (sz % decode_sz_arr[cs]);
	}
}

static int
aspeed_spi_decode_range_config(struct aspeed_spi_controller *ast_ctrl,
			       uint32_t decode_sz_arr[])
{
	struct aspeed_spi_chip *chip = ast_ctrl->chips;
	uint32_t i;
	uint32_t cs;
	uint32_t decode_reg_val;
	uint32_t start_addr_phy, end_addr_phy, pre_end_addr_phy = 0;
	uint32_t total_decode_sz = 0;

	/* decode range sanity */
	for (cs = 0; cs < ast_ctrl->num_cs; cs++) {
		total_decode_sz += decode_sz_arr[cs];
		if (ast_ctrl->ahb_window_sz < total_decode_sz) {
			dev_err(ast_ctrl->dev, "insufficient decode size\n");
			for (i = 0; i <= cs; i++)
				dev_err(ast_ctrl->dev, "cs:%d %x\n", i,
					decode_sz_arr[i]);
			return -ENOSPC;
		}
	}

	for (cs = 0; cs < ast_ctrl->num_cs; cs++) {
		if (chip[cs].ahb_base)
			devm_iounmap(ast_ctrl->dev, chip[cs].ahb_base);
	}

	/* configure each CE's decode range */
	for (cs = 0; cs < ast_ctrl->num_cs; cs++) {
		if (cs == 0)
			start_addr_phy = ast_ctrl->ahb_base_phy;
		else
			start_addr_phy = pre_end_addr_phy;

		if ((ast_ctrl->flag & SPI_DMA_READ) == SPI_DMA_READ ||
			(ast_ctrl->flag & SPI_MODE_USER) == SPI_MODE_USER) {
			/* only small decoded range is needed for DMA and user mode */
			chip[cs].ahb_base = devm_ioremap(ast_ctrl->dev, start_addr_phy,
						 FIXED_REMAPPED_MEM_SZ);
		} else {
			chip[cs].ahb_base = devm_ioremap(ast_ctrl->dev, start_addr_phy,
						 decode_sz_arr[cs]);
		}
		chip[cs].ahb_base_phy = (void __iomem *)start_addr_phy;

		chip[cs].ahb_window_sz = decode_sz_arr[cs];
		end_addr_phy = start_addr_phy + decode_sz_arr[cs];

		decode_reg_val = ast_ctrl->info->segment_reg(
			ast_ctrl, start_addr_phy, end_addr_phy);

		writel(decode_reg_val,
		       ast_ctrl->regs + OFFSET_CE0_DECODE_RANGE_REG + cs * 4);

		pre_end_addr_phy = end_addr_phy;

		dev_dbg(ast_ctrl->dev, "cs: %d, decode_reg: 0x%x\n", cs,
			decode_reg_val);
	}

	return 0;
}

static const struct aspeed_spi_info ast2600_fmc_info = {
	.max_data_bus_width = 4,
	.cmd_io_ctrl_mask = 0xf0ff40c7,
	/* for ast2600, the minimum decode size for each CE is 2MB */
	.min_decode_sz = 0x200000,
	.set_4byte = aspeed_spi_chip_set_4byte,
	.calibrate = aspeed_2600_spi_timing_calibration,
	.adjust_decode_sz = aspeed_2600_adjust_decode_sz,
	.segment_start = aspeed_2600_spi_segment_start,
	.segment_end = aspeed_2600_spi_segment_end,
	.segment_reg = aspeed_2600_spi_segment_reg,
};

void aspeed_2600_spi_fill_safs_cmd(struct aspeed_spi_controller *ast_ctrl,
		enum spi_mem_data_dir dir, uint8_t cmd,
		uint8_t addr_len, uint8_t bus_width)
{
	uint32_t tmp_val;

	if (dir == SPI_MEM_DATA_IN) {
		tmp_val = readl(ast_ctrl->regs + OFFSET_HOST_DIRECT_ACCESS_CMD_CTRL4);
		if (addr_len == 4)
			tmp_val = (tmp_val & 0xffff00ff) | (cmd << 8);
		else
			tmp_val = (tmp_val & 0xffffff00) | cmd;

		tmp_val = (tmp_val & 0x0fffffff) | aspeed_spi_get_io_mode(bus_width);

		writel(tmp_val, ast_ctrl->regs + OFFSET_HOST_DIRECT_ACCESS_CMD_CTRL4);

	} else if (dir == SPI_MEM_DATA_OUT) {
		tmp_val = readl(ast_ctrl->regs + OFFSET_HOST_DIRECT_ACCESS_CMD_CTRL4);
		tmp_val = (tmp_val & 0xf0ffffff) |
				(aspeed_spi_get_io_mode(bus_width) >> 4);

		writel(tmp_val, ast_ctrl->regs + OFFSET_HOST_DIRECT_ACCESS_CMD_CTRL4);

		tmp_val = readl(ast_ctrl->regs + OFFSET_HOST_DIRECT_ACCESS_CMD_CTRL2);
		if (addr_len == 4)
			tmp_val = (tmp_val & 0xffff00ff) | (cmd << 8);
		else
			tmp_val = (tmp_val & 0xffffff00) | cmd;

		writel(tmp_val, ast_ctrl->regs + OFFSET_HOST_DIRECT_ACCESS_CMD_CTRL2);
	}
}

static const struct aspeed_spi_info ast2600_spi_info = {
	.max_data_bus_width = 4,
	.cmd_io_ctrl_mask = 0xf0ff40c7,
	/* for ast2600, the minimum decode size for each CE is 2MB */
	.min_decode_sz = 0x200000,
	.set_4byte = aspeed_spi_chip_set_4byte,
	.calibrate = aspeed_2600_spi_timing_calibration,
	.adjust_decode_sz = aspeed_2600_adjust_decode_sz,
	.segment_start = aspeed_2600_spi_segment_start,
	.segment_end = aspeed_2600_spi_segment_end,
	.segment_reg = aspeed_2600_spi_segment_reg,
	.safs_support = aspeed_2600_spi_fill_safs_cmd,
};

/*
 * If the slave device is SPI NOR flash, there are two types
 * of command mode for ASPEED SPI memory controller used to
 * transfer data. The first one is user mode and the other is
 * command read/write mode. With user mode, SPI NOR flash
 * command, address and data processes are all handled by CPU.
 * But, when address filter is enabled to protect some flash
 * regions from being written, user mode will be disabled.
 * Thus, here, we use command read/write mode to issue SPI
 * operations. After remapping flash space correctly, we can
 * easily read/write data to flash by reading or writing
 * related remapped address, then, SPI NOR flash command and
 * address will be transferred to flash by controller
 * automatically. Besides, ASPEED SPI memory controller can
 * also block address or data bytes by configure FMC0C/SPIR0C
 * address and data mask register in order to satisfy the
 * following SPI flash operation sequences: (command) only,
 * (command and address) only or (coommand and data) only.
 */
static int aspeed_spi_exec_op_cmd_mode(
	struct spi_mem *mem,
	const struct spi_mem_op *op)
{
	struct aspeed_spi_controller *ast_ctrl =
		spi_controller_get_devdata(mem->spi->master);
	struct device *dev = ast_ctrl->dev;
	uint32_t cs = mem->spi->chip_select;
	struct aspeed_spi_chip *chip = &ast_ctrl->chips[cs];
	uint32_t ctrl_val;
	uint32_t addr_mode_reg, addr_mode_reg_backup;
	uint32_t addr_data_mask = 0;
	void __iomem *op_addr;
	const void *data_buf;
	uint32_t data_byte = 0;
	uint32_t dummy_data = 0;
	unsigned long flags;

	dev_dbg(dev, "cmd:%x(%d),addr:%llx(%d),dummy:%d(%d),data_len:%x(%d)\n",
		op->cmd.opcode, op->cmd.buswidth, op->addr.val,
		op->addr.buswidth, op->dummy.nbytes, op->dummy.buswidth,
		op->data.nbytes, op->data.buswidth);

	addr_mode_reg = addr_mode_reg_backup =
		readl(ast_ctrl->regs + OFFSET_CE_ADDR_MODE_CTRL);
	addr_data_mask = readl(ast_ctrl->regs + OFFSET_ADDR_DATA_MASK);

	ctrl_val = chip->ctrl_val[ASPEED_SPI_BASE];
	ctrl_val &= ~ast_ctrl->info->cmd_io_ctrl_mask;

	/* configure opcode */
	ctrl_val |= op->cmd.opcode << 16;

	/* configure operation address, address length and address mask */
	if (op->addr.nbytes != 0) {
		if (op->addr.nbytes == 3)
			addr_mode_reg &= ~(0x11 << cs);
		else
			addr_mode_reg |= (0x11 << cs);

		addr_data_mask &= 0x0f;
		op_addr = chip->ahb_base + op->addr.val;
	} else {
		addr_data_mask |= 0xf0;
		op_addr = chip->ahb_base;
	}

	if (op->dummy.nbytes != 0) {
		ctrl_val |= ((op->dummy.nbytes & 0x3) << 6 |
			     (op->dummy.nbytes & 0x4) << 14);
	}

	/* configure data io mode and data mask */
	if (op->data.nbytes != 0) {
		addr_data_mask &= 0xF0;
		data_byte = op->data.nbytes;
		if (op->data.dir == SPI_MEM_DATA_OUT) {
			if (data_byte % 4 != 0) {
				memset(ast_ctrl->op_buf, 0xff, ((data_byte / 4) + 1) * 4);
				memcpy(ast_ctrl->op_buf, op->data.buf.out, data_byte);
				data_buf = ast_ctrl->op_buf;
				data_byte = ((data_byte / 4) + 1) * 4;
			} else {
				data_buf = op->data.buf.out;
			}
		} else {
			data_buf = op->data.buf.in;
		}

		if (op->data.buswidth)
			ctrl_val |= aspeed_spi_get_io_mode(op->data.buswidth);

	} else {
		addr_data_mask |= 0x0f;
		data_byte = 1;
		data_buf = &dummy_data;
	}

	/* configure command mode */
	if (op->data.dir == SPI_MEM_DATA_OUT)
		ctrl_val |= CTRL_IO_MODE_CMD_WRITE;
	else
		ctrl_val |= CTRL_IO_MODE_CMD_READ;

	/* set controller registers */
	writel(ctrl_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);
	writel(addr_mode_reg, ast_ctrl->regs + OFFSET_CE_ADDR_MODE_CTRL);
	writel(addr_data_mask, ast_ctrl->regs + OFFSET_ADDR_DATA_MASK);

	dev_dbg(dev, "ctrl: 0x%08x, addr_mode: 0x%x, mask: 0x%x, addr:0x%08x\n",
		ctrl_val, addr_mode_reg, addr_data_mask, (uint32_t)op_addr);

	/* trigger spi transmission or reception sequence */
	spin_lock_irqsave(&g_lock, flags);

	if (op->data.dir == SPI_MEM_DATA_OUT)
		memcpy_toio(op_addr, data_buf, data_byte);
	else
		memcpy_fromio((void *)data_buf, op_addr, data_byte);

	spin_unlock_irqrestore(&g_lock, flags);

	/* restore controller setting */
	writel(chip->ctrl_val[ASPEED_SPI_READ],
	       ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);
	writel(addr_mode_reg_backup, ast_ctrl->regs + OFFSET_CE_ADDR_MODE_CTRL);
	writel(0x0, ast_ctrl->regs + OFFSET_ADDR_DATA_MASK);

	return 0;
}


static int aspeed_spi_read_from_ahb(void *buf, void __iomem *src, size_t len)
{
	size_t offset = 0;

	if (IS_ALIGNED((uintptr_t)src, sizeof(uintptr_t)) &&
	    IS_ALIGNED((uintptr_t)buf, sizeof(uintptr_t))) {
		ioread32_rep(src, buf, len >> 2);
		offset = len & ~0x3;
		len -= offset;
	}

	ioread8_rep(src, (uint8_t *)buf + offset, len);

	return 0;
}

static int aspeed_spi_write_to_ahb(void __iomem *dst, const void *buf,
				   size_t len)
{
	size_t offset = 0;

	if (IS_ALIGNED((uintptr_t)dst, sizeof(uintptr_t)) &&
	    IS_ALIGNED((uintptr_t)buf, sizeof(uintptr_t))) {
		iowrite32_rep(dst, buf, len >> 2);
		offset = len & ~0x3;
		len -= offset;
	}

	iowrite8_rep(dst, (const uint8_t *)buf + offset, len);

	return 0;
}

static int aspeed_spi_exec_op_user_mode(
	struct spi_mem *mem,
	const struct spi_mem_op *op)
{
	struct aspeed_spi_controller *ast_ctrl =
		spi_controller_get_devdata(mem->spi->master);
	struct device *dev = ast_ctrl->dev;
	uint32_t cs = mem->spi->chip_select;
	struct aspeed_spi_chip *chip = &ast_ctrl->chips[cs];
	uint32_t ctrl_val;
	uint8_t dummy_data[16] = {0};
	uint8_t addr[4] = {0};
	int i;

	dev_dbg(dev, "cmd:%x(%d),addr:%llx(%d),dummy:%d(%d),data_len:0x%x(%d)\n",
		op->cmd.opcode, op->cmd.buswidth, op->addr.val,
		op->addr.buswidth, op->dummy.nbytes, op->dummy.buswidth,
		op->data.nbytes, op->data.buswidth);

	/* start user mode */
	ctrl_val = chip->ctrl_val[ASPEED_SPI_BASE];
	writel(ctrl_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);
	ctrl_val &= (~CTRL_STOP_ACTIVE);
	writel(ctrl_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);

	/* send command */
	aspeed_spi_write_to_ahb(chip->ahb_base, &op->cmd.opcode, 1);

	/* send address */
	for (i = op->addr.nbytes; i > 0; i--) {
		addr[op->addr.nbytes - i] =
			((uint32_t)op->addr.val >> ((i - 1) * 8)) & 0xff;
	}
	aspeed_spi_write_to_ahb(chip->ahb_base, addr, op->addr.nbytes);

	/* send dummy cycle */
	aspeed_spi_write_to_ahb(chip->ahb_base, dummy_data, op->dummy.nbytes);

	/* change io_mode */
	ctrl_val |= aspeed_spi_get_io_mode(op->data.buswidth);
	writel(ctrl_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);

	/* send data */
	if (op->data.dir == SPI_MEM_DATA_OUT)
		aspeed_spi_write_to_ahb(chip->ahb_base, op->data.buf.out, op->data.nbytes);
	else
		aspeed_spi_read_from_ahb(op->data.buf.in, chip->ahb_base, op->data.nbytes);

	ctrl_val |= CTRL_STOP_ACTIVE;
	writel(ctrl_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);

	/* restore controller setting */
	writel(chip->ctrl_val[ASPEED_SPI_READ],
	       ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);

	return 0;
}

static ssize_t aspeed_spi_dirmap_cmd_read(struct spi_mem_dirmap_desc *desc,
				  uint64_t offs, size_t len, void *buf)
{
	struct aspeed_spi_controller *ast_ctrl =
		spi_controller_get_devdata(desc->mem->spi->master);
	struct aspeed_spi_chip *chip =
		&ast_ctrl->chips[desc->mem->spi->chip_select];
	struct spi_mem_op op_tmpl = desc->info.op_tmpl;

	if (chip->ahb_window_sz < offs + len) {
		dev_info(ast_ctrl->dev,
			 "read range exceeds flash remapping size\n");
		return 0;
	}

	dev_dbg(ast_ctrl->dev, "read op:0x%x, addr:0x%llx, len:0x%x\n",
		op_tmpl.cmd.opcode, offs, len);

	memcpy_fromio(buf, chip->ahb_base + offs, len);

	return len;
}

/*
 * When DMA memory mode is enabled, there is a limitation for AST2600,
 * both DMA source and destination address should be 4-byte aligned.
 * Thus, a 4-byte aligned buffer should be allocated previously and
 * CPU needs to copy data from it after DMA done.
 */
#if 0
static ssize_t aspeed_spi_dirmap_dma_read(struct spi_mem_dirmap_desc *desc,
				  uint64_t offs, size_t len, void *buf)
{
	int ret = 0;
	uint32_t timeout = 0;
	struct aspeed_spi_controller *ast_ctrl =
		spi_controller_get_devdata(desc->mem->spi->master);
	struct aspeed_spi_chip *chip =
		&ast_ctrl->chips[desc->mem->spi->chip_select];
	struct spi_mem_op op_tmpl = desc->info.op_tmpl;
	struct device *dev = ast_ctrl->dev;
	uint32_t reg_val;
	uint32_t target_cs = desc->mem->spi->chip_select;
	uint32_t extra;
	uint32_t tb_read_len = len;
	uint32_t read_len;
	uint32_t buf_offs = 0;
	uint32_t flash_offs = (uint32_t)offs;

	if (chip->ahb_window_sz < offs + len) {
		dev_info(ast_ctrl->dev,
			 "read range exceeds flash remapping size\n");
		return 0;
	}

	dev_dbg(ast_ctrl->dev, "read op:0x%x, addr:0x%llx, len:0x%x\n",
		op_tmpl.cmd.opcode, offs, len);

	while (tb_read_len > 0) {
		/* read max 10KB bytes once */
		read_len = MAX_READ_SZ_ONCE - (flash_offs % MAX_READ_SZ_ONCE);
		if (tb_read_len < read_len)
			read_len = tb_read_len;

		/* For AST2600 SPI DMA, flash offset should be 4 byte aligned */
		extra = flash_offs % 4;
		if (extra != 0) {
			flash_offs = (flash_offs / 4) * 4;
			read_len += extra;
		}

		writel(DMA_GET_REQ_MAGIC, ast_ctrl->regs + OFFSET_DMA_CTRL);
		if (readl(ast_ctrl->regs + OFFSET_DMA_CTRL) & SPI_DAM_REQUEST) {
			while (!(readl(ast_ctrl->regs + OFFSET_DMA_CTRL) &
				 SPI_DAM_GRANT))
				;
		}

		reg_val = ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_READ];
		writel(reg_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + target_cs * 4);

		/* don't use dma_map_single here, since we cannot make sure the buf's
		 * start address is 4-byte-aligned.
		 */
		writel(0x0, ast_ctrl->regs + OFFSET_DMA_CTRL);
		writel(ast_ctrl->dma_addr_phy, ast_ctrl->regs + OFFSET_DMA_RAM_ADDR_REG);
		writel(chip->ahb_base_phy + flash_offs, ast_ctrl->regs + OFFSET_DMA_FLASH_ADDR_REG);
		writel(read_len - 1, ast_ctrl->regs + OFFSET_DMA_LEN_REG);

		/* enable DMA irq */
		reg_val = readl(ast_ctrl->regs + OFFSET_INTR_CTRL_STATUS);
		reg_val |= SPI_DMA_IRQ_EN;
		writel(reg_val, ast_ctrl->regs + OFFSET_INTR_CTRL_STATUS);

		reinit_completion(&ast_ctrl->dma_done);

		/* enable read DMA */
		writel(0x1, ast_ctrl->regs + OFFSET_DMA_CTRL);
		timeout = wait_for_completion_timeout(&ast_ctrl->dma_done, msecs_to_jiffies(2000));
		if (timeout == 0) {
			writel(0x0, ast_ctrl->regs + OFFSET_DMA_CTRL);
			writel(DMA_DISCARD_REQ_MAGIC, ast_ctrl->regs + OFFSET_DMA_CTRL);
			dev_err(dev, "read data timeout %d\n", ret);
			ret = -1;
			goto end;
		} else {
			memcpy(buf + buf_offs, ast_ctrl->op_buf + extra, read_len - extra);
		}

		read_len -= extra;

		buf_offs += read_len;
		flash_offs += read_len;
		tb_read_len -= read_len;
	}

end:
	return ret ? 0 : len;
}

static ssize_t aspeed_spi_dirmap_cmd_write(struct spi_mem_dirmap_desc *desc,
				   uint64_t offs, size_t len, const void *buf)
{
	struct aspeed_spi_controller *ast_ctrl =
		spi_controller_get_devdata(desc->mem->spi->master);
	struct aspeed_spi_chip *chip =
		&ast_ctrl->chips[desc->mem->spi->chip_select];
	uint32_t reg_val;
	uint32_t target_cs = desc->mem->spi->chip_select;
	struct spi_mem_op op_tmpl = desc->info.op_tmpl;
	unsigned long flags;

	if (chip->ahb_window_sz < offs + len) {
		dev_info(ast_ctrl->dev,
			 "write range exceeds flash remapping size\n");
		return 0;
	}

	dev_dbg(ast_ctrl->dev, "write op:0x%x, addr:0x%llx, len:0x%x\n",
		op_tmpl.cmd.opcode, offs, len);

	reg_val = ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_WRITE];
	writel(reg_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + target_cs * 4);

	/* Due to spi-flash's characteristic, write process couldn't be
	 * interrupted. Otherwise, CS will be inactive and remain data
	 * cannot be written into flash successfully even if CS is
	 * active again.
	 */
	spin_lock_irqsave(&ast_ctrl->lock, flags);
	memcpy_toio(chip->ahb_base + offs, buf, len);
	spin_unlock_irqrestore(&ast_ctrl->lock, flags);

	reg_val = ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_READ];
	writel(reg_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + target_cs * 4);

	return len;
}
#endif

static ssize_t aspeed_spi_dirmap_dma_write(struct spi_mem_dirmap_desc *desc,
				   uint64_t offs, size_t len, const void *buf)
{
	int ret = 0;
	uint32_t timeout = 0;
	struct aspeed_spi_controller *ast_ctrl =
		spi_controller_get_devdata(desc->mem->spi->master);
	struct device *dev = ast_ctrl->dev;
	struct aspeed_spi_chip *chip =
		&ast_ctrl->chips[desc->mem->spi->chip_select];
	uint32_t reg_val;
	uint32_t target_cs = desc->mem->spi->chip_select;
	struct spi_mem_op op_tmpl = desc->info.op_tmpl;

	if (chip->ahb_window_sz < offs + len) {
		dev_info(dev, "write range exceeds flash remapping size\n");
		return 0;
	}

	if (len < 1)
		return 0;

	if (len > FMC_SPI_DMA_BUF_LEN) {
		dev_info(dev,
			 "written length exceeds expected value (0x%x)\n", len);
		return 0;
	}

	dev_dbg(dev, "write op:0x%x, addr:0x%llx, len:0x%x\n",
		op_tmpl.cmd.opcode, offs, len);

	writel(DMA_GET_REQ_MAGIC, ast_ctrl->regs + OFFSET_DMA_CTRL);
	if (readl(ast_ctrl->regs + OFFSET_DMA_CTRL) & SPI_DAM_REQUEST) {
		while (!(readl(ast_ctrl->regs + OFFSET_DMA_CTRL) &
			 SPI_DAM_GRANT))
			;
	}

	reg_val = ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_WRITE];
	writel(reg_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + target_cs * 4);

	/* don't use dma_map_single here, since we cannot make sure the buf's
	 * start address is 4-byte-aligned.
	 */
	memcpy(ast_ctrl->op_buf, buf, len);

	writel(0x0, ast_ctrl->regs + OFFSET_DMA_CTRL);
	writel(ast_ctrl->dma_addr_phy, ast_ctrl->regs + OFFSET_DMA_RAM_ADDR_REG);
	writel(chip->ahb_base_phy + offs, ast_ctrl->regs + OFFSET_DMA_FLASH_ADDR_REG);
	writel(len - 1, ast_ctrl->regs + OFFSET_DMA_LEN_REG);

	/* enable DMA irq */
	reg_val = readl(ast_ctrl->regs + OFFSET_INTR_CTRL_STATUS);
	reg_val |= SPI_DMA_IRQ_EN;
	writel(reg_val, ast_ctrl->regs + OFFSET_INTR_CTRL_STATUS);

	reinit_completion(&ast_ctrl->dma_done);

	/* enable write DMA */
	writel(0x3, ast_ctrl->regs + OFFSET_DMA_CTRL);
	timeout = wait_for_completion_timeout(&ast_ctrl->dma_done, msecs_to_jiffies(2000));
	if (timeout == 0) {
		writel(0x0, ast_ctrl->regs + OFFSET_DMA_CTRL);
		writel(DMA_DISCARD_REQ_MAGIC, ast_ctrl->regs + OFFSET_DMA_CTRL);
		dev_err(dev, "write data timeout %d\n", ret);
		ret = -1;
	}

	reg_val = ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_READ];
	writel(reg_val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + target_cs * 4);

	return ret ? 0 : len;
}

static irqreturn_t aspeed_spi_dma_isr(int irq, void *dev_id)
{
	struct aspeed_spi_controller *ast_ctrl =
		(struct aspeed_spi_controller *)dev_id;
	uint32_t reg_val;

	if (!(readl(ast_ctrl->regs + OFFSET_INTR_CTRL_STATUS) & 0x800))
		return IRQ_NONE;

	reg_val = readl(ast_ctrl->regs + OFFSET_INTR_CTRL_STATUS);
	reg_val &= ~SPI_DMA_IRQ_EN;
	writel(reg_val, ast_ctrl->regs + OFFSET_INTR_CTRL_STATUS);

	writel(0x0, ast_ctrl->regs + OFFSET_DMA_CTRL);
	writel(DMA_DISCARD_REQ_MAGIC, ast_ctrl->regs + OFFSET_DMA_CTRL);

	complete(&ast_ctrl->dma_done);

	return IRQ_HANDLED;
}

static int aspeed_spi_dirmap_create(struct spi_mem_dirmap_desc *desc)
{
	int ret = 0;
	struct aspeed_spi_controller *ast_ctrl =
		spi_controller_get_devdata(desc->mem->spi->master);
	struct device *dev = ast_ctrl->dev;
	const struct aspeed_spi_info *info = ast_ctrl->info;
	struct spi_mem_op op_tmpl = desc->info.op_tmpl;
	uint32_t decode_sz_arr[5];
	uint32_t cs, target_cs = desc->mem->spi->chip_select;
	uint32_t reg_val;

	if (desc->info.op_tmpl.data.dir == SPI_MEM_DATA_IN) {
		/* record original decode size */
		for (cs = 0; cs < ast_ctrl->num_cs; cs++) {
			reg_val = readl(ast_ctrl->regs +
					OFFSET_CE0_DECODE_RANGE_REG + cs * 4);
			decode_sz_arr[cs] =
				info->segment_end(ast_ctrl, reg_val) -
				info->segment_start(ast_ctrl, reg_val);
		}

		decode_sz_arr[target_cs] = desc->info.length;

		if (info->adjust_decode_sz)
			info->adjust_decode_sz(decode_sz_arr, ast_ctrl->num_cs);

		for (cs = 0; cs < ast_ctrl->num_cs; cs++) {
			dev_dbg(dev, "cs: %d, sz: 0x%x\n", cs,
				decode_sz_arr[cs]);
		}

		ret = aspeed_spi_decode_range_config(ast_ctrl, decode_sz_arr);
		if (ret)
			return ret;

		reg_val = readl(ast_ctrl->regs + OFFSET_CE0_CTRL_REG +
				target_cs * 4) &
			  (~info->cmd_io_ctrl_mask);
		reg_val |= aspeed_spi_get_io_mode(op_tmpl.data.buswidth) |
			   op_tmpl.cmd.opcode << 16 |
			   ((op_tmpl.dummy.nbytes) & 0x3) << 6 |
			   ((op_tmpl.dummy.nbytes) & 0x4) << 14 |
			   CTRL_IO_MODE_CMD_READ;

		writel(reg_val,
		       ast_ctrl->regs + OFFSET_CE0_CTRL_REG + target_cs * 4);
		ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_READ] = reg_val;
		ast_ctrl->chips[target_cs].max_clk_freq =
			desc->mem->spi->max_speed_hz;

		ast_ctrl->disable_calib = false;
		if (!of_property_read_bool(ast_ctrl->dev->of_node,
			"timing-calibration-disabled")) {
			ast_ctrl->disable_calib = true;
		}

		ret = info->calibrate(ast_ctrl, target_cs);

		dev_info(dev, "read bus width: %d [0x%08x]\n",
			 op_tmpl.data.buswidth,
			 ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_READ]);

	} else if (desc->info.op_tmpl.data.dir == SPI_MEM_DATA_OUT) {
		reg_val = readl(ast_ctrl->regs + OFFSET_CE0_CTRL_REG +
					target_cs * 4) & (~info->cmd_io_ctrl_mask);

		if ((ast_ctrl->flag & SPI_FIXED_LOW_W_CLK) == SPI_FIXED_LOW_W_CLK) {
			/* adjust spi clk for write */
			reg_val = (reg_val & (~0x0f000f00)) | 0x03000000;
		}

		reg_val |= aspeed_spi_get_io_mode(op_tmpl.data.buswidth) |
			   op_tmpl.cmd.opcode << 16 | CTRL_IO_MODE_CMD_WRITE;

		ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_WRITE] = reg_val;

		dev_info(dev, "write bus width: %d [0x%08x]\n",
			 op_tmpl.data.buswidth,
			 ast_ctrl->chips[target_cs].ctrl_val[ASPEED_SPI_WRITE]);
	}


	if (info->safs_support) {
		info->safs_support(ast_ctrl, desc->info.op_tmpl.data.dir,
			op_tmpl.cmd.opcode, op_tmpl.addr.nbytes, op_tmpl.data.buswidth);
	}

	if (desc->info.op_tmpl.data.dir == SPI_MEM_DATA_OUT &&
		desc->mem->spi->controller->mem_ops->dirmap_write == NULL)
		return -EINVAL;

	if (desc->info.op_tmpl.data.dir == SPI_MEM_DATA_IN &&
		desc->mem->spi->controller->mem_ops->dirmap_read == NULL)
		return -EINVAL;

	return ret;
}

static const char *aspeed_spi_get_name(struct spi_mem *mem)
{
	struct device *dev = &mem->spi->master->dev;
	const char *name;

	name = devm_kasprintf(dev, GFP_KERNEL, "%s-%d", dev_name(dev),
			      mem->spi->chip_select);

	if (!name) {
		dev_err(dev, "cannot get spi name\n");
		return ERR_PTR(-ENOMEM);
	}

	return name;
}

/*
 * Currently, only support 1-1-1, 1-1-2 or 1-1-4
 * SPI NOR flash operation format.
 */
static bool aspeed_spi_support_op(struct spi_mem *mem,
				  const struct spi_mem_op *op)
{
	struct aspeed_spi_controller *ast_ctrl =
		spi_controller_get_devdata(mem->spi->master);

	if (op->cmd.buswidth > 1)
		return false;

	if (op->addr.nbytes != 0) {
		if (op->addr.buswidth > 1 || op->addr.nbytes > 4)
			return false;
	}

	if (op->dummy.nbytes != 0) {
		if (op->dummy.buswidth > 1 || op->dummy.nbytes > 7)
			return false;
	}

	if (op->data.nbytes != 0 &&
	    ast_ctrl->info->max_data_bus_width < op->data.buswidth)
		return false;

	if (!spi_mem_default_supports_op(mem, op))
		return false;

	if (op->addr.nbytes == 4)
		ast_ctrl->info->set_4byte(ast_ctrl, mem->spi->chip_select);

	return true;
}

/* AST2600-A3 */
static const struct spi_controller_mem_ops aspeed_spi_ops_user_read_write = {
	.exec_op = aspeed_spi_exec_op_user_mode,
	.get_name = aspeed_spi_get_name,
	.supports_op = aspeed_spi_support_op,
	.dirmap_create = aspeed_spi_dirmap_create,
};

/* If CRTM feature is enabled on AST2600-A3, please use the following settings.
 * static const struct spi_controller_mem_ops aspeed_spi_ops_user_read_write = {
 *	.exec_op = aspeed_spi_exec_op_cmd_mode,
 *	.get_name = aspeed_spi_get_name,
 *	.supports_op = aspeed_spi_support_op,
 *	.dirmap_create = aspeed_spi_dirmap_create,
 *	.dirmap_read = aspeed_spi_dirmap_cmd_read,
 * };
 */

/* AST2600-A1/A2 */
static const struct spi_controller_mem_ops aspeed_spi_ops_cmd_read_dma_write = {
	.exec_op = aspeed_spi_exec_op_cmd_mode,
	.get_name = aspeed_spi_get_name,
	.supports_op = aspeed_spi_support_op,
	.dirmap_create = aspeed_spi_dirmap_create,
	.dirmap_read = aspeed_spi_dirmap_cmd_read,
	.dirmap_write = aspeed_spi_dirmap_dma_write,
};

/*
 * Initialize SPI controller for each chip select.
 * Here, only the minimum decode range is configured
 * in order to get device (SPI NOR flash) information
 * at the early stage.
 */
static int aspeed_spi_ctrl_init(struct aspeed_spi_controller *ast_ctrl)
{
	int ret;
	uint32_t cs;
	uint32_t val;
	uint32_t decode_sz_arr[ASPEED_SPI_MAX_CS];

	/* enable write capability for all CEs */
	val = readl(ast_ctrl->regs + OFFSET_CE_TYPE_SETTING);
	writel(val | (GENMASK(ast_ctrl->num_cs - 1, 0) << 16),
	       ast_ctrl->regs + OFFSET_CE_TYPE_SETTING);

	/* initial each CE's controller register */
	for (cs = 0; cs < ast_ctrl->num_cs; cs++) {
		val = CTRL_STOP_ACTIVE | CTRL_IO_MODE_USER;
		writel(val, ast_ctrl->regs + OFFSET_CE0_CTRL_REG + cs * 4);
		ast_ctrl->chips[cs].ctrl_val[ASPEED_SPI_BASE] = val;
	}

	for (cs = 0; cs < ast_ctrl->num_cs && cs < ASPEED_SPI_MAX_CS; cs++)
		decode_sz_arr[cs] = ast_ctrl->info->min_decode_sz;

	ret = aspeed_spi_decode_range_config(ast_ctrl, decode_sz_arr);

	return ret;
}

static const struct of_device_id aspeed_spi_matches[] = {
	{ .compatible = "aspeed,ast2600-fmc", .data = &ast2600_fmc_info },
	{ .compatible = "aspeed,ast2600-spi", .data = &ast2600_spi_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, aspeed_spi_matches);

static int aspeed_spi_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct spi_controller *spi_ctrl;
	struct aspeed_spi_controller *ast_ctrl;
	const struct of_device_id *match;
	struct clk *clk;
	struct resource *res;

	spi_ctrl = spi_alloc_master(dev, sizeof(struct aspeed_spi_controller));
	if (!spi_ctrl)
		return -ENOMEM;

	ast_ctrl = spi_controller_get_devdata(spi_ctrl);

	match = of_match_device(aspeed_spi_matches, dev);
	if (!match || !match->data) {
		dev_err(dev, "no compatible OF match\n");
		return -ENODEV;
	}

	ast_ctrl->info = match->data;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "spi_ctrl_reg");
	ast_ctrl->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(ast_ctrl->regs))
		return PTR_ERR(ast_ctrl->regs);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "spi_mmap");
	ast_ctrl->ahb_base_phy = res->start;
	ast_ctrl->ahb_window_sz = resource_size(res);

	ast_ctrl->dev = dev;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	ast_ctrl->ahb_clk = clk_get_rate(clk);
	devm_clk_put(&pdev->dev, clk);

	if (of_property_read_u32(dev->of_node, "num-cs", &ast_ctrl->num_cs)) {
		dev_err(dev, "fail to get chip number.\n");
		goto end;
	}

	if (ast_ctrl->num_cs > ASPEED_SPI_MAX_CS) {
		dev_err(dev, "chip number, %d, exceeds %d.\n", ast_ctrl->num_cs,
			ASPEED_SPI_MAX_CS);
		goto end;
	}

	ast_ctrl->flag = 0;
	if (of_property_read_bool(dev->of_node, "fmc-spi-user-mode")) {
		dev_info(dev, "adopt user mode\n");
		ast_ctrl->flag |= SPI_MODE_USER;
	}

	/* Should be set on AST2600-A1/A2 for errata 65 */
	if (of_property_read_bool(dev->of_node, "low-spi-clk-write")) {
		dev_info(dev, "adopt low spi clk for write\n");
		ast_ctrl->flag |= SPI_FIXED_LOW_W_CLK;
	}

	/*
	 * "spi-dma-write" should be set on AST2600-A1/A2 for errata 65.
	 * Should NOT be set on AST2600-A3 with high SPI clock
	 */
	if (of_property_read_bool(dev->of_node, "spi-dma-write")) {
		dev_info(dev, "adopt dma write mode\n");
		ast_ctrl->flag |= SPI_DMA_WRITE;
	}

	if (of_property_read_bool(dev->of_node, "spi-dma-read")) {
		dev_info(dev, "adopt dma read mode\n");
		ast_ctrl->flag |= SPI_DMA_READ;
	}

	if ((ast_ctrl->flag & SPI_MODE_USER) && (ast_ctrl->flag & SPI_DMA_WRITE)) {
		dev_err(dev, "Invalid mode selection\n");
		ret = -EINVAL;
		goto end;
	}

	ast_ctrl->op_buf = dma_alloc_coherent(dev,
		FMC_SPI_DMA_BUF_LEN, &ast_ctrl->dma_addr_phy, GFP_DMA | GFP_KERNEL);
	if (!ast_ctrl->op_buf) {
		ret = -ENOMEM;
		goto end;
	}

	ast_ctrl->irq = platform_get_irq(pdev, 0);
	if (ast_ctrl->irq < 0) {
		dev_err(dev, "fail to get irq (%d)\n", ast_ctrl->irq);
		return ast_ctrl->irq;
	}

	ret = devm_request_irq(dev, ast_ctrl->irq, aspeed_spi_dma_isr,
					IRQF_SHARED, dev_name(dev), ast_ctrl);
	if (ret < 0) {
		dev_err(dev, "fail to request irq (%d)\n", ret);
		return ret;
	}

	init_completion(&ast_ctrl->dma_done);

	ast_ctrl->chips =
		devm_kzalloc(dev,
			     sizeof(struct aspeed_spi_chip) * ast_ctrl->num_cs,
			     GFP_KERNEL);

	platform_set_drvdata(pdev, ast_ctrl);

	spi_ctrl->mode_bits =
		SPI_RX_DUAL | SPI_RX_QUAD | SPI_TX_DUAL | SPI_TX_QUAD;

	spi_ctrl->bus_num = -1;

	if ((ast_ctrl->flag & SPI_DMA_WRITE) == SPI_DMA_WRITE) {
		/* for AST2600-A1/A2 */
		spi_ctrl->mem_ops = &aspeed_spi_ops_cmd_read_dma_write;
	} else {
		/* for AST2600-A3 */
		spi_ctrl->mem_ops = &aspeed_spi_ops_user_read_write;
	}

	spi_ctrl->dev.of_node = dev->of_node;
	spi_ctrl->num_chipselect = ast_ctrl->num_cs;

	ret = aspeed_spi_ctrl_init(ast_ctrl);
	if (ret)
		goto end;

	ret = devm_spi_register_master(dev, spi_ctrl);

end:
	return ret;
}

static int aspeed_spi_remove(struct platform_device *pdev)
{
	struct aspeed_spi_controller *ast_ctrl = platform_get_drvdata(pdev);
	uint32_t val;

	/* disable write capability for all CEs */
	val = readl(ast_ctrl->regs + OFFSET_CE_TYPE_SETTING);
	writel(val & ~(GENMASK(ast_ctrl->num_cs, 0) << 16),
	       ast_ctrl->regs + OFFSET_CE_TYPE_SETTING);

	return 0;
}

static struct platform_driver aspeed_spi_driver = {
	.driver = {
		.name = "ASPEED_FMC_SPI",
		.bus = &platform_bus_type,
		.of_match_table = aspeed_spi_matches,
	},
	.probe = aspeed_spi_probe,
	.remove = aspeed_spi_remove,
};
module_platform_driver(aspeed_spi_driver);

MODULE_DESCRIPTION("ASPEED FMC/SPI Memory Controller Driver");
MODULE_AUTHOR("Chin-Ting Kuo <chin-ting_kuo@aspeedtech.com>");
MODULE_AUTHOR("Cedric Le Goater <clg@kaod.org>");
MODULE_LICENSE("GPL v2");
