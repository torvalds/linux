DocBook XML [DEPRECATED]
========================

.. attention::

   This section describes the deprecated DocBook XML toolchain. Please do not
   create new DocBook XML template files. Please consider converting existing
   DocBook XML templates files to Sphinx/reStructuredText.

Converting DocBook to Sphinx
----------------------------

Over time, we expect all of the documents under ``Documentation/DocBook`` to be
converted to Sphinx and reStructuredText. For most DocBook XML documents, a good
enough solution is to use the simple ``Documentation/sphinx/tmplcvt`` script,
which uses ``pandoc`` under the hood. For example::

  $ cd Documentation/sphinx
  $ ./tmplcvt ../DocBook/in.tmpl ../out.rst

Then edit the resulting rst files to fix any remaining issues, and add the
document in the ``toctree`` in ``Documentation/index.rst``.

Components of the kernel-doc system
-----------------------------------

Many places in the source tree have extractable documentation in the form of
block comments above functions. The components of this system are:

- ``scripts/kernel-doc``

  This is a perl script that hunts for the block comments and can mark them up
  directly into reStructuredText, DocBook, man, text, and HTML. (No, not
  texinfo.)

- ``Documentation/DocBook/*.tmpl``

  These are XML template files, which are normal XML files with special
  place-holders for where the extracted documentation should go.

- ``scripts/docproc.c``

  This is a program for converting XML template files into XML files. When a
  file is referenced it is searched for symbols exported (EXPORT_SYMBOL), to be
  able to distinguish between internal and external functions.

  It invokes kernel-doc, giving it the list of functions that are to be
  documented.

  Additionally it is used to scan the XML template files to locate all the files
  referenced herein. This is used to generate dependency information as used by
  make.

- ``Makefile``

  The targets 'xmldocs', 'psdocs', 'pdfdocs', and 'htmldocs' are used to build
  DocBook XML files, PostScript files, PDF files, and html files in
  Documentation/DocBook. The older target 'sgmldocs' is equivalent to 'xmldocs'.

- ``Documentation/DocBook/Makefile``

  This is where C files are associated with SGML templates.

How to use kernel-doc comments in DocBook XML template files
------------------------------------------------------------

DocBook XML template files (\*.tmpl) are like normal XML files, except that they
can contain escape sequences where extracted documentation should be inserted.

``!E<filename>`` is replaced by the documentation, in ``<filename>``, for
functions that are exported using ``EXPORT_SYMBOL``: the function list is
collected from files listed in ``Documentation/DocBook/Makefile``.

``!I<filename>`` is replaced by the documentation for functions that are **not**
exported using ``EXPORT_SYMBOL``.

``!D<filename>`` is used to name additional files to search for functions
exported using ``EXPORT_SYMBOL``.

``!F<filename> <function [functions...]>`` is replaced by the documentation, in
``<filename>``, for the functions listed.

``!P<filename> <section title>`` is replaced by the contents of the ``DOC:``
section titled ``<section title>`` from ``<filename>``. Spaces are allowed in
``<section title>``; do not quote the ``<section title>``.

``!C<filename>`` is replaced by nothing, but makes the tools check that all DOC:
sections and documented functions, symbols, etc. are used. This makes sense to
use when you use ``!F`` or ``!P`` only and want to verify that all documentation
is included.
