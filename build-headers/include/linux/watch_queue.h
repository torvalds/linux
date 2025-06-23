/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_WATCH_QUEUE_H
#define _LINUX_WATCH_QUEUE_H

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/ioctl.h>

#define O_NOTIFICATION_PIPE	O_EXCL	/* Parameter to pipe2() selecting notification pipe */

#define IOC_WATCH_QUEUE_SET_SIZE	_IO('W', 0x60)	/* Set the size in pages */
#define IOC_WATCH_QUEUE_SET_FILTER	_IO('W', 0x61)	/* Set the filter */

enum watch_notification_type {
	WATCH_TYPE_META		= 0,	/* Special record */
	WATCH_TYPE_KEY_NOTIFY	= 1,	/* Key change event notification */
	WATCH_TYPE__NR		= 2
};

enum watch_meta_notification_subtype {
	WATCH_META_REMOVAL_NOTIFICATION	= 0,	/* Watched object was removed */
	WATCH_META_LOSS_NOTIFICATION	= 1,	/* Data loss occurred */
};

/*
 * Notification record header.  This is aligned to 64-bits so that subclasses
 * can contain __u64 fields.
 */
struct watch_notification {
	__u32			type:24;	/* enum watch_notification_type */
	__u32			subtype:8;	/* Type-specific subtype (filterable) */
	__u32			info;
#define WATCH_INFO_LENGTH	0x0000007f	/* Length of record */
#define WATCH_INFO_LENGTH__SHIFT 0
#define WATCH_INFO_ID		0x0000ff00	/* ID of watchpoint */
#define WATCH_INFO_ID__SHIFT	8
#define WATCH_INFO_TYPE_INFO	0xffff0000	/* Type-specific info */
#define WATCH_INFO_TYPE_INFO__SHIFT 16
#define WATCH_INFO_FLAG_0	0x00010000	/* Type-specific info, flag bit 0 */
#define WATCH_INFO_FLAG_1	0x00020000	/* ... */
#define WATCH_INFO_FLAG_2	0x00040000
#define WATCH_INFO_FLAG_3	0x00080000
#define WATCH_INFO_FLAG_4	0x00100000
#define WATCH_INFO_FLAG_5	0x00200000
#define WATCH_INFO_FLAG_6	0x00400000
#define WATCH_INFO_FLAG_7	0x00800000
};

/*
 * Notification filtering rules (IOC_WATCH_QUEUE_SET_FILTER).
 */
struct watch_notification_type_filter {
	__u32	type;			/* Type to apply filter to */
	__u32	info_filter;		/* Filter on watch_notification::info */
	__u32	info_mask;		/* Mask of relevant bits in info_filter */
	__u32	subtype_filter[8];	/* Bitmask of subtypes to filter on */
};

struct watch_notification_filter {
	__u32	nr_filters;		/* Number of filters */
	__u32	__reserved;		/* Must be 0 */
	struct watch_notification_type_filter filters[];
};


/*
 * Extended watch removal notification.  This is used optionally if the type
 * wants to indicate an identifier for the object being watched, if there is
 * such.  This can be distinguished by the length.
 *
 * type -> WATCH_TYPE_META
 * subtype -> WATCH_META_REMOVAL_NOTIFICATION
 */
struct watch_notification_removal {
	struct watch_notification watch;
	__u64	id;		/* Type-dependent identifier */
};

/*
 * Type of key/keyring change notification.
 */
enum key_notification_subtype {
	NOTIFY_KEY_INSTANTIATED	= 0, /* Key was instantiated (aux is error code) */
	NOTIFY_KEY_UPDATED	= 1, /* Key was updated */
	NOTIFY_KEY_LINKED	= 2, /* Key (aux) was added to watched keyring */
	NOTIFY_KEY_UNLINKED	= 3, /* Key (aux) was removed from watched keyring */
	NOTIFY_KEY_CLEARED	= 4, /* Keyring was cleared */
	NOTIFY_KEY_REVOKED	= 5, /* Key was revoked */
	NOTIFY_KEY_INVALIDATED	= 6, /* Key was invalidated */
	NOTIFY_KEY_SETATTR	= 7, /* Key's attributes got changed */
};

/*
 * Key/keyring notification record.
 * - watch.type = WATCH_TYPE_KEY_NOTIFY
 * - watch.subtype = enum key_notification_type
 */
struct key_notification {
	struct watch_notification watch;
	__u32	key_id;		/* The key/keyring affected */
	__u32	aux;		/* Per-type auxiliary data */
};

#endif /* _LINUX_WATCH_QUEUE_H */
