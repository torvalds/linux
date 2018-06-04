/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "mod_info_packet.h"
#include "core_types.h"

static void mod_build_vsc_infopacket(const struct dc_stream_state *stream,
		struct dc_info_packet *info_packet)
{
	unsigned int vscPacketRevision = 0;
	unsigned int i;

	if (stream->timing.timing_3d_format != TIMING_3D_FORMAT_NONE && stream->view_format != VIEW_3D_FORMAT_NONE)
		vscPacketRevision = 1;


	/*VSC packet set to 2 when DP revision >= 1.2*/
	if (stream->psr_version != 0)
		vscPacketRevision = 2;


	/* VSC packet not needed based on the features
	 * supported by this DP display
	 */
	if (vscPacketRevision == 0)
		return;

	if (vscPacketRevision == 0x2) {
		/* Secondary-data Packet ID = 0*/
		info_packet->hb0 = 0x00;
		/* 07h - Packet Type Value indicating Video
		 * Stream Configuration packet
		 */
		info_packet->hb1 = 0x07;
		/* 02h = VSC SDP supporting 3D stereo and PSR
		 * (applies to eDP v1.3 or higher).
		 */
		info_packet->hb2 = 0x02;
		/* 08h = VSC packet supporting 3D stereo + PSR
		 * (HB2 = 02h).
		 */
		info_packet->hb3 = 0x08;

		for (i = 0; i < 28; i++)
			info_packet->sb[i] = 0;

		info_packet->valid = true;
	}

	if (vscPacketRevision == 0x1) {

		info_packet->hb0 = 0x00;	// Secondary-data Packet ID = 0
		info_packet->hb1 = 0x07;	// 07h = Packet Type Value indicating Video Stream Configuration packet
		info_packet->hb2 = 0x01;	// 01h = Revision number. VSC SDP supporting 3D stereo only
		info_packet->hb3 = 0x01;	// 01h = VSC SDP supporting 3D stereo only (HB2 = 01h).

		if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_INBAND_FA)
			info_packet->sb[0] = 0x1;

		info_packet->valid = true;
	}
}

void mod_build_infopackets(struct info_packet_inputs *inputs,
		struct info_packets *info_packets)
{
	if (info_packets->pVscInfoPacket != NULL)
		mod_build_vsc_infopacket(inputs->pStream, info_packets->pVscInfoPacket);
}

