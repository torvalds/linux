/*
 * The MobiCore Driver Kernel Module is a Linux device driver, which represents
 * the command proxy on the lowest layer to the secure world (Swd). Additional
 * services like memory allocation via mmap and generation of a L2 tables for
 * given virtual memory are also supported. IRQ functionality receives
 * information from the SWd in the non secure world (NWd).
 * As customary the driver is handled as linux device driver with "open",
 * "close" and "ioctl" commands. Access to the driver is possible after the
 * device "/dev/mobicore" has been opened.
 * The MobiCore Driver Kernel Module must be installed via
 * "insmod mcDrvModule.ko".
 *
 * <-- Copyright Giesecke & Devrient GmbH 2010-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *	products derived from this software without specific prior
 *	written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MC_LINUX_H_
#define _MC_LINUX_H_

#include "version.h"

#define MC_ADMIN_DEVNODE	"mobicore"
#define MC_USER_DEVNODE		"mobicore-user"

/*
 * Data exchange structure of the MC_DRV_MODULE_INIT ioctl command.
 * INIT request data to SWD
 */
struct mc_ioctl_init {
	/* notification buffer start/length [16:16] [start, length] */
	uint32_t  nq_offset;
	/* length of notification queue */
	uint32_t  nq_length;
	/* mcp buffer start/length [16:16] [start, length] */
	uint32_t  mcp_offset;
	/* length of mcp buffer */
	uint32_t  mcp_length;
};

/*
 * Data exchange structure of the MC_DRV_MODULE_INFO ioctl command.
 * INFO request data to the SWD
 */
struct mc_ioctl_info {
	uint32_t  ext_info_id;	/* extended info ID */
	uint32_t  state;	/* state */
	uint32_t  ext_info;	/* extended info */
};

/*
 * Data exchange structure of the MC_IO_MAP_WSM, MC_IO_MAP_MCI, and
 *				  MC_IO_MAP_PWSM commands.
 *
 * Allocate a contiguous memory buffer for a process.
 * The physical address can be used as for later calls to mmap.
 * The handle can be used to communicate about this buffer to the Daemon.
 * For MC_IO_MAP_MCI command, the reused field indicates that MCI was set up
 * already. I.e. Daemon was restarted.
 */
struct mc_ioctl_map {
	size_t	      len;	/* Buffer length */
	uint32_t      handle;	/* WSM handle */
	unsigned long addr;	/* Virtual address */
	unsigned long phys_addr;/* physical address of WSM (or NULL) */
	bool	      reused;	/* if WSM memory was reused, or new allocated */
};

/*
 * Data exchange structure of the MC_IO_REG_WSM command.
 *
 * Allocates a physical L2 table and maps the buffer into this page.
 * Returns the physical address of the L2 table.
 * The page alignment will be created and the appropriated pSize and pOffsetL2
 * will be modified to the used values.
 */
struct mc_ioctl_reg_wsm {
	uint32_t buffer;	/* base address of the virtual address  */
	uint32_t len;		/* size of the virtual address space */
	uint32_t pid;		/* process id */
	uint32_t handle;	/* driver handle for locked memory */
	uint32_t table_phys;	/* physical address of the L2 table */
};


/*
 * Data exchange structure of the MC_DRV_MODULE_FC_EXECUTE ioctl command.
 * internal, unsupported
 */
struct mc_ioctl_execute {
	/* base address of mobicore binary */
	uint32_t phys_start_addr;
	/* length of DDR area */
	uint32_t length;
};

/*
 * Data exchange structure of the MC_IO_RESOLVE_CONT_WSM ioctl command.
 */
struct mc_ioctl_resolv_cont_wsm {
	/* driver handle for buffer */
	uint32_t handle;
	/* base address of memory */
	uint32_t phys;
	/* length memory */
	uint32_t length;
	/* fd to owner of the buffer */
	int32_t fd;
};

/*
 * Data exchange structure of the MC_IO_RESOLVE_WSM ioctl command.
 */
struct mc_ioctl_resolv_wsm {
	/* driver handle for buffer */
	uint32_t handle;
	/* fd to owner of the buffer */
	int32_t fd;
	/* base address of memory */
	uint32_t phys;
};


/*
 * defines for the ioctl mobicore driver module function call from user space.
 */
/* MobiCore IOCTL magic number */
#define MC_IOC_MAGIC	'M'

#define MC_IO_INIT		_IOWR(MC_IOC_MAGIC, 0, struct mc_ioctl_init)
#define MC_IO_INFO		_IOWR(MC_IOC_MAGIC, 1, struct mc_ioctl_info)
#define MC_IO_VERSION		_IOR(MC_IOC_MAGIC, 2, uint32_t)
/*
 * ioctl parameter to send the YIELD command to the SWD.
 * Only possible in Privileged Mode.
 * ioctl(fd, MC_DRV_MODULE_YIELD)
 */
#define MC_IO_YIELD		_IO(MC_IOC_MAGIC, 3)
/*
 * ioctl parameter to send the NSIQ signal to the SWD.
 * Only possible in Privileged Mode
 * ioctl(fd, MC_DRV_MODULE_NSIQ)
 */
#define MC_IO_NSIQ		_IO(MC_IOC_MAGIC, 4)
/*
 * Free's memory which is formerly allocated by the driver's mmap
 * command. The parameter must be this mmaped address.
 * The internal instance data regarding to this address are deleted as
 * well as each according memory page and its appropriated reserved bit
 * is cleared (ClearPageReserved).
 * Usage: ioctl(fd, MC_DRV_MODULE_FREE, &address) with address being of
 * type long address
 */
#define MC_IO_FREE		_IO(MC_IOC_MAGIC, 5)
/*
 * Creates a L2 Table of the given base address and the size of the
 * data.
 * Parameter: mc_ioctl_app_reg_wsm_l2_params
 */
#define MC_IO_REG_WSM		_IOWR(MC_IOC_MAGIC, 6, struct mc_ioctl_reg_wsm)
#define MC_IO_UNREG_WSM		_IO(MC_IOC_MAGIC, 7)
#define MC_IO_LOCK_WSM		_IO(MC_IOC_MAGIC, 8)
#define MC_IO_UNLOCK_WSM	_IO(MC_IOC_MAGIC, 9)
#define MC_IO_EXECUTE		_IOWR(MC_IOC_MAGIC, 10, struct mc_ioctl_execute)

/*
 * Allocate contiguous memory for a process for later mapping with mmap.
 * MC_DRV_KMOD_MMAP_WSM	usual operation, pages are registered in
 *					device structure and freed later.
 * MC_DRV_KMOD_MMAP_MCI	get Instance of MCI, allocates or mmaps
 *					the MCI to daemon
 * MC_DRV_KMOD_MMAP_PERSISTENTWSM	special operation, without
 *						registration of pages
 */
#define MC_IO_MAP_WSM		_IOWR(MC_IOC_MAGIC, 11, struct mc_ioctl_map)
#define MC_IO_MAP_MCI		_IOWR(MC_IOC_MAGIC, 12, struct mc_ioctl_map)
#define MC_IO_MAP_PWSM		_IOWR(MC_IOC_MAGIC, 13, struct mc_ioctl_map)

/*
 * Clean orphaned WSM buffers. Only available to the daemon and should
 * only be carried out if the TLC crashes or otherwise calls exit() in
 * an unexpected manner.
 * The clean is needed together with the lock/unlock mechanism so the daemon
 * has clear control of the mapped buffers so it can close a Trustlet before
 * release all the WSM buffers, otherwise the Trustlet would be able to write
 * to possibly kernel memory areas
 */
#define MC_IO_CLEAN_WSM		_IO(MC_IOC_MAGIC, 14)

/*
 * Get L2 phys address of a buffer handle allocated to the user.
 * Only available to the daemon.
 */
#define MC_IO_RESOLVE_WSM	_IOWR(MC_IOC_MAGIC, 15, \
					struct mc_ioctl_resolv_wsm)

/*
 * Get the phys address & length of a allocated contiguous buffer.
 * Only available to the daemon
 */
#define MC_IO_RESOLVE_CONT_WSM	_IOWR(MC_IOC_MAGIC, 16, \
					struct mc_ioctl_resolv_cont_wsm)

/*
 * Setup the mem traces when called.
 * Only available to the daemon
 */
#define MC_IO_LOG_SETUP		_IO(MC_IOC_MAGIC, 17)

#endif /* _MC_LINUX_H_ */
