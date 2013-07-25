#ifndef _VME_USER_H_
#define _VME_USER_H_

#define VME_USER_BUS_MAX	1

/*
 * VMEbus Master Window Configuration Structure
 */
struct vme_master {
	int enable;			/* State of Window */
	unsigned long long vme_addr;	/* Starting Address on the VMEbus */
	unsigned long long size;	/* Window Size */
	u32 aspace;			/* Address Space */
	u32 cycle;		/* Cycle properties */
	u32 dwidth;		/* Maximum Data Width */
#if 0
	char prefetchenable;		/* Prefetch Read Enable State */
	int prefetchsize;		/* Prefetch Read Size (Cache Lines) */
	char wrpostenable;		/* Write Post State */
#endif
};


/*
 * IOCTL Commands and structures
 */

/* Magic number for use in ioctls */
#define VME_IOC_MAGIC 0xAE


/* VMEbus Slave Window Configuration Structure */
struct vme_slave {
	int enable;			/* State of Window */
	unsigned long long vme_addr;	/* Starting Address on the VMEbus */
	unsigned long long size;	/* Window Size */
	u32 aspace;			/* Address Space */
	u32 cycle;		/* Cycle properties */
#if 0
	char wrpostenable;		/* Write Post State */
	char rmwlock;			/* Lock PCI during RMW Cycles */
	char data64bitcapable;		/* non-VMEbus capable of 64-bit Data */
#endif
};

struct vme_irq_id {
	__u8 level;
	__u8 statid;
};

#define VME_GET_SLAVE _IOR(VME_IOC_MAGIC, 1, struct vme_slave)
#define VME_SET_SLAVE _IOW(VME_IOC_MAGIC, 2, struct vme_slave)
#define VME_GET_MASTER _IOR(VME_IOC_MAGIC, 3, struct vme_master)
#define VME_SET_MASTER _IOW(VME_IOC_MAGIC, 4, struct vme_master)
#define VME_IRQ_GEN _IOW(VME_IOC_MAGIC, 5, struct vme_irq_id)

#endif /* _VME_USER_H_ */

