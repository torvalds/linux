//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

#include "si_rx_audio.h"
#include "si_video_tables.h"
#include "si_drv_rx_isr.h"
#include "si_rx_info.h"
#include "si_rx_video_mode_detection.h"
#include "si_drv_rx_cfg.h"
#include "si_cra.h"

#include "si_common.h"

#include "si_drv_rx.h"

#include "si_drv_board.h"


#define MAX_AUDIO_HANDLER_INVOCATION_TIME 100
#define MIN_AUDIO_HANDLER_INVOCATION_TIME 10

// Uncomment following defines for debugging
//#define PRINT_AUDIO_ERRORS
//#define PRINT_RX_CHST
//#define PRINT_FS_CALCULATION

#define BEGIN_ERROR_CHECK do
#define PERFORM_ERROR_CHECK(a, err) {if(!(a)) {error = true; audio_vars.error|=err; break;}}
#define END_ERROR_CHECK while(false);

#define AUDIO_ERR__NO_AUDIO_PACKETS		0x01
#define AUDIO_ERR__NO_CTS_PACKETS		0x02
#define AUDIO_ERR__CTS_OUT_OF_RANGE		0x04
#define AUDIO_ERR__CTS_IRREGULAR		0x08
#define AUDIO_ERR__PLL_UNLOCKED			0x10
#define AUDIO_ERR__FIFO_UNSTABLE		0x20


// Audio Channel Status byte 1
#define AUDIO_CHST1__ENCODED				0x02 // 0-PCM, 1- for other purposes

// Audio Channel Status byte 4
#define AUDIO_CHST4__FS_44					0x00 // Fs = 44.1 kHz
#define AUDIO_CHST4__FS_UNKNOWN				0x01 //
#define AUDIO_CHST4__FS_48					0x02 // Fs = 48 kHz
#define AUDIO_CHST4__FS_32					0x03 // Fs = 32 kHz
#define AUDIO_CHST4__FS_22					0x04 // Fs = 22.05 kHz
#define AUDIO_CHST4__FS_24					0x06 // Fs = 24 kHz
#define AUDIO_CHST4__FS_88					0x08 // Fs = 88.2 kHz
#define AUDIO_CHST4__FS_768					0x09 // Fs = 768 kHz (HBR Audio 4x192kHz)
#define AUDIO_CHST4__FS_96					0x0A // Fs = 96 kHz
#define AUDIO_CHST4__FS_176					0x0C // Fs = 176.4 kHz
#define AUDIO_CHST4__FS_192					0x0E // Fs = 192 kHz


typedef struct
{
	uint8_t		ca; // Channel Allocation field from Audio Info Frame
	uint8_t		fs; // sample frequency from the Audio Channel Status
	uint8_t		error; // error bit mask
	//bit_fld_t	status_received : 1; // true if channel status data is valid
	bit_fld_t	audio_info_frame_received : 1; // true if audio info frame has been received
	bit_fld_t	encoded : 1; // false for PCM, true for encoded streams
	bit_fld_t	protected : 1; // true for audio with protected context and with ACP packet
	//bit_fld_t	dsd_mode : 1; // true for DSD mode
	//bit_fld_t	start_request : 1; // a flag to start audio processing
	//bit_fld_t	exceptions_enabled : 1; // true if audio exceptions are on, false otherwise (shadow bit of 0x60.0xB5.0)
	//bit_fld_t	hbr_mode : 1; // true for HBR Audio mode
	//bit_fld_t	muted : 1; // set if Audio Mute pin is asserted by FW, cleared otherwise
	bit_fld_t	new_countdown : 1; // a request for a new count down in RxAudioTimerHandler()
	bit_fld_t	audio_is_on : 1; // static variable used in switch_audio()
	bit_fld_t layout1 : 1; // true for layout 1; false for layout 0
	time_ms_t	count_down; // a countdown timer
	uint8_t		measured_fs_code; // calculated sample frequency in Channel Status's format
	uint8_t		channel_status[5]; // first 5 bytes of audio status channel
}
rx_audio_vars_type;

static rx_audio_vars_type audio_vars = {0};

typedef struct
{
	uint8_t		code_value; // corresponding audio status Fs code
	uint16_t	ref_Fs; // reference Fs frequency in 100 Hz units
	uint16_t	min_Fs; // minimum Fs frequency in 100 Hz units
	uint16_t	max_Fs; // maximum Fs frequency in 100 Hz units
}
audio_fs_search_t;

#define AUDIO_FS_LIST_LENGTH 9
static ROM const audio_fs_search_t audio_fs_list[AUDIO_FS_LIST_LENGTH+1] =
{
	{ AUDIO_CHST4__FS_22,		220,	200,	230  },
	{ AUDIO_CHST4__FS_24,		240,	230,	280  },
	{ AUDIO_CHST4__FS_32,		320,	280,	380  },
	{ AUDIO_CHST4__FS_44,		441,	380,	460  },
	{ AUDIO_CHST4__FS_48,		480,	460,	540  },
	{ AUDIO_CHST4__FS_88,		882,	820,	921  },
	{ AUDIO_CHST4__FS_96,		960,	921,	1100 },
	{ AUDIO_CHST4__FS_176,		1764,	1600,	1792 },
	{ AUDIO_CHST4__FS_192,		1920,	1792,	2500 },
	{ AUDIO_CHST4__FS_UNKNOWN,	0,    	0,		0 }

};

#define AUDIO_CHANNEL_MASK_TABLE_LENGTH 32
static ROM const uint8_t audio_channel_mask[AUDIO_CHANNEL_MASK_TABLE_LENGTH] =
{
	0x10, 0x30, 0x30, 0x30, 0x70, 0x70, 0x70, 0x70,
	0x70, 0x70, 0x70, 0x70, 0xF0, 0xF0, 0xF0, 0xF0,
	0xF0, 0xF0, 0xF0, 0xF0, 0xB0, 0xB0, 0xB0, 0xB0,
	0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
};


#define AEC_REG_BLOCK_SIZE 3

/*
 Audio exception on H Resolution Change may be disabled to improve
 interoperability with some  incomplaint sources.
 Audio exception on V Resolution Change may be  disabled in 3D or 4K2K video
 mode since the chip generates false V Chng interrupts.
 Since audio exception may be skipped due to it is disabled for one
 of the reasons above, we need to make sure the audio is off manually.
 In this table V Res Chng is enabled, but H Res Chng is disabled.
 Once 3D or 4K2K mode is detected, V Res Cnhg will be disabled and
 H Res Chng will be enabled.
*/
ROM const uint8_t default_aec_regs[AEC_REG_BLOCK_SIZE] = {0xC1, 0x07, 0x01};


//-------------------------------------------------------------------------------------------------
static uint8_t get_rx_audio_channel_mask(uint8_t ca)
{
	uint8_t audio_mask = 0x10; // default: stereo
	if(ca < AUDIO_CHANNEL_MASK_TABLE_LENGTH)
	{
		audio_mask = audio_channel_mask[ca];
	}
	return audio_mask;
}

//-------------------------------------------------------------------------------------------------
// returns true if SPDIF output should be disabled, false otherwise
static bool_t is_spdif_out_prohibited(void)
{
	return 0 !=
	(
		audio_vars.protected || // protected audio
		((!audio_vars.encoded) && audio_vars.ca) // PCM with more than 2 channels
		|| !SiiSpdifEnableGet()
	);
}

//-------------------------------------------------------------------------------------------------
#ifdef PRINT_AUDIO_ERRORS
static void print_audio_errors(void)
{
#if 0 // 1 to save some ROM space
	DEBUG_PRINT(MSG_ERR, ("RX Audio Errors: %02X\n", (int) audio_vars[pipe].error));
#else
	DEBUG_PRINT(MSG_ERR, ("Rx Audio Errors: "));
	if(audio_vars.error & AUDIO_ERR__NO_AUDIO_PACKETS)
		DEBUG_PRINT(MSG_ERR, ("No Audio Packets "));
	if(audio_vars.error & AUDIO_ERR__NO_CTS_PACKETS)
		DEBUG_PRINT(MSG_ERR, ("No CTS Packets "));
	if(audio_vars.error & AUDIO_ERR__CTS_OUT_OF_RANGE)
		DEBUG_PRINT(MSG_ERR, ("CTS out of range "));
	if(audio_vars.error & AUDIO_ERR__CTS_IRREGULAR)
		DEBUG_PRINT(MSG_ERR, ("CTS is irregular "));
	if(audio_vars.error & AUDIO_ERR__PLL_UNLOCKED)
		DEBUG_PRINT(MSG_ERR, ("PLL unlocked "));
	if(audio_vars.error & AUDIO_ERR__FIFO_UNSTABLE)
		DEBUG_PRINT(MSG_ERR, ("FIFO unstable "));
	DEBUG_PRINT(MSG_ERR, ("\n"));
#endif
}
#endif // PRINT_AUDIO_ERRORS

//-------------------------------------------------------------------------------------------------
static uint32_t get_cts(void)
{
	uint16_t cts_l;
	uint32_t cts_h;

	cts_l = SiiRegReadWord(RX_A__CTS_HVAL1);
	cts_h = SiiRegRead(RX_A__CTS_HVAL3);
	return (cts_h << 16) | cts_l;
}

//-------------------------------------------------------------------------------------------------
static uint32_t get_n(void)
{
	uint16_t n_l;
	uint32_t n_h;

	n_l = SiiRegReadWord(RX_A__N_HVAL1);
	n_h = SiiRegRead(RX_A__N_HVAL3);
	return (n_h << 16) | n_l;
}

//-------------------------------------------------------------------------------------------------
// Get TMDS clock frequency of incoming video (in 100 Hz units).
// Limitation: this function may return zero if it is called before video is detected.
static uint32_t get_tclk_10kHz(void)
{
	uint32_t tmds_clk_10kHz = VMD_GetPixFreq10kHz();
	// this is Pix Clock (not TMDS clock at that moment) in 100 Hz units

	// convert pixel clock into TMDS clock
#if 0 //Deep color is not supported in 5293
	switch(SiiDrvRxInputColorDepthGet())
	{
		case SII_RX_VBUS_WIDTH_30: // DC 30 bit
			tmds_clk_10kHz = tmds_clk_10kHz * 5 / 4; // *1.25
		break;
		case SII_RX_VBUS_WIDTH_36: // DC 36 bit
			tmds_clk_10kHz = tmds_clk_10kHz * 3 / 2; // *1.5
		break;
		case SII_RX_VBUS_WIDTH_48: // DC 48 bit (reserved for the future)
			tmds_clk_10kHz *= 2; // *2
		break;
	}
#endif
	return tmds_clk_10kHz;
}

//-------------------------------------------------------------------------------------------------
static void set_mclk(void)
{
	rxCfgAudioMclk_t mclk = RX_CFG_256FS;   //the minimum MCLK for TDM is 256*fs, set MCLK as 256*fs always 

	uint8_t fs_code_per_channel = AUDIO_CHST4__FS_UNKNOWN;

	audio_vars.measured_fs_code = AUDIO_CHST4__FS_UNKNOWN;

	{
		// non-DSD mode

		uint32_t cts = get_cts();
		uint32_t n = get_n();

		uint32_t tmds_clk_10kHz = get_tclk_10kHz();
		// Note: tmds_clk_10kHz information may be not available
		// if audio detection occurs before video detection;
		// in this case tmds_clk_10kHz is 0

		uint16_t fs_frequency_per_channel = 0;

		if(tmds_clk_10kHz && cts)
		{
			uint8_t i;

			// Calculate audio Fs in 100Hz units
			uint16_t fs_calculated_100Hz = (tmds_clk_10kHz*n/cts)*100 /128;
			// Note: the order of operations is optimized for the maximum precision.
			// Overflow should not occur during the operations.
			// tmds_clk_10kHz - assume maximum value 30,000 (15bit) for 300 MHz
			// n - assume maximum value 192kHz*128/300=81000 (17bit)
			// tmds_clk_10kHz*n should fit into 32 bits
			// Maximum fs_calculated_100Hz is 192000/100=1920 fits into 16bit

			// Find closest standard audio Fs.
			for(i = 0; i < AUDIO_FS_LIST_LENGTH; i++)
			{
				if((fs_calculated_100Hz <= ( audio_fs_list[i].max_Fs))
					&& (fs_calculated_100Hz > ( audio_fs_list[i].min_Fs) ))
				{
					// search if calculated Fs close to the Fs in the table
					break;
				}
			}
			fs_code_per_channel = audio_fs_list[i].code_value;
			fs_frequency_per_channel = audio_fs_list[i].ref_Fs;
		}

		if(AUDIO_CHST4__FS_UNKNOWN == fs_code_per_channel)
		{
			DEBUG_PRINT(MSG_STAT, "RX Audio: Fs code = %02X\n", (int) (audio_vars.fs));
		}
		else
		{
//			DEBUG_PRINT(MSG_STAT, "RX Audio: Calculated Fs = %d kHz\n", (int) fs_frequency_per_channel/10);
		}

		audio_vars.measured_fs_code = fs_code_per_channel;
	}

	SiiRegWrite(RX_A__FREQ_SVAL, (mclk << 6) | (mclk << 4) | fs_code_per_channel);

	if(fs_code_per_channel == AUDIO_CHST4__FS_UNKNOWN) 
	{
		//if DSD mode or calculated Fs invalid
		SiiRegBitsSet(RX_A__ACR_CTRL1, RX_M__ACR_CTRL1__FS_SEL, OFF);
	}
	else
	{
		// use SW selected Fs
		SiiRegBitsSet(RX_A__ACR_CTRL1, RX_M__ACR_CTRL1__FS_SEL, ON);
	}
}

//-------------------------------------------------------------------------------------------------
static void set_output(void)
{
	uint8_t out_mask =
		RX_M__I2S_CTRL2__MCLK_EN   | // always enabled
		RX_M__I2S_CTRL2__SD0_EN    | // at least one I2S (two DSD) channel
		RX_M__I2S_CTRL2__MUTE_FLAT; // mute invalid packets

	if(audio_vars.audio_info_frame_received)
	{
		if(!audio_vars.encoded)
		{
			out_mask |= get_rx_audio_channel_mask(audio_vars.ca);
		}
		// Encoded data can be whether through 1 I2S channel or through HBRA;
		// it cannot be through multiple I2S channels in non-HBRA mode.
	};
	SiiRegWrite(RX_A__I2S_CTRL2, out_mask);

	// Enable TDM as configured
	SiiRegBitsSet(RX_A__TDM_CTRL1, RX_M__TDM_CTRL1__TDM_EN, SiiTdmEnableGet());

	// enable SPDIF if allowed
	SiiRegWrite(RX_A__AUDRX_CTRL,
		RX_M__AUDRX_CTRL__PASS_SPDIF_ERR |
		RX_M__AUDRX_CTRL__I2S_MODE |
		RX_M__AUDRX_CTRL__HW_MUTE_EN |
		(is_spdif_out_prohibited() ? 0 : RX_M__AUDRX_CTRL__SPDIF_EN));

}

//-------------------------------------------------------------------------------------------------
static void update(void)
{
	{
		set_output();
		set_mclk(); // couldn't it make audio restart if MCLK changed?
	}
}

//-------------------------------------------------------------------------------------------------
void RxAudio_OnAudioInfoFrame(uint8_t *p_data, uint8_t length)
{
	if(p_data && (length>=5))
	{
		audio_vars.audio_info_frame_received = true;
		audio_vars.ca = p_data[3];
	}
	else
	{
		audio_vars.audio_info_frame_received = false;
	}

	if(audio_vars.audio_info_frame_received)
	{
		// update() function has to be called
		// since channel allocation (CA) field my be changed
		update();
	}
}

//-------------------------------------------------------------------------------------------------
static void report_audio_format(void)
{
	SiiRxAudioFormat_t rxAudioFormat;

	rxAudioFormat.audioLayout = audio_vars.layout1;
	rxAudioFormat.audioEncoded = audio_vars.encoded;
	rxAudioFormat.audioChannelAllocation = audio_vars.ca;
	memcpy(rxAudioFormat.audioStatusChannel,
		audio_vars.channel_status, SI_AUDIO_ST_CH_LEN);
}

//-------------------------------------------------------------------------------------------------
void RxAudio_OnChannelStatusChange(void)
{
	SiiRegReadBlock(RX_A__CHST1, &audio_vars.channel_status[0], 3);

	audio_vars.encoded = 
		(0 != (audio_vars.channel_status[0] & AUDIO_CHST1__ENCODED));


	SiiRegReadBlock(RX_A__CHST4, &audio_vars.channel_status[3], 2);

	// Fs data read from RX_A__CHST4 may be not the actual value coming
	// from HDMI input, but the Fs written into RX_A__FREQ_SVAL.
	// To have real Fs value, get it from RX_A__PCLK_FS register
	// and replace in the audio status channel byte 4.
	audio_vars.channel_status[3] &= ~RX_M__CHST4__AUD_SAMPLE_F;
	audio_vars.fs = SiiRegRead(RX_A__PCLK_FS) & RX_M__PCLK_FS__SPDIF_EXTRACRTED_FS; // HW measured Fs


	if(AUDIO_CHST4__FS_UNKNOWN != audio_vars.measured_fs_code)
	{
		// replace with FW measured values
		audio_vars.channel_status[3] |= audio_vars.measured_fs_code;
	}
	else
	{
		// replace with HW measured values
		audio_vars.channel_status[3] |= audio_vars.fs;
	}

	// Note: DSD does not have Audio Status Channel, so all bytes in the Audio
	// Status Channel registers are zeros.
	// That should not cause problems because the DSD format is fixed.

#ifdef PRINT_RX_CHST
	DEBUG_PRINT(MSG_STAT, ("RX CHST %02X %02X %02X %02X %02X\n",
		(int) audio_vars.channel_status[0],
		(int) audio_vars.channel_status[1],
		(int) audio_vars.channel_status[2],
		(int) audio_vars.channel_status[3],
		(int) audio_vars.channel_status[4]));
#endif // PRINT_RX_CHST

	audio_vars.layout1 =
		(0 != (SiiRegRead(RX_A__AUDP_STAT) & RX_M__AUDP_STAT__LAYOUT));

	//audio_vars.status_received = true;
	report_audio_format();
	update();
}

//-------------------------------------------------------------------------------------------------
static void switch_audio(bool_t switch_on)
{
	if(switch_on)
	{
		if(!audio_vars.audio_is_on) // reduce extra log prints
		{
			DEBUG_PRINT(MSG_STAT, ("RX Audio: ON\n"));
			audio_vars.audio_is_on = true;
		}

		SiiRegBitsSet(RX_A__AUDP_MUTE, RX_M__AUDP_MUTE__AUDIO_MUTE, OFF);
	}
	else
	{
		if(audio_vars.audio_is_on) // reduce extra log prints
		{
			DEBUG_PRINT(MSG_STAT, ("RX Audio: OFF\n"));
			audio_vars.audio_is_on = false;
		}
		SiiRegBitsSet(RX_A__AUDP_MUTE, RX_M__AUDP_MUTE__AUDIO_MUTE, ON);

		// disable I2S output, but keep MCLKenable, SPDIF is on with flat output
		SiiRegWrite(RX_A__AUDRX_CTRL,
					RX_M__AUDRX_CTRL__PASS_SPDIF_ERR |
					RX_M__AUDRX_CTRL__I2S_MODE |
					0);
	}
}

//-------------------------------------------------------------------------------------------------
void RxAudio_Start(void)
{
    RxInfo_ResetAudioInfoFrameData();
    switch_audio(ON);
}

//-------------------------------------------------------------------------------------------------
void RxAudio_Stop(void)
{
    switch_audio(OFF);
}


//-------------------------------------------------------------------------------------------------
void RxAudio_ReStart(void)
{
	RxAudio_Stop();
	RxAudio_Start();
}

//-------------------------------------------------------------------------------------------------
// block SPDIF output depending on stream type by ACP
void RxAudio_OnAcpPacketUpdate(acp_type_type acp_type)
{
	bool_t protected_audio = (acp_GeneralAudio != acp_type);
	if(audio_vars.protected != protected_audio)
	{
		audio_vars.protected = protected_audio;
		if(protected_audio)
		{
			DEBUG_PRINT(MSG_STAT, ("Protected Audio\n"));
		}
		else
		{
			DEBUG_PRINT(MSG_STAT, ("General Audio\n"));
		}
        }
//	if((as_AudioOn == audio_vars.rx_audio_state) || (as_AudioReady== audio_vars.rx_audio_state))
		SiiRegBitsSet
		(
			RX_A__AUDRX_CTRL,
			RX_M__AUDRX_CTRL__SPDIF_EN,
			!is_spdif_out_prohibited()
		);
}


//-------------------------------------------------------------------------------------------------
void RxAudio_Init(void)
{
	memset(&audio_vars, 0, sizeof(audio_vars));
	audio_vars.fs = AUDIO_CHST4__FS_UNKNOWN; // means "not indicated", default values for 48kHz audio will be used

	SiiRegWrite(RX_A__TDM_CTRL2, 0x01); // configure FS signal width to 1 clock cycle

	// default audio settings (set to I2S)
	SiiRegWrite(RX_A__I2S_CTRL1, RX_M__I2S_CTRL1__EDGE);

	// Audio PLL setting
	SiiRegWrite(RX_A__APLL_POLE, 0x88);
	SiiRegWrite(RX_A__APLL_CLIP, 0x16);

	// allow SPDIF clocking even if audio is not coming out
	//SiiRegWrite(RX_A__AUDRX_CTRL, 0);
	SiiRegWrite(RX_A__AUDRX_CTRL, RX_M__AUDRX_CTRL__I2S_MODE);

	RxAudio_Start();
}



