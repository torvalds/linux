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
#include <linux/init.h>
#include <linux/module.h>

#include <asm/wbflush.h>

#ifdef CONFIG_VT
#include <linux/console.h>
#include <linux/screen_info.h>
#endif

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
	__wbflush = wbflush_loongson2e;

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
