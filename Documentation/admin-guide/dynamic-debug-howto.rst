Dynamic debug
+++++++++++++


Introduction
============

Dynamic debug allows you to dynamically enable/disable kernel
debug-print code to obtain additional kernel information.

If ``/proc/dynamic_debug/control`` exists, your kernel has dynamic
debug.  You'll need root access (sudo su) to use this.

Dynamic debug provides:

 * a Catalog of all *prdbgs* in your kernel.
   ``cat /proc/dynamic_debug/control`` to see them.

 * a Simple query/command language to alter *prdbgs* by selecting on
   any combination of 0 or 1 of:

   - source filename
   - function name
   - line number (including ranges of line numbers)
   - module name
   - format string
   - class name (as known/declared by each module)

Viewing Dynamic Debug Behaviour
===============================

You can view the currently configured behaviour in the *prdbg* catalog::

  :#> head -n7 /proc/dynamic_debug/control
  # filename:lineno [module]function flags format
  init/main.c:1179 [main]initcall_blacklist =_ "blacklisting initcall %s\012
  init/main.c:1218 [main]initcall_blacklisted =_ "initcall %s blacklisted\012"
  init/main.c:1424 [main]run_init_process =_ "  with arguments:\012"
  init/main.c:1426 [main]run_init_process =_ "    %s\012"
  init/main.c:1427 [main]run_init_process =_ "  with environment:\012"
  init/main.c:1429 [main]run_init_process =_ "    %s\012"

The 3rd space-delimited column shows the current flags, preceded by
a ``=`` for easy use with grep/cut. ``=p`` shows enabled callsites.

Controlling dynamic debug Behaviour
===================================

The behaviour of *prdbg* sites are controlled by writing
query/commands to the control file.  Example::

  # grease the interface
  :#> alias ddcmd='echo $* > /proc/dynamic_debug/control'

  :#> ddcmd '-p; module main func run* +p'
  :#> grep =p /proc/dynamic_debug/control
  init/main.c:1424 [main]run_init_process =p "  with arguments:\012"
  init/main.c:1426 [main]run_init_process =p "    %s\012"
  init/main.c:1427 [main]run_init_process =p "  with environment:\012"
  init/main.c:1429 [main]run_init_process =p "    %s\012"

Error messages go to console/syslog::

  :#> ddcmd mode foo +p
  dyndbg: unknown keyword "mode"
  dyndbg: query parse failed
  bash: echo: write error: Invalid argument

If debugfs is also enabled and mounted, ``dynamic_debug/control`` is
also under the mount-dir, typically ``/sys/kernel/debug/``.

Command Language Reference
==========================

At the basic lexical level, a command is a sequence of words separated
by spaces or tabs.  So these are all equivalent::

  :#> ddcmd file svcsock.c line 1603 +p
  :#> ddcmd "file svcsock.c line 1603 +p"
  :#> ddcmd '  file   svcsock.c     line  1603 +p  '

Command submissions are bounded by a write() system call.
Multiple commands can be written together, separated by ``;`` or ``\n``::

  :#> ddcmd "func pnpacpi_get_resources +p; func pnp_assign_mem +p"
  :#> ddcmd <<"EOC"
  func pnpacpi_get_resources +p
  func pnp_assign_mem +p
  EOC
  :#> cat query-batch-file > /proc/dynamic_debug/control

You can also use wildcards in each query term. The match rule supports
``*`` (matches zero or more characters) and ``?`` (matches exactly one
character). For example, you can match all usb drivers::

  :#> ddcmd file "drivers/usb/*" +p	# "" to suppress shell expansion

Syntactically, a command is pairs of keyword values, followed by a
flags change or setting::

  command ::= match-spec* flags-spec

The match-spec's select *prdbgs* from the catalog, upon which to apply
the flags-spec, all constraints are ANDed together.  An absent keyword
is the same as keyword "*".


A match specification is a keyword, which selects the attribute of
the callsite to be compared, and a value to compare against.  Possible
keywords are:::

  match-spec ::= 'func' string |
		 'file' string |
		 'module' string |
		 'format' string |
		 'class' string |
		 'line' line-range

  line-range ::= lineno |
		 '-'lineno |
		 lineno'-' |
		 lineno'-'lineno

  lineno ::= unsigned-int

.. note::

  ``line-range`` cannot contain space, e.g.
  "1-30" is valid range but "1 - 30" is not.


The meanings of each keyword are:

func
    The given string is compared against the function name
    of each callsite.  Example::

	func svc_tcp_accept
	func *recv*		# in rfcomm, bluetooth, ping, tcp

file
    The given string is compared against either the src-root relative
    pathname, or the basename of the source file of each callsite.
    Examples::

	file svcsock.c
	file kernel/freezer.c	# ie column 1 of control file
	file drivers/usb/*	# all callsites under it
	file inode.c:start_*	# parse :tail as a func (above)
	file inode.c:1-100	# parse :tail as a line-range (above)

module
    The given string is compared against the module name
    of each callsite.  The module name is the string as
    seen in ``lsmod``, i.e. without the directory or the ``.ko``
    suffix and with ``-`` changed to ``_``.  Examples::

	module sunrpc
	module nfsd
	module drm*	# both drm, drm_kms_helper

format
    The given string is searched for in the dynamic debug format
    string.  Note that the string does not need to match the
    entire format, only some part.  Whitespace and other
    special characters can be escaped using C octal character
    escape ``\ooo`` notation, e.g. the space character is ``\040``.
    Alternatively, the string can be enclosed in double quote
    characters (``"``) or single quote characters (``'``).
    Examples::

	format svcrdma:         // many of the NFS/RDMA server pr_debugs
	format readahead        // some pr_debugs in the readahead cache
	format nfsd:\040SETATTR // one way to match a format with whitespace
	format "nfsd: SETATTR"  // a neater way to match a format with whitespace
	format 'nfsd: SETATTR'  // yet another way to match a format with whitespace

class
    The given class_name is validated against each module, which may
    have declared a list of known class_names.  If the class_name is
    found for a module, callsite & class matching and adjustment
    proceeds.  Examples::

	class DRM_UT_KMS	# a DRM.debug category
	class JUNK		# silent non-match
	// class TLD_*		# NOTICE: no wildcard in class names

line
    The given line number or range of line numbers is compared
    against the line number of each ``pr_debug()`` callsite.  A single
    line number matches the callsite line number exactly.  A
    range of line numbers matches any callsite between the first
    and last line number inclusive.  An empty first number means
    the first line in the file, an empty last line number means the
    last line number in the file.  Examples::

	line 1603           // exactly line 1603
	line 1600-1605      // the six lines from line 1600 to line 1605
	line -1605          // the 1605 lines from line 1 to line 1605
	line 1600-          // all lines from line 1600 to the end of the file

The flags specification comprises a change operation followed
by one or more flag characters.  The change operation is one
of the characters::

  -    remove the given flags
  +    add the given flags
  =    set the flags to the given flags

The flags are::

  p    enables the pr_debug() callsite.
  _    enables no flags.

  Decorator flags add to the message-prefix, in order:
  t    Include thread ID, or <intr>
  m    Include module name
  f    Include the function name
  l    Include line number

For ``print_hex_dump_debug()`` and ``print_hex_dump_bytes()``, only
the ``p`` flag has meaning, other flags are ignored.

Note the regexp ``^[-+=][flmpt_]+$`` matches a flags specification.
To clear all flags at once, use ``=_`` or ``-flmpt``.


Debug messages during Boot Process
==================================

To activate debug messages for core code and built-in modules during
the boot process, even before userspace and debugfs exists, use
``dyndbg="QUERY"`` or ``module.dyndbg="QUERY"``.  QUERY follows
the syntax described above, but must not exceed 1023 characters.  Your
bootloader may impose lower limits.

These ``dyndbg`` params are processed just after the ddebug tables are
processed, as part of the early_initcall.  Thus you can enable debug
messages in all code run after this early_initcall via this boot
parameter.

On an x86 system for example ACPI enablement is a subsys_initcall and::

   dyndbg="file ec.c +p"

will show early Embedded Controller transactions during ACPI setup if
your machine (typically a laptop) has an Embedded Controller.
PCI (or other devices) initialization also is a hot candidate for using
this boot parameter for debugging purposes.

If ``foo`` module is not built-in, ``foo.dyndbg`` will still be processed at
boot time, without effect, but will be reprocessed when module is
loaded later. Bare ``dyndbg=`` is only processed at boot.


Debug Messages at Module Initialization Time
============================================

When ``modprobe foo`` is called, modprobe scans ``/proc/cmdline`` for
``foo.params``, strips ``foo.``, and passes them to the kernel along with
params given in modprobe args or ``/etc/modprob.d/*.conf`` files,
in the following order:

1. parameters given via ``/etc/modprobe.d/*.conf``::

	options foo dyndbg=+pt
	options foo dyndbg # defaults to +p

2. ``foo.dyndbg`` as given in boot args, ``foo.`` is stripped and passed::

	foo.dyndbg=" func bar +p; func buz +mp"

3. args to modprobe::

	modprobe foo dyndbg==pmf # override previous settings

These ``dyndbg`` queries are applied in order, with last having final say.
This allows boot args to override or modify those from ``/etc/modprobe.d``
(sensible, since 1 is system wide, 2 is kernel or boot specific), and
modprobe args to override both.

In the ``foo.dyndbg="QUERY"`` form, the query must exclude ``module foo``.
``foo`` is extracted from the param-name, and applied to each query in
``QUERY``, and only 1 match-spec of each type is allowed.

The ``dyndbg`` option is a "fake" module parameter, which means:

- modules do not need to define it explicitly
- every module gets it tacitly, whether they use pr_debug or not
- it doesn't appear in ``/sys/module/$module/parameters/``
  To see it, grep the control file, or inspect ``/proc/cmdline.``

For ``CONFIG_DYNAMIC_DEBUG`` kernels, any settings given at boot-time (or
enabled by ``-DDEBUG`` flag during compilation) can be disabled later via
the debugfs interface if the debug messages are no longer needed::

   echo "module module_name -p" > /proc/dynamic_debug/control

Examples
========

::

  // enable the message at line 1603 of file svcsock.c
  :#> ddcmd 'file svcsock.c line 1603 +p'

  // enable all the messages in file svcsock.c
  :#> ddcmd 'file svcsock.c +p'

  // enable all the messages in the NFS server module
  :#> ddcmd 'module nfsd +p'

  // enable all 12 messages in the function svc_process()
  :#> ddcmd 'func svc_process +p'

  // disable all 12 messages in the function svc_process()
  :#> ddcmd 'func svc_process -p'

  // enable messages for NFS calls READ, READLINK, READDIR and READDIR+.
  :#> ddcmd 'format "nfsd: READ" +p'

  // enable messages in files of which the paths include string "usb"
  :#> ddcmd 'file *usb* +p' > /proc/dynamic_debug/control

  // enable all messages
  :#> ddcmd '+p' > /proc/dynamic_debug/control

  // add module, function to all enabled messages
  :#> ddcmd '+mf' > /proc/dynamic_debug/control

  // boot-args example, with newlines and comments for readability
  Kernel command line: ...
    // see whats going on in dyndbg=value processing
    dynamic_debug.verbose=3
    // enable pr_debugs in the btrfs module (can be builtin or loadable)
    btrfs.dyndbg="+p"
    // enable pr_debugs in all files under init/
    // and the function parse_one, #cmt is stripped
    dyndbg="file init/* +p #cmt ; func parse_one +p"
    // enable pr_debugs in 2 functions in a module loaded later
    pc87360.dyndbg="func pc87360_init_device +p; func pc87360_find +p"

Kernel Configuration
====================

Dynamic Debug is enabled via kernel config items::

  CONFIG_DYNAMIC_DEBUG=y	# build catalog, enables CORE
  CONFIG_DYNAMIC_DEBUG_CORE=y	# enable mechanics only, skip catalog

If you do not want to enable dynamic debug globally (i.e. in some embedded
system), you may set ``CONFIG_DYNAMIC_DEBUG_CORE`` as basic support of dynamic
debug and add ``ccflags := -DDYNAMIC_DEBUG_MODULE`` into the Makefile of any
modules which you'd like to dynamically debug later.


Kernel *prdbg* API
==================

The following functions are cataloged and controllable when dynamic
debug is enabled::

  pr_debug()
  dev_dbg()
  print_hex_dump_debug()
  print_hex_dump_bytes()

Otherwise, they are off by default; ``ccflags += -DDEBUG`` or
``#define DEBUG`` in a source file will enable them appropriately.

If ``CONFIG_DYNAMIC_DEBUG`` is not set, ``print_hex_dump_debug()`` is
just a shortcut for ``print_hex_dump(KERN_DEBUG)``.

For ``print_hex_dump_debug()``/``print_hex_dump_bytes()``, format string is
its ``prefix_str`` argument, if it is constant string; or ``hexdump``
in case ``prefix_str`` is built dynamically.
