=====================================================
Freescale i.MX8 DDR Performance Monitoring Unit (PMU)
=====================================================

There are no performance counters inside the DRAM controller, so performance
signals are brought out to the edge of the controller where a set of 4 x 32 bit
counters is implemented. This is controlled by the CSV modes programmed in counter
control register which causes a large number of PERF signals to be generated.

Selection of the value for each counter is done via the config registers. There
is one register for each counter. Counter 0 is special in that it always counts
“time” and when expired causes a lock on itself and the other counters and an
interrupt is raised. If any other counter overflows, it continues counting, and
no interrupt is raised.

The "format" directory describes format of the config (event ID) and config1
(AXI filtering) fields of the perf_event_attr structure, see /sys/bus/event_source/
devices/imx8_ddr0/format/. The "events" directory describes the events types
hardware supported that can be used with perf tool, see /sys/bus/event_source/
devices/imx8_ddr0/events/. The "caps" directory describes filter features implemented
in DDR PMU, see /sys/bus/events_source/devices/imx8_ddr0/caps/.

    .. code-block:: bash

        perf stat -a -e imx8_ddr0/cycles/ cmd
        perf stat -a -e imx8_ddr0/read/,imx8_ddr0/write/ cmd

AXI filtering is only used by CSV modes 0x41 (axid-read) and 0x42 (axid-write)
to count reading or writing matches filter setting. Filter setting is various
from different DRAM controller implementations, which is distinguished by quirks
in the driver. You also can dump info from userspace, filter in "caps" directory
indicates whether PMU supports AXI ID filter or not; enhanced_filter indicates
whether PMU supports enhanced AXI ID filter or not. Value 0 for un-supported, and
value 1 for supported.

* With DDR_CAP_AXI_ID_FILTER quirk(filter: 1, enhanced_filter: 0).
  Filter is defined with two configuration parts:
  --AXI_ID defines AxID matching value.
  --AXI_MASKING defines which bits of AxID are meaningful for the matching.

      - 0: corresponding bit is masked.
      - 1: corresponding bit is not masked, i.e. used to do the matching.

  AXI_ID and AXI_MASKING are mapped on DPCR1 register in performance counter.
  When non-masked bits are matching corresponding AXI_ID bits then counter is
  incremented. Perf counter is incremented if::

        AxID && AXI_MASKING == AXI_ID && AXI_MASKING

  This filter doesn't support filter different AXI ID for axid-read and axid-write
  event at the same time as this filter is shared between counters.

  .. code-block:: bash

      perf stat -a -e imx8_ddr0/axid-read,axi_mask=0xMMMM,axi_id=0xDDDD/ cmd
      perf stat -a -e imx8_ddr0/axid-write,axi_mask=0xMMMM,axi_id=0xDDDD/ cmd

  .. note::

      axi_mask is inverted in userspace(i.e. set bits are bits to mask), and
      it will be reverted in driver automatically. so that the user can just specify
      axi_id to monitor a specific id, rather than having to specify axi_mask.

  .. code-block:: bash

        perf stat -a -e imx8_ddr0/axid-read,axi_id=0x12/ cmd, which will monitor ARID=0x12

* With DDR_CAP_AXI_ID_FILTER_ENHANCED quirk(filter: 1, enhanced_filter: 1).
  This is an extension to the DDR_CAP_AXI_ID_FILTER quirk which permits
  counting the number of bytes (as opposed to the number of bursts) from DDR
  read and write transactions concurrently with another set of data counters.
