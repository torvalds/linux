/** @addtogroup MCD_MCDIMPL_KMOD
 * @{
 * Interface to Mobicore Driver Kernel Module.
 *
 * <h2>Introduction</h2>
 * The MobiCore Driver Kernel Module is a Linux device driver, which represents
 * the command proxy on the lowest layer to the secure world (Swd). Additional
 * services like memory allocation via mmap and generation of a L2 tables for
 * given virtual memory are also supported. IRQ functionallity receives
 * information from the SWd in the non secure world (NWd).
 * As customary the driver is handled as linux device driver with "open",
 * "close" and "ioctl" commands. Access to the driver is possible after the
 * device "/dev/mobicore" has been opened.
 * The MobiCore Driver Kernel Module must be installed via
 * "insmod mcDrvModule.ko".
 *
 *
 * <h2>Version history</h2>
 * <table class="customtab">
 * <tr><td width="100px"><b>Date</b></td><td width="80px"><b>Version</b></td>
 * <td><b>Changes</b></td></tr>
 * <tr><td>2010-05-25</td><td>0.1</td><td>Initial Release</td></tr>
 * </table>
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2010-2012 -->
 */

#ifndef _MC_DRV_MODULEAPI_H_
#define _MC_DRV_MODULEAPI_H_

#include "version.h"

#define MC_DRV_MOD_DEVNODE           "mobicore"
#define MC_DRV_MOD_DEVNODE_FULLPATH  "/dev/" MC_DRV_MOD_DEVNODE

/**
 * Data exchange structure of the MC_DRV_MODULE_INIT ioctl command.
 * INIT request data to SWD
 */
union mcIoCtlInitParams {
	struct {
		/** base address of mci buffer 4KB align */
		uint32_t  base;
		/** notification buffer start/length [16:16] [start, length] */
		uint32_t  nqOffset;
		/** length of notification queue */
		uint32_t  nqLength;
		/** mcp buffer start/length [16:16] [start, length] */
		uint32_t  mcpOffset;
		/** length of mcp buffer */
		uint32_t  mcpLength;
	} in;
	struct {
		/* nothing */
	} out;
};


/**
 * Data exchange structure of the MC_DRV_MODULE_INFO ioctl command.
 * INFO request data to the SWD
 */
union mcIoCtlInfoParams {
	struct {
		uint32_t  extInfoId; /**< extended info ID */
	} in;
	struct {
		uint32_t  state; /**< state */
		uint32_t  extInfo; /**< extended info */
	} out;
};

/**
 * Mmap allocates and maps contiguous memory into a process.
 * We use the third parameter, void *offset, to distinguish between some cases
 * offset = MC_DRV_KMOD_MMAP_WSM	usual operation, pages are registered in
					device structure and freed later.
 * offset = MC_DRV_KMOD_MMAP_MCI	get Instance of MCI, allocates or mmaps
					the MCI to daemon
 * offset = MC_DRV_KMOD_MMAP_PERSISTENTWSM	special operation, without
						registration of pages
 *
 * In mmap(), the offset specifies which of several device I/O pages is
 *  requested. Linux only transfers the page number, i.e. the upper 20 bits to
 *  kernel module. Therefore we define our special offsets as multiples of page
 *  size.
 */
enum mcMmapMemtype {
	MC_DRV_KMOD_MMAP_WSM		= 0,
	MC_DRV_KMOD_MMAP_MCI		= 4096,
	MC_DRV_KMOD_MMAP_PERSISTENTWSM	= 8192
};

struct mcMmapResp {
	uint32_t  handle; /**< WSN handle */
	uint32_t  physAddr; /**< physical address of WSM (or NULL) */
	bool	  isReused; /**< if WSM memory was reused, or new allocated */
};

/**
 * Data exchange structure of the MC_DRV_KMOD_IOCTL_FREE ioctl command.
 */
union mcIoCtltoFreeParams {
	struct {
		uint32_t  handle; /**< driver handle */
		uint32_t  pid; /**< process id */
	} in;
	struct {
		/* nothing */
	} out;
};


/**
 * Data exchange structure of the MC_DRV_KMOD_IOCTL_APP_REGISTER_WSM_L2 command.
 *
 * Allocates a physical L2 table and maps the buffer into this page.
 * Returns the physical address of the L2 table.
 * The page alignment will be created and the appropriated pSize and pOffsetL2
 * will be modified to the used values.
 */
union mcIoCtlAppRegWsmL2Params {
	struct {
		uint32_t  buffer; /**< base address of the virtual address  */
		uint32_t  len; /**< size of the virtual address space */
		uint32_t  pid; /**< process id */
	} in;
	struct {
		uint32_t  handle; /**< driver handle for locked memory */
		uint32_t  physWsmL2Table; /* physical address of the L2 table */
	} out;
};


/**
 * Data exchange structure of the MC_DRV_KMOD_IOCTL_APP_UNREGISTER_WSM_L2
 * command.
 */
struct mcIoCtlAppUnregWsmL2Params {
	struct {
		uint32_t  handle; /**< driver handle for locked memory */
		uint32_t  pid; /**< process id */
	} in;
	struct {
		/* nothing */
	} out;
};


/**
 * Data exchange structure of the MC_DRV_KMOD_IOCTL_DAEMON_LOCK_WSM_L2 command.
 */
struct mcIoCtlDaemonLockWsmL2Params {
	struct {
		uint32_t  handle; /**< driver handle for locked memory */
	} in;
	struct {
		uint32_t physWsmL2Table;
	} out;
};


/**
 * Data exchange structure of the MC_DRV_KMOD_IOCTL_DAEMON_UNLOCK_WSM_L2
 * command.
 */
struct mcIoCtlDaemonUnlockWsmL2Params {
	struct {
		uint32_t  handle; /**< driver handle for locked memory */
	} in;
	struct {
		/* nothing */
	} out;
};

/**
 * Data exchange structure of the MC_DRV_MODULE_FC_EXECUTE ioctl command.
 */
union mcIoCtlFcExecuteParams {
	struct {
		uint32_t  physStartAddr;/**< base address of mobicore binary */
		uint32_t  length;	/**< length of DDR area */
	} in;
	struct {
		/* nothing */
	} out;
};

/**
 * Data exchange structure of the MC_DRV_MODULE_GET_VERSION ioctl command.
 */
struct mcIoCtlGetVersionParams {
	struct {
		uint32_t    kernelModuleVersion;
	} out;
};

/* @defgroup Mobicore_Driver_Kernel_Module_Interface IOCTL */




/* TODO: use IOCTL macros like _IOWR. See Documentation/ioctl/ioctl-number.txt,
	Documentation/ioctl/ioctl-decoding.txt */
/**
 * defines for the ioctl mobicore driver module function call from user space.
 */
enum mcKModIoClt {

	/*
	 * get detailed MobiCore Status
	 */
	MC_DRV_KMOD_IOCTL_DUMP_STATUS  = 200,

	/*
	 * initialize MobiCore
	 */
	MC_DRV_KMOD_IOCTL_FC_INIT  = 201,

	/*
	 * get MobiCore status
	 */
	MC_DRV_KMOD_IOCTL_FC_INFO  = 202,

	/**
	 * ioctl parameter to send the YIELD command to the SWD.
	 * Only possible in Privileged Mode.
	 * ioctl(fd, MC_DRV_MODULE_YIELD)
	 */
	MC_DRV_KMOD_IOCTL_FC_YIELD =  203,
	/**
	 * ioctl parameter to send the NSIQ signal to the SWD.
	 * Only possible in Privileged Mode
	 * ioctl(fd, MC_DRV_MODULE_NSIQ)
	 */
	MC_DRV_KMOD_IOCTL_FC_NSIQ   =  204,
	/**
	 * ioctl parameter to tzbsp to start Mobicore binary from DDR.
	 * Only possible in Privileged Mode
	 * ioctl(fd, MC_DRV_KMOD_IOCTL_FC_EXECUTE)
	 */
	MC_DRV_KMOD_IOCTL_FC_EXECUTE =  205,

	/**
	 * Free's memory which is formerly allocated by the driver's mmap
	 * command. The parameter must be this mmaped address.
	 * The internal instance data regarding to this address are deleted as
	 * well as each according memory page and its appropriated reserved bit
	 * is cleared (ClearPageReserved).
	 * Usage: ioctl(fd, MC_DRV_MODULE_FREE, &address) with address beeing of
	 * type long address
	 */
	MC_DRV_KMOD_IOCTL_FREE = 218,

	/**
	 * Creates a L2 Table of the given base address and the size of the
	 * data.
	 * Parameter: mcIoCtlAppRegWsmL2Params
	 */
	MC_DRV_KMOD_IOCTL_APP_REGISTER_WSM_L2 = 220,

	/**
	 * Frees the L2 table created by a MC_DRV_KMOD_IOCTL_APP_REGISTER_WSM_L2
	 * ioctl.
	 * Parameter: mcIoCtlAppUnRegWsmL2Params
	 */
	MC_DRV_KMOD_IOCTL_APP_UNREGISTER_WSM_L2 = 221,


	/* TODO: comment this. */
	MC_DRV_KMOD_IOCTL_DAEMON_LOCK_WSM_L2 = 222,
	MC_DRV_KMOD_IOCTL_DAEMON_UNLOCK_WSM_L2 = 223,

    /**
     * Return kernel driver version.
     * Parameter: mcIoCtlGetVersionParams
     */
    MC_DRV_KMOD_IOCTL_GET_VERSION = 224,
};


#endif /* _MC_DRV_MODULEAPI_H_ */
/** @} */
