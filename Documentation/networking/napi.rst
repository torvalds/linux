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

In other words for Rx processing the ``budget`` argument limits how many
packets driver can process in a single poll. Rx specific APIs like page
pool or XDP cannot be used at all when ``budget`` is 0.
skb Tx processing should happen regardless of the ``budget``, but if
the argument is 0 driver cannot call any XDP (or page pool) APIs.

.. warning::

   The ``budget`` argument may be 0 if core tries to only process
   skb Tx completions and no Rx or XDP packets.

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
mask interrupts). napi_schedule_irqoff() will fall back to napi_schedule() if
IRQs are threaded (such as if ``PREEMPT_RT`` is enabled).

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

The above parameters can also be set on a per-NAPI basis using netlink via
netdev-genl. When used with netlink and configured on a per-NAPI basis, the
parameters mentioned above use hyphens instead of underscores:
``gro-flush-timeout`` and ``napi-defer-hard-irqs``.

Per-NAPI configuration can be done programmatically in a user application
or by using a script included in the kernel source tree:
``tools/net/ynl/cli.py``.

For example, using the script:

.. code-block:: bash

  $ kernel-source/tools/net/ynl/cli.py \
            --spec Documentation/netlink/specs/netdev.yaml \
            --do napi-set \
            --json='{"id": 345,
                     "defer-hard-irqs": 111,
                     "gro-flush-timeout": 11111}'

Similarly, the parameter ``irq-suspend-timeout`` can be set using netlink
via netdev-genl. There is no global sysfs parameter for this value.

``irq-suspend-timeout`` is used to determine how long an application can
completely suspend IRQs. It is used in combination with SO_PREFER_BUSY_POLL,
which can be set on a per-epoll context basis with ``EPIOCSPARAMS`` ioctl.

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

epoll-based busy polling
------------------------

It is possible to trigger packet processing directly from calls to
``epoll_wait``. In order to use this feature, a user application must ensure
all file descriptors which are added to an epoll context have the same NAPI ID.

If the application uses a dedicated acceptor thread, the application can obtain
the NAPI ID of the incoming connection using SO_INCOMING_NAPI_ID and then
distribute that file descriptor to a worker thread. The worker thread would add
the file descriptor to its epoll context. This would ensure each worker thread
has an epoll context with FDs that have the same NAPI ID.

Alternatively, if the application uses SO_REUSEPORT, a bpf or ebpf program can
be inserted to distribute incoming connections to threads such that each thread
is only given incoming connections with the same NAPI ID. Care must be taken to
carefully handle cases where a system may have multiple NICs.

In order to enable busy polling, there are two choices:

1. ``/proc/sys/net/core/busy_poll`` can be set with a time in useconds to busy
   loop waiting for events. This is a system-wide setting and will cause all
   epoll-based applications to busy poll when they call epoll_wait. This may
   not be desirable as many applications may not have the need to busy poll.

2. Applications using recent kernels can issue an ioctl on the epoll context
   file descriptor to set (``EPIOCSPARAMS``) or get (``EPIOCGPARAMS``) ``struct
   epoll_params``:, which user programs can define as follows:

.. code-block:: c

  struct epoll_params {
      uint32_t busy_poll_usecs;
      uint16_t busy_poll_budget;
      uint8_t prefer_busy_poll;

      /* pad the struct to a multiple of 64bits */
      uint8_t __pad;
  };

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
if ``gro_flush_timeout`` passes without any busy poll call. For epoll-based
busy polling applications, the ``prefer_busy_poll`` field of ``struct
epoll_params`` can be set to 1 and the ``EPIOCSPARAMS`` ioctl can be issued to
enable this mode. See the above section for more details.

The NAPI budget for busy polling is lower than the default (which makes
sense given the low latency intention of normal busy polling). This is
not the case with IRQ mitigation, however, so the budget can be adjusted
with the ``SO_BUSY_POLL_BUDGET`` socket option. For epoll-based busy polling
applications, the ``busy_poll_budget`` field can be adjusted to the desired value
in ``struct epoll_params`` and set on a specific epoll context using the ``EPIOCSPARAMS``
ioctl. See the above section for more details.

It is important to note that choosing a large value for ``gro_flush_timeout``
will defer IRQs to allow for better batch processing, but will induce latency
when the system is not fully loaded. Choosing a small value for
``gro_flush_timeout`` can cause interference of the user application which is
attempting to busy poll by device IRQs and softirq processing. This value
should be chosen carefully with these tradeoffs in mind. epoll-based busy
polling applications may be able to mitigate how much user processing happens
by choosing an appropriate value for ``maxevents``.

Users may want to consider an alternate approach, IRQ suspension, to help deal
with these tradeoffs.

IRQ suspension
--------------

IRQ suspension is a mechanism wherein device IRQs are masked while epoll
triggers NAPI packet processing.

While application calls to epoll_wait successfully retrieve events, the kernel will
defer the IRQ suspension timer. If the kernel does not retrieve any events
while busy polling (for example, because network traffic levels subsided), IRQ
suspension is disabled and the IRQ mitigation strategies described above are
engaged.

This allows users to balance CPU consumption with network processing
efficiency.

To use this mechanism:

  1. The per-NAPI config parameter ``irq-suspend-timeout`` should be set to the
     maximum time (in nanoseconds) the application can have its IRQs
     suspended. This is done using netlink, as described above. This timeout
     serves as a safety mechanism to restart IRQ driver interrupt processing if
     the application has stalled. This value should be chosen so that it covers
     the amount of time the user application needs to process data from its
     call to epoll_wait, noting that applications can control how much data
     they retrieve by setting ``max_events`` when calling epoll_wait.

  2. The sysfs parameter or per-NAPI config parameters ``gro_flush_timeout``
     and ``napi_defer_hard_irqs`` can be set to low values. They will be used
     to defer IRQs after busy poll has found no data.

  3. The ``prefer_busy_poll`` flag must be set to true. This can be done using
     the ``EPIOCSPARAMS`` ioctl as described above.

  4. The application uses epoll as described above to trigger NAPI packet
     processing.

As mentioned above, as long as subsequent calls to epoll_wait return events to
userland, the ``irq-suspend-timeout`` is deferred and IRQs are disabled. This
allows the application to process data without interference.

Once a call to epoll_wait results in no events being found, IRQ suspension is
automatically disabled and the ``gro_flush_timeout`` and
``napi_defer_hard_irqs`` mitigation mechanisms take over.

It is expected that ``irq-suspend-timeout`` will be set to a value much larger
than ``gro_flush_timeout`` as ``irq-suspend-timeout`` should suspend IRQs for
the duration of one userland processing cycle.

While it is not stricly necessary to use ``napi_defer_hard_irqs`` and
``gro_flush_timeout`` to use IRQ suspension, their use is strongly
recommended.

IRQ suspension causes the system to alternate between polling mode and
irq-driven packet delivery. During busy periods, ``irq-suspend-timeout``
overrides ``gro_flush_timeout`` and keeps the system busy polling, but when
epoll finds no events, the setting of ``gro_flush_timeout`` and
``napi_defer_hard_irqs`` determine the next step.

There are essentially three possible loops for network processing and
packet delivery:

1) hardirq -> softirq -> napi poll; basic interrupt delivery
2) timer -> softirq -> napi poll; deferred irq processing
3) epoll -> busy-poll -> napi poll; busy looping

Loop 2 can take control from Loop 1, if ``gro_flush_timeout`` and
``napi_defer_hard_irqs`` are set.

If ``gro_flush_timeout`` and ``napi_defer_hard_irqs`` are set, Loops 2
and 3 "wrestle" with each other for control.

During busy periods, ``irq-suspend-timeout`` is used as timer in Loop 2,
which essentially tilts network processing in favour of Loop 3.

If ``gro_flush_timeout`` and ``napi_defer_hard_irqs`` are not set, Loop 3
cannot take control from Loop 1.

Therefore, setting ``gro_flush_timeout`` and ``napi_defer_hard_irqs`` is
the recommended usage, because otherwise setting ``irq-suspend-timeout``
might not have any discernible effect.

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
