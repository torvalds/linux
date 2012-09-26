/******************************************************************************
 * Guest OS interface to ARM Xen.
 *
 * Stefano Stabellini <stefano.stabellini@eu.citrix.com>, Citrix, 2012
 */

#ifndef _ASM_ARM_XEN_INTERFACE_H
#define _ASM_ARM_XEN_INTERFACE_H

#include <linux/types.h>

#define uint64_aligned_t uint64_t __attribute__((aligned(8)))

#define __DEFINE_GUEST_HANDLE(name, type) \
	typedef struct { union { type *p; uint64_aligned_t q; }; }  \
        __guest_handle_ ## name

#define DEFINE_GUEST_HANDLE_STRUCT(name) \
	__DEFINE_GUEST_HANDLE(name, struct name)
#define DEFINE_GUEST_HANDLE(name) __DEFINE_GUEST_HANDLE(name, name)
#define GUEST_HANDLE(name)        __guest_handle_ ## name

#define set_xen_guest_handle(hnd, val)			\
	do {						\
		if (sizeof(hnd) == 8)			\
			*(uint64_t *)&(hnd) = 0;	\
		(hnd).p = val;				\
	} while (0)

#ifndef __ASSEMBLY__
/* Explicitly size integers that represent pfns in the interface with
 * Xen so that we can have one ABI that works for 32 and 64 bit guests. */
typedef uint64_t xen_pfn_t;
typedef uint64_t xen_ulong_t;
/* Guest handles for primitive C types. */
__DEFINE_GUEST_HANDLE(uchar, unsigned char);
__DEFINE_GUEST_HANDLE(uint,  unsigned int);
__DEFINE_GUEST_HANDLE(ulong, unsigned long);
DEFINE_GUEST_HANDLE(char);
DEFINE_GUEST_HANDLE(int);
DEFINE_GUEST_HANDLE(long);
DEFINE_GUEST_HANDLE(void);
DEFINE_GUEST_HANDLE(uint64_t);
DEFINE_GUEST_HANDLE(uint32_t);
DEFINE_GUEST_HANDLE(xen_pfn_t);

/* Maximum number of virtual CPUs in multi-processor guests. */
#define MAX_VIRT_CPUS 1

struct arch_vcpu_info { };
struct arch_shared_info { };

/* TODO: Move pvclock definitions some place arch independent */
struct pvclock_vcpu_time_info {
	u32   version;
	u32   pad0;
	u64   tsc_timestamp;
	u64   system_time;
	u32   tsc_to_system_mul;
	s8    tsc_shift;
	u8    flags;
	u8    pad[2];
} __attribute__((__packed__)); /* 32 bytes */

/* It is OK to have a 12 bytes struct with no padding because it is packed */
struct pvclock_wall_clock {
	u32   version;
	u32   sec;
	u32   nsec;
} __attribute__((__packed__));
#endif

#endif /* _ASM_ARM_XEN_INTERFACE_H */
