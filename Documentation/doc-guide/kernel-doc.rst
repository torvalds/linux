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

.. _kernel_doc:

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

The structure members may also be documented in-line within the definition.
There are two styles, single-line comments where both the opening ``/**`` and
closing ``*/`` are on the same line, and multi-line comments where they are each
on a line of their own, like all other kernel-doc comments::

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
        /** @foobar: Single line description. */
        int foobar;
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
