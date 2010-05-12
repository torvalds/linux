/*
 * Copyright (c) 2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * @file
 * @brief <b>NVIDIA Driver Development Kit:
 *           NvDDK USB PHY functions</b>
 *
 * @b Description: Defines USB PHY private functions
 *
 */

#ifndef INCLUDED_NVDDK_USBPHY_H
#define INCLUDED_NVDDK_USBPHY_H

#include "nvcommon.h"
#include "nvrm_init.h"
#include "nvos.h"


#if defined(__cplusplus)
extern "C"
{
#endif


/**
 * Opaque handle to a Usb phy device.
 */
typedef struct NvDdkUsbPhyRec *NvDdkUsbPhyHandle;


/**
 * Enum defining USB Phy-specific IOCTL types.
 */
typedef enum
{
    /**
     * Gets the USB VBUS status.
     *
     * @par Inputs:
     * None.
     *
     * @par Outputs:
     * ::NvDdkUsbPhyIoctl_VBusStatusOutputArgs.
     *
     * @retval NvError_Success 
     * @retval NvError_BadParameter Output Argument is invalid.
     */
    NvDdkUsbPhyIoctlType_VBusStatus,

    /**
     * Configures the VBUS Interrupt.
     *
     * @par Inputs:
     * ::NvDdkUsbPhyIoctl_VBusInterruptInputArgs.
     *
     * @par Outputs:
     * None.
     *
     * @retval NvError_Success 
     * @retval NvError_BadParameter Input Argument is invalid.
     */
    NvDdkUsbPhyIoctlType_VBusInterrupt,

    /**
     * Gets the USB ID pin status.
     *
     * @par Inputs:
     * None.
     *
     * @par Outputs:
     * ::NvDdkUsbPhyIoctl_IdPinStatusOutputArgs.
     *
     * @retval NvError_Success 
     * @retval NvError_BadParameter Output Argument is invalid.
     */
    NvDdkUsbPhyIoctlType_IdPinStatus,

    /**
     * Configures the USB Id pin Interrupt.
     *
     * @par Inputs:
     * ::NvDdkUsbPhyIoctl_IdPinInterruptInputArgs.
     *
     * @par Outputs:
     * None.
     *
     * @retval NvError_Success 
     * @retval NvError_BadParameter Input Argument is invalid.
     */
    NvDdkUsbPhyIoctlType_IdPinInterrupt,

    /**
     * Gets the USB Dedicated charger status.
     *
     * @par Inputs:
     * None.
     *
     * @par Outputs:
     * ::NvDdkUsbPhyIoctl_DedicatedChargerStatusOutputArgs.
     *
     * @retval NvError_Success 
     * @retval NvError_BadParameter Output Argument is invalid.
     */
    NvDdkUsbPhyIoctlType_DedicatedChargerStatus,

    /**
     * Configures the USB dedicated charger detection.
     *
     * @par Inputs:
     * ::NvDdkUsbPhyIoctl_DedicatedChargerDetectionInputArgs.
     *
     * @par Outputs:
     * None.
     *
     * @retval NvError_Success 
     * @retval NvError_BadParameter Input Argument is invalid.
     */
    NvDdkUsbPhyIoctlType_DedicatedChargerDetection,

     /**
     * Configures the Busy hints for the USB controller
     *
     * @par Inputs:
    * ::NvDdkUsbPhyIoctl_UsbBusyHintsOnOffInputArgs.
    *
     * @par Outputs:
     * None.
     *
     * @retval NvError_Success 
     * @retval NvError_BadParameter Input Argument is invalid.
     */
    NvDdkUsbPhyIoctlType_UsbBusyHintsOnOff,


    NvDdkUsbPhyIoctlType_Num,
    /**
     * Ignore -- Forces compilers to make 32-bit enums.
     */
    NvDdkUsbPhyIoctlType_Force32 = 0x7FFFFFFF
} NvDdkUsbPhyIoctlType;

/**
 * VBUS status IOCTL output arguments.
 */
typedef struct NvDdkUsbPhyIoctl_VBusStatusOutputArgsRec
{
    // VBUS status: If Set to NV_TRUE VBUS is detected else VBUS is not detected.
    NvBool VBusDetected;
} NvDdkUsbPhyIoctl_VBusStatusOutputArgs;

/**
 * VBUS Interrupt configuration IOCTL input arguments.
 */
typedef struct NvDdkUsbPhyIoctl_VBusInterruptInputArgsRec
{
    // VBUS Interrupt: If Set to NV_TRUE VBUS interrupt is enabled else disabled.
    NvBool EnableVBusInterrupt;
} NvDdkUsbPhyIoctl_VBusInterruptInputArgs;

/**
 * USB Id pin status IOCTL output arguments.
 */
typedef struct NvDdkUsbPhyIoctl_IdPinStatusOutputArgsRec
{
    // VBUS status: If Set to NV_TRUE Id pin is low else Id pin is high.
    NvBool IdPinSetToLow;
} NvDdkUsbPhyIoctl_IdPinStatusOutputArgs;

/*
 * USB Id pin Interrupt configuration IOCTL input arguments.
 */
typedef struct NvDdkUsbPhyIoctl_IdPinInterruptInputArgsRec
{
    // VBUS Interrupt: If Set to NV_TRUE Id pin interrupt is enabled else disabled.
    NvBool EnableIdPinInterrupt;
} NvDdkUsbPhyIoctl_IdPinInterruptInputArgs;


/**
 * USB dedicated charger status IOCTL output arguments.
 */
typedef struct NvDdkUsbPhyIoctl_DedicatedChargerStatusOutputArgsRec
{
    // Dedicated Charger status: If Set to NV_TRUE charger is detected else Id not detected.
    NvBool ChargerDetected;
} NvDdkUsbPhyIoctl_DedicatedChargerStatusOutputArgs;

/*
 * USB dedicated charger configuration IOCTL input arguments.
 */
typedef struct NvDdkUsbPhyIoctl_DedicatedChargerDetectionInputArgsRec
{
    // Charger Interrupt: If Set to NV_TRUE charger interrupt is enabled else disabled.
    NvBool EnableChargerInterrupt;
    // Charger detection: If set to NV_TRUE enables the charger detection else disables.
    NvBool EnableChargerDetection;
} NvDdkUsbPhyIoctl_DedicatedChargerDetectionInputArgs;



/**
 * USB busy hints configuration IOCTL input arguments.
 */
typedef struct NvDdkUsbPhyIoctl_UsbBusyHintsOnOffInputArgsRec
{
    // Busy hints on/off: If set to NV_TRUE enables the busy hintson else disables.
    NvBool OnOff;
    // Requested boost duration in milliseconds.
    // if BoostDurationMs = NV_WAIT_INFINITE, then busy hints will be on untill
    // busy hints are off. This is valid only if OnOff = NV_TRUE
    NvU32 BoostDurationMs;
} NvDdkUsbPhyIoctl_UsbBusyHintsOnOffInputArgs;


/**
 * Opens the Usb Phy, allocates the resources and initializes the phy.
 *
 * @param hRmDevice Handle to the Rm device, which is required to 
 * acquire the resources from RM.
 * @param Instance Instance of specific device.
 * @param hUsbPhy returns the USB phy handle.
 *
 * @retval NvSuccess
 * @retval NvError_Timeout If phy clock is not stable in expected time.
 */
NvError NvDdkUsbPhyOpen(
    NvRmDeviceHandle hRm, 
    NvU32 Instance, 
    NvDdkUsbPhyHandle *hUsbPhy);

/**
 * Power down the Phy safely and release all the resources allocated.
 *
 * @param hUsbPhy Handle acquired during the NvDdkUsbPhyOpen() call.
 *
 */
void NvDdkUsbPhyClose(NvDdkUsbPhyHandle hUsbPhy);

/**
 * Powers up the device. It could be taking out of low power mode or 
 * reinitializing.
 *
 * @param hUsbPhy Handle acquired during the NvDdkUsbPhyOpen() call.
 * @param IsHostMode indicates the host mode or not.
 * @param IsDpd  Deep sleep power up or not .
 *
 * @retval NvSuccess
 * @retval NvError_Timeout If phy clock is not stable in expected time.
 */
NvError NvDdkUsbPhyPowerUp(NvDdkUsbPhyHandle hUsbPhy, NvBool IsHostMode, NvBool IsDpd);

/**
 * Powers down the PHY. It could be low power mode or shutdown.
 *
 * @param hUsbPhy Handle acquired during the NvDdkUsbPhyOpen() call.
 * @param IsHostMode indicates the host mode or not.
 * @param IsDpd Handle Deep sleep power down or not .
 *
 * @retval NvSuccess
 */
NvError NvDdkUsbPhyPowerDown(NvDdkUsbPhyHandle hUsbPhy, NvBool IsHostMode, NvBool IsDpd);

/**
 * Perform an I/O control operation on the device.
 *
 * @param hBlockDev Handle acquired during the NvDdkXxxBlockDevOpen() call.
 * @param IoctlType Type of control operation to perform.
 * @param InputArgs A pointer to input arguments buffer.
 * @param OutputArgs A pointer to output arguments buffer.
 *
 * @retval NvError_Success IOCTL is successful.
 * @retval NvError_NotSupported \a Opcode is not recognized.
 * @retval NvError_InvalidParameter \a InputArgs or \a OutputArgs is
 *   incorrect.
 */
NvError NvDdkUsbPhyIoctl(
    NvDdkUsbPhyHandle hUsbPhy,
    NvDdkUsbPhyIoctlType IoctlType,
    const void *InputArgs,
    void *OutputArgs);

/**
 * Waits until Phy clock is stable.
 *
 * @param hUsbPhy Handle acquired during the NvDdkUsbPhyOpen() call.
 *
 * @retval NvSuccess If phy clock is stable.
 * @retval NvError_Timeout If phy clock is not stable in expected time.
 */
NvError NvDdkUsbPhyWaitForStableClock(NvDdkUsbPhyHandle hUsbPhy);


#if defined(__cplusplus)
}
#endif

/** @}*/
#endif // INCLUDED_NVDDK_USBPHY_H

