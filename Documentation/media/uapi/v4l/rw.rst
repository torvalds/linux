.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _rw:

**********
Read/Write
**********

Input and output devices support the :ref:`read() <func-read>` and
:ref:`write() <func-write>` function, respectively, when the
``V4L2_CAP_READWRITE`` flag in the ``capabilities`` field of struct
:c:type:`v4l2_capability` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl is set.

Drivers may need the CPU to copy the data, but they may also support DMA
to or from user memory, so this I/O method is not necessarily less
efficient than other methods merely exchanging buffer pointers. It is
considered inferior though because no meta-information like frame
counters or timestamps are passed. This information is necessary to
recognize frame dropping and to synchronize with other data streams.
However this is also the simplest I/O method, requiring little or no
setup to exchange data. It permits command line stunts like this (the
vidctrl tool is fictitious):


.. code-block:: none

    $ vidctrl /dev/video --input=0 --format=YUYV --size=352x288
    $ dd if=/dev/video of=myimage.422 bs=202752 count=1

To read from the device applications use the :ref:`read() <func-read>`
function, to write the :ref:`write() <func-write>` function. Drivers
must implement one I/O method if they exchange data with applications,
but it need not be this. [#f1]_ When reading or writing is supported, the
driver must also support the :ref:`select() <func-select>` and
:ref:`poll() <func-poll>` function. [#f2]_

.. [#f1]
   It would be desirable if applications could depend on drivers
   supporting all I/O interfaces, but as much as the complex memory
   mapping I/O can be inadequate for some devices we have no reason to
   require this interface, which is most useful for simple applications
   capturing still images.

.. [#f2]
   At the driver level :ref:`select() <func-select>` and :ref:`poll() <func-poll>` are
   the same, and :ref:`select() <func-select>` is too important to be optional.
