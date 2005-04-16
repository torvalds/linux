#ifdef __KERNEL__
#ifndef __ASMPPC_GEMINI_SERIAL_H
#define __ASMPPC_GEMINI_SERIAL_H

#include <linux/config.h>
#include <platforms/gemini.h>

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  4
#endif

/* Rate for the 24.576 Mhz clock for the onboard serial chip */
#define BASE_BAUD  (24576000 / 16)

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#define STD_COM4_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#define STD_COM4_FLAGS (ASYNC_BOOT_AUTOCONF)
#endif

#define STD_SERIAL_PORT_DEFNS \
        { 0, BASE_BAUD, GEMINI_SERIAL_A, 15, STD_COM_FLAGS }, /* ttyS0 */ \
        { 0, BASE_BAUD, GEMINI_SERIAL_B, 14, STD_COM_FLAGS }, /* ttyS1 */ \

#ifdef CONFIG_GEMINI_PU32
#define PU32_SERIAL_PORT_DEFNS \
        { 0, BASE_BAUD, NULL, 0, STD_COM_FLAGS },
#else
#define PU32_SERIAL_PORT_DEFNS
#endif

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DEFNS \
        PU32_SERIAL_PORT_DEFNS

#endif
#endif /* __KERNEL__ */
