/*
 * KGDB and progress routines for the Marvell/Galileo MV64x60 (Discovery).
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2003 (c) MontaVista Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 *****************************************************************************
 *
 *	Low-level MPSC/UART I/O routines
 *
 *****************************************************************************
 */


#include <linux/config.h>
#include <linux/irq.h>
#include <asm/delay.h>
#include <asm/mv64x60.h>
#include <asm/machdep.h>


#if defined(CONFIG_SERIAL_TEXT_DEBUG)

#define	MPSC_CHR_1	0x000c
#define	MPSC_CHR_2	0x0010

static struct mv64x60_handle	mv64x60_dbg_bh;

void
mv64x60_progress_init(u32 base)
{
	mv64x60_dbg_bh.v_base = base;
	return;
}

static void
mv64x60_polled_putc(int chan, char c)
{
	u32	offset;

	if (chan == 0)
		offset = 0x8000;
	else
		offset = 0x9000;

	mv64x60_write(&mv64x60_dbg_bh, offset + MPSC_CHR_1, (u32)c);
	mv64x60_write(&mv64x60_dbg_bh, offset + MPSC_CHR_2, 0x200);
	udelay(2000);
}

void
mv64x60_mpsc_progress(char *s, unsigned short hex)
{
	volatile char	c;

	mv64x60_polled_putc(0, '\r');

	while ((c = *s++) != 0)
		mv64x60_polled_putc(0, c);

	mv64x60_polled_putc(0, '\n');
	mv64x60_polled_putc(0, '\r');

	return;
}
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */


#if defined(CONFIG_KGDB)

#if defined(CONFIG_KGDB_TTYS0)
#define KGDB_PORT 0
#elif defined(CONFIG_KGDB_TTYS1)
#define KGDB_PORT 1
#else
#error "Invalid kgdb_tty port"
#endif

void
putDebugChar(unsigned char c)
{
	mv64x60_polled_putc(KGDB_PORT, (char)c);
}

int
getDebugChar(void)
{
	unsigned char	c;

	while (!mv64x60_polled_getc(KGDB_PORT, &c));
	return (int)c;
}

void
putDebugString(char* str)
{
	while (*str != '\0') {
		putDebugChar(*str);
		str++;
	}
	putDebugChar('\r');
	return;
}

void
kgdb_interruptible(int enable)
{
}

void
kgdb_map_scc(void)
{
	if (ppc_md.early_serial_map)
		ppc_md.early_serial_map();
}
#endif	/* CONFIG_KGDB */
