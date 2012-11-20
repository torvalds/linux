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
 *   Driver common header for info needed by driver source and user-space
 *   processes communicating with the driver.
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
 * THIS SOFTWARE IS PROVIDED AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES,
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

#ifndef __WAVELAN2_IF_H__
#define __WAVELAN2_IF_H__




/*******************************************************************************
 *  constant definitions
 ******************************************************************************/
#define MAX_LTV_BUF_SIZE            (512 - (sizeof(hcf_16) * 2))

#define HCF_TALLIES_SIZE            (sizeof(CFG_HERMES_TALLIES_STRCT) + \
				    (sizeof(hcf_16) * 2))

#define HCF_MAX_MULTICAST           16
#define HCF_MAX_NAME_LEN            32
#define MAX_LINE_SIZE               256
#define HCF_NUM_IO_PORTS            0x80
#define TX_TIMEOUT                  ((800 * HZ) / 1000)


//#define HCF_MIN_COMM_QUALITY        0
//#define HCF_MAX_COMM_QUALITY        92
//#define HCF_MIN_SIGNAL_LEVEL        47
//#define HCF_MAX_SIGNAL_LEVEL        138
//#define HCF_MIN_NOISE_LEVEL         47
//#define HCF_MAX_NOISE_LEVEL         138
//#define HCF_0DBM_OFFSET             149

// PE1DNN
// Better data from the real world. Not scientific but empirical data gathered
// from a Thomson Speedtouch 110 which is identified as:
// PCMCIA Info: "Agere Systems" "Wireless PC Card Model 0110"
//              Manufacture ID: 0156,0003
// Lowest measurment for noise floor seen is value 54
// Highest signal strength in close proximity to the AP seen is value 118
// Very good must be around 100 (otherwise its never "full scale"
// All other constants are derrived from these. This makes the signal gauge
// work for me...
#define HCF_MIN_SIGNAL_LEVEL        54
#define HCF_MAX_SIGNAL_LEVEL        100
#define HCF_MIN_NOISE_LEVEL         HCF_MIN_SIGNAL_LEVEL
#define HCF_MAX_NOISE_LEVEL         HCF_MAX_SIGNAL_LEVEL
#define HCF_0DBM_OFFSET             (HCF_MAX_SIGNAL_LEVEL + 1)
#define HCF_MIN_COMM_QUALITY        0
#define HCF_MAX_COMM_QUALITY        (HCF_MAX_SIGNAL_LEVEL - \
					HCF_MIN_NOISE_LEVEL + 1)


/* For encryption (WEP) */
#define MIN_KEY_SIZE                5       // 40 bits RC4 - WEP
#define MAX_KEY_SIZE                13      // 104 bits
#define MAX_KEYS                    4

#define RADIO_CHANNELS              14
#define RADIO_SENSITIVITY_LEVELS    3
#define RADIO_TX_POWER_MWATT        32
#define RADIO_TX_POWER_DBM          15

#define MIN_RTS_BYTES               0
#define MAX_RTS_BYTES               2347

#define MAX_RATES                   8
#define MEGABIT                     (1024 * 1024)

#define HCF_FAILURE                 0xFF
#define UIL_FAILURE		            0xFF
#define CFG_UIL_CONNECT             0xA123          // Define differently?
#define CFG_UIL_CONNECT_ACK_CODE    0x5653435A      // VSCZ
#define WVLAN2_UIL_CONNECTED        (0x01L << 0)
#define WVLAN2_UIL_BUSY             (0x01L << 1)




/*******************************************************************************
 * driver ioctl interface
 ******************************************************************************/
#define WVLAN2_IOCTL_UIL            SIOCDEVPRIVATE

/* The UIL Interface used in conjunction with the WVLAN2_IOCTL_UIL code above
   is defined in mdd.h. A quick reference of the UIL codes is listed below */
/*
UIL_FUN_CONNECT
UIL_FUN_DISCONNECT
UIL_FUN_ACTION
    UIL_ACT_BLOCK
    UIL_ACT_UNBLOCK
    UIL_ACT_SCA
    UIL_ACT_DIAG
    UIL_ACT_APPLY
UIL_FUN_SEND_DIAG_MSG
UIL_FUN_GET_INFO
UIL_FUN_PUT_INFO
*/

#define SIOCSIWNETNAME              (SIOCDEVPRIVATE + 1)
#define SIOCGIWNETNAME              (SIOCDEVPRIVATE + 2)
#define SIOCSIWSTANAME              (SIOCDEVPRIVATE + 3)
#define SIOCGIWSTANAME              (SIOCDEVPRIVATE + 4)
#define SIOCSIWPORTTYPE             (SIOCDEVPRIVATE + 5)
#define SIOCGIWPORTTYPE             (SIOCDEVPRIVATE + 6)

/* IOCTL code for the RTS interface */
#define WL_IOCTL_RTS                (SIOCDEVPRIVATE + 7)

/* IOCTL subcodes for WL_IOCTL_RTS */
#define WL_IOCTL_RTS_READ           1
#define WL_IOCTL_RTS_WRITE          2
#define WL_IOCTL_RTS_BATCH_READ     3
#define WL_IOCTL_RTS_BATCH_WRITE    4


/*******************************************************************************
 * STRUCTURE DEFINITIONS
 ******************************************************************************/
typedef struct
{
	__u16   length;
	__u8    name[HCF_MAX_NAME_LEN];
}
wvName_t;


typedef struct
{
	hcf_16      len;
	hcf_16      typ;
	union
	{
		hcf_8       u8[MAX_LTV_BUF_SIZE / sizeof(hcf_8)];
		hcf_16      u16[MAX_LTV_BUF_SIZE / sizeof(hcf_16)];
		hcf_32      u32[MAX_LTV_BUF_SIZE / sizeof(hcf_32)];
	} u;
}
ltv_t;


struct uilreq
{
	union
	{
		char    ifrn_name[IFNAMSIZ];
	} ifr_ifrn;

	IFBP        hcfCtx;
	__u8        command;
	__u8        result;

	/* The data field in this structure is typically an LTV of some type.
	   The len field is the size of the buffer in bytes, as opposed to words
	   (like the L-field in the LTV */
	__u16       len;
	void       *data;
};


struct rtsreq
{
	union
	{
		char    ifrn_name[IFNAMSIZ];
	}
	ifr_ifrn;

	__u16   typ;
	__u16   reg;
	__u16   len;
	__u16   *data;
};


#endif  // __WAVELAN2_IF_H__

