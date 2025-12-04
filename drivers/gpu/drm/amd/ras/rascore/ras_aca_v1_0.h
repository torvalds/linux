/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#ifndef __RAS_ACA_V1_0_H__
#define __RAS_ACA_V1_0_H__
#include "ras.h"

#define ACA__REG__FIELD(x, h, l)		  (((x) & GENMASK_ULL(h, l)) >> l)
#define ACA_REG_STATUS_VAL(x)		    ACA__REG__FIELD(x, 63, 63)
#define ACA_REG_STATUS_OVERFLOW(x)	    ACA__REG__FIELD(x, 62, 62)
#define ACA_REG_STATUS_UC(x)		    ACA__REG__FIELD(x, 61, 61)
#define ACA_REG_STATUS_EN(x)		    ACA__REG__FIELD(x, 60, 60)
#define ACA_REG_STATUS_MISCV(x)		    ACA__REG__FIELD(x, 59, 59)
#define ACA_REG_STATUS_ADDRV(x)		    ACA__REG__FIELD(x, 58, 58)
#define ACA_REG_STATUS_PCC(x)		    ACA__REG__FIELD(x, 57, 57)
#define ACA_REG_STATUS_ERRCOREIDVAL(x)  ACA__REG__FIELD(x, 56, 56)
#define ACA_REG_STATUS_TCC(x)		    ACA__REG__FIELD(x, 55, 55)
#define ACA_REG_STATUS_SYNDV(x)		    ACA__REG__FIELD(x, 53, 53)
#define ACA_REG_STATUS_CECC(x)		    ACA__REG__FIELD(x, 46, 46)
#define ACA_REG_STATUS_UECC(x)		    ACA__REG__FIELD(x, 45, 45)
#define ACA_REG_STATUS_DEFERRED(x)	    ACA__REG__FIELD(x, 44, 44)
#define ACA_REG_STATUS_POISON(x)	    ACA__REG__FIELD(x, 43, 43)
#define ACA_REG_STATUS_SCRUB(x)		    ACA__REG__FIELD(x, 40, 40)
#define ACA_REG_STATUS_ERRCOREID(x)	    ACA__REG__FIELD(x, 37, 32)
#define ACA_REG_STATUS_ADDRLSB(x)	    ACA__REG__FIELD(x, 29, 24)
#define ACA_REG_STATUS_ERRORCODEEXT(x)  ACA__REG__FIELD(x, 21, 16)
#define ACA_REG_STATUS_ERRORCODE(x)	    ACA__REG__FIELD(x, 15, 0)

#define ACA_REG_IPID_MCATYPE(x)		  ACA__REG__FIELD(x, 63, 48)
#define ACA_REG_IPID_INSTANCEIDHI(x)  ACA__REG__FIELD(x, 47, 44)
#define ACA_REG_IPID_HARDWAREID(x)	  ACA__REG__FIELD(x, 43, 32)
#define ACA_REG_IPID_INSTANCEIDLO(x)  ACA__REG__FIELD(x, 31, 0)

#define ACA_REG_MISC0_VALID(x)		  ACA__REG__FIELD(x, 63, 63)
#define ACA_REG_MISC0_OVRFLW(x)		  ACA__REG__FIELD(x, 48, 48)
#define ACA_REG_MISC0_ERRCNT(x)		  ACA__REG__FIELD(x, 43, 32)

#define ACA_REG_SYND_ERRORINFORMATION(x)	ACA__REG__FIELD(x, 17, 0)

/* NOTE: The following codes refers to the smu header file */
#define ACA_EXTERROR_CODE_CE			0x3a
#define ACA_EXTERROR_CODE_FAULT			0x3b

#define mmSMNAID_XCD0_MCA_SMU 0x36430400	/* SMN AID XCD0 */
#define mmSMNAID_XCD1_MCA_SMU 0x38430400	/* SMN AID XCD1 */
#define mmSMNXCD_XCD0_MCA_SMU 0x40430400	/* SMN XCD XCD0 */
#define mmSMNAID_AID0_MCA_SMU 0x03b30400	/* SMN AID AID0 */

extern const struct ras_aca_ip_func ras_aca_func_v1_0;
#endif
