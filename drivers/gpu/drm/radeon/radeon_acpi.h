/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#ifndef RADEON_ACPI_H
#define RADEON_ACPI_H

struct radeon_device;
struct acpi_bus_event;

/* AMD hw uses four ACPI control methods:
 * 1. ATIF
 * ARG0: (ACPI_INTEGER) function code
 * ARG1: (ACPI_BUFFER) parameter buffer, 256 bytes
 * OUTPUT: (ACPI_BUFFER) output buffer, 256 bytes
 * ATIF provides an entry point for the gfx driver to interact with the sbios.
 * The AMD ACPI notification mechanism uses Notify (VGA, 0x81) or a custom
 * notification. Which notification is used as indicated by the ATIF Control
 * Method GET_SYSTEM_PARAMETERS. When the driver receives Notify (VGA, 0x81) or
 * a custom notification it invokes ATIF Control Method GET_SYSTEM_BIOS_REQUESTS
 * to identify pending System BIOS requests and associated parameters. For
 * example, if one of the pending requests is DISPLAY_SWITCH_REQUEST, the driver
 * will perform display device detection and invoke ATIF Control Method
 * SELECT_ACTIVE_DISPLAYS.
 *
 * 2. ATPX
 * ARG0: (ACPI_INTEGER) function code
 * ARG1: (ACPI_BUFFER) parameter buffer, 256 bytes
 * OUTPUT: (ACPI_BUFFER) output buffer, 256 bytes
 * ATPX methods are used on PowerXpress systems to handle mux switching and
 * discrete GPU power control.
 *
 * 3. ATRM
 * ARG0: (ACPI_INTEGER) offset of vbios rom data
 * ARG1: (ACPI_BUFFER) size of the buffer to fill (up to 4K).
 * OUTPUT: (ACPI_BUFFER) output buffer
 * ATRM provides an interfacess to access the discrete GPU vbios image on
 * PowerXpress systems with multiple GPUs.
 *
 * 4. ATCS
 * ARG0: (ACPI_INTEGER) function code
 * ARG1: (ACPI_BUFFER) parameter buffer, 256 bytes
 * OUTPUT: (ACPI_BUFFER) output buffer, 256 bytes
 * ATCS provides an interface to AMD chipset specific functionality.
 *
 */
/* ATIF */
#define ATIF_FUNCTION_VERIFY_INTERFACE                             0x0
/* ARG0: ATIF_FUNCTION_VERIFY_INTERFACE
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - version
 * DWORD - supported notifications mask
 * DWORD - supported functions bit vector
 */
/* Notifications mask */
#       define ATIF_DISPLAY_SWITCH_REQUEST_SUPPORTED               (1 << 0)
#       define ATIF_EXPANSION_MODE_CHANGE_REQUEST_SUPPORTED        (1 << 1)
#       define ATIF_THERMAL_STATE_CHANGE_REQUEST_SUPPORTED         (1 << 2)
#       define ATIF_FORCED_POWER_STATE_CHANGE_REQUEST_SUPPORTED    (1 << 3)
#       define ATIF_SYSTEM_POWER_SOURCE_CHANGE_REQUEST_SUPPORTED   (1 << 4)
#       define ATIF_DISPLAY_CONF_CHANGE_REQUEST_SUPPORTED          (1 << 5)
#       define ATIF_PX_GFX_SWITCH_REQUEST_SUPPORTED                (1 << 6)
#       define ATIF_PANEL_BRIGHTNESS_CHANGE_REQUEST_SUPPORTED      (1 << 7)
#       define ATIF_DGPU_DISPLAY_EVENT_SUPPORTED                   (1 << 8)
/* supported functions vector */
#       define ATIF_GET_SYSTEM_PARAMETERS_SUPPORTED               (1 << 0)
#       define ATIF_GET_SYSTEM_BIOS_REQUESTS_SUPPORTED            (1 << 1)
#       define ATIF_SELECT_ACTIVE_DISPLAYS_SUPPORTED              (1 << 2)
#       define ATIF_GET_LID_STATE_SUPPORTED                       (1 << 3)
#       define ATIF_GET_TV_STANDARD_FROM_CMOS_SUPPORTED           (1 << 4)
#       define ATIF_SET_TV_STANDARD_IN_CMOS_SUPPORTED             (1 << 5)
#       define ATIF_GET_PANEL_EXPANSION_MODE_FROM_CMOS_SUPPORTED  (1 << 6)
#       define ATIF_SET_PANEL_EXPANSION_MODE_IN_CMOS_SUPPORTED    (1 << 7)
#       define ATIF_TEMPERATURE_CHANGE_NOTIFICATION_SUPPORTED     (1 << 12)
#       define ATIF_GET_GRAPHICS_DEVICE_TYPES_SUPPORTED           (1 << 14)
#       define ATIF_GET_EXTERNAL_GPU_INFORMATION_SUPPORTED        (1 << 20)
#define ATIF_FUNCTION_GET_SYSTEM_PARAMETERS                        0x1
/* ARG0: ATIF_FUNCTION_GET_SYSTEM_PARAMETERS
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * DWORD - valid flags mask
 * DWORD - flags
 *
 * OR
 *
 * WORD  - structure size in bytes (includes size field)
 * DWORD - valid flags mask
 * DWORD - flags
 * BYTE  - notify command code
 *
 * flags
 * bits 1:0:
 * 0 - Notify(VGA, 0x81) is not used for notification
 * 1 - Notify(VGA, 0x81) is used for notification
 * 2 - Notify(VGA, n) is used for notification where
 * n (0xd0-0xd9) is specified in notify command code.
 * bit 2:
 * 1 - lid changes not reported though int10
 */
#define ATIF_FUNCTION_GET_SYSTEM_BIOS_REQUESTS                     0x2
/* ARG0: ATIF_FUNCTION_GET_SYSTEM_BIOS_REQUESTS
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * DWORD - pending sbios requests
 * BYTE  - panel expansion mode
 * BYTE  - thermal state: target gfx controller
 * BYTE  - thermal state: state id (0: exit state, non-0: state)
 * BYTE  - forced power state: target gfx controller
 * BYTE  - forced power state: state id
 * BYTE  - system power source
 * BYTE  - panel backlight level (0-255)
 */
/* pending sbios requests */
#       define ATIF_DISPLAY_SWITCH_REQUEST                         (1 << 0)
#       define ATIF_EXPANSION_MODE_CHANGE_REQUEST                  (1 << 1)
#       define ATIF_THERMAL_STATE_CHANGE_REQUEST                   (1 << 2)
#       define ATIF_FORCED_POWER_STATE_CHANGE_REQUEST              (1 << 3)
#       define ATIF_SYSTEM_POWER_SOURCE_CHANGE_REQUEST             (1 << 4)
#       define ATIF_DISPLAY_CONF_CHANGE_REQUEST                    (1 << 5)
#       define ATIF_PX_GFX_SWITCH_REQUEST                          (1 << 6)
#       define ATIF_PANEL_BRIGHTNESS_CHANGE_REQUEST                (1 << 7)
#       define ATIF_DGPU_DISPLAY_EVENT                             (1 << 8)
/* panel expansion mode */
#       define ATIF_PANEL_EXPANSION_DISABLE                        0
#       define ATIF_PANEL_EXPANSION_FULL                           1
#       define ATIF_PANEL_EXPANSION_ASPECT                         2
/* target gfx controller */
#       define ATIF_TARGET_GFX_SINGLE                              0
#       define ATIF_TARGET_GFX_PX_IGPU                             1
#       define ATIF_TARGET_GFX_PX_DGPU                             2
/* system power source */
#       define ATIF_POWER_SOURCE_AC                                1
#       define ATIF_POWER_SOURCE_DC                                2
#       define ATIF_POWER_SOURCE_RESTRICTED_AC_1                   3
#       define ATIF_POWER_SOURCE_RESTRICTED_AC_2                   4
#define ATIF_FUNCTION_SELECT_ACTIVE_DISPLAYS                       0x3
/* ARG0: ATIF_FUNCTION_SELECT_ACTIVE_DISPLAYS
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - selected displays
 * WORD  - connected displays
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - selected displays
 */
#       define ATIF_LCD1                                           (1 << 0)
#       define ATIF_CRT1                                           (1 << 1)
#       define ATIF_TV                                             (1 << 2)
#       define ATIF_DFP1                                           (1 << 3)
#       define ATIF_CRT2                                           (1 << 4)
#       define ATIF_LCD2                                           (1 << 5)
#       define ATIF_DFP2                                           (1 << 7)
#       define ATIF_CV                                             (1 << 8)
#       define ATIF_DFP3                                           (1 << 9)
#       define ATIF_DFP4                                           (1 << 10)
#       define ATIF_DFP5                                           (1 << 11)
#       define ATIF_DFP6                                           (1 << 12)
#define ATIF_FUNCTION_GET_LID_STATE                                0x4
/* ARG0: ATIF_FUNCTION_GET_LID_STATE
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * BYTE  - lid state (0: open, 1: closed)
 *
 * GET_LID_STATE only works at boot and resume, for general lid
 * status, use the kernel provided status
 */
#define ATIF_FUNCTION_GET_TV_STANDARD_FROM_CMOS                    0x5
/* ARG0: ATIF_FUNCTION_GET_TV_STANDARD_FROM_CMOS
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * BYTE  - 0
 * BYTE  - TV standard
 */
#       define ATIF_TV_STD_NTSC                                    0
#       define ATIF_TV_STD_PAL                                     1
#       define ATIF_TV_STD_PALM                                    2
#       define ATIF_TV_STD_PAL60                                   3
#       define ATIF_TV_STD_NTSCJ                                   4
#       define ATIF_TV_STD_PALCN                                   5
#       define ATIF_TV_STD_PALN                                    6
#       define ATIF_TV_STD_SCART_RGB                               9
#define ATIF_FUNCTION_SET_TV_STANDARD_IN_CMOS                      0x6
/* ARG0: ATIF_FUNCTION_SET_TV_STANDARD_IN_CMOS
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * BYTE  - 0
 * BYTE  - TV standard
 * OUTPUT: none
 */
#define ATIF_FUNCTION_GET_PANEL_EXPANSION_MODE_FROM_CMOS           0x7
/* ARG0: ATIF_FUNCTION_GET_PANEL_EXPANSION_MODE_FROM_CMOS
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * BYTE  - panel expansion mode
 */
#define ATIF_FUNCTION_SET_PANEL_EXPANSION_MODE_IN_CMOS             0x8
/* ARG0: ATIF_FUNCTION_SET_PANEL_EXPANSION_MODE_IN_CMOS
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * BYTE  - panel expansion mode
 * OUTPUT: none
 */
#define ATIF_FUNCTION_TEMPERATURE_CHANGE_NOTIFICATION              0xD
/* ARG0: ATIF_FUNCTION_TEMPERATURE_CHANGE_NOTIFICATION
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - gfx controller id
 * BYTE  - current temperature (degress Celsius)
 * OUTPUT: none
 */
#define ATIF_FUNCTION_GET_GRAPHICS_DEVICE_TYPES                    0xF
/* ARG0: ATIF_FUNCTION_GET_GRAPHICS_DEVICE_TYPES
 * ARG1: none
 * OUTPUT:
 * WORD  - number of gfx devices
 * WORD  - device structure size in bytes (excludes device size field)
 * DWORD - flags         \
 * WORD  - bus number     } repeated structure
 * WORD  - device number /
 */
/* flags */
#       define ATIF_PX_REMOVABLE_GRAPHICS_DEVICE                   (1 << 0)
#       define ATIF_XGP_PORT                                       (1 << 1)
#       define ATIF_VGA_ENABLED_GRAPHICS_DEVICE                    (1 << 2)
#       define ATIF_XGP_PORT_IN_DOCK                               (1 << 3)
#define ATIF_FUNCTION_GET_EXTERNAL_GPU_INFORMATION                 0x15
/* ARG0: ATIF_FUNCTION_GET_EXTERNAL_GPU_INFORMATION
 * ARG1: none
 * OUTPUT:
 * WORD  - number of reported external gfx devices
 * WORD  - device structure size in bytes (excludes device size field)
 * WORD  - flags         \
 * WORD  - bus number    / repeated structure
 */
/* flags */
#       define ATIF_EXTERNAL_GRAPHICS_PORT                         (1 << 0)

/* ATPX */
#define ATPX_FUNCTION_VERIFY_INTERFACE                             0x0
/* ARG0: ATPX_FUNCTION_VERIFY_INTERFACE
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - version
 * DWORD - supported functions bit vector
 */
/* supported functions vector */
#       define ATPX_GET_PX_PARAMETERS_SUPPORTED                    (1 << 0)
#       define ATPX_POWER_CONTROL_SUPPORTED                        (1 << 1)
#       define ATPX_DISPLAY_MUX_CONTROL_SUPPORTED                  (1 << 2)
#       define ATPX_I2C_MUX_CONTROL_SUPPORTED                      (1 << 3)
#       define ATPX_GRAPHICS_DEVICE_SWITCH_START_NOTIFICATION_SUPPORTED (1 << 4)
#       define ATPX_GRAPHICS_DEVICE_SWITCH_END_NOTIFICATION_SUPPORTED   (1 << 5)
#       define ATPX_GET_DISPLAY_CONNECTORS_MAPPING_SUPPORTED       (1 << 7)
#       define ATPX_GET_DISPLAY_DETECTION_PORTS_SUPPORTED          (1 << 8)
#define ATPX_FUNCTION_GET_PX_PARAMETERS                            0x1
/* ARG0: ATPX_FUNCTION_GET_PX_PARAMETERS
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * DWORD - valid flags mask
 * DWORD - flags
 */
/* flags */
#       define ATPX_LVDS_I2C_AVAILABLE_TO_BOTH_GPUS                (1 << 0)
#       define ATPX_CRT1_I2C_AVAILABLE_TO_BOTH_GPUS                (1 << 1)
#       define ATPX_DVI1_I2C_AVAILABLE_TO_BOTH_GPUS                (1 << 2)
#       define ATPX_CRT1_RGB_SIGNAL_MUXED                          (1 << 3)
#       define ATPX_TV_SIGNAL_MUXED                                (1 << 4)
#       define ATPX_DFP_SIGNAL_MUXED                               (1 << 5)
#       define ATPX_SEPARATE_MUX_FOR_I2C                           (1 << 6)
#       define ATPX_DYNAMIC_PX_SUPPORTED                           (1 << 7)
#       define ATPX_ACF_NOT_SUPPORTED                              (1 << 8)
#       define ATPX_FIXED_NOT_SUPPORTED                            (1 << 9)
#       define ATPX_DYNAMIC_DGPU_POWER_OFF_SUPPORTED               (1 << 10)
#       define ATPX_DGPU_REQ_POWER_FOR_DISPLAYS                    (1 << 11)
#       define ATPX_DGPU_CAN_DRIVE_DISPLAYS                        (1 << 12)
#       define ATPX_MS_HYBRID_GFX_SUPPORTED                        (1 << 14)
#define ATPX_FUNCTION_POWER_CONTROL                                0x2
/* ARG0: ATPX_FUNCTION_POWER_CONTROL
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * BYTE  - dGPU power state (0: power off, 1: power on)
 * OUTPUT: none
 */
#define ATPX_FUNCTION_DISPLAY_MUX_CONTROL                          0x3
/* ARG0: ATPX_FUNCTION_DISPLAY_MUX_CONTROL
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - display mux control (0: iGPU, 1: dGPU)
 * OUTPUT: none
 */
#       define ATPX_INTEGRATED_GPU                                 0
#       define ATPX_DISCRETE_GPU                                   1
#define ATPX_FUNCTION_I2C_MUX_CONTROL                              0x4
/* ARG0: ATPX_FUNCTION_I2C_MUX_CONTROL
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - i2c/aux/hpd mux control (0: iGPU, 1: dGPU)
 * OUTPUT: none
 */
#define ATPX_FUNCTION_GRAPHICS_DEVICE_SWITCH_START_NOTIFICATION    0x5
/* ARG0: ATPX_FUNCTION_GRAPHICS_DEVICE_SWITCH_START_NOTIFICATION
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - target gpu (0: iGPU, 1: dGPU)
 * OUTPUT: none
 */
#define ATPX_FUNCTION_GRAPHICS_DEVICE_SWITCH_END_NOTIFICATION      0x6
/* ARG0: ATPX_FUNCTION_GRAPHICS_DEVICE_SWITCH_END_NOTIFICATION
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - target gpu (0: iGPU, 1: dGPU)
 * OUTPUT: none
 */
#define ATPX_FUNCTION_GET_DISPLAY_CONNECTORS_MAPPING               0x8
/* ARG0: ATPX_FUNCTION_GET_DISPLAY_CONNECTORS_MAPPING
 * ARG1: none
 * OUTPUT:
 * WORD  - number of display connectors
 * WORD  - connector structure size in bytes (excludes connector size field)
 * BYTE  - flags                                                     \
 * BYTE  - ATIF display vector bit position                           } repeated
 * BYTE  - adapter id (0: iGPU, 1-n: dGPU ordered by pcie bus number) } structure
 * WORD  - connector ACPI id                                         /
 */
/* flags */
#       define ATPX_DISPLAY_OUTPUT_SUPPORTED_BY_ADAPTER_ID_DEVICE  (1 << 0)
#       define ATPX_DISPLAY_HPD_SUPPORTED_BY_ADAPTER_ID_DEVICE     (1 << 1)
#       define ATPX_DISPLAY_I2C_SUPPORTED_BY_ADAPTER_ID_DEVICE     (1 << 2)
#define ATPX_FUNCTION_GET_DISPLAY_DETECTION_PORTS                  0x9
/* ARG0: ATPX_FUNCTION_GET_DISPLAY_DETECTION_PORTS
 * ARG1: none
 * OUTPUT:
 * WORD  - number of HPD/DDC ports
 * WORD  - port structure size in bytes (excludes port size field)
 * BYTE  - ATIF display vector bit position \
 * BYTE  - hpd id                            } reapeated structure
 * BYTE  - ddc id                           /
 *
 * available on A+A systems only
 */
/* hpd id */
#       define ATPX_HPD_NONE                                       0
#       define ATPX_HPD1                                           1
#       define ATPX_HPD2                                           2
#       define ATPX_HPD3                                           3
#       define ATPX_HPD4                                           4
#       define ATPX_HPD5                                           5
#       define ATPX_HPD6                                           6
/* ddc id */
#       define ATPX_DDC_NONE                                       0
#       define ATPX_DDC1                                           1
#       define ATPX_DDC2                                           2
#       define ATPX_DDC3                                           3
#       define ATPX_DDC4                                           4
#       define ATPX_DDC5                                           5
#       define ATPX_DDC6                                           6
#       define ATPX_DDC7                                           7
#       define ATPX_DDC8                                           8

/* ATCS */
#define ATCS_FUNCTION_VERIFY_INTERFACE                             0x0
/* ARG0: ATCS_FUNCTION_VERIFY_INTERFACE
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - version
 * DWORD - supported functions bit vector
 */
/* supported functions vector */
#       define ATCS_GET_EXTERNAL_STATE_SUPPORTED                   (1 << 0)
#       define ATCS_PCIE_PERFORMANCE_REQUEST_SUPPORTED             (1 << 1)
#       define ATCS_PCIE_DEVICE_READY_NOTIFICATION_SUPPORTED       (1 << 2)
#       define ATCS_SET_PCIE_BUS_WIDTH_SUPPORTED                   (1 << 3)
#define ATCS_FUNCTION_GET_EXTERNAL_STATE                           0x1
/* ARG0: ATCS_FUNCTION_GET_EXTERNAL_STATE
 * ARG1: none
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * DWORD - valid flags mask
 * DWORD - flags (0: undocked, 1: docked)
 */
/* flags */
#       define ATCS_DOCKED                                         (1 << 0)
#define ATCS_FUNCTION_PCIE_PERFORMANCE_REQUEST                     0x2
/* ARG0: ATCS_FUNCTION_PCIE_PERFORMANCE_REQUEST
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - client id (bit 2-0: func num, 7-3: dev num, 15-8: bus num)
 * WORD  - valid flags mask
 * WORD  - flags
 * BYTE  - request type
 * BYTE  - performance request
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * BYTE  - return value
 */
/* flags */
#       define ATCS_ADVERTISE_CAPS                                 (1 << 0)
#       define ATCS_WAIT_FOR_COMPLETION                            (1 << 1)
/* request type */
#       define ATCS_PCIE_LINK_SPEED                                1
/* performance request */
#       define ATCS_REMOVE                                         0
#       define ATCS_FORCE_LOW_POWER                                1
#       define ATCS_PERF_LEVEL_1                                   2 /* PCIE Gen 1 */
#       define ATCS_PERF_LEVEL_2                                   3 /* PCIE Gen 2 */
#       define ATCS_PERF_LEVEL_3                                   4 /* PCIE Gen 3 */
/* return value */
#       define ATCS_REQUEST_REFUSED                                1
#       define ATCS_REQUEST_COMPLETE                               2
#       define ATCS_REQUEST_IN_PROGRESS                            3
#define ATCS_FUNCTION_PCIE_DEVICE_READY_NOTIFICATION               0x3
/* ARG0: ATCS_FUNCTION_PCIE_DEVICE_READY_NOTIFICATION
 * ARG1: none
 * OUTPUT: none
 */
#define ATCS_FUNCTION_SET_PCIE_BUS_WIDTH                           0x4
/* ARG0: ATCS_FUNCTION_SET_PCIE_BUS_WIDTH
 * ARG1:
 * WORD  - structure size in bytes (includes size field)
 * WORD  - client id (bit 2-0: func num, 7-3: dev num, 15-8: bus num)
 * BYTE  - number of active lanes
 * OUTPUT:
 * WORD  - structure size in bytes (includes size field)
 * BYTE  - number of active lanes
 */

#if defined(CONFIG_VGA_SWITCHEROO)
void radeon_register_atpx_handler(void);
void radeon_unregister_atpx_handler(void);
bool radeon_has_atpx_dgpu_power_cntl(void);
bool radeon_is_atpx_hybrid(void);
bool radeon_has_atpx(void);
bool radeon_atpx_dgpu_req_power_for_displays(void);
#endif

#endif
