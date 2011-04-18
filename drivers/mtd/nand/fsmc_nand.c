/*
 * drivers/mtd/nand/fsmc_nand.c
 *
 * ST Microelectronics
 * Flexible Static Memory Controller (FSMC)
 * Driver for NAND portions
 *
 * Copyright © 2010 ST Microelectronics
 * Vipin Kumar <vipin.kumar@st.com>
 * Ashish Priyadarshi
 *
 * Based on drivers/mtd/nand/nomadik_nand.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/resource.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mtd/fsmc.h>
#include <linux/amba/bus.h>
#include <mtd/mtd-abi.h>

static struct nand_ecclayout fsmc_ecc1_layout = {
	.eccbytes = 24,
	.eccpos = {2, 3, 4, 18, 19, 20, 34, 35, 36, 50, 51, 52,
		66, 67, 68, 82, 83, 84, 98, 99, 100, 114, 115, 116},
	.oobfree = {
		{.offset = 8, .length = 8},
		{.offset = 24, .length = 8},
		{.offset = 40, .length = 8},
		{.offset = 56, .length = 8},
		{.offset = 72, .length = 8},
		{.offset = 88, .length = 8},
		{.offset = 104, .length = 8},
		{.offset = 120, .length = 8}
	}
};

static struct nand_ecclayout fsmc_ecc4_lp_layout = {
	.eccbytes = 104,
	.eccpos = {  2,   3,   4,   5,   6,   7,   8,
		9,  10,  11,  12,  13,  14,
		18,  19,  20,  21,  22,  23,  24,
		25,  26,  27,  28,  29,  30,
		34,  35,  36,  37,  38,  39,  40,
		41,  42,  43,  44,  45,  46,
		50,  51,  52,  53,  54,  55,  56,
		57,  58,  59,  60,  61,  62,
		66,  67,  68,  69,  70,  71,  72,
		73,  74,  75,  76,  77,  78,
		82,  83,  84,  85,  86,  87,  88,
		89,  90,  91,  92,  93,  94,
		98,  99, 100, 101, 102, 103, 104,
		105, 106, 107, 108, 109, 110,
		114, 115, 116, 117, 118, 119, 120,
		121, 122, 123, 124, 125, 126
	},
	.oobfree = {
		{.offset = 15, .length = 3},
		{.offset = 31, .length = 3},
		{.offset = 47, .length = 3},
		{.offset = 63, .length = 3},
		{.offset = 79, .length = 3},
		{.offset = 95, .length = 3},
		{.offset = 111, .length = 3},
		{.offset = 127, .length = 1}
	}
};

/*
 * ECC placement definitions in oobfree type format.
 * There are 13 bytes of ecc for every 512 byte block and it has to be read
 * consecutively and immediately after the 512 byte data block for hardware to
 * generate the error bit offsets in 512 byte data.
 * Managing the ecc bytes in the following way makes it easier for software to
 * read ecc bytes consecutive to data bytes. This way is similar to
 * oobfree structure maintained already in generic nand driver
 */
static struct fsmc_eccplace fsmc_ecc4_lp_place = {
	.eccplace = {
		{.offset = 2, .length = 13},
		{.offset = 18, .length = 13},
		{.offset = 34, .length = 13},
		{.offset = 50, .length = 13},
		{.offset = 66, .length = 13},
		{.offset = 82, .length = 13},
		{.offset = 98, .length = 13},
		{.offset = 114, .length = 13}
	}
};

static struct nand_ecclayout fsmc_ecc4_sp_layout = {
	.eccbytes = 13,
	.eccpos = { 0,  1,  2,  3,  6,  7, 8,
		9, 10, 11, 12, 13, 14
	},
	.oobfree = {
		{.offset = 15, .length = 1},
	}
};

static struct fsmc_eccplace fsmc_ecc4_sp_place = {
	.eccplace = {
		{.offset = 0, .length = 4},
		{.offset = 6, .length = 9}
	}
};


#ifdef CONFIG_MTD_PARTITIONS
/*
 * Default partition tables to be used if the partition information not
 * provided through platform data.
 *
 * Default partition layout for small page(= 512 bytes) devices
 * Size for "Root file system" is updated in driver based on actual device size
 */
static struct mtd_partition partition_info_16KB_blk[] = {
	{
		.name = "X-loader",
		.offset = 0,
		.size = 4*0x4000,
	},
	{
		.name = "U-Boot",
		.offset = 0x10000,
		.size = 20*0x4000,
	},
	{
		.name = "Kernel",
		.offset = 0x60000,
		.size = 256*0x4000,
	},
	{
		.name = "Root File System",
		.offset = 0x460000,
		.size = 0,
	},
};

/*
 * Default partition layout for large page(> 512 bytes) devices
 * Size for "Root file system" is updated in driver based on actual device size
 */
static struct mtd_partition partition_info_128KB_blk[] = {
	{
		.name = "X-loader",
		.offset = 0,
		.size = 4*0x20000,
	},
	{
		.name = "U-Boot",
		.offset = 0x80000,
		.size = 12*0x20000,
	},
	{
		.name = "Kernel",
		.offset = 0x200000,
		.size = 48*0x20000,
	},
	{
		.name = "Root File System",
		.offset = 0x800000,
		.size = 0,
	},
};

#ifdef CONFIG_MTD_CMDLINE_PARTS
const char *part_probes[] = { "cmdlinepart", NULL };
#endif
#endif

/**
 * struct fsmc_nand_data - structure for FSMC NAND device state
 *
 * @pid:		Part ID on the AMBA PrimeCell format
 * @mtd:		MTD info for a NAND flash.
 * @nand:		Chip related info for a NAND flash.
 * @partitions:		Partition info for a NAND Flash.
 * @nr_partitions:	Total number of partition of a NAND flash.
 *
 * @ecc_place:		ECC placing locations in oobfree type format.
 * @bank:		Bank number for probed device.
 * @clk:		Clock structure for FSMC.
 *
 * @data_va:		NAND port for Data.
 * @cmd_va:		NAND port for Command.
 * @addr_va:		NAND port for Address.
 * @regs_va:		FSMC regs base address.
 */
struct fsmc_nand_data {
	u32			pid;
	struct mtd_info		mtd;
	struct nand_chip	nand;
	struct mtd_partition	*partitions;
	unsigned int		nr_partitions;

	struct fsmc_eccplace	*ecc_place;
	unsigned int		bank;
	struct clk		*clk;

	struct resource		*resregs;
	struct resource		*rescmd;
	struct resource		*resaddr;
	struct resource		*resdata;

	void __iomem		*data_va;
	void __iomem		*cmd_va;
	void __iomem		*addr_va;
	void __iomem		*regs_va;

	void			(*select_chip)(uint32_t bank, uint32_t busw);
};

/* Assert CS signal based on chipnr */
static void fsmc_select_chip(struct mtd_info *mtd, int chipnr)
{
	struct nand_chip *chip = mtd->priv;
	struct fsmc_nand_data *host;

	host = container_of(mtd, struct fsmc_nand_data, mtd);

	switch (chipnr) {
	case -1:
		chip->cmd_ctrl(mtd, NAND_CMD_NONE, 0 | NAND_CTRL_CHANGE);
		break;
	case 0:
	case 1:
	case 2:
	case 3:
		if (host->select_chip)
			host->select_chip(chipnr,
					chip->options & NAND_BUSWIDTH_16);
		break;

	default:
		BUG();
	}
}

/*
 * fsmc_cmd_ctrl - For facilitaing Hardware access
 * This routine allows hardware specific access to control-lines(ALE,CLE)
 */
static void fsmc_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;
	struct fsmc_nand_data *host = container_of(mtd,
					struct fsmc_nand_data, mtd);
	struct fsmc_regs *regs = host->regs_va;
	unsigned int bank = host->bank;

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_CLE) {
			this->IO_ADDR_R = (void __iomem *)host->cmd_va;
			this->IO_ADDR_W = (void __iomem *)host->cmd_va;
		} else if (ctrl & NAND_ALE) {
			this->IO_ADDR_R = (void __iomem *)host->addr_va;
			this->IO_ADDR_W = (void __iomem *)host->addr_va;
		} else {
			this->IO_ADDR_R = (void __iomem *)host->data_va;
			this->IO_ADDR_W = (void __iomem *)host->data_va;
		}

		if (ctrl & NAND_NCE) {
			writel(readl(&regs->bank_regs[bank].pc) | FSMC_ENABLE,
					&regs->bank_regs[bank].pc);
		} else {
			writel(readl(&regs->bank_regs[bank].pc) & ~FSMC_ENABLE,
				       &regs->bank_regs[bank].pc);
		}
	}

	mb();

	if (cmd != NAND_CMD_NONE)
		writeb(cmd, this->IO_ADDR_W);
}

/*
 * fsmc_nand_setup - FSMC (Flexible Static Memory Controller) init routine
 *
 * This routine initializes timing parameters related to NAND memory access in
 * FSMC registers
 */
static void __init fsmc_nand_setup(struct fsmc_regs *regs, uint32_t bank,
				   uint32_t busw)
{
	uint32_t value = FSMC_DEVTYPE_NAND | FSMC_ENABLE | FSMC_WAITON;

	if (busw)
		writel(value | FSMC_DEVWID_16, &regs->bank_regs[bank].pc);
	else
		writel(value | FSMC_DEVWID_8, &regs->bank_regs[bank].pc);

	writel(readl(&regs->bank_regs[bank].pc) | FSMC_TCLR_1 | FSMC_TAR_1,
	       &regs->bank_regs[bank].pc);
	writel(FSMC_THIZ_1 | FSMC_THOLD_4 | FSMC_TWAIT_6 | FSMC_TSET_0,
	       &regs->bank_regs[bank].comm);
	writel(FSMC_THIZ_1 | FSMC_THOLD_4 | FSMC_TWAIT_6 | FSMC_TSET_0,
	       &regs->bank_regs[bank].attrib);
}

/*
 * fsmc_enable_hwecc - Enables Hardware ECC through FSMC registers
 */
static void fsmc_enable_hwecc(struct mtd_info *mtd, int mode)
{
	struct fsmc_nand_data *host = container_of(mtd,
					struct fsmc_nand_data, mtd);
	struct fsmc_regs *regs = host->regs_va;
	uint32_t bank = host->bank;

	writel(readl(&regs->bank_regs[bank].pc) & ~FSMC_ECCPLEN_256,
		       &regs->bank_regs[bank].pc);
	writel(readl(&regs->bank_regs[bank].pc) & ~FSMC_ECCEN,
			&regs->bank_regs[bank].pc);
	writel(readl(&regs->bank_regs[bank].pc) | FSMC_ECCEN,
			&regs->bank_regs[bank].pc);
}

/*
 * fsmc_read_hwecc_ecc4 - Hardware ECC calculator for ecc4 option supported by
 * FSMC. ECC is 13 bytes for 512 bytes of data (supports error correction up to
 * max of 8-bits)
 */
static int fsmc_read_hwecc_ecc4(struct mtd_info *mtd, const uint8_t *data,
				uint8_t *ecc)
{
	struct fsmc_nand_data *host = container_of(mtd,
					struct fsmc_nand_data, mtd);
	struct fsmc_regs *regs = host->regs_va;
	uint32_t bank = host->bank;
	uint32_t ecc_tmp;
	unsigned long deadline = jiffies + FSMC_BUSY_WAIT_TIMEOUT;

	do {
		if (readl(&regs->bank_regs[bank].sts) & FSMC_CODE_RDY)
			break;
		else
			cond_resched();
	} while (!time_after_eq(jiffies, deadline));

	ecc_tmp = readl(&regs->bank_regs[bank].ecc1);
	ecc[0] = (uint8_t) (ecc_tmp >> 0);
	ecc[1] = (uint8_t) (ecc_tmp >> 8);
	ecc[2] = (uint8_t) (ecc_tmp >> 16);
	ecc[3] = (uint8_t) (ecc_tmp >> 24);

	ecc_tmp = readl(&regs->bank_regs[bank].ecc2);
	ecc[4] = (uint8_t) (ecc_tmp >> 0);
	ecc[5] = (uint8_t) (ecc_tmp >> 8);
	ecc[6] = (uint8_t) (ecc_tmp >> 16);
	ecc[7] = (uint8_t) (ecc_tmp >> 24);

	ecc_tmp = readl(&regs->bank_regs[bank].ecc3);
	ecc[8] = (uint8_t) (ecc_tmp >> 0);
	ecc[9] = (uint8_t) (ecc_tmp >> 8);
	ecc[10] = (uint8_t) (ecc_tmp >> 16);
	ecc[11] = (uint8_t) (ecc_tmp >> 24);

	ecc_tmp = readl(&regs->bank_regs[bank].sts);
	ecc[12] = (uint8_t) (ecc_tmp >> 16);

	return 0;
}

/*
 * fsmc_read_hwecc_ecc1 - Hardware ECC calculator for ecc1 option supported by
 * FSMC. ECC is 3 bytes for 512 bytes of data (supports error correction up to
 * max of 1-bit)
 */
static int fsmc_read_hwecc_ecc1(struct mtd_info *mtd, const uint8_t *data,
				uint8_t *ecc)
{
	struct fsmc_nand_data *host = container_of(mtd,
					struct fsmc_nand_data, mtd);
	struct fsmc_regs *regs = host->regs_va;
	uint32_t bank = host->bank;
	uint32_t ecc_tmp;

	ecc_tmp = readl(&regs->bank_regs[bank].ecc1);
	ecc[0] = (uint8_t) (ecc_tmp >> 0);
	ecc[1] = (uint8_t) (ecc_tmp >> 8);
	ecc[2] = (uint8_t) (ecc_tmp >> 16);

	return 0;
}

/*
 * fsmc_read_page_hwecc
 * @mtd:	mtd info structure
 * @chip:	nand chip info structure
 * @buf:	buffer to store read data
 * @page:	page number to read
 *
 * This routine is needed for fsmc version 8 as reading from NAND chip has to be
 * performed in a strict sequence as follows:
 * data(512 byte) -> ecc(13 byte)
 * After this read, fsmc hardware generates and reports error data bits(up to a
 * max of 8 bits)
 */
static int fsmc_read_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
				 uint8_t *buf, int page)
{
	struct fsmc_nand_data *host = container_of(mtd,
					struct fsmc_nand_data, mtd);
	struct fsmc_eccplace *ecc_place = host->ecc_place;
	int i, j, s, stat, eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	uint8_t *p = buf;
	uint8_t *ecc_calc = chip->buffers->ecccalc;
	uint8_t *ecc_code = chip->buffers->ecccode;
	int off, len, group = 0;
	/*
	 * ecc_oob is intentionally taken as uint16_t. In 16bit devices, we
	 * end up reading 14 bytes (7 words) from oob. The local array is
	 * to maintain word alignment
	 */
	uint16_t ecc_oob[7];
	uint8_t *oob = (uint8_t *)&ecc_oob[0];

	for (i = 0, s = 0; s < eccsteps; s++, i += eccbytes, p += eccsize) {

		chip->cmdfunc(mtd, NAND_CMD_READ0, s * eccsize, page);
		chip->ecc.hwctl(mtd, NAND_ECC_READ);
		chip->read_buf(mtd, p, eccsize);

		for (j = 0; j < eccbytes;) {
			off = ecc_place->eccplace[group].offset;
			len = ecc_place->eccplace[group].length;
			group++;

			/*
			* length is intentionally kept a higher multiple of 2
			* to read at least 13 bytes even in case of 16 bit NAND
			* devices
			*/
			len = roundup(len, 2);
			chip->cmdfunc(mtd, NAND_CMD_READOOB, off, page);
			chip->read_buf(mtd, oob + j, len);
			j += len;
		}

		memcpy(&ecc_code[i], oob, 13);
		chip->ecc.calculate(mtd, p, &ecc_calc[i]);

		stat = chip->ecc.correct(mtd, p, &ecc_code[i], &ecc_calc[i]);
		if (stat < 0)
			mtd->ecc_stats.failed++;
		else
			mtd->ecc_stats.corrected += stat;
	}

	return 0;
}

/*
 * fsmc_correct_data
 * @mtd:	mtd info structure
 * @dat:	buffer of read data
 * @read_ecc:	ecc read from device spare area
 * @calc_ecc:	ecc calculated from read data
 *
 * calc_ecc is a 104 bit information containing maximum of 8 error
 * offset informations of 13 bits each in 512 bytes of read data.
 */
static int fsmc_correct_data(struct mtd_info *mtd, uint8_t *dat,
			     uint8_t *read_ecc, uint8_t *calc_ecc)
{
	struct fsmc_nand_data *host = container_of(mtd,
					struct fsmc_nand_data, mtd);
	struct fsmc_regs *regs = host->regs_va;
	unsigned int bank = host->bank;
	uint16_t err_idx[8];
	uint64_t ecc_data[2];
	uint32_t num_err, i;

	/* The calculated ecc is actually the correction index in data */
	memcpy(ecc_data, calc_ecc, 13);

	/*
	 * ------------------- calc_ecc[] bit wise -----------|--13 bits--|
	 * |---idx[7]--|--.....-----|---idx[2]--||---idx[1]--||---idx[0]--|
	 *
	 * calc_ecc is a 104 bit information containing maximum of 8 error
	 * offset informations of 13 bits each. calc_ecc is copied into a
	 * uint64_t array and error offset indexes are populated in err_idx
	 * array
	 */
	for (i = 0; i < 8; i++) {
		if (i == 4) {
			err_idx[4] = ((ecc_data[1] & 0x1) << 12) | ecc_data[0];
			ecc_data[1] >>= 1;
			continue;
		}
		err_idx[i] = (ecc_data[i/4] & 0x1FFF);
		ecc_data[i/4] >>= 13;
	}

	num_err = (readl(&regs->bank_regs[bank].sts) >> 10) & 0xF;

	if (num_err == 0xF)
		return -EBADMSG;

	i = 0;
	while (num_err--) {
		change_bit(0, (unsigned long *)&err_idx[i]);
		change_bit(1, (unsigned long *)&err_idx[i]);

		if (err_idx[i] <= 512 * 8) {
			change_bit(err_idx[i], (unsigned long *)dat);
			i++;
		}
	}
	return i;
}

/*
 * fsmc_nand_probe - Probe function
 * @pdev:       platform device structure
 */
static int __init fsmc_nand_probe(struct platform_device *pdev)
{
	struct fsmc_nand_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct fsmc_nand_data *host;
	struct mtd_info *mtd;
	struct nand_chip *nand;
	struct fsmc_regs *regs;
	struct resource *res;
	int ret = 0;
	u32 pid;
	int i;

	if (!pdata) {
		dev_err(&pdev->dev, "platform data is NULL\n");
		return -EINVAL;
	}

	/* Allocate memory for the device structure (and zero it) */
	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host) {
		dev_err(&pdev->dev, "failed to allocate device structure\n");
		return -ENOMEM;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "nand_data");
	if (!res) {
		ret = -EIO;
		goto err_probe1;
	}

	host->resdata = request_mem_region(res->start, resource_size(res),
			pdev->name);
	if (!host->resdata) {
		ret = -EIO;
		goto err_probe1;
	}

	host->data_va = ioremap(res->start, resource_size(res));
	if (!host->data_va) {
		ret = -EIO;
		goto err_probe1;
	}

	host->resaddr = request_mem_region(res->start + PLAT_NAND_ALE,
			resource_size(res), pdev->name);
	if (!host->resaddr) {
		ret = -EIO;
		goto err_probe1;
	}

	host->addr_va = ioremap(res->start + PLAT_NAND_ALE, resource_size(res));
	if (!host->addr_va) {
		ret = -EIO;
		goto err_probe1;
	}

	host->rescmd = request_mem_region(res->start + PLAT_NAND_CLE,
			resource_size(res), pdev->name);
	if (!host->rescmd) {
		ret = -EIO;
		goto err_probe1;
	}

	host->cmd_va = ioremap(res->start + PLAT_NAND_CLE, resource_size(res));
	if (!host->cmd_va) {
		ret = -EIO;
		goto err_probe1;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fsmc_regs");
	if (!res) {
		ret = -EIO;
		goto err_probe1;
	}

	host->resregs = request_mem_region(res->start, resource_size(res),
			pdev->name);
	if (!host->resregs) {
		ret = -EIO;
		goto err_probe1;
	}

	host->regs_va = ioremap(res->start, resource_size(res));
	if (!host->regs_va) {
		ret = -EIO;
		goto err_probe1;
	}

	host->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk)) {
		dev_err(&pdev->dev, "failed to fetch block clock\n");
		ret = PTR_ERR(host->clk);
		host->clk = NULL;
		goto err_probe1;
	}

	ret = clk_enable(host->clk);
	if (ret)
		goto err_probe1;

	/*
	 * This device ID is actually a common AMBA ID as used on the
	 * AMBA PrimeCell bus. However it is not a PrimeCell.
	 */
	for (pid = 0, i = 0; i < 4; i++)
		pid |= (readl(host->regs_va + resource_size(res) - 0x20 + 4 * i) & 255) << (i * 8);
	host->pid = pid;
	dev_info(&pdev->dev, "FSMC device partno %03x, manufacturer %02x, "
		 "revision %02x, config %02x\n",
		 AMBA_PART_BITS(pid), AMBA_MANF_BITS(pid),
		 AMBA_REV_BITS(pid), AMBA_CONFIG_BITS(pid));

	host->bank = pdata->bank;
	host->select_chip = pdata->select_bank;
	regs = host->regs_va;

	/* Link all private pointers */
	mtd = &host->mtd;
	nand = &host->nand;
	mtd->priv = nand;
	nand->priv = host;

	host->mtd.owner = THIS_MODULE;
	nand->IO_ADDR_R = host->data_va;
	nand->IO_ADDR_W = host->data_va;
	nand->cmd_ctrl = fsmc_cmd_ctrl;
	nand->chip_delay = 30;

	nand->ecc.mode = NAND_ECC_HW;
	nand->ecc.hwctl = fsmc_enable_hwecc;
	nand->ecc.size = 512;
	nand->options = pdata->options;
	nand->select_chip = fsmc_select_chip;

	if (pdata->width == FSMC_NAND_BW16)
		nand->options |= NAND_BUSWIDTH_16;

	fsmc_nand_setup(regs, host->bank, nand->options & NAND_BUSWIDTH_16);

	if (AMBA_REV_BITS(host->pid) >= 8) {
		nand->ecc.read_page = fsmc_read_page_hwecc;
		nand->ecc.calculate = fsmc_read_hwecc_ecc4;
		nand->ecc.correct = fsmc_correct_data;
		nand->ecc.bytes = 13;
	} else {
		nand->ecc.calculate = fsmc_read_hwecc_ecc1;
		nand->ecc.correct = nand_correct_data;
		nand->ecc.bytes = 3;
	}

	/*
	 * Scan to find existence of the device
	 */
	if (nand_scan_ident(&host->mtd, 1, NULL)) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "No NAND Device found!\n");
		goto err_probe;
	}

	if (AMBA_REV_BITS(host->pid) >= 8) {
		if (host->mtd.writesize == 512) {
			nand->ecc.layout = &fsmc_ecc4_sp_layout;
			host->ecc_place = &fsmc_ecc4_sp_place;
		} else {
			nand->ecc.layout = &fsmc_ecc4_lp_layout;
			host->ecc_place = &fsmc_ecc4_lp_place;
		}
	} else {
		nand->ecc.layout = &fsmc_ecc1_layout;
	}

	/* Second stage of scan to fill MTD data-structures */
	if (nand_scan_tail(&host->mtd)) {
		ret = -ENXIO;
		goto err_probe;
	}

	/*
	 * The partition information can is accessed by (in the same precedence)
	 *
	 * command line through Bootloader,
	 * platform data,
	 * default partition information present in driver.
	 */
#ifdef CONFIG_MTD_PARTITIONS
#ifdef CONFIG_MTD_CMDLINE_PARTS
	/*
	 * Check if partition info passed via command line
	 */
	host->mtd.name = "nand";
	host->nr_partitions = parse_mtd_partitions(&host->mtd, part_probes,
			&host->partitions, 0);
	if (host->nr_partitions <= 0) {
#endif
		/*
		 * Check if partition info passed via command line
		 */
		if (pdata->partitions) {
			host->partitions = pdata->partitions;
			host->nr_partitions = pdata->nr_partitions;
		} else {
			struct mtd_partition *partition;
			int i;

			/* Select the default partitions info */
			switch (host->mtd.size) {
			case 0x01000000:
			case 0x02000000:
			case 0x04000000:
				host->partitions = partition_info_16KB_blk;
				host->nr_partitions =
					sizeof(partition_info_16KB_blk) /
					sizeof(struct mtd_partition);
				break;
			case 0x08000000:
			case 0x10000000:
			case 0x20000000:
			case 0x40000000:
				host->partitions = partition_info_128KB_blk;
				host->nr_partitions =
					sizeof(partition_info_128KB_blk) /
					sizeof(struct mtd_partition);
				break;
			default:
				ret = -ENXIO;
				pr_err("Unsupported NAND size\n");
				goto err_probe;
			}

			partition = host->partitions;
			for (i = 0; i < host->nr_partitions; i++, partition++) {
				if (partition->size == 0) {
					partition->size = host->mtd.size -
						partition->offset;
					break;
				}
			}
		}
#ifdef CONFIG_MTD_CMDLINE_PARTS
	}
#endif

	if (host->partitions) {
		ret = add_mtd_partitions(&host->mtd, host->partitions,
				host->nr_partitions);
		if (ret)
			goto err_probe;
	}
#else
	dev_info(&pdev->dev, "Registering %s as whole device\n", mtd->name);
	if (!add_mtd_device(mtd)) {
		ret = -ENXIO;
		goto err_probe;
	}
#endif

	platform_set_drvdata(pdev, host);
	dev_info(&pdev->dev, "FSMC NAND driver registration successful\n");
	return 0;

err_probe:
	clk_disable(host->clk);
err_probe1:
	if (host->clk)
		clk_put(host->clk);
	if (host->regs_va)
		iounmap(host->regs_va);
	if (host->resregs)
		release_mem_region(host->resregs->start,
				resource_size(host->resregs));
	if (host->cmd_va)
		iounmap(host->cmd_va);
	if (host->rescmd)
		release_mem_region(host->rescmd->start,
				resource_size(host->rescmd));
	if (host->addr_va)
		iounmap(host->addr_va);
	if (host->resaddr)
		release_mem_region(host->resaddr->start,
				resource_size(host->resaddr));
	if (host->data_va)
		iounmap(host->data_va);
	if (host->resdata)
		release_mem_region(host->resdata->start,
				resource_size(host->resdata));

	kfree(host);
	return ret;
}

/*
 * Clean up routine
 */
static int fsmc_nand_remove(struct platform_device *pdev)
{
	struct fsmc_nand_data *host = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (host) {
#ifdef CONFIG_MTD_PARTITIONS
		del_mtd_partitions(&host->mtd);
#else
		del_mtd_device(&host->mtd);
#endif
		clk_disable(host->clk);
		clk_put(host->clk);

		iounmap(host->regs_va);
		release_mem_region(host->resregs->start,
				resource_size(host->resregs));
		iounmap(host->cmd_va);
		release_mem_region(host->rescmd->start,
				resource_size(host->rescmd));
		iounmap(host->addr_va);
		release_mem_region(host->resaddr->start,
				resource_size(host->resaddr));
		iounmap(host->data_va);
		release_mem_region(host->resdata->start,
				resource_size(host->resdata));

		kfree(host);
	}
	return 0;
}

#ifdef CONFIG_PM
static int fsmc_nand_suspend(struct device *dev)
{
	struct fsmc_nand_data *host = dev_get_drvdata(dev);
	if (host)
		clk_disable(host->clk);
	return 0;
}

static int fsmc_nand_resume(struct device *dev)
{
	struct fsmc_nand_data *host = dev_get_drvdata(dev);
	if (host)
		clk_enable(host->clk);
	return 0;
}

static const struct dev_pm_ops fsmc_nand_pm_ops = {
	.suspend = fsmc_nand_suspend,
	.resume = fsmc_nand_resume,
};
#endif

static struct platform_driver fsmc_nand_driver = {
	.remove = fsmc_nand_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "fsmc-nand",
#ifdef CONFIG_PM
		.pm = &fsmc_nand_pm_ops,
#endif
	},
};

static int __init fsmc_nand_init(void)
{
	return platform_driver_probe(&fsmc_nand_driver,
				     fsmc_nand_probe);
}
module_init(fsmc_nand_init);

static void __exit fsmc_nand_exit(void)
{
	platform_driver_unregister(&fsmc_nand_driver);
}
module_exit(fsmc_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vipin Kumar <vipin.kumar@st.com>, Ashish Priyadarshi");
MODULE_DESCRIPTION("NAND driver for SPEAr Platforms");
