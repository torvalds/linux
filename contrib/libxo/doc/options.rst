
.. index:: --libxo
.. index:: Options

.. _options:

Command-line Arguments
======================

libxo uses command line options to trigger rendering behavior.  There
are multiple conventions for passing options, all using the
"`--libxo`" option::

  --libxo <options>
  --libxo=<options>
  --libxo:<brief-options>

The *brief-options* is a series of single letter abbrevations, where
the *options* is a comma-separated list of words.  Both provide access
to identical functionality.  The following invocations are all
identical in outcome::

  my-app --libxo warn,pretty arg1
  my-app --libxo=warn,pretty arg1
  my-app --libxo:WP arg1

Programs using libxo are expecting to call the xo_parse_args function
to parse these arguments.  See :ref:`xo_parse_args` for details.

Option Keywords
---------------

Options is a comma-separated list of tokens that correspond to output
styles, flags, or features:

=============== =======================================================
Token           Action
=============== =======================================================
color           Enable colors/effects for display styles (TEXT, HTML)
colors=xxxx     Adjust color output values
dtrt            Enable "Do The Right Thing" mode
flush           Flush after every libxo function call
flush-line      Flush after every line (line-buffered)
html            Emit HTML output
indent=xx       Set the indentation level
info            Add info attributes (HTML)
json            Emit JSON output
keys            Emit the key attribute for keys (XML)
log-gettext     Log (via stderr) each gettext(3) string lookup
log-syslog      Log (via stderr) each syslog message (via xo_syslog)
no-humanize     Ignore the {h:} modifier (TEXT, HTML)
no-locale       Do not initialize the locale setting
no-retain       Prevent retaining formatting information
no-top          Do not emit a top set of braces (JSON)
not-first       Pretend the 1st output item was not 1st (JSON)
pretty          Emit pretty-printed output
retain          Force retaining formatting information
text            Emit TEXT output
underscores     Replace XML-friendly "-"s with JSON friendly "_"s
units           Add the 'units' (XML) or 'data-units (HTML) attribute
warn            Emit warnings when libxo detects bad calls
warn-xml        Emit warnings in XML
xml             Emit XML output
xpath           Add XPath expressions (HTML)
=============== =======================================================

Most of these option are simple and direct, but some require
additional details:

- "colors" is described in :ref:`color-mapping`.
- "flush-line" performs line buffering, even when the output is not
  directed to a TTY device.
- "info" generates additional data for HTML, encoded in attributes
  using names that state with "data-".
- "keys" adds a "key" attribute for XML output to indicate that a leaf
  is an identifier for the list member.
- "no-humanize" avoids "humanizing" numeric output (see
  :ref:`humanize-modifier` for details).
- "no-locale" instructs libxo to avoid translating output to the
  current locale.
- "no-retain" disables the ability of libxo to internally retain
  "compiled" information about formatting strings (see :ref:`retain`
  for details).
- "underscores" can be used with JSON output to change XML-friendly
  names with dashes into JSON-friendly name with underscores.
- "warn" allows libxo to emit warnings on stderr when application code
  make incorrect calls.
- "warn-xml" causes those warnings to be placed in XML inside the
  output.

Brief Options
-------------

The brief options are simple single-letter aliases to the normal
keywords, as detailed below:

======== =============================================
 Option   Action
======== =============================================
 c        Enable color/effects for TEXT/HTML
 F        Force line-buffered flushing
 H        Enable HTML output (XO_STYLE_HTML)
 I        Enable info output (XOF_INFO)
 i<num>   Indent by <number>
 J        Enable JSON output (XO_STYLE_JSON)
 k        Add keys to XPATH expressions in HTML
 n        Disable humanization (TEXT, HTML)
 P        Enable pretty-printed output (XOF_PRETTY)
 T        Enable text output (XO_STYLE_TEXT)
 U        Add units to HTML output
 u        Change "-"s to "_"s in element names (JSON)
 W        Enable warnings (XOF_WARN)
 X        Enable XML output (XO_STYLE_XML)
 x        Enable XPath data (XOF_XPATH)
======== =============================================

.. index:: Colors

.. _color-mapping:

Color Mapping
-------------

The "colors" option takes a value that is a set of mappings from the
pre-defined set of colors to new foreground and background colors.
The value is a series of "fg/bg" values, separated by a "+".  Each
pair of "fg/bg" values gives the colors to which a basic color is
mapped when used as a foreground or background color.  The order is
the mappings is:

- black
- red
- green
- yellow
- blue
- magenta
- cyan
- white

Pairs may be skipped, leaving them mapped as normal, as are missing
pairs or single colors.

For example consider the following xo_emit call::

    xo_emit("{C:fg-red,bg-green}Merry XMas!!{C:}\n");

To turn all colored output to red-on-blue, use eight pairs of
"red/blue" mappings separated by "+"s::

    --libxo colors=red/blue+red/blue+red/blue+red/blue+\
                   red/blue+red/blue+red/blue+red/blue

To turn the red-on-green text to magenta-on-cyan, give a "magenta"
foreground value for red (the second mapping) and a "cyan" background
to green (the third mapping)::

    --libxo colors=+magenta+/cyan

Consider the common situation where blue output looks unreadable on a
terminal session with a black background.  To turn both "blue"
foreground and background output to "yellow", give only the fifth
mapping, skipping the first four mappings with bare "+"s::

    --libxo colors=++++yellow/yellow
