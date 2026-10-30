==================================================
Runtime Power Management Framework for I/O Devices
==================================================

(C) 2009-2011 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.

(C) 2010 Alan Stern <stern@rowland.harvard.edu>

(C) 2014 Intel Corp., Rafael J. Wysocki <rafael.j.wysocki@intel.com>

.. _Section 1:

1. Introduction
===============

Runtime power management (or runtime PM, sometimes also referred to as RPM)
allows individual I/O devices that are not in active use to be put into
low-power states, in which they may not be operational or even accessible, and
go back to the fully operational state as needed.  Power can be reduced this
way without waiting for a transition into a system-wide sleep state.

Core Concepts
-------------

Runtime PM operates around a few device-level concepts — whether a device is
**active** or **suspended**; whether runtime PM is **enabled** or **disabled**;
whether runtime PM is **allowed** or **forbidden**; and whether a device is
**in use**.

* **Active**: Runtime PM tracks whether or not devices are in active use, which
  generally means that they are accessible and operational, and may be depended
  on by something (for example, other devices or user space).  This is
  represented by the **active** meta-state.  By contrast, the **suspended**
  meta-state represents a promise that the given device will not be accessed.
  This covers devices in low-power states, but it also may include devices that
  are not fully operational.  There are also two transient meta-states,
  **suspending** and **resuming**, representing transitions between **active**
  and **suspended** often referred to as *runtime suspend* and *runtime
  resume*, or just *suspend* and *resume*, respectively.  Every device handled
  by runtime PM is in one of these four meta-states at any time, and its
  meta-state is expected to reflect its actual physical configuration (that is,
  for instance, if the device is not accessible, it must not be **active**).
  The term "runtime PM status" used in what follows refers to the meta-states
  described above.

* **Enabled**: In order to track the runtime PM status and carry out
  transitions between **active** and **suspended**, runtime PM needs to be
  **enabled** for the given device.  If it is **disabled**, runtime PM
  generally leaves the device alone.  This means in particular that the runtime
  PM status of a device and its actual physical configuration may get out of
  sync after disabling runtime PM for it.  Additionally, the runtime PM status
  of a device may need to be explicitly adjusted to reflect its current
  physical configuration before enabling runtime PM for it (e.g., during device
  probe).  Once runtime PM is **enabled**, it begins managing PM status —
  performing *runtime suspend* and *runtime resume* transitions based on its
  understanding of whether a device is in use.

  As a rule, all devices are initialized with runtime PM **disabled**, though
  some bus types (such as PCI) manage some RPM initialization automatically,
  and therefore probe devices in an **enabled** state.  For other devices,
  drivers must enable runtime PM on their own to opt in.

* **Allowed**: Runtime PM may be **forbidden** even though it has been enabled.
  Doing so causes the given device to transition into the **active** meta-state
  and prevents it from being suspended.  In other words, a device with
  forbidden runtime PM remains **active** at least until runtime PM is
  **allowed** for it again.  This mechanism can be managed by in-kernel APIs,
  but more importantly, it is also available to user space via the
  ``/sys/devices/.../power/control`` sysfs attribute of the given device.
  Namely, writing ``on`` to that attribute forbids runtime PM, and writing
  ``auto`` allows it.  User space may modify that attribute at any time, so the
  ability to transition a device into the **suspended** meta-state via runtime
  PM must not be relied on for correctness.

* **In use**: Runtime PM determines whether a device should be transitioned to
  an **active** state by its understanding of whether a device is in use.  The
  first way a device is considered **in use** is by its own usage count — a
  reference counter driven by a variety of get()/put() APIs.  Additionally, a
  device may be considered **in use** by having active dependents — either
  child devices, or device-linked consumers.  When a device is no longer in
  use, runtime PM may choose to transition it to the **suspended** state.

  Thus, when a device moves from **suspended** to **active**, it may also cause
  its dependencies to resume, unless they are **active** already.  Conversely,
  suspending a device may also cause its otherwise-unused dependencies to
  suspend.

* **Autosuspend**: Runtime PM also supports a feature called "autosuspend."
  This is different than the ``control`` notion of "auto" (i.e., **allowed**).
  Autosuspend is covered in `Section 9`_.

Implementation Structure
------------------------

Support for runtime power management is provided at the power management core
(PM core) level by means of:

* Three device runtime PM callbacks in 'struct dev_pm_ops' (defined in
  include/linux/pm.h). See `Section 2`_.

* A number of runtime PM fields in the 'power' member of 'struct device' that
  can be used for synchronizing runtime PM operations with one another. These
  are covered in `Section 3`_.

* A set of helper functions defined in drivers/base/power/runtime.c that can be
  used for carrying out runtime PM operations in such a way that the
  synchronization between them is taken care of by the PM core. Bus types and
  device drivers are encouraged to use these functions. They are covered in
  `Section 4`_.

* The power management workqueue pm_wq in which bus types and device drivers can
  put their PM-related work items. It is strongly recommended that pm_wq be
  used for queuing all work items related to runtime PM, because this allows
  them to be synchronized with system-wide power transitions (suspend to RAM,
  hibernation and resume from system sleep states). pm_wq is declared in
  include/linux/pm_runtime.h and defined in kernel/power/main.c.

.. _Section 2:

2. Device Runtime PM Callbacks
==============================

There are three device runtime PM callbacks defined in 'struct dev_pm_ops'::

  struct dev_pm_ops {
	...
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
	...
  };

Most device drivers only need to implement ->runtime_suspend() and
->runtime_resume(). The ->runtime_idle() callback is optional and rarely
implemented by peripheral device drivers, as the PM core automatically handles
suspension and autosuspend when ->runtime_idle() is omitted (or returns 0).

Subsystem and Driver Callbacks
------------------------------

The ->runtime_suspend(), ->runtime_resume() and ->runtime_idle() callbacks
are executed by the PM core for the device's subsystem that may be either of
the following:

  1. PM domain of the device, if the device's PM domain object, dev->pm_domain,
     is present.

  2. Device type of the device, if both dev->type and dev->type->pm are present.

  3. Device class of the device, if both dev->class and dev->class->pm are
     present.

  4. Bus type of the device, if both dev->bus and dev->bus->pm are present.

If the subsystem chosen by applying the above rules doesn't provide the relevant
callback, the PM core will invoke the corresponding driver callback stored in
dev->driver->pm directly (if present).

The PM core always checks which callback to use in the order given above, so the
priority order of callbacks from high to low is: PM domain, device type, class
and bus type.  Moreover, the high-priority one will always take precedence over
a low-priority one.  The PM domain, bus type, device type and class callbacks
are referred to as subsystem-level callbacks in what follows.

By default, the callbacks are always invoked in process context with interrupts
enabled.  However, the pm_runtime_irq_safe() helper function can be used to tell
the PM core that it is safe to run the ->runtime_suspend(), ->runtime_resume()
and ->runtime_idle() callbacks for the given device in atomic context with
interrupts disabled.  This implies that the callback routines in question must
not block or sleep, but it also means that the synchronous helper functions
listed at the end of `Section 4`_ may be used for that device within an
interrupt handler or generally in an atomic context.

Callback Semantics
------------------

The subsystem-level suspend callback, if present, is _entirely_ _responsible_
for handling the suspend of the device as appropriate, which may, but need not
include executing the device driver's own ->runtime_suspend() callback (from the
PM core's point of view it is not necessary to implement a ->runtime_suspend()
callback in a device driver as long as the subsystem-level suspend callback
knows what to do to handle the device).

  * Once the subsystem-level suspend callback (or the driver suspend callback,
    if invoked directly) has completed successfully for the given device, the PM
    core regards the device as suspended, which need not mean that it has been
    put into a low power state.  It is supposed to mean, however, that the
    device will not process data and will not communicate with the CPU(s) and
    RAM until the appropriate resume callback is executed for it.  The runtime
    PM status of a device after successful execution of the suspend callback is
    'suspended'.

  * If the suspend callback returns -EBUSY or -EAGAIN, the device's runtime PM
    status remains 'active', which means that the device _must_ be fully
    operational afterwards.

  * If the suspend callback returns an error code different from -EBUSY and
    -EAGAIN, the PM core regards this as a fatal error and will refuse to run
    the helper functions described in `Section 4`_ for the device until its
    status is directly set to  either 'active', or 'suspended' (the PM core
    provides special helper functions for this purpose).

In particular, if the driver requires remote wakeup capability (i.e. hardware
mechanism allowing the device to request a change of its power state, such as
PCI PME) for proper functioning and device_can_wakeup() returns 'false' for the
device, then ->runtime_suspend() should return -EBUSY.  On the other hand, if
device_can_wakeup() returns 'true' for the device and the device is put into a
low-power state during the execution of the suspend callback, it is expected
that remote wakeup will be enabled for the device.  Generally, remote wakeup
should be enabled for all input devices put into low-power states at run time.

The subsystem-level resume callback, if present, is **entirely responsible** for
handling the resume of the device as appropriate, which may, but need not
include executing the device driver's own ->runtime_resume() callback (from the
PM core's point of view it is not necessary to implement a ->runtime_resume()
callback in a device driver as long as the subsystem-level resume callback knows
what to do to handle the device).

  * Once the subsystem-level resume callback (or the driver resume callback, if
    invoked directly) has completed successfully, the PM core regards the device
    as fully operational, which means that the device _must_ be able to complete
    I/O operations as needed.  The runtime PM status of the device is then
    'active'.

  * If the resume callback returns an error code, the PM core regards this as a
    fatal error and will refuse to run the helper functions described in
    `Section 4`_ for the device, until its status is directly set to either
    'active', or 'suspended' (by means of special helper functions provided by
    the PM core for this purpose).

The idle callback (a subsystem-level one, if present, or the driver one) is
executed by the PM core whenever the device appears to be idle, which is
indicated to the PM core by two counters, the device's usage counter and the
counter of 'active' children of the device.

  * If any of these counters is decreased using a helper function provided by
    the PM core and it turns out to be equal to zero, the other counter is
    checked.  If that counter also is equal to zero, the PM core executes the
    idle callback with the device as its argument.

The action performed by the idle callback is totally dependent on the subsystem
(or driver) in question, but the expected and recommended action is to check
if the device can be suspended (i.e. if all of the conditions necessary for
suspending the device are satisfied) and to queue up a suspend request for the
device in that case.  If there is no idle callback, or if the callback returns
0, then the PM core will attempt to carry out a runtime suspend of the device,
also respecting devices configured for autosuspend.  In essence this means a
call to pm_runtime_autosuspend().

To prevent this suspension (for example, if the callback routine has scheduled
a delayed suspend or determined the device cannot be idle), the routine must
return a non-zero value (typically -EBUSY or -EAGAIN).  Unlike
->runtime_suspend() and ->runtime_resume(), the PM core does not treat negative
return codes from ->runtime_idle() as a fatal device error; any non-zero value
simply stops the PM core from suspending the device.

Core Guarantees and Synchronization Rules
-----------------------------------------

The helper functions provided by the PM core, described in `Section 4`_,
guarantee that the following constraints are met with respect to runtime PM
callbacks for one device:

(1) The callbacks are mutually exclusive (e.g. it is forbidden to execute
    ->runtime_suspend() in parallel with ->runtime_resume() or with another
    instance of ->runtime_suspend() for the same device) with the exception that
    ->runtime_suspend() or ->runtime_resume() can be executed in parallel with
    ->runtime_idle() (although ->runtime_idle() will not be started while any
    of the other callbacks is being executed for the same device).

(2) ->runtime_idle() and ->runtime_suspend() can only be executed for 'active'
    devices (i.e. the PM core will only execute ->runtime_idle() or
    ->runtime_suspend() for the devices the runtime PM status of which is
    'active').

(3) ->runtime_idle() and ->runtime_suspend() can only be executed for a device
    the usage counter of which is equal to zero _and_ either the counter of
    'active' children of which is equal to zero, or the 'power.ignore_children'
    flag of which is set.

(4) ->runtime_resume() can only be executed for 'suspended' devices  (i.e. the
    PM core will only execute ->runtime_resume() for the devices the runtime
    PM status of which is 'suspended').

Additionally, the helper functions provided by the PM core obey the following
rules:

  * If ->runtime_suspend() is about to be executed or there's a pending request
    to execute it, ->runtime_idle() will not be executed for the same device.

  * A request to execute or to schedule the execution of ->runtime_suspend()
    will cancel any pending requests to execute ->runtime_idle() for the same
    device.

  * If ->runtime_resume() is about to be executed or there's a pending request
    to execute it, the other callbacks will not be executed for the same device.

  * A request to execute ->runtime_resume() will cancel any pending or
    scheduled requests to execute the other callbacks for the same device,
    except for scheduled autosuspends.

.. _Section 3:

3. Runtime PM Device Fields
===========================

Device PM fields are found in 'struct dev_pm_info', as defined in
include/linux/pm.h. Many of those fields track runtime PM configuration and
state.

.. kernel-doc:: include/linux/pm.h
   :identifiers: dev_pm_info

.. _Section 4:

4. Runtime PM Device Helper Functions
=====================================

The following runtime PM helper functions are defined in
drivers/base/power/runtime.c and include/linux/pm_runtime.h:

.. kernel-doc:: drivers/base/power/runtime.c
   :export:

.. kernel-doc:: include/linux/pm_runtime.h

It is safe to execute the following helper functions from interrupt context:

- pm_request_idle()
- pm_request_autosuspend()
- pm_schedule_suspend()
- pm_request_resume()
- pm_runtime_get_noresume()
- pm_runtime_get()
- pm_runtime_get_if_active()
- pm_runtime_get_if_in_use()
- pm_runtime_put_noidle()
- pm_runtime_put()
- pm_runtime_put_autosuspend()
- __pm_runtime_put_autosuspend()
- pm_runtime_enable()
- pm_suspend_ignore_children()
- pm_runtime_set_active()
- pm_runtime_set_suspended()
- pm_runtime_suspended()
- pm_runtime_active()
- pm_runtime_status_suspended()
- pm_runtime_enabled()
- pm_runtime_mark_last_busy()
- pm_runtime_autosuspend_expiration()

If pm_runtime_irq_safe() has been called for a device then the following helper
functions may also be used in interrupt context:

- pm_runtime_idle()
- pm_runtime_suspend()
- pm_runtime_autosuspend()
- pm_runtime_resume()
- pm_runtime_get_sync()
- pm_runtime_resume_and_get()
- pm_runtime_put_sync()
- pm_runtime_put_sync_suspend()
- pm_runtime_put_sync_autosuspend()

.. _Section 5:

5. Runtime PM Initialization, Device Probing and Removal
========================================================

Initially, the runtime PM is disabled for all devices, which means that the
majority of the runtime PM helper functions described in `Section 4`_ will
return -EACCES until pm_runtime_enable() is called for the device.

In addition to that, the initial runtime PM status of all devices is
'suspended', but it need not reflect the actual physical state of the device.
Thus, if the device is initially active (i.e. it is able to process I/O), its
runtime PM status must be changed to 'active', with the help of
pm_runtime_set_active(), before pm_runtime_enable() is called for the device.

However, if the device has a parent and the parent's runtime PM is enabled,
calling pm_runtime_set_active() for the device will affect the parent, unless
the parent's 'power.ignore_children' flag is set.  Namely, in that case the
parent won't be able to suspend at run time, using the PM core's helper
functions, as long as the child's status is 'active', even if the child's
runtime PM is still disabled (i.e. pm_runtime_enable() hasn't been called for
the child yet or pm_runtime_disable() has been called for it).  For this reason,
once pm_runtime_set_active() has been called for the device, pm_runtime_enable()
should be called for it too as soon as reasonably possible or its runtime PM
status should be changed back to 'suspended' with the help of
pm_runtime_set_suspended().

If the default initial runtime PM status of the device (i.e. 'suspended')
reflects the actual state of the device, its bus type's or its driver's
->probe() callback will likely need to wake it up using one of the PM core's
helper functions described in `Section 4`_.  In that case, pm_runtime_resume()
should be used.  Of course, for this purpose the device's runtime PM has to be
enabled earlier by calling pm_runtime_enable().

Note, if the device may execute pm_runtime calls during the probe (such as
if it is registered with a subsystem that may call back in) then the
pm_runtime_resume_and_get() call paired with a pm_runtime_put() call will be
appropriate to ensure that the device is not put back to sleep during the
probe. This can happen with systems such as the network device layer.

It may be desirable to suspend the device once ->probe() has finished.
Therefore the driver core uses the asynchronous pm_request_idle() to submit a
request to execute the subsystem-level idle callback for the device at that
time.  A driver that makes use of the runtime autosuspend feature may want to
update the last busy mark before returning from ->probe().

Moreover, the driver core prevents runtime PM callbacks from racing with the bus
notifier callback in __device_release_driver(), which is necessary because the
notifier is used by some subsystems to carry out operations affecting the
runtime PM functionality.  It does so by calling pm_runtime_get_sync() before
driver_sysfs_remove() and the BUS_NOTIFY_UNBIND_DRIVER notifications.  This
resumes the device if it's in the suspended state and prevents it from
being suspended again while those routines are being executed.

To allow bus types and drivers to put devices into the suspended state by
calling pm_runtime_suspend() from their ->remove() routines, the driver core
executes pm_runtime_put_sync() after running the BUS_NOTIFY_UNBIND_DRIVER
notifications in __device_release_driver().  This requires bus types and
drivers to make their ->remove() callbacks avoid races with runtime PM directly,
but it also allows more flexibility in the handling of devices during the
removal of their drivers.

Drivers in ->remove() callback should undo the runtime PM changes done
in ->probe(). Usually this means calling pm_runtime_disable() etc.
Alternatively, drivers can use devm_pm_runtime_enable() during probe, which
automatically takes care of calling pm_runtime_disable() upon driver detachment.
Any pm_runtime_use_autosuspend() calls in a driver will be automatically undone
on driver exit.

The user space can effectively disallow the driver of the device to power manage
it at run time by changing the value of its /sys/devices/.../power/control
attribute to "on", which causes pm_runtime_forbid() to be called.  In principle,
this mechanism may also be used by the driver to effectively turn off the
runtime power management of the device until the user space turns it on.
Namely, during the initialization the driver can make sure that the runtime PM
status of the device is 'active' and call pm_runtime_forbid().  It should be
noted, however, that if the user space has already intentionally changed the
value of /sys/devices/.../power/control to "auto" to allow the driver to power
manage the device at run time, the driver may confuse it by using
pm_runtime_forbid() this way.

.. _Section 6:

6. Runtime PM and System Sleep
==============================

Runtime PM and system sleep (i.e., system suspend and hibernation, also known
as suspend-to-RAM and suspend-to-disk) interact with each other in a couple of
ways.  If a device is active when a system sleep starts, everything is
straightforward.  But what should happen if the device is already suspended?

The device may have different wake-up settings for runtime PM and system sleep.
For example, remote wake-up may be enabled for runtime suspend but disallowed
for system sleep (device_may_wakeup(dev) returns 'false').  When this happens,
the subsystem-level system suspend callback is responsible for changing the
device's wake-up setting (it may leave that to the device driver's system
suspend routine).  It may be necessary to resume the device and suspend it again
in order to do so.  The same is true if the driver uses different power levels
or other settings for runtime suspend and system sleep.

During system resume, the simplest approach is to bring all devices back to full
power, even if they had been suspended before the system suspend began.  There
are several reasons for this, including:

  * The device might need to switch power levels, wake-up settings, etc.

  * Remote wake-up events might have been lost by the firmware.

  * The device's children may need the device to be at full power in order
    to resume themselves.

  * The driver's idea of the device state may not agree with the device's
    physical state.  This can happen during resume from hibernation.

  * The device might need to be reset.

  * Even though the device was suspended, if its usage counter was > 0 then most
    likely it would need a runtime resume in the near future anyway.

If the device had been suspended before the system suspend began and it's
brought back to full power during resume, then its runtime PM status will have
to be updated to reflect the actual post-system sleep status.  The way to do
this is:

	 - pm_runtime_disable(dev);
	 - pm_runtime_set_active(dev);
	 - pm_runtime_enable(dev);

The PM core always increments the runtime usage counter before calling the
->suspend() callback and decrements it after calling the ->resume() callback.
Hence disabling runtime PM temporarily like this will not cause any runtime
suspend attempts to be permanently lost.  If the usage count goes to zero
following the return of the ->resume() callback, the ->runtime_idle() callback
will be invoked as usual.

On some systems, however, system sleep is not entered through a global firmware
or hardware operation.  Instead, all hardware components are put into low-power
states directly by the kernel in a coordinated way.  Then, the system sleep
state effectively follows from the states the hardware components end up in
and the system is woken up from that state by a hardware interrupt or a similar
mechanism entirely under the kernel's control.  As a result, the kernel never
gives control away and the states of all devices during resume are precisely
known to it.  If that is the case and none of the situations listed above takes
place (in particular, if the system is not waking up from hibernation), it may
be more efficient to leave the devices that had been suspended before the system
suspend began in the suspended state.

To this end, the PM core provides a mechanism allowing some coordination between
different levels of device hierarchy.  Namely, if a system suspend .prepare()
callback returns a positive number for a device, that indicates to the PM core
that the device appears to be runtime-suspended and its state is fine, so it
may be left in runtime suspend provided that all of its descendants are also
left in runtime suspend.  If that happens, the PM core will not execute any
system suspend and resume callbacks for all of those devices, except for the
.complete() callback, which is then entirely responsible for handling the device
as appropriate.  This only applies to system suspend transitions that are not
related to hibernation (see Documentation/driver-api/pm/devices.rst for more
information).

The PM core does its best to reduce the probability of race conditions between
the runtime PM and system suspend/resume (and hibernation) callbacks by carrying
out the following operations:

  * During system suspend pm_runtime_get_noresume() is called for every device
    right before executing the subsystem-level .prepare() callback for it and
    pm_runtime_barrier() is called for every device right before executing the
    subsystem-level .suspend() callback for it.  In addition to that, the PM
    core disables runtime PM for every device right before executing the
    subsystem-level .suspend_late() callback for it.

  * During system resume pm_runtime_enable() and pm_runtime_put() are called for
    every device right after executing the subsystem-level .resume_early()
    callback and right after executing the subsystem-level .complete() callback
    for it, respectively.

.. _Section 7:

7. Generic subsystem callbacks
==============================

Subsystems may wish to conserve code space by using the set of generic power
management callbacks provided by the PM core, defined in
drivers/base/power/generic_ops.c:

.. kernel-doc:: drivers/base/power/generic_ops.c
   :export:

These functions are the defaults used by the PM core if a subsystem doesn't
provide its own callbacks for ->runtime_idle(), ->runtime_suspend(),
->runtime_resume(), ->suspend(), ->suspend_noirq(), ->resume(),
->resume_noirq(), ->freeze(), ->freeze_noirq(), ->thaw(), ->thaw_noirq(),
->poweroff(), ->poweroff_noirq(), ->restore(), ->restore_noirq() in the
subsystem-level dev_pm_ops structure.

Device drivers that wish to use the same function as a system suspend, freeze,
poweroff and runtime suspend callback, and similarly for system resume, thaw,
restore, and runtime resume, can achieve similar behaviour with the help of the
DEFINE_RUNTIME_DEV_PM_OPS() macro defined in include/linux/pm_runtime.h
(possibly setting its last argument to NULL).

.. _Section 8:

8. "No-Callback" Devices
========================

Some "devices" are only logical sub-devices of their parent and cannot be
power-managed on their own.  (The prototype example is a USB interface.  Entire
USB devices can go into low-power mode or send wake-up requests, but neither is
possible for individual interfaces.)  The drivers for these devices have no
need of runtime PM callbacks; if the callbacks did exist, ->runtime_suspend()
and ->runtime_resume() would always return 0 without doing anything else and
->runtime_idle() would always call pm_runtime_suspend().

Subsystems can tell the PM core about these devices by calling
pm_runtime_no_callbacks().  This should be done after the device structure is
initialized and before it is registered (although after device registration is
also okay).  The routine will set the device's power.no_callbacks flag and
prevent the non-debugging runtime PM sysfs attributes from being created.

When power.no_callbacks is set, the PM core will not invoke the
->runtime_idle(), ->runtime_suspend(), or ->runtime_resume() callbacks.
Instead it will assume that suspends and resumes always succeed and that idle
devices should be suspended.

As a consequence, the PM core will never directly inform the device's subsystem
or driver about runtime power changes.  Instead, the driver for the device's
parent must take responsibility for telling the device's driver when the
parent's power state changes.

Note that, in some cases it may not be desirable for subsystems/drivers to call
pm_runtime_no_callbacks() for their devices. This could be because a subset of
the runtime PM callbacks needs to be implemented, a platform dependent PM
domain could get attached to the device or that the device is power managed
through a supplier device link. For these reasons and to avoid boilerplate code
in subsystems/drivers, the PM core allows runtime PM callbacks to be
unassigned. More precisely, if a callback pointer is NULL, the PM core will act
as though there was a callback and it returned 0.

.. _Section 9:

9. Autosuspend, or automatically-delayed suspends
=================================================

Changing a device's power state isn't free; it requires both time and energy.
A device should be put in a low-power state only when there's some reason to
think it will remain in that state for a substantial time.  A common heuristic
says that a device which hasn't been used for a while is liable to remain
unused; following this advice, drivers should not allow devices to be suspended
at runtime until they have been inactive for some minimum period.  Even when
the heuristic ends up being non-optimal, it will still prevent devices from
"bouncing" too rapidly between low-power and full-power states.

The term "autosuspend" is an historical remnant.  It doesn't mean that the
device is automatically suspended (the subsystem or driver still has to call
the appropriate PM routines); rather it means that runtime suspends will
automatically be delayed until the desired period of inactivity has elapsed.

Inactivity is determined based on the power.last_busy field. The desired length
of the inactivity period is a matter of policy.  Subsystems can set this length
initially by calling pm_runtime_set_autosuspend_delay(), but after device
registration the length should be controlled by user space, using the
/sys/devices/.../power/autosuspend_delay_ms attribute.

In order to use autosuspend, subsystems or drivers must call
pm_runtime_use_autosuspend() (preferably before registering the device), and
thereafter they should use the various \*_autosuspend() helper functions
instead of the non-autosuspend counterparts::

	Instead of: pm_runtime_suspend    use: pm_runtime_autosuspend;
	Instead of: pm_schedule_suspend   use: pm_request_autosuspend;
	Instead of: pm_runtime_put        use: pm_runtime_put_autosuspend;
	Instead of: pm_runtime_put_sync   use: pm_runtime_put_sync_autosuspend.

Drivers may also continue to use the non-autosuspend helper functions; they
will behave normally, which means sometimes taking the autosuspend delay into
account (see pm_runtime_idle()). The autosuspend variants of the functions also
call pm_runtime_mark_last_busy().

Under some circumstances a driver or subsystem may want to prevent a device
from autosuspending immediately, even though the usage counter is zero and the
autosuspend delay time has expired.  If the ->runtime_suspend() callback
returns -EAGAIN or -EBUSY, and if the next autosuspend delay expiration time is
in the future (as it normally would be if the callback invoked
pm_runtime_mark_last_busy()), the PM core will automatically reschedule the
autosuspend.  The ->runtime_suspend() callback can't do this rescheduling
itself because no suspend requests of any kind are accepted while the device is
suspending (i.e., while the callback is running).

The implementation is well suited for asynchronous use in interrupt contexts.
However such use inevitably involves races, because the PM core can't
synchronize ->runtime_suspend() callbacks with the arrival of I/O requests.
This synchronization must be handled by the driver, using its private lock.
Here is a schematic pseudo-code example::

	foo_read_or_write(struct foo_priv *foo, void *data)
	{
		lock(&foo->private_lock);
		add_request_to_io_queue(foo, data);
		if (foo->num_pending_requests++ == 0)
			pm_runtime_get(&foo->dev);
		if (!foo->is_suspended)
			foo_process_next_request(foo);
		unlock(&foo->private_lock);
	}

	foo_io_completion(struct foo_priv *foo, void *req)
	{
		lock(&foo->private_lock);
		if (--foo->num_pending_requests == 0)
			pm_runtime_put_autosuspend(&foo->dev);
		else
			foo_process_next_request(foo);
		unlock(&foo->private_lock);
		/* Send req result back to the user ... */
	}

	int foo_runtime_suspend(struct device *dev)
	{
		struct foo_priv *foo = container_of(dev, ...);
		int ret = 0;

		lock(&foo->private_lock);
		if (foo->num_pending_requests > 0) {
			ret = -EBUSY;
		} else {
			/* ... suspend the device ... */
			foo->is_suspended = 1;
		}
		unlock(&foo->private_lock);
		return ret;
	}

	int foo_runtime_resume(struct device *dev)
	{
		struct foo_priv *foo = container_of(dev, ...);

		lock(&foo->private_lock);
		/* ... resume the device ... */
		foo->is_suspended = 0;
		pm_runtime_mark_last_busy(&foo->dev);
		if (foo->num_pending_requests > 0)
			foo_process_next_request(foo);
		unlock(&foo->private_lock);
		return 0;
	}

The important point is that after foo_io_completion() asks for an autosuspend,
the foo_runtime_suspend() callback may race with foo_read_or_write().
Therefore foo_runtime_suspend() has to check whether there are any pending I/O
requests (while holding the private lock) before allowing the suspend to
proceed.

In addition, the power.autosuspend_delay field can be changed by user space at
any time.  If a driver cares about this, it can call
pm_runtime_autosuspend_expiration() from within the ->runtime_suspend()
callback while holding its private lock.  If the function returns a nonzero
value then the delay has not yet expired and the callback should return
-EAGAIN.
