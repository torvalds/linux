.. SPDX-License-Identifier: GPL-2.0

MEI NFC
-------

Some Intel 8 and 9 Series chipsets support NFC devices connected behind
the Intel Management Engine controller.
MEI client bus exposes the NFC chips as NFC phy devices and enables
binding with Microread and NXP PN544 NFC device driver from the Linux NFC
subsystem.

.. kernel-render:: DOT
   :alt: MEI NFC digraph
   :caption: **MEI NFC** Stack

   digraph NFC {
    cl_nfc -> me_cl_nfc;
    "drivers/nfc/mei_phy" -> cl_nfc [lhead=bus];
    "drivers/nfc/microread/mei" -> cl_nfc;
    "drivers/nfc/microread/mei" -> "drivers/nfc/mei_phy";
    "drivers/nfc/pn544/mei" -> cl_nfc;
    "drivers/nfc/pn544/mei" -> "drivers/nfc/mei_phy";
    "net/nfc" -> "drivers/nfc/microread/mei";
    "net/nfc" -> "drivers/nfc/pn544/mei";
    "neard" -> "net/nfc";
    cl_nfc [label="mei/bus(nfc)"];
    me_cl_nfc [label="me fw (nfc)"];
   }
