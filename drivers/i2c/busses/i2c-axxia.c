/*
 * This driver implements I2C master functionality using the LSI API2C
 * controller.
 *
 * NOTE: The controller has a limitation in that it can only do transfers of
 * maximum 255 bytes at a time. If a larger transfer is attempted, error code
 * (-EINVAL) is returned.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#define SCL_WAIT_TIMEOUT_NS 25000000
#define I2C_XFER_TIMEOUT    (msecs_to_jiffies(250))
#define I2C_STOP_TIMEOUT    (msecs_to_jiffies(100))
#define FIFO_SIZE           8
#define SEQ_LEN             2

#define GLOBAL_CONTROL		0x00
#define   GLOBAL_MST_EN         BIT(0)
#define   GLOBAL_SLV_EN         BIT(1)
#define   GLOBAL_IBML_EN        BIT(2)
#define INTERRUPT_STATUS	0x04
#define INTERRUPT_ENABLE	0x08
#define   INT_SLV               BIT(1)
#define   INT_MST               BIT(0)
#define WAIT_TIMER_CONTROL	0x0c
#define   WT_EN			BIT(15)
#define   WT_VALUE(_x)		((_x) & 0x7fff)
#define IBML_TIMEOUT		0x10
#define IBML_LOW_MEXT		0x14
#define IBML_LOW_SEXT		0x18
#define TIMER_CLOCK_DIV		0x1c
#define I2C_BUS_MONITOR		0x20
#define   BM_SDAC		BIT(3)
#define   BM_SCLC		BIT(2)
#define   BM_SDAS		BIT(1)
#define   BM_SCLS		BIT(0)
#define SOFT_RESET		0x24
#define MST_COMMAND		0x28
#define   CMD_BUSY		(1<<3)
#define   CMD_MANUAL		(0x00 | CMD_BUSY)
#define   CMD_AUTO		(0x01 | CMD_BUSY)
#define   CMD_SEQUENCE		(0x02 | CMD_BUSY)
#define MST_RX_XFER		0x2c
#define MST_TX_XFER		0x30
#define MST_ADDR_1		0x34
#define MST_ADDR_2		0x38
#define MST_DATA		0x3c
#define MST_TX_FIFO		0x40
#define MST_RX_FIFO		0x44
#define MST_INT_ENABLE		0x48
#define MST_INT_STATUS		0x4c
#define   MST_STATUS_RFL	(1 << 13) /* RX FIFO serivce */
#define   MST_STATUS_TFL	(1 << 12) /* TX FIFO service */
#define   MST_STATUS_SNS	(1 << 11) /* Manual mode done */
#define   MST_STATUS_SS		(1 << 10) /* Automatic mode done */
#define   MST_STATUS_SCC	(1 << 9)  /* Stop complete */
#define   MST_STATUS_IP		(1 << 8)  /* Invalid parameter */
#define   MST_STATUS_TSS	(1 << 7)  /* Timeout */
#define   MST_STATUS_AL		(1 << 6)  /* Arbitration lost */
#define   MST_STATUS_ND		(1 << 5)  /* NAK on data phase */
#define   MST_STATUS_NA		(1 << 4)  /* NAK on address phase */
#define   MST_STATUS_NAK	(MST_STATUS_NA | \
				 MST_STATUS_ND)
#define   MST_STATUS_ERR	(MST_STATUS_NAK | \
				 MST_STATUS_AL  | \
				 MST_STATUS_IP)
#define MST_TX_BYTES_XFRD	0x50
#define MST_RX_BYTES_XFRD	0x54
#define SCL_HIGH_PERIOD		0x80
#define SCL_LOW_PERIOD		0x84
#define SPIKE_FLTR_LEN		0x88
#define SDA_SETUP_TIME		0x8c
#define SDA_HOLD_TIME		0x90

/**
 * axxia_i2c_dev - I2C device context
 * @base: pointer to register struct
 * @msg: pointer to current message
 * @msg_r: pointer to current read message (sequence transfer)
 * @msg_xfrd: number of bytes transferred in tx_fifo
 * @msg_xfrd_r: number of bytes transferred in rx_fifo
 * @msg_err: error code for completed message
 * @msg_complete: xfer completion object
 * @dev: device reference
 * @adapter: core i2c abstraction
 * @i2c_clk: clock reference for i2c input clock
 * @bus_clk_rate: current i2c bus clock rate
 * @last: a flag indicating is this is last message in transfer
 */
struct axxia_i2c_dev {
	void __iomem *base;
	struct i2c_msg *msg;
	struct i2c_msg *msg_r;
	size_t msg_xfrd;
	size_t msg_xfrd_r;
	int msg_err;
	struct completion msg_complete;
	struct device *dev;
	struct i2c_adapter adapter;
	struct clk *i2c_clk;
	u32 bus_clk_rate;
	bool last;
};

static void i2c_int_disable(struct axxia_i2c_dev *idev, u32 mask)
{
	u32 int_en;

	int_en = readl(idev->base + MST_INT_ENABLE);
	writel(int_en & ~mask, idev->base + MST_INT_ENABLE);
}

static void i2c_int_enable(struct axxia_i2c_dev *idev, u32 mask)
{
	u32 int_en;

	int_en = readl(idev->base + MST_INT_ENABLE);
	writel(int_en | mask, idev->base + MST_INT_ENABLE);
}

/**
 * ns_to_clk - Convert time (ns) to clock cycles for the given clock frequency.
 */
static u32 ns_to_clk(u64 ns, u32 clk_mhz)
{
	return div_u64(ns * clk_mhz, 1000);
}

static int axxia_i2c_init(struct axxia_i2c_dev *idev)
{
	u32 divisor = clk_get_rate(idev->i2c_clk) / idev->bus_clk_rate;
	u32 clk_mhz = clk_get_rate(idev->i2c_clk) / 1000000;
	u32 t_setup;
	u32 t_high, t_low;
	u32 tmo_clk;
	u32 prescale;
	unsigned long timeout;

	dev_dbg(idev->dev, "rate=%uHz per_clk=%uMHz -> ratio=1:%u\n",
		idev->bus_clk_rate, clk_mhz, divisor);

	/* Reset controller */
	writel(0x01, idev->base + SOFT_RESET);
	timeout = jiffies + msecs_to_jiffies(100);
	while (readl(idev->base + SOFT_RESET) & 1) {
		if (time_after(jiffies, timeout)) {
			dev_warn(idev->dev, "Soft reset failed\n");
			break;
		}
	}

	/* Enable Master Mode */
	writel(0x1, idev->base + GLOBAL_CONTROL);

	if (idev->bus_clk_rate <= 100000) {
		/* Standard mode SCL 50/50, tSU:DAT = 250 ns */
		t_high = divisor * 1 / 2;
		t_low = divisor * 1 / 2;
		t_setup = ns_to_clk(250, clk_mhz);
	} else {
		/* Fast mode SCL 33/66, tSU:DAT = 100 ns */
		t_high = divisor * 1 / 3;
		t_low = divisor * 2 / 3;
		t_setup = ns_to_clk(100, clk_mhz);
	}

	/* SCL High Time */
	writel(t_high, idev->base + SCL_HIGH_PERIOD);
	/* SCL Low Time */
	writel(t_low, idev->base + SCL_LOW_PERIOD);
	/* SDA Setup Time */
	writel(t_setup, idev->base + SDA_SETUP_TIME);
	/* SDA Hold Time, 300ns */
	writel(ns_to_clk(300, clk_mhz), idev->base + SDA_HOLD_TIME);
	/* Filter <50ns spikes */
	writel(ns_to_clk(50, clk_mhz), idev->base + SPIKE_FLTR_LEN);

	/* Configure Time-Out Registers */
	tmo_clk = ns_to_clk(SCL_WAIT_TIMEOUT_NS, clk_mhz);

	/* Find prescaler value that makes tmo_clk fit in 15-bits counter. */
	for (prescale = 0; prescale < 15; ++prescale) {
		if (tmo_clk <= 0x7fff)
			break;
		tmo_clk >>= 1;
	}
	if (tmo_clk > 0x7fff)
		tmo_clk = 0x7fff;

	/* Prescale divider (log2) */
	writel(prescale, idev->base + TIMER_CLOCK_DIV);
	/* Timeout in divided clocks */
	writel(WT_EN | WT_VALUE(tmo_clk), idev->base + WAIT_TIMER_CONTROL);

	/* Mask all master interrupt bits */
	i2c_int_disable(idev, ~0);

	/* Interrupt enable */
	writel(0x01, idev->base + INTERRUPT_ENABLE);

	return 0;
}

static int i2c_m_rd(const struct i2c_msg *msg)
{
	return (msg->flags & I2C_M_RD) != 0;
}

static int i2c_m_ten(const struct i2c_msg *msg)
{
	return (msg->flags & I2C_M_TEN) != 0;
}

static int i2c_m_recv_len(const struct i2c_msg *msg)
{
	return (msg->flags & I2C_M_RECV_LEN) != 0;
}

/**
 * axxia_i2c_empty_rx_fifo - Fetch data from RX FIFO and update SMBus block
 * transfer length if this is the first byte of such a transfer.
 */
static int axxia_i2c_empty_rx_fifo(struct axxia_i2c_dev *idev)
{
	struct i2c_msg *msg = idev->msg_r;
	size_t rx_fifo_avail = readl(idev->base + MST_RX_FIFO);
	int bytes_to_transfer = min(rx_fifo_avail, msg->len - idev->msg_xfrd_r);

	while (bytes_to_transfer-- > 0) {
		int c = readl(idev->base + MST_DATA);

		if (idev->msg_xfrd_r == 0 && i2c_m_recv_len(msg)) {
			/*
			 * Check length byte for SMBus block read
			 */
			if (c <= 0 || c > I2C_SMBUS_BLOCK_MAX) {
				idev->msg_err = -EPROTO;
				i2c_int_disable(idev, ~MST_STATUS_TSS);
				complete(&idev->msg_complete);
				break;
			}
			msg->len = 1 + c;
			writel(msg->len, idev->base + MST_RX_XFER);
		}
		msg->buf[idev->msg_xfrd_r++] = c;
	}

	return 0;
}

/**
 * axxia_i2c_fill_tx_fifo - Fill TX FIFO from current message buffer.
 * @return: Number of bytes left to transfer.
 */
static int axxia_i2c_fill_tx_fifo(struct axxia_i2c_dev *idev)
{
	struct i2c_msg *msg = idev->msg;
	size_t tx_fifo_avail = FIFO_SIZE - readl(idev->base + MST_TX_FIFO);
	int bytes_to_transfer = min(tx_fifo_avail, msg->len - idev->msg_xfrd);
	int ret = msg->len - idev->msg_xfrd - bytes_to_transfer;

	while (bytes_to_transfer-- > 0)
		writel(msg->buf[idev->msg_xfrd++], idev->base + MST_DATA);

	return ret;
}

static irqreturn_t axxia_i2c_isr(int irq, void *_dev)
{
	struct axxia_i2c_dev *idev = _dev;
	u32 status;

	if (!(readl(idev->base + INTERRUPT_STATUS) & INT_MST))
		return IRQ_NONE;

	/* Read interrupt status bits */
	status = readl(idev->base + MST_INT_STATUS);

	if (!idev->msg) {
		dev_warn(idev->dev, "unexpected interrupt\n");
		goto out;
	}

	/* RX FIFO needs service? */
	if (i2c_m_rd(idev->msg_r) && (status & MST_STATUS_RFL))
		axxia_i2c_empty_rx_fifo(idev);

	/* TX FIFO needs service? */
	if (!i2c_m_rd(idev->msg) && (status & MST_STATUS_TFL)) {
		if (axxia_i2c_fill_tx_fifo(idev) == 0)
			i2c_int_disable(idev, MST_STATUS_TFL);
	}

	if (unlikely(status & MST_STATUS_ERR)) {
		/* Transfer error */
		i2c_int_disable(idev, ~0);
		if (status & MST_STATUS_AL)
			idev->msg_err = -EAGAIN;
		else if (status & MST_STATUS_NAK)
			idev->msg_err = -ENXIO;
		else
			idev->msg_err = -EIO;
		dev_dbg(idev->dev, "error %#x, addr=%#x rx=%u/%u tx=%u/%u\n",
			status,
			idev->msg->addr,
			readl(idev->base + MST_RX_BYTES_XFRD),
			readl(idev->base + MST_RX_XFER),
			readl(idev->base + MST_TX_BYTES_XFRD),
			readl(idev->base + MST_TX_XFER));
		complete(&idev->msg_complete);
	} else if (status & MST_STATUS_SCC) {
		/* Stop completed */
		i2c_int_disable(idev, ~MST_STATUS_TSS);
		complete(&idev->msg_complete);
	} else if (status & (MST_STATUS_SNS | MST_STATUS_SS)) {
		/* Transfer done */
		int mask = idev->last ? ~0 : ~MST_STATUS_TSS;

		i2c_int_disable(idev, mask);
		if (i2c_m_rd(idev->msg_r) && idev->msg_xfrd_r < idev->msg_r->len)
			axxia_i2c_empty_rx_fifo(idev);
		complete(&idev->msg_complete);
	} else if (status & MST_STATUS_TSS) {
		/* Transfer timeout */
		idev->msg_err = -ETIMEDOUT;
		i2c_int_disable(idev, ~MST_STATUS_TSS);
		complete(&idev->msg_complete);
	}

out:
	/* Clear interrupt */
	writel(INT_MST, idev->base + INTERRUPT_STATUS);

	return IRQ_HANDLED;
}

static void axxia_i2c_set_addr(struct axxia_i2c_dev *idev, struct i2c_msg *msg)
{
	u32 addr_1, addr_2;

	if (i2c_m_ten(msg)) {
		/* 10-bit address
		 *   addr_1: 5'b11110 | addr[9:8] | (R/nW)
		 *   addr_2: addr[7:0]
		 */
		addr_1 = 0xF0 | ((msg->addr >> 7) & 0x06);
		if (i2c_m_rd(msg))
			addr_1 |= 1;	/* Set the R/nW bit of the address */
		addr_2 = msg->addr & 0xFF;
	} else {
		/* 7-bit address
		 *   addr_1: addr[6:0] | (R/nW)
		 *   addr_2: dont care
		 */
		addr_1 = i2c_8bit_addr_from_msg(msg);
		addr_2 = 0;
	}

	writel(addr_1, idev->base + MST_ADDR_1);
	writel(addr_2, idev->base + MST_ADDR_2);
}

/* The NAK interrupt will be sent _before_ issuing STOP command
 * so the controller might still be busy processing it. No
 * interrupt will be sent at the end so we have to poll for it
 */
static int axxia_i2c_handle_seq_nak(struct axxia_i2c_dev *idev)
{
	unsigned long timeout = jiffies + I2C_XFER_TIMEOUT;

	do {
		if ((readl(idev->base + MST_COMMAND) & CMD_BUSY) == 0)
			return 0;
		usleep_range(1, 100);
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

static int axxia_i2c_xfer_seq(struct axxia_i2c_dev *idev, struct i2c_msg msgs[])
{
	u32 int_mask = MST_STATUS_ERR | MST_STATUS_SS | MST_STATUS_RFL;
	u32 rlen = i2c_m_recv_len(&msgs[1]) ? I2C_SMBUS_BLOCK_MAX : msgs[1].len;
	unsigned long time_left;

	axxia_i2c_set_addr(idev, &msgs[0]);

	writel(msgs[0].len, idev->base + MST_TX_XFER);
	writel(rlen, idev->base + MST_RX_XFER);

	idev->msg = &msgs[0];
	idev->msg_r = &msgs[1];
	idev->msg_xfrd = 0;
	idev->msg_xfrd_r = 0;
	idev->last = true;
	axxia_i2c_fill_tx_fifo(idev);

	writel(CMD_SEQUENCE, idev->base + MST_COMMAND);

	reinit_completion(&idev->msg_complete);
	i2c_int_enable(idev, int_mask);

	time_left = wait_for_completion_timeout(&idev->msg_complete,
						I2C_XFER_TIMEOUT);

	if (idev->msg_err == -ENXIO) {
		if (axxia_i2c_handle_seq_nak(idev))
			axxia_i2c_init(idev);
	} else if (readl(idev->base + MST_COMMAND) & CMD_BUSY) {
		dev_warn(idev->dev, "busy after xfer\n");
	}

	if (time_left == 0) {
		idev->msg_err = -ETIMEDOUT;
		i2c_recover_bus(&idev->adapter);
		axxia_i2c_init(idev);
	}

	if (unlikely(idev->msg_err) && idev->msg_err != -ENXIO)
		axxia_i2c_init(idev);

	return idev->msg_err;
}

static int axxia_i2c_xfer_msg(struct axxia_i2c_dev *idev, struct i2c_msg *msg,
			      bool last)
{
	u32 int_mask = MST_STATUS_ERR;
	u32 rx_xfer, tx_xfer;
	unsigned long time_left;
	unsigned int wt_value;

	idev->msg = msg;
	idev->msg_r = msg;
	idev->msg_xfrd = 0;
	idev->msg_xfrd_r = 0;
	idev->last = last;
	reinit_completion(&idev->msg_complete);

	axxia_i2c_set_addr(idev, msg);

	if (i2c_m_rd(msg)) {
		/* I2C read transfer */
		rx_xfer = i2c_m_recv_len(msg) ? I2C_SMBUS_BLOCK_MAX : msg->len;
		tx_xfer = 0;
	} else {
		/* I2C write transfer */
		rx_xfer = 0;
		tx_xfer = msg->len;
	}

	writel(rx_xfer, idev->base + MST_RX_XFER);
	writel(tx_xfer, idev->base + MST_TX_XFER);

	if (i2c_m_rd(msg))
		int_mask |= MST_STATUS_RFL;
	else if (axxia_i2c_fill_tx_fifo(idev) != 0)
		int_mask |= MST_STATUS_TFL;

	wt_value = WT_VALUE(readl(idev->base + WAIT_TIMER_CONTROL));
	/* Disable wait timer temporarly */
	writel(wt_value, idev->base + WAIT_TIMER_CONTROL);
	/* Check if timeout error happened */
	if (idev->msg_err)
		goto out;

	if (!last) {
		writel(CMD_MANUAL, idev->base + MST_COMMAND);
		int_mask |= MST_STATUS_SNS;
	} else {
		writel(CMD_AUTO, idev->base + MST_COMMAND);
		int_mask |= MST_STATUS_SS;
	}

	writel(WT_EN | wt_value, idev->base + WAIT_TIMER_CONTROL);

	i2c_int_enable(idev, int_mask);

	time_left = wait_for_completion_timeout(&idev->msg_complete,
					      I2C_XFER_TIMEOUT);

	i2c_int_disable(idev, int_mask);

	if (readl(idev->base + MST_COMMAND) & CMD_BUSY)
		dev_warn(idev->dev, "busy after xfer\n");

	if (time_left == 0) {
		idev->msg_err = -ETIMEDOUT;
		i2c_recover_bus(&idev->adapter);
		axxia_i2c_init(idev);
	}

out:
	if (unlikely(idev->msg_err) && idev->msg_err != -ENXIO &&
			idev->msg_err != -ETIMEDOUT)
		axxia_i2c_init(idev);

	return idev->msg_err;
}

/* This function checks if the msgs[] array contains messages compatible with
 * Sequence mode of operation. This mode assumes there will be exactly one
 * write of non-zero length followed by exactly one read of non-zero length,
 * both targeted at the same client device.
 */
static bool axxia_i2c_sequence_ok(struct i2c_msg msgs[], int num)
{
	return num == SEQ_LEN && !i2c_m_rd(&msgs[0]) && i2c_m_rd(&msgs[1]) &&
	       msgs[0].len > 0 && msgs[0].len <= FIFO_SIZE &&
	       msgs[1].len > 0 && msgs[0].addr == msgs[1].addr;
}

static int
axxia_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct axxia_i2c_dev *idev = i2c_get_adapdata(adap);
	int i;
	int ret = 0;

	idev->msg_err = 0;

	if (axxia_i2c_sequence_ok(msgs, num)) {
		ret = axxia_i2c_xfer_seq(idev, msgs);
		return ret ? : SEQ_LEN;
	}

	i2c_int_enable(idev, MST_STATUS_TSS);

	for (i = 0; ret == 0 && i < num; ++i)
		ret = axxia_i2c_xfer_msg(idev, &msgs[i], i == (num - 1));

	return ret ? : i;
}

static int axxia_i2c_get_scl(struct i2c_adapter *adap)
{
	struct axxia_i2c_dev *idev = i2c_get_adapdata(adap);

	return !!(readl(idev->base + I2C_BUS_MONITOR) & BM_SCLS);
}

static void axxia_i2c_set_scl(struct i2c_adapter *adap, int val)
{
	struct axxia_i2c_dev *idev = i2c_get_adapdata(adap);
	u32 tmp;

	/* Preserve SDA Control */
	tmp = readl(idev->base + I2C_BUS_MONITOR) & BM_SDAC;
	if (!val)
		tmp |= BM_SCLC;
	writel(tmp, idev->base + I2C_BUS_MONITOR);
}

static int axxia_i2c_get_sda(struct i2c_adapter *adap)
{
	struct axxia_i2c_dev *idev = i2c_get_adapdata(adap);

	return !!(readl(idev->base + I2C_BUS_MONITOR) & BM_SDAS);
}

static struct i2c_bus_recovery_info axxia_i2c_recovery_info = {
	.recover_bus = i2c_generic_scl_recovery,
	.get_scl = axxia_i2c_get_scl,
	.set_scl = axxia_i2c_set_scl,
	.get_sda = axxia_i2c_get_sda,
};

static u32 axxia_i2c_func(struct i2c_adapter *adap)
{
	u32 caps = (I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR |
		    I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_BLOCK_DATA);
	return caps;
}

static const struct i2c_algorithm axxia_i2c_algo = {
	.master_xfer = axxia_i2c_xfer,
	.functionality = axxia_i2c_func,
};

static const struct i2c_adapter_quirks axxia_i2c_quirks = {
	.max_read_len = 255,
	.max_write_len = 255,
};

static int axxia_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct axxia_i2c_dev *idev = NULL;
	struct resource *res;
	void __iomem *base;
	int irq;
	int ret = 0;

	idev = devm_kzalloc(&pdev->dev, sizeof(*idev), GFP_KERNEL);
	if (!idev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "missing interrupt resource\n");
		return irq;
	}

	idev->i2c_clk = devm_clk_get(&pdev->dev, "i2c");
	if (IS_ERR(idev->i2c_clk)) {
		dev_err(&pdev->dev, "missing clock\n");
		return PTR_ERR(idev->i2c_clk);
	}

	idev->base = base;
	idev->dev = &pdev->dev;
	init_completion(&idev->msg_complete);

	of_property_read_u32(np, "clock-frequency", &idev->bus_clk_rate);
	if (idev->bus_clk_rate == 0)
		idev->bus_clk_rate = 100000;	/* default clock rate */

	ret = clk_prepare_enable(idev->i2c_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		return ret;
	}

	ret = axxia_i2c_init(idev);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize\n");
		goto error_disable_clk;
	}

	ret = devm_request_irq(&pdev->dev, irq, axxia_i2c_isr, 0,
			       pdev->name, idev);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim IRQ%d\n", irq);
		goto error_disable_clk;
	}

	i2c_set_adapdata(&idev->adapter, idev);
	strlcpy(idev->adapter.name, pdev->name, sizeof(idev->adapter.name));
	idev->adapter.owner = THIS_MODULE;
	idev->adapter.algo = &axxia_i2c_algo;
	idev->adapter.bus_recovery_info = &axxia_i2c_recovery_info;
	idev->adapter.quirks = &axxia_i2c_quirks;
	idev->adapter.dev.parent = &pdev->dev;
	idev->adapter.dev.of_node = pdev->dev.of_node;

	platform_set_drvdata(pdev, idev);

	ret = i2c_add_adapter(&idev->adapter);
	if (ret)
		goto error_disable_clk;

	return 0;

error_disable_clk:
	clk_disable_unprepare(idev->i2c_clk);
	return ret;
}

static int axxia_i2c_remove(struct platform_device *pdev)
{
	struct axxia_i2c_dev *idev = platform_get_drvdata(pdev);

	clk_disable_unprepare(idev->i2c_clk);
	i2c_del_adapter(&idev->adapter);

	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id axxia_i2c_of_match[] = {
	{ .compatible = "lsi,api2c", },
	{},
};

MODULE_DEVICE_TABLE(of, axxia_i2c_of_match);

static struct platform_driver axxia_i2c_driver = {
	.probe = axxia_i2c_probe,
	.remove = axxia_i2c_remove,
	.driver = {
		.name = "axxia-i2c",
		.of_match_table = axxia_i2c_of_match,
	},
};

module_platform_driver(axxia_i2c_driver);

MODULE_DESCRIPTION("Axxia I2C Bus driver");
MODULE_AUTHOR("Anders Berg <anders.berg@lsi.com>");
MODULE_LICENSE("GPL v2");
