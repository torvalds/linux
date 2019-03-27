
Howtos: Focused Directions
==========================

This section provides task-oriented instructions for selected tasks.
If you have a task that needs instructions, please open a request as
an enhancement issue on github.

Howto: Report bugs
------------------

libxo uses github to track bugs or request enhancements.  Please use
the following URL:

  https://github.com/Juniper/libxo/issues

Howto: Install libxo
--------------------

libxo is open source, under a new BSD license.  Source code is
available on github, as are recent releases.  To get the most
current release, please visit:

  https://github.com/Juniper/libxo/releases

After downloading and untarring the source code, building involves the
following steps::

    sh bin/setup.sh
    cd build
    ../configure
    make
    make test
    sudo make install

libxo uses a distinct "*build*" directory to keep generated files
separated from source files.

.. index:: configure

Use "`../configure --help`" to display available configuration
options, which include the following::

  --enable-warnings      Turn on compiler warnings
  --enable-debug         Turn on debugging
  --enable-text-only     Turn on text-only rendering
  --enable-printflike    Enable use of GCC __printflike attribute
  --disable-libxo-options  Turn off support for LIBXO_OPTIONS
  --with-gettext=PFX     Specify location of gettext installation
  --with-libslax-prefix=PFX  Specify location of libslax config

Compiler warnings are a very good thing, but recent compiler version
have added some very pedantic checks.  While every attempt is made to
keep libxo code warning-free, warnings are now optional.  If you are
doing development work on libxo, it is required that you
use --enable-warnings to keep the code warning free, but most users
need not use this option.

.. index:: --enable-text-only

libxo provides the `--enable-text-only` option to reduce the
footprint of the library for smaller installations.  XML, JSON, and
HTML rendering logic is removed.

.. index:: --with-gettext

The gettext library does not provide a simple means of learning its
location, but libxo will look for it in /usr and /opt/local.  If
installed elsewhere, the installer will need to provide this
information using the "`--with-gettext=/dir/path`" option.

.. index:: libslax

libslax is not required by libxo; it contains the "oxtradoc" program
used to format documentation.

For additional information, see :ref:`building`.

Howto: Convert command line applications
----------------------------------------

Common question: How do I convert an existing command line application?

There are four basic steps for converting command line application to
use libxo::

- Setting up the context
- Converting printf calls
- Creating hierarchy
- Converting error functions

Setting up the context
~~~~~~~~~~~~~~~~~~~~~~

To use libxo, you'll need to include the "xo.h" header file in your
source code files::

    #include <libxo/xo.h>

In your main() function, you'll need to call xo_parse_args to handling
argument parsing (:ref:`xo_parse_args`).  This function removes
libxo-specific arguments the program's argv and returns either the
number of remaining arguments or -1 to indicate an error::

    int
    main (int argc, char **argv)
    {
        argc = xo_parse_args(argc, argv);
        if (argc < 0)
            return argc;
        ....
    }

.. index:: atexit
.. index:: xo_finish_atexit

At the bottom of your main(), you'll need to call xo_finish() to
complete output processing for the default handle (:ref:`handles`).  This
is required to flush internal information buffers.  libxo provides the
xo_finish_atexit function that is suitable for use with the
:manpage:`atexit(3)` function::

    atexit(xo_finish_atexit);

Converting printf Calls
~~~~~~~~~~~~~~~~~~~~~~~

The second task is inspecting code for :manpage:`printf(3)` calls and
replacing them with xo_emit() calls.  The format strings are similar
in task, but libxo format strings wrap output fields in braces.  The
following two calls produce identical text output::

  OLD::
    printf("There are %d %s events\n", count, etype);

  NEW::
    xo_emit("There are {:count/%d} {:event} events\n", count, etype);

"count" and "event" are used as names for JSON and XML output.  The
"count" field uses the format "%d" and "event" uses the default "%s"
format.  Both are "value" roles, which is the default role.

Since text outside of output fields is passed verbatim, other roles
are less important, but their proper use can help make output more
useful.  The "note" and "label" roles allow HTML output to recognize
the relationship between text and the associated values, allowing
appropriate "hover" and "onclick" behavior.  Using the "units" role
allows the presentation layer to perform conversions when needed.  The
"warning" and "error" roles allows use of color and font to draw
attention to warnings.  The "padding" role makes the use of vital
whitespace more clear (:ref:`padding-role`).

The "*title*" role indicates the headings of table and sections.  This
allows HTML output to use CSS to make this relationship more obvious::

  OLD::
    printf("Statistics:\n");

  NEW::
    xo_emit("{T:Statistics}:\n");

The "*color*" roles controls foreground and background colors, as well
as effects like bold and underline (see :ref:`color-role`)::

  NEW::
    xo_emit("{C:bold}required{C:}\n");

Finally, the start- and stop-anchor roles allow justification and
padding over multiple fields (see :ref:`anchor-role`)::

  OLD::
    snprintf(buf, sizeof(buf), "(%u/%u/%u)", min, ave, max);
    printf("%30s", buf);

  NEW::
    xo_emit("{[:30}({:minimum/%u}/{:average/%u}/{:maximum/%u}{]:}",
            min, ave, max);

Creating Hierarchy
~~~~~~~~~~~~~~~~~~

Text output doesn't have any sort of hierarchy, but XML and JSON
require this.  Typically applications use indentation to represent
these relationship::

  OLD::
    printf("table %d\n", tnum);
    for (i = 0; i < tmax; i++) {
        printf("    %s %d\n", table[i].name, table[i].size);
    }

  NEW::
    xo_emit("{T:/table %d}\n", tnum);
    xo_open_list("table");
    for (i = 0; i < tmax; i++) {
        xo_open_instance("table");
        xo_emit("{P:    }{k:name} {:size/%d}\n",
                table[i].name, table[i].size);
        xo_close_instance("table");
    }
    xo_close_list("table");

The open and close list functions are used before and after the list,
and the open and close instance functions are used before and after
each instance with in the list.

Typically these developer looks for a "for" loop as an indication of
where to put these calls.

In addition, the open and close container functions allow for
organization levels of hierarchy::

  OLD::
    printf("Paging information:\n");
    printf("    Free:      %lu\n", free);
    printf("    Active:    %lu\n", active);
    printf("    Inactive:  %lu\n", inactive);

  NEW::
    xo_open_container("paging-information");
    xo_emit("{P:    }{L:Free:      }{:free/%lu}\n", free);
    xo_emit("{P:    }{L:Active:    }{:active/%lu}\n", active);
    xo_emit("{P:    }{L:Inactive:  }{:inactive/%lu}\n", inactive);
    xo_close_container("paging-information");

Converting Error Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~

libxo provides variants of the standard error and warning functions,
:manpage:`err(3)` and :manpage:`warn(3)`.  There are two variants, one
for putting the errors on standard error, and the other writes the
errors and warnings to the handle using the appropriate encoding
style::

  OLD::
    err(1, "cannot open output file: %s", file);

  NEW::
    xo_err(1, "cannot open output file: %s", file);
    xo_emit_err(1, "cannot open output file: {:filename}", file);

.. index:: xo_finish

Call xo_finish
~~~~~~~~~~~~~~

One important item: call `xo_finish` at the end of your program so
ensure that all buffered data is written out.  You can call it
explicitly call it, or use :manpage:`atexit(3)` to have
`xo_finish_atexit` called implicitly on exit::

  OLD::
    exit(0);

  NEW::
    xo_finish();
    exit(0);

Howto: Use "xo" in Shell Scripts
--------------------------------

.. admonition:: Needed

  Documentation is needed for this area.

.. index:: Internationalization (i18n)
.. index:: gettext
.. index:: xopo

.. _i18n:

Howto: Internationalization (i18n)
-----------------------------------------------

    How do I use libxo to support internationalization?

libxo allows format and field strings to be used a keys into message
catalogs to enable translation into a user's native language by
invoking the standard :manpage:`gettext(3)` functions.

gettext setup is a bit complicated: text strings are extracted from
source files into "*portable object template*" (.pot) files using the
`xgettext` command.  For each language, this template file is used as
the source for a message catalog in the "*portable object*" (.po)
format, which are translated by hand and compiled into "*machine
object*" (.mo) files using the `msgfmt` command.  The .mo files are
then typically installed in the /usr/share/locale or
/opt/local/share/locale directories.  At run time, the user's language
settings are used to select a .mo file which is searched for matching
messages.  Text strings in the source code are used as keys to look up
the native language strings in the .mo file.

Since the xo_emit format string is used as the key into the message
catalog, libxo removes unimportant field formatting and modifiers from
the format string before use so that minor formatting changes will not
impact the expensive translation process.  We don't want a developer
change such as changing "/%06d" to "/%08d" to force hand inspection of
all .po files.  The simplified version can be generated for a single
message using the `xopo -s $text` command, or an entire .pot can be
translated using the `xopo -f $input -o $output` command::

    EXAMPLE:
        % xopo -s "There are {:count/%u} {:event/%.6s} events\n"
        There are {:count} {:event} events\n

    Recommended workflow:
        # Extract text messages
	xgettext --default-domain=foo --no-wrap \
	    --add-comments --keyword=xo_emit --keyword=xo_emit_h \
	    --keyword=xo_emit_warn -C -E -n --foreign-user \
	    -o foo.pot.raw foo.c

        # Simplify format strings for libxo
        xopo -f foo.pot.raw -o foo.pot

        # For a new language, just copy the file
        cp foo.pot po/LC/my_lang/foo.po

        # For an existing language:
        msgmerge --no-wrap po/LC/my_lang/foo.po \
                foo.pot -o po/LC/my_lang/foo.po.new

        # Now the hard part: translate foo.po using tools
        # like poedit or emacs' po-mode

        # Compile the finished file; Use of msgfmt's "-v" option is
        # strongly encouraged, so that "fuzzy" entries are reported.
        msgfmt -v -o po/my_lang/LC_MESSAGES/foo.mo po/my_lang/foo.po

        # Install the .mo file
        sudo cp po/my_lang/LC_MESSAGES/foo.mo \
                /opt/local/share/locale/my_lang/LC_MESSAGE/

Once these steps are complete, you can use the `gettext` command to
test the message catalog::

    gettext -d foo -e "some text"

i18n and xo_emit
~~~~~~~~~~~~~~~~

There are three features used in libxo used to support i18n:

- The "{G:}" role looks for a translation of the format string.
- The "{g:}" modifier looks for a translation of the field.
- The "{p:}" modifier looks for a pluralized version of the field.

Together these three flags allows a single function call to give
native language support, as well as libxo's normal XML, JSON, and HTML
support::

    printf(gettext("Received %zu %s from {g:server} server\n"),
           counter, ngettext("byte", "bytes", counter),
           gettext("web"));

    xo_emit("{G:}Received {:received/%zu} {Ngp:byte,bytes} "
            "from {g:server} server\n", counter, "web");

libxo will see the "{G:}" role and will first simplify the format
string, removing field formats and modifiers::

    "Received {:received} {N:byte,bytes} from {:server} server\n"

libxo calls :manpage:`gettext(3)` with that string to get a localized
version.  If your language were *Pig Latin*, the result might look
like::

    "Eceivedray {:received} {N:byte,bytes} omfray "
               "{:server} erversay\n"

Note the field names do not change and they should not be translated.
The contents of the note ("byte,bytes") should also not be translated,
since the "g" modifier will need the untranslated value as the key for
the message catalog.

The field "{g:server}" requests the rendered value of the field be
translated using :manpage:`gettext(3)`.  In this example, "web" would
be used.

The field "{Ngp:byte,bytes}" shows an example of plural form using the
"{p:}" modifier with the "{g:}" modifier.  The base singular and plural
forms appear inside the field, separated by a comma.  At run time,
libxo uses the previous field's numeric value to decide which form to
use by calling :manpage:`ngettext(3)`.

If a domain name is needed, it can be supplied as the content of the
{G:} role.  Domain names remain in use throughout the format string
until cleared with another domain name::

    printf(dgettext("dns", "Host %s not found: %d(%s)\n"),
        name, errno, dgettext("strerror", strerror(errno)));

    xo_emit("{G:dns}Host {:hostname} not found: "
            "%d({G:strerror}{g:%m})\n", name, errno);
