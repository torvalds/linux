=================
BPF Documentation
=================

This directory contains documentation for the BPF (Berkeley Packet
Filter) facility, with a focus on the extended BPF version (eBPF).

This kernel side documentation is still work in progress.  The main
textual documentation is (for historical reasons) described in
`Documentation/networking/filter.rst`_, which describe both classical
and extended BPF instruction-set.
The Cilium project also maintains a `BPF and XDP Reference Guide`_
that goes into great technical depth about the BPF Architecture.

The primary info for the bpf syscall is available in the `man-pages`_
for `bpf(2)`_.

BPF Type Format (BTF)
=====================

.. toctree::
   :maxdepth: 1

   btf


Frequently asked questions (FAQ)
================================

Two sets of Questions and Answers (Q&A) are maintained.

.. toctree::
   :maxdepth: 1

   bpf_design_QA
   bpf_devel_QA


Program types
=============

.. toctree::
   :maxdepth: 1

   prog_cgroup_sockopt
   prog_cgroup_sysctl
   prog_flow_dissector
   bpf_lsm


Testing and debugging BPF
=========================

.. toctree::
   :maxdepth: 1

   drgn
   s390


.. Links:
.. _Documentation/networking/filter.rst: ../networking/filter.txt
.. _man-pages: https://www.kernel.org/doc/man-pages/
.. _bpf(2): http://man7.org/linux/man-pages/man2/bpf.2.html
.. _BPF and XDP Reference Guide: http://cilium.readthedocs.io/en/latest/bpf/
