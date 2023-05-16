// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Mellanox Technologies. All rights reserved.
// Copyright (c) 2018 Oleksandr Shamray <oleksandrs@mellanox.com>
// Copyright (c) 2019 Intel Corporation

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/jtag.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <uapi/linux/jtag.h>

#define ASPEED_JTAG_DATA		0x00
#define ASPEED_JTAG_INST		0x04
#define ASPEED_JTAG_CTRL		0x08
#define ASPEED_JTAG_ISR		0x0C
#define ASPEED_JTAG_SW			0x10
#define ASPEED_JTAG_TCK		0x14
#define ASPEED_JTAG_EC			0x18

#define ASPEED_JTAG_DATA_MSB			0x01
#define ASPEED_JTAG_DATA_CHUNK_SIZE		0x20
#define ASPEED_JTAG_HW2_DATA_CHUNK_SIZE	512

/* ASPEED_JTAG_CTRL: Engine Control 24xx and 25xx series*/
#define ASPEED_JTAG_CTL_ENG_EN		BIT(31)
#define ASPEED_JTAG_CTL_ENG_OUT_EN	BIT(30)
#define ASPEED_JTAG_CTL_FORCE_TMS	BIT(29)
#define ASPEED_JTAG_CTL_IR_UPDATE	BIT(26)
#define ASPEED_JTAG_CTL_INST_LEN(x)	((x) << 20)
#define ASPEED_JTAG_CTL_LASPEED_INST	BIT(17)
#define ASPEED_JTAG_CTL_INST_EN	BIT(16)
#define ASPEED_JTAG_CTL_DR_UPDATE	BIT(10)
#define ASPEED_JTAG_CTL_DATA_LEN(x)	((x) << 4)
#define ASPEED_JTAG_CTL_LASPEED_DATA	BIT(1)
#define ASPEED_JTAG_CTL_DATA_EN	BIT(0)

/* ASPEED_JTAG_CTRL: Engine Control 26xx series*/
#define ASPEED_JTAG_CTL_26XX_RESET_FIFO	BIT(21)
#define ASPEED_JTAG_CTL_26XX_FIFO_MODE_CTRL	BIT(20)
#define ASPEED_JTAG_CTL_26XX_TRANS_LEN(x)	((x) << 8)
#define ASPEED_JTAG_CTL_26XX_TRANS_MASK	GENMASK(17, 8)
#define ASPEED_JTAG_CTL_26XX_MSB_FIRST		BIT(6)
#define ASPEED_JTAG_CTL_26XX_TERM_TRANS	BIT(5)
#define ASPEED_JTAG_CTL_26XX_LASPEED_TRANS	BIT(4)
#define ASPEED_JTAG_CTL_26XX_INST_EN		BIT(1)

/* ASPEED_JTAG_ISR : Interrupt status and enable */
#define ASPEED_JTAG_ISR_INST_PAUSE		BIT(19)
#define ASPEED_JTAG_ISR_INST_COMPLETE		BIT(18)
#define ASPEED_JTAG_ISR_DATA_PAUSE		BIT(17)
#define ASPEED_JTAG_ISR_DATA_COMPLETE		BIT(16)
#define ASPEED_JTAG_ISR_INST_PAUSE_EN		BIT(3)
#define ASPEED_JTAG_ISR_INST_COMPLETE_EN	BIT(2)
#define ASPEED_JTAG_ISR_DATA_PAUSE_EN		BIT(1)
#define ASPEED_JTAG_ISR_DATA_COMPLETE_EN	BIT(0)
#define ASPEED_JTAG_ISR_INT_EN_MASK		GENMASK(3, 0)
#define ASPEED_JTAG_ISR_INT_MASK		GENMASK(19, 16)

/* ASPEED_JTAG_SW : Software Mode and Status */
#define ASPEED_JTAG_SW_MODE_EN			BIT(19)
#define ASPEED_JTAG_SW_MODE_TCK		BIT(18)
#define ASPEED_JTAG_SW_MODE_TMS		BIT(17)
#define ASPEED_JTAG_SW_MODE_TDIO		BIT(16)

/* ASPEED_JTAG_TCK : TCK Control */
#define ASPEED_JTAG_TCK_DIVISOR_MASK	GENMASK(10, 0)
#define ASPEED_JTAG_TCK_GET_DIV(x)	((x) & ASPEED_JTAG_TCK_DIVISOR_MASK)

/* ASPEED_JTAG_EC : Controller set for go to IDLE */
#define ASPEED_JTAG_EC_GO_IDLE		BIT(0)

#define ASPEED_JTAG_IOUT_LEN(len) \
	(ASPEED_JTAG_CTL_ENG_EN | \
	 ASPEED_JTAG_CTL_ENG_OUT_EN | \
	 ASPEED_JTAG_CTL_INST_LEN(len))

#define ASPEED_JTAG_DOUT_LEN(len) \
	(ASPEED_JTAG_CTL_ENG_EN | \
	 ASPEED_JTAG_CTL_ENG_OUT_EN | \
	 ASPEED_JTAG_CTL_DATA_LEN(len))

#define ASPEED_JTAG_TRANS_LEN(len) \
	(ASPEED_JTAG_CTL_ENG_EN | \
	 ASPEED_JTAG_CTL_ENG_OUT_EN | \
	 ASPEED_JTAG_CTL_26XX_TRANS_LEN(len))

#define ASPEED_JTAG_SW_TDIO (ASPEED_JTAG_SW_MODE_EN | ASPEED_JTAG_SW_MODE_TDIO)

#define ASPEED_JTAG_GET_TDI(direction, byte) \
	(((direction) & JTAG_WRITE_XFER) ? byte : UINT_MAX)

#define ASPEED_JTAG_TCK_WAIT		10
#define ASPEED_JTAG_RESET_CNTR		10
#define WAIT_ITERATIONS		300

/* Use this macro to switch between HW mode 1(comment out) and 2(defined)  */
#define ASPEED_JTAG_HW_MODE_2_ENABLE	1

/* ASPEED JTAG HW MODE 2 (Only supported in AST26xx series) */
#define ASPEED_JTAG_SHDATA		0x20
#define ASPEED_JTAG_SHINST		0x24
#define ASPEED_JTAG_PADCTRL0		0x28
#define ASPEED_JTAG_PADCTRL1		0x2C
#define ASPEED_JTAG_SHCTRL		0x30
#define ASPEED_JTAG_GBLCTRL		0x34
#define ASPEED_JTAG_INTCTRL		0x38
#define ASPEED_JTAG_STAT		0x3C

/* ASPEED_JTAG_PADCTRLx : Padding control 0 and 1 */
#define ASPEED_JTAG_PADCTRL_PAD_DATA	BIT(24)
#define ASPEED_JTAG_PADCTRL_POSTPAD(x)	(((x) & GENMASK(8, 0)) << 12)
#define ASPEED_JTAG_PADCTRL_PREPAD(x)	(((x) & GENMASK(8, 0)) << 0)

/* ASPEED_JTAG_SHCTRL: Shift Control */
#define ASPEED_JTAG_SHCTRL_FRUN_TCK_EN	BIT(31)
#define ASPEED_JTAG_SHCTRL_STSHIFT_EN	BIT(30)
#define ASPEED_JTAG_SHCTRL_TMS(x)	(((x) & GENMASK(13, 0)) << 16)
#define ASPEED_JTAG_SHCTRL_POST_TMS(x)	(((x) & GENMASK(3, 0)) << 13)
#define ASPEED_JTAG_SHCTRL_PRE_TMS(x)	(((x) & GENMASK(3, 0)) << 10)
#define ASPEED_JTAG_SHCTRL_PAD_SEL0	(0)
#define ASPEED_JTAG_SHCTRL_PAD_SEL1	BIT(9)
#define ASPEED_JTAG_SHCTRL_END_SHIFT	BIT(8)
#define ASPEED_JTAG_SHCTRL_START_SHIFT	BIT(7)
#define ASPEED_JTAG_SHCTRL_LWRDT_SHIFT(x) ((x) & GENMASK(6, 0))

#define ASPEED_JTAG_END_SHIFT_DISABLED	0

/* ASPEED_JTAG_GBLCTRL : Global Control */
#define ASPEED_JTAG_GBLCTRL_ENG_MODE_EN	BIT(31)
#define ASPEED_JTAG_GBLCTRL_ENG_OUT_EN	BIT(30)
#define ASPEED_JTAG_GBLCTRL_FORCE_TMS	BIT(29)
#define ASPEED_JTAG_GBLCTRL_SHIFT_COMPLETE  BIT(28)
#define ASPEED_JTAG_GBLCTRL_RESET_FIFO	BIT(25)
#define ASPEED_JTAG_GBLCTRL_FIFO_CTRL_MODE	BIT(24)
#define ASPEED_JTAG_GBLCTRL_UPDT_SHIFT(x)	(((x) & GENMASK(9, 7)) << 13)
#define ASPEED_JTAG_GBLCTRL_STSHIFT(x)	(((x) & GENMASK(0, 0)) << 16)
#define ASPEED_JTAG_GBLCTRL_TRST	BIT(15)
#define ASPEED_JTAG_CLK_DIVISOR_MASK	GENMASK(11, 0)
#define ASPEED_JTAG_CLK_GET_DIV(x)	((x) & ASPEED_JTAG_CLK_DIVISOR_MASK)

/* ASPEED_JTAG_INTCTRL: Interrupt Control */
#define ASPEED_JTAG_INTCTRL_SHCPL_IRQ_EN BIT(16)
#define ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT BIT(0)

/* ASPEED_JTAG_STAT: JTAG HW mode 2 status */
#define ASPEED_JTAG_STAT_ENG_IDLE	BIT(0)

#define ASPEED_JTAG_MAX_PAD_SIZE	512

/* Use this macro to set us delay to WA the intensive R/W FIFO usage issue */
#define AST26XX_FIFO_UDELAY		2

/* Use this macro to set us delay for JTAG Master Controller to be programmed */
#define AST26XX_JTAG_CTRL_UDELAY	2

/*#define USE_INTERRUPTS*/
#define DEBUG_JTAG

static const char * const regnames[] = {
	[ASPEED_JTAG_DATA] = "ASPEED_JTAG_DATA",
	[ASPEED_JTAG_INST] = "ASPEED_JTAG_INST",
	[ASPEED_JTAG_CTRL] = "ASPEED_JTAG_CTRL",
	[ASPEED_JTAG_ISR]  = "ASPEED_JTAG_ISR",
	[ASPEED_JTAG_SW]   = "ASPEED_JTAG_SW",
	[ASPEED_JTAG_TCK]  = "ASPEED_JTAG_TCK",
	[ASPEED_JTAG_EC]   = "ASPEED_JTAG_EC",
	[ASPEED_JTAG_SHDATA]  = "ASPEED_JTAG_SHDATA",
	[ASPEED_JTAG_SHINST]  = "ASPEED_JTAG_SHINST",
	[ASPEED_JTAG_PADCTRL0] = "ASPEED_JTAG_PADCTRL0",
	[ASPEED_JTAG_PADCTRL1] = "ASPEED_JTAG_PADCTRL1",
	[ASPEED_JTAG_SHCTRL]   = "ASPEED_JTAG_SHCTRL",
	[ASPEED_JTAG_GBLCTRL]  = "ASPEED_JTAG_GBLCTRL",
	[ASPEED_JTAG_INTCTRL]  = "ASPEED_JTAG_INTCTRL",
	[ASPEED_JTAG_STAT]     = "ASPEED_JTAG_STAT",
};

#define ASPEED_JTAG_NAME		"jtag-aspeed"

struct aspeed_jtag {
	void __iomem			*reg_base;
	struct device			*dev;
	struct clk			*pclk;
	enum jtag_tapstate		status;
	int				irq;
	struct reset_control		*rst;
	u32				flag;
	wait_queue_head_t		jtag_wq;
	u32				mode;
	enum jtag_tapstate		current_state;
	const struct jtag_low_level_functions *llops;
	u32 pad_data_one[ASPEED_JTAG_MAX_PAD_SIZE / 32];
	u32 pad_data_zero[ASPEED_JTAG_MAX_PAD_SIZE / 32];
};

/*
 * Multi generation support is enabled by fops and low level assped function
 * mapping using asped_jtag_functions struct as config mechanism.
 */

struct jtag_low_level_functions {
	void (*output_disable)(struct aspeed_jtag *aspeed_jtag);
	void (*master_enable)(struct aspeed_jtag *aspeed_jtag);
	int (*xfer_push_data)(struct aspeed_jtag *aspeed_jtag,
			      enum jtag_xfer_type type, u32 bits_len);
	int (*xfer_push_data_last)(struct aspeed_jtag *aspeed_jtag,
				   enum jtag_xfer_type type, u32 bits_len);
	void (*xfer_sw)(struct aspeed_jtag *aspeed_jtag, struct jtag_xfer *xfer,
			u32 *data);
	int (*xfer_hw)(struct aspeed_jtag *aspeed_jtag, struct jtag_xfer *xfer,
		       u32 *data);
	void (*xfer_hw_fifo_delay)(void);
	void (*xfer_sw_delay)(struct aspeed_jtag *aspeed_jtag);
	irqreturn_t (*jtag_interrupt)(s32 this_irq, void *dev_id);
};

struct aspeed_jtag_functions {
	const struct jtag_ops *aspeed_jtag_ops;
	const struct jtag_low_level_functions *aspeed_jtag_llops;
};

#ifdef DEBUG_JTAG
static char *end_status_str[] = { "tlr",   "idle",  "selDR", "capDR",
				  "sDR",   "ex1DR", "pDR",   "ex2DR",
				  "updDR", "selIR", "capIR", "sIR",
				  "ex1IR", "pIR",   "ex2IR", "updIR" };
#endif

static u32 aspeed_jtag_read(struct aspeed_jtag *aspeed_jtag, u32 reg)
{
	u32 val = readl(aspeed_jtag->reg_base + reg);

#ifdef DEBUG_JTAG
	dev_dbg(aspeed_jtag->dev, "read:%s val = 0x%08x\n", regnames[reg], val);
#endif
	return val;
}

static void aspeed_jtag_write(struct aspeed_jtag *aspeed_jtag, u32 val, u32 reg)
{
#ifdef DEBUG_JTAG
	dev_dbg(aspeed_jtag->dev, "write:%s val = 0x%08x\n", regnames[reg],
		val);
#endif
	writel(val, aspeed_jtag->reg_base + reg);
}

static int aspeed_jtag_freq_set(struct jtag *jtag, u32 freq)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);
	unsigned long apb_frq;
	u32 tck_val;
	u16 div;

	if (!freq)
		return -EINVAL;

	apb_frq = clk_get_rate(aspeed_jtag->pclk);
	if (!apb_frq)
		return -EOPNOTSUPP;

	div = (apb_frq - 1) / freq;
	tck_val = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_TCK);
	aspeed_jtag_write(aspeed_jtag,
			  (tck_val & ~ASPEED_JTAG_TCK_DIVISOR_MASK) | div,
			  ASPEED_JTAG_TCK);
	return 0;
}

static int aspeed_jtag_freq_set_26xx(struct jtag *jtag, u32 freq)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);
	unsigned long apb_frq;
	u32 tck_val;
	u16 div;

	if (!freq)
		return -EINVAL;

	apb_frq = clk_get_rate(aspeed_jtag->pclk);
	if (!apb_frq)
		return -EOPNOTSUPP;

	div = (apb_frq - 1) / freq;
	tck_val = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_GBLCTRL);
	aspeed_jtag_write(aspeed_jtag,
			  (tck_val & ~ASPEED_JTAG_CLK_DIVISOR_MASK) | div,
			  ASPEED_JTAG_GBLCTRL);
	return 0;
}

static int aspeed_jtag_freq_get(struct jtag *jtag, u32 *frq)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);
	u32 pclk;
	u32 tck;

	pclk = clk_get_rate(aspeed_jtag->pclk);
	tck = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_TCK);
	*frq = pclk / (ASPEED_JTAG_TCK_GET_DIV(tck) + 1);

	return 0;
}

static int aspeed_jtag_freq_get_26xx(struct jtag *jtag, u32 *frq)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);
	u32 pclk;
	u32 tck;

	pclk = clk_get_rate(aspeed_jtag->pclk);
	tck = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_GBLCTRL);
	*frq = pclk / (ASPEED_JTAG_CLK_GET_DIV(tck) + 1);

	return 0;
}

static inline void aspeed_jtag_output_disable(struct aspeed_jtag *aspeed_jtag)
{
	aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_CTRL);
}

static inline void
aspeed_jtag_output_disable_26xx(struct aspeed_jtag *aspeed_jtag)
{
	aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_CTRL);
	aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_GBLCTRL);
}

static inline void aspeed_jtag_master(struct aspeed_jtag *aspeed_jtag)
{
	aspeed_jtag_write(aspeed_jtag,
			  (ASPEED_JTAG_CTL_ENG_EN | ASPEED_JTAG_CTL_ENG_OUT_EN),
			  ASPEED_JTAG_CTRL);

	aspeed_jtag_write(aspeed_jtag,
			  ASPEED_JTAG_SW_MODE_EN | ASPEED_JTAG_SW_MODE_TDIO,
			  ASPEED_JTAG_SW);
	aspeed_jtag_write(aspeed_jtag,
			  ASPEED_JTAG_ISR_INST_PAUSE |
				  ASPEED_JTAG_ISR_INST_COMPLETE |
				  ASPEED_JTAG_ISR_DATA_PAUSE |
				  ASPEED_JTAG_ISR_DATA_COMPLETE |
				  ASPEED_JTAG_ISR_INST_PAUSE_EN |
				  ASPEED_JTAG_ISR_INST_COMPLETE_EN |
				  ASPEED_JTAG_ISR_DATA_PAUSE_EN |
				  ASPEED_JTAG_ISR_DATA_COMPLETE_EN,
			  ASPEED_JTAG_ISR); /* Enable Interrupt */
}

static inline void aspeed_jtag_master_26xx(struct aspeed_jtag *aspeed_jtag)
{
	if (aspeed_jtag->mode & JTAG_XFER_HW_MODE) {
		aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_CTRL);
		aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_SW);
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_GBLCTRL_ENG_MODE_EN |
					  ASPEED_JTAG_GBLCTRL_ENG_OUT_EN,
				  ASPEED_JTAG_GBLCTRL);
	} else {
		aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_GBLCTRL);
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_CTL_ENG_EN |
					  ASPEED_JTAG_CTL_ENG_OUT_EN,
				  ASPEED_JTAG_CTRL);

		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_SW_MODE_EN |
					  ASPEED_JTAG_SW_MODE_TDIO,
				  ASPEED_JTAG_SW);
	}
	aspeed_jtag_write(aspeed_jtag,
			  ASPEED_JTAG_INTCTRL_SHCPL_IRQ_EN |
				  ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT,
			  ASPEED_JTAG_INTCTRL); /* Enable HW2 IRQ */

	aspeed_jtag_write(aspeed_jtag,
			  ASPEED_JTAG_ISR_INST_PAUSE |
				  ASPEED_JTAG_ISR_INST_COMPLETE |
				  ASPEED_JTAG_ISR_DATA_PAUSE |
				  ASPEED_JTAG_ISR_DATA_COMPLETE |
				  ASPEED_JTAG_ISR_INST_PAUSE_EN |
				  ASPEED_JTAG_ISR_INST_COMPLETE_EN |
				  ASPEED_JTAG_ISR_DATA_PAUSE_EN |
				  ASPEED_JTAG_ISR_DATA_COMPLETE_EN,
			  ASPEED_JTAG_ISR); /* Enable HW1 Interrupts */
}

static int aspeed_jtag_mode_set(struct jtag *jtag, struct jtag_mode *jtag_mode)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);

	switch (jtag_mode->feature) {
	case JTAG_XFER_MODE:
		aspeed_jtag->mode = jtag_mode->mode;
		aspeed_jtag->llops->master_enable(aspeed_jtag);
		break;
	case JTAG_CONTROL_MODE:
		if (jtag_mode->mode == JTAG_MASTER_OUTPUT_DISABLE)
			aspeed_jtag->llops->output_disable(aspeed_jtag);
		else if (jtag_mode->mode == JTAG_MASTER_MODE)
			aspeed_jtag->llops->master_enable(aspeed_jtag);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * We read and write from an unused JTAG Master controller register in SW
 * mode to create a delay in xfers.
 * We found this mechanism better than any udelay or usleep option.
 */
static inline void aspeed_jtag_sw_delay_26xx(struct aspeed_jtag *aspeed_jtag)
{
	u32 read_reg = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_PADCTRL1);

	aspeed_jtag_write(aspeed_jtag, read_reg, ASPEED_JTAG_PADCTRL1);
}

static char aspeed_jtag_tck_cycle(struct aspeed_jtag *aspeed_jtag, u8 tms,
				  u8 tdi)
{
	char tdo = 0;

	/* TCK = 0 */
	aspeed_jtag_write(aspeed_jtag,
			  ASPEED_JTAG_SW_MODE_EN |
				  (tms * ASPEED_JTAG_SW_MODE_TMS) |
				  (tdi * ASPEED_JTAG_SW_MODE_TDIO),
			  ASPEED_JTAG_SW);

	/* Wait until JTAG Master controller finishes the operation */
	if (aspeed_jtag->llops->xfer_sw_delay)
		aspeed_jtag->llops->xfer_sw_delay(aspeed_jtag);
	else
		aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_SW);

	/* TCK = 1 */
	aspeed_jtag_write(aspeed_jtag,
			  ASPEED_JTAG_SW_MODE_EN | ASPEED_JTAG_SW_MODE_TCK |
				  (tms * ASPEED_JTAG_SW_MODE_TMS) |
				  (tdi * ASPEED_JTAG_SW_MODE_TDIO),
			  ASPEED_JTAG_SW);

	/* Wait until JTAG Master controller finishes the operation */
	if (aspeed_jtag->llops->xfer_sw_delay)
		aspeed_jtag->llops->xfer_sw_delay(aspeed_jtag);

	if (aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_SW) &
	    ASPEED_JTAG_SW_MODE_TDIO)
		tdo = 1;

	return tdo;
}

static int aspeed_jtag_bitbang(struct jtag *jtag,
			       struct bitbang_packet *bitbang,
			       struct tck_bitbang *bitbang_data)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);
	int i = 0;

	for (i = 0; i < bitbang->length; i++) {
		bitbang_data[i].tdo =
			aspeed_jtag_tck_cycle(aspeed_jtag, bitbang_data[i].tms,
					      bitbang_data[i].tdi);
	}
	return 0;
}

static inline void aspeed_jtag_xfer_hw_fifo_delay_26xx(void)
{
	udelay(AST26XX_FIFO_UDELAY);
}

static int aspeed_jtag_isr_wait(struct aspeed_jtag *aspeed_jtag, u32 bit)
{
	int res = 0;
#ifdef USE_INTERRUPTS
	res = wait_event_interruptible(aspeed_jtag->jtag_wq,
				       aspeed_jtag->flag & bit);
	aspeed_jtag->flag &= ~bit;
#else
	u32 status = 0;
	u32 iterations = 0;

	while ((status & bit) == 0) {
		status = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_ISR);
#ifdef DEBUG_JTAG
		dev_dbg(aspeed_jtag->dev, "%s  = 0x%08x\n", __func__, status);
#endif
		iterations++;
		if (iterations > WAIT_ITERATIONS) {
			dev_err(aspeed_jtag->dev, "%s %d in ASPEED_JTAG_ISR\n",
				"aspeed_jtag driver timed out waiting for bit",
				bit);
			res = -EFAULT;
			break;
		}
		if ((status & ASPEED_JTAG_ISR_DATA_COMPLETE) == 0) {
			if (iterations % 25 == 0)
				usleep_range(1, 5);
			else
				udelay(1);
		}
	}
	aspeed_jtag_write(aspeed_jtag, bit | (status & 0xf), ASPEED_JTAG_ISR);
#endif
	return res;
}

static int aspeed_jtag_wait_shift_complete(struct aspeed_jtag *aspeed_jtag)
{
	int res = 0;
#ifdef USE_INTERRUPTS
	res = wait_event_interruptible(aspeed_jtag->jtag_wq,
				       aspeed_jtag->flag &
				       ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT);
	aspeed_jtag->flag &= ~ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT;
#else
	u32 status = 0;
	u32 iterations = 0;

	while ((status & ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT) == 0) {
		status = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_INTCTRL);
#ifdef DEBUG_JTAG
		dev_dbg(aspeed_jtag->dev, "%s  = 0x%08x\n", __func__, status);
#endif
		iterations++;
		if (iterations > WAIT_ITERATIONS) {
			dev_err(aspeed_jtag->dev,
				"aspeed_jtag driver timed out waiting for shift completed\n");
			res = -EFAULT;
			break;
		}
		if (iterations % 25 == 0)
			usleep_range(1, 5);
		else
			udelay(1);
	}
	aspeed_jtag_write(aspeed_jtag,
			  ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT |
				  ASPEED_JTAG_INTCTRL_SHCPL_IRQ_EN,
			  ASPEED_JTAG_INTCTRL);
#endif
	return res;
}

static void aspeed_jtag_set_tap_state(struct aspeed_jtag *aspeed_jtag,
				      enum jtag_tapstate from_state,
				      enum jtag_tapstate end_state)
{
	int i = 0;
	enum jtag_tapstate from, to;

	from = from_state;
	to = end_state;

	if (from == JTAG_STATE_CURRENT)
		from = aspeed_jtag->status;

	for (i = 0; i < _tms_cycle_lookup[from][to].count; i++)
		aspeed_jtag_tck_cycle(aspeed_jtag,
				      ((_tms_cycle_lookup[from][to].tmsbits
				      >> i) & 0x1), 0);
	aspeed_jtag->current_state = end_state;
}

static void aspeed_jtag_set_tap_state_sw(struct aspeed_jtag *aspeed_jtag,
					 struct jtag_tap_state *tapstate)
{
	/* SW mode from curent tap state -> to end_state */
	if (tapstate->reset) {
		int i = 0;

		for (i = 0; i < ASPEED_JTAG_RESET_CNTR; i++)
			aspeed_jtag_tck_cycle(aspeed_jtag, 1, 0);
		aspeed_jtag->current_state = JTAG_STATE_TLRESET;
	}

	aspeed_jtag_set_tap_state(aspeed_jtag, tapstate->from,
				  tapstate->endstate);
}

static int aspeed_jtag_status_set(struct jtag *jtag,
				  struct jtag_tap_state *tapstate)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);

#ifdef DEBUG_JTAG
	dev_dbg(aspeed_jtag->dev, "Set TAP state: %s\n",
		end_status_str[tapstate->endstate]);
#endif

	if (!(aspeed_jtag->mode & JTAG_XFER_HW_MODE)) {
		aspeed_jtag_set_tap_state_sw(aspeed_jtag, tapstate);
		return 0;
	}

	/* x TMS high + 1 TMS low */
	if (tapstate->reset) {
		/* Disable sw mode */
		aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_SW);
		mdelay(1);
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_CTL_ENG_EN |
					  ASPEED_JTAG_CTL_ENG_OUT_EN |
					  ASPEED_JTAG_CTL_FORCE_TMS,
				  ASPEED_JTAG_CTRL);
		mdelay(1);
		aspeed_jtag_write(aspeed_jtag, ASPEED_JTAG_SW_TDIO,
				  ASPEED_JTAG_SW);
		aspeed_jtag->current_state = JTAG_STATE_TLRESET;
	}

	return 0;
}

static void aspeed_jtag_shctrl_tms_mask(enum jtag_tapstate from,
					enum jtag_tapstate to,
					enum jtag_tapstate there,
					enum jtag_tapstate endstate,
					u32 start_shift, u32 end_shift,
					u32 *tms_mask)
{
	u32 pre_tms = start_shift ? _tms_cycle_lookup[from][to].count : 0;
	u32 post_tms = end_shift ? _tms_cycle_lookup[there][endstate].count : 0;
	u32 tms_value = start_shift ? _tms_cycle_lookup[from][to].tmsbits : 0;

	tms_value |= end_shift ? _tms_cycle_lookup[there][endstate].tmsbits
					 << pre_tms :
				 0;
	*tms_mask = start_shift | ASPEED_JTAG_SHCTRL_PRE_TMS(pre_tms) |
		    end_shift | ASPEED_JTAG_SHCTRL_POST_TMS(post_tms) |
		    ASPEED_JTAG_SHCTRL_TMS(tms_value);
}

static void aspeed_jtag_set_tap_state_hw2(struct aspeed_jtag *aspeed_jtag,
					  struct jtag_tap_state *tapstate)
{
	u32 reg_val;

	/* x TMS high + 1 TMS low */
	if (tapstate->reset) {
		/* Disable sw mode */
		aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_SW);
		udelay(AST26XX_JTAG_CTRL_UDELAY);
		reg_val = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_GBLCTRL);
		aspeed_jtag_write(aspeed_jtag,
				  reg_val | ASPEED_JTAG_GBLCTRL_ENG_MODE_EN |
					  ASPEED_JTAG_GBLCTRL_ENG_OUT_EN |
					  ASPEED_JTAG_GBLCTRL_RESET_FIFO |
					  ASPEED_JTAG_GBLCTRL_FORCE_TMS,
				  ASPEED_JTAG_GBLCTRL);
		udelay(AST26XX_JTAG_CTRL_UDELAY);
		aspeed_jtag->current_state = JTAG_STATE_TLRESET;
	} else if (tapstate->endstate == JTAG_STATE_IDLE &&
		   aspeed_jtag->current_state != JTAG_STATE_IDLE) {
		/* Always go to RTI, do not wait for shift operation */
		aspeed_jtag_set_tap_state(aspeed_jtag,
					  aspeed_jtag->current_state,
					  JTAG_STATE_IDLE);
		aspeed_jtag->current_state = JTAG_STATE_IDLE;
	}
}

static int aspeed_jtag_status_set_26xx(struct jtag *jtag,
				       struct jtag_tap_state *tapstate)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);

#ifdef DEBUG_JTAG
	dev_dbg(aspeed_jtag->dev, "Set TAP state: status %s from %s to %s\n",
		end_status_str[aspeed_jtag->current_state],
		end_status_str[tapstate->from],
		end_status_str[tapstate->endstate]);
#endif

	if (!(aspeed_jtag->mode & JTAG_XFER_HW_MODE)) {
		aspeed_jtag_set_tap_state_sw(aspeed_jtag, tapstate);
		return 0;
	}

	aspeed_jtag_set_tap_state_hw2(aspeed_jtag, tapstate);
	return 0;
}

static void aspeed_jtag_xfer_sw(struct aspeed_jtag *aspeed_jtag,
				struct jtag_xfer *xfer, u32 *data)
{
	unsigned long remain_xfer = xfer->length;
	unsigned long shift_bits = 0;
	unsigned long index = 0;
	unsigned long tdi;
	char tdo;

#ifdef DEBUG_JTAG
	dev_dbg(aspeed_jtag->dev, "SW JTAG SHIFT %s, length = %d\n",
		(xfer->type == JTAG_SIR_XFER) ? "IR" : "DR", xfer->length);
#endif

	if (xfer->type == JTAG_SIR_XFER)
		aspeed_jtag_set_tap_state(aspeed_jtag, xfer->from,
					  JTAG_STATE_SHIFTIR);
	else
		aspeed_jtag_set_tap_state(aspeed_jtag, xfer->from,
					  JTAG_STATE_SHIFTDR);

	tdi = ASPEED_JTAG_GET_TDI(xfer->direction, data[index]);
	data[index] = 0;
	while (remain_xfer > 1) {
		tdo = aspeed_jtag_tck_cycle(aspeed_jtag, 0,
					    tdi & ASPEED_JTAG_DATA_MSB);
		data[index] |= tdo
			       << (shift_bits % ASPEED_JTAG_DATA_CHUNK_SIZE);
		tdi >>= 1;
		shift_bits++;
		remain_xfer--;

		if (shift_bits % ASPEED_JTAG_DATA_CHUNK_SIZE == 0) {
			tdo = 0;
			index++;
			tdi = ASPEED_JTAG_GET_TDI(xfer->direction, data[index]);
			data[index] = 0;
		}
	}

	if ((xfer->endstate == (xfer->type == JTAG_SIR_XFER ?
					JTAG_STATE_SHIFTIR :
					JTAG_STATE_SHIFTDR))) {
		/* Stay in Shift IR/DR*/
		tdo = aspeed_jtag_tck_cycle(aspeed_jtag, 0,
					    tdi & ASPEED_JTAG_DATA_MSB);
		data[index] |= tdo
			       << (shift_bits % ASPEED_JTAG_DATA_CHUNK_SIZE);
	} else {
		/* Goto end state */
		tdo = aspeed_jtag_tck_cycle(aspeed_jtag, 1,
					    tdi & ASPEED_JTAG_DATA_MSB);
		data[index] |= tdo
			       << (shift_bits % ASPEED_JTAG_DATA_CHUNK_SIZE);
		aspeed_jtag->status = (xfer->type == JTAG_SIR_XFER) ?
					      JTAG_STATE_EXIT1IR :
					      JTAG_STATE_EXIT1DR;
		aspeed_jtag_set_tap_state(aspeed_jtag, aspeed_jtag->status,
					  xfer->endstate);
	}
}

static int aspeed_jtag_xfer_push_data_26xx(struct aspeed_jtag *aspeed_jtag,
					   enum jtag_xfer_type type,
					   u32 bits_len)
{
	int res = 0;

	aspeed_jtag_write(aspeed_jtag, ASPEED_JTAG_TRANS_LEN(bits_len),
			  ASPEED_JTAG_CTRL);
	if (type == JTAG_SIR_XFER) {
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_TRANS_LEN(bits_len) |
					  ASPEED_JTAG_CTL_26XX_INST_EN,
				  ASPEED_JTAG_CTRL);
		res = aspeed_jtag_isr_wait(aspeed_jtag,
					   ASPEED_JTAG_ISR_INST_PAUSE);
	} else {
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_TRANS_LEN(bits_len) |
					  ASPEED_JTAG_CTL_DATA_EN,
				  ASPEED_JTAG_CTRL);
		res = aspeed_jtag_isr_wait(aspeed_jtag,
					   ASPEED_JTAG_ISR_DATA_PAUSE);
	}
	return res;
}

static int aspeed_jtag_xfer_push_data(struct aspeed_jtag *aspeed_jtag,
				      enum jtag_xfer_type type, u32 bits_len)
{
	int res = 0;

	if (type == JTAG_SIR_XFER) {
		aspeed_jtag_write(aspeed_jtag, ASPEED_JTAG_IOUT_LEN(bits_len),
				  ASPEED_JTAG_CTRL);
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_IOUT_LEN(bits_len) |
					  ASPEED_JTAG_CTL_INST_EN,
				  ASPEED_JTAG_CTRL);
		res = aspeed_jtag_isr_wait(aspeed_jtag,
					   ASPEED_JTAG_ISR_INST_PAUSE);
	} else {
		aspeed_jtag_write(aspeed_jtag, ASPEED_JTAG_DOUT_LEN(bits_len),
				  ASPEED_JTAG_CTRL);
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_DOUT_LEN(bits_len) |
					  ASPEED_JTAG_CTL_DATA_EN,
				  ASPEED_JTAG_CTRL);
		res = aspeed_jtag_isr_wait(aspeed_jtag,
					   ASPEED_JTAG_ISR_DATA_PAUSE);
	}
	return res;
}

static int aspeed_jtag_xfer_push_data_last_26xx(struct aspeed_jtag *aspeed_jtag,
						enum jtag_xfer_type type,
						u32 shift_bits)
{
	int res = 0;

	aspeed_jtag_write(aspeed_jtag,
			  ASPEED_JTAG_TRANS_LEN(shift_bits) |
				  ASPEED_JTAG_CTL_26XX_LASPEED_TRANS,
			  ASPEED_JTAG_CTRL);
	if (type == JTAG_SIR_XFER) {
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_TRANS_LEN(shift_bits) |
					  ASPEED_JTAG_CTL_26XX_LASPEED_TRANS |
					  ASPEED_JTAG_CTL_26XX_INST_EN,
				  ASPEED_JTAG_CTRL);
		res = aspeed_jtag_isr_wait(aspeed_jtag,
					   ASPEED_JTAG_ISR_INST_COMPLETE);
	} else {
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_TRANS_LEN(shift_bits) |
					  ASPEED_JTAG_CTL_26XX_LASPEED_TRANS |
					  ASPEED_JTAG_CTL_DATA_EN,
				  ASPEED_JTAG_CTRL);
		res = aspeed_jtag_isr_wait(aspeed_jtag,
					   ASPEED_JTAG_ISR_DATA_COMPLETE);
	}
	return res;
}

static int aspeed_jtag_xfer_push_data_last(struct aspeed_jtag *aspeed_jtag,
					   enum jtag_xfer_type type,
					   u32 shift_bits)
{
	int res = 0;

	if (type == JTAG_SIR_XFER) {
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_IOUT_LEN(shift_bits) |
					  ASPEED_JTAG_CTL_LASPEED_INST,
				  ASPEED_JTAG_CTRL);
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_IOUT_LEN(shift_bits) |
					  ASPEED_JTAG_CTL_LASPEED_INST |
					  ASPEED_JTAG_CTL_INST_EN,
				  ASPEED_JTAG_CTRL);
		res = aspeed_jtag_isr_wait(aspeed_jtag,
					   ASPEED_JTAG_ISR_INST_COMPLETE);
	} else {
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_DOUT_LEN(shift_bits) |
					  ASPEED_JTAG_CTL_LASPEED_DATA,
				  ASPEED_JTAG_CTRL);
		aspeed_jtag_write(aspeed_jtag,
				  ASPEED_JTAG_DOUT_LEN(shift_bits) |
					  ASPEED_JTAG_CTL_LASPEED_DATA |
					  ASPEED_JTAG_CTL_DATA_EN,
				  ASPEED_JTAG_CTRL);
		res = aspeed_jtag_isr_wait(aspeed_jtag,
					   ASPEED_JTAG_ISR_DATA_COMPLETE);
	}
	return res;
}

static int aspeed_jtag_xfer_hw(struct aspeed_jtag *aspeed_jtag,
			       struct jtag_xfer *xfer, u32 *data)
{
	unsigned long remain_xfer = xfer->length;
	unsigned long index = 0;
	char shift_bits;
	u32 data_reg;
	u32 scan_end;
	union pad_config padding;
	int retval = 0;

	padding.int_value = xfer->padding;

#ifdef DEBUG_JTAG
	dev_dbg(aspeed_jtag->dev, "HW JTAG SHIFT %s, length = %d pad = 0x%x\n",
		(xfer->type == JTAG_SIR_XFER) ? "IR" : "DR", xfer->length,
		xfer->padding);
#endif
	data_reg = xfer->type == JTAG_SIR_XFER ? ASPEED_JTAG_INST :
						 ASPEED_JTAG_DATA;
	if (xfer->endstate == JTAG_STATE_SHIFTIR ||
	    xfer->endstate == JTAG_STATE_SHIFTDR ||
	    xfer->endstate == JTAG_STATE_PAUSEIR ||
	    xfer->endstate == JTAG_STATE_PAUSEDR) {
		scan_end = 0;
	} else {
		if (padding.post_pad_number)
			scan_end = 0;
		else
			scan_end = 1;
	}

	/* Perform pre padding */
	if (padding.pre_pad_number) {
		struct jtag_xfer pre_xfer = {
			.type = xfer->type,
			.direction = JTAG_WRITE_XFER,
			.from = xfer->from,
			.endstate = xfer->type == JTAG_SIR_XFER ?
				    JTAG_STATE_SHIFTIR : JTAG_STATE_SHIFTDR,
			.padding = 0,
			.length = padding.pre_pad_number,
		};
		if (padding.pre_pad_number > ASPEED_JTAG_MAX_PAD_SIZE)
			return -EINVAL;
		retval = aspeed_jtag_xfer_hw(aspeed_jtag, &pre_xfer,
					     padding.pad_data ?
					     aspeed_jtag->pad_data_one :
					     aspeed_jtag->pad_data_zero);
		if (retval)
			return retval;
	}

	while (remain_xfer) {
		if (xfer->direction & JTAG_WRITE_XFER)
			aspeed_jtag_write(aspeed_jtag, data[index], data_reg);
		else
			aspeed_jtag_write(aspeed_jtag, 0, data_reg);
		if (aspeed_jtag->llops->xfer_hw_fifo_delay)
			aspeed_jtag->llops->xfer_hw_fifo_delay();

		if (remain_xfer > ASPEED_JTAG_DATA_CHUNK_SIZE) {
#ifdef DEBUG_JTAG
			dev_dbg(aspeed_jtag->dev,
				"Chunk len=%d chunk_size=%d remain_xfer=%lu\n",
				xfer->length, ASPEED_JTAG_DATA_CHUNK_SIZE,
				remain_xfer);
#endif
			shift_bits = ASPEED_JTAG_DATA_CHUNK_SIZE;

			/*
			 * Transmit bytes that were not equals to column length
			 * and after the transfer go to Pause IR/DR.
			 */
			if (aspeed_jtag->llops->xfer_push_data(aspeed_jtag,
							       xfer->type,
							       shift_bits)
							       != 0) {
				return -EFAULT;
			}
		} else {
			/*
			 * Read bytes equals to column length
			 */
			shift_bits = remain_xfer;
			if (scan_end) {
				/*
				 * If this data is the end of the transmission
				 * send remaining bits and go to endstate
				 */
#ifdef DEBUG_JTAG
				dev_dbg(aspeed_jtag->dev,
					"Last len=%d chunk_size=%d remain_xfer=%lu\n",
					xfer->length,
					ASPEED_JTAG_DATA_CHUNK_SIZE,
					remain_xfer);
#endif
				if (aspeed_jtag->llops->xfer_push_data_last(
					    aspeed_jtag, xfer->type,
					    shift_bits) != 0) {
					return -EFAULT;
				}
			} else {
				/*
				 * If transmission is waiting for additional
				 * data send remaining bits and then go to
				 * Pause IR/DR.
				 */
#ifdef DEBUG_JTAG
				dev_dbg(aspeed_jtag->dev,
					"Tail len=%d chunk_size=%d remain_xfer=%lu\n",
					xfer->length,
					ASPEED_JTAG_DATA_CHUNK_SIZE,
					remain_xfer);
#endif
				if (aspeed_jtag->llops->xfer_push_data(
					    aspeed_jtag, xfer->type,
					    shift_bits) != 0) {
					return -EFAULT;
				}
			}
		}

		if (xfer->direction & JTAG_READ_XFER) {
			if (shift_bits < ASPEED_JTAG_DATA_CHUNK_SIZE) {
				data[index] =
					aspeed_jtag_read(aspeed_jtag, data_reg);

				data[index] >>= ASPEED_JTAG_DATA_CHUNK_SIZE -
						shift_bits;
			} else {
				data[index] =
					aspeed_jtag_read(aspeed_jtag, data_reg);
			}
			if (aspeed_jtag->llops->xfer_hw_fifo_delay)
				aspeed_jtag->llops->xfer_hw_fifo_delay();
		}

		remain_xfer = remain_xfer - shift_bits;
		index++;
	}

	/* Perform post padding */
	if (padding.post_pad_number) {
		struct jtag_xfer post_xfer = {
			.type = xfer->type,
			.direction = JTAG_WRITE_XFER,
			.from = xfer->from,
			.endstate = xfer->endstate,
			.padding = 0,
			.length = padding.post_pad_number,
		};
		if (padding.post_pad_number > ASPEED_JTAG_MAX_PAD_SIZE)
			return -EINVAL;
		retval = aspeed_jtag_xfer_hw(aspeed_jtag, &post_xfer,
					     padding.pad_data ?
					     aspeed_jtag->pad_data_one :
					     aspeed_jtag->pad_data_zero);
		if (retval)
			return retval;
	}
	return 0;
}

static int aspeed_jtag_xfer(struct jtag *jtag, struct jtag_xfer *xfer,
			    u8 *xfer_data)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);

	if (!(aspeed_jtag->mode & JTAG_XFER_HW_MODE)) {
		/* SW mode */
		aspeed_jtag_write(aspeed_jtag, ASPEED_JTAG_SW_TDIO,
				  ASPEED_JTAG_SW);

		aspeed_jtag->llops->xfer_sw(aspeed_jtag, xfer,
					    (u32 *)xfer_data);
	} else {
		/* HW mode */
		aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_SW);
		if (aspeed_jtag->llops->xfer_hw(aspeed_jtag, xfer,
						(u32 *)xfer_data) != 0)
			return -EFAULT;
	}

	aspeed_jtag->status = xfer->endstate;
	return 0;
}

static int aspeed_jtag_xfer_hw2(struct aspeed_jtag *aspeed_jtag,
				struct jtag_xfer *xfer, u32 *data)
{
	unsigned long remain_xfer = xfer->length;
	unsigned long partial_xfer_size = 0;
	unsigned long index = 0;
	u32 shift_bits;
	u32 data_reg;
	u32 reg_val;
	enum jtag_tapstate shift;
	enum jtag_tapstate exit;
	enum jtag_tapstate exitx;
	enum jtag_tapstate pause;
	enum jtag_tapstate endstate;
	u32 start_shift;
	u32 end_shift;
	u32 tms_mask;

	if (xfer->type == JTAG_SIR_XFER) {
		data_reg = ASPEED_JTAG_SHDATA;
		shift = JTAG_STATE_SHIFTIR;
		pause = JTAG_STATE_PAUSEIR;
		exit = JTAG_STATE_EXIT1IR;
		exitx = JTAG_STATE_EXIT1DR;
	} else {
		data_reg = ASPEED_JTAG_SHDATA;
		shift = JTAG_STATE_SHIFTDR;
		pause = JTAG_STATE_PAUSEDR;
		exit = JTAG_STATE_EXIT1DR;
		exitx = JTAG_STATE_EXIT1IR;
	}
#ifdef DEBUG_JTAG
	dev_dbg(aspeed_jtag->dev,
		"HW2 JTAG SHIFT %s, length %d status %s from %s to %s then %s pad 0x%x\n",
		(xfer->type == JTAG_SIR_XFER) ? "IR" : "DR", xfer->length,
		end_status_str[aspeed_jtag->current_state],
		end_status_str[xfer->from],
		end_status_str[shift],
		end_status_str[xfer->endstate], xfer->padding);
#endif

	if (aspeed_jtag->current_state == shift) {
		start_shift = 0;
	} else if (aspeed_jtag->current_state == JTAG_STATE_IDLE ||
		   aspeed_jtag->current_state == JTAG_STATE_TLRESET ||
		   aspeed_jtag->current_state == pause ||
		   aspeed_jtag->current_state == exit ||
		   aspeed_jtag->current_state == exitx) {
		start_shift = ASPEED_JTAG_SHCTRL_START_SHIFT;
	} else {
		return -EINVAL;
	}

	if (xfer->endstate == shift) {
		/*
		 * In the case of shifting 1 bit of data and attempting to stay
		 * in the SHIFT state, the AST2600 JTAG Master Controller in
		 * Hardware mode 2 has been observed to go to EXIT1 IR/DR
		 * instead of staying in the SHIFT IR/DR state. The following
		 * code special cases this one bit shift and directs the state
		 * machine to go to the PAUSE IR/DR state instead.
		 * Alternatively, the application making driver calls can avoid
		 * this situation as follows:
		 *   1.) Bundle all of the shift bits  together into one call
		 *       AND/OR
		 *   2.) Direct all partial shifts to move to the PAUSE-IR/DR
		 *       state.
		 */
		if (xfer->length == 1) {
#ifdef DEBUG_JTAG
			dev_warn(aspeed_jtag->dev, "JTAG Silicon WA: going to pause instead of shift");
#endif
			end_shift = ASPEED_JTAG_SHCTRL_END_SHIFT;
			endstate = pause;
		} else {
			end_shift = 0;
			endstate = shift;
		}
	} else if (xfer->endstate == exit) {
		endstate = exit;
		end_shift = ASPEED_JTAG_SHCTRL_END_SHIFT;
	} else if (xfer->endstate == JTAG_STATE_IDLE) {
		endstate = JTAG_STATE_IDLE;
		end_shift = ASPEED_JTAG_SHCTRL_END_SHIFT;
	} else if (xfer->endstate == pause) {
		endstate = pause;
		end_shift = ASPEED_JTAG_SHCTRL_END_SHIFT;
	} else {
		return -EINVAL;
	}

	aspeed_jtag_write(aspeed_jtag, xfer->padding, ASPEED_JTAG_PADCTRL0);

	while (remain_xfer) {
		unsigned long partial_xfer;
		unsigned long partial_index;

		if (remain_xfer > ASPEED_JTAG_HW2_DATA_CHUNK_SIZE)
			partial_xfer_size = ASPEED_JTAG_HW2_DATA_CHUNK_SIZE;
		else
			partial_xfer_size = remain_xfer;

		partial_index = index;
		partial_xfer = partial_xfer_size;

		reg_val = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_GBLCTRL);
		aspeed_jtag_write(aspeed_jtag, reg_val |
				  ASPEED_JTAG_GBLCTRL_RESET_FIFO,
				  ASPEED_JTAG_GBLCTRL);

		/* Switch internal FIFO into CPU mode */
		reg_val = reg_val & ~BIT(24);
		aspeed_jtag_write(aspeed_jtag, reg_val,
				  ASPEED_JTAG_GBLCTRL);

		while (partial_xfer) {
			if (partial_xfer > ASPEED_JTAG_DATA_CHUNK_SIZE)
				shift_bits = ASPEED_JTAG_DATA_CHUNK_SIZE;
			else
				shift_bits = partial_xfer;

			if (xfer->direction & JTAG_WRITE_XFER)
				aspeed_jtag_write(aspeed_jtag,
						  data[partial_index++],
						  data_reg);
			else
				aspeed_jtag_write(aspeed_jtag, 0, data_reg);
			if (aspeed_jtag->llops->xfer_hw_fifo_delay)
				aspeed_jtag->llops->xfer_hw_fifo_delay();
			partial_xfer = partial_xfer - shift_bits;
		}
		if (remain_xfer > ASPEED_JTAG_HW2_DATA_CHUNK_SIZE) {
			shift_bits = ASPEED_JTAG_HW2_DATA_CHUNK_SIZE;

			/*
			 * Transmit bytes that were not equals to column length
			 * and after the transfer go to Pause IR/DR.
			 */

			aspeed_jtag_shctrl_tms_mask(aspeed_jtag->current_state,
						    shift, exit, endstate,
						    start_shift, 0, &tms_mask);

			reg_val = aspeed_jtag_read(aspeed_jtag,
						   ASPEED_JTAG_GBLCTRL);
			reg_val = reg_val & ~(GENMASK(22, 20));
			aspeed_jtag_write(aspeed_jtag, reg_val |
					  ASPEED_JTAG_GBLCTRL_FIFO_CTRL_MODE |
					  ASPEED_JTAG_GBLCTRL_UPDT_SHIFT(
						shift_bits),
					  ASPEED_JTAG_GBLCTRL);

			aspeed_jtag_write(aspeed_jtag, tms_mask |
				ASPEED_JTAG_SHCTRL_LWRDT_SHIFT(shift_bits),
				ASPEED_JTAG_SHCTRL);
			aspeed_jtag_wait_shift_complete(aspeed_jtag);
		} else {
			/*
			 * Read bytes equals to column length
			 */
			shift_bits = remain_xfer;
			aspeed_jtag_shctrl_tms_mask(aspeed_jtag->current_state,
						    shift, exit, endstate,
						    start_shift, end_shift,
						    &tms_mask);

			reg_val = aspeed_jtag_read(aspeed_jtag,
						   ASPEED_JTAG_GBLCTRL);
			reg_val = reg_val & ~(GENMASK(22, 20));
			aspeed_jtag_write(aspeed_jtag, reg_val |
					  ASPEED_JTAG_GBLCTRL_FIFO_CTRL_MODE |
					  ASPEED_JTAG_GBLCTRL_UPDT_SHIFT(
						shift_bits),
					  ASPEED_JTAG_GBLCTRL);

			aspeed_jtag_write(aspeed_jtag, tms_mask |
					  ASPEED_JTAG_SHCTRL_LWRDT_SHIFT(
						  shift_bits),
					  ASPEED_JTAG_SHCTRL);

			aspeed_jtag_wait_shift_complete(aspeed_jtag);
		}

		partial_index = index;
		partial_xfer = partial_xfer_size;
		while (partial_xfer) {
			if (partial_xfer >
			    ASPEED_JTAG_DATA_CHUNK_SIZE) {
				shift_bits =
					ASPEED_JTAG_DATA_CHUNK_SIZE;
				data[partial_index++] =
					aspeed_jtag_read(aspeed_jtag,
							 data_reg);

			} else {
				shift_bits = partial_xfer;
				data[partial_index++] =
					aspeed_jtag_read(aspeed_jtag,
							 data_reg);
			}
			if (aspeed_jtag->llops->xfer_hw_fifo_delay)
				aspeed_jtag->llops->xfer_hw_fifo_delay();
			partial_xfer = partial_xfer - shift_bits;
		}

		remain_xfer = remain_xfer - partial_xfer_size;
		index = partial_index;
		start_shift = 0;
	}
	aspeed_jtag->current_state = endstate;
	return 0;
}

static int aspeed_jtag_status_get(struct jtag *jtag, u32 *status)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);

	*status = aspeed_jtag->current_state;
	return 0;
}

static irqreturn_t aspeed_jtag_interrupt(s32 this_irq, void *dev_id)
{
	struct aspeed_jtag *aspeed_jtag = dev_id;
	irqreturn_t ret;
	u32 status;

	status = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_ISR);

	if (status & ASPEED_JTAG_ISR_INT_MASK) {
		aspeed_jtag_write(aspeed_jtag,
				  (status & ASPEED_JTAG_ISR_INT_MASK) |
					  (status &
					   ASPEED_JTAG_ISR_INT_EN_MASK),
				  ASPEED_JTAG_ISR);
		aspeed_jtag->flag |= status & ASPEED_JTAG_ISR_INT_MASK;
	}

	if (aspeed_jtag->flag) {
		wake_up_interruptible(&aspeed_jtag->jtag_wq);
		ret = IRQ_HANDLED;
	} else {
		dev_err(aspeed_jtag->dev, "irq status:%x\n", status);
		ret = IRQ_NONE;
	}
	return ret;
}

static irqreturn_t aspeed_jtag_interrupt_hw2(s32 this_irq, void *dev_id)
{
	struct aspeed_jtag *aspeed_jtag = dev_id;
	irqreturn_t ret;
	u32 status;

	status = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_INTCTRL);

	if (status & ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT) {
		aspeed_jtag_write(aspeed_jtag,
				  status | ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT,
				  ASPEED_JTAG_INTCTRL);
		aspeed_jtag->flag |= status & ASPEED_JTAG_INTCTRL_SHCPL_IRQ_STAT;
	}

	if (aspeed_jtag->flag) {
		wake_up_interruptible(&aspeed_jtag->jtag_wq);
		ret = IRQ_HANDLED;
	} else {
		dev_err(aspeed_jtag->dev, "irq status:%x\n", status);
		ret = IRQ_NONE;
	}
	return ret;
}

static int aspeed_jtag_enable(struct jtag *jtag)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);

	aspeed_jtag->llops->master_enable(aspeed_jtag);
	return 0;
}

static int aspeed_jtag_disable(struct jtag *jtag)
{
	struct aspeed_jtag *aspeed_jtag = jtag_priv(jtag);

	aspeed_jtag->llops->output_disable(aspeed_jtag);
	return 0;
}

static int aspeed_jtag_init(struct platform_device *pdev,
			    struct aspeed_jtag *aspeed_jtag)
{
	struct resource *res;
#ifdef USE_INTERRUPTS
	int err;
#endif
	memset(aspeed_jtag->pad_data_one, ~0,
	       sizeof(aspeed_jtag->pad_data_one));
	memset(aspeed_jtag->pad_data_zero, 0,
	       sizeof(aspeed_jtag->pad_data_zero));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	aspeed_jtag->reg_base = devm_ioremap_resource(aspeed_jtag->dev, res);
	if (IS_ERR(aspeed_jtag->reg_base))
		return -ENOMEM;

	aspeed_jtag->pclk = devm_clk_get(aspeed_jtag->dev, NULL);
	if (IS_ERR(aspeed_jtag->pclk)) {
		dev_err(aspeed_jtag->dev, "devm_clk_get failed\n");
		return PTR_ERR(aspeed_jtag->pclk);
	}

#ifdef USE_INTERRUPTS
	aspeed_jtag->irq = platform_get_irq(pdev, 0);
	if (aspeed_jtag->irq < 0) {
		dev_err(aspeed_jtag->dev, "no irq specified\n");
		return -ENOENT;
	}
#endif

	if (clk_prepare_enable(aspeed_jtag->pclk)) {
		dev_err(aspeed_jtag->dev, "no irq specified\n");
		return -ENOENT;
	}

	aspeed_jtag->rst = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(aspeed_jtag->rst)) {
		dev_err(aspeed_jtag->dev,
			"missing or invalid reset controller device tree entry");
		return PTR_ERR(aspeed_jtag->rst);
	}
	reset_control_deassert(aspeed_jtag->rst);

#ifdef USE_INTERRUPTS
	err = devm_request_irq(aspeed_jtag->dev, aspeed_jtag->irq,
			       aspeed_jtag->llops->jtag_interrupt, 0,
			       "aspeed-jtag", aspeed_jtag);
	if (err) {
		dev_err(aspeed_jtag->dev, "unable to get IRQ");
		clk_disable_unprepare(aspeed_jtag->pclk);
		return err;
	}
#endif

	aspeed_jtag->llops->output_disable(aspeed_jtag);

	aspeed_jtag->flag = 0;
	aspeed_jtag->mode = 0;
	init_waitqueue_head(&aspeed_jtag->jtag_wq);
	return 0;
}

static int aspeed_jtag_deinit(struct platform_device *pdev,
			      struct aspeed_jtag *aspeed_jtag)
{
	aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_ISR);
	/* Disable clock */
	aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_CTRL);
	reset_control_assert(aspeed_jtag->rst);
	clk_disable_unprepare(aspeed_jtag->pclk);
	return 0;
}

static const struct jtag_ops aspeed_jtag_ops = {
	.freq_get = aspeed_jtag_freq_get,
	.freq_set = aspeed_jtag_freq_set,
	.status_get = aspeed_jtag_status_get,
	.status_set = aspeed_jtag_status_set,
	.xfer = aspeed_jtag_xfer,
	.mode_set = aspeed_jtag_mode_set,
	.bitbang = aspeed_jtag_bitbang,
	.enable = aspeed_jtag_enable,
	.disable = aspeed_jtag_disable
};

static const struct jtag_ops aspeed_jtag_ops_26xx = {
#ifdef ASPEED_JTAG_HW_MODE_2_ENABLE
	.freq_get = aspeed_jtag_freq_get_26xx,
	.freq_set = aspeed_jtag_freq_set_26xx,
	.status_get = aspeed_jtag_status_get,
	.status_set = aspeed_jtag_status_set_26xx,
#else
	.freq_get = aspeed_jtag_freq_get,
	.freq_set = aspeed_jtag_freq_set,
	.status_get = aspeed_jtag_status_get,
	.status_set = aspeed_jtag_status_set,
#endif
	.xfer = aspeed_jtag_xfer,
	.mode_set = aspeed_jtag_mode_set,
	.bitbang = aspeed_jtag_bitbang,
	.enable = aspeed_jtag_enable,
	.disable = aspeed_jtag_disable
};

static const struct jtag_low_level_functions ast25xx_llops = {
	.master_enable = aspeed_jtag_master,
	.output_disable = aspeed_jtag_output_disable,
	.xfer_push_data = aspeed_jtag_xfer_push_data,
	.xfer_push_data_last = aspeed_jtag_xfer_push_data_last,
	.xfer_sw = aspeed_jtag_xfer_sw,
	.xfer_hw = aspeed_jtag_xfer_hw,
	.xfer_hw_fifo_delay = NULL,
	.xfer_sw_delay = NULL,
	.jtag_interrupt = aspeed_jtag_interrupt
};

static const struct aspeed_jtag_functions ast25xx_functions = {
	.aspeed_jtag_ops = &aspeed_jtag_ops,
	.aspeed_jtag_llops = &ast25xx_llops
};

static const struct jtag_low_level_functions ast26xx_llops = {
#ifdef ASPEED_JTAG_HW_MODE_2_ENABLE
	.master_enable = aspeed_jtag_master_26xx,
	.output_disable = aspeed_jtag_output_disable_26xx,
	.xfer_push_data = aspeed_jtag_xfer_push_data_26xx,
	.xfer_push_data_last = aspeed_jtag_xfer_push_data_last_26xx,
	.xfer_sw = aspeed_jtag_xfer_sw,
	.xfer_hw = aspeed_jtag_xfer_hw2,
	.xfer_hw_fifo_delay = aspeed_jtag_xfer_hw_fifo_delay_26xx,
	.xfer_sw_delay = aspeed_jtag_sw_delay_26xx,
	.jtag_interrupt = aspeed_jtag_interrupt_hw2
#else
	.master_enable = aspeed_jtag_master,
	.output_disable = aspeed_jtag_output_disable,
	.xfer_push_data = aspeed_jtag_xfer_push_data_26xx,
	.xfer_push_data_last = aspeed_jtag_xfer_push_data_last_26xx,
	.xfer_sw = aspeed_jtag_xfer_sw,
	.xfer_hw = aspeed_jtag_xfer_hw,
	.xfer_hw_fifo_delay = aspeed_jtag_xfer_hw_fifo_delay_26xx,
	.xfer_sw_delay = aspeed_jtag_sw_delay_26xx,
	.jtag_interrupt = aspeed_jtag_interrupt
#endif
};

static const struct aspeed_jtag_functions ast26xx_functions = {
	.aspeed_jtag_ops = &aspeed_jtag_ops_26xx,
	.aspeed_jtag_llops = &ast26xx_llops
};

static const struct of_device_id aspeed_jtag_of_match[] = {
	{ .compatible = "aspeed,ast2400-jtag", .data = &ast25xx_functions },
	{ .compatible = "aspeed,ast2500-jtag", .data = &ast25xx_functions },
	{ .compatible = "aspeed,ast2600-jtag", .data = &ast26xx_functions },
	{}
};

static int aspeed_jtag_probe(struct platform_device *pdev)
{
	struct aspeed_jtag *aspeed_jtag;
	struct jtag *jtag;
	const struct of_device_id *match;
	const struct aspeed_jtag_functions *jtag_functions;
	int err;

	match = of_match_node(aspeed_jtag_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;
	jtag_functions = match->data;

	jtag = jtag_alloc(&pdev->dev, sizeof(*aspeed_jtag),
			  jtag_functions->aspeed_jtag_ops);
	if (!jtag)
		return -ENOMEM;

	platform_set_drvdata(pdev, jtag);
	aspeed_jtag = jtag_priv(jtag);
	aspeed_jtag->dev = &pdev->dev;

	aspeed_jtag->llops = jtag_functions->aspeed_jtag_llops;

	/* Initialize device*/
	err = aspeed_jtag_init(pdev, aspeed_jtag);
	if (err)
		goto err_jtag_init;

	/* Initialize JTAG core structure*/
	err = devm_jtag_register(aspeed_jtag->dev, jtag);
	if (err)
		goto err_jtag_register;

	return 0;

err_jtag_register:
	aspeed_jtag_deinit(pdev, aspeed_jtag);
err_jtag_init:
	jtag_free(jtag);
	return err;
}

static int aspeed_jtag_remove(struct platform_device *pdev)
{
	struct jtag *jtag = platform_get_drvdata(pdev);

	aspeed_jtag_deinit(pdev, jtag_priv(jtag));
	return 0;
}

static struct platform_driver aspeed_jtag_driver = {
	.probe = aspeed_jtag_probe,
	.remove = aspeed_jtag_remove,
	.driver = {
		.name = ASPEED_JTAG_NAME,
		.of_match_table = aspeed_jtag_of_match,
	},
};
module_platform_driver(aspeed_jtag_driver);

MODULE_AUTHOR("Oleksandr Shamray <oleksandrs@mellanox.com>");
MODULE_DESCRIPTION("ASPEED JTAG driver");
MODULE_LICENSE("GPL v2");
