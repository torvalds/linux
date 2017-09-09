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
(``sphinx_rtd_theme``) is used if available. For PDF output you'll also need
``XeLaTeX`` and ``convert(1)`` from ImageMagick (https://www.imagemagick.org).
All of these are widely available and packaged in distributions.

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

* Please don't go overboard with reStructuredText markup. Keep it
  simple. For the most part the documentation should be plain text with
  just enough consistency in formatting that it can be converted to
  other formats.

* Please keep the formatting changes minimal when converting existing
  documentation to reStructuredText.

* Also update the content, not just the formatting, when converting
  documentation.

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

* For inserting fixed width text blocks (for code examples, use case
  examples, etc.), use ``::`` for anything that doesn't really benefit
  from syntax highlighting, especially short snippets. Use
  ``.. code-block:: <language>`` for longer code blocks that benefit
  from highlighting.


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


Figures & Images
================

If you want to add an image, you should use the ``kernel-figure`` and
``kernel-image`` directives. E.g. to insert a figure with a scalable
image format use SVG (:ref:`svg_image_example`)::

    .. kernel-figure::  svg_image.svg
       :alt:    simple SVG image

       SVG image example

.. _svg_image_example:

.. kernel-figure::  svg_image.svg
   :alt:    simple SVG image

   SVG image example

The kernel figure (and image) directive support **DOT** formated files, see

* DOT: http://graphviz.org/pdf/dotguide.pdf
* Graphviz: http://www.graphviz.org/content/dot-language

A simple example (:ref:`hello_dot_file`)::

  .. kernel-figure::  hello.dot
     :alt:    hello world

     DOT's hello world example

.. _hello_dot_file:

.. kernel-figure::  hello.dot
   :alt:    hello world

   DOT's hello world example

Embed *render* markups (or languages) like Graphviz's **DOT** is provided by the
``kernel-render`` directives.::

  .. kernel-render:: DOT
     :alt: foobar digraph
     :caption: Embedded **DOT** (Graphviz) code

     digraph foo {
      "bar" -> "baz";
     }

How this will be rendered depends on the installed tools. If Graphviz is
installed, you will see an vector image. If not the raw markup is inserted as
*literal-block* (:ref:`hello_dot_render`).

.. _hello_dot_render:

.. kernel-render:: DOT
   :alt: foobar digraph
   :caption: Embedded **DOT** (Graphviz) code

   digraph foo {
      "bar" -> "baz";
   }

The *render* directive has all the options known from the *figure* directive,
plus option ``caption``.  If ``caption`` has a value, a *figure* node is
inserted. If not, a *image* node is inserted. A ``caption`` is also needed, if
you want to refer it (:ref:`hello_svg_render`).

Embedded **SVG**::

  .. kernel-render:: SVG
     :caption: Embedded **SVG** markup
     :alt: so-nw-arrow

     <?xml version="1.0" encoding="UTF-8"?>
     <svg xmlns="http://www.w3.org/2000/svg" version="1.1" ...>
        ...
     </svg>

.. _hello_svg_render:

.. kernel-render:: SVG
   :caption: Embedded **SVG** markup
   :alt: so-nw-arrow

   <?xml version="1.0" encoding="UTF-8"?>
   <svg xmlns="http://www.w3.org/2000/svg"
     version="1.1" baseProfile="full" width="70px" height="40px" viewBox="0 0 700 400">
   <line x1="180" y1="370" x2="500" y2="50" stroke="black" stroke-width="15px"/>
   <polygon points="585 0 525 25 585 50" transform="rotate(135 525 25)"/>
   </svg>
