
/*
 * VMEbus Master Window Configuration Structure
 */
typedef struct {
	char enable;			/* State of Window */
	unsigned long long vme_addr;	/* Starting Address on the VMEbus */
	unsigned long long size;	/* Window Size */
	vme_address_t aspace;		/* Address Space */
	vme_cycle_t cycle;		/* Cycle properties */
	vme_width_t dwidth;		/* Maximum Data Width */
#if 0
	char prefetchEnable;		/* Prefetch Read Enable State */
	int prefetchSize;		/* Prefetch Read Size (Cache Lines) */
	char wrPostEnable;		/* Write Post State */
#endif
} vme_master_t;


/*
 * IOCTL Commands and structures
 */

/* Magic number for use in ioctls */
#define VME_IOC_MAGIC 0xAE


/* VMEbus Slave Window Configuration Structure */
typedef struct {
	char enable;			/* State of Window */
	unsigned long long vme_addr;	/* Starting Address on the VMEbus */
	unsigned long long size;	/* Window Size */
	vme_address_t aspace;		/* Address Space */
	vme_cycle_t cycle;		/* Cycle properties */
#if 0
	char wrPostEnable;		/* Write Post State */
	char rmwLock;			/* Lock PCI during RMW Cycles */
	char data64BitCapable;		/* non-VMEbus capable of 64-bit Data */
#endif
} vme_slave_t;

#define VME_SET_SLAVE _IOW(VME_IOC_MAGIC, 10, vme_slave_t)

