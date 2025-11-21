.. SPDX-License-Identifier: GPL-2.0

.. _media_writing_camera_sensor_drivers:

Writing camera sensor drivers
=============================

This document covers the in-kernel APIs only. For the best practices on
userspace API implementation in camera sensor drivers, please see
:ref:`media_using_camera_sensor_drivers`.

CSI-2, parallel and BT.656 buses
--------------------------------

Please see :ref:`transmitter-receiver`.

Handling clocks
---------------

Camera sensors have an internal clock tree including a PLL and a number of
divisors. The clock tree is generally configured by the driver based on a few
input parameters that are specific to the hardware: the external clock frequency
and the link frequency. The two parameters generally are obtained from system
firmware. **No other frequencies should be used in any circumstances.**

The reason why the clock frequencies are so important is that the clock signals
come out of the SoC, and in many cases a specific frequency is designed to be
used in the system. Using another frequency may cause harmful effects
elsewhere. Therefore only the pre-determined frequencies are configurable by the
user.

The external clock frequency shall be retrieved by obtaining the external clock
using the ``devm_v4l2_sensor_clk_get()`` helper function, and then getting its
frequency with ``clk_get_rate()``. Usage of the helper function guarantees
correct behaviour regardless of whether the sensor is integrated in a DT-based
or ACPI-based system.

ACPI
~~~~

ACPI-based systems typically don't register the sensor external clock with the
kernel, but specify the external clock frequency in the ``clock-frequency``
_DSD property. The ``devm_v4l2_sensor_clk_get()`` helper creates and returns a
fixed clock set at that rate.

Devicetree
~~~~~~~~~~

Devicetree-based systems declare the sensor external clock in the device tree
and reference it from the sensor node. The preferred way to select the external
clock frequency is to use the ``assigned-clocks``, ``assigned-clock-parents``
and ``assigned-clock-rates`` properties in the sensor node to set the clock
rate. See the `clock device tree bindings
<https://github.com/devicetree-org/dt-schema/blob/main/dtschema/schemas/clock/clock.yaml>`_
for more information. The ``devm_v4l2_sensor_clk_get()`` helper retrieves and
returns that clock.

This approach has the drawback that there's no guarantee that the frequency
hasn't been modified directly or indirectly by another driver, or supported by
the board's clock tree to begin with. Changes to the Common Clock Framework API
are required to ensure reliability.

Power management
----------------

Camera sensors are used in conjunction with other devices to form a camera
pipeline. They must obey the rules listed herein to ensure coherent power
management over the pipeline.

Camera sensor drivers are responsible for controlling the power state of the
device they otherwise control as well. They shall use runtime PM to manage
power states. Runtime PM shall be enabled at probe time and disabled at remove
time. Drivers should enable runtime PM autosuspend. Also see
:ref:`async sub-device registration <media-registering-async-subdevs>`.

The runtime PM handlers shall handle clocks, regulators, GPIOs, and other
system resources required to power the sensor up and down. For drivers that
don't use any of those resources (such as drivers that support ACPI systems
only), the runtime PM handlers may be left unimplemented.

In general, the device shall be powered on at least when its registers are
being accessed and when it is streaming. Drivers should use
``pm_runtime_resume_and_get()`` when starting streaming and
``pm_runtime_put()`` or ``pm_runtime_put_autosuspend()`` when stopping
streaming. They may power the device up at probe time (for example to read
identification registers), but should not keep it powered unconditionally after
probe.

At system suspend time, the whole camera pipeline must stop streaming, and
restart when the system is resumed. This requires coordination between the
camera sensor and the rest of the camera pipeline. Bridge drivers are
responsible for this coordination, and instruct camera sensors to stop and
restart streaming by calling the appropriate subdev operations
(``.enable_streams()`` or ``.disable_streams()``). Camera sensor drivers shall
therefore **not** keep track of the streaming state to stop streaming in the PM
suspend handler and restart it in the resume handler. Drivers should in general
not implement the system PM handlers.

Camera sensor drivers shall **not** implement the subdev ``.s_power()``
operation, as it is deprecated. While this operation is implemented in some
existing drivers as they predate the deprecation, new drivers shall use runtime
PM instead. If you feel you need to begin calling ``.s_power()`` from an ISP or
a bridge driver, instead add runtime PM support to the sensor driver you are
using and drop its ``.s_power()`` handler.

Please also see :ref:`examples <media-camera-sensor-examples>`.

Control framework
~~~~~~~~~~~~~~~~~

``v4l2_ctrl_handler_setup()`` function may not be used in the device's runtime
PM ``runtime_resume`` callback, as it has no way to figure out the power state
of the device. This is because the power state of the device is only changed
after the power state transition has taken place. The ``s_ctrl`` callback can be
used to obtain device's power state after the power state transition:

.. c:function:: int pm_runtime_get_if_in_use(struct device *dev);

The function returns a non-zero value if it succeeded getting the power count or
runtime PM was disabled, in either of which cases the driver may proceed to
access the device.

Rotation, orientation and flipping
----------------------------------

Use ``v4l2_fwnode_device_parse()`` to obtain rotation and orientation
information from system firmware and ``v4l2_ctrl_new_fwnode_properties()`` to
register the appropriate controls.

.. _media-camera-sensor-examples:

Example drivers
---------------

Features implemented by sensor drivers vary, and depending on the set of
supported features and other qualities, particular sensor drivers better serve
the purpose of an example. The following drivers are known to be good examples:

.. flat-table:: Example sensor drivers
    :header-rows: 0
    :widths:      1 1 1 2

    * - Driver name
      - File(s)
      - Driver type
      - Example topic
    * - CCS
      - ``drivers/media/i2c/ccs/``
      - Freely configurable
      - Power management (ACPI and DT), UAPI
    * - imx219
      - ``drivers/media/i2c/imx219.c``
      - Register list based
      - Power management (DT), UAPI, mode selection
    * - imx319
      - ``drivers/media/i2c/imx319.c``
      - Register list based
      - Power management (ACPI and DT)
