/*
 * Wireless USB Host Controller
 * Common infrastructure for WHCI and HWA WUSB-HC drivers
 *
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * This driver implements parts common to all Wireless USB Host
 * Controllers (struct wusbhc, embedding a struct usb_hcd) and is used
 * by:
 *
 *   - hwahc: HWA, USB-dongle that implements a Wireless USB host
 *     controller, (Wireless USB 1.0 Host-Wire-Adapter specification).
 *
 *   - whci: WHCI, a PCI card with a wireless host controller
 *     (Wireless Host Controller Interface 1.0 specification).
 *
 * Check out the Design-overview.txt file in the source documentation
 * for other details on the implementation.
 *
 * Main blocks:
 *
 *  rh         Root Hub emulation (part of the HCD glue)
 *
 *  devconnect Handle all the issues related to device connection,
 *             authentication, disconnection, timeout, reseting,
 *             keepalives, etc.
 *
 *  mmc        MMC IE broadcasting handling
 *
 * A host controller driver just initializes its stuff and as part of
 * that, creates a 'struct wusbhc' instance that handles all the
 * common WUSB mechanisms. Links in the function ops that are specific
 * to it and then registers the host controller. Ready to run.
 */

#ifndef __WUSBHC_H__
#define __WUSBHC_H__

#include <linux/usb.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/workqueue.h>
/* FIXME: Yes, I know: BAD--it's not my fault the USB HC iface is not
 *        public */
#include <linux/../../drivers/usb/core/hcd.h>
#include <linux/uwb.h>
#include <linux/usb/wusb.h>


/**
 * Wireless USB device
 *
 * Describe a WUSB device connected to the cluster. This struct
 * belongs to the 'struct wusb_port' it is attached to and it is
 * responsible for putting and clearing the pointer to it.
 *
 * Note this "complements" the 'struct usb_device' that the usb_hcd
 * keeps for each connected USB device. However, it extends some
 * information that is not available (there is no hcpriv ptr in it!)
 * *and* most importantly, it's life cycle is different. It is created
 * as soon as we get a DN_Connect (connect request notification) from
 * the device through the WUSB host controller; the USB stack doesn't
 * create the device until we authenticate it. FIXME: this will
 * change.
 *
 * @bos:    This is allocated when the BOS descriptors are read from
 *          the device and freed upon the wusb_dev struct dying.
 * @wusb_cap_descr: points into @bos, and has been verified to be size
 *                  safe.
 */
struct wusb_dev {
	struct kref refcnt;
	struct wusbhc *wusbhc;
	struct list_head cack_node;	/* Connect-Ack list */
	u8 port_idx;
	u8 addr;
	u8 beacon_type:4;
	struct usb_encryption_descriptor ccm1_etd;
	struct wusb_ckhdid cdid;
	unsigned long entry_ts;
	struct usb_bos_descriptor *bos;
	struct usb_wireless_cap_descriptor *wusb_cap_descr;
	struct uwb_mas_bm availability;
	struct work_struct devconnect_acked_work;
	struct urb *set_gtk_urb;
	struct usb_ctrlrequest *set_gtk_req;
	struct usb_device *usb_dev;
};

#define WUSB_DEV_ADDR_UNAUTH 0x80

static inline void wusb_dev_init(struct wusb_dev *wusb_dev)
{
	kref_init(&wusb_dev->refcnt);
	/* no need to init the cack_node */
}

extern void wusb_dev_destroy(struct kref *_wusb_dev);

static inline struct wusb_dev *wusb_dev_get(struct wusb_dev *wusb_dev)
{
	kref_get(&wusb_dev->refcnt);
	return wusb_dev;
}

static inline void wusb_dev_put(struct wusb_dev *wusb_dev)
{
	kref_put(&wusb_dev->refcnt, wusb_dev_destroy);
}

/**
 * Wireless USB Host Controlller root hub "fake" ports
 * (state and device information)
 *
 * Wireless USB is wireless, so there are no ports; but we
 * fake'em. Each RC can connect a max of devices at the same time
 * (given in the Wireless Adapter descriptor, bNumPorts or WHCI's
 * caps), referred to in wusbhc->ports_max.
 *
 * See rh.c for more information.
 *
 * The @status and @change use the same bits as in USB2.0[11.24.2.7],
 * so we don't have to do much when getting the port's status.
 *
 * WUSB1.0[7.1], USB2.0[11.24.2.7.1,fig 11-10],
 * include/linux/usb_ch9.h (#define USB_PORT_STAT_*)
 */
struct wusb_port {
	u16 status;
	u16 change;
	struct wusb_dev *wusb_dev;	/* connected device's info */
	unsigned reset_count;
	u32 ptk_tkid;
};

/**
 * WUSB Host Controller specifics
 *
 * All fields that are common to all Wireless USB controller types
 * (HWA and WHCI) are grouped here. Host Controller
 * functions/operations that only deal with general Wireless USB HC
 * issues use this data type to refer to the host.
 *
 * @usb_hcd 	   Instantiation of a USB host controller
 *                 (initialized by upper layer [HWA=HC or WHCI].
 *
 * @dev		   Device that implements this; initialized by the
 *                 upper layer (HWA-HC, WHCI...); this device should
 *                 have a refcount.
 *
 * @trust_timeout  After this time without hearing for device
 *                 activity, we consider the device gone and we have to
 *                 re-authenticate.
 *
 *                 Can be accessed w/o locking--however, read to a
 *                 local variable then use.
 *
 * @chid           WUSB Cluster Host ID: this is supposed to be a
 *                 unique value that doesn't change across reboots (so
 *                 that your devices do not require re-association).
 *
 *                 Read/Write protected by @mutex
 *
 * @dev_info       This array has ports_max elements. It is used to
 *                 give the HC information about the WUSB devices (see
 *                 'struct wusb_dev_info').
 *
 *	           For HWA we need to allocate it in heap; for WHCI it
 *                 needs to be permanently mapped, so we keep it for
 *                 both and make it easy. Call wusbhc->dev_info_set()
 *                 to update an entry.
 *
 * @ports_max	   Number of simultaneous device connections (fake
 *                 ports) this HC will take. Read-only.
 *
 * @port      	   Array of port status for each fake root port. Guaranteed to
 *                 always be the same lenght during device existence
 *                 [this allows for some unlocked but referenced reading].
 *
 * @mmcies_max	   Max number of Information Elements this HC can send
 *                 in its MMC. Read-only.
 *
 * @mmcie_add	   HC specific operation (WHCI or HWA) for adding an
 *                 MMCIE.
 *
 * @mmcie_rm	   HC specific operation (WHCI or HWA) for removing an
 *                 MMCIE.
 *
 * @enc_types	   Array which describes the encryptions methods
 *                 supported by the host as described in WUSB1.0 --
 *                 one entry per supported method. As of WUSB1.0 there
 *                 is only four methods, we make space for eight just in
 *                 case they decide to add some more (and pray they do
 *                 it in sequential order). if 'enc_types[enc_method]
 *                 != 0', then it is supported by the host. enc_method
 *                 is USB_ENC_TYPE*.
 *
 * @set_ptk:       Set the PTK and enable encryption for a device. Or, if
 *                 the supplied key is NULL, disable encryption for that
 *                 device.
 *
 * @set_gtk:       Set the GTK to be used for all future broadcast packets
 *                 (i.e., MMCs).  With some hardware, setting the GTK may start
 *                 MMC transmission.
 *
 * NOTE:
 *
 *  - If wusb_dev->usb_dev is not NULL, then usb_dev is valid
 *    (wusb_dev has a refcount on it). Likewise, if usb_dev->wusb_dev
 *    is not NULL, usb_dev->wusb_dev is valid (usb_dev keeps a
 *    refcount on it).
 *
 *    Most of the times when you need to use it, it will be non-NULL,
 *    so there is no real need to check for it (wusb_dev will
 *    dissapear before usb_dev).
 *
 *  - The following fields need to be filled out before calling
 *    wusbhc_create(): ports_max, mmcies_max, mmcie_{add,rm}.
 *
 *  - there is no wusbhc_init() method, we do everything in
 *    wusbhc_create().
 *
 *  - Creation is done in two phases, wusbhc_create() and
 *    wusbhc_create_b(); b are the parts that need to be called after
 *    calling usb_hcd_add(&wusbhc->usb_hcd).
 */
struct wusbhc {
	struct usb_hcd usb_hcd;		/* HAS TO BE 1st */
	struct device *dev;
	struct uwb_rc *uwb_rc;
	struct uwb_pal pal;

	unsigned trust_timeout;			/* in jiffies */
	struct wuie_host_info *wuie_host_info;	/* Includes CHID */

	struct mutex mutex;			/* locks everything else */
	u16 cluster_id;				/* Wireless USB Cluster ID */
	struct wusb_port *port;			/* Fake port status handling */
	struct wusb_dev_info *dev_info;		/* for Set Device Info mgmt */
	u8 ports_max;
	unsigned active:1;			/* currently xmit'ing MMCs */
	struct wuie_keep_alive keep_alive_ie;	/* protected by mutex */
	struct delayed_work keep_alive_timer;
	struct list_head cack_list;		/* Connect acknowledging */
	size_t cack_count;			/* protected by 'mutex' */
	struct wuie_connect_ack cack_ie;
	struct uwb_rsv *rsv;		/* cluster bandwidth reservation */

	struct mutex mmcie_mutex;		/* MMC WUIE handling */
	struct wuie_hdr **mmcie;		/* WUIE array */
	u8 mmcies_max;
	/* FIXME: make wusbhc_ops? */
	int (*start)(struct wusbhc *wusbhc);
	void (*stop)(struct wusbhc *wusbhc);
	int (*mmcie_add)(struct wusbhc *wusbhc, u8 interval, u8 repeat_cnt,
			 u8 handle, struct wuie_hdr *wuie);
	int (*mmcie_rm)(struct wusbhc *wusbhc, u8 handle);
	int (*dev_info_set)(struct wusbhc *, struct wusb_dev *wusb_dev);
	int (*bwa_set)(struct wusbhc *wusbhc, s8 stream_index,
		       const struct uwb_mas_bm *);
	int (*set_ptk)(struct wusbhc *wusbhc, u8 port_idx,
		       u32 tkid, const void *key, size_t key_size);
	int (*set_gtk)(struct wusbhc *wusbhc,
		       u32 tkid, const void *key, size_t key_size);
	int (*set_num_dnts)(struct wusbhc *wusbhc, u8 interval, u8 slots);

	struct {
		struct usb_key_descriptor descr;
		u8 data[16];				/* GTK key data */
	} __attribute__((packed)) gtk;
	u8 gtk_index;
	u32 gtk_tkid;
	struct work_struct gtk_rekey_done_work;
	int pending_set_gtks;

	struct usb_encryption_descriptor *ccm1_etd;
};

#define usb_hcd_to_wusbhc(u) container_of((u), struct wusbhc, usb_hcd)


extern int wusbhc_create(struct wusbhc *);
extern int wusbhc_b_create(struct wusbhc *);
extern void wusbhc_b_destroy(struct wusbhc *);
extern void wusbhc_destroy(struct wusbhc *);
extern int wusb_dev_sysfs_add(struct wusbhc *, struct usb_device *,
			      struct wusb_dev *);
extern void wusb_dev_sysfs_rm(struct wusb_dev *);
extern int wusbhc_sec_create(struct wusbhc *);
extern int wusbhc_sec_start(struct wusbhc *);
extern void wusbhc_sec_stop(struct wusbhc *);
extern void wusbhc_sec_destroy(struct wusbhc *);
extern void wusbhc_giveback_urb(struct wusbhc *wusbhc, struct urb *urb,
				int status);
void wusbhc_reset_all(struct wusbhc *wusbhc);

int wusbhc_pal_register(struct wusbhc *wusbhc);
void wusbhc_pal_unregister(struct wusbhc *wusbhc);

/*
 * Return @usb_dev's @usb_hcd (properly referenced) or NULL if gone
 *
 * @usb_dev: USB device, UNLOCKED and referenced (or otherwise, safe ptr)
 *
 * This is a safe assumption as @usb_dev->bus is referenced all the
 * time during the @usb_dev life cycle.
 */
static inline struct usb_hcd *usb_hcd_get_by_usb_dev(struct usb_device *usb_dev)
{
	struct usb_hcd *usb_hcd;
	usb_hcd = container_of(usb_dev->bus, struct usb_hcd, self);
	return usb_get_hcd(usb_hcd);
}

/*
 * Increment the reference count on a wusbhc.
 *
 * @wusbhc's life cycle is identical to that of the underlying usb_hcd.
 */
static inline struct wusbhc *wusbhc_get(struct wusbhc *wusbhc)
{
	return usb_get_hcd(&wusbhc->usb_hcd) ? wusbhc : NULL;
}

/*
 * Return the wusbhc associated to a @usb_dev
 *
 * @usb_dev: USB device, UNLOCKED and referenced (or otherwise, safe ptr)
 *
 * @returns: wusbhc for @usb_dev; NULL if the @usb_dev is being torn down.
 *           WARNING: referenced at the usb_hcd level, unlocked
 *
 * FIXME: move offline
 */
static inline struct wusbhc *wusbhc_get_by_usb_dev(struct usb_device *usb_dev)
{
	struct wusbhc *wusbhc = NULL;
	struct usb_hcd *usb_hcd;
	if (usb_dev->devnum > 1 && !usb_dev->wusb) {
		/* but root hubs */
		dev_err(&usb_dev->dev, "devnum %d wusb %d\n", usb_dev->devnum,
			usb_dev->wusb);
		BUG_ON(usb_dev->devnum > 1 && !usb_dev->wusb);
	}
	usb_hcd = usb_hcd_get_by_usb_dev(usb_dev);
	if (usb_hcd == NULL)
		return NULL;
	BUG_ON(usb_hcd->wireless == 0);
	return wusbhc = usb_hcd_to_wusbhc(usb_hcd);
}


static inline void wusbhc_put(struct wusbhc *wusbhc)
{
	usb_put_hcd(&wusbhc->usb_hcd);
}

int wusbhc_start(struct wusbhc *wusbhc, const struct wusb_ckhdid *chid);
void wusbhc_stop(struct wusbhc *wusbhc);
extern int wusbhc_chid_set(struct wusbhc *, const struct wusb_ckhdid *);

/* Device connect handling */
extern int wusbhc_devconnect_create(struct wusbhc *);
extern void wusbhc_devconnect_destroy(struct wusbhc *);
extern int wusbhc_devconnect_start(struct wusbhc *wusbhc,
				   const struct wusb_ckhdid *chid);
extern void wusbhc_devconnect_stop(struct wusbhc *wusbhc);
extern int wusbhc_devconnect_auth(struct wusbhc *, u8);
extern void wusbhc_handle_dn(struct wusbhc *, u8 srcaddr,
			     struct wusb_dn_hdr *dn_hdr, size_t size);
extern int wusbhc_dev_reset(struct wusbhc *wusbhc, u8 port);
extern void __wusbhc_dev_disable(struct wusbhc *wusbhc, u8 port);
extern int wusb_usb_ncb(struct notifier_block *nb, unsigned long val,
			void *priv);
extern int wusb_set_dev_addr(struct wusbhc *wusbhc, struct wusb_dev *wusb_dev,
			     u8 addr);

/* Wireless USB fake Root Hub methods */
extern int wusbhc_rh_create(struct wusbhc *);
extern void wusbhc_rh_destroy(struct wusbhc *);

extern int wusbhc_rh_status_data(struct usb_hcd *, char *);
extern int wusbhc_rh_control(struct usb_hcd *, u16, u16, u16, char *, u16);
extern int wusbhc_rh_suspend(struct usb_hcd *);
extern int wusbhc_rh_resume(struct usb_hcd *);
extern int wusbhc_rh_start_port_reset(struct usb_hcd *, unsigned);

/* MMC handling */
extern int wusbhc_mmcie_create(struct wusbhc *);
extern void wusbhc_mmcie_destroy(struct wusbhc *);
extern int wusbhc_mmcie_set(struct wusbhc *, u8 interval, u8 repeat_cnt,
			    struct wuie_hdr *);
extern void wusbhc_mmcie_rm(struct wusbhc *, struct wuie_hdr *);

/* Bandwidth reservation */
int wusbhc_rsv_establish(struct wusbhc *wusbhc);
void wusbhc_rsv_terminate(struct wusbhc *wusbhc);

/*
 * I've always said
 * I wanted a wedding in a church...
 *
 * but lately I've been thinking about
 * the Botanical Gardens.
 *
 * We could do it by the tulips.
 * It'll be beautiful
 *
 * --Security!
 */
extern int wusb_dev_sec_add(struct wusbhc *, struct usb_device *,
				struct wusb_dev *);
extern void wusb_dev_sec_rm(struct wusb_dev *) ;
extern int wusb_dev_4way_handshake(struct wusbhc *, struct wusb_dev *,
				   struct wusb_ckhdid *ck);
void wusbhc_gtk_rekey(struct wusbhc *wusbhc);


/* WUSB Cluster ID handling */
extern u8 wusb_cluster_id_get(void);
extern void wusb_cluster_id_put(u8);

/*
 * wusb_port_by_idx - return the port associated to a zero-based port index
 *
 * NOTE: valid without locking as long as wusbhc is referenced (as the
 *       number of ports doesn't change). The data pointed to has to
 *       be verified though :)
 */
static inline struct wusb_port *wusb_port_by_idx(struct wusbhc *wusbhc,
						 u8 port_idx)
{
	return &wusbhc->port[port_idx];
}

/*
 * wusb_port_no_to_idx - Convert port number (per usb_dev->portnum) to
 * a port_idx.
 *
 * USB stack USB ports are 1 based!!
 *
 * NOTE: only valid for WUSB devices!!!
 */
static inline u8 wusb_port_no_to_idx(u8 port_no)
{
	return port_no - 1;
}

extern struct wusb_dev *__wusb_dev_get_by_usb_dev(struct wusbhc *,
						  struct usb_device *);

/*
 * Return a referenced wusb_dev given a @usb_dev
 *
 * Returns NULL if the usb_dev is being torn down.
 *
 * FIXME: move offline
 */
static inline
struct wusb_dev *wusb_dev_get_by_usb_dev(struct usb_device *usb_dev)
{
	struct wusbhc *wusbhc;
	struct wusb_dev *wusb_dev;
	wusbhc = wusbhc_get_by_usb_dev(usb_dev);
	if (wusbhc == NULL)
		return NULL;
	mutex_lock(&wusbhc->mutex);
	wusb_dev = __wusb_dev_get_by_usb_dev(wusbhc, usb_dev);
	mutex_unlock(&wusbhc->mutex);
	wusbhc_put(wusbhc);
	return wusb_dev;
}

/* Misc */

extern struct workqueue_struct *wusbd;
#endif /* #ifndef __WUSBHC_H__ */
