.. -*- coding: utf-8; mode: rst -*-

.. _media_ioc_enum_entities:

*****************************
ioctl MEDIA_IOC_ENUM_ENTITIES
*****************************

Name
====

MEDIA_IOC_ENUM_ENTITIES - Enumerate entities and their properties


Synopsis
========

.. c:function:: int ioctl( int fd, MEDIA_IOC_ENUM_ENTITIES, struct media_entity_desc *argp )
    :name: MEDIA_IOC_ENUM_ENTITIES


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <media-func-open>`.

``argp``


Description
===========

To query the attributes of an entity, applications set the id field of a
struct :ref:`media_entity_desc <media-entity-desc>` structure and
call the MEDIA_IOC_ENUM_ENTITIES ioctl with a pointer to this
structure. The driver fills the rest of the structure or returns an
EINVAL error code when the id is invalid.

.. _media-ent-id-flag-next:

Entities can be enumerated by or'ing the id with the
``MEDIA_ENT_ID_FLAG_NEXT`` flag. The driver will return information
about the entity with the smallest id strictly larger than the requested
one ('next entity'), or the ``EINVAL`` error code if there is none.

Entity IDs can be non-contiguous. Applications must *not* try to
enumerate entities by calling MEDIA_IOC_ENUM_ENTITIES with increasing
id's until they get an error.


.. _media-entity-desc:

.. tabularcolumns:: |p{1.5cm}|p{1.5cm}|p{1.5cm}|p{1.5cm}|p{11.5cm}|

.. flat-table:: struct media_entity_desc
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 1 1 1 8


    -  .. row 1

       -  __u32

       -  ``id``

       -
       -
       -  Entity id, set by the application. When the id is or'ed with
	  ``MEDIA_ENT_ID_FLAG_NEXT``, the driver clears the flag and returns
	  the first entity with a larger id.

    -  .. row 2

       -  char

       -  ``name``\ [32]

       -
       -
       -  Entity name as an UTF-8 NULL-terminated string.

    -  .. row 3

       -  __u32

       -  ``type``

       -
       -
       -  Entity type, see :ref:`media-entity-type` for details.

    -  .. row 4

       -  __u32

       -  ``revision``

       -
       -
       -  Entity revision. Always zero (obsolete)

    -  .. row 5

       -  __u32

       -  ``flags``

       -
       -
       -  Entity flags, see :ref:`media-entity-flag` for details.

    -  .. row 6

       -  __u32

       -  ``group_id``

       -
       -
       -  Entity group ID. Always zero (obsolete)

    -  .. row 7

       -  __u16

       -  ``pads``

       -
       -
       -  Number of pads

    -  .. row 8

       -  __u16

       -  ``links``

       -
       -
       -  Total number of outbound links. Inbound links are not counted in
	  this field.

    -  .. row 9

       -  union

    -  .. row 10

       -
       -  struct

       -  ``dev``

       -
       -  Valid for (sub-)devices that create a single device node.

    -  .. row 11

       -
       -
       -  __u32

       -  ``major``

       -  Device node major number.

    -  .. row 12

       -
       -
       -  __u32

       -  ``minor``

       -  Device node minor number.

    -  .. row 13

       -
       -  __u8

       -  ``raw``\ [184]

       -
       -


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :ref:`media_entity_desc <media-entity-desc>` ``id``
    references a non-existing entity.
