/** @addtogroup FCI
 * @{
 * @file
 * Declaration of FastCall helper functions.
 *
 * @attention Helper functions are mostly RealView (ARM CC) specific.
 *
 * Holds the functions for SIQ, YIELD and FastCall for switching to the secure world.
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 */

#ifndef MCIFCFUNC_H_
#define MCIFCFUNC_H_

#include "mcifc.h"
/**
 * Execute a secure monitor call (SMC).
 *
 * @param mode SMC mode affects the way SMC is handled
 *
 * @attention This function shall not be used directly. Use N_Siq() or Yield() instead.
 */
__smc(0) void smc(int32_t mode);

/**
 * N-SIQ switch from NWd to SWd.
 * Execution will continue in the SWd. The notification queue will be drained by the MC4 and MC4 system schedules its services.
 */
inline void N_Siq(void) { smc(MC_SMC_N_SIQ); }

/**
 * Yield switch from NWd to SWd.
 * Execution will continue in the SWd without scheduling MC4 services.
 */
inline void Yield(void) { smc(MC_SMC_N_YIELD); }

/** Wrapper structure for parameter passing in registers.
 *  This structure is used as a "wrapper" return value for functions that
 *  return data in the registers r0 to r3. With the RealView compiler such
 *  function are declare as:  _value_in_regs reg_r0_r1_r2_r3_t foo()

 */
typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
} reg_r0_r1_r2_r3_t;

/** Parameterized SMC for FastCalls.
 * @attention This function shall not be used directly.
 */
__smc(0) __value_in_regs  reg_r0_r1_r2_r3_t smcFc(
        uint32_t r0,
        uint32_t r1,
        uint32_t r2,
        uint32_t r3
);

/** FastCall helper function.
 * @attention This function shall not be used directly.
 */
inline static __value_in_regs reg_r0_r1_r2_r3_t fastCall(
        uint32_t r0,
        uint32_t r1,
        uint32_t r2,
        uint32_t r3
) {
    return smcFc(r0,r1,r2,r3);
}

/**
 * Initialize the MobiCore.
 * The FcMc4init FastCall shall be used to set up the MCI. The function passes the message buffers used in the MCI to the MC4 system.
 * As long as the buffers are not set up the MC4 message passing mechanisms (notifications, MCP commands) are not available.
 * NQ buffer and MCP buffer as well as length calculations are described in the "MobiCore4 Driver Interface Specification".
 * <br> The fastCallInit() will not check the parameters for validity. Instead the MC4 will perform a check on first usage of the parameters.
 *
 * @image html DoxyMciBuffer.png "MCI buffer"
 * @image latex DoxyMciBuffer.png "MCI buffer" width=12cm
 *
 * @param base      Physical start address of the MCI buffer. Must be 4kB aligned.
 * @param nqOffset  Offset in bytes  to the beginning of the NQ buffer.
 * @param nqLength  Length of the NQ buffer in bytes.
 * @param mcpOffset Offset in bytes to the beginning of the MCP buffer.
 * @param mcpLength Length of the MCP buffer in bytes
 *
 */
inline static uint32_t fastCallInit(
        uint8_t *base,
        uint32_t  nqOffset,
        uint32_t  nqLength,
        uint32_t  mcpOffset,
        uint32_t  mcpLength
) {

    reg_r0_r1_r2_r3_t ret;

    ret = fastCall(
            MC_FC_INIT,
            (uint32_t)base,
            ((nqOffset << 16) | (nqLength & 0xFFFF)),
            ((mcpOffset << 16) | (mcpLength & 0xFFFF)) );


    return ret.r1;
}


/** Get status information about MobiCore.
 * The FcMcGetInfo FastCall provides information about the current state of the MobiCore.
 * In certain states extended information is provided.
 *
 * @param extInfoId Extended info word to be obtained.
 * @param mc4state Current state of the MobiCore.
 * @param extInfo Extended information depending on state.
 */
inline static uint32_t fastCallGetInfo(
        uint32_t extInfoId,
        uint32_t *mc4state,
        uint32_t *extInfo
) {
    reg_r0_r1_r2_r3_t ret;

    ret = fastCall(MC_FC_INFO,extInfoId,0,0);

    if (MC_FC_RET_OK == ret.r1)
    {
        *mc4state = ret.r2;
        *extInfo = ret.r3;
    }

    return ret.r1;
}

/**
 * Power management.
 * The power management FastCall is platform specific.
 *
 * @param param0  platform specific parameter.
 * @param param1  platform specific parameter.
 * @param param2  platform specific parameter.
 */
inline static uint32_t fastCallPower(
        uint32_t param0,
        uint32_t param1,
        uint32_t param2
) {

    reg_r0_r1_r2_r3_t ret;

    ret = fastCall(
            MC_FC_POWER,
            param0,
            param1,
            param2 );

    return ret.r1;
}

#endif /* MCIFCFUNC_H_ */
/**
 * @}*/
