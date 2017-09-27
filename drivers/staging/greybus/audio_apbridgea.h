/**
 * Copyright (c) 2015-2016 Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * This is a special protocol for configuring communication over the
 * I2S bus between the DSP on the MSM8994 and APBridgeA.  Therefore,
 * we can predefine several low-level attributes of the communication
 * because we know that they are supported.  In particular, the following
 * assumptions are made:
 *	- there are two channels (i.e., stereo)
 *	- the low-level protocol is I2S as defined by Philips/NXP
 *	- the DSP on the MSM8994 is the clock master for MCLK, BCLK, and WCLK
 *	- WCLK changes on the falling edge of BCLK
 *	- WCLK low for left channel; high for right channel
 *	- TX data is sent on the falling edge of BCLK
 *	- RX data is received/latched on the rising edge of BCLK
 */

#ifndef __AUDIO_APBRIDGEA_H
#define __AUDIO_APBRIDGEA_H

#define AUDIO_APBRIDGEA_TYPE_SET_CONFIG			0x01
#define AUDIO_APBRIDGEA_TYPE_REGISTER_CPORT		0x02
#define AUDIO_APBRIDGEA_TYPE_UNREGISTER_CPORT		0x03
#define AUDIO_APBRIDGEA_TYPE_SET_TX_DATA_SIZE		0x04
							/* 0x05 unused */
#define AUDIO_APBRIDGEA_TYPE_PREPARE_TX			0x06
#define AUDIO_APBRIDGEA_TYPE_START_TX			0x07
#define AUDIO_APBRIDGEA_TYPE_STOP_TX			0x08
#define AUDIO_APBRIDGEA_TYPE_SHUTDOWN_TX		0x09
#define AUDIO_APBRIDGEA_TYPE_SET_RX_DATA_SIZE		0x0a
							/* 0x0b unused */
#define AUDIO_APBRIDGEA_TYPE_PREPARE_RX			0x0c
#define AUDIO_APBRIDGEA_TYPE_START_RX			0x0d
#define AUDIO_APBRIDGEA_TYPE_STOP_RX			0x0e
#define AUDIO_APBRIDGEA_TYPE_SHUTDOWN_RX		0x0f

#define AUDIO_APBRIDGEA_PCM_FMT_8			BIT(0)
#define AUDIO_APBRIDGEA_PCM_FMT_16			BIT(1)
#define AUDIO_APBRIDGEA_PCM_FMT_24			BIT(2)
#define AUDIO_APBRIDGEA_PCM_FMT_32			BIT(3)
#define AUDIO_APBRIDGEA_PCM_FMT_64			BIT(4)

#define AUDIO_APBRIDGEA_PCM_RATE_5512			BIT(0)
#define AUDIO_APBRIDGEA_PCM_RATE_8000			BIT(1)
#define AUDIO_APBRIDGEA_PCM_RATE_11025			BIT(2)
#define AUDIO_APBRIDGEA_PCM_RATE_16000			BIT(3)
#define AUDIO_APBRIDGEA_PCM_RATE_22050			BIT(4)
#define AUDIO_APBRIDGEA_PCM_RATE_32000			BIT(5)
#define AUDIO_APBRIDGEA_PCM_RATE_44100			BIT(6)
#define AUDIO_APBRIDGEA_PCM_RATE_48000			BIT(7)
#define AUDIO_APBRIDGEA_PCM_RATE_64000			BIT(8)
#define AUDIO_APBRIDGEA_PCM_RATE_88200			BIT(9)
#define AUDIO_APBRIDGEA_PCM_RATE_96000			BIT(10)
#define AUDIO_APBRIDGEA_PCM_RATE_176400			BIT(11)
#define AUDIO_APBRIDGEA_PCM_RATE_192000			BIT(12)

#define AUDIO_APBRIDGEA_DIRECTION_TX			BIT(0)
#define AUDIO_APBRIDGEA_DIRECTION_RX			BIT(1)

/* The I2S port is passed in the 'index' parameter of the USB request */
/* The CPort is passed in the 'value' parameter of the USB request */

struct audio_apbridgea_hdr {
	__u8	type;
	__le16	i2s_port;
	__u8	data[0];
} __packed;

struct audio_apbridgea_set_config_request {
	struct audio_apbridgea_hdr	hdr;
	__le32				format;	/* AUDIO_APBRIDGEA_PCM_FMT_* */
	__le32				rate;	/* AUDIO_APBRIDGEA_PCM_RATE_* */
	__le32				mclk_freq; /* XXX Remove? */
} __packed;

struct audio_apbridgea_register_cport_request {
	struct audio_apbridgea_hdr	hdr;
	__le16				cport;
	__u8				direction;
} __packed;

struct audio_apbridgea_unregister_cport_request {
	struct audio_apbridgea_hdr	hdr;
	__le16				cport;
	__u8				direction;
} __packed;

struct audio_apbridgea_set_tx_data_size_request {
	struct audio_apbridgea_hdr	hdr;
	__le16				size;
} __packed;

struct audio_apbridgea_prepare_tx_request {
	struct audio_apbridgea_hdr	hdr;
} __packed;

struct audio_apbridgea_start_tx_request {
	struct audio_apbridgea_hdr	hdr;
	__le64				timestamp;
} __packed;

struct audio_apbridgea_stop_tx_request {
	struct audio_apbridgea_hdr	hdr;
} __packed;

struct audio_apbridgea_shutdown_tx_request {
	struct audio_apbridgea_hdr	hdr;
} __packed;

struct audio_apbridgea_set_rx_data_size_request {
	struct audio_apbridgea_hdr	hdr;
	__le16				size;
} __packed;

struct audio_apbridgea_prepare_rx_request {
	struct audio_apbridgea_hdr	hdr;
} __packed;

struct audio_apbridgea_start_rx_request {
	struct audio_apbridgea_hdr	hdr;
} __packed;

struct audio_apbridgea_stop_rx_request {
	struct audio_apbridgea_hdr	hdr;
} __packed;

struct audio_apbridgea_shutdown_rx_request {
	struct audio_apbridgea_hdr	hdr;
} __packed;

#endif /*__AUDIO_APBRIDGEA_H */
