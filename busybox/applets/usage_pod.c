/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2009 Denys Vlasenko.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "autoconf.h"

#define SKIP_applet_main
#define ALIGN1 /* nothing, just to placate applet_tables.h */
#define ALIGN2 /* nothing, just to placate applet_tables.h */
#include "applet_tables.h"

/* Since we can't use platform.h, have to do this again by hand: */
#if ENABLE_NOMMU
# define BB_MMU 0
# define USE_FOR_NOMMU(...) __VA_ARGS__
# define USE_FOR_MMU(...)
#else
# define BB_MMU 1
# define USE_FOR_NOMMU(...)
# define USE_FOR_MMU(...) __VA_ARGS__
#endif

#include "usage.h"
#define MAKE_USAGE(aname, usage) { aname, usage },
static struct usage_data {
	const char *aname;
	const char *usage;
} usage_array[] = {
#include "applets.h"
};

static int compare_func(const void *a, const void *b)
{
	const struct usage_data *ua = a;
	const struct usage_data *ub = b;
	return strcmp(ua->aname, ub->aname);
}

int main(void)
{
	int col, len2;

	int i;
	int num_messages = sizeof(usage_array) / sizeof(usage_array[0]);

	if (num_messages == 0)
		return 0;

	qsort(usage_array,
		num_messages, sizeof(usage_array[0]),
		compare_func);

	col = 0;
	for (i = 0; i < num_messages; i++) {
		len2 = strlen(usage_array[i].aname) + 2;
		if (col >= 76 - len2) {
			printf(",\n");
			col = 0;
		}
		if (col == 0) {
			col = 6;
			printf("\t");
		} else {
			printf(", ");
		}
		printf(usage_array[i].aname);
		col += len2;
	}
	printf("\n\n");

	printf("=head1 COMMAND DESCRIPTIONS\n\n");
	printf("=over 4\n\n");

	for (i = 0; i < num_messages; i++) {
		if (usage_array[i].aname[0] >= 'a' && usage_array[i].aname[0] <= 'z'
		 && usage_array[i].usage[0] != NOUSAGE_STR[0]
		) {
			printf("=item B<%s>\n\n", usage_array[i].aname);
			if (usage_array[i].usage[0])
				printf("%s %s\n\n", usage_array[i].aname, usage_array[i].usage);
			else
				printf("%s\n\n", usage_array[i].aname);
		}
	}
	printf("=back\n\n");

	return 0;
}

/* TODO: we used to make options bold with B<> and output an example too:

=item B<cat>

cat [B<-u>] [FILE]...

Concatenate FILE(s) and print them to stdout

Options:
        -u      Use unbuffered i/o (ignored)

Example:
        $ cat /proc/uptime
        110716.72 17.67

*/
