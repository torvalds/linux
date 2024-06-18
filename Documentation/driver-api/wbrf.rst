.. SPDX-License-Identifier: GPL-2.0-or-later

=================================
WBRF - Wifi Band RFI Mitigations
=================================

Due to electrical and mechanical constraints in certain platform designs
there may be likely interference of relatively high-powered harmonics of
the GPU memory clocks with local radio module frequency bands used by
certain Wifi bands.

To mitigate possible RFI interference producers can advertise the
frequencies in use and consumers can use this information to avoid using
these frequencies for sensitive features.

When a platform is known to have this issue with any contained devices,
the platform designer will advertise the availability of this feature via
ACPI devices with a device specific method (_DSM).
* Producers with this _DSM will be able to advertise the frequencies in use.
* Consumers with this _DSM will be able to register for notifications of
frequencies in use.

Some general terms
==================

Producer: such component who can produce high-powered radio frequency
Consumer: such component who can adjust its in-use frequency in
response to the radio frequencies of other components to mitigate the
possible RFI.

To make the mechanism function, those producers should notify active use
of their particular frequencies so that other consumers can make relative
internal adjustments as necessary to avoid this resonance.

ACPI interface
==============

Although initially used by for wifi + dGPU use cases, the ACPI interface
can be scaled to any type of device that a platform designer discovers
can cause interference.

The GUID used for the _DSM is 7B7656CF-DC3D-4C1C-83E9-66E721DE3070.

3 functions are available in this _DSM:

* 0: discover # of functions available
* 1: record RF bands in use
* 2: retrieve RF bands in use

Driver programming interface
============================

.. kernel-doc:: drivers/platform/x86/amd/wbrf.c

Sample Usage
=============

The expected flow for the producers:
1. During probe, call `acpi_amd_wbrf_supported_producer` to check if WBRF
can be enabled for the device.
2. On using some frequency band, call `acpi_amd_wbrf_add_remove` with 'add'
param to get other consumers properly notified.
3. Or on stopping using some frequency band, call
`acpi_amd_wbrf_add_remove` with 'remove' param to get other consumers notified.

The expected flow for the consumers:
1. During probe, call `acpi_amd_wbrf_supported_consumer` to check if WBRF
can be enabled for the device.
2. Call `amd_wbrf_register_notifier` to register for notification
of frequency band change(add or remove) from other producers.
3. Call the `amd_wbrf_retrieve_freq_band` initially to retrieve
current active frequency bands considering some producers may broadcast
such information before the consumer is up.
4. On receiving a notification for frequency band change, run
`amd_wbrf_retrieve_freq_band` again to retrieve the latest
active frequency bands.
5. During driver cleanup, call `amd_wbrf_unregister_notifier` to
unregister the notifier.
