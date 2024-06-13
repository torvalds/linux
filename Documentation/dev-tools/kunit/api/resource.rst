.. SPDX-License-Identifier: GPL-2.0

============
Resource API
============

This file documents the KUnit resource API.

Most users won't need to use this API directly, power users can use it to store
state on a per-test basis, register custom cleanup actions, and more.

.. kernel-doc:: include/kunit/resource.h
   :internal:
