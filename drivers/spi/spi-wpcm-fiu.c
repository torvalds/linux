// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Jonathan Neuschäfer

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spi/spi-mem.h>

#define FIU_CFG		0x00
#define FIU_BURST_BFG	0x01
#define FIU_RESP_CFG	0x02
#define FIU_CFBB_PROT	0x03
#define FIU_FWIN1_LOW	0x04
#define FIU_FWIN1_HIGH	0x06
#define FIU_FWIN2_LOW	0x08
#define FIU_FWIN2_HIGH	0x0a
#define FIU_FWIN3_LOW	0x0c
#define FIU_FWIN3_HIGH	0x0e
#define FIU_PROT_LOCK	0x10
#define FIU_PROT_CLEAR	0x11
#define FIU_SPI_FL_CFG	0x14
#define FIU_UMA_CODE	0x16
#define FIU_UMA_AB0	0x17
#define FIU_UMA_AB1	0x18
#define FIU_UMA_AB2	0x19
#define FIU_UMA_DB0	0x1a
#define FIU_UMA_DB1	0x1b
#define FIU_UMA_DB2	0x1c
#define FIU_UMA_DB3	0x1d
#define FIU_UMA_CTS	0x1e
#define FIU_UMA_ECTS	0x1f

#define FIU_BURST_CFG_R16	3

#define FIU_UMA_CTS_D_SIZE(x)	(x)
#define FIU_UMA_CTS_A_SIZE	BIT(3)
#define FIU_UMA_CTS_WR		BIT(4)
#define FIU_UMA_CTS_CS(x)	((x) << 5)
#define FIU_UMA_CTS_EXEC_DONE	BIT(7)

#define SHM_FLASH_SIZE	0x02
#define SHM_FLASH_SIZE_STALL_HOST BIT(6)

/*
 * I observed a typical wait time of 16 iterations for a UMA transfer to
 * finish, so this should be a safe limit.
 */
#define UMA_WAIT_ITERATIONS 100

/* The memory-mapped view of flash is 16 MiB long */
#define MAX_MEMORY_SIZE_PER_CS	(16 << 20)
#define MAX_MEMORY_SIZE_TOTAL	(4 * MAX_MEMORY_SIZE_PER_CS)

struct wpcm_fiu_spi {
	struct device *dev;
	struct clk *clk;
	void __iomem *regs;
	void __iomem *memory;
	size_t memory_size;
	struct regmap *shm_regmap;
};

static void wpcm_fiu_set_opcode(struct wpcm_fiu_spi *fiu, u8 opcode)
{
	writeb(opcode, fiu->regs + FIU_UMA_CODE);
}

static void wpcm_fiu_set_addr(struct wpcm_fiu_spi *fiu, u32 addr)
{
	writeb((addr >>  0) & 0xff, fiu->regs + FIU_UMA_AB0);
	writeb((addr >>  8) & 0xff, fiu->regs + FIU_UMA_AB1);
	writeb((addr >> 16) & 0xff, fiu->regs + FIU_UMA_AB2);
}

static void wpcm_fiu_set_data(struct wpcm_fiu_spi *fiu, const u8 *data, unsigned int nbytes)
{
	int i;

	for (i = 0; i < nbytes; i++)
		writeb(data[i], fiu->regs + FIU_UMA_DB0 + i);
}

static void wpcm_fiu_get_data(struct wpcm_fiu_spi *fiu, u8 *data, unsigned int nbytes)
{
	int i;

	for (i = 0; i < nbytes; i++)
		data[i] = readb(fiu->regs + FIU_UMA_DB0 + i);
}

/*
 * Perform a UMA (User Mode Access) operation, i.e. a software-controlled SPI transfer.
 */
static int wpcm_fiu_do_uma(struct wpcm_fiu_spi *fiu, unsigned int cs,
			   bool use_addr, bool write, int data_bytes)
{
	int i = 0;
	u8 cts = FIU_UMA_CTS_EXEC_DONE | FIU_UMA_CTS_CS(cs);

	if (use_addr)
		cts |= FIU_UMA_CTS_A_SIZE;
	if (write)
		cts |= FIU_UMA_CTS_WR;
	cts |= FIU_UMA_CTS_D_SIZE(data_bytes);

	writeb(cts, fiu->regs + FIU_UMA_CTS);

	for (i = 0; i < UMA_WAIT_ITERATIONS; i++)
		if (!(readb(fiu->regs + FIU_UMA_CTS) & FIU_UMA_CTS_EXEC_DONE))
			return 0;

	dev_info(fiu->dev, "UMA transfer has not finished in %d iterations\n", UMA_WAIT_ITERATIONS);
	return -EIO;
}

static void wpcm_fiu_ects_assert(struct wpcm_fiu_spi *fiu, unsigned int cs)
{
	u8 ects = readb(fiu->regs + FIU_UMA_ECTS);

	ects &= ~BIT(cs);
	writeb(ects, fiu->regs + FIU_UMA_ECTS);
}

static void wpcm_fiu_ects_deassert(struct wpcm_fiu_spi *fiu, unsigned int cs)
{
	u8 ects = readb(fiu->regs + FIU_UMA_ECTS);

	ects |= BIT(cs);
	writeb(ects, fiu->regs + FIU_UMA_ECTS);
}

struct wpcm_fiu_op_shape {
	bool (*match)(const struct spi_mem_op *op);
	int (*exec)(struct spi_mem *mem, const struct spi_mem_op *op);
};

static bool wpcm_fiu_normal_match(const struct spi_mem_op *op)
{
	// Opcode 0x0b (FAST READ) is treated differently in hardware
	if (op->cmd.opcode == 0x0b)
		return false;

	return (op->addr.nbytes == 0 || op->addr.nbytes == 3) &&
	       op->dummy.nbytes == 0 && op->data.nbytes <= 4;
}

static int wpcm_fiu_normal_exec(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct wpcm_fiu_spi *fiu = spi_controller_get_devdata(mem->spi->controller);
	int ret;

	wpcm_fiu_set_opcode(fiu, op->cmd.opcode);
	wpcm_fiu_set_addr(fiu, op->addr.val);
	if (op->data.dir == SPI_MEM_DATA_OUT)
		wpcm_fiu_set_data(fiu, op->data.buf.out, op->data.nbytes);

	ret = wpcm_fiu_do_uma(fiu, spi_get_chipselect(mem->spi, 0), op->addr.nbytes == 3,
			      op->data.dir == SPI_MEM_DATA_OUT, op->data.nbytes);

	if (op->data.dir == SPI_MEM_DATA_IN)
		wpcm_fiu_get_data(fiu, op->data.buf.in, op->data.nbytes);

	return ret;
}

static bool wpcm_fiu_fast_read_match(const struct spi_mem_op *op)
{
	return op->cmd.opcode == 0x0b && op->addr.nbytes == 3 &&
	       op->dummy.nbytes == 1 &&
	       op->data.nbytes >= 1 && op->data.nbytes <= 4 &&
	       op->data.dir == SPI_MEM_DATA_IN;
}

static int wpcm_fiu_fast_read_exec(struct spi_mem *mem, const struct spi_mem_op *op)
{
	return -EINVAL;
}

/*
 * 4-byte addressing.
 *
 * Flash view:  [ C  A  A  A   A     D  D  D  D]
 * bytes:        13 aa bb cc  dd -> 5a a5 f0 0f
 * FIU's view:  [ C  A  A  A][ C     D  D  D  D]
 * FIU mode:    [ read/write][      read       ]
 */
static bool wpcm_fiu_4ba_match(const struct spi_mem_op *op)
{
	return op->addr.nbytes == 4 && op->dummy.nbytes == 0 && op->data.nbytes <= 4;
}

static int wpcm_fiu_4ba_exec(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct wpcm_fiu_spi *fiu = spi_controller_get_devdata(mem->spi->controller);
	int cs = spi_get_chipselect(mem->spi, 0);

	wpcm_fiu_ects_assert(fiu, cs);

	wpcm_fiu_set_opcode(fiu, op->cmd.opcode);
	wpcm_fiu_set_addr(fiu, op->addr.val >> 8);
	wpcm_fiu_do_uma(fiu, cs, true, false, 0);

	wpcm_fiu_set_opcode(fiu, op->addr.val & 0xff);
	wpcm_fiu_set_addr(fiu, 0);
	if (op->data.dir == SPI_MEM_DATA_OUT)
		wpcm_fiu_set_data(fiu, op->data.buf.out, op->data.nbytes);
	wpcm_fiu_do_uma(fiu, cs, false, op->data.dir == SPI_MEM_DATA_OUT, op->data.nbytes);

	wpcm_fiu_ects_deassert(fiu, cs);

	if (op->data.dir == SPI_MEM_DATA_IN)
		wpcm_fiu_get_data(fiu, op->data.buf.in, op->data.nbytes);

	return 0;
}

/*
 * RDID (Read Identification) needs special handling because Linux expects to
 * be able to read 6 ID bytes and FIU can only read up to 4 at once.
 *
 * We're lucky in this case, because executing the RDID instruction twice will
 * result in the same result.
 *
 * What we do is as follows (C: write command/opcode byte, D: read data byte,
 * A: write address byte):
 *
 *  1. C D D D
 *  2. C A A A D D D
 */
static bool wpcm_fiu_rdid_match(const struct spi_mem_op *op)
{
	return op->cmd.opcode == 0x9f && op->addr.nbytes == 0 &&
	       op->dummy.nbytes == 0 && op->data.nbytes == 6 &&
	       op->data.dir == SPI_MEM_DATA_IN;
}

static int wpcm_fiu_rdid_exec(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct wpcm_fiu_spi *fiu = spi_controller_get_devdata(mem->spi->controller);
	int cs = spi_get_chipselect(mem->spi, 0);

	/* First transfer */
	wpcm_fiu_set_opcode(fiu, op->cmd.opcode);
	wpcm_fiu_set_addr(fiu, 0);
	wpcm_fiu_do_uma(fiu, cs, false, false, 3);
	wpcm_fiu_get_data(fiu, op->data.buf.in, 3);

	/* Second transfer */
	wpcm_fiu_set_opcode(fiu, op->cmd.opcode);
	wpcm_fiu_set_addr(fiu, 0);
	wpcm_fiu_do_uma(fiu, cs, true, false, 3);
	wpcm_fiu_get_data(fiu, op->data.buf.in + 3, 3);

	return 0;
}

/*
 * With some dummy bytes.
 *
 *  C A A A  X*  X D D D D
 * [C A A A  D*][C D D D D]
 */
static bool wpcm_fiu_dummy_match(const struct spi_mem_op *op)
{
	// Opcode 0x0b (FAST READ) is treated differently in hardware
	if (op->cmd.opcode == 0x0b)
		return false;

	return (op->addr.nbytes == 0 || op->addr.nbytes == 3) &&
	       op->dummy.nbytes >= 1 && op->dummy.nbytes <= 5 &&
	       op->data.nbytes <= 4;
}

static int wpcm_fiu_dummy_exec(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct wpcm_fiu_spi *fiu = spi_controller_get_devdata(mem->spi->controller);
	int cs = spi_get_chipselect(mem->spi, 0);

	wpcm_fiu_ects_assert(fiu, cs);

	/* First transfer */
	wpcm_fiu_set_opcode(fiu, op->cmd.opcode);
	wpcm_fiu_set_addr(fiu, op->addr.val);
	wpcm_fiu_do_uma(fiu, cs, op->addr.nbytes != 0, true, op->dummy.nbytes - 1);

	/* Second transfer */
	wpcm_fiu_set_opcode(fiu, 0);
	wpcm_fiu_set_addr(fiu, 0);
	wpcm_fiu_do_uma(fiu, cs, false, false, op->data.nbytes);
	wpcm_fiu_get_data(fiu, op->data.buf.in, op->data.nbytes);

	wpcm_fiu_ects_deassert(fiu, cs);

	return 0;
}

static const struct wpcm_fiu_op_shape wpcm_fiu_op_shapes[] = {
	{ .match = wpcm_fiu_normal_match, .exec = wpcm_fiu_normal_exec },
	{ .match = wpcm_fiu_fast_read_match, .exec = wpcm_fiu_fast_read_exec },
	{ .match = wpcm_fiu_4ba_match, .exec = wpcm_fiu_4ba_exec },
	{ .match = wpcm_fiu_rdid_match, .exec = wpcm_fiu_rdid_exec },
	{ .match = wpcm_fiu_dummy_match, .exec = wpcm_fiu_dummy_exec },
};

static const struct wpcm_fiu_op_shape *wpcm_fiu_find_op_shape(const struct spi_mem_op *op)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(wpcm_fiu_op_shapes); i++) {
		const struct wpcm_fiu_op_shape *shape = &wpcm_fiu_op_shapes[i];

		if (shape->match(op))
			return shape;
	}

	return NULL;
}

static bool wpcm_fiu_supports_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	if (op->cmd.dtr || op->addr.dtr || op->dummy.dtr || op->data.dtr)
		return false;

	if (op->cmd.buswidth > 1 || op->addr.buswidth > 1 ||
	    op->dummy.buswidth > 1 || op->data.buswidth > 1)
		return false;

	return wpcm_fiu_find_op_shape(op) != NULL;
}

/*
 * In order to ensure the integrity of SPI transfers performed via UMA,
 * temporarily disable (stall) memory accesses coming from the host CPU.
 */
static void wpcm_fiu_stall_host(struct wpcm_fiu_spi *fiu, bool stall)
{
	if (fiu->shm_regmap) {
		int res = regmap_update_bits(fiu->shm_regmap, SHM_FLASH_SIZE,
					     SHM_FLASH_SIZE_STALL_HOST,
					     stall ? SHM_FLASH_SIZE_STALL_HOST : 0);
		if (res)
			dev_warn(fiu->dev, "Failed to (un)stall host memory accesses: %d\n", res);
	}
}

static int wpcm_fiu_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct wpcm_fiu_spi *fiu = spi_controller_get_devdata(mem->spi->controller);
	const struct wpcm_fiu_op_shape *shape = wpcm_fiu_find_op_shape(op);

	wpcm_fiu_stall_host(fiu, true);

	if (shape)
		return shape->exec(mem, op);

	wpcm_fiu_stall_host(fiu, false);

	return -EOPNOTSUPP;
}

static int wpcm_fiu_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	if (op->data.nbytes > 4)
		op->data.nbytes = 4;

	return 0;
}

static int wpcm_fiu_dirmap_create(struct spi_mem_dirmap_desc *desc)
{
	struct wpcm_fiu_spi *fiu = spi_controller_get_devdata(desc->mem->spi->controller);
	int cs = spi_get_chipselect(desc->mem->spi, 0);

	if (desc->info.op_tmpl.data.dir != SPI_MEM_DATA_IN)
		return -EOPNOTSUPP;

	/*
	 * Unfortunately, FIU only supports a 16 MiB direct mapping window (per
	 * attached flash chip), but the SPI MEM core doesn't support partial
	 * direct mappings. This means that we can't support direct mapping on
	 * flashes that are bigger than 16 MiB.
	 */
	if (desc->info.offset + desc->info.length > MAX_MEMORY_SIZE_PER_CS)
		return -EINVAL;

	/* Don't read past the memory window */
	if (cs * MAX_MEMORY_SIZE_PER_CS + desc->info.offset + desc->info.length > fiu->memory_size)
		return -EINVAL;

	return 0;
}

static ssize_t wpcm_fiu_direct_read(struct spi_mem_dirmap_desc *desc, u64 offs, size_t len, void *buf)
{
	struct wpcm_fiu_spi *fiu = spi_controller_get_devdata(desc->mem->spi->controller);
	int cs = spi_get_chipselect(desc->mem->spi, 0);

	if (offs >= MAX_MEMORY_SIZE_PER_CS)
		return -ENOTSUPP;

	offs += cs * MAX_MEMORY_SIZE_PER_CS;

	if (!fiu->memory || offs >= fiu->memory_size)
		return -ENOTSUPP;

	len = min_t(size_t, len, fiu->memory_size - offs);
	memcpy_fromio(buf, fiu->memory + offs, len);

	return len;
}

static const struct spi_controller_mem_ops wpcm_fiu_mem_ops = {
	.adjust_op_size = wpcm_fiu_adjust_op_size,
	.supports_op = wpcm_fiu_supports_op,
	.exec_op = wpcm_fiu_exec_op,
	.dirmap_create = wpcm_fiu_dirmap_create,
	.dirmap_read = wpcm_fiu_direct_read,
};

static void wpcm_fiu_hw_init(struct wpcm_fiu_spi *fiu)
{
	/* Configure memory-mapped flash access */
	writeb(FIU_BURST_CFG_R16, fiu->regs + FIU_BURST_BFG);
	writeb(MAX_MEMORY_SIZE_TOTAL / (512 << 10), fiu->regs + FIU_CFG);
	writeb(MAX_MEMORY_SIZE_PER_CS / (512 << 10) | BIT(6), fiu->regs + FIU_SPI_FL_CFG);

	/* Deassert all manually asserted chip selects */
	writeb(0x0f, fiu->regs + FIU_UMA_ECTS);
}

static int wpcm_fiu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *ctrl;
	struct wpcm_fiu_spi *fiu;
	struct resource *res;

	ctrl = devm_spi_alloc_host(dev, sizeof(*fiu));
	if (!ctrl)
		return -ENOMEM;

	fiu = spi_controller_get_devdata(ctrl);
	fiu->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "control");
	fiu->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(fiu->regs)) {
		dev_err(dev, "Failed to map registers\n");
		return PTR_ERR(fiu->regs);
	}

	fiu->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(fiu->clk))
		return PTR_ERR(fiu->clk);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "memory");
	fiu->memory = devm_ioremap_resource(dev, res);
	fiu->memory_size = min_t(size_t, resource_size(res), MAX_MEMORY_SIZE_TOTAL);
	if (IS_ERR(fiu->memory)) {
		dev_err(dev, "Failed to map flash memory window\n");
		return PTR_ERR(fiu->memory);
	}

	fiu->shm_regmap = syscon_regmap_lookup_by_phandle_optional(dev->of_node, "nuvoton,shm");

	wpcm_fiu_hw_init(fiu);

	ctrl->bus_num = -1;
	ctrl->mem_ops = &wpcm_fiu_mem_ops;
	ctrl->num_chipselect = 4;
	ctrl->dev.of_node = dev->of_node;

	/*
	 * The FIU doesn't include a clock divider, the clock is entirely
	 * determined by the AHB3 bus clock.
	 */
	ctrl->min_speed_hz = clk_get_rate(fiu->clk);
	ctrl->max_speed_hz = clk_get_rate(fiu->clk);

	return devm_spi_register_controller(dev, ctrl);
}

static const struct of_device_id wpcm_fiu_dt_ids[] = {
	{ .compatible = "nuvoton,wpcm450-fiu", },
	{ }
};
MODULE_DEVICE_TABLE(of, wpcm_fiu_dt_ids);

static struct platform_driver wpcm_fiu_driver = {
	.driver = {
		.name	= "wpcm450-fiu",
		.bus	= &platform_bus_type,
		.of_match_table = wpcm_fiu_dt_ids,
	},
	.probe      = wpcm_fiu_probe,
};
module_platform_driver(wpcm_fiu_driver);

MODULE_DESCRIPTION("Nuvoton WPCM450 FIU SPI controller driver");
MODULE_AUTHOR("Jonathan Neuschäfer <j.neuschaefer@gmx.net>");
MODULE_LICENSE("GPL");
