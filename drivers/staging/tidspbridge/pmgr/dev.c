/*
 * dev.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implementation of Bridge Bridge driver device operations.
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
#include <linux/types.h>
#include <linux/list.h>

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/cod.h>
#include <dspbridge/drv.h>
#include <dspbridge/proc.h>
#include <dspbridge/dmm.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/mgr.h>
#include <dspbridge/node.h>

/*  ----------------------------------- Others */
#include <dspbridge/dspapi.h>	/* DSP API version info. */

#include <dspbridge/chnl.h>
#include <dspbridge/io.h>
#include <dspbridge/msg.h>
#include <dspbridge/cmm.h>
#include <dspbridge/dspdeh.h>

/*  ----------------------------------- This */
#include <dspbridge/dev.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */

#define MAKEVERSION(major, minor)   (major * 10 + minor)
#define BRD_API_VERSION		MAKEVERSION(BRD_API_MAJOR_VERSION,	\
				BRD_API_MINOR_VERSION)

/* The Bridge device object: */
struct dev_object {
	struct list_head link;	/* Link to next dev_object. */
	u8 dev_type;		/* Device Type */
	struct cfg_devnode *dev_node_obj;	/* Platform specific dev id */
	/* Bridge Context Handle */
	struct bridge_dev_context *bridge_context;
	/* Function interface to Bridge driver. */
	struct bridge_drv_interface bridge_interface;
	struct brd_object *lock_owner;	/* Client with exclusive access. */
	struct cod_manager *cod_mgr;	/* Code manager handle. */
	struct chnl_mgr *chnl_mgr;	/* Channel manager. */
	struct deh_mgr *deh_mgr;	/* DEH manager. */
	struct msg_mgr *msg_mgr;	/* Message manager. */
	struct io_mgr *iomgr;	/* IO manager (CHNL, msg_ctrl) */
	struct cmm_object *cmm_mgr;	/* SM memory manager. */
	struct dmm_object *dmm_mgr;	/* Dynamic memory manager. */
	u32 word_size;		/* DSP word size: quick access. */
	struct drv_object *drv_obj;	/* Driver Object */
	/* List of Processors attached to this device */
	struct list_head proc_list;
	struct node_mgr *node_mgr;
};

struct drv_ext {
	struct list_head link;
	char sz_string[MAXREGPATHLENGTH];
};

/*  ----------------------------------- Function Prototypes */
static int fxn_not_implemented(int arg, ...);
static int init_cod_mgr(struct dev_object *dev_obj);
static void store_interface_fxns(struct bridge_drv_interface *drv_fxns,
				 struct bridge_drv_interface *intf_fxns);
/*
 *  ======== dev_brd_write_fxn ========
 *  Purpose:
 *      Exported function to be used as the COD write function.  This function
 *      is passed a handle to a DEV_hObject, then calls the
 *      device's bridge_brd_write() function.
 */
u32 dev_brd_write_fxn(void *arb, u32 dsp_add, void *host_buf,
		      u32 ul_num_bytes, u32 mem_space)
{
	struct dev_object *dev_obj = (struct dev_object *)arb;
	u32 ul_written = 0;
	int status;

	if (dev_obj) {
		/* Require of BrdWrite() */
		status = (*dev_obj->bridge_interface.brd_write) (
					dev_obj->bridge_context, host_buf,
					dsp_add, ul_num_bytes, mem_space);
		/* Special case of getting the address only */
		if (ul_num_bytes == 0)
			ul_num_bytes = 1;
		if (!status)
			ul_written = ul_num_bytes;

	}
	return ul_written;
}

/*
 *  ======== dev_create_device ========
 *  Purpose:
 *      Called by the operating system to load the PM Bridge Driver for a
 *      PM board (device).
 */
int dev_create_device(struct dev_object **device_obj,
			     const char *driver_file_name,
			     struct cfg_devnode *dev_node_obj)
{
	struct cfg_hostres *host_res;
	struct bridge_drv_interface *drv_fxns = NULL;
	struct dev_object *dev_obj = NULL;
	struct chnl_mgrattrs mgr_attrs;
	struct io_attrs io_mgr_attrs;
	u32 num_windows;
	struct drv_object *hdrv_obj = NULL;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);
	int status = 0;

	status = drv_request_bridge_res_dsp((void *)&host_res);

	if (status) {
		dev_dbg(bridge, "%s: Failed to reserve bridge resources\n",
			__func__);
		goto leave;
	}

	/*  Get the Bridge driver interface functions */
	bridge_drv_entry(&drv_fxns, driver_file_name);

	/* Retrieve the Object handle from the driver data */
	if (drv_datap && drv_datap->drv_object) {
		hdrv_obj = drv_datap->drv_object;
	} else {
		status = -EPERM;
		pr_err("%s: Failed to retrieve the object handle\n", __func__);
	}

	/* Create the device object, and pass a handle to the Bridge driver for
	 * storage. */
	if (!status) {
		dev_obj = kzalloc(sizeof(struct dev_object), GFP_KERNEL);
		if (dev_obj) {
			/* Fill out the rest of the Dev Object structure: */
			dev_obj->dev_node_obj = dev_node_obj;
			dev_obj->cod_mgr = NULL;
			dev_obj->chnl_mgr = NULL;
			dev_obj->deh_mgr = NULL;
			dev_obj->lock_owner = NULL;
			dev_obj->word_size = DSPWORDSIZE;
			dev_obj->drv_obj = hdrv_obj;
			dev_obj->dev_type = DSP_UNIT;
			/* Store this Bridge's interface functions, based on its
			 * version. */
			store_interface_fxns(drv_fxns,
						&dev_obj->bridge_interface);

			/* Call fxn_dev_create() to get the Bridge's device
			 * context handle. */
			status = (dev_obj->bridge_interface.dev_create)
			    (&dev_obj->bridge_context, dev_obj,
			     host_res);
		} else {
			status = -ENOMEM;
		}
	}
	/* Attempt to create the COD manager for this device: */
	if (!status)
		status = init_cod_mgr(dev_obj);

	/* Attempt to create the channel manager for this device: */
	if (!status) {
		mgr_attrs.max_channels = CHNL_MAXCHANNELS;
		io_mgr_attrs.birq = host_res->birq_registers;
		io_mgr_attrs.irq_shared =
		    (host_res->birq_attrib & CFG_IRQSHARED);
		io_mgr_attrs.word_size = DSPWORDSIZE;
		mgr_attrs.word_size = DSPWORDSIZE;
		num_windows = host_res->num_mem_windows;
		if (num_windows) {
			/* Assume last memory window is for CHNL */
			io_mgr_attrs.shm_base = host_res->mem_base[1] +
			    host_res->offset_for_monitor;
			io_mgr_attrs.sm_length =
			    host_res->mem_length[1] -
			    host_res->offset_for_monitor;
		} else {
			io_mgr_attrs.shm_base = 0;
			io_mgr_attrs.sm_length = 0;
			pr_err("%s: No memory reserved for shared structures\n",
			       __func__);
		}
		status = chnl_create(&dev_obj->chnl_mgr, dev_obj, &mgr_attrs);
		if (status == -ENOSYS) {
			/* It's OK for a device not to have a channel
			 * manager: */
			status = 0;
		}
		/* Create CMM mgr even if Msg Mgr not impl. */
		status = cmm_create(&dev_obj->cmm_mgr,
				    (struct dev_object *)dev_obj, NULL);
		/* Only create IO manager if we have a channel manager */
		if (!status && dev_obj->chnl_mgr) {
			status = io_create(&dev_obj->iomgr, dev_obj,
					   &io_mgr_attrs);
		}
		/* Only create DEH manager if we have an IO manager */
		if (!status) {
			/* Instantiate the DEH module */
			status = bridge_deh_create(&dev_obj->deh_mgr, dev_obj);
		}
		/* Create DMM mgr . */
		status = dmm_create(&dev_obj->dmm_mgr,
				    (struct dev_object *)dev_obj, NULL);
	}
	/* Add the new DEV_Object to the global list: */
	if (!status)
		status = drv_insert_dev_object(hdrv_obj, dev_obj);

	/* Create the Processor List */
	if (!status)
		INIT_LIST_HEAD(&dev_obj->proc_list);
leave:
	/*  If all went well, return a handle to the dev object;
	 *  else, cleanup and return NULL in the OUT parameter. */
	if (!status) {
		*device_obj = dev_obj;
	} else {
		if (dev_obj) {
			if (dev_obj->cod_mgr)
				cod_delete(dev_obj->cod_mgr);
			if (dev_obj->dmm_mgr)
				dmm_destroy(dev_obj->dmm_mgr);
			kfree(dev_obj);
		}

		*device_obj = NULL;
	}

	return status;
}

/*
 *  ======== dev_create2 ========
 *  Purpose:
 *      After successful loading of the image from api_init_complete2
 *      (PROC Auto_Start) or proc_load this fxn is called. This creates
 *      the Node Manager and updates the DEV Object.
 */
int dev_create2(struct dev_object *hdev_obj)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	/* There can be only one Node Manager per DEV object */
	status = node_create_mgr(&dev_obj->node_mgr, hdev_obj);
	if (status)
		dev_obj->node_mgr = NULL;

	return status;
}

/*
 *  ======== dev_destroy2 ========
 *  Purpose:
 *      Destroys the Node manager for this device.
 */
int dev_destroy2(struct dev_object *hdev_obj)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (dev_obj->node_mgr) {
		if (node_delete_mgr(dev_obj->node_mgr))
			status = -EPERM;
		else
			dev_obj->node_mgr = NULL;

	}

	return status;
}

/*
 *  ======== dev_destroy_device ========
 *  Purpose:
 *      Destroys the channel manager for this device, if any, calls
 *      bridge_dev_destroy(), and then attempts to unload the Bridge module.
 */
int dev_destroy_device(struct dev_object *hdev_obj)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		if (dev_obj->cod_mgr) {
			cod_delete(dev_obj->cod_mgr);
			dev_obj->cod_mgr = NULL;
		}

		if (dev_obj->node_mgr) {
			node_delete_mgr(dev_obj->node_mgr);
			dev_obj->node_mgr = NULL;
		}

		/* Free the io, channel, and message managers for this board: */
		if (dev_obj->iomgr) {
			io_destroy(dev_obj->iomgr);
			dev_obj->iomgr = NULL;
		}
		if (dev_obj->chnl_mgr) {
			chnl_destroy(dev_obj->chnl_mgr);
			dev_obj->chnl_mgr = NULL;
		}
		if (dev_obj->msg_mgr) {
			msg_delete(dev_obj->msg_mgr);
			dev_obj->msg_mgr = NULL;
		}

		if (dev_obj->deh_mgr) {
			/* Uninitialize DEH module. */
			bridge_deh_destroy(dev_obj->deh_mgr);
			dev_obj->deh_mgr = NULL;
		}
		if (dev_obj->cmm_mgr) {
			cmm_destroy(dev_obj->cmm_mgr, true);
			dev_obj->cmm_mgr = NULL;
		}

		if (dev_obj->dmm_mgr) {
			dmm_destroy(dev_obj->dmm_mgr);
			dev_obj->dmm_mgr = NULL;
		}

		/* Call the driver's bridge_dev_destroy() function: */
		/* Require of DevDestroy */
		if (dev_obj->bridge_context) {
			status = (*dev_obj->bridge_interface.dev_destroy)
			    (dev_obj->bridge_context);
			dev_obj->bridge_context = NULL;
		} else
			status = -EPERM;
		if (!status) {
			/* Remove this DEV_Object from the global list: */
			drv_remove_dev_object(dev_obj->drv_obj, dev_obj);
			/* Free The library * LDR_FreeModule
			 * (dev_obj->module_obj); */
			/* Free this dev object: */
			kfree(dev_obj);
			dev_obj = NULL;
		}
	} else {
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_get_chnl_mgr ========
 *  Purpose:
 *      Retrieve the handle to the channel manager handle created for this
 *      device.
 */
int dev_get_chnl_mgr(struct dev_object *hdev_obj,
			    struct chnl_mgr **mgr)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		*mgr = dev_obj->chnl_mgr;
	} else {
		*mgr = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_get_cmm_mgr ========
 *  Purpose:
 *      Retrieve the handle to the shared memory manager created for this
 *      device.
 */
int dev_get_cmm_mgr(struct dev_object *hdev_obj,
			   struct cmm_object **mgr)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		*mgr = dev_obj->cmm_mgr;
	} else {
		*mgr = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_get_dmm_mgr ========
 *  Purpose:
 *      Retrieve the handle to the dynamic memory manager created for this
 *      device.
 */
int dev_get_dmm_mgr(struct dev_object *hdev_obj,
			   struct dmm_object **mgr)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		*mgr = dev_obj->dmm_mgr;
	} else {
		*mgr = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_get_cod_mgr ========
 *  Purpose:
 *      Retrieve the COD manager create for this device.
 */
int dev_get_cod_mgr(struct dev_object *hdev_obj,
			   struct cod_manager **cod_mgr)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		*cod_mgr = dev_obj->cod_mgr;
	} else {
		*cod_mgr = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ========= dev_get_deh_mgr ========
 */
int dev_get_deh_mgr(struct dev_object *hdev_obj,
			   struct deh_mgr **deh_manager)
{
	int status = 0;

	if (hdev_obj) {
		*deh_manager = hdev_obj->deh_mgr;
	} else {
		*deh_manager = NULL;
		status = -EFAULT;
	}
	return status;
}

/*
 *  ======== dev_get_dev_node ========
 *  Purpose:
 *      Retrieve the platform specific device ID for this device.
 */
int dev_get_dev_node(struct dev_object *hdev_obj,
			    struct cfg_devnode **dev_nde)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		*dev_nde = dev_obj->dev_node_obj;
	} else {
		*dev_nde = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_get_first ========
 *  Purpose:
 *      Retrieve the first Device Object handle from an internal linked list
 *      DEV_OBJECTs maintained by DEV.
 */
struct dev_object *dev_get_first(void)
{
	struct dev_object *dev_obj = NULL;

	dev_obj = (struct dev_object *)drv_get_first_dev_object();

	return dev_obj;
}

/*
 *  ======== dev_get_intf_fxns ========
 *  Purpose:
 *      Retrieve the Bridge interface function structure for the loaded driver.
 *      if_fxns != NULL.
 */
int dev_get_intf_fxns(struct dev_object *hdev_obj,
			     struct bridge_drv_interface **if_fxns)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		*if_fxns = &dev_obj->bridge_interface;
	} else {
		*if_fxns = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ========= dev_get_io_mgr ========
 */
int dev_get_io_mgr(struct dev_object *hdev_obj,
			  struct io_mgr **io_man)
{
	int status = 0;

	if (hdev_obj) {
		*io_man = hdev_obj->iomgr;
	} else {
		*io_man = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_get_next ========
 *  Purpose:
 *      Retrieve the next Device Object handle from an internal linked list
 *      of DEV_OBJECTs maintained by DEV, after having previously called
 *      dev_get_first() and zero or more dev_get_next
 */
struct dev_object *dev_get_next(struct dev_object *hdev_obj)
{
	struct dev_object *next_dev_object = NULL;

	if (hdev_obj) {
		next_dev_object = (struct dev_object *)
		    drv_get_next_dev_object((u32) hdev_obj);
	}

	return next_dev_object;
}

/*
 *  ========= dev_get_msg_mgr ========
 */
void dev_get_msg_mgr(struct dev_object *hdev_obj, struct msg_mgr **msg_man)
{
	*msg_man = hdev_obj->msg_mgr;
}

/*
 *  ======== dev_get_node_manager ========
 *  Purpose:
 *      Retrieve the Node Manager Handle
 */
int dev_get_node_manager(struct dev_object *hdev_obj,
				struct node_mgr **node_man)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		*node_man = dev_obj->node_mgr;
	} else {
		*node_man = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_get_symbol ========
 */
int dev_get_symbol(struct dev_object *hdev_obj,
			  const char *str_sym, u32 * pul_value)
{
	int status = 0;
	struct cod_manager *cod_mgr;

	if (hdev_obj) {
		status = dev_get_cod_mgr(hdev_obj, &cod_mgr);
		if (cod_mgr)
			status = cod_get_sym_value(cod_mgr, (char *)str_sym,
						   pul_value);
		else
			status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_get_bridge_context ========
 *  Purpose:
 *      Retrieve the Bridge Context handle, as returned by the
 *      bridge_dev_create fxn.
 */
int dev_get_bridge_context(struct dev_object *hdev_obj,
			       struct bridge_dev_context **phbridge_context)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj) {
		*phbridge_context = dev_obj->bridge_context;
	} else {
		*phbridge_context = NULL;
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== dev_notify_clients ========
 *  Purpose:
 *      Notify all clients of this device of a change in device status.
 */
int dev_notify_clients(struct dev_object *dev_obj, u32 ret)
{
	struct list_head *curr;

	/*
	 * FIXME: this code needs struct proc_object to have a list_head
	 * at the beginning. If not, this can go horribly wrong.
	 */
	list_for_each(curr, &dev_obj->proc_list)
		proc_notify_clients((void *)curr, ret);

	return 0;
}

/*
 *  ======== dev_remove_device ========
 */
int dev_remove_device(struct cfg_devnode *dev_node_obj)
{
	struct dev_object *hdev_obj;	/* handle to device object */
	int status = 0;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);

	if (!drv_datap)
		status = -ENODATA;

	if (!dev_node_obj)
		status = -EFAULT;

	/* Retrieve the device object handle originally stored with
	 * the dev_node: */
	if (!status) {
		/* check the device string and then store dev object */
		if (!strcmp((char *)((struct drv_ext *)dev_node_obj)->sz_string,
								"TIOMAP1510")) {
			hdev_obj = drv_datap->dev_object;
			/* Destroy the device object. */
			status = dev_destroy_device(hdev_obj);
		} else {
			status = -EPERM;
		}
	}

	if (status)
		pr_err("%s: Failed, status 0x%x\n", __func__, status);

	return status;
}

/*
 *  ======== dev_set_chnl_mgr ========
 *  Purpose:
 *      Set the channel manager for this device.
 */
int dev_set_chnl_mgr(struct dev_object *hdev_obj,
			    struct chnl_mgr *hmgr)
{
	int status = 0;
	struct dev_object *dev_obj = hdev_obj;

	if (hdev_obj)
		dev_obj->chnl_mgr = hmgr;
	else
		status = -EFAULT;

	return status;
}

/*
 *  ======== dev_set_msg_mgr ========
 *  Purpose:
 *      Set the message manager for this device.
 */
void dev_set_msg_mgr(struct dev_object *hdev_obj, struct msg_mgr *hmgr)
{
	hdev_obj->msg_mgr = hmgr;
}

/*
 *  ======== dev_start_device ========
 *  Purpose:
 *      Initializes the new device with the BRIDGE environment.
 */
int dev_start_device(struct cfg_devnode *dev_node_obj)
{
	struct dev_object *hdev_obj = NULL;	/* handle to 'Bridge Device */
	/* Bridge driver filename */
	char *bridge_file_name = "UMA";
	int status;
	struct mgr_object *hmgr_obj = NULL;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);

	/* Given all resources, create a device object. */
	status = dev_create_device(&hdev_obj, bridge_file_name,
				   dev_node_obj);
	if (!status) {
		/* Store away the hdev_obj with the DEVNODE */
		if (!drv_datap || !dev_node_obj) {
			status = -EFAULT;
			pr_err("%s: Failed, status 0x%x\n", __func__, status);
		} else if (!(strcmp((char *)dev_node_obj, "TIOMAP1510"))) {
			drv_datap->dev_object = (void *) hdev_obj;
		}
		if (!status) {
			/* Create the Manager Object */
			status = mgr_create(&hmgr_obj, dev_node_obj);
			if (status && !(strcmp((char *)dev_node_obj,
							"TIOMAP1510"))) {
				/* Ensure the device extension is NULL */
				drv_datap->dev_object = NULL;
			}
		}
		if (status) {
			/* Clean up */
			dev_destroy_device(hdev_obj);
			hdev_obj = NULL;
		}
	}

	return status;
}

/*
 *  ======== fxn_not_implemented ========
 *  Purpose:
 *      Takes the place of a Bridge Null Function.
 *  Parameters:
 *      Multiple, optional.
 *  Returns:
 *      -ENOSYS:   Always.
 */
static int fxn_not_implemented(int arg, ...)
{
	return -ENOSYS;
}

/*
 *  ======== init_cod_mgr ========
 *  Purpose:
 *      Create a COD manager for this device.
 *  Parameters:
 *      dev_obj:             Pointer to device object created with
 *                              dev_create_device()
 *  Returns:
 *      0:                Success.
 *      -EFAULT:            Invalid hdev_obj.
 *  Requires:
 *      Should only be called once by dev_create_device() for a given DevObject.
 *  Ensures:
 */
static int init_cod_mgr(struct dev_object *dev_obj)
{
	int status = 0;
	char *sz_dummy_file = "dummy";

	status = cod_create(&dev_obj->cod_mgr, sz_dummy_file);

	return status;
}

/*
 *  ======== dev_insert_proc_object ========
 *  Purpose:
 *      Insert a ProcObject into the list maintained by DEV.
 *  Parameters:
 *      p_proc_object:        Ptr to ProcObject to insert.
 *      dev_obj:         Ptr to Dev Object where the list is.
  *     already_attached:  Ptr to return the bool
 *  Returns:
 *      0:           If successful.
 *  Requires:
 *      List Exists
 *      hdev_obj is Valid handle
 *      DEV Initialized
 *      already_attached != NULL
 *      proc_obj != 0
 *  Ensures:
 *      0 and List is not Empty.
 */
int dev_insert_proc_object(struct dev_object *hdev_obj,
				  u32 proc_obj, bool *already_attached)
{
	struct dev_object *dev_obj = (struct dev_object *)hdev_obj;

	if (!list_empty(&dev_obj->proc_list))
		*already_attached = true;

	/* Add DevObject to tail. */
	/*
	 * FIXME: this code needs struct proc_object to have a list_head
	 * at the beginning. If not, this can go horribly wrong.
	 */
	list_add_tail((struct list_head *)proc_obj, &dev_obj->proc_list);

	return 0;
}

/*
 *  ======== dev_remove_proc_object ========
 *  Purpose:
 *      Search for and remove a Proc object from the given list maintained
 *      by the DEV
 *  Parameters:
 *      p_proc_object:        Ptr to ProcObject to insert.
 *      dev_obj          Ptr to Dev Object where the list is.
 *  Returns:
 *      0:            If successful.
 *  Requires:
 *      List exists and is not empty
 *      proc_obj != 0
 *      hdev_obj is a valid Dev handle.
 *  Ensures:
 *  Details:
 *      List will be deleted when the DEV is destroyed.
 */
int dev_remove_proc_object(struct dev_object *hdev_obj, u32 proc_obj)
{
	int status = -EPERM;
	struct list_head *cur_elem;
	struct dev_object *dev_obj = (struct dev_object *)hdev_obj;

	/* Search list for dev_obj: */
	list_for_each(cur_elem, &dev_obj->proc_list) {
		if ((u32) cur_elem == proc_obj) {
			list_del(cur_elem);
			status = 0;
			break;
		}
	}

	return status;
}

int dev_get_dev_type(struct dev_object *dev_obj, u8 *dev_type)
{
	*dev_type = dev_obj->dev_type;
	return 0;
}

/*
 *  ======== store_interface_fxns ========
 *  Purpose:
 *      Copy the Bridge's interface functions into the device object,
 *      ensuring that fxn_not_implemented() is set for:
 *
 *      1. All Bridge function pointers which are NULL; and
 *      2. All function slots in the struct dev_object structure which have no
 *         corresponding slots in the the Bridge's interface, because the Bridge
 *         is of an *older* version.
 *  Parameters:
 *      intf_fxns:      Interface fxn Structure of the Bridge's Dev Object.
 *      drv_fxns:      Interface Fxns offered by the Bridge during DEV_Create().
 *  Returns:
 *  Requires:
 *      Input pointers are valid.
 *      Bridge driver is *not* written for a newer DSP API.
 *  Ensures:
 *      All function pointers in the dev object's fxn interface are not NULL.
 */
static void store_interface_fxns(struct bridge_drv_interface *drv_fxns,
				 struct bridge_drv_interface *intf_fxns)
{
	u32 bridge_version;

	/* Local helper macro: */
#define  STORE_FXN(cast, pfn) \
    (intf_fxns->pfn = ((drv_fxns->pfn != NULL) ? drv_fxns->pfn : \
    (cast)fxn_not_implemented))

	bridge_version = MAKEVERSION(drv_fxns->brd_api_major_version,
				     drv_fxns->brd_api_minor_version);
	intf_fxns->brd_api_major_version = drv_fxns->brd_api_major_version;
	intf_fxns->brd_api_minor_version = drv_fxns->brd_api_minor_version;
	/* Install functions up to DSP API version .80 (first alpha): */
	if (bridge_version > 0) {
		STORE_FXN(fxn_dev_create, dev_create);
		STORE_FXN(fxn_dev_destroy, dev_destroy);
		STORE_FXN(fxn_dev_ctrl, dev_cntrl);
		STORE_FXN(fxn_brd_monitor, brd_monitor);
		STORE_FXN(fxn_brd_start, brd_start);
		STORE_FXN(fxn_brd_stop, brd_stop);
		STORE_FXN(fxn_brd_status, brd_status);
		STORE_FXN(fxn_brd_read, brd_read);
		STORE_FXN(fxn_brd_write, brd_write);
		STORE_FXN(fxn_brd_setstate, brd_set_state);
		STORE_FXN(fxn_brd_memcopy, brd_mem_copy);
		STORE_FXN(fxn_brd_memwrite, brd_mem_write);
		STORE_FXN(fxn_brd_memmap, brd_mem_map);
		STORE_FXN(fxn_brd_memunmap, brd_mem_un_map);
		STORE_FXN(fxn_chnl_create, chnl_create);
		STORE_FXN(fxn_chnl_destroy, chnl_destroy);
		STORE_FXN(fxn_chnl_open, chnl_open);
		STORE_FXN(fxn_chnl_close, chnl_close);
		STORE_FXN(fxn_chnl_addioreq, chnl_add_io_req);
		STORE_FXN(fxn_chnl_getioc, chnl_get_ioc);
		STORE_FXN(fxn_chnl_cancelio, chnl_cancel_io);
		STORE_FXN(fxn_chnl_flushio, chnl_flush_io);
		STORE_FXN(fxn_chnl_getinfo, chnl_get_info);
		STORE_FXN(fxn_chnl_getmgrinfo, chnl_get_mgr_info);
		STORE_FXN(fxn_chnl_idle, chnl_idle);
		STORE_FXN(fxn_chnl_registernotify, chnl_register_notify);
		STORE_FXN(fxn_io_create, io_create);
		STORE_FXN(fxn_io_destroy, io_destroy);
		STORE_FXN(fxn_io_onloaded, io_on_loaded);
		STORE_FXN(fxn_io_getprocload, io_get_proc_load);
		STORE_FXN(fxn_msg_create, msg_create);
		STORE_FXN(fxn_msg_createqueue, msg_create_queue);
		STORE_FXN(fxn_msg_delete, msg_delete);
		STORE_FXN(fxn_msg_deletequeue, msg_delete_queue);
		STORE_FXN(fxn_msg_get, msg_get);
		STORE_FXN(fxn_msg_put, msg_put);
		STORE_FXN(fxn_msg_registernotify, msg_register_notify);
		STORE_FXN(fxn_msg_setqueueid, msg_set_queue_id);
	}
	/* Add code for any additional functions in newerBridge versions here */
#undef  STORE_FXN
}
