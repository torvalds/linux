.. _todo:

=========
TODO list
=========

This section contains a list of smaller janitorial tasks in the kernel DRM
graphics subsystem useful as newbie projects. Or for slow rainy days.

Subsystem-wide refactorings
===========================

De-midlayer drivers
-------------------

With the recent ``drm_bus`` cleanup patches for 3.17 it is no longer required
to have a ``drm_bus`` structure set up. Drivers can directly set up the
``drm_device`` structure instead of relying on bus methods in ``drm_usb.c``
and ``drm_platform.c``. The goal is to get rid of the driver's ``->load`` /
``->unload`` callbacks and open-code the load/unload sequence properly, using
the new two-stage ``drm_device`` setup/teardown.

Once all existing drivers are converted we can also remove those bus support
files for USB and platform devices.

All you need is a GPU for a non-converted driver (currently almost all of
them, but also all the virtual ones used by KVM, so everyone qualifies).

Contact: Daniel Vetter, Thierry Reding, respective driver maintainers

Switch from reference/unreference to get/put
--------------------------------------------

For some reason DRM core uses ``reference``/``unreference`` suffixes for
refcounting functions, but kernel uses ``get``/``put`` (e.g.
``kref_get``/``put()``). It would be good to switch over for consistency, and
it's shorter. Needs to be done in 3 steps for each pair of functions:

* Create new ``get``/``put`` functions, define the old names as compatibility
  wrappers
* Switch over each file/driver using a cocci-generated spatch.
* Once all users of the old names are gone, remove them.

This way drivers/patches in the progress of getting merged won't break.

Contact: Daniel Vetter

Convert existing KMS drivers to atomic modesetting
--------------------------------------------------

3.19 has the atomic modeset interfaces and helpers, so drivers can now be
converted over. Modern compositors like Wayland or Surfaceflinger on Android
really want an atomic modeset interface, so this is all about the bright
future.

There is a conversion guide for atomic and all you need is a GPU for a
non-converted driver (again virtual HW drivers for KVM are still all
suitable).

As part of this drivers also need to convert to universal plane (which means
exposing primary & cursor as proper plane objects). But that's much easier to
do by directly using the new atomic helper driver callbacks.

Contact: Daniel Vetter, respective driver maintainers

Clean up the clipped coordination confusion around planes
---------------------------------------------------------

We have a helper to get this right with drm_plane_helper_check_update(), but
it's not consistently used. This should be fixed, preferrably in the atomic
helpers (and drivers then moved over to clipped coordinates). Probably the
helper should also be moved from drm_plane_helper.c to the atomic helpers, to
avoid confusion - the other helpers in that file are all deprecated legacy
helpers.

Contact: Ville Syrjälä, Daniel Vetter, driver maintainers

Implement deferred fbdev setup in the helper
--------------------------------------------

Many (especially embedded drivers) want to delay fbdev setup until there's a
real screen plugged in. This is to avoid the dreaded fallback to the low-res
fbdev default. Many drivers have a hacked-up (and often broken) version of this,
better to do it once in the shared helpers. Thierry has a patch series, but that
one needs to be rebased and final polish applied.

Contact: Thierry Reding, Daniel Vetter, driver maintainers

Convert early atomic drivers to async commit helpers
----------------------------------------------------

For the first year the atomic modeset helpers didn't support asynchronous /
nonblocking commits, and every driver had to hand-roll them. This is fixed
now, but there's still a pile of existing drivers that easily could be
converted over to the new infrastructure.

One issue with the helpers is that they require that drivers handle completion
events for atomic commits correctly. But fixing these bugs is good anyway.

Contact: Daniel Vetter, respective driver maintainers

Fallout from atomic KMS
-----------------------

``drm_atomic_helper.c`` provides a batch of functions which implement legacy
IOCTLs on top of the new atomic driver interface. Which is really nice for
gradual conversion of drivers, but unfortunately the semantic mismatches are
a bit too severe. So there's some follow-up work to adjust the function
interfaces to fix these issues:

* atomic needs the lock acquire context. At the moment that's passed around
  implicitly with some horrible hacks, and it's also allocate with
  ``GFP_NOFAIL`` behind the scenes. All legacy paths need to start allocating
  the acquire context explicitly on stack and then also pass it down into
  drivers explicitly so that the legacy-on-atomic functions can use them.

* A bunch of the vtable hooks are now in the wrong place: DRM has a split
  between core vfunc tables (named ``drm_foo_funcs``), which are used to
  implement the userspace ABI. And then there's the optional hooks for the
  helper libraries (name ``drm_foo_helper_funcs``), which are purely for
  internal use. Some of these hooks should be move from ``_funcs`` to
  ``_helper_funcs`` since they are not part of the core ABI. There's a
  ``FIXME`` comment in the kerneldoc for each such case in ``drm_crtc.h``.

* There's a new helper ``drm_atomic_helper_best_encoder()`` which could be
  used by all atomic drivers which don't select the encoder for a given
  connector at runtime. That's almost all of them, and would allow us to get
  rid of a lot of ``best_encoder`` boilerplate in drivers.

Contact: Daniel Vetter

Get rid of dev->struct_mutex from GEM drivers
---------------------------------------------

``dev->struct_mutex`` is the Big DRM Lock from legacy days and infested
everything. Nowadays in modern drivers the only bit where it's mandatory is
serializing GEM buffer object destruction. Which unfortunately means drivers
have to keep track of that lock and either call ``unreference`` or
``unreference_locked`` depending upon context.

Core GEM doesn't have a need for ``struct_mutex`` any more since kernel 4.8,
and there's a ``gem_free_object_unlocked`` callback for any drivers which are
entirely ``struct_mutex`` free.

For drivers that need ``struct_mutex`` it should be replaced with a driver-
private lock. The tricky part is the BO free functions, since those can't
reliably take that lock any more. Instead state needs to be protected with
suitable subordinate locks or some cleanup work pushed to a worker thread. For
performance-critical drivers it might also be better to go with a more
fine-grained per-buffer object and per-context lockings scheme. Currently the
following drivers still use ``struct_mutex``: ``msm``, ``omapdrm`` and
``udl``.

Contact: Daniel Vetter

Core refactorings
=================

Use new IDR deletion interface to clean up drm_gem_handle_delete()
------------------------------------------------------------------

See the "This is gross" comment -- apparently the IDR system now can return an
error code instead of oopsing.

Clean up the DRM header mess
----------------------------

Currently the DRM subsystem has only one global header, ``drmP.h``. This is
used both for functions exported to helper libraries and drivers and functions
only used internally in the ``drm.ko`` module. The goal would be to move all
header declarations not needed outside of ``drm.ko`` into
``drivers/gpu/drm/drm_*_internal.h`` header files. ``EXPORT_SYMBOL`` also
needs to be dropped for these functions.

This would nicely tie in with the below task to create kerneldoc after the API
is cleaned up. Or with the "hide legacy cruft better" task.

Note that this is well in progress, but ``drmP.h`` is still huge. The updated
plan is to switch to per-file driver API headers, which will also structure
the kerneldoc better. This should also allow more fine-grained ``#include``
directives.

Contact: Daniel Vetter

Add missing kerneldoc for exported functions
--------------------------------------------

The DRM reference documentation is still lacking kerneldoc in a few areas. The
task would be to clean up interfaces like moving functions around between
files to better group them and improving the interfaces like dropping return
values for functions that never fail. Then write kerneldoc for all exported
functions and an overview section and integrate it all into the drm DocBook.

See https://dri.freedesktop.org/docs/drm/ for what's there already.

Contact: Daniel Vetter

Hide legacy cruft better
------------------------

Way back DRM supported only drivers which shadow-attached to PCI devices with
userspace or fbdev drivers setting up outputs. Modern DRM drivers take charge
of the entire device, you can spot them with the DRIVER_MODESET flag.

Unfortunately there's still large piles of legacy code around which needs to
be hidden so that driver writers don't accidentally end up using it. And to
prevent security issues in those legacy IOCTLs from being exploited on modern
drivers. This has multiple possible subtasks:

* Make sure legacy IOCTLs can't be used on modern drivers.
* Extract support code for legacy features into a ``drm-legacy.ko`` kernel
  module and compile it only when one of the legacy drivers is enabled.
* Extract legacy functions into their own headers and remove it that from the
  monolithic ``drmP.h`` header.
* Remove any lingering cruft from the OS abstraction layer from modern
  drivers.

This is mostly done, the only thing left is to split up ``drm_irq.c`` into
legacy cruft and the parts needed by modern KMS drivers.

Contact: Daniel Vetter

Make panic handling work
------------------------

This is a really varied tasks with lots of little bits and pieces:

* The panic path can't be tested currently, leading to constant breaking. The
  main issue here is that panics can be triggered from hardirq contexts and
  hence all panic related callback can run in hardirq context. It would be
  awesome if we could test at least the fbdev helper code and driver code by
  e.g. trigger calls through drm debugfs files. hardirq context could be
  achieved by using an IPI to the local processor.

* There's a massive confusion of different panic handlers. DRM fbdev emulation
  helpers have one, but on top of that the fbcon code itself also has one. We
  need to make sure that they stop fighting over each another.

* ``drm_can_sleep()`` is a mess. It hides real bugs in normal operations and
  isn't a full solution for panic paths. We need to make sure that it only
  returns true if there's a panic going on for real, and fix up all the
  fallout.

* The panic handler must never sleep, which also means it can't ever
  ``mutex_lock()``. Also it can't grab any other lock unconditionally, not
  even spinlocks (because NMI and hardirq can panic too). We need to either
  make sure to not call such paths, or trylock everything. Really tricky.

* For the above locking troubles reasons it's pretty much impossible to
  attempt a synchronous modeset from panic handlers. The only thing we could
  try to achive is an atomic ``set_base`` of the primary plane, and hope that
  it shows up. Everything else probably needs to be delayed to some worker or
  something else which happens later on. Otherwise it just kills the box
  harder, prevent the panic from going out on e.g. netconsole.

* There's also proposal for a simplied DRM console instead of the full-blown
  fbcon and DRM fbdev emulation. Any kind of panic handling tricks should
  obviously work for both console, in case we ever get kmslog merged.

Contact: Daniel Vetter

Better Testing
==============

Enable trinity for DRM
----------------------

And fix up the fallout. Should be really interesting ...

Make KMS tests in i-g-t generic
-------------------------------

The i915 driver team maintains an extensive testsuite for the i915 DRM driver,
including tons of testcases for corner-cases in the modesetting API. It would
be awesome if those tests (at least the ones not relying on Intel-specific GEM
features) could be made to run on any KMS driver.

Basic work to run i-g-t tests on non-i915 is done, what's now missing is mass-
converting things over. For modeset tests we also first need a bit of
infrastructure to use dumb buffers for untiled buffers, to be able to run all
the non-i915 specific modeset tests.

Contact: Daniel Vetter

Create a virtual KMS driver for testing (vkms)
----------------------------------------------

With all the latest helpers it should be fairly simple to create a virtual KMS
driver useful for testing, or for running X or similar on headless machines
(to be able to still use the GPU). This would be similar to vgem, but aimed at
the modeset side.

Once the basics are there there's tons of possibilities to extend it.

Contact: Daniel Vetter

Driver Specific
===============

Outside DRM
===========

Better kerneldoc
----------------

This is pretty much done, but there's some advanced topics:

Come up with a way to hyperlink to struct members. Currently you can hyperlink
to the struct using ``#struct_name``, but not to a member within. Would need
buy-in from kerneldoc maintainers, and the big question is how to make it work
without totally unsightly
``drm_foo_bar_really_long_structure->even_longer_memeber`` all over the text
which breaks text flow.

Figure out how to integrate the asciidoc support for ascii-diagrams. We have a
few of those (e.g. to describe mode timings), and asciidoc supports converting
some ascii-art dialect into pngs. Would be really pretty to make that work.

Contact: Daniel Vetter, Jani Nikula

Jani is working on this already, hopefully lands in 4.8.
