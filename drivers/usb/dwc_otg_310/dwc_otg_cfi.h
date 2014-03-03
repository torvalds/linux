/* ==========================================================================
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

#if !defined(__DWC_OTG_CFI_H__)
#define __DWC_OTG_CFI_H__

#include "dwc_otg_pcd.h"
#include "dwc_cfi_common.h"

/**
 * @file
 * This file contains the CFI related OTG PCD specific common constants, 
 * interfaces(functions and macros) and data structures.The CFI Protocol is an 
 * optional interface for internal testing purposes that a DUT may implement to 
 * support testing of configurable features.
 *
 */

struct dwc_otg_pcd;
struct dwc_otg_pcd_ep;

/** OTG CFI Features (properties) ID constants */
/** This is a request for all Core Features */
#define FT_ID_DMA_MODE					0x0001
#define FT_ID_DMA_BUFFER_SETUP			0x0002
#define FT_ID_DMA_BUFF_ALIGN			0x0003
#define FT_ID_DMA_CONCAT_SETUP			0x0004
#define FT_ID_DMA_CIRCULAR				0x0005
#define FT_ID_THRESHOLD_SETUP			0x0006
#define FT_ID_DFIFO_DEPTH				0x0007
#define FT_ID_TX_FIFO_DEPTH				0x0008
#define FT_ID_RX_FIFO_DEPTH				0x0009

/**********************************************************/
#define CFI_INFO_DEF

#ifdef CFI_INFO_DEF
#define CFI_INFO(fmt...)	DWC_PRINTF("CFI: " fmt);
#else
#define CFI_INFO(fmt...)
#endif

#define min(x,y) ({ \
	x < y ? x : y; })

#define max(x,y) ({ \
	x > y ? x : y; })

/**
 * Descriptor DMA SG Buffer setup structure (SG buffer). This structure is
 * also used for setting up a buffer for Circular DDMA.
 */
struct _ddma_sg_buffer_setup {
#define BS_SG_VAL_DESC_LEN	6
	/* The OUT EP address */
	uint8_t bOutEndpointAddress;
	/* The IN EP address */
	uint8_t bInEndpointAddress;
	/* Number of bytes to put between transfer segments (must be DWORD boundaries) */
	uint8_t bOffset;
	/* The number of transfer segments (a DMA descriptors per each segment) */
	uint8_t bCount;
	/* Size (in byte) of each transfer segment */
	uint16_t wSize;
} __attribute__ ((packed));
typedef struct _ddma_sg_buffer_setup ddma_sg_buffer_setup_t;

/** Descriptor DMA Concatenation Buffer setup structure */
struct _ddma_concat_buffer_setup_hdr {
#define BS_CONCAT_VAL_HDR_LEN	4
	/* The endpoint for which the buffer is to be set up */
	uint8_t bEndpointAddress;
	/* The count of descriptors to be used */
	uint8_t bDescCount;
	/* The total size of the transfer */
	uint16_t wSize;
} __attribute__ ((packed));
typedef struct _ddma_concat_buffer_setup_hdr ddma_concat_buffer_setup_hdr_t;

/** Descriptor DMA Concatenation Buffer setup structure */
struct _ddma_concat_buffer_setup {
	/* The SG header */
	ddma_concat_buffer_setup_hdr_t hdr;

	/* The XFER sizes pointer (allocated dynamically) */
	uint16_t *wTxBytes;
} __attribute__ ((packed));
typedef struct _ddma_concat_buffer_setup ddma_concat_buffer_setup_t;

/** Descriptor DMA Alignment Buffer setup structure */
struct _ddma_align_buffer_setup {
#define BS_ALIGN_VAL_HDR_LEN	2
	uint8_t bEndpointAddress;
	uint8_t bAlign;
} __attribute__ ((packed));
typedef struct _ddma_align_buffer_setup ddma_align_buffer_setup_t;

/** Transmit FIFO Size setup structure */
struct _tx_fifo_size_setup {
	uint8_t bEndpointAddress;
	uint16_t wDepth;
} __attribute__ ((packed));
typedef struct _tx_fifo_size_setup tx_fifo_size_setup_t;

/** Transmit FIFO Size setup structure */
struct _rx_fifo_size_setup {
	uint16_t wDepth;
} __attribute__ ((packed));
typedef struct _rx_fifo_size_setup rx_fifo_size_setup_t;

/**
 * struct cfi_usb_ctrlrequest - the CFI implementation of the struct usb_ctrlrequest
 * This structure encapsulates the standard usb_ctrlrequest and adds a pointer
 * to the data returned in the data stage of a 3-stage Control Write requests.
 */
struct cfi_usb_ctrlrequest {
	uint8_t bRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
	uint8_t *data;
} UPACKED;

/*---------------------------------------------------------------------------*/

/**
 * The CFI wrapper of the enabled and activated dwc_otg_pcd_ep structures.
 * This structure is used to store the buffer setup data for any
 * enabled endpoint in the PCD.
 */
struct cfi_ep {
	/* Entry for the list container */
	dwc_list_link_t lh;
	/* Pointer to the active PCD endpoint structure */
	struct dwc_otg_pcd_ep *ep;
	/* The last descriptor in the chain of DMA descriptors of the endpoint */
	struct dwc_otg_dma_desc *dma_desc_last;
	/* The SG feature value */
	ddma_sg_buffer_setup_t *bm_sg;
	/* The Circular feature value */
	ddma_sg_buffer_setup_t *bm_circ;
	/* The Concatenation feature value */
	ddma_concat_buffer_setup_t *bm_concat;
	/* The Alignment feature value */
	ddma_align_buffer_setup_t *bm_align;
	/* XFER length */
	uint32_t xfer_len;
	/*
	 * Count of DMA descriptors currently used.
	 * The total should not exceed the MAX_DMA_DESCS_PER_EP value
	 * defined in the dwc_otg_cil.h
	 */
	uint32_t desc_count;
};
typedef struct cfi_ep cfi_ep_t;

typedef struct cfi_dma_buff {
#define CFI_IN_BUF_LEN	1024
#define CFI_OUT_BUF_LEN	1024
	dma_addr_t addr;
	uint8_t *buf;
} cfi_dma_buff_t;

struct cfiobject;

/**
 * This is the interface for the CFI operations.
 *
 * @param	ep_enable			Called when any endpoint is enabled and activated.
 * @param	release				Called when the CFI object is released and it needs to correctly
 *								deallocate the dynamic memory
 * @param	ctrl_write_complete	Called when the data stage of the request is complete
 */
typedef struct cfi_ops {
	int (*ep_enable) (struct cfiobject * cfi, struct dwc_otg_pcd * pcd,
			  struct dwc_otg_pcd_ep * ep);
	void *(*ep_alloc_buf) (struct cfiobject * cfi, struct dwc_otg_pcd * pcd,
			       struct dwc_otg_pcd_ep * ep, dma_addr_t * dma,
			       unsigned size, gfp_t flags);
	void (*release) (struct cfiobject * cfi);
	int (*ctrl_write_complete) (struct cfiobject * cfi,
				    struct dwc_otg_pcd * pcd);
	void (*build_descriptors) (struct cfiobject * cfi,
				   struct dwc_otg_pcd * pcd,
				   struct dwc_otg_pcd_ep * ep,
				   dwc_otg_pcd_request_t * req);
} cfi_ops_t;

struct cfiobject {
	cfi_ops_t ops;
	struct dwc_otg_pcd *pcd;
	struct usb_gadget *gadget;

	/* Buffers used to send/receive CFI-related request data */
	cfi_dma_buff_t buf_in;
	cfi_dma_buff_t buf_out;

	/* CFI specific Control request wrapper */
	struct cfi_usb_ctrlrequest ctrl_req;

	/* The list of active EP's in the PCD of type cfi_ep_t */
	dwc_list_link_t active_eps;

	/* This flag shall control the propagation of a specific request
	 * to the gadget's processing routines.
	 * 0 - no gadget handling
	 * 1 - the gadget needs to know about this request (w/o completing a status
	 * phase - just return a 0 to the _setup callback)
	 */
	uint8_t need_gadget_att;

	/* Flag indicating whether the status IN phase needs to be
	 * completed by the PCD
	 */
	uint8_t need_status_in_complete;
};
typedef struct cfiobject cfiobject_t;

#define DUMP_MSG

#if defined(DUMP_MSG)
static inline void dump_msg(const u8 * buf, unsigned int length)
{
	unsigned int start, num, i;
	char line[52], *p;

	if (length >= 512)
		return;

	start = 0;
	while (length > 0) {
		num = min(length, 16u);
		p = line;
		for (i = 0; i < num; ++i) {
			if (i == 8)
				*p++ = ' ';
			DWC_SPRINTF(p, " %02x", buf[i]);
			p += 3;
		}
		*p = 0;
		DWC_DEBUG("%6x: %s\n", start, line);
		buf += num;
		start += num;
		length -= num;
	}
}
#else
static inline void dump_msg(const u8 * buf, unsigned int length)
{
}
#endif

/**
 * This function returns a pointer to cfi_ep_t object with the addr address.
 */
static inline struct cfi_ep *get_cfi_ep_by_addr(struct cfiobject *cfi,
						uint8_t addr)
{
	struct cfi_ep *pcfiep;
	dwc_list_link_t *tmp;

	DWC_LIST_FOREACH(tmp, &cfi->active_eps) {
		pcfiep = DWC_LIST_ENTRY(tmp, struct cfi_ep, lh);

		if (pcfiep->ep->desc->bEndpointAddress == addr) {
			return pcfiep;
		}
	}

	return NULL;
}

/**
 * This function returns a pointer to cfi_ep_t object that matches
 * the dwc_otg_pcd_ep object.
 */
static inline struct cfi_ep *get_cfi_ep_by_pcd_ep(struct cfiobject *cfi,
						  struct dwc_otg_pcd_ep *ep)
{
	struct cfi_ep *pcfiep = NULL;
	dwc_list_link_t *tmp;

	DWC_LIST_FOREACH(tmp, &cfi->active_eps) {
		pcfiep = DWC_LIST_ENTRY(tmp, struct cfi_ep, lh);
		if (pcfiep->ep == ep) {
			return pcfiep;
		}
	}
	return NULL;
}

int cfi_setup(struct dwc_otg_pcd *pcd, struct cfi_usb_ctrlrequest *ctrl);

#endif /* (__DWC_OTG_CFI_H__) */
