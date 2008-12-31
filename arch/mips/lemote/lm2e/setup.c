/*
 * BRIEF MODULE DESCRIPTION
 * setup.c - board dependent boot routines
 *
 * Copyright (C) 2007 Lemote Inc. & Insititute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/bootinfo.h>
#include <asm/mc146818-time.h>
#include <asm/time.h>
#include <asm/wbflush.h>
#include <asm/mach-lemote/pci.h>

#ifdef CONFIG_VT
#include <linux/console.h>
#include <linux/screen_info.h>
#endif

extern void mips_reboot_setup(void);

unsigned long cpu_clock_freq;
unsigned long bus_clock;
unsigned int memsize;
unsigned int highmemsize = 0;

void __init plat_time_init(void)
{
	/* setup mips r4k timer */
	mips_hpt_frequency = cpu_clock_freq / 2;
}

unsigned long read_persistent_clock(void)
{
	return mc146818_get_cmos_time();
}

void (*__wbflush)(void);
EXPORT_SYMBOL(__wbflush);

static void wbflush_loongson2e(void)
{
	asm(".set\tpush\n\t"
	    ".set\tnoreorder\n\t"
	    ".set mips3\n\t"
	    "sync\n\t"
	    "nop\n\t"
	    ".set\tpop\n\t"
	    ".set mips0\n\t");
}

void __init plat_mem_setup(void)
{
	set_io_port_base((unsigned long)ioremap(LOONGSON2E_IO_PORT_BASE,
				IO_SPACE_LIMIT - LOONGSON2E_PCI_IO_START + 1));
	mips_reboot_setup();

	__wbflush = wbflush_loongson2e;

	add_memory_region(0x0, (memsize << 20), BOOT_MEM_RAM);
#ifdef CONFIG_64BIT
	if (highmemsize > 0) {
		add_memory_region(0x20000000, highmemsize << 20, BOOT_MEM_RAM);
	}
#endif

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;

	screen_info = (struct screen_info) {
		0, 25,		/* orig-x, orig-y */
		    0,		/* unused */
		    0,		/* orig-video-page */
		    0,		/* orig-video-mode */
		    80,		/* orig-video-cols */
		    0, 0, 0,	/* ega_ax, ega_bx, ega_cx */
		    25,		/* orig-video-lines */
		    VIDEO_TYPE_VGAC,	/* orig-video-isVGA */
		    16		/* orig-video-points */
	};
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

}
