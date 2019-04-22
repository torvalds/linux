.. SPDX-License-Identifier: GPL-2.0

The Android binderfs Filesystem
===============================

Android binderfs is a filesystem for the Android binder IPC mechanism.  It
allows to dynamically add and remove binder devices at runtime.  Binder devices
located in a new binderfs instance are independent of binder devices located in
other binderfs instances.  Mounting a new binderfs instance makes it possible
to get a set of private binder devices.

Mounting binderfs
-----------------

Android binderfs can be mounted with::

  mkdir /dev/binderfs
  mount -t binder binder /dev/binderfs

at which point a new instance of binderfs will show up at ``/dev/binderfs``.
In a fresh instance of binderfs no binder devices will be present.  There will
only be a ``binder-control`` device which serves as the request handler for
binderfs. Mounting another binderfs instance at a different location will
create a new and separate instance from all other binderfs mounts.  This is
identical to the behavior of e.g. ``devpts`` and ``tmpfs``. The Android
binderfs filesystem can be mounted in user namespaces.

Options
-------
max
  binderfs instances can be mounted with a limit on the number of binder
  devices that can be allocated. The ``max=<count>`` mount option serves as
  a per-instance limit. If ``max=<count>`` is set then only ``<count>`` number
  of binder devices can be allocated in this binderfs instance.

Allocating binder Devices
-------------------------

.. _ioctl: http://man7.org/linux/man-pages/man2/ioctl.2.html

To allocate a new binder device in a binderfs instance a request needs to be
sent through the ``binder-control`` device node.  A request is sent in the form
of an `ioctl() <ioctl_>`_.

What a program needs to do is to open the ``binder-control`` device node and
send a ``BINDER_CTL_ADD`` request to the kernel.  Users of binderfs need to
tell the kernel which name the new binder device should get.  By default a name
can only contain up to ``BINDERFS_MAX_NAME`` chars including the terminating
zero byte.

Once the request is made via an `ioctl() <ioctl_>`_ passing a ``struct
binder_device`` with the name to the kernel it will allocate a new binder
device and return the major and minor number of the new device in the struct
(This is necessary because binderfs allocates a major device number
dynamically.).  After the `ioctl() <ioctl_>`_ returns there will be a new
binder device located under /dev/binderfs with the chosen name.

Deleting binder Devices
-----------------------

.. _unlink: http://man7.org/linux/man-pages/man2/unlink.2.html
.. _rm: http://man7.org/linux/man-pages/man1/rm.1.html

Binderfs binder devices can be deleted via `unlink() <unlink_>`_.  This means
that the `rm() <rm_>`_ tool can be used to delete them. Note that the
``binder-control`` device cannot be deleted since this would make the binderfs
instance unuseable.  The ``binder-control`` device will be deleted when the
binderfs instance is unmounted and all references to it have been dropped.
