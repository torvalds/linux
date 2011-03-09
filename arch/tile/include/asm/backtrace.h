/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _TILE_BACKTRACE_H
#define _TILE_BACKTRACE_H



#include <linux/types.h>

#include <arch/chip.h>

#if defined(__tile__)
typedef unsigned long VirtualAddress;
#elif CHIP_VA_WIDTH() > 32
typedef unsigned long long VirtualAddress;
#else
typedef unsigned int VirtualAddress;
#endif


/** Reads 'size' bytes from 'address' and writes the data to 'result'.
 * Returns true if successful, else false (e.g. memory not readable).
 */
typedef bool (*BacktraceMemoryReader)(void *result,
				      VirtualAddress address,
				      unsigned int size,
				      void *extra);

typedef struct {
	/** Current PC. */
	VirtualAddress pc;

	/** Current stack pointer value. */
	VirtualAddress sp;

	/** Current frame pointer value (i.e. caller's stack pointer) */
	VirtualAddress fp;

	/** Internal use only: caller's PC for first frame. */
	VirtualAddress initial_frame_caller_pc;

	/** Internal use only: callback to read memory. */
	BacktraceMemoryReader read_memory_func;

	/** Internal use only: arbitrary argument to read_memory_func. */
	void *read_memory_func_extra;

} BacktraceIterator;


/** Initializes a backtracer to start from the given location.
 *
 * If the frame pointer cannot be determined it is set to -1.
 *
 * @param state The state to be filled in.
 * @param read_memory_func A callback that reads memory. If NULL, a default
 *        value is provided.
 * @param read_memory_func_extra An arbitrary argument to read_memory_func.
 * @param pc The current PC.
 * @param lr The current value of the 'lr' register.
 * @param sp The current value of the 'sp' register.
 * @param r52 The current value of the 'r52' register.
 */
extern void backtrace_init(BacktraceIterator *state,
			   BacktraceMemoryReader read_memory_func,
			   void *read_memory_func_extra,
			   VirtualAddress pc, VirtualAddress lr,
			   VirtualAddress sp, VirtualAddress r52);


/** Advances the backtracing state to the calling frame, returning
 * true iff successful.
 */
extern bool backtrace_next(BacktraceIterator *state);


typedef enum {

	/* We have no idea what the caller's pc is. */
	PC_LOC_UNKNOWN,

	/* The caller's pc is currently in lr. */
	PC_LOC_IN_LR,

	/* The caller's pc can be found by dereferencing the caller's sp. */
	PC_LOC_ON_STACK

} CallerPCLocation;


typedef enum {

	/* We have no idea what the caller's sp is. */
	SP_LOC_UNKNOWN,

	/* The caller's sp is currently in r52. */
	SP_LOC_IN_R52,

	/* The caller's sp can be found by adding a certain constant
	 * to the current value of sp.
	 */
	SP_LOC_OFFSET

} CallerSPLocation;


/* Bit values ORed into CALLER_* values for info ops. */
enum {
	/* Setting the low bit on any of these values means the info op
	 * applies only to one bundle ago.
	 */
	ONE_BUNDLE_AGO_FLAG = 1,

	/* Setting this bit on a CALLER_SP_* value means the PC is in LR.
	 * If not set, PC is on the stack.
	 */
	PC_IN_LR_FLAG = 2,

	/* This many of the low bits of a CALLER_SP_* value are for the
	 * flag bits above.
	 */
	NUM_INFO_OP_FLAGS = 2,

	/* We cannot have one in the memory pipe so this is the maximum. */
	MAX_INFO_OPS_PER_BUNDLE = 2
};


/** Internal constants used to define 'info' operands. */
enum {
	/* 0 and 1 are reserved, as are all negative numbers. */

	CALLER_UNKNOWN_BASE = 2,

	CALLER_SP_IN_R52_BASE = 4,

	CALLER_SP_OFFSET_BASE = 8,

	/* Marks the entry point of certain functions. */
	ENTRY_POINT_INFO_OP = 16
};


/** Current backtracer state describing where it thinks the caller is. */
typedef struct {
	/*
	 * Public fields
	 */

	/* How do we find the caller's PC? */
	CallerPCLocation pc_location : 8;

	/* How do we find the caller's SP? */
	CallerSPLocation sp_location : 8;

	/* If sp_location == SP_LOC_OFFSET, then caller_sp == sp +
	 * loc->sp_offset. Else this field is undefined.
	 */
	uint16_t sp_offset;

	/* In the most recently visited bundle a terminating bundle? */
	bool at_terminating_bundle;

	/*
	 * Private fields
	 */

	/* Will the forward scanner see someone clobbering sp
	 * (i.e. changing it with something other than addi sp, sp, N?)
	 */
	bool sp_clobber_follows;

	/* Operand to next "visible" info op (no more than one bundle past
	 * the next terminating bundle), or -32768 if none.
	 */
	int16_t next_info_operand;

	/* Is the info of in next_info_op in the very next bundle? */
	bool is_next_info_operand_adjacent;

} CallerLocation;




#endif /* _TILE_BACKTRACE_H */
