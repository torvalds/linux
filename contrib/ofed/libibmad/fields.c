/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
 * Copyright (c) 2009-2011 Mellanox Technologies LTD.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <infiniband/mad.h>

/*
 * BITSOFFS and BE_OFFS are required due the fact that the bit offsets are inconsistently
 * encoded in the IB spec - IB headers are encoded such that the bit offsets
 * are in big endian convention (BE_OFFS), while the SMI/GSI queries data fields bit
 * offsets are specified using real bit offset (?!)
 * The following macros normalize everything to big endian offsets.
 */
#define BITSOFFS(o, w)	(((o) & ~31) | ((32 - ((o) & 31) - (w)))), (w)
#define BE_OFFS(o, w)	(o), (w)
#define BE_TO_BITSOFFS(o, w)	(((o) & ~31) | ((32 - ((o) & 31) - (w))))

static const ib_field_t ib_mad_f[] = {
	{0, 0},			/* IB_NO_FIELD - reserved as invalid */

	{0, 64, "GidPrefix", mad_dump_rhex},
	{64, 64, "GidGuid", mad_dump_rhex},

	/*
	 * MAD: common MAD fields (IB spec 13.4.2)
	 * SMP: Subnet Management packets - lid routed (IB spec 14.2.1.1)
	 * DSMP: Subnet Management packets - direct route (IB spec 14.2.1.2)
	 * SA: Subnet Administration packets (IB spec 15.2.1.1)
	 */

	/* first MAD word (0-3 bytes) */
	{BE_OFFS(0, 7), "MadMethod", mad_dump_hex},	/* TODO: add dumper */
	{BE_OFFS(7, 1), "MadIsResponse", mad_dump_uint},	/* TODO: add dumper */
	{BE_OFFS(8, 8), "MadClassVersion", mad_dump_uint},
	{BE_OFFS(16, 8), "MadMgmtClass", mad_dump_uint},	/* TODO: add dumper */
	{BE_OFFS(24, 8), "MadBaseVersion", mad_dump_uint},

	/* second MAD word (4-7 bytes) */
	{BE_OFFS(48, 16), "MadStatus", mad_dump_hex},	/* TODO: add dumper */

	/* DR SMP only */
	{BE_OFFS(32, 8), "DrSmpHopCnt", mad_dump_uint},
	{BE_OFFS(40, 8), "DrSmpHopPtr", mad_dump_uint},
	{BE_OFFS(48, 15), "DrSmpStatus", mad_dump_hex},	/* TODO: add dumper */
	{BE_OFFS(63, 1), "DrSmpDirection", mad_dump_uint},	/* TODO: add dumper */

	/* words 3,4,5,6 (8-23 bytes) */
	{64, 64, "MadTRID", mad_dump_hex},
	{BE_OFFS(144, 16), "MadAttr", mad_dump_hex},	/* TODO: add dumper */
	{160, 32, "MadModifier", mad_dump_hex},	/* TODO: add dumper */

	/* word 7,8 (24-31 bytes) */
	{192, 64, "MadMkey", mad_dump_hex},

	/* word 9 (32-37 bytes) */
	{BE_OFFS(256, 16), "DrSmpDLID", mad_dump_uint},
	{BE_OFFS(272, 16), "DrSmpSLID", mad_dump_uint},

	/* word 10,11 (36-43 bytes) */
	{288, 64, "SaSMkey", mad_dump_hex},

	/* word 12 (44-47 bytes) */
	{BE_OFFS(46 * 8, 16), "SaAttrOffs", mad_dump_uint},

	/* word 13,14 (48-55 bytes) */
	{48 * 8, 64, "SaCompMask", mad_dump_hex},

	/* word 13,14 (56-255 bytes) */
	{56 * 8, (256 - 56) * 8, "SaData", mad_dump_hex},

	/* bytes 64 - 127 */
	{0, 0},			/* IB_SM_DATA_F - reserved as invalid */

	/* bytes 64 - 256 */
	{64 * 8, (256 - 64) * 8, "GsData", mad_dump_hex},

	/* bytes 128 - 191 */
	{1024, 512, "DrSmpPath", mad_dump_hex},

	/* bytes 192 - 255 */
	{1536, 512, "DrSmpRetPath", mad_dump_hex},

	/*
	 * PortInfo fields
	 */
	{0, 64, "Mkey", mad_dump_hex},
	{64, 64, "GidPrefix", mad_dump_hex},
	{BITSOFFS(128, 16), "Lid", mad_dump_uint},
	{BITSOFFS(144, 16), "SMLid", mad_dump_uint},
	{160, 32, "CapMask", mad_dump_portcapmask},
	{BITSOFFS(192, 16), "DiagCode", mad_dump_hex},
	{BITSOFFS(208, 16), "MkeyLeasePeriod", mad_dump_uint},
	{BITSOFFS(224, 8), "LocalPort", mad_dump_uint},
	{BITSOFFS(232, 8), "LinkWidthEnabled", mad_dump_linkwidthen},
	{BITSOFFS(240, 8), "LinkWidthSupported", mad_dump_linkwidthsup},
	{BITSOFFS(248, 8), "LinkWidthActive", mad_dump_linkwidth},
	{BITSOFFS(256, 4), "LinkSpeedSupported", mad_dump_linkspeedsup},
	{BITSOFFS(260, 4), "LinkState", mad_dump_portstate},
	{BITSOFFS(264, 4), "PhysLinkState", mad_dump_physportstate},
	{BITSOFFS(268, 4), "LinkDownDefState", mad_dump_linkdowndefstate},
	{BITSOFFS(272, 2), "ProtectBits", mad_dump_uint},
	{BITSOFFS(277, 3), "LMC", mad_dump_uint},
	{BITSOFFS(280, 4), "LinkSpeedActive", mad_dump_linkspeed},
	{BITSOFFS(284, 4), "LinkSpeedEnabled", mad_dump_linkspeeden},
	{BITSOFFS(288, 4), "NeighborMTU", mad_dump_mtu},
	{BITSOFFS(292, 4), "SMSL", mad_dump_uint},
	{BITSOFFS(296, 4), "VLCap", mad_dump_vlcap},
	{BITSOFFS(300, 4), "InitType", mad_dump_hex},
	{BITSOFFS(304, 8), "VLHighLimit", mad_dump_uint},
	{BITSOFFS(312, 8), "VLArbHighCap", mad_dump_uint},
	{BITSOFFS(320, 8), "VLArbLowCap", mad_dump_uint},
	{BITSOFFS(328, 4), "InitReply", mad_dump_hex},
	{BITSOFFS(332, 4), "MtuCap", mad_dump_mtu},
	{BITSOFFS(336, 3), "VLStallCount", mad_dump_uint},
	{BITSOFFS(339, 5), "HoqLife", mad_dump_uint},
	{BITSOFFS(344, 4), "OperVLs", mad_dump_opervls},
	{BITSOFFS(348, 1), "PartEnforceInb", mad_dump_uint},
	{BITSOFFS(349, 1), "PartEnforceOutb", mad_dump_uint},
	{BITSOFFS(350, 1), "FilterRawInb", mad_dump_uint},
	{BITSOFFS(351, 1), "FilterRawOutb", mad_dump_uint},
	{BITSOFFS(352, 16), "MkeyViolations", mad_dump_uint},
	{BITSOFFS(368, 16), "PkeyViolations", mad_dump_uint},
	{BITSOFFS(384, 16), "QkeyViolations", mad_dump_uint},
	{BITSOFFS(400, 8), "GuidCap", mad_dump_uint},
	{BITSOFFS(408, 1), "ClientReregister", mad_dump_uint},
	{BITSOFFS(409, 1), "McastPkeyTrapSuppressionEnabled", mad_dump_uint},
	{BITSOFFS(411, 5), "SubnetTimeout", mad_dump_uint},
	{BITSOFFS(419, 5), "RespTimeVal", mad_dump_uint},
	{BITSOFFS(424, 4), "LocalPhysErr", mad_dump_uint},
	{BITSOFFS(428, 4), "OverrunErr", mad_dump_uint},
	{BITSOFFS(432, 16), "MaxCreditHint", mad_dump_uint},
	{BITSOFFS(456, 24), "RoundTrip", mad_dump_uint},
	{0, 0},			/* IB_PORT_LAST_F */

	/*
	 * NodeInfo fields
	 */
	{BITSOFFS(0, 8), "BaseVers", mad_dump_uint},
	{BITSOFFS(8, 8), "ClassVers", mad_dump_uint},
	{BITSOFFS(16, 8), "NodeType", mad_dump_node_type},
	{BITSOFFS(24, 8), "NumPorts", mad_dump_uint},
	{32, 64, "SystemGuid", mad_dump_hex},
	{96, 64, "Guid", mad_dump_hex},
	{160, 64, "PortGuid", mad_dump_hex},
	{BITSOFFS(224, 16), "PartCap", mad_dump_uint},
	{BITSOFFS(240, 16), "DevId", mad_dump_hex},
	{256, 32, "Revision", mad_dump_hex},
	{BITSOFFS(288, 8), "LocalPort", mad_dump_uint},
	{BITSOFFS(296, 24), "VendorId", mad_dump_hex},
	{0, 0},			/* IB_NODE_LAST_F */

	/*
	 * SwitchInfo fields
	 */
	{BITSOFFS(0, 16), "LinearFdbCap", mad_dump_uint},
	{BITSOFFS(16, 16), "RandomFdbCap", mad_dump_uint},
	{BITSOFFS(32, 16), "McastFdbCap", mad_dump_uint},
	{BITSOFFS(48, 16), "LinearFdbTop", mad_dump_uint},
	{BITSOFFS(64, 8), "DefPort", mad_dump_uint},
	{BITSOFFS(72, 8), "DefMcastPrimPort", mad_dump_uint},
	{BITSOFFS(80, 8), "DefMcastNotPrimPort", mad_dump_uint},
	{BITSOFFS(88, 5), "LifeTime", mad_dump_uint},
	{BITSOFFS(93, 1), "StateChange", mad_dump_uint},
	{BITSOFFS(94, 2), "OptSLtoVLMapping", mad_dump_uint},
	{BITSOFFS(96, 16), "LidsPerPort", mad_dump_uint},
	{BITSOFFS(112, 16), "PartEnforceCap", mad_dump_uint},
	{BITSOFFS(128, 1), "InboundPartEnf", mad_dump_uint},
	{BITSOFFS(129, 1), "OutboundPartEnf", mad_dump_uint},
	{BITSOFFS(130, 1), "FilterRawInbound", mad_dump_uint},
	{BITSOFFS(131, 1), "FilterRawOutbound", mad_dump_uint},
	{BITSOFFS(132, 1), "EnhancedPort0", mad_dump_uint},
	{BITSOFFS(144, 16), "MulticastFDBTop", mad_dump_hex},
	{0, 0},			/* IB_SW_LAST_F */

	/*
	 * SwitchLinearForwardingTable fields
	 */
	{0, 512, "LinearForwTbl", mad_dump_array},

	/*
	 * SwitchMulticastForwardingTable fields
	 */
	{0, 512, "MulticastForwTbl", mad_dump_array},

	/*
	 * NodeDescription fields
	 */
	{0, 64 * 8, "NodeDesc", mad_dump_string},

	/*
	 * Notice/Trap fields
	 */
	{BITSOFFS(0, 1), "NoticeIsGeneric", mad_dump_uint},
	{BITSOFFS(1, 7), "NoticeType", mad_dump_uint},
	{BITSOFFS(8, 24), "NoticeProducerType", mad_dump_node_type},
	{BITSOFFS(32, 16), "NoticeTrapNumber", mad_dump_uint},
	{BITSOFFS(48, 16), "NoticeIssuerLID", mad_dump_uint},
	{BITSOFFS(64, 1), "NoticeToggle", mad_dump_uint},
	{BITSOFFS(65, 15), "NoticeCount", mad_dump_uint},
	{80, 432, "NoticeDataDetails", mad_dump_array},
	{BITSOFFS(80, 16), "NoticeDataLID", mad_dump_uint},
	{BITSOFFS(96, 16), "NoticeDataTrap144LID", mad_dump_uint},
	{BITSOFFS(128, 32), "NoticeDataTrap144CapMask", mad_dump_uint},

	/*
	 * Port counters
	 */
	{BITSOFFS(8, 8), "PortSelect", mad_dump_uint},
	{BITSOFFS(16, 16), "CounterSelect", mad_dump_hex},
	{BITSOFFS(32, 16), "SymbolErrorCounter", mad_dump_uint},
	{BITSOFFS(48, 8), "LinkErrorRecoveryCounter", mad_dump_uint},
	{BITSOFFS(56, 8), "LinkDownedCounter", mad_dump_uint},
	{BITSOFFS(64, 16), "PortRcvErrors", mad_dump_uint},
	{BITSOFFS(80, 16), "PortRcvRemotePhysicalErrors", mad_dump_uint},
	{BITSOFFS(96, 16), "PortRcvSwitchRelayErrors", mad_dump_uint},
	{BITSOFFS(112, 16), "PortXmitDiscards", mad_dump_uint},
	{BITSOFFS(128, 8), "PortXmitConstraintErrors", mad_dump_uint},
	{BITSOFFS(136, 8), "PortRcvConstraintErrors", mad_dump_uint},
	{BITSOFFS(144, 8), "CounterSelect2", mad_dump_hex},
	{BITSOFFS(152, 4), "LocalLinkIntegrityErrors", mad_dump_uint},
	{BITSOFFS(156, 4), "ExcessiveBufferOverrunErrors", mad_dump_uint},
	{BITSOFFS(176, 16), "VL15Dropped", mad_dump_uint},
	{192, 32, "PortXmitData", mad_dump_uint},
	{224, 32, "PortRcvData", mad_dump_uint},
	{256, 32, "PortXmitPkts", mad_dump_uint},
	{288, 32, "PortRcvPkts", mad_dump_uint},
	{320, 32, "PortXmitWait", mad_dump_uint},
	{0, 0},			/* IB_PC_LAST_F */

	/*
	 * SMInfo
	 */
	{0, 64, "SmInfoGuid", mad_dump_hex},
	{64, 64, "SmInfoKey", mad_dump_hex},
	{128, 32, "SmActivity", mad_dump_uint},
	{BITSOFFS(160, 4), "SmPriority", mad_dump_uint},
	{BITSOFFS(164, 4), "SmState", mad_dump_uint},

	/*
	 * SA RMPP
	 */
	{BE_OFFS(24 * 8 + 24, 8), "RmppVers", mad_dump_uint},
	{BE_OFFS(24 * 8 + 16, 8), "RmppType", mad_dump_uint},
	{BE_OFFS(24 * 8 + 11, 5), "RmppResp", mad_dump_uint},
	{BE_OFFS(24 * 8 + 8, 3), "RmppFlags", mad_dump_hex},
	{BE_OFFS(24 * 8 + 0, 8), "RmppStatus", mad_dump_hex},

	/* data1 */
	{28 * 8, 32, "RmppData1", mad_dump_hex},
	{28 * 8, 32, "RmppSegNum", mad_dump_uint},
	/* data2 */
	{32 * 8, 32, "RmppData2", mad_dump_hex},
	{32 * 8, 32, "RmppPayload", mad_dump_uint},
	{32 * 8, 32, "RmppNewWin", mad_dump_uint},

	/*
	 * SA Get Multi Path
	 */
	{BITSOFFS(41, 7), "MultiPathNumPath", mad_dump_uint},
	{BITSOFFS(120, 8), "MultiPathNumSrc", mad_dump_uint},
	{BITSOFFS(128, 8), "MultiPathNumDest", mad_dump_uint},
	{192, 128, "MultiPathGid", mad_dump_array},

	/*
	 * SA Path rec
	 */
	{64, 128, "PathRecDGid", mad_dump_array},
	{192, 128, "PathRecSGid", mad_dump_array},
	{BITSOFFS(320, 16), "PathRecDLid", mad_dump_uint},
	{BITSOFFS(336, 16), "PathRecSLid", mad_dump_uint},
	{BITSOFFS(393, 7), "PathRecNumPath", mad_dump_uint},
	{BITSOFFS(428, 4), "PathRecSL", mad_dump_uint},

	/*
	 * MC Member rec
	 */
	{0, 128, "McastMemMGid", mad_dump_array},
	{128, 128, "McastMemPortGid", mad_dump_array},
	{256, 32, "McastMemQkey", mad_dump_hex},
	{BITSOFFS(288, 16), "McastMemMLid", mad_dump_hex},
	{BITSOFFS(352, 4), "McastMemSL", mad_dump_uint},
	{BITSOFFS(306, 6), "McastMemMTU", mad_dump_uint},
	{BITSOFFS(338, 6), "McastMemRate", mad_dump_uint},
	{BITSOFFS(312, 8), "McastMemTClass", mad_dump_uint},
	{BITSOFFS(320, 16), "McastMemPkey", mad_dump_uint},
	{BITSOFFS(356, 20), "McastMemFlowLbl", mad_dump_uint},
	{BITSOFFS(388, 4), "McastMemJoinState", mad_dump_uint},
	{BITSOFFS(392, 1), "McastMemProxyJoin", mad_dump_uint},

	/*
	 * Service record
	 */
	{0, 64, "ServRecID", mad_dump_hex},
	{64, 128, "ServRecGid", mad_dump_array},
	{BITSOFFS(192, 16), "ServRecPkey", mad_dump_hex},
	{224, 32, "ServRecLease", mad_dump_hex},
	{256, 128, "ServRecKey", mad_dump_hex},
	{384, 512, "ServRecName", mad_dump_string},
	{896, 512, "ServRecData", mad_dump_array},	/* ATS for example */

	/*
	 * ATS SM record - within SA_SR_DATA
	 */
	{12 * 8, 32, "ATSNodeAddr", mad_dump_hex},
	{BITSOFFS(16 * 8, 16), "ATSMagicKey", mad_dump_hex},
	{BITSOFFS(18 * 8, 16), "ATSNodeType", mad_dump_hex},
	{32 * 8, 32 * 8, "ATSNodeName", mad_dump_string},

	/*
	 * SLTOVL MAPPING TABLE
	 */
	{0, 64, "SLToVLMap", mad_dump_hex},

	/*
	 * VL ARBITRATION TABLE
	 */
	{0, 512, "VLArbTbl", mad_dump_array},

	/*
	 * IB vendor classes range 2
	 */
	{BE_OFFS(36 * 8, 24), "OUI", mad_dump_array},
	{40 * 8, (256 - 40) * 8, "Vendor2Data", mad_dump_array},

	/*
	 * Extended port counters
	 */
	{BITSOFFS(8, 8), "PortSelect", mad_dump_uint},
	{BITSOFFS(16, 16), "CounterSelect", mad_dump_hex},
	{64, 64, "PortXmitData", mad_dump_uint},
	{128, 64, "PortRcvData", mad_dump_uint},
	{192, 64, "PortXmitPkts", mad_dump_uint},
	{256, 64, "PortRcvPkts", mad_dump_uint},
	{320, 64, "PortUnicastXmitPkts", mad_dump_uint},
	{384, 64, "PortUnicastRcvPkts", mad_dump_uint},
	{448, 64, "PortMulticastXmitPkts", mad_dump_uint},
	{512, 64, "PortMulticastRcvPkts", mad_dump_uint},
	{0, 0},			/* IB_PC_EXT_LAST_F */

	/*
	 * GUIDInfo fields
	 */
	{0, 64, "GUID0", mad_dump_hex},

	/*
	 * ClassPortInfo fields
	 */
	{BITSOFFS(0, 8), "BaseVersion", mad_dump_uint},
	{BITSOFFS(8, 8), "ClassVersion", mad_dump_uint},
	{BITSOFFS(16, 16), "CapabilityMask", mad_dump_hex},
	{BITSOFFS(32, 27), "CapabilityMask2", mad_dump_hex},
	{BITSOFFS(59, 5), "RespTimeVal", mad_dump_uint},
	{64, 128, "RedirectGID", mad_dump_array},
	{BITSOFFS(192, 8), "RedirectTC", mad_dump_hex},
	{BITSOFFS(200, 4), "RedirectSL", mad_dump_uint},
	{BITSOFFS(204, 20), "RedirectFL", mad_dump_hex},
	{BITSOFFS(224, 16), "RedirectLID", mad_dump_uint},
	{BITSOFFS(240, 16), "RedirectPKey", mad_dump_hex},
	{BITSOFFS(264, 24), "RedirectQP", mad_dump_hex},
	{288, 32, "RedirectQKey", mad_dump_hex},
	{320, 128, "TrapGID", mad_dump_array},
	{BITSOFFS(448, 8), "TrapTC", mad_dump_hex},
	{BITSOFFS(456, 4), "TrapSL", mad_dump_uint},
	{BITSOFFS(460, 20), "TrapFL", mad_dump_hex},
	{BITSOFFS(480, 16), "TrapLID", mad_dump_uint},
	{BITSOFFS(496, 16), "TrapPKey", mad_dump_hex},
	{BITSOFFS(512, 8), "TrapHL", mad_dump_uint},
	{BITSOFFS(520, 24), "TrapQP", mad_dump_hex},
	{544, 32, "TrapQKey", mad_dump_hex},

	/*
	 * PortXmitDataSL fields
	 */
	{32, 32, "XmtDataSL0", mad_dump_uint},
	{64, 32, "XmtDataSL1", mad_dump_uint},
	{96, 32, "XmtDataSL2", mad_dump_uint},
	{128, 32, "XmtDataSL3", mad_dump_uint},
	{160, 32, "XmtDataSL4", mad_dump_uint},
	{192, 32, "XmtDataSL5", mad_dump_uint},
	{224, 32, "XmtDataSL6", mad_dump_uint},
	{256, 32, "XmtDataSL7", mad_dump_uint},
	{288, 32, "XmtDataSL8", mad_dump_uint},
	{320, 32, "XmtDataSL9", mad_dump_uint},
	{352, 32, "XmtDataSL10", mad_dump_uint},
	{384, 32, "XmtDataSL11", mad_dump_uint},
	{416, 32, "XmtDataSL12", mad_dump_uint},
	{448, 32, "XmtDataSL13", mad_dump_uint},
	{480, 32, "XmtDataSL14", mad_dump_uint},
	{512, 32, "XmtDataSL15", mad_dump_uint},
	{0, 0},			/* IB_PC_XMT_DATA_SL_LAST_F */

	/*
	 * PortRcvDataSL fields
	 */
	{32, 32, "RcvDataSL0", mad_dump_uint},
	{64, 32, "RcvDataSL1", mad_dump_uint},
	{96, 32, "RcvDataSL2", mad_dump_uint},
	{128, 32, "RcvDataSL3", mad_dump_uint},
	{160, 32, "RcvDataSL4", mad_dump_uint},
	{192, 32, "RcvDataSL5", mad_dump_uint},
	{224, 32, "RcvDataSL6", mad_dump_uint},
	{256, 32, "RcvDataSL7", mad_dump_uint},
	{288, 32, "RcvDataSL8", mad_dump_uint},
	{320, 32, "RcvDataSL9", mad_dump_uint},
	{352, 32, "RcvDataSL10", mad_dump_uint},
	{384, 32, "RcvDataSL11", mad_dump_uint},
	{416, 32, "RcvDataSL12", mad_dump_uint},
	{448, 32, "RcvDataSL13", mad_dump_uint},
	{480, 32, "RcvDataSL14", mad_dump_uint},
	{512, 32, "RcvDataSL15", mad_dump_uint},
	{0, 0},			/* IB_PC_RCV_DATA_SL_LAST_F */

	/*
	 * PortXmitDiscardDetails fields
	 */
	{BITSOFFS(32, 16), "PortInactiveDiscards", mad_dump_uint},
	{BITSOFFS(48, 16), "PortNeighborMTUDiscards", mad_dump_uint},
	{BITSOFFS(64, 16), "PortSwLifetimeLimitDiscards", mad_dump_uint},
	{BITSOFFS(80, 16), "PortSwHOQLifetimeLimitDiscards", mad_dump_uint},
	{0, 0},			/* IB_PC_XMT_DISC_LAST_F */

	/*
	 * PortRcvErrorDetails fields
	 */
	{BITSOFFS(32, 16), "PortLocalPhysicalErrors", mad_dump_uint},
	{BITSOFFS(48, 16), "PortMalformedPktErrors", mad_dump_uint},
	{BITSOFFS(64, 16), "PortBufferOverrunErrors", mad_dump_uint},
	{BITSOFFS(80, 16), "PortDLIDMappingErrors", mad_dump_uint},
	{BITSOFFS(96, 16), "PortVLMappingErrors", mad_dump_uint},
	{BITSOFFS(112, 16), "PortLoopingErrors", mad_dump_uint},
	{0, 0},                 /* IB_PC_RCV_ERR_LAST_F */

	/*
	 * PortSamplesControl fields
	 */
	{BITSOFFS(0, 8), "OpCode", mad_dump_hex},
	{BITSOFFS(8, 8), "PortSelect", mad_dump_uint},
	{BITSOFFS(16, 8), "Tick", mad_dump_hex},
	{BITSOFFS(29, 3), "CounterWidth", mad_dump_uint},
	{BITSOFFS(34, 3), "CounterMask0", mad_dump_hex},
	{BITSOFFS(37, 27), "CounterMasks1to9", mad_dump_hex},
	{BITSOFFS(65, 15), "CounterMasks10to14", mad_dump_hex},
	{BITSOFFS(80, 8), "SampleMechanisms", mad_dump_uint},
	{BITSOFFS(94, 2), "SampleStatus", mad_dump_uint},
	{96, 64, "OptionMask", mad_dump_hex},
	{160, 64, "VendorMask", mad_dump_hex},
	{224, 32, "SampleStart", mad_dump_uint},
	{256, 32, "SampleInterval", mad_dump_uint},
	{BITSOFFS(288, 16), "Tag", mad_dump_hex},
	{BITSOFFS(304, 16), "CounterSelect0", mad_dump_hex},
	{BITSOFFS(320, 16), "CounterSelect1", mad_dump_hex},
	{BITSOFFS(336, 16), "CounterSelect2", mad_dump_hex},
	{BITSOFFS(352, 16), "CounterSelect3", mad_dump_hex},
	{BITSOFFS(368, 16), "CounterSelect4", mad_dump_hex},
	{BITSOFFS(384, 16), "CounterSelect5", mad_dump_hex},
	{BITSOFFS(400, 16), "CounterSelect6", mad_dump_hex},
	{BITSOFFS(416, 16), "CounterSelect7", mad_dump_hex},
	{BITSOFFS(432, 16), "CounterSelect8", mad_dump_hex},
	{BITSOFFS(448, 16), "CounterSelect9", mad_dump_hex},
	{BITSOFFS(464, 16), "CounterSelect10", mad_dump_hex},
	{BITSOFFS(480, 16), "CounterSelect11", mad_dump_hex},
	{BITSOFFS(496, 16), "CounterSelect12", mad_dump_hex},
	{BITSOFFS(512, 16), "CounterSelect13", mad_dump_hex},
	{BITSOFFS(528, 16), "CounterSelect14", mad_dump_hex},
	{576, 64, "SamplesOnlyOptionMask", mad_dump_hex},
	{0, 0},			/* IB_PSC_LAST_F */

	/* GUIDInfo fields */
	{0, 64, "GUID0", mad_dump_hex},
	{64, 64, "GUID1", mad_dump_hex},
	{128, 64, "GUID2", mad_dump_hex},
	{192, 64, "GUID3", mad_dump_hex},
	{256, 64, "GUID4", mad_dump_hex},
	{320, 64, "GUID5", mad_dump_hex},
	{384, 64, "GUID6", mad_dump_hex},
	{448, 64, "GUID7", mad_dump_hex},

	/* GUID Info Record */
	{BITSOFFS(0, 16), "Lid", mad_dump_uint},
	{BITSOFFS(16, 8), "BlockNum", mad_dump_uint},
	{64, 64, "Guid0", mad_dump_hex},
	{128, 64, "Guid1", mad_dump_hex},
	{192, 64, "Guid2", mad_dump_hex},
	{256, 64, "Guid3", mad_dump_hex},
	{320, 64, "Guid4", mad_dump_hex},
	{384, 64, "Guid5", mad_dump_hex},
	{448, 64, "Guid6", mad_dump_hex},
	{512, 64, "Guid7", mad_dump_hex},

	/*
	 * More PortInfo fields
	 */
	{BITSOFFS(480, 16), "CapabilityMask2", mad_dump_portcapmask2},
	{BITSOFFS(496, 4), "LinkSpeedExtActive", mad_dump_linkspeedext},
	{BITSOFFS(500, 4), "LinkSpeedExtSupported", mad_dump_linkspeedextsup},
	{BITSOFFS(507, 5), "LinkSpeedExtEnabled", mad_dump_linkspeedexten},
	{0, 0},			/* IB_PORT_LINK_SPEED_EXT_LAST_F */

	/*
	 * PortExtendedSpeedsCounters fields
	 */
	{BITSOFFS(8, 8), "PortSelect", mad_dump_uint},
	{64, 64, "CounterSelect", mad_dump_hex},
	{BITSOFFS(128, 16), "SyncHeaderErrorCounter", mad_dump_uint},
	{BITSOFFS(144, 16), "UnknownBlockCounter", mad_dump_uint},
	{BITSOFFS(160, 16), "ErrorDetectionCounterLane0", mad_dump_uint},
	{BITSOFFS(176, 16), "ErrorDetectionCounterLane1", mad_dump_uint},
	{BITSOFFS(192, 16), "ErrorDetectionCounterLane2", mad_dump_uint},
	{BITSOFFS(208, 16), "ErrorDetectionCounterLane3", mad_dump_uint},
	{BITSOFFS(224, 16), "ErrorDetectionCounterLane4", mad_dump_uint},
	{BITSOFFS(240, 16), "ErrorDetectionCounterLane5", mad_dump_uint},
	{BITSOFFS(256, 16), "ErrorDetectionCounterLane6", mad_dump_uint},
	{BITSOFFS(272, 16), "ErrorDetectionCounterLane7", mad_dump_uint},
	{BITSOFFS(288, 16), "ErrorDetectionCounterLane8", mad_dump_uint},
	{BITSOFFS(304, 16), "ErrorDetectionCounterLane9", mad_dump_uint},
	{BITSOFFS(320, 16), "ErrorDetectionCounterLane10", mad_dump_uint},
	{BITSOFFS(336, 16), "ErrorDetectionCounterLane11", mad_dump_uint},
	{352, 32, "FECCorrectableBlockCtrLane0", mad_dump_uint},
	{384, 32, "FECCorrectableBlockCtrLane1", mad_dump_uint},
	{416, 32, "FECCorrectableBlockCtrLane2", mad_dump_uint},
	{448, 32, "FECCorrectableBlockCtrLane3", mad_dump_uint},
	{480, 32, "FECCorrectableBlockCtrLane4", mad_dump_uint},
	{512, 32, "FECCorrectableBlockCtrLane5", mad_dump_uint},
	{544, 32, "FECCorrectableBlockCtrLane6", mad_dump_uint},
	{576, 32, "FECCorrectableBlockCtrLane7", mad_dump_uint},
	{608, 32, "FECCorrectableBlockCtrLane8", mad_dump_uint},
	{640, 32, "FECCorrectableBlockCtrLane9", mad_dump_uint},
	{672, 32, "FECCorrectableBlockCtrLane10", mad_dump_uint},
	{704, 32, "FECCorrectableBlockCtrLane11", mad_dump_uint},
	{736, 32, "FECUncorrectableBlockCtrLane0", mad_dump_uint},
	{768, 32, "FECUncorrectableBlockCtrLane1", mad_dump_uint},
	{800, 32, "FECUncorrectableBlockCtrLane2", mad_dump_uint},
	{832, 32, "FECUncorrectableBlockCtrLane3", mad_dump_uint},
	{864, 32, "FECUncorrectableBlockCtrLane4", mad_dump_uint},
	{896, 32, "FECUncorrectableBlockCtrLane5", mad_dump_uint},
	{928, 32, "FECUncorrectableBlockCtrLane6", mad_dump_uint},
	{960, 32, "FECUncorrectableBlockCtrLane7", mad_dump_uint},
	{992, 32, "FECUncorrectableBlockCtrLane8", mad_dump_uint},
	{1024, 32, "FECUncorrectableBlockCtrLane9", mad_dump_uint},
	{1056, 32, "FECUncorrectableBlockCtrLane10", mad_dump_uint},
	{1088, 32, "FECUncorrectableBlockCtrLane11", mad_dump_uint},
	{0, 0},			/* IB_PESC_LAST_F */



	/*
	 * PortOpRcvCounters fields
	 */
	{32, 32, "PortOpRcvPkts", mad_dump_uint},
	{64, 32, "PortOpRcvData", mad_dump_uint},
	{0, 0},			/* IB_PC_PORT_OP_RCV_COUNTERS_LAST_F */

	/*
	 * PortFlowCtlCounters fields
	 */
	{32, 32, "PortXmitFlowPkts", mad_dump_uint},
	{64, 32, "PortRcvFlowPkts", mad_dump_uint},
	{0, 0},			/* IB_PC_PORT_FLOW_CTL_COUNTERS_LAST_F */

	/*
	 * PortVLOpPackets fields
	 */
	{BITSOFFS(32, 16), "PortVLOpPackets0", mad_dump_uint},
	{BITSOFFS(48, 16), "PortVLOpPackets1", mad_dump_uint},
	{BITSOFFS(64, 16), "PortVLOpPackets2", mad_dump_uint},
	{BITSOFFS(80, 16), "PortVLOpPackets3", mad_dump_uint},
	{BITSOFFS(96, 16), "PortVLOpPackets4", mad_dump_uint},
	{BITSOFFS(112, 16), "PortVLOpPackets5", mad_dump_uint},
	{BITSOFFS(128, 16), "PortVLOpPackets6", mad_dump_uint},
	{BITSOFFS(144, 16), "PortVLOpPackets7", mad_dump_uint},
	{BITSOFFS(160, 16), "PortVLOpPackets8", mad_dump_uint},
	{BITSOFFS(176, 16), "PortVLOpPackets9", mad_dump_uint},
	{BITSOFFS(192, 16), "PortVLOpPackets10", mad_dump_uint},
	{BITSOFFS(208, 16), "PortVLOpPackets11", mad_dump_uint},
	{BITSOFFS(224, 16), "PortVLOpPackets12", mad_dump_uint},
	{BITSOFFS(240, 16), "PortVLOpPackets13", mad_dump_uint},
	{BITSOFFS(256, 16), "PortVLOpPackets14", mad_dump_uint},
	{BITSOFFS(272, 16), "PortVLOpPackets15", mad_dump_uint},
	{0, 0},			/* IB_PC_PORT_VL_OP_PACKETS_LAST_F */

	/*
	 * PortVLOpData fields
	 */
	{32, 32, "PortVLOpData0", mad_dump_uint},
	{64, 32, "PortVLOpData1", mad_dump_uint},
	{96, 32, "PortVLOpData2", mad_dump_uint},
	{128, 32, "PortVLOpData3", mad_dump_uint},
	{160, 32, "PortVLOpData4", mad_dump_uint},
	{192, 32, "PortVLOpData5", mad_dump_uint},
	{224, 32, "PortVLOpData6", mad_dump_uint},
	{256, 32, "PortVLOpData7", mad_dump_uint},
	{288, 32, "PortVLOpData8", mad_dump_uint},
	{320, 32, "PortVLOpData9", mad_dump_uint},
	{352, 32, "PortVLOpData10", mad_dump_uint},
	{384, 32, "PortVLOpData11", mad_dump_uint},
	{416, 32, "PortVLOpData12", mad_dump_uint},
	{448, 32, "PortVLOpData13", mad_dump_uint},
	{480, 32, "PortVLOpData14", mad_dump_uint},
	{512, 32, "PortVLOpData15", mad_dump_uint},
	{0, 0},			/* IB_PC_PORT_VL_OP_DATA_LAST_F */

	/*
	 * PortVLXmitFlowCtlUpdateErrors fields
	 */
	{BITSOFFS(32, 2), "PortVLXmitFlowCtlUpdateErrors0", mad_dump_uint},
	{BITSOFFS(34, 2), "PortVLXmitFlowCtlUpdateErrors1", mad_dump_uint},
	{BITSOFFS(36, 2), "PortVLXmitFlowCtlUpdateErrors2", mad_dump_uint},
	{BITSOFFS(38, 2), "PortVLXmitFlowCtlUpdateErrors3", mad_dump_uint},
	{BITSOFFS(40, 2), "PortVLXmitFlowCtlUpdateErrors4", mad_dump_uint},
	{BITSOFFS(42, 2), "PortVLXmitFlowCtlUpdateErrors5", mad_dump_uint},
	{BITSOFFS(44, 2), "PortVLXmitFlowCtlUpdateErrors6", mad_dump_uint},
	{BITSOFFS(46, 2), "PortVLXmitFlowCtlUpdateErrors7", mad_dump_uint},
	{BITSOFFS(48, 2), "PortVLXmitFlowCtlUpdateErrors8", mad_dump_uint},
	{BITSOFFS(50, 2), "PortVLXmitFlowCtlUpdateErrors9", mad_dump_uint},
	{BITSOFFS(52, 2), "PortVLXmitFlowCtlUpdateErrors10", mad_dump_uint},
	{BITSOFFS(54, 2), "PortVLXmitFlowCtlUpdateErrors11", mad_dump_uint},
	{BITSOFFS(56, 2), "PortVLXmitFlowCtlUpdateErrors12", mad_dump_uint},
	{BITSOFFS(58, 2), "PortVLXmitFlowCtlUpdateErrors13", mad_dump_uint},
	{BITSOFFS(60, 2), "PortVLXmitFlowCtlUpdateErrors14", mad_dump_uint},
	{BITSOFFS(62, 2), "PortVLXmitFlowCtlUpdateErrors15", mad_dump_uint},
	{0, 0},			/* IB_PC_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS_LAST_F */

	/*
	 * PortVLXmitWaitCounters fields
	 */
	{BITSOFFS(32, 16), "PortVLXmitWait0", mad_dump_uint},
	{BITSOFFS(48, 16), "PortVLXmitWait1", mad_dump_uint},
	{BITSOFFS(64, 16), "PortVLXmitWait2", mad_dump_uint},
	{BITSOFFS(80, 16), "PortVLXmitWait3", mad_dump_uint},
	{BITSOFFS(96, 16), "PortVLXmitWait4", mad_dump_uint},
	{BITSOFFS(112, 16), "PortVLXmitWait5", mad_dump_uint},
	{BITSOFFS(128, 16), "PortVLXmitWait6", mad_dump_uint},
	{BITSOFFS(144, 16), "PortVLXmitWait7", mad_dump_uint},
	{BITSOFFS(160, 16), "PortVLXmitWait8", mad_dump_uint},
	{BITSOFFS(176, 16), "PortVLXmitWait9", mad_dump_uint},
	{BITSOFFS(192, 16), "PortVLXmitWait10", mad_dump_uint},
	{BITSOFFS(208, 16), "PortVLXmitWait11", mad_dump_uint},
	{BITSOFFS(224, 16), "PortVLXmitWait12", mad_dump_uint},
	{BITSOFFS(240, 16), "PortVLXmitWait13", mad_dump_uint},
	{BITSOFFS(256, 16), "PortVLXmitWait14", mad_dump_uint},
	{BITSOFFS(272, 16), "PortVLXmitWait15", mad_dump_uint},
	{0, 0},			/* IB_PC_PORT_VL_XMIT_WAIT_COUNTERS_LAST_F */

	/*
	 * SwPortVLCongestion fields
	 */
	{BITSOFFS(32, 16), "SWPortVLCongestion0", mad_dump_uint},
	{BITSOFFS(48, 16), "SWPortVLCongestion1", mad_dump_uint},
	{BITSOFFS(64, 16), "SWPortVLCongestion2", mad_dump_uint},
	{BITSOFFS(80, 16), "SWPortVLCongestion3", mad_dump_uint},
	{BITSOFFS(96, 16), "SWPortVLCongestion4", mad_dump_uint},
	{BITSOFFS(112, 16), "SWPortVLCongestion5", mad_dump_uint},
	{BITSOFFS(128, 16), "SWPortVLCongestion6", mad_dump_uint},
	{BITSOFFS(144, 16), "SWPortVLCongestion7", mad_dump_uint},
	{BITSOFFS(160, 16), "SWPortVLCongestion8", mad_dump_uint},
	{BITSOFFS(176, 16), "SWPortVLCongestion9", mad_dump_uint},
	{BITSOFFS(192, 16), "SWPortVLCongestion10", mad_dump_uint},
	{BITSOFFS(208, 16), "SWPortVLCongestion11", mad_dump_uint},
	{BITSOFFS(224, 16), "SWPortVLCongestion12", mad_dump_uint},
	{BITSOFFS(240, 16), "SWPortVLCongestion13", mad_dump_uint},
	{BITSOFFS(256, 16), "SWPortVLCongestion14", mad_dump_uint},
	{BITSOFFS(272, 16), "SWPortVLCongestion15", mad_dump_uint},
	{0, 0},			/* IB_PC_SW_PORT_VL_CONGESTION_LAST_F */

	/*
	 * PortRcvConCtrl fields
	 */
	{32, 32, "PortPktRcvFECN", mad_dump_uint},
	{64, 32, "PortPktRcvBECN", mad_dump_uint},
	{0, 0},			/* IB_PC_RCV_CON_CTRL_LAST_F */

	/*
	 * PortSLRcvFECN fields
	 */
	{32, 32, "PortSLRcvFECN0", mad_dump_uint},
	{64, 32, "PortSLRcvFECN1", mad_dump_uint},
	{96, 32, "PortSLRcvFECN2", mad_dump_uint},
	{128, 32, "PortSLRcvFECN3", mad_dump_uint},
	{160, 32, "PortSLRcvFECN4", mad_dump_uint},
	{192, 32, "PortSLRcvFECN5", mad_dump_uint},
	{224, 32, "PortSLRcvFECN6", mad_dump_uint},
	{256, 32, "PortSLRcvFECN7", mad_dump_uint},
	{288, 32, "PortSLRcvFECN8", mad_dump_uint},
	{320, 32, "PortSLRcvFECN9", mad_dump_uint},
	{352, 32, "PortSLRcvFECN10", mad_dump_uint},
	{384, 32, "PortSLRcvFECN11", mad_dump_uint},
	{416, 32, "PortSLRcvFECN12", mad_dump_uint},
	{448, 32, "PortSLRcvFECN13", mad_dump_uint},
	{480, 32, "PortSLRcvFECN14", mad_dump_uint},
	{512, 32, "PortSLRcvFECN15", mad_dump_uint},
	{0, 0},			/* IB_PC_SL_RCV_FECN_LAST_F */

	/*
	 * PortSLRcvBECN fields
	 */
	{32, 32, "PortSLRcvBECN0", mad_dump_uint},
	{64, 32, "PortSLRcvBECN1", mad_dump_uint},
	{96, 32, "PortSLRcvBECN2", mad_dump_uint},
	{128, 32, "PortSLRcvBECN3", mad_dump_uint},
	{160, 32, "PortSLRcvBECN4", mad_dump_uint},
	{192, 32, "PortSLRcvBECN5", mad_dump_uint},
	{224, 32, "PortSLRcvBECN6", mad_dump_uint},
	{256, 32, "PortSLRcvBECN7", mad_dump_uint},
	{288, 32, "PortSLRcvBECN8", mad_dump_uint},
	{320, 32, "PortSLRcvBECN9", mad_dump_uint},
	{352, 32, "PortSLRcvBECN10", mad_dump_uint},
	{384, 32, "PortSLRcvBECN11", mad_dump_uint},
	{416, 32, "PortSLRcvBECN12", mad_dump_uint},
	{448, 32, "PortSLRcvBECN13", mad_dump_uint},
	{480, 32, "PortSLRcvBECN14", mad_dump_uint},
	{512, 32, "PortSLRcvBECN15", mad_dump_uint},
	{0, 0},			/* IB_PC_SL_RCV_BECN_LAST_F */

	/*
	 * PortXmitConCtrl fields
	 */
	{32, 32, "PortXmitTimeCong", mad_dump_uint},
	{0, 0},			/* IB_PC_XMIT_CON_CTRL_LAST_F */

	/*
	 * PortVLXmitTimeCong fields
	 */
	{32, 32, "PortVLXmitTimeCong0", mad_dump_uint},
	{64, 32, "PortVLXmitTimeCong1", mad_dump_uint},
	{96, 32, "PortVLXmitTimeCong2", mad_dump_uint},
	{128, 32, "PortVLXmitTimeCong3", mad_dump_uint},
	{160, 32, "PortVLXmitTimeCong4", mad_dump_uint},
	{192, 32, "PortVLXmitTimeCong5", mad_dump_uint},
	{224, 32, "PortVLXmitTimeCong6", mad_dump_uint},
	{256, 32, "PortVLXmitTimeCong7", mad_dump_uint},
	{288, 32, "PortVLXmitTimeCong8", mad_dump_uint},
	{320, 32, "PortVLXmitTimeCong9", mad_dump_uint},
	{352, 32, "PortVLXmitTimeCong10", mad_dump_uint},
	{384, 32, "PortVLXmitTimeCong11", mad_dump_uint},
	{416, 32, "PortVLXmitTimeCong12", mad_dump_uint},
	{448, 32, "PortVLXmitTimeCong13", mad_dump_uint},
	{480, 32, "PortVLXmitTimeCong14", mad_dump_uint},
	{0, 0},			/* IB_PC_VL_XMIT_TIME_CONG_LAST_F */

	/*
	 * Mellanox ExtendedPortInfo fields
	 */
	{BITSOFFS(24, 8), "StateChangeEnable", mad_dump_hex},
	{BITSOFFS(56, 8), "LinkSpeedSupported", mad_dump_hex},
	{BITSOFFS(88, 8), "LinkSpeedEnabled", mad_dump_hex},
	{BITSOFFS(120, 8), "LinkSpeedActive", mad_dump_hex},
	{0, 0},			/* IB_MLNX_EXT_PORT_LAST_F */

	/*
	 * Congestion Control Mad fields
	 * bytes 24-31 of congestion control mad
	 */
	{192, 64, "CC_Key", mad_dump_hex},	/* IB_CC_CCKEY_F */

	/*
	 * CongestionInfo fields
	 */
	{BITSOFFS(0, 16), "CongestionInfo", mad_dump_hex},
	{BITSOFFS(16, 8), "ControlTableCap", mad_dump_uint},
	{0, 0},			/* IB_CC_CONGESTION_INFO_LAST_F */

	/*
	 * CongestionKeyInfo fields
	 */
	{0, 64, "CC_Key", mad_dump_hex},
	{BITSOFFS(64, 1), "CC_KeyProtectBit", mad_dump_uint},
	{BITSOFFS(80, 16), "CC_KeyLeasePeriod", mad_dump_uint},
	{BITSOFFS(96, 16), "CC_KeyViolations", mad_dump_uint},
	{0, 0},			/* IB_CC_CONGESTION_KEY_INFO_LAST_F */

	/*
	 * CongestionLog (common) fields
	 */
	{BITSOFFS(0, 8), "LogType", mad_dump_uint},
	{BITSOFFS(8, 8), "CongestionFlags", mad_dump_hex},
	{0, 0},			/* IB_CC_CONGESTION_LOG_LAST_F */

	/*
	 * CongestionLog (Switch) fields
	 */
	{BITSOFFS(16, 16), "LogEventsCounter", mad_dump_uint},
	{32, 32, "CurrentTimeStamp", mad_dump_uint},
	{64, 256, "PortMap", mad_dump_array},
	{0, 0},			/* IB_CC_CONGESTION_LOG_SWITCH_LAST_F */

	/*
	 * CongestionLogEvent (Switch) fields
	 */
	{BITSOFFS(0, 16), "SLID", mad_dump_uint},
	{BITSOFFS(16, 16), "DLID", mad_dump_uint},
	{BITSOFFS(32, 4), "SL", mad_dump_uint},
	{64, 32, "Timestamp", mad_dump_uint},
	{0, 0},			/* IB_CC_CONGESTION_LOG_ENTRY_SWITCH_LAST_F */

	/*
	 * CongestionLog (CA) fields
	 */
	{BITSOFFS(16, 16), "ThresholdEventCounter", mad_dump_uint},
	{BITSOFFS(32, 16), "ThresholdCongestionEventMap", mad_dump_hex},
	/* XXX: Q3/2010 errata lists offset 48, but that means field is not
	 * word aligned.  Assume will be aligned to offset 64 later.
	 */
	{BITSOFFS(64, 32), "CurrentTimeStamp", mad_dump_uint},
	{0, 0},			/* IB_CC_CONGESTION_LOG_CA_LAST_F */

	/*
	 * CongestionLogEvent (CA) fields
	 */
	{BITSOFFS(0, 24), "Local_QP_CN_Entry", mad_dump_uint},
	{BITSOFFS(24, 4), "SL_CN_Entry", mad_dump_uint},
	{BITSOFFS(28, 4), "Service_Type_CN_Entry", mad_dump_hex},
	{BITSOFFS(32, 24), "Remote_QP_Number_CN_Entry", mad_dump_uint},
	{BITSOFFS(64, 16), "Local_LID_CN", mad_dump_uint},
	{BITSOFFS(80, 16), "Remote_LID_CN_Entry", mad_dump_uint},
	{BITSOFFS(96, 32), "Timestamp_CN_Entry", mad_dump_uint},
	{0, 0},			/* IB_CC_CONGESTION_LOG_ENTRY_CA_LAST_F */

	/*
	 * SwitchCongestionSetting fields
	 */
	{0, 32, "Control_Map", mad_dump_hex},
	{32, 256, "Victim_Mask", mad_dump_array},
	{288, 256, "Credit_Mask", mad_dump_array},
	{BITSOFFS(544, 4), "Threshold", mad_dump_hex},
	{BITSOFFS(552, 8), "Packet_Size", mad_dump_uint},
	{BITSOFFS(560, 4), "CS_Threshold", mad_dump_hex},
	{BITSOFFS(576, 16), "CS_ReturnDelay", mad_dump_hex}, /* TODO: CCT dump */
	{BITSOFFS(592, 16), "Marking_Rate", mad_dump_uint},
	{0, 0},			/* IB_CC_SWITCH_CONGESTION_SETTING_LAST_F */

	/*
	 * SwitchPortCongestionSettingElement fields
	 */
	{BITSOFFS(0, 1), "Valid", mad_dump_uint},
	{BITSOFFS(1, 1), "Control_Type", mad_dump_uint},
	{BITSOFFS(4, 4), "Threshold", mad_dump_hex},
	{BITSOFFS(8, 8), "Packet_Size", mad_dump_uint},
	{BITSOFFS(16, 16), "Cong_Parm_Marking_Rate", mad_dump_uint},
	{0, 0},			/* IB_CC_SWITCH_PORT_CONGESTION_SETTING_ELEMENT_LAST_F */

	/*
	 * CACongestionSetting fields
	 */
	{BITSOFFS(0, 16), "Port_Control", mad_dump_hex},
	{BITSOFFS(16, 16), "Control_Map", mad_dump_hex},
	{0, 0},			/* IB_CC_CA_CONGESTION_SETTING_LAST_F */

	/*
	 * CACongestionEntry fields
	 */
	{BITSOFFS(0, 16), "CCTI_Timer", mad_dump_uint},
	{BITSOFFS(16, 8), "CCTI_Increase", mad_dump_uint},
	{BITSOFFS(24, 8), "Trigger_Threshold", mad_dump_uint},
	{BITSOFFS(32, 8), "CCTI_Min", mad_dump_uint},
	{0, 0},			/* IB_CC_CA_CONGESTION_SETTING_ENTRY_LAST_F */

	/*
	 * CongestionControlTable fields
	 */
	{BITSOFFS(0, 16), "CCTI_Limit", mad_dump_uint},
	{0, 0},			/* IB_CC_CONGESTION_CONTROL_TABLE_LAST_F */

	/*
	 * CongestionControlTableEntry fields
	 */
	{BITSOFFS(0, 2), "CCT_Shift", mad_dump_uint},
	{BITSOFFS(2, 14), "CCT_Multiplier", mad_dump_uint},
	{0, 0},			/* IB_CC_CONGESTION_CONTROL_TABLE_ENTRY_LAST_F */

	/*
	 * Timestamp fields
	 */
	{0, 32, "Timestamp", mad_dump_uint},
	{0, 0}, /* IB_CC_TIMESTAMP_LAST_F */

	/* Node Record */
	{BITSOFFS(0, 16), "Lid", mad_dump_uint},
	{BITSOFFS(32, 8), "BaseVers", mad_dump_uint},
	{BITSOFFS(40, 8), "ClassVers", mad_dump_uint},
	{BITSOFFS(48, 8), "NodeType", mad_dump_node_type},
	{BITSOFFS(56, 8), "NumPorts", mad_dump_uint},
	{64, 64, "SystemGuid", mad_dump_hex},
	{128, 64, "Guid", mad_dump_hex},
	{192, 64, "PortGuid", mad_dump_hex},
	{BITSOFFS(256, 16), "PartCap", mad_dump_uint},
	{BITSOFFS(272, 16), "DevId", mad_dump_hex},
	{288, 32, "Revision", mad_dump_hex},
	{BITSOFFS(320, 8), "LocalPort", mad_dump_uint},
	{BITSOFFS(328, 24), "VendorId", mad_dump_hex},
	{352, 64 * 8, "NodeDesc", mad_dump_string},
	{0, 0}, /* IB_SA_NR_LAST_F */

	/*
	 * PortMirrorRoute fields
	 */
	{BITSOFFS(0, 16), "EncapRawEthType", mad_dump_hex},
	{BITSOFFS(20, 12), "MaxMirrorLen", mad_dump_hex},
	{BITSOFFS(32, 3), "MT", mad_dump_hex},
	{BITSOFFS(35, 1), "BF", mad_dump_hex},
	{BITSOFFS(56, 8), "NMPort", mad_dump_hex},
	{BITSOFFS(64, 4), "EncapLRHVL", mad_dump_hex},
	{BITSOFFS(68, 4), "EncapLRHLVer", mad_dump_hex},
	{BITSOFFS(72, 4), "EncapLRHSL", mad_dump_hex},
	{BITSOFFS(78, 2), "EncapLRHLNH", mad_dump_hex},
	{BITSOFFS(80, 16), "EncapLRHDLID", mad_dump_hex},
	{BITSOFFS(101, 11), "EncapLRHLength", mad_dump_hex},
	{BITSOFFS(112, 16), "EncapLRHSLID", mad_dump_hex},
	{0, 0},			/* IB_PMR_LAST_F */

	/*
	 * PortMirrorFilter fields
	 */
	{0, 32, "MirrorFilter0", mad_dump_hex},
	{32, 32, "MirrorFilter1", mad_dump_hex},
	{64, 32, "MirrorMask0", mad_dump_hex},
	{96, 32, "MirrorMask1", mad_dump_hex},
	{128, 32, "MirrorMask2", mad_dump_hex},
	{160, 32, "MirrorMask3", mad_dump_hex},
	{BITSOFFS(192, 1), "B0", mad_dump_hex},
	{BITSOFFS(196, 12), "MirrorMaskOffset0", mad_dump_hex},
	{BITSOFFS(208, 1), "B1", mad_dump_hex},
	{BITSOFFS(212, 12), "MirrorMaskOffset1", mad_dump_hex},
	{BITSOFFS(224, 1), "B2", mad_dump_hex},
	{BITSOFFS(228, 12), "MirrorMaskOffset2", mad_dump_hex},
	{BITSOFFS(240, 1), "B3", mad_dump_hex},
	{BITSOFFS(244, 12), "MirrorMaskOffset3", mad_dump_hex},
	{0, 0},			/* IB_PMF_LAST_F */

	/*
	 * PortMirrorPorts fields
	 */
	{BITSOFFS(10, 2), "TQ1", mad_dump_hex},
	{BITSOFFS(14, 2), "RQ1", mad_dump_hex},
	{BITSOFFS(18, 2), "TQ2", mad_dump_hex},
	{BITSOFFS(22, 2), "RQ2", mad_dump_hex},
	{BITSOFFS(26, 2), "TQ3", mad_dump_hex},
	{BITSOFFS(30, 2), "RQ3", mad_dump_hex},
	{BITSOFFS(34, 2), "TQ4", mad_dump_hex},
	{BITSOFFS(38, 2), "RQ4", mad_dump_hex},
	{BITSOFFS(42, 2), "TQ5", mad_dump_hex},
	{BITSOFFS(46, 2), "RQ5", mad_dump_hex},
	{BITSOFFS(50, 2), "TQ6", mad_dump_hex},
	{BITSOFFS(54, 2), "RQ6", mad_dump_hex},
	{BITSOFFS(58, 2), "TQ7", mad_dump_hex},
	{BITSOFFS(62, 2), "RQ7", mad_dump_hex},
	{BITSOFFS(66, 2), "TQ8", mad_dump_hex},
	{BITSOFFS(70, 2), "RQ8", mad_dump_hex},
	{BITSOFFS(74, 2), "TQ9", mad_dump_hex},
	{BITSOFFS(78, 2), "RQ9", mad_dump_hex},
	{BITSOFFS(82, 2), "TQ10", mad_dump_hex},
	{BITSOFFS(86, 2), "RQ10", mad_dump_hex},
	{BITSOFFS(90, 2), "TQ11", mad_dump_hex},
	{BITSOFFS(94, 2), "RQ11", mad_dump_hex},
	{BITSOFFS(98, 2), "TQ12", mad_dump_hex},
	{BITSOFFS(102, 2), "RQ12", mad_dump_hex},
	{BITSOFFS(106, 2), "TQ13", mad_dump_hex},
	{BITSOFFS(110, 2), "RQ13", mad_dump_hex},
	{BITSOFFS(114, 2), "TQ14", mad_dump_hex},
	{BITSOFFS(118, 2), "RQ14", mad_dump_hex},
	{BITSOFFS(122, 2), "TQ15", mad_dump_hex},
	{BITSOFFS(126, 2), "RQ15", mad_dump_hex},
	{BITSOFFS(130, 2), "TQ16", mad_dump_hex},
	{BITSOFFS(134, 2), "RQ16", mad_dump_hex},
	{BITSOFFS(138, 2), "TQ17", mad_dump_hex},
	{BITSOFFS(142, 2), "RQ17", mad_dump_hex},
	{BITSOFFS(146, 2), "TQ18", mad_dump_hex},
	{BITSOFFS(150, 2), "RQ18", mad_dump_hex},
	{BITSOFFS(154, 2), "TQ19", mad_dump_hex},
	{BITSOFFS(158, 2), "RQ19", mad_dump_hex},
	{BITSOFFS(162, 2), "TQ20", mad_dump_hex},
	{BITSOFFS(166, 2), "RQ20", mad_dump_hex},
	{BITSOFFS(170, 2), "TQ21", mad_dump_hex},
	{BITSOFFS(174, 2), "RQ21", mad_dump_hex},
	{BITSOFFS(178, 2), "TQ22", mad_dump_hex},
	{BITSOFFS(182, 2), "RQ22", mad_dump_hex},
	{BITSOFFS(186, 2), "TQ23", mad_dump_hex},
	{BITSOFFS(190, 2), "RQ23", mad_dump_hex},
	{BITSOFFS(194, 2), "TQ24", mad_dump_hex},
	{BITSOFFS(198, 2), "RQ24", mad_dump_hex},
	{BITSOFFS(202, 2), "TQ25", mad_dump_hex},
	{BITSOFFS(206, 2), "RQ25", mad_dump_hex},
	{BITSOFFS(210, 2), "TQ26", mad_dump_hex},
	{BITSOFFS(214, 2), "RQ26", mad_dump_hex},
	{BITSOFFS(218, 2), "TQ27", mad_dump_hex},
	{BITSOFFS(222, 2), "RQ27", mad_dump_hex},
	{BITSOFFS(226, 2), "TQ28", mad_dump_hex},
	{BITSOFFS(230, 2), "RQ28", mad_dump_hex},
	{BITSOFFS(234, 2), "TQ29", mad_dump_hex},
	{BITSOFFS(238, 2), "RQ29", mad_dump_hex},
	{BITSOFFS(242, 2), "TQ30", mad_dump_hex},
	{BITSOFFS(246, 2), "RQ30", mad_dump_hex},
	{BITSOFFS(250, 2), "TQ31", mad_dump_hex},
	{BITSOFFS(254, 2), "RQ31", mad_dump_hex},
	{BITSOFFS(258, 2), "TQ32", mad_dump_hex},
	{BITSOFFS(262, 2), "RQ32", mad_dump_hex},
	{BITSOFFS(266, 2), "TQ33", mad_dump_hex},
	{BITSOFFS(270, 2), "RQ33", mad_dump_hex},
	{BITSOFFS(274, 2), "TQ34", mad_dump_hex},
	{BITSOFFS(278, 2), "RQ34", mad_dump_hex},
	{BITSOFFS(282, 2), "TQ35", mad_dump_hex},
	{BITSOFFS(286, 2), "RQ35", mad_dump_hex},
	{BITSOFFS(290, 2), "TQ36", mad_dump_hex},
	{BITSOFFS(294, 2), "RQ36", mad_dump_hex},
	{0, 0},			/* IB_FIELD_LAST_ */

	/*
	 * PortSamplesResult fields
	 */
	{BITSOFFS(0, 16), "Tag", mad_dump_hex},
	{BITSOFFS(30, 2), "SampleStatus", mad_dump_hex},
	{32, 32, "Counter0", mad_dump_uint},
	{64, 32, "Counter1", mad_dump_uint},
	{96, 32, "Counter2", mad_dump_uint},
	{128, 32, "Counter3", mad_dump_uint},
	{160, 32, "Counter4", mad_dump_uint},
	{192, 32, "Counter5", mad_dump_uint},
	{224, 32, "Counter6", mad_dump_uint},
	{256, 32, "Counter7", mad_dump_uint},
	{288, 32, "Counter8", mad_dump_uint},
	{320, 32, "Counter9", mad_dump_uint},
	{352, 32, "Counter10", mad_dump_uint},
	{384, 32, "Counter11", mad_dump_uint},
	{416, 32, "Counter12", mad_dump_uint},
	{448, 32, "Counter13", mad_dump_uint},
	{480, 32, "Counter14", mad_dump_uint},
	{0, 0},			/* IB_PSR_LAST_F */

	/*
	 * PortInfoExtended fields
	 */
	{0, 32, "CapMask", mad_dump_hex},
	{BITSOFFS(32, 16), "FECModeActive", mad_dump_uint},
	{BITSOFFS(48, 16), "FDRFECModeSupported", mad_dump_uint},
	{BITSOFFS(64, 16), "FDRFECModeEnabled", mad_dump_uint},
	{BITSOFFS(80, 16), "EDRFECModeSupported", mad_dump_uint},
	{BITSOFFS(96, 16), "EDRFECModeEnabled", mad_dump_uint},
	{0, 0},			/* IB_PORT_EXT_LAST_F */

	/*
	 * PortExtendedSpeedsCounters RSFEC Active fields
	 */
	{BITSOFFS(8, 8), "PortSelect", mad_dump_uint},
	{64, 64, "CounterSelect", mad_dump_hex},
	{BITSOFFS(128, 16), "SyncHeaderErrorCounter", mad_dump_uint},
	{BITSOFFS(144, 16), "UnknownBlockCounter", mad_dump_uint},
	{352, 32, "FECCorrectableSymbolCtrLane0", mad_dump_uint},
	{384, 32, "FECCorrectableSymbolCtrLane1", mad_dump_uint},
	{416, 32, "FECCorrectableSymbolCtrLane2", mad_dump_uint},
	{448, 32, "FECCorrectableSymbolCtrLane3", mad_dump_uint},
	{480, 32, "FECCorrectableSymbolCtrLane4", mad_dump_uint},
	{512, 32, "FECCorrectableSymbolCtrLane5", mad_dump_uint},
	{544, 32, "FECCorrectableSymbolCtrLane6", mad_dump_uint},
	{576, 32, "FECCorrectableSymbolCtrLane7", mad_dump_uint},
	{608, 32, "FECCorrectableSymbolCtrLane8", mad_dump_uint},
	{640, 32, "FECCorrectableSymbolCtrLane9", mad_dump_uint},
	{672, 32, "FECCorrectableSymbolCtrLane10", mad_dump_uint},
	{704, 32, "FECCorrectableSymbolCtrLane11", mad_dump_uint},
	{1120, 32, "PortFECCorrectableBlockCtr", mad_dump_uint},
	{1152, 32, "PortFECUncorrectableBlockCtr", mad_dump_uint},
	{1184, 32, "PortFECCorrectedSymbolCtr", mad_dump_uint},
	{0, 0},			/* IB_PESC_RSFEC_LAST_F */

	/*
	 * More PortCountersExtended fields
	 */
	{32, 32, "CounterSelect2", mad_dump_hex},
	{576, 64, "SymbolErrorCounter", mad_dump_uint},
	{640, 64, "LinkErrorRecoveryCounter",  mad_dump_uint},
	{704, 64, "LinkDownedCounter", mad_dump_uint},
	{768, 64, "PortRcvErrors", mad_dump_uint},
	{832, 64, "PortRcvRemotePhysicalErrors", mad_dump_uint},
	{896, 64, "PortRcvSwitchRelayErrors", mad_dump_uint},
	{960, 64, "PortXmitDiscards", mad_dump_uint},
	{1024, 64, "PortXmitConstraintErrors", mad_dump_uint},
	{1088, 64, "PortRcvConstraintErrors", mad_dump_uint},
	{1152, 64, "LocalLinkIntegrityErrors", mad_dump_uint},
	{1216, 64, "ExcessiveBufferOverrunErrors", mad_dump_uint},
	{1280, 64, "VL15Dropped", mad_dump_uint},
	{1344, 64, "PortXmitWait", mad_dump_uint},
	{1408, 64, "QP1Dropped", mad_dump_uint},
	{0, 0},			/* IB_PC_EXT_ERR_LAST_F */

	/*
	 * Another PortCounters field
	*/
	{160, 16, "QP1Dropped", mad_dump_uint},

	{0, 0}			/* IB_FIELD_LAST_ */
};

static void _set_field64(void *buf, int base_offs, const ib_field_t * f,
			 uint64_t val)
{
	uint64_t nval;

	nval = htonll(val);
	memcpy(((void *)(char *)buf + base_offs + f->bitoffs / 8),
		(void *)&nval, sizeof(uint64_t));
}

static uint64_t _get_field64(void *buf, int base_offs, const ib_field_t * f)
{
	uint64_t val;
	memcpy((void *)&val, (void *)((char *)buf + base_offs + f->bitoffs / 8),
		sizeof(uint64_t));
	return ntohll(val);
}

static void _set_field(void *buf, int base_offs, const ib_field_t * f,
		       uint32_t val)
{
	int prebits = (8 - (f->bitoffs & 7)) & 7;
	int postbits = (f->bitoffs + f->bitlen) & 7;
	int bytelen = f->bitlen / 8;
	unsigned idx = base_offs + f->bitoffs / 8;
	char *p = (char *)buf;

	if (!bytelen && (f->bitoffs & 7) + f->bitlen < 8) {
		p[3 ^ idx] &= ~((((1 << f->bitlen) - 1)) << (f->bitoffs & 7));
		p[3 ^ idx] |=
		    (val & ((1 << f->bitlen) - 1)) << (f->bitoffs & 7);
		return;
	}

	if (prebits) {		/* val lsb in byte msb */
		p[3 ^ idx] &= (1 << (8 - prebits)) - 1;
		p[3 ^ idx++] |= (val & ((1 << prebits) - 1)) << (8 - prebits);
		val >>= prebits;
	}

	/* BIG endian byte order */
	for (; bytelen--; val >>= 8)
		p[3 ^ idx++] = val & 0xff;

	if (postbits) {		/* val msb in byte lsb */
		p[3 ^ idx] &= ~((1 << postbits) - 1);
		p[3 ^ idx] |= val;
	}
}

static uint32_t _get_field(void *buf, int base_offs, const ib_field_t * f)
{
	int prebits = (8 - (f->bitoffs & 7)) & 7;
	int postbits = (f->bitoffs + f->bitlen) & 7;
	int bytelen = f->bitlen / 8;
	unsigned idx = base_offs + f->bitoffs / 8;
	uint8_t *p = (uint8_t *) buf;
	uint32_t val = 0, v = 0, i;

	if (!bytelen && (f->bitoffs & 7) + f->bitlen < 8)
		return (p[3 ^ idx] >> (f->bitoffs & 7)) & ((1 << f->bitlen) -
							   1);

	if (prebits)		/* val lsb from byte msb */
		v = p[3 ^ idx++] >> (8 - prebits);

	if (postbits) {		/* val msb from byte lsb */
		i = base_offs + (f->bitoffs + f->bitlen) / 8;
		val = (p[3 ^ i] & ((1 << postbits) - 1));
	}

	/* BIG endian byte order */
	for (idx += bytelen - 1; bytelen--; idx--)
		val = (val << 8) | p[3 ^ idx];

	return (val << prebits) | v;
}

/* field must be byte aligned */
static void _set_array(void *buf, int base_offs, const ib_field_t * f,
		       void *val)
{
	int bitoffs = f->bitoffs;

	if (f->bitlen < 32)
		bitoffs = BE_TO_BITSOFFS(bitoffs, f->bitlen);

	memcpy((uint8_t *) buf + base_offs + bitoffs / 8, val, f->bitlen / 8);
}

static void _get_array(void *buf, int base_offs, const ib_field_t * f,
		       void *val)
{
	int bitoffs = f->bitoffs;

	if (f->bitlen < 32)
		bitoffs = BE_TO_BITSOFFS(bitoffs, f->bitlen);

	memcpy(val, (uint8_t *) buf + base_offs + bitoffs / 8, f->bitlen / 8);
}

uint32_t mad_get_field(void *buf, int base_offs, enum MAD_FIELDS field)
{
	return _get_field(buf, base_offs, ib_mad_f + field);
}

void mad_set_field(void *buf, int base_offs, enum MAD_FIELDS field,
		   uint32_t val)
{
	_set_field(buf, base_offs, ib_mad_f + field, val);
}

uint64_t mad_get_field64(void *buf, int base_offs, enum MAD_FIELDS field)
{
	return _get_field64(buf, base_offs, ib_mad_f + field);
}

void mad_set_field64(void *buf, int base_offs, enum MAD_FIELDS field,
		     uint64_t val)
{
	_set_field64(buf, base_offs, ib_mad_f + field, val);
}

void mad_set_array(void *buf, int base_offs, enum MAD_FIELDS field, void *val)
{
	_set_array(buf, base_offs, ib_mad_f + field, val);
}

void mad_get_array(void *buf, int base_offs, enum MAD_FIELDS field, void *val)
{
	_get_array(buf, base_offs, ib_mad_f + field, val);
}

void mad_decode_field(uint8_t * buf, enum MAD_FIELDS field, void *val)
{
	const ib_field_t *f = ib_mad_f + field;

	if (!field) {
		*(int *)val = *(int *)buf;
		return;
	}
	if (f->bitlen <= 32) {
		*(uint32_t *) val = _get_field(buf, 0, f);
		return;
	}
	if (f->bitlen == 64) {
		*(uint64_t *) val = _get_field64(buf, 0, f);
		return;
	}
	_get_array(buf, 0, f, val);
}

void mad_encode_field(uint8_t * buf, enum MAD_FIELDS field, void *val)
{
	const ib_field_t *f = ib_mad_f + field;

	if (!field) {
		*(int *)buf = *(int *)val;
		return;
	}
	if (f->bitlen <= 32) {
		_set_field(buf, 0, f, *(uint32_t *) val);
		return;
	}
	if (f->bitlen == 64) {
		_set_field64(buf, 0, f, *(uint64_t *) val);
		return;
	}
	_set_array(buf, 0, f, val);
}

/************************/

static char *_mad_dump_val(const ib_field_t * f, char *buf, int bufsz,
			   void *val)
{
	f->def_dump_fn(buf, bufsz, val, ALIGN(f->bitlen, 8) / 8);
	buf[bufsz - 1] = 0;

	return buf;
}

static char *_mad_dump_field(const ib_field_t * f, const char *name, char *buf,
			     int bufsz, void *val)
{
	char dots[128];
	int l, n;

	if (bufsz <= 32)
		return NULL;	/* buf too small */

	if (!name)
		name = f->name;

	l = strlen(name);
	if (l < 32) {
		memset(dots, '.', 32 - l);
		dots[32 - l] = 0;
	}

	n = snprintf(buf, bufsz, "%s:%s", name, dots);
	_mad_dump_val(f, buf + n, bufsz - n, val);
	buf[bufsz - 1] = 0;

	return buf;
}

static int _mad_dump(ib_mad_dump_fn * fn, const char *name, void *val,
		     int valsz)
{
	ib_field_t f;
	char buf[512];

	f.def_dump_fn = fn;
	f.bitlen = valsz * 8;

	return printf("%s\n", _mad_dump_field(&f, name, buf, sizeof buf, val));
}

static int _mad_print_field(const ib_field_t * f, const char *name, void *val,
			    int valsz)
{
	return _mad_dump(f->def_dump_fn, name ? name : f->name, val,
			 valsz ? valsz : ALIGN(f->bitlen, 8) / 8);
}

int mad_print_field(enum MAD_FIELDS field, const char *name, void *val)
{
	if (field <= IB_NO_FIELD || field >= IB_FIELD_LAST_)
		return -1;
	return _mad_print_field(ib_mad_f + field, name, val, 0);
}

char *mad_dump_field(enum MAD_FIELDS field, char *buf, int bufsz, void *val)
{
	if (field <= IB_NO_FIELD || field >= IB_FIELD_LAST_)
		return NULL;
	return _mad_dump_field(ib_mad_f + field, 0, buf, bufsz, val);
}

char *mad_dump_val(enum MAD_FIELDS field, char *buf, int bufsz, void *val)
{
	if (field <= IB_NO_FIELD || field >= IB_FIELD_LAST_)
		return NULL;
	return _mad_dump_val(ib_mad_f + field, buf, bufsz, val);
}

const char *mad_field_name(enum MAD_FIELDS field)
{
	return (ib_mad_f[field].name);
}
