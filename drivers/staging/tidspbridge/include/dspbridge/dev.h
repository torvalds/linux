/*
 * dev.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Bridge Bridge driver device operations.
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

#ifndef DEV_
#define DEV_

/*  ----------------------------------- Module Dependent Headers */
#include <dspbridge/chnldefs.h>
#include <dspbridge/cmm.h>
#include <dspbridge/cod.h>
#include <dspbridge/dspdeh.h>
#include <dspbridge/nodedefs.h>
#include <dspbridge/disp.h>
#include <dspbridge/dspdefs.h>
#include <dspbridge/dmm.h>
#include <dspbridge/host_os.h>

/*  ----------------------------------- This */
#include <dspbridge/devdefs.h>

/*
 *  ======== dev_brd_write_fxn ========
 *  Purpose:
 *      Exported function to be used as the COD write function.  This function
 *      is passed a handle to a DEV_hObject by ZL in arb, then calls the
 *      device's bridge_brd_write() function.
 *  Parameters:
 *      arb:           Handle to a Device Object.
 *      dev_ctxt:    Handle to Bridge driver defined device info.
 *      dsp_addr:       Address on DSP board (Destination).
 *      host_buf:       Pointer to host buffer (Source).
 *      ul_num_bytes:     Number of bytes to transfer.
 *      mem_type:       Memory space on DSP to which to transfer.
 *  Returns:
 *      Number of bytes written.  Returns 0 if the DEV_hObject passed in via
 *      arb is invalid.
 *  Requires:
 *      DEV Initialized.
 *      host_buf != NULL
 *  Ensures:
 */
extern u32 dev_brd_write_fxn(void *arb,
			     u32 dsp_add,
			     void *host_buf, u32 ul_num_bytes, u32 mem_space);

/*
 *  ======== dev_create_device ========
 *  Purpose:
 *      Called by the operating system to load the Bridge Driver for a
 *      'Bridge device.
 *  Parameters:
 *      device_obj:     Ptr to location to receive the device object handle.
 *      driver_file_name: Name of Bridge driver PE DLL file to load.  If the
 *                      absolute path is not provided, the file is loaded
 *                      through 'Bridge's module search path.
 *      host_config:    Host configuration information, to be passed down
 *                      to the Bridge driver when bridge_dev_create() is called.
 *      pDspConfig:     DSP resources, to be passed down to the Bridge driver
 *                      when bridge_dev_create() is called.
 *      dev_node_obj:       Platform specific device node.
 *  Returns:
 *      0:            Module is loaded, device object has been created
 *      -ENOMEM:        Insufficient memory to create needed resources.
 *      -EPERM:              Unable to find Bridge driver entry point function.
 *      -ESPIPE:   Unable to load ZL DLL.
 *  Requires:
 *      DEV Initialized.
 *      device_obj != NULL.
 *      driver_file_name != NULL.
 *      host_config != NULL.
 *      pDspConfig != NULL.
 *  Ensures:
 *      0:  *device_obj will contain handle to the new device object.
 *      Otherwise, does not create the device object, ensures the Bridge driver
 *      module is unloaded, and sets *device_obj to NULL.
 */
extern int dev_create_device(struct dev_object
				    **device_obj,
				    const char *driver_file_name,
				    struct cfg_devnode *dev_node_obj);

/*
 *  ======== dev_create2 ========
 *  Purpose:
 *      After successful loading of the image from api_init_complete2
 *      (PROC Auto_Start) or proc_load this fxn is called. This creates
 *      the Node Manager and updates the DEV Object.
 *  Parameters:
 *      hdev_obj: Handle to device object created with dev_create_device().
 *  Returns:
 *      0:    Successful Creation of Node Manager
 *      -EPERM:  Some Error Occurred.
 *  Requires:
 *      DEV Initialized
 *      Valid hdev_obj
 *  Ensures:
 *      0 and hdev_obj->node_mgr != NULL
 *      else    hdev_obj->node_mgr == NULL
 */
extern int dev_create2(struct dev_object *hdev_obj);

/*
 *  ======== dev_destroy2 ========
 *  Purpose:
 *      Destroys the Node manager for this device.
 *  Parameters:
 *      hdev_obj: Handle to device object created with dev_create_device().
 *  Returns:
 *      0:    Successful Creation of Node Manager
 *      -EPERM:  Some Error Occurred.
 *  Requires:
 *      DEV Initialized
 *      Valid hdev_obj
 *  Ensures:
 *      0 and hdev_obj->node_mgr == NULL
 *      else    -EPERM.
 */
extern int dev_destroy2(struct dev_object *hdev_obj);

/*
 *  ======== dev_destroy_device ========
 *  Purpose:
 *      Destroys the channel manager for this device, if any, calls
 *      bridge_dev_destroy(), and then attempts to unload the Bridge module.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *      -EPERM:     The Bridge driver failed it's bridge_dev_destroy() function.
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 */
extern int dev_destroy_device(struct dev_object
				     *hdev_obj);

/*
 *  ======== dev_get_chnl_mgr ========
 *  Purpose:
 *      Retrieve the handle to the channel manager created for this device.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      *mgr:           Ptr to location to store handle.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      mgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *mgr contains a handle to a channel manager object,
 *                      or NULL.
 *      else:           *mgr is NULL.
 */
extern int dev_get_chnl_mgr(struct dev_object *hdev_obj,
				   struct chnl_mgr **mgr);

/*
 *  ======== dev_get_cmm_mgr ========
 *  Purpose:
 *      Retrieve the handle to the shared memory manager created for this
 *      device.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      *mgr:           Ptr to location to store handle.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      mgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *mgr contains a handle to a channel manager object,
 *                      or NULL.
 *      else:           *mgr is NULL.
 */
extern int dev_get_cmm_mgr(struct dev_object *hdev_obj,
				  struct cmm_object **mgr);

/*
 *  ======== dev_get_dmm_mgr ========
 *  Purpose:
 *      Retrieve the handle to the dynamic memory manager created for this
 *      device.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      *mgr:           Ptr to location to store handle.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      mgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *mgr contains a handle to a channel manager object,
 *                      or NULL.
 *      else:           *mgr is NULL.
 */
extern int dev_get_dmm_mgr(struct dev_object *hdev_obj,
				  struct dmm_object **mgr);

/*
 *  ======== dev_get_cod_mgr ========
 *  Purpose:
 *      Retrieve the COD manager create for this device.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      *cod_mgr:       Ptr to location to store handle.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      cod_mgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *cod_mgr contains a handle to a COD manager object.
 *      else:           *cod_mgr is NULL.
 */
extern int dev_get_cod_mgr(struct dev_object *hdev_obj,
				  struct cod_manager **cod_mgr);

/*
 *  ======== dev_get_deh_mgr ========
 *  Purpose:
 *      Retrieve the DEH manager created for this device.
 *  Parameters:
 *      hdev_obj: Handle to device object created with dev_create_device().
 *      *deh_manager:  Ptr to location to store handle.
 *  Returns:
 *      0:    Success.
 *      -EFAULT:   Invalid hdev_obj.
 *  Requires:
 *      deh_manager != NULL.
 *      DEH Initialized.
 *  Ensures:
 *      0:    *deh_manager contains a handle to a DEH manager object.
 *      else:       *deh_manager is NULL.
 */
extern int dev_get_deh_mgr(struct dev_object *hdev_obj,
				  struct deh_mgr **deh_manager);

/*
 *  ======== dev_get_dev_node ========
 *  Purpose:
 *      Retrieve the platform specific device ID for this device.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      dev_nde:        Ptr to location to get the device node handle.
 *  Returns:
 *      0:        Returns a DEVNODE in *dev_node_obj.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      dev_nde != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *dev_nde contains a platform specific device ID;
 *      else:           *dev_nde is NULL.
 */
extern int dev_get_dev_node(struct dev_object *hdev_obj,
				   struct cfg_devnode **dev_nde);

/*
 *  ======== dev_get_dev_type ========
 *  Purpose:
 *      Retrieve the platform specific device ID for this device.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      dev_nde:        Ptr to location to get the device node handle.
 *  Returns:
 *      0:        Success
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      dev_nde != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *dev_nde contains a platform specific device ID;
 *      else:           *dev_nde is NULL.
 */
extern int dev_get_dev_type(struct dev_object *device_obj,
					u8 *dev_type);

/*
 *  ======== dev_get_first ========
 *  Purpose:
 *      Retrieve the first Device Object handle from an internal linked list of
 *      of DEV_OBJECTs maintained by DEV.
 *  Parameters:
 *  Returns:
 *      NULL if there are no device objects stored; else
 *      a valid DEV_HOBJECT.
 *  Requires:
 *      No calls to dev_create_device or dev_destroy_device (which my modify the
 *      internal device object list) may occur between calls to dev_get_first
 *      and dev_get_next.
 *  Ensures:
 *      The DEV_HOBJECT returned is valid.
 *      A subsequent call to dev_get_next will return the next device object in
 *      the list.
 */
extern struct dev_object *dev_get_first(void);

/*
 *  ======== dev_get_intf_fxns ========
 *  Purpose:
 *      Retrieve the Bridge driver interface function structure for the
 *      loaded Bridge driver.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      *if_fxns:       Ptr to location to store fxn interface.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      if_fxns != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *if_fxns contains a pointer to the Bridge
 *                      driver interface;
 *      else:           *if_fxns is NULL.
 */
extern int dev_get_intf_fxns(struct dev_object *hdev_obj,
			    struct bridge_drv_interface **if_fxns);

/*
 *  ======== dev_get_io_mgr ========
 *  Purpose:
 *      Retrieve the handle to the IO manager created for this device.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      *mgr:           Ptr to location to store handle.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      mgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *mgr contains a handle to an IO manager object.
 *      else:           *mgr is NULL.
 */
extern int dev_get_io_mgr(struct dev_object *hdev_obj,
				 struct io_mgr **mgr);

/*
 *  ======== dev_get_next ========
 *  Purpose:
 *      Retrieve the next Device Object handle from an internal linked list of
 *      of DEV_OBJECTs maintained by DEV, after having previously called
 *      dev_get_first() and zero or more dev_get_next
 *  Parameters:
 *      hdev_obj: Handle to the device object returned from a previous
 *                  call to dev_get_first() or dev_get_next().
 *  Returns:
 *      NULL if there are no further device objects on the list or hdev_obj
 *      was invalid;
 *      else the next valid DEV_HOBJECT in the list.
 *  Requires:
 *      No calls to dev_create_device or dev_destroy_device (which my modify the
 *      internal device object list) may occur between calls to dev_get_first
 *      and dev_get_next.
 *  Ensures:
 *      The DEV_HOBJECT returned is valid.
 *      A subsequent call to dev_get_next will return the next device object in
 *      the list.
 */
extern struct dev_object *dev_get_next(struct dev_object
				       *hdev_obj);

/*
 *  ========= dev_get_msg_mgr ========
 *  Purpose:
 *      Retrieve the msg_ctrl Manager Handle from the DevObject.
 *  Parameters:
 *      hdev_obj: Handle to the Dev Object
 *      msg_man:    Location where msg_ctrl Manager handle will be returned.
 *  Returns:
 *  Requires:
 *      DEV Initialized.
 *      Valid hdev_obj.
 *      node_man != NULL.
 *  Ensures:
 */
extern void dev_get_msg_mgr(struct dev_object *hdev_obj,
			    struct msg_mgr **msg_man);

/*
 *  ========= dev_get_node_manager ========
 *  Purpose:
 *      Retrieve the Node Manager Handle from the DevObject. It is an
 *      accessor function
 *  Parameters:
 *      hdev_obj:     Handle to the Dev Object
 *      node_man:       Location where Handle to the Node Manager will be
 *                      returned..
 *  Returns:
 *      0:        Success
 *      -EFAULT:    Invalid Dev Object handle.
 *  Requires:
 *      DEV Initialized.
 *      node_man is not null
 *  Ensures:
 *      0:        *node_man contains a handle to a Node manager object.
 *      else:           *node_man is NULL.
 */
extern int dev_get_node_manager(struct dev_object
				       *hdev_obj,
				       struct node_mgr **node_man);

/*
 *  ======== dev_get_symbol ========
 *  Purpose:
 *      Get the value of a symbol in the currently loaded program.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      str_sym:        Name of symbol to look up.
 *      pul_value:       Ptr to symbol value.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *      -ESPIPE: Symbols couldn not be found or have not been loaded onto
 *               the board.
 *  Requires:
 *      str_sym != NULL.
 *      pul_value != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *pul_value contains the symbol value;
 */
extern int dev_get_symbol(struct dev_object *hdev_obj,
				 const char *str_sym, u32 * pul_value);

/*
 *  ======== dev_get_bridge_context ========
 *  Purpose:
 *      Retrieve the Bridge Context handle, as returned by the
 *      bridge_dev_create fxn.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with dev_create_device()
 *      *phbridge_context:  Ptr to location to store context handle.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      phbridge_context != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      0:        *phbridge_context contains context handle;
 *      else:           *phbridge_context is NULL;
 */
extern int dev_get_bridge_context(struct dev_object *hdev_obj,
				      struct bridge_dev_context
				      **phbridge_context);

/*
 *  ======== dev_exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      DEV is initialized.
 *  Ensures:
 *      When reference count == 0, DEV's private resources are freed.
 */
extern void dev_exit(void);

/*
 *  ======== dev_init ========
 *  Purpose:
 *      Initialize DEV's private state, keeping a reference count on each call.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occurred.
 *  Requires:
 *  Ensures:
 *      TRUE: A requirement for the other public DEV functions.
 */
extern bool dev_init(void);

/*
 *  ======== dev_insert_proc_object ========
 *  Purpose:
 *      Inserts the Processor Object into the List of PROC Objects
 *      kept in the DEV Object
 *  Parameters:
 *      proc_obj:    Handle to the Proc Object
 *      hdev_obj      Handle to the Dev Object
 *      bAttachedNew    Specifies if there are already processors attached
 *  Returns:
 *      0:        Successfully inserted into the list
 *  Requires:
 *      proc_obj is not NULL
 *      hdev_obj is a valid handle to the DEV.
 *      DEV Initialized.
 *      List(of Proc object in Dev) Exists.
 *  Ensures:
 *      0 & the PROC Object is inserted and the list is not empty
 *  Details:
 *      If the List of Proc Object is empty bAttachedNew is TRUE, it indicated
 *      this is the first Processor attaching.
 *      If it is False, there are already processors attached.
 */
extern int dev_insert_proc_object(struct dev_object
					 *hdev_obj,
					 u32 proc_obj,
					 bool *already_attached);

/*
 *  ======== dev_remove_proc_object ========
 *  Purpose:
 *      Search for and remove a Proc object from the given list maintained
 *      by the DEV
 *  Parameters:
 *      p_proc_object:        Ptr to ProcObject to insert.
 *      dev_obj:         Ptr to Dev Object where the list is.
 *      already_attached:  Ptr to return the bool
 *  Returns:
 *      0:            If successful.
 *      -EPERM           Failure to Remove the PROC Object from the list
 *  Requires:
 *      DevObject is Valid
 *      proc_obj != 0
 *      dev_obj->proc_list != NULL
 *      !LST_IS_EMPTY(dev_obj->proc_list)
 *      already_attached !=NULL
 *  Ensures:
 *  Details:
 *      List will be deleted when the DEV is destroyed.
 *
 */
extern int dev_remove_proc_object(struct dev_object
					 *hdev_obj, u32 proc_obj);

/*
 *  ======== dev_notify_clients ========
 *  Purpose:
 *      Notify all clients of this device of a change in device status.
 *      Clients may include multiple users of BRD, as well as CHNL.
 *      This function is asychronous, and may be called by a timer event
 *      set up by a watchdog timer.
 *  Parameters:
 *      hdev_obj:  Handle to device object created with dev_create_device().
 *      ret:         A status word, most likely a BRD_STATUS.
 *  Returns:
 *      0:     All registered clients were asynchronously notified.
 *      -EINVAL:   Invalid hdev_obj.
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 *      0: Notifications are queued by the operating system to be
 *      delivered to clients.  This function does not ensure that
 *      the notifications will ever be delivered.
 */
extern int dev_notify_clients(struct dev_object *hdev_obj, u32 ret);

/*
 *  ======== dev_remove_device ========
 *  Purpose:
 *      Destroys the Device Object created by dev_start_device.
 *  Parameters:
 *      dev_node_obj:       Device node as it is know to OS.
 *  Returns:
 *      0:        If success;
 *      <error code>    Otherwise.
 *  Requires:
 *  Ensures:
 */
extern int dev_remove_device(struct cfg_devnode *dev_node_obj);

/*
 *  ======== dev_set_chnl_mgr ========
 *  Purpose:
 *      Set the channel manager for this device.
 *  Parameters:
 *      hdev_obj:     Handle to device object created with
 *                      dev_create_device().
 *      hmgr:           Handle to a channel manager, or NULL.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hdev_obj.
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 */
extern int dev_set_chnl_mgr(struct dev_object *hdev_obj,
				   struct chnl_mgr *hmgr);

/*
 *  ======== dev_set_msg_mgr ========
 *  Purpose:
 *      Set the Message manager for this device.
 *  Parameters:
 *      hdev_obj: Handle to device object created with dev_create_device().
 *      hmgr:       Handle to a message manager, or NULL.
 *  Returns:
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 */
extern void dev_set_msg_mgr(struct dev_object *hdev_obj, struct msg_mgr *hmgr);

/*
 *  ======== dev_start_device ========
 *  Purpose:
 *      Initializes the new device with bridge environment.  This involves
 *      querying CM for allocated resources, querying the registry for
 *      necessary dsp resources (requested in the INF file), and using this
 *      information to create a bridge device object.
 *  Parameters:
 *      dev_node_obj:       Device node as it is know to OS.
 *  Returns:
 *      0:        If success;
 *      <error code>    Otherwise.
 *  Requires:
 *      DEV initialized.
 *  Ensures:
 */
extern int dev_start_device(struct cfg_devnode *dev_node_obj);

#endif /* DEV_ */
