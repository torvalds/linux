.. -*- coding: utf-8; mode: rst -*-

.. _dmx_types:

****************
Demux Data Types
****************


.. _dmx-output-t:

Output for the demux
====================

.. tabularcolumns:: |p{5.0cm}|p{12.5cm}|

.. _dmx-output:

.. flat-table:: enum dmx_output
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _DMX-OUT-DECODER:

	  DMX_OUT_DECODER

       -  Streaming directly to decoder.

    -  .. row 3

       -  .. _DMX-OUT-TAP:

	  DMX_OUT_TAP

       -  Output going to a memory buffer (to be retrieved via the read
	  command). Delivers the stream output to the demux device on which
	  the ioctl is called.

    -  .. row 4

       -  .. _DMX-OUT-TS-TAP:

	  DMX_OUT_TS_TAP

       -  Output multiplexed into a new TS (to be retrieved by reading from
	  the logical DVR device). Routes output to the logical DVR device
	  ``/dev/dvb/adapter?/dvr?``, which delivers a TS multiplexed from
	  all filters for which ``DMX_OUT_TS_TAP`` was specified.

    -  .. row 5

       -  .. _DMX-OUT-TSDEMUX-TAP:

	  DMX_OUT_TSDEMUX_TAP

       -  Like :ref:`DMX_OUT_TS_TAP <DMX-OUT-TS-TAP>` but retrieved
	  from the DMX device.



.. _dmx-input-t:

dmx_input_t
===========


.. code-block:: c

    typedef enum
    {
	DMX_IN_FRONTEND, /* Input from a front-end device.  */
	DMX_IN_DVR       /* Input from the logical DVR device.  */
    } dmx_input_t;


.. _dmx-pes-type-t:

dmx_pes_type_t
==============


.. code-block:: c

    typedef enum
    {
	DMX_PES_AUDIO0,
	DMX_PES_VIDEO0,
	DMX_PES_TELETEXT0,
	DMX_PES_SUBTITLE0,
	DMX_PES_PCR0,

	DMX_PES_AUDIO1,
	DMX_PES_VIDEO1,
	DMX_PES_TELETEXT1,
	DMX_PES_SUBTITLE1,
	DMX_PES_PCR1,

	DMX_PES_AUDIO2,
	DMX_PES_VIDEO2,
	DMX_PES_TELETEXT2,
	DMX_PES_SUBTITLE2,
	DMX_PES_PCR2,

	DMX_PES_AUDIO3,
	DMX_PES_VIDEO3,
	DMX_PES_TELETEXT3,
	DMX_PES_SUBTITLE3,
	DMX_PES_PCR3,

	DMX_PES_OTHER
    } dmx_pes_type_t;


.. _dmx-filter:

struct dmx_filter
=================


.. code-block:: c

     typedef struct dmx_filter
    {
	__u8  filter[DMX_FILTER_SIZE];
	__u8  mask[DMX_FILTER_SIZE];
	__u8  mode[DMX_FILTER_SIZE];
    } dmx_filter_t;


.. _dmx-sct-filter-params:

struct dmx_sct_filter_params
============================


.. code-block:: c

    struct dmx_sct_filter_params
    {
	__u16          pid;
	dmx_filter_t   filter;
	__u32          timeout;
	__u32          flags;
    #define DMX_CHECK_CRC       1
    #define DMX_ONESHOT         2
    #define DMX_IMMEDIATE_START 4
    #define DMX_KERNEL_CLIENT   0x8000
    };


.. _dmx-pes-filter-params:

struct dmx_pes_filter_params
============================


.. code-block:: c

    struct dmx_pes_filter_params
    {
	__u16          pid;
	dmx_input_t    input;
	dmx_output_t   output;
	dmx_pes_type_t pes_type;
	__u32          flags;
    };


.. _dmx-event:

struct dmx_event
================


.. code-block:: c

     struct dmx_event
     {
	 dmx_event_t          event;
	 time_t               timeStamp;
	 union
	 {
	     dmx_scrambling_status_t scrambling;
	 } u;
     };


.. _dmx-stc:

struct dmx_stc
==============


.. code-block:: c

    struct dmx_stc {
	unsigned int num;   /* input : which STC? 0..N */
	unsigned int base;  /* output: divisor for stc to get 90 kHz clock */
	__u64 stc;      /* output: stc in 'base'*90 kHz units */
    };


.. _dmx-caps:

struct dmx_caps
===============


.. code-block:: c

     typedef struct dmx_caps {
	__u32 caps;
	int num_decoders;
    } dmx_caps_t;


.. _dmx-source-t:

enum dmx_source_t
=================


.. code-block:: c

    typedef enum {
	DMX_SOURCE_FRONT0 = 0,
	DMX_SOURCE_FRONT1,
	DMX_SOURCE_FRONT2,
	DMX_SOURCE_FRONT3,
	DMX_SOURCE_DVR0   = 16,
	DMX_SOURCE_DVR1,
	DMX_SOURCE_DVR2,
	DMX_SOURCE_DVR3
    } dmx_source_t;
