/* vi: set sw=4 ts=4: */
/*
 * Applet table generator.
 * Runs on host and produces include/applet_tables.h
 *
 * Copyright (C) 2007 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>

#undef ARRAY_SIZE
#define ARRAY_SIZE(x) ((unsigned)(sizeof(x) / sizeof((x)[0])))

#include "../include/autoconf.h"
#include "../include/applet_metadata.h"

struct bb_applet {
	const char *name;
	const char *main;
	enum bb_install_loc_t install_loc;
	enum bb_suid_t need_suid;
	/* true if instead of fork(); exec("applet"); waitpid();
	 * one can do fork(); exit(applet_main(argc,argv)); waitpid(); */
	unsigned char noexec;
	/* Even nicer */
	/* true if instead of fork(); exec("applet"); waitpid();
	 * one can simply call applet_main(argc,argv); */
	unsigned char nofork;
};

/* Define struct bb_applet applets[] */
#include "../include/applets.h"

enum { NUM_APPLETS = ARRAY_SIZE(applets) };

static int cmp_name(const void *a, const void *b)
{
	const struct bb_applet *aa = a;
	const struct bb_applet *bb = b;
	return strcmp(aa->name, bb->name);
}

static int str_isalnum_(const char *s)
{
	while (*s) {
		if (!isalnum(*s) && *s != '_')
			return 0;
		s++;
	}
	return 1;
}

int main(int argc, char **argv)
{
	int i, j;
	char tmp1[PATH_MAX], tmp2[PATH_MAX];

	// In find_applet_by_name(), before linear search, narrow it down
	// by looking at N "equidistant" names. With ~350 applets:
	// KNOWN_APPNAME_OFFSETS  cycles
	//                     0    9057
	//                     2    4604 + ~100 bytes of code
	//                     4    2407 + 4 bytes
	//                     8    1342 + 8 bytes
	//                    16     908 + 16 bytes
	//                    32     884 + 32 bytes
	// With 8, int16_t applet_nameofs[] table has 7 elements.
	int KNOWN_APPNAME_OFFSETS = 8;
	// With 128 applets we do two linear searches, with 1..7 strcmp's in the first one
	// and 1..16 strcmp's in the second. With 256 apps, second search does 1..32 strcmp's.
	if (NUM_APPLETS < 128)
		KNOWN_APPNAME_OFFSETS = 4;
	if (NUM_APPLETS < 32)
		KNOWN_APPNAME_OFFSETS = 0;

	qsort(applets, NUM_APPLETS, sizeof(applets[0]), cmp_name);

	if (!argv[1])
		return 1;
	snprintf(tmp1, PATH_MAX, "%s.%u.new", argv[1], (int) getpid());
	i = open(tmp1, O_WRONLY | O_TRUNC | O_CREAT, 0666);
	if (i < 0)
		return 1;
	dup2(i, 1);

	/* Keep in sync with include/busybox.h! */

	printf("/* This is a generated file, don't edit */\n\n");

	printf("#define NUM_APPLETS %u\n", NUM_APPLETS);
	if (NUM_APPLETS == 1) {
		printf("#define SINGLE_APPLET_STR \"%s\"\n", applets[0].name);
		printf("#define SINGLE_APPLET_MAIN %s_main\n", applets[0].main);
	}

	printf("#define KNOWN_APPNAME_OFFSETS %u\n\n", KNOWN_APPNAME_OFFSETS);
	if (KNOWN_APPNAME_OFFSETS > 0) {
		int ofs, offset[KNOWN_APPNAME_OFFSETS], index[KNOWN_APPNAME_OFFSETS];
		for (i = 0; i < KNOWN_APPNAME_OFFSETS; i++)
			index[i] = i * NUM_APPLETS / KNOWN_APPNAME_OFFSETS;
		ofs = 0;
		for (i = 0; i < NUM_APPLETS; i++) {
			for (j = 0; j < KNOWN_APPNAME_OFFSETS; j++)
				if (i == index[j])
					offset[j] = ofs;
			ofs += strlen(applets[i].name) + 1;
		}
		/* If the list of names is too long refuse to proceed */
		if (ofs > 0xffff)
			return 1;
		printf("const uint16_t applet_nameofs[] ALIGN2 = {\n");
		for (i = 1; i < KNOWN_APPNAME_OFFSETS; i++)
			printf("%d,\n", offset[i]);
		printf("};\n\n");
	}

	//printf("#ifndef SKIP_definitions\n");
	printf("const char applet_names[] ALIGN1 = \"\"\n");
	for (i = 0; i < NUM_APPLETS; i++) {
		printf("\"%s\" \"\\0\"\n", applets[i].name);
//		if (MAX_APPLET_NAME_LEN < strlen(applets[i].name))
//			MAX_APPLET_NAME_LEN = strlen(applets[i].name);
	}
	printf(";\n\n");

	for (i = 0; i < NUM_APPLETS; i++) {
		if (str_isalnum_(applets[i].name))
			printf("#define APPLET_NO_%s %d\n", applets[i].name, i);
	}
	printf("\n");

	printf("#ifndef SKIP_applet_main\n");
	printf("int (*const applet_main[])(int argc, char **argv) = {\n");
	for (i = 0; i < NUM_APPLETS; i++) {
		printf("%s_main,\n", applets[i].main);
	}
	printf("};\n");
	printf("#endif\n\n");

#if ENABLE_FEATURE_PREFER_APPLETS \
 || ENABLE_FEATURE_SH_STANDALONE \
 || ENABLE_FEATURE_SH_NOFORK
	printf("const uint8_t applet_flags[] ALIGN1 = {\n");
	i = 0;
	while (i < NUM_APPLETS) {
		int v = applets[i].nofork + (applets[i].noexec << 1);
		if (++i < NUM_APPLETS)
			v |= (applets[i].nofork + (applets[i].noexec << 1)) << 2;
		if (++i < NUM_APPLETS)
			v |= (applets[i].nofork + (applets[i].noexec << 1)) << 4;
		if (++i < NUM_APPLETS)
			v |= (applets[i].nofork + (applets[i].noexec << 1)) << 6;
		printf("0x%02x,\n", v);
		i++;
	}
	printf("};\n\n");
#endif

#if ENABLE_FEATURE_SUID
	printf("const uint8_t applet_suid[] ALIGN1 = {\n");
	i = 0;
	while (i < NUM_APPLETS) {
		int v = applets[i].need_suid; /* 2 bits */
		if (++i < NUM_APPLETS)
			v |= applets[i].need_suid << 2;
		if (++i < NUM_APPLETS)
			v |= applets[i].need_suid << 4;
		if (++i < NUM_APPLETS)
			v |= applets[i].need_suid << 6;
		printf("0x%02x,\n", v);
		i++;
	}
	printf("};\n\n");
#endif

#if ENABLE_FEATURE_INSTALLER
	printf("const uint8_t applet_install_loc[] ALIGN1 = {\n");
	i = 0;
	while (i < NUM_APPLETS) {
		int v = applets[i].install_loc; /* 3 bits */
		if (++i < NUM_APPLETS)
			v |= applets[i].install_loc << 4; /* 3 bits */
		printf("0x%02x,\n", v);
		i++;
	}
	printf("};\n");
#endif
	//printf("#endif /* SKIP_definitions */\n");

//	printf("\n");
//	printf("#define MAX_APPLET_NAME_LEN %u\n", MAX_APPLET_NAME_LEN);

	if (argv[2]) {
		FILE *fp;
		char line_new[80];
//		char line_old[80];

		sprintf(line_new, "#define NUM_APPLETS %u\n", NUM_APPLETS);
//		line_old[0] = 0;
//		fp = fopen(argv[2], "r");
//		if (fp) {
//			fgets(line_old, sizeof(line_old), fp);
//			fclose(fp);
//		}
//		if (strcmp(line_old, line_new) != 0) {
			snprintf(tmp2, PATH_MAX, "%s.%u.new", argv[2], (int) getpid());
			fp = fopen(tmp2, "w");
			if (!fp)
				return 1;
			fputs(line_new, fp);
			if (fclose(fp))
				return 1;
//		}
	}

	if (fclose(stdout))
		return 1;
	if (rename(tmp1, argv[1]))
		return 1;
	if (rename(tmp2, argv[2]))
		return 1;
	return 0;
}
