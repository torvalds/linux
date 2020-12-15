.. SPDX-License-Identifier: GPL-2.0

V4L2 clocks
-----------

.. attention::

	This is a temporary API and it shall be replaced by the generic
	clock API, when the latter becomes widely available.

Many subdevices, like camera sensors, TV decoders and encoders, need a clock
signal to be supplied by the system. Often this clock is supplied by the
respective bridge device. The Linux kernel provides a Common Clock Framework for
this purpose. However, it is not (yet) available on all architectures. Besides,
the nature of the multi-functional (clock, data + synchronisation, I2C control)
connection of subdevices to the system might impose special requirements on the
clock API usage. E.g. V4L2 has to support clock provider driver unregistration
while a subdevice driver is holding a reference to the clock. For these reasons
a V4L2 clock helper API has been developed and is provided to bridge and
subdevice drivers.

The API consists of two parts: two functions to register and unregister a V4L2
clock source: v4l2_clk_register() and v4l2_clk_unregister() and calls to control
a clock object, similar to the respective generic clock API calls:
v4l2_clk_get(), v4l2_clk_put(), v4l2_clk_enable(), v4l2_clk_disable(),
v4l2_clk_get_rate(), and v4l2_clk_set_rate(). Clock suppliers have to provide
clock operations that will be called when clock users invoke respective API
methods.

It is expected that once the CCF becomes available on all relevant
architectures this API will be removed.
