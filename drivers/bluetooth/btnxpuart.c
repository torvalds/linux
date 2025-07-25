// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  NXP Bluetooth driver
 *  Copyright 2023-2025 NXP
 */

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/skbuff.h>
#include <linux/unaligned.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <linux/crc8.h>
#include <linux/crc32.h>
#include <linux/string_helpers.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "h4_recv.h"

#define MANUFACTURER_NXP		37

#define BTNXPUART_TX_STATE_ACTIVE	1
#define BTNXPUART_FW_DOWNLOADING	2
#define BTNXPUART_CHECK_BOOT_SIGNATURE	3
#define BTNXPUART_SERDEV_OPEN		4
#define BTNXPUART_IR_IN_PROGRESS	5
#define BTNXPUART_FW_DOWNLOAD_ABORT	6
#define BTNXPUART_FW_DUMP_IN_PROGRESS	7

/* NXP HW err codes */
#define BTNXPUART_IR_HW_ERR		0xb0

#define FIRMWARE_W8987		"uart8987_bt.bin"
#define FIRMWARE_W8987_OLD	"uartuart8987_bt.bin"
#define FIRMWARE_W8997		"uart8997_bt_v4.bin"
#define FIRMWARE_W8997_OLD	"uartuart8997_bt_v4.bin"
#define FIRMWARE_W9098		"uart9098_bt_v1.bin"
#define FIRMWARE_W9098_OLD	"uartuart9098_bt_v1.bin"
#define FIRMWARE_IW416		"uartiw416_bt.bin"
#define FIRMWARE_IW416_OLD	"uartiw416_bt_v0.bin"
#define FIRMWARE_IW612		"uartspi_n61x_v1.bin.se"
#define FIRMWARE_IW610		"uartspi_iw610.bin"
#define FIRMWARE_SECURE_IW610	"uartspi_iw610.bin.se"
#define FIRMWARE_IW624		"uartiw624_bt.bin"
#define FIRMWARE_SECURE_IW624	"uartiw624_bt.bin.se"
#define FIRMWARE_AW693		"uartaw693_bt.bin"
#define FIRMWARE_SECURE_AW693	"uartaw693_bt.bin.se"
#define FIRMWARE_AW693_A1		"uartaw693_bt_v1.bin"
#define FIRMWARE_SECURE_AW693_A1	"uartaw693_bt_v1.bin.se"
#define FIRMWARE_HELPER		"helper_uart_3000000.bin"

#define CHIP_ID_W9098		0x5c03
#define CHIP_ID_IW416		0x7201
#define CHIP_ID_IW612		0x7601
#define CHIP_ID_IW624a		0x8000
#define CHIP_ID_IW624c		0x8001
#define CHIP_ID_AW693a0		0x8200
#define CHIP_ID_AW693a1		0x8201
#define CHIP_ID_IW610a0		0x8800
#define CHIP_ID_IW610a1		0x8801

#define FW_SECURE_MASK		0xc0
#define FW_OPEN			0x00
#define FW_AUTH_ILLEGAL		0x40
#define FW_AUTH_PLAIN		0x80
#define FW_AUTH_ENC		0xc0

#define HCI_NXP_PRI_BAUDRATE	115200
#define HCI_NXP_SEC_BAUDRATE_3M	3000000
#define HCI_NXP_SEC_BAUDRATE_4M	4000000

#define MAX_FW_FILE_NAME_LEN    50

/* Default ps timeout period in milliseconds */
#define PS_DEFAULT_TIMEOUT_PERIOD_MS     2000

/* wakeup methods */
#define WAKEUP_METHOD_DTR       0
#define WAKEUP_METHOD_BREAK     1
#define WAKEUP_METHOD_EXT_BREAK 2
#define WAKEUP_METHOD_RTS       3
#define WAKEUP_METHOD_GPIO      4
#define WAKEUP_METHOD_INVALID   0xff

/* power save mode status */
#define PS_MODE_DISABLE         0
#define PS_MODE_ENABLE          1

/* Power Save Commands to ps_work_func  */
#define PS_CMD_EXIT_PS          1
#define PS_CMD_ENTER_PS         2

/* power save state */
#define PS_STATE_AWAKE          0
#define PS_STATE_SLEEP          1

/* NXP Vendor Commands. Refer user manual UM11628 on nxp.com */
/* Set custom BD Address */
#define HCI_NXP_SET_BD_ADDR	0xfc22
/* Set Auto-Sleep mode */
#define HCI_NXP_AUTO_SLEEP_MODE	0xfc23
/* Set Wakeup method */
#define HCI_NXP_WAKEUP_METHOD	0xfc53
/* Set operational baudrate */
#define HCI_NXP_SET_OPER_SPEED	0xfc09
/* Independent Reset (Soft Reset) */
#define HCI_NXP_IND_RESET	0xfcfc
/* Bluetooth vendor command: Trigger FW dump */
#define HCI_NXP_TRIGGER_DUMP	0xfe91

/* Bluetooth Power State : Vendor cmd params */
#define BT_PS_ENABLE			0x02
#define BT_PS_DISABLE			0x03

/* Bluetooth Host Wakeup Methods */
#define BT_HOST_WAKEUP_METHOD_NONE      0x00
#define BT_HOST_WAKEUP_METHOD_DTR       0x01
#define BT_HOST_WAKEUP_METHOD_BREAK     0x02
#define BT_HOST_WAKEUP_METHOD_GPIO      0x03

/* Bluetooth Chip Wakeup Methods */
#define BT_CTRL_WAKEUP_METHOD_DSR       0x00
#define BT_CTRL_WAKEUP_METHOD_BREAK     0x01
#define BT_CTRL_WAKEUP_METHOD_GPIO      0x02
#define BT_CTRL_WAKEUP_METHOD_EXT_BREAK 0x04
#define BT_CTRL_WAKEUP_METHOD_RTS       0x05

struct ps_data {
	u8    target_ps_mode;	/* ps mode to be set */
	u8    cur_psmode;	/* current ps_mode */
	u8    ps_state;		/* controller's power save state */
	u8    ps_cmd;
	u8    h2c_wakeupmode;
	u8    cur_h2c_wakeupmode;
	u8    c2h_wakeupmode;
	u8    c2h_wakeup_gpio;
	u8    h2c_wakeup_gpio;
	bool  driver_sent_cmd;
	u16   h2c_ps_interval;
	u16   c2h_ps_interval;
	bool  wakeup_source;
	struct gpio_desc *h2c_ps_gpio;
	s32 irq_handler;
	struct hci_dev *hdev;
	struct work_struct work;
	struct timer_list ps_timer;
	struct mutex ps_lock;
};

struct wakeup_cmd_payload {
	u8 c2h_wakeupmode;
	u8 c2h_wakeup_gpio;
	u8 h2c_wakeupmode;
	u8 h2c_wakeup_gpio;
} __packed;

struct psmode_cmd_payload {
	u8 ps_cmd;
	__le16 c2h_ps_interval;
} __packed;

struct btnxpuart_data {
	const char *helper_fw_name;
	const char *fw_name;
	const char *fw_name_old;
};

enum bootloader_param_change {
	not_changed,
	cmd_sent,
	changed
};

struct btnxpuart_dev {
	struct hci_dev *hdev;
	struct serdev_device *serdev;

	struct work_struct tx_work;
	unsigned long tx_state;
	struct sk_buff_head txq;
	struct sk_buff *rx_skb;

	const struct firmware *fw;
	u8 fw_name[MAX_FW_FILE_NAME_LEN];
	u32 fw_dnld_v1_offset;
	u32 fw_v1_sent_bytes;
	u32 fw_dnld_v3_offset;
	u32 fw_v3_offset_correction;
	u32 fw_v3_prev_sent;
	u32 fw_v1_expected_len;
	u32 boot_reg_offset;
	wait_queue_head_t fw_dnld_done_wait_q;
	wait_queue_head_t check_boot_sign_wait_q;

	u32 new_baudrate;
	u32 current_baudrate;
	u32 fw_init_baudrate;
	u32 secondary_baudrate;
	enum bootloader_param_change timeout_changed;
	enum bootloader_param_change baudrate_changed;
	bool helper_downloaded;

	struct ps_data psdata;
	struct btnxpuart_data *nxp_data;
	struct reset_control *pdn;
};

#define NXP_V1_FW_REQ_PKT	0xa5
#define NXP_V1_CHIP_VER_PKT	0xaa
#define NXP_V3_FW_REQ_PKT	0xa7
#define NXP_V3_CHIP_VER_PKT	0xab

#define NXP_ACK_V1		0x5a
#define NXP_NAK_V1		0xbf
#define NXP_ACK_V3		0x7a
#define NXP_NAK_V3		0x7b
#define NXP_CRC_ERROR_V3	0x7c

/* Bootloader signature error codes: Refer AN12820 from nxp.com */
#define NXP_CRC_RX_ERROR	BIT(0)	/* CRC error in previous packet */
#define NXP_ACK_RX_TIMEOUT	BIT(2)	/* ACK not received from host */
#define NXP_HDR_RX_TIMEOUT	BIT(3)	/* FW Header chunk not received */
#define NXP_DATA_RX_TIMEOUT	BIT(4)	/* FW Data chunk not received */

#define HDR_LEN			16

#define NXP_RECV_CHIP_VER_V1 \
	.type = NXP_V1_CHIP_VER_PKT, \
	.hlen = 4, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = 4

#define NXP_RECV_FW_REQ_V1 \
	.type = NXP_V1_FW_REQ_PKT, \
	.hlen = 4, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = 4

#define NXP_RECV_CHIP_VER_V3 \
	.type = NXP_V3_CHIP_VER_PKT, \
	.hlen = 4, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = 4

#define NXP_RECV_FW_REQ_V3 \
	.type = NXP_V3_FW_REQ_PKT, \
	.hlen = 9, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = 9

struct v1_data_req {
	__le16 len;
	__le16 len_comp;
} __packed;

struct v1_start_ind {
	__le16 chip_id;
	__le16 chip_id_comp;
} __packed;

struct v3_data_req {
	__le16 len;
	__le32 offset;
	__le16 error;
	u8 crc;
} __packed;

struct v3_start_ind {
	__le16 chip_id;
	u8 loader_ver;
	u8 crc;
} __packed;

/* UART register addresses of BT chip */
#define CLKDIVADDR	0x7f00008f
#define UARTDIVADDR	0x7f000090
#define UARTMCRADDR	0x7f000091
#define UARTREINITADDR	0x7f000092
#define UARTICRADDR	0x7f000093
#define UARTFCRADDR	0x7f000094

#define MCR		0x00000022
#define INIT		0x00000001
#define ICR		0x000000c7
#define FCR		0x000000c7

#define POLYNOMIAL8	0x07

struct uart_reg {
	__le32 address;
	__le32 value;
} __packed;

struct uart_config {
	struct uart_reg clkdiv;
	struct uart_reg uartdiv;
	struct uart_reg mcr;
	struct uart_reg re_init;
	struct uart_reg icr;
	struct uart_reg fcr;
	__be32 crc;
} __packed;

struct nxp_bootloader_cmd {
	__le32 header;
	__le32 arg;
	__le32 payload_len;
	__be32 crc;
} __packed;

struct nxp_v3_rx_timeout_nak {
	u8 nak;
	__le32 offset;
	u8 crc;
} __packed;

union nxp_v3_rx_timeout_nak_u {
	struct nxp_v3_rx_timeout_nak pkt;
	u8 buf[6];
};

struct nxp_v3_crc_nak {
	u8 nak;
	u8 crc;
} __packed;

union nxp_v3_crc_nak_u {
	struct nxp_v3_crc_nak pkt;
	u8 buf[2];
};

/* FW dump */
#define NXP_FW_DUMP_SIZE	(1024 * 1000)

struct nxp_fw_dump_hdr {
	__le16 seq_num;
	__le16 reserved;
	__le16 buf_type;
	__le16 buf_len;
};

union nxp_set_bd_addr_payload {
	struct {
		u8 param_id;
		u8 param_len;
		u8 param[6];
	} __packed data;
	u8 buf[8];
};

static u8 crc8_table[CRC8_TABLE_SIZE];

/* Default configurations */
#define DEFAULT_H2C_WAKEUP_MODE	WAKEUP_METHOD_BREAK
#define DEFAULT_PS_MODE		PS_MODE_ENABLE
#define FW_INIT_BAUDRATE	HCI_NXP_PRI_BAUDRATE

static struct sk_buff *nxp_drv_send_cmd(struct hci_dev *hdev, u16 opcode,
					u32 plen,
					void *param,
					bool resp)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = &nxpdev->psdata;
	struct sk_buff *skb = NULL;

	/* set flag to prevent nxp_enqueue from parsing values from this command and
	 * calling hci_cmd_sync_queue() again.
	 */
	psdata->driver_sent_cmd = true;
	if (resp) {
		skb = __hci_cmd_sync(hdev, opcode, plen, param, HCI_CMD_TIMEOUT);
	} else {
		__hci_cmd_send(hdev, opcode, plen, param);
		/* Allow command to be sent before tx_work is cancelled
		 * by btnxpuart_flush()
		 */
		msleep(20);
	}
	psdata->driver_sent_cmd = false;

	return skb;
}

static void btnxpuart_tx_wakeup(struct btnxpuart_dev *nxpdev)
{
	if (schedule_work(&nxpdev->tx_work))
		set_bit(BTNXPUART_TX_STATE_ACTIVE, &nxpdev->tx_state);
}

/* NXP Power Save Feature */
static void ps_start_timer(struct btnxpuart_dev *nxpdev)
{
	struct ps_data *psdata = &nxpdev->psdata;

	if (!psdata)
		return;

	if (psdata->cur_psmode == PS_MODE_ENABLE)
		mod_timer(&psdata->ps_timer, jiffies + msecs_to_jiffies(psdata->h2c_ps_interval));

	if (psdata->ps_state == PS_STATE_AWAKE && psdata->ps_cmd == PS_CMD_ENTER_PS)
		cancel_work_sync(&psdata->work);
}

static void ps_cancel_timer(struct btnxpuart_dev *nxpdev)
{
	struct ps_data *psdata = &nxpdev->psdata;

	flush_work(&psdata->work);
	timer_shutdown_sync(&psdata->ps_timer);
}

static void ps_control(struct hci_dev *hdev, u8 ps_state)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = &nxpdev->psdata;
	int status = 0;

	if (psdata->ps_state == ps_state ||
	    !test_bit(BTNXPUART_SERDEV_OPEN, &nxpdev->tx_state))
		return;

	mutex_lock(&psdata->ps_lock);
	switch (psdata->cur_h2c_wakeupmode) {
	case WAKEUP_METHOD_GPIO:
		if (ps_state == PS_STATE_AWAKE)
			gpiod_set_value_cansleep(psdata->h2c_ps_gpio, 0);
		else
			gpiod_set_value_cansleep(psdata->h2c_ps_gpio, 1);
		bt_dev_dbg(hdev, "Set h2c_ps_gpio: %s",
			   str_high_low(ps_state == PS_STATE_SLEEP));
		break;
	case WAKEUP_METHOD_DTR:
		if (ps_state == PS_STATE_AWAKE)
			status = serdev_device_set_tiocm(nxpdev->serdev, TIOCM_DTR, 0);
		else
			status = serdev_device_set_tiocm(nxpdev->serdev, 0, TIOCM_DTR);
		break;
	case WAKEUP_METHOD_BREAK:
	default:
		if (ps_state == PS_STATE_AWAKE)
			status = serdev_device_break_ctl(nxpdev->serdev, 0);
		else
			status = serdev_device_break_ctl(nxpdev->serdev, -1);
		msleep(20); /* Allow chip to detect UART-break and enter sleep */
		bt_dev_dbg(hdev, "Set UART break: %s, status=%d",
			   str_on_off(ps_state == PS_STATE_SLEEP), status);
		break;
	}
	if (!status)
		psdata->ps_state = ps_state;
	mutex_unlock(&psdata->ps_lock);

	if (ps_state == PS_STATE_AWAKE)
		btnxpuart_tx_wakeup(nxpdev);
}

static void ps_work_func(struct work_struct *work)
{
	struct ps_data *data = container_of(work, struct ps_data, work);

	if (data->ps_cmd == PS_CMD_ENTER_PS && data->cur_psmode == PS_MODE_ENABLE)
		ps_control(data->hdev, PS_STATE_SLEEP);
	else if (data->ps_cmd == PS_CMD_EXIT_PS)
		ps_control(data->hdev, PS_STATE_AWAKE);
}

static void ps_timeout_func(struct timer_list *t)
{
	struct ps_data *data = timer_container_of(data, t, ps_timer);
	struct hci_dev *hdev = data->hdev;
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	if (test_bit(BTNXPUART_TX_STATE_ACTIVE, &nxpdev->tx_state)) {
		ps_start_timer(nxpdev);
	} else {
		data->ps_cmd = PS_CMD_ENTER_PS;
		schedule_work(&data->work);
	}
}

static irqreturn_t ps_host_wakeup_irq_handler(int irq, void *priv)
{
	struct btnxpuart_dev *nxpdev = (struct btnxpuart_dev *)priv;

	bt_dev_dbg(nxpdev->hdev, "Host wakeup interrupt");
	return IRQ_HANDLED;
}
static int ps_setup(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct serdev_device *serdev = nxpdev->serdev;
	struct ps_data *psdata = &nxpdev->psdata;
	int ret;

	/* Out-Of-Band Device Wakeup */
	psdata->h2c_ps_gpio = devm_gpiod_get_optional(&serdev->dev, "device-wakeup",
						      GPIOD_OUT_LOW);
	if (IS_ERR(psdata->h2c_ps_gpio)) {
		bt_dev_err(hdev, "Error fetching device-wakeup-gpios: %ld",
			   PTR_ERR(psdata->h2c_ps_gpio));
		return PTR_ERR(psdata->h2c_ps_gpio);
	}

	if (device_property_read_u8(&serdev->dev, "nxp,wakein-pin", &psdata->h2c_wakeup_gpio)) {
		psdata->h2c_wakeup_gpio = 0xff; /* 0xff: use default pin/gpio */
	} else if (!psdata->h2c_ps_gpio) {
		bt_dev_warn(hdev, "nxp,wakein-pin property without device-wakeup-gpios");
		psdata->h2c_wakeup_gpio = 0xff;
	}

	/* Out-Of-Band Host Wakeup */
	if (of_property_read_bool(serdev->dev.of_node, "wakeup-source")) {
		psdata->irq_handler = of_irq_get_byname(serdev->dev.of_node, "wakeup");
		bt_dev_info(nxpdev->hdev, "irq_handler: %d", psdata->irq_handler);
		if (psdata->irq_handler > 0)
			psdata->wakeup_source = true;
	}

	if (device_property_read_u8(&serdev->dev, "nxp,wakeout-pin", &psdata->c2h_wakeup_gpio)) {
		psdata->c2h_wakeup_gpio = 0xff;
		if (psdata->wakeup_source) {
			bt_dev_warn(hdev, "host wakeup interrupt without nxp,wakeout-pin");
			psdata->wakeup_source = false;
		}
	} else if (!psdata->wakeup_source) {
		bt_dev_warn(hdev, "nxp,wakeout-pin property without host wakeup interrupt");
		psdata->c2h_wakeup_gpio = 0xff;
	}

	if (psdata->wakeup_source) {
		ret = devm_request_irq(&serdev->dev, psdata->irq_handler,
					ps_host_wakeup_irq_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
					dev_name(&serdev->dev), nxpdev);
		if (ret)
			bt_dev_info(hdev, "error setting wakeup IRQ handler, ignoring\n");
		disable_irq(psdata->irq_handler);
		device_init_wakeup(&serdev->dev, true);
	}

	psdata->hdev = hdev;
	INIT_WORK(&psdata->work, ps_work_func);
	mutex_init(&psdata->ps_lock);
	timer_setup(&psdata->ps_timer, ps_timeout_func, 0);

	return 0;
}

static bool ps_wakeup(struct btnxpuart_dev *nxpdev)
{
	struct ps_data *psdata = &nxpdev->psdata;
	u8 ps_state;

	mutex_lock(&psdata->ps_lock);
	ps_state = psdata->ps_state;
	mutex_unlock(&psdata->ps_lock);

	if (ps_state != PS_STATE_AWAKE) {
		psdata->ps_cmd = PS_CMD_EXIT_PS;
		schedule_work(&psdata->work);
		return true;
	}
	return false;
}

static void ps_cleanup(struct btnxpuart_dev *nxpdev)
{
	struct ps_data *psdata = &nxpdev->psdata;
	u8 ps_state;

	mutex_lock(&psdata->ps_lock);
	ps_state = psdata->ps_state;
	mutex_unlock(&psdata->ps_lock);

	if (ps_state != PS_STATE_AWAKE)
		ps_control(psdata->hdev, PS_STATE_AWAKE);

	ps_cancel_timer(nxpdev);
	cancel_work_sync(&psdata->work);
	mutex_destroy(&psdata->ps_lock);
}

static int send_ps_cmd(struct hci_dev *hdev, void *data)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = &nxpdev->psdata;
	struct psmode_cmd_payload pcmd;
	struct sk_buff *skb;
	u8 *status;

	if (psdata->target_ps_mode == PS_MODE_ENABLE)
		pcmd.ps_cmd = BT_PS_ENABLE;
	else
		pcmd.ps_cmd = BT_PS_DISABLE;
	pcmd.c2h_ps_interval = __cpu_to_le16(psdata->c2h_ps_interval);

	skb = nxp_drv_send_cmd(hdev, HCI_NXP_AUTO_SLEEP_MODE, sizeof(pcmd),
			       &pcmd, true);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting Power Save mode failed (%ld)", PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	status = skb_pull_data(skb, 1);
	if (status) {
		if (!*status)
			psdata->cur_psmode = psdata->target_ps_mode;
		else
			psdata->target_ps_mode = psdata->cur_psmode;
		if (psdata->cur_psmode == PS_MODE_ENABLE)
			ps_start_timer(nxpdev);
		else
			ps_wakeup(nxpdev);
		bt_dev_dbg(hdev, "Power Save mode response: status=%d, ps_mode=%d",
			   *status, psdata->cur_psmode);
	}
	kfree_skb(skb);

	return 0;
}

static int send_wakeup_method_cmd(struct hci_dev *hdev, void *data)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = &nxpdev->psdata;
	struct wakeup_cmd_payload pcmd;
	struct sk_buff *skb;
	u8 *status;

	pcmd.c2h_wakeupmode = psdata->c2h_wakeupmode;
	pcmd.c2h_wakeup_gpio = psdata->c2h_wakeup_gpio;
	pcmd.h2c_wakeup_gpio = 0xff;
	switch (psdata->h2c_wakeupmode) {
	case WAKEUP_METHOD_GPIO:
		pcmd.h2c_wakeupmode = BT_CTRL_WAKEUP_METHOD_GPIO;
		pcmd.h2c_wakeup_gpio = psdata->h2c_wakeup_gpio;
		break;
	case WAKEUP_METHOD_DTR:
		pcmd.h2c_wakeupmode = BT_CTRL_WAKEUP_METHOD_DSR;
		break;
	case WAKEUP_METHOD_BREAK:
	default:
		pcmd.h2c_wakeupmode = BT_CTRL_WAKEUP_METHOD_BREAK;
		break;
	}

	skb = nxp_drv_send_cmd(hdev, HCI_NXP_WAKEUP_METHOD, sizeof(pcmd),
			       &pcmd, true);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting wake-up method failed (%ld)", PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	status = skb_pull_data(skb, 1);
	if (status) {
		if (*status == 0)
			psdata->cur_h2c_wakeupmode = psdata->h2c_wakeupmode;
		else
			psdata->h2c_wakeupmode = psdata->cur_h2c_wakeupmode;
		bt_dev_dbg(hdev, "Set Wakeup Method response: status=%d, h2c_wakeupmode=%d",
			   *status, psdata->cur_h2c_wakeupmode);
	}
	kfree_skb(skb);

	return 0;
}

static void ps_init(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = &nxpdev->psdata;
	u8 default_h2c_wakeup_mode = DEFAULT_H2C_WAKEUP_MODE;

	serdev_device_set_tiocm(nxpdev->serdev, 0, TIOCM_RTS);
	usleep_range(5000, 10000);
	serdev_device_set_tiocm(nxpdev->serdev, TIOCM_RTS, 0);
	usleep_range(5000, 10000);

	psdata->ps_state = PS_STATE_AWAKE;

	if (psdata->c2h_wakeup_gpio != 0xff)
		psdata->c2h_wakeupmode = BT_HOST_WAKEUP_METHOD_GPIO;
	else
		psdata->c2h_wakeupmode = BT_HOST_WAKEUP_METHOD_NONE;

	psdata->cur_h2c_wakeupmode = WAKEUP_METHOD_INVALID;
	if (psdata->h2c_ps_gpio)
		default_h2c_wakeup_mode = WAKEUP_METHOD_GPIO;

	psdata->h2c_ps_interval = PS_DEFAULT_TIMEOUT_PERIOD_MS;

	switch (default_h2c_wakeup_mode) {
	case WAKEUP_METHOD_GPIO:
		psdata->h2c_wakeupmode = WAKEUP_METHOD_GPIO;
		gpiod_set_value_cansleep(psdata->h2c_ps_gpio, 0);
		usleep_range(5000, 10000);
		break;
	case WAKEUP_METHOD_DTR:
		psdata->h2c_wakeupmode = WAKEUP_METHOD_DTR;
		serdev_device_set_tiocm(nxpdev->serdev, 0, TIOCM_DTR);
		serdev_device_set_tiocm(nxpdev->serdev, TIOCM_DTR, 0);
		break;
	case WAKEUP_METHOD_BREAK:
	default:
		psdata->h2c_wakeupmode = WAKEUP_METHOD_BREAK;
		serdev_device_break_ctl(nxpdev->serdev, -1);
		usleep_range(5000, 10000);
		serdev_device_break_ctl(nxpdev->serdev, 0);
		usleep_range(5000, 10000);
		break;
	}

	psdata->cur_psmode = PS_MODE_DISABLE;
	psdata->target_ps_mode = DEFAULT_PS_MODE;
}

/* NXP Firmware Download Feature */
static int nxp_download_firmware(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	int err = 0;

	nxpdev->fw_dnld_v1_offset = 0;
	nxpdev->fw_v1_sent_bytes = 0;
	nxpdev->fw_v1_expected_len = HDR_LEN;
	nxpdev->boot_reg_offset = 0;
	nxpdev->fw_dnld_v3_offset = 0;
	nxpdev->fw_v3_offset_correction = 0;
	nxpdev->baudrate_changed = not_changed;
	nxpdev->timeout_changed = not_changed;
	nxpdev->helper_downloaded = false;

	serdev_device_set_baudrate(nxpdev->serdev, HCI_NXP_PRI_BAUDRATE);
	serdev_device_set_flow_control(nxpdev->serdev, false);
	nxpdev->current_baudrate = HCI_NXP_PRI_BAUDRATE;

	/* Wait till FW is downloaded */
	err = wait_event_interruptible_timeout(nxpdev->fw_dnld_done_wait_q,
					       !test_bit(BTNXPUART_FW_DOWNLOADING,
							 &nxpdev->tx_state),
					       msecs_to_jiffies(60000));

	if (nxpdev->fw && strlen(nxpdev->fw_name)) {
		release_firmware(nxpdev->fw);
		memset(nxpdev->fw_name, 0, sizeof(nxpdev->fw_name));
	}

	if (err == 0) {
		bt_dev_err(hdev, "FW Download Timeout. offset: %d",
				nxpdev->fw_dnld_v1_offset ?
				nxpdev->fw_dnld_v1_offset :
				nxpdev->fw_dnld_v3_offset);
		return -ETIMEDOUT;
	}
	if (test_bit(BTNXPUART_FW_DOWNLOAD_ABORT, &nxpdev->tx_state)) {
		bt_dev_err(hdev, "FW Download Aborted");
		return -EINTR;
	}

	serdev_device_set_flow_control(nxpdev->serdev, true);

	/* Allow the downloaded FW to initialize */
	msleep(1200);

	return 0;
}

static void nxp_send_ack(u8 ack, struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	u8 ack_nak[2];
	int len = 1;

	ack_nak[0] = ack;
	if (ack == NXP_ACK_V3) {
		ack_nak[1] = crc8(crc8_table, ack_nak, 1, 0xff);
		len = 2;
	}
	serdev_device_write_buf(nxpdev->serdev, ack_nak, len);
}

static bool nxp_fw_change_baudrate(struct hci_dev *hdev, u16 req_len)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct nxp_bootloader_cmd nxp_cmd5;
	struct uart_config uart_config;
	u32 clkdivaddr = CLKDIVADDR - nxpdev->boot_reg_offset;
	u32 uartdivaddr = UARTDIVADDR - nxpdev->boot_reg_offset;
	u32 uartmcraddr = UARTMCRADDR - nxpdev->boot_reg_offset;
	u32 uartreinitaddr = UARTREINITADDR - nxpdev->boot_reg_offset;
	u32 uarticraddr = UARTICRADDR - nxpdev->boot_reg_offset;
	u32 uartfcraddr = UARTFCRADDR - nxpdev->boot_reg_offset;

	if (req_len == sizeof(nxp_cmd5)) {
		nxp_cmd5.header = __cpu_to_le32(5);
		nxp_cmd5.arg = 0;
		nxp_cmd5.payload_len = __cpu_to_le32(sizeof(uart_config));
		/* FW expects swapped CRC bytes */
		nxp_cmd5.crc = __cpu_to_be32(crc32_be(0UL, (char *)&nxp_cmd5,
						      sizeof(nxp_cmd5) - 4));

		serdev_device_write_buf(nxpdev->serdev, (u8 *)&nxp_cmd5, sizeof(nxp_cmd5));
		nxpdev->fw_v3_offset_correction += req_len;
	} else if (req_len == sizeof(uart_config)) {
		uart_config.clkdiv.address = __cpu_to_le32(clkdivaddr);
		if (nxpdev->new_baudrate == HCI_NXP_SEC_BAUDRATE_4M)
			uart_config.clkdiv.value = __cpu_to_le32(0x01000000);
		else
			uart_config.clkdiv.value = __cpu_to_le32(0x00c00000);
		uart_config.uartdiv.address = __cpu_to_le32(uartdivaddr);
		uart_config.uartdiv.value = __cpu_to_le32(1);
		uart_config.mcr.address = __cpu_to_le32(uartmcraddr);
		uart_config.mcr.value = __cpu_to_le32(MCR);
		uart_config.re_init.address = __cpu_to_le32(uartreinitaddr);
		uart_config.re_init.value = __cpu_to_le32(INIT);
		uart_config.icr.address = __cpu_to_le32(uarticraddr);
		uart_config.icr.value = __cpu_to_le32(ICR);
		uart_config.fcr.address = __cpu_to_le32(uartfcraddr);
		uart_config.fcr.value = __cpu_to_le32(FCR);
		/* FW expects swapped CRC bytes */
		uart_config.crc = __cpu_to_be32(crc32_be(0UL, (char *)&uart_config,
							 sizeof(uart_config) - 4));

		serdev_device_write_buf(nxpdev->serdev, (u8 *)&uart_config, sizeof(uart_config));
		serdev_device_wait_until_sent(nxpdev->serdev, 0);
		nxpdev->fw_v3_offset_correction += req_len;
		return true;
	}
	return false;
}

static bool nxp_fw_change_timeout(struct hci_dev *hdev, u16 req_len)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct nxp_bootloader_cmd nxp_cmd7;

	if (req_len != sizeof(nxp_cmd7))
		return false;

	nxp_cmd7.header = __cpu_to_le32(7);
	nxp_cmd7.arg = __cpu_to_le32(0x70);
	nxp_cmd7.payload_len = 0;
	/* FW expects swapped CRC bytes */
	nxp_cmd7.crc = __cpu_to_be32(crc32_be(0UL, (char *)&nxp_cmd7,
					      sizeof(nxp_cmd7) - 4));
	serdev_device_write_buf(nxpdev->serdev, (u8 *)&nxp_cmd7, sizeof(nxp_cmd7));
	serdev_device_wait_until_sent(nxpdev->serdev, 0);
	nxpdev->fw_v3_offset_correction += req_len;
	return true;
}

static u32 nxp_get_data_len(const u8 *buf)
{
	struct nxp_bootloader_cmd *hdr = (struct nxp_bootloader_cmd *)buf;

	return __le32_to_cpu(hdr->payload_len);
}

static bool is_fw_downloading(struct btnxpuart_dev *nxpdev)
{
	return test_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
}

static bool ind_reset_in_progress(struct btnxpuart_dev *nxpdev)
{
	return test_bit(BTNXPUART_IR_IN_PROGRESS, &nxpdev->tx_state);
}

static bool fw_dump_in_progress(struct btnxpuart_dev *nxpdev)
{
	return test_bit(BTNXPUART_FW_DUMP_IN_PROGRESS, &nxpdev->tx_state);
}

static bool process_boot_signature(struct btnxpuart_dev *nxpdev)
{
	if (test_bit(BTNXPUART_CHECK_BOOT_SIGNATURE, &nxpdev->tx_state)) {
		clear_bit(BTNXPUART_CHECK_BOOT_SIGNATURE, &nxpdev->tx_state);
		wake_up_interruptible(&nxpdev->check_boot_sign_wait_q);
		return false;
	}
	return is_fw_downloading(nxpdev);
}

static int nxp_request_firmware(struct hci_dev *hdev, const char *fw_name,
				const char *fw_name_old)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	const char *fw_name_dt;
	int err = 0;

	if (!fw_name)
		return -ENOENT;

	if (!strlen(nxpdev->fw_name)) {
		if (strcmp(fw_name, FIRMWARE_HELPER) &&
		    !device_property_read_string(&nxpdev->serdev->dev,
						 "firmware-name",
						 &fw_name_dt))
			fw_name = fw_name_dt;
		snprintf(nxpdev->fw_name, MAX_FW_FILE_NAME_LEN, "nxp/%s", fw_name);
		err = request_firmware_direct(&nxpdev->fw, nxpdev->fw_name, &hdev->dev);
		if (err < 0 && fw_name_old) {
			snprintf(nxpdev->fw_name, MAX_FW_FILE_NAME_LEN, "nxp/%s", fw_name_old);
			err = request_firmware_direct(&nxpdev->fw, nxpdev->fw_name, &hdev->dev);
		}

		bt_dev_info(hdev, "Request Firmware: %s", nxpdev->fw_name);
		if (err < 0) {
			bt_dev_err(hdev, "Firmware file %s not found", nxpdev->fw_name);
			clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
		}
	}
	return err;
}

/* for legacy chipsets with V1 bootloader */
static int nxp_recv_chip_ver_v1(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct v1_start_ind *req;
	__u16 chip_id;

	req = skb_pull_data(skb, sizeof(*req));
	if (!req)
		goto free_skb;

	chip_id = le16_to_cpu(req->chip_id ^ req->chip_id_comp);
	if (chip_id == 0xffff && nxpdev->fw_dnld_v1_offset) {
		nxpdev->fw_dnld_v1_offset = 0;
		nxpdev->fw_v1_sent_bytes = 0;
		nxpdev->fw_v1_expected_len = HDR_LEN;
		release_firmware(nxpdev->fw);
		memset(nxpdev->fw_name, 0, sizeof(nxpdev->fw_name));
		nxp_send_ack(NXP_ACK_V1, hdev);
	}

free_skb:
	kfree_skb(skb);
	return 0;
}

static int nxp_recv_fw_req_v1(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct btnxpuart_data *nxp_data = nxpdev->nxp_data;
	struct v1_data_req *req;
	__u16 len;

	if (!process_boot_signature(nxpdev))
		goto free_skb;

	req = skb_pull_data(skb, sizeof(*req));
	if (!req)
		goto free_skb;

	len = __le16_to_cpu(req->len ^ req->len_comp);
	if (len != 0xffff) {
		bt_dev_dbg(hdev, "ERR: Send NAK");
		nxp_send_ack(NXP_NAK_V1, hdev);
		goto free_skb;
	}
	nxp_send_ack(NXP_ACK_V1, hdev);

	len = __le16_to_cpu(req->len);

	if (!nxp_data->helper_fw_name) {
		if (nxpdev->timeout_changed != changed) {
			nxp_fw_change_timeout(hdev, len);
			nxpdev->timeout_changed = changed;
			goto free_skb;
		}
		if (nxpdev->baudrate_changed != changed) {
			nxpdev->new_baudrate = nxpdev->secondary_baudrate;
			if (nxp_fw_change_baudrate(hdev, len)) {
				nxpdev->baudrate_changed = changed;
				serdev_device_set_baudrate(nxpdev->serdev,
							   nxpdev->secondary_baudrate);
				serdev_device_set_flow_control(nxpdev->serdev, true);
				nxpdev->current_baudrate = nxpdev->secondary_baudrate;
			}
			goto free_skb;
		}
	}

	if (!nxp_data->helper_fw_name || nxpdev->helper_downloaded) {
		if (nxp_request_firmware(hdev, nxp_data->fw_name, nxp_data->fw_name_old))
			goto free_skb;
	} else if (nxp_data->helper_fw_name && !nxpdev->helper_downloaded) {
		if (nxp_request_firmware(hdev, nxp_data->helper_fw_name, NULL))
			goto free_skb;
	}

	if (!len) {
		bt_dev_info(hdev, "FW Download Complete: %zu bytes",
			   nxpdev->fw->size);
		if (nxp_data->helper_fw_name && !nxpdev->helper_downloaded) {
			nxpdev->helper_downloaded = true;
			serdev_device_wait_until_sent(nxpdev->serdev, 0);
			serdev_device_set_baudrate(nxpdev->serdev,
						   HCI_NXP_SEC_BAUDRATE_3M);
			serdev_device_set_flow_control(nxpdev->serdev, true);
		} else {
			clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
			wake_up_interruptible(&nxpdev->fw_dnld_done_wait_q);
		}
		goto free_skb;
	}
	if (len & 0x01) {
		/* The CRC did not match at the other end.
		 * Simply send the same bytes again.
		 */
		len = nxpdev->fw_v1_sent_bytes;
		bt_dev_dbg(hdev, "CRC error. Resend %d bytes of FW.", len);
	} else {
		nxpdev->fw_dnld_v1_offset += nxpdev->fw_v1_sent_bytes;

		/* The FW bin file is made up of many blocks of
		 * 16 byte header and payload data chunks. If the
		 * FW has requested a header, read the payload length
		 * info from the header, before sending the header.
		 * In the next iteration, the FW should request the
		 * payload data chunk, which should be equal to the
		 * payload length read from header. If there is a
		 * mismatch, clearly the driver and FW are out of sync,
		 * and we need to re-send the previous header again.
		 */
		if (len == nxpdev->fw_v1_expected_len) {
			if (len == HDR_LEN)
				nxpdev->fw_v1_expected_len = nxp_get_data_len(nxpdev->fw->data +
									nxpdev->fw_dnld_v1_offset);
			else
				nxpdev->fw_v1_expected_len = HDR_LEN;
		} else if (len == HDR_LEN) {
			/* FW download out of sync. Send previous chunk again */
			nxpdev->fw_dnld_v1_offset -= nxpdev->fw_v1_sent_bytes;
			nxpdev->fw_v1_expected_len = HDR_LEN;
		}
	}

	if (nxpdev->fw_dnld_v1_offset + len <= nxpdev->fw->size)
		serdev_device_write_buf(nxpdev->serdev, nxpdev->fw->data +
					nxpdev->fw_dnld_v1_offset, len);
	nxpdev->fw_v1_sent_bytes = len;

free_skb:
	kfree_skb(skb);
	return 0;
}

static char *nxp_get_fw_name_from_chipid(struct hci_dev *hdev, u16 chipid,
					 u8 loader_ver)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	char *fw_name = NULL;

	switch (chipid) {
	case CHIP_ID_W9098:
		fw_name = FIRMWARE_W9098;
		break;
	case CHIP_ID_IW416:
		fw_name = FIRMWARE_IW416;
		break;
	case CHIP_ID_IW612:
		fw_name = FIRMWARE_IW612;
		break;
	case CHIP_ID_IW624a:
	case CHIP_ID_IW624c:
		nxpdev->boot_reg_offset = 1;
		if ((loader_ver & FW_SECURE_MASK) == FW_OPEN)
			fw_name = FIRMWARE_IW624;
		else if ((loader_ver & FW_SECURE_MASK) != FW_AUTH_ILLEGAL)
			fw_name = FIRMWARE_SECURE_IW624;
		else
			bt_dev_err(hdev, "Illegal loader version %02x", loader_ver);
		break;
	case CHIP_ID_AW693a0:
		if ((loader_ver & FW_SECURE_MASK) == FW_OPEN)
			fw_name = FIRMWARE_AW693;
		else if ((loader_ver & FW_SECURE_MASK) != FW_AUTH_ILLEGAL)
			fw_name = FIRMWARE_SECURE_AW693;
		else
			bt_dev_err(hdev, "Illegal loader version %02x", loader_ver);
		break;
	case CHIP_ID_AW693a1:
		if ((loader_ver & FW_SECURE_MASK) == FW_OPEN)
			fw_name = FIRMWARE_AW693_A1;
		else if ((loader_ver & FW_SECURE_MASK) != FW_AUTH_ILLEGAL)
			fw_name = FIRMWARE_SECURE_AW693_A1;
		else
			bt_dev_err(hdev, "Illegal loader version %02x", loader_ver);
		break;
	case CHIP_ID_IW610a0:
	case CHIP_ID_IW610a1:
		if ((loader_ver & FW_SECURE_MASK) == FW_OPEN)
			fw_name = FIRMWARE_IW610;
		else if ((loader_ver & FW_SECURE_MASK) != FW_AUTH_ILLEGAL)
			fw_name = FIRMWARE_SECURE_IW610;
		else
			bt_dev_err(hdev, "Illegal loader version %02x", loader_ver);
		break;
	default:
		bt_dev_err(hdev, "Unknown chip signature %04x", chipid);
		break;
	}
	return fw_name;
}

static char *nxp_get_old_fw_name_from_chipid(struct hci_dev *hdev, u16 chipid,
					 u8 loader_ver)
{
	char *fw_name_old = NULL;

	switch (chipid) {
	case CHIP_ID_W9098:
		fw_name_old = FIRMWARE_W9098_OLD;
		break;
	case CHIP_ID_IW416:
		fw_name_old = FIRMWARE_IW416_OLD;
		break;
	}
	return fw_name_old;
}

static int nxp_recv_chip_ver_v3(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct v3_start_ind *req = skb_pull_data(skb, sizeof(*req));
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	const char *fw_name;
	const char *fw_name_old;
	u16 chip_id;
	u8 loader_ver;

	if (!process_boot_signature(nxpdev))
		goto free_skb;

	chip_id = le16_to_cpu(req->chip_id);
	loader_ver = req->loader_ver;
	bt_dev_info(hdev, "ChipID: %04x, Version: %d", chip_id, loader_ver);
	fw_name = nxp_get_fw_name_from_chipid(hdev, chip_id, loader_ver);
	fw_name_old = nxp_get_old_fw_name_from_chipid(hdev, chip_id, loader_ver);
	if (!nxp_request_firmware(hdev, fw_name, fw_name_old))
		nxp_send_ack(NXP_ACK_V3, hdev);

free_skb:
	kfree_skb(skb);
	return 0;
}

static void nxp_handle_fw_download_error(struct hci_dev *hdev, struct v3_data_req *req)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	__u32 offset = __le32_to_cpu(req->offset);
	__u16 err = __le16_to_cpu(req->error);
	union nxp_v3_rx_timeout_nak_u timeout_nak_buf;
	union nxp_v3_crc_nak_u crc_nak_buf;

	if (err & NXP_CRC_RX_ERROR) {
		crc_nak_buf.pkt.nak = NXP_CRC_ERROR_V3;
		crc_nak_buf.pkt.crc = crc8(crc8_table, crc_nak_buf.buf,
					   sizeof(crc_nak_buf) - 1, 0xff);
		serdev_device_write_buf(nxpdev->serdev, crc_nak_buf.buf,
					sizeof(crc_nak_buf));
	} else if (err & NXP_ACK_RX_TIMEOUT ||
		   err & NXP_HDR_RX_TIMEOUT ||
		   err & NXP_DATA_RX_TIMEOUT) {
		timeout_nak_buf.pkt.nak = NXP_NAK_V3;
		timeout_nak_buf.pkt.offset = __cpu_to_le32(offset);
		timeout_nak_buf.pkt.crc = crc8(crc8_table, timeout_nak_buf.buf,
					       sizeof(timeout_nak_buf) - 1, 0xff);
		serdev_device_write_buf(nxpdev->serdev, timeout_nak_buf.buf,
					sizeof(timeout_nak_buf));
	} else {
		bt_dev_err(hdev, "Unknown bootloader error code: %d", err);
	}
}

static int nxp_recv_fw_req_v3(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct v3_data_req *req;
	__u16 len = 0;
	__u16 err = 0;
	__u32 offset;

	if (!process_boot_signature(nxpdev))
		goto free_skb;

	req = skb_pull_data(skb, sizeof(*req));
	if (!req || !nxpdev->fw)
		goto free_skb;

	err = __le16_to_cpu(req->error);

	if (!err) {
		nxp_send_ack(NXP_ACK_V3, hdev);
		if (nxpdev->timeout_changed == cmd_sent)
			nxpdev->timeout_changed = changed;
		if (nxpdev->baudrate_changed == cmd_sent)
			nxpdev->baudrate_changed = changed;
	} else {
		nxp_handle_fw_download_error(hdev, req);
		if (nxpdev->timeout_changed == cmd_sent &&
		    err == NXP_CRC_RX_ERROR) {
			nxpdev->fw_v3_offset_correction -= nxpdev->fw_v3_prev_sent;
			nxpdev->timeout_changed = not_changed;
		}
		if (nxpdev->baudrate_changed == cmd_sent &&
		    err == NXP_CRC_RX_ERROR) {
			nxpdev->fw_v3_offset_correction -= nxpdev->fw_v3_prev_sent;
			nxpdev->baudrate_changed = not_changed;
		}
		goto free_skb;
	}

	len = __le16_to_cpu(req->len);

	if (nxpdev->timeout_changed != changed) {
		nxp_fw_change_timeout(hdev, len);
		nxpdev->timeout_changed = cmd_sent;
		goto free_skb;
	}

	if (nxpdev->baudrate_changed != changed) {
		nxpdev->new_baudrate = nxpdev->secondary_baudrate;
		if (nxp_fw_change_baudrate(hdev, len)) {
			nxpdev->baudrate_changed = cmd_sent;
			serdev_device_set_baudrate(nxpdev->serdev,
						   nxpdev->secondary_baudrate);
			serdev_device_set_flow_control(nxpdev->serdev, true);
			nxpdev->current_baudrate = nxpdev->secondary_baudrate;
		}
		goto free_skb;
	}

	if (req->len == 0) {
		bt_dev_info(hdev, "FW Download Complete: %zu bytes",
			   nxpdev->fw->size);
		clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
		wake_up_interruptible(&nxpdev->fw_dnld_done_wait_q);
		goto free_skb;
	}

	offset = __le32_to_cpu(req->offset);
	if (offset < nxpdev->fw_v3_offset_correction) {
		/* This scenario should ideally never occur. But if it ever does,
		 * FW is out of sync and needs a power cycle.
		 */
		bt_dev_err(hdev, "Something went wrong during FW download");
		bt_dev_err(hdev, "Please power cycle and try again");
		goto free_skb;
	}

	nxpdev->fw_dnld_v3_offset = offset - nxpdev->fw_v3_offset_correction;
	serdev_device_write_buf(nxpdev->serdev, nxpdev->fw->data +
				nxpdev->fw_dnld_v3_offset, len);

free_skb:
	nxpdev->fw_v3_prev_sent = len;
	kfree_skb(skb);
	return 0;
}

static int nxp_set_baudrate_cmd(struct hci_dev *hdev, void *data)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	__le32 new_baudrate = __cpu_to_le32(nxpdev->new_baudrate);
	struct ps_data *psdata = &nxpdev->psdata;
	struct sk_buff *skb;
	u8 *status;

	if (!psdata)
		return 0;

	skb = nxp_drv_send_cmd(hdev, HCI_NXP_SET_OPER_SPEED, 4,
			       (u8 *)&new_baudrate, true);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting baudrate failed (%ld)", PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	status = (u8 *)skb_pull_data(skb, 1);
	if (status) {
		if (*status == 0) {
			serdev_device_set_baudrate(nxpdev->serdev, nxpdev->new_baudrate);
			nxpdev->current_baudrate = nxpdev->new_baudrate;
		}
		bt_dev_dbg(hdev, "Set baudrate response: status=%d, baudrate=%d",
			   *status, nxpdev->new_baudrate);
	}
	kfree_skb(skb);

	return 0;
}

static int nxp_check_boot_sign(struct btnxpuart_dev *nxpdev)
{
	serdev_device_set_baudrate(nxpdev->serdev, HCI_NXP_PRI_BAUDRATE);
	if (ind_reset_in_progress(nxpdev))
		serdev_device_set_flow_control(nxpdev->serdev, false);
	else
		serdev_device_set_flow_control(nxpdev->serdev, true);
	set_bit(BTNXPUART_CHECK_BOOT_SIGNATURE, &nxpdev->tx_state);

	return wait_event_interruptible_timeout(nxpdev->check_boot_sign_wait_q,
					       !test_bit(BTNXPUART_CHECK_BOOT_SIGNATURE,
							 &nxpdev->tx_state),
					       msecs_to_jiffies(1000));
}

static int nxp_set_ind_reset(struct hci_dev *hdev, void *data)
{
	static const u8 ir_hw_err[] = { HCI_EV_HARDWARE_ERROR,
					0x01, BTNXPUART_IR_HW_ERR };
	struct sk_buff *skb;

	skb = bt_skb_alloc(3, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
	skb_put_data(skb, ir_hw_err, 3);

	/* Inject Hardware Error to upper stack */
	return hci_recv_frame(hdev, skb);
}

/* Firmware dump */
static void nxp_coredump(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	u8 pcmd = 2;

	skb = nxp_drv_send_cmd(hdev, HCI_NXP_TRIGGER_DUMP, 1, &pcmd, true);
	if (IS_ERR(skb))
		bt_dev_err(hdev, "Failed to trigger FW Dump. (%ld)", PTR_ERR(skb));
	else
		kfree_skb(skb);
}

static void nxp_coredump_hdr(struct hci_dev *hdev, struct sk_buff *skb)
{
	/* Nothing to be added in FW dump header */
}

static int nxp_process_fw_dump(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_acl_hdr *acl_hdr = (struct hci_acl_hdr *)skb_pull_data(skb,
									  sizeof(*acl_hdr));
	struct nxp_fw_dump_hdr *fw_dump_hdr = (struct nxp_fw_dump_hdr *)skb->data;
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	__u16 seq_num = __le16_to_cpu(fw_dump_hdr->seq_num);
	__u16 buf_len = __le16_to_cpu(fw_dump_hdr->buf_len);
	int err;

	if (seq_num == 0x0001) {
		if (test_and_set_bit(BTNXPUART_FW_DUMP_IN_PROGRESS, &nxpdev->tx_state)) {
			bt_dev_err(hdev, "FW dump already in progress");
			goto free_skb;
		}
		bt_dev_warn(hdev, "==== Start FW dump ===");
		err = hci_devcd_init(hdev, NXP_FW_DUMP_SIZE);
		if (err < 0)
			goto free_skb;

		schedule_delayed_work(&hdev->dump.dump_timeout,
				      msecs_to_jiffies(20000));
	}

	err = hci_devcd_append(hdev, skb_clone(skb, GFP_ATOMIC));
	if (err < 0)
		goto free_skb;

	if (buf_len == 0) {
		bt_dev_warn(hdev, "==== FW dump complete ===");
		hci_devcd_complete(hdev);
		nxp_set_ind_reset(hdev, NULL);
	}

free_skb:
	kfree_skb(skb);
	return 0;
}

static int nxp_recv_acl_pkt(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u16 handle = __le16_to_cpu(hci_acl_hdr(skb)->handle);

	/* FW dump chunks are ACL packets with conn handle 0xfff */
	if ((handle & 0x0FFF) == 0xFFF)
		return nxp_process_fw_dump(hdev, skb);
	else
		return hci_recv_frame(hdev, skb);
}

static int nxp_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	union nxp_set_bd_addr_payload pcmd;
	int err;

	pcmd.data.param_id = 0xfe;
	pcmd.data.param_len = 6;
	memcpy(pcmd.data.param, bdaddr, 6);

	/* BD address can be assigned only after first reset command. */
	err = __hci_cmd_sync_status(hdev, HCI_OP_RESET, 0, NULL,
				    HCI_INIT_TIMEOUT);
	if (err) {
		bt_dev_err(hdev,
			   "Reset before setting local-bd-addr failed (%d)",
			   err);
		return err;
	}

	err = __hci_cmd_sync_status(hdev, HCI_NXP_SET_BD_ADDR, sizeof(pcmd),
			     pcmd.buf, HCI_CMD_TIMEOUT);
	if (err) {
		bt_dev_err(hdev, "Changing device address failed (%d)", err);
		return err;
	}

	return 0;
}

/* NXP protocol */
static int nxp_setup(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct serdev_device *serdev = nxpdev->serdev;
	char device_string[30];
	char event_string[50];
	char *envp[] = {device_string, event_string, NULL};
	int err = 0;

	if (nxp_check_boot_sign(nxpdev)) {
		bt_dev_dbg(hdev, "Need FW Download.");
		err = nxp_download_firmware(hdev);
		if (err < 0)
			return err;
	} else {
		bt_dev_info(hdev, "FW already running.");
		clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
	}

	snprintf(device_string, 30, "BTNXPUART_DEV=%s", dev_name(&serdev->dev));
	snprintf(event_string, 50, "BTNXPUART_STATE=FW_READY");
	bt_dev_dbg(hdev, "==== Send uevent: %s:%s ===", device_string,
		   event_string);
	kobject_uevent_env(&serdev->dev.kobj, KOBJ_CHANGE, envp);

	serdev_device_set_baudrate(nxpdev->serdev, nxpdev->fw_init_baudrate);
	nxpdev->current_baudrate = nxpdev->fw_init_baudrate;

	ps_init(hdev);

	if (test_and_clear_bit(BTNXPUART_IR_IN_PROGRESS, &nxpdev->tx_state))
		hci_dev_clear_flag(hdev, HCI_SETUP);

	return 0;
}

static int nxp_post_init(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = &nxpdev->psdata;

	if (nxpdev->current_baudrate != nxpdev->secondary_baudrate) {
		nxpdev->new_baudrate = nxpdev->secondary_baudrate;
		nxp_set_baudrate_cmd(hdev, NULL);
	}
	if (psdata->cur_h2c_wakeupmode != psdata->h2c_wakeupmode)
		send_wakeup_method_cmd(hdev, NULL);
	if (psdata->cur_psmode != psdata->target_ps_mode)
		send_ps_cmd(hdev, NULL);
	return 0;
}

static void nxp_hw_err(struct hci_dev *hdev, u8 code)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	switch (code) {
	case BTNXPUART_IR_HW_ERR:
		set_bit(BTNXPUART_IR_IN_PROGRESS, &nxpdev->tx_state);
		hci_dev_set_flag(hdev, HCI_SETUP);
		break;
	default:
		break;
	}
}

static int nxp_shutdown(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct sk_buff *skb;
	u8 pcmd = 0;

	if (ind_reset_in_progress(nxpdev)) {
		if (test_and_clear_bit(BTNXPUART_FW_DUMP_IN_PROGRESS,
				       &nxpdev->tx_state))
			skb = nxp_drv_send_cmd(hdev, HCI_NXP_IND_RESET, 1,
					       &pcmd, false);
		else
			skb = nxp_drv_send_cmd(hdev, HCI_NXP_IND_RESET, 1,
					       &pcmd, true);
		serdev_device_set_flow_control(nxpdev->serdev, false);
		set_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
		/* HCI_NXP_IND_RESET command may not returns any response */
		if (!IS_ERR(skb))
			kfree_skb(skb);
	}

	return 0;
}

static bool nxp_wakeup(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = &nxpdev->psdata;

	if (psdata->c2h_wakeupmode != BT_HOST_WAKEUP_METHOD_NONE)
		return true;

	return false;
}

static void nxp_reset(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	if (!ind_reset_in_progress(nxpdev) && !fw_dump_in_progress(nxpdev)) {
		bt_dev_dbg(hdev, "CMD Timeout detected. Resetting.");
		nxp_set_ind_reset(hdev, NULL);
	}
}

static int btnxpuart_queue_skb(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);
	skb_queue_tail(&nxpdev->txq, skb);
	btnxpuart_tx_wakeup(nxpdev);
	return 0;
}

static int nxp_enqueue(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = &nxpdev->psdata;
	struct hci_command_hdr *hdr;
	struct psmode_cmd_payload ps_parm;
	struct wakeup_cmd_payload wakeup_parm;
	__le32 baudrate_parm;

	if (fw_dump_in_progress(nxpdev))
		return -EBUSY;

	/* if vendor commands are received from user space (e.g. hcitool), update
	 * driver flags accordingly and ask driver to re-send the command to FW.
	 * In case the payload for any command does not match expected payload
	 * length, let the firmware and user space program handle it, or throw
	 * an error.
	 */
	if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT && !psdata->driver_sent_cmd) {
		hdr = (struct hci_command_hdr *)skb->data;
		if (hdr->plen != (skb->len - HCI_COMMAND_HDR_SIZE))
			return btnxpuart_queue_skb(hdev, skb);

		switch (__le16_to_cpu(hdr->opcode)) {
		case HCI_NXP_AUTO_SLEEP_MODE:
			if (hdr->plen == sizeof(ps_parm)) {
				memcpy(&ps_parm, skb->data + HCI_COMMAND_HDR_SIZE, hdr->plen);
				if (ps_parm.ps_cmd == BT_PS_ENABLE)
					psdata->target_ps_mode = PS_MODE_ENABLE;
				else if (ps_parm.ps_cmd == BT_PS_DISABLE)
					psdata->target_ps_mode = PS_MODE_DISABLE;
				psdata->c2h_ps_interval = __le16_to_cpu(ps_parm.c2h_ps_interval);
				hci_cmd_sync_queue(hdev, send_ps_cmd, NULL, NULL);
				goto free_skb;
			}
			break;
		case HCI_NXP_WAKEUP_METHOD:
			if (hdr->plen == sizeof(wakeup_parm)) {
				memcpy(&wakeup_parm, skb->data + HCI_COMMAND_HDR_SIZE, hdr->plen);
				psdata->c2h_wakeupmode = wakeup_parm.c2h_wakeupmode;
				psdata->c2h_wakeup_gpio = wakeup_parm.c2h_wakeup_gpio;
				psdata->h2c_wakeup_gpio = wakeup_parm.h2c_wakeup_gpio;
				switch (wakeup_parm.h2c_wakeupmode) {
				case BT_CTRL_WAKEUP_METHOD_GPIO:
					psdata->h2c_wakeupmode = WAKEUP_METHOD_GPIO;
					break;
				case BT_CTRL_WAKEUP_METHOD_DSR:
					psdata->h2c_wakeupmode = WAKEUP_METHOD_DTR;
					break;
				case BT_CTRL_WAKEUP_METHOD_BREAK:
				default:
					psdata->h2c_wakeupmode = WAKEUP_METHOD_BREAK;
					break;
				}
				hci_cmd_sync_queue(hdev, send_wakeup_method_cmd, NULL, NULL);
				goto free_skb;
			}
			break;
		case HCI_NXP_SET_OPER_SPEED:
			if (hdr->plen == sizeof(baudrate_parm)) {
				memcpy(&baudrate_parm, skb->data + HCI_COMMAND_HDR_SIZE, hdr->plen);
				nxpdev->new_baudrate = __le32_to_cpu(baudrate_parm);
				hci_cmd_sync_queue(hdev, nxp_set_baudrate_cmd, NULL, NULL);
				goto free_skb;
			}
			break;
		case HCI_NXP_IND_RESET:
			if (hdr->plen == 1) {
				hci_cmd_sync_queue(hdev, nxp_set_ind_reset, NULL, NULL);
				goto free_skb;
			}
			break;
		default:
			break;
		}
	}

	return btnxpuart_queue_skb(hdev, skb);

free_skb:
	kfree_skb(skb);
	return 0;
}

static struct sk_buff *nxp_dequeue(void *data)
{
	struct btnxpuart_dev *nxpdev = (struct btnxpuart_dev *)data;

	ps_start_timer(nxpdev);
	return skb_dequeue(&nxpdev->txq);
}

/* btnxpuart based on serdev */
static void btnxpuart_tx_work(struct work_struct *work)
{
	struct btnxpuart_dev *nxpdev = container_of(work, struct btnxpuart_dev,
						   tx_work);
	struct serdev_device *serdev = nxpdev->serdev;
	struct hci_dev *hdev = nxpdev->hdev;
	struct sk_buff *skb;
	int len;

	if (ps_wakeup(nxpdev))
		return;

	while ((skb = nxp_dequeue(nxpdev))) {
		len = serdev_device_write_buf(serdev, skb->data, skb->len);
		hdev->stat.byte_tx += len;

		skb_pull(skb, len);
		if (skb->len > 0) {
			skb_queue_head(&nxpdev->txq, skb);
			continue;
		}

		switch (hci_skb_pkt_type(skb)) {
		case HCI_COMMAND_PKT:
			hdev->stat.cmd_tx++;
			break;
		case HCI_ACLDATA_PKT:
			hdev->stat.acl_tx++;
			break;
		case HCI_SCODATA_PKT:
			hdev->stat.sco_tx++;
			break;
		}

		kfree_skb(skb);
	}
	clear_bit(BTNXPUART_TX_STATE_ACTIVE, &nxpdev->tx_state);
}

static int btnxpuart_open(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	int err = 0;

	err = serdev_device_open(nxpdev->serdev);
	if (err) {
		bt_dev_err(hdev, "Unable to open UART device %s",
			   dev_name(&nxpdev->serdev->dev));
	} else {
		set_bit(BTNXPUART_SERDEV_OPEN, &nxpdev->tx_state);
	}
	return err;
}

static int btnxpuart_close(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	serdev_device_close(nxpdev->serdev);
	skb_queue_purge(&nxpdev->txq);
	if (!IS_ERR_OR_NULL(nxpdev->rx_skb)) {
		kfree_skb(nxpdev->rx_skb);
		nxpdev->rx_skb = NULL;
	}
	clear_bit(BTNXPUART_SERDEV_OPEN, &nxpdev->tx_state);
	return 0;
}

static int btnxpuart_flush(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	/* Flush any pending characters */
	serdev_device_write_flush(nxpdev->serdev);
	skb_queue_purge(&nxpdev->txq);

	cancel_work_sync(&nxpdev->tx_work);

	if (!IS_ERR_OR_NULL(nxpdev->rx_skb)) {
		kfree_skb(nxpdev->rx_skb);
		nxpdev->rx_skb = NULL;
	}

	return 0;
}

static const struct h4_recv_pkt nxp_recv_pkts[] = {
	{ H4_RECV_ACL,          .recv = nxp_recv_acl_pkt },
	{ H4_RECV_SCO,          .recv = hci_recv_frame },
	{ H4_RECV_EVENT,        .recv = hci_recv_frame },
	{ H4_RECV_ISO,		.recv = hci_recv_frame },
	{ NXP_RECV_CHIP_VER_V1, .recv = nxp_recv_chip_ver_v1 },
	{ NXP_RECV_FW_REQ_V1,   .recv = nxp_recv_fw_req_v1 },
	{ NXP_RECV_CHIP_VER_V3, .recv = nxp_recv_chip_ver_v3 },
	{ NXP_RECV_FW_REQ_V3,   .recv = nxp_recv_fw_req_v3 },
};

static size_t btnxpuart_receive_buf(struct serdev_device *serdev,
				    const u8 *data, size_t count)
{
	struct btnxpuart_dev *nxpdev = serdev_device_get_drvdata(serdev);

	ps_start_timer(nxpdev);

	nxpdev->rx_skb = h4_recv_buf(nxpdev->hdev, nxpdev->rx_skb, data, count,
				     nxp_recv_pkts, ARRAY_SIZE(nxp_recv_pkts));
	if (IS_ERR(nxpdev->rx_skb)) {
		int err = PTR_ERR(nxpdev->rx_skb);
		/* Safe to ignore out-of-sync bootloader signatures */
		if (!is_fw_downloading(nxpdev) &&
		    !ind_reset_in_progress(nxpdev))
			bt_dev_err(nxpdev->hdev, "Frame reassembly failed (%d)", err);
		return count;
	}
	if (!is_fw_downloading(nxpdev) &&
	    !ind_reset_in_progress(nxpdev))
		nxpdev->hdev->stat.byte_rx += count;
	return count;
}

static void btnxpuart_write_wakeup(struct serdev_device *serdev)
{
	serdev_device_write_wakeup(serdev);
}

static const struct serdev_device_ops btnxpuart_client_ops = {
	.receive_buf = btnxpuart_receive_buf,
	.write_wakeup = btnxpuart_write_wakeup,
};

static void nxp_coredump_notify(struct hci_dev *hdev, int state)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct serdev_device *serdev = nxpdev->serdev;
	char device_string[30];
	char event_string[50];
	char *envp[] = {device_string, event_string, NULL};

	snprintf(device_string, 30, "BTNXPUART_DEV=%s", dev_name(&serdev->dev));
	switch (state) {
	case HCI_DEVCOREDUMP_ACTIVE:
		snprintf(event_string, 50, "BTNXPUART_STATE=FW_DUMP_ACTIVE");
		break;
	case HCI_DEVCOREDUMP_DONE:
		snprintf(event_string, 50, "BTNXPUART_STATE=FW_DUMP_DONE");
		break;
	case HCI_DEVCOREDUMP_TIMEOUT:
		snprintf(event_string, 50, "BTNXPUART_STATE=FW_DUMP_TIMEOUT");
		break;
	default:
		snprintf(event_string, 50, "BTNXPUART_STATE=FW_DUMP_STATE_%d",
			 state);
		break;
	}
	bt_dev_dbg(hdev, "==== Send uevent: %s:%s ===", device_string,
		   event_string);
	kobject_uevent_env(&serdev->dev.kobj, KOBJ_CHANGE, envp);
}

static int nxp_serdev_probe(struct serdev_device *serdev)
{
	struct hci_dev *hdev;
	struct btnxpuart_dev *nxpdev;
	bdaddr_t ba = {0};
	int err;

	nxpdev = devm_kzalloc(&serdev->dev, sizeof(*nxpdev), GFP_KERNEL);
	if (!nxpdev)
		return -ENOMEM;

	nxpdev->nxp_data = (struct btnxpuart_data *)device_get_match_data(&serdev->dev);

	nxpdev->serdev = serdev;
	serdev_device_set_drvdata(serdev, nxpdev);

	serdev_device_set_client_ops(serdev, &btnxpuart_client_ops);

	INIT_WORK(&nxpdev->tx_work, btnxpuart_tx_work);
	skb_queue_head_init(&nxpdev->txq);

	init_waitqueue_head(&nxpdev->fw_dnld_done_wait_q);
	init_waitqueue_head(&nxpdev->check_boot_sign_wait_q);

	device_property_read_u32(&nxpdev->serdev->dev, "fw-init-baudrate",
				 &nxpdev->fw_init_baudrate);
	if (!nxpdev->fw_init_baudrate)
		nxpdev->fw_init_baudrate = FW_INIT_BAUDRATE;

	device_property_read_u32(&nxpdev->serdev->dev, "max-speed",
				 &nxpdev->secondary_baudrate);
	if (!nxpdev->secondary_baudrate ||
	    (nxpdev->secondary_baudrate != HCI_NXP_SEC_BAUDRATE_3M &&
	     nxpdev->secondary_baudrate != HCI_NXP_SEC_BAUDRATE_4M)) {
		if (nxpdev->secondary_baudrate)
			dev_err(&serdev->dev,
				"Invalid max-speed. Using default 3000000.");
		nxpdev->secondary_baudrate = HCI_NXP_SEC_BAUDRATE_3M;
	}

	set_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);

	crc8_populate_msb(crc8_table, POLYNOMIAL8);

	nxpdev->pdn = devm_reset_control_get_optional_shared(&serdev->dev, NULL);
	if (IS_ERR(nxpdev->pdn))
		return PTR_ERR(nxpdev->pdn);

	err = devm_regulator_get_enable(&serdev->dev, "vcc");
	if (err) {
		dev_err(&serdev->dev, "Failed to enable vcc regulator\n");
		return err;
	}

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(&serdev->dev, "Can't allocate HCI device\n");
		return -ENOMEM;
	}

	reset_control_deassert(nxpdev->pdn);

	nxpdev->hdev = hdev;

	hdev->bus = HCI_UART;
	hci_set_drvdata(hdev, nxpdev);

	hdev->manufacturer = MANUFACTURER_NXP;
	hdev->open  = btnxpuart_open;
	hdev->close = btnxpuart_close;
	hdev->flush = btnxpuart_flush;
	hdev->setup = nxp_setup;
	hdev->post_init = nxp_post_init;
	hdev->send  = nxp_enqueue;
	hdev->hw_error = nxp_hw_err;
	hdev->shutdown = nxp_shutdown;
	hdev->wakeup = nxp_wakeup;
	hdev->reset = nxp_reset;
	hdev->set_bdaddr = nxp_set_bdaddr;
	SET_HCIDEV_DEV(hdev, &serdev->dev);

	device_property_read_u8_array(&nxpdev->serdev->dev,
				      "local-bd-address",
				      (u8 *)&ba, sizeof(ba));
	if (bacmp(&ba, BDADDR_ANY))
		hci_set_quirk(hdev, HCI_QUIRK_USE_BDADDR_PROPERTY);

	if (hci_register_dev(hdev) < 0) {
		dev_err(&serdev->dev, "Can't register HCI device\n");
		goto probe_fail;
	}

	if (ps_setup(hdev))
		goto probe_fail;

	hci_devcd_register(hdev, nxp_coredump, nxp_coredump_hdr,
			   nxp_coredump_notify);

	return 0;

probe_fail:
	reset_control_assert(nxpdev->pdn);
	hci_free_dev(hdev);
	return -ENODEV;
}

static void nxp_serdev_remove(struct serdev_device *serdev)
{
	struct btnxpuart_dev *nxpdev = serdev_device_get_drvdata(serdev);
	struct hci_dev *hdev = nxpdev->hdev;

	if (is_fw_downloading(nxpdev)) {
		set_bit(BTNXPUART_FW_DOWNLOAD_ABORT, &nxpdev->tx_state);
		clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
		wake_up_interruptible(&nxpdev->check_boot_sign_wait_q);
		wake_up_interruptible(&nxpdev->fw_dnld_done_wait_q);
	} else {
		/* Restore FW baudrate to fw_init_baudrate if changed.
		 * This will ensure FW baudrate is in sync with
		 * driver baudrate in case this driver is re-inserted.
		 */
		if (nxpdev->current_baudrate != nxpdev->fw_init_baudrate) {
			nxpdev->new_baudrate = nxpdev->fw_init_baudrate;
			nxp_set_baudrate_cmd(hdev, NULL);
		}
	}

	ps_cleanup(nxpdev);
	hci_unregister_dev(hdev);
	reset_control_assert(nxpdev->pdn);
	hci_free_dev(hdev);
}

#ifdef CONFIG_PM_SLEEP
static int nxp_serdev_suspend(struct device *dev)
{
	struct btnxpuart_dev *nxpdev = dev_get_drvdata(dev);
	struct ps_data *psdata = &nxpdev->psdata;

	ps_control(psdata->hdev, PS_STATE_SLEEP);

	if (psdata->wakeup_source) {
		enable_irq_wake(psdata->irq_handler);
		enable_irq(psdata->irq_handler);
	}
	return 0;
}

static int nxp_serdev_resume(struct device *dev)
{
	struct btnxpuart_dev *nxpdev = dev_get_drvdata(dev);
	struct ps_data *psdata = &nxpdev->psdata;

	if (psdata->wakeup_source) {
		disable_irq(psdata->irq_handler);
		disable_irq_wake(psdata->irq_handler);
	}

	ps_control(psdata->hdev, PS_STATE_AWAKE);
	return 0;
}
#endif

#ifdef CONFIG_DEV_COREDUMP
static void nxp_serdev_coredump(struct device *dev)
{
	struct btnxpuart_dev *nxpdev = dev_get_drvdata(dev);
	struct hci_dev  *hdev = nxpdev->hdev;

	if (hdev->dump.coredump)
		hdev->dump.coredump(hdev);
}
#endif

static struct btnxpuart_data w8987_data __maybe_unused = {
	.helper_fw_name = NULL,
	.fw_name = FIRMWARE_W8987,
	.fw_name_old = FIRMWARE_W8987_OLD,
};

static struct btnxpuart_data w8997_data __maybe_unused = {
	.helper_fw_name = FIRMWARE_HELPER,
	.fw_name = FIRMWARE_W8997,
	.fw_name_old = FIRMWARE_W8997_OLD,
};

static const struct of_device_id nxpuart_of_match_table[] __maybe_unused = {
	{ .compatible = "nxp,88w8987-bt", .data = &w8987_data },
	{ .compatible = "nxp,88w8997-bt", .data = &w8997_data },
	{ }
};
MODULE_DEVICE_TABLE(of, nxpuart_of_match_table);

static const struct dev_pm_ops nxp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(nxp_serdev_suspend, nxp_serdev_resume)
};

static struct serdev_device_driver nxp_serdev_driver = {
	.probe = nxp_serdev_probe,
	.remove = nxp_serdev_remove,
	.driver = {
		.name = "btnxpuart",
		.of_match_table = of_match_ptr(nxpuart_of_match_table),
		.pm = &nxp_pm_ops,
#ifdef CONFIG_DEV_COREDUMP
		.coredump = nxp_serdev_coredump,
#endif
	},
};

module_serdev_device_driver(nxp_serdev_driver);

MODULE_AUTHOR("Neeraj Sanjay Kale <neeraj.sanjaykale@nxp.com>");
MODULE_DESCRIPTION("NXP Bluetooth Serial driver");
MODULE_LICENSE("GPL");
