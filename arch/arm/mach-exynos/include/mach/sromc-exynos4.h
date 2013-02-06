/*
 * arch/arm/mach-exynos/sromc.h
 */

#ifndef __SROMC_H__
#define __SROMC_H__

#include <linux/clk.h>
#include <plat/gpio-cfg.h>
#include <plat/regs-srom.h>
#include <mach/gpio.h>
#include <mach/gpio-exynos4.h>
#include <mach/regs-mem.h>

#define SROM_CS0_BASE		0x04000000
#define SROM_CS1_BASE		0x05000000
#define SROM_CS2_BASE		0x06000000
#define SROM_CS3_BASE		0x07000000
#define SROM_WIDTH		0x01000000

#define GPIO_SFN_SROMC		S3C_GPIO_SFN(2)

#define GPIO_SROMC_CSN		EXYNOS4_GPY0(0)
#define GPIO_SROMC_CSN0		EXYNOS4_GPY0(0)
#define GPIO_SROMC_CSN1		EXYNOS4_GPY0(1)
#define GPIO_SROMC_CSN2		EXYNOS4_GPY0(2)
#define GPIO_SROMC_CSN3		EXYNOS4_GPY0(3)
#define GPIO_SROMC_REN		EXYNOS4_GPY0(4)
#define GPIO_SROMC_WEN		EXYNOS4_GPY0(5)
#define GPIO_SROMC_LBN		EXYNOS4_GPY1(0)
#define GPIO_SROMC_UBN		EXYNOS4_GPY1(1)
#define GPIO_SROMC_ADDR_BUS_L	EXYNOS4_GPY3(0)
#define GPIO_SROMC_ADDR_BUS_H	EXYNOS4_GPY4(0)
#define GPIO_SROMC_DATA_BUS_L	EXYNOS4_GPY5(0)
#define GPIO_SROMC_DATA_BUS_H	EXYNOS4_GPY6(0)

/* SROMC configuration */
struct sromc_bus_cfg {
	unsigned addr_bits;	/* Width of address bus	in bits	*/
	unsigned data_bits;	/* Width of data bus in bits	*/
	unsigned byte_acc;	/* Byte access			*/
};

/* SROMC bank attributes in BW (Bus width and Wait control) register */
enum sromc_bank_attr {
	SROMC_DATA_16   = 0x1,	/* 16-bit data bus	*/
	SROMC_BYTE_ADDR = 0x2,	/* Byte base address	*/
	SROMC_WAIT_EN   = 0x4,	/* Wait enabled		*/
	SROMC_BYTE_EN   = 0x8,	/* Byte access enabled	*/
	SROMC_ATTR_MASK = 0xF
};

/* SROMC bank configuration */
struct sromc_bank_cfg {
	unsigned csn;		/* CSn #			*/
	unsigned attr;		/* SROMC bank attributes	*/
	unsigned size;		/* Size of a memory		*/
	unsigned addr;		/* Start address (physical)	*/
};

/* SROMC bank access timing configuration */
struct sromc_timing_cfg {
	u32 tacs;		/* Address set-up before CSn		*/
	u32 tcos;		/* Chip selection set-up before OEn	*/
	u32 tacc;		/* Access cycle				*/
	u32 tcoh;		/* Chip selection hold on OEn		*/
	u32 tcah;		/* Address holding time after CSn	*/
	u32 tacp;		/* Page mode access cycle at Page mode	*/
	u32 pmc;		/* Page Mode config			*/
};

static unsigned int sfn = GPIO_SFN_SROMC;

/**
 * sromc_enable
 *
 * Enables SROM controller (SROMC) block
 *
 */
static int sromc_enable(void)
{
	struct clk *clk = NULL;

	/* SROMC clk enable */
	clk = clk_get(NULL, "sromc");
	if (!clk) {
		pr_err("%s: ERR! SROMC clock gate fail\n", __func__);
		return -EINVAL;
	}

	clk_enable(clk);
	return 0;
}

/**
 * sromc_config_demux_gpio
 * @bc: pointer to an sromc_bus_cfg
 *
 * Configures GPIO pins for REn, WEn, LBn, UBn, address bus, and data bus
 * as demux mode
 *
 * Returns 0 if there is no error
 *
 */
static int sromc_config_demux_gpio(struct sromc_bus_cfg *bc)
{
	unsigned int addr_bits = bc->addr_bits;
	unsigned int data_bits = bc->data_bits;
	unsigned int byte_acc = bc->byte_acc;
	unsigned int bits;

	pr_err("[SROMC] %s: addr_bits %d, data_bits %d, byte_acc %d\n",
		__func__, addr_bits, data_bits, byte_acc);

	/* Configure address bus */
	switch (addr_bits) {
	case 1 ... EXYNOS4_GPIO_Y3_NR:
		bits = addr_bits;
		s3c_gpio_cfgrange_nopull(GPIO_SROMC_ADDR_BUS_L, bits, sfn);
		break;

	case (EXYNOS4_GPIO_Y3_NR + 1) ... 16:
		bits = EXYNOS4_GPIO_Y3_NR;
		s3c_gpio_cfgrange_nopull(GPIO_SROMC_ADDR_BUS_L, bits, sfn);
		bits = (addr_bits - EXYNOS4_GPIO_Y3_NR);
		s3c_gpio_cfgrange_nopull(GPIO_SROMC_ADDR_BUS_H, bits, sfn);
		break;

	default:
		pr_err("[SROMC] %s: ERR! invalid addr_bits %d\n",
			__func__, addr_bits);
		return -EINVAL;
	}

	/* Configure data bus (8 or 16 bits) */
	switch (data_bits) {
	case 8:
		s3c_gpio_cfgrange_nopull(GPIO_SROMC_DATA_BUS_L, 8, sfn);
		break;

	case 16:
		s3c_gpio_cfgrange_nopull(GPIO_SROMC_DATA_BUS_L, 8, sfn);
		s3c_gpio_cfgrange_nopull(GPIO_SROMC_DATA_BUS_H, 8, sfn);
		break;

	default:
		pr_err("[SROMC] %s: ERR! invalid data_bits %d\n",
			__func__, data_bits);
		return -EINVAL;
	}

	/* Configure REn */
	s3c_gpio_cfgpin(GPIO_SROMC_REN, sfn);
	s3c_gpio_setpull(GPIO_SROMC_REN, S3C_GPIO_PULL_NONE);

	/* Configure WEn */
	s3c_gpio_cfgpin(GPIO_SROMC_WEN, sfn);
	s3c_gpio_setpull(GPIO_SROMC_WEN, S3C_GPIO_PULL_NONE);

	/* Configure LBn, UBn */
	if (byte_acc) {
		s3c_gpio_cfgpin(GPIO_SROMC_LBN, sfn);
		s3c_gpio_setpull(GPIO_SROMC_LBN, S3C_GPIO_PULL_NONE);
		s3c_gpio_cfgpin(GPIO_SROMC_UBN, sfn);
		s3c_gpio_setpull(GPIO_SROMC_UBN, S3C_GPIO_PULL_NONE);
	}

	return 0;
}

/**
 * sromc_config_csn_gpio
 * @csn: CSn number (0 to 3)
 *
 * Configures GPIO pins for CSn
 *
 * Returns 0 if there is no error
 *
 */
static int sromc_config_csn_gpio(unsigned int csn)
{
	unsigned int pin = GPIO_SROMC_CSN + csn;

	pr_err("[SROMC] %s: for CSn%d\n", __func__, csn);

	if (csn > 4) {
		pr_err("[SROMC] %s: ERR! CSn%d invalid\n", __func__, csn);
		return -EINVAL;
	}

	/* Configure CSn GPIO pin */
	s3c_gpio_cfgpin(pin, sfn);
	s3c_gpio_setpull(pin, S3C_GPIO_PULL_NONE);

	return 0;
}

/**
 * sromc_config_access_attr
 * @csn: CSn number
 * @attr: SROMC attribute for this CSn
 *
 * Configures SROMC attribute for a CSn
 *
 */
static void sromc_config_access_attr(unsigned int csn, unsigned int attr)
{
	unsigned int bw = 0;	/* Bus width and Wait control */

	pr_err("[SROMC] %s: for CSn%d\n", __func__, csn);

	bw = __raw_readl(S5P_SROM_BW);
	pr_err("[SROMC] %s: old BW setting = 0x%08X\n", __func__, bw);

	/* Configure BW control field for the CSn */
	bw &= ~(SROMC_ATTR_MASK << (csn << 2));
	bw |= (attr << (csn << 2));
	writel(bw, S5P_SROM_BW);

	/* Verify SROMC settings */
	bw = __raw_readl(S5P_SROM_BW);
	pr_err("[SROMC] %s: new BW setting = 0x%08X\n", __func__, bw);
}

/**
 * sromc_config_access_timing
 * @csn: CSn number
 * @tm_cfg: pointer to an sromc_timing_cfg
 *
 * Configures SROMC access timing register
 *
 */
static void sromc_config_access_timing(unsigned int csn,
				struct sromc_timing_cfg *tm_cfg)
{
	void __iomem *bank_sfr = S5P_SROM_BC0 + (4 * csn);
	unsigned int bc = 0;	/* Bank Control */

	bc = __raw_readl(bank_sfr);
	pr_err("[SROMC] %s: old BC%d setting = 0x%08X\n", __func__, csn, bc);

	/* Configure memory access timing for the CSn */
	bc = tm_cfg->tacs | tm_cfg->tcos | tm_cfg->tacc |
	     tm_cfg->tcoh | tm_cfg->tcah | tm_cfg->tacp | tm_cfg->pmc;
	writel(bc, bank_sfr);

	/* Verify SROMC settings */
	bc = __raw_readl(bank_sfr);
	pr_err("[SROMC] %s: new BC%d setting = 0x%08X\n", __func__, csn, bc);
}

#endif

