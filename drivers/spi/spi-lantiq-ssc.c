// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011-2015 Daniel Schwierzeck <daniel.schwierzeck@gmail.com>
 * Copyright (C) 2016 Hauke Mehrtens <hauke@hauke-m.de>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>

#ifdef CONFIG_LANTIQ
#include <lantiq_soc.h>
#endif

#define LTQ_SPI_RX_IRQ_NAME	"spi_rx"
#define LTQ_SPI_TX_IRQ_NAME	"spi_tx"
#define LTQ_SPI_ERR_IRQ_NAME	"spi_err"
#define LTQ_SPI_FRM_IRQ_NAME	"spi_frm"

#define LTQ_SPI_CLC		0x00
#define LTQ_SPI_PISEL		0x04
#define LTQ_SPI_ID		0x08
#define LTQ_SPI_CON		0x10
#define LTQ_SPI_STAT		0x14
#define LTQ_SPI_WHBSTATE	0x18
#define LTQ_SPI_TB		0x20
#define LTQ_SPI_RB		0x24
#define LTQ_SPI_RXFCON		0x30
#define LTQ_SPI_TXFCON		0x34
#define LTQ_SPI_FSTAT		0x38
#define LTQ_SPI_BRT		0x40
#define LTQ_SPI_BRSTAT		0x44
#define LTQ_SPI_SFCON		0x60
#define LTQ_SPI_SFSTAT		0x64
#define LTQ_SPI_GPOCON		0x70
#define LTQ_SPI_GPOSTAT		0x74
#define LTQ_SPI_FPGO		0x78
#define LTQ_SPI_RXREQ		0x80
#define LTQ_SPI_RXCNT		0x84
#define LTQ_SPI_DMACON		0xec
#define LTQ_SPI_IRNEN		0xf4

#define LTQ_SPI_CLC_SMC_S	16	/* Clock divider for sleep mode */
#define LTQ_SPI_CLC_SMC_M	(0xFF << LTQ_SPI_CLC_SMC_S)
#define LTQ_SPI_CLC_RMC_S	8	/* Clock divider for normal run mode */
#define LTQ_SPI_CLC_RMC_M	(0xFF << LTQ_SPI_CLC_RMC_S)
#define LTQ_SPI_CLC_DISS	BIT(1)	/* Disable status bit */
#define LTQ_SPI_CLC_DISR	BIT(0)	/* Disable request bit */

#define LTQ_SPI_ID_TXFS_S	24	/* Implemented TX FIFO size */
#define LTQ_SPI_ID_RXFS_S	16	/* Implemented RX FIFO size */
#define LTQ_SPI_ID_MOD_S	8	/* Module ID */
#define LTQ_SPI_ID_MOD_M	(0xff << LTQ_SPI_ID_MOD_S)
#define LTQ_SPI_ID_CFG_S	5	/* DMA interface support */
#define LTQ_SPI_ID_CFG_M	(1 << LTQ_SPI_ID_CFG_S)
#define LTQ_SPI_ID_REV_M	0x1F	/* Hardware revision number */

#define LTQ_SPI_CON_BM_S	16	/* Data width selection */
#define LTQ_SPI_CON_BM_M	(0x1F << LTQ_SPI_CON_BM_S)
#define LTQ_SPI_CON_EM		BIT(24)	/* Echo mode */
#define LTQ_SPI_CON_IDLE	BIT(23)	/* Idle bit value */
#define LTQ_SPI_CON_ENBV	BIT(22)	/* Enable byte valid control */
#define LTQ_SPI_CON_RUEN	BIT(12)	/* Receive underflow error enable */
#define LTQ_SPI_CON_TUEN	BIT(11)	/* Transmit underflow error enable */
#define LTQ_SPI_CON_AEN		BIT(10)	/* Abort error enable */
#define LTQ_SPI_CON_REN		BIT(9)	/* Receive overflow error enable */
#define LTQ_SPI_CON_TEN		BIT(8)	/* Transmit overflow error enable */
#define LTQ_SPI_CON_LB		BIT(7)	/* Loopback control */
#define LTQ_SPI_CON_PO		BIT(6)	/* Clock polarity control */
#define LTQ_SPI_CON_PH		BIT(5)	/* Clock phase control */
#define LTQ_SPI_CON_HB		BIT(4)	/* Heading control */
#define LTQ_SPI_CON_RXOFF	BIT(1)	/* Switch receiver off */
#define LTQ_SPI_CON_TXOFF	BIT(0)	/* Switch transmitter off */

#define LTQ_SPI_STAT_RXBV_S	28
#define LTQ_SPI_STAT_RXBV_M	(0x7 << LTQ_SPI_STAT_RXBV_S)
#define LTQ_SPI_STAT_BSY	BIT(13)	/* Busy flag */
#define LTQ_SPI_STAT_RUE	BIT(12)	/* Receive underflow error flag */
#define LTQ_SPI_STAT_TUE	BIT(11)	/* Transmit underflow error flag */
#define LTQ_SPI_STAT_AE		BIT(10)	/* Abort error flag */
#define LTQ_SPI_STAT_RE		BIT(9)	/* Receive error flag */
#define LTQ_SPI_STAT_TE		BIT(8)	/* Transmit error flag */
#define LTQ_SPI_STAT_ME		BIT(7)	/* Mode error flag */
#define LTQ_SPI_STAT_MS		BIT(1)	/* Master/slave select bit */
#define LTQ_SPI_STAT_EN		BIT(0)	/* Enable bit */
#define LTQ_SPI_STAT_ERRORS	(LTQ_SPI_STAT_ME | LTQ_SPI_STAT_TE | \
				 LTQ_SPI_STAT_RE | LTQ_SPI_STAT_AE | \
				 LTQ_SPI_STAT_TUE | LTQ_SPI_STAT_RUE)

#define LTQ_SPI_WHBSTATE_SETTUE	BIT(15)	/* Set transmit underflow error flag */
#define LTQ_SPI_WHBSTATE_SETAE	BIT(14)	/* Set abort error flag */
#define LTQ_SPI_WHBSTATE_SETRE	BIT(13)	/* Set receive error flag */
#define LTQ_SPI_WHBSTATE_SETTE	BIT(12)	/* Set transmit error flag */
#define LTQ_SPI_WHBSTATE_CLRTUE	BIT(11)	/* Clear transmit underflow error flag */
#define LTQ_SPI_WHBSTATE_CLRAE	BIT(10)	/* Clear abort error flag */
#define LTQ_SPI_WHBSTATE_CLRRE	BIT(9)	/* Clear receive error flag */
#define LTQ_SPI_WHBSTATE_CLRTE	BIT(8)	/* Clear transmit error flag */
#define LTQ_SPI_WHBSTATE_SETME	BIT(7)	/* Set mode error flag */
#define LTQ_SPI_WHBSTATE_CLRME	BIT(6)	/* Clear mode error flag */
#define LTQ_SPI_WHBSTATE_SETRUE	BIT(5)	/* Set receive underflow error flag */
#define LTQ_SPI_WHBSTATE_CLRRUE	BIT(4)	/* Clear receive underflow error flag */
#define LTQ_SPI_WHBSTATE_SETMS	BIT(3)	/* Set master select bit */
#define LTQ_SPI_WHBSTATE_CLRMS	BIT(2)	/* Clear master select bit */
#define LTQ_SPI_WHBSTATE_SETEN	BIT(1)	/* Set enable bit (operational mode) */
#define LTQ_SPI_WHBSTATE_CLREN	BIT(0)	/* Clear enable bit (config mode */
#define LTQ_SPI_WHBSTATE_CLR_ERRORS	(LTQ_SPI_WHBSTATE_CLRRUE | \
					 LTQ_SPI_WHBSTATE_CLRME | \
					 LTQ_SPI_WHBSTATE_CLRTE | \
					 LTQ_SPI_WHBSTATE_CLRRE | \
					 LTQ_SPI_WHBSTATE_CLRAE | \
					 LTQ_SPI_WHBSTATE_CLRTUE)

#define LTQ_SPI_RXFCON_RXFITL_S	8	/* FIFO interrupt trigger level */
#define LTQ_SPI_RXFCON_RXFLU	BIT(1)	/* FIFO flush */
#define LTQ_SPI_RXFCON_RXFEN	BIT(0)	/* FIFO enable */

#define LTQ_SPI_TXFCON_TXFITL_S	8	/* FIFO interrupt trigger level */
#define LTQ_SPI_TXFCON_TXFLU	BIT(1)	/* FIFO flush */
#define LTQ_SPI_TXFCON_TXFEN	BIT(0)	/* FIFO enable */

#define LTQ_SPI_FSTAT_RXFFL_S	0
#define LTQ_SPI_FSTAT_TXFFL_S	8

#define LTQ_SPI_GPOCON_ISCSBN_S	8
#define LTQ_SPI_GPOCON_INVOUTN_S	0

#define LTQ_SPI_FGPO_SETOUTN_S	8
#define LTQ_SPI_FGPO_CLROUTN_S	0

#define LTQ_SPI_RXREQ_RXCNT_M	0xFFFF	/* Receive count value */
#define LTQ_SPI_RXCNT_TODO_M	0xFFFF	/* Recevie to-do value */

#define LTQ_SPI_IRNEN_TFI	BIT(4)	/* TX finished interrupt */
#define LTQ_SPI_IRNEN_F		BIT(3)	/* Frame end interrupt request */
#define LTQ_SPI_IRNEN_E		BIT(2)	/* Error end interrupt request */
#define LTQ_SPI_IRNEN_T_XWAY	BIT(1)	/* Transmit end interrupt request */
#define LTQ_SPI_IRNEN_R_XWAY	BIT(0)	/* Receive end interrupt request */
#define LTQ_SPI_IRNEN_R_XRX	BIT(1)	/* Transmit end interrupt request */
#define LTQ_SPI_IRNEN_T_XRX	BIT(0)	/* Receive end interrupt request */
#define LTQ_SPI_IRNEN_ALL	0x1F

struct lantiq_ssc_spi;

struct lantiq_ssc_hwcfg {
	int (*cfg_irq)(struct platform_device *pdev, struct lantiq_ssc_spi *spi);
	unsigned int	irnen_r;
	unsigned int	irnen_t;
	unsigned int	irncr;
	unsigned int	irnicr;
	bool		irq_ack;
	u32		fifo_size_mask;
};

struct lantiq_ssc_spi {
	struct spi_master		*master;
	struct device			*dev;
	void __iomem			*regbase;
	struct clk			*spi_clk;
	struct clk			*fpi_clk;
	const struct lantiq_ssc_hwcfg	*hwcfg;

	spinlock_t			lock;
	struct workqueue_struct		*wq;
	struct work_struct		work;

	const u8			*tx;
	u8				*rx;
	unsigned int			tx_todo;
	unsigned int			rx_todo;
	unsigned int			bits_per_word;
	unsigned int			speed_hz;
	unsigned int			tx_fifo_size;
	unsigned int			rx_fifo_size;
	unsigned int			base_cs;
	unsigned int			fdx_tx_level;
};

static u32 lantiq_ssc_readl(const struct lantiq_ssc_spi *spi, u32 reg)
{
	return __raw_readl(spi->regbase + reg);
}

static void lantiq_ssc_writel(const struct lantiq_ssc_spi *spi, u32 val,
			      u32 reg)
{
	__raw_writel(val, spi->regbase + reg);
}

static void lantiq_ssc_maskl(const struct lantiq_ssc_spi *spi, u32 clr,
			     u32 set, u32 reg)
{
	u32 val = __raw_readl(spi->regbase + reg);

	val &= ~clr;
	val |= set;
	__raw_writel(val, spi->regbase + reg);
}

static unsigned int tx_fifo_level(const struct lantiq_ssc_spi *spi)
{
	const struct lantiq_ssc_hwcfg *hwcfg = spi->hwcfg;
	u32 fstat = lantiq_ssc_readl(spi, LTQ_SPI_FSTAT);

	return (fstat >> LTQ_SPI_FSTAT_TXFFL_S) & hwcfg->fifo_size_mask;
}

static unsigned int rx_fifo_level(const struct lantiq_ssc_spi *spi)
{
	const struct lantiq_ssc_hwcfg *hwcfg = spi->hwcfg;
	u32 fstat = lantiq_ssc_readl(spi, LTQ_SPI_FSTAT);

	return (fstat >> LTQ_SPI_FSTAT_RXFFL_S) & hwcfg->fifo_size_mask;
}

static unsigned int tx_fifo_free(const struct lantiq_ssc_spi *spi)
{
	return spi->tx_fifo_size - tx_fifo_level(spi);
}

static void rx_fifo_reset(const struct lantiq_ssc_spi *spi)
{
	u32 val = spi->rx_fifo_size << LTQ_SPI_RXFCON_RXFITL_S;

	val |= LTQ_SPI_RXFCON_RXFEN | LTQ_SPI_RXFCON_RXFLU;
	lantiq_ssc_writel(spi, val, LTQ_SPI_RXFCON);
}

static void tx_fifo_reset(const struct lantiq_ssc_spi *spi)
{
	u32 val = 1 << LTQ_SPI_TXFCON_TXFITL_S;

	val |= LTQ_SPI_TXFCON_TXFEN | LTQ_SPI_TXFCON_TXFLU;
	lantiq_ssc_writel(spi, val, LTQ_SPI_TXFCON);
}

static void rx_fifo_flush(const struct lantiq_ssc_spi *spi)
{
	lantiq_ssc_maskl(spi, 0, LTQ_SPI_RXFCON_RXFLU, LTQ_SPI_RXFCON);
}

static void tx_fifo_flush(const struct lantiq_ssc_spi *spi)
{
	lantiq_ssc_maskl(spi, 0, LTQ_SPI_TXFCON_TXFLU, LTQ_SPI_TXFCON);
}

static void hw_enter_config_mode(const struct lantiq_ssc_spi *spi)
{
	lantiq_ssc_writel(spi, LTQ_SPI_WHBSTATE_CLREN, LTQ_SPI_WHBSTATE);
}

static void hw_enter_active_mode(const struct lantiq_ssc_spi *spi)
{
	lantiq_ssc_writel(spi, LTQ_SPI_WHBSTATE_SETEN, LTQ_SPI_WHBSTATE);
}

static void hw_setup_speed_hz(const struct lantiq_ssc_spi *spi,
			      unsigned int max_speed_hz)
{
	u32 spi_clk, brt;

	/*
	 * SPI module clock is derived from FPI bus clock dependent on
	 * divider value in CLC.RMS which is always set to 1.
	 *
	 *                 f_SPI
	 * baudrate = --------------
	 *             2 * (BR + 1)
	 */
	spi_clk = clk_get_rate(spi->fpi_clk) / 2;

	if (max_speed_hz > spi_clk)
		brt = 0;
	else
		brt = spi_clk / max_speed_hz - 1;

	if (brt > 0xFFFF)
		brt = 0xFFFF;

	dev_dbg(spi->dev, "spi_clk %u, max_speed_hz %u, brt %u\n",
		spi_clk, max_speed_hz, brt);

	lantiq_ssc_writel(spi, brt, LTQ_SPI_BRT);
}

static void hw_setup_bits_per_word(const struct lantiq_ssc_spi *spi,
				   unsigned int bits_per_word)
{
	u32 bm;

	/* CON.BM value = bits_per_word - 1 */
	bm = (bits_per_word - 1) << LTQ_SPI_CON_BM_S;

	lantiq_ssc_maskl(spi, LTQ_SPI_CON_BM_M, bm, LTQ_SPI_CON);
}

static void hw_setup_clock_mode(const struct lantiq_ssc_spi *spi,
				unsigned int mode)
{
	u32 con_set = 0, con_clr = 0;

	/*
	 * SPI mode mapping in CON register:
	 * Mode CPOL CPHA CON.PO CON.PH
	 *  0    0    0      0      1
	 *  1    0    1      0      0
	 *  2    1    0      1      1
	 *  3    1    1      1      0
	 */
	if (mode & SPI_CPHA)
		con_clr |= LTQ_SPI_CON_PH;
	else
		con_set |= LTQ_SPI_CON_PH;

	if (mode & SPI_CPOL)
		con_set |= LTQ_SPI_CON_PO | LTQ_SPI_CON_IDLE;
	else
		con_clr |= LTQ_SPI_CON_PO | LTQ_SPI_CON_IDLE;

	/* Set heading control */
	if (mode & SPI_LSB_FIRST)
		con_clr |= LTQ_SPI_CON_HB;
	else
		con_set |= LTQ_SPI_CON_HB;

	/* Set loopback mode */
	if (mode & SPI_LOOP)
		con_set |= LTQ_SPI_CON_LB;
	else
		con_clr |= LTQ_SPI_CON_LB;

	lantiq_ssc_maskl(spi, con_clr, con_set, LTQ_SPI_CON);
}

static void lantiq_ssc_hw_init(const struct lantiq_ssc_spi *spi)
{
	const struct lantiq_ssc_hwcfg *hwcfg = spi->hwcfg;

	/*
	 * Set clock divider for run mode to 1 to
	 * run at same frequency as FPI bus
	 */
	lantiq_ssc_writel(spi, 1 << LTQ_SPI_CLC_RMC_S, LTQ_SPI_CLC);

	/* Put controller into config mode */
	hw_enter_config_mode(spi);

	/* Clear error flags */
	lantiq_ssc_maskl(spi, 0, LTQ_SPI_WHBSTATE_CLR_ERRORS, LTQ_SPI_WHBSTATE);

	/* Enable error checking, disable TX/RX */
	lantiq_ssc_writel(spi, LTQ_SPI_CON_RUEN | LTQ_SPI_CON_AEN |
		LTQ_SPI_CON_TEN | LTQ_SPI_CON_REN | LTQ_SPI_CON_TXOFF |
		LTQ_SPI_CON_RXOFF, LTQ_SPI_CON);

	/* Setup default SPI mode */
	hw_setup_bits_per_word(spi, spi->bits_per_word);
	hw_setup_clock_mode(spi, SPI_MODE_0);

	/* Enable master mode and clear error flags */
	lantiq_ssc_writel(spi, LTQ_SPI_WHBSTATE_SETMS |
			       LTQ_SPI_WHBSTATE_CLR_ERRORS,
			       LTQ_SPI_WHBSTATE);

	/* Reset GPIO/CS registers */
	lantiq_ssc_writel(spi, 0, LTQ_SPI_GPOCON);
	lantiq_ssc_writel(spi, 0xFF00, LTQ_SPI_FPGO);

	/* Enable and flush FIFOs */
	rx_fifo_reset(spi);
	tx_fifo_reset(spi);

	/* Enable interrupts */
	lantiq_ssc_writel(spi, hwcfg->irnen_t | hwcfg->irnen_r |
			  LTQ_SPI_IRNEN_E, LTQ_SPI_IRNEN);
}

static int lantiq_ssc_setup(struct spi_device *spidev)
{
	struct spi_master *master = spidev->master;
	struct lantiq_ssc_spi *spi = spi_master_get_devdata(master);
	unsigned int cs = spidev->chip_select;
	u32 gpocon;

	/* GPIOs are used for CS */
	if (spidev->cs_gpiod)
		return 0;

	dev_dbg(spi->dev, "using internal chipselect %u\n", cs);

	if (cs < spi->base_cs) {
		dev_err(spi->dev,
			"chipselect %i too small (min %i)\n", cs, spi->base_cs);
		return -EINVAL;
	}

	/* set GPO pin to CS mode */
	gpocon = 1 << ((cs - spi->base_cs) + LTQ_SPI_GPOCON_ISCSBN_S);

	/* invert GPO pin */
	if (spidev->mode & SPI_CS_HIGH)
		gpocon |= 1 << (cs - spi->base_cs);

	lantiq_ssc_maskl(spi, 0, gpocon, LTQ_SPI_GPOCON);

	return 0;
}

static int lantiq_ssc_prepare_message(struct spi_master *master,
				      struct spi_message *message)
{
	struct lantiq_ssc_spi *spi = spi_master_get_devdata(master);

	hw_enter_config_mode(spi);
	hw_setup_clock_mode(spi, message->spi->mode);
	hw_enter_active_mode(spi);

	return 0;
}

static void hw_setup_transfer(struct lantiq_ssc_spi *spi,
			      struct spi_device *spidev, struct spi_transfer *t)
{
	unsigned int speed_hz = t->speed_hz;
	unsigned int bits_per_word = t->bits_per_word;
	u32 con;

	if (bits_per_word != spi->bits_per_word ||
		speed_hz != spi->speed_hz) {
		hw_enter_config_mode(spi);
		hw_setup_speed_hz(spi, speed_hz);
		hw_setup_bits_per_word(spi, bits_per_word);
		hw_enter_active_mode(spi);

		spi->speed_hz = speed_hz;
		spi->bits_per_word = bits_per_word;
	}

	/* Configure transmitter and receiver */
	con = lantiq_ssc_readl(spi, LTQ_SPI_CON);
	if (t->tx_buf)
		con &= ~LTQ_SPI_CON_TXOFF;
	else
		con |= LTQ_SPI_CON_TXOFF;

	if (t->rx_buf)
		con &= ~LTQ_SPI_CON_RXOFF;
	else
		con |= LTQ_SPI_CON_RXOFF;

	lantiq_ssc_writel(spi, con, LTQ_SPI_CON);
}

static int lantiq_ssc_unprepare_message(struct spi_master *master,
					struct spi_message *message)
{
	struct lantiq_ssc_spi *spi = spi_master_get_devdata(master);

	flush_workqueue(spi->wq);

	/* Disable transmitter and receiver while idle */
	lantiq_ssc_maskl(spi, 0, LTQ_SPI_CON_TXOFF | LTQ_SPI_CON_RXOFF,
			 LTQ_SPI_CON);

	return 0;
}

static void tx_fifo_write(struct lantiq_ssc_spi *spi)
{
	const u8 *tx8;
	const u16 *tx16;
	const u32 *tx32;
	u32 data;
	unsigned int tx_free = tx_fifo_free(spi);

	spi->fdx_tx_level = 0;
	while (spi->tx_todo && tx_free) {
		switch (spi->bits_per_word) {
		case 2 ... 8:
			tx8 = spi->tx;
			data = *tx8;
			spi->tx_todo--;
			spi->tx++;
			break;
		case 16:
			tx16 = (u16 *) spi->tx;
			data = *tx16;
			spi->tx_todo -= 2;
			spi->tx += 2;
			break;
		case 32:
			tx32 = (u32 *) spi->tx;
			data = *tx32;
			spi->tx_todo -= 4;
			spi->tx += 4;
			break;
		default:
			WARN_ON(1);
			data = 0;
			break;
		}

		lantiq_ssc_writel(spi, data, LTQ_SPI_TB);
		tx_free--;
		spi->fdx_tx_level++;
	}
}

static void rx_fifo_read_full_duplex(struct lantiq_ssc_spi *spi)
{
	u8 *rx8;
	u16 *rx16;
	u32 *rx32;
	u32 data;
	unsigned int rx_fill = rx_fifo_level(spi);

	/*
	 * Wait until all expected data to be shifted in.
	 * Otherwise, rx overrun may occur.
	 */
	while (rx_fill != spi->fdx_tx_level)
		rx_fill = rx_fifo_level(spi);

	while (rx_fill) {
		data = lantiq_ssc_readl(spi, LTQ_SPI_RB);

		switch (spi->bits_per_word) {
		case 2 ... 8:
			rx8 = spi->rx;
			*rx8 = data;
			spi->rx_todo--;
			spi->rx++;
			break;
		case 16:
			rx16 = (u16 *) spi->rx;
			*rx16 = data;
			spi->rx_todo -= 2;
			spi->rx += 2;
			break;
		case 32:
			rx32 = (u32 *) spi->rx;
			*rx32 = data;
			spi->rx_todo -= 4;
			spi->rx += 4;
			break;
		default:
			WARN_ON(1);
			break;
		}

		rx_fill--;
	}
}

static void rx_fifo_read_half_duplex(struct lantiq_ssc_spi *spi)
{
	u32 data, *rx32;
	u8 *rx8;
	unsigned int rxbv, shift;
	unsigned int rx_fill = rx_fifo_level(spi);

	/*
	 * In RX-only mode the bits per word value is ignored by HW. A value
	 * of 32 is used instead. Thus all 4 bytes per FIFO must be read.
	 * If remaining RX bytes are less than 4, the FIFO must be read
	 * differently. The amount of received and valid bytes is indicated
	 * by STAT.RXBV register value.
	 */
	while (rx_fill) {
		if (spi->rx_todo < 4)  {
			rxbv = (lantiq_ssc_readl(spi, LTQ_SPI_STAT) &
				LTQ_SPI_STAT_RXBV_M) >> LTQ_SPI_STAT_RXBV_S;
			data = lantiq_ssc_readl(spi, LTQ_SPI_RB);

			shift = (rxbv - 1) * 8;
			rx8 = spi->rx;

			while (rxbv) {
				*rx8++ = (data >> shift) & 0xFF;
				rxbv--;
				shift -= 8;
				spi->rx_todo--;
				spi->rx++;
			}
		} else {
			data = lantiq_ssc_readl(spi, LTQ_SPI_RB);
			rx32 = (u32 *) spi->rx;

			*rx32++ = data;
			spi->rx_todo -= 4;
			spi->rx += 4;
		}
		rx_fill--;
	}
}

static void rx_request(struct lantiq_ssc_spi *spi)
{
	unsigned int rxreq, rxreq_max;

	/*
	 * To avoid receive overflows at high clocks it is better to request
	 * only the amount of bytes that fits into all FIFOs. This value
	 * depends on the FIFO size implemented in hardware.
	 */
	rxreq = spi->rx_todo;
	rxreq_max = spi->rx_fifo_size * 4;
	if (rxreq > rxreq_max)
		rxreq = rxreq_max;

	lantiq_ssc_writel(spi, rxreq, LTQ_SPI_RXREQ);
}

static irqreturn_t lantiq_ssc_xmit_interrupt(int irq, void *data)
{
	struct lantiq_ssc_spi *spi = data;
	const struct lantiq_ssc_hwcfg *hwcfg = spi->hwcfg;
	u32 val = lantiq_ssc_readl(spi, hwcfg->irncr);

	spin_lock(&spi->lock);
	if (hwcfg->irq_ack)
		lantiq_ssc_writel(spi, val, hwcfg->irncr);

	if (spi->tx) {
		if (spi->rx && spi->rx_todo)
			rx_fifo_read_full_duplex(spi);

		if (spi->tx_todo)
			tx_fifo_write(spi);
		else if (!tx_fifo_level(spi))
			goto completed;
	} else if (spi->rx) {
		if (spi->rx_todo) {
			rx_fifo_read_half_duplex(spi);

			if (spi->rx_todo)
				rx_request(spi);
			else
				goto completed;
		} else {
			goto completed;
		}
	}

	spin_unlock(&spi->lock);
	return IRQ_HANDLED;

completed:
	queue_work(spi->wq, &spi->work);
	spin_unlock(&spi->lock);

	return IRQ_HANDLED;
}

static irqreturn_t lantiq_ssc_err_interrupt(int irq, void *data)
{
	struct lantiq_ssc_spi *spi = data;
	const struct lantiq_ssc_hwcfg *hwcfg = spi->hwcfg;
	u32 stat = lantiq_ssc_readl(spi, LTQ_SPI_STAT);
	u32 val = lantiq_ssc_readl(spi, hwcfg->irncr);

	if (!(stat & LTQ_SPI_STAT_ERRORS))
		return IRQ_NONE;

	spin_lock(&spi->lock);
	if (hwcfg->irq_ack)
		lantiq_ssc_writel(spi, val, hwcfg->irncr);

	if (stat & LTQ_SPI_STAT_RUE)
		dev_err(spi->dev, "receive underflow error\n");
	if (stat & LTQ_SPI_STAT_TUE)
		dev_err(spi->dev, "transmit underflow error\n");
	if (stat & LTQ_SPI_STAT_AE)
		dev_err(spi->dev, "abort error\n");
	if (stat & LTQ_SPI_STAT_RE)
		dev_err(spi->dev, "receive overflow error\n");
	if (stat & LTQ_SPI_STAT_TE)
		dev_err(spi->dev, "transmit overflow error\n");
	if (stat & LTQ_SPI_STAT_ME)
		dev_err(spi->dev, "mode error\n");

	/* Clear error flags */
	lantiq_ssc_maskl(spi, 0, LTQ_SPI_WHBSTATE_CLR_ERRORS, LTQ_SPI_WHBSTATE);

	/* set bad status so it can be retried */
	if (spi->master->cur_msg)
		spi->master->cur_msg->status = -EIO;
	queue_work(spi->wq, &spi->work);
	spin_unlock(&spi->lock);

	return IRQ_HANDLED;
}

static irqreturn_t intel_lgm_ssc_isr(int irq, void *data)
{
	struct lantiq_ssc_spi *spi = data;
	const struct lantiq_ssc_hwcfg *hwcfg = spi->hwcfg;
	u32 val = lantiq_ssc_readl(spi, hwcfg->irncr);

	if (!(val & LTQ_SPI_IRNEN_ALL))
		return IRQ_NONE;

	if (val & LTQ_SPI_IRNEN_E)
		return lantiq_ssc_err_interrupt(irq, data);

	if ((val & hwcfg->irnen_t) || (val & hwcfg->irnen_r))
		return lantiq_ssc_xmit_interrupt(irq, data);

	return IRQ_HANDLED;
}

static int transfer_start(struct lantiq_ssc_spi *spi, struct spi_device *spidev,
			  struct spi_transfer *t)
{
	unsigned long flags;

	spin_lock_irqsave(&spi->lock, flags);

	spi->tx = t->tx_buf;
	spi->rx = t->rx_buf;

	if (t->tx_buf) {
		spi->tx_todo = t->len;

		/* initially fill TX FIFO */
		tx_fifo_write(spi);
	}

	if (spi->rx) {
		spi->rx_todo = t->len;

		/* start shift clock in RX-only mode */
		if (!spi->tx)
			rx_request(spi);
	}

	spin_unlock_irqrestore(&spi->lock, flags);

	return t->len;
}

/*
 * The driver only gets an interrupt when the FIFO is empty, but there
 * is an additional shift register from which the data is written to
 * the wire. We get the last interrupt when the controller starts to
 * write the last word to the wire, not when it is finished. Do busy
 * waiting till it finishes.
 */
static void lantiq_ssc_bussy_work(struct work_struct *work)
{
	struct lantiq_ssc_spi *spi;
	unsigned long long timeout = 8LL * 1000LL;
	unsigned long end;

	spi = container_of(work, typeof(*spi), work);

	do_div(timeout, spi->speed_hz);
	timeout += timeout + 100; /* some tolerance */

	end = jiffies + msecs_to_jiffies(timeout);
	do {
		u32 stat = lantiq_ssc_readl(spi, LTQ_SPI_STAT);

		if (!(stat & LTQ_SPI_STAT_BSY)) {
			spi_finalize_current_transfer(spi->master);
			return;
		}

		cond_resched();
	} while (!time_after_eq(jiffies, end));

	if (spi->master->cur_msg)
		spi->master->cur_msg->status = -EIO;
	spi_finalize_current_transfer(spi->master);
}

static void lantiq_ssc_handle_err(struct spi_master *master,
				  struct spi_message *message)
{
	struct lantiq_ssc_spi *spi = spi_master_get_devdata(master);

	/* flush FIFOs on timeout */
	rx_fifo_flush(spi);
	tx_fifo_flush(spi);
}

static void lantiq_ssc_set_cs(struct spi_device *spidev, bool enable)
{
	struct lantiq_ssc_spi *spi = spi_master_get_devdata(spidev->master);
	unsigned int cs = spidev->chip_select;
	u32 fgpo;

	if (!!(spidev->mode & SPI_CS_HIGH) == enable)
		fgpo = (1 << (cs - spi->base_cs));
	else
		fgpo = (1 << (cs - spi->base_cs + LTQ_SPI_FGPO_SETOUTN_S));

	lantiq_ssc_writel(spi, fgpo, LTQ_SPI_FPGO);
}

static int lantiq_ssc_transfer_one(struct spi_master *master,
				   struct spi_device *spidev,
				   struct spi_transfer *t)
{
	struct lantiq_ssc_spi *spi = spi_master_get_devdata(master);

	hw_setup_transfer(spi, spidev, t);

	return transfer_start(spi, spidev, t);
}

static int intel_lgm_cfg_irq(struct platform_device *pdev, struct lantiq_ssc_spi *spi)
{
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	return devm_request_irq(&pdev->dev, irq, intel_lgm_ssc_isr, 0, "spi", spi);
}

static int lantiq_cfg_irq(struct platform_device *pdev, struct lantiq_ssc_spi *spi)
{
	int irq, err;

	irq = platform_get_irq_byname(pdev, LTQ_SPI_RX_IRQ_NAME);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, lantiq_ssc_xmit_interrupt,
			       0, LTQ_SPI_RX_IRQ_NAME, spi);
	if (err)
		return err;

	irq = platform_get_irq_byname(pdev, LTQ_SPI_TX_IRQ_NAME);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, lantiq_ssc_xmit_interrupt,
			       0, LTQ_SPI_TX_IRQ_NAME, spi);

	if (err)
		return err;

	irq = platform_get_irq_byname(pdev, LTQ_SPI_ERR_IRQ_NAME);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, lantiq_ssc_err_interrupt,
			       0, LTQ_SPI_ERR_IRQ_NAME, spi);
	return err;
}

static const struct lantiq_ssc_hwcfg lantiq_ssc_xway = {
	.cfg_irq	= lantiq_cfg_irq,
	.irnen_r	= LTQ_SPI_IRNEN_R_XWAY,
	.irnen_t	= LTQ_SPI_IRNEN_T_XWAY,
	.irnicr		= 0xF8,
	.irncr		= 0xFC,
	.fifo_size_mask	= GENMASK(5, 0),
	.irq_ack	= false,
};

static const struct lantiq_ssc_hwcfg lantiq_ssc_xrx = {
	.cfg_irq	= lantiq_cfg_irq,
	.irnen_r	= LTQ_SPI_IRNEN_R_XRX,
	.irnen_t	= LTQ_SPI_IRNEN_T_XRX,
	.irnicr		= 0xF8,
	.irncr		= 0xFC,
	.fifo_size_mask	= GENMASK(5, 0),
	.irq_ack	= false,
};

static const struct lantiq_ssc_hwcfg intel_ssc_lgm = {
	.cfg_irq	= intel_lgm_cfg_irq,
	.irnen_r	= LTQ_SPI_IRNEN_R_XRX,
	.irnen_t	= LTQ_SPI_IRNEN_T_XRX,
	.irnicr		= 0xFC,
	.irncr		= 0xF8,
	.fifo_size_mask	= GENMASK(7, 0),
	.irq_ack	= true,
};

static const struct of_device_id lantiq_ssc_match[] = {
	{ .compatible = "lantiq,ase-spi", .data = &lantiq_ssc_xway, },
	{ .compatible = "lantiq,falcon-spi", .data = &lantiq_ssc_xrx, },
	{ .compatible = "lantiq,xrx100-spi", .data = &lantiq_ssc_xrx, },
	{ .compatible = "intel,lgm-spi", .data = &intel_ssc_lgm, },
	{},
};
MODULE_DEVICE_TABLE(of, lantiq_ssc_match);

static int lantiq_ssc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_master *master;
	struct lantiq_ssc_spi *spi;
	const struct lantiq_ssc_hwcfg *hwcfg;
	u32 id, supports_dma, revision;
	unsigned int num_cs;
	int err;

	hwcfg = of_device_get_match_data(dev);

	master = spi_alloc_master(dev, sizeof(struct lantiq_ssc_spi));
	if (!master)
		return -ENOMEM;

	spi = spi_master_get_devdata(master);
	spi->master = master;
	spi->dev = dev;
	spi->hwcfg = hwcfg;
	platform_set_drvdata(pdev, spi);
	spi->regbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spi->regbase)) {
		err = PTR_ERR(spi->regbase);
		goto err_master_put;
	}

	err = hwcfg->cfg_irq(pdev, spi);
	if (err)
		goto err_master_put;

	spi->spi_clk = devm_clk_get(dev, "gate");
	if (IS_ERR(spi->spi_clk)) {
		err = PTR_ERR(spi->spi_clk);
		goto err_master_put;
	}
	err = clk_prepare_enable(spi->spi_clk);
	if (err)
		goto err_master_put;

	/*
	 * Use the old clk_get_fpi() function on Lantiq platform, till it
	 * supports common clk.
	 */
#if defined(CONFIG_LANTIQ) && !defined(CONFIG_COMMON_CLK)
	spi->fpi_clk = clk_get_fpi();
#else
	spi->fpi_clk = clk_get(dev, "freq");
#endif
	if (IS_ERR(spi->fpi_clk)) {
		err = PTR_ERR(spi->fpi_clk);
		goto err_clk_disable;
	}

	num_cs = 8;
	of_property_read_u32(pdev->dev.of_node, "num-cs", &num_cs);

	spi->base_cs = 1;
	of_property_read_u32(pdev->dev.of_node, "base-cs", &spi->base_cs);

	spin_lock_init(&spi->lock);
	spi->bits_per_word = 8;
	spi->speed_hz = 0;

	master->dev.of_node = pdev->dev.of_node;
	master->num_chipselect = num_cs;
	master->use_gpio_descriptors = true;
	master->setup = lantiq_ssc_setup;
	master->set_cs = lantiq_ssc_set_cs;
	master->handle_err = lantiq_ssc_handle_err;
	master->prepare_message = lantiq_ssc_prepare_message;
	master->unprepare_message = lantiq_ssc_unprepare_message;
	master->transfer_one = lantiq_ssc_transfer_one;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST | SPI_CS_HIGH |
				SPI_LOOP;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 8) |
				     SPI_BPW_MASK(16) | SPI_BPW_MASK(32);

	spi->wq = alloc_ordered_workqueue(dev_name(dev), WQ_MEM_RECLAIM);
	if (!spi->wq) {
		err = -ENOMEM;
		goto err_clk_put;
	}
	INIT_WORK(&spi->work, lantiq_ssc_bussy_work);

	id = lantiq_ssc_readl(spi, LTQ_SPI_ID);
	spi->tx_fifo_size = (id >> LTQ_SPI_ID_TXFS_S) & hwcfg->fifo_size_mask;
	spi->rx_fifo_size = (id >> LTQ_SPI_ID_RXFS_S) & hwcfg->fifo_size_mask;
	supports_dma = (id & LTQ_SPI_ID_CFG_M) >> LTQ_SPI_ID_CFG_S;
	revision = id & LTQ_SPI_ID_REV_M;

	lantiq_ssc_hw_init(spi);

	dev_info(dev,
		"Lantiq SSC SPI controller (Rev %i, TXFS %u, RXFS %u, DMA %u)\n",
		revision, spi->tx_fifo_size, spi->rx_fifo_size, supports_dma);

	err = devm_spi_register_master(dev, master);
	if (err) {
		dev_err(dev, "failed to register spi_master\n");
		goto err_wq_destroy;
	}

	return 0;

err_wq_destroy:
	destroy_workqueue(spi->wq);
err_clk_put:
	clk_put(spi->fpi_clk);
err_clk_disable:
	clk_disable_unprepare(spi->spi_clk);
err_master_put:
	spi_master_put(master);

	return err;
}

static int lantiq_ssc_remove(struct platform_device *pdev)
{
	struct lantiq_ssc_spi *spi = platform_get_drvdata(pdev);

	lantiq_ssc_writel(spi, 0, LTQ_SPI_IRNEN);
	lantiq_ssc_writel(spi, 0, LTQ_SPI_CLC);
	rx_fifo_flush(spi);
	tx_fifo_flush(spi);
	hw_enter_config_mode(spi);

	destroy_workqueue(spi->wq);
	clk_disable_unprepare(spi->spi_clk);
	clk_put(spi->fpi_clk);

	return 0;
}

static struct platform_driver lantiq_ssc_driver = {
	.probe = lantiq_ssc_probe,
	.remove = lantiq_ssc_remove,
	.driver = {
		.name = "spi-lantiq-ssc",
		.of_match_table = lantiq_ssc_match,
	},
};
module_platform_driver(lantiq_ssc_driver);

MODULE_DESCRIPTION("Lantiq SSC SPI controller driver");
MODULE_AUTHOR("Daniel Schwierzeck <daniel.schwierzeck@gmail.com>");
MODULE_AUTHOR("Hauke Mehrtens <hauke@hauke-m.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spi-lantiq-ssc");
