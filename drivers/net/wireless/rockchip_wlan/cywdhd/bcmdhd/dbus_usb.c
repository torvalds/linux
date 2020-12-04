/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Dongle BUS interface for USB, OS independent
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dbus_usb.c 565557 2015-06-22 19:29:44Z $
 */

/**
 * @file @brief
 * This file contains DBUS code that is USB, but not OS specific. DBUS is a Broadcom proprietary
 * host specific abstraction layer.
 */

#include <osl.h>
#include <bcmdefs.h>
#include <bcmutils.h>
#include <dbus.h>
#include <usbrdl.h>
#include <bcmdevs.h>
#include <bcmendian.h>

#if defined(BCM_DNGL_EMBEDIMAGE)
#ifdef EMBED_IMAGE_43236b
#include "rtecdc_43236b.h"
#endif /* EMBED_IMAGE_43236b */
#ifdef EMBED_IMAGE_43526a
#include "rtecdc_43526a.h"
#endif /* EMBED_IMAGE_43526a */
#ifdef EMBED_IMAGE_43526b
#include "rtecdc_43526b.h"
#endif /* EMBED_IMAGE_43526b */
#ifdef EMBED_IMAGE_43242a0
#include "rtecdc_43242a0.h"
#endif /* EMBED_IMAGE_43242a0 */
#ifdef EMBED_IMAGE_43143a0
#include "rtecdc_43143a0.h"
#endif /* EMBED_IMAGE_43143a0 */
#ifdef EMBED_IMAGE_43143b0
#include "rtecdc_43143b0.h"
#endif /* EMBED_IMAGE_43143b0 */
#ifdef EMBED_IMAGE_4350a0
#include "rtecdc_4350a0.h"
#endif /* EMBED_IMAGE_4350a0 */
#ifdef EMBED_IMAGE_4350b0
#include "rtecdc_4350b0.h"
#endif /* EMBED_IMAGE_4350b0 */
#ifdef EMBED_IMAGE_4350b1
#include "rtecdc_4350b1.h"
#endif /* EMBED_IMAGE_4350b1 */
#ifdef EMBED_IMAGE_4350c0
#include "rtecdc_4350c0.h"
#endif /* EMBED_IMAGE_4350c0 */
#ifdef EMBED_IMAGE_4350c1
#include "rtecdc_4350c1.h"
#endif /* EMBED_IMAGE_4350c1 */
#ifdef EMBED_IMAGE_43556b1
#include "rtecdc_43556b1.h"
#endif /* EMBED_IMAGE_43556b1 */
#ifdef EMBED_IMAGE_43569a0
#include "rtecdc_43569a0.h"
#endif /* EMBED_IMAGE_43569a0 */
#ifdef EMBED_IMAGE_GENERIC
#include "rtecdc.h"
#endif
#endif /* BCM_DNGL_EMBEDIMAGE */


#define USB_DLIMAGE_RETRY_TIMEOUT    3000    /* retry Timeout */
#define USB_SFLASH_DLIMAGE_SPINWAIT  150     /* in unit of ms */
#define USB_SFLASH_DLIMAGE_LIMIT     2000    /* spinwait limit (ms) */
#define POSTBOOT_ID                  0xA123  /* ID to detect if dongle has boot up */
#define USB_RESETCFG_SPINWAIT        1       /* wait after resetcfg (ms) */
#define USB_DEV_ISBAD(u)             (u->pub->attrib.devid == 0xDEAD)
#define USB_DLGO_SPINWAIT            100     /* wait after DL_GO (ms) */
#define TEST_CHIP                    0x4328

typedef struct {
	dbus_pub_t  *pub;

	void        *cbarg;
	dbus_intf_callbacks_t *cbs;  /** callbacks into higher DBUS level (dbus.c) */
	dbus_intf_t *drvintf;
	void        *usbosl_info;
	uint32      rdlram_base_addr;
	uint32      rdlram_size;
} usb_info_t;

/*
 * Callbacks common to all USB
 */
static void dbus_usb_disconnect(void *handle);
static void dbus_usb_send_irb_timeout(void *handle, dbus_irb_tx_t *txirb);
static void dbus_usb_send_irb_complete(void *handle, dbus_irb_tx_t *txirb, int status);
static void dbus_usb_recv_irb_complete(void *handle, dbus_irb_rx_t *rxirb, int status);
static void dbus_usb_errhandler(void *handle, int err);
static void dbus_usb_ctl_complete(void *handle, int type, int status);
static void dbus_usb_state_change(void *handle, int state);
static struct dbus_irb* dbus_usb_getirb(void *handle, bool send);
static void dbus_usb_rxerr_indicate(void *handle, bool on);
static int dbus_usb_resetcfg(usb_info_t *usbinfo);
static int dbus_usb_iovar_op(void *bus, const char *name,
	void *params, int plen, void *arg, int len, bool set);
static int dbus_iovar_process(usb_info_t* usbinfo, const char *name,
                 void *params, int plen, void *arg, int len, bool set);
static int dbus_usb_doiovar(usb_info_t *bus, const bcm_iovar_t *vi, uint32 actionid,
	const char *name, void *params, int plen, void *arg, int len, int val_size);
static int dhdusb_downloadvars(usb_info_t *bus, void *arg, int len);

static int dbus_usb_dl_writeimage(usb_info_t *usbinfo, uint8 *fw, int fwlen);
static int dbus_usb_dlstart(void *bus, uint8 *fw, int len);
static bool dbus_usb_dlneeded(void *bus);
static int dbus_usb_dlrun(void *bus);
static int dbus_usb_rdl_dwnld_state(usb_info_t *usbinfo);

#ifdef BCM_DNGL_EMBEDIMAGE
static bool dbus_usb_device_exists(void *bus);
#endif

/* OS specific */
extern bool dbus_usbos_dl_cmd(void *info, uint8 cmd, void *buffer, int buflen);
extern int dbus_usbos_wait(void *info, uint16 ms);
extern int dbus_write_membytes(usb_info_t *usbinfo, bool set, uint32 address,
	uint8 *data, uint size);
extern bool dbus_usbos_dl_send_bulk(void *info, void *buffer, int len);
extern int dbus_usbos_loopback_tx(void *usbos_info_ptr, int cnt, int size);

/**
 * These functions are called by the lower DBUS level (dbus_usb_os.c) to notify this DBUS level
 * (dbus_usb.c) of an event.
 */
static dbus_intf_callbacks_t dbus_usb_intf_cbs = {
	dbus_usb_send_irb_timeout,
	dbus_usb_send_irb_complete,
	dbus_usb_recv_irb_complete,
	dbus_usb_errhandler,
	dbus_usb_ctl_complete,
	dbus_usb_state_change,
	NULL,			/* isr */
	NULL,			/* dpc */
	NULL,			/* watchdog */
	NULL,			/* dbus_if_pktget */
	NULL, 			/* dbus_if_pktfree */
	dbus_usb_getirb,
	dbus_usb_rxerr_indicate
};

/* IOVar table */
enum {
	IOV_SET_DOWNLOAD_STATE = 1,
	IOV_MEMBYTES,
	IOV_VARS,
	IOV_LOOPBACK_TX
};

const bcm_iovar_t dhdusb_iovars[] = {
	{"vars",	IOV_VARS,	0,	IOVT_BUFFER,	0 },
	{"dwnldstate",	IOV_SET_DOWNLOAD_STATE,	0,	IOVT_BOOL,	0 },
	{"membytes",	IOV_MEMBYTES,	0,	IOVT_BUFFER,	2 * sizeof(int) },
	{"usb_lb_txfer", IOV_LOOPBACK_TX, 0,    IOVT_BUFFER,    2 * sizeof(int) },
	{NULL, 0, 0, 0, 0 }
};

/*
 * Need global for probe() and disconnect() since
 * attach() is not called at probe and detach()
 * can be called inside disconnect()
 */
static probe_cb_t	probe_cb = NULL;
static disconnect_cb_t	disconnect_cb = NULL;
static void		*probe_arg = NULL;
static void		*disc_arg = NULL;
static dbus_intf_t	*g_dbusintf = NULL;
static dbus_intf_t	dbus_usb_intf; /** functions called by higher layer DBUS into lower layer */

/*
 * dbus_intf_t common to all USB
 * These functions override dbus_usb_<os>.c.
 */
static void *dbus_usb_attach(dbus_pub_t *pub, void *cbarg, dbus_intf_callbacks_t *cbs);
static void dbus_usb_detach(dbus_pub_t *pub, void *info);
static void * dbus_usb_probe(void *arg, const char *desc, uint32 bustype, uint32 hdrlen);

/* functions */

/**
 * As part of DBUS initialization/registration, the higher level DBUS (dbus.c) needs to know what
 * lower level DBUS functions to call (in both dbus_usb.c and dbus_usb_os.c).
 */
static void *
dbus_usb_probe(void *arg, const char *desc, uint32 bustype, uint32 hdrlen)
{
	DBUSTRACE(("%s(): \n", __FUNCTION__));
	if (probe_cb) {

		if (g_dbusintf != NULL) {
			/* First, initialize all lower-level functions as default
			 * so that dbus.c simply calls directly to dbus_usb_os.c.
			 */
			bcopy(g_dbusintf, &dbus_usb_intf, sizeof(dbus_intf_t));

			/* Second, selectively override functions we need, if any. */
			dbus_usb_intf.attach = dbus_usb_attach;
			dbus_usb_intf.detach = dbus_usb_detach;
			dbus_usb_intf.iovar_op = dbus_usb_iovar_op;
			dbus_usb_intf.dlstart = dbus_usb_dlstart;
			dbus_usb_intf.dlneeded = dbus_usb_dlneeded;
			dbus_usb_intf.dlrun = dbus_usb_dlrun;
#ifdef BCM_DNGL_EMBEDIMAGE
			dbus_usb_intf.device_exists = dbus_usb_device_exists;
#endif
		}

		disc_arg = probe_cb(probe_arg, "DBUS USB", USB_BUS, hdrlen);
		return disc_arg;
	}

	return NULL;
}

/**
 * On return, *intf contains this or lower-level DBUS functions to be called by higher
 * level (dbus.c)
 */
int
dbus_bus_register(int vid, int pid, probe_cb_t prcb,
	disconnect_cb_t discb, void *prarg, dbus_intf_t **intf, void *param1, void *param2)
{
	int err;

	DBUSTRACE(("%s(): \n", __FUNCTION__));
	probe_cb = prcb;
	disconnect_cb = discb;
	probe_arg = prarg;

	*intf = &dbus_usb_intf;

	err = dbus_bus_osl_register(vid, pid, dbus_usb_probe,
		dbus_usb_disconnect, NULL, &g_dbusintf, param1, param2);

	ASSERT(g_dbusintf);
	return err;
}

int
dbus_bus_deregister()
{
	DBUSTRACE(("%s(): \n", __FUNCTION__));
	return dbus_bus_osl_deregister();
}

/** initialization consists of registration followed by 'attach'. */
void *
dbus_usb_attach(dbus_pub_t *pub, void *cbarg, dbus_intf_callbacks_t *cbs)
{
	usb_info_t *usb_info;

	DBUSTRACE(("%s(): \n", __FUNCTION__));

	if ((g_dbusintf == NULL) || (g_dbusintf->attach == NULL))
		return NULL;

	/* Sanity check for BUS_INFO() */
	ASSERT(OFFSETOF(usb_info_t, pub) == 0);

	usb_info = MALLOC(pub->osh, sizeof(usb_info_t));
	if (usb_info == NULL)
		return NULL;

	bzero(usb_info, sizeof(usb_info_t));

	usb_info->pub = pub;
	usb_info->cbarg = cbarg;
	usb_info->cbs = cbs;

	usb_info->usbosl_info = (dbus_pub_t *)g_dbusintf->attach(pub,
		usb_info, &dbus_usb_intf_cbs);
	if (usb_info->usbosl_info == NULL) {
		MFREE(pub->osh, usb_info, sizeof(usb_info_t));
		return NULL;
	}

	/* Save USB OS-specific driver entry points */
	usb_info->drvintf = g_dbusintf;

	pub->bus = usb_info;
#if  !defined(BCM_DNGL_EMBEDIMAGE) && !defined(BCM_REQUEST_FW)

	if (!dbus_usb_resetcfg(usb_info)) {
	usb_info->pub->busstate = DBUS_STATE_DL_DONE;
	}
#endif
	/* Return Lower layer info */
	return (void *) usb_info->usbosl_info;
}

void
dbus_usb_detach(dbus_pub_t *pub, void *info)
{
	usb_info_t *usb_info = (usb_info_t *) pub->bus;
	osl_t *osh = pub->osh;

	if (usb_info == NULL)
		return;

	if (usb_info->drvintf && usb_info->drvintf->detach)
		usb_info->drvintf->detach(pub, usb_info->usbosl_info);

	MFREE(osh, usb_info, sizeof(usb_info_t));
}

void
dbus_usb_disconnect(void *handle)
{
	DBUSTRACE(("%s(): \n", __FUNCTION__));
	if (disconnect_cb)
		disconnect_cb(disc_arg);
}

/**
 * When the lower DBUS level (dbus_usb_os.c) signals this event, the higher DBUS level has to be
 * notified.
 */
static void
dbus_usb_send_irb_timeout(void *handle, dbus_irb_tx_t *txirb)
{
	usb_info_t *usb_info = (usb_info_t *) handle;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (usb_info == NULL)
		return;

	if (usb_info->cbs && usb_info->cbs->send_irb_timeout)
		usb_info->cbs->send_irb_timeout(usb_info->cbarg, txirb);
}

/**
 * When the lower DBUS level (dbus_usb_os.c) signals this event, the higher DBUS level has to be
 * notified.
 */
static void
dbus_usb_send_irb_complete(void *handle, dbus_irb_tx_t *txirb, int status)
{
	usb_info_t *usb_info = (usb_info_t *) handle;

	if (usb_info == NULL)
		return;

	if (usb_info->cbs && usb_info->cbs->send_irb_complete)
		usb_info->cbs->send_irb_complete(usb_info->cbarg, txirb, status);
}

/**
 * When the lower DBUS level (dbus_usb_os.c) signals this event, the higher DBUS level has to be
 * notified.
 */
static void
dbus_usb_recv_irb_complete(void *handle, dbus_irb_rx_t *rxirb, int status)
{
	usb_info_t *usb_info = (usb_info_t *) handle;

	if (usb_info == NULL)
		return;

	if (usb_info->cbs && usb_info->cbs->recv_irb_complete)
		usb_info->cbs->recv_irb_complete(usb_info->cbarg, rxirb, status);
}

/** Lower DBUS level (dbus_usb_os.c) requests a free IRB. Pass this on to the higher DBUS level. */
static struct dbus_irb*
dbus_usb_getirb(void *handle, bool send)
{
	usb_info_t *usb_info = (usb_info_t *) handle;

	if (usb_info == NULL)
		return NULL;

	if (usb_info->cbs && usb_info->cbs->getirb)
		return usb_info->cbs->getirb(usb_info->cbarg, send);

	return NULL;
}

/**
 * When the lower DBUS level (dbus_usb_os.c) signals this event, the higher DBUS level has to be
 * notified.
 */
static void
dbus_usb_rxerr_indicate(void *handle, bool on)
{
	usb_info_t *usb_info = (usb_info_t *) handle;

	if (usb_info == NULL)
		return;

	if (usb_info->cbs && usb_info->cbs->rxerr_indicate)
		usb_info->cbs->rxerr_indicate(usb_info->cbarg, on);
}

/**
 * When the lower DBUS level (dbus_usb_os.c) signals this event, the higher DBUS level has to be
 * notified.
 */
static void
dbus_usb_errhandler(void *handle, int err)
{
	usb_info_t *usb_info = (usb_info_t *) handle;

	if (usb_info == NULL)
		return;

	if (usb_info->cbs && usb_info->cbs->errhandler)
		usb_info->cbs->errhandler(usb_info->cbarg, err);
}

/**
 * When the lower DBUS level (dbus_usb_os.c) signals this event, the higher DBUS level has to be
 * notified.
 */
static void
dbus_usb_ctl_complete(void *handle, int type, int status)
{
	usb_info_t *usb_info = (usb_info_t *) handle;

	if (usb_info == NULL)
		return;

	if (usb_info->cbs && usb_info->cbs->ctl_complete)
		usb_info->cbs->ctl_complete(usb_info->cbarg, type, status);
}

/**
 * When the lower DBUS level (dbus_usb_os.c) signals this event, the higher DBUS level has to be
 * notified.
 */
static void
dbus_usb_state_change(void *handle, int state)
{
	usb_info_t *usb_info = (usb_info_t *) handle;

	if (usb_info == NULL)
		return;

	if (usb_info->cbs && usb_info->cbs->state_change)
		usb_info->cbs->state_change(usb_info->cbarg, state);
}

/** called by higher DBUS level (dbus.c) */
static int
dbus_usb_iovar_op(void *bus, const char *name,
	void *params, int plen, void *arg, int len, bool set)
{
	int err = DBUS_OK;

	err = dbus_iovar_process((usb_info_t*)bus, name, params, plen, arg, len, set);
	return err;
}

/** process iovar request from higher DBUS level */
static int
dbus_iovar_process(usb_info_t* usbinfo, const char *name,
                 void *params, int plen, void *arg, int len, bool set)
{
	const bcm_iovar_t *vi = NULL;
	int bcmerror = 0;
	int val_size;
	uint32 actionid;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));

	/* Look up var locally; if not found pass to host driver */
	if ((vi = bcm_iovar_lookup(dhdusb_iovars, name)) == NULL) {
		/* Not Supported */
		bcmerror = BCME_UNSUPPORTED;
		DBUSTRACE(("%s: IOVAR %s is not supported\n", name, __FUNCTION__));
		goto exit;

	}

	DBUSTRACE(("%s: %s %s, len %d plen %d\n", __FUNCTION__,
	         name, (set ? "set" : "get"), len, plen));

	/* set up 'params' pointer in case this is a set command so that
	 * the convenience int and bool code can be common to set and get
	 */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		/* all other types are integer sized */
		val_size = sizeof(int);

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	bcmerror = dbus_usb_doiovar(usbinfo, vi, actionid,
		name, params, plen, arg, len, val_size);

exit:
	return bcmerror;
} /* dbus_iovar_process */

static int
dbus_usb_doiovar(usb_info_t *bus, const bcm_iovar_t *vi, uint32 actionid, const char *name,
                void *params, int plen, void *arg, int len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;
	int32 int_val2 = 0;
	bool bool_val = 0;

	DBUSTRACE(("%s: Enter, action %d name %s params %p plen %d arg %p len %d val_size %d\n",
	           __FUNCTION__, actionid, name, params, plen, arg, len, val_size));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	if (plen >= (int)sizeof(int_val) * 2)
		bcopy((void*)((uintptr)params + sizeof(int_val)), &int_val2, sizeof(int_val2));

	bool_val = (int_val != 0) ? TRUE : FALSE;

	switch (actionid) {

	case IOV_SVAL(IOV_MEMBYTES):
	case IOV_GVAL(IOV_MEMBYTES):
	{
		uint32 address;
		uint size, dsize;
		uint8 *data;

		bool set = (actionid == IOV_SVAL(IOV_MEMBYTES));

		ASSERT(plen >= 2*sizeof(int));

		address = (uint32)int_val;
		BCM_REFERENCE(address);
		bcopy((char *)params + sizeof(int_val), &int_val, sizeof(int_val));
		size = (uint)int_val;

		/* Do some validation */
		dsize = set ? plen - (2 * sizeof(int)) : len;
		if (dsize < size) {
			DBUSTRACE(("%s: error on %s membytes, addr 0x%08x size %d dsize %d\n",
			           __FUNCTION__, (set ? "set" : "get"), address, size, dsize));
			bcmerror = BCME_BADARG;
			break;
		}
		DBUSTRACE(("%s: Request to %s %d bytes at address 0x%08x\n", __FUNCTION__,
		          (set ? "write" : "read"), size, address));

		/* Generate the actual data pointer */
		data = set ? (uint8*)params + 2 * sizeof(int): (uint8*)arg;

		/* Call to do the transfer */
		bcmerror = dbus_usb_dl_writeimage(BUS_INFO(bus, usb_info_t), data, size);
	}
		break;


	case IOV_SVAL(IOV_SET_DOWNLOAD_STATE):

		if (bool_val == TRUE) {
			bcmerror = dbus_usb_dlneeded(bus);
			dbus_usb_rdl_dwnld_state(BUS_INFO(bus, usb_info_t));
		} else {
			usb_info_t *usbinfo = BUS_INFO(bus, usb_info_t);
			bcmerror = dbus_usb_dlrun(bus);
			usbinfo->pub->busstate = DBUS_STATE_DL_DONE;
		}
		break;

	case IOV_SVAL(IOV_VARS):
		bcmerror = dhdusb_downloadvars(BUS_INFO(bus, usb_info_t), arg, len);
		break;
#ifdef DBUS_USB_LOOPBACK
	case IOV_SVAL(IOV_LOOPBACK_TX):
			bcmerror = dbus_usbos_loopback_tx(BUS_INFO(bus, usb_info_t), int_val,
			  int_val2);
			break;
#endif
	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	return bcmerror;
} /* dbus_usb_doiovar */

/** higher DBUS level (dbus.c) wants to set NVRAM variables in dongle */
static int
dhdusb_downloadvars(usb_info_t *bus, void *arg, int len)
{
	int bcmerror = 0;
	uint32 varsize;
	uint32 varaddr;
	uint32 varsizew;

	if (!len) {
		bcmerror = BCME_BUFTOOSHORT;
		goto err;
	}

	/* RAM size is not set. Set it at dbus_usb_dlneeded */
	if (!bus->rdlram_size)
		bcmerror = BCME_ERROR;

	/* Even if there are no vars are to be written, we still need to set the ramsize. */
	varsize = len ? ROUNDUP(len, 4) : 0;
	varaddr = (bus->rdlram_size - 4) - varsize;

	/* Write the vars list */
	DBUSTRACE(("WriteVars: @%x varsize=%d\n", varaddr, varsize));
	bcmerror = dbus_write_membytes(bus->usbosl_info, TRUE, (varaddr + bus->rdlram_base_addr),
		arg, varsize);

	/* adjust to the user specified RAM */
	DBUSTRACE(("Usable memory size: %d\n", bus->rdlram_size));
	DBUSTRACE(("Vars are at %d, orig varsize is %d\n", varaddr, varsize));

	varsize = ((bus->rdlram_size - 4) - varaddr);

	/*
	 * Determine the length token:
	 * Varsize, converted to words, in lower 16-bits, checksum in upper 16-bits.
	 */
	if (bcmerror) {
		varsizew = 0;
	} else {
		varsizew = varsize / 4;
		varsizew = (~varsizew << 16) | (varsizew & 0x0000FFFF);
		varsizew = htol32(varsizew);
	}

	DBUSTRACE(("New varsize is %d, length token=0x%08x\n", varsize, varsizew));

	/* Write the length token to the last word */
	bcmerror = dbus_write_membytes(bus->usbosl_info, TRUE, ((bus->rdlram_size - 4) +
		bus->rdlram_base_addr), (uint8*)&varsizew, 4);
err:
	return bcmerror;
} /* dbus_usb_doiovar */

/**
 * After downloading firmware into dongle and starting it, we need to know if the firmware is
 * indeed up and running.
 */
static int
dbus_usb_resetcfg(usb_info_t *usbinfo)
{
	void *osinfo;
	bootrom_id_t id;
	uint16 waittime = 0;

	uint32 starttime = 0;
	uint32 endtime = 0;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (usbinfo == NULL)
		return DBUS_ERR;

	osinfo = usbinfo->usbosl_info;
	ASSERT(osinfo);

	/* Give dongle chance to boot */
	dbus_usbos_wait(osinfo, USB_SFLASH_DLIMAGE_SPINWAIT);
	waittime = USB_SFLASH_DLIMAGE_SPINWAIT;
	while (waittime < USB_DLIMAGE_RETRY_TIMEOUT) {

		starttime = OSL_SYSUPTIME();

		id.chip = 0xDEAD;       /* Get the ID */
		dbus_usbos_dl_cmd(osinfo, DL_GETVER, &id, sizeof(bootrom_id_t));
		id.chip = ltoh32(id.chip);

		endtime = OSL_SYSUPTIME();
		waittime += (endtime - starttime);

		if (id.chip == POSTBOOT_ID)
			break;
	}

	if (id.chip == POSTBOOT_ID) {
		DBUSERR(("%s: download done. Bootup time = %d ms postboot chip 0x%x/rev 0x%x\n",
			__FUNCTION__, waittime, id.chip, id.chiprev));

		dbus_usbos_dl_cmd(osinfo, DL_RESETCFG, &id, sizeof(bootrom_id_t));

		dbus_usbos_wait(osinfo, USB_RESETCFG_SPINWAIT);
		return DBUS_OK;
	} else {
		DBUSERR(("%s: Cannot talk to Dongle. Wait time = %d ms. Firmware is not UP \n",
			__FUNCTION__, waittime));
		return DBUS_ERR;
	}

	return DBUS_OK;
}

/** before firmware download, the dongle has to be prepared to receive the fw image */
static int
dbus_usb_rdl_dwnld_state(usb_info_t *usbinfo)
{
	void *osinfo = usbinfo->usbosl_info;
	rdl_state_t state;
	int err = DBUS_OK;

	/* 1) Prepare USB boot loader for runtime image */
	dbus_usbos_dl_cmd(osinfo, DL_START, &state, sizeof(rdl_state_t));

	state.state = ltoh32(state.state);
	state.bytes = ltoh32(state.bytes);

	/* 2) Check we are in the Waiting state */
	if (state.state != DL_WAITING) {
		DBUSERR(("%s: Failed to DL_START\n", __FUNCTION__));
		err = DBUS_ERR;
		goto fail;
	}

fail:
	return err;
}

/**
 * Dongle contains bootcode in ROM but firmware is (partially) contained in dongle RAM. Therefore,
 * firmware has to be downloaded into dongle RAM.
 */
static int
dbus_usb_dl_writeimage(usb_info_t *usbinfo, uint8 *fw, int fwlen)
{
	osl_t *osh = usbinfo->pub->osh;
	void *osinfo = usbinfo->usbosl_info;
	unsigned int sendlen, sent, dllen;
	char *bulkchunk = NULL, *dlpos;
	rdl_state_t state;
	int err = DBUS_OK;
	bootrom_id_t id;
	uint16 wait, wait_time;
	uint32 dl_trunk_size = RDL_CHUNK;

	if (BCM4350_CHIP(usbinfo->pub->attrib.devid))
		dl_trunk_size = RDL_CHUNK_MAX;

	while (!bulkchunk) {
		bulkchunk = MALLOC(osh, dl_trunk_size);
		if (dl_trunk_size == RDL_CHUNK)
			break;
		if (!bulkchunk) {
			dl_trunk_size /= 2;
			if (dl_trunk_size < RDL_CHUNK)
				dl_trunk_size = RDL_CHUNK;
		}
	}

	if (bulkchunk == NULL) {
		err = DBUS_ERR;
		goto fail;
	}

	sent = 0;
	dlpos = fw;
	dllen = fwlen;

	/* Get chip id and rev */
	id.chip = usbinfo->pub->attrib.devid;
	id.chiprev = usbinfo->pub->attrib.chiprev;

	DBUSTRACE(("enter %s: fwlen=%d\n", __FUNCTION__, fwlen));

	dbus_usbos_dl_cmd(osinfo, DL_GETSTATE, &state, sizeof(rdl_state_t));

	/* 3) Load the image */
	while ((sent < dllen)) {
		/* Wait until the usb device reports it received all the bytes we sent */

		if (sent < dllen) {
			if ((dllen-sent) < dl_trunk_size)
				sendlen = dllen-sent;
			else
				sendlen = dl_trunk_size;

			/* simply avoid having to send a ZLP by ensuring we never have an even
			 * multiple of 64
			 */
			if (!(sendlen % 64))
				sendlen -= 4;

			/* send data */
			memcpy(bulkchunk, dlpos, sendlen);
			if (!dbus_usbos_dl_send_bulk(osinfo, bulkchunk, sendlen)) {
				err = DBUS_ERR;
				goto fail;
			}

			dlpos += sendlen;
			sent += sendlen;
			DBUSTRACE(("%s: sendlen %d\n", __FUNCTION__, sendlen));
		}

		wait = 0;
		wait_time = USB_SFLASH_DLIMAGE_SPINWAIT;
		while (!dbus_usbos_dl_cmd(osinfo, DL_GETSTATE, &state,
			sizeof(rdl_state_t))) {
			if ((id.chip == 43236) && (id.chiprev == 0)) {
				DBUSERR(("%s: 43236a0 SFlash delay, waiting for dongle crc check "
					 "completion!!!\n", __FUNCTION__));
				dbus_usbos_wait(osinfo, wait_time);
				wait += wait_time;
				if (wait >= USB_SFLASH_DLIMAGE_LIMIT) {
					DBUSERR(("%s: DL_GETSTATE Failed xxxx\n", __FUNCTION__));
					err = DBUS_ERR;
					goto fail;
					break;
				}
			} else {
				DBUSERR(("%s: DL_GETSTATE Failed xxxx\n", __FUNCTION__));
				err = DBUS_ERR;
				goto fail;
			}
		}

		state.state = ltoh32(state.state);
		state.bytes = ltoh32(state.bytes);

		/* restart if an error is reported */
		if ((state.state == DL_BAD_HDR) || (state.state == DL_BAD_CRC)) {
			DBUSERR(("%s: Bad Hdr or Bad CRC\n", __FUNCTION__));
			err = DBUS_ERR;
			goto fail;
		}

	}
fail:
	if (bulkchunk)
		MFREE(osh, bulkchunk, dl_trunk_size);

	return err;
} /* dbus_usb_dl_writeimage */

/** Higher level DBUS layer (dbus.c) requests this layer to download image into dongle */
static int
dbus_usb_dlstart(void *bus, uint8 *fw, int len)
{
	usb_info_t *usbinfo = BUS_INFO(bus, usb_info_t);
	int err;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (usbinfo == NULL)
		return DBUS_ERR;

	if (USB_DEV_ISBAD(usbinfo))
		return DBUS_ERR;

	err = dbus_usb_rdl_dwnld_state(usbinfo);

	if (DBUS_OK == err) {
	err = dbus_usb_dl_writeimage(usbinfo, fw, len);
	if (err == DBUS_OK)
		usbinfo->pub->busstate = DBUS_STATE_DL_DONE;
	else
		usbinfo->pub->busstate = DBUS_STATE_DL_PENDING;
	} else
		usbinfo->pub->busstate = DBUS_STATE_DL_PENDING;

	return err;
}

static bool
dbus_usb_update_chipinfo(usb_info_t *usbinfo, uint32 chip)
{
	bool retval = TRUE;
	/* based on the CHIP Id, store the ram size which is needed for NVRAM download. */
	switch (chip) {

		case 0x4319:
			usbinfo->rdlram_size = RDL_RAM_SIZE_4319;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_4319;
			break;

		case 0x4329:
			usbinfo->rdlram_size = RDL_RAM_SIZE_4329;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_4329;
			break;

		case 43234:
		case 43235:
		case 43236:
			usbinfo->rdlram_size = RDL_RAM_SIZE_43236;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_43236;
			break;

		case 0x4328:
			usbinfo->rdlram_size = RDL_RAM_SIZE_4328;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_4328;
			break;

		case 0x4322:
			usbinfo->rdlram_size = RDL_RAM_SIZE_4322;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_4322;
			break;

		case 0x4360:
		case 0xAA06:
			usbinfo->rdlram_size = RDL_RAM_SIZE_4360;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_4360;
			break;

		case 43242:
		case 43243:
			usbinfo->rdlram_size = RDL_RAM_SIZE_43242;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_43242;
			break;

		case 43143:
			usbinfo->rdlram_size = RDL_RAM_SIZE_43143;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_43143;
			break;

		case 0x4350:
		case 43556:
		case 43558:
		case 43569:
			usbinfo->rdlram_size = RDL_RAM_SIZE_4350;
			usbinfo->rdlram_base_addr = RDL_RAM_BASE_4350;
			break;

		case POSTBOOT_ID:
			break;

		default:
			DBUSERR(("%s: Chip 0x%x Ram size is not known\n", __FUNCTION__, chip));
			retval = FALSE;
			break;

	}

	return retval;
} /* dbus_usb_update_chipinfo */

/** higher DBUS level (dbus.c) wants to know if firmware download is required. */
static bool
dbus_usb_dlneeded(void *bus)
{
	usb_info_t *usbinfo = BUS_INFO(bus, usb_info_t);
	void *osinfo;
	bootrom_id_t id;
	bool dl_needed = TRUE;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (usbinfo == NULL)
		return FALSE;

	osinfo = usbinfo->usbosl_info;
	ASSERT(osinfo);

	/* Check if firmware downloaded already by querying runtime ID */
	id.chip = 0xDEAD;
	dbus_usbos_dl_cmd(osinfo, DL_GETVER, &id, sizeof(bootrom_id_t));

	id.chip = ltoh32(id.chip);
	id.chiprev = ltoh32(id.chiprev);

	if (FALSE == dbus_usb_update_chipinfo(usbinfo, id.chip)) {
		dl_needed = FALSE;
		goto exit;
	}

	DBUSERR(("%s: chip 0x%x rev 0x%x\n", __FUNCTION__, id.chip, id.chiprev));
	if (id.chip == POSTBOOT_ID) {
		/* This code is  needed to support two enumerations on USB1.1 scenario */
		DBUSERR(("%s: Firmware already downloaded\n", __FUNCTION__));

		dbus_usbos_dl_cmd(osinfo, DL_RESETCFG, &id, sizeof(bootrom_id_t));
		dl_needed = FALSE;
		if (usbinfo->pub->busstate == DBUS_STATE_DL_PENDING)
			usbinfo->pub->busstate = DBUS_STATE_DL_DONE;
	} else {
		usbinfo->pub->attrib.devid = id.chip;
		usbinfo->pub->attrib.chiprev = id.chiprev;
	}

exit:
	return dl_needed;
}

/** After issuing firmware download, higher DBUS level (dbus.c) wants to start the firmware. */
static int
dbus_usb_dlrun(void *bus)
{
	usb_info_t *usbinfo = BUS_INFO(bus, usb_info_t);
	void *osinfo;
	rdl_state_t state;
	int err = DBUS_OK;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (usbinfo == NULL)
		return DBUS_ERR;

	if (USB_DEV_ISBAD(usbinfo))
		return DBUS_ERR;

	osinfo = usbinfo->usbosl_info;
	ASSERT(osinfo);

	/* Check we are runnable */
	dbus_usbos_dl_cmd(osinfo, DL_GETSTATE, &state, sizeof(rdl_state_t));

	state.state = ltoh32(state.state);
	state.bytes = ltoh32(state.bytes);

	/* Start the image */
	if (state.state == DL_RUNNABLE) {
		DBUSTRACE(("%s: Issue DL_GO\n", __FUNCTION__));
		dbus_usbos_dl_cmd(osinfo, DL_GO, &state, sizeof(rdl_state_t));

		if (usbinfo->pub->attrib.devid == TEST_CHIP)
			dbus_usbos_wait(osinfo, USB_DLGO_SPINWAIT);

		dbus_usb_resetcfg(usbinfo);
		/* The Donlge may go for re-enumeration. */
	} else {
		DBUSERR(("%s: Dongle not runnable\n", __FUNCTION__));
		err = DBUS_ERR;
	}

	return err;
}

/**
 * As preparation for firmware download, higher DBUS level (dbus.c) requests the firmware image
 * to be used for the type of dongle detected. Directly called by dbus.c (so not via a callback
 * construction)
 */
void
dbus_bus_fw_get(void *bus, uint8 **fw, int *fwlen, int *decomp)
{
	usb_info_t *usbinfo = BUS_INFO(bus, usb_info_t);
	unsigned int devid;
	unsigned int crev;

	devid = usbinfo->pub->attrib.devid;
	crev = usbinfo->pub->attrib.chiprev;

	*fw = NULL;
	*fwlen = 0;

	switch (devid) {
	case BCM43236_CHIP_ID:
	case BCM43235_CHIP_ID:
	case BCM43234_CHIP_ID:
	case BCM43238_CHIP_ID: {
		if (crev == 3 || crev == 2 || crev == 1) {
#ifdef EMBED_IMAGE_43236b
			*fw = (uint8 *)dlarray_43236b;
			*fwlen = sizeof(dlarray_43236b);

#endif
		}
		} break;
	case BCM4360_CHIP_ID:
	case BCM4352_CHIP_ID:
	case BCM43526_CHIP_ID:
#ifdef EMBED_IMAGE_43526a
		if (crev <= 2) {
			*fw = (uint8 *)dlarray_43526a;
			*fwlen = sizeof(dlarray_43526a);
		}
#endif
#ifdef EMBED_IMAGE_43526b
		if (crev > 2) {
			*fw = (uint8 *)dlarray_43526b;
			*fwlen = sizeof(dlarray_43526b);
		}
#endif
		break;

	case BCM43242_CHIP_ID:
#ifdef EMBED_IMAGE_43242a0
		*fw = (uint8 *)dlarray_43242a0;
		*fwlen = sizeof(dlarray_43242a0);
#endif
		break;

	case BCM43143_CHIP_ID:
#ifdef EMBED_IMAGE_43143a0
		*fw = (uint8 *)dlarray_43143a0;
		*fwlen = sizeof(dlarray_43143a0);
#endif
#ifdef EMBED_IMAGE_43143b0
		*fw = (uint8 *)dlarray_43143b0;
		*fwlen = sizeof(dlarray_43143b0);
#endif
		break;

	case BCM4350_CHIP_ID:
	case BCM4354_CHIP_ID:
	case BCM43556_CHIP_ID:
	case BCM43558_CHIP_ID:
	case BCM43566_CHIP_ID:
	case BCM43568_CHIP_ID:
	case BCM43570_CHIP_ID:
	case BCM4358_CHIP_ID:
#ifdef EMBED_IMAGE_4350a0
		if (crev == 0) {
			*fw = (uint8 *)dlarray_4350a0;
			*fwlen = sizeof(dlarray_4350a0);
		}
#endif
#ifdef EMBED_IMAGE_4350b0
		if (crev == 1) {
			*fw = (uint8 *)dlarray_4350b0;
			*fwlen = sizeof(dlarray_4350b0);
		}
#endif
#ifdef EMBED_IMAGE_4350b1
		if (crev == 2) {
			*fw = (uint8 *)dlarray_4350b1;
			*fwlen = sizeof(dlarray_4350b1);
		}
#endif
#ifdef EMBED_IMAGE_43556b1
		if (crev == 2) {
			*fw = (uint8 *)dlarray_43556b1;
			*fwlen = sizeof(dlarray_43556b1);
		}
#endif
#ifdef EMBED_IMAGE_4350c0
		if (crev == 3) {
			*fw = (uint8 *)dlarray_4350c0;
			*fwlen = sizeof(dlarray_4350c0);
		}
#endif /* EMBED_IMAGE_4350c0 */
#ifdef EMBED_IMAGE_4350c1
		if (crev == 4) {
			*fw = (uint8 *)dlarray_4350c1;
			*fwlen = sizeof(dlarray_4350c1);
		}
#endif /* EMBED_IMAGE_4350c1 */
		break;
	case BCM43569_CHIP_ID:
#ifdef EMBED_IMAGE_43569a0
		if (crev == 0) {
			*fw = (uint8 *)dlarray_43569a0;
			*fwlen = sizeof(dlarray_43569a0);
		}
#endif /* EMBED_IMAGE_43569a0 */
		break;
	default:
#ifdef EMBED_IMAGE_GENERIC
		*fw = (uint8 *)dlarray;
		*fwlen = sizeof(dlarray);
#endif
		break;
	}
} /* dbus_bus_fw_get */

#ifdef BCM_DNGL_EMBEDIMAGE
static bool
dbus_usb_device_exists(void *bus)
{
	usb_info_t *usbinfo = BUS_INFO(bus, usb_info_t);
	void *osinfo;
	bootrom_id_t id;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (usbinfo == NULL)
		return FALSE;

	osinfo = usbinfo->usbosl_info;
	ASSERT(osinfo);

	id.chip = 0xDEAD;
	/* Query device to see if we get a response */
	dbus_usbos_dl_cmd(osinfo, DL_GETVER, &id, sizeof(bootrom_id_t));

	usbinfo->pub->attrib.devid = id.chip;
	if (id.chip == 0xDEAD)
		return FALSE;
	else
		return TRUE;
}
#endif /* BCM_DNGL_EMBEDIMAGE */
