.. SPDX-License-Identifier: GPL-2.0

RISC-V Linux User ABI
=====================

ISA string ordering in /proc/cpuinfo
------------------------------------

The canonical order of ISA extension names in the ISA string is defined in
chapter 27 of the unprivileged specification.
The specification uses vague wording, such as should, when it comes to ordering,
so for our purposes the following rules apply:

#. Single-letter extensions come first, in canonical order.
   The canonical order is "IMAFDQLCBKJTPVH".

#. All multi-letter extensions will be separated from other extensions by an
   underscore.

#. Additional standard extensions (starting with 'Z') will be sorted after
   single-letter extensions and before any higher-privileged extensions.

#. For additional standard extensions, the first letter following the 'Z'
   conventionally indicates the most closely related alphabetical
   extension category. If multiple 'Z' extensions are named, they will be
   ordered first by category, in canonical order, as listed above, then
   alphabetically within a category.

#. Standard supervisor-level extensions (starting with 'S') will be listed
   after standard unprivileged extensions.  If multiple supervisor-level
   extensions are listed, they will be ordered alphabetically.

#. Standard machine-level extensions (starting with 'Zxm') will be listed
   after any lower-privileged, standard extensions. If multiple machine-level
   extensions are listed, they will be ordered alphabetically.

#. Non-standard extensions (starting with 'X') will be listed after all standard
   extensions. If multiple non-standard extensions are listed, they will be
   ordered alphabetically.

An example string following the order is::

   rv64imadc_zifoo_zigoo_zafoo_sbar_scar_zxmbaz_xqux_xrux

Misaligned accesses
-------------------

Misaligned accesses are supported in userspace, but they may perform poorly.
