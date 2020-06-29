.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _dvb_demux:

#######################
Digital TV Demux Device
#######################

The Digital TV demux device controls the MPEG-TS filters for the
digital TV. If the driver and hardware supports, those filters are
implemented at the hardware. Otherwise, the Kernel provides a software
emulation.

It can be accessed through ``/dev/adapter?/demux?``. Data types and and
ioctl definitions can be accessed by including ``linux/dvb/dmx.h`` in
your application.


.. toctree::
    :maxdepth: 1

    dmx_types
    dmx_fcalls
