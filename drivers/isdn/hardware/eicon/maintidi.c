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
#include "platform.h"
#include "kst_ifc.h"
#include "di_defs.h"
#include "maintidi.h"
#include "pc.h"
#include "man_defs.h"


extern void diva_mnt_internal_dprintf (dword drv_id, dword type, char* p, ...);

#define MODEM_PARSE_ENTRIES  16 /* amount of variables of interest */
#define FAX_PARSE_ENTRIES    12 /* amount of variables of interest */
#define LINE_PARSE_ENTRIES   15 /* amount of variables of interest */
#define STAT_PARSE_ENTRIES   70 /* amount of variables of interest */

/*
	LOCAL FUNCTIONS
	*/
static int DivaSTraceLibraryStart (void* hLib);
static int DivaSTraceLibraryStop  (void* hLib);
static int SuperTraceLibraryFinit (void* hLib);
static void*	SuperTraceGetHandle (void* hLib);
static int SuperTraceMessageInput (void* hLib);
static int SuperTraceSetAudioTap  (void* hLib, int Channel, int on);
static int SuperTraceSetBChannel  (void* hLib, int Channel, int on);
static int SuperTraceSetDChannel  (void* hLib, int on);
static int SuperTraceSetInfo      (void* hLib, int on);
static int SuperTraceClearCall (void* hLib, int Channel);
static int SuperTraceGetOutgoingCallStatistics (void* hLib);
static int SuperTraceGetIncomingCallStatistics (void* hLib);
static int SuperTraceGetModemStatistics (void* hLib);
static int SuperTraceGetFaxStatistics (void* hLib);
static int SuperTraceGetBLayer1Statistics (void* hLib);
static int SuperTraceGetBLayer2Statistics (void* hLib);
static int SuperTraceGetDLayer1Statistics (void* hLib);
static int SuperTraceGetDLayer2Statistics (void* hLib);

/*
	LOCAL FUNCTIONS
	*/
static int ScheduleNextTraceRequest (diva_strace_context_t* pLib);
static int process_idi_event (diva_strace_context_t* pLib,
															diva_man_var_header_t* pVar);
static int process_idi_info  (diva_strace_context_t* pLib,
															diva_man_var_header_t* pVar);
static int diva_modem_event (diva_strace_context_t* pLib, int Channel);
static int diva_fax_event   (diva_strace_context_t* pLib, int Channel);
static int diva_line_event (diva_strace_context_t* pLib, int Channel);
static int diva_modem_info (diva_strace_context_t* pLib,
														int Channel,
														diva_man_var_header_t* pVar);
static int diva_fax_info   (diva_strace_context_t* pLib,
														int Channel,
														diva_man_var_header_t* pVar);
static int diva_line_info  (diva_strace_context_t* pLib,
														int Channel,
														diva_man_var_header_t* pVar);
static int diva_ifc_statistics (diva_strace_context_t* pLib,
																diva_man_var_header_t* pVar);
static diva_man_var_header_t* get_next_var (diva_man_var_header_t* pVar);
static diva_man_var_header_t* find_var (diva_man_var_header_t* pVar,
																				const char* name);
static int diva_strace_read_int  (diva_man_var_header_t* pVar, int* var);
static int diva_strace_read_uint (diva_man_var_header_t* pVar, dword* var);
static int diva_strace_read_asz  (diva_man_var_header_t* pVar, char* var);
static int diva_strace_read_asc  (diva_man_var_header_t* pVar, char* var);
static int  diva_strace_read_ie  (diva_man_var_header_t* pVar,
																	diva_trace_ie_t* var);
static void diva_create_parse_table (diva_strace_context_t* pLib);
static void diva_trace_error (diva_strace_context_t* pLib,
															int error, const char* file, int line);
static void diva_trace_notify_user (diva_strace_context_t* pLib,
														 int Channel,
														 int notify_subject);
static int diva_trace_read_variable (diva_man_var_header_t* pVar,
																		 void* variable);

/*
	Initialize the library and return context
	of the created trace object that will represent
	the IDI adapter.
	Return 0 on error.
	*/
diva_strace_library_interface_t* DivaSTraceLibraryCreateInstance (int Adapter,
											const diva_trace_library_user_interface_t* user_proc,
                      byte* pmem) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)pmem;
	int i;

	if (!pLib) {
		return NULL;
	}

	pmem += sizeof(*pLib);
	memset(pLib, 0x00, sizeof(*pLib));

	pLib->Adapter  = Adapter;

	/*
		Set up Library Interface
		*/
	pLib->instance.hLib                                = pLib;
  pLib->instance.DivaSTraceLibraryStart              = DivaSTraceLibraryStart;
  pLib->instance.DivaSTraceLibraryStop               = DivaSTraceLibraryStop;
	pLib->instance.DivaSTraceLibraryFinit              = SuperTraceLibraryFinit;
	pLib->instance.DivaSTraceMessageInput              = SuperTraceMessageInput;
	pLib->instance.DivaSTraceGetHandle                 = SuperTraceGetHandle;
	pLib->instance.DivaSTraceSetAudioTap               = SuperTraceSetAudioTap;
	pLib->instance.DivaSTraceSetBChannel               = SuperTraceSetBChannel;
	pLib->instance.DivaSTraceSetDChannel               = SuperTraceSetDChannel;
	pLib->instance.DivaSTraceSetInfo                   = SuperTraceSetInfo;
	pLib->instance.DivaSTraceGetOutgoingCallStatistics = \
																			SuperTraceGetOutgoingCallStatistics;
	pLib->instance.DivaSTraceGetIncomingCallStatistics = \
																			SuperTraceGetIncomingCallStatistics;
	pLib->instance.DivaSTraceGetModemStatistics        = \
																			SuperTraceGetModemStatistics;
	pLib->instance.DivaSTraceGetFaxStatistics          = \
																			SuperTraceGetFaxStatistics;
	pLib->instance.DivaSTraceGetBLayer1Statistics      = \
																			SuperTraceGetBLayer1Statistics;
	pLib->instance.DivaSTraceGetBLayer2Statistics      = \
																			SuperTraceGetBLayer2Statistics;
	pLib->instance.DivaSTraceGetDLayer1Statistics      = \
																			SuperTraceGetDLayer1Statistics;
	pLib->instance.DivaSTraceGetDLayer2Statistics      = \
																			SuperTraceGetDLayer2Statistics;
	pLib->instance.DivaSTraceClearCall                 = SuperTraceClearCall;


	if (user_proc) {
		pLib->user_proc_table.user_context      = user_proc->user_context;
		pLib->user_proc_table.notify_proc       = user_proc->notify_proc;
		pLib->user_proc_table.trace_proc        = user_proc->trace_proc;
		pLib->user_proc_table.error_notify_proc = user_proc->error_notify_proc;
	}

	if (!(pLib->hAdapter = SuperTraceOpenAdapter (Adapter))) {
    diva_mnt_internal_dprintf (0, DLI_ERR, "Can not open XDI adapter");
		return NULL;
	}
	pLib->Channels = SuperTraceGetNumberOfChannels (pLib->hAdapter);

	/*
		Calculate amount of parte table entites necessary to translate
		information from all events of onterest
		*/
	pLib->parse_entries = (MODEM_PARSE_ENTRIES + FAX_PARSE_ENTRIES + \
												 STAT_PARSE_ENTRIES + \
												 LINE_PARSE_ENTRIES + 1) * pLib->Channels;
	pLib->parse_table = (diva_strace_path2action_t*)pmem;

	for (i = 0; i < 30; i++) {
		pLib->lines[i].pInterface     = &pLib->Interface;
		pLib->lines[i].pInterfaceStat = &pLib->InterfaceStat;
	}

  pLib->e.R = &pLib->RData;

	pLib->req_busy = 1;
	pLib->rc_ok    = ASSIGN_OK;

	diva_create_parse_table (pLib);

	return ((diva_strace_library_interface_t*)pLib);
}

static int DivaSTraceLibraryStart (void* hLib) {
  diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;

  return (SuperTraceASSIGN (pLib->hAdapter, pLib->buffer));
}

/*
  Return (-1) on error
  Return (0) if was initiated or pending
  Return (1) if removal is complete
  */
static int DivaSTraceLibraryStop  (void* hLib) {
  diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;

  if (!pLib->e.Id) { /* Was never started/assigned */
    return (1);
  }

  switch (pLib->removal_state) {
    case 0:
      pLib->removal_state = 1;
      ScheduleNextTraceRequest(pLib);
      break;

    case 3:
      return (1);
  }

  return (0);
}

static int SuperTraceLibraryFinit (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	if (pLib) {
		if (pLib->hAdapter) {
			SuperTraceCloseAdapter  (pLib->hAdapter);
		}
		return (0);
	}
	return (-1);
}

static void*	SuperTraceGetHandle (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;

  return (&pLib->e);
}

/*
	After library handle object is gone in signaled state
	this function should be called and will pick up incoming
	IDI messages (return codes and indications).
	*/
static int SuperTraceMessageInput (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	int ret = 0;
  byte Rc, Ind;

  if (pLib->e.complete == 255) {
    /*
      Process return code
      */
    pLib->req_busy = 0;
    Rc             = pLib->e.Rc;
    pLib->e.Rc     = 0;

    if (pLib->removal_state == 2) {
      pLib->removal_state = 3;
      return (0);
    }

		if (Rc != pLib->rc_ok) {
      int ignore = 0;
      /*
        Auto-detect amount of events/channels and features
        */
      if (pLib->general_b_ch_event == 1) {
        pLib->general_b_ch_event = 2;
        ignore = 1;
      } else if (pLib->general_fax_event == 1) {
        pLib->general_fax_event = 2;
        ignore = 1;
      } else if (pLib->general_mdm_event == 1) {
        pLib->general_mdm_event = 2;
        ignore = 1;
      } else if ((pLib->ChannelsTraceActive < pLib->Channels) && pLib->ChannelsTraceActive) {
        pLib->ChannelsTraceActive = pLib->Channels;
        ignore = 1;
      } else if (pLib->ModemTraceActive < pLib->Channels) {
        pLib->ModemTraceActive = pLib->Channels;
        ignore = 1;
      } else if (pLib->FaxTraceActive < pLib->Channels) {
        pLib->FaxTraceActive = pLib->Channels;
        ignore = 1;
      } else if (pLib->audio_trace_init == 2) {
        ignore = 1;
        pLib->audio_trace_init = 1;
      } else if (pLib->eye_pattern_pending) {
				pLib->eye_pattern_pending =  0;
				ignore = 1;
			} else if (pLib->audio_tap_pending) {
				pLib->audio_tap_pending = 0;
				ignore = 1;
      }

      if (!ignore) {
        return (-1); /* request failed */
      }
    } else {
      if (pLib->general_b_ch_event == 1) {
        pLib->ChannelsTraceActive = pLib->Channels;
        pLib->general_b_ch_event = 2;
      } else if (pLib->general_fax_event == 1) {
        pLib->general_fax_event = 2;
        pLib->FaxTraceActive = pLib->Channels;
      } else if (pLib->general_mdm_event == 1) {
        pLib->general_mdm_event = 2;
        pLib->ModemTraceActive = pLib->Channels;
      }
    }
    if (pLib->audio_trace_init == 2) {
      pLib->audio_trace_init = 1;
    }
    pLib->rc_ok = 0xff; /* default OK after assign was done */
    if ((ret = ScheduleNextTraceRequest(pLib))) {
      return (-1);
    }
  } else {
    /*
      Process indication
      Always 'RNR' indication if return code is pending
      */
    Ind         = pLib->e.Ind;
    pLib->e.Ind = 0;
    if (pLib->removal_state) {
      pLib->e.RNum	= 0;
      pLib->e.RNR	= 2;
    } else if (pLib->req_busy) {
      pLib->e.RNum	= 0;
      pLib->e.RNR	= 1;
    } else {
      if (pLib->e.complete != 0x02) {
        /*
          Look-ahead call, set up buffers
          */
        pLib->e.RNum       = 1;
        pLib->e.R->P       = (byte*)&pLib->buffer[0];
        pLib->e.R->PLength = (word)(sizeof(pLib->buffer) - 1);

      } else {
        /*
          Indication reception complete, process it now
          */
        byte* p = (byte*)&pLib->buffer[0];
        pLib->buffer[pLib->e.R->PLength] = 0; /* terminate I.E. with zero */

        switch (Ind) {
          case MAN_COMBI_IND: {
            int total_length    = pLib->e.R->PLength;
            word  this_ind_length;

            while (total_length > 3 && *p) {
              Ind = *p++;
              this_ind_length = (word)p[0] | ((word)p[1] << 8);
              p += 2;

              switch (Ind) {
                case MAN_INFO_IND:
                  if (process_idi_info (pLib, (diva_man_var_header_t*)p)) {
                    return (-1);
                  }
                  break;
      					case MAN_EVENT_IND:
                  if (process_idi_event (pLib, (diva_man_var_header_t*)p)) {
                    return (-1);
                  }
                  break;
                case MAN_TRACE_IND:
                  if (pLib->trace_on == 1) {
                    /*
                      Ignore first trace event that is result of
                      EVENT_ON operation
                    */
                    pLib->trace_on++;
                  } else {
                    /*
                      Delivery XLOG buffer to application
                      */
                    if (pLib->user_proc_table.trace_proc) {
                      (*(pLib->user_proc_table.trace_proc))(pLib->user_proc_table.user_context,
                                                            &pLib->instance, pLib->Adapter,
                                                            p, this_ind_length);
                    }
                  }
                  break;
                default:
                  diva_mnt_internal_dprintf (0, DLI_ERR, "Unknon IDI Ind (DMA mode): %02x", Ind);
              }
              p += (this_ind_length+1);
              total_length -= (4 + this_ind_length);
            }
          } break;
          case MAN_INFO_IND:
            if (process_idi_info (pLib, (diva_man_var_header_t*)p)) {
              return (-1);
            }
            break;
					case MAN_EVENT_IND:
            if (process_idi_event (pLib, (diva_man_var_header_t*)p)) {
              return (-1);
            }
            break;
          case MAN_TRACE_IND:
            if (pLib->trace_on == 1) {
              /*
                Ignore first trace event that is result of
                EVENT_ON operation
              */
              pLib->trace_on++;
            } else {
              /*
                Delivery XLOG buffer to application
                */
              if (pLib->user_proc_table.trace_proc) {
                (*(pLib->user_proc_table.trace_proc))(pLib->user_proc_table.user_context,
                                                      &pLib->instance, pLib->Adapter,
                                                      p, pLib->e.R->PLength);
              }
            }
            break;
          default:
            diva_mnt_internal_dprintf (0, DLI_ERR, "Unknon IDI Ind: %02x", Ind);
        }
      }
    }
  }

	if ((ret = ScheduleNextTraceRequest(pLib))) {
		return (-1);
	}

	return (ret);
}

/*
	Internal state machine responsible for scheduling of requests
	*/
static int ScheduleNextTraceRequest (diva_strace_context_t* pLib) {
	char name[64];
	int ret = 0;
	int i;

	if (pLib->req_busy) {
		return (0);
	}

  if (pLib->removal_state == 1) {
		if (SuperTraceREMOVE (pLib->hAdapter)) {
      pLib->removal_state = 3;
    } else {
      pLib->req_busy = 1;
      pLib->removal_state = 2;
    }
    return (0);
  }

  if (pLib->removal_state) {
    return (0);
  }

  if (!pLib->general_b_ch_event) {
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, "State\\B Event", pLib->buffer))) {
      return (-1);
    }
    pLib->general_b_ch_event = 1;
		pLib->req_busy = 1;
		return (0);
  }

  if (!pLib->general_fax_event) {
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, "State\\FAX Event", pLib->buffer))) {
      return (-1);
    }
    pLib->general_fax_event = 1;
		pLib->req_busy = 1;
		return (0);
  }

  if (!pLib->general_mdm_event) {
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, "State\\Modem Event", pLib->buffer))) {
      return (-1);
    }
    pLib->general_mdm_event = 1;
		pLib->req_busy = 1;
		return (0);
  }

	if (pLib->ChannelsTraceActive < pLib->Channels) {
		pLib->ChannelsTraceActive++;
		sprintf (name, "State\\B%d\\Line", pLib->ChannelsTraceActive);
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, name, pLib->buffer))) {
			pLib->ChannelsTraceActive--;
			return (-1);
		}
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->ModemTraceActive < pLib->Channels) {
		pLib->ModemTraceActive++;
		sprintf (name, "State\\B%d\\Modem\\Event", pLib->ModemTraceActive);
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, name, pLib->buffer))) {
			pLib->ModemTraceActive--;
			return (-1);
		}
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->FaxTraceActive < pLib->Channels) {
		pLib->FaxTraceActive++;
		sprintf (name, "State\\B%d\\FAX\\Event", pLib->FaxTraceActive);
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, name, pLib->buffer))) {
			pLib->FaxTraceActive--;
			return (-1);
		}
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->trace_mask_init) {
		word tmp = 0x0000;
		if (SuperTraceWriteVar (pLib->hAdapter,
														pLib->buffer,
												 		"Trace\\Event Enable",
												 		&tmp,
												 		0x87, /* MI_BITFLD */
												 		sizeof(tmp))) {
			return (-1);
		}
		pLib->trace_mask_init = 1;
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->audio_trace_init) {
		dword tmp = 0x00000000;
		if (SuperTraceWriteVar (pLib->hAdapter,
														pLib->buffer,
												 		"Trace\\AudioCh# Enable",
												 		&tmp,
												 		0x87, /* MI_BITFLD */
												 		sizeof(tmp))) {
			return (-1);
		}
		pLib->audio_trace_init = 2;
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->bchannel_init) {
		dword tmp = 0x00000000;
		if (SuperTraceWriteVar (pLib->hAdapter,
														pLib->buffer,
												 		"Trace\\B-Ch# Enable",
												 		&tmp,
												 		0x87, /* MI_BITFLD */
												 		sizeof(tmp))) {
			return (-1);
		}
		pLib->bchannel_init = 1;
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->trace_length_init) {
		word tmp = 30;
		if (SuperTraceWriteVar (pLib->hAdapter,
														pLib->buffer,
												 		"Trace\\Max Log Length",
												 		&tmp,
														0x82, /* MI_UINT */
												 		sizeof(tmp))) {
			return (-1);
		}
		pLib->trace_length_init = 1;
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->trace_on) {
		if (SuperTraceTraceOnRequest (pLib->hAdapter,
																	"Trace\\Log Buffer",
																	pLib->buffer)) {
			return (-1);
		}
		pLib->trace_on = 1;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->trace_event_mask != pLib->current_trace_event_mask) {
		if (SuperTraceWriteVar (pLib->hAdapter,
														pLib->buffer,
												 		"Trace\\Event Enable",
												 		&pLib->trace_event_mask,
												 		0x87, /* MI_BITFLD */
												 		sizeof(pLib->trace_event_mask))) {
			return (-1);
		}
		pLib->current_trace_event_mask = pLib->trace_event_mask;
		pLib->req_busy = 1;
		return (0);
	}

	if ((pLib->audio_tap_pending >= 0) && (pLib->audio_tap_mask != pLib->current_audio_tap_mask)) {
		if (SuperTraceWriteVar (pLib->hAdapter,
														pLib->buffer,
												 		"Trace\\AudioCh# Enable",
												 		&pLib->audio_tap_mask,
												 		0x87, /* MI_BITFLD */
												 		sizeof(pLib->audio_tap_mask))) {
			return (-1);
		}
		pLib->current_audio_tap_mask = pLib->audio_tap_mask;
		pLib->audio_tap_pending = 1;
		pLib->req_busy = 1;
		return (0);
	}

	if ((pLib->eye_pattern_pending >= 0) && (pLib->audio_tap_mask != pLib->current_eye_pattern_mask)) {
		if (SuperTraceWriteVar (pLib->hAdapter,
														pLib->buffer,
												 		"Trace\\EyeCh# Enable",
												 		&pLib->audio_tap_mask,
												 		0x87, /* MI_BITFLD */
												 		sizeof(pLib->audio_tap_mask))) {
			return (-1);
		}
		pLib->current_eye_pattern_mask = pLib->audio_tap_mask;
		pLib->eye_pattern_pending = 1;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->bchannel_trace_mask != pLib->current_bchannel_trace_mask) {
		if (SuperTraceWriteVar (pLib->hAdapter,
														pLib->buffer,
												 		"Trace\\B-Ch# Enable",
												 		&pLib->bchannel_trace_mask,
												 		0x87, /* MI_BITFLD */
												 		sizeof(pLib->bchannel_trace_mask))) {
			return (-1);
		}
		pLib->current_bchannel_trace_mask = pLib->bchannel_trace_mask;
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->trace_events_down) {
		if (SuperTraceTraceOnRequest (pLib->hAdapter,
																	"Events Down",
																	pLib->buffer)) {
			return (-1);
		}
		pLib->trace_events_down = 1;
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->l1_trace) {
		if (SuperTraceTraceOnRequest (pLib->hAdapter,
																	"State\\Layer1",
																	pLib->buffer)) {
			return (-1);
		}
		pLib->l1_trace = 1;
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->l2_trace) {
		if (SuperTraceTraceOnRequest (pLib->hAdapter,
																	"State\\Layer2 No1",
																	pLib->buffer)) {
			return (-1);
		}
		pLib->l2_trace = 1;
		pLib->req_busy = 1;
		return (0);
	}

	for (i = 0; i < 30; i++) {
		if (pLib->pending_line_status & (1L << i)) {
			sprintf (name, "State\\B%d", i+1);
			if (SuperTraceReadRequest (pLib->hAdapter, name, pLib->buffer)) {
				return (-1);
			}
			pLib->pending_line_status &= ~(1L << i);
			pLib->req_busy = 1;
			return (0);
		}
		if (pLib->pending_modem_status & (1L << i)) {
			sprintf (name, "State\\B%d\\Modem", i+1);
			if (SuperTraceReadRequest (pLib->hAdapter, name, pLib->buffer)) {
				return (-1);
			}
			pLib->pending_modem_status &= ~(1L << i);
			pLib->req_busy = 1;
			return (0);
		}
		if (pLib->pending_fax_status & (1L << i)) {
			sprintf (name, "State\\B%d\\FAX", i+1);
			if (SuperTraceReadRequest (pLib->hAdapter, name, pLib->buffer)) {
				return (-1);
			}
			pLib->pending_fax_status &= ~(1L << i);
			pLib->req_busy = 1;
			return (0);
		}
		if (pLib->clear_call_command & (1L << i)) {
			sprintf (name, "State\\B%d\\Clear Call", i+1);
			if (SuperTraceExecuteRequest (pLib->hAdapter, name, pLib->buffer)) {
				return (-1);
			}
			pLib->clear_call_command &= ~(1L << i);
			pLib->req_busy = 1;
			return (0);
		}
	}

	if (pLib->outgoing_ifc_stats) {
		if (SuperTraceReadRequest (pLib->hAdapter,
															 "Statistics\\Outgoing Calls",
															 pLib->buffer)) {
			return (-1);
		}
		pLib->outgoing_ifc_stats = 0;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->incoming_ifc_stats) {
		if (SuperTraceReadRequest (pLib->hAdapter,
															 "Statistics\\Incoming Calls",
															 pLib->buffer)) {
			return (-1);
		}
		pLib->incoming_ifc_stats = 0;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->modem_ifc_stats) {
		if (SuperTraceReadRequest (pLib->hAdapter,
															 "Statistics\\Modem",
															 pLib->buffer)) {
			return (-1);
		}
		pLib->modem_ifc_stats = 0;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->fax_ifc_stats) {
		if (SuperTraceReadRequest (pLib->hAdapter,
															 "Statistics\\FAX",
															 pLib->buffer)) {
			return (-1);
		}
		pLib->fax_ifc_stats = 0;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->b1_ifc_stats) {
		if (SuperTraceReadRequest (pLib->hAdapter,
															 "Statistics\\B-Layer1",
															 pLib->buffer)) {
			return (-1);
		}
		pLib->b1_ifc_stats = 0;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->b2_ifc_stats) {
		if (SuperTraceReadRequest (pLib->hAdapter,
															 "Statistics\\B-Layer2",
															 pLib->buffer)) {
			return (-1);
		}
		pLib->b2_ifc_stats = 0;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->d1_ifc_stats) {
		if (SuperTraceReadRequest (pLib->hAdapter,
															 "Statistics\\D-Layer1",
															 pLib->buffer)) {
			return (-1);
		}
		pLib->d1_ifc_stats = 0;
		pLib->req_busy = 1;
		return (0);
	}

	if (pLib->d2_ifc_stats) {
		if (SuperTraceReadRequest (pLib->hAdapter,
															 "Statistics\\D-Layer2",
															 pLib->buffer)) {
			return (-1);
		}
		pLib->d2_ifc_stats = 0;
		pLib->req_busy = 1;
		return (0);
	}

	if (!pLib->IncomingCallsCallsActive) {
		pLib->IncomingCallsCallsActive = 1;
		sprintf (name, "%s", "Statistics\\Incoming Calls\\Calls");
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, name, pLib->buffer))) {
			pLib->IncomingCallsCallsActive = 0;
			return (-1);
		}
		pLib->req_busy = 1;
		return (0);
	}
	if (!pLib->IncomingCallsConnectedActive) {
		pLib->IncomingCallsConnectedActive = 1;
		sprintf (name, "%s", "Statistics\\Incoming Calls\\Connected");
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, name, pLib->buffer))) {
			pLib->IncomingCallsConnectedActive = 0;
			return (-1);
		}
		pLib->req_busy = 1;
		return (0);
	}
	if (!pLib->OutgoingCallsCallsActive) {
		pLib->OutgoingCallsCallsActive = 1;
		sprintf (name, "%s", "Statistics\\Outgoing Calls\\Calls");
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, name, pLib->buffer))) {
			pLib->OutgoingCallsCallsActive = 0;
			return (-1);
		}
		pLib->req_busy = 1;
		return (0);
	}
	if (!pLib->OutgoingCallsConnectedActive) {
		pLib->OutgoingCallsConnectedActive = 1;
		sprintf (name, "%s", "Statistics\\Outgoing Calls\\Connected");
		if ((ret = SuperTraceTraceOnRequest(pLib->hAdapter, name, pLib->buffer))) {
			pLib->OutgoingCallsConnectedActive = 0;
			return (-1);
		}
		pLib->req_busy = 1;
		return (0);
	}

	return (0);
}

static int process_idi_event (diva_strace_context_t* pLib,
				diva_man_var_header_t* pVar) {
	const char* path = (char*)&pVar->path_length+1;
	char name[64];
	int i;

	if (!strncmp("State\\B Event", path, pVar->path_length)) {
    dword ch_id;
    if (!diva_trace_read_variable (pVar, &ch_id)) {
      if (!pLib->line_init_event && !pLib->pending_line_status) {
        for (i = 1; i <= pLib->Channels; i++) {
          diva_line_event(pLib, i);
        }
        return (0);
      } else if (ch_id && ch_id <= pLib->Channels) {
        return (diva_line_event(pLib, (int)ch_id));
      }
      return (0);
    }
    return (-1);
  }

	if (!strncmp("State\\FAX Event", path, pVar->path_length)) {
    dword ch_id;
    if (!diva_trace_read_variable (pVar, &ch_id)) {
      if (!pLib->pending_fax_status && !pLib->fax_init_event) {
        for (i = 1; i <= pLib->Channels; i++) {
          diva_fax_event(pLib, i);
        }
        return (0);
      } else if (ch_id && ch_id <= pLib->Channels) {
        return (diva_fax_event(pLib, (int)ch_id));
      }
      return (0);
    }
    return (-1);
  }

	if (!strncmp("State\\Modem Event", path, pVar->path_length)) {
    dword ch_id;
    if (!diva_trace_read_variable (pVar, &ch_id)) {
      if (!pLib->pending_modem_status && !pLib->modem_init_event) {
        for (i = 1; i <= pLib->Channels; i++) {
          diva_modem_event(pLib, i);
        }
        return (0);
      } else if (ch_id && ch_id <= pLib->Channels) {
        return (diva_modem_event(pLib, (int)ch_id));
      }
      return (0);
    }
    return (-1);
  }

	/*
		First look for Line Event
		*/
	for (i = 1; i <= pLib->Channels; i++) {
		sprintf (name, "State\\B%d\\Line", i);
		if (find_var (pVar, name)) {
			return (diva_line_event(pLib, i));
		}
	}

	/*
		Look for Moden Progress Event
		*/
	for (i = 1; i <= pLib->Channels; i++) {
		sprintf (name, "State\\B%d\\Modem\\Event", i);
		if (find_var (pVar, name)) {
			return (diva_modem_event (pLib, i));
		}
	}

	/*
		Look for Fax Event
		*/
	for (i = 1; i <= pLib->Channels; i++) {
		sprintf (name, "State\\B%d\\FAX\\Event", i);
		if (find_var (pVar, name)) {
			return (diva_fax_event (pLib, i));
		}
	}

	/*
		Notification about loss of events
		*/
	if (!strncmp("Events Down", path, pVar->path_length)) {
		if (pLib->trace_events_down == 1) {
			pLib->trace_events_down = 2;
		} else {
			diva_trace_error (pLib, 1, "Events Down", 0);
		}
		return (0);
	}

	if (!strncmp("State\\Layer1", path, pVar->path_length)) {
		diva_strace_read_asz  (pVar, &pLib->lines[0].pInterface->Layer1[0]);
		if (pLib->l1_trace == 1) {
			pLib->l1_trace = 2;
		} else {
			diva_trace_notify_user (pLib, 0, DIVA_SUPER_TRACE_INTERFACE_CHANGE);
		}
		return (0);
	}
	if (!strncmp("State\\Layer2 No1", path, pVar->path_length)) {
		char* tmp = &pLib->lines[0].pInterface->Layer2[0];
    dword l2_state;
    diva_strace_read_uint (pVar, &l2_state);

		switch (l2_state) {
			case 0:
				strcpy (tmp, "Idle");
				break;
			case 1:
				strcpy (tmp, "Layer2 UP");
				break;
			case 2:
				strcpy (tmp, "Layer2 Disconnecting");
				break;
			case 3:
				strcpy (tmp, "Layer2 Connecting");
				break;
			case 4:
				strcpy (tmp, "SPID Initializing");
				break;
			case 5:
				strcpy (tmp, "SPID Initialised");
				break;
			case 6:
				strcpy (tmp, "Layer2 Connecting");
				break;

			case  7:
				strcpy (tmp, "Auto SPID Stopped");
				break;

			case  8:
				strcpy (tmp, "Auto SPID Idle");
				break;

			case  9:
				strcpy (tmp, "Auto SPID Requested");
				break;

			case  10:
				strcpy (tmp, "Auto SPID Delivery");
				break;

			case 11:
				strcpy (tmp, "Auto SPID Complete");
				break;

			default:
				sprintf (tmp, "U:%d", (int)l2_state);
		}
		if (pLib->l2_trace == 1) {
			pLib->l2_trace = 2;
		} else {
			diva_trace_notify_user (pLib, 0, DIVA_SUPER_TRACE_INTERFACE_CHANGE);
		}
		return (0);
	}

	if (!strncmp("Statistics\\Incoming Calls\\Calls", path, pVar->path_length) ||
			!strncmp("Statistics\\Incoming Calls\\Connected", path, pVar->path_length)) {
		return (SuperTraceGetIncomingCallStatistics (pLib));
	}

	if (!strncmp("Statistics\\Outgoing Calls\\Calls", path, pVar->path_length) ||
			!strncmp("Statistics\\Outgoing Calls\\Connected", path, pVar->path_length)) {
		return (SuperTraceGetOutgoingCallStatistics (pLib));
	}

	return (-1);
}

static int diva_line_event (diva_strace_context_t* pLib, int Channel) {
	pLib->pending_line_status |= (1L << (Channel-1));
	return (0);
}

static int diva_modem_event (diva_strace_context_t* pLib, int Channel) {
	pLib->pending_modem_status |= (1L << (Channel-1));
	return (0);
}

static int diva_fax_event (diva_strace_context_t* pLib, int Channel) {
	pLib->pending_fax_status |= (1L << (Channel-1));
	return (0);
}

/*
	Process INFO indications that arrive from the card
	Uses path of first I.E. to detect the source of the
	infication
	*/
static int process_idi_info  (diva_strace_context_t* pLib,
															diva_man_var_header_t* pVar) {
	const char* path = (char*)&pVar->path_length+1;
	char name[64];
	int i, len;

	/*
		First look for Modem Status Info
		*/
	for (i = pLib->Channels; i > 0; i--) {
		len = sprintf (name, "State\\B%d\\Modem", i);
		if (!strncmp(name, path, len)) {
			return (diva_modem_info (pLib, i, pVar));
		}
	}

	/*
		Look for Fax Status Info
		*/
	for (i = pLib->Channels; i > 0; i--) {
		len = sprintf (name, "State\\B%d\\FAX", i);
		if (!strncmp(name, path, len)) {
			return (diva_fax_info (pLib, i, pVar));
		}
	}

	/*
		Look for Line Status Info
		*/
	for (i = pLib->Channels; i > 0; i--) {
		len = sprintf (name, "State\\B%d", i);
		if (!strncmp(name, path, len)) {
			return (diva_line_info (pLib, i, pVar));
		}
	}

	if (!diva_ifc_statistics (pLib, pVar)) {
		return (0);
	}

	return (-1);
}

/*
	MODEM INSTANCE STATE UPDATE

	Update Modem Status Information and issue notification to user,
	that will inform about change in the state of modem instance, that is
	associuated with this channel
	*/
static int diva_modem_info (diva_strace_context_t* pLib,
														int Channel,
														diva_man_var_header_t* pVar) {
	diva_man_var_header_t* cur;
	int i, nr = Channel - 1;

	for (i  = pLib->modem_parse_entry_first[nr];
			 i <= pLib->modem_parse_entry_last[nr]; i++) {
		if ((cur = find_var (pVar, pLib->parse_table[i].path))) {
			if (diva_trace_read_variable (cur, pLib->parse_table[i].variable)) {
				diva_trace_error (pLib, -3 , __FILE__, __LINE__);
				return (-1);
			}
		} else {
			diva_trace_error (pLib, -2 , __FILE__, __LINE__);
			return (-1);
		}
	}

	/*
		We do not use first event to notify user - this is the event that is
		generated as result of EVENT ON operation and is used only to initialize
		internal variables of application
		*/
	if (pLib->modem_init_event & (1L << nr)) {
		diva_trace_notify_user (pLib, nr, DIVA_SUPER_TRACE_NOTIFY_MODEM_CHANGE);
	} else {
		pLib->modem_init_event |= (1L << nr);
	}

	return (0);
}

static int diva_fax_info (diva_strace_context_t* pLib,
													int Channel,
													diva_man_var_header_t* pVar) {
	diva_man_var_header_t* cur;
	int i, nr = Channel - 1;

	for (i  = pLib->fax_parse_entry_first[nr];
			 i <= pLib->fax_parse_entry_last[nr]; i++) {
		if ((cur = find_var (pVar, pLib->parse_table[i].path))) {
			if (diva_trace_read_variable (cur, pLib->parse_table[i].variable)) {
				diva_trace_error (pLib, -3 , __FILE__, __LINE__);
				return (-1);
			}
		} else {
			diva_trace_error (pLib, -2 , __FILE__, __LINE__);
			return (-1);
		}
	}

	/*
		We do not use first event to notify user - this is the event that is
		generated as result of EVENT ON operation and is used only to initialize
		internal variables of application
		*/
	if (pLib->fax_init_event & (1L << nr)) {
		diva_trace_notify_user (pLib, nr, DIVA_SUPER_TRACE_NOTIFY_FAX_CHANGE);
	} else {
		pLib->fax_init_event |= (1L << nr);
	}

	return (0);
}

/*
	LINE STATE UPDATE
	Update Line Status Information and issue notification to user,
	that will inform about change in the line state.
	*/
static int diva_line_info  (diva_strace_context_t* pLib,
														int Channel,
														diva_man_var_header_t* pVar) {
	diva_man_var_header_t* cur;
	int i, nr = Channel - 1;

	for (i  = pLib->line_parse_entry_first[nr];
			 i <= pLib->line_parse_entry_last[nr]; i++) {
		if ((cur = find_var (pVar, pLib->parse_table[i].path))) {
			if (diva_trace_read_variable (cur, pLib->parse_table[i].variable)) {
				diva_trace_error (pLib, -3 , __FILE__, __LINE__);
				return (-1);
			}
		} else {
			diva_trace_error (pLib, -2 , __FILE__, __LINE__);
			return (-1);
		}
	}

	/*
		We do not use first event to notify user - this is the event that is
		generated as result of EVENT ON operation and is used only to initialize
		internal variables of application

		Exception is is if the line is "online". In this case we have to notify
		user about this confition.
		*/
	if (pLib->line_init_event & (1L << nr)) {
		diva_trace_notify_user (pLib, nr, DIVA_SUPER_TRACE_NOTIFY_LINE_CHANGE);
	} else {
		pLib->line_init_event |= (1L << nr);
		if (strcmp (&pLib->lines[nr].Line[0], "Idle")) {
			diva_trace_notify_user (pLib, nr, DIVA_SUPER_TRACE_NOTIFY_LINE_CHANGE);
		}
	}

	return (0);
}

/*
	Move position to next vatianle in the chain
	*/
static diva_man_var_header_t* get_next_var (diva_man_var_header_t* pVar) {
	byte* msg   = (byte*)pVar;
	byte* start;
	int msg_length;

	if (*msg != ESC) return NULL;

	start = msg + 2;
	msg_length = *(msg+1);
	msg = (start+msg_length);

	if (*msg != ESC) return NULL;

	return ((diva_man_var_header_t*)msg);
}

/*
	Move position to variable with given name
	*/
static diva_man_var_header_t* find_var (diva_man_var_header_t* pVar,
																				const char* name) {
	const char* path;

	do {
		path = (char*)&pVar->path_length+1;

		if (!strncmp (name, path, pVar->path_length)) {
			break;
		}
	} while ((pVar = get_next_var (pVar)));

	return (pVar);
}

static void diva_create_line_parse_table  (diva_strace_context_t* pLib,
																					 int Channel) {
	diva_trace_line_state_t* pLine = &pLib->lines[Channel];
	int nr = Channel+1;

	if ((pLib->cur_parse_entry + LINE_PARSE_ENTRIES) >= pLib->parse_entries) {
		diva_trace_error (pLib, -1, __FILE__, __LINE__);
		return;
	}

	pLine->ChannelNumber = nr;

	pLib->line_parse_entry_first[Channel] = pLib->cur_parse_entry;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Framing", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->Framing[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Line", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->Line[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Layer2", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->Layer2[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Layer3", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->Layer3[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Remote Address", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																								&pLine->RemoteAddress[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Remote SubAddr", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																								&pLine->RemoteSubAddress[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Local Address", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																								&pLine->LocalAddress[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Local SubAddr", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																								&pLine->LocalSubAddress[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\BC", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->call_BC;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\HLC", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->call_HLC;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\LLC", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->call_LLC;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Charges", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->Charges;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Call Reference", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->CallReference;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Last Disc Cause", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																										&pLine->LastDisconnecCause;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\User ID", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pLine->UserID[0];

	pLib->line_parse_entry_last[Channel] = pLib->cur_parse_entry - 1;
}

static void diva_create_fax_parse_table (diva_strace_context_t* pLib,
																				 int Channel) {
	diva_trace_fax_state_t* pFax = &pLib->lines[Channel].fax;
	int nr = Channel+1;

	if ((pLib->cur_parse_entry + FAX_PARSE_ENTRIES) >= pLib->parse_entries) {
		diva_trace_error (pLib, -1, __FILE__, __LINE__);
		return;
	}
	pFax->ChannelNumber = nr;

	pLib->fax_parse_entry_first[Channel] = pLib->cur_parse_entry;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Event", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Event;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Page Counter", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Page_Counter;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Features", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Features;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Station ID", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Station_ID[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Subaddress", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Subaddress[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Password", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Password[0];

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Speed", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Speed;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Resolution", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Resolution;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Paper Width", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Paper_Width;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Paper Length", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Paper_Length;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Scanline Time", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Scanline_Time;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\FAX\\Disc Reason", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pFax->Disc_Reason;

	pLib->fax_parse_entry_last[Channel] = pLib->cur_parse_entry - 1;
}

static void diva_create_modem_parse_table (diva_strace_context_t* pLib,
																					 int Channel) {
	diva_trace_modem_state_t* pModem = &pLib->lines[Channel].modem;
	int nr = Channel+1;

	if ((pLib->cur_parse_entry + MODEM_PARSE_ENTRIES) >= pLib->parse_entries) {
		diva_trace_error (pLib, -1, __FILE__, __LINE__);
		return;
	}
	pModem->ChannelNumber = nr;

	pLib->modem_parse_entry_first[Channel] = pLib->cur_parse_entry;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Event", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->Event;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Norm", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->Norm;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Options", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->Options;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\TX Speed", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->TxSpeed;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\RX Speed", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->RxSpeed;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Roundtrip ms", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->RoundtripMsec;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Symbol Rate", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->SymbolRate;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\RX Level dBm", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->RxLeveldBm;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Echo Level dBm", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->EchoLeveldBm;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\SNR dB", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->SNRdb;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\MAE", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->MAE;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Local Retrains", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->LocalRetrains;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Remote Retrains", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->RemoteRetrains;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Local Resyncs", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->LocalResyncs;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Remote Resyncs", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->RemoteResyncs;

	sprintf (pLib->parse_table[pLib->cur_parse_entry].path,
					 "State\\B%d\\Modem\\Disc Reason", nr);
	pLib->parse_table[pLib->cur_parse_entry++].variable = &pModem->DiscReason;

	pLib->modem_parse_entry_last[Channel] = pLib->cur_parse_entry - 1;
}

static void diva_create_parse_table (diva_strace_context_t* pLib) {
	int i;

	for (i = 0; i < pLib->Channels; i++) {
		diva_create_line_parse_table  (pLib, i);
		diva_create_modem_parse_table (pLib, i);
		diva_create_fax_parse_table   (pLib, i);
	}

	pLib->statistic_parse_first = pLib->cur_parse_entry;

	/*
		Outgoing Calls
		*/
	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Outgoing Calls\\Calls");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.outg.Calls;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Outgoing Calls\\Connected");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.outg.Connected;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Outgoing Calls\\User Busy");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.outg.User_Busy;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Outgoing Calls\\No Answer");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.outg.No_Answer;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Outgoing Calls\\Wrong Number");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.outg.Wrong_Number;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Outgoing Calls\\Call Rejected");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.outg.Call_Rejected;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Outgoing Calls\\Other Failures");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.outg.Other_Failures;

	/*
		Incoming Calls
		*/
	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Incoming Calls\\Calls");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.inc.Calls;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Incoming Calls\\Connected");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.inc.Connected;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Incoming Calls\\User Busy");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.inc.User_Busy;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Incoming Calls\\Call Rejected");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.inc.Call_Rejected;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Incoming Calls\\Wrong Number");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.inc.Wrong_Number;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Incoming Calls\\Incompatible Dst");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.inc.Incompatible_Dst;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Incoming Calls\\Out of Order");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.inc.Out_of_Order;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Incoming Calls\\Ignored");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.inc.Ignored;

	/*
		Modem Statistics
		*/
	pLib->mdm_statistic_parse_first = pLib->cur_parse_entry;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc Normal");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_Normal;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc Unspecified");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_Unspecified;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc Busy Tone");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_Busy_Tone;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc Congestion");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_Congestion;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc Carr. Wait");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_Carr_Wait;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc Trn Timeout");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_Trn_Timeout;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc Incompat.");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_Incompat;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc Frame Rej.");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_Frame_Rej;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\Modem\\Disc V42bis");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.mdm.Disc_V42bis;

	pLib->mdm_statistic_parse_last  = pLib->cur_parse_entry - 1;

	/*
		Fax Statistics
		*/
	pLib->fax_statistic_parse_first = pLib->cur_parse_entry;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Normal");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Normal;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Not Ident.");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Not_Ident;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc No Response");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_No_Response;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Retries");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Retries;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Unexp. Msg.");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Unexp_Msg;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc No Polling.");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_No_Polling;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Training");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Training;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Unexpected");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Unexpected;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Application");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Application;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Incompat.");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Incompat;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc No Command");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_No_Command;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Long Msg");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Long_Msg;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Supervisor");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Supervisor;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc SUB SEP PWD");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_SUB_SEP_PWD;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Invalid Msg");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Invalid_Msg;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Page Coding");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Page_Coding;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc App Timeout");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_App_Timeout;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\FAX\\Disc Unspecified");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.fax.Disc_Unspecified;

	pLib->fax_statistic_parse_last  = pLib->cur_parse_entry - 1;

	/*
		B-Layer1"
		*/
	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer1\\X-Frames");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b1.X_Frames;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer1\\X-Bytes");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b1.X_Bytes;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer1\\X-Errors");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b1.X_Errors;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer1\\R-Frames");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b1.R_Frames;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer1\\R-Bytes");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b1.R_Bytes;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer1\\R-Errors");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b1.R_Errors;

	/*
		B-Layer2
		*/
	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer2\\X-Frames");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b2.X_Frames;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer2\\X-Bytes");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b2.X_Bytes;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer2\\X-Errors");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b2.X_Errors;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer2\\R-Frames");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b2.R_Frames;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer2\\R-Bytes");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b2.R_Bytes;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\B-Layer2\\R-Errors");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.b2.R_Errors;

	/*
		D-Layer1
		*/
	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer1\\X-Frames");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d1.X_Frames;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer1\\X-Bytes");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d1.X_Bytes;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer1\\X-Errors");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d1.X_Errors;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer1\\R-Frames");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d1.R_Frames;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer1\\R-Bytes");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d1.R_Bytes;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer1\\R-Errors");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d1.R_Errors;

	/*
		D-Layer2
		*/
	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer2\\X-Frames");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d2.X_Frames;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer2\\X-Bytes");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d2.X_Bytes;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer2\\X-Errors");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d2.X_Errors;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer2\\R-Frames");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d2.R_Frames;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer2\\R-Bytes");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d2.R_Bytes;

	strcpy (pLib->parse_table[pLib->cur_parse_entry].path,
					"Statistics\\D-Layer2\\R-Errors");
	pLib->parse_table[pLib->cur_parse_entry++].variable = \
																		&pLib->InterfaceStat.d2.R_Errors;


	pLib->statistic_parse_last  = pLib->cur_parse_entry - 1;
}

static void diva_trace_error (diva_strace_context_t* pLib,
															int error, const char* file, int line) {
	if (pLib->user_proc_table.error_notify_proc) {
		(*(pLib->user_proc_table.error_notify_proc))(\
																						pLib->user_proc_table.user_context,
																						&pLib->instance, pLib->Adapter,
																						error, file, line);
	}
}

/*
	Delivery notification to user
	*/
static void diva_trace_notify_user (diva_strace_context_t* pLib,
														 int Channel,
														 int notify_subject) {
	if (pLib->user_proc_table.notify_proc) {
		(*(pLib->user_proc_table.notify_proc))(pLib->user_proc_table.user_context,
																					 &pLib->instance,
																					 pLib->Adapter,
																					 &pLib->lines[Channel],
																					 notify_subject);
	}
}

/*
	Read variable value to they destination based on the variable type
	*/
static int diva_trace_read_variable (diva_man_var_header_t* pVar,
																		 void* variable) {
	switch (pVar->type) {
		case 0x03: /* MI_ASCIIZ - syting                               */
			return (diva_strace_read_asz  (pVar, (char*)variable));
		case 0x04: /* MI_ASCII  - string                               */
			return (diva_strace_read_asc  (pVar, (char*)variable));
		case 0x05: /* MI_NUMBER - counted sequence of bytes            */
			return (diva_strace_read_ie  (pVar, (diva_trace_ie_t*)variable));
		case 0x81: /* MI_INT    - signed integer                       */
			return (diva_strace_read_int (pVar, (int*)variable));
		case 0x82: /* MI_UINT   - unsigned integer                     */
			return (diva_strace_read_uint (pVar, (dword*)variable));
		case 0x83: /* MI_HINT   - unsigned integer, hex representetion */
			return (diva_strace_read_uint (pVar, (dword*)variable));
		case 0x87: /* MI_BITFLD - unsigned integer, bit representation */
			return (diva_strace_read_uint (pVar, (dword*)variable));
	}

	/*
		This type of variable is not handled, indicate error
		Or one problem in management interface, or in application recodeing
		table, or this application should handle it.
		*/
	return (-1);
}

/*
	Read signed integer to destination
	*/
static int diva_strace_read_int  (diva_man_var_header_t* pVar, int* var) {
	byte* ptr = (char*)&pVar->path_length;
	int value;

	ptr += (pVar->path_length + 1);

	switch (pVar->value_length) {
		case 1:
			value = *(char*)ptr;
			break;

		case 2:
			value = (short)GET_WORD(ptr);
			break;

		case 4:
			value = (int)GET_DWORD(ptr);
			break;

		default:
			return (-1);
	}

	*var = value;

	return (0);
}

static int diva_strace_read_uint (diva_man_var_header_t* pVar, dword* var) {
	byte* ptr = (char*)&pVar->path_length;
	dword value;

	ptr += (pVar->path_length + 1);

	switch (pVar->value_length) {
		case 1:
			value = (byte)(*ptr);
			break;

		case 2:
			value = (word)GET_WORD(ptr);
			break;

		case 3:
			value  = (dword)GET_DWORD(ptr);
			value &= 0x00ffffff;
			break;

		case 4:
			value = (dword)GET_DWORD(ptr);
			break;

		default:
			return (-1);
	}

	*var = value;

	return (0);
}

/*
	Read zero terminated ASCII string
	*/
static int diva_strace_read_asz  (diva_man_var_header_t* pVar, char* var) {
	char* ptr = (char*)&pVar->path_length;
	int length;

	ptr += (pVar->path_length + 1);

	if (!(length = pVar->value_length)) {
		length = strlen (ptr);
	}
	memcpy (var, ptr, length);
	var[length] = 0;

	return (0);
}

/*
	Read counted (with leading length byte) ASCII string
	*/
static int diva_strace_read_asc  (diva_man_var_header_t* pVar, char* var) {
	char* ptr = (char*)&pVar->path_length;

	ptr += (pVar->path_length + 1);
	memcpy (var, ptr+1, *ptr);
	var[(int)*ptr] = 0;

	return (0);
}

/*
		Read one information element - i.e. one string of byte values with
		one length byte in front
	*/
static int  diva_strace_read_ie  (diva_man_var_header_t* pVar,
																	diva_trace_ie_t* var) {
	char* ptr = (char*)&pVar->path_length;

	ptr += (pVar->path_length + 1);

	var->length = *ptr;
	memcpy (&var->data[0], ptr+1, *ptr);

	return (0);
}

static int SuperTraceSetAudioTap  (void* hLib, int Channel, int on) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;

	if ((Channel < 1) || (Channel > pLib->Channels)) {
		return (-1);
	}
	Channel--;

	if (on) {
		pLib->audio_tap_mask |=  (1L << Channel);
	} else {
		pLib->audio_tap_mask &= ~(1L << Channel);
	}

  /*
    EYE patterns have TM_M_DATA set as additional
    condition
    */
  if (pLib->audio_tap_mask) {
    pLib->trace_event_mask |= TM_M_DATA;
  } else {
    pLib->trace_event_mask &= ~TM_M_DATA;
  }

	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceSetBChannel  (void* hLib, int Channel, int on) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;

	if ((Channel < 1) || (Channel > pLib->Channels)) {
		return (-1);
	}
	Channel--;

	if (on) {
		pLib->bchannel_trace_mask |=  (1L << Channel);
	} else {
		pLib->bchannel_trace_mask &= ~(1L << Channel);
	}

	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceSetDChannel  (void* hLib, int on) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;

	if (on) {
		pLib->trace_event_mask |= (TM_D_CHAN | TM_C_COMM | TM_DL_ERR | TM_LAYER1);
	} else {
		pLib->trace_event_mask &= ~(TM_D_CHAN | TM_C_COMM | TM_DL_ERR | TM_LAYER1);
	}

	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceSetInfo (void* hLib, int on) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;

	if (on) {
		pLib->trace_event_mask |= TM_STRING;
	} else {
		pLib->trace_event_mask &= ~TM_STRING;
	}

	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceClearCall (void* hLib, int Channel) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;

	if ((Channel < 1) || (Channel > pLib->Channels)) {
		return (-1);
	}
	Channel--;

	pLib->clear_call_command |= (1L << Channel);

	return (ScheduleNextTraceRequest (pLib));
}

/*
	Parse and update cumulative statistice
	*/
static int diva_ifc_statistics (diva_strace_context_t* pLib,
																diva_man_var_header_t* pVar) {
	diva_man_var_header_t* cur;
	int i, one_updated = 0, mdm_updated = 0, fax_updated = 0;

	for (i  = pLib->statistic_parse_first; i <= pLib->statistic_parse_last; i++) {
		if ((cur = find_var (pVar, pLib->parse_table[i].path))) {
			if (diva_trace_read_variable (cur, pLib->parse_table[i].variable)) {
				diva_trace_error (pLib, -3 , __FILE__, __LINE__);
				return (-1);
			}
			one_updated = 1;
      if ((i >= pLib->mdm_statistic_parse_first) && (i <= pLib->mdm_statistic_parse_last)) {
        mdm_updated = 1;
      }
      if ((i >= pLib->fax_statistic_parse_first) && (i <= pLib->fax_statistic_parse_last)) {
        fax_updated = 1;
      }
		}
	}

	/*
		We do not use first event to notify user - this is the event that is
		generated as result of EVENT ON operation and is used only to initialize
		internal variables of application
		*/
  if (mdm_updated) {
		diva_trace_notify_user (pLib, 0, DIVA_SUPER_TRACE_NOTIFY_MDM_STAT_CHANGE);
  } else if (fax_updated) {
		diva_trace_notify_user (pLib, 0, DIVA_SUPER_TRACE_NOTIFY_FAX_STAT_CHANGE);
  } else if (one_updated) {
		diva_trace_notify_user (pLib, 0, DIVA_SUPER_TRACE_NOTIFY_STAT_CHANGE);
	}

	return (one_updated ? 0 : -1);
}

static int SuperTraceGetOutgoingCallStatistics (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	pLib->outgoing_ifc_stats = 1;
	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceGetIncomingCallStatistics (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	pLib->incoming_ifc_stats = 1;
	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceGetModemStatistics (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	pLib->modem_ifc_stats = 1;
	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceGetFaxStatistics (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	pLib->fax_ifc_stats = 1;
	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceGetBLayer1Statistics (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	pLib->b1_ifc_stats = 1;
	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceGetBLayer2Statistics (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	pLib->b2_ifc_stats = 1;
	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceGetDLayer1Statistics (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	pLib->d1_ifc_stats = 1;
	return (ScheduleNextTraceRequest (pLib));
}

static int SuperTraceGetDLayer2Statistics (void* hLib) {
	diva_strace_context_t* pLib = (diva_strace_context_t*)hLib;
	pLib->d2_ifc_stats = 1;
	return (ScheduleNextTraceRequest (pLib));
}

dword DivaSTraceGetMemotyRequirement (int channels) {
  dword parse_entries = (MODEM_PARSE_ENTRIES + FAX_PARSE_ENTRIES + \
												 STAT_PARSE_ENTRIES + \
												 LINE_PARSE_ENTRIES + 1) * channels;
  return (sizeof(diva_strace_context_t) + \
          (parse_entries * sizeof(diva_strace_path2action_t)));
}

