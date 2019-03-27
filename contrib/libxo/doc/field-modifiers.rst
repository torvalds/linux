
.. index:: Field Modifiers
.. _field-modifiers:

Field Modifiers
~~~~~~~~~~~~~~~

Field modifiers are flags which modify the way content emitted for
particular output styles:

=== =============== ===================================================
 M   Name            Description
=== =============== ===================================================
 a   argument        The content appears as a 'const char \*' argument
 c   colon           A colon (":") is appended after the label
 d   display         Only emit field for display styles (text/HTML)
 e   encoding        Only emit for encoding styles (XML/JSON)
 g   gettext         Call gettext on field's render content
 h   humanize (hn)   Format large numbers in human-readable style
\    hn-space        Humanize: Place space between numeric and unit
\    hn-decimal      Humanize: Add a decimal digit, if number < 10
\    hn-1000         Humanize: Use 1000 as divisor instead of 1024
 k   key             Field is a key, suitable for XPath predicates
 l   leaf-list       Field is a leaf-list
 n   no-quotes       Do not quote the field when using JSON style
 p   plural          Gettext: Use comma-separated plural form
 q   quotes          Quote the field when using JSON style
 t   trim            Trim leading and trailing whitespace
 w   white           A blank (" ") is appended after the label
=== =============== ===================================================

Roles and modifiers can also use more verbose names, when preceded by
a comma.  For example, the modifier string "Lwc" (or "L,white,colon")
means the field has a label role (text that describes the next field)
and should be followed by a colon ('c') and a space ('w').  The
modifier string "Vkq" (or ":key,quote") means the field has a value
role (the default role), that it is a key for the current instance,
and that the value should be quoted when encoded for JSON.

.. index:: Field Modifiers; Argument
.. _argument-modifier:

The Argument Modifier ({a:})
++++++++++++++++++++++++++++

.. index:: Field Modifiers; Argument

The argument modifier indicates that the content of the field
descriptor will be placed as a UTF-8 string (const char \*) argument
within the xo_emit parameters::

    EXAMPLE:
        xo_emit("{La:} {a:}\n", "Label text", "label", "value");
    TEXT:
        Label text value
    JSON:
        "label": "value"
    XML:
        <label>value</label>

The argument modifier allows field names for value fields to be passed
on the stack, avoiding the need to build a field descriptor using
snprintf.  For many field roles, the argument modifier is not needed,
since those roles have specific mechanisms for arguments, such as
"{C:fg-%s}".

.. index:: Field Modifiers; Colon
.. _colon-modifier:

The Colon Modifier ({c:})
+++++++++++++++++++++++++

.. index:: Field Modifiers; Colon

The colon modifier appends a single colon to the data value::

    EXAMPLE:
        xo_emit("{Lc:Name}{:name}\n", "phil");
    TEXT:
        Name:phil

The colon modifier is only used for the TEXT and HTML output
styles. It is commonly combined with the space modifier ('{w:}').
It is purely a convenience feature.

.. index:: Field Modifiers; Display
.. _display-modifier:

The Display Modifier ({d:})
+++++++++++++++++++++++++++

.. index:: Field Modifiers; Display

The display modifier indicated the field should only be generated for
the display output styles, TEXT and HTML::

    EXAMPLE:
        xo_emit("{Lcw:Name}{d:name} {:id/%d}\n", "phil", 1);
    TEXT:
        Name: phil 1
    XML:
        <id>1</id>

The display modifier is the opposite of the encoding modifier, and
they are often used to give to distinct views of the underlying data.

.. index:: Field Modifiers; Encoding
.. _encoding-modifier:

The Encoding Modifier ({e:})
++++++++++++++++++++++++++++

.. index:: Field Modifiers; Encoding

The display modifier indicated the field should only be generated for
the display output styles, TEXT and HTML::

    EXAMPLE:
        xo_emit("{Lcw:Name}{:name} {e:id/%d}\n", "phil", 1);
    TEXT:
        Name: phil
    XML:
        <name>phil</name><id>1</id>

The encoding modifier is the opposite of the display modifier, and
they are often used to give to distinct views of the underlying data.

.. index:: Field Modifiers; Gettext
.. _gettext-modifier:

The Gettext Modifier ({g:})
+++++++++++++++++++++++++++

.. index:: Field Modifiers; Gettext
.. index:: gettext

The gettext modifier is used to translate individual fields using the
gettext domain (typically set using the "`{G:}`" role) and current
language settings.  Once libxo renders the field value, it is passed
to gettext(3), where it is used as a key to find the native language
translation.

In the following example, the strings "State" and "full" are passed
to gettext() to find locale-based translated strings::

    xo_emit("{Lgwc:State}{g:state}\n", "full");

See :ref:`gettext-role`, :ref:`plural-modifier`, and
:ref:`i18n` for additional details.

.. index:: Field Modifiers; Humanize
.. _humanize-modifier:

The Humanize Modifier ({h:})
++++++++++++++++++++++++++++

.. index:: Field Modifiers; Humanize

The humanize modifier is used to render large numbers as in a
human-readable format.  While numbers like "44470272" are completely
readable to computers and savants, humans will generally find "44M"
more meaningful.

"hn" can be used as an alias for "humanize".

The humanize modifier only affects display styles (TEXT and HMTL).
The "`no-humanize`" option (See :ref:`options`) will block
the function of the humanize modifier.

There are a number of modifiers that affect details of humanization.
These are only available in as full names, not single characters.  The
"`hn-space`" modifier places a space between the number and any
multiplier symbol, such as "M" or "K" (ex: "44 K").  The
"`hn-decimal`" modifier will add a decimal point and a single tenths
digit when the number is less than 10 (ex: "4.4K").  The "`hn-1000`"
modifier will use 1000 as divisor instead of 1024, following the
JEDEC-standard instead of the more natural binary powers-of-two
tradition::

    EXAMPLE:
        xo_emit("{h:input/%u}, {h,hn-space:output/%u}, "
	    "{h,hn-decimal:errors/%u}, {h,hn-1000:capacity/%u}, "
	    "{h,hn-decimal:remaining/%u}\n",
            input, output, errors, capacity, remaining);
    TEXT:
        21, 57 K, 96M, 44M, 1.2G

In the HTML style, the original numeric value is rendered in the
"data-number" attribute on the <div> element::

    <div class="data" data-tag="errors"
         data-number="100663296">96M</div>

.. index:: Field Modifiers; Key
.. _key-modifier:

The Key Modifier ({k:})
+++++++++++++++++++++++

.. index:: Field Modifiers; Key

The key modifier is used to indicate that a particular field helps
uniquely identify an instance of list data::

    EXAMPLE:
        xo_open_list("user");
        for (i = 0; i < num_users; i++) {
	    xo_open_instance("user");
            xo_emit("User {k:name} has {:count} tickets\n",
               user[i].u_name, user[i].u_tickets);
            xo_close_instance("user");
        }
        xo_close_list("user");

.. index:: XOF_XPATH

Currently the key modifier is only used when generating XPath value
for the HTML output style when XOF_XPATH is set, but other uses are
likely in the near future.

.. index:: Field Modifiers; Leaf-List
.. _leaf-list:

The Leaf-List Modifier ({l:})
+++++++++++++++++++++++++++++

.. index:: Field Modifiers; Leaf-List

The leaf-list modifier is used to distinguish lists where each
instance consists of only a single value.  In XML, these are
rendered as single elements, where JSON renders them as arrays::

    EXAMPLE:
        for (i = 0; i < num_users; i++) {
            xo_emit("Member {l:user}\n", user[i].u_name);
        }
    XML:
        <user>phil</user>
        <user>pallavi</user>
    JSON:
        "user": [ "phil", "pallavi" ]

The name of the field must match the name of the leaf list.

.. index:: Field Modifiers; No-Quotes
.. _no-quotes-modifier:

The No-Quotes Modifier ({n:})
+++++++++++++++++++++++++++++

.. index:: Field Modifiers; No-Quotes

The no-quotes modifier (and its twin, the 'quotes' modifier) affect
the quoting of values in the JSON output style.  JSON uses quotes for
string value, but no quotes for numeric, boolean, and null data.
xo_emit applies a simple heuristic to determine whether quotes are
needed, but often this needs to be controlled by the caller::

    EXAMPLE:
        const char *bool = is_true ? "true" : "false";
        xo_emit("{n:fancy/%s}", bool);
    JSON:
        "fancy": true

.. index:: Field Modifiers; Plural
.. _plural-modifier:

The Plural Modifier ({p:})
++++++++++++++++++++++++++

.. index:: Field Modifiers; Plural
.. index:: gettext

The plural modifier selects the appropriate plural form of an
expression based on the most recent number emitted and the current
language settings.  The contents of the field should be the singular
and plural English values, separated by a comma::

    xo_emit("{:bytes} {Ngp:byte,bytes}\n", bytes);

The plural modifier is meant to work with the gettext modifier ({g:})
but can work independently.  See :ref:`gettext-modifier`.

When used without the gettext modifier or when the message does not
appear in the message catalog, the first token is chosen when the last
numeric value is equal to 1; otherwise the second value is used,
mimicking the simple pluralization rules of English.

When used with the gettext modifier, the ngettext(3) function is
called to handle the heavy lifting, using the message catalog to
convert the singular and plural forms into the native language.

.. index:: Field Modifiers; Quotes
.. _quotes-modifier:

The Quotes Modifier ({q:})
++++++++++++++++++++++++++

.. index:: Field Modifiers; Quotes

The quotes modifier (and its twin, the 'no-quotes' modifier) affect
the quoting of values in the JSON output style.  JSON uses quotes for
string value, but no quotes for numeric, boolean, and null data.
xo_emit applies a simple heuristic to determine whether quotes are
needed, but often this needs to be controlled by the caller::

    EXAMPLE:
        xo_emit("{q:time/%d}", 2014);
    JSON:
        "year": "2014"

The heuristic is based on the format; if the format uses any of the
following conversion specifiers, then no quotes are used::

    d i o u x X D O U e E f F g G a A c C p

.. index:: Field Modifiers; Trim
.. _trim-modifier:

The Trim Modifier ({t:})
++++++++++++++++++++++++

.. index:: Field Modifiers; Trim

The trim modifier removes any leading or trailing whitespace from
the value::

    EXAMPLE:
        xo_emit("{t:description}", "   some  input   ");
    JSON:
        "description": "some input"

.. index:: Field Modifiers; White Space
.. _white-space-modifier:

The White Space Modifier ({w:})
+++++++++++++++++++++++++++++++

.. index:: Field Modifiers; White Space

The white space modifier appends a single space to the data value::

    EXAMPLE:
        xo_emit("{Lw:Name}{:name}\n", "phil");
    TEXT:
        Name phil

The white space modifier is only used for the TEXT and HTML output
styles. It is commonly combined with the colon modifier ('{c:}').
It is purely a convenience feature.

Note that the sense of the 'w' modifier is reversed for the units role
({Uw:}); a blank is added before the contents, rather than after it.
