
Formatting with libxo
=====================

Most unix commands emit text output aimed at humans.  It is designed
to be parsed and understood by a user.  Humans are gifted at
extracting details and pattern matching in such output.  Often
programmers need to extract information from this human-oriented
output.  Programmers use tools like grep, awk, and regular expressions
to ferret out the pieces of information they need.  Such solutions are
fragile and require maintenance when output contents change or evolve,
along with testing and validation.

Modern tool developers favor encoding schemes like XML and JSON,
which allow trivial parsing and extraction of data.  Such formats are
simple, well understood, hierarchical, easily parsed, and often
integrate easier with common tools and environments.  Changes to
content can be done in ways that do not break existing users of the
data, which can reduce maintenance costs and increase feature velocity.

In addition, modern reality means that more output ends up in web
browsers than in terminals, making HTML output valuable.

libxo allows a single set of function calls in source code to generate
traditional text output, as well as XML and JSON formatted data.  HTML
can also be generated; "<div>" elements surround the traditional text
output, with attributes that detail how to render the data.

A single libxo function call in source code is all that's required::

    xo_emit("Connecting to {:host}.{:domain}...\n", host, domain);

    TEXT:
      Connecting to my-box.example.com...
    XML:
      <host>my-box</host>
      <domain>example.com</domain>
    JSON:
      "host": "my-box",
      "domain": "example.com"
    HTML:
       <div class="line">
         <div class="text">Connecting to </div>
         <div class="data" data-tag="host"
              data-xpath="/top/host">my-box</div>
         <div class="text">.</div>
         <div class="data" data-tag="domain"
              data-xpath="/top/domain">example.com</div>
         <div class="text">...</div>
       </div>

Encoding Styles
---------------

There are four encoding styles supported by libxo:

- TEXT output can be display on a terminal session, allowing
  compatibility with traditional command line usage.
- XML output is suitable for tools like XPath and protocols like
  NETCONF.
- JSON output can be used for RESTful APIs and integration with
  languages like Javascript and Python.
- HTML can be matched with a small CSS file to permit rendering in any
  HTML5 browser.

In general, XML and JSON are suitable for encoding data, while TEXT is
suited for terminal output and HTML is suited for display in a web
browser (see :ref:`xohtml`).

Text Output
~~~~~~~~~~~

Most traditional programs generate text output on standard output,
with contents like::

    36      ./src
    40      ./bin
    90      .

In this example (taken from *du* source code), the code to generate this
data might look like::

    printf("%d\t%s\n", num_blocks, path);

Simple, direct, obvious.  But it's only making text output.  Imagine
using a single code path to make TEXT, XML, JSON or HTML, deciding at
run time which to generate.

libxo expands on the idea of printf format strings to make a single
format containing instructions for creating multiple output styles::

    xo_emit("{:blocks/%d}\t{:path/%s}\n", num_blocks, path);

This line will generate the same text output as the earlier printf
call, but also has enough information to generate XML, JSON, and HTML.

The following sections introduce the other formats.

XML Output
~~~~~~~~~~

XML output consists of a hierarchical set of elements, each encoded
with a start tag and an end tag.  The element should be named for data
value that it is encoding::

    <item>
      <blocks>36</blocks>
      <path>./src</path>
    </item>
    <item>
      <blocks>40</blocks>
      <path>./bin</path>
    </item>
    <item>
      <blocks>90</blocks>
      <path>.</path>
    </item>

`XML`_ is the W3C standard for encoding data.

.. _XML: https://w3c.org/TR/xml

JSON Output
~~~~~~~~~~~

JSON output consists of a hierarchical set of objects and lists, each
encoded with a quoted name, a colon, and a value.  If the value is a
string, it must be quoted, but numbers are not quoted.  Objects are
encoded using braces; lists are encoded using square brackets.
Data inside objects and lists is separated using commas::

    items: [
        { "blocks": 36, "path" : "./src" },
        { "blocks": 40, "path" : "./bin" },
        { "blocks": 90, "path" : "./" }
    ]

HTML Output
~~~~~~~~~~~

HTML output is designed to allow the output to be rendered in a web
browser with minimal effort.  Each piece of output data is rendered
inside a <div> element, with a class name related to the role of the
data.  By using a small set of class attribute values, a CSS
stylesheet can render the HTML into rich text that mirrors the
traditional text content.

Additional attributes can be enabled to provide more details about the
data, including data type, description, and an XPath location::

    <div class="line">
      <div class="data" data-tag="blocks">36</div>
      <div class="padding">      </div>
      <div class="data" data-tag="path">./src</div>
    </div>
    <div class="line">
      <div class="data" data-tag="blocks">40</div>
      <div class="padding">      </div>
      <div class="data" data-tag="path">./bin</div>
    </div>
    <div class="line">
      <div class="data" data-tag="blocks">90</div>
      <div class="padding">      </div>
      <div class="data" data-tag="path">./</div>
    </div>
