
.. index:: Field Roles
.. _field-roles:

Field Roles
~~~~~~~~~~~

Field roles are optional, and indicate the role and formatting of the
content.  The roles are listed below; only one role is permitted:

=== ============== =================================================
R   Name           Description
=== ============== =================================================
C   color          Field has color and effect controls
D   decoration     Field is non-text (e.g., colon, comma)
E   error          Field is an error message
G   gettext        Call gettext(3) on the format string
L   label          Field is text that prefixes a value
N   note           Field is text that follows a value
P   padding        Field is spaces needed for vertical alignment
T   title          Field is a title value for headings
U   units          Field is the units for the previous value field
V   value          Field is the name of field (the default)
W   warning        Field is a warning message
[   start-anchor   Begin a section of anchored variable-width text
]   stop-anchor    End a section of anchored variable-width text
=== ============== =================================================

    EXAMPLE:
        xo_emit("{L:Free}{D::}{P:   }{:free/%u} {U:Blocks}\n",
                free_blocks);

When a role is not provided, the "*value*" role is used as the default.

Roles and modifiers can also use more verbose names, when preceded by
a comma::

    EXAMPLE:
        xo_emit("{,label:Free}{,decoration::}{,padding:   }"
                "{,value:free/%u} {,units:Blocks}\n",
                free_blocks);

.. index:: Field Roles; Color
.. _color-role:

The Color Role ({C:})
+++++++++++++++++++++

Colors and effects control how text values are displayed; they are
used for display styles (TEXT and HTML)::

    xo_emit("{C:bold}{:value}{C:no-bold}\n", value);

Colors and effects remain in effect until modified by other "C"-role
fields::

    xo_emit("{C:bold}{C:inverse}both{C:no-bold}only inverse\n");

If the content is empty, the "*reset*" action is performed::

    xo_emit("{C:both,underline}{:value}{C:}\n", value);

The content should be a comma-separated list of zero or more colors or
display effects::

    xo_emit("{C:bold,inverse}Ugly{C:no-bold,no-inverse}\n");

The color content can be either static, when placed directly within
the field descriptor, or a printf-style format descriptor can be used,
if preceded by a slash ("/"):

   xo_emit("{C:/%s%s}{:value}{C:}", need_bold ? "bold" : "",
           need_underline ? "underline" : "", value);

Color names are prefixed with either "fg-" or "bg-" to change the
foreground and background colors, respectively::

    xo_emit("{C:/fg-%s,bg-%s}{Lwc:Cost}{:cost/%u}{C:reset}\n",
            fg_color, bg_color, cost);

The following table lists the supported effects:

=============== =================================================
 Name           Description
=============== =================================================
 bg-XXXXX       Change background color
 bold           Start bold text effect
 fg-XXXXX       Change foreground color
 inverse        Start inverse (aka reverse) text effect
 no-bold        Stop bold text effect
 no-inverse     Stop inverse (aka reverse) text effect
 no-underline   Stop underline text effect
 normal         Reset effects (only)
 reset          Reset colors and effects (restore defaults)
 underline      Start underline text effect
=============== =================================================

The following color names are supported:

========= ============================================
 Name      Description
========= ============================================
 black
 blue
 cyan
 default   Default color for foreground or background
 green
 magenta
 red
 white
 yellow
========= ============================================

When using colors, the developer should remember that users will
change the foreground and background colors of terminal session
according to their own tastes, so assuming that "blue" looks nice is
never safe, and is a constant annoyance to your dear author.  In
addition, a significant percentage of users (1 in 12) will be color
blind.  Depending on color to convey critical information is not a
good idea.  Color should enhance output, but should not be used as the
sole means of encoding information.

.. index:: Field Roles; Decoration
.. _decoration-role:

The Decoration Role ({D:})
++++++++++++++++++++++++++

Decorations are typically punctuation marks such as colons,
semi-colons, and commas used to decorate the text and make it simpler
for human readers.  By marking these distinctly, HTML usage scenarios
can use CSS to direct their display parameters::

    xo_emit("{D:((}{:name}{D:))}\n", name);

.. index:: Field Roles; Gettext
.. _gettext-role:

The Gettext Role ({G:})
+++++++++++++++++++++++

libxo supports internationalization (i18n) through its use of
gettext(3).  Use the "{G:}" role to request that the remaining part of
the format string, following the "{G:}" field, be handled using
gettext().

Since gettext() uses the string as the key into the message catalog,
libxo uses a simplified version of the format string that removes
unimportant field formatting and modifiers, stopping minor formatting
changes from impacting the expensive translation process.  A developer
change such as changing "/%06d" to "/%08d" should not force hand
inspection of all .po files.

The simplified version can be generated for a single message using the
"`xopo -s $text`" command, or an entire .pot can be translated using
the "`xopo -f $input -o $output`" command.

   xo_emit("{G:}Invalid token\n");

The {G:} role allows a domain name to be set.  gettext calls will
continue to use that domain name until the current format string
processing is complete, enabling a library function to emit strings
using it's own catalog.  The domain name can be either static as the
content of the field, or a format can be used to get the domain name
from the arguments.

   xo_emit("{G:libc}Service unavailable in restricted mode\n");

See :ref:`i18n` for additional details.

.. index:: Field Roles; Label
.. _label-role:

The Label Role ({L:})
+++++++++++++++++++++

Labels are text that appears before a value::

    xo_emit("{Lwc:Cost}{:cost/%u}\n", cost);

.. index:: Field Roles; Note
.. _note-role:

The Note Role ({N:})
++++++++++++++++++++

Notes are text that appears after a value::

    xo_emit("{:cost/%u} {N:per year}\n", cost);

.. index:: Field Roles; Padding
.. _padding-role:

The Padding Role ({P:})
+++++++++++++++++++++++

Padding represents whitespace used before and between fields.

The padding content can be either static, when placed directly within
the field descriptor, or a printf-style format descriptor can be used,
if preceded by a slash ("/")::

    xo_emit("{P:        }{Lwc:Cost}{:cost/%u}\n", cost);
    xo_emit("{P:/%30s}{Lwc:Cost}{:cost/%u}\n", "", cost);

.. index:: Field Roles; Title
.. _title-role:

The Title Role ({T:})
+++++++++++++++++++++

Title are heading or column headers that are meant to be displayed to
the user.  The title can be either static, when placed directly within
the field descriptor, or a printf-style format descriptor can be used,
if preceded by a slash ("/")::

    xo_emit("{T:Interface Statistics}\n");
    xo_emit("{T:/%20.20s}{T:/%6.6s}\n", "Item Name", "Cost");

Title fields have an extra convenience feature; if both content and
format are specified, instead of looking to the argument list for a
value, the content is used, allowing a mixture of format and content
within the field descriptor::

    xo_emit("{T:Name/%20s}{T:Count/%6s}\n");

Since the incoming argument is a string, the format must be "%s" or
something suitable.

.. index:: Field Roles; Units
.. index:: XOF_UNITS
.. _units-role:

The Units Role ({U:})
+++++++++++++++++++++

Units are the dimension by which values are measured, such as degrees,
miles, bytes, and decibels.  The units field carries this information
for the previous value field::

    xo_emit("{Lwc:Distance}{:distance/%u}{Uw:miles}\n", miles);

Note that the sense of the 'w' modifier is reversed for units;
a blank is added before the contents, rather than after it.

When the XOF_UNITS flag is set, units are rendered in XML as the
"units" attribute::

    <distance units="miles">50</distance>

Units can also be rendered in HTML as the "data-units" attribute::

    <div class="data" data-tag="distance" data-units="miles"
         data-xpath="/top/data/distance">50</div>

.. index:: Field Roles; Value
.. _value-role:

The Value Role ({V:} and {:})
+++++++++++++++++++++++++++++

The value role is used to represent the a data value that is
interesting for the non-display output styles (XML and JSON).  Value
is the default role; if no other role designation is given, the field
is a value.  The field name must appear within the field descriptor,
followed by one or two format descriptors.  The first format
descriptor is used for display styles (TEXT and HTML), while the
second one is used for encoding styles (XML and JSON).  If no second
format is given, the encoding format defaults to the first format,
with any minimum width removed.  If no first format is given, both
format descriptors default to "%s"::

    xo_emit("{:length/%02u}x{:width/%02u}x{:height/%02u}\n",
            length, width, height);
    xo_emit("{:author} wrote \"{:poem}\" in {:year/%4d}\n,
            author, poem, year);

.. index:: Field Roles; Anchor
.. _anchor-role:

The Anchor Roles ({[:} and {]:})
++++++++++++++++++++++++++++++++

The anchor roles allow a set of strings by be padded as a group,
but still be visible to xo_emit as distinct fields.  Either the start
or stop anchor can give a field width and it can be either directly in
the descriptor or passed as an argument.  Any fields between the start
and stop anchor are padded to meet the minimum width given.

To give a width directly, encode it as the content of the anchor tag::

    xo_emit("({[:10}{:min/%d}/{:max/%d}{]:})\n", min, max);

To pass a width as an argument, use "%d" as the format, which must
appear after the "/".  Note that only "%d" is supported for widths.
Using any other value could ruin your day::

    xo_emit("({[:/%d}{:min/%d}/{:max/%d}{]:})\n", width, min, max);

If the width is negative, padding will be added on the right, suitable
for left justification.  Otherwise the padding will be added to the
left of the fields between the start and stop anchors, suitable for
right justification.  If the width is zero, nothing happens.  If the
number of columns of output between the start and stop anchors is less
than the absolute value of the given width, nothing happens.

.. index:: XOF_WARN

Widths over 8k are considered probable errors and not supported.  If
XOF_WARN is set, a warning will be generated.
