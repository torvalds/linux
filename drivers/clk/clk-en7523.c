// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <dt-bindings/clock/en7523-clk.h>
#include <dt-bindings/reset/airoha,en7581-reset.h>

#define RST_NR_PER_BANK			32

#define REG_PCI_CONTROL			0x88
#define   REG_PCI_CONTROL_PERSTOUT	BIT(29)
#define   REG_PCI_CONTROL_PERSTOUT1	BIT(26)
#define   REG_PCI_CONTROL_REFCLK_EN0	BIT(23)
#define   REG_PCI_CONTROL_REFCLK_EN1	BIT(22)
#define   REG_PCI_CONTROL_PERSTOUT2	BIT(16)
#define REG_GSW_CLK_DIV_SEL		0x1b4
#define REG_EMI_CLK_DIV_SEL		0x1b8
#define REG_BUS_CLK_DIV_SEL		0x1bc
#define REG_SPI_CLK_DIV_SEL		0x1c4
#define REG_SPI_CLK_FREQ_SEL		0x1c8
#define REG_NPU_CLK_DIV_SEL		0x1fc
#define REG_CRYPTO_CLKSRC		0x200
#define REG_RESET_CONTROL2		0x830
#define   REG_RESET2_CONTROL_PCIE2	BIT(27)
#define REG_RESET_CONTROL1		0x834
#define   REG_RESET_CONTROL_PCIEHB	BIT(29)
#define   REG_RESET_CONTROL_PCIE1	BIT(27)
#define   REG_RESET_CONTROL_PCIE2	BIT(26)
/* EN7581 */
#define REG_NP_SCU_PCIC			0x88
#define REG_NP_SCU_SSTR			0x9c
#define REG_PCIE_XSI0_SEL_MASK		GENMASK(14, 13)
#define REG_PCIE_XSI1_SEL_MASK		GENMASK(12, 11)
#define REG_CRYPTO_CLKSRC2		0x20c

#define REG_RST_CTRL2			0x830
#define REG_RST_CTRL1			0x834

struct en_clk_desc {
	int id;
	const char *name;
	u32 base_reg;
	u8 base_bits;
	u8 base_shift;
	union {
		const unsigned int *base_values;
		unsigned int base_value;
	};
	size_t n_base_values;

	u16 div_reg;
	u8 div_bits;
	u8 div_shift;
	u16 div_val0;
	u8 div_step;
	u8 div_offset;
};

struct en_clk_gate {
	void __iomem *base;
	struct clk_hw hw;
};

struct en_rst_data {
	const u16 *bank_ofs;
	const u16 *idx_map;
	void __iomem *base;
	struct reset_controller_dev rcdev;
};

struct en_clk_soc_data {
	u32 num_clocks;
	const struct clk_ops pcie_ops;
	int (*hw_init)(struct platform_device *pdev,
		       struct clk_hw_onecell_data *clk_data);
};

static const u32 gsw_base[] = { 400000000, 500000000 };
static const u32 emi_base[] = { 333000000, 400000000 };
static const u32 bus_base[] = { 500000000, 540000000 };
static const u32 slic_base[] = { 100000000, 3125000 };
static const u32 npu_base[] = { 333000000, 400000000, 500000000 };
/* EN7581 */
static const u32 emi7581_base[] = { 540000000, 480000000, 400000000, 300000000 };
static const u32 bus7581_base[] = { 600000000, 540000000 };
static const u32 npu7581_base[] = { 800000000, 750000000, 720000000, 600000000 };
static const u32 crypto_base[] = { 540000000, 480000000 };
static const u32 emmc7581_base[] = { 200000000, 150000000 };

static const struct en_clk_desc en7523_base_clks[] = {
	{
		.id = EN7523_CLK_GSW,
		.name = "gsw",

		.base_reg = REG_GSW_CLK_DIV_SEL,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = gsw_base,
		.n_base_values = ARRAY_SIZE(gsw_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
		.div_offset = 1,
	}, {
		.id = EN7523_CLK_EMI,
		.name = "emi",

		.base_reg = REG_EMI_CLK_DIV_SEL,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = emi_base,
		.n_base_values = ARRAY_SIZE(emi_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
		.div_offset = 1,
	}, {
		.id = EN7523_CLK_BUS,
		.name = "bus",

		.base_reg = REG_BUS_CLK_DIV_SEL,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = bus_base,
		.n_base_values = ARRAY_SIZE(bus_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
		.div_offset = 1,
	}, {
		.id = EN7523_CLK_SLIC,
		.name = "slic",

		.base_reg = REG_SPI_CLK_FREQ_SEL,
		.base_bits = 1,
		.base_shift = 0,
		.base_values = slic_base,
		.n_base_values = ARRAY_SIZE(slic_base),

		.div_reg = REG_SPI_CLK_DIV_SEL,
		.div_bits = 5,
		.div_shift = 24,
		.div_val0 = 20,
		.div_step = 2,
	}, {
		.id = EN7523_CLK_SPI,
		.name = "spi",

		.base_reg = REG_SPI_CLK_DIV_SEL,

		.base_value = 400000000,

		.div_bits = 5,
		.div_shift = 8,
		.div_val0 = 40,
		.div_step = 2,
	}, {
		.id = EN7523_CLK_NPU,
		.name = "npu",

		.base_reg = REG_NPU_CLK_DIV_SEL,
		.base_bits = 2,
		.base_shift = 8,
		.base_values = npu_base,
		.n_base_values = ARRAY_SIZE(npu_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
		.div_offset = 1,
	}, {
		.id = EN7523_CLK_CRYPTO,
		.name = "crypto",

		.base_reg = REG_CRYPTO_CLKSRC,
		.base_bits = 1,
		.base_shift = 0,
		.base_values = emi_base,
		.n_base_values = ARRAY_SIZE(emi_base),
	}
};

static const struct en_clk_desc en7581_base_clks[] = {
	{
		.id = EN7523_CLK_GSW,
		.name = "gsw",

		.base_reg = REG_GSW_CLK_DIV_SEL,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = gsw_base,
		.n_base_values = ARRAY_SIZE(gsw_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
		.div_offset = 1,
	}, {
		.id = EN7523_CLK_EMI,
		.name = "emi",

		.base_reg = REG_EMI_CLK_DIV_SEL,
		.base_bits = 2,
		.base_shift = 8,
		.base_values = emi7581_base,
		.n_base_values = ARRAY_SIZE(emi7581_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
		.div_offset = 1,
	}, {
		.id = EN7523_CLK_BUS,
		.name = "bus",

		.base_reg = REG_BUS_CLK_DIV_SEL,
		.base_bits = 1,
		.base_shift = 8,
		.base_values = bus7581_base,
		.n_base_values = ARRAY_SIZE(bus7581_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
		.div_offset = 1,
	}, {
		.id = EN7523_CLK_SLIC,
		.name = "slic",

		.base_reg = REG_SPI_CLK_FREQ_SEL,
		.base_bits = 1,
		.base_shift = 0,
		.base_values = slic_base,
		.n_base_values = ARRAY_SIZE(slic_base),

		.div_reg = REG_SPI_CLK_DIV_SEL,
		.div_bits = 5,
		.div_shift = 24,
		.div_val0 = 20,
		.div_step = 2,
	}, {
		.id = EN7523_CLK_SPI,
		.name = "spi",

		.base_reg = REG_SPI_CLK_DIV_SEL,

		.base_value = 400000000,

		.div_bits = 5,
		.div_shift = 8,
		.div_val0 = 40,
		.div_step = 2,
	}, {
		.id = EN7523_CLK_NPU,
		.name = "npu",

		.base_reg = REG_NPU_CLK_DIV_SEL,
		.base_bits = 2,
		.base_shift = 8,
		.base_values = npu7581_base,
		.n_base_values = ARRAY_SIZE(npu7581_base),

		.div_bits = 3,
		.div_shift = 0,
		.div_step = 1,
		.div_offset = 1,
	}, {
		.id = EN7523_CLK_CRYPTO,
		.name = "crypto",

		.base_reg = REG_CRYPTO_CLKSRC2,
		.base_bits = 1,
		.base_shift = 0,
		.base_values = crypto_base,
		.n_base_values = ARRAY_SIZE(crypto_base),
	}, {
		.id = EN7581_CLK_EMMC,
		.name = "emmc",

		.base_reg = REG_CRYPTO_CLKSRC2,
		.base_bits = 1,
		.base_shift = 12,
		.base_values = emmc7581_base,
		.n_base_values = ARRAY_SIZE(emmc7581_base),
	}
};

static const u16 en7581_rst_ofs[] = {
	REG_RST_CTRL2,
	REG_RST_CTRL1,
};

static const u16 en7581_rst_map[] = {
	/* RST_CTRL2 */
	[EN7581_XPON_PHY_RST]		= 0,
	[EN7581_CPU_TIMER2_RST]		= 2,
	[EN7581_HSUART_RST]		= 3,
	[EN7581_UART4_RST]		= 4,
	[EN7581_UART5_RST]		= 5,
	[EN7581_I2C2_RST]		= 6,
	[EN7581_XSI_MAC_RST]		= 7,
	[EN7581_XSI_PHY_RST]		= 8,
	[EN7581_NPU_RST]		= 9,
	[EN7581_I2S_RST]		= 10,
	[EN7581_TRNG_RST]		= 11,
	[EN7581_TRNG_MSTART_RST]	= 12,
	[EN7581_DUAL_HSI0_RST]		= 13,
	[EN7581_DUAL_HSI1_RST]		= 14,
	[EN7581_HSI_RST]		= 15,
	[EN7581_DUAL_HSI0_MAC_RST]	= 16,
	[EN7581_DUAL_HSI1_MAC_RST]	= 17,
	[EN7581_HSI_MAC_RST]		= 18,
	[EN7581_WDMA_RST]		= 19,
	[EN7581_WOE0_RST]		= 20,
	[EN7581_WOE1_RST]		= 21,
	[EN7581_HSDMA_RST]		= 22,
	[EN7581_TDMA_RST]		= 24,
	[EN7581_EMMC_RST]		= 25,
	[EN7581_SOE_RST]		= 26,
	[EN7581_PCIE2_RST]		= 27,
	[EN7581_XFP_MAC_RST]		= 28,
	[EN7581_USB_HOST_P1_RST]	= 29,
	[EN7581_USB_HOST_P1_U3_PHY_RST]	= 30,
	/* RST_CTRL1 */
	[EN7581_PCM1_ZSI_ISI_RST]	= RST_NR_PER_BANK + 0,
	[EN7581_FE_PDMA_RST]		= RST_NR_PER_BANK + 1,
	[EN7581_FE_QDMA_RST]		= RST_NR_PER_BANK + 2,
	[EN7581_PCM_SPIWP_RST]		= RST_NR_PER_BANK + 4,
	[EN7581_CRYPTO_RST]		= RST_NR_PER_BANK + 6,
	[EN7581_TIMER_RST]		= RST_NR_PER_BANK + 8,
	[EN7581_PCM1_RST]		= RST_NR_PER_BANK + 11,
	[EN7581_UART_RST]		= RST_NR_PER_BANK + 12,
	[EN7581_GPIO_RST]		= RST_NR_PER_BANK + 13,
	[EN7581_GDMA_RST]		= RST_NR_PER_BANK + 14,
	[EN7581_I2C_MASTER_RST]		= RST_NR_PER_BANK + 16,
	[EN7581_PCM2_ZSI_ISI_RST]	= RST_NR_PER_BANK + 17,
	[EN7581_SFC_RST]		= RST_NR_PER_BANK + 18,
	[EN7581_UART2_RST]		= RST_NR_PER_BANK + 19,
	[EN7581_GDMP_RST]		= RST_NR_PER_BANK + 20,
	[EN7581_FE_RST]			= RST_NR_PER_BANK + 21,
	[EN7581_USB_HOST_P0_RST]	= RST_NR_PER_BANK + 22,
	[EN7581_GSW_RST]		= RST_NR_PER_BANK + 23,
	[EN7581_SFC2_PCM_RST]		= RST_NR_PER_BANK + 25,
	[EN7581_PCIE0_RST]		= RST_NR_PER_BANK + 26,
	[EN7581_PCIE1_RST]		= RST_NR_PER_BANK + 27,
	[EN7581_CPU_TIMER_RST]		= RST_NR_PER_BANK + 28,
	[EN7581_PCIE_HB_RST]		= RST_NR_PER_BANK + 29,
	[EN7581_XPON_MAC_RST]		= RST_NR_PER_BANK + 31,
};

static u32 en7523_get_base_rate(const struct en_clk_desc *desc, u32 val)
{
	if (!desc->base_bits)
		return desc->base_value;

	val >>= desc->base_shift;
	val &= (1 << desc->base_bits) - 1;

	if (val >= desc->n_base_values)
		return 0;

	return desc->base_values[val];
}

static u32 en7523_get_div(const struct en_clk_desc *desc, u32 val)
{
	if (!desc->div_bits)
		return 1;

	val >>= desc->div_shift;
	val &= (1 << desc->div_bits) - 1;

	if (!val && desc->div_val0)
		return desc->div_val0;

	return (val + desc->div_offset) * desc->div_step;
}

static int en7523_pci_is_enabled(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);

	return !!(readl(cg->base + REG_PCI_CONTROL) & REG_PCI_CONTROL_REFCLK_EN1);
}

static int en7523_pci_prepare(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);
	void __iomem *np_base = cg->base;
	u32 val, mask;

	/* Need to pull device low before reset */
	val = readl(np_base + REG_PCI_CONTROL);
	val &= ~(REG_PCI_CONTROL_PERSTOUT1 | REG_PCI_CONTROL_PERSTOUT);
	writel(val, np_base + REG_PCI_CONTROL);
	usleep_range(1000, 2000);

	/* Enable PCIe port 1 */
	val |= REG_PCI_CONTROL_REFCLK_EN1;
	writel(val, np_base + REG_PCI_CONTROL);
	usleep_range(1000, 2000);

	/* Reset to default */
	val = readl(np_base + REG_RESET_CONTROL1);
	mask = REG_RESET_CONTROL_PCIE1 | REG_RESET_CONTROL_PCIE2 |
	       REG_RESET_CONTROL_PCIEHB;
	writel(val & ~mask, np_base + REG_RESET_CONTROL1);
	usleep_range(1000, 2000);
	writel(val | mask, np_base + REG_RESET_CONTROL1);
	msleep(100);
	writel(val & ~mask, np_base + REG_RESET_CONTROL1);
	usleep_range(5000, 10000);

	/* Release device */
	mask = REG_PCI_CONTROL_PERSTOUT1 | REG_PCI_CONTROL_PERSTOUT;
	val = readl(np_base + REG_PCI_CONTROL);
	writel(val & ~mask, np_base + REG_PCI_CONTROL);
	usleep_range(1000, 2000);
	writel(val | mask, np_base + REG_PCI_CONTROL);
	msleep(250);

	return 0;
}

static void en7523_pci_unprepare(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);
	void __iomem *np_base = cg->base;
	u32 val;

	val = readl(np_base + REG_PCI_CONTROL);
	val &= ~REG_PCI_CONTROL_REFCLK_EN1;
	writel(val, np_base + REG_PCI_CONTROL);
}

static struct clk_hw *en7523_register_pcie_clk(struct device *dev,
					       void __iomem *np_base)
{
	const struct en_clk_soc_data *soc_data = device_get_match_data(dev);
	struct clk_init_data init = {
		.name = "pcie",
		.ops = &soc_data->pcie_ops,
	};
	struct en_clk_gate *cg;

	cg = devm_kzalloc(dev, sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return NULL;

	cg->base = np_base;
	cg->hw.init = &init;

	if (init.ops->unprepare)
		init.ops->unprepare(&cg->hw);

	if (clk_hw_register(dev, &cg->hw))
		return NULL;

	return &cg->hw;
}

static int en7581_pci_is_enabled(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);
	u32 val, mask;

	mask = REG_PCI_CONTROL_REFCLK_EN0 | REG_PCI_CONTROL_REFCLK_EN1;
	val = readl(cg->base + REG_PCI_CONTROL);
	return (val & mask) == mask;
}

static int en7581_pci_enable(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);
	void __iomem *np_base = cg->base;
	u32 val, mask;

	mask = REG_PCI_CONTROL_REFCLK_EN0 | REG_PCI_CONTROL_REFCLK_EN1 |
	       REG_PCI_CONTROL_PERSTOUT1 | REG_PCI_CONTROL_PERSTOUT2 |
	       REG_PCI_CONTROL_PERSTOUT;
	val = readl(np_base + REG_PCI_CONTROL);
	writel(val | mask, np_base + REG_PCI_CONTROL);

	return 0;
}

static void en7581_pci_disable(struct clk_hw *hw)
{
	struct en_clk_gate *cg = container_of(hw, struct en_clk_gate, hw);
	void __iomem *np_base = cg->base;
	u32 val, mask;

	mask = REG_PCI_CONTROL_REFCLK_EN0 | REG_PCI_CONTROL_REFCLK_EN1 |
	       REG_PCI_CONTROL_PERSTOUT1 | REG_PCI_CONTROL_PERSTOUT2 |
	       REG_PCI_CONTROL_PERSTOUT;
	val = readl(np_base + REG_PCI_CONTROL);
	writel(val & ~mask, np_base + REG_PCI_CONTROL);
	usleep_range(1000, 2000);
}

static void en7523_register_clocks(struct device *dev, struct clk_hw_onecell_data *clk_data,
				   void __iomem *base, void __iomem *np_base)
{
	struct clk_hw *hw;
	u32 rate;
	int i;

	for (i = 0; i < ARRAY_SIZE(en7523_base_clks); i++) {
		const struct en_clk_desc *desc = &en7523_base_clks[i];
		u32 reg = desc->div_reg ? desc->div_reg : desc->base_reg;
		u32 val = readl(base + desc->base_reg);

		rate = en7523_get_base_rate(desc, val);
		val = readl(base + reg);
		rate /= en7523_get_div(desc, val);

		hw = clk_hw_register_fixed_rate(dev, desc->name, NULL, 0, rate);
		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %ld\n",
			       desc->name, PTR_ERR(hw));
			continue;
		}

		clk_data->hws[desc->id] = hw;
	}

	hw = en7523_register_pcie_clk(dev, np_base);
	clk_data->hws[EN7523_CLK_PCIE] = hw;
}

static int en7523_clk_hw_init(struct platform_device *pdev,
			      struct clk_hw_onecell_data *clk_data)
{
	void __iomem *base, *np_base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	np_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(np_base))
		return PTR_ERR(np_base);

	en7523_register_clocks(&pdev->dev, clk_data, base, np_base);

	return 0;
}

static void en7581_register_clocks(struct device *dev, struct clk_hw_onecell_data *clk_data,
				   struct regmap *map, void __iomem *base)
{
	struct clk_hw *hw;
	u32 rate;
	int i;

	for (i = 0; i < ARRAY_SIZE(en7581_base_clks); i++) {
		const struct en_clk_desc *desc = &en7581_base_clks[i];
		u32 val, reg = desc->div_reg ? desc->div_reg : desc->base_reg;
		int err;

		err = regmap_read(map, desc->base_reg, &val);
		if (err) {
			pr_err("Failed reading fixed clk rate %s: %d\n",
			       desc->name, err);
			continue;
		}
		rate = en7523_get_base_rate(desc, val);

		err = regmap_read(map, reg, &val);
		if (err) {
			pr_err("Failed reading fixed clk div %s: %d\n",
			       desc->name, err);
			continue;
		}
		rate /= en7523_get_div(desc, val);

		hw = clk_hw_register_fixed_rate(dev, desc->name, NULL, 0, rate);
		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %ld\n",
			       desc->name, PTR_ERR(hw));
			continue;
		}

		clk_data->hws[desc->id] = hw;
	}

	hw = en7523_register_pcie_clk(dev, base);
	clk_data->hws[EN7523_CLK_PCIE] = hw;
}

static int en7523_reset_update(struct reset_controller_dev *rcdev,
			       unsigned long id, bool assert)
{
	struct en_rst_data *rst_data = container_of(rcdev, struct en_rst_data, rcdev);
	void __iomem *addr = rst_data->base + rst_data->bank_ofs[id / RST_NR_PER_BANK];
	u32 val;

	val = readl(addr);
	if (assert)
		val |= BIT(id % RST_NR_PER_BANK);
	else
		val &= ~BIT(id % RST_NR_PER_BANK);
	writel(val, addr);

	return 0;
}

static int en7523_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return en7523_reset_update(rcdev, id, true);
}

static int en7523_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return en7523_reset_update(rcdev, id, false);
}

static int en7523_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct en_rst_data *rst_data = container_of(rcdev, struct en_rst_data, rcdev);
	void __iomem *addr = rst_data->base + rst_data->bank_ofs[id / RST_NR_PER_BANK];

	return !!(readl(addr) & BIT(id % RST_NR_PER_BANK));
}

static int en7523_reset_xlate(struct reset_controller_dev *rcdev,
			      const struct of_phandle_args *reset_spec)
{
	struct en_rst_data *rst_data = container_of(rcdev, struct en_rst_data, rcdev);

	if (reset_spec->args[0] >= rcdev->nr_resets)
		return -EINVAL;

	return rst_data->idx_map[reset_spec->args[0]];
}

static const struct reset_control_ops en7581_reset_ops = {
	.assert = en7523_reset_assert,
	.deassert = en7523_reset_deassert,
	.status = en7523_reset_status,
};

static int en7581_reset_register(struct device *dev, void __iomem *base)
{
	struct en_rst_data *rst_data;

	rst_data = devm_kzalloc(dev, sizeof(*rst_data), GFP_KERNEL);
	if (!rst_data)
		return -ENOMEM;

	rst_data->bank_ofs = en7581_rst_ofs;
	rst_data->idx_map = en7581_rst_map;
	rst_data->base = base;

	rst_data->rcdev.nr_resets = ARRAY_SIZE(en7581_rst_map);
	rst_data->rcdev.of_xlate = en7523_reset_xlate;
	rst_data->rcdev.ops = &en7581_reset_ops;
	rst_data->rcdev.of_node = dev->of_node;
	rst_data->rcdev.of_reset_n_cells = 1;
	rst_data->rcdev.owner = THIS_MODULE;
	rst_data->rcdev.dev = dev;

	return devm_reset_controller_register(dev, &rst_data->rcdev);
}

static int en7581_clk_hw_init(struct platform_device *pdev,
			      struct clk_hw_onecell_data *clk_data)
{
	struct regmap *map;
	void __iomem *base;
	u32 val;

	map = syscon_regmap_lookup_by_compatible("airoha,en7581-chip-scu");
	if (IS_ERR(map))
		return PTR_ERR(map);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	en7581_register_clocks(&pdev->dev, clk_data, map, base);

	val = readl(base + REG_NP_SCU_SSTR);
	val &= ~(REG_PCIE_XSI0_SEL_MASK | REG_PCIE_XSI1_SEL_MASK);
	writel(val, base + REG_NP_SCU_SSTR);
	val = readl(base + REG_NP_SCU_PCIC);
	writel(val | 3, base + REG_NP_SCU_PCIC);

	return en7581_reset_register(&pdev->dev, base);
}

static int en7523_clk_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct en_clk_soc_data *soc_data;
	struct clk_hw_onecell_data *clk_data;
	int r;

	soc_data = device_get_match_data(&pdev->dev);

	clk_data = devm_kzalloc(&pdev->dev,
				struct_size(clk_data, hws, soc_data->num_clocks),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = soc_data->num_clocks;
	r = soc_data->hw_init(pdev, clk_data);
	if (r)
		return r;

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
}

static const struct en_clk_soc_data en7523_data = {
	.num_clocks = ARRAY_SIZE(en7523_base_clks) + 1,
	.pcie_ops = {
		.is_enabled = en7523_pci_is_enabled,
		.prepare = en7523_pci_prepare,
		.unprepare = en7523_pci_unprepare,
	},
	.hw_init = en7523_clk_hw_init,
};

static const struct en_clk_soc_data en7581_data = {
	/* We increment num_clocks by 1 to account for additional PCIe clock */
	.num_clocks = ARRAY_SIZE(en7581_base_clks) + 1,
	.pcie_ops = {
		.is_enabled = en7581_pci_is_enabled,
		.enable = en7581_pci_enable,
		.disable = en7581_pci_disable,
	},
	.hw_init = en7581_clk_hw_init,
};

static const struct of_device_id of_match_clk_en7523[] = {
	{ .compatible = "airoha,en7523-scu", .data = &en7523_data },
	{ .compatible = "airoha,en7581-scu", .data = &en7581_data },
	{ /* sentinel */ }
};

static struct platform_driver clk_en7523_drv = {
	.probe = en7523_clk_probe,
	.driver = {
		.name = "clk-en7523",
		.of_match_table = of_match_clk_en7523,
		.suppress_bind_attrs = true,
	},
};

static int __init clk_en7523_init(void)
{
	return platform_driver_register(&clk_en7523_drv);
}

arch_initcall(clk_en7523_init);
