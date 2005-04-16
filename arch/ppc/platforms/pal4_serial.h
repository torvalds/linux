/*
 * arch/ppc/platforms/pal4_serial.h
 *
 * Definitions for SBS PalomarIV serial support
 *
 * Author: Dan Cox
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __PPC_PAL4_SERIAL_H
#define __PPC_PAL4_SERIAL_H

#define CPC700_SERIAL_1       0xff600300
#define CPC700_SERIAL_2       0xff600400

#define RS_TABLE_SIZE     2
#define BASE_BAUD         (33333333 / 4 / 16)

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#define STD_COM4_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#define STD_COM4_FLAGS (ASYNC_BOOT_AUTOCONF)
#endif

#define SERIAL_PORT_DFNS \
      {0, BASE_BAUD, CPC700_SERIAL_1, 3, STD_COM_FLAGS, \
       iomem_base: (unsigned char *) CPC700_SERIAL_1, \
       io_type: SERIAL_IO_MEM},   /* ttyS0 */ \
      {0, BASE_BAUD, CPC700_SERIAL_2, 4, STD_COM_FLAGS, \
       iomem_base: (unsigned char *) CPC700_SERIAL_2, \
       io_type: SERIAL_IO_MEM}

#endif
