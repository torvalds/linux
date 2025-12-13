.. SPDX-License-Identifier: GPL-2.0

===============================
The Linux kernel dpll subsystem
===============================

DPLL
====

PLL - Phase Locked Loop is an electronic circuit which syntonizes clock
signal of a device with an external clock signal. Effectively enabling
device to run on the same clock signal beat as provided on a PLL input.

DPLL - Digital Phase Locked Loop is an integrated circuit which in
addition to plain PLL behavior incorporates a digital phase detector
and may have digital divider in the loop. As a result, the frequency on
DPLL's input and output may be configurable.

Subsystem
=========

The main purpose of dpll subsystem is to provide general interface
to configure devices that use any kind of Digital PLL and could use
different sources of input signal to synchronize to, as well as
different types of outputs.
The main interface is NETLINK_GENERIC based protocol with an event
monitoring multicast group defined.

Device object
=============

Single dpll device object means single Digital PLL circuit and bunch of
connected pins.
It reports the supported modes of operation and current status to the
user in response to the `do` request of netlink command
``DPLL_CMD_DEVICE_GET`` and list of dplls registered in the subsystem
with `dump` netlink request of the same command.
Changing the configuration of dpll device is done with `do` request of
netlink ``DPLL_CMD_DEVICE_SET`` command.
A device handle is ``DPLL_A_ID``, it shall be provided to get or set
configuration of particular device in the system. It can be obtained
with a ``DPLL_CMD_DEVICE_GET`` `dump` request or
a ``DPLL_CMD_DEVICE_ID_GET`` `do` request, where the one must provide
attributes that result in single device match.

Pin object
==========

A pin is amorphic object which represents either input or output, it
could be internal component of the device, as well as externally
connected.
The number of pins per dpll vary, but usually multiple pins shall be
provided for a single dpll device.
Pin's properties, capabilities and status is provided to the user in
response to `do` request of netlink ``DPLL_CMD_PIN_GET`` command.
It is also possible to list all the pins that were registered in the
system with `dump` request of ``DPLL_CMD_PIN_GET`` command.
Configuration of a pin can be changed by `do` request of netlink
``DPLL_CMD_PIN_SET`` command.
Pin handle is a ``DPLL_A_PIN_ID``, it shall be provided to get or set
configuration of particular pin in the system. It can be obtained with
``DPLL_CMD_PIN_GET`` `dump` request or ``DPLL_CMD_PIN_ID_GET`` `do`
request, where user provides attributes that result in single pin match.

Pin selection
=============

In general, selected pin (the one which signal is driving the dpll
device) can be obtained from ``DPLL_A_PIN_STATE`` attribute, and only
one pin shall be in ``DPLL_PIN_STATE_CONNECTED`` state for any dpll
device.

Pin selection can be done either manually or automatically, depending
on hardware capabilities and active dpll device work mode
(``DPLL_A_MODE`` attribute). The consequence is that there are
differences for each mode in terms of available pin states, as well as
for the states the user can request for a dpll device.

In manual mode (``DPLL_MODE_MANUAL``) the user can request or receive
one of following pin states:

- ``DPLL_PIN_STATE_CONNECTED`` - the pin is used to drive dpll device
- ``DPLL_PIN_STATE_DISCONNECTED`` - the pin is not used to drive dpll
  device

In automatic mode (``DPLL_MODE_AUTOMATIC``) the user can request or
receive one of following pin states:

- ``DPLL_PIN_STATE_SELECTABLE`` - the pin shall be considered as valid
  input for automatic selection algorithm
- ``DPLL_PIN_STATE_DISCONNECTED`` - the pin shall be not considered as
  a valid input for automatic selection algorithm

In automatic mode (``DPLL_MODE_AUTOMATIC``) the user can only receive
pin state ``DPLL_PIN_STATE_CONNECTED`` once automatic selection
algorithm locks a dpll device with one of the inputs.

Shared pins
===========

A single pin object can be attached to multiple dpll devices.
Then there are two groups of configuration knobs:

1) Set on a pin - the configuration affects all dpll devices pin is
   registered to (i.e., ``DPLL_A_PIN_FREQUENCY``),
2) Set on a pin-dpll tuple - the configuration affects only selected
   dpll device (i.e., ``DPLL_A_PIN_PRIO``, ``DPLL_A_PIN_STATE``,
   ``DPLL_A_PIN_DIRECTION``).

MUX-type pins
=============

A pin can be MUX-type, it aggregates child pins and serves as a pin
multiplexer. One or more pins are registered with MUX-type instead of
being directly registered to a dpll device.
Pins registered with a MUX-type pin provide user with additional nested
attribute ``DPLL_A_PIN_PARENT_PIN`` for each parent they were registered
with.
If a pin was registered with multiple parent pins, they behave like a
multiple output multiplexer. In this case output of a
``DPLL_CMD_PIN_GET`` would contain multiple pin-parent nested
attributes with current state related to each parent, like::

        'pin': [{{
          'clock-id': 282574471561216,
          'module-name': 'ice',
          'capabilities': 4,
          'id': 13,
          'parent-pin': [
          {'parent-id': 2, 'state': 'connected'},
          {'parent-id': 3, 'state': 'disconnected'}
          ],
          'type': 'synce-eth-port'
          }}]

Only one child pin can provide its signal to the parent MUX-type pin at
a time, the selection is done by requesting change of a child pin state
on desired parent, with the use of ``DPLL_A_PIN_PARENT`` nested
attribute. Example of netlink `set state on parent pin` message format:

  ========================== =============================================
  ``DPLL_A_PIN_ID``          child pin id
  ``DPLL_A_PIN_PARENT_PIN``  nested attribute for requesting configuration
                             related to parent pin
    ``DPLL_A_PIN_PARENT_ID`` parent pin id
    ``DPLL_A_PIN_STATE``     requested pin state on parent
  ========================== =============================================

Pin priority
============

Some devices might offer a capability of automatic pin selection mode
(enum value ``DPLL_MODE_AUTOMATIC`` of ``DPLL_A_MODE`` attribute).
Usually, automatic selection is performed on the hardware level, which
means only pins directly connected to the dpll can be used for automatic
input pin selection.
In automatic selection mode, the user cannot manually select a input
pin for the device, instead the user shall provide all directly
connected pins with a priority ``DPLL_A_PIN_PRIO``, the device would
pick a highest priority valid signal and use it to control the DPLL
device. Example of netlink `set priority on parent pin` message format:

  ============================ =============================================
  ``DPLL_A_PIN_ID``            configured pin id
  ``DPLL_A_PIN_PARENT_DEVICE`` nested attribute for requesting configuration
                               related to parent dpll device
    ``DPLL_A_PIN_PARENT_ID``   parent dpll device id
    ``DPLL_A_PIN_PRIO``        requested pin prio on parent dpll
  ============================ =============================================

Child pin of MUX-type pin is not capable of automatic input pin selection,
in order to configure active input of a MUX-type pin, the user needs to
request desired pin state of the child pin on the parent pin,
as described in the ``MUX-type pins`` chapter.

Phase offset measurement and adjustment
========================================

Device may provide ability to measure a phase difference between signals
on a pin and its parent dpll device. If pin-dpll phase offset measurement
is supported, it shall be provided with ``DPLL_A_PIN_PHASE_OFFSET``
attribute for each parent dpll device. The reported phase offset may be
computed as the average of prior values and the current measurement, using
the following formula:

.. math::
   curr\_avg = prev\_avg * \frac{2^N-1}{2^N} + new\_val * \frac{1}{2^N}

where `curr_avg` is the current reported phase offset, `prev_avg` is the
previously reported value, `new_val` is the current measurement, and `N` is
the averaging factor. Configured averaging factor value is provided with
``DPLL_A_PHASE_OFFSET_AVG_FACTOR`` attribute of a device and value change can
be requested with the same attribute with ``DPLL_CMD_DEVICE_SET`` command.

  ================================== ======================================
  ``DPLL_A_PHASE_OFFSET_AVG_FACTOR`` attr configured value of phase offset
                                     averaging factor
  ================================== ======================================

Device may also provide ability to adjust a signal phase on a pin.
If pin phase adjustment is supported, minimal and maximal values that pin
handle shall be provide to the user on ``DPLL_CMD_PIN_GET`` respond
with ``DPLL_A_PIN_PHASE_ADJUST_MIN`` and ``DPLL_A_PIN_PHASE_ADJUST_MAX``
attributes. Configured phase adjust value is provided with
``DPLL_A_PIN_PHASE_ADJUST`` attribute of a pin, and value change can be
requested with the same attribute with ``DPLL_CMD_PIN_SET`` command.

  =============================== ======================================
  ``DPLL_A_PIN_ID``               configured pin id
  ``DPLL_A_PIN_PHASE_ADJUST_MIN`` attr minimum value of phase adjustment
  ``DPLL_A_PIN_PHASE_ADJUST_MAX`` attr maximum value of phase adjustment
  ``DPLL_A_PIN_PHASE_ADJUST``     attr configured value of phase
                                  adjustment on parent dpll device
  ``DPLL_A_PIN_PARENT_DEVICE``    nested attribute for requesting
                                  configuration on given parent dpll
                                  device
    ``DPLL_A_PIN_PARENT_ID``      parent dpll device id
    ``DPLL_A_PIN_PHASE_OFFSET``   attr measured phase difference
                                  between a pin and parent dpll device
  =============================== ======================================

All phase related values are provided in pico seconds, which represents
time difference between signals phase. The negative value means that
phase of signal on pin is earlier in time than dpll's signal. Positive
value means that phase of signal on pin is later in time than signal of
a dpll.

Phase adjust (also min and max) values are integers, but measured phase
offset values are fractional with 3-digit decimal places and shell be
divided with ``DPLL_PIN_PHASE_OFFSET_DIVIDER`` to get integer part and
modulo divided to get fractional part.

Phase offset monitor
====================

Phase offset measurement is typically performed against the current active
source. However, some DPLL (Digital Phase-Locked Loop) devices may offer
the capability to monitor phase offsets across all available inputs.
The attribute and current feature state shall be included in the response
message of the ``DPLL_CMD_DEVICE_GET`` command for supported DPLL devices.
In such cases, users can also control the feature using the
``DPLL_CMD_DEVICE_SET`` command by setting the ``enum dpll_feature_state``
values for the attribute.
Once enabled the phase offset measurements for the input shall be returned
in the ``DPLL_A_PIN_PHASE_OFFSET`` attribute.

  =============================== ========================
  ``DPLL_A_PHASE_OFFSET_MONITOR`` attr state of a feature
  =============================== ========================

Embedded SYNC
=============

Device may provide ability to use Embedded SYNC feature. It allows
to embed additional SYNC signal into the base frequency of a pin - a one
special pulse of base frequency signal every time SYNC signal pulse
happens. The user can configure the frequency of Embedded SYNC.
The Embedded SYNC capability is always related to a given base frequency
and HW capabilities. The user is provided a range of Embedded SYNC
frequencies supported, depending on current base frequency configured for
the pin.

  ========================================= =================================
  ``DPLL_A_PIN_ESYNC_FREQUENCY``            current Embedded SYNC frequency
  ``DPLL_A_PIN_ESYNC_FREQUENCY_SUPPORTED``  nest available Embedded SYNC
                                            frequency ranges
    ``DPLL_A_PIN_FREQUENCY_MIN``            attr minimum value of frequency
    ``DPLL_A_PIN_FREQUENCY_MAX``            attr maximum value of frequency
  ``DPLL_A_PIN_ESYNC_PULSE``                pulse type of Embedded SYNC
  ========================================= =================================

Reference SYNC
==============

The device may support the Reference SYNC feature, which allows the combination
of two inputs into a input pair. In this configuration, clock signals
from both inputs are used to synchronize the DPLL device. The higher frequency
signal is utilized for the loop bandwidth of the DPLL, while the lower frequency
signal is used to syntonize the output signal of the DPLL device. This feature
enables the provision of a high-quality loop bandwidth signal from an external
source.

A capable input provides a list of inputs that can be bound with to create
Reference SYNC. To control this feature, the user must request a desired
state for a target pin: use ``DPLL_PIN_STATE_CONNECTED`` to enable or
``DPLL_PIN_STATE_DISCONNECTED`` to disable the feature. An input pin can be
bound to only one other pin at any given time.

  ============================== ==========================================
  ``DPLL_A_PIN_REFERENCE_SYNC``  nested attribute for providing info or
                                 requesting configuration of the Reference
                                 SYNC feature
    ``DPLL_A_PIN_ID``            target pin id for Reference SYNC feature
    ``DPLL_A_PIN_STATE``         state of Reference SYNC connection
  ============================== ==========================================

Configuration commands group
============================

Configuration commands are used to get information about registered
dpll devices (and pins), as well as set configuration of device or pins.
As dpll devices must be abstracted and reflect real hardware,
there is no way to add new dpll device via netlink from user space and
each device should be registered by its driver.

All netlink commands require ``GENL_ADMIN_PERM``. This is to prevent
any spamming/DoS from unauthorized userspace applications.

List of netlink commands with possible attributes
=================================================

Constants identifying command types for dpll device uses a
``DPLL_CMD_`` prefix and suffix according to command purpose.
The dpll device related attributes use a ``DPLL_A_`` prefix and
suffix according to attribute purpose.

  ==================================== =================================
  ``DPLL_CMD_DEVICE_ID_GET``           command to get device ID
    ``DPLL_A_MODULE_NAME``             attr module name of registerer
    ``DPLL_A_CLOCK_ID``                attr Unique Clock Identifier
                                       (EUI-64), as defined by the
                                       IEEE 1588 standard
    ``DPLL_A_TYPE``                    attr type of dpll device
  ==================================== =================================

  ==================================== =================================
  ``DPLL_CMD_DEVICE_GET``              command to get device info or
                                       dump list of available devices
    ``DPLL_A_ID``                      attr unique dpll device ID
    ``DPLL_A_MODULE_NAME``             attr module name of registerer
    ``DPLL_A_CLOCK_ID``                attr Unique Clock Identifier
                                       (EUI-64), as defined by the
                                       IEEE 1588 standard
    ``DPLL_A_MODE``                    attr selection mode
    ``DPLL_A_MODE_SUPPORTED``          attr available selection modes
    ``DPLL_A_LOCK_STATUS``             attr dpll device lock status
    ``DPLL_A_TEMP``                    attr device temperature info
    ``DPLL_A_TYPE``                    attr type of dpll device
  ==================================== =================================

  ==================================== =================================
  ``DPLL_CMD_DEVICE_SET``              command to set dpll device config
    ``DPLL_A_ID``                      attr internal dpll device index
    ``DPLL_A_MODE``                    attr selection mode to configure
  ==================================== =================================

Constants identifying command types for pins uses a
``DPLL_CMD_PIN_`` prefix and suffix according to command purpose.
The pin related attributes use a ``DPLL_A_PIN_`` prefix and suffix
according to attribute purpose.

  ==================================== =================================
  ``DPLL_CMD_PIN_ID_GET``              command to get pin ID
    ``DPLL_A_PIN_MODULE_NAME``         attr module name of registerer
    ``DPLL_A_PIN_CLOCK_ID``            attr Unique Clock Identifier
                                       (EUI-64), as defined by the
                                       IEEE 1588 standard
    ``DPLL_A_PIN_BOARD_LABEL``         attr pin board label provided
                                       by registerer
    ``DPLL_A_PIN_PANEL_LABEL``         attr pin panel label provided
                                       by registerer
    ``DPLL_A_PIN_PACKAGE_LABEL``       attr pin package label provided
                                       by registerer
    ``DPLL_A_PIN_TYPE``                attr type of a pin
  ==================================== =================================

  ==================================== ==================================
  ``DPLL_CMD_PIN_GET``                 command to get pin info or dump
                                       list of available pins
    ``DPLL_A_PIN_ID``                  attr unique a pin ID
    ``DPLL_A_PIN_MODULE_NAME``         attr module name of registerer
    ``DPLL_A_PIN_CLOCK_ID``            attr Unique Clock Identifier
                                       (EUI-64), as defined by the
                                       IEEE 1588 standard
    ``DPLL_A_PIN_BOARD_LABEL``         attr pin board label provided
                                       by registerer
    ``DPLL_A_PIN_PANEL_LABEL``         attr pin panel label provided
                                       by registerer
    ``DPLL_A_PIN_PACKAGE_LABEL``       attr pin package label provided
                                       by registerer
    ``DPLL_A_PIN_TYPE``                attr type of a pin
    ``DPLL_A_PIN_FREQUENCY``           attr current frequency of a pin
    ``DPLL_A_PIN_FREQUENCY_SUPPORTED`` nested attr provides supported
                                       frequencies
      ``DPLL_A_PIN_ANY_FREQUENCY_MIN`` attr minimum value of frequency
      ``DPLL_A_PIN_ANY_FREQUENCY_MAX`` attr maximum value of frequency
    ``DPLL_A_PIN_PHASE_ADJUST_MIN``    attr minimum value of phase
                                       adjustment
    ``DPLL_A_PIN_PHASE_ADJUST_MAX``    attr maximum value of phase
                                       adjustment
    ``DPLL_A_PIN_PHASE_ADJUST``        attr configured value of phase
                                       adjustment on parent device
    ``DPLL_A_PIN_PARENT_DEVICE``       nested attr for each parent device
                                       the pin is connected with
      ``DPLL_A_PIN_PARENT_ID``         attr parent dpll device id
      ``DPLL_A_PIN_PRIO``              attr priority of pin on the
                                       dpll device
      ``DPLL_A_PIN_STATE``             attr state of pin on the parent
                                       dpll device
      ``DPLL_A_PIN_DIRECTION``         attr direction of a pin on the
                                       parent dpll device
      ``DPLL_A_PIN_PHASE_OFFSET``      attr measured phase difference
                                       between a pin and parent dpll
    ``DPLL_A_PIN_PARENT_PIN``          nested attr for each parent pin
                                       the pin is connected with
      ``DPLL_A_PIN_PARENT_ID``         attr parent pin id
      ``DPLL_A_PIN_STATE``             attr state of pin on the parent
                                       pin
    ``DPLL_A_PIN_CAPABILITIES``        attr bitmask of pin capabilities
  ==================================== ==================================

  ==================================== =================================
  ``DPLL_CMD_PIN_SET``                 command to set pins configuration
    ``DPLL_A_PIN_ID``                  attr unique a pin ID
    ``DPLL_A_PIN_FREQUENCY``           attr requested frequency of a pin
    ``DPLL_A_PIN_PHASE_ADJUST``        attr requested value of phase
                                       adjustment on parent device
    ``DPLL_A_PIN_PARENT_DEVICE``       nested attr for each parent dpll
                                       device configuration request
      ``DPLL_A_PIN_PARENT_ID``         attr parent dpll device id
      ``DPLL_A_PIN_DIRECTION``         attr requested direction of a pin
      ``DPLL_A_PIN_PRIO``              attr requested priority of pin on
                                       the dpll device
      ``DPLL_A_PIN_STATE``             attr requested state of pin on
                                       the dpll device
    ``DPLL_A_PIN_PARENT_PIN``          nested attr for each parent pin
                                       configuration request
      ``DPLL_A_PIN_PARENT_ID``         attr parent pin id
      ``DPLL_A_PIN_STATE``             attr requested state of pin on
                                       parent pin
  ==================================== =================================

Netlink dump requests
=====================

The ``DPLL_CMD_DEVICE_GET`` and ``DPLL_CMD_PIN_GET`` commands are
capable of dump type netlink requests, in which case the response is in
the same format as for their ``do`` request, but every device or pin
registered in the system is returned.

SET commands format
===================

``DPLL_CMD_DEVICE_SET`` - to target a dpll device, the user provides
``DPLL_A_ID``, which is unique identifier of dpll device in the system,
as well as parameter being configured (``DPLL_A_MODE``).

``DPLL_CMD_PIN_SET`` - to target a pin user must provide a
``DPLL_A_PIN_ID``, which is unique identifier of a pin in the system.
Also configured pin parameters must be added.
If ``DPLL_A_PIN_FREQUENCY`` is configured, this affects all the dpll
devices that are connected with the pin, that is why frequency attribute
shall not be enclosed in ``DPLL_A_PIN_PARENT_DEVICE``.
Other attributes: ``DPLL_A_PIN_PRIO``, ``DPLL_A_PIN_STATE`` or
``DPLL_A_PIN_DIRECTION`` must be enclosed in
``DPLL_A_PIN_PARENT_DEVICE`` as their configuration relates to only one
of parent dplls, targeted by ``DPLL_A_PIN_PARENT_ID`` attribute which is
also required inside that nest.
For MUX-type pins the ``DPLL_A_PIN_STATE`` attribute is configured in
similar way, by enclosing required state in ``DPLL_A_PIN_PARENT_PIN``
nested attribute and targeted parent pin id in ``DPLL_A_PIN_PARENT_ID``.

In general, it is possible to configure multiple parameters at once, but
internally each parameter change will be invoked separately, where order
of configuration is not guaranteed by any means.

Configuration pre-defined enums
===============================

.. kernel-doc:: include/uapi/linux/dpll.h

Notifications
=============

dpll device can provide notifications regarding status changes of the
device, i.e. lock status changes, input/output changes or other alarms.
There is one multicast group that is used to notify user-space apps via
netlink socket: ``DPLL_MCGRP_MONITOR``

Notifications messages:

  ============================== =====================================
  ``DPLL_CMD_DEVICE_CREATE_NTF`` dpll device was created
  ``DPLL_CMD_DEVICE_DELETE_NTF`` dpll device was deleted
  ``DPLL_CMD_DEVICE_CHANGE_NTF`` dpll device has changed
  ``DPLL_CMD_PIN_CREATE_NTF``    dpll pin was created
  ``DPLL_CMD_PIN_DELETE_NTF``    dpll pin was deleted
  ``DPLL_CMD_PIN_CHANGE_NTF``    dpll pin has changed
  ============================== =====================================

Events format is the same as for the corresponding get command.
Format of ``DPLL_CMD_DEVICE_`` events is the same as response of
``DPLL_CMD_DEVICE_GET``.
Format of ``DPLL_CMD_PIN_`` events is same as response of
``DPLL_CMD_PIN_GET``.

Device driver implementation
============================

Device is allocated by dpll_device_get() call. Second call with the
same arguments will not create new object but provides pointer to
previously created device for given arguments, it also increases
refcount of that object.
Device is deallocated by dpll_device_put() call, which first
decreases the refcount, once refcount is cleared the object is
destroyed.

Device should implement set of operations and register device via
dpll_device_register() at which point it becomes available to the
users. Multiple driver instances can obtain reference to it with
dpll_device_get(), as well as register dpll device with their own
ops and priv.

The pins are allocated separately with dpll_pin_get(), it works
similarly to dpll_device_get(). Function first creates object and then
for each call with the same arguments only the object refcount
increases. Also dpll_pin_put() works similarly to dpll_device_put().

A pin can be registered with parent dpll device or parent pin, depending
on hardware needs. Each registration requires registerer to provide set
of pin callbacks, and private data pointer for calling them:

- dpll_pin_register() - register pin with a dpll device,
- dpll_pin_on_pin_register() - register pin with another MUX type pin.

Notifications of adding or removing dpll devices are created within
subsystem itself.
Notifications about registering/deregistering pins are also invoked by
the subsystem.
Notifications about status changes either of dpll device or a pin are
invoked in two ways:

- after successful change was requested on dpll subsystem, the subsystem
  calls corresponding notification,
- requested by device driver with dpll_device_change_ntf() or
  dpll_pin_change_ntf() when driver informs about the status change.

The device driver using dpll interface is not required to implement all
the callback operation. Nevertheless, there are few required to be
implemented.
Required dpll device level callback operations:

- ``.mode_get``,
- ``.lock_status_get``.

Required pin level callback operations:

- ``.state_on_dpll_get`` (pins registered with dpll device),
- ``.state_on_pin_get`` (pins registered with parent pin),
- ``.direction_get``.

Every other operation handler is checked for existence and
``-EOPNOTSUPP`` is returned in case of absence of specific handler.

The simplest implementation is in the OCP TimeCard driver. The ops
structures are defined like this:

.. code-block:: c

	static const struct dpll_device_ops dpll_ops = {
		.lock_status_get = ptp_ocp_dpll_lock_status_get,
		.mode_get = ptp_ocp_dpll_mode_get,
		.mode_supported = ptp_ocp_dpll_mode_supported,
	};

	static const struct dpll_pin_ops dpll_pins_ops = {
		.frequency_get = ptp_ocp_dpll_frequency_get,
		.frequency_set = ptp_ocp_dpll_frequency_set,
		.direction_get = ptp_ocp_dpll_direction_get,
		.direction_set = ptp_ocp_dpll_direction_set,
		.state_on_dpll_get = ptp_ocp_dpll_state_get,
	};

The registration part is then looks like this part:

.. code-block:: c

        clkid = pci_get_dsn(pdev);
        bp->dpll = dpll_device_get(clkid, 0, THIS_MODULE);
        if (IS_ERR(bp->dpll)) {
                err = PTR_ERR(bp->dpll);
                dev_err(&pdev->dev, "dpll_device_alloc failed\n");
                goto out;
        }

        err = dpll_device_register(bp->dpll, DPLL_TYPE_PPS, &dpll_ops, bp);
        if (err)
                goto out;

        for (i = 0; i < OCP_SMA_NUM; i++) {
                bp->sma[i].dpll_pin = dpll_pin_get(clkid, i, THIS_MODULE, &bp->sma[i].dpll_prop);
                if (IS_ERR(bp->sma[i].dpll_pin)) {
                        err = PTR_ERR(bp->dpll);
                        goto out_dpll;
                }

                err = dpll_pin_register(bp->dpll, bp->sma[i].dpll_pin, &dpll_pins_ops,
                                        &bp->sma[i]);
                if (err) {
                        dpll_pin_put(bp->sma[i].dpll_pin);
                        goto out_dpll;
                }
        }

In the error path we have to rewind every allocation in the reverse order:

.. code-block:: c

        while (i) {
                --i;
                dpll_pin_unregister(bp->dpll, bp->sma[i].dpll_pin, &dpll_pins_ops, &bp->sma[i]);
                dpll_pin_put(bp->sma[i].dpll_pin);
        }
        dpll_device_put(bp->dpll);

More complex example can be found in Intel's ICE driver or nVidia's mlx5 driver.

SyncE enablement
================
For SyncE enablement it is required to allow control over dpll device
for a software application which monitors and configures the inputs of
dpll device in response to current state of a dpll device and its
inputs.
In such scenario, dpll device input signal shall be also configurable
to drive dpll with signal recovered from the PHY netdevice.
This is done by exposing a pin to the netdevice - attaching pin to the
netdevice itself with
``dpll_netdev_pin_set(struct net_device *dev, struct dpll_pin *dpll_pin)``.
Exposed pin id handle ``DPLL_A_PIN_ID`` is then identifiable by the user
as it is attached to rtnetlink respond to get ``RTM_NEWLINK`` command in
nested attribute ``IFLA_DPLL_PIN``.
