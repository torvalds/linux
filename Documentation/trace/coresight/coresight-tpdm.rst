.. SPDX-License-Identifier: GPL-2.0

==========================================================
Trace performance monitoring and diagnostics monitor(TPDM)
==========================================================

    :Author:   Jinlong Mao <quic_jinlmao@quicinc.com>
    :Date:     January 2023

Hardware Description
--------------------
TPDM - The trace performance monitoring and diagnostics monitor or TPDM in
short serves as data collection component for various dataset types.
The primary use case of the TPDM is to collect data from different data
sources and send it to a TPDA for packetization, timestamping and funneling.

Sysfs files and directories
---------------------------
Root: ``/sys/bus/coresight/devices/tpdm<N>``

----

:File:            ``enable_source`` (RW)
:Notes:
    - > 0 : enable the datasets of TPDM.

    - = 0 : disable the datasets of TPDM.

:Syntax:
    ``echo 1 > enable_source``

----

:File:            ``integration_test`` (wo)
:Notes:
    Integration test will generate test data for tpdm.

:Syntax:
    ``echo value > integration_test``

    value -  1 or 2.

----

.. This text is intentionally added to make Sphinx happy.
