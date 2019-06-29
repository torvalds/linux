/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Include file for NEC VR4100 series Serial Interface Unit.
 *
 *  Copyright (C) 2005-2008  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#ifndef __NEC_VR41XX_SIU_H
#define __NEC_VR41XX_SIU_H

#define SIU_PORTS_MAX 2

typedef enum {
	SIU_INTERFACE_RS232C,
	SIU_INTERFACE_IRDA,
} siu_interface_t;

extern void vr41xx_select_siu_interface(siu_interface_t interface);

typedef enum {
	SIU_USE_IRDA,
	FIR_USE_IRDA,
} irda_use_t;

extern void vr41xx_use_irda(irda_use_t use);

typedef enum {
	SHARP_IRDA,
	TEMIC_IRDA,
	HP_IRDA,
} irda_module_t;

typedef enum {
	IRDA_TX_1_5MBPS,
	IRDA_TX_4MBPS,
} irda_speed_t;

extern void vr41xx_select_irda_module(irda_module_t module, irda_speed_t speed);

#ifdef CONFIG_SERIAL_VR41XX_CONSOLE
extern void vr41xx_siu_early_setup(struct uart_port *port);
#else
static inline void vr41xx_siu_early_setup(struct uart_port *port) {}
#endif

#endif /* __NEC_VR41XX_SIU_H */
