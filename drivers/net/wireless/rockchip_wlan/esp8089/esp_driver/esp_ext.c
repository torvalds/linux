/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *   extended gpio
 *    - interface for other driver or kernel
 *    - gpio control
 *
 */

#ifdef USE_EXT_GPIO

#include <net/cfg80211.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/version.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include <linux/completion.h>

#include "esp_ext.h"
#include "esp_debug.h"
#include "esp_sip.h"
#include "esp_sif.h"

#ifdef EXT_GPIO_OPS
extern void register_ext_gpio_ops(struct esp_ext_gpio_ops *ops);
extern void unregister_ext_gpio_ops(void);

static struct esp_ext_gpio_ops ext_gpio_ops = {
        .gpio_request    = ext_gpio_request,                             /* gpio_request gpio_no from 0x0 to 0xf*/
        .gpio_release    = ext_gpio_release,                             /* gpio_release */
        .gpio_set_mode   = ext_gpio_set_mode,                            /* gpio_set_mode, data is irq_func of irq_mode , default level of output_mode */
        .gpio_get_mode   = ext_gpio_get_mode,                            /* gpio_get_mode, current mode */
        .gpio_set_state  = ext_gpio_set_output_state,                    /* only output state, high level or low level */
        .gpio_get_state  = ext_gpio_get_state,                           /* current state */
        .irq_ack         = ext_irq_ack,                                  /* ack interrupt */
};


#endif

static struct esp_pub *ext_epub = NULL;

static u16 intr_mask_reg = 0x0000;
struct workqueue_struct *ext_irq_wkq = NULL;
struct work_struct ext_irq_work;
static struct mutex ext_mutex_lock;

static struct ext_gpio_info gpio_list[EXT_GPIO_MAX_NUM] = {
	{ 0, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 1, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 2, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 3, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 4, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 5, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 6, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 7, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 8, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{ 9, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{10, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{11, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{12, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{13, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{14, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
	{15, EXT_GPIO_MODE_DISABLE, EXT_GPIO_STATE_IDLE, NULL},
};

static struct pending_intr_list_info esp_pending_intr_list = {
	.start_pos = 0,
	.end_pos = 0,
	.curr_num = 0,
};

u16 ext_gpio_get_int_mask_reg(void)
{
	return intr_mask_reg;
}

int ext_gpio_request(int gpio_no)
{
	if (ext_epub == NULL || ext_epub->sip == NULL ||
			atomic_read(&ext_epub->sip->state) != SIP_RUN) {
		esp_dbg(ESP_DBG_ERROR, "%s esp state is not ok\n", __func__);
		return -ENOTRECOVERABLE;
	}

	mutex_lock(&ext_mutex_lock);

	if (gpio_no >= EXT_GPIO_MAX_NUM || gpio_no < 0) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s unkown gpio num\n", __func__);
		return -ERANGE;
	}

	if (gpio_list[gpio_no].gpio_mode != EXT_GPIO_MODE_DISABLE) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s gpio is already in used by other\n", __func__);
		return -EPERM;
	} else {
		gpio_list[gpio_no].gpio_mode = EXT_GPIO_MODE_MAX;
		mutex_unlock(&ext_mutex_lock);
		return 0;
	}
}
EXPORT_SYMBOL(ext_gpio_request);
	
int ext_gpio_release(int gpio_no)
{
	int ret;

	if (ext_epub == NULL || ext_epub->sip == NULL ||
			atomic_read(&ext_epub->sip->state) != SIP_RUN) {
		esp_dbg(ESP_DBG_ERROR, "%s esp state is not ok\n", __func__);
		return -ENOTRECOVERABLE;
	}

	mutex_lock(&ext_mutex_lock);

	if (gpio_no >= EXT_GPIO_MAX_NUM || gpio_no < 0) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s unkown gpio num\n", __func__);
		return -ERANGE;
	}
	sif_lock_bus(ext_epub);
	ret = sif_config_gpio_mode(ext_epub, (u8)gpio_no, EXT_GPIO_MODE_DISABLE);
	sif_unlock_bus(ext_epub);	
	if (ret) {
		esp_dbg(ESP_DBG_ERROR, "%s gpio release error\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return ret;
	}

	gpio_list[gpio_no].gpio_mode  = EXT_GPIO_MODE_DISABLE;
	gpio_list[gpio_no].gpio_state  = EXT_GPIO_STATE_IDLE;
	gpio_list[gpio_no].irq_handler = NULL;
	intr_mask_reg &= ~(1<<gpio_no);
	
	mutex_unlock(&ext_mutex_lock);

	return 0;
}
EXPORT_SYMBOL(ext_gpio_release);

int ext_gpio_set_mode(int gpio_no, int mode, void *data)
{
	u8 gpio_mode;
	int ret;
	struct ext_gpio_info backup_info;

	if (ext_epub == NULL || ext_epub->sip == NULL ||
			atomic_read(&ext_epub->sip->state) != SIP_RUN) {
		esp_dbg(ESP_DBG_LOG, "%s esp state is not ok\n", __func__);
		return -ENOTRECOVERABLE;
	}

	mutex_lock(&ext_mutex_lock);

	if (gpio_no >= EXT_GPIO_MAX_NUM || gpio_no < 0) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s unkown gpio num\n", __func__);
		return -ERANGE;
	}

	if (gpio_list[gpio_no].gpio_mode == EXT_GPIO_MODE_DISABLE) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s gpio is not in occupy, please request gpio\n", __func__);
		return -ENOTRECOVERABLE;
	}

	if (mode <= EXT_GPIO_MODE_OOB || mode >= EXT_GPIO_MODE_MAX) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s gpio mode unknown\n", __func__);
		return -EOPNOTSUPP;
	}

	memcpy(&backup_info, &gpio_list[gpio_no], sizeof(struct ext_gpio_info));

	gpio_list[gpio_no].gpio_mode = mode;
	gpio_mode = (u8)mode;

	switch (mode) {
		case EXT_GPIO_MODE_INTR_POSEDGE:
		case EXT_GPIO_MODE_INTR_NEGEDGE:
		case EXT_GPIO_MODE_INTR_LOLEVEL:
		case EXT_GPIO_MODE_INTR_HILEVEL:
			if (!data) {
				memcpy(&gpio_list[gpio_no], &backup_info, sizeof(struct ext_gpio_info));
				esp_dbg(ESP_DBG_ERROR, "%s irq_handler is NULL\n", __func__);
				mutex_unlock(&ext_mutex_lock);
				return -EINVAL;
			}
			gpio_list[gpio_no].irq_handler = (ext_irq_handler_t)data;
			intr_mask_reg |= (1<<gpio_no);
			break;
		case EXT_GPIO_MODE_OUTPUT:
			if (!data) {
				memcpy(&gpio_list[gpio_no], &backup_info, sizeof(struct ext_gpio_info));
				esp_dbg(ESP_DBG_ERROR, "%s output default value is NULL\n", __func__);
				mutex_unlock(&ext_mutex_lock);
				return -EINVAL;
			}
			*(int *)data = (*(int *)data == 0 ? 0 : 1);
			gpio_mode = (u8)(((*(int *)data)<<4) | gpio_mode);
		default:
			gpio_list[gpio_no].irq_handler = NULL;
			intr_mask_reg &= ~(1<<gpio_no);
			break;
	}

	sif_lock_bus(ext_epub);
	ret = sif_config_gpio_mode(ext_epub, (u8)gpio_no, gpio_mode);
	sif_unlock_bus(ext_epub);
	if (ret) {
		memcpy(&gpio_list[gpio_no], &backup_info, sizeof(struct ext_gpio_info));
		esp_dbg(ESP_DBG_ERROR, "%s gpio set error\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return ret;
	}

	mutex_unlock(&ext_mutex_lock);
	return 0;
}
EXPORT_SYMBOL(ext_gpio_set_mode);

int ext_gpio_get_mode(int gpio_no)
{
	int gpio_mode;

	if (ext_epub == NULL || ext_epub->sip == NULL ||
			atomic_read(&ext_epub->sip->state) != SIP_RUN) {
		esp_dbg(ESP_DBG_LOG, "%s esp state is not ok\n", __func__);
		return -ENOTRECOVERABLE;
	}

	mutex_lock(&ext_mutex_lock);

	if (gpio_no >= EXT_GPIO_MAX_NUM || gpio_no < 0) {
		esp_dbg(ESP_DBG_ERROR, "%s unkown gpio num\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return -ERANGE;
	}

	gpio_mode = gpio_list[gpio_no].gpio_mode;

	mutex_unlock(&ext_mutex_lock);

	return gpio_mode;
}
EXPORT_SYMBOL(ext_gpio_get_mode);


int ext_gpio_set_output_state(int gpio_no, int state)
{
	int ret;

	if (ext_epub == NULL || ext_epub->sip == NULL ||
			atomic_read(&ext_epub->sip->state) != SIP_RUN) {
		esp_dbg(ESP_DBG_LOG, "%s esp state is not ok\n", __func__);
		return -ENOTRECOVERABLE;
	}

	mutex_lock(&ext_mutex_lock);

	if (gpio_no >= EXT_GPIO_MAX_NUM || gpio_no < 0) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s unkown gpio num\n", __func__);
		return -ERANGE;
	}

	if (gpio_list[gpio_no].gpio_mode != EXT_GPIO_MODE_OUTPUT) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s gpio is not in output state, please request gpio or set output state\n", __func__);
		return -EOPNOTSUPP;
	}

	if (state != EXT_GPIO_STATE_LOW && state != EXT_GPIO_STATE_HIGH) {
		mutex_unlock(&ext_mutex_lock);
		esp_dbg(ESP_DBG_ERROR, "%s gpio state unknown\n", __func__);
		return -ENOTRECOVERABLE;
	}

	sif_lock_bus(ext_epub);
	ret = sif_set_gpio_output(ext_epub, 1<<gpio_no, state<<gpio_no);
	sif_unlock_bus(ext_epub);	
	if (ret) {
		esp_dbg(ESP_DBG_ERROR, "%s gpio state set error\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return ret;
	}
	gpio_list[gpio_no].gpio_state = state;
	
	mutex_unlock(&ext_mutex_lock);

	return 0;
}
EXPORT_SYMBOL(ext_gpio_set_output_state);

int ext_gpio_get_state(int gpio_no)
{
	int ret;
	u16 state;
	u16 mask;

	if (ext_epub == NULL || ext_epub->sip == NULL ||
			atomic_read(&ext_epub->sip->state) != SIP_RUN) {
		esp_dbg(ESP_DBG_LOG, "%s esp state is not ok\n", __func__);
		return -ENOTRECOVERABLE;
	}

	mutex_lock(&ext_mutex_lock);

	if (gpio_no >= EXT_GPIO_MAX_NUM || gpio_no < 0) {
		esp_dbg(ESP_DBG_ERROR, "%s unkown gpio num\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return -ERANGE;
	}

	if (gpio_list[gpio_no].gpio_mode == EXT_GPIO_MODE_OUTPUT) {
		state = gpio_list[gpio_no].gpio_state;
	 } else if (gpio_list[gpio_no].gpio_mode == EXT_GPIO_MODE_INPUT) {
		sif_lock_bus(ext_epub);
		ret = sif_get_gpio_input(ext_epub, &mask, &state);
		sif_unlock_bus(ext_epub);
		if (ret) {
			esp_dbg(ESP_DBG_ERROR, "%s get gpio_input state error\n", __func__);
			mutex_unlock(&ext_mutex_lock);
			return ret;
		}	
	 } else {
		esp_dbg(ESP_DBG_ERROR, "%s gpio_state is not input or output\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return -EOPNOTSUPP;
	}
	mutex_unlock(&ext_mutex_lock);

	return (state & (1<<gpio_no)) ? 1 : 0;
}
EXPORT_SYMBOL(ext_gpio_get_state);

int ext_irq_ack(int gpio_no)
{
	int ret;

	if (ext_epub == NULL || ext_epub->sip == NULL ||
			atomic_read(&ext_epub->sip->state) != SIP_RUN) {
		esp_dbg(ESP_DBG_LOG, "%s esp state is not ok\n", __func__);
		return -ENOTRECOVERABLE;
	}

	mutex_lock(&ext_mutex_lock);
	if (gpio_no >= EXT_GPIO_MAX_NUM || gpio_no < 0) {
		esp_dbg(ESP_DBG_ERROR, "%s unkown gpio num\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return -ERANGE;
	}

	if (gpio_list[gpio_no].gpio_mode != EXT_GPIO_MODE_INTR_POSEDGE 
			&& gpio_list[gpio_no].gpio_mode != EXT_GPIO_MODE_INTR_NEGEDGE
			&& gpio_list[gpio_no].gpio_mode != EXT_GPIO_MODE_INTR_LOLEVEL
			&& gpio_list[gpio_no].gpio_mode != EXT_GPIO_MODE_INTR_HILEVEL) {
		esp_dbg(ESP_DBG_ERROR, "%s gpio mode is not intr mode\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return -ENOTRECOVERABLE;
	}

	sif_lock_bus(ext_epub);
	ret = sif_set_gpio_output(ext_epub, 0x00, 1<<gpio_no);
	sif_unlock_bus(ext_epub);
	if (ret) {
		esp_dbg(ESP_DBG_ERROR, "%s gpio intr ack error\n", __func__);
		mutex_unlock(&ext_mutex_lock);
		return ret;
	}

	mutex_unlock(&ext_mutex_lock);
	return 0;
}
EXPORT_SYMBOL(ext_irq_ack);

void show_status(void)
{
	int i=0;
	for (i = 0; i < MAX_PENDING_INTR_LIST;i++)
		esp_dbg(ESP_DBG_ERROR, "status[%d] = [0x%04x]\n", i, esp_pending_intr_list.pending_intr_list[i]);

	esp_dbg(ESP_DBG_ERROR, "start_pos[%d]\n",esp_pending_intr_list.start_pos);
	esp_dbg(ESP_DBG_ERROR, "end_pos[%d]\n",esp_pending_intr_list.end_pos);
	esp_dbg(ESP_DBG_ERROR, "curr_num[%d]\n",esp_pending_intr_list.curr_num);
	
}
void esp_tx_work(struct work_struct *work)
{
	int i;
	u16 tmp_intr_status_reg;

	esp_dbg(ESP_DBG_TRACE, "%s enter\n", __func__);

	spin_lock(&esp_pending_intr_list.spin_lock);

	tmp_intr_status_reg = esp_pending_intr_list.pending_intr_list[esp_pending_intr_list.start_pos];
	
	esp_pending_intr_list.pending_intr_list[esp_pending_intr_list.start_pos] = 0x0000;
	esp_pending_intr_list.start_pos = (esp_pending_intr_list.start_pos + 1) % MAX_PENDING_INTR_LIST;
	esp_pending_intr_list.curr_num--;
	
	spin_unlock(&esp_pending_intr_list.spin_lock);
	
	for (i = 0; i < EXT_GPIO_MAX_NUM; i++) {
		if (tmp_intr_status_reg & (1<<i) && (gpio_list[i].irq_handler))
			gpio_list[i].irq_handler();
	}

	spin_lock(&esp_pending_intr_list.spin_lock);
	if (esp_pending_intr_list.curr_num > 0)
		queue_work(ext_irq_wkq, &ext_irq_work);
	spin_unlock(&esp_pending_intr_list.spin_lock);
}

void ext_gpio_int_process(u16 value) {
	if (value == 0x00)
		return;

	esp_dbg(ESP_DBG_TRACE, "%s enter\n", __func__);

	/* intr cycle queue is full, wait */
	while (esp_pending_intr_list.curr_num >= MAX_PENDING_INTR_LIST)
	{
		udelay(1);
	}

	spin_lock(&esp_pending_intr_list.spin_lock);
	
	esp_pending_intr_list.pending_intr_list[esp_pending_intr_list.end_pos] = value;
	esp_pending_intr_list.end_pos = (esp_pending_intr_list.end_pos + 1) % MAX_PENDING_INTR_LIST;
	esp_pending_intr_list.curr_num++;

	queue_work(ext_irq_wkq, &ext_irq_work);
	
	spin_unlock(&esp_pending_intr_list.spin_lock);
}

int ext_gpio_init(struct esp_pub *epub)
{
	esp_dbg(ESP_DBG_ERROR, "%s enter\n", __func__);

	ext_irq_wkq = create_singlethread_workqueue("esp_ext_irq_wkq");
	if (ext_irq_wkq == NULL) {
		esp_dbg(ESP_DBG_ERROR, "%s create workqueue error\n", __func__);
		return -EACCES;
	}

	INIT_WORK(&ext_irq_work, esp_tx_work);
	mutex_init(&ext_mutex_lock);

	ext_epub = epub;

	if (ext_epub == NULL)
		return -EINVAL;

#ifdef EXT_GPIO_OPS
	register_ext_gpio_ops(&ext_gpio_ops);
#endif
	
	return 0;
}

void ext_gpio_deinit(void)
{
	esp_dbg(ESP_DBG_ERROR, "%s enter\n", __func__);

#ifdef EXT_GPIO_OPS
	unregister_ext_gpio_ops();
#endif
	ext_epub = NULL;
        cancel_work_sync(&ext_irq_work);

	if (ext_irq_wkq)
		destroy_workqueue(ext_irq_wkq);

}

#endif /* USE_EXT_GPIO */
