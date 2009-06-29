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
#define  OHCI1394_SelfIDCount_selfIDError	0x80000000
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
#define   OHCI1394_NodeID_nodeNumber          0x0000003f
#define   OHCI1394_NodeID_busNumber           0x0000ffc0
#define OHCI1394_PhyControl                   0x0EC
#define   OHCI1394_PhyControl_Read(addr)	(((addr) << 8) | 0x00008000)
#define   OHCI1394_PhyControl_ReadDone		0x80000000
#define   OHCI1394_PhyControl_ReadData(r)	(((r) & 0x00ff0000) >> 16)
#define   OHCI1394_PhyControl_Write(addr, data)	(((addr) << 8) | (data) | 0x00004000)
#define   OHCI1394_PhyControl_WriteDone		0x00004000
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

#define OHCI1394_phy_tcode		0xe

#endif /* _FIREWIRE_OHCI_H */
