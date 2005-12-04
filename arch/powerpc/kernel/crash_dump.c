/*
 * Routines for doing kexec-based kdump.
 *
 * Copyright (C) 2005, IBM Corp.
 *
 * Created by: Michael Ellerman
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#undef DEBUG

#include <asm/kdump.h>
#include <asm/lmb.h>
#include <asm/firmware.h>

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static void __init create_trampoline(unsigned long addr)
{
	/* The maximum range of a single instruction branch, is the current
	 * instruction's address + (32 MB - 4) bytes. For the trampoline we
	 * need to branch to current address + 32 MB. So we insert a nop at
	 * the trampoline address, then the next instruction (+ 4 bytes)
	 * does a branch to (32 MB - 4). The net effect is that when we
	 * branch to "addr" we jump to ("addr" + 32 MB). Although it requires
	 * two instructions it doesn't require any registers.
	 */
	create_instruction(addr, 0x60000000); /* nop */
	create_branch(addr + 4, addr + PHYSICAL_START, 0);
}

void __init kdump_setup(void)
{
	unsigned long i;

	DBG(" -> kdump_setup()\n");

	for (i = KDUMP_TRAMPOLINE_START; i < KDUMP_TRAMPOLINE_END; i += 8) {
		create_trampoline(i);
	}

	create_trampoline(__pa(system_reset_fwnmi) - PHYSICAL_START);
	create_trampoline(__pa(machine_check_fwnmi) - PHYSICAL_START);

	DBG(" <- kdump_setup()\n");
}
