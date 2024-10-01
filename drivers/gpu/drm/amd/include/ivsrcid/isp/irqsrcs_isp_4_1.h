/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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
 */
#ifndef __IRQSRCS_ISP_4_1_H__
#define __IRQSRCS_ISP_4_1_H__


#define ISP_4_1__SRCID__ISP_SEMA_WAIT_FAIL_TIMEOUT			0x12	// Semaphore wait fail timeout
#define ISP_4_1__SRCID__ISP_SEMA_WAIT_INCOMPLETE_TIMEOUT		0x13	// Semaphore wait incomplete timeout
#define ISP_4_1__SRCID__ISP_SEMA_SIGNAL_INCOMPLETE_TIMEOUT		0x14	// Semaphore signal incomplete timeout
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE5_CHANGED			0x15	// Ringbuffer base5 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT5			        0x16	// Ringbuffer write point 5 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE6_CHANGED			0x17	// Ringbuffer base6 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT6			        0x18	// Ringbuffer write point 6 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE7_CHANGED			0x19	// Ringbuffer base7 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT7			        0x1A	// Ringbuffer write point 7 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE8_CHANGED			0x1B	// Ringbuffer base8 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT8			        0x1C	// Ringbuffer write point 8 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE9_CHANGED			0x00    // Ringbuffer base9 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT9			        0x01    // Ringbuffer write point 9 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE10_CHANGED			0x02    // Ringbuffer base10 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT10			        0x03    // Ringbuffer write point 10 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE11_CHANGED			0x04    // Ringbuffer base11 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT11			        0x05    // Ringbuffer write point 11 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE12_CHANGED			0x06    // Ringbuffer base12 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT12			        0x07    // Ringbuffer write point 12 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE13_CHANGED			0x08    // Ringbuffer base13 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT13			        0x09    // Ringbuffer write point 13 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE14_CHANGED			0x0A    // Ringbuffer base14 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT14			        0x0B    // Ringbuffer write point 14 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE15_CHANGED			0x0C    // Ringbuffer base15 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT15			        0x0D    // Ringbuffer write point 15 changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_BASE16_CHANGED			0x0E    // Ringbuffer base16 address changed
#define ISP_4_1__SRCID__ISP_RINGBUFFER_WPT16			        0x0F    // Ringbuffer write point 16 changed
#define ISP_4_1__SRCID__ISP_MIPI0			                0x29	// MIPI0 interrupt
#define ISP_4_1__SRCID__ISP_MIPI1			                0x2A	// MIPI1 interrupt
#define ISP_4_1__SRCID__ISP_I2C0			                0x2B	// I2C0 PAD interrupt
#define ISP_4_1__SRCID__ISP_I2C1			                0x2C	// I2C1 PAD interrupt
#define ISP_4_1__SRCID__ISP_FLASH0			                0x2D	// Flash0 interrupt
#define ISP_4_1__SRCID__ISP_FLASH1			                0x2E	// Flash1 interrupt
#define ISP_4_1__SRCID__ISP_DEBUG			                0x2F	// Debug information

#endif
