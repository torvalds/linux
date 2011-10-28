#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/errno.h>

#include <mach/cputype.h>

#ifdef CONFIG_ARCH_MMP

#define UTMI_REVISION		0x0
#define UTMI_CTRL		0x4
#define UTMI_PLL		0x8
#define UTMI_TX			0xc
#define UTMI_RX			0x10
#define UTMI_IVREF		0x14
#define UTMI_T0			0x18
#define UTMI_T1			0x1c
#define UTMI_T2			0x20
#define UTMI_T3			0x24
#define UTMI_T4			0x28
#define UTMI_T5			0x2c
#define UTMI_RESERVE		0x30
#define UTMI_USB_INT		0x34
#define UTMI_DBG_CTL		0x38
#define UTMI_OTG_ADDON		0x3c

/* For UTMICTRL Register */
#define UTMI_CTRL_USB_CLK_EN			(1 << 31)
/* pxa168 */
#define UTMI_CTRL_SUSPEND_SET1			(1 << 30)
#define UTMI_CTRL_SUSPEND_SET2			(1 << 29)
#define UTMI_CTRL_RXBUF_PDWN			(1 << 24)
#define UTMI_CTRL_TXBUF_PDWN			(1 << 11)

#define UTMI_CTRL_INPKT_DELAY_SHIFT		30
#define UTMI_CTRL_INPKT_DELAY_SOF_SHIFT		28
#define UTMI_CTRL_PU_REF_SHIFT			20
#define UTMI_CTRL_ARC_PULLDN_SHIFT		12
#define UTMI_CTRL_PLL_PWR_UP_SHIFT		1
#define UTMI_CTRL_PWR_UP_SHIFT			0
/* For UTMI_PLL Register */
#define UTMI_PLL_CLK_BLK_EN_SHIFT		24
#define UTMI_PLL_FBDIV_SHIFT			4
#define UTMI_PLL_REFDIV_SHIFT			0
#define UTMI_PLL_FBDIV_MASK			0x00000FF0
#define UTMI_PLL_REFDIV_MASK			0x0000000F
#define UTMI_PLL_ICP_MASK			0x00007000
#define UTMI_PLL_KVCO_MASK			0x00031000
#define UTMI_PLL_PLLCALI12_SHIFT		29
#define UTMI_PLL_PLLCALI12_MASK			(0x3 << 29)
#define UTMI_PLL_PLLVDD18_SHIFT			27
#define UTMI_PLL_PLLVDD18_MASK			(0x3 << 27)
#define UTMI_PLL_PLLVDD12_SHIFT			25
#define UTMI_PLL_PLLVDD12_MASK			(0x3 << 25)
#define UTMI_PLL_KVCO_SHIFT			15
#define UTMI_PLL_ICP_SHIFT			12
/* For UTMI_TX Register */
#define UTMI_TX_REG_EXT_FS_RCAL_SHIFT		27
#define UTMI_TX_REG_EXT_FS_RCAL_MASK		(0xf << 27)
#define UTMI_TX_REG_EXT_FS_RCAL_EN_MASK		26
#define UTMI_TX_REG_EXT_FS_RCAL_EN		(0x1 << 26)
#define UTMI_TX_LOW_VDD_EN_SHIFT		11
#define UTMI_TX_IMPCAL_VTH_SHIFT		14
#define UTMI_TX_IMPCAL_VTH_MASK			(0x7 << 14)
#define UTMI_TX_CK60_PHSEL_SHIFT		17
#define UTMI_TX_CK60_PHSEL_MASK			(0xf << 17)
#define UTMI_TX_TXVDD12_SHIFT                   22
#define UTMI_TX_TXVDD12_MASK			(0x3 << 22)
#define UTMI_TX_AMP_SHIFT			0
#define UTMI_TX_AMP_MASK			(0x7 << 0)
/* For UTMI_RX Register */
#define UTMI_RX_SQ_THRESH_SHIFT			4
#define UTMI_RX_SQ_THRESH_MASK			(0xf << 4)
#define UTMI_REG_SQ_LENGTH_SHIFT		15
#define UTMI_REG_SQ_LENGTH_MASK			(0x3 << 15)

#define REG_RCAL_START				0x00001000
#define VCOCAL_START				0x00200000
#define KVCO_EXT				0x00400000
#define PLL_READY				0x00800000
#define CLK_BLK_EN				0x01000000
#endif

static unsigned int u2o_read(unsigned int base, unsigned int offset)
{
	return readl(base + offset);
}

static void u2o_set(unsigned int base, unsigned int offset, unsigned int value)
{
	unsigned int reg;

	reg = readl(base + offset);
	reg |= value;
	writel(reg, base + offset);
	readl(base + offset);
}

static void u2o_clear(unsigned int base, unsigned int offset,
	unsigned int value)
{
	unsigned int reg;

	reg = readl(base + offset);
	reg &= ~value;
	writel(reg, base + offset);
	readl(base + offset);
}

static void u2o_write(unsigned int base, unsigned int offset,
	unsigned int value)
{
	writel(value, base + offset);
	readl(base + offset);
}

#ifdef CONFIG_ARCH_MMP
int mv_udc_phy_init(unsigned int base)
{
	unsigned long timeout;

	/* Initialize the USB PHY power */
	if (cpu_is_pxa910()) {
		u2o_set(base, UTMI_CTRL, (1 << UTMI_CTRL_INPKT_DELAY_SOF_SHIFT)
			| (1 << UTMI_CTRL_PU_REF_SHIFT));
	}

	u2o_set(base, UTMI_CTRL, 1 << UTMI_CTRL_PLL_PWR_UP_SHIFT);
	u2o_set(base, UTMI_CTRL, 1 << UTMI_CTRL_PWR_UP_SHIFT);

	/* UTMI_PLL settings */
	u2o_clear(base, UTMI_PLL, UTMI_PLL_PLLVDD18_MASK
		| UTMI_PLL_PLLVDD12_MASK | UTMI_PLL_PLLCALI12_MASK
		| UTMI_PLL_FBDIV_MASK | UTMI_PLL_REFDIV_MASK
		| UTMI_PLL_ICP_MASK | UTMI_PLL_KVCO_MASK);

	u2o_set(base, UTMI_PLL, (0xee << UTMI_PLL_FBDIV_SHIFT)
		| (0xb << UTMI_PLL_REFDIV_SHIFT)
		| (3 << UTMI_PLL_PLLVDD18_SHIFT)
		| (3 << UTMI_PLL_PLLVDD12_SHIFT)
		| (3 << UTMI_PLL_PLLCALI12_SHIFT)
		| (1 << UTMI_PLL_ICP_SHIFT) | (3 << UTMI_PLL_KVCO_SHIFT));

	/* UTMI_TX */
	u2o_clear(base, UTMI_TX, UTMI_TX_REG_EXT_FS_RCAL_EN_MASK
		| UTMI_TX_TXVDD12_MASK
		| UTMI_TX_CK60_PHSEL_MASK | UTMI_TX_IMPCAL_VTH_MASK
		| UTMI_TX_REG_EXT_FS_RCAL_MASK | UTMI_TX_AMP_MASK);
	u2o_set(base, UTMI_TX, (3 << UTMI_TX_TXVDD12_SHIFT)
		| (4 << UTMI_TX_CK60_PHSEL_SHIFT)
		| (4 << UTMI_TX_IMPCAL_VTH_SHIFT)
		| (8 << UTMI_TX_REG_EXT_FS_RCAL_SHIFT)
		| (3 << UTMI_TX_AMP_SHIFT));

	/* UTMI_RX */
	u2o_clear(base, UTMI_RX, UTMI_RX_SQ_THRESH_MASK
		| UTMI_REG_SQ_LENGTH_MASK);
	if (cpu_is_pxa168())
		u2o_set(base, UTMI_RX, (7 << UTMI_RX_SQ_THRESH_SHIFT)
			| (2 << UTMI_REG_SQ_LENGTH_SHIFT));
	else
		u2o_set(base, UTMI_RX, (0x7 << UTMI_RX_SQ_THRESH_SHIFT)
			| (2 << UTMI_REG_SQ_LENGTH_SHIFT));

	/* UTMI_IVREF */
	if (cpu_is_pxa168())
		/*
		 * fixing Microsoft Altair board interface with NEC hub issue -
		 * Set UTMI_IVREF from 0x4a3 to 0x4bf
		 */
		u2o_write(base, UTMI_IVREF, 0x4bf);

	/* calibrate */
	timeout = jiffies + 100;
	while ((u2o_read(base, UTMI_PLL) & PLL_READY) == 0) {
		if (time_after(jiffies, timeout))
			return -ETIME;
		cpu_relax();
	}

	/* toggle VCOCAL_START bit of UTMI_PLL */
	udelay(200);
	u2o_set(base, UTMI_PLL, VCOCAL_START);
	udelay(40);
	u2o_clear(base, UTMI_PLL, VCOCAL_START);

	/* toggle REG_RCAL_START bit of UTMI_TX */
	udelay(200);
	u2o_set(base, UTMI_TX, REG_RCAL_START);
	udelay(40);
	u2o_clear(base, UTMI_TX, REG_RCAL_START);
	udelay(200);

	/* make sure phy is ready */
	timeout = jiffies + 100;
	while ((u2o_read(base, UTMI_PLL) & PLL_READY) == 0) {
		if (time_after(jiffies, timeout))
			return -ETIME;
		cpu_relax();
	}

	if (cpu_is_pxa168()) {
		u2o_set(base, UTMI_RESERVE, 1 << 5);
		/* Turn on UTMI PHY OTG extension */
		u2o_write(base, UTMI_OTG_ADDON, 1);
	}
	return 0;
}
#else
int mv_udc_phy_init(unsigned int base)
{
	return 0;
}
#endif
