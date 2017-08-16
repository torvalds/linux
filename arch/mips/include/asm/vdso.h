/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#include <linux/mm_types.h>

#include <asm/barrier.h>

/**
 * struct mips_vdso_image - Details of a VDSO image.
 * @data: Pointer to VDSO image data (page-aligned).
 * @size: Size of the VDSO image data (page-aligned).
 * @off_sigreturn: Offset of the sigreturn() trampoline.
 * @off_rt_sigreturn: Offset of the rt_sigreturn() trampoline.
 * @mapping: Special mapping structure.
 *
 * This structure contains details of a VDSO image, including the image data
 * and offsets of certain symbols required by the kernel. It is generated as
 * part of the VDSO build process, aside from the mapping page array, which is
 * populated at runtime.
 */
struct mips_vdso_image {
	void *data;
	unsigned long size;

	unsigned long off_sigreturn;
	unsigned long off_rt_sigreturn;

	struct vm_special_mapping mapping;
};

/*
 * The following structures are auto-generated as part of the build for each
 * ABI by genvdso, see arch/mips/vdso/Makefile.
 */

extern struct mips_vdso_image vdso_image;

#ifdef CONFIG_MIPS32_O32
extern struct mips_vdso_image vdso_image_o32;
#endif

#ifdef CONFIG_MIPS32_N32
extern struct mips_vdso_image vdso_image_n32;
#endif

/**
 * union mips_vdso_data - Data provided by the kernel for the VDSO.
 * @xtime_sec:		Current real time (seconds part).
 * @xtime_nsec:		Current real time (nanoseconds part, shifted).
 * @wall_to_mono_sec:	Wall-to-monotonic offset (seconds part).
 * @wall_to_mono_nsec:	Wall-to-monotonic offset (nanoseconds part).
 * @seq_count:		Counter to synchronise updates (odd = updating).
 * @cs_shift:		Clocksource shift value.
 * @clock_mode:		Clocksource to use for time functions.
 * @cs_mult:		Clocksource multiplier value.
 * @cs_cycle_last:	Clock cycle value at last update.
 * @cs_mask:		Clocksource mask value.
 * @tz_minuteswest:	Minutes west of Greenwich (from timezone).
 * @tz_dsttime:		Type of DST correction (from timezone).
 *
 * This structure contains data needed by functions within the VDSO. It is
 * populated by the kernel and mapped read-only into user memory. The time
 * fields are mirrors of internal data from the timekeeping infrastructure.
 *
 * Note: Care should be taken when modifying as the layout must remain the same
 * for both 64- and 32-bit (for 32-bit userland on 64-bit kernel).
 */
union mips_vdso_data {
	struct {
		u64 xtime_sec;
		u64 xtime_nsec;
		u64 wall_to_mono_sec;
		u64 wall_to_mono_nsec;
		u32 seq_count;
		u32 cs_shift;
		u8 clock_mode;
		u32 cs_mult;
		u64 cs_cycle_last;
		u64 cs_mask;
		s32 tz_minuteswest;
		s32 tz_dsttime;
	};

	u8 page[PAGE_SIZE];
};

static inline u32 vdso_data_read_begin(const union mips_vdso_data *data)
{
	u32 seq;

	while (true) {
		seq = ACCESS_ONCE(data->seq_count);
		if (likely(!(seq & 1))) {
			/* Paired with smp_wmb() in vdso_data_write_*(). */
			smp_rmb();
			return seq;
		}

		cpu_relax();
	}
}

static inline bool vdso_data_read_retry(const union mips_vdso_data *data,
					u32 start_seq)
{
	/* Paired with smp_wmb() in vdso_data_write_*(). */
	smp_rmb();
	return unlikely(data->seq_count != start_seq);
}

static inline void vdso_data_write_begin(union mips_vdso_data *data)
{
	++data->seq_count;

	/* Ensure sequence update is written before other data page values. */
	smp_wmb();
}

static inline void vdso_data_write_end(union mips_vdso_data *data)
{
	/* Ensure data values are written before updating sequence again. */
	smp_wmb();
	++data->seq_count;
}

#endif /* __ASM_VDSO_H */
