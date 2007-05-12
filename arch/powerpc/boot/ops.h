/*
 * Global definition of all the bootwrapper operations.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2006 (c) MontaVista Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef _PPC_BOOT_OPS_H_
#define _PPC_BOOT_OPS_H_

#include <stddef.h>
#include "types.h"
#include "string.h"

#define	COMMAND_LINE_SIZE	512
#define	MAX_PATH_LEN		256
#define	MAX_PROP_LEN		256 /* What should this be? */

/* Platform specific operations */
struct platform_ops {
	void	(*fixups)(void);
	void	(*image_hdr)(const void *);
	void *	(*malloc)(unsigned long size);
	void	(*free)(void *ptr);
	void *	(*realloc)(void *ptr, unsigned long size);
	void	(*exit)(void);
	void *	(*vmlinux_alloc)(unsigned long size);
};
extern struct platform_ops platform_ops;

/* Device Tree operations */
struct dt_ops {
	void *	(*finddevice)(const char *name);
	int	(*getprop)(const void *phandle, const char *name, void *buf,
			const int buflen);
	int	(*setprop)(const void *phandle, const char *name,
			const void *buf, const int buflen);
	void *(*get_parent)(const void *phandle);
	/* The node must not already exist. */
	void *(*create_node)(const void *parent, const char *name);
	void *(*find_node_by_prop_value)(const void *prev,
	                                 const char *propname,
	                                 const char *propval, int proplen);
	unsigned long (*finalize)(void);
};
extern struct dt_ops dt_ops;

/* Console operations */
struct console_ops {
	int	(*open)(void);
	void	(*write)(char *buf, int len);
	void	(*edit_cmdline)(char *buf, int len);
	void	(*close)(void);
	void	*data;
};
extern struct console_ops console_ops;

/* Serial console operations */
struct serial_console_data {
	int		(*open)(void);
	void		(*putc)(unsigned char c);
	unsigned char	(*getc)(void);
	u8		(*tstc)(void);
	void		(*close)(void);
};

struct loader_info {
	void *promptr;
	unsigned long initrd_addr, initrd_size;
	char *cmdline;
	int cmdline_len;
};
extern struct loader_info loader_info;

void start(void);
int ft_init(void *dt_blob, unsigned int max_size, unsigned int max_find_device);
int serial_console_init(void);
int ns16550_console_init(void *devp, struct serial_console_data *scdp);
int mpsc_console_init(void *devp, struct serial_console_data *scdp);
void *simple_alloc_init(char *base, unsigned long heap_size,
			unsigned long granularity, unsigned long max_allocs);
extern void flush_cache(void *, unsigned long);
int dt_xlate_reg(void *node, int res, unsigned long *addr, unsigned long *size);
int dt_xlate_addr(void *node, u32 *buf, int buflen, unsigned long *xlated_addr);

static inline void *finddevice(const char *name)
{
	return (dt_ops.finddevice) ? dt_ops.finddevice(name) : NULL;
}

static inline int getprop(void *devp, const char *name, void *buf, int buflen)
{
	return (dt_ops.getprop) ? dt_ops.getprop(devp, name, buf, buflen) : -1;
}

static inline int setprop(void *devp, const char *name,
                          const void *buf, int buflen)
{
	return (dt_ops.setprop) ? dt_ops.setprop(devp, name, buf, buflen) : -1;
}
#define setprop_val(devp, name, val) \
	do { \
		typeof(val) x = (val); \
		setprop((devp), (name), &x, sizeof(x)); \
	} while (0)

static inline int setprop_str(void *devp, const char *name, const char *buf)
{
	if (dt_ops.setprop)
		return dt_ops.setprop(devp, name, buf, strlen(buf) + 1);

	return -1;
}

static inline void *get_parent(const char *devp)
{
	return dt_ops.get_parent ? dt_ops.get_parent(devp) : NULL;
}

static inline void *create_node(const void *parent, const char *name)
{
	return dt_ops.create_node ? dt_ops.create_node(parent, name) : NULL;
}


static inline void *find_node_by_prop_value(const void *prev,
                                            const char *propname,
                                            const char *propval, int proplen)
{
	if (dt_ops.find_node_by_prop_value)
		return dt_ops.find_node_by_prop_value(prev, propname,
		                                      propval, proplen);

	return NULL;
}

static inline void *find_node_by_prop_value_str(const void *prev,
                                                const char *propname,
                                                const char *propval)
{
	return find_node_by_prop_value(prev, propname, propval,
	                               strlen(propval) + 1);
}

static inline void *find_node_by_devtype(const void *prev,
                                         const char *type)
{
	return find_node_by_prop_value_str(prev, "device_type", type);
}

void dt_fixup_memory(u64 start, u64 size);
void dt_fixup_cpu_clocks(u32 cpufreq, u32 tbfreq, u32 busfreq);
void dt_fixup_clock(const char *path, u32 freq);
void __dt_fixup_mac_addresses(u32 startindex, ...);
#define dt_fixup_mac_addresses(...) \
	__dt_fixup_mac_addresses(0, __VA_ARGS__, NULL)


static inline void *find_node_by_linuxphandle(const u32 linuxphandle)
{
	return find_node_by_prop_value(NULL, "linux,phandle",
			(char *)&linuxphandle, sizeof(u32));
}

static inline void *malloc(unsigned long size)
{
	return (platform_ops.malloc) ? platform_ops.malloc(size) : NULL;
}

static inline void free(void *ptr)
{
	if (platform_ops.free)
		platform_ops.free(ptr);
}

static inline void exit(void)
{
	if (platform_ops.exit)
		platform_ops.exit();
	for(;;);
}
#define fatal(args...) { printf(args); exit(); }


#define BSS_STACK(size) \
	static char _bss_stack[size]; \
	void *_platform_stack_top = _bss_stack + sizeof(_bss_stack);

#endif /* _PPC_BOOT_OPS_H_ */
