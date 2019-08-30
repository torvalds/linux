/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Include file for NEC VR4100 series General-purpose I/O Unit.
 *
 *  Copyright (C) 2005-2009  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#ifndef __NEC_VR41XX_GIU_H
#define __NEC_VR41XX_GIU_H

/*
 * NEC VR4100 series GIU platform device IDs.
 */
enum {
	GPIO_50PINS_PULLUPDOWN,
	GPIO_36PINS,
	GPIO_48PINS_EDGE_SELECT,
};

typedef enum {
	IRQ_TRIGGER_LEVEL,
	IRQ_TRIGGER_EDGE,
	IRQ_TRIGGER_EDGE_FALLING,
	IRQ_TRIGGER_EDGE_RISING,
} irq_trigger_t;

typedef enum {
	IRQ_SIGNAL_THROUGH,
	IRQ_SIGNAL_HOLD,
} irq_signal_t;

extern void vr41xx_set_irq_trigger(unsigned int pin, irq_trigger_t trigger,
				   irq_signal_t signal);

typedef enum {
	IRQ_LEVEL_LOW,
	IRQ_LEVEL_HIGH,
} irq_level_t;

extern void vr41xx_set_irq_level(unsigned int pin, irq_level_t level);

#endif /* __NEC_VR41XX_GIU_H */
