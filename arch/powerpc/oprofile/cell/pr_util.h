 /*
 * Cell Broadband Engine OProfile Support
 *
 * (C) Copyright IBM Corporation 2006
 *
 * Author: Maynard Johnson <maynardj@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef PR_UTIL_H
#define PR_UTIL_H

#include <linux/cpumask.h>
#include <linux/oprofile.h>
#include <asm/cell-pmu.h>
#include <asm/cell-regs.h>
#include <asm/spu.h>

/* Defines used for sync_start */
#define SKIP_GENERIC_SYNC 0
#define SYNC_START_ERROR -1
#define DO_GENERIC_SYNC 1
#define SPUS_PER_NODE   8
#define DEFAULT_TIMER_EXPIRE  (HZ / 10)

extern struct delayed_work spu_work;
extern int spu_prof_running;

#define TRACE_ARRAY_SIZE 1024

extern spinlock_t oprof_spu_smpl_arry_lck;

struct spu_overlay_info {	/* map of sections within an SPU overlay */
	unsigned int vma;	/* SPU virtual memory address from elf */
	unsigned int size;	/* size of section from elf */
	unsigned int offset;	/* offset of section into elf file */
	unsigned int buf;
};

struct vma_to_fileoffset_map {	/* map of sections within an SPU program */
	struct vma_to_fileoffset_map *next;	/* list pointer */
	unsigned int vma;	/* SPU virtual memory address from elf */
	unsigned int size;	/* size of section from elf */
	unsigned int offset;	/* offset of section into elf file */
	unsigned int guard_ptr;
	unsigned int guard_val;
        /*
	 * The guard pointer is an entry in the _ovly_buf_table,
	 * computed using ovly.buf as the index into the table.  Since
	 * ovly.buf values begin at '1' to reference the first (or 0th)
	 * entry in the _ovly_buf_table, the computation subtracts 1
	 * from ovly.buf.
	 * The guard value is stored in the _ovly_buf_table entry and
	 * is an index (starting at 1) back to the _ovly_table entry
	 * that is pointing at this _ovly_buf_table entry.  So, for
	 * example, for an overlay scenario with one overlay segment
	 * and two overlay sections:
	 *      - Section 1 points to the first entry of the
	 *        _ovly_buf_table, which contains a guard value
	 *        of '1', referencing the first (index=0) entry of
	 *        _ovly_table.
	 *      - Section 2 points to the second entry of the
	 *        _ovly_buf_table, which contains a guard value
	 *        of '2', referencing the second (index=1) entry of
	 *        _ovly_table.
	 */

};

struct spu_buffer {
	int last_guard_val;
	int ctx_sw_seen;
	unsigned long *buff;
	unsigned int head, tail;
};


/* The three functions below are for maintaining and accessing
 * the vma-to-fileoffset map.
 */
struct vma_to_fileoffset_map *create_vma_map(const struct spu *spu,
					     unsigned long objectid);
unsigned int vma_map_lookup(struct vma_to_fileoffset_map *map,
			    unsigned int vma, const struct spu *aSpu,
			    int *grd_val);
void vma_map_free(struct vma_to_fileoffset_map *map);

/*
 * Entry point for SPU profiling.
 * cycles_reset is the SPU_CYCLES count value specified by the user.
 */
int start_spu_profiling_cycles(unsigned int cycles_reset);
void start_spu_profiling_events(void);

void stop_spu_profiling_cycles(void);
void stop_spu_profiling_events(void);

/* add the necessary profiling hooks */
int spu_sync_start(void);

/* remove the hooks */
int spu_sync_stop(void);

/* Record SPU program counter samples to the oprofile event buffer. */
void spu_sync_buffer(int spu_num, unsigned int *samples,
		     int num_samples);

void set_spu_profiling_frequency(unsigned int freq_khz, unsigned int cycles_reset);

#endif	  /* PR_UTIL_H */
