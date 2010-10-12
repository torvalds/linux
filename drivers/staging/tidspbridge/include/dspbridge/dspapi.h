/*
 * dspapi.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Includes the wrapper functions called directly by the
 * DeviceIOControl interface.
 *
 * Notes:
 *   Bridge services exported to Bridge driver are initialized by the DSPAPI on
 *   behalf of the Bridge driver. Bridge driver must not call module Init/Exit
 *   functions.
 *
 *   To ensure Bridge driver binary compatibility across different platforms,
 *   for the same processor, a Bridge driver must restrict its usage of system
 *   services to those exported by the DSPAPI library.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef DSPAPI_
#define DSPAPI_

#include <dspbridge/dspapi-ioctl.h>

/* This BRD API Library Version: */
#define BRD_API_MAJOR_VERSION   (u32)8	/* .8x - Alpha, .9x - Beta, 1.x FCS */
#define BRD_API_MINOR_VERSION   (u32)0

/*
 *  ======== api_call_dev_ioctl ========
 *  Purpose:
 *      Call the (wrapper) function for the corresponding API IOCTL.
 *  Parameters:
 *      cmd:        IOCTL id, base 0.
 *      args:       Argument structure.
 *      result:
 *  Returns:
 *      0 if command called; -EINVAL if command not in IOCTL
 *      table.
 *  Requires:
 *  Ensures:
 */
extern int api_call_dev_ioctl(unsigned int cmd,
				      union trapped_args *args,
				      u32 *result, void *pr_ctxt);

/*
 *  ======== api_init ========
 *  Purpose:
 *      Initialize modules used by Bridge API.
 *      This procedure is called when the driver is loaded.
 *  Parameters:
 *  Returns:
 *      TRUE if success; FALSE otherwise.
 *  Requires:
 *  Ensures:
 */
extern bool api_init(void);

/*
 *  ======== api_init_complete2 ========
 *  Purpose:
 *      Perform any required bridge initialization which cannot
 *      be performed in api_init() or dev_start_device() due
 *      to the fact that some services are not yet
 *      completely initialized.
 *  Parameters:
 *  Returns:
 *      0:        Allow this device to load
 *      -EPERM:      Failure.
 *  Requires:
 *      Bridge API initialized.
 *  Ensures:
 */
extern int api_init_complete2(void);

/*
 *  ======== api_exit ========
 *  Purpose:
 *      Exit all modules initialized in api_init(void).
 *      This procedure is called when the driver is unloaded.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      api_init(void) was previously called.
 *  Ensures:
 *      Resources acquired in api_init(void) are freed.
 */
extern void api_exit(void);

/* MGR wrapper functions */
extern u32 mgrwrap_enum_node_info(union trapped_args *args, void *pr_ctxt);
extern u32 mgrwrap_enum_proc_info(union trapped_args *args, void *pr_ctxt);
extern u32 mgrwrap_register_object(union trapped_args *args, void *pr_ctxt);
extern u32 mgrwrap_unregister_object(union trapped_args *args, void *pr_ctxt);
extern u32 mgrwrap_wait_for_bridge_events(union trapped_args *args,
					  void *pr_ctxt);

extern u32 mgrwrap_get_process_resources_info(union trapped_args *args,
					      void *pr_ctxt);

/* CPRC (Processor) wrapper Functions */
extern u32 procwrap_attach(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_ctrl(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_detach(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_enum_node_info(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_enum_resources(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_get_state(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_get_trace(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_load(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_register_notify(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_start(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_reserve_memory(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_un_reserve_memory(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_map(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_un_map(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_flush_memory(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_stop(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_invalidate_memory(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_begin_dma(union trapped_args *args, void *pr_ctxt);
extern u32 procwrap_end_dma(union trapped_args *args, void *pr_ctxt);

/* NODE wrapper functions */
extern u32 nodewrap_allocate(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_alloc_msg_buf(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_change_priority(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_connect(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_create(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_delete(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_free_msg_buf(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_get_attr(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_get_message(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_pause(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_put_message(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_register_notify(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_run(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_terminate(union trapped_args *args, void *pr_ctxt);
extern u32 nodewrap_get_uuid_props(union trapped_args *args, void *pr_ctxt);

/* STRM wrapper functions */
extern u32 strmwrap_allocate_buffer(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_close(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_free_buffer(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_get_event_handle(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_get_info(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_idle(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_issue(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_open(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_reclaim(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_register_notify(union trapped_args *args, void *pr_ctxt);
extern u32 strmwrap_select(union trapped_args *args, void *pr_ctxt);

extern u32 cmmwrap_calloc_buf(union trapped_args *args, void *pr_ctxt);
extern u32 cmmwrap_free_buf(union trapped_args *args, void *pr_ctxt);
extern u32 cmmwrap_get_handle(union trapped_args *args, void *pr_ctxt);
extern u32 cmmwrap_get_info(union trapped_args *args, void *pr_ctxt);

#endif /* DSPAPI_ */
