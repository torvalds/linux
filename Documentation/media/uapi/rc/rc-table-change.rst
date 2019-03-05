.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _Remote_controllers_table_change:

*******************************************
Changing default Remote Controller mappings
*******************************************

The event interface provides two ioctls to be used against the
/dev/input/event device, to allow changing the default keymapping.

This program demonstrates how to replace the keymap tables.


.. toctree::
    :maxdepth: 1

    keytable.c
