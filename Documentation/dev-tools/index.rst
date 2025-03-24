================================
Development tools for the kernel
================================

This document is a collection of documents about development tools that can
be used to work on the kernel. For now, the documents have been pulled
together without any significant effort to integrate them into a coherent
whole; patches welcome!

A brief overview of testing-specific tools can be found in
Documentation/dev-tools/testing-overview.rst

Tools that are specific to debugging can be found in
Documentation/process/debugging/index.rst

.. toctree::
   :caption: Table of contents
   :maxdepth: 2

   testing-overview
   checkpatch
   clang-format
   coccinelle
   sparse
   kcov
   gcov
   kasan
   kmsan
   ubsan
   kmemleak
   kcsan
   kfence
   kselftest
   kunit/index
   ktap
   checkuapi
   gpio-sloppy-logic-analyzer
   autofdo
   propeller


.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`
