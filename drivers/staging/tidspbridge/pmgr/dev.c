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

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/ldr.h>
#include <dspbridge/list.h>

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
	/* LST requires "link" to be first field! */
	struct list_head link;	/* Link to next dev_object. */
	u8 dev_type;		/* Device Type */
	struct cfg_devnode *dev_node_obj;	/* Platform specific dev id */
	/* Bridge Context Handle */
	struct bridge_dev_context *hbridge_context;
	/* Function interface to Bridge driver. */
	struct bridge_drv_interface bridge_interface;
	struct brd_object *lock_owner;	/* Client with exclusive access. */
	struct cod_manager *cod_mgr;	/* Code manager handle. */
	struct chnl_mgr *hchnl_mgr;	/* Channel manager. */
	struct deh_mgr *hdeh_mgr;	/* DEH manager. */
	struct msg_mgr *hmsg_mgr;	/* Message manager. */
	struct io_mgr *hio_mgr;	/* IO manager (CHNL, msg_ctrl) */
	struct cmm_object *hcmm_mgr;	/* SM memory manager. */
	struct dmm_object *dmm_mgr;	/* Dynamic memory manager. */
	struct ldr_module *module_obj;	/* Bridge Module handle. */
	u32 word_size;		/* DSP word size: quick access. */
	struct drv_object *hdrv_obj;	/* Driver Object */
	struct lst_list *proc_list;	/* List of Proceeosr attached to
					 * this device */
	struct node_mgr *hnode_mgr;
};

struct drv_ext {
	struct list_head link;
	char sz_string[MAXREGPATHLENGTH];
};

/*  ----------------------------------- Globals */
static u32 refs;		/* Module reference count */

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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(host_buf != NULL);	/* Required of BrdWrite(). */
	if (dev_obj) {
		/* Require of BrdWrite() */
		DBC_ASSERT(dev_obj->hbridge_context != NULL);
		status = (*dev_obj->bridge_interface.pfn_brd_write) (
					dev_obj->hbridge_context, host_buf,
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
	struct ldr_module *module_obj = NULL;
	struct bridge_drv_interface *drv_fxns = NULL;
	struct dev_object *dev_obj = NULL;
	struct chnl_mgrattrs mgr_attrs;
	struct io_attrs io_mgr_attrs;
	u32 num_windows;
	struct drv_object *hdrv_obj = NULL;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);
	int status = 0;
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(device_obj != NULL);
	DBC_REQUIRE(driver_file_name != NULL);

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
		DBC_ASSERT(drv_fxns);
		dev_obj = kzalloc(sizeof(struct dev_object), GFP_KERNEL);
		if (dev_obj) {
			/* Fill out the rest of the Dev Object structure: */
			dev_obj->dev_node_obj = dev_node_obj;
			dev_obj->module_obj = module_obj;
			dev_obj->cod_mgr = NULL;
			dev_obj->hchnl_mgr = NULL;
			dev_obj->hdeh_mgr = NULL;
			dev_obj->lock_owner = NULL;
			dev_obj->word_size = DSPWORDSIZE;
			dev_obj->hdrv_obj = hdrv_obj;
			dev_obj->dev_type = DSP_UNIT;
			/* Store this Bridge's interface functions, based on its
			 * version. */
			store_interface_fxns(drv_fxns,
						&dev_obj->bridge_interface);

			/* Call fxn_dev_create() to get the Bridge's device
			 * context handle. */
			status = (dev_obj->bridge_interface.pfn_dev_create)
			    (&dev_obj->hbridge_context, dev_obj,
			     host_res);
			/* Assert bridge_dev_create()'s ensure clause: */
			DBC_ASSERT(status
				   || (dev_obj->hbridge_context != NULL));
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
			io_mgr_attrs.shm_base = host_res->dw_mem_base[1] +
			    host_res->dw_offset_for_monitor;
			io_mgr_attrs.usm_length =
			    host_res->dw_mem_length[1] -
			    host_res->dw_offset_for_monitor;
		} else {
			io_mgr_attrs.shm_base = 0;
			io_mgr_attrs.usm_length = 0;
			pr_err("%s: No memory reserved for shared structures\n",
			       __func__);
		}
		status = chnl_create(&dev_obj->hchnl_mgr, dev_obj, &mgr_attrs);
		if (status == -ENOSYS) {
			/* It's OK for a device not to have a channel
			 * manager: */
			status = 0;
		}
		/* Create CMM mgr even if Msg Mgr not impl. */
		status = cmm_create(&dev_obj->hcmm_mgr,
				    (struct dev_object *)dev_obj, NULL);
		/* Only create IO manager if we have a channel manager */
		if (!status && dev_obj->hchnl_mgr) {
			status = io_create(&dev_obj->hio_mgr, dev_obj,
					   &io_mgr_attrs);
		}
		/* Only create DEH manager if we have an IO manager */
		if (!status) {
			/* Instantiate the DEH module */
			status = bridge_deh_create(&dev_obj->hdeh_mgr, dev_obj);
		}
		/* Create DMM mgr . */
		status = dmm_create(&dev_obj->dmm_mgr,
				    (struct dev_object *)dev_obj, NULL);
	}
	/* Add the new DEV_Object to the global list: */
	if (!status) {
		lst_init_elem(&dev_obj->link);
		status = drv_insert_dev_object(hdrv_obj, dev_obj);
	}
	/* Create the Processor List */
	if (!status) {
		dev_obj->proc_list = kzalloc(sizeof(struct lst_list),
							GFP_KERNEL);
		if (!(dev_obj->proc_list))
			status = -EPERM;
		else
			INIT_LIST_HEAD(&dev_obj->proc_list->head);
	}
leave:
	/*  If all went well, return a handle to the dev object;
	 *  else, cleanup and return NULL in the OUT parameter. */
	if (!status) {
		*device_obj = dev_obj;
	} else {
		if (dev_obj) {
			kfree(dev_obj->proc_list);
			if (dev_obj->cod_mgr)
				cod_delete(dev_obj->cod_mgr);
			if (dev_obj->dmm_mgr)
				dmm_destroy(dev_obj->dmm_mgr);
			kfree(dev_obj);
		}

		*device_obj = NULL;
	}

	DBC_ENSURE((!status && *device_obj) || (status && !*device_obj));
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hdev_obj);

	/* There can be only one Node Manager per DEV object */
	DBC_ASSERT(!dev_obj->hnode_mgr);
	status = node_create_mgr(&dev_obj->hnode_mgr, hdev_obj);
	if (status)
		dev_obj->hnode_mgr = NULL;

	DBC_ENSURE((!status && dev_obj->hnode_mgr != NULL)
		   || (status && dev_obj->hnode_mgr == NULL));
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hdev_obj);

	if (dev_obj->hnode_mgr) {
		if (node_delete_mgr(dev_obj->hnode_mgr))
			status = -EPERM;
		else
			dev_obj->hnode_mgr = NULL;

	}

	DBC_ENSURE((!status && dev_obj->hnode_mgr == NULL) || status);
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

	DBC_REQUIRE(refs > 0);

	if (hdev_obj) {
		if (dev_obj->cod_mgr) {
			cod_delete(dev_obj->cod_mgr);
			dev_obj->cod_mgr = NULL;
		}

		if (dev_obj->hnode_mgr) {
			node_delete_mgr(dev_obj->hnode_mgr);
			dev_obj->hnode_mgr = NULL;
		}

		/* Free the io, channel, and message managers for this board: */
		if (dev_obj->hio_mgr) {
			io_destroy(dev_obj->hio_mgr);
			dev_obj->hio_mgr = NULL;
		}
		if (dev_obj->hchnl_mgr) {
			chnl_destroy(dev_obj->hchnl_mgr);
			dev_obj->hchnl_mgr = NULL;
		}
		if (dev_obj->hmsg_mgr) {
			msg_delete(dev_obj->hmsg_mgr);
			dev_obj->hmsg_mgr = NULL;
		}

		if (dev_obj->hdeh_mgr) {
			/* Uninitialize DEH module. */
			bridge_deh_destroy(dev_obj->hdeh_mgr);
			dev_obj->hdeh_mgr = NULL;
		}
		if (dev_obj->hcmm_mgr) {
			cmm_destroy(dev_obj->hcmm_mgr, true);
			dev_obj->hcmm_mgr = NULL;
		}

		if (dev_obj->dmm_mgr) {
			dmm_destroy(dev_obj->dmm_mgr);
			dev_obj->dmm_mgr = NULL;
		}

		/* Call the driver's bridge_dev_destroy() function: */
		/* Require of DevDestroy */
		if (dev_obj->hbridge_context) {
			status = (*dev_obj->bridge_interface.pfn_dev_destroy)
			    (dev_obj->hbridge_context);
			dev_obj->hbridge_context = NULL;
		} else
			status = -EPERM;
		if (!status) {
			kfree(dev_obj->proc_list);
			dev_obj->proc_list = NULL;

			/* Remove this DEV_Object from the global list: */
			drv_remove_dev_object(dev_obj->hdrv_obj, dev_obj);
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(mgr != NULL);

	if (hdev_obj) {
		*mgr = dev_obj->hchnl_mgr;
	} else {
		*mgr = NULL;
		status = -EFAULT;
	}

	DBC_ENSURE(!status || (mgr != NULL && *mgr == NULL));
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(mgr != NULL);

	if (hdev_obj) {
		*mgr = dev_obj->hcmm_mgr;
	} else {
		*mgr = NULL;
		status = -EFAULT;
	}

	DBC_ENSURE(!status || (mgr != NULL && *mgr == NULL));
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(mgr != NULL);

	if (hdev_obj) {
		*mgr = dev_obj->dmm_mgr;
	} else {
		*mgr = NULL;
		status = -EFAULT;
	}

	DBC_ENSURE(!status || (mgr != NULL && *mgr == NULL));
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(cod_mgr != NULL);

	if (hdev_obj) {
		*cod_mgr = dev_obj->cod_mgr;
	} else {
		*cod_mgr = NULL;
		status = -EFAULT;
	}

	DBC_ENSURE(!status || (cod_mgr != NULL && *cod_mgr == NULL));
	return status;
}

/*
 *  ========= dev_get_deh_mgr ========
 */
int dev_get_deh_mgr(struct dev_object *hdev_obj,
			   struct deh_mgr **deh_manager)
{
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(deh_manager != NULL);
	DBC_REQUIRE(hdev_obj);
	if (hdev_obj) {
		*deh_manager = hdev_obj->hdeh_mgr;
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(dev_nde != NULL);

	if (hdev_obj) {
		*dev_nde = dev_obj->dev_node_obj;
	} else {
		*dev_nde = NULL;
		status = -EFAULT;
	}

	DBC_ENSURE(!status || (dev_nde != NULL && *dev_nde == NULL));
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(if_fxns != NULL);

	if (hdev_obj) {
		*if_fxns = &dev_obj->bridge_interface;
	} else {
		*if_fxns = NULL;
		status = -EFAULT;
	}

	DBC_ENSURE(!status || ((if_fxns != NULL) && (*if_fxns == NULL)));
	return status;
}

/*
 *  ========= dev_get_io_mgr ========
 */
int dev_get_io_mgr(struct dev_object *hdev_obj,
			  struct io_mgr **io_man)
{
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(io_man != NULL);
	DBC_REQUIRE(hdev_obj);

	if (hdev_obj) {
		*io_man = hdev_obj->hio_mgr;
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
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(msg_man != NULL);
	DBC_REQUIRE(hdev_obj);

	*msg_man = hdev_obj->hmsg_mgr;
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(node_man != NULL);

	if (hdev_obj) {
		*node_man = dev_obj->hnode_mgr;
	} else {
		*node_man = NULL;
		status = -EFAULT;
	}

	DBC_ENSURE(!status || (node_man != NULL && *node_man == NULL));
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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(str_sym != NULL && pul_value != NULL);

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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(phbridge_context != NULL);

	if (hdev_obj) {
		*phbridge_context = dev_obj->hbridge_context;
	} else {
		*phbridge_context = NULL;
		status = -EFAULT;
	}

	DBC_ENSURE(!status || ((phbridge_context != NULL) &&
					     (*phbridge_context == NULL)));
	return status;
}

/*
 *  ======== dev_exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 */
void dev_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;

	if (refs == 0) {
		cmm_exit();
		dmm_exit();
	}

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== dev_init ========
 *  Purpose:
 *      Initialize DEV's private state, keeping a reference count on each call.
 */
bool dev_init(void)
{
	bool cmm_ret, dmm_ret, ret = true;

	DBC_REQUIRE(refs >= 0);

	if (refs == 0) {
		cmm_ret = cmm_init();
		dmm_ret = dmm_init();

		ret = cmm_ret && dmm_ret;

		if (!ret) {
			if (cmm_ret)
				cmm_exit();

			if (dmm_ret)
				dmm_exit();

		}
	}

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));

	return ret;
}

/*
 *  ======== dev_notify_clients ========
 *  Purpose:
 *      Notify all clients of this device of a change in device status.
 */
int dev_notify_clients(struct dev_object *hdev_obj, u32 ret)
{
	int status = 0;

	struct dev_object *dev_obj = hdev_obj;
	void *proc_obj;

	for (proc_obj = (void *)lst_first(dev_obj->proc_list);
	     proc_obj != NULL;
	     proc_obj = (void *)lst_next(dev_obj->proc_list,
					 (struct list_head *)proc_obj))
		proc_notify_clients(proc_obj, (u32) ret);

	return status;
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

	/* Retrieve the device object handle originaly stored with
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

	DBC_REQUIRE(refs > 0);

	if (hdev_obj)
		dev_obj->hchnl_mgr = hmgr;
	else
		status = -EFAULT;

	DBC_ENSURE(status || (dev_obj->hchnl_mgr == hmgr));
	return status;
}

/*
 *  ======== dev_set_msg_mgr ========
 *  Purpose:
 *      Set the message manager for this device.
 */
void dev_set_msg_mgr(struct dev_object *hdev_obj, struct msg_mgr *hmgr)
{
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hdev_obj);

	hdev_obj->hmsg_mgr = hmgr;
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
	char bridge_file_name[CFG_MAXSEARCHPATHLEN] = "UMA";
	int status;
	struct mgr_object *hmgr_obj = NULL;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);

	DBC_REQUIRE(refs > 0);

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

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(!dev_obj || (dev_obj->cod_mgr == NULL));

	status = cod_create(&dev_obj->cod_mgr, sz_dummy_file, NULL);

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
	int status = 0;
	struct dev_object *dev_obj = (struct dev_object *)hdev_obj;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(dev_obj);
	DBC_REQUIRE(proc_obj != 0);
	DBC_REQUIRE(dev_obj->proc_list != NULL);
	DBC_REQUIRE(already_attached != NULL);
	if (!LST_IS_EMPTY(dev_obj->proc_list))
		*already_attached = true;

	/* Add DevObject to tail. */
	lst_put_tail(dev_obj->proc_list, (struct list_head *)proc_obj);

	DBC_ENSURE(!status && !LST_IS_EMPTY(dev_obj->proc_list));

	return status;
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

	DBC_REQUIRE(dev_obj);
	DBC_REQUIRE(proc_obj != 0);
	DBC_REQUIRE(dev_obj->proc_list != NULL);
	DBC_REQUIRE(!LST_IS_EMPTY(dev_obj->proc_list));

	/* Search list for dev_obj: */
	for (cur_elem = lst_first(dev_obj->proc_list); cur_elem != NULL;
	     cur_elem = lst_next(dev_obj->proc_list, cur_elem)) {
		/* If found, remove it. */
		if ((u32) cur_elem == proc_obj) {
			lst_remove_elem(dev_obj->proc_list, cur_elem);
			status = 0;
			break;
		}
	}

	return status;
}

int dev_get_dev_type(struct dev_object *device_obj, u8 *dev_type)
{
	int status = 0;
	struct dev_object *dev_obj = (struct dev_object *)device_obj;

	*dev_type = dev_obj->dev_type;

	return status;
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

	DBC_REQUIRE(intf_fxns != NULL);
	DBC_REQUIRE(drv_fxns != NULL);
	DBC_REQUIRE(MAKEVERSION(drv_fxns->brd_api_major_version,
			drv_fxns->brd_api_minor_version) <= BRD_API_VERSION);
	bridge_version = MAKEVERSION(drv_fxns->brd_api_major_version,
				     drv_fxns->brd_api_minor_version);
	intf_fxns->brd_api_major_version = drv_fxns->brd_api_major_version;
	intf_fxns->brd_api_minor_version = drv_fxns->brd_api_minor_version;
	/* Install functions up to DSP API version .80 (first alpha): */
	if (bridge_version > 0) {
		STORE_FXN(fxn_dev_create, pfn_dev_create);
		STORE_FXN(fxn_dev_destroy, pfn_dev_destroy);
		STORE_FXN(fxn_dev_ctrl, pfn_dev_cntrl);
		STORE_FXN(fxn_brd_monitor, pfn_brd_monitor);
		STORE_FXN(fxn_brd_start, pfn_brd_start);
		STORE_FXN(fxn_brd_stop, pfn_brd_stop);
		STORE_FXN(fxn_brd_status, pfn_brd_status);
		STORE_FXN(fxn_brd_read, pfn_brd_read);
		STORE_FXN(fxn_brd_write, pfn_brd_write);
		STORE_FXN(fxn_brd_setstate, pfn_brd_set_state);
		STORE_FXN(fxn_brd_memcopy, pfn_brd_mem_copy);
		STORE_FXN(fxn_brd_memwrite, pfn_brd_mem_write);
		STORE_FXN(fxn_chnl_create, pfn_chnl_create);
		STORE_FXN(fxn_chnl_destroy, pfn_chnl_destroy);
		STORE_FXN(fxn_chnl_open, pfn_chnl_open);
		STORE_FXN(fxn_chnl_close, pfn_chnl_close);
		STORE_FXN(fxn_chnl_addioreq, pfn_chnl_add_io_req);
		STORE_FXN(fxn_chnl_getioc, pfn_chnl_get_ioc);
		STORE_FXN(fxn_chnl_cancelio, pfn_chnl_cancel_io);
		STORE_FXN(fxn_chnl_flushio, pfn_chnl_flush_io);
		STORE_FXN(fxn_chnl_getinfo, pfn_chnl_get_info);
		STORE_FXN(fxn_chnl_getmgrinfo, pfn_chnl_get_mgr_info);
		STORE_FXN(fxn_chnl_idle, pfn_chnl_idle);
		STORE_FXN(fxn_chnl_registernotify, pfn_chnl_register_notify);
		STORE_FXN(fxn_io_create, pfn_io_create);
		STORE_FXN(fxn_io_destroy, pfn_io_destroy);
		STORE_FXN(fxn_io_onloaded, pfn_io_on_loaded);
		STORE_FXN(fxn_io_getprocload, pfn_io_get_proc_load);
		STORE_FXN(fxn_msg_create, pfn_msg_create);
		STORE_FXN(fxn_msg_createqueue, pfn_msg_create_queue);
		STORE_FXN(fxn_msg_delete, pfn_msg_delete);
		STORE_FXN(fxn_msg_deletequeue, pfn_msg_delete_queue);
		STORE_FXN(fxn_msg_get, pfn_msg_get);
		STORE_FXN(fxn_msg_put, pfn_msg_put);
		STORE_FXN(fxn_msg_registernotify, pfn_msg_register_notify);
		STORE_FXN(fxn_msg_setqueueid, pfn_msg_set_queue_id);
	}
	/* Add code for any additional functions in newerBridge versions here */
	/* Ensure postcondition: */
	DBC_ENSURE(intf_fxns->pfn_dev_create != NULL);
	DBC_ENSURE(intf_fxns->pfn_dev_destroy != NULL);
	DBC_ENSURE(intf_fxns->pfn_dev_cntrl != NULL);
	DBC_ENSURE(intf_fxns->pfn_brd_monitor != NULL);
	DBC_ENSURE(intf_fxns->pfn_brd_start != NULL);
	DBC_ENSURE(intf_fxns->pfn_brd_stop != NULL);
	DBC_ENSURE(intf_fxns->pfn_brd_status != NULL);
	DBC_ENSURE(intf_fxns->pfn_brd_read != NULL);
	DBC_ENSURE(intf_fxns->pfn_brd_write != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_create != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_destroy != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_open != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_close != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_add_io_req != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_get_ioc != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_cancel_io != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_flush_io != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_get_info != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_get_mgr_info != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_idle != NULL);
	DBC_ENSURE(intf_fxns->pfn_chnl_register_notify != NULL);
	DBC_ENSURE(intf_fxns->pfn_io_create != NULL);
	DBC_ENSURE(intf_fxns->pfn_io_destroy != NULL);
	DBC_ENSURE(intf_fxns->pfn_io_on_loaded != NULL);
	DBC_ENSURE(intf_fxns->pfn_io_get_proc_load != NULL);
	DBC_ENSURE(intf_fxns->pfn_msg_set_queue_id != NULL);

#undef  STORE_FXN
}
