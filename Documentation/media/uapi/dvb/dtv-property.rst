.. -*- coding: utf-8; mode: rst -*-

.. c:type:: dtv_property

*******************
struct dtv_property
*******************


.. code-block:: c

    /* Reserved fields should be set to 0 */

    struct dtv_property {
	__u32 cmd;
	__u32 reserved[3];
	union {
	    __u32 data;
	    struct dtv_fe_stats st;
	    struct {
		__u8 data[32];
		__u32 len;
		__u32 reserved1[3];
		void *reserved2;
	    } buffer;
	} u;
	int result;
    } __attribute__ ((packed));

    /* num of properties cannot exceed DTV_IOCTL_MAX_MSGS per ioctl */
    #define DTV_IOCTL_MAX_MSGS 64
