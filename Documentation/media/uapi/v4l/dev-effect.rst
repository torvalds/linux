.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _effect:

************************
Effect Devices Interface
************************

.. note::
    This interface has been be suspended from the V4L2 API.
    The implementation for such effects should be done
    via mem2mem devices.

A V4L2 video effect device can do image effects, filtering, or combine
two or more images or image streams. For example video transitions or
wipes. Applications send data to be processed and receive the result
data either with :ref:`read() <func-read>` and
:ref:`write() <func-write>` functions, or through the streaming I/O
mechanism.

[to do]
