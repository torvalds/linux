.. -*- coding: utf-8; mode: rst -*-

.. _ca_data_types:

*************
CA Data Types
*************


.. _ca-slot-info:

ca_slot_info_t
==============


.. code-block:: c

    typedef struct ca_slot_info {
	int num;               /* slot number */

	int type;              /* CA interface this slot supports */
    #define CA_CI            1     /* CI high level interface */
    #define CA_CI_LINK       2     /* CI link layer level interface */
    #define CA_CI_PHYS       4     /* CI physical layer level interface */
    #define CA_DESCR         8     /* built-in descrambler */
    #define CA_SC          128     /* simple smart card interface */

	unsigned int flags;
    #define CA_CI_MODULE_PRESENT 1 /* module (or card) inserted */
    #define CA_CI_MODULE_READY   2
    } ca_slot_info_t;


.. _ca-descr-info:

ca_descr_info_t
===============


.. code-block:: c

    typedef struct ca_descr_info {
	unsigned int num;  /* number of available descramblers (keys) */
	unsigned int type; /* type of supported scrambling system */
    #define CA_ECD           1
    #define CA_NDS           2
    #define CA_DSS           4
    } ca_descr_info_t;


.. _ca-caps:

ca_caps_t
=========


.. code-block:: c

    typedef struct ca_caps {
	unsigned int slot_num;  /* total number of CA card and module slots */
	unsigned int slot_type; /* OR of all supported types */
	unsigned int descr_num; /* total number of descrambler slots (keys) */
	unsigned int descr_type;/* OR of all supported types */
     } ca_cap_t;


.. _ca-msg:

ca_msg_t
========


.. code-block:: c

    /* a message to/from a CI-CAM */
    typedef struct ca_msg {
	unsigned int index;
	unsigned int type;
	unsigned int length;
	unsigned char msg[256];
    } ca_msg_t;


.. _ca-descr:

ca_descr_t
==========


.. code-block:: c

    typedef struct ca_descr {
	unsigned int index;
	unsigned int parity;
	unsigned char cw[8];
    } ca_descr_t;


.. _ca-pid:

ca-pid
======


.. code-block:: c

    typedef struct ca_pid {
	unsigned int pid;
	int index;      /* -1 == disable*/
    } ca_pid_t;
