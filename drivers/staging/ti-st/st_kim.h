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
/*
 * legacy rfkill support where-in 3 rfkill
 * devices are created for the 3 gpios
 * that ST has requested
 */
#define LEGACY_RFKILL_SUPPORT
/*
 * header file for ST provided by KIM
 */
struct kim_data_s {
	long uim_pid;
	struct platform_device *kim_pdev;
	struct completion kim_rcvd, ldisc_installed;
	/* MAX len of the .bts firmware script name */
	char resp_buffer[30];
	const struct firmware *fw_entry;
	long gpios[ST_MAX];
	struct kobject *kim_kobj;
/* used by kim_int_recv to validate fw response */
	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;
#ifdef LEGACY_RFKILL_SUPPORT
	struct rfkill *rfkill[ST_MAX];
	enum proto_type rf_protos[ST_MAX];
#endif
	struct st_data_s *core_data;
};

long st_kim_start(void);
long st_kim_stop(void);
/*
 * called from st_tty_receive to authenticate fw_download
 */
void st_kim_recv(void *, const unsigned char *, long count);

void st_kim_chip_toggle(enum proto_type, enum kim_gpio_state);

void st_kim_complete(void);

/* function called from ST KIM to ST Core, to
 * list out the protocols registered
 */
void kim_st_list_protocols(struct st_data_s *, char *);

/*
 * BTS headers
 */
#define ACTION_SEND_COMMAND     1
#define ACTION_WAIT_EVENT       2
#define ACTION_SERIAL           3
#define ACTION_DELAY            4
#define ACTION_RUN_SCRIPT       5
#define ACTION_REMARKS          6

/*
 *  * BRF Firmware header
 *   */
struct bts_header {
	uint32_t magic;
	uint32_t version;
	uint8_t future[24];
	uint8_t actions[0];
} __attribute__ ((packed));

/*
 *  * BRF Actions structure
 *   */
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

/* for identifying the change speed HCI VS
 * command
 */
struct hci_command {
	uint8_t prefix;
	uint16_t opcode;
	uint8_t plen;
	uint32_t speed;
} __attribute__ ((packed));


#endif /* ST_KIM_H */
