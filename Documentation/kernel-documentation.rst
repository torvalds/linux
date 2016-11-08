==========================
Linux Kernel Documentation
==========================

Introduction
============

The Linux kernel uses `Sphinx`_ to generate pretty documentation from
`reStructuredText`_ files under ``Documentation``. To build the documentation in
HTML or PDF formats, use ``make htmldocs`` or ``make pdfdocs``. The generated
documentation is placed in ``Documentation/output``.

.. _Sphinx: http://www.sphinx-doc.org/
.. _reStructuredText: http://docutils.sourceforge.net/rst.html

The reStructuredText files may contain directives to include structured
documentation comments, or kernel-doc comments, from source files. Usually these
are used to describe the functions and types and design of the code. The
kernel-doc comments have some special structure and formatting, but beyond that
they are also treated as reStructuredText.

There is also the deprecated DocBook toolchain to generate documentation from
DocBook XML template files under ``Documentation/DocBook``. The DocBook files
are to be converted to reStructuredText, and the toolchain is slated to be
removed.

Finally, there are thousands of plain text documentation files scattered around
``Documentation``. Some of these will likely be converted to reStructuredText
over time, but the bulk of them will remain in plain text.

Sphinx Build
============

The usual way to generate the documentation is to run ``make htmldocs`` or
``make pdfdocs``. There are also other formats available, see the documentation
section of ``make help``. The generated documentation is placed in
format-specific subdirectories under ``Documentation/output``.

To generate documentation, Sphinx (``sphinx-build``) must obviously be
installed. For prettier HTML output, the Read the Docs Sphinx theme
(``sphinx_rtd_theme``) is used if available. For PDF output, ``rst2pdf`` is also
needed. All of these are widely available and packaged in distributions.

To pass extra options to Sphinx, you can use the ``SPHINXOPTS`` make
variable. For example, use ``make SPHINXOPTS=-v htmldocs`` to get more verbose
output.

To remove the generated documentation, run ``make cleandocs``.

Writing Documentation
=====================

Adding new documentation can be as simple as:

1. Add a new ``.rst`` file somewhere under ``Documentation``.
2. Refer to it from the Sphinx main `TOC tree`_ in ``Documentation/index.rst``.

.. _TOC tree: http://www.sphinx-doc.org/en/stable/markup/toctree.html

This is usually good enough for simple documentation (like the one you're
reading right now), but for larger documents it may be advisable to create a
subdirectory (or use an existing one). For example, the graphics subsystem
documentation is under ``Documentation/gpu``, split to several ``.rst`` files,
and has a separate ``index.rst`` (with a ``toctree`` of its own) referenced from
the main index.

See the documentation for `Sphinx`_ and `reStructuredText`_ on what you can do
with them. In particular, the Sphinx `reStructuredText Primer`_ is a good place
to get started with reStructuredText. There are also some `Sphinx specific
markup constructs`_.

.. _reStructuredText Primer: http://www.sphinx-doc.org/en/stable/rest.html
.. _Sphinx specific markup constructs: http://www.sphinx-doc.org/en/stable/markup/index.html

Specific guidelines for the kernel documentation
------------------------------------------------

Here are some specific guidelines for the kernel documentation:

* Please don't go overboard with reStructuredText markup. Keep it simple.

* Please stick to this order of heading adornments:

  1. ``=`` with overline for document title::

       ==============
       Document title
       ==============

  2. ``=`` for chapters::

       Chapters
       ========

  3. ``-`` for sections::

       Section
       -------

  4. ``~`` for subsections::

       Subsection
       ~~~~~~~~~~

  Although RST doesn't mandate a specific order ("Rather than imposing a fixed
  number and order of section title adornment styles, the order enforced will be
  the order as encountered."), having the higher levels the same overall makes
  it easier to follow the documents.


the C domain
------------

The `Sphinx C Domain`_ (name c) is suited for documentation of C API. E.g. a
function prototype:

.. code-block:: rst

    .. c:function:: int ioctl( int fd, int request )

The C domain of the kernel-doc has some additional features. E.g. you can
*rename* the reference name of a function with a common name like ``open`` or
``ioctl``:

.. code-block:: rst

     .. c:function:: int ioctl( int fd, int request )
        :name: VIDIOC_LOG_STATUS

The func-name (e.g. ioctl) remains in the output but the ref-name changed from
``ioctl`` to ``VIDIOC_LOG_STATUS``. The index entry for this function is also
changed to ``VIDIOC_LOG_STATUS`` and the function can now referenced by:

.. code-block:: rst

     :c:func:`VIDIOC_LOG_STATUS`


list tables
-----------

We recommend the use of *list table* formats. The *list table* formats are
double-stage lists. Compared to the ASCII-art they might not be as
comfortable for 
readers of the text files. Their advantage is that they are easy to
create or modify and that the diff of a modification is much more meaningful,
because it is limited to the modified content.

The ``flat-table`` is a double-stage list similar to the ``list-table`` with
some additional features:

* column-span: with the role ``cspan`` a cell can be extended through
  additional columns

* row-span: with the role ``rspan`` a cell can be extended through
  additional rows

* auto span rightmost cell of a table row over the missing cells on the right
  side of that table-row.  With Option ``:fill-cells:`` this behavior can
  changed from *auto span* to *auto fill*, which automatically inserts (empty)
  cells instead of spanning the last cell.

options:

* ``:header-rows:``   [int] count of header rows
* ``:stub-columns:``  [int] count of stub columns
* ``:widths:``        [[int] [int] ... ] widths of columns
* ``:fill-cells:``    instead of auto-spanning missing cells, insert missing cells

roles:

* ``:cspan:`` [int] additional columns (*morecols*)
* ``:rspan:`` [int] additional rows (*morerows*)

The example below shows how to use this markup.  The first level of the staged
list is the *table-row*. In the *table-row* there is only one markup allowed,
the list of the cells in this *table-row*. Exceptions are *comments* ( ``..`` )
and *targets* (e.g. a ref to ``:ref:`last row <last row>``` / :ref:`last row
<last row>`).

.. code-block:: rst

   .. flat-table:: table title
      :widths: 2 1 1 3

      * - head col 1
        - head col 2
        - head col 3
        - head col 4

      * - column 1
        - field 1.1
        - field 1.2 with autospan

      * - column 2
        - field 2.1
        - :rspan:`1` :cspan:`1` field 2.2 - 3.3

      * .. _`last row`:

        - column 3

Rendered as:

   .. flat-table:: table title
      :widths: 2 1 1 3

      * - head col 1
        - head col 2
        - head col 3
        - head col 4

      * - column 1
        - field 1.1
        - field 1.2 with autospan

      * - column 2
        - field 2.1
        - :rspan:`1` :cspan:`1` field 2.2 - 3.3

      * .. _`last row`:

        - column 3


Including kernel-doc comments
=============================

The Linux kernel source files may contain structured documentation comments, or
kernel-doc comments to describe the functions and types and design of the
code. The documentation comments may be included to any of the reStructuredText
documents using a dedicated kernel-doc Sphinx directive extension.

The kernel-doc directive is of the format::

  .. kernel-doc:: source
     :option:

The *source* is the path to a source file, relative to the kernel source
tree. The following directive options are supported:

export: *[source-pattern ...]*
  Include documentation for all functions in *source* that have been exported
  using ``EXPORT_SYMBOL`` or ``EXPORT_SYMBOL_GPL`` either in *source* or in any
  of the files specified by *source-pattern*.

  The *source-pattern* is useful when the kernel-doc comments have been placed
  in header files, while ``EXPORT_SYMBOL`` and ``EXPORT_SYMBOL_GPL`` are next to
  the function definitions.

  Examples::

    .. kernel-doc:: lib/bitmap.c
       :export:

    .. kernel-doc:: include/net/mac80211.h
       :export: net/mac80211/*.c

internal: *[source-pattern ...]*
  Include documentation for all functions and types in *source* that have
  **not** been exported using ``EXPORT_SYMBOL`` or ``EXPORT_SYMBOL_GPL`` either
  in *source* or in any of the files specified by *source-pattern*.

  Example::

    .. kernel-doc:: drivers/gpu/drm/i915/intel_audio.c
       :internal:

doc: *title*
  Include documentation for the ``DOC:`` paragraph identified by *title* in
  *source*. Spaces are allowed in *title*; do not quote the *title*. The *title*
  is only used as an identifier for the paragraph, and is not included in the
  output. Please make sure to have an appropriate heading in the enclosing
  reStructuredText document.

  Example::

    .. kernel-doc:: drivers/gpu/drm/i915/intel_audio.c
       :doc: High Definition Audio over HDMI and Display Port

functions: *function* *[...]*
  Include documentation for each *function* in *source*.

  Example::

    .. kernel-doc:: lib/bitmap.c
       :functions: bitmap_parselist bitmap_parselist_user

Without options, the kernel-doc directive includes all documentation comments
from the source file.

The kernel-doc extension is included in the kernel source tree, at
``Documentation/sphinx/kernel-doc.py``. Internally, it uses the
``scripts/kernel-doc`` script to extract the documentation comments from the
source.

Writing kernel-doc comments
===========================

In order to provide embedded, "C" friendly, easy to maintain, but consistent and
extractable overview, function and type documentation, the Linux kernel has
adopted a consistent style for documentation comments. The format for this
documentation is called the kernel-doc format, described below. This style
embeds the documentation within the source files, using a few simple conventions
for adding documentation paragraphs and documenting functions and their
parameters, structures and unions and their members, enumerations, and typedefs.

.. note:: The kernel-doc format is deceptively similar to gtk-doc or Doxygen,
   yet distinctively different, for historical reasons. The kernel source
   contains tens of thousands of kernel-doc comments. Please stick to the style
   described here.

The ``scripts/kernel-doc`` script is used by the Sphinx kernel-doc extension in
the documentation build to extract this embedded documentation into the various
HTML, PDF, and other format documents.

In order to provide good documentation of kernel functions and data structures,
please use the following conventions to format your kernel-doc comments in the
Linux kernel source.

How to format kernel-doc comments
---------------------------------

The opening comment mark ``/**`` is reserved for kernel-doc comments. Only
comments so marked will be considered by the ``kernel-doc`` tool. Use it only
for comment blocks that contain kernel-doc formatted comments. The usual ``*/``
should be used as the closing comment marker. The lines in between should be
prefixed by `` * `` (space star space).

The function and type kernel-doc comments should be placed just before the
function or type being described. The overview kernel-doc comments may be freely
placed at the top indentation level.

Example kernel-doc function comment::

  /**
   * foobar() - Brief description of foobar.
   * @arg: Description of argument of foobar.
   *
   * Longer description of foobar.
   *
   * Return: Description of return value of foobar.
   */
  int foobar(int arg)

The format is similar for documentation for structures, enums, paragraphs,
etc. See the sections below for details.

The kernel-doc structure is extracted from the comments, and proper `Sphinx C
Domain`_ function and type descriptions with anchors are generated for them. The
descriptions are filtered for special kernel-doc highlights and
cross-references. See below for details.

.. _Sphinx C Domain: http://www.sphinx-doc.org/en/stable/domains.html

Highlights and cross-references
-------------------------------

The following special patterns are recognized in the kernel-doc comment
descriptive text and converted to proper reStructuredText markup and `Sphinx C
Domain`_ references.

.. attention:: The below are **only** recognized within kernel-doc comments,
	       **not** within normal reStructuredText documents.

``funcname()``
  Function reference.

``@parameter``
  Name of a function parameter. (No cross-referencing, just formatting.)

``%CONST``
  Name of a constant. (No cross-referencing, just formatting.)

``$ENVVAR``
  Name of an environment variable. (No cross-referencing, just formatting.)

``&struct name``
  Structure reference.

``&enum name``
  Enum reference.

``&typedef name``
  Typedef reference.

``&struct_name->member`` or ``&struct_name.member``
  Structure or union member reference. The cross-reference will be to the struct
  or union definition, not the member directly.

``&name``
  A generic type reference. Prefer using the full reference described above
  instead. This is mostly for legacy comments.

Cross-referencing from reStructuredText
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To cross-reference the functions and types defined in the kernel-doc comments
from reStructuredText documents, please use the `Sphinx C Domain`_
references. For example::

  See function :c:func:`foo` and struct/union/enum/typedef :c:type:`bar`.

While the type reference works with just the type name, without the
struct/union/enum/typedef part in front, you may want to use::

  See :c:type:`struct foo <foo>`.
  See :c:type:`union bar <bar>`.
  See :c:type:`enum baz <baz>`.
  See :c:type:`typedef meh <meh>`.

This will produce prettier links, and is in line with how kernel-doc does the
cross-references.

For further details, please refer to the `Sphinx C Domain`_ documentation.

Function documentation
----------------------

The general format of a function and function-like macro kernel-doc comment is::

  /**
   * function_name() - Brief description of function.
   * @arg1: Describe the first argument.
   * @arg2: Describe the second argument.
   *        One can provide multiple line descriptions
   *        for arguments.
   *
   * A longer description, with more discussion of the function function_name()
   * that might be useful to those using or modifying it. Begins with an
   * empty comment line, and may include additional embedded empty
   * comment lines.
   *
   * The longer description may have multiple paragraphs.
   *
   * Return: Describe the return value of foobar.
   *
   * The return value description can also have multiple paragraphs, and should
   * be placed at the end of the comment block.
   */

The brief description following the function name may span multiple lines, and
ends with an ``@argument:`` description, a blank comment line, or the end of the
comment block.

The kernel-doc function comments describe each parameter to the function, in
order, with the ``@argument:`` descriptions. The ``@argument:`` descriptions
must begin on the very next line following the opening brief function
description line, with no intervening blank comment lines. The ``@argument:``
descriptions may span multiple lines. The continuation lines may contain
indentation. If a function parameter is ``...`` (varargs), it should be listed
in kernel-doc notation as: ``@...:``.

The return value, if any, should be described in a dedicated section at the end
of the comment starting with "Return:".

Structure, union, and enumeration documentation
-----------------------------------------------

The general format of a struct, union, and enum kernel-doc comment is::

  /**
   * struct struct_name - Brief description.
   * @member_name: Description of member member_name.
   *
   * Description of the structure.
   */

Below, "struct" is used to mean structs, unions and enums, and "member" is used
to mean struct and union members as well as enumerations in an enum.

The brief description following the structure name may span multiple lines, and
ends with a ``@member:`` description, a blank comment line, or the end of the
comment block.

The kernel-doc data structure comments describe each member of the structure, in
order, with the ``@member:`` descriptions. The ``@member:`` descriptions must
begin on the very next line following the opening brief function description
line, with no intervening blank comment lines. The ``@member:`` descriptions may
span multiple lines. The continuation lines may contain indentation.

In-line member documentation comments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The structure members may also be documented in-line within the definition::

  /**
   * struct foo - Brief description.
   * @foo: The Foo member.
   */
  struct foo {
        int foo;
        /**
         * @bar: The Bar member.
         */
        int bar;
        /**
         * @baz: The Baz member.
         *
         * Here, the member description may contain several paragraphs.
         */
        int baz;
  }

Private members
~~~~~~~~~~~~~~~

Inside a struct description, you can use the "private:" and "public:" comment
tags. Structure fields that are inside a "private:" area are not listed in the
generated output documentation.  The "private:" and "public:" tags must begin
immediately following a ``/*`` comment marker.  They may optionally include
comments between the ``:`` and the ending ``*/`` marker.

Example::

  /**
   * struct my_struct - short description
   * @a: first member
   * @b: second member
   *
   * Longer description
   */
  struct my_struct {
      int a;
      int b;
  /* private: internal use only */
      int c;
  };


Typedef documentation
---------------------

The general format of a typedef kernel-doc comment is::

  /**
   * typedef type_name - Brief description.
   *
   * Description of the type.
   */

Overview documentation comments
-------------------------------

To facilitate having source code and comments close together, you can include
kernel-doc documentation blocks that are free-form comments instead of being
kernel-doc for functions, structures, unions, enums, or typedefs. This could be
used for something like a theory of operation for a driver or library code, for
example.

This is done by using a ``DOC:`` section keyword with a section title.

The general format of an overview or high-level documentation comment is::

  /**
   * DOC: Theory of Operation
   *
   * The whizbang foobar is a dilly of a gizmo. It can do whatever you
   * want it to do, at any time. It reads your mind. Here's how it works.
   *
   * foo bar splat
   *
   * The only drawback to this gizmo is that is can sometimes damage
   * hardware, software, or its subject(s).
   */

The title following ``DOC:`` acts as a heading within the source file, but also
as an identifier for extracting the documentation comment. Thus, the title must
be unique within the file.

Recommendations
---------------

We definitely need kernel-doc formatted documentation for functions that are
exported to loadable modules using ``EXPORT_SYMBOL`` or ``EXPORT_SYMBOL_GPL``.

We also look to provide kernel-doc formatted documentation for functions
externally visible to other kernel files (not marked "static").

We also recommend providing kernel-doc formatted documentation for private (file
"static") routines, for consistency of kernel source code layout. But this is
lower priority and at the discretion of the MAINTAINER of that kernel source
file.

Data structures visible in kernel include files should also be documented using
kernel-doc formatted comments.

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
