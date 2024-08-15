.. SPDX-License-Identifier: GPL-2.0+

================================
Documentation for /proc/sys/abi/
================================

.. See scripts/check-sysctl-docs to keep this up to date:
.. scripts/check-sysctl-docs -vtable="abi" \
..         Documentation/admin-guide/sysctl/abi.rst \
..         $(git grep -l register_sysctl_)

Copyright (c) 2020, Stephen Kitt

For general info, see Documentation/admin-guide/sysctl/index.rst.

------------------------------------------------------------------------------

The files in ``/proc/sys/abi`` can be used to see and modify
ABI-related settings.

Currently, these files might (depending on your configuration)
show up in ``/proc/sys/kernel``:

.. contents:: :local:

vsyscall32 (x86)
================

Determines whether the kernels maps a vDSO page into 32-bit processes;
can be set to 1 to enable, or 0 to disable. Defaults to enabled if
``CONFIG_COMPAT_VDSO`` is set, disabled otherwise.

This controls the same setting as the ``vdso32`` kernel boot
parameter.
