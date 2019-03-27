
FAQs
====

This section contains the set of questions that users typically ask,
along with answers that might be helpful.

General
-------

Can you share the history of libxo?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In 2001, we added an XML API to the JUNOS operating system, which is
built on top of FreeBSD_.  Eventually this API became standardized as
the NETCONF API (:RFC:`6241`).  As part of this effort, we modified many
FreeBSD utilities to emit XML, typically via a "-X" switch.  The
results were mixed.  The cost of maintaining this code, updating it,
and carrying it were non-trivial, and contributed to our expense (and
the associated delay) with upgrading the version of FreeBSD on which
each release of JUNOS is based.

.. _FreeBSD: https://www.freebsd.org

A recent (2014) effort within JUNOS aims at removing our modifications
to the underlying FreeBSD code as a means of reducing the expense and
delay in tracking HEAD.  JUNOS is structured to have system components
generate XML that is rendered by the CLI (think: login shell) into
human-readable text.  This allows the API to use the same plumbing as
the CLI, and ensures that all components emit XML, and that it is
emitted with knowledge of the consumer of that XML, yielding an API
that have no incremental cost or feature delay.

libxo is an effort to mix the best aspects of the JUNOS strategy into
FreeBSD in a seemless way, allowing commands to make printf-like
output calls with a single code path.

Did the complex semantics of format strings evolve over time?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The history is both long and short: libxo's functionality is based
on what JUNOS does in a data modeling language called ODL (output
definition language).  In JUNOS, all subcomponents generate XML,
which is feed to the CLI, where data from the ODL files tell is
how to render that XML into text.  ODL might had a set of tags
like::

     tag docsis-state {
         help "State of the DOCSIS interface";
         type string;
     }

     tag docsis-mode {
         help "DOCSIS mode (2.0/3.0) of the DOCSIS interface";
         type string;
     }

     tag docsis-upstream-speed {
         help "Operational upstream speed of the interface";
         type string;
     }

     tag downstream-scanning {
         help "Result of scanning in downstream direction";
         type string;
     }

     tag ranging {
         help "Result of ranging action";
         type string;
     }

     tag signal-to-noise-ratio {
         help "Signal to noise ratio for all channels";
         type string;
     }

     tag power {
         help "Operational power of the signal on all channels";
         type string;
     }

     format docsis-status-format {
         picture "
     State   : @, Mode: @, Upstream speed: @
     Downstream scanning: @, Ranging: @
     Signal to noise ratio: @
     Power: @
     ";
         line {
             field docsis-state;
             field docsis-mode;
             field docsis-upstream-speed;
             field downstream-scanning;
             field ranging;
             field signal-to-noise-ratio;
             field power;
         }
     }

These tag definitions are compiled into field definitions
that are triggered when matching XML elements are seen.  ODL
also supports other means of defining output.

The roles and modifiers describe these details.

In moving these ideas to bsd, two things had to happen: the
formatting had to happen at the source since BSD won't have
a JUNOS-like CLI to do the rendering, and we can't depend on
external data models like ODL, which was seen as too hard a
sell to the BSD community.

The results were that the xo_emit strings are used to encode the
roles, modifiers, names, and formats.  They are dense and a bit
cryptic, but not so unlike printf format strings that developers will
be lost.

libxo is a new implementation of these ideas and is distinct from
the previous implementation in JUNOS.

.. index:: XOF_UNDERSCORES

.. _good-field-names:

What makes a good field name?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To make useful, consistent field names, follow these guidelines:

Use lower case, even for TLAs
  Lower case is more civilized.  Even TLAs should be lower case
  to avoid scenarios where the differences between "XPath" and
  "Xpath" drive your users crazy.  Using "xpath" is simpler and better.

Use hyphens, not underscores
  Use of hyphens is traditional in XML, and the XOF_UNDERSCORES
  flag can be used to generate underscores in JSON, if desired.
  But the raw field name should use hyphens.

Use full words
  Don't abbreviate especially when the abbreviation is not obvious or
  not widely used.  Use "data-size", not "dsz" or "dsize".  Use
  "interface" instead of "ifname", "if-name", "iface", "if", or "intf".

Use <verb>-<units>
  Using the form <verb>-<units> or <verb>-<classifier>-<units> helps in
  making consistent, useful names, avoiding the situation where one app
  uses "sent-packet" and another "packets-sent" and another
  "packets-we-have-sent".  The <units> can be dropped when it is
  obvious, as can obvious words in the classification.
  Use "receive-after-window-packets" instead of
  "received-packets-of-data-after-window".

Reuse existing field names
  Nothing's worse than writing expressions like::

    if ($src1/process[pid == $pid]/name ==
        $src2/proc-table/proc-list
                   /prc-entry[prcss-id == $pid]/proc-name) {
        ...
    }

  Find someone else who is expressing similar data and follow their
  fields and hierarchy.  Remember the quote is not "Consistency is the
  hobgoblin of little minds", but "A *foolish* consistency is the
  hobgoblin of little minds".  Consistency rocks!

Use containment as scoping
  In the previous example, all the names are prefixed with "proc-",
  which is redundant given that they are nested under the process table.

Think about your users
  Have empathy for your users, choosing clear and useful fields that
  contain clear and useful data.  You may need to augment the display
  content with xo_attr() calls (:ref:`xo_attr`) or "{e:}"
  fields (:ref:`encoding-modifier`) to make the data useful.

Don't use an arbitrary number postfix
  What does "errors2" mean?  No one will know.  "errors-after-restart"
  would be a better choice.  Think of your users, and think of the
  future.  If you make "errors2", the next guy will happily make
  "errors3" and before you know it, someone will be asking what's the
  difference between errors37 and errors63.

Be consistent, uniform, unsurprising, and predictable
  Think of your field vocabulary as an API.  You want it useful,
  expressive, meaningful, direct, and obvious.  You want the client
  application's programmer to move between without the need to
  understand a variety of opinions on how fields are named.  They
  should see the system as a single cohesive whole, not a sack of
  cats.

Field names constitute the means by which client programmers interact
with our system.  By choosing wise names now, you are making their
lives better.

After using `xolint` to find errors in your field descriptors, use
"`xolint -V`" to spell check your field names and to help you detect
different names for the same data.  "dropped-short" and
"dropped-too-short" are both reasonable names, but using them both
will lead users to ask the difference between the two fields.  If
there is no difference, use only one of the field names.  If there is
a difference, change the names to make that difference more obvious.

.. ignore for now, since we want can't have generated content
  What does this message mean?
  ----------------------------

  !!include-file xolint.txt
