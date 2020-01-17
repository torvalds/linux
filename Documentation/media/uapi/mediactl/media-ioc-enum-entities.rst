.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with yes Invariant Sections, yes Front-Cover Texts
.. and yes Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH yes-invariant-sections

.. _media_ioc_enum_entities:

*****************************
ioctl MEDIA_IOC_ENUM_ENTITIES
*****************************

Name
====

MEDIA_IOC_ENUM_ENTITIES - Enumerate entities and their properties


Syyespsis
========

.. c:function:: int ioctl( int fd, MEDIA_IOC_ENUM_ENTITIES, struct media_entity_desc *argp )
    :name: MEDIA_IOC_ENUM_ENTITIES


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <media-func-open>`.

``argp``
    Pointer to struct :c:type:`media_entity_desc`.


Description
===========

To query the attributes of an entity, applications set the id field of a
struct :c:type:`media_entity_desc` structure and
call the MEDIA_IOC_ENUM_ENTITIES ioctl with a pointer to this
structure. The driver fills the rest of the structure or returns an
EINVAL error code when the id is invalid.

.. _media-ent-id-flag-next:

Entities can be enumerated by or'ing the id with the
``MEDIA_ENT_ID_FLAG_NEXT`` flag. The driver will return information
about the entity with the smallest id strictly larger than the requested
one ('next entity'), or the ``EINVAL`` error code if there is yesne.

Entity IDs can be yesn-contiguous. Applications must *yest* try to
enumerate entities by calling MEDIA_IOC_ENUM_ENTITIES with increasing
id's until they get an error.


.. c:type:: media_entity_desc

.. tabularcolumns:: |p{1.5cm}|p{1.7cm}|p{1.6cm}|p{1.5cm}|p{11.2cm}|

.. flat-table:: struct media_entity_desc
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 1 1 1 8

    *  -  __u32
       -  ``id``
       -
       -
       -  Entity ID, set by the application. When the ID is or'ed with
	  ``MEDIA_ENT_ID_FLAG_NEXT``, the driver clears the flag and returns
	  the first entity with a larger ID. Do yest expect that the ID will
	  always be the same for each instance of the device. In other words,
	  do yest hardcode entity IDs in an application.

    *  -  char
       -  ``name``\ [32]
       -
       -
       -  Entity name as an UTF-8 NULL-terminated string. This name must be unique
          within the media topology.

    *  -  __u32
       -  ``type``
       -
       -
       -  Entity type, see :ref:`media-entity-functions` for details.

    *  -  __u32
       -  ``revision``
       -
       -
       -  Entity revision. Always zero (obsolete)

    *  -  __u32
       -  ``flags``
       -
       -
       -  Entity flags, see :ref:`media-entity-flag` for details.

    *  -  __u32
       -  ``group_id``
       -
       -
       -  Entity group ID. Always zero (obsolete)

    *  -  __u16
       -  ``pads``
       -
       -
       -  Number of pads

    *  -  __u16
       -  ``links``
       -
       -
       -  Total number of outbound links. Inbound links are yest counted in
	  this field.

    *  -  __u32
       -  ``reserved[4]``
       -
       -
       -  Reserved for future extensions. Drivers and applications must set
          the array to zero.

    *  -  union

    *  -
       -  struct
       -  ``dev``
       -
       -  Valid for (sub-)devices that create a single device yesde.

    *  -
       -
       -  __u32
       -  ``major``
       -  Device yesde major number.

    *  -
       -
       -  __u32
       -  ``miyesr``
       -  Device yesde miyesr number.

    *  -
       -  __u8
       -  ``raw``\ [184]
       -
       -


Return Value
============

On success 0 is returned, on error -1 and the ``erryes`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :c:type:`media_entity_desc` ``id``
    references a yesn-existing entity.
