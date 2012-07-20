/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifndef CSR_WIFI_VIF_UTILS_H
#define CSR_WIFI_VIF_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

/* STANDARD INCLUDES ********************************************************/

/* PROJECT INCLUDES *********************************************************/
/* including this file for CsrWifiInterfaceMode*/
#include "csr_wifi_private_common.h"

/* MACROS *******************************************************************/

/* Common macros for NME and SME to be used temporarily until SoftMAC changes are made */
#define CSR_WIFI_NUM_INTERFACES        (u8)0x1
#define CSR_WIFI_INTERFACE_IN_USE      (u16)0x0

/* This is used at places where interface Id isn't available*/
#define CSR_WIFI_INTERFACE_ZERO        0
#define CSR_WIFI_INTERFACE_STA         0
#define CSR_WIFI_INTERFACE_AMP         0


#define CSR_WIFI_VIF_UTILS_UNDEFINED_TAG 0xFFFF

/* Extract the Interface Id from the event */
#define CsrWifiVifUtilsGetVifTagFromEvent(msg) \
    ((u16) * ((u16 *) ((u8 *) (msg) + sizeof(CsrWifiFsmEvent))))

/* The HPI Vif combines the type and the interface id */
#define CsrWifiVifUtilsGetVifTagFromHipEvent(msg) \
    ((msg)->virtualInterfaceIdentifier & 0x00FF)

#define CsrWifiVifUtilsPackHipEventVif(type, interfaceId) \
    ((u16)((interfaceId) | ((type) << 8)))


/* TYPES DEFINITIONS ********************************************************/

/* GLOBAL VARIABLE DECLARATIONS *********************************************/

/* PUBLIC FUNCTION PROTOTYPES ***********************************************/

/**
 * @brief
 *     First checks if the mode is supported capability bitmap of the interface.
 *     If this succeeds, then checks if running this mode on this interface is allowed.
 *
 * @param[in] u8 : interface capability bitmap
 * @param[in] u8* : pointer to the array of current interface modes
 * @param[in] u16 : interfaceTag
 * @param[in] CsrWifiInterfaceMode : mode
 *
 * @return
 *     CsrBool : returns true if the interface is allowed to operate in the mode otherwise false.
 */
extern CsrBool CsrWifiVifUtilsCheckCompatibility(u8             interfaceCapability,
                                                 u8            *currentInterfaceModes,
                                                 u16            interfaceTag,
                                                 CsrWifiInterfaceMode mode);

/**
 * @brief
 *     Checks if the specified interface is supported.
 *     NOTE: Only checks that the interface is supported, no checks are made to
 *     determine whether a supported interface may be made active.
 *
 * @param[in] u16 : interfaceTag
 *
 * @return
 *     CsrBool : returns true if the interface is supported, otherwise false.
 */
extern CsrBool CsrWifiVifUtilsIsSupported(u16 interfaceTag);

#ifdef CSR_LOG_ENABLE
/**
 * @brief
 *     Registers the virtual interface utils logging details.
 *     Should only be called once at initialisation.
 *
 * @param[in/out] None
 *
 * @return
 *     None
 */
void CsrWifiVifUtilsLogTextRegister(void);
#else
#define CsrWifiVifUtilsLogTextRegister()
#endif

#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_VIF_UTILS_H */

