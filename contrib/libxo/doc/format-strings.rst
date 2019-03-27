
.. index:: Format Strings
.. _format-strings:

Format Strings
--------------

libxo uses format strings to control the rendering of data into the
various output styles.  Each format string contains a set of zero or
more field descriptions, which describe independent data fields.  Each
field description contains a set of modifiers, a content string, and
zero, one, or two format descriptors.  The modifiers tell libxo what
the field is and how to treat it, while the format descriptors are
formatting instructions using printf-style format strings, telling
libxo how to format the field.  The field description is placed inside
a set of braces, with a colon (":") after the modifiers and a slash
("/") before each format descriptors.  Text may be intermixed with
field descriptions within the format string.

The field description is given as follows::

    '{' [ role | modifier ]* [',' long-names ]* ':' [ content ]
            [ '/' field-format [ '/' encoding-format ]] '}'

The role describes the function of the field, while the modifiers
enable optional behaviors.  The contents, field-format, and
encoding-format are used in varying ways, based on the role.  These
are described in the following sections.

In the following example, three field descriptors appear.  The first
is a padding field containing three spaces of padding, the second is a
label ("In stock"), and the third is a value field ("in-stock").  The
in-stock field has a "%u" format that will parse the next argument
passed to the xo_emit function as an unsigned integer::

    xo_emit("{P:   }{Lwc:In stock}{:in-stock/%u}\n", 65);

This single line of code can generate text (" In stock: 65\n"), XML
("<in-stock>65</in-stock>"), JSON ('"in-stock": 6'), or HTML (too
lengthy to be listed here).

While roles and modifiers typically use single character for brevity,
there are alternative names for each which allow more verbose
formatting strings.  These names must be preceded by a comma, and may
follow any single-character values::

    xo_emit("{L,white,colon:In stock}{,key:in-stock/%u}\n", 65);
