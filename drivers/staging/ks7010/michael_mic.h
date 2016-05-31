/*
 *   Driver for KeyStream wireless LAN
 *   
 *   michael_mic.h
 *   $Id: michael_mic.h 991 2009-09-14 01:38:58Z sekine $
 *
 *   Copyright (C) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it undr the terms of the GNU General Public License version 2 as
 *   published by the Free Sotware Foundation.
 */

/* MichelMIC routine define */
struct michel_mic_t {
	uint32_t K0;	// Key 
	uint32_t K1;	// Key 
	uint32_t L;	// Current state 
	uint32_t R;	// Current state 
	uint8_t M[4];	// Message accumulator (single word) 
	int nBytesInM;	// # bytes in M 
	uint8_t Result[8];
};

extern
void MichaelMICFunction(struct michel_mic_t *Mic, uint8_t * Key,
			uint8_t * Data, int Len, uint8_t priority,
			uint8_t * Result);
