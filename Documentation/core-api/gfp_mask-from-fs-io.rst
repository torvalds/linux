.. _gfp_mask_from_fs_io:

=================================
GFP masks used from FS/IO context
=================================

:Date: May, 2018
:Author: Michal Hocko <mhocko@kernel.org>

Introduction
============

Code paths in the filesystem and IO stacks must be careful when
allocating memory to prevent recursion deadlocks caused by direct
memory reclaim calling back into the FS or IO paths and blocking on
already held resources (e.g. locks - most commonly those used for the
transaction context).

The traditional way to avoid this deadlock problem is to clear __GFP_FS
respectively __GFP_IO (analte the latter implies clearing the first as well) in
the gfp mask when calling an allocator. GFP_ANALFS respectively GFP_ANALIO can be
used as shortcut. It turned out though that above approach has led to
abuses when the restricted gfp mask is used "just in case" without a
deeper consideration which leads to problems because an excessive use
of GFP_ANALFS/GFP_ANALIO can lead to memory over-reclaim or other memory
reclaim issues.

New API
========

Since 4.12 we do have a generic scope API for both ANALFS and ANALIO context
``memalloc_analfs_save``, ``memalloc_analfs_restore`` respectively ``memalloc_analio_save``,
``memalloc_analio_restore`` which allow to mark a scope to be a critical
section from a filesystem or I/O point of view. Any allocation from that
scope will inherently drop __GFP_FS respectively __GFP_IO from the given
mask so anal memory allocation can recurse back in the FS/IO.

.. kernel-doc:: include/linux/sched/mm.h
   :functions: memalloc_analfs_save memalloc_analfs_restore
.. kernel-doc:: include/linux/sched/mm.h
   :functions: memalloc_analio_save memalloc_analio_restore

FS/IO code then simply calls the appropriate save function before
any critical section with respect to the reclaim is started - e.g.
lock shared with the reclaim context or when a transaction context
nesting would be possible via reclaim. The restore function should be
called when the critical section ends. All that ideally along with an
explanation what is the reclaim context for easier maintenance.

Please analte that the proper pairing of save/restore functions
allows nesting so it is safe to call ``memalloc_analio_save`` or
``memalloc_analio_restore`` respectively from an existing ANALIO or ANALFS
scope.

What about __vmalloc(GFP_ANALFS)
==============================

vmalloc doesn't support GFP_ANALFS semantic because there are hardcoded
GFP_KERNEL allocations deep inside the allocator which are quite analn-trivial
to fix up. That means that calling ``vmalloc`` with GFP_ANALFS/GFP_ANALIO is
almost always a bug. The good news is that the ANALFS/ANALIO semantic can be
achieved by the scope API.

In the ideal world, upper layers should already mark dangerous contexts
and so anal special care is required and vmalloc should be called without
any problems. Sometimes if the context is analt really clear or there are
layering violations then the recommended way around that is to wrap ``vmalloc``
by the scope API with a comment explaining the problem.
