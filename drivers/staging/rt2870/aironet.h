/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

	Module Name:
	aironet.h

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
	Paul Lin	04-06-15		Initial
*/

#ifndef	__AIRONET_H__
#define	__AIRONET_H__

// Measurement Type definition
#define	MSRN_TYPE_UNUSED				0
#define	MSRN_TYPE_CHANNEL_LOAD_REQ		1
#define	MSRN_TYPE_NOISE_HIST_REQ		2
#define	MSRN_TYPE_BEACON_REQ			3
#define	MSRN_TYPE_FRAME_REQ				4

// Scan Mode in Beacon Request
#define	MSRN_SCAN_MODE_PASSIVE			0
#define	MSRN_SCAN_MODE_ACTIVE			1
#define	MSRN_SCAN_MODE_BEACON_TABLE		2

// PHY type definition for Aironet beacon report, CCX 2 table 36-9
#define	PHY_FH							1
#define	PHY_DSS							2
#define	PHY_UNUSED						3
#define	PHY_OFDM						4
#define	PHY_HR_DSS						5
#define	PHY_ERP							6

// RPI table in dBm
#define	RPI_0			0			//	Power <= -87
#define	RPI_1			1			//	-87 < Power <= -82
#define	RPI_2			2			//	-82 < Power <= -77
#define	RPI_3			3			//	-77 < Power <= -72
#define	RPI_4			4			//	-72 < Power <= -67
#define	RPI_5			5			//	-67 < Power <= -62
#define	RPI_6			6			//	-62 < Power <= -57
#define	RPI_7			7			//	-57 < Power

// Cisco Aironet IAPP definetions
#define	AIRONET_IAPP_TYPE					0x32
#define	AIRONET_IAPP_SUBTYPE_REQUEST		0x01
#define	AIRONET_IAPP_SUBTYPE_REPORT			0x81

// Measurement Request detail format
typedef	struct	_MEASUREMENT_REQUEST	{
	UCHAR	Channel;
	UCHAR	ScanMode;			// Use only in beacon request, other requests did not use this field
	USHORT	Duration;
}	MEASUREMENT_REQUEST, *PMEASUREMENT_REQUEST;

// Beacon Measurement Report
// All these field might change to UCHAR, because we didn't do anything to these report.
// We copy all these beacons and report to CCX 2 AP.
typedef	struct	_BEACON_REPORT	{
	UCHAR	Channel;
	UCHAR	Spare;
	USHORT	Duration;
	UCHAR	PhyType;			// Definiation is listed above table 36-9
	UCHAR	RxPower;
	UCHAR	BSSID[6];
	UCHAR	ParentTSF[4];
	UCHAR	TargetTSF[8];
	USHORT	BeaconInterval;
	USHORT	CapabilityInfo;
}	BEACON_REPORT, *PBEACON_REPORT;

// Frame Measurement Report (Optional)
typedef	struct	_FRAME_REPORT	{
	UCHAR	Channel;
	UCHAR	Spare;
	USHORT	Duration;
	UCHAR	TA;
	UCHAR	BSSID[6];
	UCHAR	RSSI;
	UCHAR	Count;
}	FRAME_REPORT, *PFRAME_REPORT;

#pragma pack(1)
// Channel Load Report
typedef	struct	_CHANNEL_LOAD_REPORT	{
	UCHAR	Channel;
	UCHAR	Spare;
	USHORT	Duration;
	UCHAR	CCABusy;
}	CHANNEL_LOAD_REPORT, *PCHANNEL_LOAD_REPORT;
#pragma pack()

// Nosie Histogram Report
typedef	struct	_NOISE_HIST_REPORT	{
	UCHAR	Channel;
	UCHAR	Spare;
	USHORT	Duration;
	UCHAR	Density[8];
}	NOISE_HIST_REPORT, *PNOISE_HIST_REPORT;

// Radio Management Capability element
typedef	struct	_RADIO_MANAGEMENT_CAPABILITY	{
	UCHAR	Eid;				// TODO: Why the Eid is 1 byte, not normal 2 bytes???
	UCHAR	Length;
	UCHAR	AironetOui[3];		// AIronet OUI (00 40 96)
	UCHAR	Type;				// Type / Version
	USHORT	Status;				// swap16 required
}	RADIO_MANAGEMENT_CAPABILITY, *PRADIO_MANAGEMENT_CAPABILITY;

// Measurement Mode Bit definition
typedef	struct	_MEASUREMENT_MODE	{
	UCHAR	Rsvd:4;
	UCHAR	Report:1;
	UCHAR	NotUsed:1;
	UCHAR	Enable:1;
	UCHAR	Parallel:1;
}	MEASUREMENT_MODE, *PMEASUREMENT_MODE;

// Measurement Request element, This is little endian mode
typedef	struct	_MEASUREMENT_REQUEST_ELEMENT	{
	USHORT				Eid;
	USHORT				Length;				// swap16 required
	USHORT				Token;				// non-zero unique token
	UCHAR				Mode;				// Measurement Mode
	UCHAR				Type;				// Measurement type
}	MEASUREMENT_REQUEST_ELEMENT, *PMEASUREMENT_REQUEST_ELEMENT;

// Measurement Report element, This is little endian mode
typedef	struct	_MEASUREMENT_REPORT_ELEMENT	{
	USHORT				Eid;
	USHORT				Length;				// swap16 required
	USHORT				Token;				// non-zero unique token
	UCHAR				Mode;				// Measurement Mode
	UCHAR				Type;				// Measurement type
}	MEASUREMENT_REPORT_ELEMENT, *PMEASUREMENT_REPORT_ELEMENT;

// Cisco Aironet IAPP Frame Header, Network byte order used
typedef	struct	_AIRONET_IAPP_HEADER {
	UCHAR	CiscoSnapHeader[8];	// 8 bytes Cisco snap header
	USHORT	Length;				// IAPP ID & length, remember to swap16 in LE system
	UCHAR	Type;				// IAPP type
	UCHAR	SubType;			// IAPP subtype
	UCHAR	DA[6];				// Destination MAC address
	UCHAR	SA[6];				// Source MAC address
	USHORT	Token;				// Dialog token, no need to swap16 since it is for yoken usage only
}	AIRONET_IAPP_HEADER, *PAIRONET_IAPP_HEADER;

// Radio Measurement Request frame
typedef	struct	_AIRONET_RM_REQUEST_FRAME	{
    AIRONET_IAPP_HEADER	IAPP;			// Common header
	UCHAR				Delay;			// Activation Delay
	UCHAR				Offset;			// Measurement offset
}	AIRONET_RM_REQUEST_FRAME, *PAIRONET_RM_REQUEST_FRAME;

// Radio Measurement Report frame
typedef	struct	_AIRONET_RM_REPORT_FRAME	{
    AIRONET_IAPP_HEADER	IAPP;			// Common header
}	AIRONET_RM_REPORT_FRAME, *PAIRONET_RM_REPORT_FRAME;

// Saved element request actions which will saved in StaCfg.
typedef	struct	_RM_REQUEST_ACTION	{
	MEASUREMENT_REQUEST_ELEMENT	ReqElem;		// Saved request element
	MEASUREMENT_REQUEST			Measurement;	// Saved measurement within the request element
}	RM_REQUEST_ACTION, *PRM_REQUEST_ACTION;

// CCX administration control
typedef	union	_CCX_CONTROL	{
	struct	{
		UINT32		Enable:1;			// Enable CCX2
		UINT32		LeapEnable:1;		// Enable LEAP at CCX2
		UINT32		RMEnable:1;			// Radio Measurement Enable
		UINT32		DCRMEnable:1;		// Non serving channel Radio Measurement enable
		UINT32		QOSEnable:1;		// Enable QOS for CCX 2.0 support
		UINT32		FastRoamEnable:1;	// Enable fast roaming
		UINT32		Rsvd:2;				// Not used
		UINT32		dBmToRoam:8;		// the condition to roam when receiving Rssi less than this value. It's negative value.
		UINT32		TuLimit:16;			// Limit for different channel scan
	}	field;
	UINT32			word;
}	CCX_CONTROL, *PCCX_CONTROL;

#endif	// __AIRONET_H__
