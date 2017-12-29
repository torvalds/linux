/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This supplies .note.* sections to go into the PT_NOTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

#include <linux/uts.h>
#include <linux/version.h>

#define ASM_ELF_NOTE_BEGIN(name, flags, vendor, type)			      \
	.section name, flags;						      \
	.balign 4;							      \
	.long 1f - 0f;		/* name length */			      \
	.long 3f - 2f;		/* data length */			      \
	.long type;		/* note type */				      \
0:	.asciz vendor;		/* vendor name */			      \
1:	.balign 4;							      \
2:

#define ASM_ELF_NOTE_END						      \
3:	.balign 4;		/* pad out section */			      \
	.previous

	ASM_ELF_NOTE_BEGIN(".note.kernel-version", "a", UTS_SYSNAME, 0)
	.long LINUX_VERSION_CODE
	ASM_ELF_NOTE_END
