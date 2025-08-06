.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

==================================
CXL Access Coordinates Computation
==================================

Latency and Bandwidth Calculation
=================================
A memory region performance coordinates (latency and bandwidth) are typically
provided via ACPI tables :doc:`SRAT <../platform/acpi/srat>` and
:doc:`HMAT <../platform/acpi/hmat>`. However, the platform firmware (BIOS) is
not able to annotate those for CXL devices that are hot-plugged since they do
not exist during platform firmware initialization. The CXL driver can compute
the performance coordinates by retrieving data from several components.

The :doc:`SRAT <../platform/acpi/srat>` provides a Generic Port Affinity
subtable that ties a proximity domain to a device handle, which in this case
would be the CXL hostbridge. Using this association, the performance
coordinates for the Generic Port can be retrieved from the
:doc:`HMAT <../platform/acpi/hmat>` subtable. This piece represents the
performance coordinates between a CPU and a Generic Port (CXL hostbridge).

The :doc:`CDAT <../platform/cdat>` provides the performance coordinates for
the CXL device itself. That is the bandwidth and latency to access that device's
memory region. The DSMAS subtable provides a DSMADHandle that is tied to a
Device Physical Address (DPA) range. The DSLBIS subtable provides the
performance coordinates that's tied to a DSMADhandle and this ties the two
table entries together to provide the performance coordinates for each DPA
region. For example, if a device exports a DRAM region and a PMEM region,
then there would be different performance characteristsics for each of those
regions.

If there's a CXL switch in the topology, then the performance coordinates for the
switch is provided by SSLBIS subtable. This provides the bandwidth and latency
for traversing the switch between the switch upstream port and the switch
downstream port that points to the endpoint device.

Simple topology example::

 GP0/HB0/ACPI0016-0
        RP0
         |
         | L0
         |
     SW 0 / USP0
     SW 0 / DSP0
         |
         | L1
         |
        EP0

In this example, there is a CXL switch between an endpoint and a root port.
Latency in this example is calculated as such:
L(EP0) - Latency from EP0 CDAT DSMAS+DSLBIS
L(L1) - Link latency between EP0 and SW0DSP0
L(SW0) - Latency for the switch from SW0 CDAT SSLBIS.
L(L0) - Link latency between SW0 and RP0
L(RP0) - Latency from root port to CPU via SRAT and HMAT (Generic Port).
Total read and write latencies are the sum of all these parts.

Bandwidth in this example is calculated as such:
B(EP0) - Bandwidth from EP0 CDAT DSMAS+DSLBIS
B(L1) - Link bandwidth between EP0 and SW0DSP0
B(SW0) - Bandwidth for the switch from SW0 CDAT SSLBIS.
B(L0) - Link bandwidth between SW0 and RP0
B(RP0) - Bandwidth from root port to CPU via SRAT and HMAT (Generic Port).
The total read and write bandwidth is the min() of all these parts.

To calculate the link bandwidth:
LinkOperatingFrequency (GT/s) is the current negotiated link speed.
DataRatePerLink (MB/s) = LinkOperatingFrequency / 8
Bandwidth (MB/s) = PCIeCurrentLinkWidth * DataRatePerLink
Where PCIeCurrentLinkWidth is the number of lanes in the link.

To calculate the link latency:
LinkLatency (picoseconds) = FlitSize / LinkBandwidth (MB/s)

See `CXL Memory Device SW Guide r1.0 <https://www.intel.com/content/www/us/en/content-details/643805/cxl-memory-device-software-guide.html>`_,
section 2.11.3 and 2.11.4 for details.

In the end, the access coordinates for a constructed memory region is calculated from one
or more memory partitions from each of the CXL device(s).

Shared Upstream Link Calculation
================================
For certain CXL region construction with endpoints behind CXL switches (SW) or
Root Ports (RP), there is the possibility of the total bandwidth for all
the endpoints behind a switch being more than the switch upstream link.
A similar situation can occur within the host, upstream of the root ports.
The CXL driver performs an additional pass after all the targets have
arrived for a region in order to recalculate the bandwidths with possible
upstream link being a limiting factor in mind.

The algorithm assumes the configuration is a symmetric topology as that
maximizes performance. When asymmetric topology is detected, the calculation
is aborted. An asymmetric topology is detected during topology walk where the
number of RPs detected as a grandparent is not equal to the number of devices
iterated in the same iteration loop. The assumption is made that subtle
asymmetry in properties does not happen and all paths to EPs are equal.

There can be multiple switches under an RP. There can be multiple RPs under
a CXL Host Bridge (HB). There can be multiple HBs under a CXL Fixed Memory
Window Structure (CFMWS) in the :doc:`CEDT <../platform/acpi/cedt>`.

An example hierarchy::

                CFMWS 0
                  |
         _________|_________
        |                   |
    ACPI0017-0          ACPI0017-1
 GP0/HB0/ACPI0016-0   GP1/HB1/ACPI0016-1
    |          |        |           |
   RP0        RP1      RP2         RP3
    |          |        |           |
  SW 0       SW 1     SW 2        SW 3
  |   |      |   |    |   |       |   |
 EP0 EP1    EP2 EP3  EP4  EP5    EP6 EP7

Computation for the example hierarchy:

Min (GP0 to CPU BW,
     Min(SW 0 Upstream Link to RP0 BW,
         Min(SW0SSLBIS for SW0DSP0 (EP0), EP0 DSLBIS, EP0 Upstream Link) +
         Min(SW0SSLBIS for SW0DSP1 (EP1), EP1 DSLBIS, EP1 Upstream link)) +
     Min(SW 1 Upstream Link to RP1 BW,
         Min(SW1SSLBIS for SW1DSP0 (EP2), EP2 DSLBIS, EP2 Upstream Link) +
         Min(SW1SSLBIS for SW1DSP1 (EP3), EP3 DSLBIS, EP3 Upstream link))) +
Min (GP1 to CPU BW,
     Min(SW 2 Upstream Link to RP2 BW,
         Min(SW2SSLBIS for SW2DSP0 (EP4), EP4 DSLBIS, EP4 Upstream Link) +
         Min(SW2SSLBIS for SW2DSP1 (EP5), EP5 DSLBIS, EP5 Upstream link)) +
     Min(SW 3 Upstream Link to RP3 BW,
         Min(SW3SSLBIS for SW3DSP0 (EP6), EP6 DSLBIS, EP6 Upstream Link) +
         Min(SW3SSLBIS for SW3DSP1 (EP7), EP7 DSLBIS, EP7 Upstream link))))

The calculation starts at cxl_region_shared_upstream_perf_update(). A xarray
is created to collect all the endpoint bandwidths via the
cxl_endpoint_gather_bandwidth() function. The min() of bandwidth from the
endpoint CDAT and the upstream link bandwidth is calculated. If the endpoint
has a CXL switch as a parent, then min() of calculated bandwidth and the
bandwidth from the SSLBIS for the switch downstream port that is associated
with the endpoint is calculated. The final bandwidth is stored in a
'struct cxl_perf_ctx' in the xarray indexed by a device pointer. If the
endpoint is direct attached to a root port (RP), the device pointer would be an
RP device. If the endpoint is behind a switch, the device pointer would be the
upstream device of the parent switch.

At the next stage, the code walks through one or more switches if they exist
in the topology. For endpoints directly attached to RPs, this step is skipped.
If there is another switch upstream, the code takes the min() of the current
gathered bandwidth and the upstream link bandwidth. If there's a switch
upstream, then the SSLBIS of the upstream switch.

Once the topology walk reaches the RP, whether it's direct attached endpoints
or walking through the switch(es), cxl_rp_gather_bandwidth() is called. At
this point all the bandwidths are aggregated per each host bridge, which is
also the index for the resulting xarray.

The next step is to take the min() of the per host bridge bandwidth and the
bandwidth from the Generic Port (GP). The bandwidths for the GP are retrieved
via ACPI tables (:doc:`SRAT <../platform/acpi/srat>` and
:doc:`HMAT <../platform/acpi/hmat>`). The minimum bandwidth are aggregated
under the same ACPI0017 device to form a new xarray.

Finally, the cxl_region_update_bandwidth() is called and the aggregated
bandwidth from all the members of the last xarray is updated for the
access coordinates residing in the cxl region (cxlr) context.

QTG ID
======
Each :doc:`CEDT <../platform/acpi/cedt>` has a QTG ID field. This field provides
the ID that associates with a QoS Throttling Group (QTG) for the CFMWS window.
Once the access coordinates are calculated, an ACPI Device Specific Method can
be issued to the ACPI0016 device to retrieve the QTG ID depends on the access
coordinates provided. The QTG ID for the device can be used as guidance to match
to the CFMWS to setup the best Linux root decoder for the device performance.
