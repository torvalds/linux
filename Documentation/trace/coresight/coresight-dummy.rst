.. SPDX-License-Identifier: GPL-2.0

=============================
Coresight Dummy Trace Module
=============================

    :Author:   Hao Zhang <quic_hazha@quicinc.com>
    :Date:     June 2023

Introduction
------------

The Coresight dummy trace module is for the specific devices that kernel don't
have permission to access or configure, e.g., CoreSight TPDMs on Qualcomm
platforms. For these devices, a dummy driver is needed to register them as
Coresight devices. The module may also be used to define components that may
not have any programming interfaces, so that paths can be created in the driver.
It provides Coresight API for operations on dummy devices, such as enabling and
disabling them. It also provides the Coresight dummy sink/source paths for
debugging.

Config details
--------------

There are two types of nodes, dummy sink and dummy source. These nodes
are available at ``/sys/bus/coresight/devices``.

Example output::

    $ ls -l /sys/bus/coresight/devices | grep dummy
    dummy_sink0 -> ../../../devices/platform/soc@0/soc@0:sink/dummy_sink0
    dummy_source0 -> ../../../devices/platform/soc@0/soc@0:source/dummy_source0
