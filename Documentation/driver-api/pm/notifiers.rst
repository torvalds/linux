.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

=============================
Suspend/Hibernation Notifiers
=============================

:Copyright: |copy| 2016 Intel Corporation

:Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>


There are some operations that subsystems or drivers may want to carry out
before hibernation/suspend or after restore/resume, but they require the system
to be fully functional, so the drivers' and subsystems' ``->suspend()`` and
``->resume()`` or even ``->prepare()`` and ``->complete()`` callbacks are not
suitable for this purpose.

For example, device drivers may want to upload firmware to their devices after
resume/restore, but they cannot do it by calling :c:func:`request_firmware()`
from their ``->resume()`` or ``->complete()`` callback routines (user land
processes are frozen at these points).  The solution may be to load the firmware
into memory before processes are frozen and upload it from there in the
``->resume()`` routine.  A suspend/hibernation notifier may be used for that.

Subsystems or drivers having such needs can register suspend notifiers that
will be called upon the following events by the PM core:

``PM_HIBERNATION_PREPARE``
	The system is going to hibernate, tasks will be frozen immediately. This
	is different from ``PM_SUSPEND_PREPARE`` below,	because in this case
	additional work is done between the notifiers and the invocation of PM
	callbacks for the "freeze" transition.

``PM_POST_HIBERNATION``
	The system memory state has been restored from a hibernation image or an
	error occurred during hibernation.  Device restore callbacks have been
	executed and tasks have been thawed.

``PM_RESTORE_PREPARE``
	The system is going to restore a hibernation image.  If all goes well,
	the restored image kernel will issue a ``PM_POST_HIBERNATION``
	notification.

``PM_POST_RESTORE``
	An error occurred during restore from hibernation.  Device restore
	callbacks have been executed and tasks have been thawed.

``PM_SUSPEND_PREPARE``
	The system is preparing for suspend.

``PM_POST_SUSPEND``
	The system has just resumed or an error occurred during suspend.  Device
	resume callbacks have been executed and tasks have been thawed.

It is generally assumed that whatever the notifiers do for
``PM_HIBERNATION_PREPARE``, should be undone for ``PM_POST_HIBERNATION``.
Analogously, operations carried out for ``PM_SUSPEND_PREPARE`` should be
reversed for ``PM_POST_SUSPEND``.

Moreover, if one of the notifiers fails for the ``PM_HIBERNATION_PREPARE`` or
``PM_SUSPEND_PREPARE`` event, the notifiers that have already succeeded for that
event will be called for ``PM_POST_HIBERNATION`` or ``PM_POST_SUSPEND``,
respectively.

The hibernation and suspend notifiers are called with :c:data:`pm_mutex` held.
They are defined in the usual way, but their last argument is meaningless (it is
always NULL).

To register and/or unregister a suspend notifier use
:c:func:`register_pm_notifier()` and :c:func:`unregister_pm_notifier()`,
respectively (both defined in :file:`include/linux/suspend.h`).  If you don't
need to unregister the notifier, you can also use the :c:func:`pm_notifier()`
macro defined in :file:`include/linux/suspend.h`.
