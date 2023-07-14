.. SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

.. _libbpf:

======
libbpf
======

If you are looking to develop BPF applications using the libbpf library, this
directory contains important documentation that you should read.

To get started, it is recommended to begin with the :doc:`libbpf Overview
<libbpf_overview>` document, which provides a high-level understanding of the
libbpf APIs and their usage. This will give you a solid foundation to start
exploring and utilizing the various features of libbpf to develop your BPF
applications.

.. toctree::
   :maxdepth: 1

   libbpf_overview
   API Documentation <https://libbpf.readthedocs.io/en/latest/api.html>
   program_types
   libbpf_naming_convention
   libbpf_build


All general BPF questions, including kernel functionality, libbpf APIs and their
application, should be sent to bpf@vger.kernel.org mailing list.  You can
`subscribe <http://vger.kernel.org/vger-lists.html#bpf>`_ to the mailing list
search its `archive <https://lore.kernel.org/bpf/>`_.  Please search the archive
before asking new questions. It may be that this was already addressed or
answered before.
