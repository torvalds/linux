/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_pcd.h $
 * $Revision: #48 $
 * $Date: 2012/08/10 $
 * $Change: 2047372 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */
#ifndef DWC_HOST_ONLY
#if !defined(__DWC_PCD_H__)
#define __DWC_PCD_H__

#include "dwc_otg_os_dep.h"
#include "common_port/usb.h"
#include "dwc_otg_cil.h"
#include "dwc_otg_pcd_if.h"
struct cfiobject;

/**
 * @file
 *
 * This file contains the structures, constants, and interfaces for
 * the Perpherial Contoller Driver (PCD).
 *
 * The Peripheral Controller Driver (PCD) for Linux will implement the
 * Gadget API, so that the existing Gadget drivers can be used. For
 * the Mass Storage Function driver the File-backed USB Storage Gadget
 * (FBS) driver will be used.  The FBS driver supports the
 * Control-Bulk (CB), Control-Bulk-Interrupt (CBI), and Bulk-Only
 * transports.
 *
 */

/** Invalid DMA Address */
#define DWC_DMA_ADDR_INVALID	(~(dwc_dma_t)0 & 0xFFFFFFFC)

/** Max Transfer size for any EP */
#define DDMA_MAX_TRANSFER_SIZE 65535

/**
 * Get the pointer to the core_if from the pcd pointer.
 */
#define GET_CORE_IF(_pcd) (_pcd->core_if)

/**
 * States of EP0.
 */
typedef enum ep0_state {
	EP0_DISCONNECT,		/* no host */
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_IN_STATUS_PHASE,
	EP0_OUT_STATUS_PHASE,
	EP0_STALL,
} ep0state_e;

/** Fordward declaration.*/
struct dwc_otg_pcd;

/** DWC_otg iso request structure.
 *
 */
typedef struct usb_iso_request dwc_otg_pcd_iso_request_t;

#ifdef DWC_UTE_PER_IO

/**
 * This shall be the exact analogy of the same type structure defined in the
 * usb_gadget.h. Each descriptor contains
 */
struct dwc_iso_pkt_desc_port {
	uint32_t offset;
	uint32_t length;	/* expected length */
	uint32_t actual_length;
	uint32_t status;
};

struct dwc_iso_xreq_port {
	/** transfer/submission flag */
	uint32_t tr_sub_flags;
	/** Start the request ASAP */
#define DWC_EREQ_TF_ASAP		0x00000002
	/** Just enqueue the request w/o initiating a transfer */
#define DWC_EREQ_TF_ENQUEUE		0x00000004

	/**
	* count of ISO packets attached to this request - shall
	* not exceed the pio_alloc_pkt_count
	*/
	uint32_t pio_pkt_count;
	/** count of ISO packets allocated for this request */
	uint32_t pio_alloc_pkt_count;
	/** number of ISO packet errors */
	uint32_t error_count;
	/** reserved for future extension */
	uint32_t res;
	/** Will be allocated and freed in the UTE gadget and based on the CFC value */
	struct dwc_iso_pkt_desc_port *per_io_frame_descs;
};
#endif
/** DWC_otg request structure.
 * This structure is a list of requests.
 */
typedef struct dwc_otg_pcd_request {
	void *priv;
	void *buf;
	dwc_dma_t dma;
	uint32_t length;
	uint32_t actual;
	unsigned sent_zlp:1;
    /**
     * Used instead of original buffer if
     * it(physical address) is not dword-aligned.
     **/
	uint8_t *dw_align_buf;
	dwc_dma_t dw_align_buf_dma;

	 DWC_CIRCLEQ_ENTRY(dwc_otg_pcd_request) queue_entry;
#ifdef DWC_UTE_PER_IO
	struct dwc_iso_xreq_port ext_req;
	/* void *priv_ereq_nport; */
#endif
} dwc_otg_pcd_request_t;

DWC_CIRCLEQ_HEAD(req_list, dwc_otg_pcd_request);

/**	  PCD EP structure.
 * This structure describes an EP, there is an array of EPs in the PCD
 * structure.
 */
typedef struct dwc_otg_pcd_ep {
	/** USB EP Descriptor */
	const usb_endpoint_descriptor_t *desc;

	/** queue of dwc_otg_pcd_requests. */
	struct req_list queue;
	unsigned stopped:1;
	unsigned disabling:1;
	unsigned dma:1;
	unsigned queue_sof:1;

#ifdef DWC_EN_ISOC
	/** ISOC req handle passed */
	void *iso_req_handle;
#endif /* _EN_ISOC_ */

	/** DWC_otg ep data. */
	dwc_ep_t dwc_ep;

	/** Pointer to PCD */
	struct dwc_otg_pcd *pcd;

	void *priv;
} dwc_otg_pcd_ep_t;

/** DWC_otg PCD Structure.
 * This structure encapsulates the data for the dwc_otg PCD.
 */
struct dwc_otg_pcd {
	const struct dwc_otg_pcd_function_ops *fops;
	/** The DWC otg device pointer */
	struct dwc_otg_device *otg_dev;
	/** Core Interface */
	dwc_otg_core_if_t *core_if;
	/** State of EP0 */
	ep0state_e ep0state;
	/** EP0 Request is pending */
	unsigned ep0_pending:1;
	/** Indicates when SET CONFIGURATION Request is in process */
	unsigned request_config:1;
	/** The state of the Remote Wakeup Enable. */
	unsigned remote_wakeup_enable:1;
	/** The state of the B-Device HNP Enable. */
	unsigned b_hnp_enable:1;
	/** The state of A-Device HNP Support. */
	unsigned a_hnp_support:1;
	/** The state of the A-Device Alt HNP support. */
	unsigned a_alt_hnp_support:1;
	/** Count of pending Requests */
	unsigned request_pending;

	/** SETUP packet for EP0
	 * This structure is allocated as a DMA buffer on PCD initialization
	 * with enough space for up to 3 setup packets.
	 */
	union {
		usb_device_request_t req;
		uint32_t d32[2];
	} *setup_pkt;

	dwc_dma_t setup_pkt_dma_handle;

	/* Additional buffer and flag for CTRL_WR premature case */
	uint8_t *backup_buf;
	unsigned data_terminated;

	/** 2-byte dma buffer used to return status from GET_STATUS */
	uint16_t *status_buf;
	dwc_dma_t status_buf_dma_handle;

	/** EP0 */
	dwc_otg_pcd_ep_t ep0;

	/** Array of IN EPs. */
	dwc_otg_pcd_ep_t in_ep[MAX_EPS_CHANNELS - 1];
	/** Array of OUT EPs. */
	dwc_otg_pcd_ep_t out_ep[MAX_EPS_CHANNELS - 1];
	/** number of valid EPs in the above array. */
	/** unsigned      num_eps : 4; */
	dwc_spinlock_t *lock;

	/** Tasklet to defer starting of TEST mode transmissions until
	 *	Status Phase has been completed.
	 */
	dwc_tasklet_t *test_mode_tasklet;

	/** Tasklet to delay starting of xfer in DMA mode */
	dwc_tasklet_t *start_xfer_tasklet;

	/** The test mode to enter when the tasklet is executed. */
	unsigned test_mode;
	/** The cfi_api structure that implements most of the CFI API
	 * and OTG specific core configuration functionality
	 */
#ifdef DWC_UTE_CFI
	struct cfiobject *cfi;
#endif

	/* true when pullup operation is set to on */
	bool pullups_connected;
	/** otg phy may be suspend in device mode, 1:suspend, 0:normal */
	uint8_t phy_suspend;
	/** vbus status in device mode */
	uint8_t vbus_status;
	/** enable connect to PC in device mode */
	uint8_t conn_en;
	/** connect status used during enumeration */
	int8_t conn_status;
	/* otg check vbus work and connect work,used for power management */
	struct delayed_work reconnect;
	struct delayed_work check_vbus_work;
	struct delayed_work check_id_work;
	/** pervent device suspend while usb connected */
	struct wake_lock wake_lock;

};

/* FIXME this functions should be static,
 * and this prototypes should be removed */
extern void dwc_otg_request_nuke(dwc_otg_pcd_ep_t *ep);
extern void dwc_otg_request_done(dwc_otg_pcd_ep_t *ep,
				 dwc_otg_pcd_request_t *req, int32_t status);

void dwc_otg_iso_buffer_done(dwc_otg_pcd_t *pcd, dwc_otg_pcd_ep_t *ep,
			     void *req_handle);

extern void do_test_mode(void *data);
extern void dwc_pcd_reset(dwc_otg_pcd_t *pcd);
extern void dwc_otg_pcd_start_check_vbus_work(dwc_otg_pcd_t *pcd);
#endif
#endif /* DWC_HOST_ONLY */
