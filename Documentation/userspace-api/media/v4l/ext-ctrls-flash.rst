.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _flash-controls:

***********************
Flash Control Reference
***********************

The V4L2 flash controls are intended to provide generic access to flash
controller devices. Flash controller devices are typically used in
digital cameras.

The interface can support both LED and xenon flash devices. As of
writing this, there is no xenon flash driver using this interface.


.. _flash-controls-use-cases:

Supported use cases
===================


Unsynchronised LED flash (software strobe)
------------------------------------------

Unsynchronised LED flash is controlled directly by the host as the
sensor. The flash must be enabled by the host before the exposure of the
image starts and disabled once it ends. The host is fully responsible
for the timing of the flash.

Example of such device: Nokia N900.


Synchronised LED flash (hardware strobe)
----------------------------------------

The synchronised LED flash is pre-programmed by the host (power and
timeout) but controlled by the sensor through a strobe signal from the
sensor to the flash.

The sensor controls the flash duration and timing. This information
typically must be made available to the sensor.


LED flash as torch
------------------

LED flash may be used as torch in conjunction with another use case
involving camera or individually.


.. _flash-control-id:

Flash Control IDs
-----------------

``V4L2_CID_FLASH_CLASS (class)``
    The FLASH class descriptor.

``V4L2_CID_FLASH_LED_MODE (menu)``
    Defines the mode of the flash LED, the high-power white LED attached
    to the flash controller. Setting this control may not be possible in
    presence of some faults. See V4L2_CID_FLASH_FAULT.


.. tabularcolumns:: |p{5.7cm}|p{11.8cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_FLASH_LED_MODE_NONE``
      - Off.
    * - ``V4L2_FLASH_LED_MODE_FLASH``
      - Flash mode.
    * - ``V4L2_FLASH_LED_MODE_TORCH``
      - Torch mode.

        See V4L2_CID_FLASH_TORCH_INTENSITY.



``V4L2_CID_FLASH_STROBE_SOURCE (menu)``
    Defines the source of the flash LED strobe.

.. tabularcolumns:: |p{7.5cm}|p{7.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_FLASH_STROBE_SOURCE_SOFTWARE``
      - The flash strobe is triggered by using the
	V4L2_CID_FLASH_STROBE control.
    * - ``V4L2_FLASH_STROBE_SOURCE_EXTERNAL``
      - The flash strobe is triggered by an external source. Typically
	this is a sensor, which makes it possible to synchronise the
	flash strobe start to exposure start.



``V4L2_CID_FLASH_STROBE (button)``
    Strobe flash. Valid when V4L2_CID_FLASH_LED_MODE is set to
    V4L2_FLASH_LED_MODE_FLASH and V4L2_CID_FLASH_STROBE_SOURCE
    is set to V4L2_FLASH_STROBE_SOURCE_SOFTWARE. Setting this
    control may not be possible in presence of some faults. See
    V4L2_CID_FLASH_FAULT.

``V4L2_CID_FLASH_STROBE_STOP (button)``
    Stop flash strobe immediately.

``V4L2_CID_FLASH_STROBE_STATUS (boolean)``
    Strobe status: whether the flash is strobing at the moment or not.
    This is a read-only control.

``V4L2_CID_FLASH_TIMEOUT (integer)``
    Hardware timeout for flash. The flash strobe is stopped after this
    period of time has passed from the start of the strobe.

``V4L2_CID_FLASH_INTENSITY (integer)``
    Intensity of the flash strobe when the flash LED is in flash mode
    (V4L2_FLASH_LED_MODE_FLASH). The unit should be milliamps (mA)
    if possible.

``V4L2_CID_FLASH_TORCH_INTENSITY (integer)``
    Intensity of the flash LED in torch mode
    (V4L2_FLASH_LED_MODE_TORCH). The unit should be milliamps (mA)
    if possible. Setting this control may not be possible in presence of
    some faults. See V4L2_CID_FLASH_FAULT.

``V4L2_CID_FLASH_INDICATOR_INTENSITY (integer)``
    Intensity of the indicator LED. The indicator LED may be fully
    independent of the flash LED. The unit should be microamps (uA) if
    possible.

``V4L2_CID_FLASH_FAULT (bitmask)``
    Faults related to the flash. The faults tell about specific problems
    in the flash chip itself or the LEDs attached to it. Faults may
    prevent further use of some of the flash controls. In particular,
    V4L2_CID_FLASH_LED_MODE is set to V4L2_FLASH_LED_MODE_NONE
    if the fault affects the flash LED. Exactly which faults have such
    an effect is chip dependent. Reading the faults resets the control
    and returns the chip to a usable state if possible.

.. tabularcolumns:: |p{8.4cm}|p{9.1cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_FLASH_FAULT_OVER_VOLTAGE``
      - Flash controller voltage to the flash LED has exceeded the limit
	specific to the flash controller.
    * - ``V4L2_FLASH_FAULT_TIMEOUT``
      - The flash strobe was still on when the timeout set by the user ---
	V4L2_CID_FLASH_TIMEOUT control --- has expired. Not all flash
	controllers may set this in all such conditions.
    * - ``V4L2_FLASH_FAULT_OVER_TEMPERATURE``
      - The flash controller has overheated.
    * - ``V4L2_FLASH_FAULT_SHORT_CIRCUIT``
      - The short circuit protection of the flash controller has been
	triggered.
    * - ``V4L2_FLASH_FAULT_OVER_CURRENT``
      - Current in the LED power supply has exceeded the limit specific to
	the flash controller.
    * - ``V4L2_FLASH_FAULT_INDICATOR``
      - The flash controller has detected a short or open circuit
	condition on the indicator LED.
    * - ``V4L2_FLASH_FAULT_UNDER_VOLTAGE``
      - Flash controller voltage to the flash LED has been below the
	minimum limit specific to the flash controller.
    * - ``V4L2_FLASH_FAULT_INPUT_VOLTAGE``
      - The input voltage of the flash controller is below the limit under
	which strobing the flash at full current will not be possible.The
	condition persists until this flag is no longer set.
    * - ``V4L2_FLASH_FAULT_LED_OVER_TEMPERATURE``
      - The temperature of the LED has exceeded its allowed upper limit.



``V4L2_CID_FLASH_CHARGE (boolean)``
    Enable or disable charging of the xenon flash capacitor.

``V4L2_CID_FLASH_READY (boolean)``
    Is the flash ready to strobe? Xenon flashes require their capacitors
    charged before strobing. LED flashes often require a cooldown period
    after strobe during which another strobe will not be possible. This
    is a read-only control.
