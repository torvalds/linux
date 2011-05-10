/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SCIC_SDS_STP_REQUEST_T_
#define _SCIC_SDS_STP_REQUEST_T_

#include <linux/dma-mapping.h>
#include <scsi/sas.h>

struct scic_sds_stp_request {
	union {
		u32 ncq;

		u32 udma;

		struct scic_sds_stp_pio_request {
			/**
			 * Total transfer for the entire PIO request recorded at request constuction
			 * time.
			 *
			 * @todo Should we just decrement this value for each byte of data transitted
			 *       or received to elemenate the current_transfer_bytes field?
			 */
			u32 total_transfer_bytes;

			/**
			 * Total number of bytes received/transmitted in data frames since the start
			 * of the IO request.  At the end of the IO request this should equal the
			 * total_transfer_bytes.
			 */
			u32 current_transfer_bytes;

			/**
			 * The number of bytes requested in the in the PIO setup.
			 */
			u32 pio_transfer_bytes;

			/**
			 * PIO Setup ending status value to tell us if we need to wait for another FIS
			 * or if the transfer is complete. On the receipt of a D2H FIS this will be
			 * the status field of that FIS.
			 */
			u8 ending_status;

			/**
			 * On receipt of a D2H FIS this will be the ending error field if the
			 * ending_status has the SATA_STATUS_ERR bit set.
			 */
			u8 ending_error;

			struct scic_sds_request_pio_sgl {
				struct scu_sgl_element_pair *sgl_pair;
				u8 sgl_set;
				u32 sgl_offset;
			} request_current;
		} pio;

		struct {
			/**
			 * The number of bytes requested in the PIO setup before CDB data frame.
			 */
			u32 device_preferred_cdb_length;
		} packet;
	} type;
};

/**
 * enum scic_sds_stp_request_started_udma_substates - This enumeration depicts
 *    the various sub-states associated with a SATA/STP UDMA protocol operation.
 *
 *
 */
enum scic_sds_stp_request_started_udma_substates {
	SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE,
	SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE,
};

/**
 * enum scic_sds_stp_request_started_non_data_substates - This enumeration
 *    depicts the various sub-states associated with a SATA/STP non-data
 *    protocol operation.
 *
 *
 */
enum scic_sds_stp_request_started_non_data_substates {
	SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE,
	SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE,
};

/**
 * enum scic_sds_stp_request_started_soft_reset_substates - THis enumeration
 *    depicts the various sub-states associated with a SATA/STP soft reset
 *    operation.
 *
 *
 */
enum scic_sds_stp_request_started_soft_reset_substates {
	SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE,
	SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE,
	SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE,
};

/* This is the enumeration of the SATA PIO DATA IN started substate machine. */
enum _scic_sds_stp_request_started_pio_substates {
	/**
	 * While in this state the IO request object is waiting for the TC completion
	 * notification for the H2D Register FIS
	 */
	SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE,

	/**
	 * While in this state the IO request object is waiting for either a PIO Setup
	 * FIS or a D2H register FIS.  The type of frame received is based on the
	 * result of the prior frame and line conditions.
	 */
	SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE,

	/**
	 * While in this state the IO request object is waiting for a DATA frame from
	 * the device.
	 */
	SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE,

	/**
	 * While in this state the IO request object is waiting to transmit the next data
	 * frame to the device.
	 */
	SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE,
};

struct scic_sds_request;

enum sci_status scic_sds_stp_pio_request_construct(struct scic_sds_request *sci_req,
						   bool copy_rx_frame);
enum sci_status scic_sds_stp_udma_request_construct(struct scic_sds_request *sci_req,
						    u32 transfer_length,
						    enum dma_data_direction dir);
enum sci_status scic_sds_stp_non_data_request_construct(struct scic_sds_request *sci_req);
enum sci_status scic_sds_stp_soft_reset_request_construct(struct scic_sds_request *sci_req);
enum sci_status scic_sds_stp_ncq_request_construct(struct scic_sds_request *sci_req,
						   u32 transfer_length,
						   enum dma_data_direction dir);
#endif /* _SCIC_SDS_STP_REQUEST_T_ */
