.. SPDX-License-Identifier: GPL-2.0

:Author: Chris Li <chrisl@kernel.org>, Kairui Song <kasong@tencent.com>

==========
Swap Table
==========

Swap table implements swap cache as a per-cluster swap cache value array.

Swap Entry
----------

A swap entry contains the information required to serve the anonymous page
fault.

Swap entry is encoded as two parts: swap type and swap offset.

The swap type indicates which swap device to use.
The swap offset is the offset of the swap file to read the page data from.

Swap Cache
----------

Swap cache is a map to look up folios using swap entry as the key. The result
value can have three possible types depending on which stage of this swap entry
was in.

1. NULL: This swap entry is not used.

2. folio: A folio has been allocated and bound to this swap entry. This is
   the transient state of swap out or swap in. The folio data can be in
   the folio or swap file, or both.

3. shadow: The shadow contains the working set information of the swapped
   out folio. This is the normal state for a swapped out page.

Swap Table Internals
--------------------

The previous swap cache is implemented by XArray. The XArray is a tree
structure. Each lookup will go through multiple nodes. Can we do better?

Notice that most of the time when we look up the swap cache, we are either
in a swap in or swap out path. We should already have the swap cluster,
which contains the swap entry.

If we have a per-cluster array to store swap cache value in the cluster.
Swap cache lookup within the cluster can be a very simple array lookup.

We give such a per-cluster swap cache value array a name: the swap table.

A swap table is an array of pointers. Each pointer is the same size as a
PTE. The size of a swap table for one swap cluster typically matches a PTE
page table, which is one page on modern 64-bit systems.

With swap table, swap cache lookup can achieve great locality, simpler,
and faster.

Locking
-------

Swap table modification requires taking the cluster lock. If a folio
is being added to or removed from the swap table, the folio must be
locked prior to the cluster lock. After adding or removing is done, the
folio shall be unlocked.

Swap table lookup is protected by RCU and atomic read. If the lookup
returns a folio, the user must lock the folio before use.
