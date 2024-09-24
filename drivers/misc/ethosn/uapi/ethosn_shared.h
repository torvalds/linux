/*
 *
 * (C) COPYRIGHT 2020-2023 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

/* This header defines interfaces that are shared between driver library,
 * kernel module and firmware. This is currently all profiling-related
 * definitions.
 */

#ifndef _ETHOSN_SHARED_H_
#define _ETHOSN_SHARED_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/**
 * enum ethosn_profiling_entry_type - Equivalent to the Driver Library's
 *                                 ProfilingEntry::Type enum.
 */
enum ethosn_profiling_entry_type {
	TIMELINE_EVENT_START,
	TIMELINE_EVENT_END,
	TIMELINE_EVENT_INSTANT,
	COUNTER_VALUE
};

/**
 * struct ethosn_profiling_entry - The firmware records a big stream of these,
 *                                 which store information about events and
 *                                 counters.
 * @timestamp:	The lower 32-bits of the clock cycle as defined by the PMU.
 * @type:	@see ethosn_profiling_entry_type.
 * @id:		Depending on type:
 *		  TIMELINE_EVENT_START/TIMELINE_EVENT_END: id is used to
 *                  associate start entry and end entries,
 *                  which will share the same id. Note that ID values will
 *                  be re-used once the end event has been recorded.
 *		  TIMELINE_EVENT_INSTANT: not relevant, can be anything
 *                COUNTER_VALUE: a member of FirmwareCounterName, identifying
 *                  which counter has been sampled.
 * @data:	Depending on type:
 *                TIMELINE_EVENT_START/TIMELINE_EVENT_INSTANT: a value that
 *                  can be decoded using TimelineEntryDataUnion,
 *		    describing what kind of event this is.
 *                TIMELINE_EVENT_END: not relevant
 *                COUNTER_VALUE: the counter value
 *
 * This struct is designed to be as small as possible, because we will be
 * creating and storing lots of these and we want the profiling overhead to be
 * as small as possible.
 */
struct ethosn_profiling_entry {
	uint32_t timestamp;
	uint32_t type : 2;
	uint32_t id : 5;
	uint32_t data : 25;
};

#if defined(__cplusplus)
static_assert(sizeof(ethosn_profiling_entry) == 8,
	      "ethosn_profiling_entry struct packing is incorrect");
#endif

/**
 * enum ethosn_profiling_hw_counter_types - Equivalent to the Driver Library's
 *                                       HardwareCounters enum.
 */
enum ethosn_profiling_hw_counter_types {
	BUS_ACCESS_RD_TRANSFERS,
	BUS_RD_COMPLETE_TRANSFERS,
	BUS_READ_BEATS,
	BUS_READ_TXFR_STALL_CYCLES,
	BUS_ACCESS_WR_TRANSFERS,
	BUS_WR_COMPLETE_TRANSFERS,
	BUS_WRITE_BEATS,
	BUS_WRITE_TXFR_STALL_CYCLES,
	BUS_WRITE_STALL_CYCLES,
	BUS_ERROR_COUNT,
	NCU_MCU_ICACHE_MISS,
	NCU_MCU_DCACHE_MISS,
	NCU_MCU_BUS_READ_BEATS,
	NCU_MCU_BUS_WRITE_BEATS
};

#if defined(__cplusplus)
enum class FirmwareCounterName: uint8_t {
	DwtSleepCycleCount,
	EventQueueSize,
	DmaNumReads,
	DmaNumWrites,
	DmaReadBytes,
	DmaWriteBytes,
	BusAccessRdTransfers,
	BusRdCompleteTransfers,
	BusReadBeats,
	BusReadTxfrStallCycles,
	BusAccessWrTransfers,
	BusWrCompleteTransfers,
	BusWriteBeats,
	BusWriteTxfrStallCycles,
	BusWriteStallCycles,
	BusErrorCount,
	NcuMcuIcacheMiss,
	NcuMcuDcacheMiss,
	NcuMcuBusReadBeats,
	NcuMcuBusWriteBeats,
};

enum class TimelineEventType: uint8_t {
	TimestampFull,
	Inference,
	UpdateProgress,
	Wfe,
	DmaReadSetup,
	DmaRead,
	DmaWriteSetup,
	DmaWrite,
	MceStripeSetup,
	MceStripe,
	PleStripeSetup,
	PleStripe,
	Udma,
	Label,
};

/* Describes the encoding of the "data" field for timeline events */
union TimelineEntryDataUnion {
	uint32_t m_Raw; /* Raw access to the full value. */

	/* The first 4 bits is always m_Type, which identifies the type
	 * for this timeline entry (a value of type TimelineEventType).
	 * The layout of the rest of the data is type-specific.
	 * Some types don't have any extra data.
	 */
	uint32_t m_Type : 4;
	struct {
		uint32_t m_Type : 4;
		uint32_t m_TimestampUpperBits : 21;
	} m_TimestampFullFields;
	struct {
		uint32_t m_Type : 4;
		uint32_t m_Char1 : 7;
		uint32_t m_Char2 : 7;
		uint32_t m_Char3 : 7;
	} m_LabelFields;
};

#endif /* defined(__cplusplus) */

#endif /* _ETHOSN_SHARED_H_ */
