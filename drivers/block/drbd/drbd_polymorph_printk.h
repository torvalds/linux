/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DRBD_POLYMORPH_PRINTK_H
#define DRBD_POLYMORPH_PRINTK_H

#define __drbd_printk_device(level, device, fmt, args...) \
	dev_printk(level, disk_to_dev((device)->vdisk), fmt, ## args)
#define __drbd_printk_peer_device(level, peer_device, fmt, args...) \
	dev_printk(level, disk_to_dev((peer_device)->device->vdisk), fmt, ## args)
#define __drbd_printk_resource(level, resource, fmt, args...) \
	printk(level "drbd %s: " fmt, (resource)->name, ## args)
#define __drbd_printk_connection(level, connection, fmt, args...) \
	printk(level "drbd %s: " fmt, (connection)->resource->name, ## args)

void drbd_printk_with_wrong_object_type(void);

#define __drbd_printk_if_same_type(obj, type, func, level, fmt, args...) \
	(__builtin_types_compatible_p(typeof(obj), type) || \
	 __builtin_types_compatible_p(typeof(obj), const type)), \
	func(level, (const type)(obj), fmt, ## args)

#define drbd_printk(level, obj, fmt, args...) \
	__builtin_choose_expr( \
	  __drbd_printk_if_same_type(obj, struct drbd_device *, \
			     __drbd_printk_device, level, fmt, ## args), \
	  __builtin_choose_expr( \
	    __drbd_printk_if_same_type(obj, struct drbd_resource *, \
			       __drbd_printk_resource, level, fmt, ## args), \
	    __builtin_choose_expr( \
	      __drbd_printk_if_same_type(obj, struct drbd_connection *, \
				 __drbd_printk_connection, level, fmt, ## args), \
	      __builtin_choose_expr( \
		__drbd_printk_if_same_type(obj, struct drbd_peer_device *, \
				 __drbd_printk_peer_device, level, fmt, ## args), \
		drbd_printk_with_wrong_object_type()))))

#define drbd_dbg(obj, fmt, args...) \
	drbd_printk(KERN_DEBUG, obj, fmt, ## args)
#define drbd_alert(obj, fmt, args...) \
	drbd_printk(KERN_ALERT, obj, fmt, ## args)
#define drbd_err(obj, fmt, args...) \
	drbd_printk(KERN_ERR, obj, fmt, ## args)
#define drbd_warn(obj, fmt, args...) \
	drbd_printk(KERN_WARNING, obj, fmt, ## args)
#define drbd_info(obj, fmt, args...) \
	drbd_printk(KERN_INFO, obj, fmt, ## args)
#define drbd_emerg(obj, fmt, args...) \
	drbd_printk(KERN_EMERG, obj, fmt, ## args)

#define dynamic_drbd_dbg(device, fmt, args...) \
	dynamic_dev_dbg(disk_to_dev(device->vdisk), fmt, ## args)

#define D_ASSERT(x, exp)							\
	do {									\
		if (!(exp))							\
			drbd_err(x, "ASSERTION %s FAILED in %s\n",		\
				#exp, __func__);				\
	} while (0)

/**
 * expect  -  Make an assertion
 *
 * Unlike the assert macro, this macro returns a boolean result.
 */
#define expect(exp) ({								\
		bool _bool = (exp);						\
		if (!_bool)							\
			drbd_err(device, "ASSERTION %s FAILED in %s\n",		\
			        #exp, __func__);				\
		_bool;								\
		})

#endif
