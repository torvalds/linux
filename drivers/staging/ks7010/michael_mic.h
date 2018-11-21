/*
 *   Driver for KeyStream wireless LAN
 *
 *   Copyright (C) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 */

/* MichaelMIC routine define */
struct michael_mic_t {
	u32 k0;	// Key
	u32 k1;	// Key
	u32 l;	// Current state
	u32 r;	// Current state
	u8 m[4];	// Message accumulator (single word)
	int m_bytes;	// # bytes in M
	u8 result[8];
};

void michael_mic_function(struct michael_mic_t *mic, u8 *key,
			  u8 *data, int len, u8 priority, u8 *result);
