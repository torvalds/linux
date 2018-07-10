/* vi: set sw=4 ts=4: */
/*
 * public domain -- Dave 'Kill a Cop' Cinege <dcinege@psychosis.com>
 *
 * makedevs
 * Make ranges of device files quickly.
 * known bugs: can't deal with alpha ranges
 */
//config:config MAKEDEVS
//config:	bool "makedevs (9.3 kb)"
//config:	default y
//config:	help
//config:	'makedevs' is a utility used to create a batch of devices with
//config:	one command.
//config:
//config:	There are two choices for command line behaviour, the interface
//config:	as used by LEAF/Linux Router Project, or a device table file.
//config:
//config:	'leaf' is traditionally what busybox follows, it allows multiple
//config:	devices of a particluar type to be created per command.
//config:	e.g. /dev/hda[0-9]
//config:	Device properties are passed as command line arguments.
//config:
//config:	'table' reads device properties from a file or stdin, allowing
//config:	a batch of unrelated devices to be made with one command.
//config:	User/group names are allowed as an alternative to uid/gid.
//config:
//config:choice
//config:	prompt "Choose makedevs behaviour"
//config:	depends on MAKEDEVS
//config:	default FEATURE_MAKEDEVS_TABLE
//config:
//config:config FEATURE_MAKEDEVS_LEAF
//config:	bool "leaf"
//config:
//config:config FEATURE_MAKEDEVS_TABLE
//config:	bool "table"
//config:
//config:endchoice

//applet:IF_MAKEDEVS(APPLET_NOEXEC(makedevs, makedevs, BB_DIR_SBIN, BB_SUID_DROP, makedevs))

//kbuild:lib-$(CONFIG_MAKEDEVS) += makedevs.o

//usage:#if ENABLE_FEATURE_MAKEDEVS_LEAF
//usage:#define makedevs_trivial_usage
//usage:       "NAME TYPE MAJOR MINOR FIRST LAST [s]"
//usage:#define makedevs_full_usage "\n\n"
//usage:       "Create a range of block or character special files"
//usage:     "\n"
//usage:     "\nTYPE is:"
//usage:     "\n	b	Block device"
//usage:     "\n	c	Character device"
//usage:     "\n	f	FIFO, MAJOR and MINOR are ignored"
//usage:     "\n"
//usage:     "\nFIRST..LAST specify numbers appended to NAME."
//usage:     "\nIf 's' is the last argument, the base device is created as well."
//usage:     "\n"
//usage:     "\nExamples:"
//usage:     "\n	makedevs /dev/ttyS c 4 66 2 63   ->  ttyS2-ttyS63"
//usage:     "\n	makedevs /dev/hda b 3 0 0 8 s    ->  hda,hda1-hda8"
//usage:
//usage:#define makedevs_example_usage
//usage:       "# makedevs /dev/ttyS c 4 66 2 63\n"
//usage:       "[creates ttyS2-ttyS63]\n"
//usage:       "# makedevs /dev/hda b 3 0 0 8 s\n"
//usage:       "[creates hda,hda1-hda8]\n"
//usage:#endif
//usage:
//usage:#if ENABLE_FEATURE_MAKEDEVS_TABLE
//usage:#define makedevs_trivial_usage
//usage:       "[-d device_table] rootdir"
//usage:#define makedevs_full_usage "\n\n"
//usage:       "Create a range of special files as specified in a device table.\n"
//usage:       "Device table entries take the form of:\n"
//usage:       "<name> <type> <mode> <uid> <gid> <major> <minor> <start> <inc> <count>\n"
//usage:       "Where name is the file name, type can be one of:\n"
//usage:       "	f	Regular file\n"
//usage:       "	d	Directory\n"
//usage:       "	c	Character device\n"
//usage:       "	b	Block device\n"
//usage:       "	p	Fifo (named pipe)\n"
//usage:       "uid is the user id for the target file, gid is the group id for the\n"
//usage:       "target file. The rest of the entries (major, minor, etc) apply to\n"
//usage:       "to device special files. A '-' may be used for blank entries."
//usage:
//usage:#define makedevs_example_usage
//usage:       "For example:\n"
//usage:       "<name>    <type> <mode><uid><gid><major><minor><start><inc><count>\n"
//usage:       "/dev         d   755    0    0    -      -      -      -    -\n"
//usage:       "/dev/console c   666    0    0    5      1      -      -    -\n"
//usage:       "/dev/null    c   666    0    0    1      3      0      0    -\n"
//usage:       "/dev/zero    c   666    0    0    1      5      0      0    -\n"
//usage:       "/dev/hda     b   640    0    0    3      0      0      0    -\n"
//usage:       "/dev/hda     b   640    0    0    3      1      1      1    15\n\n"
//usage:       "Will Produce:\n"
//usage:       "/dev\n"
//usage:       "/dev/console\n"
//usage:       "/dev/null\n"
//usage:       "/dev/zero\n"
//usage:       "/dev/hda\n"
//usage:       "/dev/hda[0-15]\n"
//usage:#endif

#include "libbb.h"

#if ENABLE_FEATURE_MAKEDEVS_LEAF
/*
makedevs NAME TYPE MAJOR MINOR FIRST LAST [s]
TYPEs:
b       Block device
c       Character device
f       FIFO

FIRST..LAST specify numbers appended to NAME.
If 's' is the last argument, the base device is created as well.
Examples:
        makedevs /dev/ttyS c 4 66 2 63   ->  ttyS2-ttyS63
        makedevs /dev/hda b 3 0 0 8 s    ->  hda,hda1-hda8
*/
int makedevs_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int makedevs_main(int argc, char **argv)
{
	mode_t mode;
	char *basedev, *type, *nodname, *buf;
	int Smajor, Sminor, S, E;

	if (argc < 7 || argv[1][0] == '-')
		bb_show_usage();

	basedev = argv[1];
	buf = xasprintf("%s%u", argv[1], (unsigned)-1);
	type = argv[2];
	Smajor = xatoi_positive(argv[3]);
	Sminor = xatoi_positive(argv[4]);
	S = xatoi_positive(argv[5]);
	E = xatoi_positive(argv[6]);
	nodname = argv[7] ? basedev : buf;

	mode = 0660;
	switch (type[0]) {
	case 'c':
		mode |= S_IFCHR;
		break;
	case 'b':
		mode |= S_IFBLK;
		break;
	case 'f':
		mode |= S_IFIFO;
		break;
	default:
		bb_show_usage();
	}

	while (S <= E) {
		sprintf(buf, "%s%u", basedev, S);

		/* if mode != S_IFCHR and != S_IFBLK,
		 * third param in mknod() ignored */
		if (mknod(nodname, mode, makedev(Smajor, Sminor)) != 0
		 && errno != EEXIST
		) {
			bb_perror_msg("can't create '%s'", nodname);
		}

		/*if (nodname == basedev)*/ /* ex. /dev/hda - to /dev/hda1 ... */
			nodname = buf;
		S++;
		Sminor++;
	}

	return 0;
}

#elif ENABLE_FEATURE_MAKEDEVS_TABLE

/* Licensed under GPLv2 or later, see file LICENSE in this source tree. */

int makedevs_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int makedevs_main(int argc UNUSED_PARAM, char **argv)
{
	parser_t *parser;
	char *line = (char *)"-";
	int ret = EXIT_SUCCESS;

	getopt32(argv, "^" "d:" "\0" "=1", &line);
	argv += optind;

	xchdir(*argv); /* ensure root dir exists */

	umask(0);

	printf("rootdir=%s\ntable=", *argv);
	if (NOT_LONE_DASH(line)) {
		printf("'%s'\n", line);
	} else {
		puts("<stdin>");
	}

	parser = config_open(line);
	while (config_read(parser, &line, 1, 1, "# \t", PARSE_NORMAL)) {
		int linenum;
		char type;
		unsigned mode = 0755;
		unsigned major = 0;
		unsigned minor = 0;
		unsigned count = 0;
		unsigned increment = 0;
		unsigned start = 0;
		char user[41];
		char group[41];
		char *full_name;
		int name_len;
		uid_t uid;
		gid_t gid;

		linenum = parser->lineno;

		if ((1 > sscanf(line, "%*s%n %c %o %40s %40s %u %u %u %u %u",
					&name_len, &type, &mode, user, group,
					&major, &minor, &start, &increment, &count))
		 || ((unsigned)(major | minor | start | count | increment) > 255)
		) {
			bb_error_msg("invalid line %d: '%s'", linenum, line);
			ret = EXIT_FAILURE;
			continue;
		}

		gid = (*group) ? get_ug_id(group, xgroup2gid) : getgid();
		uid = (*user) ? get_ug_id(user, xuname2uid) : getuid();
		line[name_len] = '\0';
		full_name = line;
		/* We are already in the right root dir,
		 * so make absolute paths relative */
		if ('/' == full_name[0])
			full_name++;

		if (type == 'd') {
			bb_make_directory(full_name, mode | S_IFDIR, FILEUTILS_RECUR);
			if (chown(full_name, uid, gid) == -1) {
 chown_fail:
				bb_perror_msg("line %d: can't chown %s", linenum, full_name);
				ret = EXIT_FAILURE;
				continue;
			}
			if (chmod(full_name, mode) < 0) {
 chmod_fail:
				bb_perror_msg("line %d: can't chmod %s", linenum, full_name);
				ret = EXIT_FAILURE;
				continue;
			}
		} else if (type == 'f') {
			struct stat st;
			if ((stat(full_name, &st) < 0 || !S_ISREG(st.st_mode))) {
				bb_perror_msg("line %d: regular file '%s' does not exist", linenum, full_name);
				ret = EXIT_FAILURE;
				continue;
			}
			if (chown(full_name, uid, gid) < 0)
				goto chown_fail;
			if (chmod(full_name, mode) < 0)
				goto chmod_fail;
		} else {
			unsigned i;

			if (type == 'p') {
				mode |= S_IFIFO;
			} else if (type == 'c') {
				mode |= S_IFCHR;
			} else if (type == 'b') {
				mode |= S_IFBLK;
			} else {
				bb_error_msg("line %d: unsupported file type %c", linenum, type);
				ret = EXIT_FAILURE;
				continue;
			}

			if (count != 0)
				count--;
			for (i = 0; i <= count; i++) {
				dev_t rdev;
				char *nameN = full_name;
				if (count != 0)
					nameN = xasprintf("%s%u", full_name, start + i);
				rdev = makedev(major, minor + i * increment);
				if (mknod(nameN, mode, rdev) != 0
				 && errno != EEXIST
				) {
					bb_perror_msg("line %d: can't create node %s", linenum, nameN);
					ret = EXIT_FAILURE;
				} else if (chown(nameN, uid, gid) < 0) {
					bb_perror_msg("line %d: can't chown %s", linenum, nameN);
					ret = EXIT_FAILURE;
				} else if (chmod(nameN, mode) < 0) {
					bb_perror_msg("line %d: can't chmod %s", linenum, nameN);
					ret = EXIT_FAILURE;
				}
				if (count != 0)
					free(nameN);
			}
		}
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		config_close(parser);

	return ret;
}

#else
# error makedevs configuration error, either leaf or table must be selected
#endif
