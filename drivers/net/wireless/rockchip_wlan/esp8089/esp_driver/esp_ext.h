/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifdef USE_EXT_GPIO

#ifndef _ESP_EXT_H_
#define _ESP_EXT_H_

#include <linux/interrupt.h>
#include <linux/module.h>
#include "esp_sip.h"

#define MAX_PENDING_INTR_LIST 16

#ifdef EXT_GPIO_OPS
typedef struct esp_ext_gpio_ops {
        int (*gpio_request)(int gpio_no);                             /* gpio_request gpio_no from 0x0 to 0xf*/
        int (*gpio_release)(int gpio_no);                             /* gpio_release */
        int (*gpio_set_mode)(int gpio_no, int mode , void *data);     /* gpio_set_mode, data is irq_func of irq_mode , default level of output_mode */
        int (*gpio_get_mode)(int gpio_no);                            /* gpio_get_mode, current mode */
        int (*gpio_set_state)(int gpio_no, int state);                /* only output state, high level or low level */
        int (*gpio_get_state)(int gpio_no);                           /* current state */
	int (*irq_ack)(int gpio_no);                                  /* ack interrupt */
} esp_ext_gpio_ops_t;
#endif

typedef enum EXT_GPIO_NO {
	EXT_GPIO_GPIO0 = 0,
	EXT_GPIO_U0TXD,
	EXT_GPIO_GPIO2,
	EXT_GPIO_U0RXD,
	EXT_GPIO_GPIO4,
	EXT_GPIO_GPIO5,
	EXT_GPIO_SD_CLK,
	EXT_GPIO_SD_DATA0,
	EXT_GPIO_SD_DATA1,
	EXT_GPIO_SD_DATA2,
	EXT_GPIO_SD_DATA3,
	EXT_GPIO_SD_CMD,
	EXT_GPIO_MTDI,
	EXT_GPIO_MTCK,
	EXT_GPIO_MTMS,
	EXT_GPIO_MTDO,
	EXT_GPIO_MAX_NUM
} EXT_GPIO_NO_T;
	
typedef enum EXT_GPIO_MODE {		//dir		def	pullup	mode	wake
	EXT_GPIO_MODE_OOB = 0,		//output	1	0	n/a	n/a
	EXT_GPIO_MODE_OUTPUT,		//output	/	0	n/a	n/a
	EXT_GPIO_MODE_DISABLE,		//input		n/a	0	DIS	n/a
	EXT_GPIO_MODE_INTR_POSEDGE,	//input		n/a	0	POS	1
	EXT_GPIO_MODE_INTR_NEGEDGE,	//input		n/a	1	NEG	1
	EXT_GPIO_MODE_INPUT,		//input		n/a	0	ANY	1
	EXT_GPIO_MODE_INTR_LOLEVEL,	//input		n/a	1	LOW	1
	EXT_GPIO_MODE_INTR_HILEVEL,	//input		n/a	0	HIGH	1
	EXT_GPIO_MODE_MAX,
} EXT_GPIO_MODE_T;

typedef enum EXT_GPIO_STATE {
	EXT_GPIO_STATE_LOW,
	EXT_GPIO_STATE_HIGH,
	EXT_GPIO_STATE_IDLE
} EXT_GPIO_STATE_T;

typedef irqreturn_t (*ext_irq_handler_t)(void);

struct ext_gpio_info {
	int gpio_no;
	int gpio_mode;
	int gpio_state;
	ext_irq_handler_t irq_handler;
};

struct pending_intr_list_info {
	u16 pending_intr_list[MAX_PENDING_INTR_LIST];
	int start_pos;
	int end_pos;
	int curr_num;
	spinlock_t spin_lock;
};

u16 ext_gpio_get_int_mask_reg(void);

/* for extern user start */
int ext_gpio_request(int gpio_no);
int ext_gpio_release(int gpio_no);

int ext_gpio_set_mode(int gpio_no, int mode, void *data);
int ext_gpio_get_mode(int gpio_no);

int ext_gpio_set_output_state(int gpio_no, int state);
int ext_gpio_get_state(int gpio_no);

int ext_irq_ack(int gpio_no);
/* for extern user end */

void ext_gpio_int_process(u16 value);

int ext_gpio_init(struct esp_pub *epub);
void ext_gpio_deinit(void);
#endif /* _ESP_EXT_H_ */

#endif /* USE_EXT_GPIO */
