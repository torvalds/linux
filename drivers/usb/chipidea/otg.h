/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013-2014 Freescale Semiconductor, Inc.
 *
 * Author: Peter Chen
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
static inline void ci_otg_queue_work(struct ci_hdrc *ci)
{
	disable_irq_nosync(ci->irq);
	if (queue_work(ci->wq, &ci->work) == false)
		enable_irq(ci->irq);
}

#endif /* __DRIVERS_USB_CHIPIDEA_OTG_H */
