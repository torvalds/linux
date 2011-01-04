/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_SDIOH_H
#define	_SDIOH_H

#define SD_SysAddr			0x000
#define SD_BlockSize			0x004
#define SD_BlockCount 			0x006
#define SD_Arg0				0x008
#define SD_Arg1 			0x00A
#define SD_TransferMode			0x00C
#define SD_Command 			0x00E
#define SD_Response0			0x010
#define SD_Response1 			0x012
#define SD_Response2			0x014
#define SD_Response3 			0x016
#define SD_Response4			0x018
#define SD_Response5 			0x01A
#define SD_Response6			0x01C
#define SD_Response7 			0x01E
#define SD_BufferDataPort0		0x020
#define SD_BufferDataPort1 		0x022
#define SD_PresentState			0x024
#define SD_HostCntrl			0x028
#define SD_PwrCntrl			0x029
#define SD_BlockGapCntrl 		0x02A
#define SD_WakeupCntrl 			0x02B
#define SD_ClockCntrl			0x02C
#define SD_TimeoutCntrl 		0x02E
#define SD_SoftwareReset		0x02F
#define SD_IntrStatus			0x030
#define SD_ErrorIntrStatus 		0x032
#define SD_IntrStatusEnable		0x034
#define SD_ErrorIntrStatusEnable 	0x036
#define SD_IntrSignalEnable		0x038
#define SD_ErrorIntrSignalEnable 	0x03A
#define SD_CMD12ErrorStatus		0x03C
#define SD_Capabilities			0x040
#define SD_Capabilities_Reserved	0x044
#define SD_MaxCurCap			0x048
#define SD_MaxCurCap_Reserved		0x04C
#define SD_ADMA_SysAddr			0x58
#define SD_SlotInterruptStatus		0x0FC
#define SD_HostControllerVersion 	0x0FE

/* SD specific registers in PCI config space */
#define SD_SlotInfo	0x40

#endif				/* _SDIOH_H */
