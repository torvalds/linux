/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TSS_SEGMENT_H
#define __TSS_SEGMENT_H

struct tss_segment_32 {
	u32 prev_task_link;
	u32 esp0;
	u32 ss0;
	u32 esp1;
	u32 ss1;
	u32 esp2;
	u32 ss2;
	u32 cr3;
	u32 eip;
	u32 eflags;
	u32 eax;
	u32 ecx;
	u32 edx;
	u32 ebx;
	u32 esp;
	u32 ebp;
	u32 esi;
	u32 edi;
	u32 es;
	u32 cs;
	u32 ss;
	u32 ds;
	u32 fs;
	u32 gs;
	u32 ldt_selector;
	u16 t;
	u16 io_map;
};

struct tss_segment_16 {
	u16 prev_task_link;
	u16 sp0;
	u16 ss0;
	u16 sp1;
	u16 ss1;
	u16 sp2;
	u16 ss2;
	u16 ip;
	u16 flag;
	u16 ax;
	u16 cx;
	u16 dx;
	u16 bx;
	u16 sp;
	u16 bp;
	u16 si;
	u16 di;
	u16 es;
	u16 cs;
	u16 ss;
	u16 ds;
	u16 ldt;
};

#endif
