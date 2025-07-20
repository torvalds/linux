.. SPDX-License-Identifier: GPL-2.0

============
rv-mon-sched
============
-----------------------------
Scheduler monitors collection
-----------------------------

:Manual section: 1

SYNOPSIS
========

**rv mon sched** [*OPTIONS*]

**rv mon <NESTED_MONITOR>** [*OPTIONS*]

**rv mon sched:<NESTED_MONITOR>** [*OPTIONS*]

DESCRIPTION
===========

The scheduler monitor collection is a container for several monitors to model
the behaviour of the scheduler. Each monitor describes a specification that
the scheduler should follow.

As a monitor container, it will enable all nested monitors and set them
according to OPTIONS.
Nevertheless nested monitors can also be activated independently both by name
and by specifying sched: , e.g. to enable only monitor tss you can do any of:

    # rv mon sched:tss

    # rv mon tss

See kernel documentation for further information about this monitor:
<https://docs.kernel.org/trace/rv/monitor_sched.html>

OPTIONS
=======

.. include:: common_ikm.rst

NESTED MONITOR
==============

The available nested monitors are:
  * scpd: schedule called with preemption disabled
  * snep: schedule does not enable preempt
  * sncid: schedule not called with interrupt disabled
  * snroc: set non runnable on its own context
  * sco: scheduling context operations
  * tss: task switch while scheduling

SEE ALSO
========

**rv**\(1), **rv-mon**\(1)

Linux kernel *RV* documentation:
<https://www.kernel.org/doc/html/latest/trace/rv/index.html>

AUTHOR
======

Written by Gabriele Monaco <gmonaco@redhat.com>

.. include:: common_appendix.rst
