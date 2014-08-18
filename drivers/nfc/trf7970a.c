/*
 * TI TRF7970a RFID/NFC Transceiver Driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Erick Macias <emacias@ti.com>
 * Author: Felipe Balbi <balbi@ti.com>
 * Author: Mark A. Greer <mgreer@animalcreek.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/nfc.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>

#include <net/nfc/nfc.h>
#include <net/nfc/digital.h>

/* There are 3 ways the host can communicate with the trf7970a:
 * parallel mode, SPI with Slave Select (SS) mode, and SPI without
 * SS mode.  The driver only supports the two SPI modes.
 *
 * The trf7970a is very timing sensitive and the VIN, EN2, and EN
 * pins must asserted in that order and with specific delays in between.
 * The delays used in the driver were provided by TI and have been
 * confirmed to work with this driver.
 *
 * Timeouts are implemented using the delayed workqueue kernel facility.
 * Timeouts are required so things don't hang when there is no response
 * from the trf7970a (or tag).  Using this mechanism creates a race with
 * interrupts, however.  That is, an interrupt and a timeout could occur
 * closely enough together that one is blocked by the mutex while the other
 * executes.  When the timeout handler executes first and blocks the
 * interrupt handler, it will eventually set the state to IDLE so the
 * interrupt handler will check the state and exit with no harm done.
 * When the interrupt handler executes first and blocks the timeout handler,
 * the cancel_delayed_work() call will know that it didn't cancel the
 * work item (i.e., timeout) and will return zero.  That return code is
 * used by the timer handler to indicate that it should ignore the timeout
 * once its unblocked.
 *
 * Aborting an active command isn't as simple as it seems because the only
 * way to abort a command that's already been sent to the tag is so turn
 * off power to the tag.  If we do that, though, we'd have to go through
 * the entire anticollision procedure again but the digital layer doesn't
 * support that.  So, if an abort is received before trf7970a_in_send_cmd()
 * has sent the command to the tag, it simply returns -ECANCELED.  If the
 * command has already been sent to the tag, then the driver continues
 * normally and recieves the response data (or error) but just before
 * sending the data upstream, it frees the rx_skb and sends -ECANCELED
 * upstream instead.  If the command failed, that error will be sent
 * upstream.
 *
 * When recieving data from a tag and the interrupt status register has
 * only the SRX bit set, it means that all of the data has been received
 * (once what's in the fifo has been read).  However, depending on timing
 * an interrupt status with only the SRX bit set may not be recived.  In
 * those cases, the timeout mechanism is used to wait 20 ms in case more
 * data arrives.  After 20 ms, it is assumed that all of the data has been
 * received and the accumulated rx data is sent upstream.  The
 * 'TRF7970A_ST_WAIT_FOR_RX_DATA_CONT' state is used for this purpose
 * (i.e., it indicates that some data has been received but we're not sure
 * if there is more coming so a timeout in this state means all data has
 * been received and there isn't an error).  The delay is 20 ms since delays
 * of ~16 ms have been observed during testing.
 *
 * Type 2 write and sector select commands respond with a 4-bit ACK or NACK.
 * Having only 4 bits in the FIFO won't normally generate an interrupt so
 * driver enables the '4_bit_RX' bit of the Special Functions register 1
 * to cause an interrupt in that case.  Leaving that bit for a read command
 * messes up the data returned so it is only enabled when the framing is
 * 'NFC_DIGITAL_FRAMING_NFCA_T2T' and the command is not a read command.
 * Unfortunately, that means that the driver has to peek into tx frames
 * when the framing is 'NFC_DIGITAL_FRAMING_NFCA_T2T'.  This is done by
 * the trf7970a_per_cmd_config() routine.
 *
 * ISO/IEC 15693 frames specify whether to use single or double sub-carrier
 * frequencies and whether to use low or high data rates in the flags byte
 * of the frame.  This means that the driver has to peek at all 15693 frames
 * to determine what speed to set the communication to.  In addition, write
 * and lock commands use the OPTION flag to indicate that an EOF must be
 * sent to the tag before it will send its response.  So the driver has to
 * examine all frames for that reason too.
 *
 * It is unclear how long to wait before sending the EOF.  According to the
 * Note under Table 1-1 in section 1.6 of
 * http://www.ti.com/lit/ug/scbu011/scbu011.pdf, that wait should be at least
 * 10 ms for TI Tag-it HF-I tags; however testing has shown that is not long
 * enough.  For this reason, the driver waits 20 ms which seems to work
 * reliably.
 */

#define TRF7970A_SUPPORTED_PROTOCOLS \
		(NFC_PROTO_MIFARE_MASK | NFC_PROTO_ISO14443_MASK |	\
		 NFC_PROTO_ISO14443_B_MASK | NFC_PROTO_FELICA_MASK | \
		 NFC_PROTO_ISO15693_MASK)

#define TRF7970A_AUTOSUSPEND_DELAY		30000 /* 30 seconds */

/* TX data must be prefixed with a FIFO reset cmd, a cmd that depends
 * on what the current framing is, the address of the TX length byte 1
 * register (0x1d), and the 2 byte length of the data to be transmitted.
 * That totals 5 bytes.
 */
#define TRF7970A_TX_SKB_HEADROOM		5

#define TRF7970A_RX_SKB_ALLOC_SIZE		256

#define TRF7970A_FIFO_SIZE			128

/* TX length is 3 nibbles long ==> 4KB - 1 bytes max */
#define TRF7970A_TX_MAX				(4096 - 1)

#define TRF7970A_WAIT_FOR_RX_DATA_TIMEOUT	20
#define TRF7970A_WAIT_FOR_FIFO_DRAIN_TIMEOUT	3
#define TRF7970A_WAIT_TO_ISSUE_ISO15693_EOF	20

/* Quirks */
/* Erratum: When reading IRQ Status register on trf7970a, we must issue a
 * read continuous command for IRQ Status and Collision Position registers.
 */
#define TRF7970A_QUIRK_IRQ_STATUS_READ_ERRATA	BIT(0)

/* Direct commands */
#define TRF7970A_CMD_IDLE			0x00
#define TRF7970A_CMD_SOFT_INIT			0x03
#define TRF7970A_CMD_RF_COLLISION		0x04
#define TRF7970A_CMD_RF_COLLISION_RESPONSE_N	0x05
#define TRF7970A_CMD_RF_COLLISION_RESPONSE_0	0x06
#define TRF7970A_CMD_FIFO_RESET			0x0f
#define TRF7970A_CMD_TRANSMIT_NO_CRC		0x10
#define TRF7970A_CMD_TRANSMIT			0x11
#define TRF7970A_CMD_DELAY_TRANSMIT_NO_CRC	0x12
#define TRF7970A_CMD_DELAY_TRANSMIT		0x13
#define TRF7970A_CMD_EOF			0x14
#define TRF7970A_CMD_CLOSE_SLOT			0x15
#define TRF7970A_CMD_BLOCK_RX			0x16
#define TRF7970A_CMD_ENABLE_RX			0x17
#define TRF7970A_CMD_TEST_EXT_RF		0x18
#define TRF7970A_CMD_TEST_INT_RF		0x19
#define TRF7970A_CMD_RX_GAIN_ADJUST		0x1a

/* Bits determining whether its a direct command or register R/W,
 * whether to use a continuous SPI transaction or not, and the actual
 * direct cmd opcode or regster address.
 */
#define TRF7970A_CMD_BIT_CTRL			BIT(7)
#define TRF7970A_CMD_BIT_RW			BIT(6)
#define TRF7970A_CMD_BIT_CONTINUOUS		BIT(5)
#define TRF7970A_CMD_BIT_OPCODE(opcode)		((opcode) & 0x1f)

/* Registers addresses */
#define TRF7970A_CHIP_STATUS_CTRL		0x00
#define TRF7970A_ISO_CTRL			0x01
#define TRF7970A_ISO14443B_TX_OPTIONS		0x02
#define TRF7970A_ISO14443A_HIGH_BITRATE_OPTIONS	0x03
#define TRF7970A_TX_TIMER_SETTING_H_BYTE	0x04
#define TRF7970A_TX_TIMER_SETTING_L_BYTE	0x05
#define TRF7970A_TX_PULSE_LENGTH_CTRL		0x06
#define TRF7970A_RX_NO_RESPONSE_WAIT		0x07
#define TRF7970A_RX_WAIT_TIME			0x08
#define TRF7970A_MODULATOR_SYS_CLK_CTRL		0x09
#define TRF7970A_RX_SPECIAL_SETTINGS		0x0a
#define TRF7970A_REG_IO_CTRL			0x0b
#define TRF7970A_IRQ_STATUS			0x0c
#define TRF7970A_COLLISION_IRQ_MASK		0x0d
#define TRF7970A_COLLISION_POSITION		0x0e
#define TRF7970A_RSSI_OSC_STATUS		0x0f
#define TRF7970A_SPECIAL_FCN_REG1		0x10
#define TRF7970A_SPECIAL_FCN_REG2		0x11
#define TRF7970A_RAM1				0x12
#define TRF7970A_RAM2				0x13
#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS	0x14
#define TRF7970A_NFC_LOW_FIELD_LEVEL		0x16
#define TRF7970A_NFCID1				0x17
#define TRF7970A_NFC_TARGET_LEVEL		0x18
#define TRF79070A_NFC_TARGET_PROTOCOL		0x19
#define TRF7970A_TEST_REGISTER1			0x1a
#define TRF7970A_TEST_REGISTER2			0x1b
#define TRF7970A_FIFO_STATUS			0x1c
#define TRF7970A_TX_LENGTH_BYTE1		0x1d
#define TRF7970A_TX_LENGTH_BYTE2		0x1e
#define TRF7970A_FIFO_IO_REGISTER		0x1f

/* Chip Status Control Register Bits */
#define TRF7970A_CHIP_STATUS_VRS5_3		BIT(0)
#define TRF7970A_CHIP_STATUS_REC_ON		BIT(1)
#define TRF7970A_CHIP_STATUS_AGC_ON		BIT(2)
#define TRF7970A_CHIP_STATUS_PM_ON		BIT(3)
#define TRF7970A_CHIP_STATUS_RF_PWR		BIT(4)
#define TRF7970A_CHIP_STATUS_RF_ON		BIT(5)
#define TRF7970A_CHIP_STATUS_DIRECT		BIT(6)
#define TRF7970A_CHIP_STATUS_STBY		BIT(7)

/* ISO Control Register Bits */
#define TRF7970A_ISO_CTRL_15693_SGL_1OF4_662	0x00
#define TRF7970A_ISO_CTRL_15693_SGL_1OF256_662	0x01
#define TRF7970A_ISO_CTRL_15693_SGL_1OF4_2648	0x02
#define TRF7970A_ISO_CTRL_15693_SGL_1OF256_2648	0x03
#define TRF7970A_ISO_CTRL_15693_DBL_1OF4_667a	0x04
#define TRF7970A_ISO_CTRL_15693_DBL_1OF256_667	0x05
#define TRF7970A_ISO_CTRL_15693_DBL_1OF4_2669	0x06
#define TRF7970A_ISO_CTRL_15693_DBL_1OF256_2669	0x07
#define TRF7970A_ISO_CTRL_14443A_106		0x08
#define TRF7970A_ISO_CTRL_14443A_212		0x09
#define TRF7970A_ISO_CTRL_14443A_424		0x0a
#define TRF7970A_ISO_CTRL_14443A_848		0x0b
#define TRF7970A_ISO_CTRL_14443B_106		0x0c
#define TRF7970A_ISO_CTRL_14443B_212		0x0d
#define TRF7970A_ISO_CTRL_14443B_424		0x0e
#define TRF7970A_ISO_CTRL_14443B_848		0x0f
#define TRF7970A_ISO_CTRL_FELICA_212		0x1a
#define TRF7970A_ISO_CTRL_FELICA_424		0x1b
#define TRF7970A_ISO_CTRL_RFID			BIT(5)
#define TRF7970A_ISO_CTRL_DIR_MODE		BIT(6)
#define TRF7970A_ISO_CTRL_RX_CRC_N		BIT(7)	/* true == No CRC */

#define TRF7970A_ISO_CTRL_RFID_SPEED_MASK	0x1f

/* Modulator and SYS_CLK Control Register Bits */
#define TRF7970A_MODULATOR_DEPTH(n)		((n) & 0x7)
#define TRF7970A_MODULATOR_DEPTH_ASK10		(TRF7970A_MODULATOR_DEPTH(0))
#define TRF7970A_MODULATOR_DEPTH_OOK		(TRF7970A_MODULATOR_DEPTH(1))
#define TRF7970A_MODULATOR_DEPTH_ASK7		(TRF7970A_MODULATOR_DEPTH(2))
#define TRF7970A_MODULATOR_DEPTH_ASK8_5		(TRF7970A_MODULATOR_DEPTH(3))
#define TRF7970A_MODULATOR_DEPTH_ASK13		(TRF7970A_MODULATOR_DEPTH(4))
#define TRF7970A_MODULATOR_DEPTH_ASK16		(TRF7970A_MODULATOR_DEPTH(5))
#define TRF7970A_MODULATOR_DEPTH_ASK22		(TRF7970A_MODULATOR_DEPTH(6))
#define TRF7970A_MODULATOR_DEPTH_ASK30		(TRF7970A_MODULATOR_DEPTH(7))
#define TRF7970A_MODULATOR_EN_ANA		BIT(3)
#define TRF7970A_MODULATOR_CLK(n)		(((n) & 0x3) << 4)
#define TRF7970A_MODULATOR_CLK_DISABLED		(TRF7970A_MODULATOR_CLK(0))
#define TRF7970A_MODULATOR_CLK_3_6		(TRF7970A_MODULATOR_CLK(1))
#define TRF7970A_MODULATOR_CLK_6_13		(TRF7970A_MODULATOR_CLK(2))
#define TRF7970A_MODULATOR_CLK_13_27		(TRF7970A_MODULATOR_CLK(3))
#define TRF7970A_MODULATOR_EN_OOK		BIT(6)
#define TRF7970A_MODULATOR_27MHZ		BIT(7)

/* IRQ Status Register Bits */
#define TRF7970A_IRQ_STATUS_NORESP		BIT(0) /* ISO15693 only */
#define TRF7970A_IRQ_STATUS_COL			BIT(1)
#define TRF7970A_IRQ_STATUS_FRAMING_EOF_ERROR	BIT(2)
#define TRF7970A_IRQ_STATUS_PARITY_ERROR	BIT(3)
#define TRF7970A_IRQ_STATUS_CRC_ERROR		BIT(4)
#define TRF7970A_IRQ_STATUS_FIFO		BIT(5)
#define TRF7970A_IRQ_STATUS_SRX			BIT(6)
#define TRF7970A_IRQ_STATUS_TX			BIT(7)

#define TRF7970A_IRQ_STATUS_ERROR				\
		(TRF7970A_IRQ_STATUS_COL |			\
		 TRF7970A_IRQ_STATUS_FRAMING_EOF_ERROR |	\
		 TRF7970A_IRQ_STATUS_PARITY_ERROR |		\
		 TRF7970A_IRQ_STATUS_CRC_ERROR)

#define TRF7970A_SPECIAL_FCN_REG1_COL_7_6		BIT(0)
#define TRF7970A_SPECIAL_FCN_REG1_14_ANTICOLL		BIT(1)
#define TRF7970A_SPECIAL_FCN_REG1_4_BIT_RX		BIT(2)
#define TRF7970A_SPECIAL_FCN_REG1_SP_DIR_MODE		BIT(3)
#define TRF7970A_SPECIAL_FCN_REG1_NEXT_SLOT_37US	BIT(4)
#define TRF7970A_SPECIAL_FCN_REG1_PAR43			BIT(5)

#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLH_124	(0x0 << 2)
#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLH_120	(0x1 << 2)
#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLH_112	(0x2 << 2)
#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLH_96	(0x3 << 2)
#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLL_4	0x0
#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLL_8	0x1
#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLL_16	0x2
#define TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLL_32	0x3

#define TRF7970A_FIFO_STATUS_OVERFLOW		BIT(7)

/* NFC (ISO/IEC 14443A) Type 2 Tag commands */
#define NFC_T2T_CMD_READ			0x30

/* ISO 15693 commands codes */
#define ISO15693_CMD_INVENTORY			0x01
#define ISO15693_CMD_READ_SINGLE_BLOCK		0x20
#define ISO15693_CMD_WRITE_SINGLE_BLOCK		0x21
#define ISO15693_CMD_LOCK_BLOCK			0x22
#define ISO15693_CMD_READ_MULTIPLE_BLOCK	0x23
#define ISO15693_CMD_WRITE_MULTIPLE_BLOCK	0x24
#define ISO15693_CMD_SELECT			0x25
#define ISO15693_CMD_RESET_TO_READY		0x26
#define ISO15693_CMD_WRITE_AFI			0x27
#define ISO15693_CMD_LOCK_AFI			0x28
#define ISO15693_CMD_WRITE_DSFID		0x29
#define ISO15693_CMD_LOCK_DSFID			0x2a
#define ISO15693_CMD_GET_SYSTEM_INFO		0x2b
#define ISO15693_CMD_GET_MULTIPLE_BLOCK_SECURITY_STATUS	0x2c

/* ISO 15693 request and response flags */
#define ISO15693_REQ_FLAG_SUB_CARRIER		BIT(0)
#define ISO15693_REQ_FLAG_DATA_RATE		BIT(1)
#define ISO15693_REQ_FLAG_INVENTORY		BIT(2)
#define ISO15693_REQ_FLAG_PROTOCOL_EXT		BIT(3)
#define ISO15693_REQ_FLAG_SELECT		BIT(4)
#define ISO15693_REQ_FLAG_AFI			BIT(4)
#define ISO15693_REQ_FLAG_ADDRESS		BIT(5)
#define ISO15693_REQ_FLAG_NB_SLOTS		BIT(5)
#define ISO15693_REQ_FLAG_OPTION		BIT(6)

#define ISO15693_REQ_FLAG_SPEED_MASK \
		(ISO15693_REQ_FLAG_SUB_CARRIER | ISO15693_REQ_FLAG_DATA_RATE)

enum trf7970a_state {
	TRF7970A_ST_OFF,
	TRF7970A_ST_IDLE,
	TRF7970A_ST_IDLE_RX_BLOCKED,
	TRF7970A_ST_WAIT_FOR_TX_FIFO,
	TRF7970A_ST_WAIT_FOR_RX_DATA,
	TRF7970A_ST_WAIT_FOR_RX_DATA_CONT,
	TRF7970A_ST_WAIT_TO_ISSUE_EOF,
	TRF7970A_ST_MAX
};

struct trf7970a {
	enum trf7970a_state		state;
	struct device			*dev;
	struct spi_device		*spi;
	struct regulator		*regulator;
	struct nfc_digital_dev		*ddev;
	u32				quirks;
	bool				aborting;
	struct sk_buff			*tx_skb;
	struct sk_buff			*rx_skb;
	nfc_digital_cmd_complete_t	cb;
	void				*cb_arg;
	u8				chip_status_ctrl;
	u8				iso_ctrl;
	u8				iso_ctrl_tech;
	u8				modulator_sys_clk_ctrl;
	u8				special_fcn_reg1;
	int				technology;
	int				framing;
	u8				tx_cmd;
	bool				issue_eof;
	int				en2_gpio;
	int				en_gpio;
	struct mutex			lock;
	unsigned int			timeout;
	bool				ignore_timeout;
	struct delayed_work		timeout_work;
};


static int trf7970a_cmd(struct trf7970a *trf, u8 opcode)
{
	u8 cmd = TRF7970A_CMD_BIT_CTRL | TRF7970A_CMD_BIT_OPCODE(opcode);
	int ret;

	dev_dbg(trf->dev, "cmd: 0x%x\n", cmd);

	ret = spi_write(trf->spi, &cmd, 1);
	if (ret)
		dev_err(trf->dev, "%s - cmd: 0x%x, ret: %d\n", __func__, cmd,
				ret);
	return ret;
}

static int trf7970a_read(struct trf7970a *trf, u8 reg, u8 *val)
{
	u8 addr = TRF7970A_CMD_BIT_RW | reg;
	int ret;

	ret = spi_write_then_read(trf->spi, &addr, 1, val, 1);
	if (ret)
		dev_err(trf->dev, "%s - addr: 0x%x, ret: %d\n", __func__, addr,
				ret);

	dev_dbg(trf->dev, "read(0x%x): 0x%x\n", addr, *val);

	return ret;
}

static int trf7970a_read_cont(struct trf7970a *trf, u8 reg,
		u8 *buf, size_t len)
{
	u8 addr = reg | TRF7970A_CMD_BIT_RW | TRF7970A_CMD_BIT_CONTINUOUS;
	int ret;

	dev_dbg(trf->dev, "read_cont(0x%x, %zd)\n", addr, len);

	ret = spi_write_then_read(trf->spi, &addr, 1, buf, len);
	if (ret)
		dev_err(trf->dev, "%s - addr: 0x%x, ret: %d\n", __func__, addr,
				ret);
	return ret;
}

static int trf7970a_write(struct trf7970a *trf, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	int ret;

	dev_dbg(trf->dev, "write(0x%x): 0x%x\n", reg, val);

	ret = spi_write(trf->spi, buf, 2);
	if (ret)
		dev_err(trf->dev, "%s - write: 0x%x 0x%x, ret: %d\n", __func__,
				buf[0], buf[1], ret);

	return ret;
}

static int trf7970a_read_irqstatus(struct trf7970a *trf, u8 *status)
{
	int ret;
	u8 buf[2];
	u8 addr;

	addr = TRF7970A_IRQ_STATUS | TRF7970A_CMD_BIT_RW;

	if (trf->quirks & TRF7970A_QUIRK_IRQ_STATUS_READ_ERRATA) {
		addr |= TRF7970A_CMD_BIT_CONTINUOUS;
		ret = spi_write_then_read(trf->spi, &addr, 1, buf, 2);
	} else {
		ret = spi_write_then_read(trf->spi, &addr, 1, buf, 1);
	}

	if (ret)
		dev_err(trf->dev, "%s - irqstatus: Status read failed: %d\n",
				__func__, ret);
	else
		*status = buf[0];

	return ret;
}

static void trf7970a_send_upstream(struct trf7970a *trf)
{
	u8 rssi;

	dev_kfree_skb_any(trf->tx_skb);
	trf->tx_skb = NULL;

	if (trf->rx_skb && !IS_ERR(trf->rx_skb) && !trf->aborting)
		print_hex_dump_debug("trf7970a rx data: ", DUMP_PREFIX_NONE,
				16, 1, trf->rx_skb->data, trf->rx_skb->len,
				false);

	/* According to the manual it is "good form" to reset the fifo and
	 * read the RSSI levels & oscillator status register here.  It doesn't
	 * explain why.
	 */
	trf7970a_cmd(trf, TRF7970A_CMD_FIFO_RESET);
	trf7970a_read(trf, TRF7970A_RSSI_OSC_STATUS, &rssi);

	trf->state = TRF7970A_ST_IDLE;

	if (trf->aborting) {
		dev_dbg(trf->dev, "Abort process complete\n");

		if (!IS_ERR(trf->rx_skb)) {
			kfree_skb(trf->rx_skb);
			trf->rx_skb = ERR_PTR(-ECANCELED);
		}

		trf->aborting = false;
	}

	trf->cb(trf->ddev, trf->cb_arg, trf->rx_skb);

	trf->rx_skb = NULL;
}

static void trf7970a_send_err_upstream(struct trf7970a *trf, int errno)
{
	dev_dbg(trf->dev, "Error - state: %d, errno: %d\n", trf->state, errno);

	kfree_skb(trf->rx_skb);
	trf->rx_skb = ERR_PTR(errno);

	trf7970a_send_upstream(trf);
}

static int trf7970a_transmit(struct trf7970a *trf, struct sk_buff *skb,
		unsigned int len)
{
	unsigned int timeout;
	int ret;

	print_hex_dump_debug("trf7970a tx data: ", DUMP_PREFIX_NONE,
			16, 1, skb->data, len, false);

	ret = spi_write(trf->spi, skb->data, len);
	if (ret) {
		dev_err(trf->dev, "%s - Can't send tx data: %d\n", __func__,
				ret);
		return ret;
	}

	skb_pull(skb, len);

	if (skb->len > 0) {
		trf->state = TRF7970A_ST_WAIT_FOR_TX_FIFO;
		timeout = TRF7970A_WAIT_FOR_FIFO_DRAIN_TIMEOUT;
	} else {
		if (trf->issue_eof) {
			trf->state = TRF7970A_ST_WAIT_TO_ISSUE_EOF;
			timeout = TRF7970A_WAIT_TO_ISSUE_ISO15693_EOF;
		} else {
			trf->state = TRF7970A_ST_WAIT_FOR_RX_DATA;
			timeout = trf->timeout;
		}
	}

	dev_dbg(trf->dev, "Setting timeout for %d ms, state: %d\n", timeout,
			trf->state);

	schedule_delayed_work(&trf->timeout_work, msecs_to_jiffies(timeout));

	return 0;
}

static void trf7970a_fill_fifo(struct trf7970a *trf)
{
	struct sk_buff *skb = trf->tx_skb;
	unsigned int len;
	int ret;
	u8 fifo_bytes;

	ret = trf7970a_read(trf, TRF7970A_FIFO_STATUS, &fifo_bytes);
	if (ret) {
		trf7970a_send_err_upstream(trf, ret);
		return;
	}

	dev_dbg(trf->dev, "Filling FIFO - fifo_bytes: 0x%x\n", fifo_bytes);

	if (fifo_bytes & TRF7970A_FIFO_STATUS_OVERFLOW) {
		dev_err(trf->dev, "%s - fifo overflow: 0x%x\n", __func__,
				fifo_bytes);
		trf7970a_send_err_upstream(trf, -EIO);
		return;
	}

	/* Calculate how much more data can be written to the fifo */
	len = TRF7970A_FIFO_SIZE - fifo_bytes;
	len = min(skb->len, len);

	ret = trf7970a_transmit(trf, skb, len);
	if (ret)
		trf7970a_send_err_upstream(trf, ret);
}

static void trf7970a_drain_fifo(struct trf7970a *trf, u8 status)
{
	struct sk_buff *skb = trf->rx_skb;
	int ret;
	u8 fifo_bytes;

	if (status & TRF7970A_IRQ_STATUS_ERROR) {
		trf7970a_send_err_upstream(trf, -EIO);
		return;
	}

	ret = trf7970a_read(trf, TRF7970A_FIFO_STATUS, &fifo_bytes);
	if (ret) {
		trf7970a_send_err_upstream(trf, ret);
		return;
	}

	dev_dbg(trf->dev, "Draining FIFO - fifo_bytes: 0x%x\n", fifo_bytes);

	if (!fifo_bytes)
		goto no_rx_data;

	if (fifo_bytes & TRF7970A_FIFO_STATUS_OVERFLOW) {
		dev_err(trf->dev, "%s - fifo overflow: 0x%x\n", __func__,
				fifo_bytes);
		trf7970a_send_err_upstream(trf, -EIO);
		return;
	}

	if (fifo_bytes > skb_tailroom(skb)) {
		skb = skb_copy_expand(skb, skb_headroom(skb),
				max_t(int, fifo_bytes,
					TRF7970A_RX_SKB_ALLOC_SIZE),
				GFP_KERNEL);
		if (!skb) {
			trf7970a_send_err_upstream(trf, -ENOMEM);
			return;
		}

		kfree_skb(trf->rx_skb);
		trf->rx_skb = skb;
	}

	ret = trf7970a_read_cont(trf, TRF7970A_FIFO_IO_REGISTER,
			skb_put(skb, fifo_bytes), fifo_bytes);
	if (ret) {
		trf7970a_send_err_upstream(trf, ret);
		return;
	}

	/* If received Type 2 ACK/NACK, shift right 4 bits and pass up */
	if ((trf->framing == NFC_DIGITAL_FRAMING_NFCA_T2T) && (skb->len == 1) &&
			(trf->special_fcn_reg1 ==
				 TRF7970A_SPECIAL_FCN_REG1_4_BIT_RX)) {
		skb->data[0] >>= 4;
		status = TRF7970A_IRQ_STATUS_SRX;
	} else {
		trf->state = TRF7970A_ST_WAIT_FOR_RX_DATA_CONT;
	}

no_rx_data:
	if (status == TRF7970A_IRQ_STATUS_SRX) { /* Receive complete */
		trf7970a_send_upstream(trf);
		return;
	}

	dev_dbg(trf->dev, "Setting timeout for %d ms\n",
			TRF7970A_WAIT_FOR_RX_DATA_TIMEOUT);

	schedule_delayed_work(&trf->timeout_work,
			msecs_to_jiffies(TRF7970A_WAIT_FOR_RX_DATA_TIMEOUT));
}

static irqreturn_t trf7970a_irq(int irq, void *dev_id)
{
	struct trf7970a *trf = dev_id;
	int ret;
	u8 status;

	mutex_lock(&trf->lock);

	if (trf->state == TRF7970A_ST_OFF) {
		mutex_unlock(&trf->lock);
		return IRQ_NONE;
	}

	ret = trf7970a_read_irqstatus(trf, &status);
	if (ret) {
		mutex_unlock(&trf->lock);
		return IRQ_NONE;
	}

	dev_dbg(trf->dev, "IRQ - state: %d, status: 0x%x\n", trf->state,
			status);

	if (!status) {
		mutex_unlock(&trf->lock);
		return IRQ_NONE;
	}

	switch (trf->state) {
	case TRF7970A_ST_IDLE:
	case TRF7970A_ST_IDLE_RX_BLOCKED:
		/* If getting interrupts caused by RF noise, turn off the
		 * receiver to avoid unnecessary interrupts.  It will be
		 * turned back on in trf7970a_in_send_cmd() when the next
		 * command is issued.
		 */
		if (status & TRF7970A_IRQ_STATUS_ERROR) {
			trf7970a_cmd(trf, TRF7970A_CMD_BLOCK_RX);
			trf->state = TRF7970A_ST_IDLE_RX_BLOCKED;
		}

		trf7970a_cmd(trf, TRF7970A_CMD_FIFO_RESET);
		break;
	case TRF7970A_ST_WAIT_FOR_TX_FIFO:
		if (status & TRF7970A_IRQ_STATUS_TX) {
			trf->ignore_timeout =
				!cancel_delayed_work(&trf->timeout_work);
			trf7970a_fill_fifo(trf);
		} else {
			trf7970a_send_err_upstream(trf, -EIO);
		}
		break;
	case TRF7970A_ST_WAIT_FOR_RX_DATA:
	case TRF7970A_ST_WAIT_FOR_RX_DATA_CONT:
		if (status & TRF7970A_IRQ_STATUS_SRX) {
			trf->ignore_timeout =
				!cancel_delayed_work(&trf->timeout_work);
			trf7970a_drain_fifo(trf, status);
		} else if (status == TRF7970A_IRQ_STATUS_TX) {
			trf7970a_cmd(trf, TRF7970A_CMD_FIFO_RESET);
		} else {
			trf7970a_send_err_upstream(trf, -EIO);
		}
		break;
	case TRF7970A_ST_WAIT_TO_ISSUE_EOF:
		if (status != TRF7970A_IRQ_STATUS_TX)
			trf7970a_send_err_upstream(trf, -EIO);
		break;
	default:
		dev_err(trf->dev, "%s - Driver in invalid state: %d\n",
				__func__, trf->state);
	}

	mutex_unlock(&trf->lock);
	return IRQ_HANDLED;
}

static void trf7970a_issue_eof(struct trf7970a *trf)
{
	int ret;

	dev_dbg(trf->dev, "Issuing EOF\n");

	ret = trf7970a_cmd(trf, TRF7970A_CMD_FIFO_RESET);
	if (ret)
		trf7970a_send_err_upstream(trf, ret);

	ret = trf7970a_cmd(trf, TRF7970A_CMD_EOF);
	if (ret)
		trf7970a_send_err_upstream(trf, ret);

	trf->state = TRF7970A_ST_WAIT_FOR_RX_DATA;

	dev_dbg(trf->dev, "Setting timeout for %d ms, state: %d\n",
			trf->timeout, trf->state);

	schedule_delayed_work(&trf->timeout_work,
			msecs_to_jiffies(trf->timeout));
}

static void trf7970a_timeout_work_handler(struct work_struct *work)
{
	struct trf7970a *trf = container_of(work, struct trf7970a,
			timeout_work.work);

	dev_dbg(trf->dev, "Timeout - state: %d, ignore_timeout: %d\n",
			trf->state, trf->ignore_timeout);

	mutex_lock(&trf->lock);

	if (trf->ignore_timeout)
		trf->ignore_timeout = false;
	else if (trf->state == TRF7970A_ST_WAIT_FOR_RX_DATA_CONT)
		trf7970a_send_upstream(trf); /* No more rx data so send up */
	else if (trf->state == TRF7970A_ST_WAIT_TO_ISSUE_EOF)
		trf7970a_issue_eof(trf);
	else
		trf7970a_send_err_upstream(trf, -ETIMEDOUT);

	mutex_unlock(&trf->lock);
}

static int trf7970a_init(struct trf7970a *trf)
{
	int ret;

	dev_dbg(trf->dev, "Initializing device - state: %d\n", trf->state);

	ret = trf7970a_cmd(trf, TRF7970A_CMD_SOFT_INIT);
	if (ret)
		goto err_out;

	ret = trf7970a_cmd(trf, TRF7970A_CMD_IDLE);
	if (ret)
		goto err_out;

	/* Must clear NFC Target Detection Level reg due to erratum */
	ret = trf7970a_write(trf, TRF7970A_NFC_TARGET_LEVEL, 0);
	if (ret)
		goto err_out;

	ret = trf7970a_write(trf, TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS,
			TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLH_96 |
			TRF7970A_ADJUTABLE_FIFO_IRQ_LEVELS_WLL_32);
	if (ret)
		goto err_out;

	ret = trf7970a_write(trf, TRF7970A_SPECIAL_FCN_REG1, 0);
	if (ret)
		goto err_out;

	trf->special_fcn_reg1 = 0;

	trf->iso_ctrl = 0xff;
	return 0;

err_out:
	dev_dbg(trf->dev, "Couldn't init device: %d\n", ret);
	return ret;
}

static void trf7970a_switch_rf_off(struct trf7970a *trf)
{
	dev_dbg(trf->dev, "Switching rf off\n");

	trf->chip_status_ctrl &= ~TRF7970A_CHIP_STATUS_RF_ON;

	trf7970a_write(trf, TRF7970A_CHIP_STATUS_CTRL, trf->chip_status_ctrl);

	trf->aborting = false;
	trf->state = TRF7970A_ST_OFF;

	pm_runtime_mark_last_busy(trf->dev);
	pm_runtime_put_autosuspend(trf->dev);
}

static void trf7970a_switch_rf_on(struct trf7970a *trf)
{
	dev_dbg(trf->dev, "Switching rf on\n");

	pm_runtime_get_sync(trf->dev);

	trf->state = TRF7970A_ST_IDLE;
}

static int trf7970a_switch_rf(struct nfc_digital_dev *ddev, bool on)
{
	struct trf7970a *trf = nfc_digital_get_drvdata(ddev);

	dev_dbg(trf->dev, "Switching RF - state: %d, on: %d\n", trf->state, on);

	mutex_lock(&trf->lock);

	if (on) {
		switch (trf->state) {
		case TRF7970A_ST_OFF:
			trf7970a_switch_rf_on(trf);
			break;
		case TRF7970A_ST_IDLE:
		case TRF7970A_ST_IDLE_RX_BLOCKED:
			break;
		default:
			dev_err(trf->dev, "%s - Invalid request: %d %d\n",
					__func__, trf->state, on);
			trf7970a_switch_rf_off(trf);
		}
	} else {
		switch (trf->state) {
		case TRF7970A_ST_OFF:
			break;
		default:
			dev_err(trf->dev, "%s - Invalid request: %d %d\n",
					__func__, trf->state, on);
			/* FALLTHROUGH */
		case TRF7970A_ST_IDLE:
		case TRF7970A_ST_IDLE_RX_BLOCKED:
			trf7970a_switch_rf_off(trf);
		}
	}

	mutex_unlock(&trf->lock);
	return 0;
}

static int trf7970a_config_rf_tech(struct trf7970a *trf, int tech)
{
	int ret = 0;

	dev_dbg(trf->dev, "rf technology: %d\n", tech);

	switch (tech) {
	case NFC_DIGITAL_RF_TECH_106A:
		trf->iso_ctrl_tech = TRF7970A_ISO_CTRL_14443A_106;
		trf->modulator_sys_clk_ctrl = TRF7970A_MODULATOR_DEPTH_OOK;
		break;
	case NFC_DIGITAL_RF_TECH_106B:
		trf->iso_ctrl_tech = TRF7970A_ISO_CTRL_14443B_106;
		trf->modulator_sys_clk_ctrl = TRF7970A_MODULATOR_DEPTH_ASK10;
		break;
	case NFC_DIGITAL_RF_TECH_212F:
		trf->iso_ctrl_tech = TRF7970A_ISO_CTRL_FELICA_212;
		trf->modulator_sys_clk_ctrl = TRF7970A_MODULATOR_DEPTH_ASK10;
		break;
	case NFC_DIGITAL_RF_TECH_424F:
		trf->iso_ctrl_tech = TRF7970A_ISO_CTRL_FELICA_424;
		trf->modulator_sys_clk_ctrl = TRF7970A_MODULATOR_DEPTH_ASK10;
		break;
	case NFC_DIGITAL_RF_TECH_ISO15693:
		trf->iso_ctrl_tech = TRF7970A_ISO_CTRL_15693_SGL_1OF4_2648;
		trf->modulator_sys_clk_ctrl = TRF7970A_MODULATOR_DEPTH_OOK;
		break;
	default:
		dev_dbg(trf->dev, "Unsupported rf technology: %d\n", tech);
		return -EINVAL;
	}

	trf->technology = tech;

	return ret;
}

static int trf7970a_config_framing(struct trf7970a *trf, int framing)
{
	u8 iso_ctrl = trf->iso_ctrl_tech;
	int ret;

	dev_dbg(trf->dev, "framing: %d\n", framing);

	switch (framing) {
	case NFC_DIGITAL_FRAMING_NFCA_SHORT:
	case NFC_DIGITAL_FRAMING_NFCA_STANDARD:
		trf->tx_cmd = TRF7970A_CMD_TRANSMIT_NO_CRC;
		iso_ctrl |= TRF7970A_ISO_CTRL_RX_CRC_N;
		break;
	case NFC_DIGITAL_FRAMING_NFCA_STANDARD_WITH_CRC_A:
	case NFC_DIGITAL_FRAMING_NFCA_T4T:
	case NFC_DIGITAL_FRAMING_NFCB:
	case NFC_DIGITAL_FRAMING_NFCB_T4T:
	case NFC_DIGITAL_FRAMING_NFCF:
	case NFC_DIGITAL_FRAMING_NFCF_T3T:
	case NFC_DIGITAL_FRAMING_ISO15693_INVENTORY:
	case NFC_DIGITAL_FRAMING_ISO15693_T5T:
		trf->tx_cmd = TRF7970A_CMD_TRANSMIT;
		iso_ctrl &= ~TRF7970A_ISO_CTRL_RX_CRC_N;
		break;
	case NFC_DIGITAL_FRAMING_NFCA_T2T:
		trf->tx_cmd = TRF7970A_CMD_TRANSMIT;
		iso_ctrl |= TRF7970A_ISO_CTRL_RX_CRC_N;
		break;
	default:
		dev_dbg(trf->dev, "Unsupported Framing: %d\n", framing);
		return -EINVAL;
	}

	trf->framing = framing;

	if (iso_ctrl != trf->iso_ctrl) {
		ret = trf7970a_write(trf, TRF7970A_ISO_CTRL, iso_ctrl);
		if (ret)
			return ret;

		trf->iso_ctrl = iso_ctrl;

		ret = trf7970a_write(trf, TRF7970A_MODULATOR_SYS_CLK_CTRL,
				trf->modulator_sys_clk_ctrl);
		if (ret)
			return ret;
	}

	if (!(trf->chip_status_ctrl & TRF7970A_CHIP_STATUS_RF_ON)) {
		ret = trf7970a_write(trf, TRF7970A_CHIP_STATUS_CTRL,
				trf->chip_status_ctrl |
					TRF7970A_CHIP_STATUS_RF_ON);
		if (ret)
			return ret;

		trf->chip_status_ctrl |= TRF7970A_CHIP_STATUS_RF_ON;

		usleep_range(5000, 6000);
	}

	return 0;
}

static int trf7970a_in_configure_hw(struct nfc_digital_dev *ddev, int type,
		int param)
{
	struct trf7970a *trf = nfc_digital_get_drvdata(ddev);
	int ret;

	dev_dbg(trf->dev, "Configure hw - type: %d, param: %d\n", type, param);

	mutex_lock(&trf->lock);

	if (trf->state == TRF7970A_ST_OFF)
		trf7970a_switch_rf_on(trf);

	switch (type) {
	case NFC_DIGITAL_CONFIG_RF_TECH:
		ret = trf7970a_config_rf_tech(trf, param);
		break;
	case NFC_DIGITAL_CONFIG_FRAMING:
		ret = trf7970a_config_framing(trf, param);
		break;
	default:
		dev_dbg(trf->dev, "Unknown type: %d\n", type);
		ret = -EINVAL;
	}

	mutex_unlock(&trf->lock);
	return ret;
}

static int trf7970a_is_iso15693_write_or_lock(u8 cmd)
{
	switch (cmd) {
	case ISO15693_CMD_WRITE_SINGLE_BLOCK:
	case ISO15693_CMD_LOCK_BLOCK:
	case ISO15693_CMD_WRITE_MULTIPLE_BLOCK:
	case ISO15693_CMD_WRITE_AFI:
	case ISO15693_CMD_LOCK_AFI:
	case ISO15693_CMD_WRITE_DSFID:
	case ISO15693_CMD_LOCK_DSFID:
		return 1;
		break;
	default:
		return 0;
	}
}

static int trf7970a_per_cmd_config(struct trf7970a *trf, struct sk_buff *skb)
{
	u8 *req = skb->data;
	u8 special_fcn_reg1, iso_ctrl;
	int ret;

	trf->issue_eof = false;

	/* When issuing Type 2 read command, make sure the '4_bit_RX' bit in
	 * special functions register 1 is cleared; otherwise, its a write or
	 * sector select command and '4_bit_RX' must be set.
	 *
	 * When issuing an ISO 15693 command, inspect the flags byte to see
	 * what speed to use.  Also, remember if the OPTION flag is set on
	 * a Type 5 write or lock command so the driver will know that it
	 * has to send an EOF in order to get a response.
	 */
	if ((trf->technology == NFC_DIGITAL_RF_TECH_106A) &&
			(trf->framing == NFC_DIGITAL_FRAMING_NFCA_T2T)) {
		if (req[0] == NFC_T2T_CMD_READ)
			special_fcn_reg1 = 0;
		else
			special_fcn_reg1 = TRF7970A_SPECIAL_FCN_REG1_4_BIT_RX;

		if (special_fcn_reg1 != trf->special_fcn_reg1) {
			ret = trf7970a_write(trf, TRF7970A_SPECIAL_FCN_REG1,
					special_fcn_reg1);
			if (ret)
				return ret;

			trf->special_fcn_reg1 = special_fcn_reg1;
		}
	} else if (trf->technology == NFC_DIGITAL_RF_TECH_ISO15693) {
		iso_ctrl = trf->iso_ctrl & ~TRF7970A_ISO_CTRL_RFID_SPEED_MASK;

		switch (req[0] & ISO15693_REQ_FLAG_SPEED_MASK) {
		case 0x00:
			iso_ctrl |= TRF7970A_ISO_CTRL_15693_SGL_1OF4_662;
			break;
		case ISO15693_REQ_FLAG_SUB_CARRIER:
			iso_ctrl |= TRF7970A_ISO_CTRL_15693_DBL_1OF4_667a;
			break;
		case ISO15693_REQ_FLAG_DATA_RATE:
			iso_ctrl |= TRF7970A_ISO_CTRL_15693_SGL_1OF4_2648;
			break;
		case (ISO15693_REQ_FLAG_SUB_CARRIER |
				ISO15693_REQ_FLAG_DATA_RATE):
			iso_ctrl |= TRF7970A_ISO_CTRL_15693_DBL_1OF4_2669;
			break;
		}

		if (iso_ctrl != trf->iso_ctrl) {
			ret = trf7970a_write(trf, TRF7970A_ISO_CTRL, iso_ctrl);
			if (ret)
				return ret;

			trf->iso_ctrl = iso_ctrl;
		}

		if ((trf->framing == NFC_DIGITAL_FRAMING_ISO15693_T5T) &&
				trf7970a_is_iso15693_write_or_lock(req[1]) &&
				(req[0] & ISO15693_REQ_FLAG_OPTION))
			trf->issue_eof = true;
	}

	return 0;
}

static int trf7970a_in_send_cmd(struct nfc_digital_dev *ddev,
		struct sk_buff *skb, u16 timeout,
		nfc_digital_cmd_complete_t cb, void *arg)
{
	struct trf7970a *trf = nfc_digital_get_drvdata(ddev);
	char *prefix;
	unsigned int len;
	int ret;

	dev_dbg(trf->dev, "New request - state: %d, timeout: %d ms, len: %d\n",
			trf->state, timeout, skb->len);

	if (skb->len > TRF7970A_TX_MAX)
		return -EINVAL;

	mutex_lock(&trf->lock);

	if ((trf->state != TRF7970A_ST_IDLE) &&
			(trf->state != TRF7970A_ST_IDLE_RX_BLOCKED)) {
		dev_err(trf->dev, "%s - Bogus state: %d\n", __func__,
				trf->state);
		ret = -EIO;
		goto out_err;
	}

	if (trf->aborting) {
		dev_dbg(trf->dev, "Abort process complete\n");
		trf->aborting = false;
		ret = -ECANCELED;
		goto out_err;
	}

	trf->rx_skb = nfc_alloc_recv_skb(TRF7970A_RX_SKB_ALLOC_SIZE,
			GFP_KERNEL);
	if (!trf->rx_skb) {
		dev_dbg(trf->dev, "Can't alloc rx_skb\n");
		ret = -ENOMEM;
		goto out_err;
	}

	if (trf->state == TRF7970A_ST_IDLE_RX_BLOCKED) {
		ret = trf7970a_cmd(trf, TRF7970A_CMD_ENABLE_RX);
		if (ret)
			goto out_err;

		trf->state = TRF7970A_ST_IDLE;
	}

	ret = trf7970a_per_cmd_config(trf, skb);
	if (ret)
		goto out_err;

	trf->ddev = ddev;
	trf->tx_skb = skb;
	trf->cb = cb;
	trf->cb_arg = arg;
	trf->timeout = timeout;
	trf->ignore_timeout = false;

	len = skb->len;
	prefix = skb_push(skb, TRF7970A_TX_SKB_HEADROOM);

	/* TX data must be prefixed with a FIFO reset cmd, a cmd that depends
	 * on what the current framing is, the address of the TX length byte 1
	 * register (0x1d), and the 2 byte length of the data to be transmitted.
	 */
	prefix[0] = TRF7970A_CMD_BIT_CTRL |
			TRF7970A_CMD_BIT_OPCODE(TRF7970A_CMD_FIFO_RESET);
	prefix[1] = TRF7970A_CMD_BIT_CTRL |
			TRF7970A_CMD_BIT_OPCODE(trf->tx_cmd);
	prefix[2] = TRF7970A_CMD_BIT_CONTINUOUS | TRF7970A_TX_LENGTH_BYTE1;

	if (trf->framing == NFC_DIGITAL_FRAMING_NFCA_SHORT) {
		prefix[3] = 0x00;
		prefix[4] = 0x0f; /* 7 bits */
	} else {
		prefix[3] = (len & 0xf00) >> 4;
		prefix[3] |= ((len & 0xf0) >> 4);
		prefix[4] = ((len & 0x0f) << 4);
	}

	len = min_t(int, skb->len, TRF7970A_FIFO_SIZE);

	usleep_range(1000, 2000);

	ret = trf7970a_transmit(trf, skb, len);
	if (ret) {
		kfree_skb(trf->rx_skb);
		trf->rx_skb = NULL;
	}

out_err:
	mutex_unlock(&trf->lock);
	return ret;
}

static int trf7970a_tg_configure_hw(struct nfc_digital_dev *ddev,
		int type, int param)
{
	struct trf7970a *trf = nfc_digital_get_drvdata(ddev);

	dev_dbg(trf->dev, "Unsupported interface\n");

	return -EINVAL;
}

static int trf7970a_tg_send_cmd(struct nfc_digital_dev *ddev,
		struct sk_buff *skb, u16 timeout,
		nfc_digital_cmd_complete_t cb, void *arg)
{
	struct trf7970a *trf = nfc_digital_get_drvdata(ddev);

	dev_dbg(trf->dev, "Unsupported interface\n");

	return -EINVAL;
}

static int trf7970a_tg_listen(struct nfc_digital_dev *ddev,
		u16 timeout, nfc_digital_cmd_complete_t cb, void *arg)
{
	struct trf7970a *trf = nfc_digital_get_drvdata(ddev);

	dev_dbg(trf->dev, "Unsupported interface\n");

	return -EINVAL;
}

static int trf7970a_tg_listen_mdaa(struct nfc_digital_dev *ddev,
		struct digital_tg_mdaa_params *mdaa_params,
		u16 timeout, nfc_digital_cmd_complete_t cb, void *arg)
{
	struct trf7970a *trf = nfc_digital_get_drvdata(ddev);

	dev_dbg(trf->dev, "Unsupported interface\n");

	return -EINVAL;
}

static void trf7970a_abort_cmd(struct nfc_digital_dev *ddev)
{
	struct trf7970a *trf = nfc_digital_get_drvdata(ddev);

	dev_dbg(trf->dev, "Abort process initiated\n");

	mutex_lock(&trf->lock);

	switch (trf->state) {
	case TRF7970A_ST_WAIT_FOR_TX_FIFO:
	case TRF7970A_ST_WAIT_FOR_RX_DATA:
	case TRF7970A_ST_WAIT_FOR_RX_DATA_CONT:
	case TRF7970A_ST_WAIT_TO_ISSUE_EOF:
		trf->aborting = true;
		break;
	default:
		break;
	}

	mutex_unlock(&trf->lock);
}

static struct nfc_digital_ops trf7970a_nfc_ops = {
	.in_configure_hw	= trf7970a_in_configure_hw,
	.in_send_cmd		= trf7970a_in_send_cmd,
	.tg_configure_hw	= trf7970a_tg_configure_hw,
	.tg_send_cmd		= trf7970a_tg_send_cmd,
	.tg_listen		= trf7970a_tg_listen,
	.tg_listen_mdaa		= trf7970a_tg_listen_mdaa,
	.switch_rf		= trf7970a_switch_rf,
	.abort_cmd		= trf7970a_abort_cmd,
};

static int trf7970a_get_autosuspend_delay(struct device_node *np)
{
	int autosuspend_delay, ret;

	ret = of_property_read_u32(np, "autosuspend-delay", &autosuspend_delay);
	if (ret)
		autosuspend_delay = TRF7970A_AUTOSUSPEND_DELAY;

	of_node_put(np);

	return autosuspend_delay;
}

static int trf7970a_probe(struct spi_device *spi)
{
	struct device_node *np = spi->dev.of_node;
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct trf7970a *trf;
	int uvolts, autosuspend_delay, ret;

	if (!np) {
		dev_err(&spi->dev, "No Device Tree entry\n");
		return -EINVAL;
	}

	trf = devm_kzalloc(&spi->dev, sizeof(*trf), GFP_KERNEL);
	if (!trf)
		return -ENOMEM;

	trf->state = TRF7970A_ST_OFF;
	trf->dev = &spi->dev;
	trf->spi = spi;
	trf->quirks = id->driver_data;

	spi->mode = SPI_MODE_1;
	spi->bits_per_word = 8;

	/* There are two enable pins - both must be present */
	trf->en_gpio = of_get_named_gpio(np, "ti,enable-gpios", 0);
	if (!gpio_is_valid(trf->en_gpio)) {
		dev_err(trf->dev, "No EN GPIO property\n");
		return trf->en_gpio;
	}

	ret = devm_gpio_request_one(trf->dev, trf->en_gpio,
			GPIOF_DIR_OUT | GPIOF_INIT_LOW, "EN");
	if (ret) {
		dev_err(trf->dev, "Can't request EN GPIO: %d\n", ret);
		return ret;
	}

	trf->en2_gpio = of_get_named_gpio(np, "ti,enable-gpios", 1);
	if (!gpio_is_valid(trf->en2_gpio)) {
		dev_err(trf->dev, "No EN2 GPIO property\n");
		return trf->en2_gpio;
	}

	ret = devm_gpio_request_one(trf->dev, trf->en2_gpio,
			GPIOF_DIR_OUT | GPIOF_INIT_LOW, "EN2");
	if (ret) {
		dev_err(trf->dev, "Can't request EN2 GPIO: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(trf->dev, spi->irq, NULL,
			trf7970a_irq, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			"trf7970a", trf);
	if (ret) {
		dev_err(trf->dev, "Can't request IRQ#%d: %d\n", spi->irq, ret);
		return ret;
	}

	mutex_init(&trf->lock);
	INIT_DELAYED_WORK(&trf->timeout_work, trf7970a_timeout_work_handler);

	trf->regulator = devm_regulator_get(&spi->dev, "vin");
	if (IS_ERR(trf->regulator)) {
		ret = PTR_ERR(trf->regulator);
		dev_err(trf->dev, "Can't get VIN regulator: %d\n", ret);
		goto err_destroy_lock;
	}

	ret = regulator_enable(trf->regulator);
	if (ret) {
		dev_err(trf->dev, "Can't enable VIN: %d\n", ret);
		goto err_destroy_lock;
	}

	uvolts = regulator_get_voltage(trf->regulator);

	if (uvolts > 4000000)
		trf->chip_status_ctrl = TRF7970A_CHIP_STATUS_VRS5_3;

	trf->ddev = nfc_digital_allocate_device(&trf7970a_nfc_ops,
			TRF7970A_SUPPORTED_PROTOCOLS,
			NFC_DIGITAL_DRV_CAPS_IN_CRC, TRF7970A_TX_SKB_HEADROOM,
			0);
	if (!trf->ddev) {
		dev_err(trf->dev, "Can't allocate NFC digital device\n");
		ret = -ENOMEM;
		goto err_disable_regulator;
	}

	nfc_digital_set_parent_dev(trf->ddev, trf->dev);
	nfc_digital_set_drvdata(trf->ddev, trf);
	spi_set_drvdata(spi, trf);

	autosuspend_delay = trf7970a_get_autosuspend_delay(np);

	pm_runtime_set_autosuspend_delay(trf->dev, autosuspend_delay);
	pm_runtime_use_autosuspend(trf->dev);
	pm_runtime_enable(trf->dev);

	ret = nfc_digital_register_device(trf->ddev);
	if (ret) {
		dev_err(trf->dev, "Can't register NFC digital device: %d\n",
				ret);
		goto err_free_ddev;
	}

	return 0;

err_free_ddev:
	pm_runtime_disable(trf->dev);
	nfc_digital_free_device(trf->ddev);
err_disable_regulator:
	regulator_disable(trf->regulator);
err_destroy_lock:
	mutex_destroy(&trf->lock);
	return ret;
}

static int trf7970a_remove(struct spi_device *spi)
{
	struct trf7970a *trf = spi_get_drvdata(spi);

	mutex_lock(&trf->lock);

	switch (trf->state) {
	case TRF7970A_ST_WAIT_FOR_TX_FIFO:
	case TRF7970A_ST_WAIT_FOR_RX_DATA:
	case TRF7970A_ST_WAIT_FOR_RX_DATA_CONT:
	case TRF7970A_ST_WAIT_TO_ISSUE_EOF:
		trf7970a_send_err_upstream(trf, -ECANCELED);
		/* FALLTHROUGH */
	case TRF7970A_ST_IDLE:
	case TRF7970A_ST_IDLE_RX_BLOCKED:
		pm_runtime_put_sync(trf->dev);
		break;
	default:
		break;
	}

	mutex_unlock(&trf->lock);

	pm_runtime_disable(trf->dev);

	nfc_digital_unregister_device(trf->ddev);
	nfc_digital_free_device(trf->ddev);

	regulator_disable(trf->regulator);

	mutex_destroy(&trf->lock);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int trf7970a_pm_runtime_suspend(struct device *dev)
{
	struct spi_device *spi = container_of(dev, struct spi_device, dev);
	struct trf7970a *trf = spi_get_drvdata(spi);
	int ret;

	dev_dbg(dev, "Runtime suspend\n");

	if (trf->state != TRF7970A_ST_OFF) {
		dev_dbg(dev, "Can't suspend - not in OFF state (%d)\n",
				trf->state);
		return -EBUSY;
	}

	gpio_set_value(trf->en_gpio, 0);
	gpio_set_value(trf->en2_gpio, 0);

	ret = regulator_disable(trf->regulator);
	if (ret)
		dev_err(dev, "%s - Can't disable VIN: %d\n", __func__, ret);

	return ret;
}

static int trf7970a_pm_runtime_resume(struct device *dev)
{
	struct spi_device *spi = container_of(dev, struct spi_device, dev);
	struct trf7970a *trf = spi_get_drvdata(spi);
	int ret;

	dev_dbg(dev, "Runtime resume\n");

	ret = regulator_enable(trf->regulator);
	if (ret) {
		dev_err(dev, "%s - Can't enable VIN: %d\n", __func__, ret);
		return ret;
	}

	usleep_range(5000, 6000);

	gpio_set_value(trf->en2_gpio, 1);
	usleep_range(1000, 2000);
	gpio_set_value(trf->en_gpio, 1);

	usleep_range(20000, 21000);

	ret = trf7970a_init(trf);
	if (ret) {
		dev_err(dev, "%s - Can't initialize: %d\n", __func__, ret);
		return ret;
	}

	pm_runtime_mark_last_busy(dev);

	return 0;
}
#endif

static const struct dev_pm_ops trf7970a_pm_ops = {
	SET_RUNTIME_PM_OPS(trf7970a_pm_runtime_suspend,
			trf7970a_pm_runtime_resume, NULL)
};

static const struct spi_device_id trf7970a_id_table[] = {
	{ "trf7970a", TRF7970A_QUIRK_IRQ_STATUS_READ_ERRATA },
	{ }
};
MODULE_DEVICE_TABLE(spi, trf7970a_id_table);

static struct spi_driver trf7970a_spi_driver = {
	.probe		= trf7970a_probe,
	.remove		= trf7970a_remove,
	.id_table	= trf7970a_id_table,
	.driver		= {
		.name	= "trf7970a",
		.owner	= THIS_MODULE,
		.pm	= &trf7970a_pm_ops,
	},
};

module_spi_driver(trf7970a_spi_driver);

MODULE_AUTHOR("Mark A. Greer <mgreer@animalcreek.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI trf7970a RFID/NFC Transceiver Driver");
