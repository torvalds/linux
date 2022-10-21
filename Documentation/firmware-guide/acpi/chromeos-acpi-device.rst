.. SPDX-License-Identifier: GPL-2.0

=====================
Chrome OS ACPI Device
=====================

Hardware functionality specific to Chrome OS is exposed through a Chrome OS ACPI device.
The plug and play ID of a Chrome OS ACPI device is GGL0001. GGL is a valid PNP ID of Google.
PNP ID can be used with the ACPI devices according to the guidelines. The following ACPI
objects are supported:

.. flat-table:: Supported ACPI Objects
   :widths: 1 2
   :header-rows: 1

   * - Object
     - Description

   * - CHSW
     - Chrome OS switch positions

   * - HWID
     - Chrome OS hardware ID

   * - FWID
     - Chrome OS firmware version

   * - FRID
     - Chrome OS read-only firmware version

   * - BINF
     - Chrome OS boot information

   * - GPIO
     - Chrome OS GPIO assignments

   * - VBNV
     - Chrome OS NVRAM locations

   * - VDTA
     - Chrome OS verified boot data

   * - FMAP
     - Chrome OS flashmap base address

   * - MLST
     - Chrome OS method list

CHSW (Chrome OS switch positions)
=================================
This control method returns the switch positions for Chrome OS specific hardware switches.

Arguments:
----------
None

Result code:
------------
An integer containing the switch positions as bitfields:

.. flat-table::
   :widths: 1 2

   * - 0x00000002
     - Recovery button was pressed when x86 firmware booted.

   * - 0x00000004
     - Recovery button was pressed when EC firmware booted. (required if EC EEPROM is
       rewritable; otherwise optional)

   * - 0x00000020
     - Developer switch was enabled when x86 firmware booted.

   * - 0x00000200
     - Firmware write protection was disabled when x86 firmware booted. (required if
       firmware write protection is controlled through x86 BIOS; otherwise optional)

All other bits are reserved and should be set to 0.

HWID (Chrome OS hardware ID)
============================
This control method returns the hardware ID for the Chromebook.

Arguments:
----------
None

Result code:
------------
A null-terminated ASCII string containing the hardware ID from the Model-Specific Data area of
EEPROM.

Note that the hardware ID can be up to 256 characters long, including the terminating null.

FWID (Chrome OS firmware version)
=================================
This control method returns the firmware version for the rewritable portion of the main
processor firmware.

Arguments:
----------
None

Result code:
------------
A null-terminated ASCII string containing the complete firmware version for the rewritable
portion of the main processor firmware.

FRID (Chrome OS read-only firmware version)
===========================================
This control method returns the firmware version for the read-only portion of the main
processor firmware.

Arguments:
----------
None

Result code:
------------
A null-terminated ASCII string containing the complete firmware version for the read-only
(bootstrap + recovery ) portion of the main processor firmware.

BINF (Chrome OS boot information)
=================================
This control method returns information about the current boot.

Arguments:
----------
None

Result code:
------------

.. code-block::

   Package {
           Reserved1
           Reserved2
           Active EC Firmware
           Active Main Firmware Type
           Reserved5
   }

.. flat-table::
   :widths: 1 1 2
   :header-rows: 1

   * - Field
     - Format
     - Description

   * - Reserved1
     - DWORD
     - Set to 256 (0x100). This indicates this field is no longer used.

   * - Reserved2
     - DWORD
     - Set to 256 (0x100). This indicates this field is no longer used.

   * - Active EC firmware
     - DWORD
     - The EC firmware which was used during boot.

       - 0 - Read-only (recovery) firmware
       - 1 - Rewritable firmware.

       Set to 0 if EC firmware is always read-only.

   * - Active Main Firmware Type
     - DWORD
     - The main firmware type which was used during boot.

       - 0 - Recovery
       - 1 - Normal
       - 2 - Developer
       - 3 - netboot (factory installation only)

       Other values are reserved.

   * - Reserved5
     - DWORD
     - Set to 256 (0x100). This indicates this field is no longer used.

GPIO (Chrome OS GPIO assignments)
=================================
This control method returns information about Chrome OS specific GPIO assignments for
Chrome OS hardware, so the kernel can directly control that hardware.

Arguments:
----------
None

Result code:
------------
.. code-block::

        Package {
                Package {
                        // First GPIO assignment
                        Signal Type        //DWORD
                        Attributes         //DWORD
                        Controller Offset  //DWORD
                        Controller Name    //ASCIIZ
                },
                ...
                Package {
                        // Last GPIO assignment
                        Signal Type        //DWORD
                        Attributes         //DWORD
                        Controller Offset  //DWORD
                        Controller Name    //ASCIIZ
                }
        }

Where ASCIIZ means a null-terminated ASCII string.

.. flat-table::
   :widths: 1 1 2
   :header-rows: 1

   * - Field
     - Format
     - Description

   * - Signal Type
     - DWORD
     - Type of GPIO signal

       - 0x00000001 - Recovery button
       - 0x00000002 - Developer mode switch
       - 0x00000003 - Firmware write protection switch
       - 0x00000100 - Debug header GPIO 0
       - ...
       - 0x000001FF - Debug header GPIO 255

       Other values are reserved.

   * - Attributes
     - DWORD
     - Signal attributes as bitfields:

       - 0x00000001 - Signal is active-high (for button, a GPIO value
         of 1 means the button is pressed; for switches, a GPIO value
         of 1 means the switch is enabled). If this bit is 0, the signal
         is active low. Set to 0 for debug header GPIOs.

   * - Controller Offset
     - DWORD
     - GPIO number on the specified controller.

   * - Controller Name
     - ASCIIZ
     - Name of the controller for the GPIO.
       Currently supported names:
       "NM10" - Intel NM10 chip

VBNV (Chrome OS NVRAM locations)
================================
This control method returns information about the NVRAM (CMOS) locations used to
communicate with the BIOS.

Arguments:
----------
None

Result code:
------------
.. code-block::

        Package {
                NV Storage Block Offset  //DWORD
                NV Storage Block Size    //DWORD
        }

.. flat-table::
   :widths: 1 1 2
   :header-rows: 1

   * - Field
     - Format
     - Description

   * - NV Storage Block Offset
     - DWORD
     - Offset in CMOS bank 0 of the verified boot non-volatile storage block, counting from
       the first writable CMOS byte (that is, offset=0 is the byte following the 14 bytes of
       clock data).

   * - NV Storage Block Size
     - DWORD
     - Size in bytes of the verified boot non-volatile storage block.

FMAP (Chrome OS flashmap address)
=================================
This control method returns the physical memory address of the start of the main processor
firmware flashmap.

Arguments:
----------
None

NoneResult code:
----------------
A DWORD containing the physical memory address of the start of the main processor firmware
flashmap.

VDTA (Chrome OS verified boot data)
===================================
This control method returns the verified boot data block shared between the firmware
verification step and the kernel verification step.

Arguments:
----------
None

Result code:
------------
A buffer containing the verified boot data block.

MECK (Management Engine Checksum)
=================================
This control method returns the SHA-1 or SHA-256 hash that is read out of the Management
Engine extended registers during boot. The hash is exported via ACPI so the OS can verify that
the ME firmware has not changed. If Management Engine is not present, or if the firmware was
unable to read the extended registers, this buffer can be zero.

Arguments:
----------
None

Result code:
------------
A buffer containing the ME hash.

MLST (Chrome OS method list)
============================
This control method returns a list of the other control methods supported by the Chrome OS
hardware device.

Arguments:
----------
None

Result code:
------------
A package containing a list of null-terminated ASCII strings, one for each control method
supported by the Chrome OS hardware device, not including the MLST method itself.
For this version of the specification, the result is:

.. code-block::

        Package {
                "CHSW",
                "FWID",
                "HWID",
                "FRID",
                "BINF",
                "GPIO",
                "VBNV",
                "FMAP",
                "VDTA",
                "MECK"
        }
