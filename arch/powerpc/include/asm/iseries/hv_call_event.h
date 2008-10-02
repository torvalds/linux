/*
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * This file contains the "hypervisor call" interface which is used to
 * drive the hypervisor from the OS.
 */
#ifndef _ASM_POWERPC_ISERIES_HV_CALL_EVENT_H
#define _ASM_POWERPC_ISERIES_HV_CALL_EVENT_H

#include <linux/types.h>
#include <linux/dma-mapping.h>

#include <asm/iseries/hv_call_sc.h>
#include <asm/iseries/hv_types.h>
#include <asm/abs_addr.h>

struct HvLpEvent;

typedef u8 HvLpEvent_Type;
typedef u8 HvLpEvent_AckInd;
typedef u8 HvLpEvent_AckType;

typedef u8 HvLpDma_Direction;
typedef u8 HvLpDma_AddressType;

typedef u64 HvLpEvent_Rc;
typedef u64 HvLpDma_Rc;

#define HvCallEventAckLpEvent				HvCallEvent +  0
#define HvCallEventCancelLpEvent			HvCallEvent +  1
#define HvCallEventCloseLpEventPath			HvCallEvent +  2
#define HvCallEventDmaBufList				HvCallEvent +  3
#define HvCallEventDmaSingle				HvCallEvent +  4
#define HvCallEventDmaToSp				HvCallEvent +  5
#define HvCallEventGetOverflowLpEvents			HvCallEvent +  6
#define HvCallEventGetSourceLpInstanceId		HvCallEvent +  7
#define HvCallEventGetTargetLpInstanceId		HvCallEvent +  8
#define HvCallEventOpenLpEventPath			HvCallEvent +  9
#define HvCallEventSetLpEventStack			HvCallEvent + 10
#define HvCallEventSignalLpEvent			HvCallEvent + 11
#define HvCallEventSignalLpEventParms			HvCallEvent + 12
#define HvCallEventSetInterLpQueueIndex			HvCallEvent + 13
#define HvCallEventSetLpEventQueueInterruptProc		HvCallEvent + 14
#define HvCallEventRouter15				HvCallEvent + 15

static inline void HvCallEvent_getOverflowLpEvents(u8 queueIndex)
{
	HvCall1(HvCallEventGetOverflowLpEvents, queueIndex);
}

static inline void HvCallEvent_setInterLpQueueIndex(u8 queueIndex)
{
	HvCall1(HvCallEventSetInterLpQueueIndex, queueIndex);
}

static inline void HvCallEvent_setLpEventStack(u8 queueIndex,
		char *eventStackAddr, u32 eventStackSize)
{
	HvCall3(HvCallEventSetLpEventStack, queueIndex,
			virt_to_abs(eventStackAddr), eventStackSize);
}

static inline void HvCallEvent_setLpEventQueueInterruptProc(u8 queueIndex,
		u16 lpLogicalProcIndex)
{
	HvCall2(HvCallEventSetLpEventQueueInterruptProc, queueIndex,
			lpLogicalProcIndex);
}

static inline HvLpEvent_Rc HvCallEvent_signalLpEvent(struct HvLpEvent *event)
{
	return HvCall1(HvCallEventSignalLpEvent, virt_to_abs(event));
}

static inline HvLpEvent_Rc HvCallEvent_signalLpEventFast(HvLpIndex targetLp,
		HvLpEvent_Type type, u16 subtype, HvLpEvent_AckInd ackInd,
		HvLpEvent_AckType ackType, HvLpInstanceId sourceInstanceId,
		HvLpInstanceId targetInstanceId, u64 correlationToken,
		u64 eventData1, u64 eventData2, u64 eventData3,
		u64 eventData4, u64 eventData5)
{
	/* Pack the misc bits into a single Dword to pass to PLIC */
	union {
		struct {
			u8		ack_and_target;
			u8		type;
			u16		subtype;
			HvLpInstanceId	src_inst;
			HvLpInstanceId	target_inst;
		} parms;
		u64		dword;
	} packed;

	packed.parms.ack_and_target = (ackType << 7) | (ackInd << 6) | targetLp;
	packed.parms.type = type;
	packed.parms.subtype = subtype;
	packed.parms.src_inst = sourceInstanceId;
	packed.parms.target_inst = targetInstanceId;

	return HvCall7(HvCallEventSignalLpEventParms, packed.dword,
			correlationToken, eventData1, eventData2,
			eventData3, eventData4, eventData5);
}

extern void *iseries_hv_alloc(size_t size, dma_addr_t *dma_handle, gfp_t flag);
extern void iseries_hv_free(size_t size, void *vaddr, dma_addr_t dma_handle);
extern dma_addr_t iseries_hv_map(void *vaddr, size_t size,
			enum dma_data_direction direction);
extern void iseries_hv_unmap(dma_addr_t dma_handle, size_t size,
			enum dma_data_direction direction);

static inline HvLpEvent_Rc HvCallEvent_ackLpEvent(struct HvLpEvent *event)
{
	return HvCall1(HvCallEventAckLpEvent, virt_to_abs(event));
}

static inline HvLpEvent_Rc HvCallEvent_cancelLpEvent(struct HvLpEvent *event)
{
	return HvCall1(HvCallEventCancelLpEvent, virt_to_abs(event));
}

static inline HvLpInstanceId HvCallEvent_getSourceLpInstanceId(
		HvLpIndex targetLp, HvLpEvent_Type type)
{
	return HvCall2(HvCallEventGetSourceLpInstanceId, targetLp, type);
}

static inline HvLpInstanceId HvCallEvent_getTargetLpInstanceId(
		HvLpIndex targetLp, HvLpEvent_Type type)
{
	return HvCall2(HvCallEventGetTargetLpInstanceId, targetLp, type);
}

static inline void HvCallEvent_openLpEventPath(HvLpIndex targetLp,
		HvLpEvent_Type type)
{
	HvCall2(HvCallEventOpenLpEventPath, targetLp, type);
}

static inline void HvCallEvent_closeLpEventPath(HvLpIndex targetLp,
		HvLpEvent_Type type)
{
	HvCall2(HvCallEventCloseLpEventPath, targetLp, type);
}

static inline HvLpDma_Rc HvCallEvent_dmaBufList(HvLpEvent_Type type,
		HvLpIndex remoteLp, HvLpDma_Direction direction,
		HvLpInstanceId localInstanceId,
		HvLpInstanceId remoteInstanceId,
		HvLpDma_AddressType localAddressType,
		HvLpDma_AddressType remoteAddressType,
		/* Do these need to be converted to absolute addresses? */
		u64 localBufList, u64 remoteBufList, u32 transferLength)
{
	/* Pack the misc bits into a single Dword to pass to PLIC */
	union {
		struct {
			u8		flags;
			HvLpIndex	remote;
			u8		type;
			u8		reserved;
			HvLpInstanceId	local_inst;
			HvLpInstanceId	remote_inst;
		} parms;
		u64		dword;
	} packed;

	packed.parms.flags = (direction << 7) |
		(localAddressType << 6) | (remoteAddressType << 5);
	packed.parms.remote = remoteLp;
	packed.parms.type = type;
	packed.parms.reserved = 0;
	packed.parms.local_inst = localInstanceId;
	packed.parms.remote_inst = remoteInstanceId;

	return HvCall4(HvCallEventDmaBufList, packed.dword, localBufList,
			remoteBufList, transferLength);
}

static inline HvLpDma_Rc HvCallEvent_dmaToSp(void *local, u32 remote,
		u32 length, HvLpDma_Direction dir)
{
	return HvCall4(HvCallEventDmaToSp, virt_to_abs(local), remote,
			length, dir);
}

#endif /* _ASM_POWERPC_ISERIES_HV_CALL_EVENT_H */
