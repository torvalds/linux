===============================
The Linux Kernel Tracepoint API
===============================

:Author: Jason Baron
:Author: William Cohen

Introduction
============

Tracepoints are static probe points that are located in strategic points
throughout the kernel. 'Probes' register/unregister with tracepoints via
a callback mechanism. The 'probes' are strictly typed functions that are
passed a unique set of parameters defined by each tracepoint.

From this simple callback mechanism, 'probes' can be used to profile,
debug, and understand kernel behavior. There are a number of tools that
provide a framework for using 'probes'. These tools include Systemtap,
ftrace, and LTTng.

Tracepoints are defined in a number of header files via various macros.
Thus, the purpose of this document is to provide a clear accounting of
the available tracepoints. The intention is to understand not only what
tracepoints are available but also to understand where future
tracepoints might be added.

The API presented has functions of the form:
``trace_tracepointname(function parameters)``. These are the tracepoints
callbacks that are found throughout the code. Registering and
unregistering probes with these callback sites is covered in the
``Documentation/trace/*`` directory.

IRQ
===

.. kernel-doc:: include/trace/events/irq.h
   :internal:

SIGNAL
======

.. kernel-doc:: include/trace/events/signal.h
   :internal:

Block IO
========

.. kernel-doc:: include/trace/events/block.h
   :internal:

Workqueue
=========

.. kernel-doc:: include/trace/events/workqueue.h
   :internal:
