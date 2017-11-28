/*
 * Analog Devices ADF7242 Low-Power IEEE 802.15.4 Transceiver
 *
 * Copyright 2009-2017 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 *
 * http://www.analog.com/ADF7242
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/firmware.h>
#include <linux/spi/spi.h>
#include <linux/skbuff.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/ieee802154.h>
#include <net/mac802154.h>
#include <net/cfg802154.h>

#define FIRMWARE "adf7242_firmware.bin"
#define MAX_POLL_LOOPS 200

/* All Registers */

#define REG_EXT_CTRL	0x100	/* RW External LNA/PA and internal PA control */
#define REG_TX_FSK_TEST 0x101	/* RW TX FSK test mode configuration */
#define REG_CCA1	0x105	/* RW RSSI threshold for CCA */
#define REG_CCA2	0x106	/* RW CCA mode configuration */
#define REG_BUFFERCFG	0x107	/* RW RX_BUFFER overwrite control */
#define REG_PKT_CFG	0x108	/* RW FCS evaluation configuration */
#define REG_DELAYCFG0	0x109	/* RW RC_RX command to SFD or sync word delay */
#define REG_DELAYCFG1	0x10A	/* RW RC_TX command to TX state */
#define REG_DELAYCFG2	0x10B	/* RW Mac delay extension */
#define REG_SYNC_WORD0	0x10C	/* RW sync word bits [7:0] of [23:0]  */
#define REG_SYNC_WORD1	0x10D	/* RW sync word bits [15:8] of [23:0]  */
#define REG_SYNC_WORD2	0x10E	/* RW sync word bits [23:16] of [23:0]	*/
#define REG_SYNC_CONFIG	0x10F	/* RW sync word configuration */
#define REG_RC_CFG	0x13E	/* RW RX / TX packet configuration */
#define REG_RC_VAR44	0x13F	/* RW RESERVED */
#define REG_CH_FREQ0	0x300	/* RW Channel Frequency Settings - Low */
#define REG_CH_FREQ1	0x301	/* RW Channel Frequency Settings - Middle */
#define REG_CH_FREQ2	0x302	/* RW Channel Frequency Settings - High */
#define REG_TX_FD	0x304	/* RW TX Frequency Deviation Register */
#define REG_DM_CFG0	0x305	/* RW RX Discriminator BW Register */
#define REG_TX_M	0x306	/* RW TX Mode Register */
#define REG_RX_M	0x307	/* RW RX Mode Register */
#define REG_RRB		0x30C	/* R RSSI Readback Register */
#define REG_LRB		0x30D	/* R Link Quality Readback Register */
#define REG_DR0		0x30E	/* RW bits [15:8] of [15:0] data rate setting */
#define REG_DR1		0x30F	/* RW bits [7:0] of [15:0] data rate setting */
#define REG_PRAMPG	0x313	/* RW RESERVED */
#define REG_TXPB	0x314	/* RW TX Packet Storage Base Address */
#define REG_RXPB	0x315	/* RW RX Packet Storage Base Address */
#define REG_TMR_CFG0	0x316	/* RW Wake up Timer Conf Register - High */
#define REG_TMR_CFG1	0x317	/* RW Wake up Timer Conf Register - Low */
#define REG_TMR_RLD0	0x318	/* RW Wake up Timer Value Register - High */
#define REG_TMR_RLD1	0x319	/* RW Wake up Timer Value Register - Low  */
#define REG_TMR_CTRL	0x31A	/* RW Wake up Timer Timeout flag */
#define REG_PD_AUX	0x31E	/* RW Battmon enable */
#define REG_GP_CFG	0x32C	/* RW GPIO Configuration */
#define REG_GP_OUT	0x32D	/* RW GPIO Configuration */
#define REG_GP_IN	0x32E	/* R GPIO Configuration */
#define REG_SYNT	0x335	/* RW bandwidth calibration timers */
#define REG_CAL_CFG	0x33D	/* RW Calibration Settings */
#define REG_PA_BIAS	0x36E	/* RW PA BIAS */
#define REG_SYNT_CAL	0x371	/* RW Oscillator and Doubler Configuration */
#define REG_IIRF_CFG	0x389	/* RW BB Filter Decimation Rate */
#define REG_CDR_CFG	0x38A	/* RW CDR kVCO */
#define REG_DM_CFG1	0x38B	/* RW Postdemodulator Filter */
#define REG_AGCSTAT	0x38E	/* R RXBB Ref Osc Calibration Engine Readback */
#define REG_RXCAL0	0x395	/* RW RX BB filter tuning, LSB */
#define REG_RXCAL1	0x396	/* RW RX BB filter tuning, MSB */
#define REG_RXFE_CFG	0x39B	/* RW RXBB Ref Osc & RXFE Calibration */
#define REG_PA_RR	0x3A7	/* RW Set PA ramp rate */
#define REG_PA_CFG	0x3A8	/* RW PA enable */
#define REG_EXTPA_CFG	0x3A9	/* RW External PA BIAS DAC */
#define REG_EXTPA_MSC	0x3AA	/* RW PA Bias Mode */
#define REG_ADC_RBK	0x3AE	/* R Readback temp */
#define REG_AGC_CFG1	0x3B2	/* RW GC Parameters */
#define REG_AGC_MAX	0x3B4	/* RW Slew rate	 */
#define REG_AGC_CFG2	0x3B6	/* RW RSSI Parameters */
#define REG_AGC_CFG3	0x3B7	/* RW RSSI Parameters */
#define REG_AGC_CFG4	0x3B8	/* RW RSSI Parameters */
#define REG_AGC_CFG5	0x3B9	/* RW RSSI & NDEC Parameters */
#define REG_AGC_CFG6	0x3BA	/* RW NDEC Parameters */
#define REG_OCL_CFG1	0x3C4	/* RW OCL System Parameters */
#define REG_IRQ1_EN0	0x3C7	/* RW Interrupt Mask set bits for IRQ1 */
#define REG_IRQ1_EN1	0x3C8	/* RW Interrupt Mask set bits for IRQ1 */
#define REG_IRQ2_EN0	0x3C9	/* RW Interrupt Mask set bits for IRQ2 */
#define REG_IRQ2_EN1	0x3CA	/* RW Interrupt Mask set bits for IRQ2 */
#define REG_IRQ1_SRC0	0x3CB	/* RW Interrupt Source bits for IRQ */
#define REG_IRQ1_SRC1	0x3CC	/* RW Interrupt Source bits for IRQ */
#define REG_OCL_BW0	0x3D2	/* RW OCL System Parameters */
#define REG_OCL_BW1	0x3D3	/* RW OCL System Parameters */
#define REG_OCL_BW2	0x3D4	/* RW OCL System Parameters */
#define REG_OCL_BW3	0x3D5	/* RW OCL System Parameters */
#define REG_OCL_BW4	0x3D6	/* RW OCL System Parameters */
#define REG_OCL_BWS	0x3D7	/* RW OCL System Parameters */
#define REG_OCL_CFG13	0x3E0	/* RW OCL System Parameters */
#define REG_GP_DRV	0x3E3	/* RW I/O pads Configuration and bg trim */
#define REG_BM_CFG	0x3E6	/* RW Batt. Monitor Threshold Voltage setting */
#define REG_SFD_15_4	0x3F4	/* RW Option to set non standard SFD */
#define REG_AFC_CFG	0x3F7	/* RW AFC mode and polarity */
#define REG_AFC_KI_KP	0x3F8	/* RW AFC ki and kp */
#define REG_AFC_RANGE	0x3F9	/* RW AFC range */
#define REG_AFC_READ	0x3FA	/* RW Readback frequency error */

/* REG_EXTPA_MSC */
#define PA_PWR(x)		(((x) & 0xF) << 4)
#define EXTPA_BIAS_SRC		BIT(3)
#define EXTPA_BIAS_MODE(x)	(((x) & 0x7) << 0)

/* REG_PA_CFG */
#define PA_BRIDGE_DBIAS(x)	(((x) & 0x1F) << 0)
#define PA_DBIAS_HIGH_POWER	21
#define PA_DBIAS_LOW_POWER	13

/* REG_PA_BIAS */
#define PA_BIAS_CTRL(x)		(((x) & 0x1F) << 1)
#define REG_PA_BIAS_DFL		BIT(0)
#define PA_BIAS_HIGH_POWER	63
#define PA_BIAS_LOW_POWER	55

#define REG_PAN_ID0		0x112
#define REG_PAN_ID1		0x113
#define REG_SHORT_ADDR_0	0x114
#define REG_SHORT_ADDR_1	0x115
#define REG_IEEE_ADDR_0		0x116
#define REG_IEEE_ADDR_1		0x117
#define REG_IEEE_ADDR_2		0x118
#define REG_IEEE_ADDR_3		0x119
#define REG_IEEE_ADDR_4		0x11A
#define REG_IEEE_ADDR_5		0x11B
#define REG_IEEE_ADDR_6		0x11C
#define REG_IEEE_ADDR_7		0x11D
#define REG_FFILT_CFG		0x11E
#define REG_AUTO_CFG		0x11F
#define REG_AUTO_TX1		0x120
#define REG_AUTO_TX2		0x121
#define REG_AUTO_STATUS		0x122

/* REG_FFILT_CFG */
#define ACCEPT_BEACON_FRAMES   BIT(0)
#define ACCEPT_DATA_FRAMES     BIT(1)
#define ACCEPT_ACK_FRAMES      BIT(2)
#define ACCEPT_MACCMD_FRAMES   BIT(3)
#define ACCEPT_RESERVED_FRAMES BIT(4)
#define ACCEPT_ALL_ADDRESS     BIT(5)

/* REG_AUTO_CFG */
#define AUTO_ACK_FRAMEPEND     BIT(0)
#define IS_PANCOORD	       BIT(1)
#define RX_AUTO_ACK_EN	       BIT(3)
#define CSMA_CA_RX_TURNAROUND  BIT(4)

/* REG_AUTO_TX1 */
#define MAX_FRAME_RETRIES(x)   ((x) & 0xF)
#define MAX_CCA_RETRIES(x)     (((x) & 0x7) << 4)

/* REG_AUTO_TX2 */
#define CSMA_MAX_BE(x)	       ((x) & 0xF)
#define CSMA_MIN_BE(x)	       (((x) & 0xF) << 4)

#define CMD_SPI_NOP		0xFF /* No operation. Use for dummy writes */
#define CMD_SPI_PKT_WR		0x10 /* Write telegram to the Packet RAM
				      * starting from the TX packet base address
				      * pointer tx_packet_base
				      */
#define CMD_SPI_PKT_RD		0x30 /* Read telegram from the Packet RAM
				      * starting from RX packet base address
				      * pointer rxpb.rx_packet_base
				      */
#define CMD_SPI_MEM_WR(x)	(0x18 + (x >> 8)) /* Write data to MCR or
						   * Packet RAM sequentially
						   */
#define CMD_SPI_MEM_RD(x)	(0x38 + (x >> 8)) /* Read data from MCR or
						   * Packet RAM sequentially
						   */
#define CMD_SPI_MEMR_WR(x)	(0x08 + (x >> 8)) /* Write data to MCR or Packet
						   * RAM as random block
						   */
#define CMD_SPI_MEMR_RD(x)	(0x28 + (x >> 8)) /* Read data from MCR or
						   * Packet RAM random block
						   */
#define CMD_SPI_PRAM_WR		0x1E /* Write data sequentially to current
				      * PRAM page selected
				      */
#define CMD_SPI_PRAM_RD		0x3E /* Read data sequentially from current
				      * PRAM page selected
				      */
#define CMD_RC_SLEEP		0xB1 /* Invoke transition of radio controller
				      * into SLEEP state
				      */
#define CMD_RC_IDLE		0xB2 /* Invoke transition of radio controller
				      * into IDLE state
				      */
#define CMD_RC_PHY_RDY		0xB3 /* Invoke transition of radio controller
				      * into PHY_RDY state
				      */
#define CMD_RC_RX		0xB4 /* Invoke transition of radio controller
				      * into RX state
				      */
#define CMD_RC_TX		0xB5 /* Invoke transition of radio controller
				      * into TX state
				      */
#define CMD_RC_MEAS		0xB6 /* Invoke transition of radio controller
				      * into MEAS state
				      */
#define CMD_RC_CCA		0xB7 /* Invoke Clear channel assessment */
#define CMD_RC_CSMACA		0xC1 /* initiates CSMA-CA channel access
				      * sequence and frame transmission
				      */
#define CMD_RC_PC_RESET		0xC7 /* Program counter reset */
#define CMD_RC_RESET		0xC8 /* Resets the ADF7242 and puts it in
				      * the sleep state
				      */
#define CMD_RC_PC_RESET_NO_WAIT (CMD_RC_PC_RESET | BIT(31))

/* STATUS */

#define STAT_SPI_READY		BIT(7)
#define STAT_IRQ_STATUS		BIT(6)
#define STAT_RC_READY		BIT(5)
#define STAT_CCA_RESULT		BIT(4)
#define RC_STATUS_IDLE		1
#define RC_STATUS_MEAS		2
#define RC_STATUS_PHY_RDY	3
#define RC_STATUS_RX		4
#define RC_STATUS_TX		5
#define RC_STATUS_MASK		0xF

/* AUTO_STATUS */

#define SUCCESS			0
#define SUCCESS_DATPEND		1
#define FAILURE_CSMACA		2
#define FAILURE_NOACK		3
#define AUTO_STATUS_MASK	0x3

#define PRAM_PAGESIZE		256

/* IRQ1 */

#define IRQ_CCA_COMPLETE	BIT(0)
#define IRQ_SFD_RX		BIT(1)
#define IRQ_SFD_TX		BIT(2)
#define IRQ_RX_PKT_RCVD		BIT(3)
#define IRQ_TX_PKT_SENT		BIT(4)
#define IRQ_FRAME_VALID		BIT(5)
#define IRQ_ADDRESS_VALID	BIT(6)
#define IRQ_CSMA_CA		BIT(7)

#define AUTO_TX_TURNAROUND	BIT(3)
#define ADDON_EN		BIT(4)

#define FLAG_XMIT		0
#define FLAG_START		1

#define ADF7242_REPORT_CSMA_CA_STAT 0 /* framework doesn't handle yet */

struct adf7242_local {
	struct spi_device *spi;
	struct completion tx_complete;
	struct ieee802154_hw *hw;
	struct mutex bmux; /* protect SPI messages */
	struct spi_message stat_msg;
	struct spi_transfer stat_xfer;
	struct dentry *debugfs_root;
	unsigned long flags;
	int tx_stat;
	bool promiscuous;
	s8 rssi;
	u8 max_frame_retries;
	u8 max_cca_retries;
	u8 max_be;
	u8 min_be;

	/* DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */

	u8 buf[3] ____cacheline_aligned;
	u8 buf_reg_tx[3];
	u8 buf_read_tx[4];
	u8 buf_read_rx[4];
	u8 buf_stat_rx;
	u8 buf_stat_tx;
	u8 buf_cmd;
};

static int adf7242_soft_reset(struct adf7242_local *lp, int line);

static int adf7242_status(struct adf7242_local *lp, u8 *stat)
{
	int status;

	mutex_lock(&lp->bmux);
	status = spi_sync(lp->spi, &lp->stat_msg);
	*stat = lp->buf_stat_rx;
	mutex_unlock(&lp->bmux);

	return status;
}

static int adf7242_wait_status(struct adf7242_local *lp, unsigned int status,
			       unsigned int mask, int line)
{
	int cnt = 0, ret = 0;
	u8 stat;

	do {
		adf7242_status(lp, &stat);
		cnt++;
	} while (((stat & mask) != status) && (cnt < MAX_POLL_LOOPS));

	if (cnt >= MAX_POLL_LOOPS) {
		ret = -ETIMEDOUT;

		if (!(stat & STAT_RC_READY)) {
			adf7242_soft_reset(lp, line);
			adf7242_status(lp, &stat);

			if ((stat & mask) == status)
				ret = 0;
		}

		if (ret < 0)
			dev_warn(&lp->spi->dev,
				 "%s:line %d Timeout status 0x%x (%d)\n",
				 __func__, line, stat, cnt);
	}

	dev_vdbg(&lp->spi->dev, "%s : loops=%d line %d\n", __func__, cnt, line);

	return ret;
}

static int adf7242_wait_ready(struct adf7242_local *lp, int line)
{
	return adf7242_wait_status(lp, STAT_RC_READY | STAT_SPI_READY,
				   STAT_RC_READY | STAT_SPI_READY, line);
}

static int adf7242_write_fbuf(struct adf7242_local *lp, u8 *data, u8 len)
{
	u8 *buf = lp->buf;
	int status;
	struct spi_message msg;
	struct spi_transfer xfer_head = {
		.len = 2,
		.tx_buf = buf,

	};
	struct spi_transfer xfer_buf = {
		.len = len,
		.tx_buf = data,
	};

	spi_message_init(&msg);
	spi_message_add_tail(&xfer_head, &msg);
	spi_message_add_tail(&xfer_buf, &msg);

	adf7242_wait_ready(lp, __LINE__);

	mutex_lock(&lp->bmux);
	buf[0] = CMD_SPI_PKT_WR;
	buf[1] = len + 2;

	status = spi_sync(lp->spi, &msg);
	mutex_unlock(&lp->bmux);

	return status;
}

static int adf7242_read_fbuf(struct adf7242_local *lp,
			     u8 *data, size_t len, bool packet_read)
{
	u8 *buf = lp->buf;
	int status;
	struct spi_message msg;
	struct spi_transfer xfer_head = {
		.len = 3,
		.tx_buf = buf,
		.rx_buf = buf,
	};
	struct spi_transfer xfer_buf = {
		.len = len,
		.rx_buf = data,
	};

	spi_message_init(&msg);
	spi_message_add_tail(&xfer_head, &msg);
	spi_message_add_tail(&xfer_buf, &msg);

	adf7242_wait_ready(lp, __LINE__);

	mutex_lock(&lp->bmux);
	if (packet_read) {
		buf[0] = CMD_SPI_PKT_RD;
		buf[1] = CMD_SPI_NOP;
		buf[2] = 0;	/* PHR */
	} else {
		buf[0] = CMD_SPI_PRAM_RD;
		buf[1] = 0;
		buf[2] = CMD_SPI_NOP;
	}

	status = spi_sync(lp->spi, &msg);

	mutex_unlock(&lp->bmux);

	return status;
}

static int adf7242_read_reg(struct adf7242_local *lp, u16 addr, u8 *data)
{
	int status;
	struct spi_message msg;

	struct spi_transfer xfer = {
		.len = 4,
		.tx_buf = lp->buf_read_tx,
		.rx_buf = lp->buf_read_rx,
	};

	adf7242_wait_ready(lp, __LINE__);

	mutex_lock(&lp->bmux);
	lp->buf_read_tx[0] = CMD_SPI_MEM_RD(addr);
	lp->buf_read_tx[1] = addr;
	lp->buf_read_tx[2] = CMD_SPI_NOP;
	lp->buf_read_tx[3] = CMD_SPI_NOP;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	status = spi_sync(lp->spi, &msg);
	if (msg.status)
		status = msg.status;

	if (!status)
		*data = lp->buf_read_rx[3];

	mutex_unlock(&lp->bmux);

	dev_vdbg(&lp->spi->dev, "%s : REG 0x%X, VAL 0x%X\n", __func__,
		 addr, *data);

	return status;
}

static int adf7242_write_reg(struct adf7242_local *lp, u16 addr, u8 data)
{
	int status;

	adf7242_wait_ready(lp, __LINE__);

	mutex_lock(&lp->bmux);
	lp->buf_reg_tx[0] = CMD_SPI_MEM_WR(addr);
	lp->buf_reg_tx[1] = addr;
	lp->buf_reg_tx[2] = data;
	status = spi_write(lp->spi, lp->buf_reg_tx, 3);
	mutex_unlock(&lp->bmux);

	dev_vdbg(&lp->spi->dev, "%s : REG 0x%X, VAL 0x%X\n",
		 __func__, addr, data);

	return status;
}

static int adf7242_cmd(struct adf7242_local *lp, unsigned int cmd)
{
	int status;

	dev_vdbg(&lp->spi->dev, "%s : CMD=0x%X\n", __func__, cmd);

	if (cmd != CMD_RC_PC_RESET_NO_WAIT)
		adf7242_wait_ready(lp, __LINE__);

	mutex_lock(&lp->bmux);
	lp->buf_cmd = cmd;
	status = spi_write(lp->spi, &lp->buf_cmd, 1);
	mutex_unlock(&lp->bmux);

	return status;
}

static int adf7242_upload_firmware(struct adf7242_local *lp, u8 *data, u16 len)
{
	struct spi_message msg;
	struct spi_transfer xfer_buf = { };
	int status, i, page = 0;
	u8 *buf = lp->buf;

	struct spi_transfer xfer_head = {
		.len = 2,
		.tx_buf = buf,
	};

	buf[0] = CMD_SPI_PRAM_WR;
	buf[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer_head, &msg);
	spi_message_add_tail(&xfer_buf, &msg);

	for (i = len; i >= 0; i -= PRAM_PAGESIZE) {
		adf7242_write_reg(lp, REG_PRAMPG, page);

		xfer_buf.len = (i >= PRAM_PAGESIZE) ? PRAM_PAGESIZE : i;
		xfer_buf.tx_buf = &data[page * PRAM_PAGESIZE];

		mutex_lock(&lp->bmux);
		status = spi_sync(lp->spi, &msg);
		mutex_unlock(&lp->bmux);
		page++;
	}

	return status;
}

static int adf7242_verify_firmware(struct adf7242_local *lp,
				   const u8 *data, size_t len)
{
#ifdef DEBUG
	int i, j;
	unsigned int page;
	u8 *buf = kmalloc(PRAM_PAGESIZE, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	for (page = 0, i = len; i >= 0; i -= PRAM_PAGESIZE, page++) {
		size_t nb = (i >= PRAM_PAGESIZE) ? PRAM_PAGESIZE : i;

		adf7242_write_reg(lp, REG_PRAMPG, page);
		adf7242_read_fbuf(lp, buf, nb, false);

		for (j = 0; j < nb; j++) {
			if (buf[j] != data[page * PRAM_PAGESIZE + j]) {
				kfree(buf);
				return -EIO;
			}
		}
	}
	kfree(buf);
#endif
	return 0;
}

static int adf7242_set_txpower(struct ieee802154_hw *hw, int mbm)
{
	struct adf7242_local *lp = hw->priv;
	u8 pwr, bias_ctrl, dbias, tmp;
	int db = mbm / 100;

	dev_vdbg(&lp->spi->dev, "%s : Power %d dB\n", __func__, db);

	if (db > 5 || db < -26)
		return -EINVAL;

	db = DIV_ROUND_CLOSEST(db + 29, 2);

	if (db > 15) {
		dbias = PA_DBIAS_HIGH_POWER;
		bias_ctrl = PA_BIAS_HIGH_POWER;
	} else {
		dbias = PA_DBIAS_LOW_POWER;
		bias_ctrl = PA_BIAS_LOW_POWER;
	}

	pwr = clamp_t(u8, db, 3, 15);

	adf7242_read_reg(lp, REG_PA_CFG, &tmp);
	tmp &= ~PA_BRIDGE_DBIAS(~0);
	tmp |= PA_BRIDGE_DBIAS(dbias);
	adf7242_write_reg(lp, REG_PA_CFG, tmp);

	adf7242_read_reg(lp, REG_PA_BIAS, &tmp);
	tmp &= ~PA_BIAS_CTRL(~0);
	tmp |= PA_BIAS_CTRL(bias_ctrl);
	adf7242_write_reg(lp, REG_PA_BIAS, tmp);

	adf7242_read_reg(lp, REG_EXTPA_MSC, &tmp);
	tmp &= ~PA_PWR(~0);
	tmp |= PA_PWR(pwr);

	return adf7242_write_reg(lp, REG_EXTPA_MSC, tmp);
}

static int adf7242_set_csma_params(struct ieee802154_hw *hw, u8 min_be,
				   u8 max_be, u8 retries)
{
	struct adf7242_local *lp = hw->priv;
	int ret;

	dev_vdbg(&lp->spi->dev, "%s : min_be=%d max_be=%d retries=%d\n",
		 __func__, min_be, max_be, retries);

	if (min_be > max_be || max_be > 8 || retries > 5)
		return -EINVAL;

	ret = adf7242_write_reg(lp, REG_AUTO_TX1,
				MAX_FRAME_RETRIES(lp->max_frame_retries) |
				MAX_CCA_RETRIES(retries));
	if (ret)
		return ret;

	lp->max_cca_retries = retries;
	lp->max_be = max_be;
	lp->min_be = min_be;

	return adf7242_write_reg(lp, REG_AUTO_TX2, CSMA_MAX_BE(max_be) |
			CSMA_MIN_BE(min_be));
}

static int adf7242_set_frame_retries(struct ieee802154_hw *hw, s8 retries)
{
	struct adf7242_local *lp = hw->priv;
	int ret = 0;

	dev_vdbg(&lp->spi->dev, "%s : Retries = %d\n", __func__, retries);

	if (retries < -1 || retries > 15)
		return -EINVAL;

	if (retries >= 0)
		ret = adf7242_write_reg(lp, REG_AUTO_TX1,
					MAX_FRAME_RETRIES(retries) |
					MAX_CCA_RETRIES(lp->max_cca_retries));

	lp->max_frame_retries = retries;

	return ret;
}

static int adf7242_ed(struct ieee802154_hw *hw, u8 *level)
{
	struct adf7242_local *lp = hw->priv;

	*level = lp->rssi;

	dev_vdbg(&lp->spi->dev, "%s :Exit level=%d\n",
		 __func__, *level);

	return 0;
}

static int adf7242_start(struct ieee802154_hw *hw)
{
	struct adf7242_local *lp = hw->priv;

	adf7242_cmd(lp, CMD_RC_PHY_RDY);
	adf7242_write_reg(lp, REG_IRQ1_SRC1, 0xFF);
	enable_irq(lp->spi->irq);
	set_bit(FLAG_START, &lp->flags);

	return adf7242_cmd(lp, CMD_RC_RX);
}

static void adf7242_stop(struct ieee802154_hw *hw)
{
	struct adf7242_local *lp = hw->priv;

	adf7242_cmd(lp, CMD_RC_IDLE);
	clear_bit(FLAG_START, &lp->flags);
	disable_irq(lp->spi->irq);
	adf7242_write_reg(lp, REG_IRQ1_SRC1, 0xFF);
}

static int adf7242_channel(struct ieee802154_hw *hw, u8 page, u8 channel)
{
	struct adf7242_local *lp = hw->priv;
	unsigned long freq;

	dev_dbg(&lp->spi->dev, "%s :Channel=%d\n", __func__, channel);

	might_sleep();

	WARN_ON(page != 0);
	WARN_ON(channel < 11);
	WARN_ON(channel > 26);

	freq = (2405 + 5 * (channel - 11)) * 100;
	adf7242_cmd(lp, CMD_RC_PHY_RDY);

	adf7242_write_reg(lp, REG_CH_FREQ0, freq);
	adf7242_write_reg(lp, REG_CH_FREQ1, freq >> 8);
	adf7242_write_reg(lp, REG_CH_FREQ2, freq >> 16);

	return adf7242_cmd(lp, CMD_RC_RX);
}

static int adf7242_set_hw_addr_filt(struct ieee802154_hw *hw,
				    struct ieee802154_hw_addr_filt *filt,
				    unsigned long changed)
{
	struct adf7242_local *lp = hw->priv;
	u8 reg;

	dev_dbg(&lp->spi->dev, "%s :Changed=0x%lX\n", __func__, changed);

	might_sleep();

	if (changed & IEEE802154_AFILT_IEEEADDR_CHANGED) {
		u8 addr[8], i;

		memcpy(addr, &filt->ieee_addr, 8);

		for (i = 0; i < 8; i++)
			adf7242_write_reg(lp, REG_IEEE_ADDR_0 + i, addr[i]);
	}

	if (changed & IEEE802154_AFILT_SADDR_CHANGED) {
		u16 saddr = le16_to_cpu(filt->short_addr);

		adf7242_write_reg(lp, REG_SHORT_ADDR_0, saddr);
		adf7242_write_reg(lp, REG_SHORT_ADDR_1, saddr >> 8);
	}

	if (changed & IEEE802154_AFILT_PANID_CHANGED) {
		u16 pan_id = le16_to_cpu(filt->pan_id);

		adf7242_write_reg(lp, REG_PAN_ID0, pan_id);
		adf7242_write_reg(lp, REG_PAN_ID1, pan_id >> 8);
	}

	if (changed & IEEE802154_AFILT_PANC_CHANGED) {
		adf7242_read_reg(lp, REG_AUTO_CFG, &reg);
		if (filt->pan_coord)
			reg |= IS_PANCOORD;
		else
			reg &= ~IS_PANCOORD;
		adf7242_write_reg(lp, REG_AUTO_CFG, reg);
	}

	return 0;
}

static int adf7242_set_promiscuous_mode(struct ieee802154_hw *hw, bool on)
{
	struct adf7242_local *lp = hw->priv;

	dev_dbg(&lp->spi->dev, "%s : mode %d\n", __func__, on);

	lp->promiscuous = on;

	if (on) {
		adf7242_write_reg(lp, REG_AUTO_CFG, 0);
		return adf7242_write_reg(lp, REG_FFILT_CFG,
				  ACCEPT_BEACON_FRAMES |
				  ACCEPT_DATA_FRAMES |
				  ACCEPT_MACCMD_FRAMES |
				  ACCEPT_ALL_ADDRESS |
				  ACCEPT_ACK_FRAMES |
				  ACCEPT_RESERVED_FRAMES);
	} else {
		adf7242_write_reg(lp, REG_FFILT_CFG,
				  ACCEPT_BEACON_FRAMES |
				  ACCEPT_DATA_FRAMES |
				  ACCEPT_MACCMD_FRAMES |
				  ACCEPT_RESERVED_FRAMES);

		return adf7242_write_reg(lp, REG_AUTO_CFG, RX_AUTO_ACK_EN);
	}
}

static int adf7242_set_cca_ed_level(struct ieee802154_hw *hw, s32 mbm)
{
	struct adf7242_local *lp = hw->priv;
	s8 level = clamp_t(s8, mbm / 100, S8_MIN, S8_MAX);

	dev_dbg(&lp->spi->dev, "%s : level %d\n", __func__, level);

	return adf7242_write_reg(lp, REG_CCA1, level);
}

static int adf7242_xmit(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	struct adf7242_local *lp = hw->priv;
	int ret;

	set_bit(FLAG_XMIT, &lp->flags);
	reinit_completion(&lp->tx_complete);
	adf7242_cmd(lp, CMD_RC_PHY_RDY);

	ret = adf7242_write_fbuf(lp, skb->data, skb->len);
	if (ret)
		goto err;

	ret = adf7242_cmd(lp, CMD_RC_CSMACA);
	if (ret)
		goto err;

	ret = wait_for_completion_interruptible_timeout(&lp->tx_complete,
							HZ / 10);
	if (ret < 0)
		goto err;
	if (ret == 0) {
		dev_dbg(&lp->spi->dev, "Timeout waiting for TX interrupt\n");
		ret = -ETIMEDOUT;
		goto err;
	}

	if (lp->tx_stat != SUCCESS) {
		dev_dbg(&lp->spi->dev,
			"Error xmit: Retry count exceeded Status=0x%x\n",
			lp->tx_stat);
		ret = -ECOMM;
	} else {
		ret = 0;
	}

err:
	clear_bit(FLAG_XMIT, &lp->flags);
	adf7242_cmd(lp, CMD_RC_RX);

	return ret;
}

static int adf7242_rx(struct adf7242_local *lp)
{
	struct sk_buff *skb;
	size_t len;
	int ret;
	u8 lqi, len_u8, *data;

	adf7242_read_reg(lp, 0, &len_u8);

	len = len_u8;

	if (!ieee802154_is_valid_psdu_len(len)) {
		dev_dbg(&lp->spi->dev,
			"corrupted frame received len %d\n", (int)len);
		len = IEEE802154_MTU;
	}

	skb = dev_alloc_skb(len);
	if (!skb) {
		adf7242_cmd(lp, CMD_RC_RX);
		return -ENOMEM;
	}

	data = skb_put(skb, len);
	ret = adf7242_read_fbuf(lp, data, len, true);
	if (ret < 0) {
		kfree_skb(skb);
		adf7242_cmd(lp, CMD_RC_RX);
		return ret;
	}

	lqi = data[len - 2];
	lp->rssi = data[len - 1];

	adf7242_cmd(lp, CMD_RC_RX);

	skb_trim(skb, len - 2);	/* Don't put RSSI/LQI or CRC into the frame */

	ieee802154_rx_irqsafe(lp->hw, skb, lqi);

	dev_dbg(&lp->spi->dev, "%s: ret=%d len=%d lqi=%d rssi=%d\n",
		__func__, ret, (int)len, (int)lqi, lp->rssi);

	return 0;
}

static const struct ieee802154_ops adf7242_ops = {
	.owner = THIS_MODULE,
	.xmit_sync = adf7242_xmit,
	.ed = adf7242_ed,
	.set_channel = adf7242_channel,
	.set_hw_addr_filt = adf7242_set_hw_addr_filt,
	.start = adf7242_start,
	.stop = adf7242_stop,
	.set_csma_params = adf7242_set_csma_params,
	.set_frame_retries = adf7242_set_frame_retries,
	.set_txpower = adf7242_set_txpower,
	.set_promiscuous_mode = adf7242_set_promiscuous_mode,
	.set_cca_ed_level = adf7242_set_cca_ed_level,
};

static void adf7242_debug(struct adf7242_local *lp, u8 irq1)
{
#ifdef DEBUG
	u8 stat;

	adf7242_status(lp, &stat);

	dev_dbg(&lp->spi->dev, "%s IRQ1 = %X:\n%s%s%s%s%s%s%s%s\n",
		__func__, irq1,
		irq1 & IRQ_CCA_COMPLETE ? "IRQ_CCA_COMPLETE\n" : "",
		irq1 & IRQ_SFD_RX ? "IRQ_SFD_RX\n" : "",
		irq1 & IRQ_SFD_TX ? "IRQ_SFD_TX\n" : "",
		irq1 & IRQ_RX_PKT_RCVD ? "IRQ_RX_PKT_RCVD\n" : "",
		irq1 & IRQ_TX_PKT_SENT ? "IRQ_TX_PKT_SENT\n" : "",
		irq1 & IRQ_CSMA_CA ? "IRQ_CSMA_CA\n" : "",
		irq1 & IRQ_FRAME_VALID ? "IRQ_FRAME_VALID\n" : "",
		irq1 & IRQ_ADDRESS_VALID ? "IRQ_ADDRESS_VALID\n" : "");

	dev_dbg(&lp->spi->dev, "%s STATUS = %X:\n%s\n%s%s%s%s%s\n",
		__func__, stat,
		stat & STAT_RC_READY ? "RC_READY" : "RC_BUSY",
		(stat & 0xf) == RC_STATUS_IDLE ? "RC_STATUS_IDLE" : "",
		(stat & 0xf) == RC_STATUS_MEAS ? "RC_STATUS_MEAS" : "",
		(stat & 0xf) == RC_STATUS_PHY_RDY ? "RC_STATUS_PHY_RDY" : "",
		(stat & 0xf) == RC_STATUS_RX ? "RC_STATUS_RX" : "",
		(stat & 0xf) == RC_STATUS_TX ? "RC_STATUS_TX" : "");
#endif
}

static irqreturn_t adf7242_isr(int irq, void *data)
{
	struct adf7242_local *lp = data;
	unsigned int xmit;
	u8 irq1;

	adf7242_wait_status(lp, RC_STATUS_PHY_RDY, RC_STATUS_MASK, __LINE__);

	adf7242_read_reg(lp, REG_IRQ1_SRC1, &irq1);
	adf7242_write_reg(lp, REG_IRQ1_SRC1, irq1);

	if (!(irq1 & (IRQ_RX_PKT_RCVD | IRQ_CSMA_CA)))
		dev_err(&lp->spi->dev, "%s :ERROR IRQ1 = 0x%X\n",
			__func__, irq1);

	adf7242_debug(lp, irq1);

	xmit = test_bit(FLAG_XMIT, &lp->flags);

	if (xmit && (irq1 & IRQ_CSMA_CA)) {
		if (ADF7242_REPORT_CSMA_CA_STAT) {
			u8 astat;

			adf7242_read_reg(lp, REG_AUTO_STATUS, &astat);
			astat &= AUTO_STATUS_MASK;

			dev_dbg(&lp->spi->dev, "AUTO_STATUS = %X:\n%s%s%s%s\n",
				astat,
				astat == SUCCESS ? "SUCCESS" : "",
				astat ==
				SUCCESS_DATPEND ? "SUCCESS_DATPEND" : "",
				astat == FAILURE_CSMACA ? "FAILURE_CSMACA" : "",
				astat == FAILURE_NOACK ? "FAILURE_NOACK" : "");

			/* save CSMA-CA completion status */
			lp->tx_stat = astat;
		} else {
			lp->tx_stat = SUCCESS;
		}
		complete(&lp->tx_complete);
	} else if (!xmit && (irq1 & IRQ_RX_PKT_RCVD) &&
		   (irq1 & IRQ_FRAME_VALID)) {
		adf7242_rx(lp);
	} else if (!xmit && test_bit(FLAG_START, &lp->flags)) {
		/* Invalid packet received - drop it and restart */
		dev_dbg(&lp->spi->dev, "%s:%d : ERROR IRQ1 = 0x%X\n",
			__func__, __LINE__, irq1);
		adf7242_cmd(lp, CMD_RC_PHY_RDY);
		adf7242_write_reg(lp, REG_IRQ1_SRC1, 0xFF);
		adf7242_cmd(lp, CMD_RC_RX);
	} else {
		/* This can only be xmit without IRQ, likely a RX packet.
		 * we get an TX IRQ shortly - do nothing or let the xmit
		 * timeout handle this
		 */
		dev_dbg(&lp->spi->dev, "%s:%d : ERROR IRQ1 = 0x%X, xmit %d\n",
			__func__, __LINE__, irq1, xmit);
		complete(&lp->tx_complete);
	}

	return IRQ_HANDLED;
}

static int adf7242_soft_reset(struct adf7242_local *lp, int line)
{
	dev_warn(&lp->spi->dev, "%s (line %d)\n", __func__, line);

	if (test_bit(FLAG_START, &lp->flags))
		disable_irq_nosync(lp->spi->irq);

	adf7242_cmd(lp, CMD_RC_PC_RESET_NO_WAIT);
	usleep_range(200, 250);
	adf7242_write_reg(lp, REG_PKT_CFG, ADDON_EN | BIT(2));
	adf7242_cmd(lp, CMD_RC_PHY_RDY);
	adf7242_set_promiscuous_mode(lp->hw, lp->promiscuous);
	adf7242_set_csma_params(lp->hw, lp->min_be, lp->max_be,
				lp->max_cca_retries);
	adf7242_write_reg(lp, REG_IRQ1_SRC1, 0xFF);

	if (test_bit(FLAG_START, &lp->flags)) {
		enable_irq(lp->spi->irq);
		return adf7242_cmd(lp, CMD_RC_RX);
	}

	return 0;
}

static int adf7242_hw_init(struct adf7242_local *lp)
{
	int ret;
	const struct firmware *fw;

	adf7242_cmd(lp, CMD_RC_RESET);
	adf7242_cmd(lp, CMD_RC_IDLE);

	/* get ADF7242 addon firmware
	 * build this driver as module
	 * and place under /lib/firmware/adf7242_firmware.bin
	 * or compile firmware into the kernel.
	 */
	ret = request_firmware(&fw, FIRMWARE, &lp->spi->dev);
	if (ret) {
		dev_err(&lp->spi->dev,
			"request_firmware() failed with %d\n", ret);
		return ret;
	}

	ret = adf7242_upload_firmware(lp, (u8 *)fw->data, fw->size);
	if (ret) {
		dev_err(&lp->spi->dev,
			"upload firmware failed with %d\n", ret);
		release_firmware(fw);
		return ret;
	}

	ret = adf7242_verify_firmware(lp, (u8 *)fw->data, fw->size);
	if (ret) {
		dev_err(&lp->spi->dev,
			"verify firmware failed with %d\n", ret);
		release_firmware(fw);
		return ret;
	}

	adf7242_cmd(lp, CMD_RC_PC_RESET);

	release_firmware(fw);

	adf7242_write_reg(lp, REG_FFILT_CFG,
			  ACCEPT_BEACON_FRAMES |
			  ACCEPT_DATA_FRAMES |
			  ACCEPT_MACCMD_FRAMES |
			  ACCEPT_RESERVED_FRAMES);

	adf7242_write_reg(lp, REG_AUTO_CFG, RX_AUTO_ACK_EN);

	adf7242_write_reg(lp, REG_PKT_CFG, ADDON_EN | BIT(2));

	adf7242_write_reg(lp, REG_EXTPA_MSC, 0xF1);
	adf7242_write_reg(lp, REG_RXFE_CFG, 0x1D);

	adf7242_write_reg(lp, REG_IRQ1_EN0, 0);
	adf7242_write_reg(lp, REG_IRQ1_EN1, IRQ_RX_PKT_RCVD | IRQ_CSMA_CA);

	adf7242_write_reg(lp, REG_IRQ1_SRC1, 0xFF);
	adf7242_write_reg(lp, REG_IRQ1_SRC0, 0xFF);

	adf7242_cmd(lp, CMD_RC_IDLE);

	return 0;
}

static int adf7242_stats_show(struct seq_file *file, void *offset)
{
	struct adf7242_local *lp = spi_get_drvdata(file->private);
	u8 stat, irq1;

	adf7242_status(lp, &stat);
	adf7242_read_reg(lp, REG_IRQ1_SRC1, &irq1);

	seq_printf(file, "IRQ1 = %X:\n%s%s%s%s%s%s%s%s\n", irq1,
		   irq1 & IRQ_CCA_COMPLETE ? "IRQ_CCA_COMPLETE\n" : "",
		   irq1 & IRQ_SFD_RX ? "IRQ_SFD_RX\n" : "",
		   irq1 & IRQ_SFD_TX ? "IRQ_SFD_TX\n" : "",
		   irq1 & IRQ_RX_PKT_RCVD ? "IRQ_RX_PKT_RCVD\n" : "",
		   irq1 & IRQ_TX_PKT_SENT ? "IRQ_TX_PKT_SENT\n" : "",
		   irq1 & IRQ_CSMA_CA ? "IRQ_CSMA_CA\n" : "",
		   irq1 & IRQ_FRAME_VALID ? "IRQ_FRAME_VALID\n" : "",
		   irq1 & IRQ_ADDRESS_VALID ? "IRQ_ADDRESS_VALID\n" : "");

	seq_printf(file, "STATUS = %X:\n%s\n%s%s%s%s%s\n", stat,
		   stat & STAT_RC_READY ? "RC_READY" : "RC_BUSY",
		   (stat & 0xf) == RC_STATUS_IDLE ? "RC_STATUS_IDLE" : "",
		   (stat & 0xf) == RC_STATUS_MEAS ? "RC_STATUS_MEAS" : "",
		   (stat & 0xf) == RC_STATUS_PHY_RDY ? "RC_STATUS_PHY_RDY" : "",
		   (stat & 0xf) == RC_STATUS_RX ? "RC_STATUS_RX" : "",
		   (stat & 0xf) == RC_STATUS_TX ? "RC_STATUS_TX" : "");

	seq_printf(file, "RSSI = %d\n", lp->rssi);

	return 0;
}

static int adf7242_debugfs_init(struct adf7242_local *lp)
{
	char debugfs_dir_name[DNAME_INLINE_LEN + 1] = "adf7242-";
	struct dentry *stats;

	strncat(debugfs_dir_name, dev_name(&lp->spi->dev), DNAME_INLINE_LEN);

	lp->debugfs_root = debugfs_create_dir(debugfs_dir_name, NULL);
	if (IS_ERR_OR_NULL(lp->debugfs_root))
		return PTR_ERR_OR_ZERO(lp->debugfs_root);

	stats = debugfs_create_devm_seqfile(&lp->spi->dev, "status",
					    lp->debugfs_root,
					    adf7242_stats_show);
	return PTR_ERR_OR_ZERO(stats);

	return 0;
}

static const s32 adf7242_powers[] = {
	500, 400, 300, 200, 100, 0, -100, -200, -300, -400, -500, -600, -700,
	-800, -900, -1000, -1100, -1200, -1300, -1400, -1500, -1600, -1700,
	-1800, -1900, -2000, -2100, -2200, -2300, -2400, -2500, -2600,
};

static const s32 adf7242_ed_levels[] = {
	-9000, -8900, -8800, -8700, -8600, -8500, -8400, -8300, -8200, -8100,
	-8000, -7900, -7800, -7700, -7600, -7500, -7400, -7300, -7200, -7100,
	-7000, -6900, -6800, -6700, -6600, -6500, -6400, -6300, -6200, -6100,
	-6000, -5900, -5800, -5700, -5600, -5500, -5400, -5300, -5200, -5100,
	-5000, -4900, -4800, -4700, -4600, -4500, -4400, -4300, -4200, -4100,
	-4000, -3900, -3800, -3700, -3600, -3500, -3400, -3200, -3100, -3000
};

static int adf7242_probe(struct spi_device *spi)
{
	struct ieee802154_hw *hw;
	struct adf7242_local *lp;
	int ret, irq_type;

	if (!spi->irq) {
		dev_err(&spi->dev, "no IRQ specified\n");
		return -EINVAL;
	}

	hw = ieee802154_alloc_hw(sizeof(*lp), &adf7242_ops);
	if (!hw)
		return -ENOMEM;

	lp = hw->priv;
	lp->hw = hw;
	lp->spi = spi;

	hw->priv = lp;
	hw->parent = &spi->dev;
	hw->extra_tx_headroom = 0;

	/* We support only 2.4 Ghz */
	hw->phy->supported.channels[0] = 0x7FFF800;

	hw->flags = IEEE802154_HW_OMIT_CKSUM |
		    IEEE802154_HW_CSMA_PARAMS |
		    IEEE802154_HW_FRAME_RETRIES | IEEE802154_HW_AFILT |
		    IEEE802154_HW_PROMISCUOUS;

	hw->phy->flags = WPAN_PHY_FLAG_TXPOWER |
			 WPAN_PHY_FLAG_CCA_ED_LEVEL |
			 WPAN_PHY_FLAG_CCA_MODE;

	hw->phy->supported.cca_modes = BIT(NL802154_CCA_ENERGY);

	hw->phy->supported.cca_ed_levels = adf7242_ed_levels;
	hw->phy->supported.cca_ed_levels_size = ARRAY_SIZE(adf7242_ed_levels);

	hw->phy->cca.mode = NL802154_CCA_ENERGY;

	hw->phy->supported.tx_powers = adf7242_powers;
	hw->phy->supported.tx_powers_size = ARRAY_SIZE(adf7242_powers);

	hw->phy->supported.min_minbe = 0;
	hw->phy->supported.max_minbe = 8;

	hw->phy->supported.min_maxbe = 3;
	hw->phy->supported.max_maxbe = 8;

	hw->phy->supported.min_frame_retries = 0;
	hw->phy->supported.max_frame_retries = 15;

	hw->phy->supported.min_csma_backoffs = 0;
	hw->phy->supported.max_csma_backoffs = 5;

	ieee802154_random_extended_addr(&hw->phy->perm_extended_addr);

	mutex_init(&lp->bmux);
	init_completion(&lp->tx_complete);

	/* Setup Status Message */
	lp->stat_xfer.len = 1;
	lp->stat_xfer.tx_buf = &lp->buf_stat_tx;
	lp->stat_xfer.rx_buf = &lp->buf_stat_rx;
	lp->buf_stat_tx = CMD_SPI_NOP;

	spi_message_init(&lp->stat_msg);
	spi_message_add_tail(&lp->stat_xfer, &lp->stat_msg);

	spi_set_drvdata(spi, lp);

	ret = adf7242_hw_init(lp);
	if (ret)
		goto err_hw_init;

	irq_type = irq_get_trigger_type(spi->irq);
	if (!irq_type)
		irq_type = IRQF_TRIGGER_HIGH;

	ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL, adf7242_isr,
					irq_type | IRQF_ONESHOT,
					dev_name(&spi->dev), lp);
	if (ret)
		goto err_hw_init;

	disable_irq(spi->irq);

	ret = ieee802154_register_hw(lp->hw);
	if (ret)
		goto err_hw_init;

	dev_set_drvdata(&spi->dev, lp);

	adf7242_debugfs_init(lp);

	dev_info(&spi->dev, "mac802154 IRQ-%d registered\n", spi->irq);

	return ret;

err_hw_init:
	mutex_destroy(&lp->bmux);
	ieee802154_free_hw(lp->hw);

	return ret;
}

static int adf7242_remove(struct spi_device *spi)
{
	struct adf7242_local *lp = spi_get_drvdata(spi);

	if (!IS_ERR_OR_NULL(lp->debugfs_root))
		debugfs_remove_recursive(lp->debugfs_root);

	ieee802154_unregister_hw(lp->hw);
	mutex_destroy(&lp->bmux);
	ieee802154_free_hw(lp->hw);

	return 0;
}

static const struct of_device_id adf7242_of_match[] = {
	{ .compatible = "adi,adf7242", },
	{ .compatible = "adi,adf7241", },
	{ },
};
MODULE_DEVICE_TABLE(of, adf7242_of_match);

static const struct spi_device_id adf7242_device_id[] = {
	{ .name = "adf7242", },
	{ .name = "adf7241", },
	{ },
};
MODULE_DEVICE_TABLE(spi, adf7242_device_id);

static struct spi_driver adf7242_driver = {
	.id_table = adf7242_device_id,
	.driver = {
		   .of_match_table = of_match_ptr(adf7242_of_match),
		   .name = "adf7242",
		   .owner = THIS_MODULE,
		   },
	.probe = adf7242_probe,
	.remove = adf7242_remove,
};

module_spi_driver(adf7242_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("ADF7242 IEEE802.15.4 Transceiver Driver");
MODULE_LICENSE("GPL");
