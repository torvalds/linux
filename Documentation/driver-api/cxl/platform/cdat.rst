.. SPDX-License-Identifier: GPL-2.0

======================================
Coherent Device Attribute Table (CDAT)
======================================

The CDAT provides functional and performance attributes of devices such
as CXL accelerators, switches, or endpoints.  The table formatting is
similar to ACPI tables. CDAT data may be parsed by BIOS at boot or may
be enumerated at runtime (after device hotplug, for example).

Terminology:
DPA - Device Physical Address, used by the CXL device to denote the address
it supports for that device.

DSMADHandle - A device unique handle that is associated with a DPA range
defined by the DSMAS table.


===============================================
Device Scoped Memory Affinity Structure (DSMAS)
===============================================

The DSMAS contains information such as DSMADHandle, the DPA Base, and DPA
Length.

This table is used by Linux in conjunction with the Device Scoped Latency and
Bandwidth Information Structure (DSLBIS) to determine the performance
attributes of the CXL device itself.

Example ::

 Structure Type : 00 [DSMAS]
       Reserved : 00
         Length : 0018              <- 24d, size of structure
    DSMADHandle : 01
          Flags : 00
       Reserved : 0000
       DPA Base : 0000000040000000  <- 1GiB base
     DPA Length : 0000000080000000  <- 2GiB size


==================================================================
Device Scoped Latency and Bandwidth Information Structure (DSLBIS)
==================================================================

This table is used by Linux in conjunction with DSMAS to determine the
performance attributes of a CXL device.  The DSLBIS contains latency
and bandwidth information based on DSMADHandle matching.

Example ::

   Structure Type : 01 [DSLBIS]
         Reserved : 00
           Length : 18                     <- 24d, size of structure
           Handle : 0001                   <- DSMAS handle
            Flags : 00                     <- Matches flag field for HMAT SLLBIS
        Data Type : 00                     <- Latency
 Entry Basee Unit : 0000000000001000       <- Entry Base Unit field in HMAT SSLBIS
            Entry : 010000000000           <- First byte used here, CXL LTC
         Reserved : 0000

   Structure Type : 01 [DSLBIS]
         Reserved : 00
           Length : 18                     <- 24d, size of structure
           Handle : 0001                   <- DSMAS handle
            Flags : 00                     <- Matches flag field for HMAT SLLBIS
        Data Type : 03                     <- Bandwidth
 Entry Basee Unit : 0000000000001000       <- Entry Base Unit field in HMAT SSLBIS
            Entry : 020000000000           <- First byte used here, CXL BW
         Reserved : 0000


==================================================================
Switch Scoped Latency and Bandwidth Information Structure (SSLBIS)
==================================================================

The SSLBIS contains information about the latency and bandwidth of a switch.

The table is used by Linux to compute the performance coordinates of a CXL path
from the device to the root port where a switch is part of the path.

Example ::

  Structure Type : 05 [SSLBIS]
        Reserved : 00
          Length : 20                           <- 32d, length of record, including SSLB entries
       Data Type : 00                           <- Latency
        Reserved : 000000
 Entry Base Unit : 00000000000000001000         <- Matches Entry Base Unit in HMAT SSLBIS

                                                <- SSLB Entry 0
       Port X ID : 0100                         <- First port, 0100h represents an upstream port
       Port Y ID : 0000                         <- Second port, downstream port 0
         Latency : 0100                         <- Port latency
        Reserved : 0000
                                                <- SSLB Entry 1
       Port X ID : 0100
       Port Y ID : 0001
         Latency : 0100
        Reserved : 0000


  Structure Type : 05 [SSLBIS]
        Reserved : 00
          Length : 18                           <- 24d, length of record, including SSLB entry
       Data Type : 03                           <- Bandwidth
        Reserved : 000000
 Entry Base Unit : 00000000000000001000         <- Matches Entry Base Unit in HMAT SSLBIS

                                                <- SSLB Entry 0
       Port X ID : 0100                         <- First port, 0100h represents an upstream port
       Port Y ID : FFFF                         <- Second port, FFFFh indicates any port
       Bandwidth : 1200                         <- Port bandwidth
        Reserved : 0000

The CXL driver uses a combination of CDAT, HMAT, SRAT, and other data to
generate "whole path performance" data for a CXL device.
