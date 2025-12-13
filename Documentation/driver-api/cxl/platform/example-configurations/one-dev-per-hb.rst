.. SPDX-License-Identifier: GPL-2.0

==========================
One Device per Host Bridge
==========================

This system has a single socket with two CXL host bridges. Each host bridge
has a single CXL memory expander with a 4GB of memory.

Things to note:

* Cross-Bridge interleave is not being used.
* The expanders are in two separate but adjacent memory regions.
* This CEDT/SRAT describes one node per device
* The expanders have the same performance and will be in the same memory tier.

:doc:`CEDT <../acpi/cedt>`::

            Subtable Type : 00 [CXL Host Bridge Structure]
                 Reserved : 00
                   Length : 0020
   Associated host bridge : 00000007
    Specification version : 00000001
                 Reserved : 00000000
            Register base : 0000010370400000
          Register length : 0000000000010000

            Subtable Type : 00 [CXL Host Bridge Structure]
                 Reserved : 00
                   Length : 0020
   Associated host bridge : 00000006
    Specification version : 00000001
                 Reserved : 00000000
            Register base : 0000010380800000
          Register length : 0000000000010000

            Subtable Type : 01 [CXL Fixed Memory Window Structure]
                 Reserved : 00
                   Length : 002C
                 Reserved : 00000000
      Window base address : 0000001000000000
              Window size : 0000000100000000
 Interleave Members (2^n) : 00
    Interleave Arithmetic : 00
                 Reserved : 0000
              Granularity : 00000000
             Restrictions : 0006
                    QtgId : 0001
             First Target : 00000007

            Subtable Type : 01 [CXL Fixed Memory Window Structure]
                 Reserved : 00
                   Length : 002C
                 Reserved : 00000000
      Window base address : 0000001100000000
              Window size : 0000000100000000
 Interleave Members (2^n) : 00
    Interleave Arithmetic : 00
                 Reserved : 0000
              Granularity : 00000000
             Restrictions : 0006
                    QtgId : 0001
             First Target : 00000006

:doc:`SRAT <../acpi/srat>`::

         Subtable Type : 01 [Memory Affinity]
                Length : 28
      Proximity Domain : 00000001
             Reserved1 : 0000
          Base Address : 0000001000000000
        Address Length : 0000000100000000
             Reserved2 : 00000000
 Flags (decoded below) : 0000000B
             Enabled : 1
       Hot Pluggable : 1
        Non-Volatile : 0

         Subtable Type : 01 [Memory Affinity]
                Length : 28
      Proximity Domain : 00000002
             Reserved1 : 0000
          Base Address : 0000001100000000
        Address Length : 0000000100000000
             Reserved2 : 00000000
 Flags (decoded below) : 0000000B
             Enabled : 1
       Hot Pluggable : 1
        Non-Volatile : 0

:doc:`HMAT <../acpi/hmat>`::

               Structure Type : 0001 [SLLBI]
                    Data Type : 00   [Latency]
 Target Proximity Domain List : 00000000
 Target Proximity Domain List : 00000001
 Target Proximity Domain List : 00000002
                        Entry : 0080
                        Entry : 0100
                        Entry : 0100

               Structure Type : 0001 [SLLBI]
                    Data Type : 03   [Bandwidth]
 Target Proximity Domain List : 00000000
 Target Proximity Domain List : 00000001
 Target Proximity Domain List : 00000002
                        Entry : 1200
                        Entry : 0200
                        Entry : 0200

:doc:`SLIT <../acpi/slit>`::

     Signature : "SLIT"    [System Locality Information Table]
    Localities : 0000000000000003
  Locality   0 : 10 20 20
  Locality   1 : FF 0A FF
  Locality   2 : FF FF 0A

:doc:`DSDT <../acpi/dsdt>`::

  Scope (_SB)
  {
    Device (S0D0)
    {
        Name (_HID, "ACPI0016" /* Compute Express Link Host Bridge */)  // _HID: Hardware ID
        ...
        Name (_UID, 0x07)  // _UID: Unique ID
    }
    ...
    Device (S0D5)
    {
        Name (_HID, "ACPI0016" /* Compute Express Link Host Bridge */)  // _HID: Hardware ID
        ...
        Name (_UID, 0x06)  // _UID: Unique ID
    }
  }
