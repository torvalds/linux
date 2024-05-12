.. SPDX-License-Identifier: BSD-3-Clause

=====================================
Using Netlink protocol specifications
=====================================

This document is a quick starting guide for using Netlink protocol
specifications. For more detailed description of the specs see :doc:`specs`.

Simple CLI
==========

Kernel comes with a simple CLI tool which should be useful when
developing Netlink related code. The tool is implemented in Python
and can use a YAML specification to issue Netlink requests
to the kernel. Only Generic Netlink is supported.

The tool is located at ``tools/net/ynl/cli.py``. It accepts
a handul of arguments, the most important ones are:

 - ``--spec`` - point to the spec file
 - ``--do $name`` / ``--dump $name`` - issue request ``$name``
 - ``--json $attrs`` - provide attributes for the request
 - ``--subscribe $group`` - receive notifications from ``$group``

YAML specs can be found under ``Documentation/netlink/specs/``.

Example use::

  $ ./tools/net/ynl/cli.py --spec Documentation/netlink/specs/ethtool.yaml \
        --do rings-get \
	--json '{"header":{"dev-index": 18}}'
  {'header': {'dev-index': 18, 'dev-name': 'eni1np1'},
   'rx': 0,
   'rx-jumbo': 0,
   'rx-jumbo-max': 4096,
   'rx-max': 4096,
   'rx-mini': 0,
   'rx-mini-max': 4096,
   'tx': 0,
   'tx-max': 4096,
   'tx-push': 0}

The input arguments are parsed as JSON, while the output is only
Python-pretty-printed. This is because some Netlink types can't
be expressed as JSON directly. If such attributes are needed in
the input some hacking of the script will be necessary.

The spec and Netlink internals are factored out as a standalone
library - it should be easy to write Python tools / tests reusing
code from ``cli.py``.

Generating kernel code
======================

``tools/net/ynl/ynl-regen.sh`` scans the kernel tree in search of
auto-generated files which need to be updated. Using this tool is the easiest
way to generate / update auto-generated code.

By default code is re-generated only if spec is newer than the source,
to force regeneration use ``-f``.

``ynl-regen.sh`` searches for ``YNL-GEN`` in the contents of files
(note that it only scans files in the git index, that is only files
tracked by git!) For instance the ``fou_nl.c`` kernel source contains::

  /*	Documentation/netlink/specs/fou.yaml */
  /* YNL-GEN kernel source */

``ynl-regen.sh`` will find this marker and replace the file with
kernel source based on fou.yaml.

The simplest way to generate a new file based on a spec is to add
the two marker lines like above to a file, add that file to git,
and run the regeneration tool. Grep the tree for ``YNL-GEN``
to see other examples.

The code generation itself is performed by ``tools/net/ynl/ynl-gen-c.py``
but it takes a few arguments so calling it directly for each file
quickly becomes tedious.
