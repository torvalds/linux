
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
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
#ifndef __DIVA_SOFT_DSP_TASK_ENTRY_H__
#define __DIVA_SOFT_DSP_TASK_ENTRY_H__
/*
 The soft DSP image is described by binary header contained on begin of this
 image:
OFFSET FROM IMAGE START |  VARIABLE
------------------------------------------------------------------------
 DIVA_MIPS_TASK_IMAGE_LINK_OFFS   |  link to the next image
  ----------------------------------------------------------------------
 DIVA_MIPS_TASK_IMAGE_GP_OFFS    |  image gp register value, void*
  ----------------------------------------------------------------------
 DIVA_MIPS_TASK_IMAGE_ENTRY_OFFS   |  diva_mips_sdp_task_entry_t*
  ----------------------------------------------------------------------
 DIVA_MIPS_TASK_IMAGE_LOAD_ADDR_OFFS |  image image start address (void*)
  ----------------------------------------------------------------------
 DIVA_MIPS_TASK_IMAGE_END_ADDR_OFFS |  image image end address   (void*)
  ----------------------------------------------------------------------
 DIVA_MIPS_TASK_IMAGE_ID_STRING_OFFS |  image id string char[...];
  ----------------------------------------------------------------------
 */
#define DIVA_MIPS_TASK_IMAGE_LINK_OFFS   0x6C
#define DIVA_MIPS_TASK_IMAGE_GP_OFFS    0x70
#define DIVA_MIPS_TASK_IMAGE_ENTRY_OFFS   0x74
#define DIVA_MIPS_TASK_IMAGE_LOAD_ADDR_OFFS 0x78
#define DIVA_MIPS_TASK_IMAGE_END_ADDR_OFFS 0x7c
#define DIVA_MIPS_TASK_IMAGE_ID_STRING_OFFS 0x80
/*
 This function is called in order to set GP register of this task
 This function should be always called before any function of the
 task is called
 */
typedef void (*diva_task_set_prog_gp_proc_t)(void* new_gp);
/*
 This function is called to clear .bss at task initialization step
 */
typedef void  (*diva_task_sys_reset_proc_t)(void);
/*
 This function is called in order to provide GP of master call to
 task, that will be used by calls from the task to the master
 */
typedef void (*diva_task_set_main_gp_proc_t)(void* main_gp);
/*
 This function is called to provide address of 'dprintf' function
 to the task
 */
typedef word (*diva_prt_proc_t)(char *, ...);
typedef void (*diva_task_set_prt_proc_t)(diva_prt_proc_t fn);
/*
 This function is called to set task PID
 */
typedef void (*diva_task_set_pid_proc_t)(dword id);
/*
 This function is called for run-time task init
 */
typedef int (*diva_task_run_time_init_proc_t)(void*, dword);
/*
 This function is called from system scheduler or from timer
 */
typedef void (*diva_task_callback_proc_t)(void);
/*
 This callback is used by task to get current time im mS
  */
typedef dword (*diva_task_get_tick_count_proc_t)(void);
typedef void (*diva_task_set_get_time_proc_t)(\
                diva_task_get_tick_count_proc_t fn);
typedef struct _diva_mips_sdp_task_entry {
 diva_task_set_prog_gp_proc_t  set_gp_proc;
 diva_task_sys_reset_proc_t   sys_reset_proc;
 diva_task_set_main_gp_proc_t  set_main_gp_proc;
 diva_task_set_prt_proc_t    set_dprintf_proc;
 diva_task_set_pid_proc_t    set_pid_proc;
 diva_task_run_time_init_proc_t run_time_init_proc;
 diva_task_callback_proc_t    task_callback_proc;
 diva_task_callback_proc_t    timer_callback_proc;
 diva_task_set_get_time_proc_t  set_get_time_proc;
 void*              last_entry_proc;
} diva_mips_sdp_task_entry_t;
/*
 'last_entry_proc' should be set to zero and is used for future extensuios
 */
typedef struct _diva_mips_sw_task {
  diva_mips_sdp_task_entry_t  sdp_entry;
  void*                       sdp_gp_reg;
  void*                       own_gp_reg;
} diva_mips_sw_task_t;
#if !defined(DIVA_BRI2F_SDP_1_NAME)
#define DIVA_BRI2F_SDP_1_NAME "sdp0.2q0"
#endif
#if !defined(DIVA_BRI2F_SDP_2_NAME)
#define DIVA_BRI2F_SDP_2_NAME "sdp1.2q0"
#endif
#endif
