/*
 * proc.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This is the DSP API RM module interface.
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

#ifndef PROC_
#define PROC_

#include <dspbridge/cfgdefs.h>
#include <dspbridge/devdefs.h>
#include <dspbridge/drv.h>

extern char *iva_img;

/*
 *  ======== proc_attach ========
 *  Purpose:
 *      Prepare for communication with a particular DSP processor, and return
 *      a handle to the processor object. The PROC Object gets created
 *  Parameters:
 *      processor_id  :	   The processor index (zero-based).
 *      hmgr_obj  :	   Handle to the Manager Object
 *      attr_in     :	   Ptr to the dsp_processorattrin structure.
 *			      A NULL value means use default values.
 *      ph_processor :	   Ptr to location to store processor handle.
 *  Returns:
 *      0     :	   Success.
 *      -EPERM   :	   General failure.
 *      -EFAULT :	   Invalid processor handle.
 *      0:   Success; Processor already attached.
 *  Requires:
 *      ph_processor != NULL.
 *      PROC Initialized.
 *  Ensures:
 *      -EPERM, and *ph_processor == NULL, OR
 *      Success and *ph_processor is a Valid Processor handle OR
 *      0 and *ph_processor is a Valid Processor.
 *  Details:
 *      When attr_in is NULL, the default timeout value is 10 seconds.
 */
extern int proc_attach(u32 processor_id,
			      const struct dsp_processorattrin
			      *attr_in, void **ph_processor,
			      struct process_context *pr_ctxt);

/*
 *  ======== proc_auto_start =========
 *  Purpose:
 *      A Particular device gets loaded with the default image
 *      if the AutoStart flag is set.
 *  Parameters:
 *      hdev_obj  :   Handle to the Device
 *  Returns:
 *      0     :   On Successful Loading
 *      -ENOENT   :   No DSP exec file found.
 *      -EPERM   :   General Failure
 *  Requires:
 *      hdev_obj != NULL.
 *      dev_node_obj != NULL.
 *      PROC Initialized.
 *  Ensures:
 */
extern int proc_auto_start(struct cfg_devnode *dev_node_obj,
				  struct dev_object *hdev_obj);

/*
 *  ======== proc_ctrl ========
 *  Purpose:
 *      Pass control information to the GPP device driver managing the DSP
 *      processor. This will be an OEM-only function, and not part of the
 *      'Bridge application developer's API.
 *  Parameters:
 *      hprocessor  :       The processor handle.
 *      dw_cmd       :       Private driver IOCTL cmd ID.
 *      pargs       :       Ptr to an driver defined argument structure.
 *  Returns:
 *      0     :       SUCCESS
 *      -EFAULT :       Invalid processor handle.
 *      -ETIME:       A Timeout Occurred before the Control information
 *			  could be sent.
 *      -EPERM   :       General Failure.
 *  Requires:
 *      PROC Initialized.
 *  Ensures
 *  Details:
 *      This function Calls bridge_dev_ctrl.
 */
extern int proc_ctrl(void *hprocessor,
			    u32 dw_cmd, struct dsp_cbdata *arg);

/*
 *  ======== proc_detach ========
 *  Purpose:
 *      Close a DSP processor and de-allocate all (GPP) resources reserved
 *      for it. The Processor Object is deleted.
 *  Parameters:
 *      pr_ctxt     :   The processor handle.
 *  Returns:
 *      0     :   Success.
 *      -EFAULT :   InValid Handle.
 *      -EPERM   :   General failure.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *      PROC Object is destroyed.
 */
extern int proc_detach(struct process_context *pr_ctxt);

/*
 *  ======== proc_enum_nodes ========
 *  Purpose:
 *      Enumerate the nodes currently allocated on a processor.
 *  Parameters:
 *      hprocessor  :   The processor handle.
 *      node_tab    :   The first Location of an array allocated for node
 *		      handles.
 *      node_tab_size:   The number of (DSP_HNODE) handles that can be held
 *		      to the memory the client has allocated for node_tab
 *      pu_num_nodes  :   Location where DSPProcessor_EnumNodes will return
 *		      the number of valid handles written to node_tab
 *      pu_allocated :   Location where DSPProcessor_EnumNodes will return
 *		      the number of nodes that are allocated on the DSP.
 *  Returns:
 *      0     :   Success.
 *      -EFAULT :   Invalid processor handle.
 *      -EINVAL   :   The amount of memory allocated for node_tab is
 *		      insufficent. That is the number of nodes actually
 *		      allocated on the DSP is greater than the value
 *		      specified for node_tab_size.
 *      -EPERM   :   Unable to get Resource Information.
 *  Details:
 *  Requires
 *      pu_num_nodes is not NULL.
 *      pu_allocated is not NULL.
 *      node_tab is not NULL.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_enum_nodes(void *hprocessor,
				  void **node_tab,
				  u32 node_tab_size,
				  u32 *pu_num_nodes,
				  u32 *pu_allocated);

/*
 *  ======== proc_get_resource_info ========
 *  Purpose:
 *      Enumerate the resources currently available on a processor.
 *  Parameters:
 *      hprocessor  :       The processor handle.
 *      resource_type:      Type of resource .
 *      resource_info:      Ptr to the dsp_resourceinfo structure.
 *      resource_info_size:  Size of the structure.
 *  Returns:
 *      0     :       Success.
 *      -EFAULT :       Invalid processor handle.
 *      -EBADR:    The processor is not in the PROC_RUNNING state.
 *      -ETIME:       A timeout occurred before the DSP responded to the
 *			  querry.
 *      -EPERM   :       Unable to get Resource Information
 *  Requires:
 *      resource_info is not NULL.
 *      Parameter resource_type is Valid.[TBD]
 *      resource_info_size is >= sizeof dsp_resourceinfo struct.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 *      This function currently returns
 *      -ENOSYS, and does not write any data to the resource_info struct.
 */
extern int proc_get_resource_info(void *hprocessor,
					 u32 resource_type,
					 struct dsp_resourceinfo
					 *resource_info,
					 u32 resource_info_size);

/*
 *  ======== proc_exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      PROC is initialized.
 *  Ensures:
 *      When reference count == 0, PROC's private resources are freed.
 */
extern void proc_exit(void);

/*
 * ======== proc_get_dev_object =========
 *  Purpose:
 *      Returns the DEV Hanlde for a given Processor handle
 *  Parameters:
 *      hprocessor  :   Processor Handle
 *      device_obj :    Location to store the DEV Handle.
 *  Returns:
 *      0     :   Success; *device_obj has Dev handle
 *      -EPERM   :   Failure; *device_obj is zero.
 *  Requires:
 *      device_obj is not NULL
 *      PROC Initialized.
 *  Ensures:
 *      0     :   *device_obj is not NULL
 *      -EPERM   :   *device_obj is NULL.
 */
extern int proc_get_dev_object(void *hprocessor,
				      struct dev_object **device_obj);

/*
 *  ======== proc_init ========
 *  Purpose:
 *      Initialize PROC's private state, keeping a reference count on each
 *      call.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occurred.
 *  Requires:
 *  Ensures:
 *      TRUE: A requirement for the other public PROC functions.
 */
extern bool proc_init(void);

/*
 *  ======== proc_get_state ========
 *  Purpose:
 *      Report the state of the specified DSP processor.
 *  Parameters:
 *      hprocessor  :   The processor handle.
 *      proc_state_obj :   Ptr to location to store the dsp_processorstate
 *		      structure.
 *      state_info_size: Size of dsp_processorstate.
 *  Returns:
 *      0     :   Success.
 *      -EFAULT :   Invalid processor handle.
 *      -EPERM   :   General failure while querying processor state.
 *  Requires:
 *      proc_state_obj is not NULL
 *      state_info_size is >= than the size of dsp_processorstate structure.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_get_state(void *hprocessor, struct dsp_processorstate
				 *proc_state_obj, u32 state_info_size);

/*
 *  ======== PROC_GetProcessorID ========
 *  Purpose:
 *      Report the state of the specified DSP processor.
 *  Parameters:
 *      hprocessor  :   The processor handle.
 *      proc_id      :   Processor ID
 *
 *  Returns:
 *      0     :   Success.
 *      -EFAULT :   Invalid processor handle.
 *      -EPERM   :   General failure while querying processor state.
 *  Requires:
 *      proc_state_obj is not NULL
 *      state_info_size is >= than the size of dsp_processorstate structure.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_get_processor_id(void *proc, u32 * proc_id);

/*
 *  ======== proc_get_trace ========
 *  Purpose:
 *      Retrieve the trace buffer from the specified DSP processor.
 *  Parameters:
 *      hprocessor  :   The processor handle.
 *      pbuf	:   Ptr to buffer to hold trace output.
 *      max_size    :   Maximum size of the output buffer.
 *  Returns:
 *      0     :   Success.
 *      -EFAULT :   Invalid processor handle.
 *      -EPERM   :   General failure while retireving processor trace
 *		      Buffer.
 *  Requires:
 *      pbuf is not NULL
 *      max_size is > 0.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_get_trace(void *hprocessor, u8 * pbuf, u32 max_size);

/*
 *  ======== proc_load ========
 *  Purpose:
 *      Reset a processor and load a new base program image.
 *      This will be an OEM-only function.
 *  Parameters:
 *      hprocessor:       The processor handle.
 *      argc_index:       The number of Arguments(strings)in the aArgV[]
 *      user_args:       An Array of Arguments(Unicode Strings)
 *      user_envp:       An Array of Environment settings(Unicode Strings)
 *  Returns:
 *      0:       Success.
 *      -ENOENT:       The DSP Execuetable was not found.
 *      -EFAULT:       Invalid processor handle.
 *      -EPERM   :       Unable to Load the Processor
 *  Requires:
 *      user_args is not NULL
 *      argc_index is > 0
 *      PROC Initialized.
 *  Ensures:
 *      Success and ProcState == PROC_LOADED
 *      or DSP_FAILED status.
 *  Details:
 *      Does not implement access rights to control which GPP application
 *      can load the processor.
 */
extern int proc_load(void *hprocessor,
			    const s32 argc_index, const char **user_args,
			    const char **user_envp);

/*
 *  ======== proc_register_notify ========
 *  Purpose:
 *      Register to be notified of specific processor events
 *  Parameters:
 *      hprocessor  :   The processor handle.
 *      event_mask  :   Mask of types of events to be notified about.
 *      notify_type :   Type of notification to be sent.
 *      hnotification:  Handle to be used for notification.
 *  Returns:
 *      0     :   Success.
 *      -EFAULT :   Invalid processor handle or hnotification.
 *      -EINVAL  :   Parameter event_mask is Invalid
 *      DSP_ENOTIMP :   The notification type specified in uNotifyMask
 *		      is not supported.
 *      -EPERM   :   Unable to register for notification.
 *  Requires:
 *      hnotification is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_register_notify(void *hprocessor,
				       u32 event_mask, u32 notify_type,
				       struct dsp_notification
				       *hnotification);

/*
 *  ======== proc_notify_clients ========
 *  Purpose:
 *      Notify the Processor Clients
 *  Parameters:
 *      proc       :   The processor handle.
 *      events     :   Event to be notified about.
 *  Returns:
 *      0     :   Success.
 *      -EFAULT :   Invalid processor handle.
 *      -EPERM   :   Failure to Set or Reset the Event
 *  Requires:
 *      events is Supported or Valid type of Event
 *      proc is a valid handle
 *      PROC Initialized.
 *  Ensures:
 */
extern int proc_notify_clients(void *proc, u32 events);

/*
 *  ======== proc_notify_all_clients ========
 *  Purpose:
 *      Notify the Processor Clients
 *  Parameters:
 *      proc       :   The processor handle.
 *      events     :   Event to be notified about.
 *  Returns:
 *      0     :   Success.
 *      -EFAULT :   Invalid processor handle.
 *      -EPERM   :   Failure to Set or Reset the Event
 *  Requires:
 *      events is Supported or Valid type of Event
 *      proc is a valid handle
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 *      NODE And STRM would use this function to notify their clients
 *      about the state changes in NODE or STRM.
 */
extern int proc_notify_all_clients(void *proc, u32 events);

/*
 *  ======== proc_start ========
 *  Purpose:
 *      Start a processor running.
 *      Processor must be in PROC_LOADED state.
 *      This will be an OEM-only function, and not part of the 'Bridge
 *      application developer's API.
 *  Parameters:
 *      hprocessor  :       The processor handle.
 *  Returns:
 *      0     :       Success.
 *      -EFAULT :       Invalid processor handle.
 *      -EBADR:    Processor is not in PROC_LOADED state.
 *      -EPERM   :       Unable to start the processor.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *      Success and ProcState == PROC_RUNNING or DSP_FAILED status.
 *  Details:
 */
extern int proc_start(void *hprocessor);

/*
 *  ======== proc_stop ========
 *  Purpose:
 *      Start a processor running.
 *      Processor must be in PROC_LOADED state.
 *      This will be an OEM-only function, and not part of the 'Bridge
 *      application developer's API.
 *  Parameters:
 *      hprocessor  :       The processor handle.
 *  Returns:
 *      0     :       Success.
 *      -EFAULT :       Invalid processor handle.
 *      -EBADR:    Processor is not in PROC_LOADED state.
 *      -EPERM   :       Unable to start the processor.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *      Success and ProcState == PROC_RUNNING or DSP_FAILED status.
 *  Details:
 */
extern int proc_stop(void *hprocessor);

/*
 *  ======== proc_end_dma ========
 *  Purpose:
 *      Begin a DMA transfer
 *  Parameters:
 *      hprocessor      :   The processor handle.
 *      pmpu_addr	:   Buffer start address
 *      ul_size		:   Buffer size
 *      dir		:   The direction of the transfer
 *  Requires:
 *      Memory was previously mapped.
 */
extern int proc_end_dma(void *hprocessor, void *pmpu_addr, u32 ul_size,
						enum dma_data_direction dir);
/*
 *  ======== proc_begin_dma ========
 *  Purpose:
 *      Begin a DMA transfer
 *  Parameters:
 *      hprocessor      :   The processor handle.
 *      pmpu_addr	:   Buffer start address
 *      ul_size		:   Buffer size
 *      dir		:   The direction of the transfer
 *  Requires:
 *      Memory was previously mapped.
 */
extern int proc_begin_dma(void *hprocessor, void *pmpu_addr, u32 ul_size,
						enum dma_data_direction dir);

/*
 *  ======== proc_flush_memory ========
 *  Purpose:
 *      Flushes a buffer from the MPU data cache.
 *  Parameters:
 *      hprocessor      :   The processor handle.
 *      pmpu_addr	:   Buffer start address
 *      ul_size	  :   Buffer size
 *      ul_flags	 :   Reserved.
 *  Returns:
 *      0	 :   Success.
 *      -EFAULT     :   Invalid processor handle.
 *      -EPERM       :   General failure.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 *      All the arguments are currently ignored.
 */
extern int proc_flush_memory(void *hprocessor,
				    void *pmpu_addr, u32 ul_size, u32 ul_flags);

/*
 *  ======== proc_invalidate_memory ========
 *  Purpose:
 *      Invalidates a buffer from the MPU data cache.
 *  Parameters:
 *      hprocessor      :   The processor handle.
 *      pmpu_addr	:   Buffer start address
 *      ul_size	  :   Buffer size
 *  Returns:
 *      0	 :   Success.
 *      -EFAULT     :   Invalid processor handle.
 *      -EPERM       :   General failure.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 *      All the arguments are currently ignored.
 */
extern int proc_invalidate_memory(void *hprocessor,
					 void *pmpu_addr, u32 ul_size);

/*
 *  ======== proc_map ========
 *  Purpose:
 *      Maps a MPU buffer to DSP address space.
 *  Parameters:
 *      hprocessor      :   The processor handle.
 *      pmpu_addr	:   Starting address of the memory region to map.
 *      ul_size	  :   Size of the memory region to map.
 *      req_addr	:   Requested DSP start address. Offset-adjusted actual
 *			  mapped address is in the last argument.
 *      pp_map_addr       :   Ptr to DSP side mapped u8 address.
 *      ul_map_attr       :   Optional endianness attributes, virt to phys flag.
 *  Returns:
 *      0	 :   Success.
 *      -EFAULT     :   Invalid processor handle.
 *      -EPERM       :   General failure.
 *      -ENOMEM     :   MPU side memory allocation error.
 *      -ENOENT   :   Cannot find a reserved region starting with this
 *		      :   address.
 *  Requires:
 *      pmpu_addr is not NULL
 *      ul_size is not zero
 *      pp_map_addr is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_map(void *hprocessor,
			   void *pmpu_addr,
			   u32 ul_size,
			   void *req_addr,
			   void **pp_map_addr, u32 ul_map_attr,
			   struct process_context *pr_ctxt);

/*
 *  ======== proc_reserve_memory ========
 *  Purpose:
 *      Reserve a virtually contiguous region of DSP address space.
 *  Parameters:
 *      hprocessor      :   The processor handle.
 *      ul_size	  :   Size of the address space to reserve.
 *      pp_rsv_addr       :   Ptr to DSP side reserved u8 address.
 *  Returns:
 *      0	 :   Success.
 *      -EFAULT     :   Invalid processor handle.
 *      -EPERM       :   General failure.
 *      -ENOMEM     :   Cannot reserve chunk of this size.
 *  Requires:
 *      pp_rsv_addr is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_reserve_memory(void *hprocessor,
				      u32 ul_size, void **pp_rsv_addr,
				      struct process_context *pr_ctxt);

/*
 *  ======== proc_un_map ========
 *  Purpose:
 *      Removes a MPU buffer mapping from the DSP address space.
 *  Parameters:
 *      hprocessor      :   The processor handle.
 *      map_addr	:   Starting address of the mapped memory region.
 *  Returns:
 *      0	 :   Success.
 *      -EFAULT     :   Invalid processor handle.
 *      -EPERM       :   General failure.
 *      -ENOENT   :   Cannot find a mapped region starting with this
 *		      :   address.
 *  Requires:
 *      map_addr is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_un_map(void *hprocessor, void *map_addr,
			      struct process_context *pr_ctxt);

/*
 *  ======== proc_un_reserve_memory ========
 *  Purpose:
 *      Frees a previously reserved region of DSP address space.
 *  Parameters:
 *      hprocessor      :   The processor handle.
 *      prsv_addr	:   Ptr to DSP side reservedBYTE address.
 *  Returns:
 *      0	 :   Success.
 *      -EFAULT     :   Invalid processor handle.
 *      -EPERM       :   General failure.
 *      -ENOENT   :   Cannot find a reserved region starting with this
 *		      :   address.
 *  Requires:
 *      prsv_addr is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
extern int proc_un_reserve_memory(void *hprocessor,
					 void *prsv_addr,
					 struct process_context *pr_ctxt);

#endif /* PROC_ */
