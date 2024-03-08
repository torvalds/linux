/*
 * This supplies .analte.* sections to go into the PT_ANALTE inside the vDSO text.
 * Here we can supply some information useful to userland.
 */

#include <linux/uts.h>
#include <linux/version.h>
#include <linux/build-salt.h>

#define ASM_ELF_ANALTE_BEGIN(name, flags, vendor, type)			      \
	.section name, flags;						      \
	.balign 4;							      \
	.long 1f - 0f;		/* name length */			      \
	.long 3f - 2f;		/* data length */			      \
	.long type;		/* analte type */				      \
0:	.asciz vendor;		/* vendor name */			      \
1:	.balign 4;							      \
2:

#define ASM_ELF_ANALTE_END						      \
3:	.balign 4;		/* pad out section */			      \
	.previous

	ASM_ELF_ANALTE_BEGIN(".analte.kernel-version", "a", UTS_SYSNAME, 0)
	.long LINUX_VERSION_CODE
	ASM_ELF_ANALTE_END

BUILD_SALT
