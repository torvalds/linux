.. SPDX-License-Identifier: GPL-2.0

=====================================
SRAT - Static Resource Affinity Table
=====================================

The System/Static Resource Affinity Table describes resource (CPU, Memory)
affinity to "Proximity Domains". This table is technically optional, but for
performance information (see "HMAT") to be enumerated by linux it must be
present.

There is a careful dance between the CEDT and SRAT tables and how NUMA nodes are
created.  If things don't look quite the way you expect - check the SRAT Memory
Affinity entries and CEDT CFMWS to determine what your platform actually
supports in terms of flexible topologies.

The SRAT may statically assign portions of a CFMWS SPA range to a specific
proximity domains.  See linux numa creation for more information about how
this presents in the NUMA topology.

Proximity Domain
================
A proximity domain is ROUGHLY equivalent to "NUMA Node" - though a 1-to-1
mapping is not guaranteed.  There are scenarios where "Proximity Domain 4" may
map to "NUMA Node 3", for example.  (See "NUMA Node Creation")

Memory Affinity
===============
Generally speaking, if a host does any amount of CXL fabric (decoder)
programming in BIOS - an SRAT entry for that memory needs to be present.

Example ::

         Subtable Type : 01 [Memory Affinity]
                Length : 28
      Proximity Domain : 00000001          <- NUMA Node 1
             Reserved1 : 0000
          Base Address : 000000C050000000  <- Physical Memory Region
        Address Length : 0000003CA0000000
             Reserved2 : 00000000
 Flags (decoded below) : 0000000B
              Enabled : 1
        Hot Pluggable : 1
         Non-Volatile : 0
