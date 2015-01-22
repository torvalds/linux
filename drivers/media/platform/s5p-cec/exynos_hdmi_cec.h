/* drivers/media/platform/s5p-cec/exynos_hdmi_cec.h
 *
 * Copyright (c) 2010, 2014 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Header file for interface of Samsung Exynos hdmi cec hardware
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _EXYNOS_HDMI_CEC_H_
#define _EXYNOS_HDMI_CEC_H_ __FILE__

#include <linux/regmap.h>
#include <linux/miscdevice.h>
#include "s5p_cec.h"

void s5p_cec_set_divider(struct s5p_cec_dev *cec);
void s5p_cec_enable_rx(struct s5p_cec_dev *cec);
void s5p_cec_mask_rx_interrupts(struct s5p_cec_dev *cec);
void s5p_cec_unmask_rx_interrupts(struct s5p_cec_dev *cec);
void s5p_cec_mask_tx_interrupts(struct s5p_cec_dev *cec);
void s5p_cec_unmask_tx_interrupts(struct s5p_cec_dev *cec);
void s5p_cec_reset(struct s5p_cec_dev *cec);
void s5p_cec_tx_reset(struct s5p_cec_dev *cec);
void s5p_cec_rx_reset(struct s5p_cec_dev *cec);
void s5p_cec_threshold(struct s5p_cec_dev *cec);
void s5p_cec_copy_packet(struct s5p_cec_dev *cec, char *data, size_t count);
void s5p_cec_set_addr(struct s5p_cec_dev *cec, u32 addr);
u32 s5p_cec_get_status(struct s5p_cec_dev *cec);
void s5p_clr_pending_tx(struct s5p_cec_dev *cec);
void s5p_clr_pending_rx(struct s5p_cec_dev *cec);
void s5p_cec_get_rx_buf(struct s5p_cec_dev *cec, u32 size, u8 *buffer);

#endif /* _EXYNOS_HDMI_CEC_H_ */
