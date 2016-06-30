.. -*- coding: utf-8; mode: rst -*-

.. _dtv-fe-stats:

*******************
struct dtv_fe_stats
*******************


.. code-block:: c

    #define MAX_DTV_STATS   4

    struct dtv_fe_stats {
        __u8 len;
        struct dtv_stats stat[MAX_DTV_STATS];
    } __packed;




.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
