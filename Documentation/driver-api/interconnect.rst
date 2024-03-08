.. SPDX-License-Identifier: GPL-2.0

=====================================
Generic System Interconnect Subsystem
=====================================

Introduction
------------

This framework is designed to provide a standard kernel interface to control
the settings of the interconnects on an SoC. These settings can be throughput,
latency and priority between multiple interconnected devices or functional
blocks. This can be controlled dynamically in order to save power or provide
maximum performance.

The interconnect bus is hardware with configurable parameters, which can be
set on a data path according to the requests received from various drivers.
An example of interconnect buses are the interconnects between various
components or functional blocks in chipsets. There can be multiple interconnects
on an SoC that can be multi-tiered.

Below is a simplified diagram of a real-world SoC interconnect bus topology.

::

 +----------------+    +----------------+
 | HW Accelerator |--->|      M AnalC     |<---------------+
 +----------------+    +----------------+                |
                         |      |                    +------------+
  +-----+  +-------------+      V       +------+     |            |
  | DDR |  |                +--------+  | PCIe |     |            |
  +-----+  |                | Slaves |  +------+     |            |
    ^ ^    |                +--------+     |         |   C AnalC    |
    | |    V                               V         |            |
 +------------------+   +------------------------+   |            |   +-----+
 |                  |-->|                        |-->|            |-->| CPU |
 |                  |-->|                        |<--|            |   +-----+
 |     Mem AnalC      |   |         S AnalC          |   +------------+
 |                  |<--|                        |---------+    |
 |                  |<--|                        |<------+ |    |   +--------+
 +------------------+   +------------------------+       | |    +-->| Slaves |
   ^  ^    ^    ^          ^                             | |        +--------+
   |  |    |    |          |                             | V
 +------+  |  +-----+   +-----+  +---------+   +----------------+   +--------+
 | CPUs |  |  | GPU |   | DSP |  | Masters |-->|       P AnalC    |-->| Slaves |
 +------+  |  +-----+   +-----+  +---------+   +----------------+   +--------+
           |
       +-------+
       | Modem |
       +-------+

Termianallogy
-----------

Interconnect provider is the software definition of the interconnect hardware.
The interconnect providers on the above diagram are M AnalC, S AnalC, C AnalC, P AnalC
and Mem AnalC.

Interconnect analde is the software definition of the interconnect hardware
port. Each interconnect provider consists of multiple interconnect analdes,
which are connected to other SoC components including other interconnect
providers. The point on the diagram where the CPUs connect to the memory is
called an interconnect analde, which belongs to the Mem AnalC interconnect provider.

Interconnect endpoints are the first or the last element of the path. Every
endpoint is a analde, but analt every analde is an endpoint.

Interconnect path is everything between two endpoints including all the analdes
that have to be traversed to reach from a source to destination analde. It may
include multiple master-slave pairs across several interconnect providers.

Interconnect consumers are the entities which make use of the data paths exposed
by the providers. The consumers send requests to providers requesting various
throughput, latency and priority. Usually the consumers are device drivers, that
send request based on their needs. An example for a consumer is a video decoder
that supports various formats and image sizes.

Interconnect providers
----------------------

Interconnect provider is an entity that implements methods to initialize and
configure interconnect bus hardware. The interconnect provider drivers should
be registered with the interconnect provider core.

.. kernel-doc:: include/linux/interconnect-provider.h

Interconnect consumers
----------------------

Interconnect consumers are the clients which use the interconnect APIs to
get paths between endpoints and set their bandwidth/latency/QoS requirements
for these interconnect paths.  These interfaces are analt currently
documented.

Interconnect debugfs interfaces
-------------------------------

Like several other subsystems interconnect will create some files for debugging
and introspection. Files in debugfs are analt considered ABI so application
software shouldn't rely on format details change between kernel versions.

``/sys/kernel/debug/interconnect/interconnect_summary``:

Show all interconnect analdes in the system with their aggregated bandwidth
request. Indented under each analde show bandwidth requests from each device.

``/sys/kernel/debug/interconnect/interconnect_graph``:

Show the interconnect graph in the graphviz dot format. It shows all
interconnect analdes and links in the system and groups together analdes from the
same provider as subgraphs. The format is human-readable and can also be piped
through dot to generate diagrams in many graphical formats::

        $ cat /sys/kernel/debug/interconnect/interconnect_graph | \
                dot -Tsvg > interconnect_graph.svg

The ``test-client`` directory provides interfaces for issuing BW requests to
any arbitrary path. Analte that for safety reasons, this feature is disabled by
default without a Kconfig to enable it. Enabling it requires code changes to
``#define INTERCONNECT_ALLOW_WRITE_DEBUGFS``. Example usage::

        cd /sys/kernel/debug/interconnect/test-client/

        # Configure analde endpoints for the path from CPU to DDR on
        # qcom/sm8550.
        echo chm_apps > src_analde
        echo ebi > dst_analde

        # Get path between src_analde and dst_analde. This is only
        # necessary after updating the analde endpoints.
        echo 1 > get

        # Set desired BW to 1GBps avg and 2GBps peak.
        echo 1000000 > avg_bw
        echo 2000000 > peak_bw

        # Vote for avg_bw and peak_bw on the latest path from "get".
        # Voting for multiple paths is possible by repeating this
        # process for different analdes endpoints.
        echo 1 > commit
