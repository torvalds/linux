/*	$NetBSD: hid_test_data.c,v 1.2 2016/01/07 15:58:23 jakllsch Exp $	*/

/*
 * Copyright (c) 2016 Jonathan A. Kollasch
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

static const uint8_t range_test_report_descriptor[] = {
	0x0b, 0x03, 0x00, 0x00, 0xff,	// Usage
	0x75, 0x20,			// Report Size
	0x95, 0x01,			// Report Count
	0x17, 0x00, 0x00, 0x00, 0x80,	// Logical Minimum
	0x27, 0xff, 0xff, 0xff, 0x7f,	// Logical Maximum
	0x37, 0x00, 0x00, 0x00, 0x80,	// Physical Minimum
	0x47, 0xff, 0xff, 0xff, 0x7f,	// Physical Maximum
	0x81, 0x00,			// Input

	0x0b, 0x02, 0x00, 0x00, 0xff,	// Usage
	0x75, 0x10,			// Report Size
	0x95, 0x01,			// Report Count
	0x16, 0x00, 0x80,		// Logical Minimum
	0x26, 0xff, 0x7f,		// Logical Maximum
	0x36, 0x00, 0x80,		// Physical Minimum
	0x46, 0xff, 0x7f,		// Physical Maximum
	0x81, 0x00,			// Input

	0x0b, 0x01, 0x00, 0x00, 0xff,	// Usage
	0x75, 0x08,			// Report Size
	0x95, 0x01,			// Report Count
	0x15, 0x80,			// Logical Minimum
	0x25, 0x7f,			// Logical Maximum
	0x35, 0x80,			// Physical Minimum
	0x45, 0x7f,			// Physical Maximum
	0x81, 0x00,			// Input
};

static const uint8_t range_test_minimum_report[7] = {
	0x00, 0x00, 0x00, 0x80,
	0x00, 0x80,
	0x80,
};

static const uint8_t range_test_negative_one_report[7] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff,
	0xff,
};

static const uint8_t range_test_positive_one_report[7] = {
	0x01, 0x00, 0x00, 0x00,
	0x01, 0x00,
	0x01,
};

static const uint8_t range_test_maximum_report[7] = {
	0xff, 0xff, 0xff, 0x7f,
	0xff, 0x7f,
	0x7f,
};

static const uint8_t unsigned_range_test_report_descriptor[] = {
	0x0b, 0x13, 0x00, 0x00, 0xff,	// Usage
	0x75, 0x20,			// Report Size
	0x95, 0x01,			// Report Count
	0x17, 0x00, 0x00, 0x00, 0x00,	// Logical Minimum
	0x27, 0xff, 0xff, 0xff, 0xff,	// Logical Maximum
	0x37, 0x00, 0x00, 0x00, 0x00,	// Physical Minimum
	0x47, 0xff, 0xff, 0xff, 0xff,	// Physical Maximum
	0x81, 0x00,			// Input

	0x0b, 0x12, 0x00, 0x00, 0xff,	// Usage
	0x75, 0x10,			// Report Size
	0x95, 0x01,			// Report Count
	0x16, 0x00, 0x00,		// Logical Minimum
	0x26, 0xff, 0xff,		// Logical Maximum
	0x36, 0x00, 0x00,		// Physical Minimum
	0x46, 0xff, 0xff,		// Physical Maximum
	0x81, 0x00,			// Input

	0x0b, 0x11, 0x00, 0x00, 0xff,	// Usage
	0x75, 0x08,			// Report Size
	0x95, 0x01,			// Report Count
	0x15, 0x00,			// Logical Minimum
	0x25, 0xff,			// Logical Maximum
	0x35, 0x00,			// Physical Minimum
	0x45, 0xff,			// Physical Maximum
	0x81, 0x00,			// Input
};

static const uint8_t unsigned_range_test_minimum_report[7] = {
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,
	0x00,
};

static const uint8_t unsigned_range_test_positive_one_report[7] = {
	0x01, 0x00, 0x00, 0x00,
	0x01, 0x00,
	0x01,
};

static const uint8_t unsigned_range_test_negative_one_report[7] = {
	0xfe, 0xff, 0xff, 0xff,
	0xfe, 0xff,
	0xfe,
};

static const uint8_t unsigned_range_test_maximum_report[7] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff,
	0xff,
};

static const uint8_t just_pop_report_descriptor[] = {
	0xb4,
};
