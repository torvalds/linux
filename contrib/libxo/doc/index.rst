.. #
   # Copyright (c) 2014, Juniper Networks, Inc.
   # All rights reserved.
   # This SOFTWARE is licensed under the LICENSE provided in the
   # ../Copyright file. By downloading, installing, copying, or
   # using the SOFTWARE, you agree to be bound by the terms of that
   # LICENSE.
   # Phil Shafer, July 2014
   #

.. default-role:: code

libxo - A Library for Generating Text, XML, JSON, and HTML Output
===================================================================

The libxo library allows an application to generate text, XML, JSON,
and HTML output, suitable for both command line use and for web
applications.  The application decides at run time which output style
should be produced.  By using libxo, a single source code path can
emit multiple styles of output using command line options to select
the style, along with optional behaviors.  libxo includes support for
multiple output streams, pluralization, color, syslog,
:manpage:`humanized(3)` output, internationalization, and UTF-8.  The
library aims to minimize the cost of migrating code to libxo.

libxo ships as part of FreeBSD.

.. toctree::
    :maxdepth: 3
    :caption: Documentation Contents:

    intro
    getting
    formatting
    options
    format-strings
    field-roles
    field-modifiers
    field-formatting
    api
    xo
    xolint
    xohtml
    xopo
    faq
    howto
    example

Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
