/*
 *  Shared Transport Line discipline driver Core
 *	Init Manager Module header file
 *  Copyright (C) 2009 Texas Instruments
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef ST_KIM_H
#define ST_KIM_H

#include <linux/types.h>
#include "st.h"
#include "st_core.h"
#include "st_ll.h"
#include <linux/rfkill.h>

/* time in msec to wait for
 * line discipline to be installed
 */
#define LDISC_TIME	500
#define CMD_RESP_TIME	500
#define MAKEWORD(a, b)  ((unsigned short)(((unsigned char)(a)) \
	| ((unsigned short)((unsigned char)(b))) << 8))

#define GPIO_HIGH 1
#define GPIO_LOW  0

/* the Power-On-Reset logic, requires to attempt
 * to download firmware onto chip more than once
 * since the self-test for chip takes a while
 */
#define POR_RETRY_COUNT 5

/**
 * struct chip_version - save the chip version
 */
struct chip_version {
	unsigned short full;
	unsigned short chip;
	unsigned short min_ver;
	unsigned short maj_ver;
};

/**
 * struct kim_data_s - the KIM internal data, embedded as the
 *	platform's drv data. One for each ST device in the system.
 * @uim_pid: KIM needs to communicate with UIM to request to install
 *	the ldisc by opening UART when protocol drivers register.
 * @kim_pdev: the platform device added in one of the board-XX.c file
 *	in arch/XX/ directory, 1 for each ST device.
 * @kim_rcvd: completion handler to notify when data was received,
 *	mainly used during fw download, which involves multiple send/wait
 *	for each of the HCI-VS commands.
 * @ldisc_installed: completion handler to notify that the UIM accepted
 *	the request to install ldisc, notify from tty_open which suggests
 *	the ldisc was properly installed.
 * @resp_buffer: data buffer for the .bts fw file name.
 * @fw_entry: firmware class struct to request/release the fw.
 * @gpios: the list of core/chip enable gpios for BT, FM and GPS cores.
 * @rx_state: the rx state for kim's receive func during fw download.
 * @rx_count: the rx count for the kim's receive func during fw download.
 * @rx_skb: all of fw data might not come at once, and hence data storage for
 *	whole of the fw response, only HCI_EVENTs and hence diff from ST's
 *	response.
 * @rfkill: rfkill data for each of the cores to be registered with rfkill.
 * @rf_protos: proto types of the data registered with rfkill sub-system.
 * @core_data: ST core's data, which mainly is the tty's disc_data
 * @version: chip version available via a sysfs entry.
 *
 */
struct kim_data_s {
	long uim_pid;
	struct platform_device *kim_pdev;
	struct completion kim_rcvd, ldisc_installed;
	char resp_buffer[30];
	const struct firmware *fw_entry;
	long gpios[ST_MAX];
	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;
	struct rfkill *rfkill[ST_MAX];
	enum proto_type rf_protos[ST_MAX];
	struct st_data_s *core_data;
	struct chip_version version;
};

/**
 * functions called when 1 of the protocol drivers gets
 * registered, these need to communicate with UIM to request
 * ldisc installed, read chip_version, download relevant fw
 */
long st_kim_start(void *);
long st_kim_stop(void *);

void st_kim_recv(void *, const unsigned char *, long count);
void st_kim_chip_toggle(enum proto_type, enum kim_gpio_state);
void st_kim_complete(void *);
void kim_st_list_protocols(struct st_data_s *, void *);

/*
 * BTS headers
 */
#define ACTION_SEND_COMMAND     1
#define ACTION_WAIT_EVENT       2
#define ACTION_SERIAL           3
#define ACTION_DELAY            4
#define ACTION_RUN_SCRIPT       5
#define ACTION_REMARKS          6

/**
 * struct bts_header - the fw file is NOT binary which can
 *	be sent onto TTY as is. The .bts is more a script
 *	file which has different types of actions.
 *	Each such action needs to be parsed by the KIM and
 *	relevant procedure to be called.
 */
struct bts_header {
	uint32_t magic;
	uint32_t version;
	uint8_t future[24];
	uint8_t actions[0];
} __attribute__ ((packed));

/**
 * struct bts_action - Each .bts action has its own type of
 *	data.
 */
struct bts_action {
	uint16_t type;
	uint16_t size;
	uint8_t data[0];
} __attribute__ ((packed));

struct bts_action_send {
	uint8_t data[0];
} __attribute__ ((packed));

struct bts_action_wait {
	uint32_t msec;
	uint32_t size;
	uint8_t data[0];
} __attribute__ ((packed));

struct bts_action_delay {
	uint32_t msec;
} __attribute__ ((packed));

struct bts_action_serial {
	uint32_t baud;
	uint32_t flow_control;
} __attribute__ ((packed));

/**
 * struct hci_command - the HCI-VS for intrepreting
 *	the change baud rate of host-side UART, which
 *	needs to be ignored, since UIM would do that
 *	when it receives request from KIM for ldisc installation.
 */
struct hci_command {
	uint8_t prefix;
	uint16_t opcode;
	uint8_t plen;
	uint32_t speed;
} __attribute__ ((packed));


#endif /* ST_KIM_H */
