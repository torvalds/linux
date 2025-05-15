.. SPDX-License-Identifier: GPL-2.0+

=========================
 drm/Panthor CSF driver
=========================

.. _panthor-usage-stats:

Panthor DRM client usage stats implementation
==============================================

The drm/Panthor driver implements the DRM client usage stats specification as
documented in :ref:`drm-client-usage-stats`.

Example of the output showing the implemented key value pairs and entirety of
the currently possible format options:

::
     pos:    0
     flags:  02400002
     mnt_id: 29
     ino:    491
     drm-driver:     panthor
     drm-client-id:  10
     drm-engine-panthor:     111110952750 ns
     drm-cycles-panthor:     94439687187
     drm-maxfreq-panthor:    1000000000 Hz
     drm-curfreq-panthor:    1000000000 Hz
     panthor-resident-memory:        10396 KiB
     panthor-active-memory:  10396 KiB
     drm-total-memory:       16480 KiB
     drm-shared-memory:      0
     drm-active-memory:      16200 KiB
     drm-resident-memory:    16480 KiB
     drm-purgeable-memory:   0

Possible `drm-engine-` key names are: `panthor`.
`drm-curfreq-` values convey the current operating frequency for that engine.

Users must bear in mind that engine and cycle sampling are disabled by default,
because of power saving concerns. `fdinfo` users and benchmark applications which
query the fdinfo file must make sure to toggle the job profiling status of the
driver by writing into the appropriate sysfs node::

    echo <N> > /sys/bus/platform/drivers/panthor/[a-f0-9]*.gpu/profiling

Where `N` is a bit mask where cycle and timestamp sampling are respectively
enabled by the first and second bits.

Possible `panthor-*-memory` keys are: `active` and `resident`.
These values convey the sizes of the internal driver-owned shmem BO's that
aren't exposed to user-space through a DRM handle, like queue ring buffers,
sync object arrays and heap chunks. Because they are all allocated and pinned
at creation time, only `panthor-resident-memory` is necessary to tell us their
size. `panthor-active-memory` shows the size of kernel BO's associated with
VM's and groups currently being scheduled for execution by the GPU.
