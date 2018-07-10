/*
 * Common modutils related functions for busybox
 *
 * Copyright (C) 2008 by Timo Teras <timo.teras@iki.fi>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "modutils.h"

#include <sys/syscall.h>

#define init_module(mod, len, opts) syscall(__NR_init_module, mod, len, opts)
#if defined(__NR_finit_module)
# define finit_module(fd, uargs, flags) syscall(__NR_finit_module, fd, uargs, flags)
#endif
#define delete_module(mod, flags) syscall(__NR_delete_module, mod, flags)

static module_entry *helper_get_module(module_db *db, const char *module, int create)
{
	char modname[MODULE_NAME_LEN];
	struct module_entry *e;
	unsigned i, hash;

	filename2modname(module, modname);

	hash = 0;
	for (i = 0; modname[i]; i++)
		hash = ((hash << 5) + hash) + modname[i];
	hash %= MODULE_HASH_SIZE;

	for (e = db->buckets[hash]; e; e = e->next)
		if (strcmp(e->modname, modname) == 0)
			return e;
	if (!create)
		return NULL;

	e = xzalloc(sizeof(*e));
	e->modname = xstrdup(modname);
	e->next = db->buckets[hash];
	db->buckets[hash] = e;
	IF_DEPMOD(e->dnext = e->dprev = e;)

	return e;
}
module_entry* FAST_FUNC moddb_get(module_db *db, const char *module)
{
	return helper_get_module(db, module, 0);
}
module_entry* FAST_FUNC moddb_get_or_create(module_db *db, const char *module)
{
	return helper_get_module(db, module, 1);
}

void FAST_FUNC moddb_free(module_db *db)
{
	module_entry *e, *n;
	unsigned i;

	for (i = 0; i < MODULE_HASH_SIZE; i++) {
		for (e = db->buckets[i]; e; e = n) {
			n = e->next;
			free(e->name);
			free(e->modname);
			free(e);
		}
	}
}

void FAST_FUNC replace(char *s, char what, char with)
{
	while (*s) {
		if (what == *s)
			*s = with;
		++s;
	}
}

int FAST_FUNC string_to_llist(char *string, llist_t **llist, const char *delim)
{
	char *tok;
	int len = 0;

	while ((tok = strsep(&string, delim)) != NULL) {
		if (tok[0] == '\0')
			continue;
		llist_add_to_end(llist, xstrdup(tok));
		len += strlen(tok);
	}
	return len;
}

char* FAST_FUNC filename2modname(const char *filename, char *modname)
{
	char local_modname[MODULE_NAME_LEN];
	int i;
	const char *from;

	if (filename == NULL)
		return NULL;
	if (modname == NULL)
		modname = local_modname;
	// Disabled since otherwise "modprobe dir/name" would work
	// as if it is "modprobe name". It is unclear why
	// 'basenamization' was here in the first place.
	//from = bb_get_last_path_component_nostrip(filename);
	from = filename;
	for (i = 0; i < (MODULE_NAME_LEN-1) && from[i] != '\0' && from[i] != '.'; i++)
		modname[i] = (from[i] == '-') ? '_' : from[i];
	modname[i] = '\0';

	if (modname == local_modname)
		return xstrdup(modname);

	return modname;
}

#if ENABLE_FEATURE_CMDLINE_MODULE_OPTIONS
char* FAST_FUNC parse_cmdline_module_options(char **argv, int quote_spaces)
{
	char *options;
	int optlen;

	options = xzalloc(1);
	optlen = 0;
	while (*++argv) {
		const char *fmt;
		const char *var;
		const char *val;

		var = *argv;
		options = xrealloc(options, optlen + 2 + strlen(var) + 2);
		fmt = "%.*s%s ";
		val = strchrnul(var, '=');
		if (quote_spaces) {
			/*
			 * modprobe (module-init-tools version 3.11.1) compat:
			 * quote only value:
			 * var="val with spaces", not "var=val with spaces"
			 * (note: var *name* is not checked for spaces!)
			 */
			if (*val) { /* has var=val format. skip '=' */
				val++;
				if (strchr(val, ' '))
					fmt = "%.*s\"%s\" ";
			}
		}
		optlen += sprintf(options + optlen, fmt, (int)(val - var), var, val);
	}
	/* Remove trailing space. Disabled */
	/* if (optlen != 0) options[optlen-1] = '\0'; */
	return options;
}
#endif

#if ENABLE_FEATURE_INSMOD_TRY_MMAP
void* FAST_FUNC try_to_mmap_module(const char *filename, size_t *image_size_p)
{
	/* We have user reports of failure to load 3MB module
	 * on a 16MB RAM machine. Apparently even a transient
	 * memory spike to 6MB during module load
	 * is too big for that system. */
	void *image;
	struct stat st;
	int fd;

	fd = xopen(filename, O_RDONLY);
	fstat(fd, &st);
	image = NULL;
	/* st.st_size is off_t, we can't just pass it to mmap */
	if (st.st_size <= *image_size_p) {
		size_t image_size = st.st_size;
		image = mmap(NULL, image_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (image == MAP_FAILED) {
			image = NULL;
		} else if (*(uint32_t*)image != SWAP_BE32(0x7f454C46)) {
			/* No ELF signature. Compressed module? */
			munmap(image, image_size);
			image = NULL;
		} else {
			/* Success. Report the size */
			*image_size_p = image_size;
		}
	}
	close(fd);
	return image;
}
#endif

/* Return:
 * 0 on success,
 * -errno on open/read error,
 * errno on init_module() error
 */
int FAST_FUNC bb_init_module(const char *filename, const char *options)
{
	size_t image_size;
	char *image;
	int rc;
	bool mmaped;

	if (!options)
		options = "";

//TODO: audit bb_init_module_24 to match error code convention
#if ENABLE_FEATURE_2_4_MODULES
	if (get_linux_version_code() < KERNEL_VERSION(2,6,0))
		return bb_init_module_24(filename, options);
#endif

	/*
	 * First we try finit_module if available.  Some kernels are configured
	 * to only allow loading of modules off of secure storage (like a read-
	 * only rootfs) which needs the finit_module call.  If it fails, we fall
	 * back to normal module loading to support compressed modules.
	 */
# ifdef __NR_finit_module
	{
		int fd = open(filename, O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			rc = finit_module(fd, options, 0) != 0;
			close(fd);
			if (rc == 0)
				return rc;
		}
	}
# endif

	image_size = INT_MAX - 4095;
	mmaped = 0;
	image = try_to_mmap_module(filename, &image_size);
	if (image) {
		mmaped = 1;
	} else {
		errno = ENOMEM; /* may be changed by e.g. open errors below */
		image = xmalloc_open_zipped_read_close(filename, &image_size);
		if (!image)
			return -errno;
	}

	errno = 0;
	init_module(image, image_size, options);
	rc = errno;
	if (mmaped)
		munmap(image, image_size);
	else
		free(image);
	return rc;
}

int FAST_FUNC bb_delete_module(const char *module, unsigned int flags)
{
	errno = 0;
	delete_module(module, flags);
	return errno;
}

/* Note: not suitable for delete_module() errnos.
 * For them, probably only EWOULDBLOCK needs explaining:
 * "Other modules depend on us". So far we don't do such
 * translation and don't use moderror() for removal errors.
 */
const char* FAST_FUNC moderror(int err)
{
	switch (err) {
	case -1: /* btw: it's -EPERM */
		return "no such module";
	case ENOEXEC:
		return "invalid module format";
	case ENOENT:
		return "unknown symbol in module, or unknown parameter";
	case ESRCH:
		return "module has wrong symbol version";
	case ENOSYS:
		return "kernel does not support requested operation";
	}
	if (err < 0) /* should always be */
		err = -err;
	return strerror(err);
}
