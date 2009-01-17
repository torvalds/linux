/*
 * AV/C API
 *
 * Copyright (C) 2000 Manfred Weihs
 * Copyright (C) 2003 Philipp Gutgsell <0014guph@edu.fh-kaernten.ac.at>
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Ben Backx <ben@bbackx.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 * This is based on code written by Peter Halwachs, Thomas Groiss and
 * Andreas Monitzer.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#ifndef _AVC_API_H
#define _AVC_API_H

#include <linux/types.h>

/*************************************************************
	Constants from EN510221
**************************************************************/
#define LIST_MANAGEMENT_ONLY 0x03

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
	__u8 ctype  : 4 ;   // command type
	__u8 cts    : 4 ;   // always 0x0 for AVC
	__u8 suid   : 3 ;   // subunit ID
	__u8 sutyp  : 5 ;   // subunit_typ
	__u8 opcode : 8 ;   // opcode
	__u8 operand[509] ; // array of operands [1-507]
	int length;         //length of the command frame
} AVCCmdFrm ;

// Definition FCP Response Frame
typedef struct _AVCRspFrm
{
        // AV/C response frame
	__u8 resp		: 4 ;   // response type
	__u8 cts		: 4 ;   // always 0x0 for AVC
	__u8 suid		: 3 ;   // subunit ID
	__u8 sutyp	: 5 ;   // subunit_typ
	__u8 opcode	: 8 ;   // opcode
	__u8 operand[509] ; // array of operands [1-507]
	int length;         //length of the response frame
} AVCRspFrm ;

#else

typedef struct _AVCCmdFrm
{
	__u8 cts:4;
	__u8 ctype:4;
	__u8 sutyp:5;
	__u8 suid:3;
	__u8 opcode;
	__u8 operand[509];
	int length;
} AVCCmdFrm;

typedef struct _AVCRspFrm
{
	__u8 cts:4;
	__u8 resp:4;
	__u8 sutyp:5;
	__u8 suid:3;
	__u8 opcode;
	__u8 operand[509];
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

// CA Tags
#define SFE_VENDOR_TAG_CA_RESET			0x00
#define SFE_VENDOR_TAG_CA_APPLICATION_INFO	0x01
#define SFE_VENDOR_TAG_CA_PMT			0x02
#define SFE_VENDOR_TAG_CA_DATE_TIME		0x04
#define SFE_VENDOR_TAG_CA_MMI			0x05
#define SFE_VENDOR_TAG_CA_ENTER_MENU		0x07


//AVCTuner DVB identifier service_ID
#define DVB 0x20

/*************************************************************
						AVC descriptor types
**************************************************************/

#define Subunit_Identifier_Descriptor		 0x00
#define Tuner_Status_Descriptor				 0x80

typedef struct {
	__u8          Subunit_Type;
	__u8          Max_Subunit_ID;
} SUBUNIT_INFO;

/*************************************************************

		AVCTuner DVB object IDs are 6 byte long

**************************************************************/

typedef struct {
	__u8  Byte0;
	__u8  Byte1;
	__u8  Byte2;
	__u8  Byte3;
	__u8  Byte4;
	__u8  Byte5;
}OBJECT_ID;

/*************************************************************
						MULIPLEX Structs
**************************************************************/
typedef struct
{
#ifdef __LITTLE_ENDIAN
	__u8       RF_frequency_hByte:6;
	__u8       raster_Frequency:2;//Bit7,6 raster frequency
#else
	__u8 raster_Frequency:2;
	__u8 RF_frequency_hByte:6;
#endif
	__u8       RF_frequency_mByte;
	__u8       RF_frequency_lByte;

}FREQUENCY;

#ifdef __LITTLE_ENDIAN

typedef struct
{
		 __u8        Modulation	    :1;
		 __u8        FEC_inner	    :1;
		 __u8        FEC_outer	    :1;
		 __u8        Symbol_Rate    :1;
		 __u8        Frequency	    :1;
		 __u8        Orbital_Pos	:1;
		 __u8        Polarisation	:1;
		 __u8        reserved_fields :1;
		 __u8        reserved1		:7;
		 __u8        Network_ID	:1;

}MULTIPLEX_VALID_FLAGS;

typedef struct
{
	__u8	GuardInterval:1;
	__u8	CodeRateLPStream:1;
	__u8	CodeRateHPStream:1;
	__u8	HierarchyInfo:1;
	__u8	Constellation:1;
	__u8	Bandwidth:1;
	__u8	CenterFrequency:1;
	__u8	reserved1:1;
	__u8	reserved2:5;
	__u8	OtherFrequencyFlag:1;
	__u8	TransmissionMode:1;
	__u8	NetworkId:1;
}MULTIPLEX_VALID_FLAGS_DVBT;

#else

typedef struct {
	__u8 reserved_fields:1;
	__u8 Polarisation:1;
	__u8 Orbital_Pos:1;
	__u8 Frequency:1;
	__u8 Symbol_Rate:1;
	__u8 FEC_outer:1;
	__u8 FEC_inner:1;
	__u8 Modulation:1;
	__u8 Network_ID:1;
	__u8 reserved1:7;
}MULTIPLEX_VALID_FLAGS;

typedef struct {
	__u8 reserved1:1;
	__u8 CenterFrequency:1;
	__u8 Bandwidth:1;
	__u8 Constellation:1;
	__u8 HierarchyInfo:1;
	__u8 CodeRateHPStream:1;
	__u8 CodeRateLPStream:1;
	__u8 GuardInterval:1;
	__u8 NetworkId:1;
	__u8 TransmissionMode:1;
	__u8 OtherFrequencyFlag:1;
	__u8 reserved2:5;
}MULTIPLEX_VALID_FLAGS_DVBT;

#endif

typedef union {
	MULTIPLEX_VALID_FLAGS Bits;
	MULTIPLEX_VALID_FLAGS_DVBT Bits_T;
	struct {
		__u8	ByteHi;
		__u8	ByteLo;
	} Valid_Word;
} M_VALID_FLAGS;

typedef struct
{
#ifdef __LITTLE_ENDIAN
  __u8      ActiveSystem;
  __u8      reserved:5;
  __u8      NoRF:1;
  __u8      Moving:1;
  __u8      Searching:1;

  __u8      SelectedAntenna:7;
  __u8      Input:1;

  __u8      BER[4];

  __u8      SignalStrength;
  FREQUENCY Frequency;

  __u8      ManDepInfoLength;

  __u8 PowerSupply:1;
  __u8 FrontEndPowerStatus:1;
  __u8 reserved3:1;
  __u8 AntennaError:1;
  __u8 FrontEndError:1;
  __u8 reserved2:3;

  __u8 CarrierNoiseRatio[2];
  __u8 reserved4[2];
  __u8 PowerSupplyVoltage;
  __u8 AntennaVoltage;
  __u8 FirewireBusVoltage;

  __u8 CaMmi:1;
  __u8 reserved5:7;

  __u8 reserved6:1;
  __u8 CaInitializationStatus:1;
  __u8 CaErrorFlag:1;
  __u8 CaDvbFlag:1;
  __u8 CaModulePresentStatus:1;
  __u8 CaApplicationInfo:1;
  __u8 CaDateTimeRequest:1;
  __u8 CaPmtReply:1;

#else
  __u8 ActiveSystem;
  __u8 Searching:1;
  __u8 Moving:1;
  __u8 NoRF:1;
  __u8 reserved:5;

  __u8 Input:1;
  __u8 SelectedAntenna:7;

  __u8 BER[4];

  __u8 SignalStrength;
  FREQUENCY Frequency;

  __u8 ManDepInfoLength;

  __u8 reserved2:3;
  __u8 FrontEndError:1;
  __u8 AntennaError:1;
  __u8 reserved3:1;
  __u8 FrontEndPowerStatus:1;
  __u8 PowerSupply:1;

  __u8 CarrierNoiseRatio[2];
  __u8 reserved4[2];
  __u8 PowerSupplyVoltage;
  __u8 AntennaVoltage;
  __u8 FirewireBusVoltage;

  __u8 reserved5:7;
  __u8 CaMmi:1;
  __u8 CaPmtReply:1;
  __u8 CaDateTimeRequest:1;
  __u8 CaApplicationInfo:1;
  __u8 CaModulePresentStatus:1;
  __u8 CaDvbFlag:1;
  __u8 CaErrorFlag:1;
  __u8 CaInitializationStatus:1;
  __u8 reserved6:1;

#endif
} ANTENNA_INPUT_INFO; // 22 Byte

#define LNBCONTROL_DONTCARE 0xff

struct dvb_diseqc_master_cmd;
struct dvb_frontend_parameters;
struct firedtv;

int avc_recv(struct firedtv *fdtv, u8 *data, size_t length);

int AVCTuner_DSIT(struct firedtv *fdtv, int Source_Plug,
		struct dvb_frontend_parameters *params, __u8 *status);

int avc_tuner_status(struct firedtv *fdtv,
		ANTENNA_INPUT_INFO *antenna_input_info);
int avc_tuner_dsd(struct firedtv *fdtv,
		struct dvb_frontend_parameters *params);
int avc_tuner_set_pids(struct firedtv *fdtv, unsigned char pidc, u16 pid[]);
int avc_tuner_get_ts(struct firedtv *fdtv);
int avc_identify_subunit(struct firedtv *fdtv);
int avc_lnb_control(struct firedtv *fdtv, char voltage, char burst,
		char conttone, char nrdiseq,
		struct dvb_diseqc_master_cmd *diseqcmd);
void avc_remote_ctrl_work(struct work_struct *work);
int avc_register_remote_control(struct firedtv *fdtv);
int avc_ca_app_info(struct firedtv *fdtv, char *app_info, unsigned int *len);
int avc_ca_info(struct firedtv *fdtv, char *app_info, unsigned int *len);
int avc_ca_reset(struct firedtv *fdtv);
int avc_ca_pmt(struct firedtv *fdtv, char *app_info, int length);
int avc_ca_get_time_date(struct firedtv *fdtv, int *interval);
int avc_ca_enter_menu(struct firedtv *fdtv);
int avc_ca_get_mmi(struct firedtv *fdtv, char *mmi_object, unsigned int *len);

#endif /* _AVC_API_H */
