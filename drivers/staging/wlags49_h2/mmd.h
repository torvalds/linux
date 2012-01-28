
//   vim:tw=110:ts=4:
#ifndef MMD_H
#define MMD_H 1

/*************************************************************************************************************
*
* FILE	  : mmd.h
*
* DATE    : $Date: 2004/07/19 08:16:14 $   $Revision: 1.2 $
* Original: 2004/05/17 07:33:14    Revision: 1.18      Tag: hcf7_t20040602_01
* Original: 2004/05/11 06:22:59    Revision: 1.17      Tag: hcf7_t7_20040513_01
* Original: 2004/04/15 09:24:42    Revision: 1.13      Tag: hcf7_t7_20040415_01
* Original: 2004/04/08 15:18:17    Revision: 1.12      Tag: t7_20040413_01
* Original: 2004/04/01 15:32:55    Revision: 1.10      Tag: t7_20040401_01
* Original: 2004/03/04 16:47:50    Revision: 1.7      Tag: t20040310_01
* Original: 2004/03/03 12:47:05    Revision: 1.6      Tag: t20040304_01
* Original: 2004/02/25 14:14:39    Revision: 1.5      Tag: t20040302_03
* Original: 2004/02/24 13:00:29    Revision: 1.4      Tag: t20040224_01
* Original: 2004/01/30 09:59:33    Revision: 1.3      Tag: t20040219_01
*
* AUTHOR  : Nico Valster
*
* DESC    : Definitions and Prototypes for HCF, MSF, UIL as well as USF sources
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
* COPYRIGHT © 2001 - 2004	by Agere Systems Inc.	All Rights Reserved
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
#ifndef HCF_H
#include "hcf.h"	//just to get going with swig
#endif

EXTERN_C CFG_RANGE_SPEC_STRCT* mmd_check_comp( CFG_RANGES_STRCT *actp, CFG_SUP_RANGE_STRCT *supp );

#endif // MMD_H
