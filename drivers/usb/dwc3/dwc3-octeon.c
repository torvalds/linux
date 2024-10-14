// SPDX-License-Identifier: GPL-2.0
/*
 * DWC3 glue for Cavium Octeon III SOCs.
 *
 * Copyright (C) 2010-2017 Cavium Networks
 * Copyright (C) 2023 RACOM s.r.o.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

/*
 * USB Control Register
 */
#define USBDRD_UCTL_CTL				0x00
/* BIST fast-clear mode select. A BIST run with this bit set
 * clears all entries in USBH RAMs to 0x0.
 */
# define USBDRD_UCTL_CTL_CLEAR_BIST		BIT_ULL(63)
/* 1 = Start BIST and cleared by hardware */
# define USBDRD_UCTL_CTL_START_BIST		BIT_ULL(62)
/* Reference clock select for SuperSpeed and HighSpeed PLLs:
 *	0x0 = Both PLLs use DLMC_REF_CLK0 for reference clock
 *	0x1 = Both PLLs use DLMC_REF_CLK1 for reference clock
 *	0x2 = SuperSpeed PLL uses DLMC_REF_CLK0 for reference clock &
 *	      HighSpeed PLL uses PLL_REF_CLK for reference clck
 *	0x3 = SuperSpeed PLL uses DLMC_REF_CLK1 for reference clock &
 *	      HighSpeed PLL uses PLL_REF_CLK for reference clck
 */
# define USBDRD_UCTL_CTL_REF_CLK_SEL		GENMASK_ULL(61, 60)
/* 1 = Spread-spectrum clock enable, 0 = SS clock disable */
# define USBDRD_UCTL_CTL_SSC_EN			BIT_ULL(59)
/* Spread-spectrum clock modulation range:
 *	0x0 = -4980 ppm downspread
 *	0x1 = -4492 ppm downspread
 *	0x2 = -4003 ppm downspread
 *	0x3 - 0x7 = Reserved
 */
# define USBDRD_UCTL_CTL_SSC_RANGE		GENMASK_ULL(58, 56)
/* Enable non-standard oscillator frequencies:
 *	[55:53] = modules -1
 *	[52:47] = 2's complement push amount, 0 = Feature disabled
 */
# define USBDRD_UCTL_CTL_SSC_REF_CLK_SEL	GENMASK_ULL(55, 47)
/* Reference clock multiplier for non-standard frequencies:
 *	0x19 = 100MHz on DLMC_REF_CLK* if REF_CLK_SEL = 0x0 or 0x1
 *	0x28 = 125MHz on DLMC_REF_CLK* if REF_CLK_SEL = 0x0 or 0x1
 *	0x32 =  50MHz on DLMC_REF_CLK* if REF_CLK_SEL = 0x0 or 0x1
 *	Other Values = Reserved
 */
# define USBDRD_UCTL_CTL_MPLL_MULTIPLIER	GENMASK_ULL(46, 40)
/* Enable reference clock to prescaler for SuperSpeed functionality.
 * Should always be set to "1"
 */
# define USBDRD_UCTL_CTL_REF_SSP_EN		BIT_ULL(39)
/* Divide the reference clock by 2 before entering the
 * REF_CLK_FSEL divider:
 *	If REF_CLK_SEL = 0x0 or 0x1, then only 0x0 is legal
 *	If REF_CLK_SEL = 0x2 or 0x3, then:
 *		0x1 = DLMC_REF_CLK* is 125MHz
 *		0x0 = DLMC_REF_CLK* is another supported frequency
 */
# define USBDRD_UCTL_CTL_REF_CLK_DIV2		BIT_ULL(38)
/* Select reference clock freqnuency for both PLL blocks:
 *	0x27 = REF_CLK_SEL is 0x0 or 0x1
 *	0x07 = REF_CLK_SEL is 0x2 or 0x3
 */
# define USBDRD_UCTL_CTL_REF_CLK_FSEL		GENMASK_ULL(37, 32)
/* Controller clock enable. */
# define USBDRD_UCTL_CTL_H_CLK_EN		BIT_ULL(30)
/* Select bypass input to controller clock divider:
 *	0x0 = Use divided coprocessor clock from H_CLKDIV
 *	0x1 = Use clock from GPIO pins
 */
# define USBDRD_UCTL_CTL_H_CLK_BYP_SEL		BIT_ULL(29)
/* Reset controller clock divider. */
# define USBDRD_UCTL_CTL_H_CLKDIV_RST		BIT_ULL(28)
/* Clock divider select:
 *	0x0 = divide by 1
 *	0x1 = divide by 2
 *	0x2 = divide by 4
 *	0x3 = divide by 6
 *	0x4 = divide by 8
 *	0x5 = divide by 16
 *	0x6 = divide by 24
 *	0x7 = divide by 32
 */
# define USBDRD_UCTL_CTL_H_CLKDIV_SEL		GENMASK_ULL(26, 24)
/* USB3 port permanently attached: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_USB3_PORT_PERM_ATTACH	BIT_ULL(21)
/* USB2 port permanently attached: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_USB2_PORT_PERM_ATTACH	BIT_ULL(20)
/* Disable SuperSpeed PHY: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_USB3_PORT_DISABLE	BIT_ULL(18)
/* Disable HighSpeed PHY: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_USB2_PORT_DISABLE	BIT_ULL(16)
/* Enable PHY SuperSpeed block power: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_SS_POWER_EN		BIT_ULL(14)
/* Enable PHY HighSpeed block power: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_HS_POWER_EN		BIT_ULL(12)
/* Enable USB UCTL interface clock: 0xx = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_CSCLK_EN		BIT_ULL(4)
/* Controller mode: 0x0 = Host, 0x1 = Device */
# define USBDRD_UCTL_CTL_DRD_MODE		BIT_ULL(3)
/* PHY reset */
# define USBDRD_UCTL_CTL_UPHY_RST		BIT_ULL(2)
/* Software reset UAHC */
# define USBDRD_UCTL_CTL_UAHC_RST		BIT_ULL(1)
/* Software resets UCTL */
# define USBDRD_UCTL_CTL_UCTL_RST		BIT_ULL(0)

#define USBDRD_UCTL_BIST_STATUS			0x08
#define USBDRD_UCTL_SPARE0			0x10
#define USBDRD_UCTL_INTSTAT			0x30
#define USBDRD_UCTL_PORT_CFG_HS(port)		(0x40 + (0x20 * port))
#define USBDRD_UCTL_PORT_CFG_SS(port)		(0x48 + (0x20 * port))
#define USBDRD_UCTL_PORT_CR_DBG_CFG(port)	(0x50 + (0x20 * port))
#define USBDRD_UCTL_PORT_CR_DBG_STATUS(port)	(0x58 + (0x20 * port))

/*
 * UCTL Configuration Register
 */
#define USBDRD_UCTL_HOST_CFG			0xe0
/* Indicates minimum value of all received BELT values */
# define USBDRD_UCTL_HOST_CFG_HOST_CURRENT_BELT	GENMASK_ULL(59, 48)
/* HS jitter adjustment */
# define USBDRD_UCTL_HOST_CFG_FLA		GENMASK_ULL(37, 32)
/* Bus-master enable: 0x0 = Disabled (stall DMAs), 0x1 = enabled */
# define USBDRD_UCTL_HOST_CFG_BME		BIT_ULL(28)
/* Overcurrent protection enable: 0x0 = unavailable, 0x1 = available */
# define USBDRD_UCTL_HOST_OCI_EN		BIT_ULL(27)
/* Overcurrent sene selection:
 *	0x0 = Overcurrent indication from off-chip is active-low
 *	0x1 = Overcurrent indication from off-chip is active-high
 */
# define USBDRD_UCTL_HOST_OCI_ACTIVE_HIGH_EN	BIT_ULL(26)
/* Port power control enable: 0x0 = unavailable, 0x1 = available */
# define USBDRD_UCTL_HOST_PPC_EN		BIT_ULL(25)
/* Port power control sense selection:
 *	0x0 = Port power to off-chip is active-low
 *	0x1 = Port power to off-chip is active-high
 */
# define USBDRD_UCTL_HOST_PPC_ACTIVE_HIGH_EN	BIT_ULL(24)

/*
 * UCTL Shim Features Register
 */
#define USBDRD_UCTL_SHIM_CFG			0xe8
/* Out-of-bound UAHC register access: 0 = read, 1 = write */
# define USBDRD_UCTL_SHIM_CFG_XS_NCB_OOB_WRN	BIT_ULL(63)
/* SRCID error log for out-of-bound UAHC register access:
 *	[59:58] = chipID
 *	[57] = Request source: 0 = core, 1 = NCB-device
 *	[56:51] = Core/NCB-device number, [56] always 0 for NCB devices
 *	[50:48] = SubID
 */
# define USBDRD_UCTL_SHIM_CFG_XS_NCB_OOB_OSRC	GENMASK_ULL(59, 48)
/* Error log for bad UAHC DMA access: 0 = Read log, 1 = Write log */
# define USBDRD_UCTL_SHIM_CFG_XM_BAD_DMA_WRN	BIT_ULL(47)
/* Encoded error type for bad UAHC DMA */
# define USBDRD_UCTL_SHIM_CFG_XM_BAD_DMA_TYPE	GENMASK_ULL(43, 40)
/* Select the IOI read command used by DMA accesses */
# define USBDRD_UCTL_SHIM_CFG_DMA_READ_CMD	BIT_ULL(12)
/* Select endian format for DMA accesses to the L2C:
 *	0x0 = Little endian
 *	0x1 = Big endian
 *	0x2 = Reserved
 *	0x3 = Reserved
 */
# define USBDRD_UCTL_SHIM_CFG_DMA_ENDIAN_MODE	GENMASK_ULL(9, 8)
/* Select endian format for IOI CSR access to UAHC:
 *	0x0 = Little endian
 *	0x1 = Big endian
 *	0x2 = Reserved
 *	0x3 = Reserved
 */
# define USBDRD_UCTL_SHIM_CFG_CSR_ENDIAN_MODE	GENMASK_ULL(1, 0)

#define USBDRD_UCTL_ECC				0xf0
#define USBDRD_UCTL_SPARE1			0xf8

struct dwc3_octeon {
	struct device *dev;
	void __iomem *base;
};

#define DWC3_GPIO_POWER_NONE	(-1)

#ifdef CONFIG_CAVIUM_OCTEON_SOC
#include <asm/octeon/octeon.h>
static inline uint64_t dwc3_octeon_readq(void __iomem *addr)
{
	return cvmx_readq_csr(addr);
}

static inline void dwc3_octeon_writeq(void __iomem *base, uint64_t val)
{
	cvmx_writeq_csr(base, val);
}

static void dwc3_octeon_config_gpio(int index, int gpio)
{
	union cvmx_gpio_bit_cfgx gpio_bit;

	if ((OCTEON_IS_MODEL(OCTEON_CN73XX) ||
	    OCTEON_IS_MODEL(OCTEON_CNF75XX))
	    && gpio <= 31) {
		gpio_bit.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(gpio));
		gpio_bit.s.tx_oe = 1;
		gpio_bit.s.output_sel = (index == 0 ? 0x14 : 0x15);
		cvmx_write_csr(CVMX_GPIO_BIT_CFGX(gpio), gpio_bit.u64);
	} else if (gpio <= 15) {
		gpio_bit.u64 = cvmx_read_csr(CVMX_GPIO_BIT_CFGX(gpio));
		gpio_bit.s.tx_oe = 1;
		gpio_bit.s.output_sel = (index == 0 ? 0x14 : 0x19);
		cvmx_write_csr(CVMX_GPIO_BIT_CFGX(gpio), gpio_bit.u64);
	} else {
		gpio_bit.u64 = cvmx_read_csr(CVMX_GPIO_XBIT_CFGX(gpio));
		gpio_bit.s.tx_oe = 1;
		gpio_bit.s.output_sel = (index == 0 ? 0x14 : 0x19);
		cvmx_write_csr(CVMX_GPIO_XBIT_CFGX(gpio), gpio_bit.u64);
	}
}
#else
static inline uint64_t dwc3_octeon_readq(void __iomem *addr)
{
	return 0;
}

static inline void dwc3_octeon_writeq(void __iomem *base, uint64_t val) { }

static inline void dwc3_octeon_config_gpio(int index, int gpio) { }

static uint64_t octeon_get_io_clock_rate(void)
{
	return 150000000;
}
#endif

static int dwc3_octeon_get_divider(void)
{
	static const uint8_t clk_div[] = { 1, 2, 4, 6, 8, 16, 24, 32 };
	int div = 0;

	while (div < ARRAY_SIZE(clk_div)) {
		uint64_t rate = octeon_get_io_clock_rate() / clk_div[div];
		if (rate <= 300000000 && rate >= 150000000)
			return div;
		div++;
	}

	return -EINVAL;
}

static int dwc3_octeon_setup(struct dwc3_octeon *octeon,
			     int ref_clk_sel, int ref_clk_fsel, int mpll_mul,
			     int power_gpio, int power_active_low)
{
	u64 val;
	int div;
	struct device *dev = octeon->dev;
	void __iomem *uctl_ctl_reg = octeon->base + USBDRD_UCTL_CTL;
	void __iomem *uctl_host_cfg_reg = octeon->base + USBDRD_UCTL_HOST_CFG;

	/*
	 * Step 1: Wait for all voltages to be stable...that surely
	 *         happened before starting the kernel. SKIP
	 */

	/* Step 2: Select GPIO for overcurrent indication, if desired. SKIP */

	/* Step 3: Assert all resets. */
	val = dwc3_octeon_readq(uctl_ctl_reg);
	val |= USBDRD_UCTL_CTL_UPHY_RST |
	       USBDRD_UCTL_CTL_UAHC_RST |
	       USBDRD_UCTL_CTL_UCTL_RST;
	dwc3_octeon_writeq(uctl_ctl_reg, val);

	/* Step 4a: Reset the clock dividers. */
	val = dwc3_octeon_readq(uctl_ctl_reg);
	val |= USBDRD_UCTL_CTL_H_CLKDIV_RST;
	dwc3_octeon_writeq(uctl_ctl_reg, val);

	/* Step 4b: Select controller clock frequency. */
	div = dwc3_octeon_get_divider();
	if (div < 0) {
		dev_err(dev, "clock divider invalid\n");
		return div;
	}
	val = dwc3_octeon_readq(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_H_CLKDIV_SEL;
	val |= FIELD_PREP(USBDRD_UCTL_CTL_H_CLKDIV_SEL, div);
	val |= USBDRD_UCTL_CTL_H_CLK_EN;
	dwc3_octeon_writeq(uctl_ctl_reg, val);
	val = dwc3_octeon_readq(uctl_ctl_reg);
	if ((div != FIELD_GET(USBDRD_UCTL_CTL_H_CLKDIV_SEL, val)) ||
	    (!(FIELD_GET(USBDRD_UCTL_CTL_H_CLK_EN, val)))) {
		dev_err(dev, "clock init failure (UCTL_CTL=%016llx)\n", val);
		return -EINVAL;
	}

	/* Step 4c: Deassert the controller clock divider reset. */
	val &= ~USBDRD_UCTL_CTL_H_CLKDIV_RST;
	dwc3_octeon_writeq(uctl_ctl_reg, val);

	/* Step 5a: Reference clock configuration. */
	val = dwc3_octeon_readq(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_REF_CLK_DIV2;
	val &= ~USBDRD_UCTL_CTL_REF_CLK_SEL;
	val |= FIELD_PREP(USBDRD_UCTL_CTL_REF_CLK_SEL, ref_clk_sel);

	val &= ~USBDRD_UCTL_CTL_REF_CLK_FSEL;
	val |= FIELD_PREP(USBDRD_UCTL_CTL_REF_CLK_FSEL, ref_clk_fsel);

	val &= ~USBDRD_UCTL_CTL_MPLL_MULTIPLIER;
	val |= FIELD_PREP(USBDRD_UCTL_CTL_MPLL_MULTIPLIER, mpll_mul);

	/* Step 5b: Configure and enable spread-spectrum for SuperSpeed. */
	val |= USBDRD_UCTL_CTL_SSC_EN;

	/* Step 5c: Enable SuperSpeed. */
	val |= USBDRD_UCTL_CTL_REF_SSP_EN;

	/* Step 5d: Configure PHYs. SKIP */

	/* Step 6a & 6b: Power up PHYs. */
	val |= USBDRD_UCTL_CTL_HS_POWER_EN;
	val |= USBDRD_UCTL_CTL_SS_POWER_EN;
	dwc3_octeon_writeq(uctl_ctl_reg, val);

	/* Step 7: Wait 10 controller-clock cycles to take effect. */
	udelay(10);

	/* Step 8a: Deassert UCTL reset signal. */
	val = dwc3_octeon_readq(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_UCTL_RST;
	dwc3_octeon_writeq(uctl_ctl_reg, val);

	/* Step 8b: Wait 10 controller-clock cycles. */
	udelay(10);

	/* Step 8c: Setup power control. */
	val = dwc3_octeon_readq(uctl_host_cfg_reg);
	val |= USBDRD_UCTL_HOST_PPC_EN;
	if (power_gpio == DWC3_GPIO_POWER_NONE) {
		val &= ~USBDRD_UCTL_HOST_PPC_EN;
	} else {
		val |= USBDRD_UCTL_HOST_PPC_EN;
		dwc3_octeon_config_gpio(((__force uintptr_t)octeon->base >> 24) & 1,
					power_gpio);
		dev_dbg(dev, "power control is using gpio%d\n", power_gpio);
	}
	if (power_active_low)
		val &= ~USBDRD_UCTL_HOST_PPC_ACTIVE_HIGH_EN;
	else
		val |= USBDRD_UCTL_HOST_PPC_ACTIVE_HIGH_EN;
	dwc3_octeon_writeq(uctl_host_cfg_reg, val);

	/* Step 8d: Deassert UAHC reset signal. */
	val = dwc3_octeon_readq(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_UAHC_RST;
	dwc3_octeon_writeq(uctl_ctl_reg, val);

	/* Step 8e: Wait 10 controller-clock cycles. */
	udelay(10);

	/* Step 9: Enable conditional coprocessor clock of UCTL. */
	val = dwc3_octeon_readq(uctl_ctl_reg);
	val |= USBDRD_UCTL_CTL_CSCLK_EN;
	dwc3_octeon_writeq(uctl_ctl_reg, val);

	/*Step 10: Set for host mode only. */
	val = dwc3_octeon_readq(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_DRD_MODE;
	dwc3_octeon_writeq(uctl_ctl_reg, val);

	return 0;
}

static void dwc3_octeon_set_endian_mode(struct dwc3_octeon *octeon)
{
	u64 val;
	void __iomem *uctl_shim_cfg_reg = octeon->base + USBDRD_UCTL_SHIM_CFG;

	val = dwc3_octeon_readq(uctl_shim_cfg_reg);
	val &= ~USBDRD_UCTL_SHIM_CFG_DMA_ENDIAN_MODE;
	val &= ~USBDRD_UCTL_SHIM_CFG_CSR_ENDIAN_MODE;
#ifdef __BIG_ENDIAN
	val |= FIELD_PREP(USBDRD_UCTL_SHIM_CFG_DMA_ENDIAN_MODE, 1);
	val |= FIELD_PREP(USBDRD_UCTL_SHIM_CFG_CSR_ENDIAN_MODE, 1);
#endif
	dwc3_octeon_writeq(uctl_shim_cfg_reg, val);
}

static void dwc3_octeon_phy_reset(struct dwc3_octeon *octeon)
{
	u64 val;
	void __iomem *uctl_ctl_reg = octeon->base + USBDRD_UCTL_CTL;

	val = dwc3_octeon_readq(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_UPHY_RST;
	dwc3_octeon_writeq(uctl_ctl_reg, val);
}

static int dwc3_octeon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct dwc3_octeon *octeon;
	const char *hs_clock_type, *ss_clock_type;
	int ref_clk_sel, ref_clk_fsel, mpll_mul;
	int power_active_low, power_gpio;
	int err, len;
	u32 clock_rate, gpio_pwr[3];

	if (of_property_read_u32(node, "refclk-frequency", &clock_rate)) {
		dev_err(dev, "No UCTL \"refclk-frequency\"\n");
		return -EINVAL;
	}
	if (of_property_read_string(node, "refclk-type-ss", &ss_clock_type)) {
		dev_err(dev, "No UCTL \"refclk-type-ss\"\n");
		return -EINVAL;
	}
	if (of_property_read_string(node, "refclk-type-hs", &hs_clock_type)) {
		dev_err(dev, "No UCTL \"refclk-type-hs\"\n");
		return -EINVAL;
	}

	ref_clk_sel = 2;
	if (strcmp("dlmc_ref_clk0", ss_clock_type) == 0) {
		if (strcmp(hs_clock_type, "dlmc_ref_clk0") == 0)
			ref_clk_sel = 0;
		else if (strcmp(hs_clock_type, "pll_ref_clk"))
			dev_warn(dev, "Invalid HS clock type %s, using pll_ref_clk instead\n",
				 hs_clock_type);
	} else if (strcmp(ss_clock_type, "dlmc_ref_clk1") == 0) {
		if (strcmp(hs_clock_type, "dlmc_ref_clk1") == 0) {
			ref_clk_sel = 1;
		} else {
			ref_clk_sel = 3;
			if (strcmp(hs_clock_type, "pll_ref_clk"))
				dev_warn(dev, "Invalid HS clock type %s, using pll_ref_clk instead\n",
					 hs_clock_type);
		}
	} else {
		dev_warn(dev, "Invalid SS clock type %s, using dlmc_ref_clk0 instead\n",
			 ss_clock_type);
	}

	ref_clk_fsel = 0x07;
	switch (clock_rate) {
	default:
		dev_warn(dev, "Invalid ref_clk %u, using 100000000 instead\n",
			 clock_rate);
		fallthrough;
	case 100000000:
		mpll_mul = 0x19;
		if (ref_clk_sel < 2)
			ref_clk_fsel = 0x27;
		break;
	case 50000000:
		mpll_mul = 0x32;
		break;
	case 125000000:
		mpll_mul = 0x28;
		break;
	}

	power_gpio = DWC3_GPIO_POWER_NONE;
	power_active_low = 0;
	len = of_property_read_variable_u32_array(node, "power", gpio_pwr, 2, 3);
	if (len > 0) {
		if (len == 3)
			power_active_low = gpio_pwr[2] & 0x01;
		power_gpio = gpio_pwr[1];
	}

	octeon = devm_kzalloc(dev, sizeof(*octeon), GFP_KERNEL);
	if (!octeon)
		return -ENOMEM;

	octeon->dev = dev;
	octeon->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(octeon->base))
		return PTR_ERR(octeon->base);

	err = dwc3_octeon_setup(octeon, ref_clk_sel, ref_clk_fsel, mpll_mul,
				power_gpio, power_active_low);
	if (err)
		return err;

	dwc3_octeon_set_endian_mode(octeon);
	dwc3_octeon_phy_reset(octeon);

	platform_set_drvdata(pdev, octeon);

	return of_platform_populate(node, NULL, NULL, dev);
}

static void dwc3_octeon_remove(struct platform_device *pdev)
{
	struct dwc3_octeon *octeon = platform_get_drvdata(pdev);

	of_platform_depopulate(octeon->dev);
}

static const struct of_device_id dwc3_octeon_of_match[] = {
	{ .compatible = "cavium,octeon-7130-usb-uctl" },
	{ },
};
MODULE_DEVICE_TABLE(of, dwc3_octeon_of_match);

static struct platform_driver dwc3_octeon_driver = {
	.probe		= dwc3_octeon_probe,
	.remove_new	= dwc3_octeon_remove,
	.driver		= {
		.name	= "dwc3-octeon",
		.of_match_table = dwc3_octeon_of_match,
	},
};
module_platform_driver(dwc3_octeon_driver);

MODULE_ALIAS("platform:dwc3-octeon");
MODULE_AUTHOR("Ladislav Michl <ladis@linux-mips.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 OCTEON III Glue Layer");
