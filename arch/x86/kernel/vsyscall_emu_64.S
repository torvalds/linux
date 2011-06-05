/*
 * vsyscall_emu_64.S: Vsyscall emulation page
 *
 * Copyright (c) 2011 Andy Lutomirski
 *
 * Subject to the GNU General Public License, version 2
 */

#include <linux/linkage.h>
#include <asm/irq_vectors.h>

/* The unused parts of the page are filled with 0xcc by the linker script. */

.section .vsyscall_0, "a"
ENTRY(vsyscall_0)
	int $VSYSCALL_EMU_VECTOR
END(vsyscall_0)

.section .vsyscall_1, "a"
ENTRY(vsyscall_1)
	int $VSYSCALL_EMU_VECTOR
END(vsyscall_1)

.section .vsyscall_2, "a"
ENTRY(vsyscall_2)
	int $VSYSCALL_EMU_VECTOR
END(vsyscall_2)
