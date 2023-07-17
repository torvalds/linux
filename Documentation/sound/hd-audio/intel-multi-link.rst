.. SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
.. include:: <isonum.txt>

================================================
HDAudio multi-link extensions on Intel platforms
================================================

:Copyright: |copy| 2023 Intel Corporation

This file documents the 'multi-link structure' introduced in 2015 with
the Skylake processor and recently extended in newer Intel platforms

HDaudio existing link mapping (2015 addition in SkyLake)
========================================================

External HDAudio codecs are handled with link #0, while iDISP codec
for HDMI/DisplayPort is handled with link #1.

The only change to the 2015 definitions is the declaration of the
LCAP.ALT=0x0 - since the ALT bit was previously reserved, this is a
backwards-compatible change.

LCTL.SPA and LCTL.CPA are automatically set when exiting reset. They
are only used in existing drivers when the SCF value needs to be
corrected.

Basic structure for HDaudio codecs
----------------------------------

::

  +-----------+
  | ML cap #0 |
  +-----------+
  | ML cap #1 |---+
  +-----------+   |
                  |
                  +--> 0x0 +---------------+ LCAP
                           | ALT=0         |
                           +---------------+
                           | S192          |
                           +---------------+
                           | S96           |
                           +---------------+
                           | S48           |
                           +---------------+
                           | S24           |
                           +---------------+
                           | S12           |
                           +---------------+
                           | S6            |
                           +---------------+

                       0x4 +---------------+ LCTL
                           | INTSTS        |
                           +---------------+
                           | CPA           |
                           +---------------+
                           | SPA           |
                           +---------------+
                           | SCF           |
                           +---------------+

                       0x8 +---------------+ LOSIDV
                           | L1OSIVD15     |
                           +---------------+
                           | L1OSIDV..     |
                           +---------------+
                           | L1OSIDV1      |
                           +---------------+

                       0xC +---------------+ LSDIID
                           | SDIID14       |
                           +---------------+
                           | SDIID...      |
                           +---------------+
                           | SDIID0        |
                           +---------------+

SoundWire HDaudio extended link mapping
=======================================

A SoundWire extended link is identified when LCAP.ALT=1 and
LEPTR.ID=0.

DMA control uses the existing LOSIDV register.

Changes include additional descriptions for enumeration that were not
present in earlier generations.

- multi-link synchronization: capabilities in LCAP.LSS and control in LSYNC
- number of sublinks (manager IP) in LCAP.LSCOUNT
- power management moved from SHIM to LCTL.SPA bits
- hand-over to the DSP for access to multi-link registers, SHIM/IP with LCTL.OFLEN
- mapping of SoundWire codecs to SDI ID bits
- move of SHIM and Cadence registers to different offsets, with no
  change in functionality. The LEPTR.PTR value is an offset from the
  ML address, with a default value of 0x30000.

Extended structure for SoundWire (assuming 4 Manager IP)
--------------------------------------------------------

::

  +-----------+
  | ML cap #0 |
  +-----------+
  | ML cap #1 |
  +-----------+
  | ML cap #2 |---+
  +-----------+   |
                  |
                  +--> 0x0 +---------------+ LCAP
                           | ALT=1         |
                           +---------------+
                           | INTC          |
                           +---------------+
                           | OFLS          |
                           +---------------+
                           | LSS           |
                           +---------------+
                           | SLCOUNT=4     |-----------+
                           +---------------+           |
                                                       |
                       0x4 +---------------+ LCTL      |
                           | INTSTS        |           |
                           +---------------+           |
                           | CPA (x bits)  |           |
                           +---------------+           |
                           | SPA (x bits)  |           |
                           +---------------+         for each sublink x
                           | INTEN         |           |
                           +---------------+           |
                           | OFLEN         |           |
                           +---------------+           |
                                                       |
                       0x8 +---------------+ LOSIDV    |
                           | L1OSIVD15     |           |
                           +---------------+           |
                           | L1OSIDV..     |           |
                           +---------------+           |
                           | L1OSIDV1      |       +---+----------------------------------------------------------+
                           +---------------+       |                                                              |
                                                   v                                                              |
             0xC + 0x2 * x +---------------+ LSDIIDx    +---> 0x30000  +-----------------+  0x00030000            |
                           | SDIID14       |            |              | SoundWire SHIM  |                        |
                           +---------------+            |              | generic         |                        |
                           | SDIID...      |            |              +-----------------+  0x00030100            |
                           +---------------+            |              | SoundWire IP    |                        |
                           | SDIID0        |            |              +-----------------+  0x00036000            |
                           +---------------+            |              | SoundWire SHIM  |                        |
                                                        |              | vendor-specific |                        |
                      0x1C +---------------+ LSYNC      |              +-----------------+                        |
                           | CMDSYNC       |            |                                                         v
                           +---------------+            |              +-----------------+  0x00030000 + 0x8000 * x
                           | SYNCGO        |            |              | SoundWire SHIM  |
                           +---------------+            |              | generic         |
                           | SYNCPU        |            |              +-----------------+  0x00030100 + 0x8000 * x
                           +---------------+            |              | SoundWire IP    |
                           | SYNPRD        |            |              +-----------------+  0x00036000 + 0x8000 * x
                           +---------------+            |              | SoundWire SHIM  |
                                                        |              | vendor-specific |
                      0x20 +---------------+ LEPTR      |              +-----------------+
                           | ID = 0        |            |
                           +---------------+            |
                           | VER           |            |
                           +---------------+            |
                           | PTR           |------------+
                           +---------------+


DMIC HDaudio extended link mapping
==================================

A DMIC extended link is identified when LCAP.ALT=1 and
LEPTR.ID=0xC1 are set.

DMA control uses the existing LOSIDV register

Changes include additional descriptions for enumeration that were not
present in earlier generations.

- multi-link synchronization: capabilities in LCAP.LSS and control in LSYNC
- power management with LCTL.SPA bits
- hand-over to the DSP for access to multi-link registers, SHIM/IP with LCTL.OFLEN

- move of DMIC registers to different offsets, with no change in
  functionality. The LEPTR.PTR value is an offset from the ML
  address, with a default value of 0x10000.

Extended structure for DMIC
---------------------------

::

  +-----------+
  | ML cap #0 |
  +-----------+
  | ML cap #1 |
  +-----------+
  | ML cap #2 |---+
  +-----------+   |
                  |
                  +--> 0x0 +---------------+ LCAP
                           | ALT=1         |
                           +---------------+
                           | INTC          |
                           +---------------+
                           | OFLS          |
                           +---------------+
                           | SLCOUNT=1     |
                           +---------------+

                       0x4 +---------------+ LCTL
                           | INTSTS        |
                           +---------------+
                           | CPA           |
                           +---------------+
                           | SPA           |
                           +---------------+
                           | INTEN         |
                           +---------------+
                           | OFLEN         |
                           +---------------+           +---> 0x10000  +-----------------+  0x00010000
                                                       |              | DMIC SHIM       |
                       0x8 +---------------+ LOSIDV    |              | generic         |
                           | L1OSIVD15     |           |              +-----------------+  0x00010100
                           +---------------+           |              | DMIC IP         |
                           | L1OSIDV..     |           |              +-----------------+  0x00016000
                           +---------------+           |              | DMIC SHIM       |
                           | L1OSIDV1      |           |              | vendor-specific |
                           +---------------+           |              +-----------------+
                                                       |
                      0x20 +---------------+ LEPTR     |
                           | ID = 0xC1     |           |
                           +---------------+           |
                           | VER           |           |
                           +---------------+           |
                           | PTR           |-----------+
                           +---------------+


SSP HDaudio extended link mapping
=================================

A DMIC extended link is identified when LCAP.ALT=1 and
LEPTR.ID=0xC0 are set.

DMA control uses the existing LOSIDV register

Changes include additional descriptions for enumeration and control that were not
present in earlier generations:
- number of sublinks (SSP IP instances) in LCAP.LSCOUNT
- power management moved from SHIM to LCTL.SPA bits
- hand-over to the DSP for access to multi-link registers, SHIM/IP
with LCTL.OFLEN
- move of SHIM and SSP IP registers to different offsets, with no
change in functionality.  The LEPTR.PTR value is an offset from the ML
address, with a default value of 0x28000.

Extended structure for SSP (assuming 3 instances of the IP)
-----------------------------------------------------------

::

  +-----------+
  | ML cap #0 |
  +-----------+
  | ML cap #1 |
  +-----------+
  | ML cap #2 |---+
  +-----------+   |
                  |
                  +--> 0x0 +---------------+ LCAP
                           | ALT=1         |
                           +---------------+
                           | INTC          |
                           +---------------+
                           | OFLS          |
                           +---------------+
                           | SLCOUNT=3     |-------------------------for each sublink x -------------------------+
                           +---------------+                                                                     |
                                                                                                                 |
                       0x4 +---------------+ LCTL                                                                |
                           | INTSTS        |                                                                     |
                           +---------------+                                                                     |
                           | CPA (x bits)  |                                                                     |
                           +---------------+                                                                     |
                           | SPA (x bits)  |                                                                     |
                           +---------------+                                                                     |
                           | INTEN         |                                                                     |
                           +---------------+                                                                     |
                           | OFLEN         |                                                                     |
                           +---------------+           +---> 0x28000  +-----------------+  0x00028000            |
                                                       |              | SSP SHIM        |                        |
                       0x8 +---------------+ LOSIDV    |              | generic         |                        |
                           | L1OSIVD15     |           |              +-----------------+  0x00028100            |
                           +---------------+           |              | SSP IP          |                        |
                           | L1OSIDV..     |           |              +-----------------+  0x00028C00            |
                           +---------------+           |              | SSP SHIM        |                        |
                           | L1OSIDV1      |           |              | vendor-specific |                        |
                           +---------------+           |              +-----------------+                        |
                                                       |                                                         v
                      0x20 +---------------+ LEPTR     |              +-----------------+  0x00028000 + 0x1000 * x
                           | ID = 0xC0     |           |              | SSP SHIM        |
                           +---------------+           |              | generic         |
                           | VER           |           |              +-----------------+  0x00028100 + 0x1000 * x
                           +---------------+           |              | SSP IP          |
                           | PTR           |-----------+              +-----------------+  0x00028C00 + 0x1000 * x
                           +---------------+                          | SSP SHIM        |
                                                                      | vendor-specific |
                                                                      +-----------------+
