=========================================================
NVIDIA Tegra SoC Uncore Performance Monitoring Unit (PMU)
=========================================================

The NVIDIA Tegra SoC includes various system PMUs to measure key performance
metrics like memory bandwidth, latency, and utilization:

* Scalable Coherency Fabric (SCF)
* NVLink-C2C0
* NVLink-C2C1
* CNVLink
* PCIE

PMU Driver
----------

The PMUs in this document are based on ARM CoreSight PMU Architecture as
described in document: ARM IHI 0091. Since this is a standard architecture, the
PMUs are managed by a common driver "arm-cs-arch-pmu". This driver describes
the available events and configuration of each PMU in sysfs. Please see the
sections below to get the sysfs path of each PMU. Like other uncore PMU drivers,
the driver provides "cpumask" sysfs attribute to show the CPU id used to handle
the PMU event. There is also "associated_cpus" sysfs attribute, which contains a
list of CPUs associated with the PMU instance.

.. _SCF_PMU_Section:

SCF PMU
-------

The SCF PMU monitors system level cache events, CPU traffic, and
strongly-ordered (SO) PCIE write traffic to local/remote memory. Please see
:ref:`NVIDIA_Uncore_PMU_Traffic_Coverage_Section` for more info about the PMU
traffic coverage.

The events and configuration options of this PMU device are described in sysfs,
see /sys/bus/event_sources/devices/nvidia_scf_pmu_<socket-id>.

Example usage:

* Count event id 0x0 in socket 0::

   perf stat -a -e nvidia_scf_pmu_0/event=0x0/

* Count event id 0x0 in socket 1::

   perf stat -a -e nvidia_scf_pmu_1/event=0x0/

NVLink-C2C0 PMU
--------------------

The NVLink-C2C0 PMU monitors incoming traffic from a GPU/CPU connected with
NVLink-C2C (Chip-2-Chip) interconnect. The type of traffic captured by this PMU
varies dependent on the chip configuration:

* NVIDIA Grace Hopper Superchip: Hopper GPU is connected with Grace SoC.

  In this config, the PMU captures GPU ATS translated or EGM traffic from the GPU.

* NVIDIA Grace CPU Superchip: two Grace CPU SoCs are connected.

  In this config, the PMU captures read and relaxed ordered (RO) writes from
  PCIE device of the remote SoC.

Please see :ref:`NVIDIA_Uncore_PMU_Traffic_Coverage_Section` for more info about
the PMU traffic coverage.

The events and configuration options of this PMU device are described in sysfs,
see /sys/bus/event_sources/devices/nvidia_nvlink_c2c0_pmu_<socket-id>.

Example usage:

* Count event id 0x0 from the GPU/CPU connected with socket 0::

   perf stat -a -e nvidia_nvlink_c2c0_pmu_0/event=0x0/

* Count event id 0x0 from the GPU/CPU connected with socket 1::

   perf stat -a -e nvidia_nvlink_c2c0_pmu_1/event=0x0/

* Count event id 0x0 from the GPU/CPU connected with socket 2::

   perf stat -a -e nvidia_nvlink_c2c0_pmu_2/event=0x0/

* Count event id 0x0 from the GPU/CPU connected with socket 3::

   perf stat -a -e nvidia_nvlink_c2c0_pmu_3/event=0x0/

NVLink-C2C1 PMU
-------------------

The NVLink-C2C1 PMU monitors incoming traffic from a GPU connected with
NVLink-C2C (Chip-2-Chip) interconnect. This PMU captures untranslated GPU
traffic, in contrast with NvLink-C2C0 PMU that captures ATS translated traffic.
Please see :ref:`NVIDIA_Uncore_PMU_Traffic_Coverage_Section` for more info about
the PMU traffic coverage.

The events and configuration options of this PMU device are described in sysfs,
see /sys/bus/event_sources/devices/nvidia_nvlink_c2c1_pmu_<socket-id>.

Example usage:

* Count event id 0x0 from the GPU connected with socket 0::

   perf stat -a -e nvidia_nvlink_c2c1_pmu_0/event=0x0/

* Count event id 0x0 from the GPU connected with socket 1::

   perf stat -a -e nvidia_nvlink_c2c1_pmu_1/event=0x0/

* Count event id 0x0 from the GPU connected with socket 2::

   perf stat -a -e nvidia_nvlink_c2c1_pmu_2/event=0x0/

* Count event id 0x0 from the GPU connected with socket 3::

   perf stat -a -e nvidia_nvlink_c2c1_pmu_3/event=0x0/

CNVLink PMU
---------------

The CNVLink PMU monitors traffic from GPU and PCIE device on remote sockets
to local memory. For PCIE traffic, this PMU captures read and relaxed ordered
(RO) write traffic. Please see :ref:`NVIDIA_Uncore_PMU_Traffic_Coverage_Section`
for more info about the PMU traffic coverage.

The events and configuration options of this PMU device are described in sysfs,
see /sys/bus/event_sources/devices/nvidia_cnvlink_pmu_<socket-id>.

Each SoC socket can be connected to one or more sockets via CNVLink. The user can
use "rem_socket" bitmap parameter to select the remote socket(s) to monitor.
Each bit represents the socket number, e.g. "rem_socket=0xE" corresponds to
socket 1 to 3.
/sys/bus/event_sources/devices/nvidia_cnvlink_pmu_<socket-id>/format/rem_socket
shows the valid bits that can be set in the "rem_socket" parameter.

The PMU can not distinguish the remote traffic initiator, therefore it does not
provide filter to select the traffic source to monitor. It reports combined
traffic from remote GPU and PCIE devices.

Example usage:

* Count event id 0x0 for the traffic from remote socket 1, 2, and 3 to socket 0::

   perf stat -a -e nvidia_cnvlink_pmu_0/event=0x0,rem_socket=0xE/

* Count event id 0x0 for the traffic from remote socket 0, 2, and 3 to socket 1::

   perf stat -a -e nvidia_cnvlink_pmu_1/event=0x0,rem_socket=0xD/

* Count event id 0x0 for the traffic from remote socket 0, 1, and 3 to socket 2::

   perf stat -a -e nvidia_cnvlink_pmu_2/event=0x0,rem_socket=0xB/

* Count event id 0x0 for the traffic from remote socket 0, 1, and 2 to socket 3::

   perf stat -a -e nvidia_cnvlink_pmu_3/event=0x0,rem_socket=0x7/


PCIE PMU
------------

The PCIE PMU monitors all read/write traffic from PCIE root ports to
local/remote memory. Please see :ref:`NVIDIA_Uncore_PMU_Traffic_Coverage_Section`
for more info about the PMU traffic coverage.

The events and configuration options of this PMU device are described in sysfs,
see /sys/bus/event_sources/devices/nvidia_pcie_pmu_<socket-id>.

Each SoC socket can support multiple root ports. The user can use
"root_port" bitmap parameter to select the port(s) to monitor, i.e.
"root_port=0xF" corresponds to root port 0 to 3.
/sys/bus/event_sources/devices/nvidia_pcie_pmu_<socket-id>/format/root_port
shows the valid bits that can be set in the "root_port" parameter.

Example usage:

* Count event id 0x0 from root port 0 and 1 of socket 0::

   perf stat -a -e nvidia_pcie_pmu_0/event=0x0,root_port=0x3/

* Count event id 0x0 from root port 0 and 1 of socket 1::

   perf stat -a -e nvidia_pcie_pmu_1/event=0x0,root_port=0x3/

.. _NVIDIA_Uncore_PMU_Traffic_Coverage_Section:

Traffic Coverage
----------------

The PMU traffic coverage may vary dependent on the chip configuration:

* **NVIDIA Grace Hopper Superchip**: Hopper GPU is connected with Grace SoC.

  Example configuration with two Grace SoCs::

   *********************************          *********************************
   * SOCKET-A                      *          * SOCKET-B                      *
   *                               *          *                               *
   *                     ::::::::  *          *  ::::::::                     *
   *                     : PCIE :  *          *  : PCIE :                     *
   *                     ::::::::  *          *  ::::::::                     *
   *                         |     *          *      |                        *
   *                         |     *          *      |                        *
   *  :::::::            ::::::::: *          *  :::::::::            ::::::: *
   *  :     :            :       : *          *  :       :            :     : *
   *  : GPU :<--NVLink-->: Grace :<---CNVLink--->: Grace :<--NVLink-->: GPU : *
   *  :     :    C2C     :  SoC  : *          *  :  SoC  :    C2C     :     : *
   *  :::::::            ::::::::: *          *  :::::::::            ::::::: *
   *     |                   |     *          *      |                   |    *
   *     |                   |     *          *      |                   |    *
   *  &&&&&&&&           &&&&&&&&  *          *   &&&&&&&&           &&&&&&&& *
   *  & GMEM &           & CMEM &  *          *   & CMEM &           & GMEM & *
   *  &&&&&&&&           &&&&&&&&  *          *   &&&&&&&&           &&&&&&&& *
   *                               *          *                               *
   *********************************          *********************************

   GMEM = GPU Memory (e.g. HBM)
   CMEM = CPU Memory (e.g. LPDDR5X)

  |
  | Following table contains traffic coverage of Grace SoC PMU in socket-A:

  ::

   +--------------+-------+-----------+-----------+-----+----------+----------+
   |              |                        Source                             |
   +              +-------+-----------+-----------+-----+----------+----------+
   | Destination  |       |GPU ATS    |GPU Not-ATS|     | Socket-B | Socket-B |
   |              |PCI R/W|Translated,|Translated | CPU | CPU/PCIE1| GPU/PCIE2|
   |              |       |EGM        |           |     |          |          |
   +==============+=======+===========+===========+=====+==========+==========+
   | Local        | PCIE  |NVLink-C2C0|NVLink-C2C1| SCF | SCF PMU  | CNVLink  |
   | SYSRAM/CMEM  | PMU   |PMU        |PMU        | PMU |          | PMU      |
   +--------------+-------+-----------+-----------+-----+----------+----------+
   | Local GMEM   | PCIE  |    N/A    |NVLink-C2C1| SCF | SCF PMU  | CNVLink  |
   |              | PMU   |           |PMU        | PMU |          | PMU      |
   +--------------+-------+-----------+-----------+-----+----------+----------+
   | Remote       | PCIE  |NVLink-C2C0|NVLink-C2C1| SCF |          |          |
   | SYSRAM/CMEM  | PMU   |PMU        |PMU        | PMU |   N/A    |   N/A    |
   | over CNVLink |       |           |           |     |          |          |
   +--------------+-------+-----------+-----------+-----+----------+----------+
   | Remote GMEM  | PCIE  |NVLink-C2C0|NVLink-C2C1| SCF |          |          |
   | over CNVLink | PMU   |PMU        |PMU        | PMU |   N/A    |   N/A    |
   +--------------+-------+-----------+-----------+-----+----------+----------+

   PCIE1 traffic represents strongly ordered (SO) writes.
   PCIE2 traffic represents reads and relaxed ordered (RO) writes.

* **NVIDIA Grace CPU Superchip**: two Grace CPU SoCs are connected.

  Example configuration with two Grace SoCs::

   *******************             *******************
   * SOCKET-A        *             * SOCKET-B        *
   *                 *             *                 *
   *    ::::::::     *             *    ::::::::     *
   *    : PCIE :     *             *    : PCIE :     *
   *    ::::::::     *             *    ::::::::     *
   *        |        *             *        |        *
   *        |        *             *        |        *
   *    :::::::::    *             *    :::::::::    *
   *    :       :    *             *    :       :    *
   *    : Grace :<--------NVLink------->: Grace :    *
   *    :  SoC  :    *     C2C     *    :  SoC  :    *
   *    :::::::::    *             *    :::::::::    *
   *        |        *             *        |        *
   *        |        *             *        |        *
   *     &&&&&&&&    *             *     &&&&&&&&    *
   *     & CMEM &    *             *     & CMEM &    *
   *     &&&&&&&&    *             *     &&&&&&&&    *
   *                 *             *                 *
   *******************             *******************

   GMEM = GPU Memory (e.g. HBM)
   CMEM = CPU Memory (e.g. LPDDR5X)

  |
  | Following table contains traffic coverage of Grace SoC PMU in socket-A:

  ::

   +-----------------+-----------+---------+----------+-------------+
   |                 |                      Source                  |
   +                 +-----------+---------+----------+-------------+
   | Destination     |           |         | Socket-B | Socket-B    |
   |                 |  PCI R/W  |   CPU   | CPU/PCIE1| PCIE2       |
   |                 |           |         |          |             |
   +=================+===========+=========+==========+=============+
   | Local           |  PCIE PMU | SCF PMU | SCF PMU  | NVLink-C2C0 |
   | SYSRAM/CMEM     |           |         |          | PMU         |
   +-----------------+-----------+---------+----------+-------------+
   | Remote          |           |         |          |             |
   | SYSRAM/CMEM     |  PCIE PMU | SCF PMU |   N/A    |     N/A     |
   | over NVLink-C2C |           |         |          |             |
   +-----------------+-----------+---------+----------+-------------+

   PCIE1 traffic represents strongly ordered (SO) writes.
   PCIE2 traffic represents reads and relaxed ordered (RO) writes.
