=================
BPF Documentation
=================

This directory contains documentation for the BPF (Berkeley Packet
Filter) facility, with a focus on the extended BPF version (eBPF).

This kernel side documentation is still work in progress.
The Cilium project also maintains a `BPF and XDP Reference Guide`_
that goes into great technical depth about the BPF Architecture.

.. toctree::
   :maxdepth: 1

   verifier
   libbpf/index
   standardization/index
   btf
   faq
   syscall_api
   helpers
   kfuncs
   cpumasks
   programs
   maps
   bpf_prog_run
   classic_vs_extended.rst
   bpf_iterators
   bpf_licensing
   test_debug
   clang-notes
   other
   redirect

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`

.. Links:
.. _BPF and XDP Reference Guide: https://docs.cilium.io/en/latest/bpf/
