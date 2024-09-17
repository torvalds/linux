.. SPDX-License-Identifier: GPL-2.0

.. _kernel_licensing:

Linux kernel licensing rules
============================

The Linux Kernel is provided under the terms of the GNU General Public
License version 2 only (GPL-2.0), as provided in LICENSES/preferred/GPL-2.0,
with an explicit syscall exception described in
LICENSES/exceptions/Linux-syscall-note, as described in the COPYING file.

This documentation file provides a description of how each source file
should be annotated to make its license clear and unambiguous.
It doesn't replace the Kernel's license.

The license described in the COPYING file applies to the kernel source
as a whole, though individual source files can have a different license
which is required to be compatible with the GPL-2.0::

    GPL-1.0+  :  GNU General Public License v1.0 or later
    GPL-2.0+  :  GNU General Public License v2.0 or later
    LGPL-2.0  :  GNU Library General Public License v2 only
    LGPL-2.0+ :  GNU Library General Public License v2 or later
    LGPL-2.1  :  GNU Lesser General Public License v2.1 only
    LGPL-2.1+ :  GNU Lesser General Public License v2.1 or later

Aside from that, individual files can be provided under a dual license,
e.g. one of the compatible GPL variants and alternatively under a
permissive license like BSD, MIT etc.

The User-space API (UAPI) header files, which describe the interface of
user-space programs to the kernel are a special case.  According to the
note in the kernel COPYING file, the syscall interface is a clear boundary,
which does not extend the GPL requirements to any software which uses it to
communicate with the kernel.  Because the UAPI headers must be includable
into any source files which create an executable running on the Linux
kernel, the exception must be documented by a special license expression.

The common way of expressing the license of a source file is to add the
matching boilerplate text into the top comment of the file.  Due to
formatting, typos etc. these "boilerplates" are hard to validate for
tools which are used in the context of license compliance.

An alternative to boilerplate text is the use of Software Package Data
Exchange (SPDX) license identifiers in each source file.  SPDX license
identifiers are machine parsable and precise shorthands for the license
under which the content of the file is contributed.  SPDX license
identifiers are managed by the SPDX Workgroup at the Linux Foundation and
have been agreed on by partners throughout the industry, tool vendors, and
legal teams.  For further information see https://spdx.org/

The Linux kernel requires the precise SPDX identifier in all source files.
The valid identifiers used in the kernel are explained in the section
`License identifiers`_ and have been retrieved from the official SPDX
license list at https://spdx.org/licenses/ along with the license texts.

License identifier syntax
-------------------------

1. Placement:

   The SPDX license identifier in kernel files shall be added at the first
   possible line in a file which can contain a comment.  For the majority
   of files this is the first line, except for scripts which require the
   '#!PATH_TO_INTERPRETER' in the first line.  For those scripts the SPDX
   identifier goes into the second line.

|

2. Style:

   The SPDX license identifier is added in form of a comment.  The comment
   style depends on the file type::

      C source:	// SPDX-License-Identifier: <SPDX License Expression>
      C header:	/* SPDX-License-Identifier: <SPDX License Expression> */
      ASM:	/* SPDX-License-Identifier: <SPDX License Expression> */
      scripts:	# SPDX-License-Identifier: <SPDX License Expression>
      .rst:	.. SPDX-License-Identifier: <SPDX License Expression>
      .dts{i}:	// SPDX-License-Identifier: <SPDX License Expression>

   If a specific tool cannot handle the standard comment style, then the
   appropriate comment mechanism which the tool accepts shall be used. This
   is the reason for having the "/\* \*/" style comment in C header
   files. There was build breakage observed with generated .lds files where
   'ld' failed to parse the C++ comment. This has been fixed by now, but
   there are still older assembler tools which cannot handle C++ style
   comments.

|

3. Syntax:

   A <SPDX License Expression> is either an SPDX short form license
   identifier found on the SPDX License List, or the combination of two
   SPDX short form license identifiers separated by "WITH" when a license
   exception applies. When multiple licenses apply, an expression consists
   of keywords "AND", "OR" separating sub-expressions and surrounded by
   "(", ")" .

   License identifiers for licenses like [L]GPL with the 'or later' option
   are constructed by using a "+" for indicating the 'or later' option.::

      // SPDX-License-Identifier: GPL-2.0+
      // SPDX-License-Identifier: LGPL-2.1+

   WITH should be used when there is a modifier to a license needed.
   For example, the linux kernel UAPI files use the expression::

      // SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
      // SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note

   Other examples using WITH exceptions found in the kernel are::

      // SPDX-License-Identifier: GPL-2.0 WITH mif-exception
      // SPDX-License-Identifier: GPL-2.0+ WITH GCC-exception-2.0

   Exceptions can only be used with particular License identifiers. The
   valid License identifiers are listed in the tags of the exception text
   file. For details see the point `Exceptions`_ in the chapter `License
   identifiers`_.

   OR should be used if the file is dual licensed and only one license is
   to be selected.  For example, some dtsi files are available under dual
   licenses::

      // SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

   Examples from the kernel for license expressions in dual licensed files::

      // SPDX-License-Identifier: GPL-2.0 OR MIT
      // SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
      // SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
      // SPDX-License-Identifier: GPL-2.0 OR MPL-1.1
      // SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT
      // SPDX-License-Identifier: GPL-1.0+ OR BSD-3-Clause OR OpenSSL

   AND should be used if the file has multiple licenses whose terms all
   apply to use the file. For example, if code is inherited from another
   project and permission has been given to put it in the kernel, but the
   original license terms need to remain in effect::

      // SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) AND MIT

   Another other example where both sets of license terms need to be
   adhered to is::

      // SPDX-License-Identifier: GPL-1.0+ AND LGPL-2.1+

License identifiers
-------------------

The licenses currently used, as well as the licenses for code added to the
kernel, can be broken down into:

1. _`Preferred licenses`:

   Whenever possible these licenses should be used as they are known to be
   fully compatible and widely used.  These licenses are available from the
   directory::

      LICENSES/preferred/

   in the kernel source tree.

   The files in this directory contain the full license text and
   `Metatags`_.  The file names are identical to the SPDX license
   identifier which shall be used for the license in source files.

   Examples::

      LICENSES/preferred/GPL-2.0

   Contains the GPL version 2 license text and the required metatags::

      LICENSES/preferred/MIT

   Contains the MIT license text and the required metatags

   _`Metatags`:

   The following meta tags must be available in a license file:

   - Valid-License-Identifier:

     One or more lines which declare which License Identifiers are valid
     inside the project to reference this particular license text.  Usually
     this is a single valid identifier, but e.g. for licenses with the 'or
     later' options two identifiers are valid.

   - SPDX-URL:

     The URL of the SPDX page which contains additional information related
     to the license.

   - Usage-Guidance:

     Freeform text for usage advice. The text must include correct examples
     for the SPDX license identifiers as they should be put into source
     files according to the `License identifier syntax`_ guidelines.

   - License-Text:

     All text after this tag is treated as the original license text

   File format examples::

      Valid-License-Identifier: GPL-2.0
      Valid-License-Identifier: GPL-2.0+
      SPDX-URL: https://spdx.org/licenses/GPL-2.0.html
      Usage-Guide:
        To use this license in source code, put one of the following SPDX
	tag/value pairs into a comment according to the placement
	guidelines in the licensing rules documentation.
	For 'GNU General Public License (GPL) version 2 only' use:
	  SPDX-License-Identifier: GPL-2.0
	For 'GNU General Public License (GPL) version 2 or any later version' use:
	  SPDX-License-Identifier: GPL-2.0+
      License-Text:
        Full license text

   ::

      SPDX-License-Identifier: MIT
      SPDX-URL: https://spdx.org/licenses/MIT.html
      Usage-Guide:
	To use this license in source code, put the following SPDX
	tag/value pair into a comment according to the placement
	guidelines in the licensing rules documentation.
	  SPDX-License-Identifier: MIT
      License-Text:
        Full license text

|

2. Deprecated licenses:

   These licenses should only be used for existing code or for importing
   code from a different project.  These licenses are available from the
   directory::

      LICENSES/deprecated/

   in the kernel source tree.

   The files in this directory contain the full license text and
   `Metatags`_.  The file names are identical to the SPDX license
   identifier which shall be used for the license in source files.

   Examples::

      LICENSES/deprecated/ISC

   Contains the Internet Systems Consortium license text and the required
   metatags::

      LICENSES/deprecated/GPL-1.0

   Contains the GPL version 1 license text and the required metatags.

   Metatags:

   The metatag requirements for 'other' licenses are identical to the
   requirements of the `Preferred licenses`_.

   File format example::

      Valid-License-Identifier: ISC
      SPDX-URL: https://spdx.org/licenses/ISC.html
      Usage-Guide:
        Usage of this license in the kernel for new code is discouraged
	and it should solely be used for importing code from an already
	existing project.
        To use this license in source code, put the following SPDX
	tag/value pair into a comment according to the placement
	guidelines in the licensing rules documentation.
	  SPDX-License-Identifier: ISC
      License-Text:
        Full license text

|

3. Dual Licensing Only

   These licenses should only be used to dual license code with another
   license in addition to a preferred license.  These licenses are available
   from the directory::

      LICENSES/dual/

   in the kernel source tree.

   The files in this directory contain the full license text and
   `Metatags`_.  The file names are identical to the SPDX license
   identifier which shall be used for the license in source files.

   Examples::

      LICENSES/dual/MPL-1.1

   Contains the Mozilla Public License version 1.1 license text and the
   required metatags::

      LICENSES/dual/Apache-2.0

   Contains the Apache License version 2.0 license text and the required
   metatags.

   Metatags:

   The metatag requirements for 'other' licenses are identical to the
   requirements of the `Preferred licenses`_.

   File format example::

      Valid-License-Identifier: MPL-1.1
      SPDX-URL: https://spdx.org/licenses/MPL-1.1.html
      Usage-Guide:
        Do NOT use. The MPL-1.1 is not GPL2 compatible. It may only be used for
        dual-licensed files where the other license is GPL2 compatible.
        If you end up using this it MUST be used together with a GPL2 compatible
        license using "OR".
        To use the Mozilla Public License version 1.1 put the following SPDX
        tag/value pair into a comment according to the placement guidelines in
        the licensing rules documentation:
      SPDX-License-Identifier: MPL-1.1
      License-Text:
        Full license text

|

4. _`Exceptions`:

   Some licenses can be amended with exceptions which grant certain rights
   which the original license does not.  These exceptions are available
   from the directory::

      LICENSES/exceptions/

   in the kernel source tree.  The files in this directory contain the full
   exception text and the required `Exception Metatags`_.

   Examples::

      LICENSES/exceptions/Linux-syscall-note

   Contains the Linux syscall exception as documented in the COPYING
   file of the Linux kernel, which is used for UAPI header files.
   e.g. /\* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note \*/::

      LICENSES/exceptions/GCC-exception-2.0

   Contains the GCC 'linking exception' which allows to link any binary
   independent of its license against the compiled version of a file marked
   with this exception. This is required for creating runnable executables
   from source code which is not compatible with the GPL.

   _`Exception Metatags`:

   The following meta tags must be available in an exception file:

   - SPDX-Exception-Identifier:

     One exception identifier which can be used with SPDX license
     identifiers.

   - SPDX-URL:

     The URL of the SPDX page which contains additional information related
     to the exception.

   - SPDX-Licenses:

     A comma separated list of SPDX license identifiers for which the
     exception can be used.

   - Usage-Guidance:

     Freeform text for usage advice. The text must be followed by correct
     examples for the SPDX license identifiers as they should be put into
     source files according to the `License identifier syntax`_ guidelines.

   - Exception-Text:

     All text after this tag is treated as the original exception text

   File format examples::

      SPDX-Exception-Identifier: Linux-syscall-note
      SPDX-URL: https://spdx.org/licenses/Linux-syscall-note.html
      SPDX-Licenses: GPL-2.0, GPL-2.0+, GPL-1.0+, LGPL-2.0, LGPL-2.0+, LGPL-2.1, LGPL-2.1+
      Usage-Guidance:
        This exception is used together with one of the above SPDX-Licenses
	to mark user-space API (uapi) header files so they can be included
	into non GPL compliant user-space application code.
        To use this exception add it with the keyword WITH to one of the
	identifiers in the SPDX-Licenses tag:
	  SPDX-License-Identifier: <SPDX-License> WITH Linux-syscall-note
      Exception-Text:
        Full exception text

   ::

      SPDX-Exception-Identifier: GCC-exception-2.0
      SPDX-URL: https://spdx.org/licenses/GCC-exception-2.0.html
      SPDX-Licenses: GPL-2.0, GPL-2.0+
      Usage-Guidance:
        The "GCC Runtime Library exception 2.0" is used together with one
	of the above SPDX-Licenses for code imported from the GCC runtime
	library.
        To use this exception add it with the keyword WITH to one of the
	identifiers in the SPDX-Licenses tag:
	  SPDX-License-Identifier: <SPDX-License> WITH GCC-exception-2.0
      Exception-Text:
        Full exception text


All SPDX license identifiers and exceptions must have a corresponding file
in the LICENSES subdirectories. This is required to allow tool
verification (e.g. checkpatch.pl) and to have the licenses ready to read
and extract right from the source, which is recommended by various FOSS
organizations, e.g. the `FSFE REUSE initiative <https://reuse.software/>`_.

_`MODULE_LICENSE`
-----------------

   Loadable kernel modules also require a MODULE_LICENSE() tag. This tag is
   neither a replacement for proper source code license information
   (SPDX-License-Identifier) nor in any way relevant for expressing or
   determining the exact license under which the source code of the module
   is provided.

   The sole purpose of this tag is to provide sufficient information
   whether the module is free software or proprietary for the kernel
   module loader and for user space tools.

   The valid license strings for MODULE_LICENSE() are:

    ============================= =============================================
    "GPL"			  Module is licensed under GPL version 2. This
				  does not express any distinction between
				  GPL-2.0-only or GPL-2.0-or-later. The exact
				  license information can only be determined
				  via the license information in the
				  corresponding source files.

    "GPL v2"			  Same as "GPL". It exists for historic
				  reasons.

    "GPL and additional rights"   Historical variant of expressing that the
				  module source is dual licensed under a
				  GPL v2 variant and MIT license. Please do
				  not use in new code.

    "Dual MIT/GPL"		  The correct way of expressing that the
				  module is dual licensed under a GPL v2
				  variant or MIT license choice.

    "Dual BSD/GPL"		  The module is dual licensed under a GPL v2
				  variant or BSD license choice. The exact
				  variant of the BSD license can only be
				  determined via the license information
				  in the corresponding source files.

    "Dual MPL/GPL"		  The module is dual licensed under a GPL v2
				  variant or Mozilla Public License (MPL)
				  choice. The exact variant of the MPL
				  license can only be determined via the
				  license information in the corresponding
				  source files.

    "Proprietary"		  The module is under a proprietary license.
				  This string is solely for proprietary third
				  party modules and cannot be used for modules
				  which have their source code in the kernel
				  tree. Modules tagged that way are tainting
				  the kernel with the 'P' flag when loaded and
				  the kernel module loader refuses to link such
				  modules against symbols which are exported
				  with EXPORT_SYMBOL_GPL().
    ============================= =============================================



