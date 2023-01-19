.. SPDX-License-Identifier: GPL-2.0

==========
rv-mon-wip
==========
----------------------------
Wakeup In Preemptive monitor
----------------------------

:Manual section: 1

SYNOPSIS
========

**rv mon wip** [*OPTIONS*]

DESCRIPTION
===========

The wakeup in preemptive (**wip**) monitor is a sample per-cpu monitor that
checks if the wakeup events always take place with preemption disabled.

See kernel documentation for further information about this monitor:
<https://docs.kernel.org/trace/rv/monitor_wip.html>

OPTIONS
=======

.. include:: common_ikm.rst

SEE ALSO
========

**rv**\(1), **rv-mon**\(1)

Linux kernel *RV* documentation:
<https://www.kernel.org/doc/html/latest/trace/rv/index.html>

AUTHOR
======

Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
