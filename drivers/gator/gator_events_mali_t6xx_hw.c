/**
 * Copyright (C) ARM Limited 2012-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"

#include <linux/module.h>
#include <linux/time.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <asm/io.h>

/* Mali T6xx DDK includes */
#include "linux/mali_linux_trace.h"
#include "kbase/src/common/mali_kbase.h"
#include "kbase/src/linux/mali_kbase_mem_linux.h"

#include "gator_events_mali_common.h"

/* If API version is not specified then assume API version 1. */
#ifndef MALI_DDK_GATOR_API_VERSION
#define MALI_DDK_GATOR_API_VERSION 1
#endif

#if (MALI_DDK_GATOR_API_VERSION != 1) && (MALI_DDK_GATOR_API_VERSION != 2)
#error MALI_DDK_GATOR_API_VERSION is invalid (must be 1 for r1/r2 DDK, or 2 for r3 DDK).
#endif

/*
 * Mali-T6xx
 */
typedef struct kbase_device *kbase_find_device_type(int);
typedef kbase_context *kbase_create_context_type(kbase_device *);
typedef void kbase_destroy_context_type(kbase_context *);

#if MALI_DDK_GATOR_API_VERSION == 1
typedef void *kbase_va_alloc_type(kbase_context *, u32);
typedef void kbase_va_free_type(kbase_context *, void *);
#elif MALI_DDK_GATOR_API_VERSION == 2
typedef void *kbase_va_alloc_type(kbase_context *, u32, kbase_hwc_dma_mapping * handle);
typedef void kbase_va_free_type(kbase_context *, kbase_hwc_dma_mapping * handle);
#endif

typedef mali_error kbase_instr_hwcnt_enable_type(kbase_context *, kbase_uk_hwcnt_setup *);
typedef mali_error kbase_instr_hwcnt_disable_type(kbase_context *);
typedef mali_error kbase_instr_hwcnt_clear_type(kbase_context *);
typedef mali_error kbase_instr_hwcnt_dump_irq_type(kbase_context *);
typedef mali_bool kbase_instr_hwcnt_dump_complete_type(kbase_context *, mali_bool *);

static kbase_find_device_type *kbase_find_device_symbol;
static kbase_create_context_type *kbase_create_context_symbol;
static kbase_va_alloc_type *kbase_va_alloc_symbol;
static kbase_instr_hwcnt_enable_type *kbase_instr_hwcnt_enable_symbol;
static kbase_instr_hwcnt_clear_type *kbase_instr_hwcnt_clear_symbol;
static kbase_instr_hwcnt_dump_irq_type *kbase_instr_hwcnt_dump_irq_symbol;
static kbase_instr_hwcnt_dump_complete_type *kbase_instr_hwcnt_dump_complete_symbol;
static kbase_instr_hwcnt_disable_type *kbase_instr_hwcnt_disable_symbol;
static kbase_va_free_type *kbase_va_free_symbol;
static kbase_destroy_context_type *kbase_destroy_context_symbol;

/** The interval between reads, in ns.
 *
 * Earlier we introduced
 * a 'hold off for 1ms after last read' to resolve MIDBASE-2178 and MALINE-724.
 * However, the 1ms hold off is too long if no context switches occur as there is a race
 * between this value and the tick of the read clock in gator which is also 1ms. If we 'miss' the
 * current read, the counter values are effectively 'spread' over 2ms and the values seen are half
 * what they should be (since Streamline averages over sample time). In the presence of context switches
 * this spread can vary and markedly affect the counters.  Currently there is no 'proper' solution to
 * this, but empirically we have found that reducing the minimum read interval to 950us causes the
 * counts to be much more stable.
 */
static const int READ_INTERVAL_NSEC = 950000;

#if GATOR_TEST
#include "gator_events_mali_t6xx_hw_test.c"
#endif

/* Blocks for HW counters */
enum {
	JM_BLOCK = 0,
	TILER_BLOCK,
	SHADER_BLOCK,
	MMU_BLOCK
};

/* Counters for Mali-T6xx:
 *
 *  - HW counters, 4 blocks
 *    For HW counters we need strings to create /dev/gator/events files.
 *    Enums are not needed because the position of the HW name in the array is the same
 *    of the corresponding value in the received block of memory.
 *    HW counters are requested by calculating a bitmask, passed then to the driver.
 *    Every millisecond a HW counters dump is requested, and if the previous has been completed they are read.
 */

/* Hardware Counters */
static const char *const hardware_counter_names[] = {
	/* Job Manager */
	"",
	"",
	"",
	"",
	"MESSAGES_SENT",
	"MESSAGES_RECEIVED",
	"GPU_ACTIVE",		/* 6 */
	"IRQ_ACTIVE",
	"JS0_JOBS",
	"JS0_TASKS",
	"JS0_ACTIVE",
	"",
	"JS0_WAIT_READ",
	"JS0_WAIT_ISSUE",
	"JS0_WAIT_DEPEND",
	"JS0_WAIT_FINISH",
	"JS1_JOBS",
	"JS1_TASKS",
	"JS1_ACTIVE",
	"",
	"JS1_WAIT_READ",
	"JS1_WAIT_ISSUE",
	"JS1_WAIT_DEPEND",
	"JS1_WAIT_FINISH",
	"JS2_JOBS",
	"JS2_TASKS",
	"JS2_ACTIVE",
	"",
	"JS2_WAIT_READ",
	"JS2_WAIT_ISSUE",
	"JS2_WAIT_DEPEND",
	"JS2_WAIT_FINISH",
	"JS3_JOBS",
	"JS3_TASKS",
	"JS3_ACTIVE",
	"",
	"JS3_WAIT_READ",
	"JS3_WAIT_ISSUE",
	"JS3_WAIT_DEPEND",
	"JS3_WAIT_FINISH",
	"JS4_JOBS",
	"JS4_TASKS",
	"JS4_ACTIVE",
	"",
	"JS4_WAIT_READ",
	"JS4_WAIT_ISSUE",
	"JS4_WAIT_DEPEND",
	"JS4_WAIT_FINISH",
	"JS5_JOBS",
	"JS5_TASKS",
	"JS5_ACTIVE",
	"",
	"JS5_WAIT_READ",
	"JS5_WAIT_ISSUE",
	"JS5_WAIT_DEPEND",
	"JS5_WAIT_FINISH",
	"JS6_JOBS",
	"JS6_TASKS",
	"JS6_ACTIVE",
	"",
	"JS6_WAIT_READ",
	"JS6_WAIT_ISSUE",
	"JS6_WAIT_DEPEND",
	"JS6_WAIT_FINISH",

	/*Tiler */
	"",
	"",
	"",
	"JOBS_PROCESSED",
	"TRIANGLES",
	"QUADS",
	"POLYGONS",
	"POINTS",
	"LINES",
	"VCACHE_HIT",
	"VCACHE_MISS",
	"FRONT_FACING",
	"BACK_FACING",
	"PRIM_VISIBLE",
	"PRIM_CULLED",
	"PRIM_CLIPPED",
	"LEVEL0",
	"LEVEL1",
	"LEVEL2",
	"LEVEL3",
	"LEVEL4",
	"LEVEL5",
	"LEVEL6",
	"LEVEL7",
	"COMMAND_1",
	"COMMAND_2",
	"COMMAND_3",
	"COMMAND_4",
	"COMMAND_4_7",
	"COMMAND_8_15",
	"COMMAND_16_63",
	"COMMAND_64",
	"COMPRESS_IN",
	"COMPRESS_OUT",
	"COMPRESS_FLUSH",
	"TIMESTAMPS",
	"PCACHE_HIT",
	"PCACHE_MISS",
	"PCACHE_LINE",
	"PCACHE_STALL",
	"WRBUF_HIT",
	"WRBUF_MISS",
	"WRBUF_LINE",
	"WRBUF_PARTIAL",
	"WRBUF_STALL",
	"ACTIVE",
	"LOADING_DESC",
	"INDEX_WAIT",
	"INDEX_RANGE_WAIT",
	"VERTEX_WAIT",
	"PCACHE_WAIT",
	"WRBUF_WAIT",
	"BUS_READ",
	"BUS_WRITE",
	"",
	"",
	"",
	"",
	"",
	"UTLB_STALL",
	"UTLB_REPLAY_MISS",
	"UTLB_REPLAY_FULL",
	"UTLB_NEW_MISS",
	"UTLB_HIT",

	/* Shader Core */
	"",
	"",
	"",
	"SHADER_CORE_ACTIVE",
	"FRAG_ACTIVE",
	"FRAG_PRIMATIVES",
	"FRAG_PRIMATIVES_DROPPED",
	"FRAG_CYCLE_DESC",
	"FRAG_CYCLES_PLR",
	"FRAG_CYCLES_VERT",
	"FRAG_CYCLES_TRISETUP",
	"FRAG_CYCLES_RAST",
	"FRAG_THREADS",
	"FRAG_DUMMY_THREADS",
	"FRAG_QUADS_RAST",
	"FRAG_QUADS_EZS_TEST",
	"FRAG_QUADS_EZS_KILLED",
	"FRAG_QUADS_LZS_TEST",
	"FRAG_QUADS_LZS_KILLED",
	"FRAG_CYCLE_NO_TILE",
	"FRAG_NUM_TILES",
	"FRAG_TRANS_ELIM",
	"COMPUTE_ACTIVE",
	"COMPUTE_TASKS",
	"COMPUTE_THREADS",
	"COMPUTE_CYCLES_DESC",
	"TRIPIPE_ACTIVE",
	"ARITH_WORDS",
	"ARITH_CYCLES_REG",
	"ARITH_CYCLES_L0",
	"ARITH_FRAG_DEPEND",
	"LS_WORDS",
	"LS_ISSUES",
	"LS_RESTARTS",
	"LS_REISSUES_MISS",
	"LS_REISSUES_VD",
	"LS_REISSUE_ATTRIB_MISS",
	"LS_NO_WB",
	"TEX_WORDS",
	"TEX_BUBBLES",
	"TEX_WORDS_L0",
	"TEX_WORDS_DESC",
	"TEX_THREADS",
	"TEX_RECIRC_FMISS",
	"TEX_RECIRC_DESC",
	"TEX_RECIRC_MULTI",
	"TEX_RECIRC_PMISS",
	"TEX_RECIRC_CONF",
	"LSC_READ_HITS",
	"LSC_READ_MISSES",
	"LSC_WRITE_HITS",
	"LSC_WRITE_MISSES",
	"LSC_ATOMIC_HITS",
	"LSC_ATOMIC_MISSES",
	"LSC_LINE_FETCHES",
	"LSC_DIRTY_LINE",
	"LSC_SNOOPS",
	"AXI_TLB_STALL",
	"AXI_TLB_MIESS",
	"AXI_TLB_TRANSACTION",
	"LS_TLB_MISS",
	"LS_TLB_HIT",
	"AXI_BEATS_READ",
	"AXI_BEATS_WRITTEN",

	/*L2 and MMU */
	"",
	"",
	"",
	"",
	"MMU_TABLE_WALK",
	"MMU_REPLAY_MISS",
	"MMU_REPLAY_FULL",
	"MMU_NEW_MISS",
	"MMU_HIT",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"UTLB_STALL",
	"UTLB_REPLAY_MISS",
	"UTLB_REPLAY_FULL",
	"UTLB_NEW_MISS",
	"UTLB_HIT",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"L2_WRITE_BEATS",
	"L2_READ_BEATS",
	"L2_ANY_LOOKUP",
	"L2_READ_LOOKUP",
	"L2_SREAD_LOOKUP",
	"L2_READ_REPLAY",
	"L2_READ_SNOOP",
	"L2_READ_HIT",
	"L2_CLEAN_MISS",
	"L2_WRITE_LOOKUP",
	"L2_SWRITE_LOOKUP",
	"L2_WRITE_REPLAY",
	"L2_WRITE_SNOOP",
	"L2_WRITE_HIT",
	"L2_EXT_READ_FULL",
	"L2_EXT_READ_HALF",
	"L2_EXT_WRITE_FULL",
	"L2_EXT_WRITE_HALF",
	"L2_EXT_READ",
	"L2_EXT_READ_LINE",
	"L2_EXT_WRITE",
	"L2_EXT_WRITE_LINE",
	"L2_EXT_WRITE_SMALL",
	"L2_EXT_BARRIER",
	"L2_EXT_AR_STALL",
	"L2_EXT_R_BUF_FULL",
	"L2_EXT_RD_BUF_FULL",
	"L2_EXT_R_RAW",
	"L2_EXT_W_STALL",
	"L2_EXT_W_BUF_FULL",
	"L2_EXT_R_W_HAZARD",
	"L2_TAG_HAZARD",
	"L2_SNOOP_FULL",
	"L2_REPLAY_FULL"
};

#define NUMBER_OF_HARDWARE_COUNTERS (sizeof(hardware_counter_names) / sizeof(hardware_counter_names[0]))

#define GET_HW_BLOCK(c) (((c) >> 6) & 0x3)
#define GET_COUNTER_OFFSET(c) ((c) & 0x3f)

/* Memory to dump hardware counters into */
static void *kernel_dump_buffer;

#if MALI_DDK_GATOR_API_VERSION == 2
/* DMA state used to manage lifetime of the buffer */
kbase_hwc_dma_mapping kernel_dump_buffer_handle;
#endif

/* kbase context and device */
static kbase_context *kbcontext = NULL;
static struct kbase_device *kbdevice = NULL;

/*
 * The following function has no external prototype in older DDK revisions.  When the DDK
 * is updated then this should be removed.
 */
struct kbase_device *kbase_find_device(int minor);

static volatile bool kbase_device_busy = false;
static unsigned int num_hardware_counters_enabled;

/*
 * gatorfs variables for counter enable state
 */
static mali_counter counters[NUMBER_OF_HARDWARE_COUNTERS];

/* An array used to return the data we recorded
 * as key,value pairs hence the *2
 */
static unsigned long counter_dump[NUMBER_OF_HARDWARE_COUNTERS * 2];

#define SYMBOL_GET(FUNCTION, ERROR_COUNT) \
	if(FUNCTION ## _symbol) \
	{ \
		printk("gator: mali " #FUNCTION " symbol was already registered\n"); \
		(ERROR_COUNT)++; \
	} \
	else \
	{ \
		FUNCTION ## _symbol = symbol_get(FUNCTION); \
		if(! FUNCTION ## _symbol) \
		{ \
			printk("gator: mali online " #FUNCTION " symbol not found\n"); \
			(ERROR_COUNT)++; \
		} \
	}

#define SYMBOL_CLEANUP(FUNCTION) \
	if(FUNCTION ## _symbol) \
	{ \
        symbol_put(FUNCTION); \
        FUNCTION ## _symbol = NULL; \
	}

/**
 * Execute symbol_get for all the Mali symbols and check for success.
 * @return the number of symbols not loaded.
 */
static int init_symbols(void)
{
	int error_count = 0;
	SYMBOL_GET(kbase_find_device, error_count);
	SYMBOL_GET(kbase_create_context, error_count);
	SYMBOL_GET(kbase_va_alloc, error_count);
	SYMBOL_GET(kbase_instr_hwcnt_enable, error_count);
	SYMBOL_GET(kbase_instr_hwcnt_clear, error_count);
	SYMBOL_GET(kbase_instr_hwcnt_dump_irq, error_count);
	SYMBOL_GET(kbase_instr_hwcnt_dump_complete, error_count);
	SYMBOL_GET(kbase_instr_hwcnt_disable, error_count);
	SYMBOL_GET(kbase_va_free, error_count);
	SYMBOL_GET(kbase_destroy_context, error_count);

	return error_count;
}

/**
 * Execute symbol_put for all the registered Mali symbols.
 */
static void clean_symbols(void)
{
	SYMBOL_CLEANUP(kbase_find_device);
	SYMBOL_CLEANUP(kbase_create_context);
	SYMBOL_CLEANUP(kbase_va_alloc);
	SYMBOL_CLEANUP(kbase_instr_hwcnt_enable);
	SYMBOL_CLEANUP(kbase_instr_hwcnt_clear);
	SYMBOL_CLEANUP(kbase_instr_hwcnt_dump_irq);
	SYMBOL_CLEANUP(kbase_instr_hwcnt_dump_complete);
	SYMBOL_CLEANUP(kbase_instr_hwcnt_disable);
	SYMBOL_CLEANUP(kbase_va_free);
	SYMBOL_CLEANUP(kbase_destroy_context);
}

/**
 * Determines whether a read should take place
 * @param current_time The current time, obtained from getnstimeofday()
 * @param prev_time_s The number of seconds at the previous read attempt.
 * @param next_read_time_ns The time (in ns) when the next read should be allowed.
 *
 * Note that this function has been separated out here to allow it to be tested.
 */
static int is_read_scheduled(const struct timespec *current_time, u32 *prev_time_s, s32 *next_read_time_ns)
{
	/* If the current ns count rolls over a second, roll the next read time too. */
	if (current_time->tv_sec != *prev_time_s) {
		*next_read_time_ns = *next_read_time_ns - NSEC_PER_SEC;
	}

	/* Abort the read if the next read time has not arrived. */
	if (current_time->tv_nsec < *next_read_time_ns) {
		return 0;
	}

	/* Set the next read some fixed time after this one, and update the read timestamp. */
	*next_read_time_ns = current_time->tv_nsec + READ_INTERVAL_NSEC;

	*prev_time_s = current_time->tv_sec;
	return 1;
}

static int start(void)
{
	kbase_uk_hwcnt_setup setup;
	mali_error err;
	int cnt;
	u16 bitmask[] = { 0, 0, 0, 0 };

	/* Setup HW counters */
	num_hardware_counters_enabled = 0;

	if (NUMBER_OF_HARDWARE_COUNTERS != 256) {
		pr_debug("Unexpected number of hardware counters defined: expecting 256, got %d\n", NUMBER_OF_HARDWARE_COUNTERS);
	}

	/* Calculate enable bitmasks based on counters_enabled array */
	for (cnt = 0; cnt < NUMBER_OF_HARDWARE_COUNTERS; cnt++) {
		const mali_counter *counter = &counters[cnt];
		if (counter->enabled) {
			int block = GET_HW_BLOCK(cnt);
			int enable_bit = GET_COUNTER_OFFSET(cnt) / 4;
			bitmask[block] |= (1 << enable_bit);
			pr_debug("gator: Mali-T6xx: hardware counter %s selected [%d]\n", hardware_counter_names[cnt], cnt);
			num_hardware_counters_enabled++;
		}
	}

	/* Create a kbase context for HW counters */
	if (num_hardware_counters_enabled > 0) {
		if (init_symbols() > 0) {
			clean_symbols();
			/* No Mali driver code entrypoints found - not a fault. */
			return 0;
		}

		kbdevice = kbase_find_device_symbol(-1);

		/* If we already got a context, fail */
		if (kbcontext) {
			pr_debug("gator: Mali-T6xx: error context already present\n");
			goto out;
		}

		/* kbcontext will only be valid after all the Mali symbols are loaded successfully */
		kbcontext = kbase_create_context_symbol(kbdevice);
		if (!kbcontext) {
			pr_debug("gator: Mali-T6xx: error creating kbase context\n");
			goto out;
		}

		/*
		 * The amount of memory needed to store the dump (bytes)
		 * DUMP_SIZE = number of core groups
		 *             * number of blocks (always 8 for midgard)
		 *             * number of counters per block (always 64 for midgard)
		 *             * number of bytes per counter (always 4 in midgard)
		 * For a Mali-T6xx with a single core group = 1 * 8 * 64 * 4 = 2048
		 * For a Mali-T6xx with a dual core group   = 2 * 8 * 64 * 4 = 4096
		 */
#if MALI_DDK_GATOR_API_VERSION == 1
		kernel_dump_buffer = kbase_va_alloc_symbol(kbcontext, 4096);
#elif MALI_DDK_GATOR_API_VERSION == 2
		kernel_dump_buffer = kbase_va_alloc_symbol(kbcontext, 4096, &kernel_dump_buffer_handle);
#endif
		if (!kernel_dump_buffer) {
			pr_debug("gator: Mali-T6xx: error trying to allocate va\n");
			goto destroy_context;
		}

		setup.dump_buffer = (uintptr_t)kernel_dump_buffer;
		setup.jm_bm = bitmask[JM_BLOCK];
		setup.tiler_bm = bitmask[TILER_BLOCK];
		setup.shader_bm = bitmask[SHADER_BLOCK];
		setup.mmu_l2_bm = bitmask[MMU_BLOCK];
		/* These counters do not exist on Mali-T60x */
		setup.l3_cache_bm = 0;

		/* Use kbase API to enable hardware counters and provide dump buffer */
		err = kbase_instr_hwcnt_enable_symbol(kbcontext, &setup);
		if (err != MALI_ERROR_NONE) {
			pr_debug("gator: Mali-T6xx: can't setup hardware counters\n");
			goto free_buffer;
		}
		pr_debug("gator: Mali-T6xx: hardware counters enabled\n");
		kbase_instr_hwcnt_clear_symbol(kbcontext);
		pr_debug("gator: Mali-T6xx: hardware counters cleared \n");

		kbase_device_busy = false;
	}

	return 0;

free_buffer:
#if MALI_DDK_GATOR_API_VERSION == 1
	kbase_va_free_symbol(kbcontext, kernel_dump_buffer);
#elif MALI_DDK_GATOR_API_VERSION == 2
	kbase_va_free_symbol(kbcontext, &kernel_dump_buffer_handle);
#endif

destroy_context:
	kbase_destroy_context_symbol(kbcontext);

out:
	clean_symbols();
	return -1;
}

static void stop(void)
{
	unsigned int cnt;
	kbase_context *temp_kbcontext;

	pr_debug("gator: Mali-T6xx: stop\n");

	/* Set all counters as disabled */
	for (cnt = 0; cnt < NUMBER_OF_HARDWARE_COUNTERS; cnt++) {
		counters[cnt].enabled = 0;
	}

	/* Destroy the context for HW counters */
	if (num_hardware_counters_enabled > 0 && kbcontext != NULL) {
		/*
		 * Set the global variable to NULL before destroying it, because
		 * other function will check this before using it.
		 */
		temp_kbcontext = kbcontext;
		kbcontext = NULL;

		kbase_instr_hwcnt_disable_symbol(temp_kbcontext);

#if MALI_DDK_GATOR_API_VERSION == 1
		kbase_va_free_symbol(temp_kbcontext, kernel_dump_buffer);
#elif MALI_DDK_GATOR_API_VERSION == 2
		kbase_va_free_symbol(temp_kbcontext, &kernel_dump_buffer_handle);
#endif

		kbase_destroy_context_symbol(temp_kbcontext);

		pr_debug("gator: Mali-T6xx: hardware counters stopped\n");

		clean_symbols();
	}
}

static int read(int **buffer)
{
	int cnt;
	int len = 0;
	u32 value = 0;
	mali_bool success;

	struct timespec current_time;
	static u32 prev_time_s = 0;
	static s32 next_read_time_ns = 0;

	if (!on_primary_core()) {
		return 0;
	}

	getnstimeofday(&current_time);

	/*
	 * Discard reads unless a respectable time has passed.  This reduces the load on the GPU without sacrificing
	 * accuracy on the Streamline display.
	 */
	if (!is_read_scheduled(&current_time, &prev_time_s, &next_read_time_ns)) {
		return 0;
	}

	/*
	 * Report the HW counters
	 * Only process hardware counters if at least one of the hardware counters is enabled.
	 */
	if (num_hardware_counters_enabled > 0) {
		const unsigned int vithar_blocks[] = {
			0x700,	/* VITHAR_JOB_MANAGER,     Block 0 */
			0x400,	/* VITHAR_TILER,           Block 1 */
			0x000,	/* VITHAR_SHADER_CORE,     Block 2 */
			0x500	/* VITHAR_MEMORY_SYSTEM,   Block 3 */
		};

		if (!kbcontext) {
			return -1;
		}

		/* Mali symbols can be called safely since a kbcontext is valid */
		if (kbase_instr_hwcnt_dump_complete_symbol(kbcontext, &success) == MALI_TRUE) {
			kbase_device_busy = false;

			if (success == MALI_TRUE) {
				for (cnt = 0; cnt < NUMBER_OF_HARDWARE_COUNTERS; cnt++) {
					const mali_counter *counter = &counters[cnt];
					if (counter->enabled) {
						const int block = GET_HW_BLOCK(cnt);
						const int counter_offset = GET_COUNTER_OFFSET(cnt);
						const u32 *counter_block = (u32 *) ((uintptr_t)kernel_dump_buffer + vithar_blocks[block]);
						const u32 *counter_address = counter_block + counter_offset;

						value = *counter_address;

						if (block == SHADER_BLOCK) {
							/* (counter_address + 0x000) has already been accounted-for above. */
							value += *(counter_address + 0x100);
							value += *(counter_address + 0x200);
							value += *(counter_address + 0x300);
						}

						counter_dump[len++] = counter->key;
						counter_dump[len++] = value;
					}
				}
			}
		}

		if (!kbase_device_busy) {
			kbase_device_busy = true;
			kbase_instr_hwcnt_dump_irq_symbol(kbcontext);
		}
	}

	/* Update the buffer */
	if (buffer) {
		*buffer = (int *)counter_dump;
	}

	return len;
}

static int create_files(struct super_block *sb, struct dentry *root)
{
	unsigned int event;
	/*
	 * Create the filesystem for all events
	 */
	int counter_index = 0;
	const char *mali_name = gator_mali_get_mali_name();

	for (event = 0; event < NUMBER_OF_HARDWARE_COUNTERS; event++) {
		if (gator_mali_create_file_system(mali_name, hardware_counter_names[counter_index], sb, root, &counters[event]) != 0)
			return -1;
		counter_index++;
	}

	return 0;
}

static struct gator_interface gator_events_mali_t6xx_interface = {
	.create_files = create_files,
	.start = start,
	.stop = stop,
	.read = read
};

int gator_events_mali_t6xx_hw_init(void)
{
	pr_debug("gator: Mali-T6xx: sw_counters init\n");

#if GATOR_TEST
	test_all_is_read_scheduled();
#endif

	gator_mali_initialise_counters(counters, NUMBER_OF_HARDWARE_COUNTERS);

	return gator_events_install(&gator_events_mali_t6xx_interface);
}

gator_events_init(gator_events_mali_t6xx_hw_init);
