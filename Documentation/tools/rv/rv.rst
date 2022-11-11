.. SPDX-License-Identifier: GPL-2.0

==
rv
==
--------------------
Runtime Verification
--------------------

:Manual section: 1

SYNOPSIS
========

**rv** *COMMAND* [*OPTIONS*]

DESCRIPTION
===========

Runtime Verification (**RV**) is a lightweight (yet rigorous) method
for formal verification with a practical approach for complex systems.
Instead of relying on a fine-grained model of a system (e.g., a
re-implementation a instruction level), RV works by analyzing the trace
of the system's actual execution, comparing it against a formal
specification of the system behavior.

The **rv** tool provides the interface for a collection of runtime
verification (rv) monitors.

COMMANDS
========

**list**

        List all available monitors.

**mon**

        Run monitor.

OPTIONS
=======

**-h**, **--help**

        Display the help text.

For other options, see the man page for the corresponding command.

SEE ALSO
========

**rv-list**\(1), **rv-mon**\(1)

Linux kernel *RV* documentation:
<https://www.kernel.org/doc/html/latest/trace/rv/index.html>

AUTHOR
======

Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
