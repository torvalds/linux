/*
 * This supplies .note.* sections to go into the PT_NOTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

#include <linux/version.h>
#include <linux/elfnote.h>

/* Ideally this would use UTS_NAME, but using a quoted string here
   doesn't work. Remember to change this when changing the
   kernel's name. */
ELFNOTE_START(Linux, 0, "a")
	.long LINUX_VERSION_CODE
ELFNOTE_END

#ifdef CONFIG_XEN
/*
 * Add a special note telling glibc's dynamic linker a fake hardware
 * flavor that it will use to choose the search path for libraries in the
 * same way it uses real hardware capabilities like "mmx".
 * We supply "nosegneg" as the fake capability, to indicate that we
 * do not like negative offsets in instructions using segment overrides,
 * since we implement those inefficiently.  This makes it possible to
 * install libraries optimized to avoid those access patterns in someplace
 * like /lib/i686/tls/nosegneg.  Note that an /etc/ld.so.conf.d/file
 * corresponding to the bits here is needed to make ldconfig work right.
 * It should contain:
 *	hwcap 1 nosegneg
 * to match the mapping of bit to name that we give here.
 *
 * At runtime, the fake hardware feature will be considered to be present
 * if its bit is set in the mask word.  So, we start with the mask 0, and
 * at boot time we set VDSO_NOTE_NONEGSEG_BIT if running under Xen.
 */

#include "../../xen/vdso.h"	/* Defines VDSO_NOTE_NONEGSEG_BIT.  */

ELFNOTE_START(GNU, 2, "a")
	.long 1			/* ncaps */
VDSO32_NOTE_MASK:		/* Symbol used by arch/x86/xen/setup.c */
	.long 0			/* mask */
	.byte VDSO_NOTE_NONEGSEG_BIT; .asciz "nosegneg"	/* bit, name */
ELFNOTE_END
#endif
