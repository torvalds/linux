.. -*- coding: utf-8; mode: rst -*-

.. _media_ioc_g_topology:

**************************
ioctl MEDIA_IOC_G_TOPOLOGY
**************************

Name
====

MEDIA_IOC_G_TOPOLOGY - Enumerate the graph topology and graph element properties


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct media_v2_topology *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <media-func-open>`.

``request``
    MEDIA_IOC_G_TOPOLOGY

``argp``


Description
===========

The typical usage of this ioctl is to call it twice. On the first call,
the structure defined at struct
:ref:`media_v2_topology <media-v2-topology>` should be zeroed. At
return, if no errors happen, this ioctl will return the
``topology_version`` and the total number of entities, interfaces, pads
and links.

Before the second call, the userspace should allocate arrays to store
the graph elements that are desired, putting the pointers to them at the
ptr_entities, ptr_interfaces, ptr_links and/or ptr_pads, keeping the
other values untouched.

If the ``topology_version`` remains the same, the ioctl should fill the
desired arrays with the media graph elements.


.. _media-v2-topology:

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.7cm}|

.. flat-table:: struct media_v2_topology
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8


    -  .. row 1

       -  __u64

       -  ``topology_version``

       -  Version of the media graph topology. When the graph is created,
	  this field starts with zero. Every time a graph element is added
	  or removed, this field is incremented.

    -  .. row 2

       -  __u64

       -  ``num_entities``

       -  Number of entities in the graph

    -  .. row 3

       -  __u64

       -  ``ptr_entities``

       -  A pointer to a memory area where the entities array will be
	  stored, converted to a 64-bits integer. It can be zero. if zero,
	  the ioctl won't store the entities. It will just update
	  ``num_entities``

    -  .. row 4

       -  __u64

       -  ``num_interfaces``

       -  Number of interfaces in the graph

    -  .. row 5

       -  __u64

       -  ``ptr_interfaces``

       -  A pointer to a memory area where the interfaces array will be
	  stored, converted to a 64-bits integer. It can be zero. if zero,
	  the ioctl won't store the interfaces. It will just update
	  ``num_interfaces``

    -  .. row 6

       -  __u64

       -  ``num_pads``

       -  Total number of pads in the graph

    -  .. row 7

       -  __u64

       -  ``ptr_pads``

       -  A pointer to a memory area where the pads array will be stored,
	  converted to a 64-bits integer. It can be zero. if zero, the ioctl
	  won't store the pads. It will just update ``num_pads``

    -  .. row 8

       -  __u64

       -  ``num_links``

       -  Total number of data and interface links in the graph

    -  .. row 9

       -  __u64

       -  ``ptr_links``

       -  A pointer to a memory area where the links array will be stored,
	  converted to a 64-bits integer. It can be zero. if zero, the ioctl
	  won't store the links. It will just update ``num_links``



.. _media-v2-entity:

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.7cm}|

.. flat-table:: struct media_v2_entity
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8


    -  .. row 1

       -  __u32

       -  ``id``

       -  Unique ID for the entity.

    -  .. row 2

       -  char

       -  ``name``\ [64]

       -  Entity name as an UTF-8 NULL-terminated string.

    -  .. row 3

       -  __u32

       -  ``function``

       -  Entity main function, see :ref:`media-entity-type` for details.

    -  .. row 4

       -  __u32

       -  ``reserved``\ [12]

       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.



.. _media-v2-interface:

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.7cm}|

.. flat-table:: struct media_v2_interface
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8

    -  .. row 1

       -  __u32

       -  ``id``

       -  Unique ID for the interface.

    -  .. row 2

       -  __u32

       -  ``intf_type``

       -  Interface type, see :ref:`media-intf-type` for details.

    -  .. row 3

       -  __u32

       -  ``flags``

       -  Interface flags. Currently unused.

    -  .. row 4

       -  __u32

       -  ``reserved``\ [9]

       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.

    -  .. row 5

       -  struct media_v2_intf_devnode

       -  ``devnode``

       -  Used only for device node interfaces. See
	  :ref:`media-v2-intf-devnode` for details..



.. _media-v2-intf-devnode:

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.7cm}|

.. flat-table:: struct media_v2_interface
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8


    -  .. row 1

       -  __u32

       -  ``major``

       -  Device node major number.

    -  .. row 2

       -  __u32

       -  ``minor``

       -  Device node minor number.



.. _media-v2-pad:

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.7cm}|

.. flat-table:: struct media_v2_pad
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8


    -  .. row 1

       -  __u32

       -  ``id``

       -  Unique ID for the pad.

    -  .. row 2

       -  __u32

       -  ``entity_id``

       -  Unique ID for the entity where this pad belongs.

    -  .. row 3

       -  __u32

       -  ``flags``

       -  Pad flags, see :ref:`media-pad-flag` for more details.

    -  .. row 4

       -  __u32

       -  ``reserved``\ [9]

       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.



.. _media-v2-link:

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.7cm}|

.. flat-table:: struct media_v2_pad
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8


    -  .. row 1

       -  __u32

       -  ``id``

       -  Unique ID for the pad.

    -  .. row 2

       -  __u32

       -  ``source_id``

       -  On pad to pad links: unique ID for the source pad.

	  On interface to entity links: unique ID for the interface.

    -  .. row 3

       -  __u32

       -  ``sink_id``

       -  On pad to pad links: unique ID for the sink pad.

	  On interface to entity links: unique ID for the entity.

    -  .. row 4

       -  __u32

       -  ``flags``

       -  Link flags, see :ref:`media-link-flag` for more details.

    -  .. row 5

       -  __u32

       -  ``reserved``\ [5]

       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENOSPC
    This is returned when either one or more of the num_entities,
    num_interfaces, num_links or num_pads are non-zero and are
    smaller than the actual number of elements inside the graph. This
    may happen if the ``topology_version`` changed when compared to the
    last time this ioctl was called. Userspace should usually free the
    area for the pointers, zero the struct elements and call this ioctl
    again.
