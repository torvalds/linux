/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
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
 * <b>NVIDIA Tegra ODM Kit:
 *         GPIO Query Interface</b>
 *
 * @b Description: Defines the ODM query interface for GPIO pins.
 */

#ifndef INCLUDED_NVODM_QUERY_GPIO_H
#define INCLUDED_NVODM_QUERY_GPIO_H
/**
 * @defgroup nvodm_gpio GPIO Query Interface
 * This is the ODM query interface for GPIO configurations.
 * @ingroup nvodm_query
 * @{
 */
#include "nvcommon.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * Defines GPIO pin groups.
 */
typedef enum
{
    /// Specifies a NULL display group.
    NvOdmGpioPinGroup_None = 0,

    /// Specifies a display pin group.
    NvOdmGpioPinGroup_Display,

    /// Specifies a keypad column pin group--used only if the system uses
    /// GPIO-based keypad.
    NvOdmGpioPinGroup_keypadColumns,

    /// Specifies a keypad rows pin group--used only if the system uses
    /// GPIO-based keypad.
    NvOdmGpioPinGroup_keypadRows,

    /// Specifies a special key for a keypad--This is used in both KBC based
    /// keypad and GPIO-based keypad.
    NvOdmGpioPinGroup_keypadSpecialKeys,

    /// Specifies a pin group representing all the other keypad GPIOs.
    NvOdmGpioPinGroup_keypadMisc,

    /// Specifies an SDIO pin group. This pin group has 2 instances.
    /// @note If this value is set, NvOdmQuerySdioInterfaceProperty::IsCardRemovable
    /// is ignored.
    NvOdmGpioPinGroup_Sdio,

    /// Specifies an HSMMC pin group.
    /// @note If this value is set, NvOdmQueryHsmmcInterfaceProperty::IsCardRemovable
    /// is ignored.
    NvOdmGpioPinGroup_Hsmmc,

    /// Specifies a USB pin group.
    NvOdmGpioPinGroup_Usb,

    /// Specifies an IDE function pin group.
    NvOdmGpioPinGroup_Ide,

    /// Specifies an OEM pin group.
    NvOdmGpioPinGroup_OEM,

    /// Specifies a test pin group used by the internal tests.
    NvOdmGpioPinGroup_Test,

    /// Specifies a group used by the external MIO Ethernet adapter.
    NvOdmGpioPinGroup_MioEthernet,

    /// Deprecated name -- retained for backward compatibility.
    NvOdmGpioPinGroup_Ethernet = NvOdmGpioPinGroup_MioEthernet,

    /// Specifies a group used for NAND flash write protect.
    NvOdmGpioPinGroup_NandFlash,

    /// Specifies a group used for scroll wheel pins.
    NvOdmGpioPinGroup_ScrollWheel,

    /// Specifies a group used for MIO bus control signals.
    NvOdmGpioPinGroup_Mio,

    /// Specifies a group used for bluetooth control signals.
    NvOdmGpioPinGroup_Bluetooth,

    /// Specifies a group used for WLAN control signals.
    NvOdmGpioPinGroup_Wlan,

    /// Specifies a group for HDMI.
    NvOdmGpioPinGroup_Hdmi,

    /// Specifies a group for CRT.
    NvOdmGpioPinGroup_Crt,

    /// Specifies a group for SPI.
    NvOdmGpioPinGroup_SpiEthernet,

    /// Deprecated name -- retained for backward compatibility.
    NvOdmGpioPinGroup_Spi = NvOdmGpioPinGroup_SpiEthernet,

    /// Specifies a group for Vi.
    NvOdmGpioPinGroup_Vi,

    /// Specifies a group for DSI.
    NvOdmGpioPinGroup_Dsi,

    
    /// Specifies a group for keys used to suspend/resume/shutdown.
    NvOdmGpioPinGroup_Power,

    /// Specifies a group for keys used to resume from EC keyboard.
    NvOdmGpioPinGroup_WakeFromECKeyboard,

    /// Specifies the total number of pin groups.
    NvOdmGpioPinGroup_Num,
    NvOdmGpioPinGroup_Force32 = 0x7FFFFFFF,
} NvOdmGpioPinGroup;

/**  @name Display GPIO Pins
 *  Each panel uses some number of GPIO pins for configuring the panel. The
 *  usage and the number of pins vary from panel to panel. One main board can
 *  support multiple panels. In such cases, GPIOs used by all the panels must
 *  be reserved. GPIO pin table exported by display pin group is a set of all
 *  the panels it supports--one set for each panel. The client of this API is
 *  the display ODM adaptation. Display ODM adaptation is written once for a
 *  panel and all the board-specific changes are abstratced in this API.
 *
 *  If the ODM implements its own display adaptation, rather than the using the
 *  display adaptation provided by NVIDIA, there is no need to implement
 *  display virtual pin map.
 *
 *  Please refer to the documentation for the mapping between physical panel
 *  and panel index used here.
 */
/*@{*/
#define NvOdmGpioPin_DisplayPanel0Pincount   (6)
#define NvOdmGpioPin_DisplayPanel0Start      (0)
#define NvOdmGpioPin_DisplayPanel0End        (NvOdmGpioPin_DisplayPanel0Start + NvOdmGpioPin_DisplayPanel0Pincount - 1)

#define NvOdmGpioPin_DisplayPanel1Pincount   (4)
#define NvOdmGpioPin_DisplayPanel1Start      (NvOdmGpioPin_DisplayPanel0End + 1)
#define NvOdmGpioPin_DisplayPanel1End        (NvOdmGpioPin_DisplayPanel1Start + NvOdmGpioPin_DisplayPanel1Pincount - 1)

#define NvOdmGpioPin_DisplayPanel2Pincount   (1)
#define NvOdmGpioPin_DisplayPanel2Start      (NvOdmGpioPin_DisplayPanel1End + 1)
#define NvOdmGpioPin_DisplayPanel2End        (NvOdmGpioPin_DisplayPanel2Start + NvOdmGpioPin_DisplayPanel2Pincount - 1)

#define NvOdmGpioPin_DisplayPanel3Pincount   (21)
#define NvOdmGpioPin_DisplayPanel3Start      (NvOdmGpioPin_DisplayPanel2End + 1)
#define NvOdmGpioPin_DisplayPanel3End        (NvOdmGpioPin_DisplayPanel3Start + NvOdmGpioPin_DisplayPanel3Pincount - 1)

#define NvOdmGpioPin_DisplayPanel4Pincount   (1)
#define NvOdmGpioPin_DisplayPanel4Start      (NvOdmGpioPin_DisplayPanel3End + 1)

#define NvOdmGpioPin_DisplayPanel4End        (NvOdmGpioPin_DisplayPanel4Start + NvOdmGpioPin_DisplayPanel4Pincount - 1)

#define NvOdmGpioPin_DisplayPanel5Pincount   (4)
#define NvOdmGpioPin_DisplayPanel5Start      (NvOdmGpioPin_DisplayPanel4End + 1)

#define NvOdmGpioPin_DisplayPanel5End        (NvOdmGpioPin_DisplayPanel5Start + NvOdmGpioPin_DisplayPanel5Pincount - 1)

#define NvOdmGpioPin_DisplayPinCount         (NvOdmGpioPin_DisplayPanel5End + 1)

/*@}*/
/**@name Keypad Virtual Pins */
/*@{*/
/** Max GPIOs that can be used for as rows in GPIO-based keypad. For chips
 * later than AP15, silicon supports a dedicated KBC controller.
 * If using GPIO-based keypad driver, it has the upper limit on the number of
 * the rows defined by this macro.
 * When the NvOdmQueryGpioPinMap() is called with virtual pin group of
 * ::NvOdmGpioPinGroup_keypadColumns, ODM should return an array
 * of GPIOs mapped for column keys. Array size should not be more than 32. This
 * upper limit is just for saftey checks and in no sense inidicates the
 * limitations of the NVIDIA driver. */
#define NvOdmGpioPin_keypadColumnsPinCountMax       (32)

/** Max GPIOs that can be used for as rows in GPIO  based keypad. For chips
 * later than AP15, silicon supports a dedicated KBC controller.
 * If using GPIO-based keypad driver, it has the upper limit on the number of
 * the rows defined by this macro.
 * When NvOdmQueryGpioPinMap() is called with virtual pin group of
 * ::NvOdmGpioPinGroup_keypadRows, ODM should return a array
 * of GPIOs mapped for row keys. Array size should not be more than
 * ::NvOdmGpioPin_keypadRowsPinCountMax.
 * This upper limit is just for saftey checks and does not inidicate the
 * limitations of the NVIDIA driver. */
#define NvOdmGpioPin_keypadRowsPinCountMax          (32)

/** Max GPIOs that can be used for the special keys. Driver is limited by this
 * number of special keys. When NvOdmQueryGpioPinMap() is called with virtual
 * pin group of ::NvOdmGpioPinGroup_keypadSpecialKeys, ODM should return an array
 * of GPIOs mapped for special keys. Array size should not be more than 32. This
 * upper limit is just for saftey checks and in no sense inidicates the
 * limitations of the NVIDIA driver. */
#define NvOdmGpioPin_keypadSpecialKeysCountMax      (32)

/*@}*/
/** @name Misc Keypad GPIOs */
/*@{*/
/** GPIO used to control illuminating the keypad. This GPIO line is set to
 * active state when a key is pressed. */
#define NvOdmGpioPin_keypadMiscBackLight         (0)
/** When this key is in pressed state all the inputs are disabled. */
#define NvOdmGpioPin_keypadMiscHoldKey           (NvOdmGpioPin_keypadMiscBackLight + 1)
/** Total count pin count for keypad pin group */
#define NvOdmGpioPin_keypadMiscPinCount          (NvOdmGpioPin_keypadMiscHoldKey + 1)

/*@}*/
/** @name HSMMC Virtual Pins */
/*@{*/
#define NvOdmGpioPin_HsmmcCardDetect        (0)
#define NvOdmGpioPin_HsmmcWriteProtect      (NvOdmGpioPin_HsmmcCardDetect + 1)
#define NvOdmGpioPin_HsmmcPinCount          (NvOdmGpioPin_HsmmcWriteProtect + 1)

/*@}*/
/** @name SDIO Virtual Pins */
/*@{*/
#define NvOdmGpioPin_SdioCardDetect         (0)
#define NvOdmGpioPin_SdioWriteProtect       (NvOdmGpioPin_SdioCardDetect +1)
#define NvOdmGpioPin_SdioPinCount           (NvOdmGpioPin_SdioWriteProtect + 1)

/*@}*/
/** @name USB GPIO Pins */
/*@{*/
#define NvOdmGpioPin_UsbCableId    (0)
#define NvOdmGpioPin_UsbPinCount           (NvOdmGpioPin_UsbCableId + 1)

/*@}*/
/** @name IDE Function GPIO Pins */
/*@{*/
#define NvOdmGpioPin_IdePowerEnable (0)
#define NvOdmGpioPin_IdePinCount (NvOdmGpioPin_IdePowerEnable + 1)

/*@}*/
/** @name Test Pin Groups */
/*@{*/
#define NvOdmGpioPin_Test1                  (0)
#define NvOdmGpioPin_Test2                  (1)
#define NvOdmGpioPin_TestPinCount           (NvOdmGpioPin_Test2 + 1)

/*@}*/
/** @name External Ethernet Adapter Pin Groups */
/*@{*/
#define NvOdmGpioPin_Ethernet               (0)
#define NvOdmGpioPin_EthernetCount          (NvOdmGpioPin_Ethernet + 1)

/*@}*/
/** @name NAND Flash WP Pin Groups */
/*@{*/
#define NvOdmGpioPin_NandFlash              (0)
#define NvOdmGpioPin_NandFlashCount         (NvOdmGpioPin_NandFlash + 1)

/*@}*/
/** @name Scroll Wheel Pin Groups */
/*@{*/
#define NvOdmGpioPin_ScrollWheelInputPin1   (0)
#define NvOdmGpioPin_ScrollWheelOnOff       (NvOdmGpioPin_ScrollWheelInputPin1 + 1)
#define NvOdmGpioPin_ScrollWheelSelectPin   (NvOdmGpioPin_ScrollWheelOnOff + 1)
#define NvOdmGpioPin_ScrollWheelInputPin2   (NvOdmGpioPin_ScrollWheelSelectPin + 1)
/*@}*/
/** @name Bluetooth Control Pin Groups */
/*@{*/
#define NvOdmGpioPin_Bluetooth   (0)
#define NvOdmGpioPin_BluetoothReset        (NvOdmGpioPin_Bluetooth + 1)

/*@}*/
/** @name WLAN Control Pin Groups */
/*@{*/
#define NvOdmGpioPin_Wlan   (0)
#define NvOdmGpioPin_WlanPower             (NvOdmGpioPin_Wlan + 1)
#define NvOdmGpioPin_WlanReset             (NvOdmGpioPin_WlanPower + 1)

/*@}*/
/** @name DSI Pin Groups */
/*@{*/
#define NvOdmGpioPin_DsiLcdResetId               (0)
#define NvOdmGpioPin_DsiLcdTeId          (NvOdmGpioPin_DsiLcdResetId + 1)
#define NvOdmGpioPin_DsiLcdHsIntId          (NvOdmGpioPin_DsiLcdTeId + 1)
/*@}*/

/**
 *  Defines the active state of the pin. For example, a USB cable connect pin
 *  might be configured to have a active state of low when the cable is
 *  connetced. On some boards the same pin can be configured as active high.
 *  This enum abstracts this information.
 */
typedef enum
{
    NvOdmGpioPinActiveState_Low = 0,
    NvOdmGpioPinActiveState_High,
    NvOdmGpioPinActiveState_Force32 = 0x7FFFFFFF,
} NvOdmGpioPinActiveState;

/**
 * Holds the GPIO pin information.
 */
typedef struct  NvOdmGpioPinInfo_t {
    /// Holds the physical port mapped to the virtual pin group \c vGroup/virtual pin \c vPin.
    NvU32 Port;
    /// Holds the physical pin mapped to the virtual pin group \c vGroup/virtual pin \c vPin.
    NvU32 Pin;
    /// Holds the active state of the pin. This is valid only for the input pins. Active
    /// state is defined by each pin. For example, for a USB cable connect virtual pin,
    /// the active state is when the cable is connected.
    NvOdmGpioPinActiveState activeState;
} NvOdmGpioPinInfo;

#define NVODM_GPIO_INVALID_PORT 0xFF
#define NVODM_GPIO_INVALID_PIN  0xFF

/// Connected imager devices use the camera reserved GPIOs.
/// Valid GPIO pins are between 0 and 6, and map to the external
/// pins referred to as VGP0 thru VGP6, with the exception of 1 and 2.
/// VGP1 and VGP2 are used for the camera I2C, so the VD10 and VD11 pins
/// are substituted, via this interface, so as to avoid accidental use.
#define NVODM_GPIO_CAMERA_PORT  0xFE

/**
 * Gets the pin mappings for a virtual group. For optimal access the table should be
 * sorted using the vPin value.
 *
 * @see NvOdmGpioPinGroup
 *
 * @param Group The pin group for which the query is being made.
 * @param instance The instance of the pin group. For example, there are 2 instances
 * of the SDIO pin group.
 * @param count A pointer to the count of entires in the ::NvOdmGpioPinInfo.
 *
 * @return A const pointer to the pin info table if the pin group
 * has valid GPIO configuration.
 */
const NvOdmGpioPinInfo *NvOdmQueryGpioPinMap(NvOdmGpioPinGroup Group, NvU32 instance, NvU32 *count);

#if defined(__cplusplus)
}
#endif

/** @} */

#endif  // INCLUDED_NVODM_QUERY_GPIO_H
