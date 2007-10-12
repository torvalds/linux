/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * arch/mips/jmr3927/common/init.c
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
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
 */
#include <linux/init.h>
#include <asm/bootinfo.h>
#include <asm/jmr3927/jmr3927.h>

extern void  __init prom_init_cmdline(void);

const char *get_system_type(void)
{
	return "Toshiba"
#ifdef CONFIG_TOSHIBA_JMR3927
	       " JMR_TX3927"
#endif
	;
}

extern void puts(const char *cp);

void __init prom_init(void)
{
#ifdef CONFIG_TOSHIBA_JMR3927
	/* CCFG */
	if ((tx3927_ccfgptr->ccfg & TX3927_CCFG_TLBOFF) == 0)
		puts("Warning: TX3927 TLB off\n");
#endif

#ifdef CONFIG_TOSHIBA_JMR3927
	mips_machtype = MACH_TOSHIBA_JMR3927;
#endif

	prom_init_cmdline();
	add_memory_region(0, JMR3927_SDRAM_SIZE, BOOT_MEM_RAM);
}
