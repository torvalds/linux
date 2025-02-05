.. SPDX-License-Identifier: GPL-2.0

Power Sourcing Equipment (PSE) in IEEE 802.3 Standard
=====================================================

Overview
--------

Power Sourcing Equipment (PSE) is essential in networks for delivering power
along with data over Ethernet cables. It usually refers to devices like
switches and hubs that supply power to Powered Devices (PDs) such as IP
cameras, VoIP phones, and wireless access points.

PSE vs. PoDL PSE
----------------

PSE in the IEEE 802.3 standard generally refers to equipment that provides
power alongside data over Ethernet cables, typically associated with Power over
Ethernet (PoE).

PoDL PSE, or Power over Data Lines PSE, specifically denotes PSEs operating
with single balanced twisted-pair PHYs, as per Clause 104 of IEEE 802.3. PoDL
is significant in contexts like automotive and industrial controls where power
and data delivery over a single pair is advantageous.

IEEE 802.3-2018 Addendums and Related Clauses
---------------------------------------------

Key addenda to the IEEE 802.3-2018 standard relevant to power delivery over
Ethernet are as follows:

- **802.3af (Approved in 2003-06-12)**: Known as PoE in the market, detailed in
  Clause 33, delivering up to 15.4W of power.
- **802.3at (Approved in 2009-09-11)**: Marketed as PoE+, enhancing PoE as
  covered in Clause 33, increasing power delivery to up to 30W.
- **802.3bt (Approved in 2018-09-27)**: Known as 4PPoE in the market, outlined
  in Clause 33. Type 3 delivers up to 60W, and Type 4 up to 100W.
- **802.3bu (Approved in 2016-12-07)**: Formerly referred to as PoDL, detailed
  in Clause 104. Introduces Classes 0 - 9. Class 9 PoDL PSE delivers up to ~65W

Kernel Naming Convention Recommendations
----------------------------------------

For clarity and consistency within the Linux kernel's networking subsystem, the
following naming conventions are recommended:

- For general PSE (PoE) code, use "c33_pse" key words. For example:
  ``enum ethtool_c33_pse_admin_state c33_admin_control;``.
  This aligns with Clause 33, encompassing various PoE forms.

- For PoDL PSE - specific code, use "podl_pse". For example:
  ``enum ethtool_podl_pse_admin_state podl_admin_control;`` to differentiate
  PoDL PSE settings according to Clause 104.

Summary of Clause 33: Data Terminal Equipment (DTE) Power via Media Dependent Interface (MDI)
---------------------------------------------------------------------------------------------

Clause 33 of the IEEE 802.3 standard defines the functional and electrical
characteristics of Powered Device (PD) and Power Sourcing Equipment (PSE).
These entities enable power delivery using the same generic cabling as for data
transmission, integrating power with data communication for devices such as
10BASE-T, 100BASE-TX, or 1000BASE-T.

Summary of Clause 104: Power over Data Lines (PoDL) of Single Balanced Twisted-Pair Ethernet
--------------------------------------------------------------------------------------------

Clause 104 of the IEEE 802.3 standard delineates the functional and electrical
characteristics of PoDL Powered Devices (PDs) and PoDL Power Sourcing Equipment
(PSEs). These are designed for use with single balanced twisted-pair Ethernet
Physical Layers. In this clause, 'PSE' refers specifically to PoDL PSE, and
'PD' to PoDL PD. The key intent is to provide devices with a unified interface
for both data and the power required to process this data over a single
balanced twisted-pair Ethernet connection.
