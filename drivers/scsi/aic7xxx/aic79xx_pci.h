/*
 * Adaptec AIC79xx device driver for Linux.
 *
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id$
 *
 */
#ifndef _AIC79XX_PCI_H_
#define _AIC79XX_PCI_H_

#define ID_ALL_MASK			0xFFFFFFFFFFFFFFFFull
#define ID_ALL_IROC_MASK		0xFF7FFFFFFFFFFFFFull
#define ID_DEV_VENDOR_MASK		0xFFFFFFFF00000000ull
#define ID_9005_GENERIC_MASK		0xFFF0FFFF00000000ull
#define ID_9005_GENERIC_IROC_MASK	0xFF70FFFF00000000ull

#define ID_AIC7901			0x800F9005FFFF9005ull
#define ID_AHA_29320A			0x8000900500609005ull
#define ID_AHA_29320ALP			0x8017900500449005ull

#define ID_AIC7901A			0x801E9005FFFF9005ull
#define ID_AHA_29320LP			0x8014900500449005ull

#define ID_AIC7902			0x801F9005FFFF9005ull
#define ID_AIC7902_B			0x801D9005FFFF9005ull
#define ID_AHA_39320			0x8010900500409005ull
#define ID_AHA_29320			0x8012900500429005ull
#define ID_AHA_29320B			0x8013900500439005ull
#define ID_AHA_39320_B			0x8015900500409005ull
#define ID_AHA_39320_B_DELL		0x8015900501681028ull
#define ID_AHA_39320A			0x8016900500409005ull
#define ID_AHA_39320D			0x8011900500419005ull
#define ID_AHA_39320D_B			0x801C900500419005ull
#define ID_AHA_39320D_HP		0x8011900500AC0E11ull
#define ID_AHA_39320D_B_HP		0x801C900500AC0E11ull

#endif /* _AIC79XX_PCI_H_ */
