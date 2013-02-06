/** @addtogroup FCI
 * @{
 * @file
 * FastCall declarations.
 *
 * Holds the functions for SIQ, YIELD and FastCall for switching to the secure world.
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 */
#ifndef MCIFC_H_
#define MCIFC_H_

/** @name MobiCore FastCall Defines
 * Defines for the two different FastCall's.
 */
/** @{ */

// --- global ----
#define MC_FC_INVALID                ((uint32_t)  0 )  /**< Invalid FastCall ID */
#define MC_FC_INIT                   ((uint32_t)(-1))  /**< Initializing FastCall. */
#define MC_FC_INFO                   ((uint32_t)(-2))  /**< Info FastCall. */

// following defines are currently frozen, so they will candidate for later big-change
// --- sleep modes ---
#define MC_FC_SLEEP                  ((uint32_t)(-3))  /**< enter power-sleep */
#define MC_FC_AFTR                   ((uint32_t)(-5))  /**< enter AFTR-sleep (called from core-0) */
// --- wake-up access ---
#define MC_FC_CORE_X_WAKEUP          ((uint32_t)(-4))  /**< wakeup/boot core-x (optional core-number in r1, not "0" ) */
#define MC_FC_C15_RESUME            ((uint32_t)(-11))  /**< Write power control & diag registers */
// --- L2 cache access ---
#define MC_FC_L2X0_CTRL             ((uint32_t)(-21))  /**< Write to L2X0 control register */
#define MC_FC_L2X0_SETUP1           ((uint32_t)(-22))  /**< Setup L2X0 register - part 1 */
#define MC_FC_L2X0_SETUP2           ((uint32_t)(-23))  /**< Setup L2X0 register - part 2 */
#define MC_FC_L2X0_INVALL           ((uint32_t)(-24))  /**< Invalidate all L2 cache */
#define MC_FC_L2X0_DEBUG            ((uint32_t)(-25))  /**< Write L2X0 debug register */
// --- MEM traces ---
#define MC_FC_MEM_TRACE             ((uint32_t)(-31))  /**< Enable SWd tracing via memory */
// --- write access to CP15 regs ---
#define MC_FC_CP15_REG             ((uint32_t)(-101))  /**< general CP15/cache register update */
// --- store value in sDDRRAM ---
#define MC_FC_STORE_BINFO          ((uint32_t)(-201))  /**< write a 32bit value in secure DDRRAM in incremented art (max 2kB) */
#define MC_FC_LOAD_BINFO           ((uint32_t)(-202))  /**< load a 32bit value from secure DDRRAM using an offset */

#define MC_FC_MAX_ID         ((uint32_t)(0xFFFF0000))  /**< Maximum allowed FastCall ID */

// r1 is requested status (0,1,2), on return r2 holds this status value

/** @} */

/** @name MobiCore SMC Defines
 * Defines the different secure monitor calls (SMC) for world switching.
 * @{ */
#define MC_SMC_N_YIELD     0x3     /**< Yield to switch from NWd to SWd. */
#define MC_SMC_N_SIQ       0x4     /**< SIQ to switch from NWd to SWd. */
/** @} */

/** @name MobiCore status
 *  MobiCore status information.
 * @{ */
#define MC_STATUS_NOT_INITIALIZED  0   /**< MobiCore is not yet initialized. FastCall FcInit() has to be used function to set up MobiCore.*/
#define MC_STATUS_BAD_INIT         1   /**< Bad parameters have been passed in FcInit(). */
#define MC_STATUS_INITIALIZED      2   /**< MobiCore did initialize properly. */
#define MC_STATUS_HALT             3   /**< MobiCore kernel halted due to an unrecoverable exception. Further information is available extended info */
/** @} */

/** @name Extended Info Identifiers
 *  Extended info parameters for MC_FC_INFO to obtain further information depending on MobiCore state.
 * @{ */
#define MC_EXT_INFO_ID_MCI_VERSION      0 /**< Version of the MobiCore Control Interface (MCI) */
#define MC_EXT_INFO_ID_FLAGS            1 /**< MobiCore control flags */
#define MC_EXT_INFO_ID_HALT_CODE        2 /**< MobiCore halt condition code */
#define MC_EXT_INFO_ID_HALT_IP          3 /**< MobiCore halt condition instruction pointer */
#define MC_EXT_INFO_ID_FAULT_CNT        4 /**< MobiCore fault counter */
#define MC_EXT_INFO_ID_FAULT_CAUSE      5 /**< MobiCore last fault cause */
#define MC_EXT_INFO_ID_FAULT_META       6 /**< MobiCore last fault meta */
#define MC_EXT_INFO_ID_FAULT_THREAD     7 /**< MobiCore last fault threadid */
#define MC_EXT_INFO_ID_FAULT_IP         8 /**< MobiCore last fault instruction pointer */
#define MC_EXT_INFO_ID_FAULT_SP         9 /**< MobiCore last fault stack pointer */
#define MC_EXT_INFO_ID_FAULT_ARCH_DFSR  10 /**< MobiCore last fault ARM arch information */
#define MC_EXT_INFO_ID_FAULT_ARCH_ADFSR 11 /**< MobiCore last fault ARM arch information */
#define MC_EXT_INFO_ID_FAULT_ARCH_DFAR  12 /**< MobiCore last fault ARM arch information */
#define MC_EXT_INFO_ID_FAULT_ARCH_IFSR  13 /**< MobiCore last fault ARM arch information */
#define MC_EXT_INFO_ID_FAULT_ARCH_AIFSR 14 /**< MobiCore last fault ARM arch information */
#define MC_EXT_INFO_ID_FAULT_ARCH_IFAR  15 /**< MobiCore last fault ARM arch information */
#define MC_EXT_INFO_ID_MC_CONFIGURED    16 /**< MobiCore configured by Daemon via fc_init flag */
#define MC_EXT_INFO_ID_MC_SCHED_STATUS  17 /**< MobiCore scheduling status: idle/non-idle */
#define MC_EXT_INFO_ID_MC_STATUS        18 /**< MobiCore runtime status: initialized, halted */
#define MC_EXT_INFO_ID_MC_EXC_PARTNER   19 /**< MobiCore exception handler last partner */
#define MC_EXT_INFO_ID_MC_EXC_IPCPEER   20 /**< MobiCore exception handler last peer */
#define MC_EXT_INFO_ID_MC_EXC_IPCMSG    21 /**< MobiCore exception handler last IPC message */
#define MC_EXT_INFO_ID_MC_EXC_IPCDATA   22 /**< MobiCore exception handler last IPC data */

/** @} */

/** @name FastCall return values
 * Return values of the MobiCore FastCalls.
 * @{ */
#define MC_FC_RET_OK                       0     /**< No error. Everything worked fine. */
#define MC_FC_RET_ERR_INVALID              1     /**< FastCall was not successful. */
#define MC_FC_RET_ERR_ALREADY_INITIALIZED  5     /**< MobiCore has already been initialized. */
/** @} */

#endif /** MCIFC_H_ */

/** @} */
