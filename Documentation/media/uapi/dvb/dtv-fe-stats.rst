.. -*- coding: utf-8; mode: rst -*-

.. c:type:: dtv_fe_stats

*******************
struct dtv_fe_stats
*******************


.. code-block:: c

    #define MAX_DTV_STATS   4

    struct dtv_fe_stats {
	__u8 len;
	struct dtv_stats stat[MAX_DTV_STATS];
    } __packed;
