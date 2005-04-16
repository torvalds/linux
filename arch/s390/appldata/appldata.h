/*
 * arch/s390/appldata/appldata.h
 *
 * Definitions and interface for Linux - z/VM Monitor Stream.
 *
 * Copyright (C) 2003 IBM Corporation, IBM Deutschland Entwicklung GmbH.
 *
 * Author: Gerald Schaefer <geraldsc@de.ibm.com>
 */

//#define APPLDATA_DEBUG			/* Debug messages on/off */

#define APPLDATA_MAX_REC_SIZE	  4024	/* Maximum size of the */
					/* data buffer */
#define APPLDATA_MAX_PROCS 100

#define APPLDATA_PROC_NAME_LENGTH 16	/* Max. length of /proc name */

#define APPLDATA_RECORD_MEM_ID		0x01	/* IDs to identify the */
#define APPLDATA_RECORD_OS_ID		0x02	/* individual records, */
#define APPLDATA_RECORD_NET_SUM_ID	0x03	/* must be < 256 !     */
#define APPLDATA_RECORD_PROC_ID		0x04

#define CTL_APPLDATA 		2120	/* sysctl IDs, must be unique */
#define CTL_APPLDATA_TIMER 	2121
#define CTL_APPLDATA_INTERVAL 	2122
#define CTL_APPLDATA_MEM	2123
#define CTL_APPLDATA_OS		2124
#define CTL_APPLDATA_NET_SUM	2125
#define CTL_APPLDATA_PROC	2126

#define P_INFO(x...)	printk(KERN_INFO MY_PRINT_NAME " info: " x)
#define P_ERROR(x...)	printk(KERN_ERR MY_PRINT_NAME " error: " x)
#define P_WARNING(x...)	printk(KERN_WARNING MY_PRINT_NAME " status: " x)

#ifdef APPLDATA_DEBUG
#define P_DEBUG(x...)   printk(KERN_DEBUG MY_PRINT_NAME " debug: " x)
#else
#define P_DEBUG(x...)   do {} while (0)
#endif

struct appldata_ops {
	struct list_head list;
	struct ctl_table_header *sysctl_header;
	struct ctl_table *ctl_table;
	int    active;				/* monitoring status */

	/* fill in from here */
	unsigned int ctl_nr;			/* sysctl ID */
	char name[APPLDATA_PROC_NAME_LENGTH];	/* name of /proc fs node */
	unsigned char record_nr;		/* Record Nr. for Product ID */
	void (*callback)(void *data);		/* callback function */
	void *data;				/* record data */
	unsigned int size;			/* size of record */
	struct module *owner;			/* THIS_MODULE */
};

extern int appldata_register_ops(struct appldata_ops *ops);
extern void appldata_unregister_ops(struct appldata_ops *ops);
