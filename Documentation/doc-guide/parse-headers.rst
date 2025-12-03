===========================
Including uAPI header files
===========================

Sometimes, it is useful to include header files and C example codes in
order to describe the userspace API and to generate cross-references
between the code and the documentation. Adding cross-references for
userspace API files has an additional advantage: Sphinx will generate warnings
if a symbol is not found at the documentation. That helps to keep the
uAPI documentation in sync with the Kernel changes.
The :ref:`parse_headers.py <parse_headers>` provides a way to generate such
cross-references. It has to be called via Makefile, while building the
documentation. Please see ``Documentation/userspace-api/media/Makefile`` for an example
about how to use it inside the Kernel tree.

.. _parse_headers:

tools/docs/parse_headers.py
^^^^^^^^^^^^^^^^^^^^^^^^^^^

NAME
****

parse_headers.py - parse a C file, in order to identify functions, structs,
enums and defines and create cross-references to a Sphinx book.

USAGE
*****

parse-headers.py [-h] [-d] [-t] ``FILE_IN`` ``FILE_OUT`` ``FILE_RULES``

SYNOPSIS
********

Converts a C header or source file ``FILE_IN`` into a ReStructured Text
included via ..parsed-literal block with cross-references for the
documentation files that describe the API. It accepts an optional
``FILE_RULES`` file to describe what elements will be either ignored or
be pointed to a non-default reference type/name.

The output is written at ``FILE_OUT``.

It is capable of identifying ``define``, ``struct``, ``typedef``, ``enum``
and enum ``symbol``, creating cross-references for all of them.

It is also capable of distinguishing ``#define`` used for specifying
Linux-specific macros used to define ``ioctl``.

The optional ``FILE_RULES`` contains a set of rules like::

    ignore ioctl VIDIOC_ENUM_FMT
    replace ioctl VIDIOC_DQBUF vidioc_qbuf
    replace define V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ :c:type:`v4l2_event_motion_det`

POSITIONAL ARGUMENTS
********************

  ``FILE_IN``
      Input C file

  ``FILE_OUT``
      Output RST file

  ``FILE_RULES``
      Exceptions file (optional)

OPTIONS
*******

  ``-h``, ``--help``
      show a help message and exit
  ``-d``, ``--debug``
      Increase debug level. Can be used multiple times
  ``-t``, ``--toc``
      instead of a literal block, outputs a TOC table at the RST file


DESCRIPTION
***********

Creates an enriched version of a Kernel header file with cross-links
to each C data structure type, from ``FILE_IN``, formatting it with
reStructuredText notation, either as-is or as a table of contents.

It accepts an optional ``FILE_RULES`` which describes what elements will be
either ignored or be pointed to a non-default reference, and optionally
defines the C namespace to be used.

It is meant to allow having more comprehensive documentation, where
uAPI headers will create cross-reference links to the code.

The output is written at the ``FILE_OUT``.

The ``FILE_RULES`` may contain contain three types of statements:
**ignore**, **replace** and **namespace**.

By default, it create rules for all symbols and defines, but it also
allows parsing an exception file. Such file contains a set of rules
using the syntax below:

1. Ignore rules:

    ignore *type* *symbol*

Removes the symbol from reference generation.

2. Replace rules:

    replace *type* *old_symbol* *new_reference*

    Replaces *old_symbol* with a *new_reference*.
    The *new_reference* can be:

    - A simple symbol name;
    - A full Sphinx reference.

3. Namespace rules

    namespace *namespace*

    Sets C *namespace* to be used during cross-reference generation. Can
    be overridden by replace rules.

On ignore and replace rules, *type* can be:

    - ioctl:
        for defines of the form ``_IO*``, e.g., ioctl definitions

    - define:
        for other defines

    - symbol:
        for symbols defined within enums;

    - typedef:
        for typedefs;

    - enum:
        for the name of a non-anonymous enum;

    - struct:
        for structs.


EXAMPLES
********

- Ignore a define ``_VIDEODEV2_H`` at ``FILE_IN``::

    ignore define _VIDEODEV2_H

- On an data structure like this enum::

    enum foo { BAR1, BAR2, PRIVATE };

  It won't generate cross-references for ``PRIVATE``::

    ignore symbol PRIVATE

  At the same struct, instead of creating one cross reference per symbol,
  make them all point to the ``enum foo`` C type::

    replace symbol BAR1 :c:type:\`foo\`
    replace symbol BAR2 :c:type:\`foo\`


- Use C namespace ``MC`` for all symbols at ``FILE_IN``::

    namespace MC

BUGS
****


Report bugs to Mauro Carvalho Chehab <mchehab@kernel.org>


COPYRIGHT
*********


Copyright (c) 2016, 2025 by Mauro Carvalho Chehab <mchehab+huawei@kernel.org>.

License GPLv2: GNU GPL version 2 <https://gnu.org/licenses/gpl.html>.

This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
