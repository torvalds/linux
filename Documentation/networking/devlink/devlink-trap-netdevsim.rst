.. SPDX-License-Identifier: GPL-2.0

======================
Devlink Trap netdevsim
======================

Driver-specific Traps
=====================

.. list-table:: List of Driver-specific Traps Registered by ``netdevsim``
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fid_miss``
     - ``exception``
     - When a packet enters the device it is classified to a filtering
       indentifier (FID) based on the ingress port and VLAN. This trap is used
       to trap packets for which a FID could not be found
