/*
 * XHCI HCD glue for Cavium Octeon III SOCs.
 *
 * Copyright (C) 2010-2017 Cavium Networks
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>

#include <asm/octeon/octeon.h>

/*
 * USB Control Register
 */
#define USBDRD_UCTL_CTL				0x00
/* BIST fast-clear mode select. A BIST run with this bit set
 * clears all entries in USBH RAMs to 0x0.
 */
# define USBDRD_UCTL_CTL_CLEAR_BIST		BIT(63)
/* 1 = Start BIST and cleared by hardware */
# define USBDRD_UCTL_CTL_START_BIST		BIT(62)
/* Reference clock select for SuperSpeed and HighSpeed PLLs:
 *	0x0 = Both PLLs use DLMC_REF_CLK0 for reference clock
 *	0x1 = Both PLLs use DLMC_REF_CLK1 for reference clock
 *	0x2 = SuperSpeed PLL uses DLMC_REF_CLK0 for reference clock &
 *	      HighSpeed PLL uses PLL_REF_CLK for reference clck
 *	0x3 = SuperSpeed PLL uses DLMC_REF_CLK1 for reference clock &
 *	      HighSpeed PLL uses PLL_REF_CLK for reference clck
 */
# define USBDRD_UCTL_CTL_REF_CLK_SEL		GENMASK(61, 60)
/* 1 = Spread-spectrum clock enable, 0 = SS clock disable */
# define USBDRD_UCTL_CTL_SSC_EN			BIT(59)
/* Spread-spectrum clock modulation range:
 *	0x0 = -4980 ppm downspread
 *	0x1 = -4492 ppm downspread
 *	0x2 = -4003 ppm downspread
 *	0x3 - 0x7 = Reserved
 */
# define USBDRD_UCTL_CTL_SSC_RANGE		GENMASK(58, 56)
/* Enable non-standard oscillator frequencies:
 *	[55:53] = modules -1
 *	[52:47] = 2's complement push amount, 0 = Feature disabled
 */
# define USBDRD_UCTL_CTL_SSC_REF_CLK_SEL	GENMASK(55, 47)
/* Reference clock multiplier for non-standard frequencies:
 *	0x19 = 100MHz on DLMC_REF_CLK* if REF_CLK_SEL = 0x0 or 0x1
 *	0x28 = 125MHz on DLMC_REF_CLK* if REF_CLK_SEL = 0x0 or 0x1
 *	0x32 =  50MHz on DLMC_REF_CLK* if REF_CLK_SEL = 0x0 or 0x1
 *	Other Values = Reserved
 */
# define USBDRD_UCTL_CTL_MPLL_MULTIPLIER	GENMASK(46, 40)
/* Enable reference clock to prescaler for SuperSpeed functionality.
 * Should always be set to "1"
 */
# define USBDRD_UCTL_CTL_REF_SSP_EN		BIT(39)
/* Divide the reference clock by 2 before entering the
 * REF_CLK_FSEL divider:
 *	If REF_CLK_SEL = 0x0 or 0x1, then only 0x0 is legal
 *	If REF_CLK_SEL = 0x2 or 0x3, then:
 *		0x1 = DLMC_REF_CLK* is 125MHz
 *		0x0 = DLMC_REF_CLK* is another supported frequency
 */
# define USBDRD_UCTL_CTL_REF_CLK_DIV2		BIT(38)
/* Select reference clock freqnuency for both PLL blocks:
 *	0x27 = REF_CLK_SEL is 0x0 or 0x1
 *	0x07 = REF_CLK_SEL is 0x2 or 0x3
 */
# define USBDRD_UCTL_CTL_REF_CLK_FSEL		GENMASK(37, 32)
/* Controller clock enable. */
# define USBDRD_UCTL_CTL_H_CLK_EN		BIT(30)
/* Select bypass input to controller clock divider:
 *	0x0 = Use divided coprocessor clock from H_CLKDIV
 *	0x1 = Use clock from GPIO pins
 */
# define USBDRD_UCTL_CTL_H_CLK_BYP_SEL		BIT(29)
/* Reset controller clock divider. */
# define USBDRD_UCTL_CTL_H_CLKDIV_RST		BIT(28)
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
# define USBDRD_UCTL_CTL_H_CLKDIV_SEL		GENMASK(26, 24)
/* USB3 port permanently attached: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_USB3_PORT_PERM_ATTACH	BIT(21)
/* USB2 port permanently attached: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_USB2_PORT_PERM_ATTACH	BIT(20)
/* Disable SuperSpeed PHY: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_USB3_PORT_DISABLE	BIT(18)
/* Disable HighSpeed PHY: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_USB2_PORT_DISABLE	BIT(16)
/* Enable PHY SuperSpeed block power: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_SS_POWER_EN		BIT(14)
/* Enable PHY HighSpeed block power: 0x0 = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_HS_POWER_EN		BIT(12)
/* Enable USB UCTL interface clock: 0xx = No, 0x1 = Yes */
# define USBDRD_UCTL_CTL_CSCLK_EN		BIT(4)
/* Controller mode: 0x0 = Host, 0x1 = Device */
# define USBDRD_UCTL_CTL_DRD_MODE		BIT(3)
/* PHY reset */
# define USBDRD_UCTL_CTL_UPHY_RST		BIT(2)
/* Software reset UAHC */
# define USBDRD_UCTL_CTL_UAHC_RST		BIT(1)
/* Software resets UCTL */
# define USBDRD_UCTL_CTL_UCTL_RST		BIT(0)

#define USBDRD_UCTL_BIST_STATUS			0x08
#define USBDRD_UCTL_SPARE0			0x10
#define USBDRD_UCTL_INTSTAT			0x30
#define USBDRD_UCTL_PORT_CFG_HS(port)		(0x40 + (0x20 * port))
#define USBDRD_UCTL_PORT_CFG_SS(port)		(0x48 + (0x20 * port))
#define USBDRD_UCTL_PORT_CR_DBG_CFG(port)	(0x50 + (0x20 * port))
#define USBDRD_UCTL_PORT_CR_DBG_STATUS(port)	(0x58 + (0x20 * port))
#define USBDRD_UCTL_HOST_CFG			0xe0
#define USBDRD_UCTL_SHIM_CFG			0xe8
#define USBDRD_UCTL_ECC				0xf0
#define USBDRD_UCTL_SPARE1			0xf8

/* UAHC Configuration Register */
union cvm_usbdrd_uctl_host_cfg {
	uint64_t u64;
	struct cvm_usbdrd_uctl_host_cfg_s {
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_60_63:4,
	/* Indicates minimum value of all received BELT values */
	__BITFIELD_FIELD(uint64_t host_current_belt:12,
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_38_47:10,
	/* HS jitter adjustment */
	__BITFIELD_FIELD(uint64_t fla:6,
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_29_31:3,
	/* Bus-master enable: 0x0 = Disabled (stall DMAs), 0x1 = enabled */
	__BITFIELD_FIELD(uint64_t bme:1,
	/* Overcurrent protection enable: 0x0 = unavailable, 0x1 = available */
	__BITFIELD_FIELD(uint64_t oci_en:1,
	/* Overcurrent sene selection:
	 *	0x0 = Overcurrent indication from off-chip is active-low
	 *	0x1 = Overcurrent indication from off-chip is active-high
	 */
	__BITFIELD_FIELD(uint64_t oci_active_high_en:1,
	/* Port power control enable: 0x0 = unavailable, 0x1 = available */
	__BITFIELD_FIELD(uint64_t ppc_en:1,
	/* Port power control sense selection:
	 *	0x0 = Port power to off-chip is active-low
	 *	0x1 = Port power to off-chip is active-high
	 */
	__BITFIELD_FIELD(uint64_t ppc_active_high_en:1,
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_0_23:24,
	;)))))))))))
	} s;
};

/* UCTL Shim Features Register */
union cvm_usbdrd_uctl_shim_cfg {
	uint64_t u64;
	struct cvm_usbdrd_uctl_shim_cfg_s {
	/* Out-of-bound UAHC register access: 0 = read, 1 = write */
	__BITFIELD_FIELD(uint64_t xs_ncb_oob_wrn:1,
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_60_62:3,
	/* SRCID error log for out-of-bound UAHC register access:
	 *	[59:58] = chipID
	 *	[57] = Request source: 0 = core, 1 = NCB-device
	 *	[56:51] = Core/NCB-device number, [56] always 0 for NCB devices
	 *	[50:48] = SubID
	 */
	__BITFIELD_FIELD(uint64_t xs_ncb_oob_osrc:12,
	/* Error log for bad UAHC DMA access: 0 = Read log, 1 = Write log */
	__BITFIELD_FIELD(uint64_t xm_bad_dma_wrn:1,
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_44_46:3,
	/* Encoded error type for bad UAHC DMA */
	__BITFIELD_FIELD(uint64_t xm_bad_dma_type:4,
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_13_39:27,
	/* Select the IOI read command used by DMA accesses */
	__BITFIELD_FIELD(uint64_t dma_read_cmd:1,
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_10_11:2,
	/* Select endian format for DMA accesses to the L2c:
	 *	0x0 = Little endian
	 *`	0x1 = Big endian
	 *	0x2 = Reserved
	 *	0x3 = Reserved
	 */
	__BITFIELD_FIELD(uint64_t dma_endian_mode:2,
	/* Reserved */
	__BITFIELD_FIELD(uint64_t reserved_2_7:6,
	/* Select endian format for IOI CSR access to UAHC:
	 *	0x0 = Little endian
	 *`	0x1 = Big endian
	 *	0x2 = Reserved
	 *	0x3 = Reserved
	 */
	__BITFIELD_FIELD(uint64_t csr_endian_mode:2,
	;))))))))))))
	} s;
};

#define OCTEON_H_CLKDIV_SEL		8
#define OCTEON_MIN_H_CLK_RATE		150000000
#define OCTEON_MAX_H_CLK_RATE		300000000

static DEFINE_MUTEX(dwc3_octeon_clocks_mutex);
static uint8_t clk_div[OCTEON_H_CLKDIV_SEL] = {1, 2, 4, 6, 8, 16, 24, 32};


static int dwc3_octeon_config_power(struct device *dev, u64 base)
{
	union cvm_usbdrd_uctl_host_cfg uctl_host_cfg;
	union cvmx_gpio_bit_cfgx gpio_bit;
	uint32_t gpio_pwr[3];
	int gpio, len, power_active_low;
	struct device_node *node = dev->of_node;
	int index = (base >> 24) & 1;

	if (of_find_property(node, "power", &len) != NULL) {
		if (len == 12) {
			of_property_read_u32_array(node, "power", gpio_pwr, 3);
			power_active_low = gpio_pwr[2] & 0x01;
			gpio = gpio_pwr[1];
		} else if (len == 8) {
			of_property_read_u32_array(node, "power", gpio_pwr, 2);
			power_active_low = 0;
			gpio = gpio_pwr[1];
		} else {
			dev_err(dev, "invalid power configuration\n");
			return -EINVAL;
		}
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

		/* Enable XHCI power control and set if active high or low. */
		uctl_host_cfg.u64 = cvmx_read_csr(base + USBDRD_UCTL_HOST_CFG);
		uctl_host_cfg.s.ppc_en = 1;
		uctl_host_cfg.s.ppc_active_high_en = !power_active_low;
		cvmx_write_csr(base + USBDRD_UCTL_HOST_CFG, uctl_host_cfg.u64);
	} else {
		/* Disable XHCI power control and set if active high. */
		uctl_host_cfg.u64 = cvmx_read_csr(base + USBDRD_UCTL_HOST_CFG);
		uctl_host_cfg.s.ppc_en = 0;
		uctl_host_cfg.s.ppc_active_high_en = 0;
		cvmx_write_csr(base + USBDRD_UCTL_HOST_CFG, uctl_host_cfg.u64);
		dev_info(dev, "power control disabled\n");
	}
	return 0;
}

static int dwc3_octeon_clocks_start(struct device *dev, u64 base)
{
	int i, mpll_mul, ref_clk_fsel, ref_clk_sel = 2;
	u32 clock_rate;
	u64 div, h_clk_rate, val;
	u64 uctl_ctl_reg = base + USBDRD_UCTL_CTL;

	if (dev->of_node) {
		const char *ss_clock_type;
		const char *hs_clock_type;

		i = of_property_read_u32(dev->of_node,
					 "refclk-frequency", &clock_rate);
		if (i) {
			dev_err(dev, "No UCTL \"refclk-frequency\"\n");
			return -EINVAL;
		}
		i = of_property_read_string(dev->of_node,
					    "refclk-type-ss", &ss_clock_type);
		if (i) {
			dev_err(dev, "No UCTL \"refclk-type-ss\"\n");
			return -EINVAL;
		}
		i = of_property_read_string(dev->of_node,
					    "refclk-type-hs", &hs_clock_type);
		if (i) {
			dev_err(dev, "No UCTL \"refclk-type-hs\"\n");
			return -EINVAL;
		}
		if (strcmp("dlmc_ref_clk0", ss_clock_type) == 0) {
			if (strcmp(hs_clock_type, "dlmc_ref_clk0") == 0)
				ref_clk_sel = 0;
			else if (strcmp(hs_clock_type, "pll_ref_clk") == 0)
				ref_clk_sel = 2;
			else
				dev_warn(dev, "Invalid HS clock type %s, using pll_ref_clk instead\n",
					 hs_clock_type);
		} else if (strcmp(ss_clock_type, "dlmc_ref_clk1") == 0) {
			if (strcmp(hs_clock_type, "dlmc_ref_clk1") == 0)
				ref_clk_sel = 1;
			else if (strcmp(hs_clock_type, "pll_ref_clk") == 0)
				ref_clk_sel = 3;
			else {
				dev_warn(dev, "Invalid HS clock type %s, using pll_ref_clk instead\n",
					 hs_clock_type);
				ref_clk_sel = 3;
			}
		} else
			dev_warn(dev, "Invalid SS clock type %s, using dlmc_ref_clk0 instead\n",
				 ss_clock_type);

		if ((ref_clk_sel == 0 || ref_clk_sel == 1) &&
		    (clock_rate != 100000000))
			dev_warn(dev, "Invalid UCTL clock rate of %u, using 100000000 instead\n",
				 clock_rate);

	} else {
		dev_err(dev, "No USB UCTL device node\n");
		return -EINVAL;
	}

	/*
	 * Step 1: Wait for all voltages to be stable...that surely
	 *         happened before starting the kernel. SKIP
	 */

	/* Step 2: Select GPIO for overcurrent indication, if desired. SKIP */

	/* Step 3: Assert all resets. */
	val = cvmx_read_csr(uctl_ctl_reg);
	val |= USBDRD_UCTL_CTL_UPHY_RST |
	       USBDRD_UCTL_CTL_UAHC_RST |
	       USBDRD_UCTL_CTL_UCTL_RST;
	cvmx_write_csr(uctl_ctl_reg, val);

	/* Step 4a: Reset the clock dividers. */
	val = cvmx_read_csr(uctl_ctl_reg);
	val |= USBDRD_UCTL_CTL_H_CLKDIV_RST;
	cvmx_write_csr(uctl_ctl_reg, val);

	/* Step 4b: Select controller clock frequency. */
	for (div = 0; div < OCTEON_H_CLKDIV_SEL; div++) {
		h_clk_rate = octeon_get_io_clock_rate() / clk_div[div];
		if (h_clk_rate <= OCTEON_MAX_H_CLK_RATE &&
				 h_clk_rate >= OCTEON_MIN_H_CLK_RATE)
			break;
	}
	val = cvmx_read_csr(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_H_CLKDIV_SEL;
	val |= FIELD_PREP(USBDRD_UCTL_CTL_H_CLKDIV_SEL, div);
	val |= USBDRD_UCTL_CTL_H_CLK_EN;
	cvmx_write_csr(uctl_ctl_reg, val);
	val = cvmx_read_csr(uctl_ctl_reg);
	if ((div != FIELD_GET(USBDRD_UCTL_CTL_H_CLKDIV_SEL, val)) ||
	    (!(FIELD_GET(USBDRD_UCTL_CTL_H_CLK_EN, val)))) {
		dev_err(dev, "dwc3 controller clock init failure.\n");
			return -EINVAL;
	}

	/* Step 4c: Deassert the controller clock divider reset. */
	val &= ~USBDRD_UCTL_CTL_H_CLKDIV_RST;
	cvmx_write_csr(uctl_ctl_reg, val);

	/* Step 5a: Reference clock configuration. */
	val = cvmx_read_csr(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_REF_CLK_DIV2;
	val &= ~USBDRD_UCTL_CTL_REF_CLK_SEL;
	val |= FIELD_PREP(USBDRD_UCTL_CTL_REF_CLK_SEL, ref_clk_sel);

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
	cvmx_write_csr(uctl_ctl_reg, val);

	/* Step 7: Wait 10 controller-clock cycles to take effect. */
	udelay(10);

	/* Step 8a: Deassert UCTL reset signal. */
	val = cvmx_read_csr(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_UCTL_RST;
	cvmx_write_csr(uctl_ctl_reg, val);

	/* Step 8b: Wait 10 controller-clock cycles. */
	udelay(10);

	/* Steo 8c: Setup power-power control. */
	if (dwc3_octeon_config_power(dev, base))
		return -EINVAL;

	/* Step 8d: Deassert UAHC reset signal. */
	val = cvmx_read_csr(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_UAHC_RST;
	cvmx_write_csr(uctl_ctl_reg, val);

	/* Step 8e: Wait 10 controller-clock cycles. */
	udelay(10);

	/* Step 9: Enable conditional coprocessor clock of UCTL. */
	val = cvmx_read_csr(uctl_ctl_reg);
	val |= USBDRD_UCTL_CTL_CSCLK_EN;
	cvmx_write_csr(uctl_ctl_reg, val);

	/*Step 10: Set for host mode only. */
	val = cvmx_read_csr(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_DRD_MODE;
	cvmx_write_csr(uctl_ctl_reg, val);

	return 0;
}

static void __init dwc3_octeon_set_endian_mode(u64 base)
{
	union cvm_usbdrd_uctl_shim_cfg shim_cfg;

	shim_cfg.u64 = cvmx_read_csr(base + USBDRD_UCTL_SHIM_CFG);
#ifdef __BIG_ENDIAN
	shim_cfg.s.dma_endian_mode = 1;
	shim_cfg.s.csr_endian_mode = 1;
#else
	shim_cfg.s.dma_endian_mode = 0;
	shim_cfg.s.csr_endian_mode = 0;
#endif
	cvmx_write_csr(base + USBDRD_UCTL_SHIM_CFG, shim_cfg.u64);
}

static void __init dwc3_octeon_phy_reset(u64 base)
{
	u64 val;
	u64 uctl_ctl_reg = base + USBDRD_UCTL_CTL;

	val = cvmx_read_csr(uctl_ctl_reg);
	val &= ~USBDRD_UCTL_CTL_UPHY_RST;
	cvmx_write_csr(uctl_ctl_reg, val);
}

static int __init dwc3_octeon_device_init(void)
{
	const char compat_node_name[] = "cavium,octeon-7130-usb-uctl";
	struct platform_device *pdev;
	struct device_node *node;
	struct resource *res;
	void __iomem *base;

	/*
	 * There should only be three universal controllers, "uctl"
	 * in the device tree. Two USB and a SATA, which we ignore.
	 */
	node = NULL;
	do {
		node = of_find_node_by_name(node, "uctl");
		if (!node)
			return -ENODEV;

		if (of_device_is_compatible(node, compat_node_name)) {
			pdev = of_find_device_by_node(node);
			if (!pdev)
				return -ENODEV;

			/*
			 * The code below maps in the registers necessary for
			 * setting up the clocks and reseting PHYs. We must
			 * release the resources so the dwc3 subsystem doesn't
			 * know the difference.
			 */
			base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
			if (IS_ERR(base)) {
				put_device(&pdev->dev);
				return PTR_ERR(base);
			}

			mutex_lock(&dwc3_octeon_clocks_mutex);
			if (dwc3_octeon_clocks_start(&pdev->dev, (u64)base) == 0)
				dev_info(&pdev->dev, "clocks initialized.\n");
			dwc3_octeon_set_endian_mode((u64)base);
			dwc3_octeon_phy_reset((u64)base);
			mutex_unlock(&dwc3_octeon_clocks_mutex);
			devm_iounmap(&pdev->dev, base);
			devm_release_mem_region(&pdev->dev, res->start,
						resource_size(res));
			put_device(&pdev->dev);
		}
	} while (node != NULL);

	return 0;
}
device_initcall(dwc3_octeon_device_init);

MODULE_AUTHOR("David Daney <david.daney@cavium.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB driver for OCTEON III SoC");
