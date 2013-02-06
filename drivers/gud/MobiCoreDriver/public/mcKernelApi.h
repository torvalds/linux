/** @addtogroup MCD_MCDIMPL_KMOD
 * @{
 * Interface to Mobicore Driver Kernel Module inside Kernel.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2010-2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MOBICORE_KERNELMODULE_API_H_
#define _MOBICORE_KERNELMODULE_API_H_

struct mcInstance;

/**
 * Initialize a new mobicore API instance object
 *
 * @return Instance or NULL if no allocation was possible.
 */
struct mcInstance *mobicore_open(
	void
);

/**
 * Release a mobicore instance object and all objects related to it
 * @param pInstance instance
 * @return 0 if Ok or -E ERROR
 */
int mobicore_release(
	struct mcInstance	*pInstance
);

/**
 * Free a WSM buffer allocated with mobicore_allocate_wsm
 * @param pInstance
 * @param handle		handle of the buffer
 *
 * @return 0 if no error
 *
 */
int mobicore_allocate_wsm(
	struct mcInstance	*pInstance,
	unsigned long		requestedSize,
	uint32_t		*pHandle,
	void			**pKernelVirtAddr,
	void			**pPhysAddr
);

/**
 * Free a WSM buffer allocated with mobicore_allocate_wsm
 * @param pInstance
 * @param handle		handle of the buffer
 *
 * @return 0 if no error
 *
 */
int mobicore_free(
	struct mcInstance	*pInstance,
	uint32_t		handle
);

/**
 * Map a virtual memory buffer structure to Mobicore
 * @param pInstance
 * @param addr		address of the buffer(NB it must be kernel virtual!)
 * @param len		buffer length
 * @param pHandle	pointer to handle
 * @param physWsmL2Table	pointer to physical L2 table(?)
 *
 * @return 0 if no error
 *
 */
int mobicore_map_vmem(
	struct mcInstance	*pInstance,
	void			*addr,
	uint32_t		len,
	uint32_t		*pHandle,
	void			**physWsmL2Table
);

/**
 * Unmap a virtual memory buffer from mobicore
 * @param pInstance
 * @param handle
 *
 * @return 0 if no error
 *
 */
int mobicore_unmap_vmem(
	struct mcInstance	*pInstance,
	uint32_t		handle
);
#endif /* _MOBICORE_KERNELMODULE_API_H_ */
/** @} */
