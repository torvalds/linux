.. SPDX-License-Identifier: GPL-2.0

=========================================
The device for Mediatek MT6370 RGB LED
=========================================

Description
-----------

The MT6370 integrates a four-channel RGB LED driver, designed to provide a
variety of lighting effect for mobile device applications. The RGB LED devices
includes a smart LED string controller and it can drive 3 channels of LEDs with
a sink current up to 24mA and a CHG_VIN power good indicator LED with sink
current up to 6mA. It provides three operation modes for RGB LEDs:
PWM Dimming mode, breath pattern mode, and constant current mode. The device
can increase or decrease the brightness of the RGB LED via an I2C interface.

The breath pattern for a channel can be programmed using the "pattern" trigger,
using the hw_pattern attribute.

/sys/class/leds/<led>/hw_pattern
--------------------------------

Specify a hardware breath pattern for a MT6370 RGB LED.

The breath pattern is a series of timing pairs, with the hold-time expressed in
milliseconds. And the brightness is controlled by
'/sys/class/leds/<led>/brightness'. The pattern doesn't include the brightness
setting. Hardware pattern only controls the timing for each pattern stage
depending on the current brightness setting.

Pattern diagram::

         "0 Tr1 0 Tr2 0 Tf1 0 Tf2 0 Ton 0 Toff" --> '0' for dummy brightness code

          ^
          |           ============
          |          /            \                                /
    Icurr |         /              \                              /
          |        /                \                            /
          |       /                  \                          /   .....repeat
          |      /                    \                        /
          |   ---                      ---                  ---
          |---                            ---            ---
          +----------------------------------============------------> Time
          < Tr1><Tr2><   Ton    ><Tf1><Tf2 ><  Toff    >< Tr1><Tr2>

Timing description:

  * Tr1:    First rising time for 0% - 30% load.
  * Tr2:    Second rising time for 31% - 100% load.
  * Ton:    On time for 100% load.
  * Tf1:    First falling time for 100% - 31% load.
  * Tf2:    Second falling time for 30% to 0% load.
  * Toff:   Off time for 0% load.

  * Tr1/Tr2/Tf1/Tf2/Ton: 125ms to 3125ms, 200ms per step.
  * Toff: 250ms to 6250ms, 400ms per step.

Pattern example::

       "0 125 0 125 0 125 0 125 0 625 0 1050"

This Will configure Tr1/Tr2/Tf1/Tf2 to 125m, Ton to 625ms, and Toff to 1050ms.
