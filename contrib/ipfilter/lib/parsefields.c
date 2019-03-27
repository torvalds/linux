#include "ipf.h"
#include <err.h>

extern int nohdrfields;

wordtab_t *parsefields(table, arg)
	wordtab_t *table;
	char *arg;
{
	wordtab_t *f, *fields;
	char *s, *t;
	int num;

	fields = NULL;
	num = 0;

	for (s = strtok(arg, ","); s != NULL; s = strtok(NULL, ",")) {
		t = strchr(s, '=');
		if (t != NULL) {
			*t++ = '\0';
			if (*t == '\0')
				nohdrfields = 1;
		}

		f = findword(table, s);
		if (f == NULL) {
			fprintf(stderr, "Unknown field '%s'\n", s);
			exit(1);
		}

		num++;
		if (fields == NULL) {
			fields = malloc(2 * sizeof(*fields));
		} else {
			fields = reallocarray(fields, num + 1, sizeof(*fields));
			if (fields == NULL) {
				warnx("memory allocation error at %d in %s in %s", __LINE__, __FUNCTION__, __FILE__);
				abort();
			}
		}

		if (t == NULL) {
			fields[num - 1].w_word = f->w_word;
		} else {
			fields[num - 1].w_word = t;
		}
		fields[num - 1].w_value = f->w_value;
		fields[num].w_word = NULL;
		fields[num].w_value = 0;
	}

	return fields;
}
