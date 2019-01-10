.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _v4l2-selections-common:

Common selection definitions
============================

While the :ref:`V4L2 selection API <selection-api>` and
:ref:`V4L2 subdev selection APIs <v4l2-subdev-selections>` are very
similar, there's one fundamental difference between the two. On
sub-device API, the selection rectangle refers to the media bus format,
and is bound to a sub-device's pad. On the V4L2 interface the selection
rectangles refer to the in-memory pixel format.

This section defines the common definitions of the selection interfaces
on the two APIs.


.. toctree::
    :maxdepth: 1

    v4l2-selection-targets
    v4l2-selection-flags
