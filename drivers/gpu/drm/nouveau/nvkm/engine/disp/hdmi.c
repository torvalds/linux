// SPDX-License-Identifier: GPL-2.0
#include "hdmi.h"

void pack_hdmi_infoframe(struct packed_hdmi_infoframe *packed_frame,
			 u8 *raw_frame, ssize_t len)
{
	u32 header = 0;
	u32 subpack0_low = 0;
	u32 subpack0_high = 0;
	u32 subpack1_low = 0;
	u32 subpack1_high = 0;

	switch (len) {
		/*
		 * "When in doubt, use brute force."
		 *     -- Ken Thompson.
		 */
	default:
		/*
		 * We presume that no valid frame is longer than 17
		 * octets, including header...  And truncate to that
		 * if it's longer.
		 */
	case 17:
		subpack1_high = (raw_frame[16] << 16);
		/* fall through */
	case 16:
		subpack1_high |= (raw_frame[15] << 8);
		/* fall through */
	case 15:
		subpack1_high |= raw_frame[14];
		/* fall through */
	case 14:
		subpack1_low = (raw_frame[13] << 24);
		/* fall through */
	case 13:
		subpack1_low |= (raw_frame[12] << 16);
		/* fall through */
	case 12:
		subpack1_low |= (raw_frame[11] << 8);
		/* fall through */
	case 11:
		subpack1_low |= raw_frame[10];
		/* fall through */
	case 10:
		subpack0_high = (raw_frame[9] << 16);
		/* fall through */
	case 9:
		subpack0_high |= (raw_frame[8] << 8);
		/* fall through */
	case 8:
		subpack0_high |= raw_frame[7];
		/* fall through */
	case 7:
		subpack0_low = (raw_frame[6] << 24);
		/* fall through */
	case 6:
		subpack0_low |= (raw_frame[5] << 16);
		/* fall through */
	case 5:
		subpack0_low |= (raw_frame[4] << 8);
		/* fall through */
	case 4:
		subpack0_low |= raw_frame[3];
		/* fall through */
	case 3:
		header = (raw_frame[2] << 16);
		/* fall through */
	case 2:
		header |= (raw_frame[1] << 8);
		/* fall through */
	case 1:
		header |= raw_frame[0];
		/* fall through */
	case 0:
		break;
	}

	packed_frame->header = header;
	packed_frame->subpack0_low = subpack0_low;
	packed_frame->subpack0_high = subpack0_high;
	packed_frame->subpack1_low = subpack1_low;
	packed_frame->subpack1_high = subpack1_high;
}
