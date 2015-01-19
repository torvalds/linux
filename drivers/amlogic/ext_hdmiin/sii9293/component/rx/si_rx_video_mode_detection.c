//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

#include "si_rx_video_mode_detection.h"
#include "si_video_tables.h"
#include "si_drv_rx_info.h"

#include "si_cra.h"

#include "si_drv_rx.h"

#include "si_drv_rx_isr.h"

#define NMB_OF_SAMPLES_TO_AVERAGE 8

#define	FPIX_TOLERANCE		200	// in 10 kHz units, i.e. 100 means +-1MHz
#define	FPIX_TOLERANCE_RANGE		10	// in 1/x, i.e. 10 means 10%
#define	PIXELS_TOLERANCE	30	// should be no more then 55 to distinguish all CEA861C modes
#define	LINES_TOLERANCE		15	// should be no more then 29 to distinguish all CEA861C modes

#define	H_FREQ_TOLERANCE	2	// H Freq tolerance in kHz, used for analog video detection

#define	FH_TOLERANCE	1	// H freq in 1 kHz units, used for range check
#define	FV_TOLERANCE	2	// V Freq in 1 Hz units, used for range check

#define ABS_DIFF(A, B) ((A>B) ? (A-B) : (B-A))


typedef struct
{
#if defined(__KERNEL__)
    SiiOsTimer_t videoStableTimer;
#else
    uint32_t    videoStableTime;
#endif
    bool_t      bReadyforVMD;
	uint8_t		video_index;
	uint8_t		cea861_vic;
	uint8_t		hdmi_vic;
	uint8_t		hdmi_3d_structure;
	uint8_t		hdmi_3d_ext_data;
	uint16_t	pix_freq; // pixel frequency in 10kHz units
	bit_fld_t	avi_received : 1;
	bit_fld_t	hdmi_3d_vsif_received : 1;
	bit_fld_t	hdmi_vic_received : 1;
} vmd_state_t;

static vmd_state_t vmd_data = {0};


uint8_t VMD_GetVideoIndex(void)
{
	return vmd_data.video_index;
}

static uint8_t search_861_mode(sync_info_type *p_sync_info)
{
	int i;
	uint8_t detected_video_idx = SI_VIDEO_MODE_NON_STD;
	int16_t range;

	enum
	{
		not_found = 0,
		found_not_exact = 1,
		found_exact = 2
	}
	search_result = not_found;

	for(i=0; VideoModeTable[i].Vic4x3 || VideoModeTable[i].Vic16x9; i++)
	{
		const videoMode_t *p_video_table = &VideoModeTable[i];
		bool_t interlaced = p_sync_info->Interlaced;
		uint16_t total_V_lines_measured =
			(interlaced ?
			(p_sync_info->TotalLines * 2)
			: p_sync_info->TotalLines);

		// check progressive/interlaced
		if(interlaced != p_video_table->Interlaced)
			continue;

		// check number of lines
		if(ABS_DIFF(total_V_lines_measured, p_video_table->Total.V) > LINES_TOLERANCE)
			continue;

		// check number of clocks per line (it works for all possible replications)
		if(ABS_DIFF(p_sync_info->ClocksPerLine, p_video_table->Total.H) > PIXELS_TOLERANCE)
			continue;

		// check Pixel Freq (in 10kHz units)
//		if(ABS_DIFF(p_sync_info->PixelFreq, p_video_table->PixClk) > FPIX_TOLERANCE)  // tolerance based on fixed bandwidth
                if (0 != ABS_DIFF(p_sync_info->PixelFreq, p_video_table->PixClk))  // tolerance based on dynamic bandwidth (fixed ratio)
                {
                    range = p_video_table->PixClk / ABS_DIFF(p_sync_info->PixelFreq, p_video_table->PixClk);

                    if( (range) < FPIX_TOLERANCE_RANGE)    // per PLL range
                        continue;
                }

#if 0        // enable it for mode search tuning
                DEBUG_PRINT(MSG_STAT,
                "Index in table: %d, interlaced: %d, range: %d",
                (int) i, (int) interlaced, (int)range);

                DEBUG_PRINT(MSG_STAT,
                "Pixel Freq detected: %d, Pixel Freq in video table: %d",
                (int) p_sync_info->PixelFreq, (int) p_video_table->PixClk);

                DEBUG_PRINT(MSG_STAT,
                "clock per lines detected: %d, lines detected: %d",
                (int) p_sync_info->ClocksPerLine, (int) total_V_lines_measured);

                DEBUG_PRINT(MSG_STAT,
                "clock per lines in video table: %d, lines in video table: %d\n",
                (int) p_video_table->Total.H, (int) p_video_table->Total.V);
#endif

		// if all previous tests passed, then we found at least one mode even polarity is mismatched
		if(search_result == not_found)
		{
			search_result = found_not_exact;
			detected_video_idx = i;
		}

		// check exact number of lines
		if(ABS_DIFF(total_V_lines_measured, p_video_table->Total.V) > 1)
			continue;

		// check polarities
		if(
			(p_sync_info->HPol == p_video_table->HPol) &&
			(p_sync_info->VPol == p_video_table->VPol)
			)
		{
			// if all previous checks passed
			search_result = found_exact;
			detected_video_idx = i;
			break;
		}
	}

	switch(search_result)
	{
	case not_found:
		break;
	case found_exact:
		break;
	case found_not_exact:
		DEBUG_PRINT(MSG_STAT, ("RX: Warning: not exact video mode found\n"));
		break;
	}

	return detected_video_idx;
}


static void verify_cea861vic(uint8_t vid_idx)
{
#if (SI_USE_DEBUG_PRINT == ENABLE)

	if(vmd_data.avi_received)
	{
		uint8_t vic = vmd_data.cea861_vic;

		if(
			(0 != vic) && // if AVI with VIC has been received
			(SI_VIDEO_MODE_NON_STD != vid_idx) // if resolution has been measured
			)
		{
			uint8_t table_idx = vid_idx & ~SI_VIDEO_MODE_3D_RESOLUTION_MASK;
			bool_t vic_correct = false;

			if(table_idx < (NMB_OF_CEA861_VIDEO_MODES + NMB_OF_HDMI_VIDEO_MODES))
			{
				// verify VIC
				vic_correct =
					(VideoModeTable[table_idx].Vic4x3 == vic) ||
					(VideoModeTable[table_idx].Vic16x9 == vic);
			}

			if(!vic_correct)
			{
				DEBUG_PRINT(MSG_STAT,
					"RX: Warning: AVI carries incorrect VIC=%d for resolution Idx=%d\n",
					(int) vic, (int) vid_idx);
			}

		}
	}
#endif // SI_USE_DEBUG_PRINT
}

static bool_t is_video_in_range(sync_info_type *p_sync_info)
{
	bool_t test_passed = false;

	if( (p_sync_info->ClocksPerLine < 100) || (p_sync_info->TotalLines < 100) )
	{
		// Also prevents from devision by 0 in h_freq and v_freq calculations.
		DEBUG_PRINT(MSG_STAT,
			"RX: Unsuitable Video: %d clocks per line, %d total lines\n",
			(int) p_sync_info->ClocksPerLine, (int) p_sync_info->TotalLines);
	}
	else
	{
		uint16_t pix_freq = p_sync_info->PixelFreq; // in 10kHz units
		uint8_t h_freq = ((uint32_t) (((uint32_t)p_sync_info->PixelFreq) * 10 + 5))
			/ p_sync_info->ClocksPerLine; // in 1 kHz units
		uint16_t v_freq = (((uint32_t) h_freq) * 1000 + 500)
			/ p_sync_info->TotalLines; // in 1 Hz units

		if(pix_freq <= (((uint16_t) VIDEO__MAX_PIX_CLK_10MHZ)
			* 1000 + FPIX_TOLERANCE))
		{
			if(
				(h_freq + FH_TOLERANCE >= VIDEO__MIN_H_KHZ) &&
				(h_freq <= (VIDEO__MAX_H_KHZ + FH_TOLERANCE))
				) // in 1 kHz units
			{
				if(
					(v_freq + FV_TOLERANCE >= VIDEO__MIN_V_HZ) &&
					(v_freq <= VIDEO__MAX_V_HZ + FV_TOLERANCE)
					)
				{
					test_passed = true;
				}
			}
		}

		if(!test_passed)
		{
			DEBUG_PRINT(MSG_STAT,
				"RX: Out Of Range: Fpix=%d0 kHz Fh=%d00 Hz Fv=%d Hz\n",
				(int) pix_freq, (int) h_freq, (int) v_freq);
		}
	}

	return test_passed;
}

static void print_861_resolution(void)
{
#if (SI_USE_DEBUG_PRINT == ENABLE)
	uint8_t vid_idx = vmd_data.video_index;
	uint8_t vic4x3 = VideoModeTable[vid_idx].Vic4x3;
	uint8_t vic16x9 = VideoModeTable[vid_idx].Vic16x9;
	DEBUG_PRINT(MSG_STAT, ("RX: Measured CEA861 mode: "));
	if(vic4x3 && vic16x9)
	{
		DEBUG_PRINT(MSG_STAT, "VIC %d/%d", (int) vic4x3, (int) vic16x9);
	}
	else if(vic4x3)
	{
		DEBUG_PRINT(MSG_STAT, "VIC %d", (int) vic4x3);
	}
	else if(vic16x9)
	{
		DEBUG_PRINT(MSG_STAT, "VIC %d", (int) vic16x9);
	}
	else
	{
		DEBUG_PRINT(MSG_STAT, "Internal Error");
	}
	DEBUG_PRINT(MSG_STAT, ", Idx = %d, ", (int) vid_idx);
	DEBUG_PRINT(MSG_STAT, "%d x %d %c @ %d Hz\n",
		(int) VideoModeTable[vid_idx].Active.H,
		(int) VideoModeTable[vid_idx].Active.V,
		VideoModeTable[vid_idx].Interlaced ? 'I' : 'P',
		(int) VideoModeTable[vid_idx].VFreq );
#endif // SI_USE_DEBUG_PRINT
}


static void print_3D_resolution(void)
{
#if (SI_USE_DEBUG_PRINT == ENABLE)
	vmd_state_t *p_vmd = &vmd_data;
	DEBUG_PRINT
	(
		MSG_STAT,
		
			"RX: 3D resolution VIC=0x%02X 3d_struct=0x%02X ext_data=0x%02X\n",
			(int) p_vmd->cea861_vic,
			(int) p_vmd->hdmi_3d_structure,
			(int) p_vmd->hdmi_3d_ext_data
		
	);
#endif // SI_USE_DEBUG_PRINT
}

static void print_hdmi_vic_resolution(void)
{
#if (SI_USE_DEBUG_PRINT == ENABLE)
	uint8_t vid_idx = vmd_data.video_index;
	DEBUG_PRINT(MSG_STAT, "RX: HDMI VIC: %d, Idx = %d, %d x %d %c @ %d Hz\n",
		(int) VideoModeTable[vid_idx].HdmiVic,
		(int) vid_idx,
		(int) VideoModeTable[vid_idx].Active.H,
		(int) VideoModeTable[vid_idx].Active.V,
		VideoModeTable[vid_idx].Interlaced ? 'I' : 'P',
		(int) VideoModeTable[vid_idx].VFreq );
#endif // SI_USE_DEBUG_PRINT
}

static void save_sync_info(sync_info_type *p_sync_info)
{
	SiiRxTiming_t rxTiming;

	rxTiming.videoIndex = vmd_data.video_index,
	rxTiming.clocksPerLine = p_sync_info->ClocksPerLine,
	rxTiming.totalLines = p_sync_info->TotalLines,
	rxTiming.pixelFreq = p_sync_info->PixelFreq,
	rxTiming.interlaced = p_sync_info->Interlaced,
	rxTiming.hPol = p_sync_info->HPol,
	rxTiming.vPol = p_sync_info->VPol,
	rxTiming.hdmi3dStructure = vmd_data.hdmi_3d_structure,
	rxTiming.extra3dData = vmd_data.hdmi_3d_ext_data;

	// Update RX timing info
	SiiDrvRxTimingInfoSet(&rxTiming);
}

static void clear_sync_info(void)
{
	SiiRxTiming_t rxTiming;
	memset(&rxTiming, 0, sizeof(rxTiming));
	rxTiming.videoIndex = SI_VIDEO_MODE_NON_STD;

    // Update RX timing info
	SiiDrvRxTimingInfoSet(&rxTiming);
}

// 3D timing depends not only on the CEA-861D VIC code, but also on a parameter
// called 3D_Structure. This function corrects the timing according to the parameter.
static void correct_sync_info_according_to_3d_mode(sync_info_type *p_sync_info)
{
	switch(vmd_data.hdmi_3d_structure)
	{
	case 0:	// Frame packing: progressive/interlaced
		if(p_sync_info->Interlaced)
		{
			// One frame contains even and odd images
			// and even it is interlaced ia a scaler perspective,
			// it appears as progressive format for HDMI chip HW.
			p_sync_info->TotalLines *= 2;
			p_sync_info->Interlaced = false;
		}
		// no break here
	case 2:	// Line alternative: progressive only
	case 4:	// L + depth: progressive only
		// multiply lines x2; multiply clock x2
		p_sync_info->TotalLines *= 2;
		p_sync_info->PixelFreq *= 2;
		break;

	case 1:	// Field alternative: interlaced only
		// multiply clock x2
		p_sync_info->PixelFreq *= 2;
		break;

	case 3:	// Side-by-Side (Full): progressive/interlaced
		// multiply pixel x2; multiply clock x2
		p_sync_info->ClocksPerLine *= 2;
		p_sync_info->PixelFreq *= 2;
		break;

	case 5:	// L + depth + graphics + graphics-depth: progressive only
		// multiply lines x4; multiply clock x4
		p_sync_info->TotalLines *= 4;
		p_sync_info->PixelFreq *= 4;
		break;

	default: // 2D timing compatible: progressive/interlaced
		break;
	}
}

static uint8_t get_video_index_from_hdmi_vsif(void)
{
	uint8_t detected_video_index = SI_VIDEO_MODE_NON_STD;

	if(vmd_data.hdmi_vic_received)
	{
		uint8_t hdmi_vic = vmd_data.hdmi_vic;
		if((hdmi_vic > 0) && (hdmi_vic <= LAST_KNOWN_HDMI_VIC))
		{
			detected_video_index = hdmiVicToVideoTableIndex[hdmi_vic];
		}
	}
	else if(vmd_data.avi_received && vmd_data.hdmi_3d_vsif_received)
	{
		uint8_t cea_vic = vmd_data.cea861_vic;
		if((cea_vic > 0) && (cea_vic <= LAST_KNOWN_CEA_VIC))
		{
			detected_video_index =
				ceaVicToVideoTableIndex[cea_vic] | SI_VIDEO_MODE_3D_RESOLUTION_MASK;
		}
	}
	return detected_video_index;
}

// This function takes video timing based information from the video timing table
// and fills p_sync_info structure with this timing information for vid_idx video index.
// Returnstrue on success, false on failure.
// Note: Repetition is not taken in account by this function because the function
// is used for 3D and HDMI VIC (4K2K) formats which do not use repetition
// by HDMI 1.4a specification. Moreover, HDMI 1.4a does not specify how exactly treat
// the formats with repetition. The function might require modification in the future
// if such support is required.
static bool_t fill_sync_info_from_video_table(sync_info_type *p_sync_info, uint8_t vid_idx)
{
	bool_t success = false;

	if(vid_idx < (NMB_OF_CEA861_VIDEO_MODES + NMB_OF_HDMI_VIDEO_MODES))
	{
		const videoMode_t *p_video_table = &VideoModeTable[vid_idx];
		p_sync_info->Interlaced = p_video_table->Interlaced;
		p_sync_info->ClocksPerLine = p_video_table->Total.H;
		p_sync_info->TotalLines = p_video_table->Total.V;
		p_sync_info->PixelFreq = p_video_table->PixClk;
		p_sync_info->HPol = p_video_table->HPol;
		p_sync_info->VPol = p_video_table->VPol;
		if(p_sync_info->Interlaced)
		{
			p_sync_info->TotalLines /= 2;
		}
		success = true;
	}
	return success;
}

// Returns detected video format index; fills structure pointed by p_sync_info.
static uint8_t detect_video_resolution(sync_info_type *p_sync_info)
{
	uint8_t detected_video_idx = get_video_index_from_hdmi_vsif();
	bool_t bb;

	if(SI_VIDEO_MODE_NON_STD == detected_video_idx)
	{
		SiiDrvRxGetSyncInfo(p_sync_info);

		if(is_video_in_range(p_sync_info))
		{
			detected_video_idx = search_861_mode(p_sync_info);

			if(SI_VIDEO_MODE_NON_STD != detected_video_idx)
			{
				// CEA861 mode found
				// Verify if VIC matches the format.
				// If it does not match, an error message will be printed.
				// Measured format has higher priority than AVI's VIC.
				verify_cea861vic(detected_video_idx);
			}
			else
			{
				// Input does not much to any known CEA861 mode or 3D format.
				// It can be considered as a PC mode
				// if such compilation option is enabled.
#if (SI_ALLOW_PC_MODES == ENABLE)
				// In other words, consider any non- CEA-861D or non-3D
				// format as a PC resolution if video timing parameters
				// are within allowed range.
				detected_video_idx = SI_VIDEO_MODE_PC_OTHER;
#endif // SI_ALLOW_PC_MODES
			}
		}
	}
	else if(detected_video_idx & SI_VIDEO_MODE_3D_RESOLUTION_MASK)
	{
		// 3D video
		if(fill_sync_info_from_video_table(p_sync_info,
			(detected_video_idx & ~SI_VIDEO_MODE_3D_RESOLUTION_MASK)))
		{
			correct_sync_info_according_to_3d_mode(p_sync_info);
			if(!is_video_in_range(p_sync_info))
			{
				// Input video is out of range, do not use it.
				detected_video_idx = SI_VIDEO_MODE_NON_STD;
			}
		}
		else
		{
			detected_video_idx = SI_VIDEO_MODE_NON_STD;
		}
	}
	else if((detected_video_idx >= NMB_OF_CEA861_VIDEO_MODES)
		&& (detected_video_idx < (NMB_OF_CEA861_VIDEO_MODES + NMB_OF_HDMI_VIDEO_MODES)))
	{
		// HDMI VIC resolutions (e.g. 4K2K)
		if(fill_sync_info_from_video_table(p_sync_info, detected_video_idx))
		{
			if(!is_video_in_range(p_sync_info))
			{
				// Input video is out of range, do not use it.
				detected_video_idx = SI_VIDEO_MODE_NON_STD;
			}
		}
		else
		{
			detected_video_idx = SI_VIDEO_MODE_NON_STD;
		}
	}
	else
	{
		// internal error case, should never happen
		DEBUG_PRINT(MSG_ERR, "RX: Internal detection error\n");
	}

	return detected_video_idx;
}

#if defined(__KERNEL__)
void VMD_VideoStableNotify(uint8_t vid_idx )
{
    #define MAX_REPORT_DATA_STRING_SIZE 20
    uint8_t vic4x3, vic16x9;
    char str[MAX_REPORT_DATA_STRING_SIZE];
    
    if (SI_VIDEO_MODE_NON_STD == vid_idx)
    {
        scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "out of range");
    }else if (SI_VIDEO_MODE_PC_OTHER == vid_idx)
    {
        scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "pc resolution");
    }else
    {
        vid_idx &= 0x7f;
        if (vid_idx >= NMB_OF_CEA861_VIDEO_MODES)
        {
            scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "HDMI VIC %d", VideoModeTable[vid_idx].HdmiVic);
        }
        else
        {
            vic4x3 = VideoModeTable[vid_idx].Vic4x3;
            vic16x9 = VideoModeTable[vid_idx].Vic16x9;
            if (vic4x3 && vic16x9)
                scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "CEA VIC %d %d", vic4x3, vic16x9);
            else if (vic4x3)
                scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "CEA VIC %d", vic4x3);
            else if (vic16x9)
                scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "CEA VIC %d", vic16x9);
            else
                scnprintf(str,	MAX_REPORT_DATA_STRING_SIZE, "%s", "null");
        }
    }
    sysfs_notify(&devinfo->device->kobj, NULL, "input_video_mode");
    send_sii5293_uevent(devinfo->device, DEVICE_EVENT, DEV_INPUT_VIDEO_MODE_EVENT, str);
}
#endif

// returns true if video format is detected, false otherwise
bool_t VMD_DetectVideoResolution(void)
{
	bool_t format_detected = true;
	sync_info_type sync_info;
	uint8_t vid_idx;

    if (vmd_data.bReadyforVMD)
    {
    	vid_idx = detect_video_resolution(&sync_info);
    	vmd_data.video_index = vid_idx;

#if defined(__KERNEL__)
        gDriverContext.input_video_mode = vid_idx;
        VMD_VideoStableNotify(vid_idx);
#endif
    	if(SI_VIDEO_MODE_NON_STD == vid_idx)
    	{
    		format_detected = false;
    	}
    	else if(SI_VIDEO_MODE_PC_OTHER == vid_idx)
    	{
    		DEBUG_PRINT(MSG_STAT, "RX: PC resolution\n");
    	}
    	else if(vid_idx & SI_VIDEO_MODE_3D_RESOLUTION_MASK)
    	{
    		print_3D_resolution();
    	}
    	else if(vid_idx >= NMB_OF_CEA861_VIDEO_MODES)
    	{
    		print_hdmi_vic_resolution();
    	}
    	else
    	{
    		// CEA-861D resolutions
    		print_861_resolution();
    	}

    	if(format_detected)
    	{
    		vmd_data.pix_freq = sync_info.PixelFreq;
    		save_sync_info(&sync_info);
    	}
    	else
    	{
    		VMD_ResetTimingData();
    	}
    }else
    {
        // Video is not stable, no need to dectect resolution
        return false;
    }

	return format_detected;
}

void VMD_OnAviPacketReceiving(uint8_t cea861vic)
{
	vmd_data.avi_received = true;
	vmd_data.cea861_vic = cea861vic;

	verify_cea861vic(vmd_data.video_index);

	if(vmd_data.hdmi_3d_vsif_received)
	{
		// 3D video detected
		VMD_DetectVideoResolution();
	}
}

void VMD_VsifProcessing(uint8_t *p_packet, uint8_t length)
{
       // check IEEE OUI first

       // Check if it is HDMI VSIF, HDMI IEEE OUI is 0x000C03
       if ( 0x03 == p_packet[0] && 0x0C == p_packet[1] && 0x00 == p_packet[2]  )
       {
           VMD_HdmiVsifProcessing(p_packet, length);
           DEBUG_PRINT(MSG_STAT, "RX: HDMI VSIF found!\n");
       }
       // Check if it is MHL VSIF, MHL IEEE OUI is 0x7CA61D
       else if ( 0x1D == p_packet[0] && 0xA6 == p_packet[1] && 0x7C == p_packet[2] )
       {
           VMD_MhlVsifProcessing(p_packet, length);
           DEBUG_PRINT(MSG_STAT, "RX: MHL VSIF found!\n");
       }

}

void VMD_HdmiVsifProcessing(uint8_t *p_packet, uint8_t length)
{
	// p_packet should point to an HDMI VSIF packet.
	// No other VSIF packets are supported by this function.

	uint8_t hdmi_video_format = p_packet[3] >> 5;

	switch(hdmi_video_format)
	{
	case 1:	// Extended resolution format
		vmd_data.hdmi_vic = p_packet[4];
		vmd_data.hdmi_vic_received = true;

		// Reset 3D info
		// (HDMI 1.4 spec does not allow HDMI VIC & 3D at the same time)
		vmd_data.hdmi_3d_structure = 0;
		vmd_data.hdmi_3d_ext_data = 0;
		vmd_data.hdmi_3d_vsif_received = false;

		// HDMI VIC (4K2K) video detected.
		// RX Video State Machine has to be informed
		// as video re-detection may be required.
		VMD_DetectVideoResolution();
		break;
	case 2:	// 3D format
		// Get 3D structure field.
		// Timing from VideoModeTable[] has to be modified
		// depending on the 3d_Structure field.
		// Store it for further processing.
		vmd_data.hdmi_3d_structure = p_packet[4] >> 4;

              // Configure GPIO as indicator only for Frame Packing in HDMI
              if ( 0 == vmd_data.hdmi_3d_structure )
              {
                  RX_ConfigureGpioAs3dFrameIndicator();
              }

		// Side-by-Side (Half) has at least one additional parameter,
		// collect it.
		vmd_data.hdmi_3d_ext_data =
			(vmd_data.hdmi_3d_structure >= 8) && (length > 4) ?
			(p_packet[5] >> 4) : // has 3D_Ext_Data field
			0;
		vmd_data.hdmi_3d_vsif_received = true;

		// Reset HDMI VIC info
		// (HDMI 1.4 spec does not allow HDMI VIC & 3D at the same time)
		vmd_data.hdmi_vic = 0;
		vmd_data.hdmi_vic_received = false;

		if(vmd_data.avi_received)
		{
			// 3D video detected.
			// RX Video State Machine has to be informed
			// as video re-detection may be required.
			VMD_DetectVideoResolution();
		}
		break;
	}
}

void VMD_MhlVsifProcessing(uint8_t *p_packet, uint8_t length)
{
	// p_packet should point to an HDMI VSIF packet.
	// No other VSIF packets are supported by this function.
	uint8_t mhl_video_format = p_packet[3] & 0x03;       // get the MHL_VID_FMT field
	uint8_t mhl_3d_format_type;                                     // to keep the MHL_3D_FMT_TYPE field
	bool_t bValid3DVsif = false;

       // Check whether 3D format present
       if ( 0x01 != mhl_video_format || length < 4 )
       {
           return;
       }

       mhl_3d_format_type = ( p_packet[3] & 0x3F ) >> 2;     // get the MHL_3D_FMT_TYPE field
       
	switch(mhl_3d_format_type)
	{
            case 0:     // Frame Sequential 3D Video
                bValid3DVsif = true;        	
                vmd_data.hdmi_3d_structure = 0x00;   // Store it for further processing.
                vmd_data.hdmi_3d_ext_data = 0x00;   // Reset to Zero

                // Configure GPIO as indicator only for Frame Sequential in MHL
                RX_ConfigureGpioAs3dFrameIndicator();
                break;     

            case 1:     // Top-Bottom 3D Video
                bValid3DVsif = true;        	
                vmd_data.hdmi_3d_structure = 0x06;   // Store it for further processing.
                vmd_data.hdmi_3d_ext_data = 0x00;   // Reset to Zero
                break;

            case 2:     // Left-Right 3D Video
                bValid3DVsif = true;      	
                vmd_data.hdmi_3d_structure = 0x08;   // Store it for further processing.

                // one additional parameter, set to the default value 0001
                vmd_data.hdmi_3d_ext_data = 0x01;
                break;

            default:
                break;
	}

       if ( bValid3DVsif )
       {
		vmd_data.hdmi_3d_vsif_received = true;

		// Reset HDMI VIC info
		// (HDMI 1.4 spec does not allow HDMI VIC & 3D at the same time)
		vmd_data.hdmi_vic = 0;
		vmd_data.hdmi_vic_received = false;

		if(vmd_data.avi_received)
		{
			// 3D video detected.
			// RX Video State Machine has to be informed
			// as video re-detection may be required.
			VMD_DetectVideoResolution();
		}
        }
       
}

vsif_check_result_t VMD_GetVsifPacketType(uint8_t *p_packet, uint8_t length)
{
	vsif_check_result_t packet_analysis = NOT_HDMI_VSIF;

	// DEBUG_PRINT(MSG_ALWAYS, ("VMD_GetVsifPacketType \n"));

	// Check HDMI VSIF signature.
	// HDMI IEEE Registration Identifier is 0x000C03 (least significant byte first).
	if((0x03 == p_packet[0]) && (0x0C == p_packet[1]) && (0x00 == p_packet[2]))
	{
		// HDMI VSIF signature is found.
		// Check HDMI format.
		packet_analysis = VMD_GetHdmiVsifPacketType(p_packet, length);
	}
	// Check MHL VSIF signature.
	// MHL IEEE Registration Identifier is 0x7CA61D (least significant byte first).
       // Check if it is MHL VSIF, MHL IEEE OUI is 
	else if((0x1D == p_packet[0]) && (0xA6 == p_packet[1]) && (0x7C == p_packet[2]))
	{
		// MHL VSIF signature is found.
		// Check MHL format.
		packet_analysis = VMD_GetMhlVsifPacketType(p_packet, length);
	}
	else
	{
		DEBUG_PRINT(MSG_ALWAYS, ("No valid signature recognized\n"));
       }

	return packet_analysis;
}

vsif_check_result_t VMD_GetHdmiVsifPacketType(uint8_t *p_packet, uint8_t length)
{
	vsif_check_result_t packet_analysis = NOT_HDMI_VSIF;

	// Check HDMI VSIF signature.
	// HDMI IEEE Registration Identifier is 0x000C03 (least significant byte first).
	if((0x03 == p_packet[0]) && (0x0C == p_packet[1]) && (0x00 == p_packet[2]))
	{
		// HDMI VSIF signature is found.
		// Check HDMI format.

		uint8_t hdmi_video_format = p_packet[3] >> 5;

		switch(hdmi_video_format)
		{
		case 1:
			// HDMI VIC format (extended resolution format)
			packet_analysis = NEW_EXTENDED_RESOLUTION;
			if(
				vmd_data.hdmi_vic_received &&
				(vmd_data.hdmi_vic == p_packet[4])
				)
			{
				// HDMI VIC has been received and the new packet
				// caries the same HDMI VIC.
				packet_analysis = OLD_EXTENDED_RESOLUTION;
			}
			break;
		case 2:
			// 3D format.
			if(vmd_data.hdmi_3d_vsif_received)
			{
				packet_analysis = OLD_3D;
				// may be modified by the next few lines.

				// Check if new 3D structure field matches
				// previously received packet.
				if(vmd_data.hdmi_3d_structure != p_packet[4] >> 4)
				{
					// 3D_Structure is different; the packet is new 3D.
					packet_analysis = NEW_3D;
				}
				// Side-by-Side (Half) has at least one additional parameter.
				// Check if it matches to the previously received one.
				if((8 == vmd_data.hdmi_3d_structure) && (length > 4))
				{
					// 3D_Ext_Data field
					if(vmd_data.hdmi_3d_ext_data != (p_packet[5] >> 4))
					{
						// 3D_Structure is different; the packet is new 3D.
						packet_analysis = NEW_3D;
					}
				}
			}
			else
			{
				// First 3D packet receiving.
				packet_analysis = NEW_3D;
			}
			break;
		}
	}
	return packet_analysis;
}

vsif_check_result_t VMD_GetMhlVsifPacketType(uint8_t *p_packet, uint8_t length)
{
	vsif_check_result_t packet_analysis = NOT_HDMI_VSIF;

	// Check MHL VSIF signature.
	// MHL IEEE Registration Identifier is 0x7CA61D (least significant byte first).
	if((0x1D == p_packet[0]) && (0xA6 == p_packet[1]) && (0x7C == p_packet[2]))
	{
		// MHL VSIF signature is found.
		// Check MHL format.

        	uint8_t mhl_video_format = p_packet[3] & 0x03;       // get the MHL_VID_FMT field
        	uint8_t mhl_3d_format_type;                                     // to keep the MHL_3D_FMT_TYPE field

               // Check whether 3D format present
               if ( 0x01 != mhl_video_format || length < 4 )
               {
                   return packet_analysis;
               }

               mhl_3d_format_type = ( p_packet[3] & 0x3F ) >> 2;     // get the MHL_3D_FMT_TYPE field

        	// Check if new 3D structure field matches
        	// previously received packet. Assume there is no change
              packet_analysis = OLD_3D;
        	switch(mhl_3d_format_type)
        	{
                    case 0:     // Frame Sequential 3D Video
                        if ( 0x00 != vmd_data.hdmi_3d_structure || false == vmd_data.hdmi_3d_vsif_received )
                        {
                            packet_analysis = NEW_3D;
                        }
                        break;     

                    case 1:     // Top-Bottom 3D Video
                        if ( 0x06 != vmd_data.hdmi_3d_structure )
                        {
                            packet_analysis = NEW_3D;
                        }
                        break;

                    case 2:     // Left-Right 3D Video
                        if ( 0x08 != vmd_data.hdmi_3d_structure )
                        {
                            packet_analysis = NEW_3D;
                        }
                        // FD: no need to worry about this per MHL 2.0 spec, there is only one value 0001 for Left-Right 3D Video
                        /*
                        // One more parameter to check for Left-Right 3D Video
                        else if ( 0x01 != vmd_data[pipe].hdmi_3d_ext_data )
                        {
                        }
                        */
                        break;

                    default:
                        DEBUG_PRINT(MSG_ALWAYS, ("Unrecognized MHL 3D format type.\n"));
                        break;
        	}
	}

	return packet_analysis;
}

void VMD_OnHdmiVsifPacketDiscontinuation(void)
{
	// Forget HDMI VSIF data
	vmd_data.hdmi_vic = 0;
	vmd_data.hdmi_vic_received = false;
	vmd_data.hdmi_3d_structure = 0;
	vmd_data.hdmi_3d_ext_data = 0;
	vmd_data.hdmi_3d_vsif_received = false;

	// Changing from 3D/4K2K to non-3D/4K2K in most cases causes pixel clock interruption
	// that change RX state machine's state. In this case no other processing is required.
	// However, some 3D to 2D formats have compatible timings and it is possible
	// skipping such transaction. This function is called when HDMI VSIF packets
	// are no longer comming. That event may happen on 3D to 2D change.
	// The function starts format verification.
	VMD_DetectVideoResolution();
}

// This function is called from RX interrupt service routine to
// measure current resolution and compare it with the current one.
// If the resolutions differ, the function return true.
// Otherwise it returns false.
// Since the function is used for timing verification and the measured timing
// data is not valid in 3D/HDMI_VIC mode, it always returns false if 3D/HDMI_VIC is detected.
// PC resolutions always return true.
bool_t VMD_WasResolutionChanged(void)
{
	sync_info_type sync_info;
	uint8_t measured_video_idx = detect_video_resolution(&sync_info);
	bool_t resolution_changed = false;

	if(measured_video_idx == SI_VIDEO_MODE_PC_OTHER)
	{
		// PC resolutions.
		// Since we do not keep the timing information for PC resolutions,
		// assume that the resolution change event is real.
		// It is acceptable because in most cases the false resolution
		// change ocures due to deep color mode changing
		// and the deep color is rarely used with PC resolutions.
		// In the worst case when PC resolution is used with deep color,
		// returning true on false resolution change will only delay
		// the video at the sink.
		resolution_changed = true;
	}
	if(measured_video_idx < NMB_OF_CEA861_VIDEO_MODES)
	{
		// CEA-861D resolutions only
		uint8_t current_video_idx = vmd_data.video_index;
		resolution_changed = (current_video_idx != measured_video_idx);
	}
	return resolution_changed;
}

void VMD_ResetTimingData(void)
{
#if defined(__KERNEL__)
    gDriverContext.input_video_mode = 0;
#endif
    vmd_data.bReadyforVMD = false;
    vmd_data.video_index = SI_VIDEO_MODE_NON_STD;
    vmd_data.pix_freq = 0;
    clear_sync_info();
}
#if defined(__KERNEL__)
extern void sii_signal_notify(unsigned int status);
static void VMD_Timer_Callback(void *pArg)
{
	bool_t signal_detected = false;

    DEBUG_PRINT(MSG_STAT, ("Video stable timer expired.\n"));
	vmd_data.bReadyforVMD = true;
    signal_detected = VMD_DetectVideoResolution();
    SiiDrvRxMuteVideo(OFF);

    if( signal_detected == true )
    {
    	sii_signal_notify(1);
    }
}
#else
void SiiRxFormatDetect(void)
{
    if (vmd_data.videoStableTime)
    {
        //Wait 100ms after sync detect to determine the video resolution.
        if (SiiTimerTotalElapsed() - vmd_data.videoStableTime > VIDEO_STABLE_TIME)
        {
            vmd_data.videoStableTime = 0;
            vmd_data.bReadyforVMD = true;
            VMD_DetectVideoResolution();
            SiiDrvRxMuteVideo(OFF);
        }
    }
}
#endif
void VMD_Init(void)
{
	memset(&vmd_data, 0, sizeof(vmd_state_t));
	VMD_ResetTimingData();
#if defined(__KERNEL__)
    SiiOsTimerCreate("Vid Timer", VMD_Timer_Callback, NULL, &vmd_data.videoStableTimer);
#endif
}

void VMD_ResetInfoFrameData(void)
{
	vmd_data.avi_received = false;
	vmd_data.hdmi_3d_vsif_received = false;
	vmd_data.hdmi_vic_received = false;
}

// Returns detected pixel frequency in 10 kHz units if video is detected
// or zero otherwise.
uint16_t VMD_GetPixFreq10kHz(void)
{
	return vmd_data.pix_freq;
}

void SiiRxSetVideoStableTimer(void)
{
    bool_t bScdtState = false;
    if ( SiiDrvVidStableGet(&bScdtState) )
    {
        VMD_ResetTimingData();

#if defined(__KERNEL__)
        if (bScdtState)
        {
            SiiOsTimerStart(vmd_data.videoStableTimer, VIDEO_STABLE_TIME);
        }
        else
        {
            SiiDrvSoftwareReset(RX_M__SRST__HDCP_RST);      //Do HDCP reset when there's no scdt
            SiiOsTimerStop(vmd_data.videoStableTimer);
        }
#else
        if (bScdtState)
        {
            vmd_data.videoStableTime = SiiTimerTotalElapsed();
        }
        else
        {
            SiiDrvSoftwareReset(RX_M__SRST__HDCP_RST);
            vmd_data.videoStableTime = 0;
        }
#endif
    }
}


/*****************************************************************************/
/**
 *  @brief		Configure GPIO as 3D Frame Indicator
 *
 *****************************************************************************/
void RX_ConfigureGpioAs3dFrameIndicator(void)
{
    SiiRegWrite( GPIO_MODE, VAL_GPIO_0_3D_INDICATOR );
}

