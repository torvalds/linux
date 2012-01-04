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
 *   This header file contains version information for the code base, as well as
 *   special definitions and macros needed by certain versions of the code.
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

#ifndef __WL_VERSION_H__
#define __WL_VERSION_H__

/*******************************************************************************
 *  include files
 ******************************************************************************/
//#include <linux/config.h>

#ifndef CONFIG_MODVERSIONS
#define __NO_VERSION__
#endif  // CONFIG_MODVERSIONS

/*******************************************************************************
 *  constant definitions
 ******************************************************************************/

#define VENDOR_NAME         "Agere Systems, http://www.agere.com"

#define DRIVER_NAME         "wlags49"
#define DRV_IDENTITY        49

#define DRV_MAJOR_VERSION   7
#define DRV_MINOR_VERSION   22
#define DRV_VERSION_STR     "7.22"


#if defined BUS_PCMCIA
#define BUS_TYPE            "PCMCIA"
#elif defined BUS_PCI
#define BUS_TYPE            "PCI"
#else
err: define bus type;
#endif  // BUS_XXX

#if defined HERMES25
#define HW_TYPE				"HII.5"
#else
#define HW_TYPE				"HII"
#endif // HERMES25

#if defined WARP
#define FW_TYPE				"WARP"
#else
#define FW_TYPE				"BEAGLE"
#endif // WARP

#if defined HERMES25
#if defined WARP
#define DRV_VARIANT         3
#else
#define DRV_VARIANT         4
#endif // WARP
#else
#define DRV_VARIANT         2
#endif // HERMES25

#ifdef BUS_PCMCIA
#if defined HERMES25
#define MODULE_NAME         DRIVER_NAME "_h25_cs"
#else
#define MODULE_NAME         DRIVER_NAME "_h2_cs"
#endif  /* HERMES25 */
#elif defined BUS_PCI
#if defined HERMES25
#define MODULE_NAME         DRIVER_NAME "_h25"
#else
#define MODULE_NAME         DRIVER_NAME "_h2"
#endif  /* HERMES25 */
#endif  /* BUS_XXX */

#ifdef DBG
#define MODULE_DATE         __DATE__ " " __TIME__
#else
#define MODULE_DATE         "07/18/2004 13:30:00"
#endif // DBG

//#define STR2(m) #m
//#define STR1(m) STR2(m)
//#define MODULE_NAME			STR1( MOD_NAME )

#define VERSION_INFO        MODULE_NAME " v" DRV_VERSION_STR \
							" for " BUS_TYPE ", " 											   	 \
							MODULE_DATE " by " VENDOR_NAME

/* The version of wireless extensions we support */
#define WIRELESS_SUPPORT    21

//#define DBG_MOD_NAME         DRIVER_NAME ":" BUS_TYPE ":" HW_TYPE ":" FW_TYPE
#define DBG_MOD_NAME        MODULE_NAME



/*******************************************************************************
 *  bus architechture specific defines, includes, etc.
 ******************************************************************************/
/*
 * There doesn't seem to be a difference for PCMCIA and PCI anymore, at least
 * for PCMCIA the same defines are needed now as previously only used for PCI
 */

#define NEW_MULTICAST
#define ALLOC_SKB(len)   dev_alloc_skb(len+2)
#define GET_PACKET(dev, skb, count)\
                        skb_reserve((skb), 2); \
                        BLOCK_INPUT(skb_put((skb), (count)), (count)); \
                        (skb)->protocol = eth_type_trans((skb), (dev))
#define GET_PACKET_DMA(dev, skb, count)\
                        skb_reserve((skb), 2); \
                        BLOCK_INPUT_DMA(skb_put((skb), (count)), (count)); \
                        (skb)->protocol = eth_type_trans((skb), (dev))




#endif  // __WL_VERSION_H__
