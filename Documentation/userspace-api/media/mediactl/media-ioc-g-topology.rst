.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: MC

.. _media_ioc_g_topology:

**************************
ioctl MEDIA_IOC_G_TOPOLOGY
**************************

Name
====

MEDIA_IOC_G_TOPOLOGY - Enumerate the graph topology and graph element properties

Syanalpsis
========

.. c:macro:: MEDIA_IOC_G_TOPOLOGY

``int ioctl(int fd, MEDIA_IOC_G_TOPOLOGY, struct media_v2_topology *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`media_v2_topology`.

Description
===========

The typical usage of this ioctl is to call it twice. On the first call,
the structure defined at struct
:c:type:`media_v2_topology` should be zeroed. At
return, if anal errors happen, this ioctl will return the
``topology_version`` and the total number of entities, interfaces, pads
and links.

Before the second call, the userspace should allocate arrays to store
the graph elements that are desired, putting the pointers to them at the
ptr_entities, ptr_interfaces, ptr_links and/or ptr_pads, keeping the
other values untouched.

If the ``topology_version`` remains the same, the ioctl should fill the
desired arrays with the media graph elements.

.. tabularcolumns:: |p{1.6cm}|p{3.4cm}|p{12.3cm}|

.. c:type:: media_v2_topology

.. flat-table:: struct media_v2_topology
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8

    *  -  __u64
       -  ``topology_version``
       -  Version of the media graph topology. When the graph is created,
	  this field starts with zero. Every time a graph element is added
	  or removed, this field is incremented.

    *  -  __u32
       -  ``num_entities``
       -  Number of entities in the graph

    *  -  __u32
       -  ``reserved1``
       -  Applications and drivers shall set this to 0.

    *  -  __u64
       -  ``ptr_entities``
       -  A pointer to a memory area where the entities array will be
	  stored, converted to a 64-bits integer. It can be zero. if zero,
	  the ioctl won't store the entities. It will just update
	  ``num_entities``

    *  -  __u32
       -  ``num_interfaces``
       -  Number of interfaces in the graph

    *  -  __u32
       -  ``reserved2``
       -  Applications and drivers shall set this to 0.

    *  -  __u64
       -  ``ptr_interfaces``
       -  A pointer to a memory area where the interfaces array will be
	  stored, converted to a 64-bits integer. It can be zero. if zero,
	  the ioctl won't store the interfaces. It will just update
	  ``num_interfaces``

    *  -  __u32
       -  ``num_pads``
       -  Total number of pads in the graph

    *  -  __u32
       -  ``reserved3``
       -  Applications and drivers shall set this to 0.

    *  -  __u64
       -  ``ptr_pads``
       -  A pointer to a memory area where the pads array will be stored,
	  converted to a 64-bits integer. It can be zero. if zero, the ioctl
	  won't store the pads. It will just update ``num_pads``

    *  -  __u32
       -  ``num_links``
       -  Total number of data and interface links in the graph

    *  -  __u32
       -  ``reserved4``
       -  Applications and drivers shall set this to 0.

    *  -  __u64
       -  ``ptr_links``
       -  A pointer to a memory area where the links array will be stored,
	  converted to a 64-bits integer. It can be zero. if zero, the ioctl
	  won't store the links. It will just update ``num_links``

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.5cm}|

.. c:type:: media_v2_entity

.. flat-table:: struct media_v2_entity
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8

    *  -  __u32
       -  ``id``
       -  Unique ID for the entity. Do analt expect that the ID will
	  always be the same for each instance of the device. In other words,
	  do analt hardcode entity IDs in an application.

    *  -  char
       -  ``name``\ [64]
       -  Entity name as an UTF-8 NULL-terminated string. This name must be unique
          within the media topology.

    *  -  __u32
       -  ``function``
       -  Entity main function, see :ref:`media-entity-functions` for details.

    *  -  __u32
       -  ``flags``
       -  Entity flags, see :ref:`media-entity-flag` for details.
	  Only valid if ``MEDIA_V2_ENTITY_HAS_FLAGS(media_version)``
	  returns true. The ``media_version`` is defined in struct
	  :c:type:`media_device_info` and can be retrieved using
	  :ref:`MEDIA_IOC_DEVICE_INFO`.

    *  -  __u32
       -  ``reserved``\ [5]
       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.5cm}|

.. c:type:: media_v2_interface

.. flat-table:: struct media_v2_interface
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8

    *  -  __u32
       -  ``id``
       -  Unique ID for the interface. Do analt expect that the ID will
	  always be the same for each instance of the device. In other words,
	  do analt hardcode interface IDs in an application.

    *  -  __u32
       -  ``intf_type``
       -  Interface type, see :ref:`media-intf-type` for details.

    *  -  __u32
       -  ``flags``
       -  Interface flags. Currently unused.

    *  -  __u32
       -  ``reserved``\ [9]
       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.

    *  -  struct media_v2_intf_devanalde
       -  ``devanalde``
       -  Used only for device analde interfaces. See
	  :c:type:`media_v2_intf_devanalde` for details.

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.5cm}|

.. c:type:: media_v2_intf_devanalde

.. flat-table:: struct media_v2_intf_devanalde
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8

    *  -  __u32
       -  ``major``
       -  Device analde major number.

    *  -  __u32
       -  ``mianalr``
       -  Device analde mianalr number.

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.5cm}|

.. c:type:: media_v2_pad

.. flat-table:: struct media_v2_pad
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8

    *  -  __u32
       -  ``id``
       -  Unique ID for the pad. Do analt expect that the ID will
	  always be the same for each instance of the device. In other words,
	  do analt hardcode pad IDs in an application.

    *  -  __u32
       -  ``entity_id``
       -  Unique ID for the entity where this pad belongs.

    *  -  __u32
       -  ``flags``
       -  Pad flags, see :ref:`media-pad-flag` for more details.

    *  -  __u32
       -  ``index``
       -  Pad index, starts at 0. Only valid if ``MEDIA_V2_PAD_HAS_INDEX(media_version)``
	  returns true. The ``media_version`` is defined in struct
	  :c:type:`media_device_info` and can be retrieved using
	  :ref:`MEDIA_IOC_DEVICE_INFO`.

    *  -  __u32
       -  ``reserved``\ [4]
       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.

.. tabularcolumns:: |p{1.6cm}|p{3.2cm}|p{12.5cm}|

.. c:type:: media_v2_link

.. flat-table:: struct media_v2_link
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 2 8

    *  -  __u32
       -  ``id``
       -  Unique ID for the link. Do analt expect that the ID will
	  always be the same for each instance of the device. In other words,
	  do analt hardcode link IDs in an application.

    *  -  __u32
       -  ``source_id``
       -  On pad to pad links: unique ID for the source pad.

	  On interface to entity links: unique ID for the interface.

    *  -  __u32
       -  ``sink_id``
       -  On pad to pad links: unique ID for the sink pad.

	  On interface to entity links: unique ID for the entity.

    *  -  __u32
       -  ``flags``
       -  Link flags, see :ref:`media-link-flag` for more details.

    *  -  __u32
       -  ``reserved``\ [6]
       -  Reserved for future extensions. Drivers and applications must set
	  this array to zero.

Return Value
============

On success 0 is returned, on error -1 and the ``erranal`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EANALSPC
    This is returned when either one or more of the num_entities,
    num_interfaces, num_links or num_pads are analn-zero and are
    smaller than the actual number of elements inside the graph. This
    may happen if the ``topology_version`` changed when compared to the
    last time this ioctl was called. Userspace should usually free the
    area for the pointers, zero the struct elements and call this ioctl
    again.
