
Introducing libxo
=================

The libxo library allows an application to generate text, XML, JSON,
and HTML output using a common set of function calls.  The application
decides at run time which output style should be produced.  The
application calls a function "xo_emit" to product output that is
described in a format string.  A "field descriptor" tells libxo what
the field is and what it means.  Each field descriptor is placed in
braces with printf-like :ref:`format-strings`::

    xo_emit(" {:lines/%7ju} {:words/%7ju} "
            "{:characters/%7ju} {d:filename/%s}\n",
            linect, wordct, charct, file);

Each field can have a role, with the 'value' role being the default,
and the role tells libxo how and when to render that field (see
:ref:`field-roles` for details).  Modifiers change how the field is
rendered in different output styles (see :ref:`field-modifiers` for
details.  Output can then be generated in various style, using the
"--libxo" option::

    % wc /etc/motd
          25     165    1140 /etc/motd
    % wc --libxo xml,pretty,warn /etc/motd
    <wc>
      <file>
        <lines>25</lines>
        <words>165</words>
        <characters>1140</characters>
        <filename>/etc/motd</filename>
      </file>
    </wc>
    % wc --libxo json,pretty,warn /etc/motd
    {
      "wc": {
        "file": [
          {
            "lines": 25,
            "words": 165,
            "characters": 1140,
            "filename": "/etc/motd"
          }
        ]
      }
    }
    % wc --libxo html,pretty,warn /etc/motd
    <div class="line">
      <div class="text"> </div>
      <div class="data" data-tag="lines">     25</div>
      <div class="text"> </div>
      <div class="data" data-tag="words">    165</div>
      <div class="text"> </div>
      <div class="data" data-tag="characters">   1140</div>
      <div class="text"> </div>
      <div class="data" data-tag="filename">/etc/motd</div>
    </div>

Same code path, same format strings, same information, but it's
rendered in distinct styles based on run-time flags.

.. admonition:: Tale of Two Code Paths

  You want to prepare for the future, but you need to live in the
  present.  You'd love a flying car, but need to get work done today.
  You want to support features like XML, JSON, and HTML rendering to
  allow integration with NETCONF, REST, and web browsers, but you need
  to make text output for command line users.

  And you don't want multiple code paths that can't help but get out
  of sync::

      /* None of this "if (xml) {... } else {...}"  logic */
      if (xml) {
          /* some code to make xml */
      } else {
          /* other code to make text */
          /* oops! forgot to add something on both clauses! */
      }

      /* And ifdefs are right out. */
      #ifdef MAKE_XML
          /* icky */
      #else
          /* pooh */
      #endif

  But you'd really, really like all the fancy features that modern
  encoding formats can provide.  libxo can help.
