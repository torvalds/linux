/*
 * arch/ppc/platforms/lopec.h
 *
 * Definitions for Motorola LoPEC board.
 *
 * Author: Dan Cox
 *         danc@mvista.com (or, alternately, source@mvista.com)
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __H_LOPEC_SERIAL
#define __H_LOPEC_SERIAL

#define RS_TABLE_SIZE 3

#define BASE_BAUD (1843200 / 16)

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

#define SERIAL_PORT_DFNS \
         { 0, BASE_BAUD, 0xffe10000, 29, STD_COM_FLAGS, \
           iomem_base: (u8 *) 0xffe10000, \
           io_type: SERIAL_IO_MEM }, \
         { 0, BASE_BAUD, 0xffe11000, 20, STD_COM_FLAGS, \
           iomem_base: (u8 *) 0xffe11000, \
           io_type: SERIAL_IO_MEM }, \
         { 0, BASE_BAUD, 0xffe12000, 21, STD_COM_FLAGS, \
           iomem_base: (u8 *) 0xffe12000, \
           io_type: SERIAL_IO_MEM }

#endif
