.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

.. _napi:

====
NAPI
====

NAPI is the event handling mechanism used by the Linux networking stack.
The name NAPI no longer stands for anything in particular [#]_.

In basic operation the device notifies the host about new events
via an interrupt.
The host then schedules a NAPI instance to process the events.
The device may also be polled for events via NAPI without receiving
interrupts first (:ref:`busy polling<poll>`).

NAPI processing usually happens in the software interrupt context,
but there is an option to use :ref:`separate kernel threads<threaded>`
for NAPI processing.

All in all NAPI abstracts away from the drivers the context and configuration
of event (packet Rx and Tx) processing.

Driver API
==========

The two most important elements of NAPI are the struct napi_struct
and the associated poll method. struct napi_struct holds the state
of the NAPI instance while the method is the driver-specific event
handler. The method will typically free Tx packets that have been
transmitted and process newly received packets.

.. _drv_ctrl:

Control API
-----------

netif_napi_add() and netif_napi_del() add/remove a NAPI instance
from the system. The instances are attached to the netdevice passed
as argument (and will be deleted automatically when netdevice is
unregistered). Instances are added in a disabled state.

napi_enable() and napi_disable() manage the disabled state.
A disabled NAPI can't be scheduled and its poll method is guaranteed
to not be invoked. napi_disable() waits for ownership of the NAPI
instance to be released.

The control APIs are not idempotent. Control API calls are safe against
concurrent use of datapath APIs but an incorrect sequence of control API
calls may result in crashes, deadlocks, or race conditions. For example,
calling napi_disable() multiple times in a row will deadlock.

Datapath API
------------

napi_schedule() is the basic method of scheduling a NAPI poll.
Drivers should call this function in their interrupt handler
(see :ref:`drv_sched` for more info). A successful call to napi_schedule()
will take ownership of the NAPI instance.

Later, after NAPI is scheduled, the driver's poll method will be
called to process the events/packets. The method takes a ``budget``
argument - drivers can process completions for any number of Tx
packets but should only process up to ``budget`` number of
Rx packets. Rx processing is usually much more expensive.

In other words, it is recommended to ignore the budget argument when
performing TX buffer reclamation to ensure that the reclamation is not
arbitrarily bounded; however, it is required to honor the budget argument
for RX processing.

.. warning::

   The ``budget`` argument may be 0 if core tries to only process Tx completions
   and no Rx packets.

The poll method returns the amount of work done. If the driver still
has outstanding work to do (e.g. ``budget`` was exhausted)
the poll method should return exactly ``budget``. In that case,
the NAPI instance will be serviced/polled again (without the
need to be scheduled).

If event processing has been completed (all outstanding packets
processed) the poll method should call napi_complete_done()
before returning. napi_complete_done() releases the ownership
of the instance.

.. warning::

   The case of finishing all events and using exactly ``budget``
   must be handled carefully. There is no way to report this
   (rare) condition to the stack, so the driver must either
   not call napi_complete_done() and wait to be called again,
   or return ``budget - 1``.

   If the ``budget`` is 0 napi_complete_done() should never be called.

Call sequence
-------------

Drivers should not make assumptions about the exact sequencing
of calls. The poll method may be called without the driver scheduling
the instance (unless the instance is disabled). Similarly,
it's not guaranteed that the poll method will be called, even
if napi_schedule() succeeded (e.g. if the instance gets disabled).

As mentioned in the :ref:`drv_ctrl` section - napi_disable() and subsequent
calls to the poll method only wait for the ownership of the instance
to be released, not for the poll method to exit. This means that
drivers should avoid accessing any data structures after calling
napi_complete_done().

.. _drv_sched:

Scheduling and IRQ masking
--------------------------

Drivers should keep the interrupts masked after scheduling
the NAPI instance - until NAPI polling finishes any further
interrupts are unnecessary.

Drivers which have to mask the interrupts explicitly (as opposed
to IRQ being auto-masked by the device) should use the napi_schedule_prep()
and __napi_schedule() calls:

.. code-block:: c

  if (napi_schedule_prep(&v->napi)) {
      mydrv_mask_rxtx_irq(v->idx);
      /* schedule after masking to avoid races */
      __napi_schedule(&v->napi);
  }

IRQ should only be unmasked after a successful call to napi_complete_done():

.. code-block:: c

  if (budget && napi_complete_done(&v->napi, work_done)) {
    mydrv_unmask_rxtx_irq(v->idx);
    return min(work_done, budget - 1);
  }

napi_schedule_irqoff() is a variant of napi_schedule() which takes advantage
of guarantees given by being invoked in IRQ context (no need to
mask interrupts). Note that PREEMPT_RT forces all interrupts
to be threaded so the interrupt may need to be marked ``IRQF_NO_THREAD``
to avoid issues on real-time kernel configurations.

Instance to queue mapping
-------------------------

Modern devices have multiple NAPI instances (struct napi_struct) per
interface. There is no strong requirement on how the instances are
mapped to queues and interrupts. NAPI is primarily a polling/processing
abstraction without specific user-facing semantics. That said, most networking
devices end up using NAPI in fairly similar ways.

NAPI instances most often correspond 1:1:1 to interrupts and queue pairs
(queue pair is a set of a single Rx and single Tx queue).

In less common cases a NAPI instance may be used for multiple queues
or Rx and Tx queues can be serviced by separate NAPI instances on a single
core. Regardless of the queue assignment, however, there is usually still
a 1:1 mapping between NAPI instances and interrupts.

It's worth noting that the ethtool API uses a "channel" terminology where
each channel can be either ``rx``, ``tx`` or ``combined``. It's not clear
what constitutes a channel; the recommended interpretation is to understand
a channel as an IRQ/NAPI which services queues of a given type. For example,
a configuration of 1 ``rx``, 1 ``tx`` and 1 ``combined`` channel is expected
to utilize 3 interrupts, 2 Rx and 2 Tx queues.

User API
========

User interactions with NAPI depend on NAPI instance ID. The instance IDs
are only visible to the user thru the ``SO_INCOMING_NAPI_ID`` socket option.
It's not currently possible to query IDs used by a given device.

Software IRQ coalescing
-----------------------

NAPI does not perform any explicit event coalescing by default.
In most scenarios batching happens due to IRQ coalescing which is done
by the device. There are cases where software coalescing is helpful.

NAPI can be configured to arm a repoll timer instead of unmasking
the hardware interrupts as soon as all packets are processed.
The ``gro_flush_timeout`` sysfs configuration of the netdevice
is reused to control the delay of the timer, while
``napi_defer_hard_irqs`` controls the number of consecutive empty polls
before NAPI gives up and goes back to using hardware IRQs.

.. _poll:

Busy polling
------------

Busy polling allows a user process to check for incoming packets before
the device interrupt fires. As is the case with any busy polling it trades
off CPU cycles for lower latency (production uses of NAPI busy polling
are not well known).

Busy polling is enabled by either setting ``SO_BUSY_POLL`` on
selected sockets or using the global ``net.core.busy_poll`` and
``net.core.busy_read`` sysctls. An io_uring API for NAPI busy polling
also exists.

IRQ mitigation
---------------

While busy polling is supposed to be used by low latency applications,
a similar mechanism can be used for IRQ mitigation.

Very high request-per-second applications (especially routing/forwarding
applications and especially applications using AF_XDP sockets) may not
want to be interrupted until they finish processing a request or a batch
of packets.

Such applications can pledge to the kernel that they will perform a busy
polling operation periodically, and the driver should keep the device IRQs
permanently masked. This mode is enabled by using the ``SO_PREFER_BUSY_POLL``
socket option. To avoid system misbehavior the pledge is revoked
if ``gro_flush_timeout`` passes without any busy poll call.

The NAPI budget for busy polling is lower than the default (which makes
sense given the low latency intention of normal busy polling). This is
not the case with IRQ mitigation, however, so the budget can be adjusted
with the ``SO_BUSY_POLL_BUDGET`` socket option.

.. _threaded:

Threaded NAPI
-------------

Threaded NAPI is an operating mode that uses dedicated kernel
threads rather than software IRQ context for NAPI processing.
The configuration is per netdevice and will affect all
NAPI instances of that device. Each NAPI instance will spawn a separate
thread (called ``napi/${ifc-name}-${napi-id}``).

It is recommended to pin each kernel thread to a single CPU, the same
CPU as the CPU which services the interrupt. Note that the mapping
between IRQs and NAPI instances may not be trivial (and is driver
dependent). The NAPI instance IDs will be assigned in the opposite
order than the process IDs of the kernel threads.

Threaded NAPI is controlled by writing 0/1 to the ``threaded`` file in
netdev's sysfs directory.

.. rubric:: Footnotes

.. [#] NAPI was originally referred to as New API in 2.4 Linux.
