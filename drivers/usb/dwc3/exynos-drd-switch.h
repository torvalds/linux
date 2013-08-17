/* drivers/usb/dwc3/exynos-drd-switch.h
 *
 * Copyright (c) 2012 Samsung Electronics Co. Ltd
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * Exynos SuperSpeed USB 3.0 DRD role switch
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DRIVERS_USB_DWC3_EXYNOS_DRD_SWITCH_H
#define __DRIVERS_USB_DWC3_EXYNOS_DRD_SWITCH_H

#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/usb/otg.h>

/* TODO: adjust */
#define ID_DEBOUNCE_DELAY	(HZ / 2)	/* 0.5 sec */
#define VBUS_DEBOUNCE_DELAY	(HZ / 2)	/* 0.5 sec */
#define EAGAIN_DELAY		100		/* msec */

enum id_pin_state {
	NA = -1,
	A_DEV,
	B_DEV,
};

/**
 * struct exynos_drd_switch: DRD role switch driver data.
 * @core: DRD core.
 * @otg: USB OTG Transceiver structure.
 * @id_irq: ID GPIO IRQ number.
 * @id_gpio: GPIO that is used to detect a change in connector ID status.
 * @vbus_irq: VBUS GPIO IRQ numbler.
 * @vbus_gpio: GPIO that is used to detect a change in VBus status.
 * @wq: switch workqueue.
 * @work: OTG state machine work.
 * @vbus_active: true if VBus is applied in device mode, false otherwise.
 * @id_state: last value of ID GPIO.
 * @lock: lock to protect the switch state.
 * @wakelock: lock to block suspend.
 */
struct exynos_drd_switch {
	struct exynos_drd_core *core;
	struct usb_otg otg;
	int id_irq;
	int id_gpio;
	struct timer_list id_db_timer;
	int vbus_irq;
	int vbus_gpio;
	struct timer_list vbus_db_timer;
	struct workqueue_struct *wq;
	struct delayed_work work;
	bool vbus_active;
	enum id_pin_state id_state;
	spinlock_t lock;
	struct wake_lock wakelock;
};

int exynos_drd_switch_init(struct exynos_drd *drd);
void exynos_drd_switch_exit(struct exynos_drd *drd);
void exynos_drd_switch_reset(struct exynos_drd *drd, int run);

#endif /* __DRIVERS_USB_DWC3_EXYNOS_DRD_SWITCH_H */
