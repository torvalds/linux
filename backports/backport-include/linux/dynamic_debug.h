#ifndef __BACKPORT_LINUX_DYNAMIC_DEBUG_H
#define __BACKPORT_LINUX_DYNAMIC_DEBUG_H
#include <linux/version.h>
#include_next <linux/dynamic_debug.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
/* backports 07613b0b */
#if defined(CONFIG_DYNAMIC_DEBUG)
#if (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(6,4))
#define DEFINE_DYNAMIC_DEBUG_METADATA(name, fmt)               \
	static struct _ddebug __used __aligned(8)               \
	__attribute__((section("__verbose"))) name = {          \
		.modname = KBUILD_MODNAME,                      \
		.function = __func__,                           \
		.filename = __FILE__,                           \
		.format = (fmt),                                \
		.lineno = __LINE__,                             \
		.flags =  _DPRINTK_FLAGS_DEFAULT,               \
		.enabled = false,                               \
	}
#else
#define DEFINE_DYNAMIC_DEBUG_METADATA(name, fmt)               \
	static struct _ddebug __used __aligned(8)               \
	__attribute__((section("__verbose"))) name = {          \
		.modname = KBUILD_MODNAME,                      \
		.function = __func__,                           \
		.filename = __FILE__,                           \
		.format = (fmt),                                \
		.lineno = __LINE__,                             \
		.flags =  _DPRINTK_FLAGS_DEFAULT,               \
	}
#endif /* RHEL_RELEASE_CODE < 6.4 */
#endif /* defined(CONFIG_DYNAMIC_DEBUG) */
#endif /* < 3.2 */

#endif /* __BACKPORT_LINUX_DYNAMIC_DEBUG_H */
