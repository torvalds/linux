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
#ifndef __DIVA_EICON_TRACE_IDI_IFC_H__
#define __DIVA_EICON_TRACE_IDI_IFC_H__

void* SuperTraceOpenAdapter   (int AdapterNumber);
int   SuperTraceCloseAdapter  (void* AdapterHandle);
int   SuperTraceWrite         (void* AdapterHandle,
                               const void* data, int length);
int   SuperTraceReadRequest   (void* AdapterHandle,const char* name,byte* data);
int   SuperTraceGetNumberOfChannels (void* AdapterHandle);
int   SuperTraceASSIGN        (void* AdapterHandle, byte* data);
int   SuperTraceREMOVE        (void* AdapterHandle);
int   SuperTraceTraceOnRequest(void* hAdapter, const char* name, byte* data);
int   SuperTraceWriteVar (void* AdapterHandle,
												byte* data,
										 		const char* name,
										 		void* var,
										 		byte type,
										 		byte var_length);
int   SuperTraceExecuteRequest (void* AdapterHandle,
																const char* name,
																byte* data);

typedef struct _diva_strace_path2action {
	char               path[64]; /* Full path to variable            */
	void*							 variable; /* Variable that will receive value */
} diva_strace_path2action_t;

#define DIVA_MAX_MANAGEMENT_TRANSFER_SIZE 4096

typedef struct _diva_strace_context {
	diva_strace_library_interface_t	instance;

	int   Adapter;
	void* hAdapter;

	int Channels;
	int	req_busy;

  ENTITY   e;
  IDI_CALL request;
  BUFFERS  XData;
  BUFFERS  RData;
	byte buffer[DIVA_MAX_MANAGEMENT_TRANSFER_SIZE + 1];
  int removal_state;
  int general_b_ch_event;
  int general_fax_event;
  int general_mdm_event;

	byte	rc_ok;

	/*
		Initialization request state machine
		*/
	int ChannelsTraceActive;
	int ModemTraceActive;
	int FaxTraceActive;
	int IncomingCallsCallsActive;
	int IncomingCallsConnectedActive;
	int OutgoingCallsCallsActive;
	int OutgoingCallsConnectedActive;

	int trace_mask_init;
	int audio_trace_init;
	int bchannel_init;
	int trace_length_init;
	int	trace_on;
	int trace_events_down;
	int l1_trace;
	int l2_trace;

	/*
		Trace\Event Enable
		*/
	word trace_event_mask;
	word current_trace_event_mask;

	dword audio_tap_mask;
	dword current_audio_tap_mask;
	dword current_eye_pattern_mask;
	int   audio_tap_pending;
	int   eye_pattern_pending;

	dword bchannel_trace_mask;
	dword current_bchannel_trace_mask;


	diva_trace_line_state_t lines[30];

	int	parse_entries;
	int	cur_parse_entry;
	diva_strace_path2action_t* parse_table;

	diva_trace_library_user_interface_t user_proc_table;

	int line_parse_entry_first[30];
	int line_parse_entry_last[30];

	int modem_parse_entry_first[30];
	int modem_parse_entry_last[30];

	int fax_parse_entry_first[30];
	int fax_parse_entry_last[30];

	int statistic_parse_first;
	int statistic_parse_last;

	int mdm_statistic_parse_first;
	int mdm_statistic_parse_last;

	int fax_statistic_parse_first;
	int fax_statistic_parse_last;

	dword	line_init_event;
	dword	modem_init_event;
	dword	fax_init_event;

	dword	pending_line_status;
	dword	pending_modem_status;
	dword	pending_fax_status;

	dword clear_call_command;

	int outgoing_ifc_stats;
	int incoming_ifc_stats;
	int modem_ifc_stats;
	int fax_ifc_stats;
	int b1_ifc_stats;
	int b2_ifc_stats;
	int d1_ifc_stats;
	int d2_ifc_stats;

	diva_trace_interface_state_t Interface;
	diva_ifc_statistics_t				 InterfaceStat;
} diva_strace_context_t;

typedef struct _diva_man_var_header {
	byte   escape;
	byte   length;
	byte   management_id;
	byte   type;
	byte   attribute;
	byte   status;
	byte   value_length;
	byte	 path_length;
} diva_man_var_header_t;

#endif

