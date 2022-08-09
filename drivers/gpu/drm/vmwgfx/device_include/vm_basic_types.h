/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 * Copyright 2015-2021 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/
#ifndef VM_BASIC_TYPES_H
#define VM_BASIC_TYPES_H

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/page.h>

typedef u32 uint32;
typedef s32 int32;
typedef u64 uint64;
typedef u16 uint16;
typedef s16 int16;
typedef u8  uint8;
typedef s8  int8;

typedef uint64 PA;
typedef uint32 PPN;
typedef uint32 PPN32;
typedef uint64 PPN64;

typedef bool Bool;

#define MAX_UINT64 U64_MAX
#define MAX_UINT32 U32_MAX
#define MAX_UINT16 U16_MAX

#define CONST64U(x) x##ULL

#ifndef MBYTES_SHIFT
#define MBYTES_SHIFT 20
#endif
#ifndef MBYTES_2_BYTES
#define MBYTES_2_BYTES(_nbytes) ((uint64)(_nbytes) << MBYTES_SHIFT)
#endif

/*
 * MKS Guest Stats types
 */

typedef struct MKSGuestStatCounter {
	atomic64_t count;
} MKSGuestStatCounter;

typedef struct MKSGuestStatCounterTime {
	MKSGuestStatCounter counter;
	atomic64_t selfCycles;
	atomic64_t totalCycles;
} MKSGuestStatCounterTime;

/*
 * Flags for MKSGuestStatInfoEntry::flags below
 */

#define MKS_GUEST_STAT_FLAG_NONE    0
#define MKS_GUEST_STAT_FLAG_TIME    (1U << 0)

typedef __attribute__((aligned(32))) struct MKSGuestStatInfoEntry {
	union {
		const char *s;
		uint64 u;
	} name;
	union {
		const char *s;
		uint64 u;
	} description;
	uint64 flags;
	union {
		MKSGuestStatCounter *counter;
		MKSGuestStatCounterTime *counterTime;
		uint64 u;
	} stat;
} MKSGuestStatInfoEntry;

#define INVALID_PPN64       ((PPN64)0x000fffffffffffffULL)

#define MKS_GUEST_STAT_INSTANCE_DESC_LENGTH 1024
#define MKS_GUEST_STAT_INSTANCE_MAX_STATS   4096
#define MKS_GUEST_STAT_INSTANCE_MAX_STAT_PPNS        \
	(PFN_UP(MKS_GUEST_STAT_INSTANCE_MAX_STATS *  \
		sizeof(MKSGuestStatCounterTime)))
#define MKS_GUEST_STAT_INSTANCE_MAX_INFO_PPNS        \
	(PFN_UP(MKS_GUEST_STAT_INSTANCE_MAX_STATS *  \
		sizeof(MKSGuestStatInfoEntry)))
#define MKS_GUEST_STAT_AVERAGE_NAME_LENGTH  40
#define MKS_GUEST_STAT_INSTANCE_MAX_STRS_PPNS        \
	(PFN_UP(MKS_GUEST_STAT_INSTANCE_MAX_STATS *  \
		MKS_GUEST_STAT_AVERAGE_NAME_LENGTH))

/*
 * The MKSGuestStatInstanceDescriptor is used as main interface to
 * communicate guest stats back to the host code.  The guest must
 * allocate an instance of this structure at the start of a page and
 * provide the physical address to the host.  From there the host code
 * can walk this structure to find other (pinned) pages containing the
 * stats data.
 *
 * Since the MKSGuestStatInfoEntry structures contain userlevel
 * pointers, the InstanceDescriptor also contains pointers to the
 * begining of these sections allowing the host side code to correctly
 * interpret the pointers.
 *
 * Because the host side code never acknowledges anything back to the
 * guest there is no strict requirement to maintain compatability
 * across releases.  If the interface changes the host might not be
 * able to log stats, but the guest will continue to run normally.
 */

typedef struct MKSGuestStatInstanceDescriptor {
	uint64 reservedMBZ; /* must be zero for now. */
	uint64 statStartVA; /* VA of the start of the stats section. */
	uint64 strsStartVA; /* VA of the start of the strings section. */
	uint64 statLength;  /* length of the stats section in bytes. */
	uint64 infoLength;  /* length of the info entry section in bytes. */
	uint64 strsLength;  /* length of the strings section in bytes. */
	PPN64  statPPNs[MKS_GUEST_STAT_INSTANCE_MAX_STAT_PPNS]; /* stat counters */
	PPN64  infoPPNs[MKS_GUEST_STAT_INSTANCE_MAX_INFO_PPNS]; /* stat info */
	PPN64  strsPPNs[MKS_GUEST_STAT_INSTANCE_MAX_STRS_PPNS]; /* strings */
	char   description[MKS_GUEST_STAT_INSTANCE_DESC_LENGTH];
} MKSGuestStatInstanceDescriptor;

#endif
