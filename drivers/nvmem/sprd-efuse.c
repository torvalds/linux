// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Spreadtrum Communications Inc.

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define SPRD_EFUSE_ENABLE		0x20
#define SPRD_EFUSE_ERR_FLAG		0x24
#define SPRD_EFUSE_ERR_CLR		0x28
#define SPRD_EFUSE_MAGIC_NUM		0x2c
#define SPRD_EFUSE_FW_CFG		0x50
#define SPRD_EFUSE_PW_SWT		0x54
#define SPRD_EFUSE_MEM(val)		(0x1000 + ((val) << 2))

#define SPRD_EFUSE_VDD_EN		BIT(0)
#define SPRD_EFUSE_AUTO_CHECK_EN	BIT(1)
#define SPRD_EFUSE_DOUBLE_EN		BIT(2)
#define SPRD_EFUSE_MARGIN_RD_EN		BIT(3)
#define SPRD_EFUSE_LOCK_WR_EN		BIT(4)

#define SPRD_EFUSE_ERR_CLR_MASK		GENMASK(13, 0)

#define SPRD_EFUSE_ENK1_ON		BIT(0)
#define SPRD_EFUSE_ENK2_ON		BIT(1)
#define SPRD_EFUSE_PROG_EN		BIT(2)

#define SPRD_EFUSE_MAGIC_NUMBER		0x8810

/* Block width (bytes) definitions */
#define SPRD_EFUSE_BLOCK_WIDTH		4

/*
 * The Spreadtrum AP efuse contains 2 parts: normal efuse and secure efuse,
 * and we can only access the normal efuse in kernel. So define the normal
 * block offset index and normal block numbers.
 */
#define SPRD_EFUSE_NORMAL_BLOCK_NUMS	24
#define SPRD_EFUSE_NORMAL_BLOCK_OFFSET	72

/* Timeout (ms) for the trylock of hardware spinlocks */
#define SPRD_EFUSE_HWLOCK_TIMEOUT	5000

/*
 * Since different Spreadtrum SoC chip can have different normal block numbers
 * and offset. And some SoC can support block double feature, which means
 * when reading or writing data to efuse memory, the controller can save double
 * data in case one data become incorrect after a long period.
 *
 * Thus we should save them in the device data structure.
 */
struct sprd_efuse_variant_data {
	u32 blk_nums;
	u32 blk_offset;
	bool blk_double;
};

struct sprd_efuse {
	struct device *dev;
	struct clk *clk;
	struct hwspinlock *hwlock;
	struct mutex mutex;
	void __iomem *base;
	const struct sprd_efuse_variant_data *data;
};

static const struct sprd_efuse_variant_data ums312_data = {
	.blk_nums = SPRD_EFUSE_NORMAL_BLOCK_NUMS,
	.blk_offset = SPRD_EFUSE_NORMAL_BLOCK_OFFSET,
	.blk_double = false,
};

/*
 * On Spreadtrum platform, we have multi-subsystems will access the unique
 * efuse controller, so we need one hardware spinlock to synchronize between
 * the multiple subsystems.
 */
static int sprd_efuse_lock(struct sprd_efuse *efuse)
{
	int ret;

	mutex_lock(&efuse->mutex);

	ret = hwspin_lock_timeout_raw(efuse->hwlock,
				      SPRD_EFUSE_HWLOCK_TIMEOUT);
	if (ret) {
		dev_err(efuse->dev, "timeout get the hwspinlock\n");
		mutex_unlock(&efuse->mutex);
		return ret;
	}

	return 0;
}

static void sprd_efuse_unlock(struct sprd_efuse *efuse)
{
	hwspin_unlock_raw(efuse->hwlock);
	mutex_unlock(&efuse->mutex);
}

static void sprd_efuse_set_prog_power(struct sprd_efuse *efuse, bool en)
{
	u32 val = readl(efuse->base + SPRD_EFUSE_PW_SWT);

	if (en)
		val &= ~SPRD_EFUSE_ENK2_ON;
	else
		val &= ~SPRD_EFUSE_ENK1_ON;

	writel(val, efuse->base + SPRD_EFUSE_PW_SWT);

	/* Open or close efuse power need wait 1000us to make power stable. */
	usleep_range(1000, 1200);

	if (en)
		val |= SPRD_EFUSE_ENK1_ON;
	else
		val |= SPRD_EFUSE_ENK2_ON;

	writel(val, efuse->base + SPRD_EFUSE_PW_SWT);

	/* Open or close efuse power need wait 1000us to make power stable. */
	usleep_range(1000, 1200);
}

static void sprd_efuse_set_read_power(struct sprd_efuse *efuse, bool en)
{
	u32 val = readl(efuse->base + SPRD_EFUSE_ENABLE);

	if (en)
		val |= SPRD_EFUSE_VDD_EN;
	else
		val &= ~SPRD_EFUSE_VDD_EN;

	writel(val, efuse->base + SPRD_EFUSE_ENABLE);

	/* Open or close efuse power need wait 1000us to make power stable. */
	usleep_range(1000, 1200);
}

static void sprd_efuse_set_prog_lock(struct sprd_efuse *efuse, bool en)
{
	u32 val = readl(efuse->base + SPRD_EFUSE_ENABLE);

	if (en)
		val |= SPRD_EFUSE_LOCK_WR_EN;
	else
		val &= ~SPRD_EFUSE_LOCK_WR_EN;

	writel(val, efuse->base + SPRD_EFUSE_ENABLE);
}

static void sprd_efuse_set_auto_check(struct sprd_efuse *efuse, bool en)
{
	u32 val = readl(efuse->base + SPRD_EFUSE_ENABLE);

	if (en)
		val |= SPRD_EFUSE_AUTO_CHECK_EN;
	else
		val &= ~SPRD_EFUSE_AUTO_CHECK_EN;

	writel(val, efuse->base + SPRD_EFUSE_ENABLE);
}

static void sprd_efuse_set_data_double(struct sprd_efuse *efuse, bool en)
{
	u32 val = readl(efuse->base + SPRD_EFUSE_ENABLE);

	if (en)
		val |= SPRD_EFUSE_DOUBLE_EN;
	else
		val &= ~SPRD_EFUSE_DOUBLE_EN;

	writel(val, efuse->base + SPRD_EFUSE_ENABLE);
}

static void sprd_efuse_set_prog_en(struct sprd_efuse *efuse, bool en)
{
	u32 val = readl(efuse->base + SPRD_EFUSE_PW_SWT);

	if (en)
		val |= SPRD_EFUSE_PROG_EN;
	else
		val &= ~SPRD_EFUSE_PROG_EN;

	writel(val, efuse->base + SPRD_EFUSE_PW_SWT);
}

static int sprd_efuse_raw_prog(struct sprd_efuse *efuse, u32 blk, bool doub,
			       bool lock, u32 *data)
{
	u32 status;
	int ret = 0;

	/*
	 * We need set the correct magic number before writing the efuse to
	 * allow programming, and block other programming until we clear the
	 * magic number.
	 */
	writel(SPRD_EFUSE_MAGIC_NUMBER,
	       efuse->base + SPRD_EFUSE_MAGIC_NUM);

	/*
	 * Power on the efuse, enable programme and enable double data
	 * if asked.
	 */
	sprd_efuse_set_prog_power(efuse, true);
	sprd_efuse_set_prog_en(efuse, true);
	sprd_efuse_set_data_double(efuse, doub);

	/*
	 * Enable the auto-check function to validate if the programming is
	 * successful.
	 */
	sprd_efuse_set_auto_check(efuse, true);

	writel(*data, efuse->base + SPRD_EFUSE_MEM(blk));

	/* Disable auto-check and data double after programming */
	sprd_efuse_set_auto_check(efuse, false);
	sprd_efuse_set_data_double(efuse, false);

	/*
	 * Check the efuse error status, if the programming is successful,
	 * we should lock this efuse block to avoid programming again.
	 */
	status = readl(efuse->base + SPRD_EFUSE_ERR_FLAG);
	if (status) {
		dev_err(efuse->dev,
			"write error status %d of block %d\n", ret, blk);

		writel(SPRD_EFUSE_ERR_CLR_MASK,
		       efuse->base + SPRD_EFUSE_ERR_CLR);
		ret = -EBUSY;
	} else {
		sprd_efuse_set_prog_lock(efuse, lock);
		writel(0, efuse->base + SPRD_EFUSE_MEM(blk));
		sprd_efuse_set_prog_lock(efuse, false);
	}

	sprd_efuse_set_prog_power(efuse, false);
	writel(0, efuse->base + SPRD_EFUSE_MAGIC_NUM);

	return ret;
}

static int sprd_efuse_raw_read(struct sprd_efuse *efuse, int blk, u32 *val,
			       bool doub)
{
	u32 status;

	/*
	 * Need power on the efuse before reading data from efuse, and will
	 * power off the efuse after reading process.
	 */
	sprd_efuse_set_read_power(efuse, true);

	/* Enable double data if asked */
	sprd_efuse_set_data_double(efuse, doub);

	/* Start to read data from efuse block */
	*val = readl(efuse->base + SPRD_EFUSE_MEM(blk));

	/* Disable double data */
	sprd_efuse_set_data_double(efuse, false);

	/* Power off the efuse */
	sprd_efuse_set_read_power(efuse, false);

	/*
	 * Check the efuse error status and clear them if there are some
	 * errors occurred.
	 */
	status = readl(efuse->base + SPRD_EFUSE_ERR_FLAG);
	if (status) {
		dev_err(efuse->dev,
			"read error status %d of block %d\n", status, blk);

		writel(SPRD_EFUSE_ERR_CLR_MASK,
		       efuse->base + SPRD_EFUSE_ERR_CLR);
		return -EBUSY;
	}

	return 0;
}

static int sprd_efuse_read(void *context, u32 offset, void *val, size_t bytes)
{
	struct sprd_efuse *efuse = context;
	bool blk_double = efuse->data->blk_double;
	u32 index = offset / SPRD_EFUSE_BLOCK_WIDTH + efuse->data->blk_offset;
	u32 blk_offset = (offset % SPRD_EFUSE_BLOCK_WIDTH) * BITS_PER_BYTE;
	u32 data;
	int ret;

	ret = sprd_efuse_lock(efuse);
	if (ret)
		return ret;

	ret = clk_prepare_enable(efuse->clk);
	if (ret)
		goto unlock;

	ret = sprd_efuse_raw_read(efuse, index, &data, blk_double);
	if (!ret) {
		data >>= blk_offset;
		memcpy(val, &data, bytes);
	}

	clk_disable_unprepare(efuse->clk);

unlock:
	sprd_efuse_unlock(efuse);
	return ret;
}

static int sprd_efuse_write(void *context, u32 offset, void *val, size_t bytes)
{
	struct sprd_efuse *efuse = context;
	int ret;

	ret = sprd_efuse_lock(efuse);
	if (ret)
		return ret;

	ret = clk_prepare_enable(efuse->clk);
	if (ret)
		goto unlock;

	ret = sprd_efuse_raw_prog(efuse, offset, false, false, val);

	clk_disable_unprepare(efuse->clk);

unlock:
	sprd_efuse_unlock(efuse);
	return ret;
}

static int sprd_efuse_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct nvmem_device *nvmem;
	struct nvmem_config econfig = { };
	struct sprd_efuse *efuse;
	const struct sprd_efuse_variant_data *pdata;
	int ret;

	pdata = of_device_get_match_data(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "No matching driver data found\n");
		return -EINVAL;
	}

	efuse = devm_kzalloc(&pdev->dev, sizeof(*efuse), GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	efuse->base = devm_platform_ioremap_resource(pdev, 0);
	if (!efuse->base)
		return -ENOMEM;

	ret = of_hwspin_lock_get_id(np, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get hwlock id\n");
		return ret;
	}

	efuse->hwlock = devm_hwspin_lock_request_specific(&pdev->dev, ret);
	if (!efuse->hwlock) {
		dev_err(&pdev->dev, "failed to request hwlock\n");
		return -ENXIO;
	}

	efuse->clk = devm_clk_get(&pdev->dev, "enable");
	if (IS_ERR(efuse->clk)) {
		dev_err(&pdev->dev, "failed to get enable clock\n");
		return PTR_ERR(efuse->clk);
	}

	mutex_init(&efuse->mutex);
	efuse->dev = &pdev->dev;
	efuse->data = pdata;

	econfig.stride = 1;
	econfig.word_size = 1;
	econfig.read_only = false;
	econfig.name = "sprd-efuse";
	econfig.size = efuse->data->blk_nums * SPRD_EFUSE_BLOCK_WIDTH;
	econfig.reg_read = sprd_efuse_read;
	econfig.reg_write = sprd_efuse_write;
	econfig.priv = efuse;
	econfig.dev = &pdev->dev;
	nvmem = devm_nvmem_register(&pdev->dev, &econfig);
	if (IS_ERR(nvmem)) {
		dev_err(&pdev->dev, "failed to register nvmem\n");
		return PTR_ERR(nvmem);
	}

	return 0;
}

static const struct of_device_id sprd_efuse_of_match[] = {
	{ .compatible = "sprd,ums312-efuse", .data = &ums312_data },
	{ }
};

static struct platform_driver sprd_efuse_driver = {
	.probe = sprd_efuse_probe,
	.driver = {
		.name = "sprd-efuse",
		.of_match_table = sprd_efuse_of_match,
	},
};

module_platform_driver(sprd_efuse_driver);

MODULE_AUTHOR("Freeman Liu <freeman.liu@spreadtrum.com>");
MODULE_DESCRIPTION("Spreadtrum AP efuse driver");
MODULE_LICENSE("GPL v2");
