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
 *         I/O Module Definitions</b>
 *
 * @b Description: Defines all of the I/O module types (buses, I/Os, etc.)
 *                 that may exist on an application processor.
 */

#ifndef INCLUDED_NVODM_MODULES_H
#define INCLUDED_NVODM_MODULES_H

/**
 * @addtogroup nvodm_services
 * @{
 */

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * Defines I/O module types.
 * Application processors provide a multitude of interfaces for connecting
 * to external peripheral devices. These take the forms of individual pins
 * (such as GPIOs), buses (such as USB), and power rails. Each interface
 * may have zero, one, or multiple instantiations on the application processor;
 * see the technical notes to determine the availability of interconnects for
 * your platform.
 */
typedef enum
{
    NvOdmIoModule_Ata,
    NvOdmIoModule_Crt,
    NvOdmIoModule_Csi,
    NvOdmIoModule_Dap,
    NvOdmIoModule_Display,
    NvOdmIoModule_Dsi,
    NvOdmIoModule_Gpio,
    NvOdmIoModule_Hdcp,
    NvOdmIoModule_Hdmi,
    NvOdmIoModule_Hsi,
    NvOdmIoModule_Hsmmc,
    NvOdmIoModule_I2s,
    NvOdmIoModule_I2c,
    NvOdmIoModule_I2c_Pmu,
    NvOdmIoModule_Kbd,
    NvOdmIoModule_Mio,
    NvOdmIoModule_Nand,
    NvOdmIoModule_Pwm,
    NvOdmIoModule_Sdio,
    NvOdmIoModule_Sflash,
    NvOdmIoModule_Slink,
    NvOdmIoModule_Spdif,
    NvOdmIoModule_Spi,
    NvOdmIoModule_Twc,
    NvOdmIoModule_Tvo,
    NvOdmIoModule_Uart,
    NvOdmIoModule_Usb,
    NvOdmIoModule_Vdd,
    NvOdmIoModule_VideoInput,
    NvOdmIoModule_Xio,
    NvOdmIoModule_ExternalClock,
    NvOdmIoModule_Ulpi,
    NvOdmIoModule_OneWire,
    NvOdmIoModule_SyncNor,
    NvOdmIoModule_PciExpress,
    NvOdmIoModule_Trace,
    NvOdmIoModule_Tsense,
    NvOdmIoModule_BacklightPwm,

    NvOdmIoModule_Num,
    NvOdmIoModule_Force32 = 0x7fffffffUL
} NvOdmIoModule;


#if defined(__cplusplus)
}
#endif

/** @} */

#endif  // INCLUDED_NVODM_MODULES_H
