.. SPDX-License-Identifier: GPL-2.0

========================
Generic Radix Page Table
========================

.. kernel-doc:: include/linux/generic_pt/common.h
	:doc: Generic Radix Page Table

.. kernel-doc:: drivers/iommu/generic_pt/pt_defs.h
	:doc: Generic Page Table Language

Usage
=====

Generic PT is structured as a multi-compilation system. Since each format
provides an API using a common set of names there can be only one format active
within a compilation unit. This design avoids function pointers around the low
level API.

Instead the function pointers can end up at the higher level API (i.e.
map/unmap, etc.) and the per-format code can be directly inlined into the
per-format compilation unit. For something like IOMMU each format will be
compiled into a per-format IOMMU operations kernel module.

For this to work the .c file for each compilation unit will include both the
format headers and the generic code for the implementation. For instance in an
implementation compilation unit the headers would normally be included as
follows:

generic_pt/fmt/iommu_amdv1.c::

	#include <linux/generic_pt/common.h>
	#include "defs_amdv1.h"
	#include "../pt_defs.h"
	#include "amdv1.h"
	#include "../pt_common.h"
	#include "../pt_iter.h"
	#include "../iommu_pt.h"  /* The IOMMU implementation */

iommu_pt.h includes definitions that will generate the operations functions for
map/unmap/etc. using the definitions provided by AMDv1. The resulting module
will have exported symbols named like pt_iommu_amdv1_init().

Refer to drivers/iommu/generic_pt/fmt/iommu_template.h for an example of how the
IOMMU implementation uses multi-compilation to generate per-format ops structs
pointers.

The format code is written so that the common names arise from #defines to
distinct format specific names. This is intended to aid debuggability by
avoiding symbol clashes across all the different formats.

Exported symbols and other global names are mangled using a per-format string
via the NS() helper macro.

The format uses struct pt_common as the top-level struct for the table,
and each format will have its own struct pt_xxx which embeds it to store
format-specific information.

The implementation will further wrap struct pt_common in its own top-level
struct, such as struct pt_iommu_amdv1.

Format functions at the struct pt_common level
----------------------------------------------

.. kernel-doc:: include/linux/generic_pt/common.h
	:identifiers:
.. kernel-doc:: drivers/iommu/generic_pt/pt_common.h

Iteration Helpers
-----------------

.. kernel-doc:: drivers/iommu/generic_pt/pt_iter.h

Writing a Format
----------------

It is best to start from a simple format that is similar to the target. x86_64
is usually a good reference for something simple, and AMDv1 is something fairly
complete.

The required inline functions need to be implemented in the format header.
These should all follow the standard pattern of::

 static inline pt_oaddr_t amdv1pt_entry_oa(const struct pt_state *pts)
 {
	[..]
 }
 #define pt_entry_oa amdv1pt_entry_oa

where a uniquely named per-format inline function provides the implementation
and a define maps it to the generic name. This is intended to make debug symbols
work better. inline functions should always be used as the prototypes in
pt_common.h will cause the compiler to validate the function signature to
prevent errors.

Review pt_fmt_defaults.h to understand some of the optional inlines.

Once the format compiles then it should be run through the generic page table
kunit test in kunit_generic_pt.h using kunit. For example::

   $ tools/testing/kunit/kunit.py run --build_dir build_kunit_x86_64 --arch x86_64 --kunitconfig ./drivers/iommu/generic_pt/.kunitconfig amdv1_fmt_test.*
   [...]
   [11:15:08] Testing complete. Ran 9 tests: passed: 9
   [11:15:09] Elapsed time: 3.137s total, 0.001s configuring, 2.368s building, 0.311s running

The generic tests are intended to prove out the format functions and give
clearer failures to speed up finding the problems. Once those pass then the
entire kunit suite should be run.

IOMMU Invalidation Features
---------------------------

Invalidation is how the page table algorithms synchronize with a HW cache of the
page table memory, typically called the TLB (or IOTLB for IOMMU cases).

The TLB can store present PTEs, non-present PTEs and table pointers, depending
on its design. Every HW has its own approach on how to describe what has changed
to have changed items removed from the TLB.

PT_FEAT_FLUSH_RANGE
~~~~~~~~~~~~~~~~~~~

PT_FEAT_FLUSH_RANGE is the easiest scheme to understand. It tries to generate a
single range invalidation for each operation, over-invalidating if there are
gaps of VA that don't need invalidation. This trades off impacted VA for number
of invalidation operations. It does not keep track of what is being invalidated;
however, if pages have to be freed then page table pointers have to be cleaned
from the walk cache. The range can start/end at any page boundary.

PT_FEAT_FLUSH_RANGE_NO_GAPS
~~~~~~~~~~~~~~~~~~~~~~~~~~~

PT_FEAT_FLUSH_RANGE_NO_GAPS is similar to PT_FEAT_FLUSH_RANGE; however, it tries
to minimize the amount of impacted VA by issuing extra flush operations. This is
useful if the cost of processing VA is very high, for instance because a
hypervisor is processing the page table with a shadowing algorithm.
