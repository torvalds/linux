.. SPDX-License-Identifier: GPL-2.0

=======
rv-list
=======
-----------------------
List available monitors
-----------------------

:Manual section: 1

SYNOPSIS
========

**rv mon** [*-h*] **monitor_name** [*-h*] [*MONITOR OPTIONS*]

DESCRIPTION
===========

The **rv mon** command runs the monitor named *monitor_name*. Each monitor
has its own set of options. The **rv list** command shows all available
monitors.

OPTIONS
=======

**-h**, **--help**

        Print help menu.

AVAILABLE MONITORS
==================

The **rv** tool provides the interface for a set of monitors. Use the
**rv list** command to list all available monitors.

Each monitor has its own set of options. See man **rv-mon**-*monitor_name*
for details about each specific monitor. Also, running **rv mon**
**monitor_name** **-h** display the help menu with the available
options.

SEE ALSO
========

**rv**\(1), **rv-mon**\(1)

Linux kernel *RV* documentation:
<https://www.kernel.org/doc/html/latest/trace/rv/index.html>

AUTHOR
======

Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
