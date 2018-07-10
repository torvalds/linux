/* vi: set sw=4 ts=4: */
/*
 * Support functions for mounting devices by label/uuid
 *
 * Copyright (C) 2006 by Jason Schoon <floydpink@gmail.com>
 * Some portions cribbed from e2fsprogs, util-linux, dosfstools
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

//kbuild:lib-$(CONFIG_BLKID) += get_devname.o
//kbuild:lib-$(CONFIG_FINDFS) += get_devname.o
//kbuild:lib-$(CONFIG_FEATURE_MOUNT_LABEL) += get_devname.o
//kbuild:lib-$(CONFIG_FEATURE_SWAPONOFF_LABEL) += get_devname.o

#include <sys/mount.h> /* BLKGETSIZE64 */
#if !defined(BLKGETSIZE64)
# define BLKGETSIZE64 _IOR(0x12,114,size_t)
#endif
#include "volume_id_internal.h"

static struct uuidCache_s {
	struct uuidCache_s *next;
//	int major, minor;
	char *device;
	char *label;
	char *uc_uuid; /* prefix makes it easier to grep for */
	IF_FEATURE_BLKID_TYPE(const char *type;)
} *uuidCache;

#if !ENABLE_FEATURE_BLKID_TYPE
#define get_label_uuid(fd, label, uuid, type) \
	get_label_uuid(fd, label, uuid)
#define uuidcache_addentry(device, label, uuid, type) \
	uuidcache_addentry(device, label, uuid)
#endif

/* Returns !0 on error.
 * Otherwise, returns malloc'ed strings for label and uuid
 * (and they can't be NULL, although they can be "").
 * NB: closes fd. */
static int
get_label_uuid(int fd, char **label, char **uuid, const char **type)
{
	int rv = 1;
	uint64_t size;
	struct volume_id *vid;

	/* fd is owned by vid now */
	vid = volume_id_open_node(fd);

	if (ioctl(/*vid->*/fd, BLKGETSIZE64, &size) != 0)
		size = 0;

	if (volume_id_probe_all(vid, /*0,*/ size) != 0)
		goto ret;

	if (vid->label[0] != '\0' || vid->uuid[0] != '\0'
#if ENABLE_FEATURE_BLKID_TYPE
	 || vid->type != NULL
#endif
	) {
		*label = xstrndup(vid->label, sizeof(vid->label));
		*uuid  = xstrndup(vid->uuid, sizeof(vid->uuid));
#if ENABLE_FEATURE_BLKID_TYPE
		*type = vid->type;
		dbg("found label '%s', uuid '%s', type '%s'", *label, *uuid, *type);
#else
		dbg("found label '%s', uuid '%s'", *label, *uuid);
#endif
		rv = 0;
	}
 ret:
	free_volume_id(vid); /* also closes fd */
	return rv;
}

/* NB: we take ownership of (malloc'ed) label and uuid */
static void
uuidcache_addentry(char *device, /*int major, int minor,*/ char *label, char *uuid, const char *type)
{
	struct uuidCache_s *last;

	if (!uuidCache) {
		last = uuidCache = xzalloc(sizeof(*uuidCache));
	} else {
		for (last = uuidCache; last->next; last = last->next)
			continue;
		last->next = xzalloc(sizeof(*uuidCache));
		last = last->next;
	}
	/*last->next = NULL; - xzalloc did it*/
//	last->major = major;
//	last->minor = minor;
	last->device = device;
	last->label = label;
	last->uc_uuid = uuid;
	IF_FEATURE_BLKID_TYPE(last->type = type;)
}

/* If get_label_uuid() on device_name returns success,
 * add a cache entry for this device.
 * If device node does not exist, it will be temporarily created. */
static int FAST_FUNC
uuidcache_check_device(const char *device,
		struct stat *statbuf,
		void *userData UNUSED_PARAM,
		int depth UNUSED_PARAM)
{
	/* note: this check rejects links to devices, among other nodes */
	if (!S_ISBLK(statbuf->st_mode)
#if ENABLE_FEATURE_VOLUMEID_UBIFS
	 && !(S_ISCHR(statbuf->st_mode) && strncmp(bb_basename(device), "ubi", 3) == 0)
#endif
	)
		return TRUE;

	/* Users report that mucking with floppies (especially non-present
	 * ones) is significant PITA. This is a horribly dirty hack,
	 * but it is very useful in real world.
	 * If this will ever need to be enabled, consider using O_NONBLOCK.
	 */
	if (major(statbuf->st_rdev) == 2)
		return TRUE;

	add_to_uuid_cache(device);

	return TRUE;
}

static struct uuidCache_s*
uuidcache_init(int scan_devices)
{
	dbg("DBG: uuidCache=%x, uuidCache");
	if (uuidCache)
		return uuidCache;

	/* We were scanning /proc/partitions
	 * and /proc/sys/dev/cdrom/info here.
	 * Missed volume managers. I see that "standard" blkid uses these:
	 * /dev/mapper/control
	 * /proc/devices
	 * /proc/evms/volumes
	 * /proc/lvm/VGs
	 * This is unacceptably complex. Let's just scan /dev.
	 * (Maybe add scanning of /sys/block/XXX/dev for devices
	 * somehow not having their /dev/XXX entries created?) */
	if (scan_devices)
		recursive_action("/dev", ACTION_RECURSE,
			uuidcache_check_device, /* file_action */
			NULL, /* dir_action */
			NULL, /* userData */
			0 /* depth */);

	return uuidCache;
}

#define UUID   1
#define VOL    2

#ifdef UNUSED
static char *
get_spec_by_x(int n, const char *t, int *majorPtr, int *minorPtr)
{
	struct uuidCache_s *uc;

	uc = uuidcache_init(/*scan_devices:*/ 1);
	while (uc) {
		switch (n) {
		case UUID:
			if (strcmp(t, uc->uc_uuid) == 0) {
				*majorPtr = uc->major;
				*minorPtr = uc->minor;
				return uc->device;
			}
			break;
		case VOL:
			if (strcmp(t, uc->label) == 0) {
				*majorPtr = uc->major;
				*minorPtr = uc->minor;
				return uc->device;
			}
			break;
		}
		uc = uc->next;
	}
	return NULL;
}

static unsigned char
fromhex(char c)
{
	if (isdigit(c))
		return (c - '0');
	return ((c|0x20) - 'a' + 10);
}

static char *
get_spec_by_uuid(const char *s, int *major, int *minor)
{
	unsigned char uuid[16];
	int i;

	if (strlen(s) != 36 || s[8] != '-' || s[13] != '-'
	 || s[18] != '-' || s[23] != '-'
	) {
		goto bad_uuid;
	}
	for (i = 0; i < 16; i++) {
		if (*s == '-')
			s++;
		if (!isxdigit(s[0]) || !isxdigit(s[1]))
			goto bad_uuid;
		uuid[i] = ((fromhex(s[0]) << 4) | fromhex(s[1]));
		s += 2;
	}
	return get_spec_by_x(UUID, (char *)uuid, major, minor);

 bad_uuid:
	fprintf(stderr, _("mount: bad UUID"));
	return 0;
}

static char *
get_spec_by_volume_label(const char *s, int *major, int *minor)
{
	return get_spec_by_x(VOL, s, major, minor);
}
#endif // UNUSED

/* Used by blkid */
void display_uuid_cache(int scan_devices)
{
	struct uuidCache_s *uc;

	uc = uuidcache_init(scan_devices);
	while (uc) {
		printf("%s:", uc->device);
		if (uc->label[0])
			printf(" LABEL=\"%s\"", uc->label);
		if (uc->uc_uuid[0])
			printf(" UUID=\"%s\"", uc->uc_uuid);
#if ENABLE_FEATURE_BLKID_TYPE
	if (uc->type)
		printf(" TYPE=\"%s\"", uc->type);
#endif
		bb_putchar('\n');
		uc = uc->next;
	}
}

int add_to_uuid_cache(const char *device)
{
	char *uuid = uuid; /* for compiler */
	char *label = label;
#if ENABLE_FEATURE_BLKID_TYPE
	const char *type = type;
#endif
	int fd;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return 0;

	/* get_label_uuid() closes fd in all cases (success & failure) */
	if (get_label_uuid(fd, &label, &uuid, &type) == 0) {
		/* uuidcache_addentry() takes ownership of all four params */
		uuidcache_addentry(xstrdup(device), /*ma, mi,*/ label, uuid, type);
		return 1;
	}
	return 0;
}


/* Used by mount and findfs */

char *get_devname_from_label(const char *spec)
{
	struct uuidCache_s *uc;

	uc = uuidcache_init(/*scan_devices:*/ 1);
	while (uc) {
		if (uc->label[0] && strcmp(spec, uc->label) == 0) {
			return xstrdup(uc->device);
		}
		uc = uc->next;
	}
	return NULL;
}

char *get_devname_from_uuid(const char *spec)
{
	struct uuidCache_s *uc;

	uc = uuidcache_init(/*scan_devices:*/ 1);
	while (uc) {
		/* case of hex numbers doesn't matter */
		if (strcasecmp(spec, uc->uc_uuid) == 0) {
			return xstrdup(uc->device);
		}
		uc = uc->next;
	}
	return NULL;
}

int resolve_mount_spec(char **fsname)
{
	char *tmp = *fsname;

	if (is_prefixed_with(*fsname, "UUID="))
		tmp = get_devname_from_uuid(*fsname + 5);
	else if (is_prefixed_with(*fsname, "LABEL="))
		tmp = get_devname_from_label(*fsname + 6);

	if (tmp == *fsname)
		return 0; /* no UUID= or LABEL= prefix found */

	if (tmp)
		*fsname = tmp;
	return 1;
}
