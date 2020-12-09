.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _media-controller-intro:

Introduction
============

Media devices increasingly handle multiple related functions. Many USB
cameras include microphones, video capture hardware can also output
video, or SoC camera interfaces also perform memory-to-memory operations
similar to video codecs.

Independent functions, even when implemented in the same hardware, can
be modelled as separate devices. A USB camera with a microphone will be
presented to userspace applications as V4L2 and ALSA capture devices.
The devices' relationships (when using a webcam, end-users shouldn't
have to manually select the associated USB microphone), while not made
available directly to applications by the drivers, can usually be
retrieved from sysfs.

With more and more advanced SoC devices being introduced, the current
approach will not scale. Device topologies are getting increasingly
complex and can't always be represented by a tree structure. Hardware
blocks are shared between different functions, creating dependencies
between seemingly unrelated devices.

Kernel abstraction APIs such as V4L2 and ALSA provide means for
applications to access hardware parameters. As newer hardware expose an
increasingly high number of those parameters, drivers need to guess what
applications really require based on limited information, thereby
implementing policies that belong to userspace.

The media controller API aims at solving those problems.
