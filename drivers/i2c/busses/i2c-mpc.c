// SPDX-License-Identifier: GPL-2.0
/*
 * This is a combined i2c adapter and algorithm driver for the
 * MPC107/Tsi107 PowerPC northbridge and processors that include
 * the same I2C unit (8240, 8245, 85xx).
 *
 * Copyright (C) 2003-2004 Humboldt Solutions Ltd, adrian@humboldt.co.uk
 * Copyright (C) 2021 Allied Telesis Labs
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/fsl_devices.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <asm/mpc52xx.h>
#include <asm/mpc85xx.h>
#include <sysdev/fsl_soc.h>

#define DRV_NAME "mpc-i2c"

#define MPC_I2C_CLOCK_LEGACY   0
#define MPC_I2C_CLOCK_PRESERVE (~0U)

#define MPC_I2C_FDR   0x04
#define MPC_I2C_CR    0x08
#define MPC_I2C_SR    0x0c
#define MPC_I2C_DR    0x10
#define MPC_I2C_DFSRR 0x14

#define CCR_MEN  0x80
#define CCR_MIEN 0x40
#define CCR_MSTA 0x20
#define CCR_MTX  0x10
#define CCR_TXAK 0x08
#define CCR_RSTA 0x04
#define CCR_RSVD 0x02

#define CSR_MCF  0x80
#define CSR_MAAS 0x40
#define CSR_MBB  0x20
#define CSR_MAL  0x10
#define CSR_SRW  0x04
#define CSR_MIF  0x02
#define CSR_RXAK 0x01

enum mpc_i2c_action {
	MPC_I2C_ACTION_START = 1,
	MPC_I2C_ACTION_RESTART,
	MPC_I2C_ACTION_READ_BEGIN,
	MPC_I2C_ACTION_READ_BYTE,
	MPC_I2C_ACTION_WRITE,
	MPC_I2C_ACTION_STOP,

	__MPC_I2C_ACTION_CNT
};

static const char * const action_str[] = {
	"invalid",
	"start",
	"restart",
	"read begin",
	"read",
	"write",
	"stop",
};

static_assert(ARRAY_SIZE(action_str) == __MPC_I2C_ACTION_CNT);

struct mpc_i2c {
	struct device *dev;
	void __iomem *base;
	u32 interrupt;
	wait_queue_head_t waitq;
	spinlock_t lock;
	struct i2c_adapter adap;
	int irq;
	u32 real_clk;
	u8 fdr, dfsrr;
	struct clk *clk_per;
	u32 cntl_bits;
	enum mpc_i2c_action action;
	struct i2c_msg *msgs;
	int num_msgs;
	int curr_msg;
	u32 byte_posn;
	u32 block;
	int rc;
	int expect_rxack;
	bool has_errata_A004447;
};

struct mpc_i2c_divider {
	u16 divider;
	u16 fdr;	/* including dfsrr */
};

struct mpc_i2c_data {
	void (*setup)(struct device_node *node, struct mpc_i2c *i2c, u32 clock);
};

static inline void writeccr(struct mpc_i2c *i2c, u32 x)
{
	writeb(x, i2c->base + MPC_I2C_CR);
}

/* Sometimes 9th clock pulse isn't generated, and slave doesn't release
 * the bus, because it wants to send ACK.
 * Following sequence of enabling/disabling and sending start/stop generates
 * the 9 pulses, so it's all OK.
 */
static void mpc_i2c_fixup(struct mpc_i2c *i2c)
{
	int k;
	u32 delay_val = 1000000 / i2c->real_clk + 1;

	if (delay_val < 2)
		delay_val = 2;

	for (k = 9; k; k--) {
		writeccr(i2c, 0);
		writeccr(i2c, CCR_MSTA | CCR_MTX | CCR_MEN);
		readb(i2c->base + MPC_I2C_DR);
		writeccr(i2c, CCR_MEN);
		udelay(delay_val << 1);
	}
}

static int i2c_mpc_wait_sr(struct mpc_i2c *i2c, int mask)
{
	void __iomem *addr = i2c->base + MPC_I2C_SR;
	u8 val;

	return readb_poll_timeout(addr, val, val & mask, 0, 100);
}

/*
 * Workaround for Erratum A004447. From the P2040CE Rev Q
 *
 * 1.  Set up the frequency divider and sampling rate.
 * 2.  I2CCR - a0h
 * 3.  Poll for I2CSR[MBB] to get set.
 * 4.  If I2CSR[MAL] is set (an indication that SDA is stuck low), then go to
 *     step 5. If MAL is not set, then go to step 13.
 * 5.  I2CCR - 00h
 * 6.  I2CCR - 22h
 * 7.  I2CCR - a2h
 * 8.  Poll for I2CSR[MBB] to get set.
 * 9.  Issue read to I2CDR.
 * 10. Poll for I2CSR[MIF] to be set.
 * 11. I2CCR - 82h
 * 12. Workaround complete. Skip the next steps.
 * 13. Issue read to I2CDR.
 * 14. Poll for I2CSR[MIF] to be set.
 * 15. I2CCR - 80h
 */
static void mpc_i2c_fixup_A004447(struct mpc_i2c *i2c)
{
	int ret;
	u32 val;

	writeccr(i2c, CCR_MEN | CCR_MSTA);
	ret = i2c_mpc_wait_sr(i2c, CSR_MBB);
	if (ret) {
		dev_err(i2c->dev, "timeout waiting for CSR_MBB\n");
		return;
	}

	val = readb(i2c->base + MPC_I2C_SR);

	if (val & CSR_MAL) {
		writeccr(i2c, 0x00);
		writeccr(i2c, CCR_MSTA | CCR_RSVD);
		writeccr(i2c, CCR_MEN | CCR_MSTA | CCR_RSVD);
		ret = i2c_mpc_wait_sr(i2c, CSR_MBB);
		if (ret) {
			dev_err(i2c->dev, "timeout waiting for CSR_MBB\n");
			return;
		}
		val = readb(i2c->base + MPC_I2C_DR);
		ret = i2c_mpc_wait_sr(i2c, CSR_MIF);
		if (ret) {
			dev_err(i2c->dev, "timeout waiting for CSR_MIF\n");
			return;
		}
		writeccr(i2c, CCR_MEN | CCR_RSVD);
	} else {
		val = readb(i2c->base + MPC_I2C_DR);
		ret = i2c_mpc_wait_sr(i2c, CSR_MIF);
		if (ret) {
			dev_err(i2c->dev, "timeout waiting for CSR_MIF\n");
			return;
		}
		writeccr(i2c, CCR_MEN);
	}
}

#if defined(CONFIG_PPC_MPC52xx) || defined(CONFIG_PPC_MPC512x)
static const struct mpc_i2c_divider mpc_i2c_dividers_52xx[] = {
	{20, 0x20}, {22, 0x21}, {24, 0x22}, {26, 0x23},
	{28, 0x24}, {30, 0x01}, {32, 0x25}, {34, 0x02},
	{36, 0x26}, {40, 0x27}, {44, 0x04}, {48, 0x28},
	{52, 0x63}, {56, 0x29}, {60, 0x41}, {64, 0x2a},
	{68, 0x07}, {72, 0x2b}, {80, 0x2c}, {88, 0x09},
	{96, 0x2d}, {104, 0x0a}, {112, 0x2e}, {120, 0x81},
	{128, 0x2f}, {136, 0x47}, {144, 0x0c}, {160, 0x30},
	{176, 0x49}, {192, 0x31}, {208, 0x4a}, {224, 0x32},
	{240, 0x0f}, {256, 0x33}, {272, 0x87}, {288, 0x10},
	{320, 0x34}, {352, 0x89}, {384, 0x35}, {416, 0x8a},
	{448, 0x36}, {480, 0x13}, {512, 0x37}, {576, 0x14},
	{640, 0x38}, {768, 0x39}, {896, 0x3a}, {960, 0x17},
	{1024, 0x3b}, {1152, 0x18}, {1280, 0x3c}, {1536, 0x3d},
	{1792, 0x3e}, {1920, 0x1b}, {2048, 0x3f}, {2304, 0x1c},
	{2560, 0x1d}, {3072, 0x1e}, {3584, 0x7e}, {3840, 0x1f},
	{4096, 0x7f}, {4608, 0x5c}, {5120, 0x5d}, {6144, 0x5e},
	{7168, 0xbe}, {7680, 0x5f}, {8192, 0xbf}, {9216, 0x9c},
	{10240, 0x9d}, {12288, 0x9e}, {15360, 0x9f}
};

static int mpc_i2c_get_fdr_52xx(struct device_node *node, u32 clock,
					  u32 *real_clk)
{
	const struct mpc_i2c_divider *div = NULL;
	unsigned int pvr = mfspr(SPRN_PVR);
	u32 divider;
	int i;

	if (clock == MPC_I2C_CLOCK_LEGACY) {
		/* see below - default fdr = 0x3f -> div = 2048 */
		*real_clk = mpc5xxx_get_bus_frequency(node) / 2048;
		return -EINVAL;
	}

	/* Determine divider value */
	divider = mpc5xxx_get_bus_frequency(node) / clock;

	/*
	 * We want to choose an FDR/DFSR that generates an I2C bus speed that
	 * is equal to or lower than the requested speed.
	 */
	for (i = 0; i < ARRAY_SIZE(mpc_i2c_dividers_52xx); i++) {
		div = &mpc_i2c_dividers_52xx[i];
		/* Old MPC5200 rev A CPUs do not support the high bits */
		if (div->fdr & 0xc0 && pvr == 0x80822011)
			continue;
		if (div->divider >= divider)
			break;
	}

	*real_clk = mpc5xxx_get_bus_frequency(node) / div->divider;
	return (int)div->fdr;
}

static void mpc_i2c_setup_52xx(struct device_node *node,
					 struct mpc_i2c *i2c,
					 u32 clock)
{
	int ret, fdr;

	if (clock == MPC_I2C_CLOCK_PRESERVE) {
		dev_dbg(i2c->dev, "using fdr %d\n",
			readb(i2c->base + MPC_I2C_FDR));
		return;
	}

	ret = mpc_i2c_get_fdr_52xx(node, clock, &i2c->real_clk);
	fdr = (ret >= 0) ? ret : 0x3f; /* backward compatibility */

	writeb(fdr & 0xff, i2c->base + MPC_I2C_FDR);

	if (ret >= 0)
		dev_info(i2c->dev, "clock %u Hz (fdr=%d)\n", i2c->real_clk,
			 fdr);
}
#else /* !(CONFIG_PPC_MPC52xx || CONFIG_PPC_MPC512x) */
static void mpc_i2c_setup_52xx(struct device_node *node,
					 struct mpc_i2c *i2c,
					 u32 clock)
{
}
#endif /* CONFIG_PPC_MPC52xx || CONFIG_PPC_MPC512x */

#ifdef CONFIG_PPC_MPC512x
static void mpc_i2c_setup_512x(struct device_node *node,
					 struct mpc_i2c *i2c,
					 u32 clock)
{
	struct device_node *node_ctrl;
	void __iomem *ctrl;
	const u32 *pval;
	u32 idx;

	/* Enable I2C interrupts for mpc5121 */
	node_ctrl = of_find_compatible_node(NULL, NULL,
					    "fsl,mpc5121-i2c-ctrl");
	if (node_ctrl) {
		ctrl = of_iomap(node_ctrl, 0);
		if (ctrl) {
			/* Interrupt enable bits for i2c-0/1/2: bit 24/26/28 */
			pval = of_get_property(node, "reg", NULL);
			idx = (*pval & 0xff) / 0x20;
			setbits32(ctrl, 1 << (24 + idx * 2));
			iounmap(ctrl);
		}
		of_node_put(node_ctrl);
	}

	/* The clock setup for the 52xx works also fine for the 512x */
	mpc_i2c_setup_52xx(node, i2c, clock);
}
#else /* CONFIG_PPC_MPC512x */
static void mpc_i2c_setup_512x(struct device_node *node,
					 struct mpc_i2c *i2c,
					 u32 clock)
{
}
#endif /* CONFIG_PPC_MPC512x */

#ifdef CONFIG_FSL_SOC
static const struct mpc_i2c_divider mpc_i2c_dividers_8xxx[] = {
	{160, 0x0120}, {192, 0x0121}, {224, 0x0122}, {256, 0x0123},
	{288, 0x0100}, {320, 0x0101}, {352, 0x0601}, {384, 0x0102},
	{416, 0x0602}, {448, 0x0126}, {480, 0x0103}, {512, 0x0127},
	{544, 0x0b03}, {576, 0x0104}, {608, 0x1603}, {640, 0x0105},
	{672, 0x2003}, {704, 0x0b05}, {736, 0x2b03}, {768, 0x0106},
	{800, 0x3603}, {832, 0x0b06}, {896, 0x012a}, {960, 0x0107},
	{1024, 0x012b}, {1088, 0x1607}, {1152, 0x0108}, {1216, 0x2b07},
	{1280, 0x0109}, {1408, 0x1609}, {1536, 0x010a}, {1664, 0x160a},
	{1792, 0x012e}, {1920, 0x010b}, {2048, 0x012f}, {2176, 0x2b0b},
	{2304, 0x010c}, {2560, 0x010d}, {2816, 0x2b0d}, {3072, 0x010e},
	{3328, 0x2b0e}, {3584, 0x0132}, {3840, 0x010f}, {4096, 0x0133},
	{4608, 0x0110}, {5120, 0x0111}, {6144, 0x0112}, {7168, 0x0136},
	{7680, 0x0113}, {8192, 0x0137}, {9216, 0x0114}, {10240, 0x0115},
	{12288, 0x0116}, {14336, 0x013a}, {15360, 0x0117}, {16384, 0x013b},
	{18432, 0x0118}, {20480, 0x0119}, {24576, 0x011a}, {28672, 0x013e},
	{30720, 0x011b}, {32768, 0x013f}, {36864, 0x011c}, {40960, 0x011d},
	{49152, 0x011e}, {61440, 0x011f}
};

static u32 mpc_i2c_get_sec_cfg_8xxx(void)
{
	struct device_node *node;
	u32 __iomem *reg;
	u32 val = 0;

	node = of_find_node_by_name(NULL, "global-utilities");
	if (node) {
		const u32 *prop = of_get_property(node, "reg", NULL);
		if (prop) {
			/*
			 * Map and check POR Device Status Register 2
			 * (PORDEVSR2) at 0xE0014. Note than while MPC8533
			 * and MPC8544 indicate SEC frequency ratio
			 * configuration as bit 26 in PORDEVSR2, other MPC8xxx
			 * parts may store it differently or may not have it
			 * at all.
			 */
			reg = ioremap(get_immrbase() + *prop + 0x14, 0x4);
			if (!reg)
				printk(KERN_ERR
				       "Error: couldn't map PORDEVSR2\n");
			else
				val = in_be32(reg) & 0x00000020; /* sec-cfg */
			iounmap(reg);
		}
	}
	of_node_put(node);

	return val;
}

static u32 mpc_i2c_get_prescaler_8xxx(void)
{
	/*
	 * According to the AN2919 all MPC824x have prescaler 1, while MPC83xx
	 * may have prescaler 1, 2, or 3, depending on the power-on
	 * configuration.
	 */
	u32 prescaler = 1;

	/* mpc85xx */
	if (pvr_version_is(PVR_VER_E500V1) || pvr_version_is(PVR_VER_E500V2)
		|| pvr_version_is(PVR_VER_E500MC)
		|| pvr_version_is(PVR_VER_E5500)
		|| pvr_version_is(PVR_VER_E6500)) {
		unsigned int svr = mfspr(SPRN_SVR);

		if ((SVR_SOC_VER(svr) == SVR_8540)
			|| (SVR_SOC_VER(svr) == SVR_8541)
			|| (SVR_SOC_VER(svr) == SVR_8560)
			|| (SVR_SOC_VER(svr) == SVR_8555)
			|| (SVR_SOC_VER(svr) == SVR_8610))
			/* the above 85xx SoCs have prescaler 1 */
			prescaler = 1;
		else if ((SVR_SOC_VER(svr) == SVR_8533)
			|| (SVR_SOC_VER(svr) == SVR_8544))
			/* the above 85xx SoCs have prescaler 3 or 2 */
			prescaler = mpc_i2c_get_sec_cfg_8xxx() ? 3 : 2;
		else
			/* all the other 85xx have prescaler 2 */
			prescaler = 2;
	}

	return prescaler;
}

static int mpc_i2c_get_fdr_8xxx(struct device_node *node, u32 clock,
					  u32 *real_clk)
{
	const struct mpc_i2c_divider *div = NULL;
	u32 prescaler = mpc_i2c_get_prescaler_8xxx();
	u32 divider;
	int i;

	if (clock == MPC_I2C_CLOCK_LEGACY) {
		/* see below - default fdr = 0x1031 -> div = 16 * 3072 */
		*real_clk = fsl_get_sys_freq() / prescaler / (16 * 3072);
		return -EINVAL;
	}

	divider = fsl_get_sys_freq() / clock / prescaler;

	pr_debug("I2C: src_clock=%d clock=%d divider=%d\n",
		 fsl_get_sys_freq(), clock, divider);

	/*
	 * We want to choose an FDR/DFSR that generates an I2C bus speed that
	 * is equal to or lower than the requested speed.
	 */
	for (i = 0; i < ARRAY_SIZE(mpc_i2c_dividers_8xxx); i++) {
		div = &mpc_i2c_dividers_8xxx[i];
		if (div->divider >= divider)
			break;
	}

	*real_clk = fsl_get_sys_freq() / prescaler / div->divider;
	return (int)div->fdr;
}

static void mpc_i2c_setup_8xxx(struct device_node *node,
					 struct mpc_i2c *i2c,
					 u32 clock)
{
	int ret, fdr;

	if (clock == MPC_I2C_CLOCK_PRESERVE) {
		dev_dbg(i2c->dev, "using dfsrr %d, fdr %d\n",
			readb(i2c->base + MPC_I2C_DFSRR),
			readb(i2c->base + MPC_I2C_FDR));
		return;
	}

	ret = mpc_i2c_get_fdr_8xxx(node, clock, &i2c->real_clk);
	fdr = (ret >= 0) ? ret : 0x1031; /* backward compatibility */

	writeb(fdr & 0xff, i2c->base + MPC_I2C_FDR);
	writeb((fdr >> 8) & 0xff, i2c->base + MPC_I2C_DFSRR);

	if (ret >= 0)
		dev_info(i2c->dev, "clock %d Hz (dfsrr=%d fdr=%d)\n",
			 i2c->real_clk, fdr >> 8, fdr & 0xff);
}

#else /* !CONFIG_FSL_SOC */
static void mpc_i2c_setup_8xxx(struct device_node *node,
					 struct mpc_i2c *i2c,
					 u32 clock)
{
}
#endif /* CONFIG_FSL_SOC */

static void mpc_i2c_finish(struct mpc_i2c *i2c, int rc)
{
	i2c->rc = rc;
	i2c->block = 0;
	i2c->cntl_bits = CCR_MEN;
	writeccr(i2c, i2c->cntl_bits);
	wake_up(&i2c->waitq);
}

static void mpc_i2c_do_action(struct mpc_i2c *i2c)
{
	struct i2c_msg *msg = &i2c->msgs[i2c->curr_msg];
	int dir = 0;
	int recv_len = 0;
	u8 byte;

	dev_dbg(i2c->dev, "action = %s\n", action_str[i2c->action]);

	i2c->cntl_bits &= ~(CCR_RSTA | CCR_MTX | CCR_TXAK);

	if (msg->flags & I2C_M_RD)
		dir = 1;
	if (msg->flags & I2C_M_RECV_LEN)
		recv_len = 1;

	switch (i2c->action) {
	case MPC_I2C_ACTION_RESTART:
		i2c->cntl_bits |= CCR_RSTA;
		fallthrough;

	case MPC_I2C_ACTION_START:
		i2c->cntl_bits |= CCR_MSTA | CCR_MTX;
		writeccr(i2c, i2c->cntl_bits);
		writeb((msg->addr << 1) | dir, i2c->base + MPC_I2C_DR);
		i2c->expect_rxack = 1;
		i2c->action = dir ? MPC_I2C_ACTION_READ_BEGIN : MPC_I2C_ACTION_WRITE;
		break;

	case MPC_I2C_ACTION_READ_BEGIN:
		if (msg->len) {
			if (msg->len == 1 && !(msg->flags & I2C_M_RECV_LEN))
				i2c->cntl_bits |= CCR_TXAK;

			writeccr(i2c, i2c->cntl_bits);
			/* Dummy read */
			readb(i2c->base + MPC_I2C_DR);
		}
		i2c->action = MPC_I2C_ACTION_READ_BYTE;
		break;

	case MPC_I2C_ACTION_READ_BYTE:
		if (i2c->byte_posn || !recv_len) {
			/* Generate Tx ACK on next to last byte */
			if (i2c->byte_posn == msg->len - 2)
				i2c->cntl_bits |= CCR_TXAK;
			/* Do not generate stop on last byte */
			if (i2c->byte_posn == msg->len - 1)
				i2c->cntl_bits |= CCR_MTX;

			writeccr(i2c, i2c->cntl_bits);
		}

		byte = readb(i2c->base + MPC_I2C_DR);

		if (i2c->byte_posn == 0 && recv_len) {
			if (byte == 0 || byte > I2C_SMBUS_BLOCK_MAX) {
				mpc_i2c_finish(i2c, -EPROTO);
				return;
			}
			msg->len += byte;
			/*
			 * For block reads, generate Tx ACK here if data length
			 * is 1 byte (total length is 2 bytes).
			 */
			if (msg->len == 2) {
				i2c->cntl_bits |= CCR_TXAK;
				writeccr(i2c, i2c->cntl_bits);
			}
		}

		dev_dbg(i2c->dev, "%s %02x\n", action_str[i2c->action], byte);
		msg->buf[i2c->byte_posn++] = byte;
		break;

	case MPC_I2C_ACTION_WRITE:
		dev_dbg(i2c->dev, "%s %02x\n", action_str[i2c->action],
			msg->buf[i2c->byte_posn]);
		writeb(msg->buf[i2c->byte_posn++], i2c->base + MPC_I2C_DR);
		i2c->expect_rxack = 1;
		break;

	case MPC_I2C_ACTION_STOP:
		mpc_i2c_finish(i2c, 0);
		break;

	default:
		WARN(1, "Unexpected action %d\n", i2c->action);
		break;
	}

	if (msg->len == i2c->byte_posn) {
		i2c->curr_msg++;
		i2c->byte_posn = 0;

		if (i2c->curr_msg == i2c->num_msgs) {
			i2c->action = MPC_I2C_ACTION_STOP;
			/*
			 * We don't get another interrupt on read so
			 * finish the transfer now
			 */
			if (dir)
				mpc_i2c_finish(i2c, 0);
		} else {
			i2c->action = MPC_I2C_ACTION_RESTART;
		}
	}
}

static void mpc_i2c_do_intr(struct mpc_i2c *i2c, u8 status)
{
	spin_lock(&i2c->lock);

	if (!(status & CSR_MCF)) {
		dev_dbg(i2c->dev, "unfinished\n");
		mpc_i2c_finish(i2c, -EIO);
		goto out;
	}

	if (status & CSR_MAL) {
		dev_dbg(i2c->dev, "arbitration lost\n");
		mpc_i2c_finish(i2c, -EAGAIN);
		goto out;
	}

	if (i2c->expect_rxack && (status & CSR_RXAK)) {
		dev_dbg(i2c->dev, "no Rx ACK\n");
		mpc_i2c_finish(i2c, -ENXIO);
		goto out;
	}
	i2c->expect_rxack = 0;

	mpc_i2c_do_action(i2c);

out:
	spin_unlock(&i2c->lock);
}

static irqreturn_t mpc_i2c_isr(int irq, void *dev_id)
{
	struct mpc_i2c *i2c = dev_id;
	u8 status;

	status = readb(i2c->base + MPC_I2C_SR);
	if (status & CSR_MIF) {
		/* Wait up to 100us for transfer to properly complete */
		readb_poll_timeout_atomic(i2c->base + MPC_I2C_SR, status, status & CSR_MCF, 0, 100);
		writeb(0, i2c->base + MPC_I2C_SR);
		mpc_i2c_do_intr(i2c, status);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int mpc_i2c_wait_for_completion(struct mpc_i2c *i2c)
{
	long time_left;

	time_left = wait_event_timeout(i2c->waitq, !i2c->block, i2c->adap.timeout);
	if (!time_left)
		return -ETIMEDOUT;
	if (time_left < 0)
		return time_left;

	return 0;
}

static int mpc_i2c_execute_msg(struct mpc_i2c *i2c)
{
	unsigned long orig_jiffies;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&i2c->lock, flags);

	i2c->curr_msg = 0;
	i2c->rc = 0;
	i2c->byte_posn = 0;
	i2c->block = 1;
	i2c->action = MPC_I2C_ACTION_START;

	i2c->cntl_bits = CCR_MEN | CCR_MIEN;
	writeb(0, i2c->base + MPC_I2C_SR);
	writeccr(i2c, i2c->cntl_bits);

	mpc_i2c_do_action(i2c);

	spin_unlock_irqrestore(&i2c->lock, flags);

	ret = mpc_i2c_wait_for_completion(i2c);
	if (ret)
		i2c->rc = ret;

	if (i2c->rc == -EIO || i2c->rc == -EAGAIN || i2c->rc == -ETIMEDOUT)
		i2c_recover_bus(&i2c->adap);

	orig_jiffies = jiffies;
	/* Wait until STOP is seen, allow up to 1 s */
	while (readb(i2c->base + MPC_I2C_SR) & CSR_MBB) {
		if (time_after(jiffies, orig_jiffies + HZ)) {
			u8 status = readb(i2c->base + MPC_I2C_SR);

			dev_dbg(i2c->dev, "timeout\n");
			if ((status & (CSR_MCF | CSR_MBB | CSR_RXAK)) != 0) {
				writeb(status & ~CSR_MAL,
				       i2c->base + MPC_I2C_SR);
				i2c_recover_bus(&i2c->adap);
			}
			return -EIO;
		}
		cond_resched();
	}

	return i2c->rc;
}

static int mpc_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int rc, ret = num;
	struct mpc_i2c *i2c = i2c_get_adapdata(adap);
	int i;

	dev_dbg(i2c->dev, "num = %d\n", num);
	for (i = 0; i < num; i++)
		dev_dbg(i2c->dev, "  addr = %02x, flags = %02x, len = %d, %*ph\n",
			msgs[i].addr, msgs[i].flags, msgs[i].len,
			msgs[i].flags & I2C_M_RD ? 0 : msgs[i].len,
			msgs[i].buf);

	WARN_ON(i2c->msgs != NULL);
	i2c->msgs = msgs;
	i2c->num_msgs = num;

	rc = mpc_i2c_execute_msg(i2c);
	if (rc < 0)
		ret = rc;

	i2c->num_msgs = 0;
	i2c->msgs = NULL;

	return ret;
}

static u32 mpc_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL
	  | I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_FUNC_SMBUS_BLOCK_PROC_CALL;
}

static int fsl_i2c_bus_recovery(struct i2c_adapter *adap)
{
	struct mpc_i2c *i2c = i2c_get_adapdata(adap);

	if (i2c->has_errata_A004447)
		mpc_i2c_fixup_A004447(i2c);
	else
		mpc_i2c_fixup(i2c);

	return 0;
}

static const struct i2c_algorithm mpc_algo = {
	.master_xfer = mpc_xfer,
	.functionality = mpc_functionality,
};

static struct i2c_adapter mpc_ops = {
	.owner = THIS_MODULE,
	.algo = &mpc_algo,
	.timeout = HZ,
};

static struct i2c_bus_recovery_info fsl_i2c_recovery_info = {
	.recover_bus = fsl_i2c_bus_recovery,
};

static int fsl_i2c_probe(struct platform_device *op)
{
	const struct mpc_i2c_data *data;
	struct mpc_i2c *i2c;
	const u32 *prop;
	u32 clock = MPC_I2C_CLOCK_LEGACY;
	int result = 0;
	int plen;
	struct clk *clk;
	int err;

	i2c = devm_kzalloc(&op->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->dev = &op->dev; /* for debug and error output */

	init_waitqueue_head(&i2c->waitq);
	spin_lock_init(&i2c->lock);

	i2c->base = devm_platform_ioremap_resource(op, 0);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	i2c->irq = platform_get_irq(op, 0);
	if (i2c->irq < 0)
		return i2c->irq;

	result = devm_request_irq(&op->dev, i2c->irq, mpc_i2c_isr,
			IRQF_SHARED, "i2c-mpc", i2c);
	if (result < 0) {
		dev_err(i2c->dev, "failed to attach interrupt\n");
		return result;
	}

	/*
	 * enable clock for the I2C peripheral (non fatal),
	 * keep a reference upon successful allocation
	 */
	clk = devm_clk_get_optional(&op->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	err = clk_prepare_enable(clk);
	if (err) {
		dev_err(&op->dev, "failed to enable clock\n");
		return err;
	}

	i2c->clk_per = clk;

	if (of_property_read_bool(op->dev.of_node, "fsl,preserve-clocking")) {
		clock = MPC_I2C_CLOCK_PRESERVE;
	} else {
		prop = of_get_property(op->dev.of_node, "clock-frequency",
					&plen);
		if (prop && plen == sizeof(u32))
			clock = *prop;
	}

	data = device_get_match_data(&op->dev);
	if (data) {
		data->setup(op->dev.of_node, i2c, clock);
	} else {
		/* Backwards compatibility */
		if (of_get_property(op->dev.of_node, "dfsrr", NULL))
			mpc_i2c_setup_8xxx(op->dev.of_node, i2c, clock);
	}

	prop = of_get_property(op->dev.of_node, "fsl,timeout", &plen);
	if (prop && plen == sizeof(u32)) {
		mpc_ops.timeout = *prop * HZ / 1000000;
		if (mpc_ops.timeout < 5)
			mpc_ops.timeout = 5;
	}
	dev_info(i2c->dev, "timeout %u us\n", mpc_ops.timeout * 1000000 / HZ);

	if (of_property_read_bool(op->dev.of_node, "fsl,i2c-erratum-a004447"))
		i2c->has_errata_A004447 = true;

	i2c->adap = mpc_ops;
	scnprintf(i2c->adap.name, sizeof(i2c->adap.name),
		  "MPC adapter (%s)", of_node_full_name(op->dev.of_node));
	i2c->adap.dev.parent = &op->dev;
	i2c->adap.nr = op->id;
	i2c->adap.dev.of_node = of_node_get(op->dev.of_node);
	i2c->adap.bus_recovery_info = &fsl_i2c_recovery_info;
	platform_set_drvdata(op, i2c);
	i2c_set_adapdata(&i2c->adap, i2c);

	result = i2c_add_numbered_adapter(&i2c->adap);
	if (result)
		goto fail_add;

	return 0;

 fail_add:
	clk_disable_unprepare(i2c->clk_per);

	return result;
};

static int fsl_i2c_remove(struct platform_device *op)
{
	struct mpc_i2c *i2c = platform_get_drvdata(op);

	i2c_del_adapter(&i2c->adap);

	clk_disable_unprepare(i2c->clk_per);

	return 0;
};

static int __maybe_unused mpc_i2c_suspend(struct device *dev)
{
	struct mpc_i2c *i2c = dev_get_drvdata(dev);

	i2c->fdr = readb(i2c->base + MPC_I2C_FDR);
	i2c->dfsrr = readb(i2c->base + MPC_I2C_DFSRR);

	return 0;
}

static int __maybe_unused mpc_i2c_resume(struct device *dev)
{
	struct mpc_i2c *i2c = dev_get_drvdata(dev);

	writeb(i2c->fdr, i2c->base + MPC_I2C_FDR);
	writeb(i2c->dfsrr, i2c->base + MPC_I2C_DFSRR);

	return 0;
}
static SIMPLE_DEV_PM_OPS(mpc_i2c_pm_ops, mpc_i2c_suspend, mpc_i2c_resume);

static const struct mpc_i2c_data mpc_i2c_data_512x = {
	.setup = mpc_i2c_setup_512x,
};

static const struct mpc_i2c_data mpc_i2c_data_52xx = {
	.setup = mpc_i2c_setup_52xx,
};

static const struct mpc_i2c_data mpc_i2c_data_8313 = {
	.setup = mpc_i2c_setup_8xxx,
};

static const struct mpc_i2c_data mpc_i2c_data_8543 = {
	.setup = mpc_i2c_setup_8xxx,
};

static const struct mpc_i2c_data mpc_i2c_data_8544 = {
	.setup = mpc_i2c_setup_8xxx,
};

static const struct of_device_id mpc_i2c_of_match[] = {
	{.compatible = "mpc5200-i2c", .data = &mpc_i2c_data_52xx, },
	{.compatible = "fsl,mpc5200b-i2c", .data = &mpc_i2c_data_52xx, },
	{.compatible = "fsl,mpc5200-i2c", .data = &mpc_i2c_data_52xx, },
	{.compatible = "fsl,mpc5121-i2c", .data = &mpc_i2c_data_512x, },
	{.compatible = "fsl,mpc8313-i2c", .data = &mpc_i2c_data_8313, },
	{.compatible = "fsl,mpc8543-i2c", .data = &mpc_i2c_data_8543, },
	{.compatible = "fsl,mpc8544-i2c", .data = &mpc_i2c_data_8544, },
	/* Backward compatibility */
	{.compatible = "fsl-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, mpc_i2c_of_match);

/* Structure for a device driver */
static struct platform_driver mpc_i2c_driver = {
	.probe		= fsl_i2c_probe,
	.remove		= fsl_i2c_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = mpc_i2c_of_match,
		.pm = &mpc_i2c_pm_ops,
	},
};

module_platform_driver(mpc_i2c_driver);

MODULE_AUTHOR("Adrian Cox <adrian@humboldt.co.uk>");
MODULE_DESCRIPTION("I2C-Bus adapter for MPC107 bridge and "
		   "MPC824x/83xx/85xx/86xx/512x/52xx processors");
MODULE_LICENSE("GPL");
