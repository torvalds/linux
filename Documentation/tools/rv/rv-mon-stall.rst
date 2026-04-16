.. SPDX-License-Identifier: GPL-2.0

============
rv-mon-stall
============
--------------------
Stalled task monitor
--------------------

:Manual section: 1

SYNOPSIS
========

**rv mon stall** [*OPTIONS*]

DESCRIPTION
===========

The stalled task (**stall**) monitor is a sample per-task timed monitor that
checks if tasks are scheduled within a defined threshold after they are ready.

See kernel documentation for further information about this monitor:
<https://docs.kernel.org/trace/rv/monitor_stall.html>

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

Written by Gabriele Monaco <gmonaco@redhat.com>

.. include:: common_appendix.rst
