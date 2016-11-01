.. -*- coding: utf-8; mode: rst -*-

.. _dtv-stats:

****************
struct dtv_stats
****************


.. code-block:: c

    struct dtv_stats {
	__u8 scale; /* enum fecap_scale_params type */
	union {
	    __u64 uvalue;   /* for counters and relative scales */
	    __s64 svalue;   /* for 1/1000 dB measures */
	};
    } __packed;
