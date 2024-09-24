============
Introduction
============

The Linux DRM layer contains code intended to support the needs of
complex graphics devices, usually containing programmable pipelines well
suited to 3D graphics acceleration. Graphics drivers in the kernel may
make use of DRM functions to make tasks like memory management,
interrupt handling and DMA easier, and provide a uniform interface to
applications.

A note on versions: this guide covers features found in the DRM tree,
including the TTM memory manager, output configuration and mode setting,
and the new vblank internals, in addition to all the regular features
found in current kernels.

[Insert diagram of typical DRM stack here]

Style Guidelines
================

For consistency this documentation uses American English. Abbreviations
are written as all-uppercase, for example: DRM, KMS, IOCTL, CRTC, and so
on. To aid in reading, documentations make full use of the markup
characters kerneldoc provides: @parameter for function parameters,
@member for structure members (within the same structure), &struct structure to
reference structures and function() for functions. These all get automatically
hyperlinked if kerneldoc for the referenced objects exists. When referencing
entries in function vtables (and structure members in general) please use
&vtable_name.vfunc. Unfortunately this does not yet yield a direct link to the
member, only the structure.

Except in special situations (to separate locked from unlocked variants)
locking requirements for functions aren't documented in the kerneldoc.
Instead locking should be check at runtime using e.g.
``WARN_ON(!mutex_is_locked(...));``. Since it's much easier to ignore
documentation than runtime noise this provides more value. And on top of
that runtime checks do need to be updated when the locking rules change,
increasing the chances that they're correct. Within the documentation
the locking rules should be explained in the relevant structures: Either
in the comment for the lock explaining what it protects, or data fields
need a note about which lock protects them, or both.

Functions which have a non-\ ``void`` return value should have a section
called "Returns" explaining the expected return values in different
cases and their meanings. Currently there's no consensus whether that
section name should be all upper-case or not, and whether it should end
in a colon or not. Go with the file-local style. Other common section
names are "Notes" with information for dangerous or tricky corner cases,
and "FIXME" where the interface could be cleaned up.

Also read the :ref:`guidelines for the kernel documentation at large <doc_guide>`.

Documentation Requirements for kAPI
-----------------------------------

All kernel APIs exported to other modules must be documented, including their
datastructures and at least a short introductory section explaining the overall
concepts. Documentation should be put into the code itself as kerneldoc comments
as much as reasonable.

Do not blindly document everything, but document only what's relevant for driver
authors: Internal functions of drm.ko and definitely static functions should not
have formal kerneldoc comments. Use normal C comments if you feel like a comment
is warranted. You may use kerneldoc syntax in the comment, but it shall not
start with a /** kerneldoc marker. Similar for data structures, annotate
anything entirely private with ``/* private: */`` comments as per the
documentation guide.

Getting Started
===============

Developers interested in helping out with the DRM subsystem are very welcome.
Often people will resort to sending in patches for various issues reported by
checkpatch or sparse. We welcome such contributions.

Anyone looking to kick it up a notch can find a list of janitorial tasks on
the :ref:`TODO list <todo>`.

Contribution Process
====================

Mostly the DRM subsystem works like any other kernel subsystem, see :ref:`the
main process guidelines and documentation <process_index>` for how things work.
Here we just document some of the specialities of the GPU subsystem.

Feature Merge Deadlines
-----------------------

All feature work must be in the linux-next tree by the -rc6 release of the
current release cycle, otherwise they must be postponed and can't reach the next
merge window. All patches must have landed in the drm-next tree by latest -rc7,
but if your branch is not in linux-next then this must have happened by -rc6
already.

After that point only bugfixes (like after the upstream merge window has closed
with the -rc1 release) are allowed. No new platform enabling or new drivers are
allowed.

This means that there's a blackout-period of about one month where feature work
can't be merged. The recommended way to deal with that is having a -next tree
that's always open, but making sure to not feed it into linux-next during the
blackout period. As an example, drm-misc works like that.

Code of Conduct
---------------

As a freedesktop.org project, dri-devel, and the DRM community, follows the
Contributor Covenant, found at: https://www.freedesktop.org/wiki/CodeOfConduct

Please conduct yourself in a respectful and civilised manner when
interacting with community members on mailing lists, IRC, or bug
trackers. The community represents the project as a whole, and abusive
or bullying behaviour is not tolerated by the project.

Simple DRM drivers to use as examples
=====================================

The DRM subsystem contains a lot of helper functions to ease writing drivers for
simple graphic devices. For example, the `drivers/gpu/drm/tiny/` directory has a
set of drivers that are simple enough to be implemented in a single source file.

These drivers make use of the `struct drm_simple_display_pipe_funcs`, that hides
any complexity of the DRM subsystem and just requires drivers to implement a few
functions needed to operate the device. This could be used for devices that just
need a display pipeline with one full-screen scanout buffer feeding one output.

The tiny DRM drivers are good examples to understand how DRM drivers should look
like. Since are just a few hundreds lines of code, they are quite easy to read.

External References
===================

Delving into a Linux kernel subsystem for the first time can be an overwhelming
experience, one needs to get familiar with all the concepts and learn about the
subsystem's internals, among other details.

To shallow the learning curve, this section contains a list of presentations
and documents that can be used to learn about DRM/KMS and graphics in general.

There are different reasons why someone might want to get into DRM: porting an
existing fbdev driver, write a DRM driver for a new hardware, fixing bugs that
could face when working on the graphics user-space stack, etc. For this reason,
the learning material covers many aspects of the Linux graphics stack. From an
overview of the kernel and user-space stacks to very specific topics.

The list is sorted in reverse chronological order, to keep the most up-to-date
material at the top. But all of them contain useful information, and it can be
valuable to go through older material to understand the rationale and context
in which the changes to the DRM subsystem were made.

Conference talks
----------------

* `An Overview of the Linux and Userspace Graphics Stack <https://www.youtube.com/watch?v=wjAJmqwg47k>`_ - Paul Kocialkowski (2020)
* `Getting pixels on screen on Linux: introduction to Kernel Mode Setting <https://www.youtube.com/watch?v=haes4_Xnc5Q>`_ - Simon Ser (2020)
* `Everything Great about Upstream Graphics <https://www.youtube.com/watch?v=kVzHOgt6WGE>`_ - Simona Vetter (2019)
* `An introduction to the Linux DRM subsystem <https://www.youtube.com/watch?v=LbDOCJcDRoo>`_ - Maxime Ripard (2017)
* `Embrace the Atomic (Display) Age <https://www.youtube.com/watch?v=LjiB_JeDn2M>`_ - Simona Vetter (2016)
* `Anatomy of an Atomic KMS Driver <https://www.youtube.com/watch?v=lihqR9sENpc>`_ - Laurent Pinchart (2015)
* `Atomic Modesetting for Drivers <https://www.youtube.com/watch?v=kl9suFgbTc8>`_ - Simona Vetter (2015)
* `Anatomy of an Embedded KMS Driver <https://www.youtube.com/watch?v=Ja8fM7rTae4>`_ - Laurent Pinchart (2013)

Slides and articles
-------------------

* `The Linux graphics stack in a nutshell, part 1 <https://lwn.net/Articles/955376/>`_ - Thomas Zimmermann (2023)
* `The Linux graphics stack in a nutshell, part 2 <https://lwn.net/Articles/955708/>`_ - Thomas Zimmermann (2023)
* `Understanding the Linux Graphics Stack <https://bootlin.com/doc/training/graphics/graphics-slides.pdf>`_ - Bootlin (2022)
* `DRM KMS overview <https://wiki.st.com/stm32mpu/wiki/DRM_KMS_overview>`_ - STMicroelectronics (2021)
* `Linux graphic stack <https://studiopixl.com/2017-05-13/linux-graphic-stack-an-overview>`_ - Nathan Gauër (2017)
* `Atomic mode setting design overview, part 1 <https://lwn.net/Articles/653071/>`_ - Simona Vetter (2015)
* `Atomic mode setting design overview, part 2 <https://lwn.net/Articles/653466/>`_ - Simona Vetter (2015)
* `The DRM/KMS subsystem from a newbie’s point of view <https://bootlin.com/pub/conferences/2014/elce/brezillon-drm-kms/brezillon-drm-kms.pdf>`_ - Boris Brezillon (2014)
* `A brief introduction to the Linux graphics stack <https://blogs.igalia.com/itoral/2014/07/29/a-brief-introduction-to-the-linux-graphics-stack/>`_ - Iago Toral (2014)
* `The Linux Graphics Stack <https://blog.mecheye.net/2012/06/the-linux-graphics-stack/>`_ - Jasper St. Pierre (2012)
