The Elftoolchain Project
========================

.. contents:: Table of Contents

Description
-----------

This software implements essential compilation tools and libraries for:

- managing program objects conforming to the ELF_ object format, and
- for managing DWARF_ debugging information in ELF objects.

The project currently implements the following utilities and
libraries:

=========== ============================================
Name        Description
=========== ============================================
ar          Archive manager.
addr2line   Debug tool.
brandelf    Manage the ELF brand on executables.
c++filt     Translate encoded symbols.
elfcopy     Copy and translate between object formats.
elfdump     Diagnostic tool.
findtextrel Find undesired text relocations.
libdwarf    DWARF access library.
libelf      ELF access library.
mcs         Manage comment sections.
nm          List symbols in an ELF object.
ranlib      Add archive symbol tables to an archive.
readelf     Display ELF information.
size        List object sizes.
strings     Extract printable strings.
strip       Discard information from ELF objects.
=========== ============================================

.. _ELF: http://en.wikipedia.org/wiki/Executable_and_Linkable_Format
.. _DWARF: http://www.dwarfstd.org/


Project Documentation
---------------------

- Release notes for released versions of this software are present in
  the file ``RELEASE-NOTES`` in the current directory.
- The file ``INSTALL`` in the current directory contains instructions
  on building and installing this software.
- Reference documentation in the form of manual pages is provided for
  the utilities and libraries developed by the project.
- Additional tutorial documentation is present in the
  ``documentation`` directory.


Tracking Ongoing Development
----------------------------

The project uses subversion_ for its version control system.

.. _subversion: https://subversion.apache.org/

The subversion branch for the current set of sources may be accessed
at the following URL::

    https://elftoolchain.svn.sourceforge.net/svnroot/elftoolchain/trunk

The project's source tree may be checked out from its repository by
using the ``svn checkout`` command::

    % svn checkout https://elftoolchain.svn.sourceforge.net/svnroot/elftoolchain/trunk

Checked-out sources may be kept upto-date by running ``svn update``
inside the source directory::

    % svn update


Instructions on building and installing the software are given in the
file ``INSTALL`` in the current directory.

Downloading Released Software
-----------------------------

Released versions of the project's software may also be downloaded
from SourceForge's `file release system`_.

.. _file release system: http://sourceforge.net/projects/elftoolchain/files/

Copyright and License
---------------------

This code is copyright its authors, and is distributed under the `BSD
License`_.

.. _BSD License: http://www.opensource.org/licenses/bsd-license.php


Developer Community
-------------------

The project's developers may be contacted using the mailing list:
``<elftoolchain-developers@lists.sourceforge.net>``.


Reporting Bugs
--------------

Please use our `Trac instance`_ for viewing existing bug reports and
for submitting new bug reports.

.. _`Trac instance`: http://sourceforge.net/apps/trac/elftoolchain/report


Additional Information
----------------------

Additional information about the project may be found on the `project
website`_.

.. _project website:  http://elftoolchain.sourceforge.net/

.. $Id: README.rst 3656 2018-12-26 09:46:24Z jkoshy $

.. Local Variables:
.. mode: rst
.. End:
