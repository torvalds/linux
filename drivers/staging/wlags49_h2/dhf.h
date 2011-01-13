
/*   vim:tw=110:ts=4: */
#ifndef DHF_H
#define DHF_H

/**************************************************************************************************************
*
* FILE   :	DHF.H
*
* DATE	:	$Date: 2004/07/19 08:16:14 $   $Revision: 1.2 $
* Original	:	2004/05/17 07:33:13    Revision: 1.25      Tag: hcf7_t20040602_01
* Original	:	2004/05/11 06:03:14    Revision: 1.24      Tag: hcf7_t7_20040513_01
* Original	:	2004/04/15 09:24:42    Revision: 1.22      Tag: hcf7_t7_20040415_01
* Original	:	2004/04/09 14:35:52    Revision: 1.21      Tag: t7_20040413_01
* Original	:	2004/04/01 15:32:55    Revision: 1.18      Tag: t7_20040401_01
* Original	:	2004/03/10 15:39:28    Revision: 1.15      Tag: t20040310_01
* Original	:	2004/03/04 11:03:38    Revision: 1.13      Tag: t20040304_01
* Original	:	2004/02/25 14:14:37    Revision: 1.11      Tag: t20040302_03
* Original	:	2004/02/24 13:00:28    Revision: 1.10      Tag: t20040224_01
* Original	:	2004/02/19 10:57:28    Revision: 1.8      Tag: t20040219_01
*
* AUTHOR :	John Meertens
*			Nico Valster
*
* SPECIFICATION: .........
*
* DESC   :	structure definitions and function prototypes for unit DHF.
*
*			Customizable via HCFCFG.H, which is included indirectly via HCF.H
*
***************************************************************************************************************
*
*
* SOFTWARE LICENSE
*
* This software is provided subject to the following terms and conditions,
* which you should read carefully before using the software.  Using this
* software indicates your acceptance of these terms and conditions.  If you do
* not agree with these terms and conditions, do not use the software.
*
* COPYRIGHT (C) 1994 - 1995	by AT&T.				All Rights Reserved
* COPYRIGHT (C) 1999 - 2000 by Lucent Technologies.	All Rights Reserved
* COPYRIGHT (C) 2001 - 2004	by Agere Systems Inc.	All Rights Reserved
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
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
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
*
**************************************************************************************************************/


#ifdef _WIN32_WCE
#include <windef.h>
#endif

#include "hcf.h"   		 	/* includes HCFCFG.H too */

#ifdef DHF_UIL
#define GET_INFO(pp)  uil_get_info((LTVP)pp)
#define PUT_INFO(pp)  uil_put_info((LTVP)pp)
#else
#define GET_INFO(pp)  hcf_get_info(ifbp, (LTVP)pp)
#define PUT_INFO(pp)  hcf_put_info(ifbp, (LTVP)pp)
#endif


/*---- Defines --------------------------------------------------------------*/
#define CODEMASK				0x0000FFFFL    	/* Codemask for plug records */

/*---- Error numbers --------------------------------------------------------*/

#define DHF_ERR_INCOMP_FW		0x40	/* Image not compatible with NIC */

/*---- Type definitions -----------------------------------------------------*/
/* needed by dhf_wrap.c */

typedef struct {
	LTVP 	ltvp;
	hcf_16	len;
} LTV_INFO_STRUCT , *LTV_INFO_STRUCT_PTR;


/*
 * Type: 	plugrecord
 *
 * Abstract: This structure represents a Plug Data Record.
 *
 * Description:
 * This structure is used to overlay the plug records in the firmware memory image.
 */

typedef struct {
	hcf_32	code;      	/* Code to plug */
	hcf_32	addr;      	/* Address within the memory image to plug it in */
	hcf_32	len;       	/* The # of bytes which are available to store it */
} plugrecord;

/*
 * Type: 	stringrecord
 *
 * Abstract: This structure represents a Firmware debug/assert string
 *
 * Description:
 * This structure is used to get assert and debug outputs in the driver and/or utility to be
 * able to get more visability of the FW.
 */

#define MAX_DEBUGSTRINGS 		1024
#define MAX_DEBUGSTRING_LEN 	  82

typedef struct {
	hcf_32	id;
	char 	str[MAX_DEBUGSTRING_LEN];
} stringrecord;

/*
 * Type: 	exportrecord
 *
 * Abstract: This structure represents a Firmware export of a variable
 *
 * Description:
 * This structure is used to get the address and name of a FW variable.
 */

#define MAX_DEBUGEXPORTS 		2048
#define MAX_DEBUGEXPORT_LEN 	  12

typedef struct {
	hcf_32	id;
	char 	str[MAX_DEBUGEXPORT_LEN];
} exportrecord;

/* Offsets in memimage array p[] */
#define FWSTRINGS_FUNCTION		0
#define FWEXPORTS_FUNCTION		1

/*
 * Type: memimage
 *
 * Abstract: The "root" description of a complete memory image
 *
 * Description:
 * This type represents an entire memory image. The image is built up of several
 * segments. These segments need not be contiguous areas in memory, in other words
 * the image may contain 'holes'.
 *
 * The 'codep' field points to an array of segment_descriptor structures.
 * The end of the array is indicated by a segment_descriptor of which all fields are zero.
 * The 'execution' field is a 32-bit address representing the execution address
 *	of the firmware within the memory image. This address is zero in case of non-volatile
 *	memory download.
 * The 'compat' field points to an array of TODO
 * 	The end of the array is indicated by a plug record of which all fields are zero.
 * The 'identity' field points to an array of TODO
 * 	The end of the array is indicated by a plug record of which all fields are zero.
 * The Hermes-I specific 'pdaplug' field points to an array of Production Data Plug record structures.
 * 	The end of the array is indicated by a plug record of which all fields are zero.
 * The Hermes-I specific 'priplug' field points to an array of Primary Information Plug record structures.
 * 	The end of the array is indicated by a plug record of which all fields are zero.
 */
typedef struct {
	char					signature[14+1+1];	/* signature (see DHF.C) + C/LE-Bin/BE-Bin-flag + format version */
	CFG_PROG_STRCT FAR *codep;				/* */
	hcf_32           	 	execution;    		/* Execution address of the firmware */
	void FAR *place_holder_1;
	void FAR  		     	*place_holder_2;
	CFG_RANGE20_STRCT FAR  	*compat;      		/* Pointer to the compatibility info records */
	CFG_IDENTITY_STRCT FAR 	*identity;    		/* Pointer to the identity info records */
	void FAR				*p[2];				/* (Up to 9) pointers for (future) expansion
												 * currently in use:
												 *  - F/W printf information
												 */
} memimage;



/*-----------------------------------------------------------------------------
 *
 * DHF function prototypes
 *
 *---------------------------------------------------------------------------*/

EXTERN_C int dhf_download_fw(void *ifbp, memimage *fw);	/* ifbp, ignored when using the UIL */
EXTERN_C int dhf_download_binary(memimage *fw);


/*-----------------------------------------------------------------------------
 *
 * Functions to be provided by the user of the DHF module.
 *
 *---------------------------------------------------------------------------*/

/* defined in DHF.C; see there for comments */
EXTERN_C hcf_16 *find_record_in_pda(hcf_16 *pdap, hcf_16 code);

#endif  /* DHF_H */

