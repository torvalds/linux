===============
/proc/sys/xen/
===============

Copyright (c) 2026, Shubham Chakraborty <chakrabortyshubham66@gmail.com>

For general info and legal blurb, please look in
Documentation/admin-guide/sysctl/index.rst.

------------------------------------------------------------------------------

These files show up in ``/proc/sys/xen/``, depending on the
kernel configuration:

.. contents:: :local:

balloon/hotplug_unpopulated
===========================

This flag controls whether unpopulated memory ranges are automatically
hotplugged as system RAM.

- ``0``: Unpopulated ranges are not hotplugged (default).
- ``1``: Unpopulated ranges are automatically hotplugged.

When enabled, the Xen balloon driver will add memory regions that are
marked as unpopulated in the Xen memory map to the system as usable RAM.
This allows for dynamic memory expansion in Xen guest domains.

This option is only available when the kernel is built with
``CONFIG_XEN_BALLOON_MEMORY_HOTPLUG`` enabled.
