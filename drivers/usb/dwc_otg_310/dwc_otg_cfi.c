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

/** @file 
 *
 * This file contains the most of the CFI(Core Feature Interface) 
 * implementation for the OTG. 
 */

#ifdef DWC_UTE_CFI

#include "dwc_otg_pcd.h"
#include "dwc_otg_cfi.h"

/** This definition should actually migrate to the Portability Library */
#define DWC_CONSTANT_CPU_TO_LE16(x) (x)

extern dwc_otg_pcd_ep_t *get_ep_by_addr(dwc_otg_pcd_t * pcd, u16 wIndex);

static int cfi_core_features_buf(uint8_t * buf, uint16_t buflen);
static int cfi_get_feature_value(uint8_t * buf, uint16_t buflen,
				 struct dwc_otg_pcd *pcd,
				 struct cfi_usb_ctrlrequest *ctrl_req);
static int cfi_set_feature_value(struct dwc_otg_pcd *pcd);
static int cfi_ep_get_sg_val(uint8_t * buf, struct dwc_otg_pcd *pcd,
			     struct cfi_usb_ctrlrequest *req);
static int cfi_ep_get_concat_val(uint8_t * buf, struct dwc_otg_pcd *pcd,
				 struct cfi_usb_ctrlrequest *req);
static int cfi_ep_get_align_val(uint8_t * buf, struct dwc_otg_pcd *pcd,
				struct cfi_usb_ctrlrequest *req);
static int cfi_preproc_reset(struct dwc_otg_pcd *pcd,
			     struct cfi_usb_ctrlrequest *req);
static void cfi_free_ep_bs_dyn_data(cfi_ep_t * cfiep);

static uint16_t get_dfifo_size(dwc_otg_core_if_t * core_if);
static int32_t get_rxfifo_size(dwc_otg_core_if_t * core_if, uint16_t wValue);
static int32_t get_txfifo_size(struct dwc_otg_pcd *pcd, uint16_t wValue);

static uint8_t resize_fifos(dwc_otg_core_if_t * core_if);

/** This is the header of the all features descriptor */
static cfi_all_features_header_t all_props_desc_header = {
	.wVersion = DWC_CONSTANT_CPU_TO_LE16(0x100),
	.wCoreID = DWC_CONSTANT_CPU_TO_LE16(CFI_CORE_ID_OTG),
	.wNumFeatures = DWC_CONSTANT_CPU_TO_LE16(9),
};

/** This is an array of statically allocated feature descriptors */
static cfi_feature_desc_header_t prop_descs[] = {

	/* FT_ID_DMA_MODE */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_DMA_MODE),
	 .bmAttributes = CFI_FEATURE_ATTR_RW,
	 .wDataLength = DWC_CONSTANT_CPU_TO_LE16(1),
	 },

	/* FT_ID_DMA_BUFFER_SETUP */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_DMA_BUFFER_SETUP),
	 .bmAttributes = CFI_FEATURE_ATTR_RW,
	 .wDataLength = DWC_CONSTANT_CPU_TO_LE16(6),
	 },

	/* FT_ID_DMA_BUFF_ALIGN */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_DMA_BUFF_ALIGN),
	 .bmAttributes = CFI_FEATURE_ATTR_RW,
	 .wDataLength = DWC_CONSTANT_CPU_TO_LE16(2),
	 },

	/* FT_ID_DMA_CONCAT_SETUP */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_DMA_CONCAT_SETUP),
	 .bmAttributes = CFI_FEATURE_ATTR_RW,
	 //.wDataLength  = DWC_CONSTANT_CPU_TO_LE16(6),
	 },

	/* FT_ID_DMA_CIRCULAR */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_DMA_CIRCULAR),
	 .bmAttributes = CFI_FEATURE_ATTR_RW,
	 .wDataLength = DWC_CONSTANT_CPU_TO_LE16(6),
	 },

	/* FT_ID_THRESHOLD_SETUP */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_THRESHOLD_SETUP),
	 .bmAttributes = CFI_FEATURE_ATTR_RW,
	 .wDataLength = DWC_CONSTANT_CPU_TO_LE16(6),
	 },

	/* FT_ID_DFIFO_DEPTH */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_DFIFO_DEPTH),
	 .bmAttributes = CFI_FEATURE_ATTR_RO,
	 .wDataLength = DWC_CONSTANT_CPU_TO_LE16(2),
	 },

	/* FT_ID_TX_FIFO_DEPTH */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_TX_FIFO_DEPTH),
	 .bmAttributes = CFI_FEATURE_ATTR_RW,
	 .wDataLength = DWC_CONSTANT_CPU_TO_LE16(2),
	 },

	/* FT_ID_RX_FIFO_DEPTH */
	{
	 .wFeatureID = DWC_CONSTANT_CPU_TO_LE16(FT_ID_RX_FIFO_DEPTH),
	 .bmAttributes = CFI_FEATURE_ATTR_RW,
	 .wDataLength = DWC_CONSTANT_CPU_TO_LE16(2),
	 }
};

/** The table of feature names */
cfi_string_t prop_name_table[] = {
	{FT_ID_DMA_MODE, "dma_mode"},
	{FT_ID_DMA_BUFFER_SETUP, "buffer_setup"},
	{FT_ID_DMA_BUFF_ALIGN, "buffer_align"},
	{FT_ID_DMA_CONCAT_SETUP, "concat_setup"},
	{FT_ID_DMA_CIRCULAR, "buffer_circular"},
	{FT_ID_THRESHOLD_SETUP, "threshold_setup"},
	{FT_ID_DFIFO_DEPTH, "dfifo_depth"},
	{FT_ID_TX_FIFO_DEPTH, "txfifo_depth"},
	{FT_ID_RX_FIFO_DEPTH, "rxfifo_depth"},
	{}
};

/************************************************************************/

/** 
 * Returns the name of the feature by its ID 
 * or NULL if no featute ID matches.
 * 
 */
const uint8_t *get_prop_name(uint16_t prop_id, int *len)
{
	cfi_string_t *pstr;
	*len = 0;

	for (pstr = prop_name_table; pstr && pstr->s; pstr++) {
		if (pstr->id == prop_id) {
			*len = DWC_STRLEN(pstr->s);
			return pstr->s;
		}
	}
	return NULL;
}

/**
 * This function handles all CFI specific control requests.
 * 
 * Return a negative value to stall the DCE.
 */
int cfi_setup(struct dwc_otg_pcd *pcd, struct cfi_usb_ctrlrequest *ctrl)
{
	int retval = 0;
	dwc_otg_pcd_ep_t *ep = NULL;
	cfiobject_t *cfi = pcd->cfi;
	struct dwc_otg_core_if *coreif = GET_CORE_IF(pcd);
	uint16_t wLen = DWC_LE16_TO_CPU(&ctrl->wLength);
	uint16_t wValue = DWC_LE16_TO_CPU(&ctrl->wValue);
	uint16_t wIndex = DWC_LE16_TO_CPU(&ctrl->wIndex);
	uint32_t regaddr = 0;
	uint32_t regval = 0;

	/* Save this Control Request in the CFI object. 
	 * The data field will be assigned in the data stage completion CB function.
	 */
	cfi->ctrl_req = *ctrl;
	cfi->ctrl_req.data = NULL;

	cfi->need_gadget_att = 0;
	cfi->need_status_in_complete = 0;

	switch (ctrl->bRequest) {
	case VEN_CORE_GET_FEATURES:
		retval = cfi_core_features_buf(cfi->buf_in.buf, CFI_IN_BUF_LEN);
		if (retval >= 0) {
			//dump_msg(cfi->buf_in.buf, retval);
			ep = &pcd->ep0;

			retval = min((uint16_t) retval, wLen);
			/* Transfer this buffer to the host through the EP0-IN EP */
			ep->dwc_ep.dma_addr = cfi->buf_in.addr;
			ep->dwc_ep.start_xfer_buff = cfi->buf_in.buf;
			ep->dwc_ep.xfer_buff = cfi->buf_in.buf;
			ep->dwc_ep.xfer_len = retval;
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

			pcd->ep0_pending = 1;
			dwc_otg_ep0_start_transfer(coreif, &ep->dwc_ep);
		}
		retval = 0;
		break;

	case VEN_CORE_GET_FEATURE:
		CFI_INFO("VEN_CORE_GET_FEATURE\n");
		retval = cfi_get_feature_value(cfi->buf_in.buf, CFI_IN_BUF_LEN,
					       pcd, ctrl);
		if (retval >= 0) {
			ep = &pcd->ep0;

			retval = min((uint16_t) retval, wLen);
			/* Transfer this buffer to the host through the EP0-IN EP */
			ep->dwc_ep.dma_addr = cfi->buf_in.addr;
			ep->dwc_ep.start_xfer_buff = cfi->buf_in.buf;
			ep->dwc_ep.xfer_buff = cfi->buf_in.buf;
			ep->dwc_ep.xfer_len = retval;
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

			pcd->ep0_pending = 1;
			dwc_otg_ep0_start_transfer(coreif, &ep->dwc_ep);
		}
		CFI_INFO("VEN_CORE_GET_FEATURE=%d\n", retval);
		dump_msg(cfi->buf_in.buf, retval);
		break;

	case VEN_CORE_SET_FEATURE:
		CFI_INFO("VEN_CORE_SET_FEATURE\n");
		/* Set up an XFER to get the data stage of the control request,
		 * which is the new value of the feature to be modified.
		 */
		ep = &pcd->ep0;
		ep->dwc_ep.is_in = 0;
		ep->dwc_ep.dma_addr = cfi->buf_out.addr;
		ep->dwc_ep.start_xfer_buff = cfi->buf_out.buf;
		ep->dwc_ep.xfer_buff = cfi->buf_out.buf;
		ep->dwc_ep.xfer_len = wLen;
		ep->dwc_ep.xfer_count = 0;
		ep->dwc_ep.sent_zlp = 0;
		ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

		pcd->ep0_pending = 1;
		/* Read the control write's data stage */
		dwc_otg_ep0_start_transfer(coreif, &ep->dwc_ep);
		retval = 0;
		break;

	case VEN_CORE_RESET_FEATURES:
		CFI_INFO("VEN_CORE_RESET_FEATURES\n");
		cfi->need_gadget_att = 1;
		cfi->need_status_in_complete = 1;
		retval = cfi_preproc_reset(pcd, ctrl);
		CFI_INFO("VEN_CORE_RESET_FEATURES = (%d)\n", retval);
		break;

	case VEN_CORE_ACTIVATE_FEATURES:
		CFI_INFO("VEN_CORE_ACTIVATE_FEATURES\n");
		break;

	case VEN_CORE_READ_REGISTER:
		CFI_INFO("VEN_CORE_READ_REGISTER\n");
		/* wValue optionally contains the HI WORD of the register offset and
		 * wIndex contains the LOW WORD of the register offset 
		 */
		if (wValue == 0) {
			/* @TODO - MAS - fix the access to the base field */
			regaddr = 0;
			//regaddr = (uint32_t) pcd->otg_dev->os_dep.base;
			//GET_CORE_IF(pcd)->co
			regaddr |= wIndex;
		} else {
			regaddr = (wValue << 16) | wIndex;
		}

		/* Read a 32-bit value of the memory at the regaddr */
		regval = DWC_READ_REG32((uint32_t *) regaddr);

		ep = &pcd->ep0;
		dwc_memcpy(cfi->buf_in.buf, &regval, sizeof(uint32_t));
		ep->dwc_ep.is_in = 1;
		ep->dwc_ep.dma_addr = cfi->buf_in.addr;
		ep->dwc_ep.start_xfer_buff = cfi->buf_in.buf;
		ep->dwc_ep.xfer_buff = cfi->buf_in.buf;
		ep->dwc_ep.xfer_len = wLen;
		ep->dwc_ep.xfer_count = 0;
		ep->dwc_ep.sent_zlp = 0;
		ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

		pcd->ep0_pending = 1;
		dwc_otg_ep0_start_transfer(coreif, &ep->dwc_ep);
		cfi->need_gadget_att = 0;
		retval = 0;
		break;

	case VEN_CORE_WRITE_REGISTER:
		CFI_INFO("VEN_CORE_WRITE_REGISTER\n");
		/* Set up an XFER to get the data stage of the control request,
		 * which is the new value of the register to be modified.
		 */
		ep = &pcd->ep0;
		ep->dwc_ep.is_in = 0;
		ep->dwc_ep.dma_addr = cfi->buf_out.addr;
		ep->dwc_ep.start_xfer_buff = cfi->buf_out.buf;
		ep->dwc_ep.xfer_buff = cfi->buf_out.buf;
		ep->dwc_ep.xfer_len = wLen;
		ep->dwc_ep.xfer_count = 0;
		ep->dwc_ep.sent_zlp = 0;
		ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

		pcd->ep0_pending = 1;
		/* Read the control write's data stage */
		dwc_otg_ep0_start_transfer(coreif, &ep->dwc_ep);
		retval = 0;
		break;

	default:
		retval = -DWC_E_NOT_SUPPORTED;
		break;
	}

	return retval;
}

/**
 * This function prepares the core features descriptors and copies its
 * raw representation into the buffer <buf>.
 * 
 * The buffer structure is as follows:
 *	all_features_header (8 bytes)
 *	features_#1 (8 bytes + feature name string length)
 *	features_#2 (8 bytes + feature name string length)
 *	.....
 *	features_#n - where n=the total count of feature descriptors
 */
static int cfi_core_features_buf(uint8_t * buf, uint16_t buflen)
{
	cfi_feature_desc_header_t *prop_hdr = prop_descs;
	cfi_feature_desc_header_t *prop;
	cfi_all_features_header_t *all_props_hdr = &all_props_desc_header;
	cfi_all_features_header_t *tmp;
	uint8_t *tmpbuf = buf;
	const uint8_t *pname = NULL;
	int i, j, namelen = 0, totlen;

	/* Prepare and copy the core features into the buffer */
	CFI_INFO("%s:\n", __func__);

	tmp = (cfi_all_features_header_t *) tmpbuf;
	*tmp = *all_props_hdr;
	tmpbuf += CFI_ALL_FEATURES_HDR_LEN;

	j = sizeof(prop_descs) / sizeof(cfi_all_features_header_t);
	for (i = 0; i < j; i++, prop_hdr++) {
		pname = get_prop_name(prop_hdr->wFeatureID, &namelen);
		prop = (cfi_feature_desc_header_t *) tmpbuf;
		*prop = *prop_hdr;

		prop->bNameLen = namelen;
		prop->wLength =
		    DWC_CONSTANT_CPU_TO_LE16(CFI_FEATURE_DESC_HDR_LEN +
					     namelen);

		tmpbuf += CFI_FEATURE_DESC_HDR_LEN;
		dwc_memcpy(tmpbuf, pname, namelen);
		tmpbuf += namelen;
	}

	totlen = tmpbuf - buf;

	if (totlen > 0) {
		tmp = (cfi_all_features_header_t *) buf;
		tmp->wTotalLen = DWC_CONSTANT_CPU_TO_LE16(totlen);
	}

	return totlen;
}

/**
 * This function releases all the dynamic memory in the CFI object.
 */
static void cfi_release(cfiobject_t * cfiobj)
{
	cfi_ep_t *cfiep;
	dwc_list_link_t *tmp;

	CFI_INFO("%s\n", __func__);

	if (cfiobj->buf_in.buf) {
		DWC_DMA_FREE(CFI_IN_BUF_LEN, cfiobj->buf_in.buf,
			     cfiobj->buf_in.addr);
		cfiobj->buf_in.buf = NULL;
	}

	if (cfiobj->buf_out.buf) {
		DWC_DMA_FREE(CFI_OUT_BUF_LEN, cfiobj->buf_out.buf,
			     cfiobj->buf_out.addr);
		cfiobj->buf_out.buf = NULL;
	}

	/* Free the Buffer Setup values for each EP */
	//list_for_each_entry(cfiep, &cfiobj->active_eps, lh) {
	DWC_LIST_FOREACH(tmp, &cfiobj->active_eps) {
		cfiep = DWC_LIST_ENTRY(tmp, struct cfi_ep, lh);
		cfi_free_ep_bs_dyn_data(cfiep);
	}
}

/**
 * This function frees the dynamically allocated EP buffer setup data.
 */
static void cfi_free_ep_bs_dyn_data(cfi_ep_t * cfiep)
{
	if (cfiep->bm_sg) {
		DWC_FREE(cfiep->bm_sg);
		cfiep->bm_sg = NULL;
	}

	if (cfiep->bm_align) {
		DWC_FREE(cfiep->bm_align);
		cfiep->bm_align = NULL;
	}

	if (cfiep->bm_concat) {
		if (NULL != cfiep->bm_concat->wTxBytes) {
			DWC_FREE(cfiep->bm_concat->wTxBytes);
			cfiep->bm_concat->wTxBytes = NULL;
		}
		DWC_FREE(cfiep->bm_concat);
		cfiep->bm_concat = NULL;
	}
}

/**
 * This function initializes the default values of the features
 * for a specific endpoint and should be called only once when
 * the EP is enabled first time.
 */
static int cfi_ep_init_defaults(struct dwc_otg_pcd *pcd, cfi_ep_t * cfiep)
{
	int retval = 0;

	cfiep->bm_sg = DWC_ALLOC(sizeof(ddma_sg_buffer_setup_t));
	if (NULL == cfiep->bm_sg) {
		CFI_INFO("Failed to allocate memory for SG feature value\n");
		return -DWC_E_NO_MEMORY;
	}
	dwc_memset(cfiep->bm_sg, 0, sizeof(ddma_sg_buffer_setup_t));

	/* For the Concatenation feature's default value we do not allocate
	 * memory for the wTxBytes field - it will be done in the set_feature_value
	 * request handler.
	 */
	cfiep->bm_concat = DWC_ALLOC(sizeof(ddma_concat_buffer_setup_t));
	if (NULL == cfiep->bm_concat) {
		CFI_INFO
		    ("Failed to allocate memory for CONCATENATION feature value\n");
		DWC_FREE(cfiep->bm_sg);
		return -DWC_E_NO_MEMORY;
	}
	dwc_memset(cfiep->bm_concat, 0, sizeof(ddma_concat_buffer_setup_t));

	cfiep->bm_align = DWC_ALLOC(sizeof(ddma_align_buffer_setup_t));
	if (NULL == cfiep->bm_align) {
		CFI_INFO
		    ("Failed to allocate memory for Alignment feature value\n");
		DWC_FREE(cfiep->bm_sg);
		DWC_FREE(cfiep->bm_concat);
		return -DWC_E_NO_MEMORY;
	}
	dwc_memset(cfiep->bm_align, 0, sizeof(ddma_align_buffer_setup_t));

	return retval;
}

/**
 * The callback function that notifies the CFI on the activation of
 * an endpoint in the PCD. The following steps are done in this function:
 *
 *	Create a dynamically allocated cfi_ep_t object (a CFI wrapper to the PCD's 
 *		active endpoint)
 *	Create MAX_DMA_DESCS_PER_EP count DMA Descriptors for the EP
 *	Set the Buffer Mode to standard
 *	Initialize the default values for all EP modes (SG, Circular, Concat, Align)
 *	Add the cfi_ep_t object to the list of active endpoints in the CFI object
 */
static int cfi_ep_enable(struct cfiobject *cfi, struct dwc_otg_pcd *pcd,
			 struct dwc_otg_pcd_ep *ep)
{
	cfi_ep_t *cfiep;
	int retval = -DWC_E_NOT_SUPPORTED;

	CFI_INFO("%s: epname=%s; epnum=0x%02x\n", __func__,
		 "EP_" /*ep->ep.name */ , ep->desc->bEndpointAddress);
	/* MAS - Check whether this endpoint already is in the list */
	cfiep = get_cfi_ep_by_pcd_ep(cfi, ep);

	if (NULL == cfiep) {
		/* Allocate a cfi_ep_t object */
		cfiep = DWC_ALLOC(sizeof(cfi_ep_t));
		if (NULL == cfiep) {
			CFI_INFO
			    ("Unable to allocate memory for <cfiep> in function %s\n",
			     __func__);
			return -DWC_E_NO_MEMORY;
		}
		dwc_memset(cfiep, 0, sizeof(cfi_ep_t));

		/* Save the dwc_otg_pcd_ep pointer in the cfiep object */
		cfiep->ep = ep;

		/* Allocate the DMA Descriptors chain of MAX_DMA_DESCS_PER_EP count */
		ep->dwc_ep.descs =
		    DWC_DMA_ALLOC(MAX_DMA_DESCS_PER_EP *
				  sizeof(dwc_otg_dma_desc_t),
				  &ep->dwc_ep.descs_dma_addr);

		if (NULL == ep->dwc_ep.descs) {
			DWC_FREE(cfiep);
			return -DWC_E_NO_MEMORY;
		}

		DWC_LIST_INIT(&cfiep->lh);

		/* Set the buffer mode to BM_STANDARD. It will be modified 
		 * when building descriptors for a specific buffer mode */
		ep->dwc_ep.buff_mode = BM_STANDARD;

		/* Create and initialize the default values for this EP's Buffer modes */
		if ((retval = cfi_ep_init_defaults(pcd, cfiep)) < 0)
			return retval;

		/* Add the cfi_ep_t object to the CFI object's list of active endpoints */
		DWC_LIST_INSERT_TAIL(&cfi->active_eps, &cfiep->lh);
		retval = 0;
	} else {		/* The sought EP already is in the list */
		CFI_INFO("%s: The sought EP already is in the list\n",
			 __func__);
	}

	return retval;
}

/**
 * This function is called when the data stage of a 3-stage Control Write request
 * is complete.
 * 
 */
static int cfi_ctrl_write_complete(struct cfiobject *cfi,
				   struct dwc_otg_pcd *pcd)
{
	uint32_t addr, reg_value;
	uint16_t wIndex, wValue;
	uint8_t bRequest;
	uint8_t *buf = cfi->buf_out.buf;
	//struct usb_ctrlrequest *ctrl_req = &cfi->ctrl_req_saved;
	struct cfi_usb_ctrlrequest *ctrl_req = &cfi->ctrl_req;
	int retval = -DWC_E_NOT_SUPPORTED;

	CFI_INFO("%s\n", __func__);

	bRequest = ctrl_req->bRequest;
	wIndex = DWC_CONSTANT_CPU_TO_LE16(ctrl_req->wIndex);
	wValue = DWC_CONSTANT_CPU_TO_LE16(ctrl_req->wValue);

	/* 
	 * Save the pointer to the data stage in the ctrl_req's <data> field.
	 * The request should be already saved in the command stage by now.
	 */
	ctrl_req->data = cfi->buf_out.buf;
	cfi->need_status_in_complete = 0;
	cfi->need_gadget_att = 0;

	switch (bRequest) {
	case VEN_CORE_WRITE_REGISTER:
		/* The buffer contains raw data of the new value for the register */
		reg_value = *((uint32_t *) buf);
		if (wValue == 0) {
			addr = 0;
			//addr = (uint32_t) pcd->otg_dev->os_dep.base;
			addr += wIndex;
		} else {
			addr = (wValue << 16) | wIndex;
		}

		//writel(reg_value, addr);

		retval = 0;
		cfi->need_status_in_complete = 1;
		break;

	case VEN_CORE_SET_FEATURE:
		/* The buffer contains raw data of the new value of the feature */
		retval = cfi_set_feature_value(pcd);
		if (retval < 0)
			return retval;

		cfi->need_status_in_complete = 1;
		break;

	default:
		break;
	}

	return retval;
}

/**
 * This function builds the DMA descriptors for the SG buffer mode.
 */
static void cfi_build_sg_descs(struct cfiobject *cfi, cfi_ep_t * cfiep,
			       dwc_otg_pcd_request_t * req)
{
	struct dwc_otg_pcd_ep *ep = cfiep->ep;
	ddma_sg_buffer_setup_t *sgval = cfiep->bm_sg;
	struct dwc_otg_dma_desc *desc = cfiep->ep->dwc_ep.descs;
	struct dwc_otg_dma_desc *desc_last = cfiep->ep->dwc_ep.descs;
	dma_addr_t buff_addr = req->dma;
	int i;
	uint32_t txsize, off;

	txsize = sgval->wSize;
	off = sgval->bOffset;

//      CFI_INFO("%s: %s TXSIZE=0x%08x; OFFSET=0x%08x\n", 
//              __func__, cfiep->ep->ep.name, txsize, off);

	for (i = 0; i < sgval->bCount; i++) {
		desc->status.b.bs = BS_HOST_BUSY;
		desc->buf = buff_addr;
		desc->status.b.l = 0;
		desc->status.b.ioc = 0;
		desc->status.b.sp = 0;
		desc->status.b.bytes = txsize;
		desc->status.b.bs = BS_HOST_READY;

		/* Set the next address of the buffer */
		buff_addr += txsize + off;
		desc_last = desc;
		desc++;
	}

	/* Set the last, ioc and sp bits on the Last DMA Descriptor */
	desc_last->status.b.l = 1;
	desc_last->status.b.ioc = 1;
	desc_last->status.b.sp = ep->dwc_ep.sent_zlp;
	/* Save the last DMA descriptor pointer */
	cfiep->dma_desc_last = desc_last;
	cfiep->desc_count = sgval->bCount;
}

/**
 * This function builds the DMA descriptors for the Concatenation buffer mode.
 */
static void cfi_build_concat_descs(struct cfiobject *cfi, cfi_ep_t * cfiep,
				   dwc_otg_pcd_request_t * req)
{
	struct dwc_otg_pcd_ep *ep = cfiep->ep;
	ddma_concat_buffer_setup_t *concatval = cfiep->bm_concat;
	struct dwc_otg_dma_desc *desc = cfiep->ep->dwc_ep.descs;
	struct dwc_otg_dma_desc *desc_last = cfiep->ep->dwc_ep.descs;
	dma_addr_t buff_addr = req->dma;
	int i;
	uint16_t *txsize;

	txsize = concatval->wTxBytes;

	for (i = 0; i < concatval->hdr.bDescCount; i++) {
		desc->buf = buff_addr;
		desc->status.b.bs = BS_HOST_BUSY;
		desc->status.b.l = 0;
		desc->status.b.ioc = 0;
		desc->status.b.sp = 0;
		desc->status.b.bytes = *txsize;
		desc->status.b.bs = BS_HOST_READY;

		txsize++;
		/* Set the next address of the buffer */
		buff_addr += UGETW(ep->desc->wMaxPacketSize);
		desc_last = desc;
		desc++;
	}

	/* Set the last, ioc and sp bits on the Last DMA Descriptor */
	desc_last->status.b.l = 1;
	desc_last->status.b.ioc = 1;
	desc_last->status.b.sp = ep->dwc_ep.sent_zlp;
	cfiep->dma_desc_last = desc_last;
	cfiep->desc_count = concatval->hdr.bDescCount;
}

/**
 * This function builds the DMA descriptors for the Circular buffer mode
 */
static void cfi_build_circ_descs(struct cfiobject *cfi, cfi_ep_t * cfiep,
				 dwc_otg_pcd_request_t * req)
{
	/* @todo: MAS - add implementation when this feature needs to be tested */
}

/**
 * This function builds the DMA descriptors for the Alignment buffer mode
 */
static void cfi_build_align_descs(struct cfiobject *cfi, cfi_ep_t * cfiep,
				  dwc_otg_pcd_request_t * req)
{
	struct dwc_otg_pcd_ep *ep = cfiep->ep;
	ddma_align_buffer_setup_t *alignval = cfiep->bm_align;
	struct dwc_otg_dma_desc *desc = cfiep->ep->dwc_ep.descs;
	dma_addr_t buff_addr = req->dma;

	desc->status.b.bs = BS_HOST_BUSY;
	desc->status.b.l = 1;
	desc->status.b.ioc = 1;
	desc->status.b.sp = ep->dwc_ep.sent_zlp;
	desc->status.b.bytes = req->length;
	/* Adjust the buffer alignment */
	desc->buf = (buff_addr + alignval->bAlign);
	desc->status.b.bs = BS_HOST_READY;
	cfiep->dma_desc_last = desc;
	cfiep->desc_count = 1;
}

/**
 * This function builds the DMA descriptors chain for different modes of the
 * buffer setup of an endpoint.
 */
static void cfi_build_descriptors(struct cfiobject *cfi,
				  struct dwc_otg_pcd *pcd,
				  struct dwc_otg_pcd_ep *ep,
				  dwc_otg_pcd_request_t * req)
{
	cfi_ep_t *cfiep;

	/* Get the cfiep by the dwc_otg_pcd_ep */
	cfiep = get_cfi_ep_by_pcd_ep(cfi, ep);
	if (NULL == cfiep) {
		CFI_INFO("%s: Unable to find a matching active endpoint\n",
			 __func__);
		return;
	}

	cfiep->xfer_len = req->length;

	/* Iterate through all the DMA descriptors */
	switch (cfiep->ep->dwc_ep.buff_mode) {
	case BM_SG:
		cfi_build_sg_descs(cfi, cfiep, req);
		break;

	case BM_CONCAT:
		cfi_build_concat_descs(cfi, cfiep, req);
		break;

	case BM_CIRCULAR:
		cfi_build_circ_descs(cfi, cfiep, req);
		break;

	case BM_ALIGN:
		cfi_build_align_descs(cfi, cfiep, req);
		break;

	default:
		break;
	}
}

/**
 * Allocate DMA buffer for different Buffer modes.
 */
static void *cfi_ep_alloc_buf(struct cfiobject *cfi, struct dwc_otg_pcd *pcd,
			      struct dwc_otg_pcd_ep *ep, dma_addr_t * dma,
			      unsigned size, gfp_t flags)
{
	return DWC_DMA_ALLOC(size, dma);
}

/**
 * This function initializes the CFI object.
 */
int init_cfi(cfiobject_t * cfiobj)
{
	CFI_INFO("%s\n", __func__);

	/* Allocate a buffer for IN XFERs */
	cfiobj->buf_in.buf =
	    DWC_DMA_ALLOC(CFI_IN_BUF_LEN, &cfiobj->buf_in.addr);
	if (NULL == cfiobj->buf_in.buf) {
		CFI_INFO("Unable to allocate buffer for INs\n");
		return -DWC_E_NO_MEMORY;
	}

	/* Allocate a buffer for OUT XFERs */
	cfiobj->buf_out.buf =
	    DWC_DMA_ALLOC(CFI_OUT_BUF_LEN, &cfiobj->buf_out.addr);
	if (NULL == cfiobj->buf_out.buf) {
		CFI_INFO("Unable to allocate buffer for OUT\n");
		return -DWC_E_NO_MEMORY;
	}

	/* Initialize the callback function pointers */
	cfiobj->ops.release = cfi_release;
	cfiobj->ops.ep_enable = cfi_ep_enable;
	cfiobj->ops.ctrl_write_complete = cfi_ctrl_write_complete;
	cfiobj->ops.build_descriptors = cfi_build_descriptors;
	cfiobj->ops.ep_alloc_buf = cfi_ep_alloc_buf;

	/* Initialize the list of active endpoints in the CFI object */
	DWC_LIST_INIT(&cfiobj->active_eps);

	return 0;
}

/**
 * This function reads the required feature's current value into the buffer
 *
 * @retval: Returns negative as error, or the data length of the feature  
 */
static int cfi_get_feature_value(uint8_t * buf, uint16_t buflen,
				 struct dwc_otg_pcd *pcd,
				 struct cfi_usb_ctrlrequest *ctrl_req)
{
	int retval = -DWC_E_NOT_SUPPORTED;
	struct dwc_otg_core_if *coreif = GET_CORE_IF(pcd);
	uint16_t dfifo, rxfifo, txfifo;

	switch (ctrl_req->wIndex) {
		/* Whether the DDMA is enabled or not */
	case FT_ID_DMA_MODE:
		*buf = (coreif->dma_enable && coreif->dma_desc_enable) ? 1 : 0;
		retval = 1;
		break;

	case FT_ID_DMA_BUFFER_SETUP:
		retval = cfi_ep_get_sg_val(buf, pcd, ctrl_req);
		break;

	case FT_ID_DMA_BUFF_ALIGN:
		retval = cfi_ep_get_align_val(buf, pcd, ctrl_req);
		break;

	case FT_ID_DMA_CONCAT_SETUP:
		retval = cfi_ep_get_concat_val(buf, pcd, ctrl_req);
		break;

	case FT_ID_DMA_CIRCULAR:
		CFI_INFO("GetFeature value (FT_ID_DMA_CIRCULAR)\n");
		break;

	case FT_ID_THRESHOLD_SETUP:
		CFI_INFO("GetFeature value (FT_ID_THRESHOLD_SETUP)\n");
		break;

	case FT_ID_DFIFO_DEPTH:
		dfifo = get_dfifo_size(coreif);
		*((uint16_t *) buf) = dfifo;
		retval = sizeof(uint16_t);
		break;

	case FT_ID_TX_FIFO_DEPTH:
		retval = get_txfifo_size(pcd, ctrl_req->wValue);
		if (retval >= 0) {
			txfifo = retval;
			*((uint16_t *) buf) = txfifo;
			retval = sizeof(uint16_t);
		}
		break;

	case FT_ID_RX_FIFO_DEPTH:
		retval = get_rxfifo_size(coreif, ctrl_req->wValue);
		if (retval >= 0) {
			rxfifo = retval;
			*((uint16_t *) buf) = rxfifo;
			retval = sizeof(uint16_t);
		}
		break;
	}

	return retval;
}

/**
 * This function resets the SG for the specified EP to its default value
 */
static int cfi_reset_sg_val(cfi_ep_t * cfiep)
{
	dwc_memset(cfiep->bm_sg, 0, sizeof(ddma_sg_buffer_setup_t));
	return 0;
}

/**
 * This function resets the Alignment for the specified EP to its default value
 */
static int cfi_reset_align_val(cfi_ep_t * cfiep)
{
	dwc_memset(cfiep->bm_sg, 0, sizeof(ddma_sg_buffer_setup_t));
	return 0;
}

/**
 * This function resets the Concatenation for the specified EP to its default value
 * This function will also set the value of the wTxBytes field to NULL after 
 * freeing the memory previously allocated for this field.
 */
static int cfi_reset_concat_val(cfi_ep_t * cfiep)
{
	/* First we need to free the wTxBytes field */
	if (cfiep->bm_concat->wTxBytes) {
		DWC_FREE(cfiep->bm_concat->wTxBytes);
		cfiep->bm_concat->wTxBytes = NULL;
	}

	dwc_memset(cfiep->bm_concat, 0, sizeof(ddma_concat_buffer_setup_t));
	return 0;
}

/**
 * This function resets all the buffer setups of the specified endpoint
 */
static int cfi_ep_reset_all_setup_vals(cfi_ep_t * cfiep)
{
	cfi_reset_sg_val(cfiep);
	cfi_reset_align_val(cfiep);
	cfi_reset_concat_val(cfiep);
	return 0;
}

static int cfi_handle_reset_fifo_val(struct dwc_otg_pcd *pcd, uint8_t ep_addr,
				     uint8_t rx_rst, uint8_t tx_rst)
{
	int retval = -DWC_E_INVALID;
	uint16_t tx_siz[15];
	uint16_t rx_siz = 0;
	dwc_otg_pcd_ep_t *ep = NULL;
	dwc_otg_core_if_t *core_if = GET_CORE_IF(pcd);
	dwc_otg_core_params_t *params = GET_CORE_IF(pcd)->core_params;

	if (rx_rst) {
		rx_siz = params->dev_rx_fifo_size;
		params->dev_rx_fifo_size = GET_CORE_IF(pcd)->init_rxfsiz;
	}

	if (tx_rst) {
		if (ep_addr == 0) {
			int i;

			for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
				tx_siz[i] =
				    core_if->core_params->dev_tx_fifo_size[i];
				core_if->core_params->dev_tx_fifo_size[i] =
				    core_if->init_txfsiz[i];
			}
		} else {

			ep = get_ep_by_addr(pcd, ep_addr);

			if (NULL == ep) {
				CFI_INFO
				    ("%s: Unable to get the endpoint addr=0x%02x\n",
				     __func__, ep_addr);
				return -DWC_E_INVALID;
			}

			tx_siz[0] =
			    params->dev_tx_fifo_size[ep->dwc_ep.tx_fifo_num -
						     1];
			params->dev_tx_fifo_size[ep->dwc_ep.tx_fifo_num - 1] =
			    GET_CORE_IF(pcd)->init_txfsiz[ep->
							  dwc_ep.tx_fifo_num -
							  1];
		}
	}

	if (resize_fifos(GET_CORE_IF(pcd))) {
		retval = 0;
	} else {
		CFI_INFO
		    ("%s: Error resetting the feature Reset All(FIFO size)\n",
		     __func__);
		if (rx_rst) {
			params->dev_rx_fifo_size = rx_siz;
		}

		if (tx_rst) {
			if (ep_addr == 0) {
				int i;
				for (i = 0; i < core_if->hwcfg4.b.num_in_eps;
				     i++) {
					core_if->
					    core_params->dev_tx_fifo_size[i] =
					    tx_siz[i];
				}
			} else {
				params->dev_tx_fifo_size[ep->
							 dwc_ep.tx_fifo_num -
							 1] = tx_siz[0];
			}
		}
		retval = -DWC_E_INVALID;
	}
	return retval;
}

static int cfi_handle_reset_all(struct dwc_otg_pcd *pcd, uint8_t addr)
{
	int retval = 0;
	cfi_ep_t *cfiep;
	cfiobject_t *cfi = pcd->cfi;
	dwc_list_link_t *tmp;

	retval = cfi_handle_reset_fifo_val(pcd, addr, 1, 1);
	if (retval < 0) {
		return retval;
	}

	/* If the EP address is known then reset the features for only that EP */
	if (addr) {
		cfiep = get_cfi_ep_by_addr(pcd->cfi, addr);
		if (NULL == cfiep) {
			CFI_INFO("%s: Error getting the EP address 0x%02x\n",
				 __func__, addr);
			return -DWC_E_INVALID;
		}
		retval = cfi_ep_reset_all_setup_vals(cfiep);
		cfiep->ep->dwc_ep.buff_mode = BM_STANDARD;
	}
	/* Otherwise (wValue == 0), reset all features of all EP's */
	else {
		/* Traverse all the active EP's and reset the feature(s) value(s) */
		//list_for_each_entry(cfiep, &cfi->active_eps, lh) {
		DWC_LIST_FOREACH(tmp, &cfi->active_eps) {
			cfiep = DWC_LIST_ENTRY(tmp, struct cfi_ep, lh);
			retval = cfi_ep_reset_all_setup_vals(cfiep);
			cfiep->ep->dwc_ep.buff_mode = BM_STANDARD;
			if (retval < 0) {
				CFI_INFO
				    ("%s: Error resetting the feature Reset All\n",
				     __func__);
				return retval;
			}
		}
	}
	return retval;
}

static int cfi_handle_reset_dma_buff_setup(struct dwc_otg_pcd *pcd,
					   uint8_t addr)
{
	int retval = 0;
	cfi_ep_t *cfiep;
	cfiobject_t *cfi = pcd->cfi;
	dwc_list_link_t *tmp;

	/* If the EP address is known then reset the features for only that EP */
	if (addr) {
		cfiep = get_cfi_ep_by_addr(pcd->cfi, addr);
		if (NULL == cfiep) {
			CFI_INFO("%s: Error getting the EP address 0x%02x\n",
				 __func__, addr);
			return -DWC_E_INVALID;
		}
		retval = cfi_reset_sg_val(cfiep);
	}
	/* Otherwise (wValue == 0), reset all features of all EP's */
	else {
		/* Traverse all the active EP's and reset the feature(s) value(s) */
		//list_for_each_entry(cfiep, &cfi->active_eps, lh) {
		DWC_LIST_FOREACH(tmp, &cfi->active_eps) {
			cfiep = DWC_LIST_ENTRY(tmp, struct cfi_ep, lh);
			retval = cfi_reset_sg_val(cfiep);
			if (retval < 0) {
				CFI_INFO
				    ("%s: Error resetting the feature Buffer Setup\n",
				     __func__);
				return retval;
			}
		}
	}
	return retval;
}

static int cfi_handle_reset_concat_val(struct dwc_otg_pcd *pcd, uint8_t addr)
{
	int retval = 0;
	cfi_ep_t *cfiep;
	cfiobject_t *cfi = pcd->cfi;
	dwc_list_link_t *tmp;

	/* If the EP address is known then reset the features for only that EP */
	if (addr) {
		cfiep = get_cfi_ep_by_addr(pcd->cfi, addr);
		if (NULL == cfiep) {
			CFI_INFO("%s: Error getting the EP address 0x%02x\n",
				 __func__, addr);
			return -DWC_E_INVALID;
		}
		retval = cfi_reset_concat_val(cfiep);
	}
	/* Otherwise (wValue == 0), reset all features of all EP's */
	else {
		/* Traverse all the active EP's and reset the feature(s) value(s) */
		//list_for_each_entry(cfiep, &cfi->active_eps, lh) {
		DWC_LIST_FOREACH(tmp, &cfi->active_eps) {
			cfiep = DWC_LIST_ENTRY(tmp, struct cfi_ep, lh);
			retval = cfi_reset_concat_val(cfiep);
			if (retval < 0) {
				CFI_INFO
				    ("%s: Error resetting the feature Concatenation Value\n",
				     __func__);
				return retval;
			}
		}
	}
	return retval;
}

static int cfi_handle_reset_align_val(struct dwc_otg_pcd *pcd, uint8_t addr)
{
	int retval = 0;
	cfi_ep_t *cfiep;
	cfiobject_t *cfi = pcd->cfi;
	dwc_list_link_t *tmp;

	/* If the EP address is known then reset the features for only that EP */
	if (addr) {
		cfiep = get_cfi_ep_by_addr(pcd->cfi, addr);
		if (NULL == cfiep) {
			CFI_INFO("%s: Error getting the EP address 0x%02x\n",
				 __func__, addr);
			return -DWC_E_INVALID;
		}
		retval = cfi_reset_align_val(cfiep);
	}
	/* Otherwise (wValue == 0), reset all features of all EP's */
	else {
		/* Traverse all the active EP's and reset the feature(s) value(s) */
		//list_for_each_entry(cfiep, &cfi->active_eps, lh) {
		DWC_LIST_FOREACH(tmp, &cfi->active_eps) {
			cfiep = DWC_LIST_ENTRY(tmp, struct cfi_ep, lh);
			retval = cfi_reset_align_val(cfiep);
			if (retval < 0) {
				CFI_INFO
				    ("%s: Error resetting the feature Aliignment Value\n",
				     __func__);
				return retval;
			}
		}
	}
	return retval;

}

static int cfi_preproc_reset(struct dwc_otg_pcd *pcd,
			     struct cfi_usb_ctrlrequest *req)
{
	int retval = 0;

	switch (req->wIndex) {
	case 0:
		/* Reset all features */
		retval = cfi_handle_reset_all(pcd, req->wValue & 0xff);
		break;

	case FT_ID_DMA_BUFFER_SETUP:
		/* Reset the SG buffer setup */
		retval =
		    cfi_handle_reset_dma_buff_setup(pcd, req->wValue & 0xff);
		break;

	case FT_ID_DMA_CONCAT_SETUP:
		/* Reset the Concatenation buffer setup */
		retval = cfi_handle_reset_concat_val(pcd, req->wValue & 0xff);
		break;

	case FT_ID_DMA_BUFF_ALIGN:
		/* Reset the Alignment buffer setup */
		retval = cfi_handle_reset_align_val(pcd, req->wValue & 0xff);
		break;

	case FT_ID_TX_FIFO_DEPTH:
		retval =
		    cfi_handle_reset_fifo_val(pcd, req->wValue & 0xff, 0, 1);
		pcd->cfi->need_gadget_att = 0;
		break;

	case FT_ID_RX_FIFO_DEPTH:
		retval = cfi_handle_reset_fifo_val(pcd, 0, 1, 0);
		pcd->cfi->need_gadget_att = 0;
		break;
	default:
		break;
	}
	return retval;
}

/**
 * This function sets a new value for the SG buffer setup.
 */
static int cfi_ep_set_sg_val(uint8_t * buf, struct dwc_otg_pcd *pcd)
{
	uint8_t inaddr, outaddr;
	cfi_ep_t *epin, *epout;
	ddma_sg_buffer_setup_t *psgval;
	uint32_t desccount, size;

	CFI_INFO("%s\n", __func__);

	psgval = (ddma_sg_buffer_setup_t *) buf;
	desccount = (uint32_t) psgval->bCount;
	size = (uint32_t) psgval->wSize;

	/* Check the DMA descriptor count */
	if ((desccount > MAX_DMA_DESCS_PER_EP) || (desccount == 0)) {
		CFI_INFO
		    ("%s: The count of DMA Descriptors should be between 1 and %d\n",
		     __func__, MAX_DMA_DESCS_PER_EP);
		return -DWC_E_INVALID;
	}

	/* Check the DMA descriptor count */

	if (size == 0) {

		CFI_INFO("%s: The transfer size should be at least 1 byte\n",
			 __func__);

		return -DWC_E_INVALID;

	}

	inaddr = psgval->bInEndpointAddress;
	outaddr = psgval->bOutEndpointAddress;

	epin = get_cfi_ep_by_addr(pcd->cfi, inaddr);
	epout = get_cfi_ep_by_addr(pcd->cfi, outaddr);

	if (NULL == epin || NULL == epout) {
		CFI_INFO
		    ("%s: Unable to get the endpoints inaddr=0x%02x outaddr=0x%02x\n",
		     __func__, inaddr, outaddr);
		return -DWC_E_INVALID;
	}

	epin->ep->dwc_ep.buff_mode = BM_SG;
	dwc_memcpy(epin->bm_sg, psgval, sizeof(ddma_sg_buffer_setup_t));

	epout->ep->dwc_ep.buff_mode = BM_SG;
	dwc_memcpy(epout->bm_sg, psgval, sizeof(ddma_sg_buffer_setup_t));

	return 0;
}

/**
 * This function sets a new value for the buffer Alignment setup.
 */
static int cfi_ep_set_alignment_val(uint8_t * buf, struct dwc_otg_pcd *pcd)
{
	cfi_ep_t *ep;
	uint8_t addr;
	ddma_align_buffer_setup_t *palignval;

	palignval = (ddma_align_buffer_setup_t *) buf;
	addr = palignval->bEndpointAddress;

	ep = get_cfi_ep_by_addr(pcd->cfi, addr);

	if (NULL == ep) {
		CFI_INFO("%s: Unable to get the endpoint addr=0x%02x\n",
			 __func__, addr);
		return -DWC_E_INVALID;
	}

	ep->ep->dwc_ep.buff_mode = BM_ALIGN;
	dwc_memcpy(ep->bm_align, palignval, sizeof(ddma_align_buffer_setup_t));

	return 0;
}

/**
 * This function sets a new value for the Concatenation buffer setup.
 */
static int cfi_ep_set_concat_val(uint8_t * buf, struct dwc_otg_pcd *pcd)
{
	uint8_t addr;
	cfi_ep_t *ep;
	struct _ddma_concat_buffer_setup_hdr *pConcatValHdr;
	uint16_t *pVals;
	uint32_t desccount;
	int i;
	uint16_t mps;

	pConcatValHdr = (struct _ddma_concat_buffer_setup_hdr *)buf;
	desccount = (uint32_t) pConcatValHdr->bDescCount;
	pVals = (uint16_t *) (buf + BS_CONCAT_VAL_HDR_LEN);

	/* Check the DMA descriptor count */
	if (desccount > MAX_DMA_DESCS_PER_EP) {
		CFI_INFO("%s: Maximum DMA Descriptor count should be %d\n",
			 __func__, MAX_DMA_DESCS_PER_EP);
		return -DWC_E_INVALID;
	}

	addr = pConcatValHdr->bEndpointAddress;
	ep = get_cfi_ep_by_addr(pcd->cfi, addr);
	if (NULL == ep) {
		CFI_INFO("%s: Unable to get the endpoint addr=0x%02x\n",
			 __func__, addr);
		return -DWC_E_INVALID;
	}

	mps = UGETW(ep->ep->desc->wMaxPacketSize);

#if 0
	for (i = 0; i < desccount; i++) {
		CFI_INFO("%s: wTxSize[%d]=0x%04x\n", __func__, i, pVals[i]);
	}
	CFI_INFO("%s: epname=%s; mps=%d\n", __func__, ep->ep->ep.name, mps);
#endif

	/* Check the wTxSizes to be less than or equal to the mps */
	for (i = 0; i < desccount; i++) {
		if (pVals[i] > mps) {
			CFI_INFO
			    ("%s: ERROR - the wTxSize[%d] should be <= MPS (wTxSize=%d)\n",
			     __func__, i, pVals[i]);
			return -DWC_E_INVALID;
		}
	}

	ep->ep->dwc_ep.buff_mode = BM_CONCAT;
	dwc_memcpy(ep->bm_concat, pConcatValHdr, BS_CONCAT_VAL_HDR_LEN);

	/* Free the previously allocated storage for the wTxBytes */
	if (ep->bm_concat->wTxBytes) {
		DWC_FREE(ep->bm_concat->wTxBytes);
	}

	/* Allocate a new storage for the wTxBytes field */
	ep->bm_concat->wTxBytes =
	    DWC_ALLOC(sizeof(uint16_t) * pConcatValHdr->bDescCount);
	if (NULL == ep->bm_concat->wTxBytes) {
		CFI_INFO("%s: Unable to allocate memory\n", __func__);
		return -DWC_E_NO_MEMORY;
	}

	/* Copy the new values into the wTxBytes filed */
	dwc_memcpy(ep->bm_concat->wTxBytes, buf + BS_CONCAT_VAL_HDR_LEN,
		   sizeof(uint16_t) * pConcatValHdr->bDescCount);

	return 0;
}

/**
 * This function calculates the total of all FIFO sizes
 * 
 * @param core_if Programming view of DWC_otg controller
 *
 * @return The total of data FIFO sizes.
 *
 */
static uint16_t get_dfifo_size(dwc_otg_core_if_t * core_if)
{
	dwc_otg_core_params_t *params = core_if->core_params;
	uint16_t dfifo_total = 0;
	int i;

	/* The shared RxFIFO size */
	dfifo_total =
	    params->dev_rx_fifo_size + params->dev_nperio_tx_fifo_size;

	/* Add up each TxFIFO size to the total */
	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
		dfifo_total += params->dev_tx_fifo_size[i];
	}

	return dfifo_total;
}

/**
 * This function returns Rx FIFO size
 * 
 * @param core_if Programming view of DWC_otg controller
 *
 * @return The total of data FIFO sizes.
 *
 */
static int32_t get_rxfifo_size(dwc_otg_core_if_t * core_if, uint16_t wValue)
{
	switch (wValue >> 8) {
	case 0:
		return (core_if->pwron_rxfsiz <
			32768) ? core_if->pwron_rxfsiz : 32768;
		break;
	case 1:
		return core_if->core_params->dev_rx_fifo_size;
		break;
	default:
		return -DWC_E_INVALID;
		break;
	}
}

/**
 * This function returns Tx FIFO size for IN EP
 * 
 * @param core_if Programming view of DWC_otg controller
 *
 * @return The total of data FIFO sizes.
 *
 */
static int32_t get_txfifo_size(struct dwc_otg_pcd *pcd, uint16_t wValue)
{
	dwc_otg_pcd_ep_t *ep;

	ep = get_ep_by_addr(pcd, wValue & 0xff);

	if (NULL == ep) {
		CFI_INFO("%s: Unable to get the endpoint addr=0x%02x\n",
			 __func__, wValue & 0xff);
		return -DWC_E_INVALID;
	}

	if (!ep->dwc_ep.is_in) {
		CFI_INFO
		    ("%s: No Tx FIFO assingned to the Out endpoint addr=0x%02x\n",
		     __func__, wValue & 0xff);
		return -DWC_E_INVALID;
	}

	switch (wValue >> 8) {
	case 0:
		return (GET_CORE_IF(pcd)->pwron_txfsiz
			[ep->dwc_ep.tx_fifo_num - 1] <
			768) ? GET_CORE_IF(pcd)->pwron_txfsiz[ep->
							      dwc_ep.tx_fifo_num
							      - 1] : 32768;
		break;
	case 1:
		return GET_CORE_IF(pcd)->core_params->
		    dev_tx_fifo_size[ep->dwc_ep.num - 1];
		break;
	default:
		return -DWC_E_INVALID;
		break;
	}
}

/**
 * This function checks if the submitted combination of 
 * device mode FIFO sizes is possible or not.
 * 
 * @param core_if Programming view of DWC_otg controller
 *
 * @return 1 if possible, 0 otherwise.
 *
 */
static uint8_t check_fifo_sizes(dwc_otg_core_if_t * core_if)
{
	uint16_t dfifo_actual = 0;
	dwc_otg_core_params_t *params = core_if->core_params;
	uint16_t start_addr = 0;
	int i;

	dfifo_actual =
	    params->dev_rx_fifo_size + params->dev_nperio_tx_fifo_size;

	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
		dfifo_actual += params->dev_tx_fifo_size[i];
	}

	if (dfifo_actual > core_if->total_fifo_size) {
		return 0;
	}

	if (params->dev_rx_fifo_size > 32768 || params->dev_rx_fifo_size < 16)
		return 0;

	if (params->dev_nperio_tx_fifo_size > 32768
	    || params->dev_nperio_tx_fifo_size < 16)
		return 0;

	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {

		if (params->dev_tx_fifo_size[i] > 768
		    || params->dev_tx_fifo_size[i] < 4)
			return 0;
	}

	if (params->dev_rx_fifo_size > core_if->pwron_rxfsiz)
		return 0;
	start_addr = params->dev_rx_fifo_size;

	if (params->dev_nperio_tx_fifo_size > core_if->pwron_gnptxfsiz)
		return 0;
	start_addr += params->dev_nperio_tx_fifo_size;

	for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {

		if (params->dev_tx_fifo_size[i] > core_if->pwron_txfsiz[i])
			return 0;
		start_addr += params->dev_tx_fifo_size[i];
	}

	return 1;
}

/**
 * This function resizes Device mode FIFOs
 * 
 * @param core_if Programming view of DWC_otg controller
 *
 * @return 1 if successful, 0 otherwise
 *
 */
static uint8_t resize_fifos(dwc_otg_core_if_t * core_if)
{
	int i = 0;
	dwc_otg_core_global_regs_t *global_regs = core_if->core_global_regs;
	dwc_otg_core_params_t *params = core_if->core_params;
	uint32_t rx_fifo_size;
	fifosize_data_t nptxfifosize;
	fifosize_data_t txfifosize[15];

	uint32_t rx_fsz_bak;
	uint32_t nptxfsz_bak;
	uint32_t txfsz_bak[15];

	uint16_t start_address;
	uint8_t retval = 1;

	if (!check_fifo_sizes(core_if)) {
		return 0;
	}

	/* Configure data FIFO sizes */
	if (core_if->hwcfg2.b.dynamic_fifo && params->enable_dynamic_fifo) {
		rx_fsz_bak = DWC_READ_REG32(&global_regs->grxfsiz);
		rx_fifo_size = params->dev_rx_fifo_size;
		DWC_WRITE_REG32(&global_regs->grxfsiz, rx_fifo_size);

		/*
		 * Tx FIFOs These FIFOs are numbered from 1 to 15.
		 * Indexes of the FIFO size module parameters in the
		 * dev_tx_fifo_size array and the FIFO size registers in
		 * the dtxfsiz array run from 0 to 14.
		 */

		/* Non-periodic Tx FIFO */
		nptxfsz_bak = DWC_READ_REG32(&global_regs->gnptxfsiz);
		nptxfifosize.b.depth = params->dev_nperio_tx_fifo_size;
		start_address = params->dev_rx_fifo_size;
		nptxfifosize.b.startaddr = start_address;

		DWC_WRITE_REG32(&global_regs->gnptxfsiz, nptxfifosize.d32);

		start_address += nptxfifosize.b.depth;

		for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
			txfsz_bak[i] = DWC_READ_REG32(&global_regs->dtxfsiz[i]);

			txfifosize[i].b.depth = params->dev_tx_fifo_size[i];
			txfifosize[i].b.startaddr = start_address;
			DWC_WRITE_REG32(&global_regs->dtxfsiz[i],
					txfifosize[i].d32);

			start_address += txfifosize[i].b.depth;
		}

		/** Check if register values are set correctly */
		if (rx_fifo_size != DWC_READ_REG32(&global_regs->grxfsiz)) {
			retval = 0;
		}

		if (nptxfifosize.d32 != DWC_READ_REG32(&global_regs->gnptxfsiz)) {
			retval = 0;
		}

		for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
			if (txfifosize[i].d32 !=
			    DWC_READ_REG32(&global_regs->dtxfsiz[i])) {
				retval = 0;
			}
		}

		/** If register values are not set correctly, reset old values */
		if (retval == 0) {
			DWC_WRITE_REG32(&global_regs->grxfsiz, rx_fsz_bak);

			/* Non-periodic Tx FIFO */
			DWC_WRITE_REG32(&global_regs->gnptxfsiz, nptxfsz_bak);

			for (i = 0; i < core_if->hwcfg4.b.num_in_eps; i++) {
				DWC_WRITE_REG32(&global_regs->dtxfsiz[i],
						txfsz_bak[i]);
			}
		}
	} else {
		return 0;
	}

	/* Flush the FIFOs */
	dwc_otg_flush_tx_fifo(core_if, 0x10);	/* all Tx FIFOs */
	dwc_otg_flush_rx_fifo(core_if);

	return retval;
}

/**
 * This function sets a new value for the buffer Alignment setup.
 */
static int cfi_ep_set_tx_fifo_val(uint8_t * buf, dwc_otg_pcd_t * pcd)
{
	int retval;
	uint32_t fsiz;
	uint16_t size;
	uint16_t ep_addr;
	dwc_otg_pcd_ep_t *ep;
	dwc_otg_core_params_t *params = GET_CORE_IF(pcd)->core_params;
	tx_fifo_size_setup_t *ptxfifoval;

	ptxfifoval = (tx_fifo_size_setup_t *) buf;
	ep_addr = ptxfifoval->bEndpointAddress;
	size = ptxfifoval->wDepth;

	ep = get_ep_by_addr(pcd, ep_addr);

	CFI_INFO
	    ("%s: Set Tx FIFO size: endpoint addr=0x%02x, depth=%d, FIFO Num=%d\n",
	     __func__, ep_addr, size, ep->dwc_ep.tx_fifo_num);

	if (NULL == ep) {
		CFI_INFO("%s: Unable to get the endpoint addr=0x%02x\n",
			 __func__, ep_addr);
		return -DWC_E_INVALID;
	}

	fsiz = params->dev_tx_fifo_size[ep->dwc_ep.tx_fifo_num - 1];
	params->dev_tx_fifo_size[ep->dwc_ep.tx_fifo_num - 1] = size;

	if (resize_fifos(GET_CORE_IF(pcd))) {
		retval = 0;
	} else {
		CFI_INFO
		    ("%s: Error setting the feature Tx FIFO Size for EP%d\n",
		     __func__, ep_addr);
		params->dev_tx_fifo_size[ep->dwc_ep.tx_fifo_num - 1] = fsiz;
		retval = -DWC_E_INVALID;
	}

	return retval;
}

/**
 * This function sets a new value for the buffer Alignment setup.
 */
static int cfi_set_rx_fifo_val(uint8_t * buf, dwc_otg_pcd_t * pcd)
{
	int retval;
	uint32_t fsiz;
	uint16_t size;
	dwc_otg_core_params_t *params = GET_CORE_IF(pcd)->core_params;
	rx_fifo_size_setup_t *prxfifoval;

	prxfifoval = (rx_fifo_size_setup_t *) buf;
	size = prxfifoval->wDepth;

	fsiz = params->dev_rx_fifo_size;
	params->dev_rx_fifo_size = size;

	if (resize_fifos(GET_CORE_IF(pcd))) {
		retval = 0;
	} else {
		CFI_INFO("%s: Error setting the feature Rx FIFO Size\n",
			 __func__);
		params->dev_rx_fifo_size = fsiz;
		retval = -DWC_E_INVALID;
	}

	return retval;
}

/**
 * This function reads the SG of an EP's buffer setup into the buffer buf
 */
static int cfi_ep_get_sg_val(uint8_t * buf, struct dwc_otg_pcd *pcd,
			     struct cfi_usb_ctrlrequest *req)
{
	int retval = -DWC_E_INVALID;
	uint8_t addr;
	cfi_ep_t *ep;

	/* The Low Byte of the wValue contains a non-zero address of the endpoint */
	addr = req->wValue & 0xFF;
	if (addr == 0)		/* The address should be non-zero */
		return retval;

	ep = get_cfi_ep_by_addr(pcd->cfi, addr);
	if (NULL == ep) {
		CFI_INFO("%s: Unable to get the endpoint address(0x%02x)\n",
			 __func__, addr);
		return retval;
	}

	dwc_memcpy(buf, ep->bm_sg, BS_SG_VAL_DESC_LEN);
	retval = BS_SG_VAL_DESC_LEN;
	return retval;
}

/**
 * This function reads the Concatenation value of an EP's buffer mode into 
 * the buffer buf
 */
static int cfi_ep_get_concat_val(uint8_t * buf, struct dwc_otg_pcd *pcd,
				 struct cfi_usb_ctrlrequest *req)
{
	int retval = -DWC_E_INVALID;
	uint8_t addr;
	cfi_ep_t *ep;
	uint8_t desc_count;

	/* The Low Byte of the wValue contains a non-zero address of the endpoint */
	addr = req->wValue & 0xFF;
	if (addr == 0)		/* The address should be non-zero */
		return retval;

	ep = get_cfi_ep_by_addr(pcd->cfi, addr);
	if (NULL == ep) {
		CFI_INFO("%s: Unable to get the endpoint address(0x%02x)\n",
			 __func__, addr);
		return retval;
	}

	/* Copy the header to the buffer */
	dwc_memcpy(buf, ep->bm_concat, BS_CONCAT_VAL_HDR_LEN);
	/* Advance the buffer pointer by the header size */
	buf += BS_CONCAT_VAL_HDR_LEN;

	desc_count = ep->bm_concat->hdr.bDescCount;
	/* Copy alll the wTxBytes to the buffer */
	dwc_memcpy(buf, ep->bm_concat->wTxBytes, sizeof(uid16_t) * desc_count);

	retval = BS_CONCAT_VAL_HDR_LEN + sizeof(uid16_t) * desc_count;
	return retval;
}

/**
 * This function reads the buffer Alignment value of an EP's buffer mode into 
 * the buffer buf
 *
 * @return The total number of bytes copied to the buffer or negative error code.
 */
static int cfi_ep_get_align_val(uint8_t * buf, struct dwc_otg_pcd *pcd,
				struct cfi_usb_ctrlrequest *req)
{
	int retval = -DWC_E_INVALID;
	uint8_t addr;
	cfi_ep_t *ep;

	/* The Low Byte of the wValue contains a non-zero address of the endpoint */
	addr = req->wValue & 0xFF;
	if (addr == 0)		/* The address should be non-zero */
		return retval;

	ep = get_cfi_ep_by_addr(pcd->cfi, addr);
	if (NULL == ep) {
		CFI_INFO("%s: Unable to get the endpoint address(0x%02x)\n",
			 __func__, addr);
		return retval;
	}

	dwc_memcpy(buf, ep->bm_align, BS_ALIGN_VAL_HDR_LEN);
	retval = BS_ALIGN_VAL_HDR_LEN;

	return retval;
}

/**
 * This function sets a new value for the specified feature
 * 
 * @param	pcd	A pointer to the PCD object
 * 
 * @return 0 if successful, negative error code otherwise to stall the DCE.
 */
static int cfi_set_feature_value(struct dwc_otg_pcd *pcd)
{
	int retval = -DWC_E_NOT_SUPPORTED;
	uint16_t wIndex, wValue;
	uint8_t bRequest;
	struct dwc_otg_core_if *coreif;
	cfiobject_t *cfi = pcd->cfi;
	struct cfi_usb_ctrlrequest *ctrl_req;
	uint8_t *buf;
	ctrl_req = &cfi->ctrl_req;

	buf = pcd->cfi->ctrl_req.data;

	coreif = GET_CORE_IF(pcd);
	bRequest = ctrl_req->bRequest;
	wIndex = DWC_CONSTANT_CPU_TO_LE16(ctrl_req->wIndex);
	wValue = DWC_CONSTANT_CPU_TO_LE16(ctrl_req->wValue);

	/* See which feature is to be modified */
	switch (wIndex) {
	case FT_ID_DMA_BUFFER_SETUP:
		/* Modify the feature */
		if ((retval = cfi_ep_set_sg_val(buf, pcd)) < 0)
			return retval;

		/* And send this request to the gadget */
		cfi->need_gadget_att = 1;
		break;

	case FT_ID_DMA_BUFF_ALIGN:
		if ((retval = cfi_ep_set_alignment_val(buf, pcd)) < 0)
			return retval;
		cfi->need_gadget_att = 1;
		break;

	case FT_ID_DMA_CONCAT_SETUP:
		/* Modify the feature */
		if ((retval = cfi_ep_set_concat_val(buf, pcd)) < 0)
			return retval;
		cfi->need_gadget_att = 1;
		break;

	case FT_ID_DMA_CIRCULAR:
		CFI_INFO("FT_ID_DMA_CIRCULAR\n");
		break;

	case FT_ID_THRESHOLD_SETUP:
		CFI_INFO("FT_ID_THRESHOLD_SETUP\n");
		break;

	case FT_ID_DFIFO_DEPTH:
		CFI_INFO("FT_ID_DFIFO_DEPTH\n");
		break;

	case FT_ID_TX_FIFO_DEPTH:
		CFI_INFO("FT_ID_TX_FIFO_DEPTH\n");
		if ((retval = cfi_ep_set_tx_fifo_val(buf, pcd)) < 0)
			return retval;
		cfi->need_gadget_att = 0;
		break;

	case FT_ID_RX_FIFO_DEPTH:
		CFI_INFO("FT_ID_RX_FIFO_DEPTH\n");
		if ((retval = cfi_set_rx_fifo_val(buf, pcd)) < 0)
			return retval;
		cfi->need_gadget_att = 0;
		break;
	}

	return retval;
}

#endif //DWC_UTE_CFI
