/*
 * Greybus protocol handling
 *
 * Copyright 2014 Google Inc.
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
};

bool gb_protocol_register(struct gb_protocol *protocol);
bool gb_protocol_deregister(struct gb_protocol *protocol);

struct gb_protocol *gb_protocol_get(u8 id, u8 major, u8 minor);
void gb_protocol_put(struct gb_protocol *protocol);

/*
 * These are defined in their respective protocol source files.
 * Declared here for now.  They could be added via modules, or maybe
 * just use initcalls (which level?).
 */
extern bool gb_battery_protocol_init(void);
extern void gb_battery_protocol_exit(void);

extern bool gb_gpio_protocol_init(void);
extern void gb_gpio_protocol_exit(void);

extern bool gb_i2c_protocol_init(void);
extern void gb_i2c_protocol_exit(void);

extern bool gb_pwm_protocol_init(void);
extern void gb_pwm_protocol_exit(void);

extern bool gb_uart_protocol_init(void);
extern void gb_uart_protocol_exit(void);

extern bool gb_sdio_protocol_init(void);
extern void gb_sdio_protocol_exit(void);

extern bool gb_vibrator_protocol_init(void);
extern void gb_vibrator_protocol_exit(void);

bool gb_protocol_init(void);
void gb_protocol_exit(void);

#endif /* __PROTOCOL_H */
