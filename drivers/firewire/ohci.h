/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FIREWIRE_OHCI_H
#define _FIREWIRE_OHCI_H

/* OHCI register map */

#define OHCI1394_Version                      0x000
#define OHCI1394_GUID_ROM                     0x004
#define OHCI1394_ATRetries                    0x008
#define OHCI1394_CSRData                      0x00C
#define OHCI1394_CSRCompareData               0x010
#define OHCI1394_CSRControl                   0x014
#define OHCI1394_ConfigROMhdr                 0x018
#define OHCI1394_BusID                        0x01C
#define OHCI1394_BusOptions                   0x020
#define OHCI1394_GUIDHi                       0x024
#define OHCI1394_GUIDLo                       0x028
#define OHCI1394_ConfigROMmap                 0x034
#define OHCI1394_PostedWriteAddressLo         0x038
#define OHCI1394_PostedWriteAddressHi         0x03C
#define OHCI1394_VendorID                     0x040
#define OHCI1394_HCControlSet                 0x050
#define OHCI1394_HCControlClear               0x054
#define  OHCI1394_HCControl_BIBimageValid	0x80000000
#define  OHCI1394_HCControl_noByteSwapData	0x40000000
#define  OHCI1394_HCControl_programPhyEnable	0x00800000
#define  OHCI1394_HCControl_aPhyEnhanceEnable	0x00400000
#define  OHCI1394_HCControl_LPS			0x00080000
#define  OHCI1394_HCControl_postedWriteEnable	0x00040000
#define  OHCI1394_HCControl_linkEnable		0x00020000
#define  OHCI1394_HCControl_softReset		0x00010000
#define OHCI1394_SelfIDBuffer                 0x064
#define OHCI1394_SelfIDCount                  0x068
#define OHCI1394_IRMultiChanMaskHiSet         0x070
#define OHCI1394_IRMultiChanMaskHiClear       0x074
#define OHCI1394_IRMultiChanMaskLoSet         0x078
#define OHCI1394_IRMultiChanMaskLoClear       0x07C
#define OHCI1394_IntEventSet                  0x080
#define OHCI1394_IntEventClear                0x084
#define OHCI1394_IntMaskSet                   0x088
#define OHCI1394_IntMaskClear                 0x08C
#define OHCI1394_IsoXmitIntEventSet           0x090
#define OHCI1394_IsoXmitIntEventClear         0x094
#define OHCI1394_IsoXmitIntMaskSet            0x098
#define OHCI1394_IsoXmitIntMaskClear          0x09C
#define OHCI1394_IsoRecvIntEventSet           0x0A0
#define OHCI1394_IsoRecvIntEventClear         0x0A4
#define OHCI1394_IsoRecvIntMaskSet            0x0A8
#define OHCI1394_IsoRecvIntMaskClear          0x0AC
#define OHCI1394_InitialBandwidthAvailable    0x0B0
#define OHCI1394_InitialChannelsAvailableHi   0x0B4
#define OHCI1394_InitialChannelsAvailableLo   0x0B8
#define OHCI1394_FairnessControl              0x0DC
#define OHCI1394_LinkControlSet               0x0E0
#define OHCI1394_LinkControlClear             0x0E4
#define   OHCI1394_LinkControl_rcvSelfID	(1 << 9)
#define   OHCI1394_LinkControl_rcvPhyPkt	(1 << 10)
#define   OHCI1394_LinkControl_cycleTimerEnable	(1 << 20)
#define   OHCI1394_LinkControl_cycleMaster	(1 << 21)
#define   OHCI1394_LinkControl_cycleSource	(1 << 22)
#define OHCI1394_NodeID                       0x0E8
#define   OHCI1394_NodeID_idValid             0x80000000
#define   OHCI1394_NodeID_root                0x40000000
#define   OHCI1394_NodeID_nodeNumber          0x0000003f
#define   OHCI1394_NodeID_busNumber           0x0000ffc0
#define OHCI1394_PhyControl                   0x0EC
#define   OHCI1394_PhyControl_Read(addr)	(((addr) << 8) | 0x00008000)
#define   OHCI1394_PhyControl_ReadDone		0x80000000
#define   OHCI1394_PhyControl_ReadData(r)	(((r) & 0x00ff0000) >> 16)
#define   OHCI1394_PhyControl_Write(addr, data)	(((addr) << 8) | (data) | 0x00004000)
#define   OHCI1394_PhyControl_WritePending	0x00004000
#define OHCI1394_IsochronousCycleTimer        0x0F0
#define OHCI1394_AsReqFilterHiSet             0x100
#define OHCI1394_AsReqFilterHiClear           0x104
#define OHCI1394_AsReqFilterLoSet             0x108
#define OHCI1394_AsReqFilterLoClear           0x10C
#define OHCI1394_PhyReqFilterHiSet            0x110
#define OHCI1394_PhyReqFilterHiClear          0x114
#define OHCI1394_PhyReqFilterLoSet            0x118
#define OHCI1394_PhyReqFilterLoClear          0x11C
#define OHCI1394_PhyUpperBound                0x120

#define OHCI1394_AsReqTrContextBase           0x180
#define OHCI1394_AsReqTrContextControlSet     0x180
#define OHCI1394_AsReqTrContextControlClear   0x184
#define OHCI1394_AsReqTrCommandPtr            0x18C

#define OHCI1394_AsRspTrContextBase           0x1A0
#define OHCI1394_AsRspTrContextControlSet     0x1A0
#define OHCI1394_AsRspTrContextControlClear   0x1A4
#define OHCI1394_AsRspTrCommandPtr            0x1AC

#define OHCI1394_AsReqRcvContextBase          0x1C0
#define OHCI1394_AsReqRcvContextControlSet    0x1C0
#define OHCI1394_AsReqRcvContextControlClear  0x1C4
#define OHCI1394_AsReqRcvCommandPtr           0x1CC

#define OHCI1394_AsRspRcvContextBase          0x1E0
#define OHCI1394_AsRspRcvContextControlSet    0x1E0
#define OHCI1394_AsRspRcvContextControlClear  0x1E4
#define OHCI1394_AsRspRcvCommandPtr           0x1EC

/* Isochronous transmit registers */
#define OHCI1394_IsoXmitContextBase(n)           (0x200 + 16 * (n))
#define OHCI1394_IsoXmitContextControlSet(n)     (0x200 + 16 * (n))
#define OHCI1394_IsoXmitContextControlClear(n)   (0x204 + 16 * (n))
#define OHCI1394_IsoXmitCommandPtr(n)            (0x20C + 16 * (n))

/* Isochronous receive registers */
#define OHCI1394_IsoRcvContextBase(n)         (0x400 + 32 * (n))
#define OHCI1394_IsoRcvContextControlSet(n)   (0x400 + 32 * (n))
#define OHCI1394_IsoRcvContextControlClear(n) (0x404 + 32 * (n))
#define OHCI1394_IsoRcvCommandPtr(n)          (0x40C + 32 * (n))
#define OHCI1394_IsoRcvContextMatch(n)        (0x410 + 32 * (n))

/* Interrupts Mask/Events */
#define OHCI1394_reqTxComplete		0x00000001
#define OHCI1394_respTxComplete		0x00000002
#define OHCI1394_ARRQ			0x00000004
#define OHCI1394_ARRS			0x00000008
#define OHCI1394_RQPkt			0x00000010
#define OHCI1394_RSPkt			0x00000020
#define OHCI1394_isochTx		0x00000040
#define OHCI1394_isochRx		0x00000080
#define OHCI1394_postedWriteErr		0x00000100
#define OHCI1394_lockRespErr		0x00000200
#define OHCI1394_selfIDComplete		0x00010000
#define OHCI1394_busReset		0x00020000
#define OHCI1394_regAccessFail		0x00040000
#define OHCI1394_phy			0x00080000
#define OHCI1394_cycleSynch		0x00100000
#define OHCI1394_cycle64Seconds		0x00200000
#define OHCI1394_cycleLost		0x00400000
#define OHCI1394_cycleInconsistent	0x00800000
#define OHCI1394_unrecoverableError	0x01000000
#define OHCI1394_cycleTooLong		0x02000000
#define OHCI1394_phyRegRcvd		0x04000000
#define OHCI1394_masterIntEnable	0x80000000

#define OHCI1394_evt_no_status		0x0
#define OHCI1394_evt_long_packet	0x2
#define OHCI1394_evt_missing_ack	0x3
#define OHCI1394_evt_underrun		0x4
#define OHCI1394_evt_overrun		0x5
#define OHCI1394_evt_descriptor_read	0x6
#define OHCI1394_evt_data_read		0x7
#define OHCI1394_evt_data_write		0x8
#define OHCI1394_evt_bus_reset		0x9
#define OHCI1394_evt_timeout		0xa
#define OHCI1394_evt_tcode_err		0xb
#define OHCI1394_evt_reserved_b		0xc
#define OHCI1394_evt_reserved_c		0xd
#define OHCI1394_evt_unknown		0xe
#define OHCI1394_evt_flushed		0xf


// Asynchronous Transmit DMA.
//
// The content of first two quadlets of data for AT DMA is different from the header for IEEE 1394
// asynchronous packet.

#define OHCI1394_AT_DATA_Q0_srcBusID_MASK		0x00800000
#define OHCI1394_AT_DATA_Q0_srcBusID_SHIFT		23
#define OHCI1394_AT_DATA_Q0_spd_MASK			0x00070000
#define OHCI1394_AT_DATA_Q0_spd_SHIFT			16
#define OHCI1394_AT_DATA_Q0_tLabel_MASK			0x0000fc00
#define OHCI1394_AT_DATA_Q0_tLabel_SHIFT		10
#define OHCI1394_AT_DATA_Q0_rt_MASK			0x00000300
#define OHCI1394_AT_DATA_Q0_rt_SHIFT			8
#define OHCI1394_AT_DATA_Q0_tCode_MASK			0x000000f0
#define OHCI1394_AT_DATA_Q0_tCode_SHIFT			4
#define OHCI1394_AT_DATA_Q1_destinationId_MASK		0xffff0000
#define OHCI1394_AT_DATA_Q1_destinationId_SHIFT		16
#define OHCI1394_AT_DATA_Q1_destinationOffsetHigh_MASK	0x0000ffff
#define OHCI1394_AT_DATA_Q1_destinationOffsetHigh_SHIFT	0
#define OHCI1394_AT_DATA_Q1_rCode_MASK			0x0000f000
#define OHCI1394_AT_DATA_Q1_rCode_SHIFT			12

static inline bool ohci1394_at_data_get_src_bus_id(const __le32 *data)
{
	return !!((data[0] & OHCI1394_AT_DATA_Q0_srcBusID_MASK) >> OHCI1394_AT_DATA_Q0_srcBusID_SHIFT);
}

static inline void ohci1394_at_data_set_src_bus_id(__le32 *data, bool src_bus_id)
{
	data[0] &= cpu_to_le32(~OHCI1394_AT_DATA_Q0_srcBusID_MASK);
	data[0] |= cpu_to_le32((src_bus_id << OHCI1394_AT_DATA_Q0_srcBusID_SHIFT) & OHCI1394_AT_DATA_Q0_srcBusID_MASK);
}

static inline unsigned int ohci1394_at_data_get_speed(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_AT_DATA_Q0_spd_MASK) >> OHCI1394_AT_DATA_Q0_spd_SHIFT;
}

static inline void ohci1394_at_data_set_speed(__le32 *data, unsigned int scode)
{
	data[0] &= cpu_to_le32(~OHCI1394_AT_DATA_Q0_spd_MASK);
	data[0] |= cpu_to_le32((scode << OHCI1394_AT_DATA_Q0_spd_SHIFT) & OHCI1394_AT_DATA_Q0_spd_MASK);
}

static inline unsigned int ohci1394_at_data_get_tlabel(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_AT_DATA_Q0_tLabel_MASK) >> OHCI1394_AT_DATA_Q0_tLabel_SHIFT;
}

static inline void ohci1394_at_data_set_tlabel(__le32 *data, unsigned int tlabel)
{
	data[0] &= cpu_to_le32(~OHCI1394_AT_DATA_Q0_tLabel_MASK);
	data[0] |= cpu_to_le32((tlabel << OHCI1394_AT_DATA_Q0_tLabel_SHIFT) & OHCI1394_AT_DATA_Q0_tLabel_MASK);
}

static inline unsigned int ohci1394_at_data_get_retry(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_AT_DATA_Q0_rt_MASK) >> OHCI1394_AT_DATA_Q0_rt_SHIFT;
}

static inline void ohci1394_at_data_set_retry(__le32 *data, unsigned int retry)
{
	data[0] &= cpu_to_le32(~OHCI1394_AT_DATA_Q0_rt_MASK);
	data[0] |= cpu_to_le32((retry << OHCI1394_AT_DATA_Q0_rt_SHIFT) & OHCI1394_AT_DATA_Q0_rt_MASK);
}

static inline unsigned int ohci1394_at_data_get_tcode(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_AT_DATA_Q0_tCode_MASK) >> OHCI1394_AT_DATA_Q0_tCode_SHIFT;
}

static inline void ohci1394_at_data_set_tcode(__le32 *data, unsigned int tcode)
{
	data[0] &= cpu_to_le32(~OHCI1394_AT_DATA_Q0_tCode_MASK);
	data[0] |= cpu_to_le32((tcode << OHCI1394_AT_DATA_Q0_tCode_SHIFT) & OHCI1394_AT_DATA_Q0_tCode_MASK);
}

static inline unsigned int ohci1394_at_data_get_destination_id(const __le32 *data)
{
	return (le32_to_cpu(data[1]) & OHCI1394_AT_DATA_Q1_destinationId_MASK) >> OHCI1394_AT_DATA_Q1_destinationId_SHIFT;
}

static inline void ohci1394_at_data_set_destination_id(__le32 *data, unsigned int destination_id)
{
	data[1] &= cpu_to_le32(~OHCI1394_AT_DATA_Q1_destinationId_MASK);
	data[1] |= cpu_to_le32((destination_id << OHCI1394_AT_DATA_Q1_destinationId_SHIFT) & OHCI1394_AT_DATA_Q1_destinationId_MASK);
}

static inline u64 ohci1394_at_data_get_destination_offset(const __le32 *data)
{
	u64 hi = (u64)((le32_to_cpu(data[1]) & OHCI1394_AT_DATA_Q1_destinationOffsetHigh_MASK) >> OHCI1394_AT_DATA_Q1_destinationOffsetHigh_SHIFT);
	u64 lo = (u64)le32_to_cpu(data[2]);
	return (hi << 32) | lo;
}

static inline void ohci1394_at_data_set_destination_offset(__le32 *data, u64 offset)
{
	u32 hi = (u32)(offset >> 32);
	u32 lo = (u32)(offset & 0x00000000ffffffff);
	data[1] &= cpu_to_le32(~OHCI1394_AT_DATA_Q1_destinationOffsetHigh_MASK);
	data[1] |= cpu_to_le32((hi << OHCI1394_AT_DATA_Q1_destinationOffsetHigh_SHIFT) & OHCI1394_AT_DATA_Q1_destinationOffsetHigh_MASK);
	data[2] = cpu_to_le32(lo);
}

static inline unsigned int ohci1394_at_data_get_rcode(const __le32 *data)
{
	return (le32_to_cpu(data[1]) & OHCI1394_AT_DATA_Q1_rCode_MASK) >> OHCI1394_AT_DATA_Q1_rCode_SHIFT;
}

static inline void ohci1394_at_data_set_rcode(__le32 *data, unsigned int rcode)
{
	data[1] &= cpu_to_le32(~OHCI1394_AT_DATA_Q1_rCode_MASK);
	data[1] |= cpu_to_le32((rcode << OHCI1394_AT_DATA_Q1_rCode_SHIFT) & OHCI1394_AT_DATA_Q1_rCode_MASK);
}

// Isochronous Transmit DMA.
//
// The content of first two quadlets of data for IT DMA is different from the header for IEEE 1394
// isochronous packet.

#define OHCI1394_IT_DATA_Q0_spd_MASK		0x00070000
#define OHCI1394_IT_DATA_Q0_spd_SHIFT		16
#define OHCI1394_IT_DATA_Q0_tag_MASK		0x0000c000
#define OHCI1394_IT_DATA_Q0_tag_SHIFT		14
#define OHCI1394_IT_DATA_Q0_chanNum_MASK	0x00003f00
#define OHCI1394_IT_DATA_Q0_chanNum_SHIFT	8
#define OHCI1394_IT_DATA_Q0_tcode_MASK		0x000000f0
#define OHCI1394_IT_DATA_Q0_tcode_SHIFT		4
#define OHCI1394_IT_DATA_Q0_sy_MASK		0x0000000f
#define OHCI1394_IT_DATA_Q0_sy_SHIFT		0
#define OHCI1394_IT_DATA_Q1_dataLength_MASK	0xffff0000
#define OHCI1394_IT_DATA_Q1_dataLength_SHIFT	16

static inline unsigned int ohci1394_it_data_get_speed(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_IT_DATA_Q0_spd_MASK) >> OHCI1394_IT_DATA_Q0_spd_SHIFT;
}

static inline void ohci1394_it_data_set_speed(__le32 *data, unsigned int scode)
{
	data[0] &= cpu_to_le32(~OHCI1394_IT_DATA_Q0_spd_MASK);
	data[0] |= cpu_to_le32((scode << OHCI1394_IT_DATA_Q0_spd_SHIFT) & OHCI1394_IT_DATA_Q0_spd_MASK);
}

static inline unsigned int ohci1394_it_data_get_tag(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_IT_DATA_Q0_tag_MASK) >> OHCI1394_IT_DATA_Q0_tag_SHIFT;
}

static inline void ohci1394_it_data_set_tag(__le32 *data, unsigned int tag)
{
	data[0] &= cpu_to_le32(~OHCI1394_IT_DATA_Q0_tag_MASK);
	data[0] |= cpu_to_le32((tag << OHCI1394_IT_DATA_Q0_tag_SHIFT) & OHCI1394_IT_DATA_Q0_tag_MASK);
}

static inline unsigned int ohci1394_it_data_get_channel(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_IT_DATA_Q0_chanNum_MASK) >> OHCI1394_IT_DATA_Q0_chanNum_SHIFT;
}

static inline void ohci1394_it_data_set_channel(__le32 *data, unsigned int channel)
{
	data[0] &= cpu_to_le32(~OHCI1394_IT_DATA_Q0_chanNum_MASK);
	data[0] |= cpu_to_le32((channel << OHCI1394_IT_DATA_Q0_chanNum_SHIFT) & OHCI1394_IT_DATA_Q0_chanNum_MASK);
}

static inline unsigned int ohci1394_it_data_get_tcode(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_IT_DATA_Q0_tcode_MASK) >> OHCI1394_IT_DATA_Q0_tcode_SHIFT;
}

static inline void ohci1394_it_data_set_tcode(__le32 *data, unsigned int tcode)
{
	data[0] &= cpu_to_le32(~OHCI1394_IT_DATA_Q0_tcode_MASK);
	data[0] |= cpu_to_le32((tcode << OHCI1394_IT_DATA_Q0_tcode_SHIFT) & OHCI1394_IT_DATA_Q0_tcode_MASK);
}

static inline unsigned int ohci1394_it_data_get_sync(const __le32 *data)
{
	return (le32_to_cpu(data[0]) & OHCI1394_IT_DATA_Q0_sy_MASK) >> OHCI1394_IT_DATA_Q0_sy_SHIFT;
}

static inline void ohci1394_it_data_set_sync(__le32 *data, unsigned int sync)
{
	data[0] &= cpu_to_le32(~OHCI1394_IT_DATA_Q0_sy_MASK);
	data[0] |= cpu_to_le32((sync << OHCI1394_IT_DATA_Q0_sy_SHIFT) & OHCI1394_IT_DATA_Q0_sy_MASK);
}

static inline unsigned int ohci1394_it_data_get_data_length(const __le32 *data)
{
	return (le32_to_cpu(data[1]) & OHCI1394_IT_DATA_Q1_dataLength_MASK) >> OHCI1394_IT_DATA_Q1_dataLength_SHIFT;
}

static inline void ohci1394_it_data_set_data_length(__le32 *data, unsigned int data_length)
{
	data[1] &= cpu_to_le32(~OHCI1394_IT_DATA_Q1_dataLength_MASK);
	data[1] |= cpu_to_le32((data_length << OHCI1394_IT_DATA_Q1_dataLength_SHIFT) & OHCI1394_IT_DATA_Q1_dataLength_MASK);
}

// Self-ID DMA.

#define OHCI1394_SelfIDCount_selfIDError_MASK		0x80000000
#define OHCI1394_SelfIDCount_selfIDError_SHIFT		31
#define OHCI1394_SelfIDCount_selfIDGeneration_MASK	0x00ff0000
#define OHCI1394_SelfIDCount_selfIDGeneration_SHIFT	16
#define OHCI1394_SelfIDCount_selfIDSize_MASK		0x000007fc
#define OHCI1394_SelfIDCount_selfIDSize_SHIFT		2

static inline bool ohci1394_self_id_count_is_error(u32 value)
{
	return !!((value & OHCI1394_SelfIDCount_selfIDError_MASK) >> OHCI1394_SelfIDCount_selfIDError_SHIFT);
}

static inline u8 ohci1394_self_id_count_get_generation(u32 value)
{
	return (value & OHCI1394_SelfIDCount_selfIDGeneration_MASK) >> OHCI1394_SelfIDCount_selfIDGeneration_SHIFT;
}

// In 1394 OHCI specification, the maximum size of self ID stream is 504 quadlets
// (= 63 devices * 4 self ID packets * 2 quadlets). The selfIDSize field accommodates it and its
// additional first quadlet, since the field is 9 bits (0x1ff = 511).
static inline u32 ohci1394_self_id_count_get_size(u32 value)
{
	return (value & OHCI1394_SelfIDCount_selfIDSize_MASK) >> OHCI1394_SelfIDCount_selfIDSize_SHIFT;
}

#define OHCI1394_SELF_ID_RECEIVE_Q0_GENERATION_MASK	0x00ff0000
#define OHCI1394_SELF_ID_RECEIVE_Q0_GENERATION_SHIFT	16
#define OHCI1394_SELF_ID_RECEIVE_Q0_TIMESTAMP_MASK	0x0000ffff
#define OHCI1394_SELF_ID_RECEIVE_Q0_TIMESTAMP_SHIFT	0

static inline u8 ohci1394_self_id_receive_q0_get_generation(u32 quadlet0)
{
	return (quadlet0 & OHCI1394_SELF_ID_RECEIVE_Q0_GENERATION_MASK) >> OHCI1394_SELF_ID_RECEIVE_Q0_GENERATION_SHIFT;
}

static inline u16 ohci1394_self_id_receive_q0_get_timestamp(u32 quadlet0)
{
	return (quadlet0 & OHCI1394_SELF_ID_RECEIVE_Q0_TIMESTAMP_MASK) >> OHCI1394_SELF_ID_RECEIVE_Q0_TIMESTAMP_SHIFT;
}

#endif /* _FIREWIRE_OHCI_H */
