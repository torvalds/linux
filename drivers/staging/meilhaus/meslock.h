/**
 * @file meslock.h
 *
 * @brief Provides the subdevice lock class.
 * @note Copyright (C) 2006 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

#ifndef _MESLOCK_H_
#define _MESLOCK_H_

#include <linux/spinlock.h>

#ifdef __KERNEL__

/**
 * @brief The subdevice lock class.
 */
typedef struct me_slock {
	struct file *filep;		/**< Pointer to file structure holding the subdevice. */
	int count;			/**< Number of tasks which are inside the subdevice. */
	spinlock_t spin_lock;		/**< Spin lock protecting the attributes from concurrent access. */
} me_slock_t;

/**
 * @brief Tries to enter a subdevice.
 *
 * @param slock The subdevice lock instance.
 * @param filep The file structure identifying the calling process.
 *
 * @return 0 on success.
 */
int me_slock_enter(struct me_slock *slock, struct file *filep);

/**
 * @brief Exits a subdevice.
 *
 * @param slock The subdevice lock instance.
 * @param filep The file structure identifying the calling process.
 *
 * @return 0 on success.
 */
int me_slock_exit(struct me_slock *slock, struct file *filep);

/**
 * @brief Tries to perform a locking action on a subdevice.
 *
 * @param slock The subdevice lock instance.
 * @param filep The file structure identifying the calling process.
 * @param The action to be done.
 *
 * @return 0 on success.
 */
int me_slock_lock(struct me_slock *slock, struct file *filep, int lock);

/**
 * @brief Initializes a lock structure.
 *
 * @param slock The lock structure to initialize.
 * @return 0 on success.
 */
int me_slock_init(me_slock_t * slock);

/**
 * @brief Deinitializes a lock structure.
 *
 * @param slock The lock structure to deinitialize.
 * @return 0 on success.
 */
void me_slock_deinit(me_slock_t * slock);

#endif
#endif
