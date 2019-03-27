
xolint
======

`xolint` is a tool for reporting common mistakes in format strings
in source code that invokes `xo_emit`.  It allows these errors
to be diagnosed at build time, rather than waiting until runtime.

`xolint` takes the one or more C files as arguments, and reports
and errors, warning, or informational messages as needed:

============ ===================================================
 Option       Meaning
============ ===================================================
 -c           Invoke 'cpp' against the input file
 -C <flags>   Flags that are passed to 'cpp
 -d           Enable debug output
 -D           Generate documentation for all xolint messages
 -I           Generate info table code
 -p           Print the offending lines after the message
 -V           Print vocabulary of all field names
 -X           Extract samples from xolint, suitable for testing
============ ===================================================

The output message will contain the source filename and line number, the
class of the message, the message, and, if -p is given, the
line that contains the error::

    % xolint.pl -t xolint.c
    xolint.c: 16: error: anchor format should be "%d"
    16         xo_emit("{[:/%s}");

The "-I" option will generate a table of `xo_info_t`_ structures,
suitable for inclusion in source code.

.. _xo_info_t: :ref:`field-information`

The "-V" option does not report errors, but prints a complete list of
all field names, sorted alphabetically.  The output can help spot
inconsistencies and spelling errors.
