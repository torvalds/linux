ATOM-based lld
==============

Note: this document discuss Mach-O port of LLD. For ELF and COFF,
see :doc:`index`.

ATOM-based lld is a new set of modular code for creating linker tools.
Currently it supports Mach-O.

* End-User Features:

  * Compatible with existing linker options
  * Reads standard Object Files
  * Writes standard Executable Files
  * Remove clang's reliance on "the system linker"
  * Uses the LLVM `"UIUC" BSD-Style license`__.

* Applications:

  * Modular design
  * Support cross linking
  * Easy to add new CPU support
  * Can be built as static tool or library

* Design and Implementation:

  * Extensive unit tests
  * Internal linker model can be dumped/read to textual format
  * Additional linking features can be plugged in as "passes"
  * OS specific and CPU specific code factored out

Why a new linker?
-----------------

The fact that clang relies on whatever linker tool you happen to have installed
means that clang has been very conservative adopting features which require a
recent linker.

In the same way that the MC layer of LLVM has removed clang's reliance on the
system assembler tool, the lld project will remove clang's reliance on the
system linker tool.


Contents
--------

.. toctree::
   :maxdepth: 2

   design
   getting_started
   development
   open_projects
   sphinx_intro

Indices and tables
------------------

* :ref:`genindex`
* :ref:`search`

__ http://llvm.org/docs/DeveloperPolicy.html#license
