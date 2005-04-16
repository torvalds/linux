/*
 * sercons.c
 *      choose the right serial device at boot time
 *
 * triemer 6-SEP-1998
 *      sercons.c is designed to allow the three different kinds 
 *      of serial devices under the decstation world to co-exist
 *      in the same kernel.  The idea here is to abstract 
 *      the pieces of the drivers that are common to this file
 *      so that they do not clash at compile time and runtime.
 *
 * HK 16-SEP-1998 v0.002
 *      removed the PROM console as this is not a real serial
 *      device. Added support for PROM console in drivers/char/tty_io.c
 *      instead. Although it may work to enable more than one 
 *      console device I strongly recommend to use only one.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/dec/machtype.h>

#ifdef CONFIG_ZS
extern int zs_init(void);
#endif

#ifdef CONFIG_DZ
extern int dz_init(void);
#endif

#ifdef CONFIG_SERIAL_CONSOLE

#ifdef CONFIG_ZS
extern void zs_serial_console_init(void);
#endif

#ifdef CONFIG_DZ
extern void dz_serial_console_init(void);
#endif

#endif

/* rs_init - starts up the serial interface -
   handle normal case of starting up the serial interface */

#ifdef CONFIG_SERIAL

int __init rs_init(void)
{

#if defined(CONFIG_ZS) && defined(CONFIG_DZ)
    if (IOASIC)
	return zs_init();
    else
	return dz_init();
#else

#ifdef CONFIG_ZS
    return zs_init();
#endif

#ifdef CONFIG_DZ
    return dz_init();
#endif

#endif
}

__initcall(rs_init);

#endif

#ifdef CONFIG_SERIAL_CONSOLE

/* serial_console_init handles the special case of starting
 *   up the console on the serial port
 */
static int __init decserial_console_init(void)
{
#if defined(CONFIG_ZS) && defined(CONFIG_DZ)
    if (IOASIC)
	zs_serial_console_init();
    else
	dz_serial_console_init();
#else

#ifdef CONFIG_ZS
    zs_serial_console_init();
#endif

#ifdef CONFIG_DZ
    dz_serial_console_init();
#endif

#endif
    return 0;
}
console_initcall(decserial_console_init);

#endif
