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

/* MichelMIC routine define */
struct michel_mic_t {
	u32 K0;	// Key
	u32 K1;	// Key
	u32 L;	// Current state
	u32 R;	// Current state
	u8 M[4];	// Message accumulator (single word)
	int nBytesInM;	// # bytes in M
	u8 Result[8];
};

void MichaelMICFunction(struct michel_mic_t *Mic, u8 *Key,
			u8 *Data, int Len, u8 priority,
			u8 *Result);
