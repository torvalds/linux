/*******************************************************************************
 * Agere Systems Inc.
 * Wireless device driver for Linux (wlags49).
 *
 * Copyright (c) 1998-2003 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Initially developed by TriplePoint, Inc.
 *   http://www.triplepoint.com
 *
 *------------------------------------------------------------------------------
 *
 *   This file contains DHF configuration info.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2003 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 ******************************************************************************/

#ifndef DHFCFG_H
#define DHFCFG_H
/*-----------------------------------------------------------------------------
 * File DHFCFG.H
 *
 * Contents: #defines for the DHF module
 *
 * Comments:
 *   Some combinations of the #defines in this file are illegal (as noted below).
 *   If an illegal combinations of #defines is specified a compile error is
 *   generated. See document DHFUG.DOC for more information.
 *
 * Author: John Meertens
 * Date:   11-01-2000
 *
 * Change history:
 *---------------------------------------------------------------------------*/


// Define DHF_WCI if you want to use the WCI to access the ORiNOCO card.
// Define DHF_UIL if you want to use the UIL to access the ORiNOCO card.
// You must define either DHF_WCI or DHF_UIL. If neither of the two is defined
// or both a compile error is generated.
#define DHF_WCI
//!!!#define DHF_UIL

// Define DHF_BIG_ENDIAN if you are working on a big endian platform.
// Define DHF_LITTLE_ENDIAN if you are working on a little endian platform.
// You must define either DHF_BIG_ENDIAN or DHF_LITTLE_ENDIAN. If neither of
// the two is defined or both a compile error is generated.
#ifdef USE_BIG_ENDIAN
#define DHF_BIG_ENDIAN
#else
#define DHF_LITTLE_ENDIAN
#endif  /* USE_BIG_ENDIAN */

// Define DHF_WIN if you are working on Windows platform.
// Define DHF_DOS if you are working on DOS.
// You must define either DHF_WIN or DHF_DOS. If neither of
// the two is defined or both a compile error is generated.
//!!!#define DHF_WIN
//!!!#define DHF_DOS

// Define if you want the DHF to users. Not defining DHF_GET_RES_MSG
// leads to a decrease in code size as message strings are not included.
//!!!#define DHF_GET_RES_MSG

// Linux driver specific
// Prevent inclusion of stdlib.h and string.h
#define _INC_STDLIB
#define _INC_STRING

//-----------------------------------------------------------------------------
// Define one or more of the following DSF #defines if you want to implement
// the related DSF-function. Function dsf_callback must allways be implemented.
// See file DHF.H for prototypes of the functions.

// Define DSF_ALLOC if you want to manage memory allocation and de-allocation
// for the DHF. If DSF_ALLOC is defined you must implement dsf_alloc and dsf_free.
//!!!#define DSF_ALLOC

// Define DSF_CONFIRM if you want the DHF to ask the user for confirmation in a
// number of situations. If DSF_CONFIRM is defined you must implement dsf_confirm.
// Not defining DSF_CONFIRM leads to a decrease in code size as confirmation
// strings are not included.
//!!!#define DSF_CONFIRM

// Define DSF_DEBUG_MESSAGE if you want debug messages added to your output.
// If you define DSF_DEBUG_MESSAGE then you must implement function
// dsf_debug_message.
//#define DSF_DEBUG_MESSAGE

// Define DSF_ASSERT if you want asserts to be activated.
// If you define DSF_ASSERT then you must implement function dsf_assert.
//#define DBG 1
//#define DSF_ASSERT

// Define DSF_DBWIN if you want asserts and debug messages to be send to a debug
// window like SOFTICE or DebugView from SysInternals.
//!!!#define DSF_DBWIN
//!!! Not implemented yet!

// Define DSF_VOLATILE_ONLY if you only wants to use valatile functions
// This is a typical setting for a AP and a driver.
#define DSF_VOLATILE_ONLY

// Define DSF_HERMESII if you want to use the DHF for the Hermes-II
#ifdef HERMES2
#define DSF_HERMESII
#else
#undef DSF_HERMESII
#endif // HERMES2

// Define DSF_BINARY_FILE if you want to use the DHF in combination with
// reading the Firmware from a separate binary file.
//!!!#define DSF_BINARY_FILE

#endif // DHFCFG_H
