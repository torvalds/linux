.. SPDX-License-Identifier: GPL-2.0

========================================
SLIT - System Locality Information Table
========================================

The system locality information table provides "abstract distances" between
accessor and memory nodes.  Node without initiators (cpus) are infinitely (FF)
distance away from all other nodes.

The abstract distance described in this table does not describe any real
latency of bandwidth information.

Example ::

    Signature : "SLIT"    [System Locality Information Table]
   Localities : 0000000000000004
 Locality   0 : 10 20 20 30
 Locality   1 : 20 10 30 20
 Locality   2 : FF FF 0A FF
 Locality   3 : FF FF FF 0A
