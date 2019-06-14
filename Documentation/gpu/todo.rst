.. _todo:

=========
TODO list
=========

This section contains a list of smaller janitorial tasks in the kernel DRM
graphics subsystem useful as newbie projects. Or for slow rainy days.

Subsystem-wide refactorings
===========================

Remove custom dumb_map_offset implementations
---------------------------------------------

All GEM based drivers should be using drm_gem_create_mmap_offset() instead.
Audit each individual driver, make sure it'll work with the generic
implementation (there's lots of outdated locking leftovers in various
implementations), and then remove it.

Contact: Daniel Vetter, respective driver maintainers

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

  Except for some driver code this is done. This task should be finished by
  adding WARN_ON(!drm_drv_uses_atomic_modeset) in drm_modeset_lock_all().

* A bunch of the vtable hooks are now in the wrong place: DRM has a split
  between core vfunc tables (named ``drm_foo_funcs``), which are used to
  implement the userspace ABI. And then there's the optional hooks for the
  helper libraries (name ``drm_foo_helper_funcs``), which are purely for
  internal use. Some of these hooks should be move from ``_funcs`` to
  ``_helper_funcs`` since they are not part of the core ABI. There's a
  ``FIXME`` comment in the kerneldoc for each such case in ``drm_crtc.h``.

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
fine-grained per-buffer object and per-context lockings scheme. Currently only the
``msm`` driver still use ``struct_mutex``.

Contact: Daniel Vetter, respective driver maintainers

Convert instances of dev_info/dev_err/dev_warn to their DRM_DEV_* equivalent
----------------------------------------------------------------------------

For drivers which could have multiple instances, it is necessary to
differentiate between which is which in the logs. Since DRM_INFO/WARN/ERROR
don't do this, drivers used dev_info/warn/err to make this differentiation. We
now have DRM_DEV_* variants of the drm print macros, so we can start to convert
those drivers back to using drm-formwatted specific log messages.

Before you start this conversion please contact the relevant maintainers to make
sure your work will be merged - not everyone agrees that the DRM dmesg macros
are better.

Contact: Sean Paul, Maintainer of the driver you plan to convert

Convert drivers to use simple modeset suspend/resume
----------------------------------------------------

Most drivers (except i915 and nouveau) that use
drm_atomic_helper_suspend/resume() can probably be converted to use
drm_mode_config_helper_suspend/resume(). Also there's still open-coded version
of the atomic suspend/resume code in older atomic modeset drivers.

Contact: Maintainer of the driver you plan to convert

Convert drivers to use drm_fb_helper_fbdev_setup/teardown()
-----------------------------------------------------------

Most drivers can use drm_fb_helper_fbdev_setup() except maybe:

- amdgpu which has special logic to decide whether to call
  drm_helper_disable_unused_functions()

- armada which isn't atomic and doesn't call
  drm_helper_disable_unused_functions()

- i915 which calls drm_fb_helper_initial_config() in a worker

Drivers that use drm_framebuffer_remove() to clean up the fbdev framebuffer can
probably use drm_fb_helper_fbdev_teardown().

Contact: Maintainer of the driver you plan to convert

Clean up mmap forwarding
------------------------

A lot of drivers forward gem mmap calls to dma-buf mmap for imported buffers.
And also a lot of them forward dma-buf mmap to the gem mmap implementations.
Would be great to refactor this all into a set of small common helpers.

Contact: Daniel Vetter

Generic fbdev defio support
---------------------------

The defio support code in the fbdev core has some very specific requirements,
which means drivers need to have a special framebuffer for fbdev. Which prevents
us from using the generic fbdev emulation code everywhere. The main issue is
that it uses some fields in struct page itself, which breaks shmem gem objects
(and other things).

Possible solution would be to write our own defio mmap code in the drm fbdev
emulation. It would need to fully wrap the existing mmap ops, forwarding
everything after it has done the write-protect/mkwrite trickery:

- In the drm_fbdev_fb_mmap helper, if we need defio, change the
  default page prots to write-protected with something like this::

      vma->vm_page_prot = pgprot_wrprotect(vma->vm_page_prot);

- Set the mkwrite and fsync callbacks with similar implementions to the core
  fbdev defio stuff. These should all work on plain ptes, they don't actually
  require a struct page.  uff. These should all work on plain ptes, they don't
  actually require a struct page.

- Track the dirty pages in a separate structure (bitfield with one bit per page
  should work) to avoid clobbering struct page.

Might be good to also have some igt testcases for this.

Contact: Daniel Vetter, Noralf Tronnes

Remove the ->gem_prime_res_obj callback
--------------------------------------------

The ->gem_prime_res_obj callback can be removed from drivers by using the
reservation_object in the drm_gem_object. It may also be possible to use the
generic drm_gem_reservation_object_wait helper for waiting for a bo.

Contact: Daniel Vetter

idr_init_base()
---------------

DRM core&drivers uses a lot of idr (integer lookup directories) for mapping
userspace IDs to internal objects, and in most places ID=0 means NULL and hence
is never used. Switching to idr_init_base() for these would make the idr more
efficient.

Contact: Daniel Vetter

struct drm_gem_object_funcs
---------------------------

GEM objects can now have a function table instead of having the callbacks on the
DRM driver struct. This is now the preferred way and drivers can be moved over.

DRM_GEM_CMA_VMAP_DRIVER_OPS, DRM_GEM_SHMEM_DRIVER_OPS already support this, but
DRM_GEM_VRAM_DRIVER_PRIME does not yet and needs to be aligned with the previous
two. We also need a 2nd version of the CMA define that doesn't require the
vmapping to be present (different hook for prime importing). Plus this needs to
be rolled out to all drivers using their own implementations, too.

Use DRM_MODESET_LOCK_ALL_* helpers instead of boilerplate
---------------------------------------------------------

For cases where drivers are attempting to grab the modeset locks with a local
acquire context. Replace the boilerplate code surrounding
drm_modeset_lock_all_ctx() with DRM_MODESET_LOCK_ALL_BEGIN() and
DRM_MODESET_LOCK_ALL_END() instead.

This should also be done for all places where drm_modest_lock_all() is still
used.

As a reference, take a look at the conversions already completed in drm core.

Contact: Sean Paul, respective driver maintainers

Rename CMA helpers to DMA helpers
---------------------------------

CMA (standing for contiguous memory allocator) is really a bit an accident of
what these were used for first, a much better name would be DMA helpers. In the
text these should even be called coherent DMA memory helpers (so maybe CDM, but
no one knows what that means) since underneath they just use dma_alloc_coherent.

Contact: Laurent Pinchart, Daniel Vetter

Convert direct mode.vrefresh accesses to use drm_mode_vrefresh()
----------------------------------------------------------------

drm_display_mode.vrefresh isn't guaranteed to be populated. As such, using it
is risky and has been known to cause div-by-zero bugs. Fortunately, drm core
has helper which will use mode.vrefresh if it's !0 and will calculate it from
the timings when it's 0.

Use simple search/replace, or (more fun) cocci to replace instances of direct
vrefresh access with a call to the helper. Check out
https://lists.freedesktop.org/archives/dri-devel/2019-January/205186.html for
inspiration.

Once all instances of vrefresh have been converted, remove vrefresh from
drm_display_mode to avoid future use.

Contact: Sean Paul

Remove drm_display_mode.hsync
-----------------------------

We have drm_mode_hsync() to calculate this from hsync_start/end, since drivers
shouldn't/don't use this, remove this member to avoid any temptations to use it
in the future. If there is any debug code using drm_display_mode.hsync, convert
it to use drm_mode_hsync() instead.

Contact: Sean Paul

drm_fb_helper tasks
-------------------

- drm_fb_helper_restore_fbdev_mode_unlocked() should call restore_fbdev_mode()
  not the _force variant so it can bail out if there is a master. But first
  these igt tests need to be fixed: kms_fbcon_fbt@psr and
  kms_fbcon_fbt@psr-suspend.

- The max connector argument for drm_fb_helper_init() and
  drm_fb_helper_fbdev_setup() isn't used anymore and can be removed.

- The helper doesn't keep an array of connectors anymore so these can be
  removed: drm_fb_helper_single_add_all_connectors(),
  drm_fb_helper_add_one_connector() and drm_fb_helper_remove_one_connector().

Core refactorings
=================

Clean up the DRM header mess
----------------------------

The DRM subsystem originally had only one huge global header, ``drmP.h``. This
is now split up, but many source files still include it. The remaining part of
the cleanup work here is to replace any ``#include <drm/drmP.h>`` by only the
headers needed (and fixing up any missing pre-declarations in the headers).

In the end no .c file should need to include ``drmP.h`` anymore.

Contact: Daniel Vetter

Add missing kerneldoc for exported functions
--------------------------------------------

The DRM reference documentation is still lacking kerneldoc in a few areas. The
task would be to clean up interfaces like moving functions around between
files to better group them and improving the interfaces like dropping return
values for functions that never fail. Then write kerneldoc for all exported
functions and an overview section and integrate it all into the drm book.

See https://dri.freedesktop.org/docs/drm/ for what's there already.

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

Clean up the debugfs support
----------------------------

There's a bunch of issues with it:

- The drm_info_list ->show() function doesn't even bother to cast to the drm
  structure for you. This is lazy.

- We probably want to have some support for debugfs files on crtc/connectors and
  maybe other kms objects directly in core. There's even drm_print support in
  the funcs for these objects to dump kms state, so it's all there. And then the
  ->show() functions should obviously give you a pointer to the right object.

- The drm_info_list stuff is centered on drm_minor instead of drm_device. For
  anything we want to print drm_device (or maybe drm_file) is the right thing.

- The drm_driver->debugfs_init hooks we have is just an artifact of the old
  midlayered load sequence. DRM debugfs should work more like sysfs, where you
  can create properties/files for an object anytime you want, and the core
  takes care of publishing/unpuplishing all the files at register/unregister
  time. Drivers shouldn't need to worry about these technicalities, and fixing
  this (together with the drm_minor->drm_device move) would allow us to remove
  debugfs_init.

Contact: Daniel Vetter

KMS cleanups
------------

Some of these date from the very introduction of KMS in 2008 ...

- Make ->funcs and ->helper_private vtables optional. There's a bunch of empty
  function tables in drivers, but before we can remove them we need to make sure
  that all the users in helpers and drivers do correctly check for a NULL
  vtable.

- Cleanup up the various ->destroy callbacks. A lot of them just wrapt the
  drm_*_cleanup implementations and can be removed. Some tack a kfree() at the
  end, for which we could add drm_*_cleanup_kfree(). And then there's the (for
  historical reasons) misnamed drm_primary_helper_destroy() function.

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

Extend virtual test driver (VKMS)
---------------------------------

See the documentation of :ref:`VKMS <vkms>` for more details. This is an ideal
internship task, since it only requires a virtual machine and can be sized to
fit the available time.

Contact: Daniel Vetter

Backlight Refactoring
---------------------

Backlight drivers have a triple enable/disable state, which is a bit overkill.
Plan to fix this:

1. Roll out backlight_enable() and backlight_disable() helpers everywhere. This
   has started already.
2. In all, only look at one of the three status bits set by the above helpers.
3. Remove the other two status bits.

Contact: Daniel Vetter

Driver Specific
===============

tinydrm
-------

Tinydrm is the helper driver for really simple fb drivers. The goal is to make
those drivers as simple as possible, so lots of room for refactoring:

- spi helpers, probably best put into spi core/helper code. Thierry said
  the spi maintainer is fast&reactive, so shouldn't be a big issue.

- extract the mipi-dbi helper (well, the non-tinydrm specific parts at
  least) into a separate helper, like we have for mipi-dsi already. Or follow
  one of the ideas for having a shared dsi/dbi helper, abstracting away the
  transport details more.

Contact: Noralf Trønnes, Daniel Vetter

AMD DC Display Driver
---------------------

AMD DC is the display driver for AMD devices starting with Vega. There has been
a bunch of progress cleaning it up but there's still plenty of work to be done.

See drivers/gpu/drm/amd/display/TODO for tasks.

Contact: Harry Wentland, Alex Deucher

i915
----

- Our early/late pm callbacks could be removed in favour of using
  device_link_add to model the dependency between i915 and snd_had. See
  https://dri.freedesktop.org/docs/drm/driver-api/device_link.html

Bootsplash
==========

There is support in place now for writing internal DRM clients making it
possible to pick up the bootsplash work that was rejected because it was written
for fbdev.

- [v6,8/8] drm/client: Hack: Add bootsplash example
  https://patchwork.freedesktop.org/patch/306579/

- [RFC PATCH v2 00/13] Kernel based bootsplash
  https://lkml.org/lkml/2017/12/13/764

Contact: Sam Ravnborg

Outside DRM
===========
