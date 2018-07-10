/* vi: set sw=4 ts=4: */
/*
 *  mini dpkg implementation for busybox.
 *  this is not meant as a replacement for dpkg
 *
 *  written by glenn mcgrath with the help of others
 *  copyright (c) 2001 by glenn mcgrath
 *
 *  parts of the version comparison code is plucked from the real dpkg
 *  application which is licensed GPLv2 and
 *  copyright (c) 1995 Ian Jackson <ian@chiark.greenend.org.uk>
 *
 *  started life as a busybox implementation of udpkg
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/*
 * known difference between busybox dpkg and the official dpkg that i don't
 * consider important, its worth keeping a note of differences anyway, just to
 * make it easier to maintain.
 *  - the first value for the confflile: field isn't placed on a new line.
 *  - when installing a package the status: field is placed at the end of the
 *      section, rather than just after the package: field.
 *
 * bugs that need to be fixed
 *  - (unknown, please let me know when you find any)
 */
//config:config DPKG
//config:	bool "dpkg (44 kb)"
//config:	default y
//config:	select FEATURE_SEAMLESS_GZ
//config:	help
//config:	dpkg is a medium-level tool to install, build, remove and manage
//config:	Debian packages.
//config:
//config:	This implementation of dpkg has a number of limitations,
//config:	you should use the official dpkg if possible.

//applet:IF_DPKG(APPLET(dpkg, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_DPKG) += dpkg.o

//usage:#define dpkg_trivial_usage
//usage:       "[-ilCPru] [-F OPT] PACKAGE"
//usage:#define dpkg_full_usage "\n\n"
//usage:       "Install, remove and manage Debian packages\n"
//usage:	IF_LONG_OPTS(
//usage:     "\n	-i,--install	Install the package"
//usage:     "\n	-l,--list	List of installed packages"
//usage:     "\n	--configure	Configure an unpackaged package"
//usage:     "\n	-P,--purge	Purge all files of a package"
//usage:     "\n	-r,--remove	Remove all but the configuration files for a package"
//usage:     "\n	--unpack	Unpack a package, but don't configure it"
//usage:     "\n	--force-depends	Ignore dependency problems"
//usage:     "\n	--force-confnew	Overwrite existing config files when installing"
//usage:     "\n	--force-confold	Keep old config files when installing"
//usage:	)
//usage:	IF_NOT_LONG_OPTS(
//usage:     "\n	-i		Install the package"
//usage:     "\n	-l		List of installed packages"
//usage:     "\n	-C		Configure an unpackaged package"
//usage:     "\n	-P		Purge all files of a package"
//usage:     "\n	-r		Remove all but the configuration files for a package"
//usage:     "\n	-u		Unpack a package, but don't configure it"
//usage:     "\n	-F depends	Ignore dependency problems"
//usage:     "\n	-F confnew	Overwrite existing config files when installing"
//usage:     "\n	-F confold	Keep old config files when installing"
//usage:	)

#include "libbb.h"
#include <fnmatch.h>
#include "bb_archive.h"

/* note: if you vary hash_prime sizes be aware,
 * 1) tweaking these will have a big effect on how much memory this program uses.
 * 2) for computational efficiency these hash tables should be at least 20%
 *    larger than the maximum number of elements stored in it.
 * 3) all _hash_prime's must be a prime number or chaos is assured, if your looking
 *    for a prime, try http://www.utm.edu/research/primes/lists/small/10000.txt
 * 4) if you go bigger than 15 bits you may get into trouble (untested) as its
 *    sometimes cast to an unsigned, if you go to 16 bit you will overlap
 *    int's and chaos is assured, 16381 is the max prime for 14 bit field
 */

/* NAME_HASH_PRIME, Stores package names and versions,
 * I estimate it should be at least 50% bigger than PACKAGE_HASH_PRIME,
 * as there a lot of duplicate version numbers */
#define NAME_HASH_PRIME 16381

/* PACKAGE_HASH_PRIME, Maximum number of unique packages,
 * It must not be smaller than STATUS_HASH_PRIME,
 * Currently only packages from status_hashtable are stored in here, but in
 * future this may be used to store packages not only from a status file,
 * but an available_hashtable, and even multiple packages files.
 * Package can be stored more than once if they have different versions.
 * e.g. The same package may have different versions in the status file
 *      and available file */
#define PACKAGE_HASH_PRIME 10007
typedef struct edge_s {
	unsigned operator:4; /* was:3 */
	unsigned type:4;
	unsigned name:16; /* was:14 */
	unsigned version:16; /* was:14 */
} edge_t;

typedef struct common_node_s {
	unsigned name:16; /* was:14 */
	unsigned version:16; /* was:14 */
	unsigned num_of_edges:16; /* was:14 */
	edge_t **edge;
} common_node_t;

/* Currently it doesn't store packages that have state-status of not-installed
 * So it only really has to be the size of the maximum number of packages
 * likely to be installed at any one time, so there is a bit of leeway here */
#define STATUS_HASH_PRIME 8191
typedef struct status_node_s {
	unsigned package:16; /* was:14 */       /* has to fit PACKAGE_HASH_PRIME */
	unsigned status:16; /* was:14 */        /* has to fit STATUS_HASH_PRIME */
} status_node_t;


/* Globals */
struct globals {
	char          *name_hashtable[NAME_HASH_PRIME + 1];
	common_node_t *package_hashtable[PACKAGE_HASH_PRIME + 1];
	status_node_t *status_hashtable[STATUS_HASH_PRIME + 1];
};
#define G (*ptr_to_globals)
#define name_hashtable    (G.name_hashtable   )
#define package_hashtable (G.package_hashtable)
#define status_hashtable  (G.status_hashtable )
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)


/* Even numbers are for 'extras', like ored dependencies or null */
enum edge_type_e {
	EDGE_NULL = 0,
	EDGE_PRE_DEPENDS = 1,
	EDGE_OR_PRE_DEPENDS = 2,
	EDGE_DEPENDS = 3,
	EDGE_OR_DEPENDS = 4,
	EDGE_REPLACES = 5,
	EDGE_PROVIDES = 7,
	EDGE_CONFLICTS = 9,
	EDGE_SUGGESTS = 11,
	EDGE_RECOMMENDS = 13,
	EDGE_ENHANCES = 15
};
enum operator_e {
	VER_NULL = 0,
	VER_EQUAL = 1,
	VER_LESS = 2,
	VER_LESS_EQUAL = 3,
	VER_MORE = 4,
	VER_MORE_EQUAL = 5,
	VER_ANY = 6
};

typedef struct deb_file_s {
	char *control_file;
	char *filename;
	unsigned package:16; /* was:14 */
} deb_file_t;


static void make_hash(const char *key, unsigned *start, unsigned *decrement, const int hash_prime)
{
	unsigned long hash_num = key[0];
	int len = strlen(key);
	int i;

	/* Maybe i should have uses a "proper" hashing algorithm here instead
	 * of making one up myself, seems to be working ok though. */
	for (i = 1; i < len; i++) {
		/* shifts the ascii based value and adds it to previous value
		 * shift amount is mod 24 because long int is 32 bit and data
		 * to be shifted is 8, don't want to shift data to where it has
		 * no effect */
		hash_num += (key[i] + key[i-1]) << ((key[i] * i) % 24);
	}
	*start = (unsigned) hash_num % hash_prime;
	*decrement = (unsigned) 1 + (hash_num % (hash_prime - 1));
}

/* this adds the key to the hash table */
static int search_name_hashtable(const char *key)
{
	unsigned probe_address;
	unsigned probe_decrement;

	make_hash(key, &probe_address, &probe_decrement, NAME_HASH_PRIME);
	while (name_hashtable[probe_address] != NULL) {
		if (strcmp(name_hashtable[probe_address], key) == 0) {
			return probe_address;
		}
		probe_address -= probe_decrement;
		if ((int)probe_address < 0) {
			probe_address += NAME_HASH_PRIME;
		}
	}
	name_hashtable[probe_address] = xstrdup(key);
	return probe_address;
}

/* this DOESN'T add the key to the hashtable
 * TODO make it consistent with search_name_hashtable
 */
static unsigned search_status_hashtable(const char *key)
{
	unsigned probe_address;
	unsigned probe_decrement;

	make_hash(key, &probe_address, &probe_decrement, STATUS_HASH_PRIME);
	while (status_hashtable[probe_address] != NULL) {
		if (strcmp(key, name_hashtable[package_hashtable[status_hashtable[probe_address]->package]->name]) == 0) {
			break;
		}
		probe_address -= probe_decrement;
		if ((int)probe_address < 0) {
			probe_address += STATUS_HASH_PRIME;
		}
	}
	return probe_address;
}

static int order(char x)
{
	return (x == '~' ? -1
		: x == '\0' ? 0
		: isdigit(x) ? 0
		: isalpha(x) ? x
		: (unsigned char)x + 256
	);
}

/* This code is taken from dpkg and modified slightly to work with busybox */
static int version_compare_part(const char *val, const char *ref)
{
	if (!val) val = "";
	if (!ref) ref = "";

	while (*val || *ref) {
		int first_diff;

		while ((*val && !isdigit(*val)) || (*ref && !isdigit(*ref))) {
			int vc = order(*val);
			int rc = order(*ref);
			if (vc != rc)
				return vc - rc;
			val++;
			ref++;
		}

		while (*val == '0')
			val++;
		while (*ref == '0')
			ref++;

		first_diff = 0;
		while (isdigit(*val) && isdigit(*ref)) {
			if (first_diff == 0)
				first_diff = *val - *ref;
			val++;
			ref++;
		}
		if (isdigit(*val))
			return 1;
		if (isdigit(*ref))
			return -1;
		if (first_diff)
			return first_diff;
	}
	return 0;
}

/* if ver1 < ver2 return -1,
 * if ver1 = ver2 return 0,
 * if ver1 > ver2 return 1,
 */
static int version_compare(const unsigned ver1, const unsigned ver2)
{
	char *ch_ver1 = name_hashtable[ver1];
	char *ch_ver2 = name_hashtable[ver2];
	unsigned epoch1 = 0, epoch2 = 0;
	char *colon;
	char *deb_ver1, *deb_ver2;
	char *upstream_ver1;
	char *upstream_ver2;
	int result;

	/* Compare epoch */
	colon = strchr(ch_ver1, ':');
	if (colon) {
		epoch1 = atoi(ch_ver1);
		ch_ver1 = colon + 1;
	}
	colon = strchr(ch_ver2, ':');
	if (colon) {
		epoch2 = atoi(ch_ver2);
		ch_ver2 = colon + 1;
	}
	if (epoch1 < epoch2) {
		return -1;
	}
	if (epoch1 > epoch2) {
		return 1;
	}

	/* Compare upstream version */
	upstream_ver1 = xstrdup(ch_ver1);
	upstream_ver2 = xstrdup(ch_ver2);

	/* Chop off debian version, and store for later use */
	deb_ver1 = strrchr(upstream_ver1, '-');
	deb_ver2 = strrchr(upstream_ver2, '-');
	if (deb_ver1) {
		*deb_ver1++ = '\0';
	}
	if (deb_ver2) {
		*deb_ver2++ = '\0';
	}
	result = version_compare_part(upstream_ver1, upstream_ver2);
	if (result == 0) {
		/* Compare debian versions */
		result = version_compare_part(deb_ver1, deb_ver2);
	}

	free(upstream_ver1);
	free(upstream_ver2);
	return result;
}

static int test_version(const unsigned version1, const unsigned version2, const unsigned operator)
{
	const int version_result = version_compare(version1, version2);
	switch (operator) {
	case VER_ANY:
		return TRUE;
	case VER_EQUAL:
		return (version_result == 0);
	case VER_LESS:
		return (version_result < 0);
	case VER_LESS_EQUAL:
		return (version_result <= 0);
	case VER_MORE:
		return (version_result > 0);
	case VER_MORE_EQUAL:
		return (version_result >= 0);
	}
	return FALSE;
}

static int search_package_hashtable(const unsigned name, const unsigned version, const unsigned operator)
{
	unsigned probe_address;
	unsigned probe_decrement;

	make_hash(name_hashtable[name], &probe_address, &probe_decrement, PACKAGE_HASH_PRIME);
	while (package_hashtable[probe_address] != NULL) {
		if (package_hashtable[probe_address]->name == name) {
			if (operator == VER_ANY) {
				return probe_address;
			}
			if (test_version(package_hashtable[probe_address]->version, version, operator)) {
				return probe_address;
			}
		}
		probe_address -= probe_decrement;
		if ((int)probe_address < 0) {
			probe_address += PACKAGE_HASH_PRIME;
		}
	}
	return probe_address;
}

/*
 * This function searches through the entire package_hashtable looking
 * for a package which provides "needle". It returns the index into
 * the package_hashtable for the providing package.
 *
 * needle is the index into name_hashtable of the package we are
 * looking for.
 *
 * start_at is the index in the package_hashtable to start looking
 * at. If start_at is -1 then start at the beginning. This is to allow
 * for repeated searches since more than one package might provide
 * needle.
 *
 * FIXME: I don't think this is very efficient, but I thought I'd keep
 * it simple for now until it proves to be a problem.
 */
static int search_for_provides(int needle, int start_at)
{
	int i, j;
	common_node_t *p;
	for (i = start_at + 1; i < PACKAGE_HASH_PRIME; i++) {
		p = package_hashtable[i];
		if (p == NULL)
			continue;
		for (j = 0; j < p->num_of_edges; j++)
			if (p->edge[j]->type == EDGE_PROVIDES && p->edge[j]->name == needle)
				return i;
	}
	return -1;
}

/*
 * Add an edge to a node
 */
static void add_edge_to_node(common_node_t *node, edge_t *edge)
{
	node->edge = xrealloc_vector(node->edge, 2, node->num_of_edges);
	node->edge[node->num_of_edges++] = edge;
}

/*
 * Create one new node and one new edge for every dependency.
 *
 * Dependencies which contain multiple alternatives are represented as
 * an EDGE_OR_PRE_DEPENDS or EDGE_OR_DEPENDS node, followed by a
 * number of EDGE_PRE_DEPENDS or EDGE_DEPENDS nodes. The name field of
 * the OR edge contains the full dependency string while the version
 * field contains the number of EDGE nodes which follow as part of
 * this alternative.
 */
static void add_split_dependencies(common_node_t *parent_node, const char *whole_line, unsigned edge_type)
{
	char *line = xstrdup(whole_line);
	char *line2;
	char *line_ptr1 = NULL;
	char *line_ptr2 = NULL;
	char *field;
	char *field2;
	char *version;
	edge_t *edge;
	edge_t *or_edge;
	int offset_ch;

	field = strtok_r(line, ",", &line_ptr1);
	do {
		/* skip leading spaces */
		field += strspn(field, " ");
		line2 = xstrdup(field);
		field2 = strtok_r(line2, "|", &line_ptr2);
		or_edge = NULL;
		if ((edge_type == EDGE_DEPENDS || edge_type == EDGE_PRE_DEPENDS)
		 && (strcmp(field, field2) != 0)
		) {
			or_edge = xzalloc(sizeof(edge_t));
			or_edge->type = edge_type + 1;
			or_edge->name = search_name_hashtable(field);
			//or_edge->version = 0; // tracks the number of alternatives
			add_edge_to_node(parent_node, or_edge);
		}

		do {
			edge = xmalloc(sizeof(edge_t));
			edge->type = edge_type;

			/* Skip any extra leading spaces */
			field2 += strspn(field2, " ");

			/* Get dependency version info */
			version = strchr(field2, '(');
			if (version == NULL) {
				edge->operator = VER_ANY;
				/* Get the versions hash number, adding it if the number isn't already in there */
				edge->version = search_name_hashtable("ANY");
			} else {
				/* Skip leading ' ' or '(' */
				version += strspn(version, " (");
				/* Calculate length of any operator characters */
				offset_ch = strspn(version, "<=>");
				/* Determine operator */
				if (offset_ch > 0) {
					if (strncmp(version, "=", offset_ch) == 0) {
						edge->operator = VER_EQUAL;
					} else if (strncmp(version, "<<", offset_ch) == 0) {
						edge->operator = VER_LESS;
					} else if (strncmp(version, "<=", offset_ch) == 0) {
						edge->operator = VER_LESS_EQUAL;
					} else if (strncmp(version, ">>", offset_ch) == 0) {
						edge->operator = VER_MORE;
					} else if (strncmp(version, ">=", offset_ch) == 0) {
						edge->operator = VER_MORE_EQUAL;
					} else {
						bb_error_msg_and_die("illegal operator");
					}
				}
				/* skip to start of version numbers */
				version += offset_ch;
				version += strspn(version, " ");

				/* Truncate version at trailing ' ' or ')' */
				version[strcspn(version, " )")] = '\0';
				/* Get the versions hash number, adding it if the number isn't already in there */
				edge->version = search_name_hashtable(version);
			}

			/* Get the dependency name */
			field2[strcspn(field2, " (")] = '\0';
			edge->name = search_name_hashtable(field2);

			if (or_edge)
				or_edge->version++;

			add_edge_to_node(parent_node, edge);
			field2 = strtok_r(NULL, "|", &line_ptr2);
		} while (field2 != NULL);

		free(line2);
		field = strtok_r(NULL, ",", &line_ptr1);
	} while (field != NULL);

	free(line);
}

static void free_package(common_node_t *node)
{
	unsigned i;
	if (node) {
		for (i = 0; i < node->num_of_edges; i++) {
			free(node->edge[i]);
		}
		free(node->edge);
		free(node);
	}
}

/*
 * Gets the next package field from package_buffer, separated into the field name
 * and field value, it returns the int offset to the first character of the next field
 */
static int read_package_field(const char *package_buffer, char **field_name, char **field_value)
{
	int offset_name_start = 0;
	int offset_name_end = 0;
	int offset_value_start = 0;
	int offset_value_end = 0;
	int offset = 0;
	int next_offset;
	int name_length;
	int value_length;
	int exit_flag = FALSE;

	if (package_buffer == NULL) {
		*field_name = NULL;
		*field_value = NULL;
		return -1;
	}
	while (1) {
		next_offset = offset + 1;
		switch (package_buffer[offset]) {
			case '\0':
				exit_flag = TRUE;
				break;
			case ':':
				if (offset_name_end == 0) {
					offset_name_end = offset;
					offset_value_start = next_offset;
				}
				/* TODO: Name might still have trailing spaces if ':' isn't
				 * immediately after name */
				break;
			case '\n':
				/* TODO: The char next_offset may be out of bounds */
				if (package_buffer[next_offset] != ' ') {
					exit_flag = TRUE;
					break;
				}
			case '\t':
			case ' ':
				/* increment the value start point if its a just filler */
				if (offset_name_start == offset) {
					offset_name_start++;
				}
				if (offset_value_start == offset) {
					offset_value_start++;
				}
				break;
		}
		if (exit_flag) {
			/* Check that the names are valid */
			offset_value_end = offset;
			name_length = offset_name_end - offset_name_start;
			value_length = offset_value_end - offset_value_start;
			if (name_length == 0) {
				break;
			}
			if ((name_length > 0) && (value_length > 0)) {
				break;
			}

			/* If not valid, start fresh with next field */
			exit_flag = FALSE;
			offset_name_start = offset + 1;
			offset_name_end = 0;
			offset_value_start = offset + 1;
			offset_value_end = offset + 1;
			offset++;
		}
		offset++;
	}
	*field_name = NULL;
	if (name_length) {
		*field_name = xstrndup(&package_buffer[offset_name_start], name_length);
	}
	*field_value = NULL;
	if (value_length > 0) {
		*field_value = xstrndup(&package_buffer[offset_value_start], value_length);
	}
	return next_offset;
}

static unsigned fill_package_struct(char *control_buffer)
{
	static const char field_names[] ALIGN1 =
		"Package\0""Version\0"
		"Pre-Depends\0""Depends\0""Replaces\0""Provides\0"
		"Conflicts\0""Suggests\0""Recommends\0""Enhances\0";

	common_node_t *new_node = xzalloc(sizeof(common_node_t));
	char *field_name;
	char *field_value;
	int field_start = 0;
	int num = -1;
	int buffer_length = strlen(control_buffer);

	new_node->version = search_name_hashtable("unknown");
	while (field_start < buffer_length) {
		unsigned field_num;

		field_start += read_package_field(&control_buffer[field_start],
				&field_name, &field_value);

		if (field_name == NULL) {
			goto fill_package_struct_cleanup;
		}

		field_num = index_in_strings(field_names, field_name);
		switch (field_num) {
		case 0: /* Package */
			new_node->name = search_name_hashtable(field_value);
			break;
		case 1: /* Version */
			new_node->version = search_name_hashtable(field_value);
			break;
		case 2: /* Pre-Depends */
			add_split_dependencies(new_node, field_value, EDGE_PRE_DEPENDS);
			break;
		case 3: /* Depends */
			add_split_dependencies(new_node, field_value, EDGE_DEPENDS);
			break;
		case 4: /* Replaces */
			add_split_dependencies(new_node, field_value, EDGE_REPLACES);
			break;
		case 5: /* Provides */
			add_split_dependencies(new_node, field_value, EDGE_PROVIDES);
			break;
		case 6: /* Conflicts */
			add_split_dependencies(new_node, field_value, EDGE_CONFLICTS);
			break;
		case 7: /* Suggests */
			add_split_dependencies(new_node, field_value, EDGE_SUGGESTS);
			break;
		case 8: /* Recommends */
			add_split_dependencies(new_node, field_value, EDGE_RECOMMENDS);
			break;
		case 9: /* Enhances */
			add_split_dependencies(new_node, field_value, EDGE_ENHANCES);
			break;
		}
 fill_package_struct_cleanup:
		free(field_name);
		free(field_value);
	}

	if (new_node->version == search_name_hashtable("unknown")) {
		free_package(new_node);
		return -1;
	}
	num = search_package_hashtable(new_node->name, new_node->version, VER_EQUAL);
	free_package(package_hashtable[num]);
	package_hashtable[num] = new_node;
	return num;
}

/* if num = 1, it returns the want status, 2 returns flag, 3 returns status */
static unsigned get_status(const unsigned status_node, const int num)
{
	char *status_string = name_hashtable[status_hashtable[status_node]->status];
	char *state_sub_string;
	unsigned state_sub_num;
	int len;
	int i;

	/* set tmp_string to point to the start of the word number */
	for (i = 1; i < num; i++) {
		/* skip past a word */
		status_string += strcspn(status_string, " ");
		/* skip past the separating spaces */
		status_string += strspn(status_string, " ");
	}
	len = strcspn(status_string, " \n");
	state_sub_string = xstrndup(status_string, len);
	state_sub_num = search_name_hashtable(state_sub_string);
	free(state_sub_string);
	return state_sub_num;
}

static void set_status(const unsigned status_node_num, const char *new_value, const int position)
{
	const unsigned new_value_num = search_name_hashtable(new_value);
	unsigned want = get_status(status_node_num, 1);
	unsigned flag = get_status(status_node_num, 2);
	unsigned status = get_status(status_node_num, 3);
	char *new_status;

	switch (position) {
		case 1:
			want = new_value_num;
			break;
		case 2:
			flag = new_value_num;
			break;
		case 3:
			status = new_value_num;
			break;
		default:
			bb_error_msg_and_die("DEBUG ONLY: this shouldnt happen");
	}

	new_status = xasprintf("%s %s %s", name_hashtable[want], name_hashtable[flag], name_hashtable[status]);
	status_hashtable[status_node_num]->status = search_name_hashtable(new_status);
	free(new_status);
}

static const char *describe_status(int status_num)
{
	int status_want, status_state;
	if (status_hashtable[status_num] == NULL || status_hashtable[status_num]->status == 0)
		return "is not installed or flagged to be installed";

	status_want = get_status(status_num, 1);
	status_state = get_status(status_num, 3);

	if (status_state == search_name_hashtable("installed")) {
		if (status_want == search_name_hashtable("install"))
			return "is installed";
		if (status_want == search_name_hashtable("deinstall"))
			return "is marked to be removed";
		if (status_want == search_name_hashtable("purge"))
			return "is marked to be purged";
	}
	if (status_want == search_name_hashtable("unknown"))
		return "is in an indeterminate state";
	if (status_want == search_name_hashtable("install"))
		return "is marked to be installed";

	return "is not installed or flagged to be installed";
}

static void index_status_file(const char *filename)
{
	FILE *status_file;
	char *control_buffer;
	char *status_line;
	status_node_t *status_node = NULL;
	unsigned status_num;

	status_file = xfopen_for_read(filename);
	while ((control_buffer = xmalloc_fgetline_str(status_file, "\n\n")) != NULL) {
		const unsigned package_num = fill_package_struct(control_buffer);
		if (package_num != -1) {
			status_node = xmalloc(sizeof(status_node_t));
			/* fill_package_struct doesn't handle the status field */
			status_line = strstr(control_buffer, "Status:");
			if (status_line != NULL) {
				status_line += 7;
				status_line += strspn(status_line, " \n\t");
				status_line = xstrndup(status_line, strcspn(status_line, "\n"));
				status_node->status = search_name_hashtable(status_line);
				free(status_line);
			}
			status_node->package = package_num;
			status_num = search_status_hashtable(name_hashtable[package_hashtable[status_node->package]->name]);
			status_hashtable[status_num] = status_node;
		}
		free(control_buffer);
	}
	fclose(status_file);
}

static void write_buffer_no_status(FILE *new_status_file, const char *control_buffer)
{
	char *name;
	char *value;
	int start = 0;
	while (1) {
		start += read_package_field(&control_buffer[start], &name, &value);
		if (name == NULL) {
			break;
		}
		if (strcmp(name, "Status") != 0) {
			fprintf(new_status_file, "%s: %s\n", name, value);
		}
	}
}

/* This could do with a cleanup */
static void write_status_file(deb_file_t **deb_file)
{
	FILE *old_status_file = xfopen_for_read("/var/lib/dpkg/status");
	FILE *new_status_file = xfopen_for_write("/var/lib/dpkg/status.udeb");
	char *package_name;
	char *status_from_file;
	char *control_buffer = NULL;
	char *tmp_string;
	int status_num;
	int field_start = 0;
	int write_flag;
	int i = 0;

	/* Update previously known packages */
	while ((control_buffer = xmalloc_fgetline_str(old_status_file, "\n\n")) != NULL) {
		tmp_string = strstr(control_buffer, "Package:");
		if (tmp_string == NULL) {
			continue;
		}

		tmp_string += 8;
		tmp_string += strspn(tmp_string, " \n\t");
		package_name = xstrndup(tmp_string, strcspn(tmp_string, "\n"));
		write_flag = FALSE;
		tmp_string = strstr(control_buffer, "Status:");
		if (tmp_string != NULL) {
			/* Separate the status value from the control buffer */
			tmp_string += 7;
			tmp_string += strspn(tmp_string, " \n\t");
			status_from_file = xstrndup(tmp_string, strcspn(tmp_string, "\n"));
		} else {
			status_from_file = NULL;
		}

		/* Find this package in the status hashtable */
		status_num = search_status_hashtable(package_name);
		if (status_hashtable[status_num] != NULL) {
			const char *status_from_hashtable = name_hashtable[status_hashtable[status_num]->status];
			if (strcmp(status_from_file, status_from_hashtable) != 0) {
				/* New status isn't exactly the same as old status */
				const int state_status = get_status(status_num, 3);
				if ((strcmp("installed", name_hashtable[state_status]) == 0)
				 || (strcmp("unpacked", name_hashtable[state_status]) == 0)
				) {
					/* We need to add the control file from the package */
					i = 0;
					while (deb_file[i] != NULL) {
						if (strcmp(package_name, name_hashtable[package_hashtable[deb_file[i]->package]->name]) == 0) {
							/* Write a status file entry with a modified status */
							/* remove trailing \n's */
							write_buffer_no_status(new_status_file, deb_file[i]->control_file);
							set_status(status_num, "ok", 2);
							fprintf(new_status_file, "Status: %s\n\n",
									name_hashtable[status_hashtable[status_num]->status]);
							write_flag = TRUE;
							break;
						}
						i++;
					}
					/* This is temperary, debugging only */
					if (deb_file[i] == NULL) {
						bb_error_msg_and_die("ALERT: cannot find a control file, "
							"your status file may be broken, status may be "
							"incorrect for %s", package_name);
					}
				}
				else if (strcmp("not-installed", name_hashtable[state_status]) == 0) {
					/* Only write the Package, Status, Priority and Section lines */
					fprintf(new_status_file, "Package: %s\n", package_name);
					fprintf(new_status_file, "Status: %s\n", status_from_hashtable);

					while (1) {
						char *field_name;
						char *field_value;
						field_start += read_package_field(&control_buffer[field_start], &field_name, &field_value);
						if (field_name == NULL) {
							break;
						}
						if ((strcmp(field_name, "Priority") == 0)
						 || (strcmp(field_name, "Section") == 0)
						) {
							fprintf(new_status_file, "%s: %s\n", field_name, field_value);
						}
					}
					write_flag = TRUE;
					fputs("\n", new_status_file);
				}
				else if (strcmp("config-files", name_hashtable[state_status]) == 0) {
					/* only change the status line */
					while (1) {
						char *field_name;
						char *field_value;
						field_start += read_package_field(&control_buffer[field_start], &field_name, &field_value);
						if (field_name == NULL) {
							break;
						}
						/* Setup start point for next field */
						if (strcmp(field_name, "Status") == 0) {
							fprintf(new_status_file, "Status: %s\n", status_from_hashtable);
						} else {
							fprintf(new_status_file, "%s: %s\n", field_name, field_value);
						}
					}
					write_flag = TRUE;
					fputs("\n", new_status_file);
				}
			}
		}
		/* If the package from the status file wasn't handle above, do it now*/
		if (!write_flag) {
			fprintf(new_status_file, "%s\n\n", control_buffer);
		}

		free(status_from_file);
		free(package_name);
		free(control_buffer);
	}

	/* Write any new packages */
	for (i = 0; deb_file[i] != NULL; i++) {
		status_num = search_status_hashtable(name_hashtable[package_hashtable[deb_file[i]->package]->name]);
		if (strcmp("reinstreq", name_hashtable[get_status(status_num, 2)]) == 0) {
			write_buffer_no_status(new_status_file, deb_file[i]->control_file);
			set_status(status_num, "ok", 2);
			fprintf(new_status_file, "Status: %s\n\n", name_hashtable[status_hashtable[status_num]->status]);
		}
	}
	fclose(old_status_file);
	fclose(new_status_file);

	/* Create a separate backfile to dpkg */
	if (rename("/var/lib/dpkg/status", "/var/lib/dpkg/status.udeb.bak") == -1) {
		if (errno != ENOENT)
			bb_error_msg_and_die("can't create backup status file");
		/* Its ok if renaming the status file fails because status
		 * file doesn't exist, maybe we are starting from scratch */
		bb_error_msg("no status file found, creating new one");
	}

	xrename("/var/lib/dpkg/status.udeb", "/var/lib/dpkg/status");
}

/* This function returns TRUE if the given package can satisfy a
 * dependency of type depend_type.
 *
 * A pre-depends is satisfied only if a package is already installed,
 * which a regular depends can be satisfied by a package which we want
 * to install.
 */
static int package_satisfies_dependency(int package, int depend_type)
{
	int status_num = search_status_hashtable(name_hashtable[package_hashtable[package]->name]);

	/* status could be unknown if package is a pure virtual
	 * provides which cannot satisfy any dependency by itself.
	 */
	if (status_hashtable[status_num] == NULL)
		return 0;

	switch (depend_type) {
	case EDGE_PRE_DEPENDS: return get_status(status_num, 3) == search_name_hashtable("installed");
	case EDGE_DEPENDS:     return get_status(status_num, 1) == search_name_hashtable("install");
	}
	return 0;
}

static int check_deps(deb_file_t **deb_file, int deb_start /*, int dep_max_count - ?? */)
{
	int *conflicts = NULL;
	int conflicts_num = 0;
	int i = deb_start;
	int j;

	/* Check for conflicts
	 * TODO: TEST if conflicts with other packages to be installed
	 *
	 * Add install packages and the packages they provide
	 * to the list of files to check conflicts for
	 */

	/* Create array of package numbers to check against
	 * installed package for conflicts*/
	while (deb_file[i] != NULL) {
		const unsigned package_num = deb_file[i]->package;
		conflicts = xrealloc_vector(conflicts, 2, conflicts_num);
		conflicts[conflicts_num] = package_num;
		conflicts_num++;
		/* add provides to conflicts list */
		for (j = 0; j < package_hashtable[package_num]->num_of_edges; j++) {
			if (package_hashtable[package_num]->edge[j]->type == EDGE_PROVIDES) {
				const int conflicts_package_num = search_package_hashtable(
					package_hashtable[package_num]->edge[j]->name,
					package_hashtable[package_num]->edge[j]->version,
					package_hashtable[package_num]->edge[j]->operator);
				if (package_hashtable[conflicts_package_num] == NULL) {
					/* create a new package */
					common_node_t *new_node = xzalloc(sizeof(common_node_t));
					new_node->name = package_hashtable[package_num]->edge[j]->name;
					new_node->version = package_hashtable[package_num]->edge[j]->version;
					package_hashtable[conflicts_package_num] = new_node;
				}
				conflicts = xrealloc_vector(conflicts, 2, conflicts_num);
				conflicts[conflicts_num] = conflicts_package_num;
				conflicts_num++;
			}
		}
		i++;
	}

	/* Check conflicts */
	i = 0;
	while (deb_file[i] != NULL) {
		const common_node_t *package_node = package_hashtable[deb_file[i]->package];
		int status_num = 0;
		status_num = search_status_hashtable(name_hashtable[package_node->name]);

		if (get_status(status_num, 3) == search_name_hashtable("installed")) {
			i++;
			continue;
		}

		for (j = 0; j < package_node->num_of_edges; j++) {
			const edge_t *package_edge = package_node->edge[j];

			if (package_edge->type == EDGE_CONFLICTS) {
				const unsigned package_num =
					search_package_hashtable(package_edge->name,
								package_edge->version,
								package_edge->operator);
				int result = 0;
				if (package_hashtable[package_num] != NULL) {
					status_num = search_status_hashtable(name_hashtable[package_hashtable[package_num]->name]);

					if (get_status(status_num, 1) == search_name_hashtable("install")) {
						result = test_version(package_hashtable[deb_file[i]->package]->version,
							package_edge->version, package_edge->operator);
					}
				}

				if (result) {
					bb_error_msg_and_die("package %s conflicts with %s",
						name_hashtable[package_node->name],
						name_hashtable[package_edge->name]);
				}
			}
		}
		i++;
	}


	/* Check dependentcies */
	for (i = 0; i < PACKAGE_HASH_PRIME; i++) {
		int status_num = 0;
		int number_of_alternatives = 0;
		const edge_t * root_of_alternatives = NULL;
		const common_node_t *package_node = package_hashtable[i];

		/* If the package node does not exist then this
		 * package is a virtual one. In which case there are
		 * no dependencies to check.
		 */
		if (package_node == NULL) continue;

		status_num = search_status_hashtable(name_hashtable[package_node->name]);

		/* If there is no status then this package is a
		 * virtual one provided by something else. In which
		 * case there are no dependencies to check.
		 */
		if (status_hashtable[status_num] == NULL) continue;

		/* If we don't want this package installed then we may
		 * as well ignore it's dependencies.
		 */
		if (get_status(status_num, 1) != search_name_hashtable("install")) {
			continue;
		}

		/* This code is tested only for EDGE_DEPENDS, since I
		 * have no suitable pre-depends available. There is no
		 * reason that it shouldn't work though :-)
		 */
		for (j = 0; j < package_node->num_of_edges; j++) {
			const edge_t *package_edge = package_node->edge[j];
			unsigned package_num;

			if (package_edge->type == EDGE_OR_PRE_DEPENDS
			 || package_edge->type == EDGE_OR_DEPENDS
			) {
				/* start an EDGE_OR_ list */
				number_of_alternatives = package_edge->version;
				root_of_alternatives = package_edge;
				continue;
			}
			if (number_of_alternatives == 0) {  /* not in the middle of an EDGE_OR_ list */
				number_of_alternatives = 1;
				root_of_alternatives = NULL;
			}

			package_num = search_package_hashtable(package_edge->name, package_edge->version, package_edge->operator);

			if (package_edge->type == EDGE_PRE_DEPENDS
			 || package_edge->type == EDGE_DEPENDS
			) {
				int result=1;
				status_num = 0;

				/* If we are inside an alternative then check
				 * this edge is the right type.
				 *
				 * EDGE_DEPENDS == OR_DEPENDS -1
				 * EDGE_PRE_DEPENDS == OR_PRE_DEPENDS -1
				 */
				if (root_of_alternatives && package_edge->type != root_of_alternatives->type - 1)
					bb_error_msg_and_die("fatal error, package dependencies corrupt: %d != %d - 1",
							package_edge->type, root_of_alternatives->type);

				if (package_hashtable[package_num] != NULL)
					result = !package_satisfies_dependency(package_num, package_edge->type);

				if (result) { /* check for other package which provide what we are looking for */
					int provider = -1;

					while ((provider = search_for_provides(package_edge->name, provider)) > -1) {
						if (package_hashtable[provider] == NULL) {
							puts("Have a provider but no package information for it");
							continue;
						}
						result = !package_satisfies_dependency(provider, package_edge->type);

						if (result == 0)
							break;
					}
				}

				/* It must be already installed, or to be installed */
				number_of_alternatives--;
				if (result && number_of_alternatives == 0) {
					if (root_of_alternatives)
						bb_error_msg_and_die(
							"package %s %sdepends on %s, which %s",
							name_hashtable[package_node->name],
							package_edge->type == EDGE_PRE_DEPENDS ? "pre-" : "",
							name_hashtable[root_of_alternatives->name],
							"cannot be satisfied");
					bb_error_msg_and_die(
						"package %s %sdepends on %s, which %s",
						name_hashtable[package_node->name],
						package_edge->type == EDGE_PRE_DEPENDS ? "pre-" : "",
						name_hashtable[package_edge->name],
						describe_status(status_num));
				}
				if (result == 0 && number_of_alternatives) {
					/* we've found a package which
					 * satisfies the dependency,
					 * so skip over the rest of
					 * the alternatives.
					 */
					j += number_of_alternatives;
					number_of_alternatives = 0;
				}
			}
		}
	}
	free(conflicts);
	return TRUE;
}

static char **create_list(const char *filename)
{
	FILE *list_stream;
	char **file_list;
	char *line;
	int count;

	/* don't use [xw]fopen here, handle error ourself */
	list_stream = fopen_for_read(filename);
	if (list_stream == NULL) {
		return NULL;
	}

	file_list = NULL;
	count = 0;
	while ((line = xmalloc_fgetline(list_stream)) != NULL) {
		file_list = xrealloc_vector(file_list, 2, count);
		file_list[count++] = line;
		/*file_list[count] = NULL; - xrealloc_vector did it */
	}
	fclose(list_stream);

	return file_list;
}

/* maybe i should try and hook this into remove_file.c somehow */
static int remove_file_array(char **remove_names, char **exclude_names)
{
	struct stat path_stat;
	int remove_flag = 1; /* not removed anything yet */
	int i, j;

	if (remove_names == NULL) {
		return 0;
	}
	for (i = 0; remove_names[i] != NULL; i++) {
		if (exclude_names != NULL) {
			for (j = 0; exclude_names[j] != NULL; j++) {
				if (strcmp(remove_names[i], exclude_names[j]) == 0) {
					goto skip;
				}
			}
		}
		/* TODO: why we are checking lstat? we can just try rm/rmdir */
		if (lstat(remove_names[i], &path_stat) < 0) {
			continue;
		}
		if (S_ISDIR(path_stat.st_mode)) {
			remove_flag &= rmdir(remove_names[i]); /* 0 if no error */
		} else {
			remove_flag &= unlink(remove_names[i]); /* 0 if no error */
		}
 skip:
		continue;
	}
	return (remove_flag == 0);
}

static void run_package_script_or_die(const char *package_name, const char *script_type)
{
	char *script_path;
	int result;

	script_path = xasprintf("/var/lib/dpkg/info/%s.%s", package_name, script_type);

	/* If the file doesn't exist it isn't fatal */
	result = access(script_path, F_OK) ? EXIT_SUCCESS : system(script_path);
	free(script_path);
	if (result)
		bb_error_msg_and_die("%s failed, exit code %d", script_type, result);
}

/*
The policy manual defines what scripts get called when and with
what arguments. I realize that busybox does not support all of
these scenarios, but it does support some of them; it does not,
however, run them with any parameters in run_package_script_or_die().
Here are the scripts:

preinst install
preinst install <old_version>
preinst upgrade <old_version>
preinst abort_upgrade <new_version>
postinst configure <most_recent_version>
postinst abort-upgade <new_version>
postinst abort-remove
postinst abort-remove in-favour <package> <version>
postinst abort-deconfigure in-favor <failed_install_package> removing <conflicting_package> <version>
prerm remove
prerm upgrade <new_version>
prerm failed-upgrade <old_version>
prerm remove in-favor <package> <new_version>
prerm deconfigure in-favour <package> <version> removing <package> <version>
postrm remove
postrm purge
postrm upgrade <new_version>
postrm failed-upgrade <old_version>
postrm abort-install
postrm abort-install <old_version>
postrm abort-upgrade <old_version>
postrm disappear <overwriter> <version>
*/
static const char *const all_control_files[] = {
	"preinst", "postinst", "prerm", "postrm",
	"list", "md5sums", "shlibs", "conffiles",
	"config", "templates"
};

static char **all_control_list(const char *package_name)
{
	unsigned i = 0;
	char **remove_files;

	/* Create a list of all /var/lib/dpkg/info/<package> files */
	remove_files = xzalloc(sizeof(all_control_files) + sizeof(char*));
	while (i < ARRAY_SIZE(all_control_files)) {
		remove_files[i] = xasprintf("/var/lib/dpkg/info/%s.%s",
				package_name, all_control_files[i]);
		i++;
	}

	return remove_files;
}

static void free_array(char **array)
{
	if (array) {
		unsigned i = 0;
		while (array[i]) {
			free(array[i]);
			i++;
		}
		free(array);
	}
}

/* This function lists information on the installed packages. It loops through
 * the status_hashtable to retrieve the info. This results in smaller code than
 * scanning the status file. The resulting list, however, is unsorted.
 */
static void list_packages(const char *pattern)
{
	int i;

	puts("    Name           Version");
	puts("+++-==============-==============");

	/* go through status hash, dereference package hash and finally strings */
	for (i = 0; i < STATUS_HASH_PRIME+1; i++) {
		if (status_hashtable[i]) {
			const char *stat_str;  /* status string */
			const char *name_str;  /* package name */
			const char *vers_str;  /* version */
			char  s1, s2;          /* status abbreviations */
			int   spccnt;          /* space count */
			int   j;

			stat_str = name_hashtable[status_hashtable[i]->status];
			name_str = name_hashtable[package_hashtable[status_hashtable[i]->package]->name];
			vers_str = name_hashtable[package_hashtable[status_hashtable[i]->package]->version];

			if (pattern && fnmatch(pattern, name_str, 0) != 0)
				continue;

			/* get abbreviation for status field 1 */
			s1 = stat_str[0] == 'i' ? 'i' : 'r';

			/* get abbreviation for status field 2 */
			for (j = 0, spccnt = 0; stat_str[j] && spccnt < 2; j++) {
				if (stat_str[j] == ' ') spccnt++;
			}
			s2 = stat_str[j];

			/* print out the line formatted like Debian dpkg */
			printf("%c%c  %-14s %s\n", s1, s2, name_str, vers_str);
		}
	}
}

static void remove_package(const unsigned package_num, int noisy)
{
	const char *package_name = name_hashtable[package_hashtable[package_num]->name];
	const char *package_version = name_hashtable[package_hashtable[package_num]->version];
	const unsigned status_num = search_status_hashtable(package_name);
	const int package_name_length = strlen(package_name);
	char **remove_files;
	char **exclude_files;
	char list_name[package_name_length + 25];
	char conffile_name[package_name_length + 30];

	if (noisy)
		printf("Removing %s (%s)...\n", package_name, package_version);

	/* Run prerm script */
	run_package_script_or_die(package_name, "prerm");

	/* Create a list of files to remove, and a separate list of those to keep */
	sprintf(list_name, "/var/lib/dpkg/info/%s.%s", package_name, "list");
	remove_files = create_list(list_name);

	sprintf(conffile_name, "/var/lib/dpkg/info/%s.%s", package_name, "conffiles");
	exclude_files = create_list(conffile_name);

	/* Some directories can't be removed straight away, so do multiple passes */
	while (remove_file_array(remove_files, exclude_files))
		continue;
	free_array(exclude_files);
	free_array(remove_files);

	/* Create a list of files in /var/lib/dpkg/info/<package>.* to keep */
	exclude_files = xzalloc(sizeof(exclude_files[0]) * 3);
	exclude_files[0] = xstrdup(conffile_name);
	exclude_files[1] = xasprintf("/var/lib/dpkg/info/%s.%s", package_name, "postrm");

	/* Create a list of all /var/lib/dpkg/info/<package> files */
	remove_files = all_control_list(package_name);

	remove_file_array(remove_files, exclude_files);
	free_array(remove_files);
	free_array(exclude_files);

	/* rename <package>.conffiles to <package>.list
	 * The conffiles control file isn't required in Debian packages, so don't
	 * error out if it's missing.  */
	rename(conffile_name, list_name);

	/* Change package status */
	set_status(status_num, "config-files", 3);
}

static void purge_package(const unsigned package_num)
{
	const char *package_name = name_hashtable[package_hashtable[package_num]->name];
	const char *package_version = name_hashtable[package_hashtable[package_num]->version];
	const unsigned status_num = search_status_hashtable(package_name);
	char **remove_files;
	char **exclude_files;
	char list_name[strlen(package_name) + 25];

	printf("Purging %s (%s)...\n", package_name, package_version);

	/* Run prerm script */
	run_package_script_or_die(package_name, "prerm");

	/* Create a list of files to remove */
	sprintf(list_name, "/var/lib/dpkg/info/%s.%s", package_name, "list");
	remove_files = create_list(list_name);

	/* Some directories cant be removed straight away, so do multiple passes */
	while (remove_file_array(remove_files, NULL))
		continue;
	free_array(remove_files);

	/* Create a list of all /var/lib/dpkg/info/<package> files */
	remove_files = all_control_list(package_name);

	/* Delete all of them except the postrm script */
	exclude_files = xzalloc(sizeof(exclude_files[0]) * 2);
	exclude_files[0] = xasprintf("/var/lib/dpkg/info/%s.%s", package_name, "postrm");
	remove_file_array(remove_files, exclude_files);
	free_array(exclude_files);

	/* Run and remove postrm script */
	run_package_script_or_die(package_name, "postrm");
	remove_file_array(remove_files, NULL);

	free_array(remove_files);

	/* Change package status */
	set_status(status_num, "not-installed", 3);
}

static archive_handle_t *init_archive_deb_ar(const char *filename)
{
	archive_handle_t *ar_handle;

	/* Setup an ar archive handle that refers to the gzip sub archive */
	ar_handle = init_handle();
	ar_handle->filter = filter_accept_list_reassign;
	ar_handle->src_fd = xopen(filename, O_RDONLY);

	return ar_handle;
}

static void init_archive_deb_control(archive_handle_t *ar_handle)
{
	archive_handle_t *tar_handle;

	/* Setup the tar archive handle */
	tar_handle = init_handle();
	tar_handle->src_fd = ar_handle->src_fd;

	/* We don't care about data.tar.* or debian-binary, just control.tar.* */
	llist_add_to(&(ar_handle->accept), (char*)"control.tar");
#if ENABLE_FEATURE_SEAMLESS_GZ
	llist_add_to(&(ar_handle->accept), (char*)"control.tar.gz");
#endif
#if ENABLE_FEATURE_SEAMLESS_BZ2
	llist_add_to(&(ar_handle->accept), (char*)"control.tar.bz2");
#endif
#if ENABLE_FEATURE_SEAMLESS_XZ
	llist_add_to(&(ar_handle->accept), (char*)"control.tar.xz");
#endif

	/* Assign the tar handle as a subarchive of the ar handle */
	ar_handle->dpkg__sub_archive = tar_handle;
}

static void init_archive_deb_data(archive_handle_t *ar_handle)
{
	archive_handle_t *tar_handle;

	/* Setup the tar archive handle */
	tar_handle = init_handle();
	tar_handle->src_fd = ar_handle->src_fd;

	/* We don't care about control.tar.* or debian-binary, just data.tar.* */
	llist_add_to(&(ar_handle->accept), (char*)"data.tar");
#if ENABLE_FEATURE_SEAMLESS_GZ
	llist_add_to(&(ar_handle->accept), (char*)"data.tar.gz");
#endif
#if ENABLE_FEATURE_SEAMLESS_BZ2
	llist_add_to(&(ar_handle->accept), (char*)"data.tar.bz2");
#endif
#if ENABLE_FEATURE_SEAMLESS_LZMA
	llist_add_to(&(ar_handle->accept), (char*)"data.tar.lzma");
#endif
#if ENABLE_FEATURE_SEAMLESS_XZ
	llist_add_to(&(ar_handle->accept), (char*)"data.tar.xz");
#endif

	/* Assign the tar handle as a subarchive of the ar handle */
	ar_handle->dpkg__sub_archive = tar_handle;
}

static void FAST_FUNC data_extract_to_buffer(archive_handle_t *archive_handle)
{
	unsigned size = archive_handle->file_header->size;

	archive_handle->dpkg__buffer = xzalloc(size + 1);
	xread(archive_handle->src_fd, archive_handle->dpkg__buffer, size);
}

static char *deb_extract_control_file_to_buffer(archive_handle_t *ar_handle, llist_t *myaccept)
{
	ar_handle->dpkg__sub_archive->action_data = data_extract_to_buffer;
	ar_handle->dpkg__sub_archive->accept = myaccept;
	ar_handle->dpkg__sub_archive->filter = filter_accept_list;

	unpack_ar_archive(ar_handle);
	close(ar_handle->src_fd);

	return ar_handle->dpkg__sub_archive->dpkg__buffer;
}

static void append_control_file_to_llist(const char *package_name, const char *control_name, llist_t **ll)
{
	FILE *fp;
	char *filename, *line;

	filename = xasprintf("/var/lib/dpkg/info/%s.%s", package_name, control_name);
	fp = fopen_for_read(filename);
	free(filename);
	if (fp != NULL) {
		while ((line = xmalloc_fgetline(fp)) != NULL)
			llist_add_to(ll, line);
		fclose(fp);
	}
}

static char FAST_FUNC filter_rename_config(archive_handle_t *archive_handle)
{
	int fd;
	char *name_ptr = archive_handle->file_header->name + 1;

	/* Is this file marked as config file? */
	if (!find_list_entry(archive_handle->accept, name_ptr))
		return EXIT_SUCCESS; /* no */

	fd = open(name_ptr, O_RDONLY);
	if (fd >= 0) {
		md5_ctx_t md5;
		char *md5line, *buf;
		int count;

		/* Calculate MD5 of existing file */
		buf = xmalloc(4096);
		md5_begin(&md5);
		while ((count = safe_read(fd, buf, 4096)) > 0)
			md5_hash(&md5, buf, count);
		md5_end(&md5, buf); /* using buf as result storage */
		close(fd);

		md5line = xmalloc(16 * 2 + 2 + strlen(name_ptr) + 1);
		sprintf(bin2hex(md5line, buf, 16), "  %s", name_ptr);
		free(buf);

		/* Is it changed after install? */
		if (find_list_entry(archive_handle->accept, md5line) == NULL) {
			printf("Warning: Creating %s as %s.dpkg-new\n", name_ptr, name_ptr);
			archive_handle->file_header->name = xasprintf("%s.dpkg-new", archive_handle->file_header->name);
		}
		free(md5line);
	}
	return EXIT_SUCCESS;
}

static void FAST_FUNC data_extract_all_prefix(archive_handle_t *archive_handle)
{
	char *name_ptr = archive_handle->file_header->name;

	/* Skip all leading "/" */
	while (*name_ptr == '/')
		name_ptr++;
	/* Skip all leading "./" and "../" */
	while (name_ptr[0] == '.') {
		if (name_ptr[1] == '.')
			name_ptr++;
		if (name_ptr[1] != '/')
			break;
		name_ptr += 2;
	}

	if (name_ptr[0] != '\0') {
		archive_handle->file_header->name = xasprintf("%s%s", archive_handle->dpkg__buffer, name_ptr);
		data_extract_all(archive_handle);
		if (fnmatch("*.dpkg-new", archive_handle->file_header->name, 0) == 0) {
			/* remove .dpkg-new suffix */
			archive_handle->file_header->name[strlen(archive_handle->file_header->name) - 9] = '\0';
		}
	}
}

enum {
	/* Commands */
	OPT_configure            = (1 << 0),
	OPT_install              = (1 << 1),
	OPT_list_installed       = (1 << 2),
	OPT_purge                = (1 << 3),
	OPT_remove               = (1 << 4),
	OPT_unpack               = (1 << 5),
	OPTMASK_cmd              = (1 << 6) - 1,
	/* Options */
	OPT_force                = (1 << 6),
	OPT_force_ignore_depends = (1 << 7),
	OPT_force_confnew        = (1 << 8),
	OPT_force_confold        = (1 << 9),
};

static void unpack_package(deb_file_t *deb_file)
{
	const char *package_name = name_hashtable[package_hashtable[deb_file->package]->name];
	const unsigned status_num = search_status_hashtable(package_name);
	const unsigned status_package_num = status_hashtable[status_num]->package;
	char *info_prefix;
	char *list_filename;
	archive_handle_t *archive_handle;
	FILE *out_stream;
	llist_t *accept_list;
	llist_t *conffile_list;
	int i;

	/* If existing version, remove it first */
	conffile_list = NULL;
	if (strcmp(name_hashtable[get_status(status_num, 3)], "installed") == 0) {
		/* Package is already installed, remove old version first */
		printf("Preparing to replace %s %s (using %s)...\n", package_name,
			name_hashtable[package_hashtable[status_package_num]->version],
			deb_file->filename);

		/* Read md5sums from old package */
		if (!(option_mask32 & OPT_force_confold))
			append_control_file_to_llist(package_name, "md5sums", &conffile_list);

		remove_package(status_package_num, 0);
	} else {
		printf("Unpacking %s (from %s)...\n", package_name, deb_file->filename);
	}

	/* Extract control.tar.gz to /var/lib/dpkg/info/<package>.filename */
	info_prefix = xasprintf("/var/lib/dpkg/info/%s.%s", package_name, "");
	archive_handle = init_archive_deb_ar(deb_file->filename);
	init_archive_deb_control(archive_handle);

	accept_list = NULL;
	i = 0;
	while (i < ARRAY_SIZE(all_control_files)) {
		char *c = xasprintf("./%s", all_control_files[i]);
		llist_add_to(&accept_list, c);
		i++;
	}
	archive_handle->dpkg__sub_archive->accept = accept_list;
	archive_handle->dpkg__sub_archive->filter = filter_accept_list;
	archive_handle->dpkg__sub_archive->action_data = data_extract_all_prefix;
	archive_handle->dpkg__sub_archive->dpkg__buffer = info_prefix;
	archive_handle->dpkg__sub_archive->ah_flags |= ARCHIVE_UNLINK_OLD;
	unpack_ar_archive(archive_handle);

	/* Run the preinst prior to extracting */
	run_package_script_or_die(package_name, "preinst");

	/* Don't overwrite existing config files */
	if (!(option_mask32 & OPT_force_confnew))
		append_control_file_to_llist(package_name, "conffiles", &conffile_list);

	/* Extract data.tar.gz to the root directory */
	archive_handle = init_archive_deb_ar(deb_file->filename);
	init_archive_deb_data(archive_handle);
	archive_handle->dpkg__sub_archive->accept = conffile_list;
	/* Why ARCHIVE_REMEMBER_NAMES?
	 * We want names collected in ->passed list even if conffile_list
	 * is NULL (otherwise get_header_tar may optimize name saving out):
	 */
	archive_handle->dpkg__sub_archive->ah_flags |= ARCHIVE_REMEMBER_NAMES | ARCHIVE_UNLINK_OLD;
	archive_handle->dpkg__sub_archive->filter = filter_rename_config;
	archive_handle->dpkg__sub_archive->action_data = data_extract_all_prefix;
	archive_handle->dpkg__sub_archive->dpkg__buffer = (char*)"/"; /* huh? */
	unpack_ar_archive(archive_handle);

	/* Create the list file */
	list_filename = xasprintf("/var/lib/dpkg/info/%s.%s", package_name, "list");
	out_stream = xfopen_for_write(list_filename);
	archive_handle->dpkg__sub_archive->passed = llist_rev(archive_handle->dpkg__sub_archive->passed);
	while (archive_handle->dpkg__sub_archive->passed) {
		char *filename = llist_pop(&archive_handle->dpkg__sub_archive->passed);
		/* the leading . has been stripped by data_extract_all_prefix already */
		fprintf(out_stream, "%s\n", filename);
		free(filename);
	}
	fclose(out_stream);

	/* change status */
	set_status(status_num, "install", 1);
	set_status(status_num, "unpacked", 3);

	free(info_prefix);
	free(list_filename);
}

static void configure_package(deb_file_t *deb_file)
{
	const char *package_name = name_hashtable[package_hashtable[deb_file->package]->name];
	const char *package_version = name_hashtable[package_hashtable[deb_file->package]->version];
	const int status_num = search_status_hashtable(package_name);

	printf("Setting up %s (%s)...\n", package_name, package_version);

	/* Run the postinst script */
	/* TODO: handle failure gracefully */
	run_package_script_or_die(package_name, "postinst");

	/* Change status to reflect success */
	set_status(status_num, "install", 1);
	set_status(status_num, "installed", 3);
}

int dpkg_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int dpkg_main(int argc UNUSED_PARAM, char **argv)
{
	deb_file_t **deb_file = NULL;
	status_node_t *status_node;
	char *str_f;
	int opt;
	int package_num;
	int deb_count = 0;
	int state_status;
	int status_num;
	int i;
#if ENABLE_LONG_OPTS
	static const char dpkg_longopts[] ALIGN1 =
// FIXME: we use -C non-compatibly, should be:
// "-C|--audit Check for broken package(s)"
		"configure\0"      No_argument        "C"
		"force\0"          Required_argument  "F"
		"install\0"        No_argument        "i"
		"list\0"           No_argument        "l"
		"purge\0"          No_argument        "P"
		"remove\0"         No_argument        "r"
		"unpack\0"         No_argument        "u"
		"force-depends\0"  No_argument        "\xff"
		"force-confnew\0"  No_argument        "\xfe"
		"force-confold\0"  No_argument        "\xfd"
		;
#endif

	INIT_G();

	opt = getopt32long(argv, "CilPruF:", dpkg_longopts, &str_f);
	argv += optind;
	//if (opt & OPT_configure) ... // -C
	if (opt & OPT_force) { // -F (--force in official dpkg)
		if (strcmp(str_f, "depends") == 0)
			opt |= OPT_force_ignore_depends;
		else if (strcmp(str_f, "confnew") == 0)
			opt |= OPT_force_confnew;
		else if (strcmp(str_f, "confold") == 0)
			opt |= OPT_force_confold;
		else
			bb_show_usage();
		option_mask32 = opt;
	}
	//if (opt & OPT_install) ... // -i
	//if (opt & OPT_list_installed) ... // -l
	//if (opt & OPT_purge) ... // -P
	//if (opt & OPT_remove) ... // -r
	//if (opt & OPT_unpack) ... // -u (--unpack in official dpkg)
	if (!(opt & OPTMASK_cmd) /* no cmd */
	 || ((opt & OPTMASK_cmd) & ((opt & OPTMASK_cmd)-1)) /* more than one cmd */
	) {
		bb_show_usage();
	}

/*	puts("(Reading database ... xxxxx files and directories installed.)"); */
	index_status_file("/var/lib/dpkg/status");

	/* if the list action was given print the installed packages and exit */
	if (opt & OPT_list_installed) {
		list_packages(argv[0]); /* param can be NULL */
		return EXIT_SUCCESS;
	}

	/* Read arguments and store relevant info in structs */
	while (*argv) {
		/* deb_count = nb_elem - 1 and we need nb_elem + 1 to allocate terminal node [NULL pointer] */
		deb_file = xrealloc_vector(deb_file, 2, deb_count);
		deb_file[deb_count] = xzalloc(sizeof(deb_file[0][0]));
		if (opt & (OPT_install | OPT_unpack)) {
			/* -i/-u: require filename */
			archive_handle_t *archive_handle;
			llist_t *control_list = NULL;

			/* Extract the control file */
			llist_add_to(&control_list, (char*)"./control");
			archive_handle = init_archive_deb_ar(argv[0]);
			init_archive_deb_control(archive_handle);
			deb_file[deb_count]->control_file = deb_extract_control_file_to_buffer(archive_handle, control_list);
			if (deb_file[deb_count]->control_file == NULL) {
				bb_error_msg_and_die("can't extract control file");
			}
			deb_file[deb_count]->filename = xstrdup(argv[0]);
			package_num = fill_package_struct(deb_file[deb_count]->control_file);

			if (package_num == -1) {
				bb_error_msg("invalid control file in %s", argv[0]);
				argv++;
				continue;
			}
			deb_file[deb_count]->package = (unsigned) package_num;

			/* Add the package to the status hashtable */
			if (opt & (OPT_unpack | OPT_install)) {
				/* Try and find a currently installed version of this package */
				status_num = search_status_hashtable(name_hashtable[package_hashtable[deb_file[deb_count]->package]->name]);
				/* If no previous entry was found initialise a new entry */
				if (status_hashtable[status_num] == NULL
				 || status_hashtable[status_num]->status == 0
				) {
					status_node = xmalloc(sizeof(status_node_t));
					status_node->package = deb_file[deb_count]->package;
					/* reinstreq isn't changed to "ok" until the package control info
					 * is written to the status file*/
					status_node->status = search_name_hashtable("install reinstreq not-installed");
					status_hashtable[status_num] = status_node;
				} else {
					set_status(status_num, "install", 1);
					set_status(status_num, "reinstreq", 2);
				}
			}
		} else if (opt & (OPT_configure | OPT_purge | OPT_remove)) {
			/* -C/-p/-r: require package name */
			deb_file[deb_count]->package = search_package_hashtable(
					search_name_hashtable(argv[0]),
					search_name_hashtable("ANY"), VER_ANY);
			if (package_hashtable[deb_file[deb_count]->package] == NULL) {
				bb_error_msg_and_die("package %s is uninstalled or unknown", argv[0]);
			}
			package_num = deb_file[deb_count]->package;
			status_num = search_status_hashtable(name_hashtable[package_hashtable[package_num]->name]);
			state_status = get_status(status_num, 3);

			/* check package status is "installed" */
			if (opt & OPT_remove) {
				if (strcmp(name_hashtable[state_status], "not-installed") == 0
				 || strcmp(name_hashtable[state_status], "config-files") == 0
				) {
					bb_error_msg_and_die("%s is already removed", name_hashtable[package_hashtable[package_num]->name]);
				}
				set_status(status_num, "deinstall", 1);
			} else if (opt & OPT_purge) {
				/* if package status is "conf-files" then its ok */
				if (strcmp(name_hashtable[state_status], "not-installed") == 0) {
					bb_error_msg_and_die("%s is already purged", name_hashtable[package_hashtable[package_num]->name]);
				}
				set_status(status_num, "purge", 1);
			}
		}
		deb_count++;
		argv++;
	}
	if (!deb_count)
		bb_error_msg_and_die("no package files specified");
	deb_file[deb_count] = NULL;

	/* Check that the deb file arguments are installable */
	if (!(opt & OPT_force_ignore_depends)) {
		if (!check_deps(deb_file, 0 /*, deb_count*/)) {
			bb_error_msg_and_die("dependency check failed");
		}
	}

	/* TODO: install or remove packages in the correct dependency order */
	for (i = 0; i < deb_count; i++) {
		/* Remove or purge packages */
		if (opt & OPT_remove) {
			remove_package(deb_file[i]->package, 1);
		}
		else if (opt & OPT_purge) {
			purge_package(deb_file[i]->package);
		}
		else if (opt & OPT_unpack) {
			unpack_package(deb_file[i]);
		}
		else if (opt & OPT_install) {
			unpack_package(deb_file[i]);
			/* package is configured in second pass below */
		}
		else if (opt & OPT_configure) {
			configure_package(deb_file[i]);
		}
	}
	/* configure installed packages */
	if (opt & OPT_install) {
		for (i = 0; i < deb_count; i++)
			configure_package(deb_file[i]);
	}

	write_status_file(deb_file);

	if (ENABLE_FEATURE_CLEAN_UP) {
		for (i = 0; i < deb_count; i++) {
			free(deb_file[i]->control_file);
			free(deb_file[i]->filename);
			free(deb_file[i]);
		}

		free(deb_file);

		for (i = 0; i < NAME_HASH_PRIME; i++) {
			free(name_hashtable[i]);
		}

		for (i = 0; i < PACKAGE_HASH_PRIME; i++) {
			free_package(package_hashtable[i]);
		}

		for (i = 0; i < STATUS_HASH_PRIME; i++) {
			free(status_hashtable[i]);
		}
	}

	return EXIT_SUCCESS;
}
