/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SCI_CONTROLLER_CONSTANTS_H_
#define _SCI_CONTROLLER_CONSTANTS_H_

/**
 * This file contains constant values that change based on the type of core or
 *    framework being managed.  These constants are exported in order to
 *    provide the user with information as to the bounds (i.e. how many) of
 *    specific objects.
 *
 *
 */


#ifdef SCIC_SDS_4_ENABLED

#ifndef SCI_MAX_PHYS
/**
 *
 *
 * This constant defines the maximum number of phy objects that can be
 * supported for the SCU Driver Standard (SDS) library.  This is tied directly
 * to silicon capabilities.
 */
#define SCI_MAX_PHYS  (4)
#endif

#ifndef SCI_MAX_PORTS
/**
 *
 *
 * This constant defines the maximum number of port objects that can be
 * supported for the SCU Driver Standard (SDS) library.  This is tied directly
 * to silicon capabilities.
 */
#define SCI_MAX_PORTS SCI_MAX_PHYS
#endif

#ifndef SCI_MIN_SMP_PHYS
/**
 *
 *
 * This constant defines the minimum number of SMP phy objects that can be
 * supported for a single expander level. This was determined by using 36
 * physical phys and room for 2 virtual phys.
 */
#define SCI_MIN_SMP_PHYS  (38)
#endif

#ifndef SCI_MAX_SMP_PHYS
/**
 *
 *
 * This constant defines the maximum number of SMP phy objects that can be
 * supported for the SCU Driver Standard (SDS) library. This number can be
 * increased if required.
 */
#define SCI_MAX_SMP_PHYS  (384)
#endif

#ifndef SCI_MAX_REMOTE_DEVICES
/**
 *
 *
 * This constant defines the maximum number of remote device objects that can
 * be supported for the SCU Driver Standard (SDS) library.  This is tied
 * directly to silicon capabilities.
 */
#define SCI_MAX_REMOTE_DEVICES (256)
#endif

#ifndef SCI_MIN_REMOTE_DEVICES
/**
 *
 *
 * This constant defines the minimum number of remote device objects that can
 * be supported for the SCU Driver Standard (SDS) library.  This # can be
 * configured for minimum memory environments to any value less than
 * SCI_MAX_REMOTE_DEVICES
 */
#define SCI_MIN_REMOTE_DEVICES (16)
#endif

#ifndef SCI_MAX_IO_REQUESTS
/**
 *
 *
 * This constant defines the maximum number of IO request objects that can be
 * supported for the SCU Driver Standard (SDS) library.  This is tied directly
 * to silicon capabilities.
 */
#define SCI_MAX_IO_REQUESTS (256)
#endif

#ifndef SCI_MIN_IO_REQUESTS
/**
 *
 *
 * This constant defines the minimum number of IO request objects that can be
 * supported for the SCU Driver Standard (SDS) library.  This # can be
 * configured for minimum memory environments to any value less than
 * SCI_MAX_IO_REQUESTS.
 */
#define SCI_MIN_IO_REQUESTS (1)
#endif

#ifndef SCI_MAX_MSIX_MESSAGES
/**
 *
 *
 * This constant defines the maximum number of MSI-X interrupt vectors/messages
 * supported for an SCU hardware controller instance.
 */
#define SCI_MAX_MSIX_MESSAGES  (2)
#endif

#ifndef SCI_MAX_SCATTER_GATHER_ELEMENTS
/**
 *
 *
 * This constant defines the maximum number of Scatter-Gather Elements to be
 * used by any SCI component.
 */
#define SCI_MAX_SCATTER_GATHER_ELEMENTS 130
#endif

#ifndef SCI_MIN_SCATTER_GATHER_ELEMENTS
/**
 *
 *
 * This constant defines the minimum number of Scatter-Gather Elements to be
 * used by any SCI component.
 */
#define SCI_MIN_SCATTER_GATHER_ELEMENTS 1
#endif

#else /* SCIC_SDS_4_ENABLED */

#error "SCI Core configuration left unspecified (e.g. SCIC_SDS_4_ENABLED)"

#endif /* SCIC_SDS_4_ENABLED */

/**
 *
 *
 * This constant defines the maximum number of controllers that can occur in a
 * single silicon package.
 */
#define SCI_MAX_CONTROLLERS 2

/**
 *
 *
 * The maximum number of supported domain objects is currently tied to the
 * maximum number of support port objects.
 */
#define SCI_MAX_DOMAINS  SCI_MAX_PORTS


#endif  /* _SCI_CONTROLLER_CONSTANTS_H_ */

