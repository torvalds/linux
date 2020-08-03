.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _remote_controllers_sysfs_nodes:

*******************************
Remote Controller's sysfs nodes
*******************************

As defined at ``Documentation/ABI/testing/sysfs-class-rc``, those are
the sysfs nodes that control the Remote Controllers:


.. _sys_class_rc:

/sys/class/rc/
==============

The ``/sys/class/rc/`` class sub-directory belongs to the Remote
Controller core and provides a sysfs interface for configuring infrared
remote controller receivers.


.. _sys_class_rc_rcN:

/sys/class/rc/rcN/
==================

A ``/sys/class/rc/rcN`` directory is created for each remote control
receiver device where N is the number of the receiver.


.. _sys_class_rc_rcN_protocols:

/sys/class/rc/rcN/protocols
===========================

Reading this file returns a list of available protocols, something like::

	rc5 [rc6] nec jvc [sony]

Enabled protocols are shown in [] brackets.

Writing "+proto" will add a protocol to the list of enabled protocols.

Writing "-proto" will remove a protocol from the list of enabled
protocols.

Writing "proto" will enable only "proto".

Writing "none" will disable all protocols.

Write fails with ``EINVAL`` if an invalid protocol combination or unknown
protocol name is used.


.. _sys_class_rc_rcN_filter:

/sys/class/rc/rcN/filter
========================

Sets the scancode filter expected value.

Use in combination with ``/sys/class/rc/rcN/filter_mask`` to set the
expected value of the bits set in the filter mask. If the hardware
supports it then scancodes which do not match the filter will be
ignored. Otherwise the write will fail with an error.

This value may be reset to 0 if the current protocol is altered.


.. _sys_class_rc_rcN_filter_mask:

/sys/class/rc/rcN/filter_mask
=============================

Sets the scancode filter mask of bits to compare. Use in combination
with ``/sys/class/rc/rcN/filter`` to set the bits of the scancode which
should be compared against the expected value. A value of 0 disables the
filter to allow all valid scancodes to be processed.

If the hardware supports it then scancodes which do not match the filter
will be ignored. Otherwise the write will fail with an error.

This value may be reset to 0 if the current protocol is altered.


.. _sys_class_rc_rcN_wakeup_protocols:

/sys/class/rc/rcN/wakeup_protocols
==================================

Reading this file returns a list of available protocols to use for the
wakeup filter, something like::

	rc-5 nec nec-x rc-6-0 rc-6-6a-24 [rc-6-6a-32] rc-6-mce

Note that protocol variants are listed, so ``nec``, ``sony``, ``rc-5``, ``rc-6``
have their different bit length encodings listed if available.

Note that all protocol variants are listed.

The enabled wakeup protocol is shown in [] brackets.

Only one protocol can be selected at a time.

Writing "proto" will use "proto" for wakeup events.

Writing "none" will disable wakeup.

Write fails with ``EINVAL`` if an invalid protocol combination or unknown
protocol name is used, or if wakeup is not supported by the hardware.


.. _sys_class_rc_rcN_wakeup_filter:

/sys/class/rc/rcN/wakeup_filter
===============================

Sets the scancode wakeup filter expected value. Use in combination with
``/sys/class/rc/rcN/wakeup_filter_mask`` to set the expected value of
the bits set in the wakeup filter mask to trigger a system wake event.

If the hardware supports it and wakeup_filter_mask is not 0 then
scancodes which match the filter will wake the system from e.g. suspend
to RAM or power off. Otherwise the write will fail with an error.

This value may be reset to 0 if the wakeup protocol is altered.


.. _sys_class_rc_rcN_wakeup_filter_mask:

/sys/class/rc/rcN/wakeup_filter_mask
====================================

Sets the scancode wakeup filter mask of bits to compare. Use in
combination with ``/sys/class/rc/rcN/wakeup_filter`` to set the bits of
the scancode which should be compared against the expected value to
trigger a system wake event.

If the hardware supports it and wakeup_filter_mask is not 0 then
scancodes which match the filter will wake the system from e.g. suspend
to RAM or power off. Otherwise the write will fail with an error.

This value may be reset to 0 if the wakeup protocol is altered.
