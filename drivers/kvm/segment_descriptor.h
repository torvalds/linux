#ifndef __SEGMENT_DESCRIPTOR_H
#define __SEGMENT_DESCRIPTOR_H

struct segment_descriptor {
	u16 limit_low;
	u16 base_low;
	u8  base_mid;
	u8  type : 4;
	u8  system : 1;
	u8  dpl : 2;
	u8  present : 1;
	u8  limit_high : 4;
	u8  avl : 1;
	u8  long_mode : 1;
	u8  default_op : 1;
	u8  granularity : 1;
	u8  base_high;
} __attribute__((packed));

#ifdef CONFIG_X86_64
/* LDT or TSS descriptor in the GDT. 16 bytes. */
struct segment_descriptor_64 {
	struct segment_descriptor s;
	u32 base_higher;
	u32 pad_zero;
};

#endif
#endif
