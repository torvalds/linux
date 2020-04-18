.. SPDX-License-Identifier: GPL-2.0

Other PCI Hardware supported by media subsystem
===============================================

Generally, media hardware manufacturers frequently change the ancillary
drivers, like tuners and demodulator units used, usually without
changing the product name, revision number or specs.

- Cards based on the Phillips saa7146 multimedia PCI bridge chip:

  - TI AV7110 based cards (i.e. with hardware MPEG decoder):
    - Siemens/Technotrend/Hauppauge PCI DVB card revision 1.1, 1.3, 1.5, 1.6, 2.1 (aka Hauppauge Nexus)
  - "budget" cards (i.e. without hardware MPEG decoder):
    - Technotrend Budget / Hauppauge WinTV-Nova PCI Cards
    - SATELCO Multimedia PCI
    - KNC1 DVB-S, Typhoon DVB-S, Terratec Cinergy 1200 DVB-S (no CI support)
    - Typhoon DVB-S budget
    - Fujitsu-Siemens Activy DVB-S budget card

- Cards based on the B2C2 Inc. FlexCopII/IIb/III:

  - Technisat SkyStar2 PCI DVB card revision 2.3, 2.6B, 2.6C

- Experimental support for the analog module of the Siemens DVB-C PCI card
