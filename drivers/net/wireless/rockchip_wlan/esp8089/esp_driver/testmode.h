/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __TEST_MODE
#define __TEST_MODE

enum {
        TEST_CMD_UNSPEC,
        TEST_CMD_ECHO,
        TEST_CMD_ASK,
        TEST_CMD_SLEEP,
        TEST_CMD_WAKEUP,
        TEST_CMD_LOOPBACK,
        TEST_CMD_TX,
        TEST_CMD_RX,
        TEST_CMD_DEBUG,
        TEST_CMD_SDIO_WR,
        TEST_CMD_SDIO_RD,
        TEST_CMD_ATE,
        TEST_CMD_SDIOTEST,
        TEST_CMD_SDIOSPEED,
        __TEST_CMD_MAX,
};
#define TEST_CMD_MAX (__TEST_CMD_MAX - 1)

enum {
        TEST_ATTR_UNSPEC,
        TEST_ATTR_CMD_NAME,
        TEST_ATTR_CMD_TYPE,
        TEST_ATTR_PARA_NUM,
        TEST_ATTR_PARA0,
        TEST_ATTR_PARA1,
        TEST_ATTR_PARA2,
        TEST_ATTR_PARA3,
        TEST_ATTR_PARA4,
        TEST_ATTR_PARA5,
        TEST_ATTR_PARA6,
        TEST_ATTR_PARA7,
        TEST_ATTR_STR,
        __TEST_ATTR_MAX,
};
#define TEST_ATTR_MAX (__TEST_ATTR_MAX - 1)
#define TEST_ATTR_PARA(i) (TEST_ATTR_PARA0+(i))

enum {
	RD_REG = 0,
	WR_REG,
	SET_SENSE,
	SET_TX_RATE,
	SET_TX_FREQ,
	TKIP_MIC_ERROR,
	RIFS_CTRL,
	BACKOFF,
	SET_RXSENSE,
	CONFIGURE_TRC,
	RDPER,
	RDRSSI,
	DBGTRC,
	WRMEM,
	RDMEM
};

u32 get_loopback_num(void);
u32 get_loopback_id(void);
void inc_loopback_id(void);

void esp_test_ate_done_cb(char *ep);

struct sdiotest_param {
	atomic_t start;
	u32 mode; //1: read 2: write 3: read&write
	u32 addr;
	u32 idle_period; //in msec
	struct task_struct *thread;
};

#endif //__TEST_MODE


