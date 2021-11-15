.. SPDX-License-Identifier: GPL-2.0-or-later

.. include:: <isonum.txt>

Kernel driver dell-smm-hwmon
============================

:Copyright: |copy| 2002-2005 Massimo Dal Zotto <dz@debian.org>
:Copyright: |copy| 2019 Giovanni Mascellani <gio@debian.org>

Description
-----------

On many Dell laptops the System Management Mode (SMM) BIOS can be
queried for the status of fans and temperature sensors.  Userspace
utilities like ``sensors`` can be used to return the readings. The
userspace suite `i8kutils`__ can also be used to read the sensors and
automatically adjust fan speed (please notice that it currently uses
the deprecated ``/proc/i8k`` interface).

 __ https://github.com/vitorafsr/i8kutils

``sysfs`` interface
-------------------

Temperature sensors and fans can be queried and set via the standard
``hwmon`` interface on ``sysfs``, under the directory
``/sys/class/hwmon/hwmonX`` for some value of ``X`` (search for the
``X`` such that ``/sys/class/hwmon/hwmonX/name`` has content
``dell_smm``). A number of other attributes can be read or written:

=============================== ======= =======================================
Name				Perm	Description
=============================== ======= =======================================
fan[1-3]_input                  RO      Fan speed in RPM.
fan[1-3]_label                  RO      Fan label.
fan[1-3]_min                    RO      Minimal Fan speed in RPM
fan[1-3]_max                    RO      Maximal Fan speed in RPM
fan[1-3]_target                 RO      Expected Fan speed in RPM
pwm[1-3]                        RW      Control the fan PWM duty-cycle.
pwm1_enable                     WO      Enable or disable automatic BIOS fan
                                        control (not supported on all laptops,
                                        see below for details).
temp[1-10]_input                RO      Temperature reading in milli-degrees
                                        Celsius.
temp[1-10]_label                RO      Temperature sensor label.
=============================== ======= =======================================

Disabling automatic BIOS fan control
------------------------------------

On some laptops the BIOS automatically sets fan speed every few
seconds. Therefore the fan speed set by mean of this driver is quickly
overwritten.

There is experimental support for disabling automatic BIOS fan
control, at least on laptops where the corresponding SMM command is
known, by writing the value ``1`` in the attribute ``pwm1_enable``
(writing ``2`` enables automatic BIOS control again). Even if you have
more than one fan, all of them are set to either enabled or disabled
automatic fan control at the same time and, notwithstanding the name,
``pwm1_enable`` sets automatic control for all fans.

If ``pwm1_enable`` is not available, then it means that SMM codes for
enabling and disabling automatic BIOS fan control are not whitelisted
for your hardware. It is possible that codes that work for other
laptops actually work for yours as well, or that you have to discover
new codes.

Check the list ``i8k_whitelist_fan_control`` in file
``drivers/hwmon/dell-smm-hwmon.c`` in the kernel tree: as a first
attempt you can try to add your machine and use an already-known code
pair. If, after recompiling the kernel, you see that ``pwm1_enable``
is present and works (i.e., you can manually control the fan speed),
then please submit your finding as a kernel patch, so that other users
can benefit from it. Please see
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
for information on submitting patches.

If no known code works on your machine, you need to resort to do some
probing, because unfortunately Dell does not publish datasheets for
its SMM. You can experiment with the code in `this repository`__ to
probe the BIOS on your machine and discover the appropriate codes.

 __ https://github.com/clopez/dellfan/

Again, when you find new codes, we'd be happy to have your patches!

Module parameters
-----------------

* force:bool
                   Force loading without checking for supported
                   models. (default: 0)

* ignore_dmi:bool
                   Continue probing hardware even if DMI data does not
                   match. (default: 0)

* restricted:bool
                   Allow fan control only to processes with the
                   ``CAP_SYS_ADMIN`` capability set or processes run
                   as root when using the legacy ``/proc/i8k``
                   interface. In this case normal users will be able
                   to read temperature and fan status but not to
                   control the fan.  If your notebook is shared with
                   other users and you don't trust them you may want
                   to use this option. (default: 1, only available
                   with ``CONFIG_I8K``)

* power_status:bool
                   Report AC status in ``/proc/i8k``. (default: 0,
                   only available with ``CONFIG_I8K``)

* fan_mult:uint
                   Factor to multiply fan speed with. (default:
                   autodetect)

* fan_max:uint
                   Maximum configurable fan speed. (default:
                   autodetect)

Legacy ``/proc`` interface
--------------------------

.. warning:: This interface is obsolete and deprecated and should not
             used in new applications. This interface is only
             available when kernel is compiled with option
             ``CONFIG_I8K``.

The information provided by the kernel driver can be accessed by
simply reading the ``/proc/i8k`` file. For example::

    $ cat /proc/i8k
    1.0 A17 2J59L02 52 2 1 8040 6420 1 2

The fields read from ``/proc/i8k`` are::

    1.0 A17 2J59L02 52 2 1 8040 6420 1 2
    |   |   |       |  | | |    |    | |
    |   |   |       |  | | |    |    | +------- 10. buttons status
    |   |   |       |  | | |    |    +--------- 9.  AC status
    |   |   |       |  | | |    +-------------- 8.  fan0 RPM
    |   |   |       |  | | +------------------- 7.  fan1 RPM
    |   |   |       |  | +--------------------- 6.  fan0 status
    |   |   |       |  +----------------------- 5.  fan1 status
    |   |   |       +-------------------------- 4.  temp0 reading (Celsius)
    |   |   +---------------------------------- 3.  Dell service tag (later known as 'serial number')
    |   +-------------------------------------- 2.  BIOS version
    +------------------------------------------ 1.  /proc/i8k format version

A negative value, for example -22, indicates that the BIOS doesn't
return the corresponding information. This is normal on some
models/BIOSes.

For performance reasons the ``/proc/i8k`` doesn't report by default
the AC status since this SMM call takes a long time to execute and is
not really needed.  If you want to see the ac status in ``/proc/i8k``
you must explictitly enable this option by passing the
``power_status=1`` parameter to insmod. If AC status is not
available -1 is printed instead.

The driver provides also an ioctl interface which can be used to
obtain the same information and to control the fan status. The ioctl
interface can be accessed from C programs or from shell using the
i8kctl utility. See the source file of ``i8kutils`` for more
information on how to use the ioctl interface.
