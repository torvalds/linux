/*
 * Marvell Bluetooth driver: global definitions & declarations
 *
 * Copyright (C) 2009, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

#include <linux/kthread.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <net/bluetooth/bluetooth.h>
#include <linux/ctype.h>
#include <linux/firmware.h>

#define BTM_HEADER_LEN			4
#define BTM_UPLD_SIZE			2312

/* Time to wait until Host Sleep state change in millisecond */
#define WAIT_UNTIL_HS_STATE_CHANGED	5000
/* Time to wait for command response in millisecond */
#define WAIT_UNTIL_CMD_RESP		5000

struct btmrvl_thread {
	struct task_struct *task;
	wait_queue_head_t wait_q;
	void *priv;
};

struct btmrvl_device {
	void *card;
	struct hci_dev *hcidev;
	struct device *dev;
	const char *cal_data;

	u8 dev_type;

	u8 tx_dnld_rdy;

	u8 psmode;
	u8 pscmd;
	u8 hsmode;
	u8 hscmd;

	/* Low byte is gap, high byte is GPIO */
	u16 gpio_gap;

	u8 hscfgcmd;
	u8 sendcmdflag;
};

struct btmrvl_adapter {
	u32 int_count;
	struct sk_buff_head tx_queue;
	u8 psmode;
	u8 ps_state;
	u8 hs_state;
	u8 wakeup_tries;
	wait_queue_head_t cmd_wait_q;
	u8 cmd_complete;
	bool is_suspended;
};

struct btmrvl_private {
	struct btmrvl_device btmrvl_dev;
	struct btmrvl_adapter *adapter;
	struct btmrvl_thread main_thread;
	int (*hw_host_to_card) (struct btmrvl_private *priv,
				u8 *payload, u16 nb);
	int (*hw_wakeup_firmware) (struct btmrvl_private *priv);
	int (*hw_process_int_status) (struct btmrvl_private *priv);
	spinlock_t driver_lock;		/* spinlock used by driver */
#ifdef CONFIG_DEBUG_FS
	void *debugfs_data;
#endif
};

#define MRVL_VENDOR_PKT			0xFE

/* Bluetooth commands  */
#define BT_CMD_AUTO_SLEEP_MODE		0x23
#define BT_CMD_HOST_SLEEP_CONFIG	0x59
#define BT_CMD_HOST_SLEEP_ENABLE	0x5A
#define BT_CMD_MODULE_CFG_REQ		0x5B
#define BT_CMD_LOAD_CONFIG_DATA		0x61

/* Sub-commands: Module Bringup/Shutdown Request/Response */
#define MODULE_BRINGUP_REQ		0xF1
#define MODULE_BROUGHT_UP		0x00
#define MODULE_ALREADY_UP		0x0C

#define MODULE_SHUTDOWN_REQ		0xF2

#define BT_EVENT_POWER_STATE		0x20

/* Bluetooth Power States */
#define BT_PS_ENABLE			0x02
#define BT_PS_DISABLE			0x03
#define BT_PS_SLEEP			0x01

#define OGF				0x3F

/* Host Sleep states */
#define HS_ACTIVATED			0x01
#define HS_DEACTIVATED			0x00

/* Power Save modes */
#define PS_SLEEP			0x01
#define PS_AWAKE			0x00

#define BT_CMD_DATA_SIZE		32
#define BT_CAL_DATA_SIZE		28

struct btmrvl_event {
	u8 ec;		/* event counter */
	u8 length;
	u8 data[4];
} __packed;

/* Prototype of global function */

int btmrvl_register_hdev(struct btmrvl_private *priv);
struct btmrvl_private *btmrvl_add_card(void *card);
int btmrvl_remove_card(struct btmrvl_private *priv);

void btmrvl_interrupt(struct btmrvl_private *priv);

bool btmrvl_check_evtpkt(struct btmrvl_private *priv, struct sk_buff *skb);
int btmrvl_process_event(struct btmrvl_private *priv, struct sk_buff *skb);

int btmrvl_send_module_cfg_cmd(struct btmrvl_private *priv, int subcmd);
int btmrvl_send_hscfg_cmd(struct btmrvl_private *priv);
int btmrvl_enable_ps(struct btmrvl_private *priv);
int btmrvl_prepare_command(struct btmrvl_private *priv);
int btmrvl_enable_hs(struct btmrvl_private *priv);

#ifdef CONFIG_DEBUG_FS
void btmrvl_debugfs_init(struct hci_dev *hdev);
void btmrvl_debugfs_remove(struct hci_dev *hdev);
#endif
