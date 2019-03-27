=======================
lld 8.0.0 Release Notes
=======================

.. contents::
    :local:

Introduction
============

lld is a high-performance linker that supports ELF (Unix), COFF (Windows),
Mach-O (macOS), MinGW and WebAssembly. lld is command-line-compatible with
GNU linkers and Microsoft link.exe and is significantly faster than the
system default linkers.

lld 8.0.0 has lots of feature improvements and bug fixes.

Non-comprehensive list of changes in this release
=================================================

ELF Improvements
----------------

* lld now supports RISC-V. (`r339364
  <https://reviews.llvm.org/rL339364>`_)

* Default image base address has changed from 65536 to 2 MiB for i386
  and 4 MiB for AArch64 to make lld-generated executables work better
  with automatic superpage promotion. FreeBSD can promote contiguous
  non-superpages to a superpage if they are aligned to the superpage
  size. (`r342746 <https://reviews.llvm.org/rL342746>`_)

* lld now attempts to place a ``.note`` segment in the first page of a
  generated file, so that you can find some important information
  (``.note.gnu.build-id`` in particular) in a core file even if a core
  file is truncated by ulimit.
  (`r349524 <https://reviews.llvm.org/rL349524>`_)

* lld now reports an error if ``_GLOBAL_OFFSET_TABLE_`` symbol is
  defined by an input object file, as the symbol is supposed to be
  synthesized by the linker.
  (`r347854 <https://reviews.llvm.org/rL347854>`_)

* lld/Hexagon can now link Linux kernel and musl libc for Qualcomm
  Hexagon ISA.

* Initial MSP430 ISA support has landed.

* lld now uses the ``sigrie`` instruction as a trap instruction for
  MIPS targets.

* lld now creates a TLS segment for AArch64 with a slightly larger
  alignment requirement, so that the loader makes a few bytes room
  before each TLS segment at runtime. The aim of this change is to
  make room to accomodate nonstandard Android TLS slots while keeping
  the compatibility with the standard AArch64 ABI.
  (`r350681 <https://reviews.llvm.org/rL350681>`_)

* The following flags have been added: ``--call-graph-profile``,
  ``--no-call-graph-profile``, ``--warn-ifunc-textrel``,
  ``-z interpose``, ``-z global``, ``-z nodefaultlib``

COFF Improvements
-----------------

* PDB GUID is set to hash of PDB contents instead to a random byte
  sequence for build reproducibility.

* ``/pdbsourcepath:`` is now also used to make ``"cwd"``, ``"exe"``, ``"pdb"``
  in the env block of PDB outputs absolute if they are relative, and to make
  paths to obj files referenced in PDB outputs absolute if they are relative.
  Together with the previous item, this makes it possible to generate
  executables and PDBs that are fully deterministic and independent of the
  absolute path to the build directory, so that different machines building
  the same code in different directories can produce exactly the same output.

* The following flags have been added: ``/force:multiple``

* lld now can link against import libraries produced by GNU tools.

* lld can create thunks for ARM and ARM64, to allow linking larger images
  (over 16 MB for ARM and over 128 MB for ARM64)

* Several speed and memory usage improvements.

* lld now creates debug info for typedefs.

* lld can now link obj files produced by ``cl.exe /Z7 /Yc``.

* lld now understands ``%_PDB%`` and ``%_EXT%`` in ``/pdbaltpath:``.

* Undefined symbols are now printed in demangled form in addition to raw form.

MinGW Improvements
------------------

* lld can now automatically import data variables from DLLs without the
  use of the dllimport attribute.

* lld can now use existing normal MinGW sysroots with import libraries and
  CRT startup object files for GNU binutils. lld can handle most object
  files produced by GCC, and thus works as a drop-in replacement for
  ld.bfd in such environments. (There are known issues with linking crtend.o
  from GCC in setups with DWARF exceptions though, where object files are
  linked in a different order than with GNU ld, inserting a DWARF exception
  table terminator too early.)

* lld now supports COFF embedded directives for linking to nondefault
  libraries, just like for the normal COFF target.

* Actually generate a codeview build id signature, even if not creating a PDB.
  Previously, the ``--build-id`` option did not actually generate a build id
  unless ``--pdb`` was specified.

WebAssembly Improvements
------------------------

* Add initial support for creating shared libraries (-shared).
  Note: The shared library format is still under active development and may
  undergo significant changes in future versions.
  See: https://github.com/WebAssembly/tool-conventions/blob/master/DynamicLinking.md
