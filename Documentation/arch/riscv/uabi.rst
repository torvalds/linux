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

"isa" and "hart isa" lines in /proc/cpuinfo
-------------------------------------------

The "isa" line in /proc/cpuinfo describes the lowest common denominator of
RISC-V ISA extensions recognized by the kernel and implemented on all harts. The
"hart isa" line, in contrast, describes the set of extensions recognized by the
kernel on the particular hart being described, even if those extensions may not
be present on all harts in the system.

In both lines, the presence of an extension guarantees only that the hardware
has the described capability. Additional kernel support or policy changes may be
required before an extension's capability is fully usable by userspace programs.
Similarly, for S-mode extensions, presence in one of these lines does not
guarantee that the kernel is taking advantage of the extension, or that the
feature will be visible in guest VMs managed by this kernel.

Inversely, the absence of an extension in these lines does not necessarily mean
the hardware does not support that feature. The running kernel may not recognize
the extension, or may have deliberately removed it from the listing.

Misaligned accesses
-------------------

Misaligned scalar accesses are supported in userspace, but they may perform
poorly.  Misaligned vector accesses are only supported if the Zicclsm extension
is supported.
