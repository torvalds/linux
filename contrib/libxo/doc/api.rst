.. index: API

The libxo API
=============

This section gives details about the functions in libxo, how to call
them, and the actions they perform.

.. index:: Handles
.. _handles:

Handles
-------

libxo uses "handles" to control its rendering functionality.  The
handle contains state and buffered data, as well as callback functions
to process data.

Handles give an abstraction for libxo that encapsulates the state of a
stream of output.  Handles have the data type "`xo_handle_t`" and are
opaque to the caller.

The library has a default handle that is automatically initialized.
By default, this handle will send text style output (`XO_STYLE_TEXT`) to
standard output.  The xo_set_style and xo_set_flags functions can be
used to change this behavior.

For the typical command that is generating output on standard output,
there is no need to create an explicit handle, but they are available
when needed, e.g., for daemons that generate multiple streams of
output.

Many libxo functions take a handle as their first parameter; most that
do not use the default handle.  Any function taking a handle can be
passed NULL to access the default handle.  For the convenience of
callers, the libxo library includes handle-less functions that
implicitly use the default handle.

For example, the following are equivalent::

    xo_emit("test");
    xo_emit_h(NULL, "test");

Handles are created using `xo_create` and destroy using
`xo_destroy`. 

.. index:: xo_create

xo_create
~~~~~~~~~

.. c:function:: xo_handle_t *xo_create (xo_style_t style, xo_xof_flags_t flags)

  The `xo_create` function allocates a new handle which can be passed
  to further libxo function calls.  The `xo_handle_t` structure is
  opaque.

  :param xo_style_t style: Output style (XO_STYLE\_*)
  :param xo_xof_flags_t flags: Flags for this handle (XOF\_*)
  :return: New libxo handle
  :rtype: xo_handle_t \*

  ::

    EXAMPLE:
        xo_handle_t *xop = xo_create(XO_STYLE_JSON, XOF_WARN | XOF_PRETTY);
        ....
        xo_emit_h(xop, "testing\n");

  See also :ref:`output-styles` and :ref:`flags`.

.. index:: xo_create_to_file
.. index:: XOF_CLOSE_FP

xo_create_to_file
~~~~~~~~~~~~~~~~~

.. c:function::
  xo_handle_t *xo_create_to_file (FILE *fp, unsigned style, unsigned flags)

  The `xo_create_to_file` function is aconvenience function is
  provided for situations when output should be written to a different
  file, rather than the default of standard output.

  The `XOF_CLOSE_FP` flag can be set on the returned handle to trigger a
  call to fclose() for the FILE pointer when the handle is destroyed,
  avoiding the need for the caller to perform this task.

  :param fp: FILE to use as base for this handle
  :type fp: FILE *
  :param xo_style_t style: Output style (XO_STYLE\_*)
  :param xo_xof_flags_t flags: Flags for this handle (XOF\_*)
  :return: New libxo handle
  :rtype: xo_handle_t \*

.. index:: xo_set_writer
.. index:: xo_write_func_t
.. index:: xo_close_func_t
.. index:: xo_flush_func_t

xo_set_writer
~~~~~~~~~~~~~

.. c:function::
  void xo_set_writer (xo_handle_t *xop, void *opaque, \
  xo_write_func_t write_func, xo_close_func_t close_func, \
  xo_flush_func_t flush_func)

  The `xo_set_writer` function allows custom functions which can
  tailor how libxo writes data.  The `opaque` argument is recorded and
  passed back to the functions, allowing the function to acquire
  context information. The *write_func* function writes data to the
  output stream.  The *close_func* function can release this opaque
  data and any other resources as needed.  The *flush_func* function
  is called to flush buffered data associated with the opaque object.

  :param xop: Handle to modify (or NULL for default handle)
  :type xop: xo_handle_t *
  :param opaque: Pointer to opaque data passed to the given functions
  :type opaque: void *
  :param xo_write_func_t write_func: New write function
  :param xo_close_func_t close_func: New close function
  :param xo_flush_func_t flush_func: New flush function
  :returns: void

.. index:: xo_get_style

xo_get_style
~~~~~~~~~~~~

.. c:function:: xo_style_t xo_get_style(xo_handle_t *xop)

  Use the `xo_get_style` function to find the current output style for
  a given handle.  To use the default handle, pass a `NULL` handle.

  :param xop: Handle to interrogate (or NULL for default handle)
  :type xop: xo_handle_t *
  :returns: Output style (XO_STYLE\_*)
  :rtype: xo_style_t

  ::

    EXAMPLE::
        style = xo_get_style(NULL);

.. index::  XO_STYLE_TEXT
.. index::  XO_STYLE_XML
.. index::  XO_STYLE_JSON
.. index::  XO_STYLE_HTML

.. _output-styles:

Output Styles (XO_STYLE\_\*)
++++++++++++++++++++++++++++

The libxo functions accept a set of output styles:

=============== =========================
 Flag            Description
=============== =========================
 XO_STYLE_TEXT   Traditional text output
 XO_STYLE_XML    XML encoded data
 XO_STYLE_JSON   JSON encoded data
 XO_STYLE_HTML   HTML encoded data
=============== =========================

The "XML", "JSON", and "HTML" output styles all use the UTF-8
character encoding.  "TEXT" using locale-based encoding.

.. index:: xo_set_style

xo_set_style
~~~~~~~~~~~~

.. c:function:: void xo_set_style(xo_handle_t *xop, xo_style_t style)

  The `xo_set_style` function is used to change the output style
  setting for a handle.  To use the default handle, pass a `NULL`
  handle.

  :param xop: Handle to modify
  :type xop: xo_handle_t *
  :param xo_style_t style: Output style (XO_STYLE\_*)
  :returns: void

  ::

    EXAMPLE:
        xo_set_style(NULL, XO_STYLE_XML);

.. index:: xo_set_style_name

xo_set_style_name
~~~~~~~~~~~~~~~~~

.. c:function:: int xo_set_style_name (xo_handle_t *xop, const char *style)

  The `xo_set_style_name` function can be used to set the style based
  on a name encoded as a string: The name can be any of the supported
  styles: "text", "xml", "json", or "html".

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :param style: Text name of the style
  :type style: const char \*
  :returns: zero for success, non-zero for error
  :rtype: int

  ::

    EXAMPLE:
        xo_set_style_name(NULL, "html");

.. index:: xo_set_flags

xo_set_flags
~~~~~~~~~~~~

.. c:function:: void xo_set_flags(xo_handle_t *xop, xo_xof_flags_t flags)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :param xo_xof_flags_t flags: Flags to add for the handle
  :returns: void

  Use the `xo_set_flags` function to turn on flags for a given libxo
  handle.  To use the default handle, pass a `NULL` handle.

  ::

    EXAMPLE:
        xo_set_flags(NULL, XOF_PRETTY | XOF_WARN);

.. index:: Flags; XOF_*
.. index:: XOF_CLOSE_FP
.. index:: XOF_COLOR
.. index:: XOF_COLOR_ALLOWED
.. index:: XOF_DTRT
.. index:: XOF_INFO
.. index:: XOF_KEYS
.. index:: XOF_NO_ENV
.. index:: XOF_NO_HUMANIZE
.. index:: XOF_PRETTY
.. index:: XOF_UNDERSCORES
.. index:: XOF_UNITS
.. index:: XOF_WARN
.. index:: XOF_WARN_XML
.. index:: XOF_XPATH
.. index:: XOF_COLUMNS
.. index:: XOF_FLUSH

.. _flags:

Flags (XOF\_\*)
+++++++++++++++

The set of valid flags include:

=================== =========================================
 Flag                Description
=================== =========================================
 XOF_CLOSE_FP        Close file pointer on `xo_destroy`
 XOF_COLOR           Enable color and effects in output
 XOF_COLOR_ALLOWED   Allow color/effect for terminal output
 XOF_DTRT            Enable "do the right thing" mode
 XOF_INFO            Display info data attributes (HTML)
 XOF_KEYS            Emit the key attribute (XML)
 XOF_NO_ENV          Do not use the :ref:`libxo-options` env var
 XOF_NO_HUMANIZE     Display humanization (TEXT, HTML)
 XOF_PRETTY          Make "pretty printed" output
 XOF_UNDERSCORES     Replaces hyphens with underscores
 XOF_UNITS           Display units (XML, HMTL)
 XOF_WARN            Generate warnings for broken calls
 XOF_WARN_XML        Generate warnings in XML on stdout
 XOF_XPATH           Emit XPath expressions (HTML)
 XOF_COLUMNS         Force xo_emit to return columns used
 XOF_FLUSH           Flush output after each `xo_emit` call
=================== =========================================

The `XOF_CLOSE_FP` flag will trigger the call of the *close_func*
(provided via `xo_set_writer`) when the handle is destroyed.

The `XOF_COLOR` flag enables color and effects in output regardless
of output device, while the `XOF_COLOR_ALLOWED` flag allows color
and effects only if the output device is a terminal.

The `XOF_PRETTY` flag requests "pretty printing", which will trigger
the addition of indentation and newlines to enhance the readability of
XML, JSON, and HTML output.  Text output is not affected.

The `XOF_WARN` flag requests that warnings will trigger diagnostic
output (on standard error) when the library notices errors during
operations, or with arguments to functions.  Without warnings enabled,
such conditions are ignored.

Warnings allow developers to debug their interaction with libxo.
The function `xo_failure` can used as a breakpoint for a debugger,
regardless of whether warnings are enabled.

If the style is `XO_STYLE_HTML`, the following additional flags can be
used:

=============== =========================================
 Flag            Description
=============== =========================================
 XOF_XPATH       Emit "data-xpath" attributes
 XOF_INFO        Emit additional info fields
=============== =========================================

The `XOF_XPATH` flag enables the emission of XPath expressions detailing
the hierarchy of XML elements used to encode the data field, if the
XPATH style of output were requested.

The `XOF_INFO` flag encodes additional informational fields for HTML
output.  See :ref:`field-information` for details.

If the style is `XO_STYLE_XML`, the following additional flags can be
used:

=============== =========================================
 Flag            Description
=============== =========================================
 XOF_KEYS        Flag "key" fields for XML
=============== =========================================

The `XOF_KEYS` flag adds "key" attribute to the XML encoding for
field definitions that use the "k" modifier.  The key attribute has
the value "key"::

    xo_emit("{k:name}", item);

  XML:
      <name key="key">truck</name>

.. index:: xo_clear_flags

xo_clear_flags
++++++++++++++

.. c:function:: void xo_clear_flags (xo_handle_t *xop, xo_xof_flags_t flags)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :param xo_xof_flags_t flags: Flags to clear for the handle
  :returns: void

  Use the `xo_clear_flags` function to turn off the given flags in a
  specific handle.  To use the default handle, pass a `NULL` handle.

.. index:: xo_set_options

xo_set_options
++++++++++++++

.. c:function:: int xo_set_options (xo_handle_t *xop, const char *input)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :param input: string containing options to set
  :type input: const char *
  :returns: zero for success, non-zero for error
  :rtype: int

  The `xo_set_options` function accepts a comma-separated list of
  output styles and modifier flags and enables them for a specific
  handle.  The options are identical to those listed in
  :ref:`options`.  To use the default handle, pass a `NULL` handle.

.. index:: xo_destroy

xo_destroy
++++++++++

.. c:function:: void xo_destroy(xo_handle_t *xop)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :returns: void  

  The `xo_destroy` function releases a handle and any resources it is
  using.  Calling `xo_destroy` with a `NULL` handle will release any
  resources associated with the default handle.

.. index:: xo_emit

Emitting Content (xo_emit)
--------------------------

The functions in this section are used to emit output.

The "fmt" argument is a string containing field descriptors as
specified in :ref:`format-strings`.  The use of a handle is optional and
`NULL` can be passed to access the internal "default" handle.  See
:ref:`handles`.

The remaining arguments to `xo_emit` and `xo_emit_h` are a set of
arguments corresponding to the fields in the format string.  Care must
be taken to ensure the argument types match the fields in the format
string, since an inappropriate cast can ruin your day.  The vap
argument to `xo_emit_hv` points to a variable argument list that can
be used to retrieve arguments via `va_arg`.

.. c:function:: int xo_emit (const char *fmt, ...)

  :param fmt: The format string, followed by zero or more arguments
  :returns: If XOF_COLUMNS is set, the number of columns used; otherwise the number of bytes emitted
  :rtype: int

.. c:function:: int xo_emit_h (xo_handle_t *xop, const char *fmt, ...)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :param fmt: The format string, followed by zero or more arguments
  :returns: If XOF_COLUMNS is set, the number of columns used; otherwise the number of bytes emitted
  :rtype: int

.. c:function:: int xo_emit_hv (xo_handle_t *xop, const char *fmt, va_list vap)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :param fmt: The format string
  :param va_list vap: A set of variadic arguments
  :returns: If XOF_COLUMNS is set, the number of columns used; otherwise the number of bytes emitted
  :rtype: int

.. index:: xo_emit_field

Single Field Emitting Functions (xo_emit_field)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The functions in this section can also make output, but only make a
single field at a time.  These functions are intended to avoid the
scenario where one would otherwise need to compose a format
descriptors using `snprintf`.  The individual parts of the format
descriptor are passed in distinctly.

.. c:function:: int xo_emit_field (const char *rolmod, const char *contents, const char *fmt, const char *efmt, ...)

  :param rolmod: A comma-separated list of field roles and field modifiers
  :type rolmod: const char *
  :param contents: The "contents" portion of the field description string
  :type contents: const char *
  :param fmt: Content format string
  :type fmt: const char *
  :param efmt: Encoding format string, followed by additional arguments
  :type efmt: const char *
  :returns: If XOF_COLUMNS is set, the number of columns used; otherwise the number of bytes emitted
  :rtype: int

  ::

    EXAMPLE::
        xo_emit_field("T", "Host name is ", NULL, NULL);
        xo_emit_field("V", "host-name", NULL, NULL, host-name);

.. c:function:: int xo_emit_field_h (xo_handle_t *xop, const char *rolmod, const char *contents, const char *fmt, const char *efmt, ...)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :param rolmod: A comma-separated list of field roles and field modifiers
  :type rolmod: const char *
  :param contents: The "contents" portion of the field description string
  :type contents: const char *
  :param fmt: Content format string
  :type fmt: const char *
  :param efmt: Encoding format string, followed by additional arguments
  :type efmt: const char *
  :returns: If XOF_COLUMNS is set, the number of columns used; otherwise the number of bytes emitted
  :rtype: int

.. c:function:: int xo_emit_field_hv (xo_handle_t *xop, const char *rolmod, const char *contents, const char *fmt, const char *efmt, va_list vap)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*
  :param rolmod: A comma-separated list of field roles and field modifiers
  :type rolmod: const char *
  :param contents: The "contents" portion of the field description string
  :type contents: const char *
  :param fmt: Content format string
  :type fmt: const char *
  :param efmt: Encoding format string
  :type efmt: const char *
  :param va_list vap: A set of variadic arguments
  :returns: If XOF_COLUMNS is set, the number of columns used; otherwise the number of bytes emitted
  :rtype: int

.. index:: xo_attr
.. _xo_attr:

Attributes (xo_attr)
~~~~~~~~~~~~~~~~~~~~

The functions in this section emit an XML attribute with the given name
and value.  This only affects the XML output style.

The `name` parameter give the name of the attribute to be encoded.  The
`fmt` parameter gives a printf-style format string used to format the
value of the attribute using any remaining arguments, or the vap
parameter passed to `xo_attr_hv`.

All attributes recorded via `xo_attr` are placed on the next
container, instance, leaf, or leaf list that is emitted.

Since attributes are only emitted in XML, their use should be limited
to meta-data and additional or redundant representations of data
already emitted in other form.

.. c:function:: int xo_attr (const char *name, const char *fmt, ...)

  :param name: Attribute name
  :type name: const char *
  :param fmt: Attribute value, as variadic arguments
  :type fmt: const char *
  :returns: -1 for error, or the number of bytes in the formatted attribute value
  :rtype: int

  ::

    EXAMPLE:
        xo_attr("seconds", "%ld", (unsigned long) login_time);
        struct tm *tmp = localtime(login_time);
        strftime(buf, sizeof(buf), "%R", tmp);
        xo_emit("Logged in at {:login-time}\n", buf);
    XML:
        <login-time seconds="1408336270">00:14</login-time>


.. c:function:: int xo_attr_h (xo_handle_t *xop, const char *name, const char *fmt, ...)

  :param xop: Handle for modify (or NULL for default handle)
  :type xop: xo_handle_t \*

  The `xo_attr_h` function follows the conventions of `xo_attr` but
  adds an explicit libxo handle.

.. c:function:: int xo_attr_hv (xo_handle_t *xop, const char *name, const char *fmt, va_list vap)

  The `xo_attr_h` function follows the conventions of `xo_attr_h`
  but replaced the variadic list with a variadic pointer.

.. index:: xo_flush

Flushing Output (xo_flush)
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. c:function:: xo_ssize_t xo_flush (void)

  :returns: -1 for error, or the number of bytes generated
  :rtype: xo_ssize_t

  libxo buffers data, both for performance and consistency, but also
  to allow for the proper function of various advanced features.  At
  various times, the caller may wish to flush any data buffered within
  the library.  The `xo_flush` call is used for this.

  Calling `xo_flush` also triggers the flush function associated with
  the handle.  For the default handle, this is equivalent to
  "fflush(stdio);".

.. c:function:: xo_ssize_t xo_flush_h (xo_handle_t *xop)

  :param xop: Handle for flush (or NULL for default handle)
  :type xop: xo_handle_t \*
  :returns: -1 for error, or the number of bytes generated
  :rtype: xo_ssize_t

  The `xo_flush_h` function follows the conventions of `xo_flush`,
  but adds an explicit libxo handle.

.. index:: xo_finish
.. index:: xo_finish_atexit
.. index:: atexit

Finishing Output (xo_finish)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When the program is ready to exit or close a handle, a call to
`xo_finish` or `xo_finish_h` is required.  This flushes any buffered
data, closes open libxo constructs, and completes any pending
operations.

Calling this function is vital to the proper operation of libxo,
especially for the non-TEXT output styles.

.. c:function:: xo_ssize_t xo_finish (void)

  :returns: -1 on error, or the number of bytes flushed
  :rtype: xo_ssize_t

.. c:function:: xo_ssize_t xo_finish_h (xo_handle_t *xop)

  :param xop: Handle for finish (or NULL for default handle)
  :type xop: xo_handle_t \*
  :returns: -1 on error, or the number of bytes flushed
  :rtype: xo_ssize_t

.. c:function:: void xo_finish_atexit (void)

  The `xo_finish_atexit` function is suitable for use with
  :manpage:`atexit(3)` to ensure that `xo_finish` is called
  on the default handle when the application exits.

.. index:: UTF-8
.. index:: xo_open_container
.. index:: xo_close_container

Emitting Hierarchy
------------------

libxo represents two types of hierarchy: containers and lists.  A
container appears once under a given parent where a list consists of
instances that can appear multiple times.  A container is used to hold
related fields and to give the data organization and scope.

.. index:: YANG

.. admonition:: YANG Terminology

  libxo uses terminology from YANG (:RFC:`7950`), the data modeling
  language for NETCONF: container, list, leaf, and leaf-list.

For XML and JSON, individual fields appear inside hierarchies which
provide context and meaning to the fields.  Unfortunately, these
encoding have a basic disconnect between how lists is similar objects
are represented.

XML encodes lists as set of sequential elements::

    <user>phil</user>
    <user>pallavi</user>
    <user>sjg</user>

JSON encodes lists using a single name and square brackets::

    "user": [ "phil", "pallavi", "sjg" ]

This means libxo needs three distinct indications of hierarchy: one
for containers of hierarchy appear only once for any specific parent,
one for lists, and one for each item in a list.

.. index:: Containers

Containers
~~~~~~~~~~

A "*container*" is an element of a hierarchy that appears only once
under any specific parent.  The container has no value, but serves to
contain and organize other nodes.

To open a container, call xo_open_container() or
xo_open_container_h().  The former uses the default handle and the
latter accepts a specific handle.  To close a level, use the
xo_close_container() or xo_close_container_h() functions.

Each open call must have a matching close call.  If the XOF_WARN flag
is set and the name given does not match the name of the currently open
container, a warning will be generated.

.. c:function:: xo_ssize_t xo_open_container (const char *name)

  :param name: Name of the container
  :type name: const char *
  :returns: -1 on error, or the number of bytes generated
  :rtype: xo_ssize_t

  The `name` parameter gives the name of the container, encoded in
  UTF-8.  Since ASCII is a proper subset of UTF-8, traditional C
  strings can be used directly.

.. c:function:: xo_ssize_t xo_open_container_h (xo_handle_t *xop, const char *name)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

  The `xo_open_container_h` function adds a `handle` parameter.

.. c:function:: xo_ssize_t xo_close_container (const char *name)

  :param name: Name of the container
  :type name: const char *
  :returns: -1 on error, or the number of bytes generated
  :rtype: xo_ssize_t

.. c:function:: xo_ssize_t xo_close_container_h (xo_handle_t *xop, const char *name)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

  The `xo_close_container_h` function adds a `handle` parameter.

Use the :index:`XOF_WARN` flag to generate a warning if the name given
on the close does not match the current open container.

For TEXT and HTML output, containers are not rendered into output
text, though for HTML they are used to record an XPath value when the
:index:`XOF_XPATH` flag is set.

::

    EXAMPLE:
        xo_open_container("top");
        xo_open_container("system");
        xo_emit("{:host-name/%s%s%s}", hostname,
                domainname ? "." : "", domainname ?: "");
        xo_close_container("system");
        xo_close_container("top");
    TEXT:
        my-host.example.org
    XML:
        <top>
          <system>
              <host-name>my-host.example.org</host-name>
          </system>
        </top>
    JSON:
        "top" : {
          "system" : {
              "host-name": "my-host.example.org"
          }
        }
    HTML:
        <div class="data"
             data-tag="host-name">my-host.example.org</div>

.. index:: xo_open_instance
.. index:: xo_close_instance
.. index:: xo_open_list
.. index:: xo_close_list

Lists and Instances
~~~~~~~~~~~~~~~~~~~

A "*list*" is set of one or more instances that appear under the same
parent.  The instances contain details about a specific object.  One
can think of instances as objects or records.  A call is needed to
open and close the list, while a distinct call is needed to open and
close each instance of the list.

The name given to all calls must be identical, and it is strongly
suggested that the name be singular, not plural, as a matter of
style and usage expectations::

  EXAMPLE:
      xo_open_list("item");

      for (ip = list; ip->i_title; ip++) {
          xo_open_instance("item");
          xo_emit("{L:Item} '{:name/%s}':\n", ip->i_title);
          xo_close_instance("item");
      }

      xo_close_list("item");

Getting the list and instance calls correct is critical to the proper
generation of XML and JSON data.

Opening Lists
+++++++++++++

.. c:function:: xo_ssize_t xo_open_list (const char *name)

  :param name: Name of the list
  :type name: const char *
  :returns: -1 on error, or the number of bytes generated
  :rtype: xo_ssize_t
		
  The `xo_open_list` function open a list of instances.

.. c:function:: xo_ssize_t xo_open_list_h (xo_handle_t *xop, const char *name)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

Closing Lists
+++++++++++++

.. c:function:: xo_ssize_t xo_close_list (const char *name)

  :param name: Name of the list
  :type name: const char *
  :returns: -1 on error, or the number of bytes generated
  :rtype: xo_ssize_t
		
  The `xo_close_list` function closes a list of instances.

.. c:function:: xo_ssize_t xo_close_list_h (xo_handle_t *xop, const char *name)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

   The `xo_close_container_h` function adds a `handle` parameter.

Opening Instances
+++++++++++++++++

.. c:function:: xo_ssize_t xo_open_instance (const char *name)

  :param name: Name of the instance (same as the list name)
  :type name: const char *
  :returns: -1 on error, or the number of bytes generated
  :rtype: xo_ssize_t
		
  The `xo_open_instance` function open a single instance.

.. c:function:: xo_ssize_t xo_open_instance_h (xo_handle_t *xop, const char *name)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

   The `xo_open_instance_h` function adds a `handle` parameter.

Closing Instances
+++++++++++++++++

.. c:function:: xo_ssize_t xo_close_instance (const char *name)

  :param name: Name of the instance
  :type name: const char *
  :returns: -1 on error, or the number of bytes generated
  :rtype: xo_ssize_t

  The `xo_close_instance` function closes an open instance.

.. c:function:: xo_ssize_t xo_close_instance_h (xo_handle_t *xop, const char *name)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

  The `xo_close_instance_h` function adds a `handle` parameter.

  ::

    EXAMPLE:
        xo_open_list("user");
        for (i = 0; i < num_users; i++) {
            xo_open_instance("user");
            xo_emit("{k:name}:{:uid/%u}:{:gid/%u}:{:home}\n",
                    pw[i].pw_name, pw[i].pw_uid,
                    pw[i].pw_gid, pw[i].pw_dir);
            xo_close_instance("user");
        }
        xo_close_list("user");
    TEXT:
        phil:1001:1001:/home/phil
        pallavi:1002:1002:/home/pallavi
    XML:
        <user>
            <name>phil</name>
            <uid>1001</uid>
            <gid>1001</gid>
            <home>/home/phil</home>
        </user>
        <user>
            <name>pallavi</name>
            <uid>1002</uid>
            <gid>1002</gid>
            <home>/home/pallavi</home>
        </user>
    JSON:
        user: [
            {
                "name": "phil",
                "uid": 1001,
                "gid": 1001,
                "home": "/home/phil",
            },
            {
                "name": "pallavi",
                "uid": 1002,
                "gid": 1002,
                "home": "/home/pallavi",
            }
        ]

Markers
~~~~~~~

Markers are used to protect and restore the state of open hierarchy
constructs (containers, lists, or instances).  While a marker is open,
no other open constructs can be closed.  When a marker is closed, all
constructs open since the marker was opened will be closed.

Markers use names which are not user-visible, allowing the caller to
choose appropriate internal names.

In this example, the code whiffles through a list of fish, calling a
function to emit details about each fish.  The marker "fish-guts" is
used to ensure that any constructs opened by the function are closed
properly::

  EXAMPLE:
      for (i = 0; fish[i]; i++) {
          xo_open_instance("fish");
          xo_open_marker("fish-guts");
          dump_fish_details(i);
          xo_close_marker("fish-guts");
      }

.. c:function:: xo_ssize_t xo_open_marker(const char *name)

  :param name: Name of the instance
  :type name: const char *
  :returns: -1 on error, or the number of bytes generated
  :rtype: xo_ssize_t

  The `xo_open_marker` function records the current state of open tags
  in order for `xo_close_marker` to close them at some later point.

.. c:function:: xo_ssize_t xo_open_marker_h(const char *name)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

  The `xo_open_marker_h` function adds a `handle` parameter.

.. c:function:: xo_ssize_t xo_close_marker(const char *name)

  :param name: Name of the instance
  :type name: const char *
  :returns: -1 on error, or the number of bytes generated
  :rtype: xo_ssize_t

  The `xo_close_marker` function closes any open containers, lists, or
  instances as needed to return to the state recorded when
  `xo_open_marker` was called with the matching name.

.. c:function:: xo_ssize_t xo_close_marker(const char *name)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

  The `xo_close_marker_h` function adds a `handle` parameter.

DTRT Mode
~~~~~~~~~

Some users may find tracking the names of open containers, lists, and
instances inconvenient.  libxo offers a "Do The Right Thing" mode, where
libxo will track the names of open containers, lists, and instances so
the close function can be called without a name.  To enable DTRT mode,
turn on the XOF_DTRT flag prior to making any other libxo output::

    xo_set_flags(NULL, XOF_DTRT);

.. index:: XOF_DTRT

Each open and close function has a version with the suffix "_d", which
will close the open container, list, or instance::

    xo_open_container_d("top");
    ...
    xo_close_container_d();

This also works for lists and instances::

    xo_open_list_d("item");
    for (...) {
        xo_open_instance_d("item");
        xo_emit(...);
        xo_close_instance_d();
    }
    xo_close_list_d();

.. index:: XOF_WARN

Note that the XOF_WARN flag will also cause libxo to track open
containers, lists, and instances.  A warning is generated when the
name given to the close function and the name recorded do not match.

Support Functions
-----------------

.. index:: xo_parse_args
.. _xo_parse_args:

Parsing Command-line Arguments (xo_parse_args)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. c:function:: int xo_parse_args (int argc, char **argv)

  :param int argc: Number of arguments
  :param argv: Array of argument strings
  :return: -1 on error, or the number of remaining arguments
  :rtype: int

  The `xo_parse_args` function is used to process a program's
  arguments.  libxo-specific options are processed and removed from
  the argument list so the calling application does not need to
  process them.  If successful, a new value for argc is returned.  On
  failure, a message is emitted and -1 is returned::

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
        exit(EXIT_FAILURE);

  Following the call to xo_parse_args, the application can process the
  remaining arguments in a normal manner.  See :ref:`options` for a
  description of valid arguments.

.. index:: xo_set_program

xo_set_program
~~~~~~~~~~~~~~

.. c:function:: void xo_set_program (const char *name)

  :param name: Name to use as the program name
  :type name: const char *
  :returns: void

  The `xo_set_program` function sets the name of the program as
  reported by functions like `xo_failure`, `xo_warn`, `xo_err`, etc.
  The program name is initialized by `xo_parse_args`, but subsequent
  calls to `xo_set_program` can override this value::

    EXAMPLE:
        xo_set_program(argv[0]);

  Note that the value is not copied, so the memory passed to
  `xo_set_program` (and `xo_parse_args`) must be maintained by the
  caller.

.. index:: xo_set_version

xo_set_version
~~~~~~~~~~~~~~

.. c:function:: void xo_set_version (const char *version)

  :param name: Value to use as the version string
  :type name: const char *
  :returns: void

  The `xo_set_version` function records a version number to be emitted
  as part of the data for encoding styles (XML and JSON).  This
  version number is suitable for tracking changes in the content,
  allowing a user of the data to discern which version of the data
  model is in use.

.. c:function:: void xo_set_version_h (xo_handle_t *xop, const char *version)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *

  The `xo_set_version` function adds a `handle` parameter.

.. index:: --libxo
.. index:: XOF_INFO
.. index:: xo_info_t

.. _field-information:

Field Information (xo_info_t)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

HTML data can include additional information in attributes that
begin with "data-".  To enable this, three things must occur:

First the application must build an array of xo_info_t structures,
one per tag.  The array must be sorted by name, since libxo uses a
binary search to find the entry that matches names from format
instructions.

Second, the application must inform libxo about this information using
the `xo_set_info` call::

    typedef struct xo_info_s {
        const char *xi_name;    /* Name of the element */
        const char *xi_type;    /* Type of field */
        const char *xi_help;    /* Description of field */
    } xo_info_t;

    void xo_set_info (xo_handle_t *xop, xo_info_t *infop, int count);

Like other libxo calls, passing `NULL` for the handle tells libxo to
use the default handle.

If the count is -1, libxo will count the elements of infop, but there
must be an empty element at the end.  More typically, the number is
known to the application::

    xo_info_t info[] = {
        { "in-stock", "number", "Number of items in stock" },
        { "name", "string", "Name of the item" },
        { "on-order", "number", "Number of items on order" },
        { "sku", "string", "Stock Keeping Unit" },
        { "sold", "number", "Number of items sold" },
    };
    int info_count = (sizeof(info) / sizeof(info[0]));
    ...
    xo_set_info(NULL, info, info_count);

Third, the emission of info must be triggered with the `XOF_INFO` flag
using either the `xo_set_flags` function or the "`--libxo=info`"
command line argument.

The type and help values, if present, are emitted as the "data-type"
and "data-help" attributes::

  <div class="data" data-tag="sku" data-type="string"
       data-help="Stock Keeping Unit">GRO-000-533</div>

.. c:function:: void xo_set_info (xo_handle_t *xop, xo_info_t *infop, int count)

  :param xop: Handle to use (or NULL for default handle)
  :type xop: xo_handle_t *
  :param infop: Array of information structures
  :type infop: xo_info_t *
  :returns: void

.. index:: xo_set_allocator
.. index:: xo_realloc_func_t
.. index:: xo_free_func_t

Memory Allocation
~~~~~~~~~~~~~~~~~

The `xo_set_allocator` function allows libxo to be used in
environments where the standard :manpage:`realloc(3)` and
:manpage:`free(3)` functions are not appropriate.

.. c:function:: void xo_set_allocator (xo_realloc_func_t realloc_func, xo_free_func_t free_func)

  :param xo_realloc_func_t realloc_func:  Allocation function
  :param xo_free_func_t free_func: Free function

  *realloc_func* should expect the same arguments as
  :manpage:`realloc(3)` and return a pointer to memory following the
  same convention.  *free_func* will receive the same argument as
  :manpage:`free(3)` and should release it, as appropriate for the
  environment.

By default, the standard :manpage:`realloc(3)` and :manpage:`free(3)`
functions are used.

.. index:: --libxo

.. _libxo-options:

LIBXO_OPTIONS
~~~~~~~~~~~~~

The environment variable "LIBXO_OPTIONS" can be set to a subset of
libxo options, including:

- color
- flush
- flush-line
- no-color
- no-humanize
- no-locale
- no-retain
- pretty
- retain
- underscores
- warn

For example, warnings can be enabled by::

    % env LIBXO_OPTIONS=warn my-app

Since environment variables are inherited, child processes will have
the same options, which may be undesirable, making the use of the
"`--libxo`" command-line option preferable in most situations.

.. index:: xo_warn
.. index:: xo_err
.. index:: xo_errx
.. index:: xo_message

Errors, Warnings, and Messages
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Many programs make use of the standard library functions
:manpage:`err(3)` and :manpage:`warn(3)` to generate errors and
warnings for the user.  libxo wants to pass that information via the
current output style, and provides compatible functions to allow
this::

    void xo_warn (const char *fmt, ...);
    void xo_warnx (const char *fmt, ...);
    void xo_warn_c (int code, const char *fmt, ...);
    void xo_warn_hc (xo_handle_t *xop, int code,
                     const char *fmt, ...);
    void xo_err (int eval, const char *fmt, ...);
    void xo_errc (int eval, int code, const char *fmt, ...);
    void xo_errx (int eval, const char *fmt, ...);

::

    void xo_message (const char *fmt, ...);
    void xo_message_c (int code, const char *fmt, ...);
    void xo_message_hc (xo_handle_t *xop, int code,
                        const char *fmt, ...);
    void xo_message_hcv (xo_handle_t *xop, int code,
                         const char *fmt, va_list vap);

These functions display the program name, a colon, a formatted message
based on the arguments, and then optionally a colon and an error
message associated with either *errno* or the *code* parameter::

    EXAMPLE:
        if (open(filename, O_RDONLY) < 0)
            xo_err(1, "cannot open file '%s'", filename);

.. index:: xo_error

xo_error
~~~~~~~~

.. c:function:: void xo_error (const char *fmt, ...)

  :param fmt: Format string
  :type fmt: const char *
  :returns: void

  The `xo_error` function can be used for generic errors that should
  be reported over the handle, rather than to stderr.  The `xo_error`
  function behaves like `xo_err` for TEXT and HTML output styles, but
  puts the error into XML or JSON elements::

    EXAMPLE::
        xo_error("Does not %s", "compute");
    XML::
        <error><message>Does not compute</message></error>
    JSON::
        "error": { "message": "Does not compute" }

.. index:: xo_no_setlocale
.. index:: Locale

xo_no_setlocale
~~~~~~~~~~~~~~~

.. c:function:: void xo_no_setlocale (void)

  libxo automatically initializes the locale based on setting of the
  environment variables LC_CTYPE, LANG, and LC_ALL.  The first of this
  list of variables is used and if none of the variables, the locale
  defaults to "UTF-8".  The caller may wish to avoid this behavior,
  and can do so by calling the `xo_no_setlocale` function.

Emitting syslog Messages
------------------------

syslog is the system logging facility used throughout the unix world.
Messages are sent from commands, applications, and daemons to a
hierarchy of servers, where they are filtered, saved, and forwarded
based on configuration behaviors.

syslog is an older protocol, originally documented only in source
code.  By the time :RFC:`3164` published, variation and mutation left the
leading "<pri>" string as only common content.  :RFC:`5424` defines a new
version (version 1) of syslog and introduces structured data into the
messages.  Structured data is a set of name/value pairs transmitted
distinctly alongside the traditional text message, allowing filtering
on precise values instead of regular expressions.

These name/value pairs are scoped by a two-part identifier; an
enterprise identifier names the party responsible for the message
catalog and a name identifying that message.  `Enterprise IDs`_ are
defined by IANA, the Internet Assigned Numbers Authority.

.. _Enterprise IDs:
    https://www.iana.org/assignments/enterprise-numbers/enterprise-numbers

Use the `xo_set_syslog_enterprise_id` function to set the Enterprise
ID, as needed.

The message name should follow the conventions in
:ref:`good-field-names`\ , as should the fields within the message::

    /* Both of these calls are optional */
    xo_set_syslog_enterprise_id(32473);
    xo_open_log("my-program", 0, LOG_DAEMON);

    /* Generate a syslog message */
    xo_syslog(LOG_ERR, "upload-failed",
              "error <%d> uploading file '{:filename}' "
              "as '{:target/%s:%s}'",
              code, filename, protocol, remote);

    xo_syslog(LOG_INFO, "poofd-invalid-state",
              "state {:current/%u} is invalid {:connection/%u}",
	      state, conn);

The developer should be aware that the message name may be used in the
future to allow access to further information, including
documentation.  Care should be taken to choose quality, descriptive
names.

.. _syslog-details:

Priority, Facility, and Flags
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The `xo_syslog`, `xo_vsyslog`, and `xo_open_log` functions
accept a set of flags which provide the priority of the message, the
source facility, and some additional features.  These values are OR'd
together to create a single integer argument::

    xo_syslog(LOG_ERR | LOG_AUTH, "login-failed",
             "Login failed; user '{:user}' from host '{:address}'",
             user, addr);

These values are defined in <syslog.h>.

The priority value indicates the importance and potential impact of
each message:

============= =======================================================
 Priority      Description
============= =======================================================
 LOG_EMERG     A panic condition, normally broadcast to all users
 LOG_ALERT     A condition that should be corrected immediately
 LOG_CRIT      Critical conditions
 LOG_ERR       Generic errors
 LOG_WARNING   Warning messages
 LOG_NOTICE    Non-error conditions that might need special handling
 LOG_INFO      Informational messages
 LOG_DEBUG     Developer-oriented messages
============= =======================================================

The facility value indicates the source of message, in fairly generic
terms:

=============== =======================================================
 Facility        Description
=============== =======================================================
 LOG_AUTH        The authorization system (e.g. :manpage:`login(1)`)
 LOG_AUTHPRIV    As LOG_AUTH, but logged to a privileged file
 LOG_CRON        The cron daemon: :manpage:`cron(8)`
 LOG_DAEMON      System daemons, not otherwise explicitly listed
 LOG_FTP         The file transfer protocol daemons
 LOG_KERN        Messages generated by the kernel
 LOG_LPR         The line printer spooling system
 LOG_MAIL        The mail system
 LOG_NEWS        The network news system
 LOG_SECURITY    Security subsystems, such as :manpage:`ipfw(4)`
 LOG_SYSLOG      Messages generated internally by :manpage:`syslogd(8)`
 LOG_USER        Messages generated by user processes (default)
 LOG_UUCP        The uucp system
 LOG_LOCAL0..7   Reserved for local use
=============== =======================================================

In addition to the values listed above, xo_open_log accepts a set of
addition flags requesting specific logging behaviors:

============ ====================================================
 Flag         Description
============ ====================================================
 LOG_CONS     If syslogd fails, attempt to write to /dev/console
 LOG_NDELAY   Open the connection to :manpage:`syslogd(8)` immediately
 LOG_PERROR   Write the message also to standard error output
 LOG_PID      Log the process id with each message
============ ====================================================

.. index:: xo_syslog

xo_syslog
~~~~~~~~~

.. c:function:: void xo_syslog (int pri, const char *name, const char *fmt, ...)

  :param int pri: syslog priority
  :param name: Name of the syslog event
  :type name: const char *
  :param fmt: Format string, followed by arguments
  :type fmt: const char *
  :returns: void

  Use the `xo_syslog` function to generate syslog messages by calling
  it with a log priority and facility, a message name, a format
  string, and a set of arguments.  The priority/facility argument are
  discussed above, as is the message name.

  The format string follows the same conventions as `xo_emit`'s format
  string, with each field being rendered as an SD-PARAM pair::

    xo_syslog(LOG_ERR, "poofd-missing-file",
              "'{:filename}' not found: {:error/%m}", filename);

    ... [poofd-missing-file@32473 filename="/etc/poofd.conf"
          error="Permission denied"] '/etc/poofd.conf' not
          found: Permission denied

Support functions
~~~~~~~~~~~~~~~~~

.. index:: xo_vsyslog

xo_vsyslog
++++++++++

.. c:function:: void xo_vsyslog (int pri, const char *name, const char *fmt, va_list vap)

  :param int pri: syslog priority
  :param name: Name of the syslog event
  :type name: const char *
  :param fmt: Format string
  :type fmt: const char *
  :param va_list vap: Variadic argument list
  :returns: void

  xo_vsyslog is identical in function to xo_syslog, but takes the set of
  arguments using a va_list::

    EXAMPLE:
        void
        my_log (const char *name, const char *fmt, ...)
        {
            va_list vap;
            va_start(vap, fmt);
            xo_vsyslog(LOG_ERR, name, fmt, vap);
            va_end(vap);
        }

.. index:: xo_open_log

xo_open_log
+++++++++++

.. c:function:: void xo_open_log (const char *ident, int logopt, int facility)

  :param indent:
  :type indent: const char *
  :param int logopt: Bit field containing logging options
  :param int facility:
  :returns: void

  xo_open_log functions similar to :manpage:`openlog(3)`, allowing
  customization of the program name, the log facility number, and the
  additional option flags described in :ref:`syslog-details`.

.. index:: xo_close_log

xo_close_log
++++++++++++

.. c:function:: void xo_close_log (void)

  The `xo_close_log` function is similar to :manpage:`closelog(3)`,
  closing the log file and releasing any associated resources.

.. index:: xo_set_logmask

xo_set_logmask
++++++++++++++

.. c:function:: int xo_set_logmask (int maskpri)

  :param int maskpri: the log priority mask
  :returns: The previous log priority mask

  The `xo_set_logmask` function is similar to :manpage:`setlogmask(3)`,
  restricting the set of generated log event to those whose associated
  bit is set in maskpri.  Use `LOG_MASK(pri)` to find the appropriate bit,
  or `LOG_UPTO(toppri)` to create a mask for all priorities up to and
  including toppri::

    EXAMPLE:
        setlogmask(LOG_UPTO(LOG_WARN));

.. index:: xo_set_syslog_enterprise_id

xo_set_syslog_enterprise_id
+++++++++++++++++++++++++++

.. c:function:: void xo_set_syslog_enterprise_id (unsigned short eid)

  Use the `xo_set_syslog_enterprise_id` to supply a platform- or
  application-specific enterprise id.  This value is used in any future
  syslog messages.

  Ideally, the operating system should supply a default value via the
  "kern.syslog.enterprise_id" sysctl value.  Lacking that, the
  application should provide a suitable value.

Enterprise IDs are administered by IANA, the Internet Assigned Number
Authority.  The complete list is EIDs on their web site::

    https://www.iana.org/assignments/enterprise-numbers/enterprise-numbers

New EIDs can be requested from IANA using the following page::

    http://pen.iana.org/pen/PenApplication.page

Each software development organization that defines a set of syslog
messages should register their own EID and use that value in their
software to ensure that messages can be uniquely identified by the
combination of EID + message name.

Creating Custom Encoders
------------------------

The number of encoding schemes in current use is staggering, with new
and distinct schemes appearing daily.  While libxo provide XML, JSON,
HMTL, and text natively, there are requirements for other encodings.

Rather than bake support for all possible encoders into libxo, the API
allows them to be defined externally.  libxo can then interfaces with
these encoding modules using a simplistic API.  libxo processes all
functions calls, handles state transitions, performs all formatting,
and then passes the results as operations to a customized encoding
function, which implements specific encoding logic as required.  This
means your encoder doesn't need to detect errors with unbalanced
open/close operations but can rely on libxo to pass correct data.

By making a simple API, libxo internals are not exposed, insulating the
encoder and the library from future or internal changes.

The three elements of the API are:

- loading
- initialization
- operations

The following sections provide details about these topics.

.. index:: CBOR

libxo source contains an encoder for Concise Binary Object
Representation, aka CBOR (:RFC:`7049`), which can be used as an
example for the API for other encoders.

Loading Encoders
~~~~~~~~~~~~~~~~

Encoders can be registered statically or discovered dynamically.
Applications can choose to call the `xo_encoder_register` function
to explicitly register encoders, but more typically they are built as
shared libraries, placed in the libxo/extensions directory, and loaded
based on name.  libxo looks for a file with the name of the encoder
and an extension of ".enc".  This can be a file or a symlink to the
shared library file that supports the encoder::

    % ls -1 lib/libxo/extensions/*.enc
    lib/libxo/extensions/cbor.enc
    lib/libxo/extensions/test.enc

Encoder Initialization
~~~~~~~~~~~~~~~~~~~~~~

Each encoder must export a symbol used to access the library, which
must have the following signature::

    int xo_encoder_library_init (XO_ENCODER_INIT_ARGS);

`XO_ENCODER_INIT_ARGS` is a macro defined in "xo_encoder.h" that defines
an argument called "arg", a pointer of the type
`xo_encoder_init_args_t`.  This structure contains two fields:

- `xei_version` is the version number of the API as implemented
  within libxo.  This version is currently as 1 using
  `XO_ENCODER_VERSION`.  This number can be checked to ensure
  compatibility.  The working assumption is that all versions should
  be backward compatible, but each side may need to accurately know
  the version supported by the other side.  `xo_encoder_library_init`
  can optionally check this value, and must then set it to the version
  number used by the encoder, allowing libxo to detect version
  differences and react accordingly.  For example, if version 2 adds
  new operations, then libxo will know that an encoding library that
  set `xei_version` to 1 cannot be expected to handle those new
  operations.

- xei_handler must be set to a pointer to a function of type
  `xo_encoder_func_t`, as defined in "xo_encoder.h".  This function
  takes a set of parameters:
  - xop is a pointer to the opaque `xo_handle_t` structure
  - op is an integer representing the current operation
  - name is a string whose meaning differs by operation
  - value is a string whose meaning differs by operation
  - private is an opaque structure provided by the encoder

Additional arguments may be added in the future, so handler functions
should use the `XO_ENCODER_HANDLER_ARGS` macro.  An appropriate
"extern" declaration is provided to help catch errors.

Once the encoder initialization function has completed processing, it
should return zero to indicate that no error has occurred.  A non-zero
return code will cause the handle initialization to fail.

Operations
~~~~~~~~~~

The encoder API defines a set of operations representing the
processing model of libxo.  Content is formatted within libxo, and
callbacks are made to the encoder's handler function when data is
ready to be processed:

======================= =======================================
 Operation               Meaning  (Base function)
======================= =======================================
 XO_OP_CREATE            Called when the handle is created
 XO_OP_OPEN_CONTAINER    Container opened (xo_open_container)
 XO_OP_CLOSE_CONTAINER   Container closed (xo_close_container)
 XO_OP_OPEN_LIST         List opened (xo_open_list)
 XO_OP_CLOSE_LIST        List closed (xo_close_list)
 XO_OP_OPEN_LEAF_LIST    Leaf list opened (xo_open_leaf_list)
 XO_OP_CLOSE_LEAF_LIST   Leaf list closed (xo_close_leaf_list)
 XO_OP_OPEN_INSTANCE     Instance opened (xo_open_instance)
 XO_OP_CLOSE_INSTANCE    Instance closed (xo_close_instance)
 XO_OP_STRING            Field with Quoted UTF-8 string
 XO_OP_CONTENT           Field with content
 XO_OP_FINISH            Finish any pending output
 XO_OP_FLUSH             Flush any buffered output
 XO_OP_DESTROY           Clean up resources
 XO_OP_ATTRIBUTE         An attribute name/value pair
 XO_OP_VERSION           A version string
======================= =======================================

For all the open and close operations, the name parameter holds the
name of the construct.  For string, content, and attribute operations,
the name parameter is the name of the field and the value parameter is
the value.  "string" are differentiated from "content" to allow differing
treatment of true, false, null, and numbers from real strings, though
content values are formatted as strings before the handler is called.
For version operations, the value parameter contains the version.

All strings are encoded in UTF-8.
