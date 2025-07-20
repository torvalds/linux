.. SPDX-License-Identifier: GPL-2.0-only

=========================
IIO Abstractions for ADCs
=========================

1. Overview
===========

The IIO subsystem supports many Analog to Digital Converters (ADCs). Some ADCs
have features and characteristics that are supported in specific ways by IIO
device drivers. This documentation describes common ADC features and explains
how they are supported by the IIO subsystem.

1. ADC Channel Types
====================

ADCs can have distinct types of inputs, each of them measuring analog voltages
in a slightly different way. An ADC digitizes the analog input voltage over a
span that is often given by the provided voltage reference, the input type, and
the input polarity. The input range allowed to an ADC channel is needed to
determine the scale factor and offset needed to obtain the measured value in
real-world units (millivolts for voltage measurement, milliamps for current
measurement, etc.).

Elaborate designs may have nonlinear characteristics or integrated components
(such as amplifiers and reference buffers) that might also have to be considered
to derive the allowed input range for an ADC. For clarity, the sections below
assume the input range only depends on the provided voltage references, input
type, and input polarity.

There are three general types of ADC inputs (single-ended, differential,
pseudo-differential) and two possible polarities (unipolar, bipolar). The input
type (single-ended, differential, pseudo-differential) is one channel
characteristic, and is completely independent of the polarity (unipolar,
bipolar) aspect. A comprehensive article about ADC input types (on which this
doc is heavily based on) can be found at
https://www.analog.com/en/resources/technical-articles/sar-adc-input-types.html.

1.1 Single-ended channels
-------------------------

Single-ended channels digitize the analog input voltage relative to ground and
can be either unipolar or bipolar.

1.1.1 Single-ended Unipolar Channels
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

  ---------- VREF -------------
      ´ `           ´ `                  _____________
    /     \       /     \               /             |
   /       \     /       \         --- <  IN    ADC   |
            \   /         \   /         \             |
             `-´           `-´           \       VREF |
  -------- GND (0V) -----------           +-----------+
                                                  ^
                                                  |
                                             External VREF

The input voltage to a **single-ended unipolar** channel is allowed to swing
from GND to VREF (where VREF is a voltage reference with electrical potential
higher than system ground). The maximum input voltage is also called VFS
(Voltage input Full-Scale), with VFS being determined by VREF. The voltage
reference may be provided from an external supply or derived from the chip power
source.

A single-ended unipolar channel could be described in device tree like the
following example::

    adc@0 {
        ...
        #address-cells = <1>;
        #size-cells = <0>;

        channel@0 {
            reg = <0>;
        };
    };

One is always allowed to include ADC channel nodes in the device tree. Though,
if the device has a uniform set of inputs (e.g. all inputs are single-ended),
then declaring the channel nodes is optional.

One caveat for devices that support mixed single-ended and differential channels
is that single-ended channel nodes also need to provide a ``single-channel``
property when ``reg`` is an arbitrary number that doesn't match the input pin
number.

See ``Documentation/devicetree/bindings/iio/adc/adc.yaml`` for the complete
documentation of ADC specific device tree properties.


1.1.2 Single-ended Bipolar Channels
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

  ---------- +VREF ------------
      ´ `           ´ `                  _____________________
    /     \       /     \               /                     |
   /       \     /       \         --- <  IN          ADC     |
            \   /         \   /         \                     |
             `-´           `-´           \       +VREF  -VREF |
  ---------- -VREF ------------           +-------------------+
                                                  ^       ^
                                                  |       |
                             External +VREF ------+  External -VREF

For a **single-ended bipolar** channel, the analog voltage input can go from
-VREF to +VREF (where -VREF is the voltage reference that has the lower
electrical potential while +VREF is the reference with the higher one). Some ADC
chips derive the lower reference from +VREF, others get it from a separate
input. Often, +VREF and -VREF are symmetric but they don't need to be so. When
-VREF is lower than system ground, these inputs are also called single-ended
true bipolar. Also, while there is a relevant difference between bipolar and
true bipolar from the electrical perspective, IIO makes no explicit distinction
between them.

Here's an example device tree description of a single-ended bipolar channel::

    adc@0 {
        ...
        #address-cells = <1>;
        #size-cells = <0>;

        channel@0 {
            reg = <0>;
            bipolar;
        };
    };

1.2 Differential channels
-------------------------

A differential voltage measurement digitizes the voltage level at the positive
input (IN+) relative to the negative input (IN-) over the -VREF to +VREF span.
In other words, a differential channel measures the potential difference between
IN+ and IN-, which is often denoted by the IN+ - IN- formula.

1.2.1 Differential Bipolar Channels
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

  -------- +VREF ------         +-------------------+
    ´ `       ´ `              /                    |
  /     \   /     \   /   --- <  IN+                |
         `-´       `-´         |                    |
  -------- -VREF ------        |                    |
                               |            ADC     |
  -------- +VREF ------        |                    |
        ´ `       ´ `          |                    |
  \   /     \   /     \   --- <  IN-                |
   `-´       `-´               \       +VREF  -VREF |
  -------- -VREF ------         +-------------------+
                                         ^       ^
                                         |       +---- External -VREF
                                  External +VREF

The analog signals to **differential bipolar** inputs are also allowed to swing
from -VREF to +VREF. The bipolar part of the name means that the resulting value
of the difference (IN+ - IN-) can be positive or negative. If -VREF is below
system GND, these are also called differential true bipolar inputs.

Device tree example of a differential bipolar channel::

    adc@0 {
        ...
        #address-cells = <1>;
        #size-cells = <0>;

        channel@0 {
            reg = <0>;
            bipolar;
            diff-channels = <0 1>;
        };
    };

In the ADC driver, ``differential = 1`` is set into ``struct iio_chan_spec`` for
the channel. Even though, there are three general input types, ``differential``
is only used to distinguish between differential and non-differential (either
single-ended or pseudo-differential) input types. See
``include/linux/iio/iio.h`` for more information.

1.2.2 Differential Unipolar Channels
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For **differential unipolar** channels, the analog voltage at the positive input
must also be higher than the voltage at the negative input. Thus, the actual
input range allowed to a differential unipolar channel is IN- to +VREF. Because
IN+ is allowed to swing with the measured analog signal and the input setup must
guarantee IN+ will not go below IN- (nor IN- will raise above IN+), most
differential unipolar channel setups have IN- fixed to a known voltage that does
not fall within the voltage range expected for the measured signal. That leads
to a setup that is equivalent to a pseudo-differential channel. Thus,
differential unipolar setups can often be supported as pseudo-differential
unipolar channels.

1.3 Pseudo-differential Channels
--------------------------------

There is a third ADC input type which is called pseudo-differential or
single-ended to differential configuration. A pseudo-differential channel is
similar to a differential channel in that it also measures IN+ relative to IN-.
However, unlike bipolar differential channels, the negative input is limited to
a narrow voltage range (taken as a constant voltage) while only IN+ is allowed
to swing. A pseudo-differential channel can be made out from a differential pair
of inputs by restricting the negative input to a known voltage while allowing
only the positive input to swing. Sometimes, the input provided to IN- is called
common-mode voltage. Besides, some parts have a COM pin that allows single-ended
inputs to be referenced to a common-mode voltage, making them
pseudo-differential channels. Often, the common mode input voltage can be
described in the device tree as a voltage regulator (e.g. ``com-supply``) since
it is basically a constant voltage source.

1.3.1 Pseudo-differential Unipolar Channels
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

  -------- +VREF ------          +-------------------+
    ´ `       ´ `               /                    |
  /     \   /     \   /    --- <  IN+                |
         `-´       `-´          |                    |
  --------- IN- -------         |            ADC     |
                                |                    |
  Common-mode voltage -->  --- <  IN-                |
                                \       +VREF  -VREF |
                                 +-------------------+
                                         ^       ^
                                         |       +---- External -VREF
                                  External +VREF

A **pseudo-differential unipolar** input has the limitations a differential
unipolar channel would have, meaning the analog voltage to the positive input
IN+ must stay within IN- to +VREF. The fixed voltage to IN- is often called
common-mode voltage and it must be within -VREF to +VREF as would be expected
from the signal to any differential channel negative input.

The voltage measured from IN+ is relative to IN- but, unlike differential
channels, pseudo-differential setups are intended to gauge single-ended input
signals. To enable applications to calculate IN+ voltage with respect to system
ground, the IIO channel may provide an ``_offset`` sysfs attribute to be added
to ADC output when converting raw data to voltage units. In many setups, the
common-mode voltage input is at GND level and the ``_offset`` attribute is
omitted due to being always zero.

Device tree example for pseudo-differential unipolar channel::

    adc@0 {
        ...
        #address-cells = <1>;
        #size-cells = <0>;

        channel@0 {
            reg = <0>;
            single-channel = <0>;
            common-mode-channel = <1>;
        };
    };

Do not set ``differential`` in the channel ``iio_chan_spec`` struct of
pseudo-differential channels.

1.3.2 Pseudo-differential Bipolar Channels
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

  -------- +VREF ------          +-------------------+
    ´ `       ´ `               /                    |
  /     \   /     \   /    --- <  IN+                |
         `-´       `-´          |                    |
  -------- -VREF ------         |            ADC     |
                                |                    |
  Common-mode voltage -->  --- <  IN-                |
                                \       +VREF  -VREF |
                                 +-------------------+
                                          ^       ^
                                          |       +---- External -VREF
                                   External +VREF

A **pseudo-differential bipolar** input is not limited by the level at IN- but
it will be limited to -VREF or to GND on the lower end of the input range
depending on the particular ADC. Similar to their unipolar counter parts,
pseudo-differential bipolar channels ought to declare an ``_offset`` attribute
to enable the conversion of raw ADC data to voltage units. For the setup with
IN- connected to GND, ``_offset`` is often omitted.

Device tree example for pseudo-differential bipolar channel::

    adc@0 {
        ...
        #address-cells = <1>;
        #size-cells = <0>;

        channel@0 {
            reg = <0>;
            bipolar;
            single-channel = <0>;
            common-mode-channel = <1>;
        };
    };
