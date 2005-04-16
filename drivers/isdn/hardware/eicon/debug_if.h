/*
 *
  Copyright (c) Eicon Technology Corporation, 2000.
 *
  This source file is supplied for the use with Eicon
  Technology Corporation's range of DIVA Server Adapters.
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
#ifndef __DIVA_DEBUG_IF_H__
#define __DIVA_DEBUG_IF_H__
#define MSG_TYPE_DRV_ID		0x0001
#define MSG_TYPE_FLAGS		0x0002
#define MSG_TYPE_STRING		0x0003
#define MSG_TYPE_BINARY		0x0004
#define MSG_TYPE_MLOG     0x0005

#define MSG_FRAME_MAX_SIZE 2150

typedef struct _diva_dbg_entry_head {
  dword sequence;
  dword time_sec;
  dword time_usec;
  dword facility;
  dword dli;
  dword drv_id;
  dword di_cpu;
  dword data_length;
} diva_dbg_entry_head_t;

int diva_maint_init (byte* base, unsigned long length, int do_init);
void* diva_maint_finit (void);
dword diva_dbg_q_length (void);
diva_dbg_entry_head_t* diva_maint_get_message (word* size,
                                               diva_os_spin_lock_magic_t* old_irql);
void diva_maint_ack_message (int do_release,
                             diva_os_spin_lock_magic_t* old_irql);
void diva_maint_prtComp (char *format, ...);
void diva_maint_wakeup_read (void);
int diva_get_driver_info (dword id, byte* data, int data_length);
int diva_get_driver_dbg_mask (dword id, byte* data);
int diva_set_driver_dbg_mask (dword id, dword mask);
void diva_mnt_remove_xdi_adapter (const DESCRIPTOR* d);
void diva_mnt_add_xdi_adapter    (const DESCRIPTOR* d);
int diva_mnt_shutdown_xdi_adapters (void);

#define DIVA_MAX_SELECTIVE_FILTER_LENGTH 127
int diva_set_trace_filter (int filter_length, const char* filter);
int diva_get_trace_filter (int max_length,    char*       filter);


#define DITRACE_CMD_GET_DRIVER_INFO   1
#define DITRACE_READ_DRIVER_DBG_MASK  2
#define DITRACE_WRITE_DRIVER_DBG_MASK 3
#define DITRACE_READ_TRACE_ENTRY      4
#define DITRACE_READ_TRACE_ENTRYS     5
#define DITRACE_WRITE_SELECTIVE_TRACE_FILTER 6
#define DITRACE_READ_SELECTIVE_TRACE_FILTER  7

/*
  Trace lavels for debug via management interface
  */
#define DIVA_MGT_DBG_TRACE          0x00000001 /* All trace messages from the card */
#define DIVA_MGT_DBG_DCHAN          0x00000002 /* All D-channel relater trace messages */
#define DIVA_MGT_DBG_MDM_PROGRESS   0x00000004 /* Modem progress events */
#define DIVA_MGT_DBG_FAX_PROGRESS   0x00000008 /* Fax progress events */
#define DIVA_MGT_DBG_IFC_STATISTICS 0x00000010 /* Interface call statistics */
#define DIVA_MGT_DBG_MDM_STATISTICS 0x00000020 /* Global modem statistics   */
#define DIVA_MGT_DBG_FAX_STATISTICS 0x00000040 /* Global call statistics    */
#define DIVA_MGT_DBG_LINE_EVENTS    0x00000080 /* Line state events */
#define DIVA_MGT_DBG_IFC_EVENTS     0x00000100 /* Interface/L1/L2 state events */
#define DIVA_MGT_DBG_IFC_BCHANNEL   0x00000200 /* B-Channel trace for all channels */
#define DIVA_MGT_DBG_IFC_AUDIO      0x00000400 /* Audio Tap trace for all channels */

# endif /* DEBUG_IF___H */


