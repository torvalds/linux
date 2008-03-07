/***************************************************************************
                          avc_api.h  -  description
                             -------------------
    begin                : Wed May 1 2000
    copyright            : (C) 2000 by Manfred Weihs
    copyright            : (C) 2003 by Philipp Gutgsell
    email                : 0014guph@edu.fh-kaernten.ac.at
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/*
 This is based on code written by Peter Halwachs,
 Thomas Groiss and Andreas Monitzer.
*/


#ifndef __AVC_API_H__
#define __AVC_API_H__

#include <linux/dvb/frontend.h>

#define BYTE	unsigned char
#define WORD	unsigned short
#define DWORD	unsigned long
#define ULONG	unsigned long
#define LONG	long


/*************************************************************
	FCP Address range
**************************************************************/

#define RESPONSE_REGISTER	0xFFFFF0000D00ULL
#define COMMAND_REGISTER	0xFFFFF0000B00ULL
#define PCR_BASE_ADDRESS	0xFFFFF0000900ULL


/************************************************************
	definition of structures
*************************************************************/
typedef struct {
	   int           Nr_SourcePlugs;
	   int 	         Nr_DestinationPlugs;
} TunerInfo;


/***********************************************

         supported cts

************************************************/

#define AVC  0x0

// FCP command frame with ctype = 0x0 is AVC command frame

#ifdef __LITTLE_ENDIAN

// Definition FCP Command Frame
typedef struct _AVCCmdFrm
{
		// AV/C command frame
	BYTE ctype  : 4 ;   // command type
	BYTE cts    : 4 ;   // always 0x0 for AVC
	BYTE suid   : 3 ;   // subunit ID
	BYTE sutyp  : 5 ;   // subunit_typ
	BYTE opcode : 8 ;   // opcode
	BYTE operand[509] ; // array of operands [1-507]
	int length;         //length of the command frame
} AVCCmdFrm ;

// Definition FCP Response Frame
typedef struct _AVCRspFrm
{
        // AV/C response frame
	BYTE resp		: 4 ;   // response type
	BYTE cts		: 4 ;   // always 0x0 for AVC
	BYTE suid		: 3 ;   // subunit ID
	BYTE sutyp	: 5 ;   // subunit_typ
	BYTE opcode	: 8 ;   // opcode
	BYTE operand[509] ; // array of operands [1-507]
	int length;         //length of the response frame
} AVCRspFrm ;

#else

typedef struct _AVCCmdFrm
{
	BYTE cts:4;
	BYTE ctype:4;
	BYTE sutyp:5;
	BYTE suid:3;
	BYTE opcode;
	BYTE operand[509];
	int length;
} AVCCmdFrm;

typedef struct _AVCRspFrm
{
	BYTE cts:4;
	BYTE resp:4;
	BYTE sutyp:5;
	BYTE suid:3;
	BYTE opcode;
	BYTE operand[509];
	int length;
} AVCRspFrm;

#endif

/*************************************************************
	AVC command types (ctype)
**************************************************************///
#define CONTROL    0x00
#define STATUS     0x01
#define INQUIRY    0x02
#define NOTIFY     0x03

/*************************************************************
	AVC respond types
**************************************************************///
#define NOT_IMPLEMENTED 0x8
#define ACCEPTED        0x9
#define REJECTED        0xA
#define STABLE          0xC
#define CHANGED         0xD
#define INTERIM         0xF

/*************************************************************
	AVC opcodes
**************************************************************///
#define CONNECT			0x24
#define DISCONNECT		0x25
#define UNIT_INFO		0x30
#define SUBUNIT_Info		0x31
#define VENDOR			0x00

#define PLUG_INFO		0x02
#define OPEN_DESCRIPTOR		0x08
#define READ_DESCRIPTOR		0x09
#define OBJECT_NUMBER_SELECT	0x0D

/*************************************************************
	AVCTuner opcodes
**************************************************************/

#define DSIT				0xC8
#define DSD				0xCB
#define DESCRIPTOR_TUNER_STATUS 	0x80
#define DESCRIPTOR_SUBUNIT_IDENTIFIER	0x00

/*************************************************************
	AVCTuner list types
**************************************************************/
#define Multiplex_List   0x80
#define Service_List     0x82

/*************************************************************
	AVCTuner object entries
**************************************************************/
#define Multiplex	 			0x80
#define Service 	 			0x82
#define Service_with_specified_components	0x83
#define Preferred_components			0x90
#define Component				0x84

/*************************************************************
	Vendor-specific commands
**************************************************************/

// digital everywhere vendor ID
#define SFE_VENDOR_DE_COMPANYID_0			0x00
#define SFE_VENDOR_DE_COMPANYID_1			0x12
#define SFE_VENDOR_DE_COMPANYID_2			0x87

#define SFE_VENDOR_MAX_NR_COMPONENTS		0x4
#define SFE_VENDOR_MAX_NR_SERVICES			0x3
#define SFE_VENDOR_MAX_NR_DSD_ELEMENTS		0x10

// vendor commands
#define SFE_VENDOR_OPCODE_REGISTER_REMOTE_CONTROL	0x0A
#define SFE_VENDOR_OPCODE_LNB_CONTROL		0x52
#define SFE_VENDOR_OPCODE_TUNE_QPSK			0x58	// QPSK command for DVB-S

// TODO: following vendor specific commands needs to be implemented
#define SFE_VENDOR_OPCODE_GET_FIRMWARE_VERSION	0x00
#define SFE_VENDOR_OPCODE_HOST2CA				0x56
#define SFE_VENDOR_OPCODE_CA2HOST				0x57
#define SFE_VENDOR_OPCODE_CISTATUS				0x59
#define SFE_VENDOR_OPCODE_TUNE_QPSK2			0x60 // QPSK command for DVB-S2 devices


//AVCTuner DVB identifier service_ID
#define DVB 0x20

/*************************************************************
						AVC descriptor types
**************************************************************/

#define Subunit_Identifier_Descriptor		 0x00
#define Tuner_Status_Descriptor				 0x80

typedef struct {
	BYTE          Subunit_Type;
	BYTE          Max_Subunit_ID;
} SUBUNIT_INFO;

/*************************************************************

		AVCTuner DVB object IDs are 6 byte long

**************************************************************/

typedef struct {
	BYTE  Byte0;
	BYTE  Byte1;
	BYTE  Byte2;
	BYTE  Byte3;
	BYTE  Byte4;
	BYTE  Byte5;
}OBJECT_ID;

/*************************************************************
						MULIPLEX Structs
**************************************************************/
typedef struct
{
#ifdef __LITTLE_ENDIAN
	BYTE       RF_frequency_hByte:6;
	BYTE       raster_Frequency:2;//Bit7,6 raster frequency
#else
	BYTE raster_Frequency:2;
	BYTE RF_frequency_hByte:6;
#endif
	BYTE       RF_frequency_mByte;
	BYTE       RF_frequency_lByte;

}FREQUENCY;

#ifdef __LITTLE_ENDIAN

typedef struct
{
		 BYTE        Modulation	    :1;
		 BYTE        FEC_inner	    :1;
		 BYTE        FEC_outer	    :1;
		 BYTE        Symbol_Rate    :1;
		 BYTE        Frequency	    :1;
		 BYTE        Orbital_Pos	:1;
		 BYTE        Polarisation	:1;
		 BYTE        reserved_fields :1;
		 BYTE        reserved1		:7;
		 BYTE        Network_ID	:1;

}MULTIPLEX_VALID_FLAGS;

typedef struct
{
	BYTE	GuardInterval:1;
	BYTE	CodeRateLPStream:1;
	BYTE	CodeRateHPStream:1;
	BYTE	HierarchyInfo:1;
	BYTE	Constellation:1;
	BYTE	Bandwidth:1;
	BYTE	CenterFrequency:1;
	BYTE	reserved1:1;
	BYTE	reserved2:5;
	BYTE	OtherFrequencyFlag:1;
	BYTE	TransmissionMode:1;
	BYTE	NetworkId:1;
}MULTIPLEX_VALID_FLAGS_DVBT;

#else

typedef struct {
	BYTE reserved_fields:1;
	BYTE Polarisation:1;
	BYTE Orbital_Pos:1;
	BYTE Frequency:1;
	BYTE Symbol_Rate:1;
	BYTE FEC_outer:1;
	BYTE FEC_inner:1;
	BYTE Modulation:1;
	BYTE Network_ID:1;
	BYTE reserved1:7;
}MULTIPLEX_VALID_FLAGS;

typedef struct {
	BYTE reserved1:1;
	BYTE CenterFrequency:1;
	BYTE Bandwidth:1;
	BYTE Constellation:1;
	BYTE HierarchyInfo:1;
	BYTE CodeRateHPStream:1;
	BYTE CodeRateLPStream:1;
	BYTE GuardInterval:1;
	BYTE NetworkId:1;
	BYTE TransmissionMode:1;
	BYTE OtherFrequencyFlag:1;
	BYTE reserved2:5;
}MULTIPLEX_VALID_FLAGS_DVBT;

#endif

typedef union {
	MULTIPLEX_VALID_FLAGS Bits;
	MULTIPLEX_VALID_FLAGS_DVBT Bits_T;
	struct {
		BYTE	ByteHi;
		BYTE	ByteLo;
	} Valid_Word;
} M_VALID_FLAGS;

typedef struct
{
#ifdef __LITTLE_ENDIAN
  BYTE      ActiveSystem;
  BYTE      reserved:5;
  BYTE      NoRF:1;
  BYTE      Moving:1;
  BYTE      Searching:1;

  BYTE      SelectedAntenna:7;
  BYTE      Input:1;

  BYTE      BER[4];

  BYTE      SignalStrength;
  FREQUENCY Frequency;

  BYTE      ManDepInfoLength;
#else
  BYTE ActiveSystem;
  BYTE Searching:1;
  BYTE Moving:1;
  BYTE NoRF:1;
  BYTE reserved:5;

  BYTE Input:1;
  BYTE SelectedAntenna:7;

  BYTE BER[4];

  BYTE SignalStrength;
  FREQUENCY Frequency;

  BYTE ManDepInfoLength;
#endif
} ANTENNA_INPUT_INFO; // 11 Byte

#define LNBCONTROL_DONTCARE 0xff


extern int AVCWrite(struct firesat *firesat, const AVCCmdFrm *CmdFrm, AVCRspFrm *RspFrm);
extern int AVCRecv(struct firesat *firesat, u8 *data, size_t length);

extern int AVCTuner_DSIT(struct firesat *firesat,
                           int Source_Plug,
						   struct dvb_frontend_parameters *params,
                           BYTE *status);

extern int AVCTunerStatus(struct firesat *firesat, ANTENNA_INPUT_INFO *antenna_input_info);
extern int AVCTuner_DSD(struct firesat *firesat, struct dvb_frontend_parameters *params, BYTE *status);
extern int AVCTuner_SetPIDs(struct firesat *firesat, unsigned char pidc, u16 pid[]);
extern int AVCTuner_GetTS(struct firesat *firesat);

extern int AVCIdentifySubunit(struct firesat *firesat, unsigned char *systemId, int *transport, int *has_ci);
extern int AVCLNBControl(struct firesat *firesat, char voltage, char burst, char conttone, char nrdiseq, struct dvb_diseqc_master_cmd *diseqcmd);
extern int AVCSubUnitInfo(struct firesat *firesat, char *subunitcount);
extern int AVCRegisterRemoteControl(struct firesat *firesat);

#endif

