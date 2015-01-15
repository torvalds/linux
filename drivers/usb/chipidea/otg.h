/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc.
 *
 * Author: Peter Chen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DRIVERS_USB_CHIPIDEA_OTG_H
#define __DRIVERS_USB_CHIPIDEA_OTG_H

u32 hw_read_otgsc(struct ci_hdrc *ci, u32 mask);
void hw_write_otgsc(struct ci_hdrc *ci, u32 mask, u32 data);
int ci_hdrc_otg_init(struct ci_hdrc *ci);
void ci_hdrc_otg_destroy(struct ci_hdrc *ci);
enum ci_role ci_otg_role(struct ci_hdrc *ci);
void ci_handle_vbus_change(struct ci_hdrc *ci);
void ci_handle_id_switch(struct ci_hdrc *ci);
void ci_handle_vbus_connected(struct ci_hdrc *ci);
static inline void ci_otg_queue_work(struct ci_hdrc *ci)
{
	disable_irq_nosync(ci->irq);
	queue_work(ci->wq, &ci->work);
}

#endif /* __DRIVERS_USB_CHIPIDEA_OTG_H */
