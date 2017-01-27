#ifndef _UAPI_ASM_E820_TYPES_H
#define _UAPI_ASM_E820_TYPES_H

/* The maximum number of entries in E820MAP: */
#define E820MAX			128

#ifndef __ASSEMBLY__

/*
 * A single E820 map entry, describing a memory range of [addr...addr+size-1],
 * of 'type' memory type:
 */
struct e820_entry {
	__u64 addr;
	__u64 size;
	__u32 type;
} __attribute__((packed));

#endif /* __ASSEMBLY__ */

#endif /* _UAPI_ASM_E820_TYPES_H */
