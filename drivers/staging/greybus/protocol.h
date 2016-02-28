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

struct gb_connection;
struct gb_operation;

typedef int (*gb_connection_init_t)(struct gb_connection *);
typedef void (*gb_connection_exit_t)(struct gb_connection *);
typedef int (*gb_request_recv_t)(u8, struct gb_operation *);

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
void gb_protocol_deregister(struct gb_protocol *protocol);

#define gb_protocol_register(protocol) \
	__gb_protocol_register(protocol, THIS_MODULE)

struct gb_protocol *gb_protocol_get(u8 id, u8 major, u8 minor);
int gb_protocol_get_version(struct gb_connection *connection);

void gb_protocol_put(struct gb_protocol *protocol);

/* __protocol: Pointer to struct gb_protocol */
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
module_exit(protocol_exit)

/* __protocol: string matching name of struct gb_protocol */
#define gb_builtin_protocol_driver(__protocol)		\
int __init gb_##__protocol##_init(void)			\
{							\
	return gb_protocol_register(&__protocol);	\
}							\
void gb_##__protocol##_exit(void)			\
{							\
	gb_protocol_deregister(&__protocol);		\
}							\

#endif /* __PROTOCOL_H */
