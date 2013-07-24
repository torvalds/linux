/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _GDM_TTY_H_
#define _GDM_TTY_H_

#include <linux/version.h>
#include <linux/types.h>
#include <linux/tty.h>


#define TTY_MAX_COUNT		2

#define MAX_ISSUE_NUM 3

enum TO_HOST_RESULT {
	TO_HOST_BUFFER_REQUEST_FAIL = 1,
	TO_HOST_PORT_CLOSE = 2,
	TO_HOST_INVALID_PACKET = 3,
};

enum RECV_PACKET_PROCESS {
	RECV_PACKET_PROCESS_COMPLETE = 0,
	RECV_PACKET_PROCESS_CONTINUE = 1,
};

struct tty_dev {
	void *priv_dev;
	int (*send_func)(void *priv_dev, void *data, int len, int tty_index,
			void (*cb)(void *cb_data), void *cb_data);
	int (*recv_func)(void *priv_dev, int (*cb)(void *data, int len,
			 int tty_index, int minor, int complete));
	int (*send_control)(void *priv_dev, int request, int value, void *data,
			    int len);
	u8 minor[2];
};

struct tty_str {
	struct tty_dev *tty_dev;
	int tty_drv_index;
	struct tty_port port;
};

int register_lte_tty_driver(void);
void unregister_lte_tty_driver(void);
int register_lte_tty_device(struct tty_dev *tty_dev, struct device *dev);
void unregister_lte_tty_device(struct tty_dev *tty_dev);

#endif /* _GDM_USB_H_ */

