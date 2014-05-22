/* =========================================================================
 * $File: //dwh/usb_iip/dev/software/dwc_common_port_2/dwc_cc.h $
 * $Revision: #4 $
 * $Date: 2010/09/28 $
 * $Change: 1596182 $
 *
 * Synopsys Portability Library Software and documentation
 * (hereinafter, "Software") is an Unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing
 * between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product
 * under any End User Software License Agreement or Agreement for
 * Licensed Product with Synopsys or any supplement thereto. You are
 * permitted to use and redistribute this Software in source and binary
 * forms, with or without modification, provided that redistributions
 * of source code must retain this notice. You may not view, use,
 * disclose, copy or distribute this file or any information contained
 * herein except pursuant to this license grant from Synopsys. If you
 * do not agree with this notice, including the disclaimer below, then
 * you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 * BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL
 * SYNOPSYS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================= */
#ifndef _DWC_CC_H_
#define _DWC_CC_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @file
 *
 * This file defines the Context Context library.
 *
 * The main data structure is dwc_cc_if_t which is returned by either the
 * dwc_cc_if_alloc function or returned by the module to the user via a provided
 * function. The data structure is opaque and should only be manipulated via the
 * functions provied in this API.
 *
 * It manages a list of connection contexts and operations can be performed to
 * add, remove, query, search, and change, those contexts.  Additionally,
 * a dwc_notifier_t object can be requested from the manager so that
 * the user can be notified whenever the context list has changed.
 */

#include "dwc_os.h"
#include "dwc_list.h"
#include "dwc_notifier.h"


/* Notifications */
#define DWC_CC_LIST_CHANGED_NOTIFICATION "DWC_CC_LIST_CHANGED_NOTIFICATION"

struct dwc_cc_if;
typedef struct dwc_cc_if dwc_cc_if_t;


/** @name Connection Context Operations */
/** @{ */

/** This function allocates memory for a dwc_cc_if_t structure, initializes
 * fields to default values, and returns a pointer to the structure or NULL on
 * error. */
extern dwc_cc_if_t *dwc_cc_if_alloc(void *mem_ctx, void *mtx_ctx,
				    dwc_notifier_t *notifier, unsigned is_host);

/** Frees the memory for the specified CC structure allocated from
 * dwc_cc_if_alloc(). */
extern void dwc_cc_if_free(void *mem_ctx, void *mtx_ctx, dwc_cc_if_t *cc_if);

/** Removes all contexts from the connection context list */
extern void dwc_cc_clear(void *mem_ctx, dwc_cc_if_t *cc_if);

/** Adds a connection context (CHID, CK, CDID, Name) to the connection context list.
 * If a CHID already exists, the CK and name are overwritten.  Statistics are
 * not overwritten.
 *
 * @param cc_if The cc_if structure.
 * @param chid A pointer to the 16-byte CHID.  This value will be copied.
 * @param ck A pointer to the 16-byte CK.  This value will be copied.
 * @param cdid A pointer to the 16-byte CDID.  This value will be copied.
 * @param name An optional host friendly name as defined in the association model
 * spec.  Must be a UTF16-LE unicode string.  Can be NULL to indicated no name.
 * @param length The length othe unicode string.
 * @return A unique identifier used to refer to this context that is valid for
 * as long as this context is still in the list. */
extern int32_t dwc_cc_add(void *mem_ctx, dwc_cc_if_t *cc_if, uint8_t *chid,
			  uint8_t *cdid, uint8_t *ck, uint8_t *name,
			  uint8_t length);

/** Changes the CHID, CK, CDID, or Name values of a connection context in the
 * list, preserving any accumulated statistics.  This would typically be called
 * if the host decideds to change the context with a SET_CONNECTION request.
 *
 * @param cc_if The cc_if structure.
 * @param id The identifier of the connection context.
 * @param chid A pointer to the 16-byte CHID.  This value will be copied.  NULL
 * indicates no change.
 * @param cdid A pointer to the 16-byte CDID.  This value will be copied.  NULL
 * indicates no change.
 * @param ck A pointer to the 16-byte CK.  This value will be copied.  NULL
 * indicates no change.
 * @param name Host friendly name UTF16-LE.  NULL indicates no change.
 * @param length Length of name. */
extern void dwc_cc_change(void *mem_ctx, dwc_cc_if_t *cc_if, int32_t id,
			  uint8_t *chid, uint8_t *cdid, uint8_t *ck,
			  uint8_t *name, uint8_t length);

/** Remove the specified connection context.
 * @param cc_if The cc_if structure.
 * @param id The identifier of the connection context to remove. */
extern void dwc_cc_remove(void *mem_ctx, dwc_cc_if_t *cc_if, int32_t id);

/** Get a binary block of data for the connection context list and attributes.
 * This data can be used by the OS specific driver to save the connection
 * context list into non-volatile memory.
 *
 * @param cc_if The cc_if structure.
 * @param length Return the length of the data buffer.
 * @return A pointer to the data buffer.  The memory for this buffer should be
 * freed with DWC_FREE() after use. */
extern uint8_t *dwc_cc_data_for_save(void *mem_ctx, dwc_cc_if_t *cc_if,
				     unsigned int *length);

/** Restore the connection context list from the binary data that was previously
 * returned from a call to dwc_cc_data_for_save.  This can be used by the OS specific
 * driver to load a connection context list from non-volatile memory.
 *
 * @param cc_if The cc_if structure.
 * @param data The data bytes as returned from dwc_cc_data_for_save.
 * @param length The length of the data. */
extern void dwc_cc_restore_from_data(void *mem_ctx, dwc_cc_if_t *cc_if,
				     uint8_t *data, unsigned int length);

/** Find the connection context from the specified CHID.
 *
 * @param cc_if The cc_if structure.
 * @param chid A pointer to the CHID data.
 * @return A non-zero identifier of the connection context if the CHID matches.
 * Otherwise returns 0. */
extern uint32_t dwc_cc_match_chid(dwc_cc_if_t *cc_if, uint8_t *chid);

/** Find the connection context from the specified CDID.
 *
 * @param cc_if The cc_if structure.
 * @param cdid A pointer to the CDID data.
 * @return A non-zero identifier of the connection context if the CHID matches.
 * Otherwise returns 0. */
extern uint32_t dwc_cc_match_cdid(dwc_cc_if_t *cc_if, uint8_t *cdid);

/** Retrieve the CK from the specified connection context.
 *
 * @param cc_if The cc_if structure.
 * @param id The identifier of the connection context.
 * @return A pointer to the CK data.  The memory does not need to be freed. */
extern uint8_t *dwc_cc_ck(dwc_cc_if_t *cc_if, int32_t id);

/** Retrieve the CHID from the specified connection context.
 *
 * @param cc_if The cc_if structure.
 * @param id The identifier of the connection context.
 * @return A pointer to the CHID data.  The memory does not need to be freed. */
extern uint8_t *dwc_cc_chid(dwc_cc_if_t *cc_if, int32_t id);

/** Retrieve the CDID from the specified connection context.
 *
 * @param cc_if The cc_if structure.
 * @param id The identifier of the connection context.
 * @return A pointer to the CDID data.  The memory does not need to be freed. */
extern uint8_t *dwc_cc_cdid(dwc_cc_if_t *cc_if, int32_t id);

extern uint8_t *dwc_cc_name(dwc_cc_if_t *cc_if, int32_t id, uint8_t *length);

/** Checks a buffer for non-zero.
 * @param id A pointer to a 16 byte buffer.
 * @return true if the 16 byte value is non-zero. */
static inline unsigned dwc_assoc_is_not_zero_id(uint8_t *id)
{
	int i;
	for (i = 0; i < 16; i++) {
		if (id[i])
			return 1;
	}
	return 0;
}

/** Checks a buffer for zero.
 * @param id A pointer to a 16 byte buffer.
 * @return true if the 16 byte value is zero. */
static inline unsigned dwc_assoc_is_zero_id(uint8_t *id)
{
	return !dwc_assoc_is_not_zero_id(id);
}

/** Prints an ASCII representation for the 16-byte chid, cdid, or ck, into
 * buffer. */
static inline int dwc_print_id_string(char *buffer, uint8_t *id)
{
	char *ptr = buffer;
	int i;
	for (i = 0; i < 16; i++) {
		ptr += DWC_SPRINTF(ptr, "%02x", id[i]);
		if (i < 15)
			ptr += DWC_SPRINTF(ptr, " ");
	}
	return ptr - buffer;
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _DWC_CC_H_ */

