/**
 * @file medlock.h
 *
 * @brief Provides the device lock class.
 * @note Copyright (C) 2006 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

#ifndef _MEDLOCK_H_
#define _MEDLOCK_H_

#include <linux/spinlock.h>

#ifdef __KERNEL__

/**
 * @brief The device lock class.
 */
typedef struct me_dlock {
	struct file *filep;		/**< Pointer to file structure holding the device. */
	int count;				/**< Number of tasks which are inside the device. */
	spinlock_t spin_lock;	/**< Spin lock protecting the attributes from concurrent access. */
} me_dlock_t;

/**
 * @brief Tries to enter a device.
 *
 * @param dlock The device lock instance.
 * @param filep The file structure identifying the calling process.
 *
 * @return 0 on success.
 */
int me_dlock_enter(struct me_dlock *dlock, struct file *filep);

/**
 * @brief Exits a device.
 *
 * @param dlock The device lock instance.
 * @param filep The file structure identifying the calling process.
 *
 * @return 0 on success.
 */
int me_dlock_exit(struct me_dlock *dlock, struct file *filep);

/**
 * @brief Tries to perform a locking action on a device.
 *
 * @param dlock The device lock instance.
 * @param filep The file structure identifying the calling process.
 * @param The action to be done.
 * @param flags Flags from user space.
 * @param slist The subdevice list of the device.
 *
 * @return 0 on success.
 */
int me_dlock_lock(struct me_dlock *dlock,
		  struct file *filep, int lock, int flags, me_slist_t * slist);

/**
 * @brief Initializes a lock structure.
 *
 * @param dlock The lock structure to initialize.
 * @return 0 on success.
 */
int me_dlock_init(me_dlock_t * dlock);

/**
 * @brief Deinitializes a lock structure.
 *
 * @param dlock The lock structure to deinitialize.
 * @return 0 on success.
 */
void me_dlock_deinit(me_dlock_t * dlock);

#endif
#endif
