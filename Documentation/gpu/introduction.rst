============
Introduction
============

The Linux DRM layer contains code intended to support the needs of
complex graphics devices, usually containing programmable pipelines well
suited to 3D graphics acceleration. Graphics drivers in the kernel may
make use of DRM functions to make tasks like memory management,
interrupt handling and DMA easier, and provide a uniform interface to
applications.

A note on versions: this guide covers features found in the DRM tree,
including the TTM memory manager, output configuration and mode setting,
and the new vblank internals, in addition to all the regular features
found in current kernels.

[Insert diagram of typical DRM stack here]

Style Guidelines
================

For consistency this documentation uses American English. Abbreviations
are written as all-uppercase, for example: DRM, KMS, IOCTL, CRTC, and so
on. To aid in reading, documentations make full use of the markup
characters kerneldoc provides: @parameter for function parameters,
@member for structure members (within the same structure), &struct structure to
reference structures and function() for functions. These all get automatically
hyperlinked if kerneldoc for the referenced objects exists. When referencing
entries in function vtables (and structure members in general) please use
&vtable_name.vfunc. Unfortunately this does not yet yield a direct link to the
member, only the structure.

Except in special situations (to separate locked from unlocked variants)
locking requirements for functions aren't documented in the kerneldoc.
Instead locking should be check at runtime using e.g.
``WARN_ON(!mutex_is_locked(...));``. Since it's much easier to ignore
documentation than runtime noise this provides more value. And on top of
that runtime checks do need to be updated when the locking rules change,
increasing the chances that they're correct. Within the documentation
the locking rules should be explained in the relevant structures: Either
in the comment for the lock explaining what it protects, or data fields
need a note about which lock protects them, or both.

Functions which have a non-\ ``void`` return value should have a section
called "Returns" explaining the expected return values in different
cases and their meanings. Currently there's no consensus whether that
section name should be all upper-case or not, and whether it should end
in a colon or not. Go with the file-local style. Other common section
names are "Notes" with information for dangerous or tricky corner cases,
and "FIXME" where the interface could be cleaned up.

Also read the :ref:`guidelines for the kernel documentation at large <doc_guide>`.
