================================
Linux Tracing Technologies Guide
================================

Tracing in the Linux kernel is a powerful mechanism that allows
developers and system administrators to analyze and debug system
behavior. This guide provides documentation on various tracing
frameworks and tools available in the Linux kernel.

Introduction to Tracing
-----------------------

This section provides an overview of Linux tracing mechanisms
and debugging approaches.

.. toctree::
   :maxdepth: 1

   debugging
   tracepoints
   tracepoint-analysis
   ring-buffer-map

Core Tracing Frameworks
-----------------------

The following are the primary tracing frameworks integrated into
the Linux kernel.

.. toctree::
   :maxdepth: 1

   ftrace
   ftrace-design
   ftrace-uses
   kprobes
   kprobetrace
   fprobetrace
   eprobetrace
   fprobe
   ring-buffer-design

Event Tracing and Analysis
--------------------------

A detailed explanation of event tracing mechanisms and their
applications.

.. toctree::
   :maxdepth: 1

   events
   events-kmem
   events-power
   events-nmi
   events-msr
   boottime-trace
   histogram
   histogram-design

Hardware and Performance Tracing
--------------------------------

This section covers tracing features that monitor hardware
interactions and system performance.

.. toctree::
   :maxdepth: 1

   intel_th
   stm
   sys-t
   coresight/index
   rv/index
   hisi-ptt
   mmiotrace
   hwlat_detector
   osnoise-tracer
   timerlat-tracer

User-Space Tracing
------------------

These tools allow tracing user-space applications and
interactions.

.. toctree::
   :maxdepth: 1

   user_events
   uprobetracer

Additional Resources
--------------------

For more details, refer to the respective documentation of each
tracing tool and framework.

.. only:: subproject and html

   Indices
   =======

   * :ref:`genindex`
