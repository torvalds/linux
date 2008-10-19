/*
 * cs_internal.h -- definitions internal to the PCMCIA core modules
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 * (C) 2003 - 2008	Dominik Brodowski
 *
 *
 * This file contains definitions _only_ needed by the PCMCIA core modules.
 * It must not be included by PCMCIA socket drivers or by PCMCIA device
 * drivers.
 */

#ifndef _LINUX_CS_INTERNAL_H
#define _LINUX_CS_INTERNAL_H

#include <linux/kref.h>

/* Flags in client state */
#define CLIENT_WIN_REQ(i)	(0x1<<(i))

/* Each card function gets one of these guys */
typedef struct config_t {
	struct kref	ref;
	unsigned int	state;
	unsigned int	Attributes;
	unsigned int	IntType;
	unsigned int	ConfigBase;
	unsigned char	Status, Pin, Copy, Option, ExtStatus;
	unsigned int	CardValues;
	io_req_t	io;
	struct {
		u_int	Attributes;
	} irq;
} config_t;


struct cis_cache_entry {
	struct list_head	node;
	unsigned int		addr;
	unsigned int		len;
	unsigned int		attr;
	unsigned char		cache[0];
};

struct pccard_resource_ops {
	int	(*validate_mem)		(struct pcmcia_socket *s);
	int	(*adjust_io_region)	(struct resource *res,
					 unsigned long r_start,
					 unsigned long r_end,
					 struct pcmcia_socket *s);
	struct resource* (*find_io)	(unsigned long base, int num,
					 unsigned long align,
					 struct pcmcia_socket *s);
	struct resource* (*find_mem)	(unsigned long base, unsigned long num,
					 unsigned long align, int low,
					 struct pcmcia_socket *s);
	int	(*add_io)		(struct pcmcia_socket *s,
					 unsigned int action,
					 unsigned long r_start,
					 unsigned long r_end);
	int	(*add_mem)		(struct pcmcia_socket *s,
					 unsigned int action,
					 unsigned long r_start,
					 unsigned long r_end);
	int	(*init)			(struct pcmcia_socket *s);
	void	(*exit)			(struct pcmcia_socket *s);
};

/* Flags in config state */
#define CONFIG_LOCKED		0x01
#define CONFIG_IRQ_REQ		0x02
#define CONFIG_IO_REQ		0x04

/* Flags in socket state */
#define SOCKET_PRESENT		0x0008
#define SOCKET_INUSE		0x0010
#define SOCKET_SUSPEND		0x0080
#define SOCKET_WIN_REQ(i)	(0x0100<<(i))
#define SOCKET_CARDBUS		0x8000
#define SOCKET_CARDBUS_CONFIG	0x10000

static inline int cs_socket_get(struct pcmcia_socket *skt)
{
	int ret;

	WARN_ON(skt->state & SOCKET_INUSE);

	ret = try_module_get(skt->owner);
	if (ret)
		skt->state |= SOCKET_INUSE;
	return ret;
}

static inline void cs_socket_put(struct pcmcia_socket *skt)
{
	if (skt->state & SOCKET_INUSE) {
		skt->state &= ~SOCKET_INUSE;
		module_put(skt->owner);
	}
}

#ifdef CONFIG_PCMCIA_DEBUG
extern int cs_debug_level(int);

#define cs_dbg(skt, lvl, fmt, arg...) do {		\
	if (cs_debug_level(lvl))			\
		dev_printk(KERN_DEBUG, &skt->dev,	\
		 "cs: " fmt, ## arg);			\
} while (0)
#define __cs_dbg(lvl, fmt, arg...) do {			\
	if (cs_debug_level(lvl))			\
		printk(KERN_DEBUG 			\
		 "cs: " fmt, ## arg);			\
} while (0)

#else
#define cs_dbg(skt, lvl, fmt, arg...) do { } while (0)
#define __cs_dbg(lvl, fmt, arg...) do { } while (0)
#endif

#define cs_err(skt, fmt, arg...) \
	dev_printk(KERN_ERR, &skt->dev, "cs: " fmt, ## arg)


/*
 * Stuff internal to module "pcmcia_core":
 */

/* cistpl.c */
int verify_cis_cache(struct pcmcia_socket *s);

/* rsrc_mgr.c */
void release_resource_db(struct pcmcia_socket *s);

/* socket_sysfs.c */
extern int pccard_sysfs_add_socket(struct device *dev);
extern void pccard_sysfs_remove_socket(struct device *dev);

/* cardbus.c */
int cb_alloc(struct pcmcia_socket *s);
void cb_free(struct pcmcia_socket *s);
int read_cb_mem(struct pcmcia_socket *s, int space, u_int addr, u_int len,
		void *ptr);



/*
 * Stuff exported by module "pcmcia_core" to module "pcmcia"
 */

struct pcmcia_callback{
	struct module	*owner;
	int		(*event) (struct pcmcia_socket *s,
				  event_t event, int priority);
	void		(*requery) (struct pcmcia_socket *s, int new_cis);
	int		(*suspend) (struct pcmcia_socket *s);
	int		(*resume) (struct pcmcia_socket *s);
};

/* cs.c */
extern struct rw_semaphore pcmcia_socket_list_rwsem;
extern struct list_head pcmcia_socket_list;
extern struct class pcmcia_socket_class;

int pcmcia_get_window(struct pcmcia_socket *s,
		      window_handle_t *handle,
		      int idx,
		      win_req_t *req);
int pccard_register_pcmcia(struct pcmcia_socket *s, struct pcmcia_callback *c);
struct pcmcia_socket *pcmcia_get_socket_by_nr(unsigned int nr);

int pcmcia_suspend_card(struct pcmcia_socket *skt);
int pcmcia_resume_card(struct pcmcia_socket *skt);

int pcmcia_eject_card(struct pcmcia_socket *skt);
int pcmcia_insert_card(struct pcmcia_socket *skt);

struct pcmcia_socket *pcmcia_get_socket(struct pcmcia_socket *skt);
void pcmcia_put_socket(struct pcmcia_socket *skt);

/* cistpl.c */
int pcmcia_read_cis_mem(struct pcmcia_socket *s, int attr,
			u_int addr, u_int len, void *ptr);
void pcmcia_write_cis_mem(struct pcmcia_socket *s, int attr,
			  u_int addr, u_int len, void *ptr);
void release_cis_mem(struct pcmcia_socket *s);
void destroy_cis_cache(struct pcmcia_socket *s);
int pccard_read_tuple(struct pcmcia_socket *s, unsigned int function,
		      cisdata_t code, void *parse);
int pcmcia_replace_cis(struct pcmcia_socket *s,
		       const u8 *data, const size_t len);
int pccard_validate_cis(struct pcmcia_socket *s, unsigned int function,
			unsigned int *count);

/* rsrc_mgr.c */
int pcmcia_validate_mem(struct pcmcia_socket *s);
struct resource *pcmcia_find_io_region(unsigned long base,
				       int num,
				       unsigned long align,
				       struct pcmcia_socket *s);
int pcmcia_adjust_io_region(struct resource *res,
			    unsigned long r_start,
			    unsigned long r_end,
			    struct pcmcia_socket *s);
struct resource *pcmcia_find_mem_region(u_long base,
					u_long num,
					u_long align,
					int low,
					struct pcmcia_socket *s);

/*
 * Stuff internal to module "pcmcia".
 */
/* ds.c */
extern struct bus_type pcmcia_bus_type;

/* pcmcia_resource.c */
extern int pcmcia_release_configuration(struct pcmcia_device *p_dev);

#ifdef CONFIG_PCMCIA_IOCTL
/* ds.c */
extern spinlock_t pcmcia_dev_list_lock;

extern struct pcmcia_device *pcmcia_get_dev(struct pcmcia_device *p_dev);
extern void pcmcia_put_dev(struct pcmcia_device *p_dev);

struct pcmcia_device *pcmcia_device_add(struct pcmcia_socket *s,
					unsigned int function);

/* pcmcia_ioctl.c */
extern void __init pcmcia_setup_ioctl(void);
extern void __exit pcmcia_cleanup_ioctl(void);
extern void handle_event(struct pcmcia_socket *s, event_t event);
extern int handle_request(struct pcmcia_socket *s, event_t event);

#else /* CONFIG_PCMCIA_IOCTL */

static inline void __init pcmcia_setup_ioctl(void) { return; }
static inline void __exit pcmcia_cleanup_ioctl(void) { return; }
static inline void handle_event(struct pcmcia_socket *s, event_t event)
{
	return;
}
static inline int handle_request(struct pcmcia_socket *s, event_t event)
{
	return 0;
}

#endif /* CONFIG_PCMCIA_IOCTL */

#endif /* _LINUX_CS_INTERNAL_H */
