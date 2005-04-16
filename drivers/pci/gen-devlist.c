/*
 *	Generate devlist.h and classlist.h from the PCI ID file.
 *
 *	(c) 1999--2002 Martin Mares <mj@ucw.cz>
 */

#include <stdio.h>
#include <string.h>

#define MAX_NAME_SIZE 200

static void
pq(FILE *f, const char *c, int len)
{
	int i = 1;
	while (*c && i != len) {
		if (*c == '"')
			fprintf(f, "\\\"");
		else {
			fputc(*c, f);
			if (*c == '?' && c[1] == '?') {
				/* Avoid trigraphs */
				fprintf(f, "\" \"");
			}
		}
		c++;
		i++;
	}
}

int
main(void)
{
	char line[1024], *c, *bra, vend[8];
	int vendors = 0;
	int mode = 0;
	int lino = 0;
	int vendor_len = 0;
	FILE *devf, *clsf;

	devf = fopen("devlist.h", "w");
	clsf = fopen("classlist.h", "w");
	if (!devf || !clsf) {
		fprintf(stderr, "Cannot create output file!\n");
		return 1;
	}

	while (fgets(line, sizeof(line)-1, stdin)) {
		lino++;
		if ((c = strchr(line, '\n')))
			*c = 0;
		if (!line[0] || line[0] == '#')
			continue;
		if (line[1] == ' ') {
			if (line[0] == 'C' && strlen(line) > 4 && line[4] == ' ') {
				vend[0] = line[2];
				vend[1] = line[3];
				vend[2] = 0;
				mode = 2;
			} else goto err;
		}
		else if (line[0] == '\t') {
			if (line[1] == '\t')
				continue;
			switch (mode) {
			case 1:
				if (strlen(line) > 5 && line[5] == ' ') {
					c = line + 5;
					while (*c == ' ')
						*c++ = 0;
					if (vendor_len + strlen(c) + 1 > MAX_NAME_SIZE) {
						/* Too long, try cutting off long description */
						bra = strchr(c, '[');
						if (bra && bra > c && bra[-1] == ' ')
							bra[-1] = 0;
						if (vendor_len + strlen(c) + 1 > MAX_NAME_SIZE) {
							fprintf(stderr, "Line %d: Device name too long. Name truncated.\n", lino);
							fprintf(stderr, "%s\n", c);
							/*return 1;*/
						}
					}
					fprintf(devf, "\tDEVICE(%s,%s,\"", vend, line+1);
					pq(devf, c, MAX_NAME_SIZE - vendor_len - 1);
					fputs("\")\n", devf);
				} else goto err;
				break;
			case 2:
				if (strlen(line) > 3 && line[3] == ' ') {
					c = line + 3;
					while (*c == ' ')
						*c++ = 0;
					fprintf(clsf, "CLASS(%s%s, \"%s\")\n", vend, line+1, c);
				} else goto err;
				break;
			default:
				goto err;
			}
		} else if (strlen(line) > 4 && line[4] == ' ') {
			c = line + 4;
			while (*c == ' ')
				*c++ = 0;
			if (vendors)
				fputs("ENDVENDOR()\n\n", devf);
			vendors++;
			strcpy(vend, line);
			vendor_len = strlen(c);
			if (vendor_len + 24 > MAX_NAME_SIZE) {
				fprintf(stderr, "Line %d: Vendor name too long\n", lino);
				return 1;
			}
			fprintf(devf, "VENDOR(%s,\"", vend);
			pq(devf, c, 0);
			fputs("\")\n", devf);
			mode = 1;
		} else {
		err:
			fprintf(stderr, "Line %d: Syntax error in mode %d: %s\n", lino, mode, line);
			return 1;
		}
	}
	fputs("ENDVENDOR()\n\
\n\
#undef VENDOR\n\
#undef DEVICE\n\
#undef ENDVENDOR\n", devf);
	fputs("\n#undef CLASS\n", clsf);

	fclose(devf);
	fclose(clsf);

	return 0;
}
