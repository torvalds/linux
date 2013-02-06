/**
 * Header file of MobiCore Driver Kernel Module.
 *
 * @addtogroup MobiCore_Driver_Kernel_Module
 * @{
 * Internal structures of the McDrvModule
 * @file
 *
 * MobiCore Fast Call interface
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_DRV_MODULE_FC_H_
#define _MC_DRV_MODULE_FC_H_

#include "mcDrvModule.h"

/**
 * MobiCore SMCs
 */
enum mcSmcCodes {
	MC_SMC_N_YIELD  = 0x3, /**< Yield to switch from NWd to SWd. */
	MC_SMC_N_SIQ    = 0x4  /**< SIQ to switch from NWd to SWd. */
};

/**
 * MobiCore fast calls. See MCI documentation
 */
enum mcFastCallCodes {
	MC_FC_INIT      = -1,
	MC_FC_INFO      = -2,
	MC_FC_POWER     = -3,
	MC_FC_DUMP      = -4,
	MC_FC_NWD_TRACE = -31 /**< Mem trace setup fastcall */
};

/**
 * return code for fast calls
 */
enum mcFastCallsResult {
	MC_FC_RET_OK                       = 0,
	MC_FC_RET_ERR_INVALID              = 1,
	MC_FC_RET_ERR_ALREADY_INITIALIZED  = 5
};



/*------------------------------------------------------------------------------
	structure wrappers for specific fastcalls
------------------------------------------------------------------------------*/

/** generic fast call parameters */
union fcGeneric {
	struct {
		uint32_t cmd;
		uint32_t param[3];
	} asIn;
	struct {
		uint32_t resp;
		uint32_t ret;
		uint32_t param[2];
	} asOut;
};


/** fast call init */
union mcFcInit {
	union fcGeneric asGeneric;
	struct {
		uint32_t cmd;
		uint32_t base;
		uint32_t nqInfo;
		uint32_t mcpInfo;
	} asIn;
	struct {
		uint32_t resp;
		uint32_t ret;
		uint32_t rfu[2];
	} asOut;
};


/** fast call info parameters */
union mcFcInfo {
	union fcGeneric asGeneric;
	struct {
		uint32_t cmd;
		uint32_t extInfoId;
		uint32_t rfu[2];
	} asIn;
	struct {
		uint32_t resp;
		uint32_t ret;
		uint32_t state;
		uint32_t extInfo;
	} asOut;
};


/** fast call S-Yield parameters */
union mcFcSYield {
	union fcGeneric asGeneric;
	struct {
		uint32_t cmd;
		uint32_t rfu[3];
	} asIn;
	struct {
		uint32_t resp;
		uint32_t ret;
		uint32_t rfu[2];
	} asOut;
};


/** fast call N-SIQ parameters */
union mcFcNSIQ {
	union fcGeneric asGeneric;
	struct {
		uint32_t cmd;
		uint32_t rfu[3];
	} asIn;
	struct {
		uint32_t resp;
		uint32_t ret;
		uint32_t rfu[2];
	} asOut;
};


/*----------------------------------------------------------------------------*/
/**
 * fast call to MobiCore
 *
 * @param pFcGeneric pointer to fast call data
 */
static inline void mcFastCall(
	union fcGeneric *pFcGeneric
)
{
	MCDRV_ASSERT(pFcGeneric != NULL);
	/* We only expect to make smc calls on CPU0 otherwise something wrong
	 * will happen */
	MCDRV_ASSERT(raw_smp_processor_id() == 0);
	dsb();
#ifdef MC_SMC_FASTCALL
	{
		int ret = 0;
		MCDRV_DBG("Going into SCM()");
		ret = smc_fastcall((void *)pFcGeneric, sizeof(*pFcGeneric));
		MCDRV_DBG("Coming from SCM, scm_call=%i, resp=%d/0x%x\n",
			ret,
			pFcGeneric->asOut.resp, pFcGeneric->asOut.resp);
	}
#else
	{
		/* SVC expect values in r0-r3 */
		register u32 reg0 __asm__("r0") = pFcGeneric->asIn.cmd;
		register u32 reg1 __asm__("r1") = pFcGeneric->asIn.param[0];
		register u32 reg2 __asm__("r2") = pFcGeneric->asIn.param[1];
		register u32 reg3 __asm__("r3") = pFcGeneric->asIn.param[2];

		/* one of the famous preprocessor hacks to stingitize things.*/
#define __STR2(x)   #x
#define __STR(x)    __STR2(x)

		/* compiler does not support certain instructions
		"SMC": secure monitor call.*/
#define ASM_ARM_SMC         0xE1600070
		/*   "BPKT": debugging breakpoint. We keep this, as is comes
				quite handy for debugging. */
#define ASM_ARM_BPKT        0xE1200070
#define ASM_THUMB_BPKT      0xBE00


		__asm__ volatile (
			".word " __STR(ASM_ARM_SMC) "\n"
			: "+r"(reg0), "+r"(reg1), "+r"(reg2), "+r"(reg3)
		);

		/* set response */
		pFcGeneric->asOut.resp     = reg0;
		pFcGeneric->asOut.ret      = reg1;
		pFcGeneric->asOut.param[0] = reg2;
		pFcGeneric->asOut.param[1] = reg3;
	}
#endif
}


/*----------------------------------------------------------------------------*/
/**
 * convert fast call return code to linux driver module error code
 *
 */
static inline int convertFcRet(
	uint32_t sret
)
{
	int         ret = -EFAULT;

	switch (sret) {

	case MC_FC_RET_OK:
		ret = 0;
		break;

	case MC_FC_RET_ERR_INVALID:
		ret = -EINVAL;
		break;

	case MC_FC_RET_ERR_ALREADY_INITIALIZED:
		ret = -EBUSY;
		break;

	default:
		break;
	} /* end switch( sret ) */
	return ret;
}

#endif /* _MC_DRV_MODULE_FC_H_ */
/** @} */
