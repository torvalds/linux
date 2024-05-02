.. SPDX-License-Identifier: GPL-2.0

==========================
Devlink E-Switch Attribute
==========================

Devlink E-Switch supports two modes of operation: legacy and switchdev.
Legacy mode operates based on traditional MAC/VLAN steering rules. Switching
decisions are made based on MAC addresses, VLANs, etc. There is limited ability
to offload switching rules to hardware.

On the other hand, switchdev mode allows for more advanced offloading
capabilities of the E-Switch to hardware. In switchdev mode, more switching
rules and logic can be offloaded to the hardware switch ASIC. It enables
representor netdevices that represent the slow path of virtual functions (VFs)
or scalable-functions (SFs) of the device. See more information about
:ref:`Documentation/networking/switchdev.rst <switchdev>` and
:ref:`Documentation/networking/representors.rst <representors>`.

In addition, the devlink E-Switch also comes with other attributes listed
in the following section.

Attributes Description
======================

The following is a list of E-Switch attributes.

.. list-table:: E-Switch attributes
   :widths: 8 5 45

   * - Name
     - Type
     - Description
   * - ``mode``
     - enum
     - The mode of the device. The mode can be one of the following:

       * ``legacy`` operates based on traditional MAC/VLAN steering
         rules.
       * ``switchdev`` allows for more advanced offloading capabilities of
         the E-Switch to hardware.
   * - ``inline-mode``
     - enum
     - Some HWs need the VF driver to put part of the packet
       headers on the TX descriptor so the e-switch can do proper
       matching and steering. Support for both switchdev mode and legacy mode.

       * ``none`` none.
       * ``link`` L2 mode.
       * ``network`` L3 mode.
       * ``transport`` L4 mode.
   * - ``encap-mode``
     - enum
     - The encapsulation mode of the device. Support for both switchdev mode
       and legacy mode. The mode can be one of the following:

       * ``none`` Disable encapsulation support.
       * ``basic`` Enable encapsulation support.

Example Usage
=============

.. code:: shell

    # enable switchdev mode
    $ devlink dev eswitch set pci/0000:08:00.0 mode switchdev

    # set inline-mode and encap-mode
    $ devlink dev eswitch set pci/0000:08:00.0 inline-mode none encap-mode basic

    # display devlink device eswitch attributes
    $ devlink dev eswitch show pci/0000:08:00.0
      pci/0000:08:00.0: mode switchdev inline-mode none encap-mode basic

    # enable encap-mode with legacy mode
    $ devlink dev eswitch set pci/0000:08:00.0 mode legacy inline-mode none encap-mode basic
