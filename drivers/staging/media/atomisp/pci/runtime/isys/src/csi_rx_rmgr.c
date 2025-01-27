// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#include "system_global.h"


#include "assert_support.h"
#include "platform_support.h"
#include "ia_css_isys.h"
#include "bitop_support.h"
#include "ia_css_pipeline.h"	/* ia_css_pipeline_get_pipe_io_status() */
#include "sh_css_internal.h"	/* sh_css_sp_pipeline_io_status
				 * SH_CSS_MAX_SP_THREADS
				 */
#include "csi_rx_rmgr.h"

static isys_csi_rx_rsrc_t  isys_csi_rx_rsrc[N_CSI_RX_BACKEND_ID];

void ia_css_isys_csi_rx_lut_rmgr_init(void)
{
	memset(isys_csi_rx_rsrc, 0, sizeof(isys_csi_rx_rsrc));
}

void ia_css_isys_csi_rx_lut_rmgr_uninit(void)
{
	memset(isys_csi_rx_rsrc, 0, sizeof(isys_csi_rx_rsrc));
}

bool ia_css_isys_csi_rx_lut_rmgr_acquire(
    csi_rx_backend_ID_t		backend,
    csi_mipi_packet_type_t		packet_type,
    csi_rx_backend_lut_entry_t	*entry)
{
	bool retval = false;
	u32 max_num_packets_of_type;
	u32 num_active_of_type;
	isys_csi_rx_rsrc_t *cur_rsrc = NULL;
	u16 i;

	assert(backend < N_CSI_RX_BACKEND_ID);
	assert((packet_type == CSI_MIPI_PACKET_TYPE_LONG) ||
	       (packet_type == CSI_MIPI_PACKET_TYPE_SHORT));
	assert(entry);

	if ((backend < N_CSI_RX_BACKEND_ID) && (entry)) {
		cur_rsrc = &isys_csi_rx_rsrc[backend];
		if (packet_type == CSI_MIPI_PACKET_TYPE_LONG) {
			max_num_packets_of_type = N_LONG_PACKET_LUT_ENTRIES[backend];
			num_active_of_type = cur_rsrc->num_long_packets;
		} else {
			max_num_packets_of_type = N_SHORT_PACKET_LUT_ENTRIES[backend];
			num_active_of_type = cur_rsrc->num_short_packets;
		}

		if (num_active_of_type < max_num_packets_of_type) {
			for (i = 0; i < max_num_packets_of_type; i++) {
				if (bitop_getbit(cur_rsrc->active_table, i) == 0) {
					bitop_setbit(cur_rsrc->active_table, i);

					if (packet_type == CSI_MIPI_PACKET_TYPE_LONG) {
						entry->long_packet_entry = i;
						entry->short_packet_entry = 0;
						cur_rsrc->num_long_packets++;
					} else {
						entry->long_packet_entry = 0;
						entry->short_packet_entry = i;
						cur_rsrc->num_short_packets++;
					}
					cur_rsrc->num_active++;
					retval = true;
					break;
				}
			}
		}
	}
	return retval;
}

void ia_css_isys_csi_rx_lut_rmgr_release(
    csi_rx_backend_ID_t		backend,
    csi_mipi_packet_type_t		packet_type,
    csi_rx_backend_lut_entry_t	*entry)
{
	u32 max_num_packets;
	isys_csi_rx_rsrc_t *cur_rsrc = NULL;
	u32 packet_entry = 0;

	assert(backend < N_CSI_RX_BACKEND_ID);
	assert(entry);
	assert((packet_type >= CSI_MIPI_PACKET_TYPE_LONG) ||
	       (packet_type <= CSI_MIPI_PACKET_TYPE_SHORT));

	if ((backend < N_CSI_RX_BACKEND_ID) && (entry)) {
		if (packet_type == CSI_MIPI_PACKET_TYPE_LONG) {
			max_num_packets = N_LONG_PACKET_LUT_ENTRIES[backend];
			packet_entry = entry->long_packet_entry;
		} else {
			max_num_packets = N_SHORT_PACKET_LUT_ENTRIES[backend];
			packet_entry = entry->short_packet_entry;
		}

		cur_rsrc = &isys_csi_rx_rsrc[backend];
		if ((packet_entry < max_num_packets) && (cur_rsrc->num_active > 0)) {
			if (bitop_getbit(cur_rsrc->active_table, packet_entry) == 1) {
				bitop_clearbit(cur_rsrc->active_table, packet_entry);

				if (packet_type == CSI_MIPI_PACKET_TYPE_LONG)
					cur_rsrc->num_long_packets--;
				else
					cur_rsrc->num_short_packets--;
				cur_rsrc->num_active--;
			}
		}
	}
}

int ia_css_isys_csi_rx_register_stream(
    enum mipi_port_id port,
    uint32_t isys_stream_id)
{
	int retval = -EINVAL;

	if ((port < N_INPUT_SYSTEM_CSI_PORT) &&
	    (isys_stream_id < SH_CSS_MAX_ISYS_CHANNEL_NODES)) {
		struct sh_css_sp_pipeline_io_status *pipe_io_status;

		pipe_io_status = ia_css_pipeline_get_pipe_io_status();
		if (bitop_getbit(pipe_io_status->active[port], isys_stream_id) == 0) {
			bitop_setbit(pipe_io_status->active[port], isys_stream_id);
			pipe_io_status->running[port] = 0;
			retval = 0;
		}
	}
	return retval;
}

int ia_css_isys_csi_rx_unregister_stream(
    enum mipi_port_id port,
    uint32_t isys_stream_id)
{
	int retval = -EINVAL;

	if ((port < N_INPUT_SYSTEM_CSI_PORT) &&
	    (isys_stream_id < SH_CSS_MAX_ISYS_CHANNEL_NODES)) {
		struct sh_css_sp_pipeline_io_status *pipe_io_status;

		pipe_io_status = ia_css_pipeline_get_pipe_io_status();
		if (bitop_getbit(pipe_io_status->active[port], isys_stream_id) == 1) {
			bitop_clearbit(pipe_io_status->active[port], isys_stream_id);
			retval = 0;
		}
	}
	return retval;
}
