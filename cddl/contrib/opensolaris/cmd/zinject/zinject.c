/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 */

/*
 * ZFS Fault Injector
 *
 * This userland component takes a set of options and uses libzpool to translate
 * from a user-visible object type and name to an internal representation.
 * There are two basic types of faults: device faults and data faults.
 *
 *
 * DEVICE FAULTS
 *
 * Errors can be injected into a particular vdev using the '-d' option.  This
 * option takes a path or vdev GUID to uniquely identify the device within a
 * pool.  There are two types of errors that can be injected, EIO and ENXIO,
 * that can be controlled through the '-e' option.  The default is ENXIO.  For
 * EIO failures, any attempt to read data from the device will return EIO, but
 * subsequent attempt to reopen the device will succeed.  For ENXIO failures,
 * any attempt to read from the device will return EIO, but any attempt to
 * reopen the device will also return ENXIO.
 * For label faults, the -L option must be specified. This allows faults
 * to be injected into either the nvlist, uberblock, pad1, or pad2 region
 * of all the labels for the specified device.
 *
 * This form of the command looks like:
 *
 * 	zinject -d device [-e errno] [-L <uber | nvlist | pad1 | pad2>] pool
 *
 *
 * DATA FAULTS
 *
 * We begin with a tuple of the form:
 *
 * 	<type,level,range,object>
 *
 * 	type	A string describing the type of data to target.  Each type
 * 		implicitly describes how to interpret 'object'. Currently,
 * 		the following values are supported:
 *
 * 		data		User data for a file
 * 		dnode		Dnode for a file or directory
 *
 *		The following MOS objects are special.  Instead of injecting
 *		errors on a particular object or blkid, we inject errors across
 *		all objects of the given type.
 *
 * 		mos		Any data in the MOS
 * 		mosdir		object directory
 * 		config		pool configuration
 * 		bpobj		blkptr list
 * 		spacemap	spacemap
 * 		metaslab	metaslab
 * 		errlog		persistent error log
 *
 * 	level	Object level.  Defaults to '0', not applicable to all types.  If
 * 		a range is given, this corresponds to the indirect block
 * 		corresponding to the specific range.
 *
 *	range	A numerical range [start,end) within the object.  Defaults to
 *		the full size of the file.
 *
 * 	object	A string describing the logical location of the object.  For
 * 		files and directories (currently the only supported types),
 * 		this is the path of the object on disk.
 *
 * This is translated, via libzpool, into the following internal representation:
 *
 * 	<type,objset,object,level,range>
 *
 * These types should be self-explanatory.  This tuple is then passed to the
 * kernel via a special ioctl() to initiate fault injection for the given
 * object.  Note that 'type' is not strictly necessary for fault injection, but
 * is used when translating existing faults into a human-readable string.
 *
 *
 * The command itself takes one of the forms:
 *
 * 	zinject
 * 	zinject <-a | -u pool>
 * 	zinject -c <id|all>
 * 	zinject [-q] <-t type> [-f freq] [-u] [-a] [-m] [-e errno] [-l level]
 *	    [-r range] <object>
 * 	zinject [-f freq] [-a] [-m] [-u] -b objset:object:level:start:end pool
 *
 * With no arguments, the command prints all currently registered injection
 * handlers, with their numeric identifiers.
 *
 * The '-c' option will clear the given handler, or all handlers if 'all' is
 * specified.
 *
 * The '-e' option takes a string describing the errno to simulate.  This must
 * be either 'io' or 'checksum'.  In most cases this will result in the same
 * behavior, but RAID-Z will produce a different set of ereports for this
 * situation.
 *
 * The '-a', '-u', and '-m' flags toggle internal flush behavior.  If '-a' is
 * specified, then the ARC cache is flushed appropriately.  If '-u' is
 * specified, then the underlying SPA is unloaded.  Either of these flags can be
 * specified independently of any other handlers.  The '-m' flag automatically
 * does an unmount and remount of the underlying dataset to aid in flushing the
 * cache.
 *
 * The '-f' flag controls the frequency of errors injected, expressed as a
 * integer percentage between 1 and 100.  The default is 100.
 *
 * The this form is responsible for actually injecting the handler into the
 * framework.  It takes the arguments described above, translates them to the
 * internal tuple using libzpool, and then issues an ioctl() to register the
 * handler.
 *
 * The final form can target a specific bookmark, regardless of whether a
 * human-readable interface has been designed.  It allows developers to specify
 * a particular block by number.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <sys/fs/zfs.h>
#include <sys/param.h>
#include <sys/mount.h>

#include <libzfs.h>
#include <libzfs_compat.h>

#undef verify	/* both libzfs.h and zfs_context.h want to define this */

#include "zinject.h"

libzfs_handle_t *g_zfs;
int zfs_fd;

#ifndef ECKSUM
#define	ECKSUM	EBADE
#endif

static const char *errtable[TYPE_INVAL] = {
	"data",
	"dnode",
	"mos",
	"mosdir",
	"metaslab",
	"config",
	"bpobj",
	"spacemap",
	"errlog",
	"uber",
	"nvlist",
	"pad1",
	"pad2"
};

static err_type_t
name_to_type(const char *arg)
{
	int i;
	for (i = 0; i < TYPE_INVAL; i++)
		if (strcmp(errtable[i], arg) == 0)
			return (i);

	return (TYPE_INVAL);
}

static const char *
type_to_name(uint64_t type)
{
	switch (type) {
	case DMU_OT_OBJECT_DIRECTORY:
		return ("mosdir");
	case DMU_OT_OBJECT_ARRAY:
		return ("metaslab");
	case DMU_OT_PACKED_NVLIST:
		return ("config");
	case DMU_OT_BPOBJ:
		return ("bpobj");
	case DMU_OT_SPACE_MAP:
		return ("spacemap");
	case DMU_OT_ERROR_LOG:
		return ("errlog");
	default:
		return ("-");
	}
}


/*
 * Print usage message.
 */
void
usage(void)
{
	(void) printf(
	    "usage:\n"
	    "\n"
	    "\tzinject\n"
	    "\n"
	    "\t\tList all active injection records.\n"
	    "\n"
	    "\tzinject -c <id|all>\n"
	    "\n"
	    "\t\tClear the particular record (if given a numeric ID), or\n"
	    "\t\tall records if 'all' is specificed.\n"
	    "\n"
	    "\tzinject -p <function name> pool\n"
	    "\n"
	    "\t\tInject a panic fault at the specified function. Only \n"
	    "\t\tfunctions which call spa_vdev_config_exit(), or \n"
	    "\t\tspa_vdev_exit() will trigger a panic.\n"
	    "\n"
	    "\tzinject -d device [-e errno] [-L <nvlist|uber|pad1|pad2>] [-F]\n"
	    "\t    [-T <read|write|free|claim|all> pool\n"
	    "\n"
	    "\t\tInject a fault into a particular device or the device's\n"
	    "\t\tlabel.  Label injection can either be 'nvlist', 'uber',\n "
	    "\t\t'pad1', or 'pad2'.\n"
	    "\t\t'errno' can be 'nxio' (the default), 'io', or 'dtl'.\n"
	    "\n"
	    "\tzinject -d device -A <degrade|fault> pool\n"
	    "\n"
	    "\t\tPerform a specific action on a particular device\n"
	    "\n"
	    "\tzinject -d device -D latency:lanes pool\n"
	    "\n"
	    "\t\tAdd an artificial delay to IO requests on a particular\n"
	    "\t\tdevice, such that the requests take a minimum of 'latency'\n"
	    "\t\tmilliseconds to complete. Each delay has an associated\n"
	    "\t\tnumber of 'lanes' which defines the number of concurrent\n"
	    "\t\tIO requests that can be processed.\n"
	    "\n"
	    "\t\tFor example, with a single lane delay of 10 ms (-D 10:1),\n"
	    "\t\tthe device will only be able to service a single IO request\n"
	    "\t\tat a time with each request taking 10 ms to complete. So,\n"
	    "\t\tif only a single request is submitted every 10 ms, the\n"
	    "\t\taverage latency will be 10 ms; but if more than one request\n"
	    "\t\tis submitted every 10 ms, the average latency will be more\n"
	    "\t\tthan 10 ms.\n"
	    "\n"
	    "\t\tSimilarly, if a delay of 10 ms is specified to have two\n"
	    "\t\tlanes (-D 10:2), then the device will be able to service\n"
	    "\t\ttwo requests at a time, each with a minimum latency of\n"
	    "\t\t10 ms. So, if two requests are submitted every 10 ms, then\n"
	    "\t\tthe average latency will be 10 ms; but if more than two\n"
	    "\t\trequests are submitted every 10 ms, the average latency\n"
	    "\t\twill be more than 10 ms.\n"
	    "\n"
	    "\t\tAlso note, these delays are additive. So two invocations\n"
	    "\t\tof '-D 10:1', is roughly equivalent to a single invocation\n"
	    "\t\tof '-D 10:2'. This also means, one can specify multiple\n"
	    "\t\tlanes with differing target latencies. For example, an\n"
	    "\t\tinvocation of '-D 10:1' followed by '-D 25:2' will\n"
	    "\t\tcreate 3 lanes on the device; one lane with a latency\n"
	    "\t\tof 10 ms and two lanes with a 25 ms latency.\n"
	    "\n"
	    "\tzinject -I [-s <seconds> | -g <txgs>] pool\n"
	    "\n"
	    "\t\tCause the pool to stop writing blocks yet not\n"
	    "\t\treport errors for a duration.  Simulates buggy hardware\n"
	    "\t\tthat fails to honor cache flush requests.\n"
	    "\t\tDefault duration is 30 seconds.  The machine is panicked\n"
	    "\t\tat the end of the duration.\n"
	    "\n"
	    "\tzinject -b objset:object:level:blkid pool\n"
	    "\n"
	    "\t\tInject an error into pool 'pool' with the numeric bookmark\n"
	    "\t\tspecified by the remaining tuple.  Each number is in\n"
	    "\t\thexidecimal, and only one block can be specified.\n"
	    "\n"
	    "\tzinject [-q] <-t type> [-e errno] [-l level] [-r range]\n"
	    "\t    [-a] [-m] [-u] [-f freq] <object>\n"
	    "\n"
	    "\t\tInject an error into the object specified by the '-t' option\n"
	    "\t\tand the object descriptor.  The 'object' parameter is\n"
	    "\t\tinterperted depending on the '-t' option.\n"
	    "\n"
	    "\t\t-q\tQuiet mode.  Only print out the handler number added.\n"
	    "\t\t-e\tInject a specific error.  Must be either 'io' or\n"
	    "\t\t\t'checksum'.  Default is 'io'.\n"
	    "\t\t-l\tInject error at a particular block level. Default is "
	    "0.\n"
	    "\t\t-m\tAutomatically remount underlying filesystem.\n"
	    "\t\t-r\tInject error over a particular logical range of an\n"
	    "\t\t\tobject.  Will be translated to the appropriate blkid\n"
	    "\t\t\trange according to the object's properties.\n"
	    "\t\t-a\tFlush the ARC cache.  Can be specified without any\n"
	    "\t\t\tassociated object.\n"
	    "\t\t-u\tUnload the associated pool.  Can be specified with only\n"
	    "\t\t\ta pool object.\n"
	    "\t\t-f\tOnly inject errors a fraction of the time.  Expressed as\n"
	    "\t\t\ta percentage between 1 and 100.\n"
	    "\n"
	    "\t-t data\t\tInject an error into the plain file contents of a\n"
	    "\t\t\tfile.  The object must be specified as a complete path\n"
	    "\t\t\tto a file on a ZFS filesystem.\n"
	    "\n"
	    "\t-t dnode\tInject an error into the metadnode in the block\n"
	    "\t\t\tcorresponding to the dnode for a file or directory.  The\n"
	    "\t\t\t'-r' option is incompatible with this mode.  The object\n"
	    "\t\t\tis specified as a complete path to a file or directory\n"
	    "\t\t\ton a ZFS filesystem.\n"
	    "\n"
	    "\t-t <mos>\tInject errors into the MOS for objects of the given\n"
	    "\t\t\ttype.  Valid types are: mos, mosdir, config, bpobj,\n"
	    "\t\t\tspacemap, metaslab, errlog.  The only valid <object> is\n"
	    "\t\t\tthe poolname.\n");
}

static int
iter_handlers(int (*func)(int, const char *, zinject_record_t *, void *),
    void *data)
{
	zfs_cmd_t zc = { 0 };
	int ret;

	while (ioctl(zfs_fd, ZFS_IOC_INJECT_LIST_NEXT, &zc) == 0)
		if ((ret = func((int)zc.zc_guid, zc.zc_name,
		    &zc.zc_inject_record, data)) != 0)
			return (ret);

	if (errno != ENOENT) {
		(void) fprintf(stderr, "Unable to list handlers: %s\n",
		    strerror(errno));
		return (-1);
	}

	return (0);
}

static int
print_data_handler(int id, const char *pool, zinject_record_t *record,
    void *data)
{
	int *count = data;

	if (record->zi_guid != 0 || record->zi_func[0] != '\0')
		return (0);

	if (*count == 0) {
		(void) printf("%3s  %-15s  %-6s  %-6s  %-8s  %3s  %-15s\n",
		    "ID", "POOL", "OBJSET", "OBJECT", "TYPE", "LVL",  "RANGE");
		(void) printf("---  ---------------  ------  "
		    "------  --------  ---  ---------------\n");
	}

	*count += 1;

	(void) printf("%3d  %-15s  %-6llu  %-6llu  %-8s  %3d  ", id, pool,
	    (u_longlong_t)record->zi_objset, (u_longlong_t)record->zi_object,
	    type_to_name(record->zi_type), record->zi_level);

	if (record->zi_start == 0 &&
	    record->zi_end == -1ULL)
		(void) printf("all\n");
	else
		(void) printf("[%llu, %llu]\n", (u_longlong_t)record->zi_start,
		    (u_longlong_t)record->zi_end);

	return (0);
}

static int
print_device_handler(int id, const char *pool, zinject_record_t *record,
    void *data)
{
	int *count = data;

	if (record->zi_guid == 0 || record->zi_func[0] != '\0')
		return (0);

	if (record->zi_cmd == ZINJECT_DELAY_IO)
		return (0);

	if (*count == 0) {
		(void) printf("%3s  %-15s  %s\n", "ID", "POOL", "GUID");
		(void) printf("---  ---------------  ----------------\n");
	}

	*count += 1;

	(void) printf("%3d  %-15s  %llx\n", id, pool,
	    (u_longlong_t)record->zi_guid);

	return (0);
}

static int
print_delay_handler(int id, const char *pool, zinject_record_t *record,
    void *data)
{
	int *count = data;

	if (record->zi_guid == 0 || record->zi_func[0] != '\0')
		return (0);

	if (record->zi_cmd != ZINJECT_DELAY_IO)
		return (0);

	if (*count == 0) {
		(void) printf("%3s  %-15s  %-15s  %-15s  %s\n",
		    "ID", "POOL", "DELAY (ms)", "LANES", "GUID");
		(void) printf("---  ---------------  ---------------  "
		    "---------------  ----------------\n");
	}

	*count += 1;

	(void) printf("%3d  %-15s  %-15llu  %-15llu  %llx\n", id, pool,
	    (u_longlong_t)NSEC2MSEC(record->zi_timer),
	    (u_longlong_t)record->zi_nlanes,
	    (u_longlong_t)record->zi_guid);

	return (0);
}

static int
print_panic_handler(int id, const char *pool, zinject_record_t *record,
    void *data)
{
	int *count = data;

	if (record->zi_func[0] == '\0')
		return (0);

	if (*count == 0) {
		(void) printf("%3s  %-15s  %s\n", "ID", "POOL", "FUNCTION");
		(void) printf("---  ---------------  ----------------\n");
	}

	*count += 1;

	(void) printf("%3d  %-15s  %s\n", id, pool, record->zi_func);

	return (0);
}

/*
 * Print all registered error handlers.  Returns the number of handlers
 * registered.
 */
static int
print_all_handlers(void)
{
	int count = 0, total = 0;

	(void) iter_handlers(print_device_handler, &count);
	if (count > 0) {
		total += count;
		(void) printf("\n");
		count = 0;
	}

	(void) iter_handlers(print_delay_handler, &count);
	if (count > 0) {
		total += count;
		(void) printf("\n");
		count = 0;
	}

	(void) iter_handlers(print_data_handler, &count);
	if (count > 0) {
		total += count;
		(void) printf("\n");
		count = 0;
	}

	(void) iter_handlers(print_panic_handler, &count);

	return (count + total);
}

/* ARGSUSED */
static int
cancel_one_handler(int id, const char *pool, zinject_record_t *record,
    void *data)
{
	zfs_cmd_t zc = { 0 };

	zc.zc_guid = (uint64_t)id;

	if (ioctl(zfs_fd, ZFS_IOC_CLEAR_FAULT, &zc) != 0) {
		(void) fprintf(stderr, "failed to remove handler %d: %s\n",
		    id, strerror(errno));
		return (1);
	}

	return (0);
}

/*
 * Remove all fault injection handlers.
 */
static int
cancel_all_handlers(void)
{
	int ret = iter_handlers(cancel_one_handler, NULL);

	if (ret == 0)
		(void) printf("removed all registered handlers\n");

	return (ret);
}

/*
 * Remove a specific fault injection handler.
 */
static int
cancel_handler(int id)
{
	zfs_cmd_t zc = { 0 };

	zc.zc_guid = (uint64_t)id;

	if (ioctl(zfs_fd, ZFS_IOC_CLEAR_FAULT, &zc) != 0) {
		(void) fprintf(stderr, "failed to remove handler %d: %s\n",
		    id, strerror(errno));
		return (1);
	}

	(void) printf("removed handler %d\n", id);

	return (0);
}

/*
 * Register a new fault injection handler.
 */
static int
register_handler(const char *pool, int flags, zinject_record_t *record,
    int quiet)
{
	zfs_cmd_t zc = { 0 };

	(void) strcpy(zc.zc_name, pool);
	zc.zc_inject_record = *record;
	zc.zc_guid = flags;

	if (ioctl(zfs_fd, ZFS_IOC_INJECT_FAULT, &zc) != 0) {
		(void) fprintf(stderr, "failed to add handler: %s\n",
		    strerror(errno));
		return (1);
	}

	if (flags & ZINJECT_NULL)
		return (0);

	if (quiet) {
		(void) printf("%llu\n", (u_longlong_t)zc.zc_guid);
	} else {
		(void) printf("Added handler %llu with the following "
		    "properties:\n", (u_longlong_t)zc.zc_guid);
		(void) printf("  pool: %s\n", pool);
		if (record->zi_guid) {
			(void) printf("  vdev: %llx\n",
			    (u_longlong_t)record->zi_guid);
		} else if (record->zi_func[0] != '\0') {
			(void) printf("  panic function: %s\n",
			    record->zi_func);
		} else if (record->zi_duration > 0) {
			(void) printf(" time: %lld seconds\n",
			    (u_longlong_t)record->zi_duration);
		} else if (record->zi_duration < 0) {
			(void) printf(" txgs: %lld \n",
			    (u_longlong_t)-record->zi_duration);
		} else {
			(void) printf("objset: %llu\n",
			    (u_longlong_t)record->zi_objset);
			(void) printf("object: %llu\n",
			    (u_longlong_t)record->zi_object);
			(void) printf("  type: %llu\n",
			    (u_longlong_t)record->zi_type);
			(void) printf(" level: %d\n", record->zi_level);
			if (record->zi_start == 0 &&
			    record->zi_end == -1ULL)
				(void) printf(" range: all\n");
			else
				(void) printf(" range: [%llu, %llu)\n",
				    (u_longlong_t)record->zi_start,
				    (u_longlong_t)record->zi_end);
		}
	}

	return (0);
}

int
perform_action(const char *pool, zinject_record_t *record, int cmd)
{
	zfs_cmd_t zc = { 0 };

	ASSERT(cmd == VDEV_STATE_DEGRADED || cmd == VDEV_STATE_FAULTED);
	(void) strlcpy(zc.zc_name, pool, sizeof (zc.zc_name));
	zc.zc_guid = record->zi_guid;
	zc.zc_cookie = cmd;

	if (ioctl(zfs_fd, ZFS_IOC_VDEV_SET_STATE, &zc) == 0)
		return (0);

	return (1);
}

static int
parse_delay(char *str, uint64_t *delay, uint64_t *nlanes)
{
	unsigned long scan_delay;
	unsigned long scan_nlanes;

	if (sscanf(str, "%lu:%lu", &scan_delay, &scan_nlanes) != 2)
		return (1);

	/*
	 * We explicitly disallow a delay of zero here, because we key
	 * off this value being non-zero in translate_device(), to
	 * determine if the fault is a ZINJECT_DELAY_IO fault or not.
	 */
	if (scan_delay == 0)
		return (1);

	/*
	 * The units for the CLI delay parameter is milliseconds, but
	 * the data passed to the kernel is interpreted as nanoseconds.
	 * Thus we scale the milliseconds to nanoseconds here, and this
	 * nanosecond value is used to pass the delay to the kernel.
	 */
	*delay = MSEC2NSEC(scan_delay);
	*nlanes = scan_nlanes;

	return (0);
}

int
main(int argc, char **argv)
{
	int c;
	char *range = NULL;
	char *cancel = NULL;
	char *end;
	char *raw = NULL;
	char *device = NULL;
	int level = 0;
	int quiet = 0;
	int error = 0;
	int domount = 0;
	int io_type = ZIO_TYPES;
	int action = VDEV_STATE_UNKNOWN;
	err_type_t type = TYPE_INVAL;
	err_type_t label = TYPE_INVAL;
	zinject_record_t record = { 0 };
	char pool[MAXNAMELEN];
	char dataset[MAXNAMELEN];
	zfs_handle_t *zhp;
	int nowrites = 0;
	int dur_txg = 0;
	int dur_secs = 0;
	int ret;
	int flags = 0;

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "internal error: failed to "
		    "initialize ZFS library\n");
		return (1);
	}

	libzfs_print_on_error(g_zfs, B_TRUE);

	if ((zfs_fd = open(ZFS_DEV, O_RDWR)) < 0) {
		(void) fprintf(stderr, "failed to open ZFS device\n");
		return (1);
	}

	if (argc == 1) {
		/*
		 * No arguments.  Print the available handlers.  If there are no
		 * available handlers, direct the user to '-h' for help
		 * information.
		 */
		if (print_all_handlers() == 0) {
			(void) printf("No handlers registered.\n");
			(void) printf("Run 'zinject -h' for usage "
			    "information.\n");
		}

		return (0);
	}

	while ((c = getopt(argc, argv,
	    ":aA:b:d:D:f:Fg:qhIc:t:T:l:mr:s:e:uL:p:")) != -1) {
		switch (c) {
		case 'a':
			flags |= ZINJECT_FLUSH_ARC;
			break;
		case 'A':
			if (strcasecmp(optarg, "degrade") == 0) {
				action = VDEV_STATE_DEGRADED;
			} else if (strcasecmp(optarg, "fault") == 0) {
				action = VDEV_STATE_FAULTED;
			} else {
				(void) fprintf(stderr, "invalid action '%s': "
				    "must be 'degrade' or 'fault'\n", optarg);
				usage();
				return (1);
			}
			break;
		case 'b':
			raw = optarg;
			break;
		case 'c':
			cancel = optarg;
			break;
		case 'd':
			device = optarg;
			break;
		case 'D':
			ret = parse_delay(optarg, &record.zi_timer,
			    &record.zi_nlanes);
			if (ret != 0) {
				(void) fprintf(stderr, "invalid i/o delay "
				    "value: '%s'\n", optarg);
				usage();
				return (1);
			}
			break;
		case 'e':
			if (strcasecmp(optarg, "io") == 0) {
				error = EIO;
			} else if (strcasecmp(optarg, "checksum") == 0) {
				error = ECKSUM;
			} else if (strcasecmp(optarg, "nxio") == 0) {
				error = ENXIO;
			} else if (strcasecmp(optarg, "dtl") == 0) {
				error = ECHILD;
			} else {
				(void) fprintf(stderr, "invalid error type "
				    "'%s': must be 'io', 'checksum' or "
				    "'nxio'\n", optarg);
				usage();
				return (1);
			}
			break;
		case 'f':
			record.zi_freq = atoi(optarg);
			if (record.zi_freq < 1 || record.zi_freq > 100) {
				(void) fprintf(stderr, "frequency range must "
				    "be in the range (0, 100]\n");
				return (1);
			}
			break;
		case 'F':
			record.zi_failfast = B_TRUE;
			break;
		case 'g':
			dur_txg = 1;
			record.zi_duration = (int)strtol(optarg, &end, 10);
			if (record.zi_duration <= 0 || *end != '\0') {
				(void) fprintf(stderr, "invalid duration '%s': "
				    "must be a positive integer\n", optarg);
				usage();
				return (1);
			}
			/* store duration of txgs as its negative */
			record.zi_duration *= -1;
			break;
		case 'h':
			usage();
			return (0);
		case 'I':
			/* default duration, if one hasn't yet been defined */
			nowrites = 1;
			if (dur_secs == 0 && dur_txg == 0)
				record.zi_duration = 30;
			break;
		case 'l':
			level = (int)strtol(optarg, &end, 10);
			if (*end != '\0') {
				(void) fprintf(stderr, "invalid level '%s': "
				    "must be an integer\n", optarg);
				usage();
				return (1);
			}
			break;
		case 'm':
			domount = 1;
			break;
		case 'p':
			(void) strlcpy(record.zi_func, optarg,
			    sizeof (record.zi_func));
			record.zi_cmd = ZINJECT_PANIC;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'r':
			range = optarg;
			break;
		case 's':
			dur_secs = 1;
			record.zi_duration = (int)strtol(optarg, &end, 10);
			if (record.zi_duration <= 0 || *end != '\0') {
				(void) fprintf(stderr, "invalid duration '%s': "
				    "must be a positive integer\n", optarg);
				usage();
				return (1);
			}
			break;
		case 'T':
			if (strcasecmp(optarg, "read") == 0) {
				io_type = ZIO_TYPE_READ;
			} else if (strcasecmp(optarg, "write") == 0) {
				io_type = ZIO_TYPE_WRITE;
			} else if (strcasecmp(optarg, "free") == 0) {
				io_type = ZIO_TYPE_FREE;
			} else if (strcasecmp(optarg, "claim") == 0) {
				io_type = ZIO_TYPE_CLAIM;
			} else if (strcasecmp(optarg, "all") == 0) {
				io_type = ZIO_TYPES;
			} else {
				(void) fprintf(stderr, "invalid I/O type "
				    "'%s': must be 'read', 'write', 'free', "
				    "'claim' or 'all'\n", optarg);
				usage();
				return (1);
			}
			break;
		case 't':
			if ((type = name_to_type(optarg)) == TYPE_INVAL &&
			    !MOS_TYPE(type)) {
				(void) fprintf(stderr, "invalid type '%s'\n",
				    optarg);
				usage();
				return (1);
			}
			break;
		case 'u':
			flags |= ZINJECT_UNLOAD_SPA;
			break;
		case 'L':
			if ((label = name_to_type(optarg)) == TYPE_INVAL &&
			    !LABEL_TYPE(type)) {
				(void) fprintf(stderr, "invalid label type "
				    "'%s'\n", optarg);
				usage();
				return (1);
			}
			break;
		case ':':
			(void) fprintf(stderr, "option -%c requires an "
			    "operand\n", optopt);
			usage();
			return (1);
		case '?':
			(void) fprintf(stderr, "invalid option '%c'\n",
			    optopt);
			usage();
			return (2);
		}
	}

	argc -= optind;
	argv += optind;

	if (record.zi_duration != 0)
		record.zi_cmd = ZINJECT_IGNORED_WRITES;

	if (cancel != NULL) {
		/*
		 * '-c' is invalid with any other options.
		 */
		if (raw != NULL || range != NULL || type != TYPE_INVAL ||
		    level != 0 || record.zi_cmd != ZINJECT_UNINITIALIZED) {
			(void) fprintf(stderr, "cancel (-c) incompatible with "
			    "any other options\n");
			usage();
			return (2);
		}
		if (argc != 0) {
			(void) fprintf(stderr, "extraneous argument to '-c'\n");
			usage();
			return (2);
		}

		if (strcmp(cancel, "all") == 0) {
			return (cancel_all_handlers());
		} else {
			int id = (int)strtol(cancel, &end, 10);
			if (*end != '\0') {
				(void) fprintf(stderr, "invalid handle id '%s':"
				    " must be an integer or 'all'\n", cancel);
				usage();
				return (1);
			}
			return (cancel_handler(id));
		}
	}

	if (device != NULL) {
		/*
		 * Device (-d) injection uses a completely different mechanism
		 * for doing injection, so handle it separately here.
		 */
		if (raw != NULL || range != NULL || type != TYPE_INVAL ||
		    level != 0 || record.zi_cmd != ZINJECT_UNINITIALIZED) {
			(void) fprintf(stderr, "device (-d) incompatible with "
			    "data error injection\n");
			usage();
			return (2);
		}

		if (argc != 1) {
			(void) fprintf(stderr, "device (-d) injection requires "
			    "a single pool name\n");
			usage();
			return (2);
		}

		(void) strcpy(pool, argv[0]);
		dataset[0] = '\0';

		if (error == ECKSUM) {
			(void) fprintf(stderr, "device error type must be "
			    "'io' or 'nxio'\n");
			return (1);
		}

		record.zi_iotype = io_type;
		if (translate_device(pool, device, label, &record) != 0)
			return (1);
		if (!error)
			error = ENXIO;

		if (action != VDEV_STATE_UNKNOWN)
			return (perform_action(pool, &record, action));

	} else if (raw != NULL) {
		if (range != NULL || type != TYPE_INVAL || level != 0 ||
		    record.zi_cmd != ZINJECT_UNINITIALIZED) {
			(void) fprintf(stderr, "raw (-b) format with "
			    "any other options\n");
			usage();
			return (2);
		}

		if (argc != 1) {
			(void) fprintf(stderr, "raw (-b) format expects a "
			    "single pool name\n");
			usage();
			return (2);
		}

		(void) strcpy(pool, argv[0]);
		dataset[0] = '\0';

		if (error == ENXIO) {
			(void) fprintf(stderr, "data error type must be "
			    "'checksum' or 'io'\n");
			return (1);
		}

		record.zi_cmd = ZINJECT_DATA_FAULT;
		if (translate_raw(raw, &record) != 0)
			return (1);
		if (!error)
			error = EIO;
	} else if (record.zi_cmd == ZINJECT_PANIC) {
		if (raw != NULL || range != NULL || type != TYPE_INVAL ||
		    level != 0 || device != NULL) {
			(void) fprintf(stderr, "panic (-p) incompatible with "
			    "other options\n");
			usage();
			return (2);
		}

		if (argc < 1 || argc > 2) {
			(void) fprintf(stderr, "panic (-p) injection requires "
			    "a single pool name and an optional id\n");
			usage();
			return (2);
		}

		(void) strcpy(pool, argv[0]);
		if (argv[1] != NULL)
			record.zi_type = atoi(argv[1]);
		dataset[0] = '\0';
	} else if (record.zi_cmd == ZINJECT_IGNORED_WRITES) {
		if (nowrites == 0) {
			(void) fprintf(stderr, "-s or -g meaningless "
			    "without -I (ignore writes)\n");
			usage();
			return (2);
		} else if (dur_secs && dur_txg) {
			(void) fprintf(stderr, "choose a duration either "
			    "in seconds (-s) or a number of txgs (-g) "
			    "but not both\n");
			usage();
			return (2);
		} else if (argc != 1) {
			(void) fprintf(stderr, "ignore writes (-I) "
			    "injection requires a single pool name\n");
			usage();
			return (2);
		}

		(void) strcpy(pool, argv[0]);
		dataset[0] = '\0';
	} else if (type == TYPE_INVAL) {
		if (flags == 0) {
			(void) fprintf(stderr, "at least one of '-b', '-d', "
			    "'-t', '-a', '-p', '-I' or '-u' "
			    "must be specified\n");
			usage();
			return (2);
		}

		if (argc == 1 && (flags & ZINJECT_UNLOAD_SPA)) {
			(void) strcpy(pool, argv[0]);
			dataset[0] = '\0';
		} else if (argc != 0) {
			(void) fprintf(stderr, "extraneous argument for "
			    "'-f'\n");
			usage();
			return (2);
		}

		flags |= ZINJECT_NULL;
	} else {
		if (argc != 1) {
			(void) fprintf(stderr, "missing object\n");
			usage();
			return (2);
		}

		if (error == ENXIO) {
			(void) fprintf(stderr, "data error type must be "
			    "'checksum' or 'io'\n");
			return (1);
		}

		record.zi_cmd = ZINJECT_DATA_FAULT;
		if (translate_record(type, argv[0], range, level, &record, pool,
		    dataset) != 0)
			return (1);
		if (!error)
			error = EIO;
	}

	/*
	 * If this is pool-wide metadata, unmount everything.  The ioctl() will
	 * unload the pool, so that we trigger spa-wide reopen of metadata next
	 * time we access the pool.
	 */
	if (dataset[0] != '\0' && domount) {
		if ((zhp = zfs_open(g_zfs, dataset, ZFS_TYPE_DATASET)) == NULL)
			return (1);

		if (zfs_unmount(zhp, NULL, 0) != 0)
			return (1);
	}

	record.zi_error = error;

	ret = register_handler(pool, flags, &record, quiet);

	if (dataset[0] != '\0' && domount)
		ret = (zfs_mount(zhp, NULL, 0) != 0);

	libzfs_fini(g_zfs);

	return (ret);
}
