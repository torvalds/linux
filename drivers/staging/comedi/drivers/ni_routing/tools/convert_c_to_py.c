// SPDX-License-Identifier: GPL-2.0+
/* vim: set ts=8 sw=8 noet tw=80 nowrap: */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int8_t  s8;
#define __user
#define BIT(x)  (1UL << (x))

#define NI_ROUTE_VALUE_EXTERNAL_CONVERSION 1

#include "../ni_route_values.c"
#include "../ni_device_routes.c"
#include "all_cfiles.c"

#include <stdio.h>

#define RVij(rv, src, dest)	((rv)->register_values[(dest)][(src)])

/*
 * write out
 * {
 *   "family" : "<family-name>",
 *   "register_values": {
 *      <destination0>:[src0, src1, ...],
 *      <destination0>:[src0, src1, ...],
 *      ...
 *   }
 * }
 */
void family_write(const struct family_route_values *rv, FILE *fp)
{
	fprintf(fp,
		"  \"%s\" : {\n"
		"    # dest -> {src0:val0, src1:val1, ...}\n"
		, rv->family);
	for (unsigned int dest = NI_NAMES_BASE;
	     dest < (NI_NAMES_BASE + NI_NUM_NAMES);
	     ++dest) {
		unsigned int src = NI_NAMES_BASE;

		for (; src < (NI_NAMES_BASE + NI_NUM_NAMES) &&
		     RVij(rv, B(src), B(dest)) == 0; ++src)
			;

		if (src >= (NI_NAMES_BASE + NI_NUM_NAMES))
			continue; /* no data here */

		fprintf(fp, "    %u : {\n", dest);
		for (src = NI_NAMES_BASE; src < (NI_NAMES_BASE + NI_NUM_NAMES);
		     ++src) {
			register_type r = RVij(rv, B(src), B(dest));
			const char *M;

			if (r == 0) {
				continue;
			} else if (MARKED_V(r)) {
				M = "V";
			} else if (MARKED_I(r)) {
				M = "I";
			} else if (MARKED_U(r)) {
				M = "U";
			} else {
				fprintf(stderr,
					"Invalid register marking %s[%u][%u] = %u\n",
					rv->family, dest, src, r);
				exit(1);
			}

			fprintf(fp, "      %u : \"%s(%u)\",\n",
				src, M, UNMARK(r));
		}
		fprintf(fp, "    },\n");
	}
	fprintf(fp, "  },\n\n");
}

bool is_valid_ni_sig(unsigned int sig)
{
	return (sig >= NI_NAMES_BASE) && (sig < (NI_NAMES_BASE + NI_NUM_NAMES));
}

/*
 * write out
 * {
 *   "family" : "<family-name>",
 *   "register_values": {
 *      <destination0>:[src0, src1, ...],
 *      <destination0>:[src0, src1, ...],
 *      ...
 *   }
 * }
 */
void device_write(const struct ni_device_routes *dR, FILE *fp)
{
	fprintf(fp,
		"  \"%s\" : {\n"
		"    # dest -> [src0, src1, ...]\n"
		, dR->device);

	unsigned int i = 0;

	while (dR->routes[i].dest != 0) {
		if (!is_valid_ni_sig(dR->routes[i].dest)) {
			fprintf(stderr,
				"Invalid NI signal value [%u] for destination %s.[%u]\n",
				dR->routes[i].dest, dR->device, i);
			exit(1);
		}

		fprintf(fp, "    %u : [", dR->routes[i].dest);

		unsigned int j = 0;

		while (dR->routes[i].src[j] != 0) {
			if (!is_valid_ni_sig(dR->routes[i].src[j])) {
				fprintf(stderr,
					"Invalid NI signal value [%u] for source %s.[%u].[%u]\n",
					dR->routes[i].src[j], dR->device, i, j);
				exit(1);
			}

			fprintf(fp, "%u,", dR->routes[i].src[j]);

			++j;
		}
		fprintf(fp, "],\n");

		++i;
	}
	fprintf(fp, "  },\n\n");
}

int main(void)
{
	FILE *fp = fopen("ni_values.py", "w");

	/* write route register values */
	fprintf(fp, "ni_route_values = {\n");
	for (int i = 0; ni_all_route_values[i]; ++i)
		family_write(ni_all_route_values[i], fp);
	fprintf(fp, "}\n\n");

	/* write valid device routes */
	fprintf(fp, "ni_device_routes = {\n");
	for (int i = 0; ni_device_routes_list[i]; ++i)
		device_write(ni_device_routes_list[i], fp);
	fprintf(fp, "}\n");

	/* finish; close file */
	fclose(fp);
	return 0;
}
