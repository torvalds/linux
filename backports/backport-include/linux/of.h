#ifndef _COMPAT_LINUX_OF_H
#define _COMPAT_LINUX_OF_H 1

#include <linux/version.h>
#include_next <linux/of.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
#ifdef CONFIG_OF
extern struct device_node *of_get_child_by_name(const struct device_node *node,
						const char *name);
#else
static inline struct device_node *of_get_child_by_name(
					const struct device_node *node,
					const char *name)
{
	return NULL;
}
#endif /* CONFIG_OF */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
#ifndef CONFIG_OF
static inline struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name)
{
	return NULL;
}
#endif /* CONFIG_OF */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
#define of_property_read_u8_array LINUX_BACKPORT(of_property_read_u8_array)
#ifdef CONFIG_OF
extern int of_property_read_u8_array(const struct device_node *np,
			const char *propname, u8 *out_values, size_t sz);
#else
static inline int of_property_read_u8_array(const struct device_node *np,
			const char *propname, u8 *out_values, size_t sz)
{
	return -ENOSYS;
}
#endif /* CONFIG_OF */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
#define of_property_read_u32_array LINUX_BACKPORT(of_property_read_u32_array)
#ifdef CONFIG_OF
extern int of_property_read_u32_array(const struct device_node *np,
				      const char *propname,
				      u32 *out_values,
				      size_t sz);
#else
static inline int of_property_read_u32_array(const struct device_node *np,
					     const char *propname,
					     u32 *out_values, size_t sz)
{
	return -ENOSYS;
}
#endif /* CONFIG_OF */
#define of_property_read_u32 LINUX_BACKPORT(of_property_read_u32)
static inline int of_property_read_u32(const struct device_node *np,
				       const char *propname,
				       u32 *out_value)
{
	return of_property_read_u32_array(np, propname, out_value, 1);
}
#ifndef CONFIG_OF
#define of_get_property LINUX_BACKPORT(of_get_property)
static inline const void *of_get_property(const struct device_node *node,
				const char *name,
				int *lenp)
{
	return NULL;
}

#endif
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#define of_property_read_u32_index LINUX_BACKPORT(of_property_read_u32_index)
#ifdef CONFIG_OF
extern int of_property_read_u32_index(const struct device_node *np,
				       const char *propname,
				       u32 index, u32 *out_value);
/* This is static in the kernel, but we need it in multiple places */
void *of_find_property_value_of_size(const struct device_node *np,
				     const char *propname, u32 len);
#else
static inline int of_property_read_u32_index(const struct device_node *np,
			const char *propname, u32 index, u32 *out_value)
{
	return -ENOSYS;
}
#endif /* CONFIG_OF */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
#define of_property_count_elems_of_size LINUX_BACKPORT(of_property_count_elems_of_size)
#ifdef CONFIG_OF
extern int of_property_count_elems_of_size(const struct device_node *np,
				const char *propname, int elem_size);
#else
static inline int of_property_count_elems_of_size(const struct device_node *np,
			const char *propname, int elem_size)
{
	return -ENOSYS;
}
#endif /* CONFIG_OF */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0) */


#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
/**
 * of_property_count_u32_elems - Count the number of u32 elements in a property
 *
 * @np:		device node from which the property value is to be read.
 * @propname:	name of the property to be searched.
 *
 * Search for a property in a device node and count the number of u32 elements
 * in it. Returns number of elements on sucess, -EINVAL if the property does
 * not exist or its length does not match a multiple of u32 and -ENODATA if the
 * property does not have a value.
 */
#define of_property_count_u32_elems LINUX_BACKPORT(of_property_count_u32_elems)
static inline int of_property_count_u32_elems(const struct device_node *np,
				const char *propname)
{
	return of_property_count_elems_of_size(np, propname, sizeof(u32));
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
#ifndef CONFIG_OF
#define of_node_get LINUX_BACKPORT(of_node_get)
/* Dummy ref counting routines - to be implemented later */
static inline struct device_node *of_node_get(struct device_node *node)
{
	return node;
}
static inline void of_node_put(struct device_node *node) { }
#endif /* CONFIG_OF */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0) */

#ifndef of_match_ptr
#ifdef CONFIG_OF
#define of_match_ptr(_ptr)	(_ptr)
#else
#define of_match_ptr(_ptr)	NULL
#endif /* CONFIG_OF */
#endif /* of_match_ptr */

#ifndef for_each_compatible_node
#define for_each_compatible_node(dn, type, compatible) \
	for (dn = of_find_compatible_node(NULL, type, compatible); dn; \
	     dn = of_find_compatible_node(dn, type, compatible))
#endif /* for_each_compatible_node */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
#ifndef CONFIG_OF
static inline struct device_node *of_find_compatible_node(
						struct device_node *from,
						const char *type,
						const char *compat)
{
	return NULL;
}
#endif
#endif

#endif	/* _COMPAT_LINUX_OF_H */
