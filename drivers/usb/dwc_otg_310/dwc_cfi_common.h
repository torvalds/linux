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

#if !defined(__DWC_CFI_COMMON_H__)
#define __DWC_CFI_COMMON_H__

//#include <linux/types.h>

/**
 * @file 
 *
 * This file contains the CFI specific common constants, interfaces
 * (functions and macros) and structures for Linux. No PCD specific
 * data structure or definition is to be included in this file.
 *
 */

/** This is a request for all Core Features */
#define VEN_CORE_GET_FEATURES		0xB1

/** This is a request to get the value of a specific Core Feature */
#define VEN_CORE_GET_FEATURE		0xB2

/** This command allows the host to set the value of a specific Core Feature */
#define VEN_CORE_SET_FEATURE		0xB3

/** This command allows the host to set the default values of 
 * either all or any specific Core Feature 
 */
#define VEN_CORE_RESET_FEATURES		0xB4

/** This command forces the PCD to write the deferred values of a Core Features */
#define VEN_CORE_ACTIVATE_FEATURES	0xB5

/** This request reads a DWORD value from a register at the specified offset */
#define VEN_CORE_READ_REGISTER		0xB6

/** This request writes a DWORD value into a register at the specified offset */
#define VEN_CORE_WRITE_REGISTER		0xB7

/** This structure is the header of the Core Features dataset returned to 
 *  the Host
 */
struct cfi_all_features_header {
/** The features header structure length is */
#define CFI_ALL_FEATURES_HDR_LEN		8
	/**
	 * The total length of the features dataset returned to the Host 
	 */
	uint16_t wTotalLen;

	/**
	 * CFI version number inBinary-Coded Decimal (i.e., 1.00 is 100H).
	 * This field identifies the version of the CFI Specification with which 
	 * the device is compliant.
	 */
	uint16_t wVersion;

	/** The ID of the Core */
	uint16_t wCoreID;
#define CFI_CORE_ID_UDC		1
#define CFI_CORE_ID_OTG		2
#define CFI_CORE_ID_WUDEV	3

	/** Number of features returned by VEN_CORE_GET_FEATURES request */
	uint16_t wNumFeatures;
} UPACKED;

typedef struct cfi_all_features_header cfi_all_features_header_t;

/** This structure is a header of the Core Feature descriptor dataset returned to 
 *  the Host after the VEN_CORE_GET_FEATURES request
 */
struct cfi_feature_desc_header {
#define CFI_FEATURE_DESC_HDR_LEN	8

	/** The feature ID */
	uint16_t wFeatureID;

	/** Length of this feature descriptor in bytes - including the
	 * length of the feature name string
	 */
	uint16_t wLength;

	/** The data length of this feature in bytes */
	uint16_t wDataLength;

	/** 
	 * Attributes of this features 
	 * D0: Access rights
	 * 0 - Read/Write
	 * 1 - Read only
	 */
	uint8_t bmAttributes;
#define CFI_FEATURE_ATTR_RO		1
#define CFI_FEATURE_ATTR_RW		0

	/** Length of the feature name in bytes */
	uint8_t bNameLen;

	/** The feature name buffer */
	//uint8_t *name;
} UPACKED;

typedef struct cfi_feature_desc_header cfi_feature_desc_header_t;

/**
 * This structure describes a NULL terminated string referenced by its id field.
 * It is very similar to usb_string structure but has the id field type set to 16-bit.
 */
struct cfi_string {
	uint16_t id;
	const uint8_t *s;
};
typedef struct cfi_string cfi_string_t;

#endif
