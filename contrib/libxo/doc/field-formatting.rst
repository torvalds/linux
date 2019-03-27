
.. index:: Field Formatting

Field Formatting
----------------

The field format is similar to the format string for printf(3).  Its
use varies based on the role of the field, but generally is used to
format the field's contents.

If the format string is not provided for a value field, it defaults to
"%s".

Note a field definition can contain zero or more printf-style
'directives', which are sequences that start with a '%' and end with
one of following characters: "diouxXDOUeEfFgGaAcCsSp".  Each directive
is matched by one of more arguments to the xo_emit function.

The format string has the form::

  '%' format-modifier * format-character

The format-modifier can be:

- a '#' character, indicating the output value should be prefixed
  with '0x', typically to indicate a base 16 (hex) value.
- a minus sign ('-'), indicating the output value should be padded on
  the right instead of the left.
- a leading zero ('0') indicating the output value should be padded on the
  left with zeroes instead of spaces (' ').
- one or more digits ('0' - '9') indicating the minimum width of the
  argument.  If the width in columns of the output value is less than
  the minimum width, the value will be padded to reach the minimum.
- a period followed by one or more digits indicating the maximum
  number of bytes which will be examined for a string argument, or the maximum
  width for a non-string argument.  When handling ASCII strings this
  functions as the field width but for multi-byte characters, a single
  character may be composed of multiple bytes.
  xo_emit will never dereference memory beyond the given number of bytes.
- a second period followed by one or more digits indicating the maximum
  width for a string argument.  This modifier cannot be given for non-string
  arguments.
- one or more 'h' characters, indicating shorter input data.
- one or more 'l' characters, indicating longer input data.
- a 'z' character, indicating a 'size_t' argument.
- a 't' character, indicating a 'ptrdiff_t' argument.
- a ' ' character, indicating a space should be emitted before
  positive numbers.
- a '+' character, indicating sign should emitted before any number.

Note that 'q', 'D', 'O', and 'U' are considered deprecated and will be
removed eventually.

The format character is described in the following table:

===== ================= ======================
 Ltr   Argument Type     Format
===== ================= ======================
 d     int               base 10 (decimal)
 i     int               base 10 (decimal)
 o     int               base 8 (octal)
 u     unsigned          base 10 (decimal)
 x     unsigned          base 16 (hex)
 X     unsigned long     base 16 (hex)
 D     long              base 10 (decimal)
 O     unsigned long     base 8 (octal)
 U     unsigned long     base 10 (decimal)
 e     double            [-]d.ddde+-dd
 E     double            [-]d.dddE+-dd
 f     double            [-]ddd.ddd
 F     double            [-]ddd.ddd
 g     double            as 'e' or 'f'
 G     double            as 'E' or 'F'
 a     double            [-]0xh.hhhp[+-]d
 A     double            [-]0Xh.hhhp[+-]d
 c     unsigned char     a character
 C     wint_t            a character
 s     char \*           a UTF-8 string
 S     wchar_t \*        a unicode/WCS string
 p     void \*           '%#lx'
===== ================= ======================

The 'h' and 'l' modifiers affect the size and treatment of the
argument:

===== ============= ====================
 Mod   d, i          o, u, x, X
===== ============= ====================
 hh    signed char   unsigned char
 h     short         unsigned short
 l     long          unsigned long
 ll    long long     unsigned long long
 j     intmax_t      uintmax_t
 t     ptrdiff_t     ptrdiff_t
 z     size_t        size_t
 q     quad_t        u_quad_t
===== ============= ====================

.. index:: UTF-8
.. index:: Locale

.. _utf-8:

UTF-8 and Locale Strings
~~~~~~~~~~~~~~~~~~~~~~~~

For strings, the 'h' and 'l' modifiers affect the interpretation of
the bytes pointed to argument.  The default '%s' string is a 'char \*'
pointer to a string encoded as UTF-8.  Since UTF-8 is compatible with
ASCII data, a normal 7-bit ASCII string can be used.  '%ls' expects a
'wchar_t \*' pointer to a wide-character string, encoded as a 32-bit
Unicode values.  '%hs' expects a 'char \*' pointer to a multi-byte
string encoded with the current locale, as given by the LC_CTYPE,
LANG, or LC_ALL environment varibles.  The first of this list of
variables is used and if none of the variables are set, the locale
defaults to "UTF-8".

libxo will convert these arguments as needed to either UTF-8 (for XML,
JSON, and HTML styles) or locale-based strings for display in text
style::

   xo_emit("All strings are utf-8 content {:tag/%ls}",
           L"except for wide strings");

======== ================== ===============================
 Format   Argument Type      Argument Contents
======== ================== ===============================
 %s       const char \*      UTF-8 string
 %S       const char \*      UTF-8 string (alias for '%ls')
 %ls      const wchar_t \*   Wide character UNICODE string
 %hs      const char *       locale-based string
======== ================== ===============================

.. admonition:: "Long", not "locale"

  The "*l*" in "%ls" is for "*long*", following the convention of "%ld".
  It is not "*locale*", a common mis-mnemonic.  "%S" is equivalent to
  "%ls".

For example, the following function is passed a locale-base name, a
hat size, and a time value.  The hat size is formatted in a UTF-8
(ASCII) string, and the time value is formatted into a wchar_t
string::

    void print_order (const char *name, int size,
                      struct tm *timep) {
        char buf[32];
        const char *size_val = "unknown";

	if (size > 0)
            snprintf(buf, sizeof(buf), "%d", size);
            size_val = buf;
        }

        wchar_t when[32];
        wcsftime(when, sizeof(when), L"%d%b%y", timep);

        xo_emit("The hat for {:name/%hs} is {:size/%s}.\n",
                name, size_val);
        xo_emit("It was ordered on {:order-time/%ls}.\n",
                when);
    }

It is important to note that xo_emit will perform the conversion
required to make appropriate output.  Text style output uses the
current locale (as described above), while XML, JSON, and HTML use
UTF-8.

UTF-8 and locale-encoded strings can use multiple bytes to encode one
column of data.  The traditional "precision'" (aka "max-width") value
for "%s" printf formatting becomes overloaded since it specifies both
the number of bytes that can be safely referenced and the maximum
number of columns to emit.  xo_emit uses the precision as the former,
and adds a third value for specifying the maximum number of columns.

In this example, the name field is printed with a minimum of 3 columns
and a maximum of 6.  Up to ten bytes of data at the location given by
'name' are in used in filling those columns::

    xo_emit("{:name/%3.10.6s}", name);

Characters Outside of Field Definitions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Characters in the format string that are not part of a field
definition are copied to the output for the TEXT style, and are
ignored for the JSON and XML styles.  For HTML, these characters are
placed in a <div> with class "text"::

  EXAMPLE:
      xo_emit("The hat is {:size/%s}.\n", size_val);
  TEXT:
      The hat is extra small.
  XML:
      <size>extra small</size>
  JSON:
      "size": "extra small"
  HTML:
      <div class="text">The hat is </div>
      <div class="data" data-tag="size">extra small</div>
      <div class="text">.</div>

.. index:: errno

"%m" Is Supported
~~~~~~~~~~~~~~~~~

libxo supports the '%m' directive, which formats the error message
associated with the current value of "errno".  It is the equivalent
of "%s" with the argument strerror(errno)::

    xo_emit("{:filename} cannot be opened: {:error/%m}", filename);
    xo_emit("{:filename} cannot be opened: {:error/%s}",
            filename, strerror(errno));

"%n" Is Not Supported
~~~~~~~~~~~~~~~~~~~~~

libxo does not support the '%n' directive.  It's a bad idea and we
just don't do it.

The Encoding Format (eformat)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The "eformat" string is the format string used when encoding the field
for JSON and XML.  If not provided, it defaults to the primary format
with any minimum width removed.  If the primary is not given, both
default to "%s".

Content Strings
~~~~~~~~~~~~~~~

For padding and labels, the content string is considered the content,
unless a format is given.

.. index:: printf-like

Argument Validation
~~~~~~~~~~~~~~~~~~~

Many compilers and tool chains support validation of printf-like
arguments.  When the format string fails to match the argument list,
a warning is generated.  This is a valuable feature and while the
formatting strings for libxo differ considerably from printf, many of
these checks can still provide build-time protection against bugs.

libxo provide variants of functions that provide this ability, if the
"--enable-printflike" option is passed to the "configure" script.
These functions use the "_p" suffix, like "xo_emit_p()",
xo_emit_hp()", etc.

The following are features of libxo formatting strings that are
incompatible with printf-like testing:

- implicit formats, where "{:tag}" has an implicit "%s";
- the "max" parameter for strings, where "{:tag/%4.10.6s}" means up to
  ten bytes of data can be inspected to fill a minimum of 4 columns and
  a maximum of 6;
- percent signs in strings, where "{:filled}%" makes a single,
  trailing percent sign;
- the "l" and "h" modifiers for strings, where "{:tag/%hs}" means
  locale-based string and "{:tag/%ls}" means a wide character string;
- distinct encoding formats, where "{:tag/#%s/%s}" means the display
  styles (text and HTML) will use "#%s" where other styles use "%s";

If none of these features are in use by your code, then using the "_p"
variants might be wise:

================== ========================
 Function           printf-like Equivalent
================== ========================
 xo_emit_hv         xo_emit_hvp
 xo_emit_h          xo_emit_hp
 xo_emit            xo_emit_p
 xo_emit_warn_hcv   xo_emit_warn_hcvp
 xo_emit_warn_hc    xo_emit_warn_hcp
 xo_emit_warn_c     xo_emit_warn_cp
 xo_emit_warn       xo_emit_warn_p
 xo_emit_warnx      xo_emit_warnx_p
 xo_emit_err        xo_emit_err_p
 xo_emit_errx       xo_emit_errx_p
 xo_emit_errc       xo_emit_errc_p
================== ========================

.. index:: performance
.. index:: XOEF_RETAIN

.. _retain:

Retaining Parsed Format Information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

libxo can retain the parsed internal information related to the given
format string, allowing subsequent xo_emit calls, the retained
information is used, avoiding repetitive parsing of the format string::

    SYNTAX:
      int xo_emit_f(xo_emit_flags_t flags, const char fmt, ...);
    EXAMPLE:
      xo_emit_f(XOEF_RETAIN, "{:some/%02d}{:thing/%-6s}{:fancy}\n",
                     some, thing, fancy);

To retain parsed format information, use the XOEF_RETAIN flag to the
xo_emit_f() function.  A complete set of xo_emit_f functions exist to
match all the xo_emit function signatures (with handles, varadic
argument, and printf-like flags):

================== ========================
 Function           Flags Equivalent
================== ========================
 xo_emit_hv         xo_emit_hvf
 xo_emit_h          xo_emit_hf
 xo_emit            xo_emit_f
 xo_emit_hvp        xo_emit_hvfp
 xo_emit_hp         xo_emit_hfp
 xo_emit_p          xo_emit_fp
================== ========================

The format string must be immutable across multiple calls to xo_emit_f(),
since the library retains the string.  Typically this is done by using
static constant strings, such as string literals. If the string is not
immutable, the XOEF_RETAIN flag must not be used.

The functions xo_retain_clear() and xo_retain_clear_all() release
internal information on either a single format string or all format
strings, respectively.  Neither is required, but the library will
retain this information until it is cleared or the process exits::

    const char *fmt = "{:name}  {:count/%d}\n";
    for (i = 0; i < 1000; i++) {
        xo_open_instance("item");
        xo_emit_f(XOEF_RETAIN, fmt, name[i], count[i]);
    }
    xo_retain_clear(fmt);

The retained information is kept as thread-specific data.

Example
~~~~~~~

In this example, the value for the number of items in stock is emitted::

        xo_emit("{P:   }{Lwc:In stock}{:in-stock/%u}\n",
                instock);

This call will generate the following output::

  TEXT:
       In stock: 144
  XML:
      <in-stock>144</in-stock>
  JSON:
      "in-stock": 144,
  HTML:
      <div class="line">
        <div class="padding">   </div>
        <div class="label">In stock</div>
        <div class="decoration">:</div>
        <div class="padding"> </div>
        <div class="data" data-tag="in-stock">144</div>
      </div>

Clearly HTML wins the verbosity award, and this output does
not include XOF_XPATH or XOF_INFO data, which would expand the
penultimate line to::

       <div class="data" data-tag="in-stock"
          data-xpath="/top/data/item/in-stock"
          data-type="number"
          data-help="Number of items in stock">144</div>
