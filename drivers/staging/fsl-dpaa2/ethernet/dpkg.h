/* Copyright 2013-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * * Neither the name of the above-listed copyright holders nor the
 * names of any contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __FSL_DPKG_H_
#define __FSL_DPKG_H_

#include <linux/types.h>
#include "net.h"

/* Data Path Key Generator API
 * Contains initialization APIs and runtime APIs for the Key Generator
 */

/** Key Generator properties */

/**
 * Number of masks per key extraction
 */
#define DPKG_NUM_OF_MASKS		4
/**
 * Number of extractions per key profile
 */
#define DPKG_MAX_NUM_OF_EXTRACTS	10

/**
 * enum dpkg_extract_from_hdr_type - Selecting extraction by header types
 * @DPKG_FROM_HDR: Extract selected bytes from header, by offset
 * @DPKG_FROM_FIELD: Extract selected bytes from header, by offset from field
 * @DPKG_FULL_FIELD: Extract a full field
 */
enum dpkg_extract_from_hdr_type {
	DPKG_FROM_HDR = 0,
	DPKG_FROM_FIELD = 1,
	DPKG_FULL_FIELD = 2
};

/**
 * enum dpkg_extract_type - Enumeration for selecting extraction type
 * @DPKG_EXTRACT_FROM_HDR: Extract from the header
 * @DPKG_EXTRACT_FROM_DATA: Extract from data not in specific header
 * @DPKG_EXTRACT_FROM_PARSE: Extract from parser-result;
 *	e.g. can be used to extract header existence;
 *	please refer to 'Parse Result definition' section in the parser BG
 */
enum dpkg_extract_type {
	DPKG_EXTRACT_FROM_HDR = 0,
	DPKG_EXTRACT_FROM_DATA = 1,
	DPKG_EXTRACT_FROM_PARSE = 3
};

/**
 * struct dpkg_mask - A structure for defining a single extraction mask
 * @mask: Byte mask for the extracted content
 * @offset: Offset within the extracted content
 */
struct dpkg_mask {
	u8 mask;
	u8 offset;
};

/**
 * struct dpkg_extract - A structure for defining a single extraction
 * @type: Determines how the union below is interpreted:
 *		DPKG_EXTRACT_FROM_HDR: selects 'from_hdr';
 *		DPKG_EXTRACT_FROM_DATA: selects 'from_data';
 *		DPKG_EXTRACT_FROM_PARSE: selects 'from_parse'
 * @extract: Selects extraction method
 * @num_of_byte_masks: Defines the number of valid entries in the array below;
 *		This is	also the number of bytes to be used as masks
 * @masks: Masks parameters
 */
struct dpkg_extract {
	enum dpkg_extract_type type;
	/**
	 * union extract - Selects extraction method
	 * @from_hdr - Used when 'type = DPKG_EXTRACT_FROM_HDR'
	 * @from_data - Used when 'type = DPKG_EXTRACT_FROM_DATA'
	 * @from_parse - Used when 'type = DPKG_EXTRACT_FROM_PARSE'
	 */
	union {
		/**
		 * struct from_hdr - Used when 'type = DPKG_EXTRACT_FROM_HDR'
		 * @prot: Any of the supported headers
		 * @type: Defines the type of header extraction:
		 *	DPKG_FROM_HDR: use size & offset below;
		 *	DPKG_FROM_FIELD: use field, size and offset below;
		 *	DPKG_FULL_FIELD: use field below
		 * @field: One of the supported fields (NH_FLD_)
		 *
		 * @size: Size in bytes
		 * @offset: Byte offset
		 * @hdr_index: Clear for cases not listed below;
		 *	Used for protocols that may have more than a single
		 *	header, 0 indicates an outer header;
		 *	Supported protocols (possible values):
		 *	NET_PROT_VLAN (0, HDR_INDEX_LAST);
		 *	NET_PROT_MPLS (0, 1, HDR_INDEX_LAST);
		 *	NET_PROT_IP(0, HDR_INDEX_LAST);
		 *	NET_PROT_IPv4(0, HDR_INDEX_LAST);
		 *	NET_PROT_IPv6(0, HDR_INDEX_LAST);
		 */

		struct {
			enum net_prot			prot;
			enum dpkg_extract_from_hdr_type type;
			u32			field;
			u8			size;
			u8			offset;
			u8			hdr_index;
		} from_hdr;
		/**
		 * struct from_data - Used when 'type = DPKG_EXTRACT_FROM_DATA'
		 * @size: Size in bytes
		 * @offset: Byte offset
		 */
		struct {
			u8 size;
			u8 offset;
		} from_data;

		/**
		 * struct from_parse - Used when
		 *		       'type = DPKG_EXTRACT_FROM_PARSE'
		 * @size: Size in bytes
		 * @offset: Byte offset
		 */
		struct {
			u8 size;
			u8 offset;
		} from_parse;
	} extract;

	u8		num_of_byte_masks;
	struct dpkg_mask	masks[DPKG_NUM_OF_MASKS];
};

/**
 * struct dpkg_profile_cfg - A structure for defining a full Key Generation
 *				profile (rule)
 * @num_extracts: Defines the number of valid entries in the array below
 * @extracts: Array of required extractions
 */
struct dpkg_profile_cfg {
	u8 num_extracts;
	struct dpkg_extract extracts[DPKG_MAX_NUM_OF_EXTRACTS];
};

#endif /* __FSL_DPKG_H_ */
