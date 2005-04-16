/*
 * arch/ppc/platforms/85xx/sbc8560.h
 *
 * Wind River SBC8560 board definitions
 *
 * Copyright 2003 Motorola Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
 
#ifndef __MACH_SBC8560_H__
#define __MACH_SBC8560_H__
 
#include <linux/config.h>
#include <platforms/85xx/sbc85xx.h>

#define CPM_MAP_ADDR    (CCSRBAR + MPC85xx_CPM_OFFSET)
 
#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  2
#endif
 
/* Rate for the 1.8432 Mhz clock for the onboard serial chip */
#define BASE_BAUD ( 1843200 / 16 )
 
#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_SKIP_TEST)
#endif

#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, UARTA_ADDR, MPC85xx_IRQ_EXT9, STD_COM_FLAGS, /* ttyS0 */ \
                iomem_base: (u8 *)UARTA_ADDR,                       \
                io_type: SERIAL_IO_MEM },                                 \
        { 0, BASE_BAUD, UARTB_ADDR, MPC85xx_IRQ_EXT10, STD_COM_FLAGS, /* ttyS1 */ \
                iomem_base: (u8 *)UARTB_ADDR,                       \
                io_type: SERIAL_IO_MEM },
 
#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS
 
#endif /* __MACH_SBC8560_H__ */
