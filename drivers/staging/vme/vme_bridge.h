#ifndef _VME_BRIDGE_H_
#define _VME_BRIDGE_H_

#define VME_CRCSR_BUF_SIZE (508*1024)
#define VME_SLOTS_MAX 32
/*
 * Resource structures
 */
struct vme_master_resource {
	struct list_head list;
	struct vme_bridge *parent;
	/*
	 * We are likely to need to access the VME bus in interrupt context, so
	 * protect master routines with a spinlock rather than a mutex.
	 */
	spinlock_t lock;
	int locked;
	int number;
	vme_address_t address_attr;
	vme_cycle_t cycle_attr;
	vme_width_t width_attr;
	struct resource pci_resource;	/* XXX Rename to be bus agnostic */
	void *kern_base;
};

struct vme_slave_resource {
	struct list_head list;
	struct vme_bridge *parent;
	struct mutex mtx;
	int locked;
	int number;
	vme_address_t address_attr;
	vme_cycle_t cycle_attr;
};

struct vme_dma_pattern {
	u32 pattern;
	vme_pattern_t type;
};

struct vme_dma_pci {
	dma_addr_t address;
};

struct vme_dma_vme {
	unsigned long long address;
	vme_address_t aspace;
	vme_cycle_t cycle;
	vme_width_t dwidth;
};

struct vme_dma_list {
	struct list_head list;
	struct vme_dma_resource *parent;
	struct list_head entries;
	struct mutex mtx;
};

struct vme_dma_resource {
	struct list_head list;
	struct vme_bridge *parent;
	struct mutex mtx;
	int locked;
	int number;
	struct list_head pending;
	struct list_head running;
};

struct vme_lm_resource {
	struct list_head list;
	struct vme_bridge *parent;
	struct mutex mtx;
	int locked;
	int number;
	int monitors;
};

struct vme_bus_error {
	struct list_head list;
	unsigned long long address;
	u32 attributes;
};

struct vme_callback {
	void (*func)(int, int, void*);
	void *priv_data;
};

struct vme_irq {
	int count;
	struct vme_callback callback[255];
};

/* Allow 16 characters for name (including null character) */
#define VMENAMSIZ 16

/* This structure stores all the information about one bridge
 * The structure should be dynamically allocated by the driver and one instance
 * of the structure should be present for each VME chip present in the system.
 *
 * Currently we assume that all chips are PCI-based
 */
struct vme_bridge {
        char name[VMENAMSIZ];
	int num;
	struct list_head master_resources;
	struct list_head slave_resources;
	struct list_head dma_resources;
	struct list_head lm_resources;

	struct list_head vme_errors;	/* List for errors generated on VME */

	/* Bridge Info - XXX Move to private structure? */
	struct device *parent;	/* Generic device struct (pdev->dev for PCI) */
	void * base;		/* Base Address of device registers */

	struct device dev[VME_SLOTS_MAX];	/* Device registered with
						 * device model on VME bus
						 */

	/* Interrupt callbacks */
	struct vme_irq irq[7];
	/* Locking for VME irq callback configuration */
	struct mutex irq_mtx;

	/* Slave Functions */
	int (*slave_get) (struct vme_slave_resource *, int *,
		unsigned long long *, unsigned long long *, dma_addr_t *,
		vme_address_t *, vme_cycle_t *);
	int (*slave_set) (struct vme_slave_resource *, int, unsigned long long,
		unsigned long long, dma_addr_t, vme_address_t, vme_cycle_t);

	/* Master Functions */
	int (*master_get) (struct vme_master_resource *, int *,
		unsigned long long *, unsigned long long *, vme_address_t *,
		vme_cycle_t *, vme_width_t *);
	int (*master_set) (struct vme_master_resource *, int,
		unsigned long long, unsigned long long,  vme_address_t,
		vme_cycle_t, vme_width_t);
	ssize_t (*master_read) (struct vme_master_resource *, void *, size_t,
		loff_t);
	ssize_t (*master_write) (struct vme_master_resource *, void *, size_t,
		loff_t);
	unsigned int (*master_rmw) (struct vme_master_resource *, unsigned int,
		unsigned int, unsigned int, loff_t);

	/* DMA Functions */
	int (*dma_list_add) (struct vme_dma_list *, struct vme_dma_attr *,
		struct vme_dma_attr *, size_t);
	int (*dma_list_exec) (struct vme_dma_list *);
	int (*dma_list_empty) (struct vme_dma_list *);

	/* Interrupt Functions */
	void (*irq_set) (int, int, int);
	int (*irq_generate) (int, int);

	/* Location monitor functions */
	int (*lm_set) (struct vme_lm_resource *, unsigned long long,
		vme_address_t, vme_cycle_t);
	int (*lm_get) (struct vme_lm_resource *, unsigned long long *,
		vme_address_t *, vme_cycle_t *);
	int (*lm_attach) (struct vme_lm_resource *, int, void (*callback)(int));
	int (*lm_detach) (struct vme_lm_resource *, int);

	/* CR/CSR space functions */
	int (*slot_get) (void);
	/* Use standard master read and write functions to access CR/CSR */

#if 0
	int (*set_prefetch) (void);
	int (*get_prefetch) (void);
	int (*set_arbiter) (void);
	int (*get_arbiter) (void);
	int (*set_requestor) (void);
	int (*get_requestor) (void);
#endif
};

void vme_irq_handler(struct vme_bridge *, int, int);

int vme_register_bridge (struct vme_bridge *);
void vme_unregister_bridge (struct vme_bridge *);

#endif /* _VME_BRIDGE_H_ */

#if 0
/*
 *  VMEbus GET INFO Arg Structure
 */
struct vmeInfoCfg {
	int vmeSlotNum;		/*  VME slot number of interest */
	int boardResponded;	/* Board responded */
	char sysConFlag;	/*  System controller flag */
	int vmeControllerID;	/*  Vendor/device ID of VME bridge */
	int vmeControllerRev;	/*  Revision of VME bridge */
	char osName[8];		/*  Name of OS e.g. "Linux" */
	int vmeSharedDataValid;	/*  Validity of data struct */
	int vmeDriverRev;	/*  Revision of VME driver */
	unsigned int vmeAddrHi[8];	/* Address on VME bus */
	unsigned int vmeAddrLo[8];	/* Address on VME bus */
	unsigned int vmeSize[8];	/* Size on VME bus */
	unsigned int vmeAm[8];	/* Address modifier on VME bus */
	int reserved;		/* For future use */
};
typedef struct vmeInfoCfg vmeInfoCfg_t;

/*
 *  VMEbus Requester Arg Structure
 */
struct vmeRequesterCfg {
	int requestLevel;	/*  Requester Bus Request Level */
	char fairMode;		/*  Requester Fairness Mode Indicator */
	int releaseMode;	/*  Requester Bus Release Mode */
	int timeonTimeoutTimer;	/*  Master Time-on Time-out Timer */
	int timeoffTimeoutTimer;	/*  Master Time-off Time-out Timer */
	int reserved;		/* For future use */
};
typedef struct vmeRequesterCfg vmeRequesterCfg_t;

/*
 *  VMEbus Arbiter Arg Structure
 */
struct vmeArbiterCfg {
	vme_arbitration_t arbiterMode;	/*  Arbitration Scheduling Algorithm */
	char arbiterTimeoutFlag;	/*  Arbiter Time-out Timer Indicator */
	int globalTimeoutTimer;	/*  VMEbus Global Time-out Timer */
	char noEarlyReleaseFlag;	/*  No Early Release on BBUSY */
	int reserved;		/* For future use */
};
typedef struct vmeArbiterCfg vmeArbiterCfg_t;


/*
 *  VMEbus RMW Configuration Data
 */
struct vmeRmwCfg {
	unsigned int targetAddrU;	/*  VME Address (Upper) to trigger RMW cycle */
	unsigned int targetAddr;	/*  VME Address (Lower) to trigger RMW cycle */
	vme_address_t addrSpace;	/*  VME Address Space */
	int enableMask;		/*  Bit mask defining the bits of interest */
	int compareData;	/*  Data to be compared with the data read */
	int swapData;		/*  Data written to the VMEbus on success */
	int maxAttempts;	/*  Maximum times to try */
	int numAttempts;	/*  Number of attempts before success */
	int reserved;		/* For future use */

};
typedef struct vmeRmwCfg vmeRmwCfg_t;

/*
 *  VMEbus Location Monitor Arg Structure
 */
struct vmeLmCfg {
	unsigned int addrU;	/*  Location Monitor Address upper */
	unsigned int addr;	/*  Location Monitor Address lower */
	vme_address_t addrSpace;	/*  Address Space */
	int userAccessType;	/*  User/Supervisor Access Type */
	int dataAccessType;	/*  Data/Program Access Type */
	int lmWait;		/* Time to wait for access */
	int lmEvents;		/* Lm event mask */
	int reserved;		/* For future use */
};
typedef struct vmeLmCfg vmeLmCfg_t;
#endif
