/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __TLB_H__
#define __TLB_H__

#include "um_mmu.h"

struct host_vm_op {
	enum { MMAP, MUNMAP, MPROTECT } type;
	union {
		struct {
			unsigned long addr;
			unsigned long len;
			unsigned int r:1;
			unsigned int w:1;
			unsigned int x:1;
			int fd;
			__u64 offset;
		} mmap;
		struct {
			unsigned long addr;
			unsigned long len;
		} munmap;
		struct {
			unsigned long addr;
			unsigned long len;
			unsigned int r:1;
			unsigned int w:1;
			unsigned int x:1;
		} mprotect;
	} u;
};

extern void mprotect_kernel_vm(int w);
extern void force_flush_all(void);
extern void fix_range_common(struct mm_struct *mm, unsigned long start_addr,
                             unsigned long end_addr, int force,
                             void (*do_ops)(union mm_context *,
                                            struct host_vm_op *, int));
extern int flush_tlb_kernel_range_common(unsigned long start,
					 unsigned long end);

extern int add_mmap(unsigned long virt, unsigned long phys, unsigned long len,
		    int r, int w, int x, struct host_vm_op *ops, int index,
                    int last_filled, union mm_context *mmu,
                    void (*do_ops)(union mm_context *, struct host_vm_op *,
                                   int));
extern int add_munmap(unsigned long addr, unsigned long len,
		      struct host_vm_op *ops, int index, int last_filled,
                      union mm_context *mmu,
                      void (*do_ops)(union mm_context *, struct host_vm_op *,
                                     int));
extern int add_mprotect(unsigned long addr, unsigned long len, int r, int w,
			int x, struct host_vm_op *ops, int index,
                        int last_filled, union mm_context *mmu,
                        void (*do_ops)(union mm_context *, struct host_vm_op *,
                                       int));
#endif
