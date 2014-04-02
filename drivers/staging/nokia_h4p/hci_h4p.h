/*
 * This file is part of Nokia H4P bluetooth driver
 *
 * Copyright (C) 2005-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __DRIVERS_BLUETOOTH_HCI_H4P_H
#define __DRIVERS_BLUETOOTH_HCI_H4P_H

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>

#define UART_SYSC_OMAP_RESET	0x03
#define UART_SYSS_RESETDONE	0x01
#define UART_OMAP_SCR_EMPTY_THR	0x08
#define UART_OMAP_SCR_WAKEUP	0x10
#define UART_OMAP_SSR_WAKEUP	0x02
#define UART_OMAP_SSR_TXFULL	0x01

#define UART_OMAP_SYSC_IDLEMODE		0x03
#define UART_OMAP_SYSC_IDLEMASK		(3 << UART_OMAP_SYSC_IDLEMODE)

#define UART_OMAP_SYSC_FORCE_IDLE	(0 << UART_OMAP_SYSC_IDLEMODE)
#define UART_OMAP_SYSC_NO_IDLE		(1 << UART_OMAP_SYSC_IDLEMODE)
#define UART_OMAP_SYSC_SMART_IDLE	(2 << UART_OMAP_SYSC_IDLEMODE)

#define H4P_TRANSFER_MODE		1
#define H4P_SCHED_TRANSFER_MODE		2
#define H4P_ACTIVE_MODE			3

struct hci_h4p_info {
	struct timer_list lazy_release;
	struct hci_dev *hdev;
	spinlock_t lock;

	void __iomem *uart_base;
	unsigned long uart_phys_base;
	int irq;
	struct device *dev;
	u8 chip_type;
	u8 bt_wakeup_gpio;
	u8 host_wakeup_gpio;
	u8 reset_gpio;
	u8 reset_gpio_shared;
	u8 bt_sysclk;
	u8 man_id;
	u8 ver_id;

	struct sk_buff_head fw_queue;
	struct sk_buff *alive_cmd_skb;
	struct completion init_completion;
	struct completion fw_completion;
	struct completion test_completion;
	int fw_error;
	int init_error;

	struct sk_buff_head txq;

	struct sk_buff *rx_skb;
	long rx_count;
	unsigned long rx_state;
	unsigned long garbage_bytes;

	u8 bd_addr[6];
	struct sk_buff_head *fw_q;

	int pm_enabled;
	int tx_enabled;
	int autorts;
	int rx_enabled;
	unsigned long pm_flags;

	int tx_clocks_en;
	int rx_clocks_en;
	spinlock_t clocks_lock;
	struct clk *uart_iclk;
	struct clk *uart_fclk;
	atomic_t clk_users;
	u16 dll;
	u16 dlh;
	u16 ier;
	u16 mdr1;
	u16 efr;
};

struct hci_h4p_radio_hdr {
	__u8 evt;
	__u8 dlen;
} __packed;

struct hci_h4p_neg_hdr {
	__u8 dlen;
} __packed;
#define H4P_NEG_HDR_SIZE 1

#define H4P_NEG_REQ	0x00
#define H4P_NEG_ACK	0x20
#define H4P_NEG_NAK	0x40

#define H4P_PROTO_PKT	0x44
#define H4P_PROTO_BYTE	0x4c

#define H4P_ID_CSR	0x02
#define H4P_ID_BCM2048	0x04
#define H4P_ID_TI1271	0x31

struct hci_h4p_neg_cmd {
	__u8	ack;
	__u16	baud;
	__u16	unused1;
	__u8	proto;
	__u16	sys_clk;
	__u16	unused2;
} __packed;

struct hci_h4p_neg_evt {
	__u8	ack;
	__u16	baud;
	__u16	unused1;
	__u8	proto;
	__u16	sys_clk;
	__u16	unused2;
	__u8	man_id;
	__u8	ver_id;
} __packed;

#define H4P_ALIVE_REQ	0x55
#define H4P_ALIVE_RESP	0xcc

struct hci_h4p_alive_hdr {
	__u8	dlen;
} __packed;
#define H4P_ALIVE_HDR_SIZE 1

struct hci_h4p_alive_pkt {
	__u8	mid;
	__u8	unused;
} __packed;

#define MAX_BAUD_RATE		921600
#define BC4_MAX_BAUD_RATE	3692300
#define UART_CLOCK		48000000
#define BT_INIT_DIVIDER		320
#define BT_BAUDRATE_DIVIDER	384000000
#define BT_SYSCLK_DIV		1000
#define INIT_SPEED		120000

#define H4_TYPE_SIZE		1
#define H4_RADIO_HDR_SIZE	2

/* H4+ packet types */
#define H4_CMD_PKT		0x01
#define H4_ACL_PKT		0x02
#define H4_SCO_PKT		0x03
#define H4_EVT_PKT		0x04
#define H4_NEG_PKT		0x06
#define H4_ALIVE_PKT		0x07
#define H4_RADIO_PKT		0x08

/* TX states */
#define WAIT_FOR_PKT_TYPE	1
#define WAIT_FOR_HEADER		2
#define WAIT_FOR_DATA		3

struct hci_fw_event {
	struct hci_event_hdr hev;
	struct hci_ev_cmd_complete cmd;
	u8 status;
} __packed;

int hci_h4p_send_alive_packet(struct hci_h4p_info *info);

void hci_h4p_bcm_parse_fw_event(struct hci_h4p_info *info,
				struct sk_buff *skb);
int hci_h4p_bcm_send_fw(struct hci_h4p_info *info,
			struct sk_buff_head *fw_queue);

void hci_h4p_bc4_parse_fw_event(struct hci_h4p_info *info,
				struct sk_buff *skb);
int hci_h4p_bc4_send_fw(struct hci_h4p_info *info,
			struct sk_buff_head *fw_queue);

void hci_h4p_ti1273_parse_fw_event(struct hci_h4p_info *info,
				    struct sk_buff *skb);
int hci_h4p_ti1273_send_fw(struct hci_h4p_info *info,
			    struct sk_buff_head *fw_queue);

int hci_h4p_read_fw(struct hci_h4p_info *info, struct sk_buff_head *fw_queue);
int hci_h4p_send_fw(struct hci_h4p_info *info, struct sk_buff_head *fw_queue);
void hci_h4p_parse_fw_event(struct hci_h4p_info *info, struct sk_buff *skb);

void hci_h4p_outb(struct hci_h4p_info *info, unsigned int offset, u8 val);
u8 hci_h4p_inb(struct hci_h4p_info *info, unsigned int offset);
void hci_h4p_set_rts(struct hci_h4p_info *info, int active);
int hci_h4p_wait_for_cts(struct hci_h4p_info *info, int active, int timeout_ms);
void __hci_h4p_set_auto_ctsrts(struct hci_h4p_info *info, int on, u8 which);
void hci_h4p_set_auto_ctsrts(struct hci_h4p_info *info, int on, u8 which);
void hci_h4p_change_speed(struct hci_h4p_info *info, unsigned long speed);
int hci_h4p_reset_uart(struct hci_h4p_info *info);
void hci_h4p_init_uart(struct hci_h4p_info *info);
void hci_h4p_enable_tx(struct hci_h4p_info *info);
void hci_h4p_store_regs(struct hci_h4p_info *info);
void hci_h4p_restore_regs(struct hci_h4p_info *info);
void hci_h4p_smart_idle(struct hci_h4p_info *info, bool enable);

#endif /* __DRIVERS_BLUETOOTH_HCI_H4P_H */
