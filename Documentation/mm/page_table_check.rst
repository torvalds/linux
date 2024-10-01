.. SPDX-License-Identifier: GPL-2.0

.. _page_table_check:

================
Page Table Check
================

Introduction
============

Page table check allows to harden the kernel by ensuring that some types of
the memory corruptions are prevented.

Page table check performs extra verifications at the time when new pages become
accessible from the userspace by getting their page table entries (PTEs PMDs
etc.) added into the table.

In case of detected corruption, the kernel is crashed. There is a small
performance and memory overhead associated with the page table check. Therefore,
it is disabled by default, but can be optionally enabled on systems where the
extra hardening outweighs the performance costs. Also, because page table check
is synchronous, it can help with debugging double map memory corruption issues,
by crashing kernel at the time wrong mapping occurs instead of later which is
often the case with memory corruptions bugs.

Double mapping detection logic
==============================

+-------------------+-------------------+-------------------+------------------+
| Current Mapping   | New mapping       | Permissions       | Rule             |
+===================+===================+===================+==================+
| Anonymous         | Anonymous         | Read              | Allow            |
+-------------------+-------------------+-------------------+------------------+
| Anonymous         | Anonymous         | Read / Write      | Prohibit         |
+-------------------+-------------------+-------------------+------------------+
| Anonymous         | Named             | Any               | Prohibit         |
+-------------------+-------------------+-------------------+------------------+
| Named             | Anonymous         | Any               | Prohibit         |
+-------------------+-------------------+-------------------+------------------+
| Named             | Named             | Any               | Allow            |
+-------------------+-------------------+-------------------+------------------+

Enabling Page Table Check
=========================

Build kernel with:

- PAGE_TABLE_CHECK=y
  Note, it can only be enabled on platforms where ARCH_SUPPORTS_PAGE_TABLE_CHECK
  is available.

- Boot with 'page_table_check=on' kernel parameter.

Optionally, build kernel with PAGE_TABLE_CHECK_ENFORCED in order to have page
table support without extra kernel parameter.

Implementation notes
====================

We specifically decided not to use VMA information in order to avoid relying on
MM states (except for limited "struct page" info). The page table check is a
separate from Linux-MM state machine that verifies that the user accessible
pages are not falsely shared.

PAGE_TABLE_CHECK depends on EXCLUSIVE_SYSTEM_RAM. The reason is that without
EXCLUSIVE_SYSTEM_RAM, users are allowed to map arbitrary physical memory
regions into the userspace via /dev/mem. At the same time, pages may change
their properties (e.g., from anonymous pages to named pages) while they are
still being mapped in the userspace, leading to "corruption" detected by the
page table check.

Even with EXCLUSIVE_SYSTEM_RAM, I/O pages may be still allowed to be mapped via
/dev/mem. However, these pages are always considered as named pages, so they
won't break the logic used in the page table check.
