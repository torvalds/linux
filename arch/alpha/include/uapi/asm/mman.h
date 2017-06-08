#ifndef __ALPHA_MMAN_H__
#define __ALPHA_MMAN_H__

#define PROT_READ	0x1		/* page can be read */
#define PROT_WRITE	0x2		/* page can be written */
#define PROT_EXEC	0x4		/* page can be executed */
#define PROT_SEM	0x8		/* page may be used for atomic ops */
#define PROT_NONE	0x0		/* page can not be accessed */
#define PROT_GROWSDOWN	0x01000000	/* mprotect flag: extend change to start of growsdown vma */
#define PROT_GROWSUP	0x02000000	/* mprotect flag: extend change to end of growsup vma */

#define MAP_SHARED	0x01		/* Share changes */
#define MAP_PRIVATE	0x02		/* Changes are private */
#define MAP_TYPE	0x0f		/* Mask for type of mapping (OSF/1 is _wrong_) */
#define MAP_FIXED	0x100		/* Interpret addr exactly */
#define MAP_ANONYMOUS	0x10		/* don't use a file */

/* not used by linux, but here to make sure we don't clash with OSF/1 defines */
#define _MAP_HASSEMAPHORE 0x0200
#define _MAP_INHERIT	0x0400
#define _MAP_UNALIGNED	0x0800

/* These are linux-specific */
#define MAP_GROWSDOWN	0x01000		/* stack-like segment */
#define MAP_DENYWRITE	0x02000		/* ETXTBSY */
#define MAP_EXECUTABLE	0x04000		/* mark it as an executable */
#define MAP_LOCKED	0x08000		/* lock the mapping */
#define MAP_NORESERVE	0x10000		/* don't check for reservations */
#define MAP_POPULATE	0x20000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x40000		/* do not block on IO */
#define MAP_STACK	0x80000		/* give out an address that is best suited for process/thread stacks */
#define MAP_HUGETLB	0x100000	/* create a huge page mapping */

#define MS_ASYNC	1		/* sync memory asynchronously */
#define MS_SYNC		2		/* synchronous memory sync */
#define MS_INVALIDATE	4		/* invalidate the caches */

#define MCL_CURRENT	 8192		/* lock all currently mapped pages */
#define MCL_FUTURE	16384		/* lock all additions to address space */
#define MCL_ONFAULT	32768		/* lock all pages that are faulted in */

#define MLOCK_ONFAULT	0x01		/* Lock pages in range after they are faulted in, do not prefault */

#define MADV_NORMAL	0		/* no further special treatment */
#define MADV_RANDOM	1		/* expect random page references */
#define MADV_SEQUENTIAL	2		/* expect sequential page references */
#define MADV_WILLNEED	3		/* will need these pages */
#define	MADV_SPACEAVAIL	5		/* ensure resources are available */
#define MADV_DONTNEED	6		/* don't need these pages */

/* common/generic parameters */
#define MADV_FREE	8		/* free pages only if memory pressure */
#define MADV_REMOVE	9		/* remove these pages & resources */
#define MADV_DONTFORK	10		/* don't inherit across fork */
#define MADV_DOFORK	11		/* do inherit across fork */

#define MADV_MERGEABLE   12		/* KSM may merge identical pages */
#define MADV_UNMERGEABLE 13		/* KSM may not merge identical pages */

#define MADV_HUGEPAGE	14		/* Worth backing with hugepages */
#define MADV_NOHUGEPAGE	15		/* Not worth backing with hugepages */

#define MADV_DONTDUMP   16		/* Explicity exclude from the core dump,
					   overrides the coredump filter bits */
#define MADV_DODUMP	17		/* Clear the MADV_NODUMP flag */

/* compatibility flags */
#define MAP_FILE	0

/*
 * When MAP_HUGETLB is set bits [26:31] encode the log2 of the huge page size.
 * This gives us 6 bits, which is enough until someone invents 128 bit address
 * spaces.
 *
 * Assume these are all power of twos.
 * When 0 use the default page size.
 */
#define MAP_HUGE_SHIFT	26
#define MAP_HUGE_MASK	0x3f

#define PKEY_DISABLE_ACCESS	0x1
#define PKEY_DISABLE_WRITE	0x2
#define PKEY_ACCESS_MASK	(PKEY_DISABLE_ACCESS |\
				 PKEY_DISABLE_WRITE)

#endif /* __ALPHA_MMAN_H__ */
