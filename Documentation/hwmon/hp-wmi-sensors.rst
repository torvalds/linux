.. SPDX-License-Identifier: GPL-2.0-or-later

.. include:: <isonum.txt>

===========================
Linux HP WMI Sensors Driver
===========================

:Copyright: |copy| 2023 James Seo <james@equiv.tech>

Description
===========

Hewlett-Packard (and some HP Compaq) business-class computers report hardware
monitoring information via Windows Management Instrumentation (WMI).
This driver exposes that information to the Linux hwmon subsystem, allowing
userspace utilities like ``sensors`` to gather numeric sensor readings.

sysfs interface
===============

When the driver is loaded, it discovers the sensors available on the
system and creates the following sysfs attributes as necessary within
``/sys/class/hwmon/hwmon[X]``:

(``[X]`` is some number that depends on other system components.)

======================= ======= ===================================
Name                    Perm    Description
======================= ======= ===================================
``curr[X]_input``       RO      Current in milliamperes (mA).
``curr[X]_label``       RO      Current sensor label.
``fan[X]_input``        RO      Fan speed in RPM.
``fan[X]_label``        RO      Fan sensor label.
``fan[X]_fault``        RO      Fan sensor fault indicator.
``fan[X]_alarm``        RO      Fan sensor alarm indicator.
``in[X]_input``         RO      Voltage in millivolts (mV).
``in[X]_label``         RO      Voltage sensor label.
``temp[X]_input``       RO      Temperature in millidegrees Celsius
                                (m\ |deg|\ C).
``temp[X]_label``       RO      Temperature sensor label.
``temp[X]_fault``       RO      Temperature sensor fault indicator.
``temp[X]_alarm``       RO      Temperature sensor alarm indicator.
``intrusion[X]_alarm``  RW      Chassis intrusion alarm indicator.
======================= ======= ===================================

``fault`` attributes
  Reading ``1`` instead of ``0`` as the ``fault`` attribute for a sensor
  indicates that it has encountered some issue during operation such that
  measurements from it should not be trusted. If a sensor with the fault
  condition recovers later, reading this attribute will return ``0`` again.

``alarm`` attributes
  Reading ``1`` instead of ``0`` as the ``alarm`` attribute for a sensor
  indicates that one of the following has occurred, depending on its type:

  - ``fan``: The fan has stalled or has been disconnected while running.
  - ``temp``: The sensor reading has reached a critical threshold.
    The exact threshold is system-dependent.
  - ``intrusion``: The system's chassis has been opened.

  After ``1`` is read from an ``alarm`` attribute, the attribute resets itself
  and returns ``0`` on subsequent reads. As an exception, an
  ``intrusion[X]_alarm`` can only be manually reset by writing ``0`` to it.

debugfs interface
=================

.. warning:: The debugfs interface is subject to change without notice
             and is only available when the kernel is compiled with
             ``CONFIG_DEBUG_FS`` defined.

The standard hwmon interface in sysfs exposes sensors of several common types
that are connected as of driver initialization. However, there are usually
other sensors in WMI that do not meet these criteria. In addition, a number of
system-dependent "platform events objects" used for ``alarm`` attributes may
be present. A debugfs interface is therefore provided for read-only access to
all available HP WMI sensors and platform events objects.

``/sys/kernel/debug/hp-wmi-sensors-[X]/sensor``
contains one numbered entry per sensor with the following attributes:

=============================== =======================================
Name                            Example
=============================== =======================================
``name``                        ``CPU0 Fan``
``description``                 ``Reports CPU0 fan speed``
``sensor_type``                 ``12``
``other_sensor_type``           (an empty string)
``operational_status``          ``2``
``possible_states``             ``Normal,Caution,Critical,Not Present``
``current_state``               ``Normal``
``base_units``                  ``19``
``unit_modifier``               ``0``
``current_reading``             ``1008``
``rate_units``                  ``0`` (only exists on some systems)
=============================== =======================================

If platform events objects are available,
``/sys/kernel/debug/hp-wmi-sensors-[X]/platform_events``
contains one numbered entry per object with the following attributes:

=============================== ====================
Name                            Example
=============================== ====================
``name``                        ``CPU0 Fan Stall``
``description``                 ``CPU0 Fan Speed``
``source_namespace``            ``root\wmi``
``source_class``                ``HPBIOS_BIOSEvent``
``category``                    ``3``
``possible_severity``           ``25``
``possible_status``             ``5``
=============================== ====================

These represent the properties of the underlying ``HPBIOS_BIOSNumericSensor``
and ``HPBIOS_PlatformEvents`` WMI objects, which vary between systems.
See [#]_ for more details and Managed Object Format (MOF) definitions.

Known issues and limitations
============================

- If the existing hp-wmi driver for non-business-class HP systems is already
  loaded, ``alarm`` attributes will be unavailable even on systems that
  support them. This is because the same WMI event GUID used by this driver
  for ``alarm`` attributes is used on those systems for e.g. laptop hotkeys.
- Dubious sensor hardware and inconsistent BIOS WMI implementations have been
  observed to cause inaccurate readings and peculiar behavior, such as alarms
  failing to occur or occurring only once per boot.
- Only temperature, fan speed, and intrusion sensor types have been seen in
  the wild so far. Support for voltage and current sensors is therefore
  provisional.
- Although HP WMI sensors may claim to be of any type, any oddball sensor
  types unknown to hwmon will not be supported.

References
==========

.. [#] Hewlett-Packard Development Company, L.P.,
       "HP Client Management Interface Technical White Paper", 2005. [Online].
       Available: https://h20331.www2.hp.com/hpsub/downloads/cmi_whitepaper.pdf
