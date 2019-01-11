.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _query-dvb-frontend-info:

*****************************
Querying frontend information
*****************************

Usually, the first thing to do when the frontend is opened is to check
the frontend capabilities. This is done using
:ref:`FE_GET_INFO`. This ioctl will enumerate the
Digital TV API version and other characteristics about the frontend, and can
be opened either in read only or read/write mode.
