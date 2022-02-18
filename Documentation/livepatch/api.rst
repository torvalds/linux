.. SPDX-License-Identifier: GPL-2.0

=================
Livepatching APIs
=================

Livepatch Enablement
====================

.. kernel-doc:: kernel/livepatch/core.c
   :export:


Shadow Variables
================

.. kernel-doc:: kernel/livepatch/shadow.c
   :export:

System State Changes
====================

.. kernel-doc:: kernel/livepatch/state.c
   :export:

Object Types
============

.. kernel-doc:: include/linux/livepatch.h
   :identifiers: klp_patch klp_object klp_func klp_callbacks klp_state
