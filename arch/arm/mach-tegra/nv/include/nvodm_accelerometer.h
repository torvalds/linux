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
 *         Accelerometer Interface</b>
 *
 * @b Description: Defines the ODM interface for accelerometer devices.
 *
 */

#ifndef INCLUDED_NVODM_ACCELEROMETER_H
#define INCLUDED_NVODM_ACCELEROMETER_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvodm_services.h"
#include "nvassert.h"

/**
 * @defgroup nvodm_accelerometer Accelerometer Adapation Interface
 *
 * This is the accelerometer ODM adaptation interface.  Currently, only 3-axis
 * accelerometers are supported by this interface.
 *
 * This section shows the calls made by the NVIDIA&reg; Driver Development Kit
 * (DDK) accelerometer. 
 *
 * @par Physical Accelerometer
 * 
 * All applications share the same physical accelerometer.
 *
 * @par Sample Rate
 *
 * Every application has its own sample rate in Hz (samples/second).
 * You can set the sample rate with the NvOdmAccelSetSampleRate() function,
 * or you can request the sample rate using the NvOdmAccelGetSampleRate()
 * function.
 *
 *
 * @par	Motion/Tap Interrupt Trigger
 * 
 * Applications can decide to accept motion/tap interrupts in NvOdmAccelOpen().
 * The motion/tap interrupt threshold is set by the driver. The application
 * can get a message queue name by \c NvOdmAccelOpen, and then use the name to
 * create a Windows message queue. Then the application can read interrupt
 * information from the queue.
 *
 * See also <a class="el" href="group__nvodm__example__accel.html">Examples: 
 * Accelerometer</a>
 * 
 * @ingroup nvodm_adaptation
 * @{
 */
/**
 *  @brief Opaque handle to the vibrate device.
 */
typedef struct NvOdmAccelRec *NvOdmAcrDeviceHandle;

/**
 * Defines interrupt events that accelerometers may generate during
 * operation.
 */

typedef enum
{
    /// Indicates that no interrupt has been generated (this value is returned
    /// when interrupt time-outs occur).
    NvOdmAccelInt_None = 0,

    /// Indicates that an interrupt has been generated due to motion across
    /// any axis crossing the specified threshold level.
    NvOdmAccelInt_MotionThreshold,

    /// Indicates that an interrupt has been generated due to a swinging
    /// (forward and back motion) ocurring within the specified time threshold.
    NvOdmAccelInt_TapThreshold,

    /// Indicates that an interrupt has been generated due to detection of
    /// linear freefall motion.
    NvOdmAccelInt_Freefall,

    NvOdmAccelInt_Num,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmAccelInt_Force32 = 0x7fffffffUL,
} NvOdmAccelIntType;

/**
 * Defines axis types for accelerometer. Interrupts are trigger by the axis.
 * An interrupt is triggered for enabled interrupts whenever a forced value
 * on an axis is greater than the threshold.
 */
typedef enum {
    NvOdmAccelAxis_None = 0x0,
    NvOdmAccelAxis_X = 0x1,
    NvOdmAccelAxis_Y = 0x2,
    NvOdmAccelAxis_Z = 0x4,
    NvOdmAccelAxis_All = 0x7,
    NvOdmAccelAxis_Force32 = 0x7fffffffUL,
} NvOdmAccelAxisType;

/**
 * Defines the accelerometer power state.
 */
typedef enum {
   /// Specifies the accelerometer is working normally -- sample rate is high.
   NvOdmAccelPower_Fullrun = 0,
   /// Specifies the accelerometer is working normally -- sample rate is lower
   /// than \c NvOdmAccelPower_Fullrun.
   NvOdmAccelPower_Low,
   /// Specifies the accelerometer is not working, but the power supply is there.
   NvOdmAccelPower_Standby,
   /// Specifies the accelerometer is not working, and there is no power supply
   /// to the device.
   NvOdmAccelPower_Off,
   NvOdmAccelPower_None,
   /// Ignore -- Forces compilers to make 32-bit enums.
   NvOdmAccelPower_Force32 =0x7fffffffUL,
} NvOdmAccelPowerType;

/**
 * Holds device-specific accelerometer capabilities.
 */
typedef struct NvOdmAccelCapsRec
{
    ///  Holds the maximum force in g-force (\em g) registered by this
    ///  accelerometer.
    ///  The value is in increments of 1000. For example, when the maximum
    ///  force is 2 \em g, the value should return 2000.
    NvU32 MaxForceInGs;

    ///  Holds the size of the register for the g-force values in bits.
    ///  This is to specify the resolution of the force value range.
    NvU32 ForceResolution;

    ///  Holds the number of motion thresholds that clients may use to generate
    ///  interrupts. 0 indicates that no threshold motion interrupts
    ///  are supported.
    NvU32 NumMotionThresholds;

    ///  Holds the maximum amount of time in microseconds (Usecs) that may
    ///  be specified as the threshold for a tap-style interrupt. 0
    ///  indicates that tap interrupts are not supported by the accelerometer.
    NvU32 MaxTapTimeDeltaInUs;

    ///  Holds TRUE if the accelerometer can generate an interrupt when
    ///  linear free-fall motion is detected.
    NvBool SupportsFreefallInt;

    ///  Holds the maximum sample rate the accelerometer supports.
    NvU32 MaxSampleRate;

    ///  Holds the minimum sample rate the accelerometer supports.
    NvU32 MinSampleRate;
} NvOdmAccelerometerCaps;

///  Opaque handle to an accelerometer object.
typedef struct NvOdmAccelRec *NvOdmAccelHandle;


/**
 * Initializes the accelerometer and allocates resources used by the ODM
 * adaptation driver.
 *
 * @return A handle to the accelerometer if initialization is successful, or
 *         NULL if unsuccessful or no accelerometer exists.
 */
NvBool
NvOdmAccelOpen(NvOdmAccelHandle* hDevice);

/**
 * Disables the accelerometer and frees any resources used by the driver.
 *
 * @param hDevice The accelerometer handle.
 */
void
NvOdmAccelClose(NvOdmAccelHandle hDevice);

/**
 * Sets the threshold value in g-force (\em g) for interrupt types that are
 * triggered at g-force thresholds, such as NvOdmAccelInt_MotionThreshold().
 * The threshold is applied to all 3 axes on the accelerometer. This does not
 * enable or disable the specified interrupt.
 *
 * @param hDevice The accelerometer handle.
 * @param IntType The type of interrupt being configured (::NvOdmAccelIntType).
 * @param IntNum For accelerometers that support multiple interrupt thresholds
 *               (::NvOdmAccelerometerCaps), specifies which threshold to
 *               configure. If the accelerometer supports a single threshold for
 *               the specified interrupt type, this parameter should be 0.
 * @param Threshold The desired threshold value, in g-forces. If this value is
 *                  outside of the accelerometer's supported range, it will be
 *                  clamped to the maximum supported value. If the accelerometer
 *                  does not have enough precision to support the exact value
 *                  specified, the threshold will be rounded to the nearest
 *                  supported value. The value is by increments of 1000.
 *                  For example, when the maximum force is 2 \em g, the value
 *                  should return 2000.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmAccelSetIntForceThreshold(NvOdmAccelHandle  hDevice,
                               NvOdmAccelIntType IntType,
                               NvU32             IntNum,
                               NvU32             Threshold);

/**
 * Sets the threshold value in microseconds (Usecs) for interrupt types that
 * are triggered at time thresholds. This does not enable or disable the
 * specified interrupt.
 *
 * Sets the threshold value in g-force (\em g) for interrupt types that are
 * triggered at g-force thresholds, such as NvOdmAccelInt_MotionThreshold().
 * The threshold is applied to all 3 axes on the accelerometer. This does not
 * enable or disable the specified interrupt.
 *
 * @param hDevice The accelerometer handle.
 * @param IntType The type of interrupt being configured (::NvOdmAccelIntType).
 * @param IntNum For accelerometers that support multiple interrupt thresholds
 *               (::NvOdmAccelerometerCaps), specifies which threshold to
 *               configure. If the accelerometer supports a single threshold for
 *               the specified interrupt type, this parameter should be 0.
 * @param Threshold The desired threshold value in microseconds. If this value
 *                  is outside of the accelerometer's supported range, it will be
 *                  clamped to the maximum supported value.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmAccelSetIntTimeThreshold(NvOdmAccelHandle  hDevice,
                              NvOdmAccelIntType IntType,
                              NvU32             IntNum,
                              NvU32             Threshold);


/**
 * Enables/disables the specified interrupt source. If the interrupt
 * thresholds were not set prior to enabling the interrupt, the ODM-defined
 * default values are used. If enabling a previously-enabled interrupt,
 * or disabling a previously-disabled interrupt, this function returns
 * silently.
 *
 * @param hDevice The accelerometer handle.
 * @param IntType The type of interrupt being configured (::NvOdmAccelIntType).
 * @param IntAxis The axis interrupt type (::NvOdmAccelAxisType).
 * @param IntNum For accelerometers that support multiple interrupt thresholds
 *               (::NvOdmAccelerometerCaps), specifies which threshold to
 *               configure. If the accelerometer supports a single threshold for
 *               the specified interrupt type, this parameter should be 0.
 * @param Toggle NV_TRUE specifies to enable the interrupt source, NV_FALSE to
 *               disable.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmAccelSetIntEnable(NvOdmAccelHandle  hDevice,
                           NvOdmAccelIntType  IntType,
                           NvOdmAccelAxisType IntAxis,
                           NvU32              IntNum,
                           NvBool             Toggle);

/**
 * Waits for any enabled interrupt, and returns the type of interrupt to the
 * caller. If multiple interrupts occur simultaneously, returns each
 * separately.
 *
 * @param hDevice The accelerometer handle.
 * @param IntType The type of the interrupt that has been generated
 *         (::NvOdmAccelIntType).  If no interrupt occurs before the timeout
 *         interval expires, or no interrupts are enabled, returns
 *         ::NvOdmAccelInt_None.
 * @param IntMotionAxis The axis that triggered the motion interrupt (::NvOdmAccelAxisType).
 *         If no interrupt occurs before the timeout interval expires, or no
 *         interrupts are enabled, returns ::NvOdmAccelAxis_None.
 * @param IntTapAxis The axis that triggered the tap interrupt (::NvOdmAccelAxisType).
 *         If no interrupt occurs before the timeout interval expires, or no
 *         interrupts are enabled, returns ::NvOdmAccelAxis_None.
 */

void
NvOdmAccelWaitInt(NvOdmAccelHandle    hDevice,
                  NvOdmAccelIntType  *IntType,
                  NvOdmAccelAxisType *IntMotionAxis,
                  NvOdmAccelAxisType *IntTapAxis);

/**
 * Signals the waiting semaphore.
 *
 * @param hDevice The accelerometer handle.
 */
void
NvOdmAccelSignal(NvOdmAccelHandle hDevice);


/**
 * Returns the current acceleration data in g-forces (\em g) as measured
 * by the accelerometer.
 *
 * To allow higher-level software to be written independently of the
 * precision and physical orientation of the accelerometer, the values
 * returned by this function must be normalized by the adaptation to the
 * following coordinate system:
 *
 * - Upright -- when the device is held upright with the primary display
 * facing the user, the returned acceleration should be (0, 1, 0).
 * - 90% rotation -- when the device is rotated 90 degrees clockwise, so that
 * the left edge of the primary display is pointing up, the returned acceleration
 * should be (1, 0, 0).
 * - Flat face up -- when the device is laid flat, like on a desk, with
 * the primary display face-up, the returned acceleration should be
 * (0, 0, 1).
 *
 * @param [in] hDevice The accelerometer handle.
 * @param [out] AccelX Measured acceleration along the X axis. The value is by
 *         increments of 1000. For example, 1 \em g is equal to 1000.
 * @param [out] AccelY Measured acceleration along the Y axis. The value is by
 *         increments of 1000. For example, 1 \em g is equal to 1000.
 * @param [out] AccelZ Measured acceleration along the Z axis. The value is by
 *         increments of 1000. For example, 1 \em g is equal to 1000.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmAccelGetAcceleration(NvOdmAccelHandle hDevice,
                          NvS32           *AccelX,
                          NvS32           *AccelY,
                          NvS32           *AccelZ);

/**
 * Gets the accelerometer's character.
 *
 * @param hDevice The accelerometer handle.
 * @return The accelerometer's character.
 */
NvOdmAccelerometerCaps
NvOdmAccelGetCaps(NvOdmAccelHandle hDevice);


/**
 * Sets the accelerometer's power state.
 *
 * @param hDevice The accelerometer handle.
 * @param PowerState  The accelerometer power state to set.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmAccelSetPowerState(NvOdmAccelHandle hDevice, NvOdmAccelPowerType PowerState);


/**
 * Sets the accelerometer's current sample rate state.
 *
 * @param hDevice The accelerometer handle.
 * @param SampleRate  The ::NvOdmAccelPowerType accelerometer
 *         sample rate in Hz (samples/second); if there
 *         is none suitable, the nearest sample rate is set.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmAccelSetSampleRate(NvOdmAccelHandle hDevice, NvU32 SampleRate);

/**
 * Gets the accelerometer's current sample rate state.
 *
 * @param hDevice The accelerometer handle.
 * @param pSampleRate  The ::NvOdmAccelPowerType accelerometer
 *          sample rate in Hz (samples/second); if there
 *          is none suitable, the nearest sample rate is set.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmAccelGetSampleRate(NvOdmAccelHandle hDevice, NvU32* pSampleRate);


#if defined(__cplusplus)
}
#endif
/** @} */
#endif // INCLUDED_NVODM_ACCELEROMETER_H
