/*
 * Copyright (c) 2006-2009 NVIDIA Corporation.
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
 *         Touch Pad Sensor Interface</b>
 *
 * @b Description: Defines the ODM adaptation interface for touch pad sensor devices.
 *
 */

#ifndef INCLUDED_NVODM_TOUCH_H
#define INCLUDED_NVODM_TOUCH_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvodm_services.h"

/**
 * @defgroup nvodm_touch Touch Pad Adaptation Interface
 *
 * This is the touch pad ODM adaptation interface.
 *
 * @ingroup nvodm_adaptation
 * @{
 */


/**
 * Defines an opaque handle that exists for each touch device in the
 * system, each of which is defined by the customer implementation.
 */
typedef struct NvOdmTouchDeviceRec *NvOdmTouchDeviceHandle;

#define NVODM_MAX_INPUT_COORDS 5

/**
 * @brief Defines the gesture type.
 */
typedef enum
{
    /// Indicates the gesture is not supported.
    NvOdmTouchGesture_Not_Supported = 0x0000,

    /// Indicates that there was no gesture recognized.
    NvOdmTouchGesture_No_Gesture = 0x0001,

    /// Indicates the "tap" gesture: one rapid finger press and release.
    NvOdmTouchGesture_Tap = 0x0002,

    /// Indicates the "double tap" gesture: two taps in rapid succession.
    NvOdmTouchGesture_Double_Tap = 0x0004,

    /// Indicates the "tap and hold" gesture: a tap event rapidly followed by press-and-hold.
    NvOdmTouchGesture_Tap_and_Hold = 0x0008,

    /// Indicates the "press" gesture: a finger that presses down and stays on the panel.
    NvOdmTouchGesture_Press = 0x0010,

    /**
     * Indicates the "press and drag" gesture: a finger that presses down, stays
     * on the panel and then moves
     */
    NvOdmTouchGesture_Press_Drag = 0x0020,

    /**
     * Indicates the "zoom" gesture: a simultaneous two-fingered gesture, where
     * the two fingers are either moving towards each other, or moving away from
     * each other.
     */
    NvOdmTouchGesture_Zoom = 0x0040,

    /**
     * Indicates the "Flick" gesture: a "tap and drag" gesture or a fast
     * "press and drag" where the finger stays on the panel only for a short time.
     */
    NvOdmTouchGesture_Flick = 0x0080,

    NvOdmTouchGestureType_Force32 = 0x7fffffffUL
} NvOdmTouchGestureType;


/**
 *  Defines the touch orientation based on the LCD screen.
 */
typedef enum
{
    /// Indicates the touch panel X/Y coords are swapped in relation to LCD screen.
    NvOdmTouchOrientation_XY_SWAP = 0x01,
    /// Indicates the touch panel X/Y coords are horizontally flipped (or mirrored) in relation to LCD screen.
    NvOdmTouchOrientation_H_FLIP = 0x02,
    /// Indicates the touch panel X/Y coords are vertically flipped in relation to LCD screen.
    NvOdmTouchOrientation_V_FLIP = 0x04,

    NvOdmTouchOrientation_Force32 = 0x7fffffffUL
} NvOdmTouchOrientationType;
/**
 *  Defines the touch capabilities.
 */
typedef struct
{
    /// Holds a value indicating whether or not multi-touch is supported: 1 single touch : 0 multi-touch.
    NvBool IsMultiTouchSupported;

    /// Holds the maximum number of finger coordinates that can be reported.
    NvU32 MaxNumberOfFingerCoordReported;

    /// Holds a value indicating whether or not relative data for X/Y is supported: 1  not support : 0 supported.
    NvBool IsRelativeDataSupported;

    /// Holds the maximum value for relative coords that can be reported.
    NvU32 MaxNumberOfRelativeCoordReported;

    /// Holds the maximum width value that can be reported.
    NvU32 MaxNumberOfWidthReported;

    /// Holds the maximum pressure value that can be reported.
    NvU32 MaxNumberOfPressureReported;

    /// Holds a bitmask specifying the supported gestures
    NvU32 Gesture;

    /// Holds a value indicating whether or not finger width is supported.
    NvBool IsWidthSupported;

    /// Holds a value indicating whether finger signal strength or finger contact only is supported.
    NvBool IsPressureSupported;

    /// Holds a value indicating whether or not fingers on the panel are supported.
    NvBool IsFingersSupported;

    /// Holds the minimum X position.
    NvU32 XMinPosition;

    /// Holds the minimum Y position.
    NvU32 YMinPosition;

    /// Holds the maximum X position.
    NvU32 XMaxPosition;

    /// Holds the maximum Y position.
    NvU32 YMaxPosition;

    /// Holds the orientation based on the LCD screen.
    NvU32 Orientation;

} NvOdmTouchCapabilities;


/**
 * @brief Defines the possible touch states.
 */
typedef enum
{
    /// Indicates a valid reading.
    NvOdmTouchSampleValidFlag = 0x1,

    /// Indicates to ignore the sample.
    NvOdmTouchSampleIgnore = 0x2,

    /// Indicates the state of the finger.
    NvOdmTouchSampleDownFlag = 0x4,

    NvOdmTouchSampleFlags_Force32 = 0x7fffffffUL
} NvOdmTouchSampleFlags;


/**
 * Defines the ODM touch-specific functionality information.
 */
typedef struct
{
    /// Indicates x,y-coordinates for multiple input coordinates (fingers).
    NvU32 multi_XYCoords[NVODM_MAX_INPUT_COORDS][2];
    /// Indicates approximate finger width on the touch panel.
    NvU8 width[NVODM_MAX_INPUT_COORDS];
    /// Specifies a bitmask describing the gesture recognized.
    NvU32 Gesture;
    /// Indicates whether or not the gesture was valid.
    NvU8 Confirmed;
    /// Indicates the number of fingers on the touch panel.
    NvU8 Fingers;
    /// Indicates an approximate finger pressure.
    NvU8 Pressure[NVODM_MAX_INPUT_COORDS];
    /// Indicates the relative coordinate information.
    NvS8 XYDelta[NVODM_MAX_INPUT_COORDS][2];

} NvOdmTouchAdditionalInfo;


/**
 * Defines the ODM touch pad coordinate information.
 */
typedef struct
{
    /// Specifies the X-coordinate.
    NvU32 xcoord;
    /// Specifies the Y-coordinate.
    NvU32 ycoord;
    /// Specifies the sample state information.
    NvU32 fingerstate;
    /// Specifies additional functionality information.
    NvOdmTouchAdditionalInfo additionalInfo;
    /// Specifies a pointer to extended data, if needed.
    void* pextrainfo;
    /// Specifies the size of extended data.
    NvU32 extrainfosize;

} NvOdmTouchCoordinateInfo;


/**
 * @brief Defines the structure for the sampling rate.
 */
typedef struct
{
    /// Specifies a low amount of samples per second.
    NvU32 NvOdmTouchSampleRateLow;

    /// Specifies a high amount of samples per second.
    NvU32 NvOdmTouchSampleRateHigh;

    /// Specifies the current sample rate setting, zero is low, 1 is high.
    NvU32 NvOdmTouchCurrentSampleRate;
} NvOdmTouchSampleRate;


/**
 * @brief Defines the power mode for the touch panel.
 */
typedef enum
{
    /// Indicates full power.
    NvOdmTouch_PowerMode_0 = 0x1,
    NvOdmTouch_PowerMode_1,
    NvOdmTouch_PowerMode_2,
    /// Indicates power off.
    NvOdmTouch_PowerMode_3,
    NvOdmTouchPowerMode_Force32 = 0x7fffffffUL
} NvOdmTouchPowerModeType;



/**
 * Gets a handle to the touch pad in the system.
 *
 * @param hDevice A pointer to the handle of the touch pad.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmTouchDeviceOpen( NvOdmTouchDeviceHandle *hDevice );

/**
 * Gets capabilities for the specified touch device.
 *
 * @param hDevice The handle of the touch pad.
 * @param pCapabilities A pointer to the targeted
 *  capabilities returned by the ODM.
 */
void
NvOdmTouchDeviceGetCapabilities(NvOdmTouchDeviceHandle hDevice, NvOdmTouchCapabilities* pCapabilities);

/**
 * Gets coordinate info from the touch device.
 *
 * @param hDevice The handle to the touch pad.
 * @param coord  A pointer to the structure holding coordinate info.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmTouchReadCoordinate( NvOdmTouchDeviceHandle hDevice, NvOdmTouchCoordinateInfo *coord);


/**
 * Hooks up the interrupt handle to the GPIO interrupt and enables the interrupt.
 *
 * @param hDevice The handle to the touch pad.
 * @param hInterruptSemaphore A handle to hook up the interrupt.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmTouchEnableInterrupt(NvOdmTouchDeviceHandle hDevice, NvOdmOsSemaphoreHandle hInterruptSemaphore);

/**
 * Prepares the next interrupt to get notified from the touch device.
 *
 * @param hDevice A handle to the touch pad.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmTouchHandleInterrupt(NvOdmTouchDeviceHandle hDevice);

/**
 * Gets the touch ADC sample rate.
 *
 * @param hDevice A handle to the touch ADC.
 * @param pTouchSampleRate A pointer to the NvOdmTouchSampleRate stucture.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
*/
NvBool
NvOdmTouchGetSampleRate(NvOdmTouchDeviceHandle hDevice, NvOdmTouchSampleRate* pTouchSampleRate);


/**
 * Sets the touch ADC sample rate.
 *
 * @param hDevice A handle to the touch ADC.
 * @param SampleRate 1 indicates high frequency, 0 indicates low frequency.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
*/
NvBool
NvOdmTouchSetSampleRate(NvOdmTouchDeviceHandle hDevice, NvU32 SampleRate);


/**
 * Sets the touch panel power mode.
 *
 * @param hDevice A handle to the touch ADC.
 * @param mode The mode, ranging from full power to power off.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
*/
NvBool
NvOdmTouchPowerControl(NvOdmTouchDeviceHandle hDevice, NvOdmTouchPowerModeType mode);

/**
 * Powers the touch device on or off.
 *
 * @param hDevice A handle to the touch ADC.
 * @param OnOff  Specify 1 to power on; 0 to power off.
*/
void
NvOdmTouchPowerOnOff(NvOdmTouchDeviceHandle hDevice, NvBool OnOff);

/**
 *  Releases the touch pad handle.
 *
 * @param hDevice The touch pad handle to be released. If
 *     NULL, this API has no effect.
 */
void NvOdmTouchDeviceClose(NvOdmTouchDeviceHandle hDevice);

/**
 * Gets the touch ODM debug configuration.
 *
 * @param hDevice A handle to the touch pad.
 *
 * @return NV_TRUE if debug message is enabled, or NV_FALSE if not.
 */
NvBool
NvOdmTouchOutputDebugMessage(NvOdmTouchDeviceHandle hDevice);

/**
 * Gets the touch panel calibration data.
 * This is optional as calibration may perform after the OS is up.
 * This is not required to bring up the touch panel.
 *
 * @param hDevice A handle to the touch panel.
 * @param NumOfCalibrationData Indicates the number of calibration points.
 * @param pRawCoordBuffer The collection of X/Y coordinate data.
 *
 * @return NV_TRUE if preset calibration data is required, or NV_FALSE otherwise.
 */
NvBool
NvOdmTouchGetCalibrationData(NvOdmTouchDeviceHandle hDevice, NvU32 NumOfCalibrationData, NvS32* pRawCoordBuffer);
#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_TOUCH_H
