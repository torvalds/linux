/* linux/drivers/media/video/samsung/tvout/hw_if/hw_if.h
 *
 * Copyright (c) 2010 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Header file for interface of Samsung TVOUT-related hardware
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _SAMSUNG_TVOUT_CEC_H_
#define _SAMSUNG_TVOUT_CEC_H_ __FILE__

/*****************************************************************************
 * This file includes declarations for external functions of
 * Samsung TVOUT-related hardware. So only external functions
 * to be used by higher layer must exist in this file.
 *
 * Higher layer must use only the declarations included in this file.
 ****************************************************************************/

#define to_tvout_plat(d) (to_platform_device(d)->dev.platform_data)

#ifndef tvout_dbg
#ifdef CONFIG_TV_DEBUG
#define tvout_dbg(fmt, ...)					\
		printk(KERN_INFO "[%s] %s(): " fmt,		\
			DRV_NAME, __func__, ##__VA_ARGS__)
#else
#define tvout_dbg(fmt, ...)
#endif
#endif

enum s5p_tvout_endian {
	TVOUT_LITTLE_ENDIAN = 0,
	TVOUT_BIG_ENDIAN = 1
};

enum cec_state {
	STATE_RX,
	STATE_TX,
	STATE_DONE,
	STATE_ERROR
};

struct cec_rx_struct {
	spinlock_t lock;
	wait_queue_head_t waitq;
	atomic_t state;
	u8 *buffer;
	unsigned int size;
};

struct cec_tx_struct {
	wait_queue_head_t waitq;
	atomic_t state;
};

extern struct cec_rx_struct cec_rx_struct;
extern struct cec_tx_struct cec_tx_struct;

void s5p_cec_set_divider(void);
void s5p_cec_enable_rx(void);
void s5p_cec_mask_rx_interrupts(void);
void s5p_cec_unmask_rx_interrupts(void);
void s5p_cec_mask_tx_interrupts(void);
void s5p_cec_unmask_tx_interrupts(void);
void s5p_cec_reset(void);
void s5p_cec_tx_reset(void);
void s5p_cec_rx_reset(void);
void s5p_cec_threshold(void);
void s5p_cec_set_tx_state(enum cec_state state);
void s5p_cec_set_rx_state(enum cec_state state);
void s5p_cec_copy_packet(char *data, size_t count);
void s5p_cec_set_addr(u32 addr);
u32 s5p_cec_get_status(void);
void s5p_clr_pending_tx(void);
void s5p_clr_pending_rx(void);
void s5p_cec_get_rx_buf(u32 size, u8 *buffer);
int __init s5p_cec_mem_probe(struct platform_device *pdev);

#endif /* _SAMSUNG_TVOUT_CEC_H_ */
