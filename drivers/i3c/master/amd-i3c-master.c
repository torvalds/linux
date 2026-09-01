// SPDX-License-Identifier: GPL-2.0
/*
 * I3C master driver for the AMD I3C controller.
 *
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i3c/master.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/unaligned.h>

#define XI3C_VERSION_OFFSET			0x00	/* Version Register */
#define XI3C_RESET_OFFSET			0x04	/* Soft Reset Register */
#define XI3C_CR_OFFSET				0x08	/* Control Register */
#define XI3C_ADDRESS_OFFSET			0x0C	/* Target Address Register */
#define XI3C_SR_OFFSET				0x10	/* Status Register */
#define XI3C_CMD_FIFO_OFFSET			0x20	/* I3C Command FIFO Register */
#define XI3C_WR_FIFO_OFFSET			0x24	/* I3C Write Data FIFO Register */
#define XI3C_RD_FIFO_OFFSET			0x28	/* I3C Read Data FIFO Register */
#define XI3C_RESP_STATUS_FIFO_OFFSET		0x2C	/* I3C Response status FIFO Register */
#define XI3C_FIFO_LVL_STATUS_OFFSET		0x30	/* CMD slots free | WR-FIFO free (words) */
#define XI3C_FIFO_LVL_STATUS_1_OFFSET		0x34	/* RESP fill | RD-FIFO fill level (words) */
#define XI3C_SCL_HIGH_TIME_OFFSET		0x38	/* I3C SCL HIGH Register */
#define XI3C_SCL_LOW_TIME_OFFSET		0x3C	/* I3C SCL LOW Register */
#define XI3C_SDA_HOLD_TIME_OFFSET		0x40	/* I3C SDA HOLD Register */
#define XI3C_TSU_START_OFFSET			0x48	/* I3C START SETUP Register */
#define XI3C_THD_START_OFFSET			0x4C	/* I3C START HOLD Register */
#define XI3C_TSU_STOP_OFFSET			0x50	/* I3C STOP Setup Register */
#define XI3C_OD_SCL_HIGH_TIME_OFFSET		0x54	/* I3C OD SCL HIGH Register */
#define XI3C_OD_SCL_LOW_TIME_OFFSET		0x58	/* I3C OD SCL LOW Register */
#define XI3C_PID0_OFFSET			0x6C	/* LSB 4 bytes of the PID */
#define XI3C_PID1_BCR_DCR			0x70	/* MSB 2 bytes of the PID, BCR and DCR */

#define XI3C_CR_EN_MASK				BIT(0)	/* Core Enable */
#define XI3C_CR_RESUME_MASK			BIT(2)	/* Core Resume */
#define XI3C_SR_RESP_NOT_EMPTY_MASK		BIT(4)	/* Resp Fifo not empty status mask */
#define XI3C_RD_FIFO_NOT_EMPTY_MASK		BIT(15)	/* Read Fifo not empty status mask */

#define XI3C_BCR_MASK				GENMASK(23, 16)
#define XI3C_DCR_MASK				GENMASK(31, 24)
#define XI3C_PID_MASK				GENMASK_ULL(63, 16)
#define XI3C_TIMING_MASK			GENMASK(17, 0)
#define XI3C_REV_NUM_MASK			GENMASK(15, 8)
#define XI3C_PID1_MASK				GENMASK(15, 0)
#define XI3C_FIFO_LEVEL_MASK			GENMASK(15, 0)
#define XI3C_RESP_CODE_MASK			GENMASK(8, 5)

/* Controller response codes; PG439 page 34, Table 46 */
#define XI3C_RESP_CODE_SUCCESS			0	/* Transfer completed OK */
#define XI3C_RESP_CODE_NO_TARGET		2	/* 7E NACK: no target on bus */
#define XI3C_RESP_CODE_NACK			3	/* Target NACK / DAA end */
#define XI3C_RESP_CODE_READ_EARLY_TERM	5	/* Target ended read early (T-bit), short read */

/* xi3c_get_response() private return: read ended early with valid data; resume needed */
#define XI3C_XFER_SHORT_READ			1

#define XI3C_RESP_BYTES_MASK			GENMASK(20, 9)	/* NUM_BYTES processed */
#define XI3C_ADDR_MASK				GENMASK(6, 0)
#define XI3C_FIFOS_RST_MASK			GENMASK(4, 1)

/* Command FIFO word layout (bit ranges encoded in the GENMASK/BIT args) */
#define XI3C_CMD_TYPE				GENMASK(3, 0)	/* command type */
#define XI3C_CMD_TERMINATE			BIT(4)		/* terminate (last cmd of xfer) */
#define XI3C_CMD_ADDR				GENMASK(15, 8)	/* target address << 1 | RnW */
#define XI3C_CMD_LEN				GENMASK(27, 16)	/* payload length in bytes */
#define XI3C_CMD_TID				GENMASK(31, 28)	/* transfer ID */

/* tLOW_OD open-drain SCL low; MIPI I3C v1.1.1 Table 74 min 200 ns, 500 ns chosen conservatively */
#define XI3C_OD_TLOW_NS				500
/* Open-drain SCL high (tHIGH) max; MIPI I3C v1.1.1 Table 74 (41 ns) */
#define XI3C_OD_THIGH_NS			41
/*
 * tSU_STA/tHD_STA/tSU_STO min, 400 kHz/Fm; MIPI I3C v1.1.1 Table 73
 * (mixed bus with legacy I2C device)
 */
#define XI3C_I2C_TCASMIN_NS			600
/* tSU_STA/tHD_STA/tSU_STO min, 1 MHz/Fm+; MIPI I3C v1.1.1 Table 73 (pure I3C bus) */
#define XI3C_TCASMIN_NS				260
/* Max payload per transfer: 12-bit CMD length field (XI3C_CMD_LEN); PG439 page 32, Table 42 */
#define XI3C_MAXDATA_LENGTH			4095
/* Max enumerated devices; PG439 page 27, AXI_I3C_IBI_TARGET_ADDR register detail */
#define XI3C_MAX_DEVS				128
/* DAA target response = 48-bit PID + BCR + DCR = 8 bytes; PG439 page 28 */
#define XI3C_DAA_SLAVEINFO_READ_BYTECOUNT	8

/*
 * Min SDA hold cycles, rev 0 IP. Revision-specific value, PG439 page 24,
 * AXI_I3C_SDA_HOLD_TIME register detail
 */
#define XI3C_THOLD_MIN_REV0			5
/*
 * Min SDA hold cycles, rev >= 1 IP. Revision-specific value, PG439 page 24,
 * AXI_I3C_SDA_HOLD_TIME register detail
 */
#define XI3C_THOLD_MIN_REV1			6
/*
 * SCL/SDA pre-bias to account for the HW pipeline. PG439 page 24,
 * AXI_I3C_SDA_HOLD_TIME register detail
 */
#define XI3C_CYCLE_ADJUST			2
/* Short settling delay so the FIFO reset assert/de-assert takes effect before the FIFOs are used */
#define XI3C_FIFO_RESET_DELAY_US		10
/*
 * Poll/sleep slice for FIFO and response waits: small enough to stay
 * responsive, avoids busy-waiting
 */
#define XI3C_POLL_INTERVAL_US			10

#define XI3C_I2C_MODE				0
#define XI3C_I2C_TID				0
#define XI3C_SDR_MODE				1
#define XI3C_SDR_TID				1

#define XI3C_WORD_LEN				4

/* Software guard: 500 ms (us, for readl_poll_timeout) to bail out if no response word arrives */
#define XI3C_RESP_TIMEOUT_US			500000
/* Software guard: 1 s (ms, for msecs_to_jiffies) to bail out if a transfer never completes */
#define XI3C_XFER_TIMEOUT_MS			1000

struct xi3c_cmd {
	const void *tx_buf;
	void *rx_buf;
	u16 tx_len;
	u16 rx_len;
	u16 rx_actual;
	u8 addr;
	u8 type;
	u8 tid;
	bool rnw;
	bool is_daa;
	bool continued;
	enum i3c_error_code err;
};

struct xi3c_xfer {
	unsigned int ncmds;
	unsigned int nissued;
	struct xi3c_cmd cmds[] __counted_by(ncmds);
};

/**
 * struct xi3c_master - I3C master controller state.
 * @base: I3C master controller embedded by the framework.
 * @dev: Pointer to the backing device structure.
 * @membase: Memory base of the HW registers.
 * @pclk: Input clock driving the controller.
 * @lock: Serializes transfers and CCC submission.
 * @daa: ENTDAA enumeration state.
 * @daa.addrs: Dynamic addresses assigned in enumeration order.
 * @daa.index: Number of responders enumerated so far.
 */
struct xi3c_master {
	struct i3c_master_controller base;
	struct device *dev;
	void __iomem *membase;
	struct clk *pclk;
	struct mutex lock; /* serializes transfers and CCC submission */
	struct {
		u8 addrs[XI3C_MAX_DEVS];
		u8 index;
	} daa;
};

static inline struct xi3c_master *
to_xi3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct xi3c_master, base);
}

static inline u8 xi3c_get_revision_number(struct xi3c_master *master)
{
	return FIELD_GET(XI3C_REV_NUM_MASK,
			 ioread32(master->membase + XI3C_VERSION_OFFSET));
}

static inline u16 xi3c_wr_fifo_level(struct xi3c_master *master)
{
	return ioread32(master->membase + XI3C_FIFO_LVL_STATUS_OFFSET) &
	       XI3C_FIFO_LEVEL_MASK;
}

static inline u16 xi3c_rd_fifo_level(struct xi3c_master *master)
{
	return ioread32(master->membase + XI3C_FIFO_LVL_STATUS_1_OFFSET) &
	       XI3C_FIFO_LEVEL_MASK;
}

static inline bool xi3c_is_resp_available(struct xi3c_master *master)
{
	return FIELD_GET(XI3C_SR_RESP_NOT_EMPTY_MASK,
			 ioread32(master->membase + XI3C_SR_OFFSET));
}

static int xi3c_get_response(struct xi3c_master *master, struct xi3c_cmd *cmd)
{
	u32 response_data;
	u32 resp_reg;
	u8 code;
	int ret;

	ret = readl_poll_timeout(master->membase + XI3C_SR_OFFSET,
				 resp_reg,
				 resp_reg & XI3C_SR_RESP_NOT_EMPTY_MASK,
				 XI3C_POLL_INTERVAL_US, XI3C_RESP_TIMEOUT_US);
	if (ret) {
		dev_err(master->dev, "XI3C response timeout\n");
		return ret;
	}

	response_data = ioread32(master->membase + XI3C_RESP_STATUS_FIFO_OFFSET);
	code = FIELD_GET(XI3C_RESP_CODE_MASK, response_data);

	switch (code) {
	case XI3C_RESP_CODE_SUCCESS:
		cmd->err = I3C_ERROR_UNKNOWN;
		cmd->rx_actual = FIELD_GET(XI3C_RESP_BYTES_MASK, response_data);
		return 0;
	case XI3C_RESP_CODE_READ_EARLY_TERM:
		/* Short read: valid data, but controller parked in STOP and must be resumed */
		cmd->err = I3C_ERROR_UNKNOWN;
		cmd->rx_actual = FIELD_GET(XI3C_RESP_BYTES_MASK, response_data);
		return XI3C_XFER_SHORT_READ;
	case XI3C_RESP_CODE_NO_TARGET:
	case XI3C_RESP_CODE_NACK:
		cmd->err = I3C_ERROR_M2;
		return cmd->is_daa ? -ENODEV : -EIO;
	default:
		cmd->err = I3C_ERROR_M0;
		dev_err(master->dev, "XI3C transfer error, response code %u\n",
			code);
		return -EIO;
	}
}

static inline void xi3c_writesl_be(void __iomem *addr, const void *buffer,
				   unsigned int count)
{
	const u32 *buf = buffer;

	while (count--)
		iowrite32be(get_unaligned(buf++), addr);
}

static inline void xi3c_readsl_be(const void __iomem *addr, void *buffer,
				  unsigned int count)
{
	u32 *buf = buffer;

	while (count--)
		put_unaligned(ioread32be(addr), buf++);
}

static inline void xi3c_writel_fifo(void __iomem *addr, const void *buf,
				    int nbytes)
{
	xi3c_writesl_be(addr, buf, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, (const u8 *)buf + (nbytes & ~3), nbytes & 3);
		xi3c_writesl_be(addr, &tmp, 1);
	}
}

static inline void xi3c_readl_fifo(const void __iomem *addr, void *buf,
				   int nbytes)
{
	xi3c_readsl_be(addr, buf, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		xi3c_readsl_be(addr, &tmp, 1);
		memcpy((u8 *)buf + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

static void xi3c_master_write_to_cmdfifo(struct xi3c_master *master,
					 struct xi3c_cmd *cmd, u16 len)
{
	u32 transfer_cmd;
	u8 addr;

	addr = ((cmd->addr & XI3C_ADDR_MASK) << 1) | (u8)cmd->rnw;

	transfer_cmd  = FIELD_PREP(XI3C_CMD_TYPE, cmd->type);
	transfer_cmd |= FIELD_PREP(XI3C_CMD_TERMINATE, !cmd->continued);
	transfer_cmd |= FIELD_PREP(XI3C_CMD_ADDR, addr);
	transfer_cmd |= FIELD_PREP(XI3C_CMD_TID, cmd->tid);

	/*
	 * For dynamic addressing, an additional 1-byte length must be added
	 * to the command FIFO to account for the address present in the TX FIFO
	 */
	if (cmd->is_daa) {
		xi3c_writel_fifo(master->membase + XI3C_WR_FIFO_OFFSET,
				 cmd->tx_buf, cmd->tx_len);

		len++;
	}

	transfer_cmd |= FIELD_PREP(XI3C_CMD_LEN, len);
	iowrite32(transfer_cmd, master->membase + XI3C_CMD_FIFO_OFFSET);
}

static inline void xi3c_master_enable(struct xi3c_master *master)
{
	iowrite32(ioread32(master->membase + XI3C_CR_OFFSET) | XI3C_CR_EN_MASK,
		  master->membase + XI3C_CR_OFFSET);
}

static inline void xi3c_master_disable(struct xi3c_master *master)
{
	iowrite32(ioread32(master->membase + XI3C_CR_OFFSET) & ~XI3C_CR_EN_MASK,
		  master->membase + XI3C_CR_OFFSET);
}

static inline void xi3c_master_resume(struct xi3c_master *master)
{
	iowrite32(ioread32(master->membase + XI3C_CR_OFFSET) |
		  XI3C_CR_RESUME_MASK, master->membase + XI3C_CR_OFFSET);
}

static void xi3c_master_reset_fifos(struct xi3c_master *master)
{
	u32 data;

	/* Assert FIFO reset. */
	data = ioread32(master->membase + XI3C_RESET_OFFSET);
	data |= XI3C_FIFOS_RST_MASK;
	iowrite32(data, master->membase + XI3C_RESET_OFFSET);
	/* Read-back flushes the posted write before the settling delay below. */
	ioread32(master->membase + XI3C_RESET_OFFSET);
	fsleep(XI3C_FIFO_RESET_DELAY_US);

	/* De-assert FIFO reset, then wait for the FIFOs to come back up. */
	data &= ~XI3C_FIFOS_RST_MASK;
	iowrite32(data, master->membase + XI3C_RESET_OFFSET);
	ioread32(master->membase + XI3C_RESET_OFFSET);
	fsleep(XI3C_FIFO_RESET_DELAY_US);
}

static inline void xi3c_master_init(struct xi3c_master *master)
{
	/* Reset fifos */
	xi3c_master_reset_fifos(master);

	/* Enable controller */
	xi3c_master_enable(master);
}

static inline void xi3c_master_reinit(struct xi3c_master *master)
{
	/* Reset fifos */
	xi3c_master_reset_fifos(master);

	/* Resume controller */
	xi3c_master_resume(master);
}

static struct xi3c_xfer *xi3c_master_alloc_xfer(unsigned int ncmds)
{
	struct xi3c_xfer *xfer;

	xfer = kzalloc_flex(*xfer, cmds, ncmds, GFP_KERNEL);
	if (!xfer)
		return NULL;

	xfer->ncmds = ncmds;

	return xfer;
}

static void xi3c_master_rd_from_rx_fifo(struct xi3c_master *master,
					struct xi3c_cmd *cmd)
{
	u16 rx_data_available;
	u16 copy_len;
	u16 len;

	rx_data_available = xi3c_rd_fifo_level(master);
	len = rx_data_available * XI3C_WORD_LEN;

	if (!len)
		return;

	copy_len = min(len, cmd->rx_len);
	xi3c_readl_fifo(master->membase + XI3C_RD_FIFO_OFFSET,
			(u8 *)cmd->rx_buf, copy_len);

	cmd->rx_buf = (u8 *)cmd->rx_buf + copy_len;
	cmd->rx_len -= copy_len;
}

static int xi3c_master_read(struct xi3c_master *master, struct xi3c_cmd *cmd)
{
	unsigned long timeout;
	u32 status_reg;
	int ret;

	if (cmd->rx_len > XI3C_MAXDATA_LENGTH)
		return -EINVAL;
	/*
	 * Zero-length probes (e.g. i2cdetect) legitimately pass a NULL
	 * buffer; only a non-zero length requires one.
	 */
	if (cmd->rx_len && !cmd->rx_buf)
		return -EINVAL;

	/* Fill command fifo */
	xi3c_master_write_to_cmdfifo(master, cmd, cmd->rx_len);

	if (!cmd->rx_len)
		return 0;

	ret = readl_poll_timeout(master->membase + XI3C_SR_OFFSET,
				 status_reg,
				 status_reg & (XI3C_RD_FIFO_NOT_EMPTY_MASK |
					       XI3C_SR_RESP_NOT_EMPTY_MASK),
				 XI3C_POLL_INTERVAL_US, XI3C_RESP_TIMEOUT_US);
	if (ret) {
		dev_err(master->dev, "XI3C read timeout\n");
		return ret;
	}

	if (!(status_reg & XI3C_RD_FIFO_NOT_EMPTY_MASK))
		return 0;

	timeout = jiffies + msecs_to_jiffies(XI3C_XFER_TIMEOUT_MS);

	/* Read data from rx fifo */
	while (cmd->rx_len > 0 && !xi3c_is_resp_available(master)) {
		if (time_after(jiffies, timeout)) {
			dev_err(master->dev, "XI3C read timeout\n");
			return -EIO;
		}
		xi3c_master_rd_from_rx_fifo(master, cmd);
		usleep_range(XI3C_POLL_INTERVAL_US, 2 * XI3C_POLL_INTERVAL_US);
	}

	/* Read remaining data */
	xi3c_master_rd_from_rx_fifo(master, cmd);

	return 0;
}

static void xi3c_master_wr_to_tx_fifo(struct xi3c_master *master,
				      struct xi3c_cmd *cmd)
{
	u16 wrfifo_space;
	u16 len;

	wrfifo_space = xi3c_wr_fifo_level(master);
	if (cmd->tx_len > wrfifo_space * XI3C_WORD_LEN)
		len = wrfifo_space * XI3C_WORD_LEN;
	else
		len = cmd->tx_len;

	if (len) {
		xi3c_writel_fifo(master->membase + XI3C_WR_FIFO_OFFSET, cmd->tx_buf,
				 len);

		cmd->tx_buf = (const u8 *)cmd->tx_buf + len;
		cmd->tx_len -= len;
	}
}

static int xi3c_master_write(struct xi3c_master *master, struct xi3c_cmd *cmd)
{
	unsigned long timeout;
	u16 cmd_len;

	if (cmd->tx_len > XI3C_MAXDATA_LENGTH)
		return -EINVAL;
	/*
	 * Zero-length probes (e.g. i2cdetect) legitimately pass a NULL
	 * buffer; only a non-zero length requires one.
	 */
	if (cmd->tx_len && !cmd->tx_buf)
		return -EINVAL;

	cmd_len = cmd->tx_len;

	/* Fill Tx fifo */
	xi3c_master_wr_to_tx_fifo(master, cmd);

	/* Write to command fifo */
	xi3c_master_write_to_cmdfifo(master, cmd, cmd_len);

	timeout = jiffies + msecs_to_jiffies(XI3C_XFER_TIMEOUT_MS);
	/* Fill if any remaining data to tx fifo */
	while (cmd->tx_len > 0 && !xi3c_is_resp_available(master)) {
		if (time_after(jiffies, timeout)) {
			dev_err(master->dev, "XI3C write timeout\n");
			return -EIO;
		}

		xi3c_master_wr_to_tx_fifo(master, cmd);
		usleep_range(XI3C_POLL_INTERVAL_US, 2 * XI3C_POLL_INTERVAL_US);
	}

	return 0;
}

static int xi3c_master_xfer(struct xi3c_master *master, struct xi3c_cmd *cmd)
{
	int ret;

	if (cmd->rnw)
		ret = xi3c_master_read(master, cmd);
	else
		ret = xi3c_master_write(master, cmd);

	if (ret)
		goto err_xfer_out;

	ret = xi3c_get_response(master, cmd);
	if (ret < 0)
		goto err_xfer_out;

	/* Short read leaves the controller parked in STOP; resume it for the next command */
	if (ret == XI3C_XFER_SHORT_READ)
		xi3c_master_resume(master);

	return 0;

err_xfer_out:
	xi3c_master_reinit(master);
	return ret;
}

static int xi3c_master_common_xfer(struct xi3c_master *master,
				   struct xi3c_xfer *xfer)
{
	unsigned int i;
	int ret;

	guard(mutex)(&master->lock);

	for (i = 0; i < xfer->ncmds; i++) {
		ret = xi3c_master_xfer(master, &xfer->cmds[i]);
		if (ret) {
			/* Count commands sent on the bus; the rest never ran */
			xfer->nissued = i + 1;
			return ret;
		}
	}

	xfer->nissued = xfer->ncmds;

	return 0;
}

static int xi3c_master_do_daa(struct i3c_master_controller *m)
{
	u8 (*pid_bufs)[XI3C_DAA_SLAVEINFO_READ_BYTECOUNT];
	struct xi3c_master *master = to_xi3c_master(m);
	struct xi3c_cmd *daa_cmd;
	struct xi3c_xfer *xfer;
	int addr, ret, i;
	u8 last_addr = 0;
	u8 *pid_buf;
	u8 ccc_id;

	xfer = xi3c_master_alloc_xfer(1);
	if (!xfer)
		return -ENOMEM;

	pid_bufs = kcalloc(XI3C_MAX_DEVS, XI3C_DAA_SLAVEINFO_READ_BYTECOUNT,
			   GFP_KERNEL);
	if (!pid_bufs) {
		ret = -ENOMEM;
		goto out;
	}

	/* Fill ENTDAA CCC */
	ccc_id = I3C_CCC_ENTDAA;
	daa_cmd = &xfer->cmds[0];
	daa_cmd->addr = I3C_BROADCAST_ADDR;
	daa_cmd->rnw = false;
	daa_cmd->tx_buf = &ccc_id;
	daa_cmd->tx_len = 1;
	daa_cmd->type = XI3C_SDR_MODE;
	daa_cmd->tid = XI3C_SDR_TID;
	daa_cmd->continued = true;

	ret = xi3c_master_common_xfer(master, xfer);
	if (ret) {
		/* NACK on ENTDAA broadcast means no devices to enumerate */
		if (daa_cmd->err == I3C_ERROR_M2)
			ret = 0;
		goto err_daa;
	}

	master->daa.index = 0;

	while (true) {
		struct xi3c_cmd *cmd = &xfer->cmds[0];
		u8 daa_byte;

		/* Out of device slots; stop and keep what was enumerated */
		if (master->daa.index >= XI3C_MAX_DEVS) {
			dev_warn(master->dev,
				 "DAA: reached %d devices, stopping enumeration\n",
				 XI3C_MAX_DEVS);
			xi3c_master_reinit(master);
			break;
		}

		addr = i3c_master_get_free_addr(m, last_addr + 1);
		if (addr < 0) {
			dev_warn(master->dev,
				 "DAA: no free dynamic address, stopping enumeration\n");
			xi3c_master_reinit(master);
			break;
		}

		pid_buf = pid_bufs[master->daa.index];

		daa_byte = (addr << 1) | (parity8(addr) ^ 1);

		cmd->tx_buf = &daa_byte;
		cmd->tx_len = 1;
		cmd->addr = I3C_BROADCAST_ADDR;
		cmd->rnw = true;
		cmd->rx_buf = pid_buf;
		cmd->rx_len = XI3C_DAA_SLAVEINFO_READ_BYTECOUNT;
		cmd->is_daa = true;
		cmd->type = XI3C_SDR_MODE;
		cmd->tid = XI3C_SDR_TID;
		cmd->continued = true;

		ret = xi3c_master_common_xfer(master, xfer);

		/* -ENODEV: no more responders, enumeration complete */
		if (ret == -ENODEV) {
			ret = 0;
			break;
		}
		if (ret)
			goto err_daa;

		master->daa.addrs[master->daa.index] = addr;
		last_addr = addr;
		master->daa.index++;
	}

	for (i = 0; i < master->daa.index; i++) {
		u64 pid;

		/* Ignore per-device add errors so one failure doesn't abort the rest */
		i3c_master_add_i3c_dev_locked(m, master->daa.addrs[i]);

		pid = FIELD_GET(XI3C_PID_MASK,
				get_unaligned_be64(pid_bufs[i]));
		dev_dbg(master->dev, "Client %d: PID: 0x%llx\n", i, pid);
	}

	ret = 0;
	goto out;

err_daa:
	xi3c_master_reinit(master);
out:
	kfree(pid_bufs);
	kfree(xfer);
	return ret;
}

static bool
xi3c_master_supports_ccc_cmd(struct i3c_master_controller *master,
			     const struct i3c_ccc_cmd *cmd)
{
	if (cmd->ndests > 1)
		return false;

	switch (cmd->id) {
	case I3C_CCC_ENEC(true):
	case I3C_CCC_ENEC(false):
	case I3C_CCC_DISEC(true):
	case I3C_CCC_DISEC(false):
	case I3C_CCC_ENTAS(0, true):
	case I3C_CCC_ENTAS(0, false):
	case I3C_CCC_RSTDAA(true):
	case I3C_CCC_RSTDAA(false):
	case I3C_CCC_ENTDAA:
	case I3C_CCC_SETMWL(true):
	case I3C_CCC_SETMWL(false):
	case I3C_CCC_SETMRL(true):
	case I3C_CCC_SETMRL(false):
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETMXDS:
		return true;
	default:
		return false;
	}
}

static int xi3c_master_send_bdcast_ccc_cmd(struct xi3c_master *master,
					   struct i3c_ccc_cmd *ccc)
{
	struct xi3c_xfer *xfer __free(kfree) = NULL;
	u8 *buf __free(kfree) = NULL;
	struct xi3c_cmd *cmd;
	u16 xfer_len;
	int ret;

	if (ccc->dests[0].payload.len >= XI3C_MAXDATA_LENGTH)
		return -EINVAL;

	xfer_len = ccc->dests[0].payload.len + 1;

	xfer = xi3c_master_alloc_xfer(1);
	if (!xfer)
		return -ENOMEM;

	buf = kmalloc_objs(*buf, xfer_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = ccc->id;
	memcpy(&buf[1], ccc->dests[0].payload.data, ccc->dests[0].payload.len);

	cmd = &xfer->cmds[0];
	cmd->addr = ccc->dests[0].addr;
	cmd->rnw = ccc->rnw;
	cmd->tx_buf = buf;
	cmd->tx_len = xfer_len;
	cmd->type = XI3C_SDR_MODE;
	cmd->tid = XI3C_SDR_TID;
	cmd->continued = false;

	ret = xi3c_master_common_xfer(master, xfer);
	ccc->err = cmd->err;

	return ret;
}

static int xi3c_master_send_direct_ccc_cmd(struct xi3c_master *master,
					   struct i3c_ccc_cmd *ccc)
{
	struct xi3c_xfer *xfer __free(kfree) = NULL;
	struct xi3c_cmd *cmd;
	int ret;

	if (ccc->dests[0].payload.len > XI3C_MAXDATA_LENGTH)
		return -EINVAL;

	xfer = xi3c_master_alloc_xfer(2);
	if (!xfer)
		return -ENOMEM;

	/* Broadcasted message */
	cmd = &xfer->cmds[0];
	cmd->addr = I3C_BROADCAST_ADDR;
	cmd->rnw = false;
	cmd->tx_buf = &ccc->id;
	cmd->tx_len = 1;
	cmd->type = XI3C_SDR_MODE;
	cmd->tid = XI3C_SDR_TID;
	cmd->continued = true;

	/* Directed message */
	cmd = &xfer->cmds[1];
	cmd->addr = ccc->dests[0].addr;
	cmd->rnw = ccc->rnw;
	if (cmd->rnw) {
		cmd->rx_buf = ccc->dests[0].payload.data;
		cmd->rx_len = ccc->dests[0].payload.len;
	} else {
		cmd->tx_buf = ccc->dests[0].payload.data;
		cmd->tx_len = ccc->dests[0].payload.len;
	}
	cmd->type = XI3C_SDR_MODE;
	cmd->tid = XI3C_SDR_TID;
	cmd->continued = false;

	ret = xi3c_master_common_xfer(master, xfer);

	/*
	 * Report broadcast error if any, else the directed one, so either
	 * NACK reaches the caller
	 */
	ccc->err = xfer->cmds[0].err ? xfer->cmds[0].err : xfer->cmds[1].err;

	/* Report actual byte count so the core sees the right length on short reads */
	if (!ret && ccc->rnw)
		ccc->dests[0].payload.len = min(xfer->cmds[1].rx_actual,
						ccc->dests[0].payload.len);

	return ret;
}

static int xi3c_master_send_ccc_cmd(struct i3c_master_controller *m,
				    struct i3c_ccc_cmd *cmd)
{
	struct xi3c_master *master = to_xi3c_master(m);

	if (cmd->id & I3C_CCC_DIRECT)
		return xi3c_master_send_direct_ccc_cmd(master, cmd);

	return xi3c_master_send_bdcast_ccc_cmd(master, cmd);
}

static int xi3c_master_i3c_xfers(struct i3c_dev_desc *dev,
				 struct i3c_xfer *xfers,
				 int nxfers, enum i3c_xfer_mode mode)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct xi3c_master *master = to_xi3c_master(m);
	struct xi3c_xfer *xfer __free(kfree) = NULL;
	int i, ret;

	if (!nxfers)
		return 0;

	if (mode != I3C_SDR)
		return -EOPNOTSUPP;

	for (i = 0; i < nxfers; i++)
		if (xfers[i].len > XI3C_MAXDATA_LENGTH)
			return -EINVAL;

	xfer = xi3c_master_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		struct xi3c_cmd *cmd = &xfer->cmds[i];

		cmd->addr = dev->info.dyn_addr;
		cmd->rnw = xfers[i].rnw;

		if (cmd->rnw) {
			cmd->rx_buf = xfers[i].data.in;
			cmd->rx_len = xfers[i].len;
		} else {
			cmd->tx_buf = xfers[i].data.out;
			cmd->tx_len = xfers[i].len;
		}

		cmd->type = XI3C_SDR_MODE;
		cmd->tid = XI3C_SDR_TID;
		cmd->continued = (i + 1) < nxfers;
	}

	ret = xi3c_master_common_xfer(master, xfer);

	for (i = 0; i < xfer->nissued; i++) {
		xfers[i].err = xfer->cmds[i].err;
		if (xfers[i].rnw)
			xfers[i].actual_len = min(xfer->cmds[i].rx_actual,
						  xfers[i].len);
	}

	return ret;
}

static int xi3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				 struct i2c_msg *xfers,
				 int nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct xi3c_master *master = to_xi3c_master(m);
	struct xi3c_xfer *xfer __free(kfree) = NULL;
	int i;

	if (!nxfers)
		return 0;

	for (i = 0; i < nxfers; i++)
		if (xfers[i].len > XI3C_MAXDATA_LENGTH)
			return -EINVAL;

	xfer = xi3c_master_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		struct xi3c_cmd *cmd = &xfer->cmds[i];

		cmd->addr = xfers[i].addr & XI3C_ADDR_MASK;
		cmd->rnw = !!(xfers[i].flags & I2C_M_RD);

		if (cmd->rnw) {
			cmd->rx_buf = xfers[i].buf;
			cmd->rx_len = xfers[i].len;
		} else {
			cmd->tx_buf = xfers[i].buf;
			cmd->tx_len = xfers[i].len;
		}

		cmd->type = XI3C_I2C_MODE;
		cmd->tid = XI3C_I2C_TID;
		cmd->continued = (i + 1) < nxfers;
	}

	return xi3c_master_common_xfer(master, xfer);
}

static int xi3c_clk_cfg(struct xi3c_master *master, unsigned long sclhz, u8 mode)
{
	unsigned long core_rate, core_periodns;
	u32 tcasmin, tsustart, tsustop, thdstart;
	u32 thigh, tlow, thold;
	u32 odthigh, odtlow;

	core_rate = clk_get_rate(master->pclk);
	if (!core_rate)
		return -EINVAL;

	if (!sclhz)
		return -EINVAL;

	core_periodns = DIV_ROUND_UP(NSEC_PER_SEC, core_rate);

	thigh = DIV_ROUND_UP(core_rate, sclhz) >> 1;
	tlow = thigh;

	/* Reject rates whose timing exceeds the 18-bit registers (would wrap) */
	if (thigh <= XI3C_CYCLE_ADJUST ||
	    (thigh - XI3C_CYCLE_ADJUST) > XI3C_TIMING_MASK)
		return -EINVAL;

	/* Hold time : 40% of tlow time */
	thold = (tlow * 4) / 10;

	if (xi3c_get_revision_number(master) == 0)
		thold = max_t(u32, thold, XI3C_THOLD_MIN_REV0);
	else
		thold = max_t(u32, thold, XI3C_THOLD_MIN_REV1);

	iowrite32((thigh - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
		  master->membase + XI3C_SCL_HIGH_TIME_OFFSET);
	iowrite32((tlow - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
		  master->membase + XI3C_SCL_LOW_TIME_OFFSET);
	iowrite32((thold - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
		  master->membase + XI3C_SDA_HOLD_TIME_OFFSET);

	if (mode == XI3C_I2C_MODE) {
		iowrite32((thigh - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
			  master->membase + XI3C_OD_SCL_HIGH_TIME_OFFSET);
		iowrite32((tlow - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
			  master->membase + XI3C_OD_SCL_LOW_TIME_OFFSET);

		tcasmin = DIV_ROUND_UP(XI3C_I2C_TCASMIN_NS, core_periodns);
	} else {
		odtlow = DIV_ROUND_UP(XI3C_OD_TLOW_NS, core_periodns);
		odthigh = DIV_ROUND_UP(XI3C_OD_THIGH_NS, core_periodns);

		odtlow = max(tlow, odtlow);
		odthigh = min(thigh, odthigh);

		if (odthigh <= XI3C_CYCLE_ADJUST)
			return -EINVAL;

		iowrite32((odthigh - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
			  master->membase + XI3C_OD_SCL_HIGH_TIME_OFFSET);
		iowrite32((odtlow - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
			  master->membase + XI3C_OD_SCL_LOW_TIME_OFFSET);

		tcasmin = DIV_ROUND_UP(XI3C_TCASMIN_NS, core_periodns);
	}

	thdstart = max(thigh, tcasmin);
	tsustart = max(tlow, tcasmin);
	tsustop = max(tlow, tcasmin);

	iowrite32((tsustart - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
		  master->membase + XI3C_TSU_START_OFFSET);
	iowrite32((thdstart - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
		  master->membase + XI3C_THD_START_OFFSET);
	iowrite32((tsustop - XI3C_CYCLE_ADJUST) & XI3C_TIMING_MASK,
		  master->membase + XI3C_TSU_STOP_OFFSET);

	return 0;
}

static int xi3c_master_bus_init(struct i3c_master_controller *m)
{
	struct xi3c_master *master = to_xi3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = {};
	unsigned long sclhz;
	u32 pid1_bcr_dcr;
	u8 mode;
	int ret;

	switch (bus->mode) {
	case I3C_BUS_MODE_MIXED_FAST:
	case I3C_BUS_MODE_MIXED_LIMITED:
	case I3C_BUS_MODE_MIXED_SLOW:
		mode = XI3C_I2C_MODE;
		sclhz = bus->scl_rate.i2c;
		break;
	case I3C_BUS_MODE_PURE:
		mode = XI3C_SDR_MODE;
		sclhz = bus->scl_rate.i3c;
		break;
	default:
		return -EINVAL;
	}

	ret = xi3c_clk_cfg(master, sclhz, mode);
	if (ret)
		return ret;

	xi3c_master_init(master);

	/* Get an address for the master. */
	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	info.dyn_addr = ret;

	/* Write the dynamic address value to the address register. */
	iowrite32(info.dyn_addr, master->membase + XI3C_ADDRESS_OFFSET);

	/* Read PID, BCR and DCR values, and assign to i3c device info. */
	pid1_bcr_dcr = ioread32(master->membase + XI3C_PID1_BCR_DCR);
	info.pid = ((u64)FIELD_GET(XI3C_PID1_MASK, pid1_bcr_dcr) << 32) |
		   ioread32(master->membase + XI3C_PID0_OFFSET);
	info.bcr = FIELD_GET(XI3C_BCR_MASK, pid1_bcr_dcr);
	info.dcr = FIELD_GET(XI3C_DCR_MASK, pid1_bcr_dcr);

	return i3c_master_set_info(&master->base, &info);
}

static void xi3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct xi3c_master *master = to_xi3c_master(m);

	xi3c_master_disable(master);
}

static const struct i3c_master_controller_ops xi3c_master_ops = {
	.bus_init = xi3c_master_bus_init,
	.bus_cleanup = xi3c_master_bus_cleanup,
	.do_daa = xi3c_master_do_daa,
	.supports_ccc_cmd = xi3c_master_supports_ccc_cmd,
	.send_ccc_cmd = xi3c_master_send_ccc_cmd,
	.i3c_xfers = xi3c_master_i3c_xfers,
	.i2c_xfers = xi3c_master_i2c_xfers,
};

static int xi3c_master_probe(struct platform_device *pdev)
{
	struct xi3c_master *master;
	int ret;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->dev = &pdev->dev;

	master->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(master->membase))
		return dev_err_probe(master->dev, PTR_ERR(master->membase),
				     "Failed to map registers\n");

	master->pclk = devm_clk_get_enabled(master->dev, NULL);
	if (IS_ERR(master->pclk))
		return dev_err_probe(master->dev, PTR_ERR(master->pclk),
				     "Failed to get and enable clock\n");

	ret = devm_mutex_init(master->dev, &master->lock);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, master);

	return i3c_master_register(&master->base, master->dev,
				   &xi3c_master_ops, false);
}

static void xi3c_master_remove(struct platform_device *pdev)
{
	struct xi3c_master *master = platform_get_drvdata(pdev);

	i3c_master_unregister(&master->base);
}

static const struct of_device_id xi3c_master_of_ids[] = {
	{ .compatible = "xlnx,axi-i3c-1.0" },
	{ },
};
MODULE_DEVICE_TABLE(of, xi3c_master_of_ids);

static struct platform_driver xi3c_master_driver = {
	.probe = xi3c_master_probe,
	.remove = xi3c_master_remove,
	.driver = {
		.name = "axi-i3c-master",
		.of_match_table = xi3c_master_of_ids,
	},
};
module_platform_driver(xi3c_master_driver);

MODULE_AUTHOR("Manikanta Guntupalli <manikanta.guntupalli@amd.com>");
MODULE_DESCRIPTION("AMD AXI I3C master driver");
MODULE_LICENSE("GPL");
