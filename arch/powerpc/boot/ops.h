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

#include "types.h"

#define	COMMAND_LINE_SIZE	512
#define	MAX_PATH_LEN		256
#define	MAX_PROP_LEN		256 /* What should this be? */

/* Platform specific operations */
struct platform_ops {
	void	(*fixups)(void);
	void	(*image_hdr)(const void *);
	void *	(*malloc)(u32 size);
	void	(*free)(void *ptr);
	void *	(*realloc)(void *ptr, unsigned long size);
	void	(*exit)(void);
};
extern struct platform_ops platform_ops;

/* Device Tree operations */
struct dt_ops {
	void *	(*finddevice)(const char *name);
	int	(*getprop)(const void *phandle, const char *name, void *buf,
			const int buflen);
	int	(*setprop)(const void *phandle, const char *name,
			const void *buf, const int buflen);
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

int platform_init(void *promptr, char *dt_blob_start, char *dt_blob_end);
int ft_init(void *dt_blob, unsigned int max_size, unsigned int max_find_device);
int serial_console_init(void);
int ns16550_console_init(void *devp, struct serial_console_data *scdp);
void *simple_alloc_init(char *base, u32 heap_size, u32 granularity,
		u32 max_allocs);


static inline void *finddevice(const char *name)
{
	return (dt_ops.finddevice) ? dt_ops.finddevice(name) : NULL;
}

static inline int getprop(void *devp, const char *name, void *buf, int buflen)
{
	return (dt_ops.getprop) ? dt_ops.getprop(devp, name, buf, buflen) : -1;
}

static inline int setprop(void *devp, const char *name, void *buf, int buflen)
{
	return (dt_ops.setprop) ? dt_ops.setprop(devp, name, buf, buflen) : -1;
}

static inline void *malloc(u32 size)
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

#endif /* _PPC_BOOT_OPS_H_ */
