.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

==================================
CXL Access Coordinates Computation
==================================

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
Window Structure (CFMWS).

An example hierarchy:

>                CFMWS 0
>                  |
>         _________|_________
>        |                   |
>    ACPI0017-0          ACPI0017-1
> GP0/HB0/ACPI0016-0   GP1/HB1/ACPI0016-1
>    |          |        |           |
>   RP0        RP1      RP2         RP3
>    |          |        |           |
>  SW 0       SW 1     SW 2        SW 3
>  |   |      |   |    |   |       |   |
> EP0 EP1    EP2 EP3  EP4  EP5    EP6 EP7

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
bandwidth from the Generic Port (GP). The bandwidths for the GP is retrieved
via ACPI tables SRAT/HMAT. The min bandwidth are aggregated under the same
ACPI0017 device to form a new xarray.

Finally, the cxl_region_update_bandwidth() is called and the aggregated
bandwidth from all the members of the last xarray is updated for the
access coordinates residing in the cxl region (cxlr) context.
