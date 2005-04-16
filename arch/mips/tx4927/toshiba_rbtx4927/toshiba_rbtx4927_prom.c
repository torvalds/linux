/*
 * rbtx4927 specific prom routines
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 * Copyright (C) 2004 MontaVista Software Inc.
 * Author: Manish Lachwani, mlachwani@mvista.com
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/tx4927/tx4927.h>

void __init prom_init_cmdline(void)
{
	int argc = (int) fw_arg0;
	char **argv = (char **) fw_arg1;
	int i;			/* Always ignore the "-c" at argv[0] */

	/* ignore all built-in args if any f/w args given */
	if (argc > 1) {
		*arcs_cmdline = '\0';
	}

	for (i = 1; i < argc; i++) {
		if (i != 1) {
			strcat(arcs_cmdline, " ");
		}
		strcat(arcs_cmdline, argv[i]);
	}
}

void __init prom_init(void)
{
	const char* toshiba_name_list[] = GROUP_TOSHIBA_NAMES;
	extern int tx4927_get_mem_size(void);
	extern char* toshiba_name;
	int msize;

	prom_init_cmdline();

	mips_machgroup = MACH_GROUP_TOSHIBA;

	if ((read_c0_prid() & 0xff) == PRID_REV_TX4927)
		mips_machtype = MACH_TOSHIBA_RBTX4927;
	else
		mips_machtype = MACH_TOSHIBA_RBTX4937;

        toshiba_name = toshiba_name_list[mips_machtype];

	msize = tx4927_get_mem_size();
	add_memory_region(0, msize << 20, BOOT_MEM_RAM);
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}

const char *get_system_type(void)
{
	return "Toshiba RBTX4927/RBTX4937";
}

char * __init prom_getcmdline(void)
{
        return &(arcs_cmdline[0]);
}

