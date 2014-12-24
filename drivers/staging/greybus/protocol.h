/*
 * Greybus protocol handling
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "greybus.h"

struct gb_operation;

typedef int (*gb_connection_init_t)(struct gb_connection *);
typedef void (*gb_connection_exit_t)(struct gb_connection *);
typedef void (*gb_request_recv_t)(u8, struct gb_operation *);

/*
 * Protocols having the same id but different major and/or minor
 * version numbers are treated as distinct protocols.  If it makes
 * sense someday we could group protocols having the same id.
 */
struct gb_protocol {
	u8			id;
	u8			major;
	u8			minor;
	u8			count;

	struct list_head	links;		/* global list */

	gb_connection_init_t	connection_init;
	gb_connection_exit_t	connection_exit;
	gb_request_recv_t	request_recv;
	struct module		*owner;
	char			*name;
};

int __gb_protocol_register(struct gb_protocol *protocol, struct module *module);
int gb_protocol_deregister(struct gb_protocol *protocol);

#define gb_protocol_register(protocol) \
	__gb_protocol_register(protocol, THIS_MODULE)

struct gb_protocol *gb_protocol_get(u8 id, u8 major, u8 minor);
void gb_protocol_put(struct gb_protocol *protocol);

/*
 * These are defined in their respective protocol source files.
 * Declared here for now.  They could be added via modules, or maybe
 * just use initcalls (which level?).
 */
extern int gb_gpio_protocol_init(void);
extern void gb_gpio_protocol_exit(void);

extern int gb_pwm_protocol_init(void);
extern void gb_pwm_protocol_exit(void);

extern int gb_uart_protocol_init(void);
extern void gb_uart_protocol_exit(void);

extern int gb_sdio_protocol_init(void);
extern void gb_sdio_protocol_exit(void);

extern int gb_usb_protocol_init(void);
extern void gb_usb_protocol_exit(void);

extern int gb_i2c_protocol_init(void);
extern void gb_i2c_protocol_exit(void);

#define gb_protocol_driver(__protocol)			\
static int __init protocol_init(void)			\
{							\
	return gb_protocol_register(__protocol);	\
}							\
module_init(protocol_init);				\
static void __exit protocol_exit(void)			\
{							\
	gb_protocol_deregister(__protocol);		\
}							\
module_exit(protocol_exit);

#endif /* __PROTOCOL_H */
