.. -*- coding: utf-8; mode: rst -*-

.. _open:

***************************
Opening and Closing Devices
***************************


Device Naming
=============

V4L2 drivers are implemented as kernel modules, loaded manually by the
system administrator or automatically when a device is first discovered.
The driver modules plug into the "videodev" kernel module. It provides
helper functions and a common application interface specified in this
document.

Each driver thus loaded registers one or more device nodes with major
number 81 and a minor number between 0 and 255. Minor numbers are
allocated dynamically unless the kernel is compiled with the kernel
option CONFIG_VIDEO_FIXED_MINOR_RANGES. In that case minor numbers
are allocated in ranges depending on the device node type (video, radio,
etc.).

Many drivers support "video_nr", "radio_nr" or "vbi_nr" module
options to select specific video/radio/vbi node numbers. This allows the
user to request that the device node is named e.g. /dev/video5 instead
of leaving it to chance. When the driver supports multiple devices of
the same type more than one device node number can be assigned,
separated by commas:

.. code-block:: none

   # modprobe mydriver video_nr=0,1 radio_nr=0,1

In ``/etc/modules.conf`` this may be written as:

::

    options mydriver video_nr=0,1 radio_nr=0,1

When no device node number is given as module option the driver supplies
a default.

Normally udev will create the device nodes in /dev automatically for
you. If udev is not installed, then you need to enable the
CONFIG_VIDEO_FIXED_MINOR_RANGES kernel option in order to be able to
correctly relate a minor number to a device node number. I.e., you need
to be certain that minor number 5 maps to device node name video5. With
this kernel option different device types have different minor number
ranges. These ranges are listed in :ref:`devices`.

The creation of character special files (with mknod) is a privileged
operation and devices cannot be opened by major and minor number. That
means applications cannot *reliable* scan for loaded or installed
drivers. The user must enter a device name, or the application can try
the conventional device names.


.. _related:

Related Devices
===============

Devices can support several functions. For example video capturing, VBI
capturing and radio support.

The V4L2 API creates different nodes for each of these functions.

The V4L2 API was designed with the idea that one device node could
support all functions. However, in practice this never worked: this
'feature' was never used by applications and many drivers did not
support it and if they did it was certainly never tested. In addition,
switching a device node between different functions only works when
using the streaming I/O API, not with the
:ref:`read() <func-read>`/\ :ref:`write() <func-write>` API.

Today each device node supports just one function.

Besides video input or output the hardware may also support audio
sampling or playback. If so, these functions are implemented as ALSA PCM
devices with optional ALSA audio mixer devices.

One problem with all these devices is that the V4L2 API makes no
provisions to find these related devices. Some really complex devices
use the Media Controller (see :ref:`media_controller`) which can be
used for this purpose. But most drivers do not use it, and while some
code exists that uses sysfs to discover related devices (see
libmedia_dev in the
`v4l-utils <http://git.linuxtv.org/cgit.cgi/v4l-utils.git/>`__ git
repository), there is no library yet that can provide a single API
towards both Media Controller-based devices and devices that do not use
the Media Controller. If you want to work on this please write to the
linux-media mailing list:
`https://linuxtv.org/lists.php <https://linuxtv.org/lists.php>`__.


Multiple Opens
==============

V4L2 devices can be opened more than once. [#f1]_ When this is supported
by the driver, users can for example start a "panel" application to
change controls like brightness or audio volume, while another
application captures video and audio. In other words, panel applications
are comparable to an ALSA audio mixer application. Just opening a V4L2
device should not change the state of the device. [#f2]_

Once an application has allocated the memory buffers needed for
streaming data (by calling the :ref:`VIDIOC_REQBUFS`
or :ref:`VIDIOC_CREATE_BUFS` ioctls, or
implicitly by calling the :ref:`read() <func-read>` or
:ref:`write() <func-write>` functions) that application (filehandle)
becomes the owner of the device. It is no longer allowed to make changes
that would affect the buffer sizes (e.g. by calling the
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl) and other applications are
no longer allowed to allocate buffers or start or stop streaming. The
EBUSY error code will be returned instead.

Merely opening a V4L2 device does not grant exclusive access. [#f3]_
Initiating data exchange however assigns the right to read or write the
requested type of data, and to change related properties, to this file
descriptor. Applications can request additional access privileges using
the priority mechanism described in :ref:`app-pri`.


Shared Data Streams
===================

V4L2 drivers should not support multiple applications reading or writing
the same data stream on a device by copying buffers, time multiplexing
or similar means. This is better handled by a proxy application in user
space.


Functions
=========

To open and close V4L2 devices applications use the
:ref:`open() <func-open>` and :ref:`close() <func-close>` function,
respectively. Devices are programmed using the
:ref:`ioctl() <func-ioctl>` function as explained in the following
sections.

.. [#f1]
   There are still some old and obscure drivers that have not been
   updated to allow for multiple opens. This implies that for such
   drivers :ref:`open() <func-open>` can return an ``EBUSY`` error code
   when the device is already in use.

.. [#f2]
   Unfortunately, opening a radio device often switches the state of the
   device to radio mode in many drivers. This behavior should be fixed
   eventually as it violates the V4L2 specification.

.. [#f3]
   Drivers could recognize the ``O_EXCL`` open flag. Presently this is
   not required, so applications cannot know if it really works.
