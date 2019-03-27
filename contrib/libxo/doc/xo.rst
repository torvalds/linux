
.. index:: --libxo, xo

The "xo" Utility
================

The `xo` utility allows command line access to the functionality of
the libxo library.  Using `xo`, shell scripts can emit XML, JSON, and
HTML using the same commands that emit text output.

The style of output can be selected using a specific option: "-X" for
XML, "-J" for JSON, "-H" for HTML, or "-T" for TEXT, which is the
default.  The "--style <style>" option can also be used.  The standard
set of "--libxo" options are available (see :ref:`options`), as well
as the `LIBXO_OPTIONS`_ environment variable.

.. _`LIBXO_OPTIONS`: :ref:`libxo-options`

The `xo` utility accepts a format string suitable for `xo_emit` and
a set of zero or more arguments used to supply data for that string::

    xo "The {k:name} weighs {:weight/%d} pounds.\n" fish 6

  TEXT:
    The fish weighs 6 pounds.
  XML:
    <name>fish</name>
    <weight>6</weight>
  JSON:
    "name": "fish",
    "weight": 6
  HTML:
    <div class="line">
      <div class="text">The </div>
      <div class="data" data-tag="name">fish</div>
      <div class="text"> weighs </div>
      <div class="data" data-tag="weight">6</div>
      <div class="text"> pounds.</div>
    </div>

The `--wrap $path` option can be used to wrap emitted content in a
specific hierarchy.  The path is a set of hierarchical names separated
by the '/' character::

    xo --wrap top/a/b/c '{:tag}' value

  XML:
    <top>
      <a>
        <b>
          <c>
            <tag>value</tag>
          </c>
        </b>
      </a>
    </top>
  JSON:
    "top": {
      "a": {
        "b": {
          "c": {
            "tag": "value"
          }
        }
      }
    }

The `--open $path` and `--close $path` can be used to emit
hierarchical information without the matching close and open
tag.  This allows a shell script to emit open tags, data, and
then close tags.  The `--depth` option may be used to set the
depth for indentation.  The `--leading-xpath` may be used to
prepend data to the XPath values used for HTML output style::

  EXAMPLE;
    #!/bin/sh
    xo --open top/data
    xo --depth 2 '{tag}' value
    xo --close top/data
  XML:
    <top>
      <data>
        <tag>value</tag>
      </data>
    </top>
  JSON:
    "top": {
      "data": {
        "tag": "value"
      }
    }

Command Line Options
--------------------

::

  Usage: xo [options] format [fields]
    --close <path>        Close tags for the given path
    --depth <num>         Set the depth for pretty printing
    --help                Display this help text
    --html OR -H          Generate HTML output
    --json OR -J          Generate JSON output
    --leading-xpath <path> Add a prefix to generated XPaths (HTML)
    --open <path>         Open tags for the given path
    --pretty OR -p        Make 'pretty' output (add indent, newlines)
    --style <style>       Generate given style (xml, json, text, html)
    --text OR -T          Generate text output (the default style)
    --version             Display version information
    --warn OR -W          Display warnings in text on stderr
    --warn-xml            Display warnings in xml on stdout
    --wrap <path>         Wrap output in a set of containers
    --xml OR -X           Generate XML output
    --xpath               Add XPath data to HTML output);

Example
-------

::

  % xo 'The {:product} is {:status}\n' stereo "in route"
  The stereo is in route
  % ./xo/xo -p -X 'The {:product} is {:status}\n' stereo "in route"
  <product>stereo</product>
  <status>in route</status>
