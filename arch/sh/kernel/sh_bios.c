// SPDX-License-Identifier: GPL-2.0
/*
 *  C interface for trapping into the standard LinuxSH BIOS.
 *
 *  Copyright (C) 2000 Greg Banks, Mitch Davis
 *  Copyright (C) 1999, 2000  Niibe Yutaka
 *  Copyright (C) 2002  M. R. Brown
 *  Copyright (C) 2004 - 2010  Paul Mundt
 */
#include <linux/module.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/sh_bios.h>

#define BIOS_CALL_CONSOLE_WRITE		0
#define BIOS_CALL_ETH_NODE_ADDR		10
#define BIOS_CALL_SHUTDOWN		11
#define BIOS_CALL_GDB_DETACH		0xff

void *gdb_vbr_vector = NULL;

static inline long sh_bios_call(long func, long arg0, long arg1, long arg2,
				    long arg3)
{
	register long r0 __asm__("r0") = func;
	register long r4 __asm__("r4") = arg0;
	register long r5 __asm__("r5") = arg1;
	register long r6 __asm__("r6") = arg2;
	register long r7 __asm__("r7") = arg3;

	if (!gdb_vbr_vector)
		return -ENOSYS;

	__asm__ __volatile__("trapa	#0x3f":"=z"(r0)
			     :"0"(r0), "r"(r4), "r"(r5), "r"(r6), "r"(r7)
			     :"memory");
	return r0;
}

void sh_bios_console_write(const char *buf, unsigned int len)
{
	sh_bios_call(BIOS_CALL_CONSOLE_WRITE, (long)buf, (long)len, 0, 0);
}

void sh_bios_gdb_detach(void)
{
	sh_bios_call(BIOS_CALL_GDB_DETACH, 0, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(sh_bios_gdb_detach);

void sh_bios_get_node_addr(unsigned char *node_addr)
{
	sh_bios_call(BIOS_CALL_ETH_NODE_ADDR, 0, (long)node_addr, 0, 0);
}
EXPORT_SYMBOL_GPL(sh_bios_get_node_addr);

void sh_bios_shutdown(unsigned int how)
{
	sh_bios_call(BIOS_CALL_SHUTDOWN, how, 0, 0, 0);
}

/*
 * Read the old value of the VBR register to initialise the vector
 * through which debug and BIOS traps are delegated by the Linux trap
 * handler.
 */
void sh_bios_vbr_init(void)
{
	unsigned long vbr;

	if (unlikely(gdb_vbr_vector))
		return;

	__asm__ __volatile__ ("stc vbr, %0" : "=r" (vbr));

	if (vbr) {
		gdb_vbr_vector = (void *)(vbr + 0x100);
		printk(KERN_NOTICE "Setting GDB trap vector to %p\n",
		       gdb_vbr_vector);
	} else
		printk(KERN_NOTICE "SH-BIOS not detected\n");
}

/**
 * sh_bios_vbr_reload - Re-load the system VBR from the BIOS vector.
 *
 * This can be used by save/restore code to reinitialize the system VBR
 * from the fixed BIOS VBR. A no-op if no BIOS VBR is known.
 */
void sh_bios_vbr_reload(void)
{
	if (gdb_vbr_vector)
		__asm__ __volatile__ (
			"ldc %0, vbr"
			:
			: "r" (((unsigned long) gdb_vbr_vector) - 0x100)
			: "memory"
		);
}

#ifdef CONFIG_EARLY_PRINTK
/*
 *	Print a string through the BIOS
 */
static void sh_console_write(struct console *co, const char *s,
				 unsigned count)
{
	sh_bios_console_write(s, count);
}

/*
 *	Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first rs_open()
 *	- initialize the serial port
 *	Return non-zero if we didn't find a serial port.
 */
static int __init sh_console_setup(struct console *co, char *options)
{
	int	cflag = CREAD | HUPCL | CLOCAL;

	/*
	 *	Now construct a cflag setting.
	 *	TODO: this is a totally bogus cflag, as we have
	 *	no idea what serial settings the BIOS is using, or
	 *	even if its using the serial port at all.
	 */
	cflag |= B115200 | CS8 | /*no parity*/0;

	co->cflag = cflag;

	return 0;
}

static struct console bios_console = {
	.name		= "bios",
	.write		= sh_console_write,
	.setup		= sh_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

static int __init setup_early_printk(char *buf)
{
	int keep_early = 0;

	if (!buf)
		return 0;

	if (strstr(buf, "keep"))
		keep_early = 1;

	if (!strncmp(buf, "bios", 4))
		early_console = &bios_console;

	if (likely(early_console)) {
		if (keep_early)
			early_console->flags &= ~CON_BOOT;
		else
			early_console->flags |= CON_BOOT;
		register_console(early_console);
	}

	return 0;
}
early_param("earlyprintk", setup_early_printk);
#endif
