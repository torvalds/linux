/*
 *
  Copyright (c) Eicon Networks, 2000.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    1.9
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __DIVA_EICON_TRACE_API__
#define __DIVA_EICON_TRACE_API__

#define DIVA_TRACE_LINE_TYPE_LEN 64
#define DIVA_TRACE_IE_LEN        64
#define DIVA_TRACE_FAX_PRMS_LEN  128

typedef struct _diva_trace_ie {
	byte length;
	byte data[DIVA_TRACE_IE_LEN];
} diva_trace_ie_t;

/*
	Structure used to represent "State\\BX\\Modem" directory
	to user.
	*/
typedef struct _diva_trace_modem_state {
	dword	ChannelNumber;

	dword	Event;

	dword	Norm;

	dword Options; /* Options received from Application */

	dword	TxSpeed;
	dword	RxSpeed;

	dword RoundtripMsec;

	dword SymbolRate;

	int		RxLeveldBm;
	int		EchoLeveldBm;

	dword	SNRdb;
	dword MAE;

	dword LocalRetrains;
	dword RemoteRetrains;
	dword LocalResyncs;
	dword RemoteResyncs;

	dword DiscReason;

} diva_trace_modem_state_t;

/*
	Representation of "State\\BX\\FAX" directory
	*/
typedef struct _diva_trace_fax_state {
	dword	ChannelNumber;
	dword Event;
	dword Page_Counter;
	dword Features;
	char Station_ID[DIVA_TRACE_FAX_PRMS_LEN];
	char Subaddress[DIVA_TRACE_FAX_PRMS_LEN];
	char Password[DIVA_TRACE_FAX_PRMS_LEN];
	dword Speed;
	dword Resolution;
	dword Paper_Width;
	dword Paper_Length;
	dword Scanline_Time;
	dword Disc_Reason;
	dword	dummy;
} diva_trace_fax_state_t;

/*
	Structure used to represent Interface State in the abstract
	and interface/D-channel protocol independent form.
	*/
typedef struct _diva_trace_interface_state {
	char Layer1[DIVA_TRACE_LINE_TYPE_LEN];
	char Layer2[DIVA_TRACE_LINE_TYPE_LEN];
} diva_trace_interface_state_t;

typedef struct _diva_incoming_call_statistics {
	dword Calls;
	dword Connected;
	dword User_Busy;
	dword Call_Rejected;
	dword Wrong_Number;
	dword Incompatible_Dst;
	dword Out_of_Order;
	dword Ignored;
} diva_incoming_call_statistics_t;

typedef struct _diva_outgoing_call_statistics {
	dword Calls;
	dword Connected;
	dword User_Busy;
	dword No_Answer;
	dword Wrong_Number;
	dword Call_Rejected;
	dword Other_Failures;
} diva_outgoing_call_statistics_t;

typedef struct _diva_modem_call_statistics {
	dword Disc_Normal;
	dword Disc_Unspecified;
	dword Disc_Busy_Tone;
	dword Disc_Congestion;
	dword Disc_Carr_Wait;
	dword Disc_Trn_Timeout;
	dword Disc_Incompat;
	dword Disc_Frame_Rej;
	dword Disc_V42bis;
} diva_modem_call_statistics_t;

typedef struct _diva_fax_call_statistics {
	dword Disc_Normal;
	dword Disc_Not_Ident;
	dword Disc_No_Response;
	dword Disc_Retries;
	dword Disc_Unexp_Msg;
	dword Disc_No_Polling;
	dword Disc_Training;
	dword Disc_Unexpected;
	dword Disc_Application;
	dword Disc_Incompat;
	dword Disc_No_Command;
	dword Disc_Long_Msg;
	dword Disc_Supervisor;
	dword Disc_SUB_SEP_PWD;
	dword Disc_Invalid_Msg;
	dword Disc_Page_Coding;
	dword Disc_App_Timeout;
	dword Disc_Unspecified;
} diva_fax_call_statistics_t;

typedef struct _diva_prot_statistics {
	dword X_Frames;
	dword X_Bytes;
	dword X_Errors;
	dword R_Frames;
	dword R_Bytes;
	dword R_Errors;
} diva_prot_statistics_t;

typedef struct _diva_ifc_statistics {
	diva_incoming_call_statistics_t	inc;
	diva_outgoing_call_statistics_t outg;
	diva_modem_call_statistics_t		mdm;
	diva_fax_call_statistics_t			fax;
	diva_prot_statistics_t					b1;
	diva_prot_statistics_t					b2;
	diva_prot_statistics_t					d1;
	diva_prot_statistics_t					d2;
} diva_ifc_statistics_t;

/*
	Structure used to represent "State\\BX" directory
	to user.
	*/
typedef struct _diva_trace_line_state {
	dword	ChannelNumber;

	char Line[DIVA_TRACE_LINE_TYPE_LEN];

	char Framing[DIVA_TRACE_LINE_TYPE_LEN];

	char Layer2[DIVA_TRACE_LINE_TYPE_LEN];
	char Layer3[DIVA_TRACE_LINE_TYPE_LEN];

	char RemoteAddress[DIVA_TRACE_LINE_TYPE_LEN];
	char RemoteSubAddress[DIVA_TRACE_LINE_TYPE_LEN];

	char LocalAddress[DIVA_TRACE_LINE_TYPE_LEN];
	char LocalSubAddress[DIVA_TRACE_LINE_TYPE_LEN];

	diva_trace_ie_t	call_BC;
	diva_trace_ie_t	call_HLC;
	diva_trace_ie_t	call_LLC;

	dword Charges;

	dword CallReference;

	dword LastDisconnecCause;

	char UserID[DIVA_TRACE_LINE_TYPE_LEN];

	diva_trace_modem_state_t modem;
	diva_trace_fax_state_t   fax;

	diva_trace_interface_state_t* pInterface;

	diva_ifc_statistics_t*				pInterfaceStat;

} diva_trace_line_state_t;

#define DIVA_SUPER_TRACE_NOTIFY_LINE_CHANGE             ('l')
#define DIVA_SUPER_TRACE_NOTIFY_MODEM_CHANGE            ('m')
#define DIVA_SUPER_TRACE_NOTIFY_FAX_CHANGE              ('f')
#define DIVA_SUPER_TRACE_INTERFACE_CHANGE               ('i')
#define DIVA_SUPER_TRACE_NOTIFY_STAT_CHANGE             ('s')
#define DIVA_SUPER_TRACE_NOTIFY_MDM_STAT_CHANGE         ('M')
#define DIVA_SUPER_TRACE_NOTIFY_FAX_STAT_CHANGE         ('F')

struct _diva_strace_library_interface;
typedef void (*diva_trace_channel_state_change_proc_t)(void* user_context,
							struct _diva_strace_library_interface* hLib,
							int Adapter,
							diva_trace_line_state_t* channel, int notify_subject);
typedef void (*diva_trace_channel_trace_proc_t)(void* user_context,
							struct _diva_strace_library_interface* hLib,
							int Adapter, void* xlog_buffer, int length);
typedef void (*diva_trace_error_proc_t)(void* user_context,
							struct _diva_strace_library_interface* hLib,
							int Adapter,
							int error, const char* file, int line);

/*
	This structure creates interface from user to library
	*/
typedef struct _diva_trace_library_user_interface {
	void*																		user_context;
	diva_trace_channel_state_change_proc_t	notify_proc;
	diva_trace_channel_trace_proc_t					trace_proc;
	diva_trace_error_proc_t									error_notify_proc;
} diva_trace_library_user_interface_t;

/*
	Interface from Library to User
	*/
typedef int   (*DivaSTraceLibraryStart_proc_t)(void* hLib);
typedef int   (*DivaSTraceLibraryFinit_proc_t)(void* hLib);
typedef int   (*DivaSTraceMessageInput_proc_t)(void* hLib);
typedef void*	(*DivaSTraceGetHandle_proc_t)(void* hLib);

/*
	Turn Audio Tap trace on/off
	Channel should be in the range 1 ... Number of Channels
	*/
typedef int (*DivaSTraceSetAudioTap_proc_t)(void* hLib, int Channel, int on);

/*
	Turn B-channel trace on/off
	Channel should be in the range 1 ... Number of Channels
	*/
typedef int (*DivaSTraceSetBChannel_proc_t)(void* hLib, int Channel, int on);

/*
	Turn	D-channel (Layer1/Layer2/Layer3) trace on/off
		Layer1 - All D-channel frames received/sent over the interface
						 inclusive Layer 2 headers, Layer 2 frames and TEI management frames
		Layer2 - Events from LAPD protocol instance with SAPI of signalling protocol
		Layer3 - All D-channel frames addressed to assigned to the card TEI and
						 SAPI of signalling protocol, and signalling protocol events.
	*/
typedef int (*DivaSTraceSetDChannel_proc_t)(void* hLib, int on);

/*
	Get overall card statistics
	*/
typedef int (*DivaSTraceGetOutgoingCallStatistics_proc_t)(void* hLib);
typedef int (*DivaSTraceGetIncomingCallStatistics_proc_t)(void* hLib);
typedef int (*DivaSTraceGetModemStatistics_proc_t)(void* hLib);
typedef int (*DivaSTraceGetFaxStatistics_proc_t)(void* hLib);
typedef int (*DivaSTraceGetBLayer1Statistics_proc_t)(void* hLib);
typedef int (*DivaSTraceGetBLayer2Statistics_proc_t)(void* hLib);
typedef int (*DivaSTraceGetDLayer1Statistics_proc_t)(void* hLib);
typedef int (*DivaSTraceGetDLayer2Statistics_proc_t)(void* hLib);

/*
	Call control
	*/
typedef int (*DivaSTraceClearCall_proc_t)(void* hLib, int Channel);

typedef struct _diva_strace_library_interface {
	void* hLib;
  DivaSTraceLibraryStart_proc_t DivaSTraceLibraryStart;
  DivaSTraceLibraryStart_proc_t DivaSTraceLibraryStop;
	DivaSTraceLibraryFinit_proc_t DivaSTraceLibraryFinit;
	DivaSTraceMessageInput_proc_t DivaSTraceMessageInput;
	DivaSTraceGetHandle_proc_t    DivaSTraceGetHandle;
	DivaSTraceSetAudioTap_proc_t  DivaSTraceSetAudioTap;
	DivaSTraceSetBChannel_proc_t  DivaSTraceSetBChannel;
	DivaSTraceSetDChannel_proc_t  DivaSTraceSetDChannel;
	DivaSTraceSetDChannel_proc_t  DivaSTraceSetInfo;
	DivaSTraceGetOutgoingCallStatistics_proc_t \
																DivaSTraceGetOutgoingCallStatistics;
	DivaSTraceGetIncomingCallStatistics_proc_t \
																DivaSTraceGetIncomingCallStatistics;
	DivaSTraceGetModemStatistics_proc_t \
																DivaSTraceGetModemStatistics;
	DivaSTraceGetFaxStatistics_proc_t \
																DivaSTraceGetFaxStatistics;
	DivaSTraceGetBLayer1Statistics_proc_t \
																DivaSTraceGetBLayer1Statistics;
	DivaSTraceGetBLayer2Statistics_proc_t \
																DivaSTraceGetBLayer2Statistics;
	DivaSTraceGetDLayer1Statistics_proc_t \
																DivaSTraceGetDLayer1Statistics;
	DivaSTraceGetDLayer2Statistics_proc_t \
																DivaSTraceGetDLayer2Statistics;
	DivaSTraceClearCall_proc_t    DivaSTraceClearCall;
} diva_strace_library_interface_t;

/*
	Create and return Library interface
	*/
diva_strace_library_interface_t* DivaSTraceLibraryCreateInstance (int Adapter,
													const diva_trace_library_user_interface_t* user_proc,
                          byte* pmem);
dword DivaSTraceGetMemotyRequirement (int channels);

#define DIVA_MAX_ADAPTERS  64
#define DIVA_MAX_LINES     32

#endif

