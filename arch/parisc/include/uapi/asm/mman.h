/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __PARISC_MMAN_H__
#define __PARISC_MMAN_H__

#define PROT_READ	0x1		/* page can be read */
#define PROT_WRITE	0x2		/* page can be written */
#define PROT_EXEC	0x4		/* page can be executed */
#define PROT_SEM	0x8		/* page may be used for atomic ops */
#define PROT_NONE	0x0		/* page can not be accessed */
#define PROT_GROWSDOWN	0x01000000	/* mprotect flag: extend change to start of growsdown vma */
#define PROT_GROWSUP	0x02000000	/* mprotect flag: extend change to end of growsup vma */

/* 0x01 - 0x03 are defined in linux/mman.h */
#define MAP_TYPE	0x2b		/* Mask for type of mapping, includes bits 0x08 and 0x20 */
#define MAP_FIXED	0x04		/* Interpret addr exactly */
#define MAP_ANONYMOUS	0x10		/* don't use a file */

#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */
#define MAP_LOCKED	0x2000		/* pages are locked */
#define MAP_NORESERVE	0x4000		/* don't check for reservations */
#define MAP_GROWSDOWN	0x8000		/* stack-like segment */
#define MAP_POPULATE	0x10000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x20000		/* do not block on IO */
#define MAP_STACK	0x40000		/* give out an address that is best suited for process/thread stacks */
#define MAP_HUGETLB	0x80000		/* create a huge page mapping */
#define MAP_FIXED_NOREPLACE 0x100000	/* MAP_FIXED which doesn't unmap underlying mapping */
#define MAP_UNINITIALIZED 0		/* uninitialized anonymous mmap */

#define MS_SYNC		1		/* synchronous memory sync */
#define MS_ASYNC	2		/* sync memory asynchronously */
#define MS_INVALIDATE	4		/* invalidate the caches */

#define MCL_CURRENT	1		/* lock all current mappings */
#define MCL_FUTURE	2		/* lock all future mappings */
#define MCL_ONFAULT	4		/* lock all pages that are faulted in */

#define MLOCK_ONFAULT	0x01		/* Lock pages in range after they are faulted in, do not prefault */

#define MADV_NORMAL     0               /* no further special treatment */
#define MADV_RANDOM     1               /* expect random page references */
#define MADV_SEQUENTIAL 2               /* expect sequential page references */
#define MADV_WILLNEED   3               /* will need these pages */
#define MADV_DONTNEED   4               /* don't need these pages */

/* common/generic parameters */
#define MADV_FREE	8		/* free pages only if memory pressure */
#define MADV_REMOVE	9		/* remove these pages & resources */
#define MADV_DONTFORK	10		/* don't inherit across fork */
#define MADV_DOFORK	11		/* do inherit across fork */

#define MADV_COLD	20		/* deactivate these pages */
#define MADV_PAGEOUT	21		/* reclaim these pages */

#define MADV_POPULATE_READ	22	/* populate (prefault) page tables readable */
#define MADV_POPULATE_WRITE	23	/* populate (prefault) page tables writable */

#define MADV_DONTNEED_LOCKED	24	/* like DONTNEED, but drop locked pages too */

#define MADV_MERGEABLE   65		/* KSM may merge identical pages */
#define MADV_UNMERGEABLE 66		/* KSM may not merge identical pages */

#define MADV_HUGEPAGE	67		/* Worth backing with hugepages */
#define MADV_NOHUGEPAGE	68		/* Not worth backing with hugepages */

#define MADV_DONTDUMP   69		/* Explicity exclude from the core dump,
					   overrides the coredump filter bits */
#define MADV_DODUMP	70		/* Clear the MADV_NODUMP flag */

#define MADV_WIPEONFORK 71		/* Zero memory on fork, child only */
#define MADV_KEEPONFORK 72		/* Undo MADV_WIPEONFORK */

#define MADV_HWPOISON     100		/* poison a page for testing */
#define MADV_SOFT_OFFLINE 101		/* soft offline page for testing */

/* compatibility flags */
#define MAP_FILE	0
#define MAP_VARIABLE	0

#define PKEY_DISABLE_ACCESS	0x1
#define PKEY_DISABLE_WRITE	0x2
#define PKEY_ACCESS_MASK	(PKEY_DISABLE_ACCESS |\
				 PKEY_DISABLE_WRITE)

#endif /* __PARISC_MMAN_H__ */
