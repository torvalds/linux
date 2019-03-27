/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include <ctype.h>

#include "ipf.h"

typedef	struct	variable	{
	struct	variable	*v_next;
	char	*v_name;
	char	*v_value;
} variable_t;

static	variable_t	*vtop = NULL;

static variable_t *find_var __P((char *));
static char *expand_string __P((char *, int));


static variable_t *find_var(name)
	char *name;
{
	variable_t *v;

	for (v = vtop; v != NULL; v = v->v_next)
		if (!strcmp(name, v->v_name))
			return v;
	return NULL;
}


char *get_variable(string, after, line)
	char *string, **after;
	int line;
{
	char c, *s, *t, *value;
	variable_t *v;

	s = string;

	if (*s == '{') {
		s++;
		for (t = s; *t != '\0'; t++)
			if (*t == '}')
				break;
		if (*t == '\0') {
			fprintf(stderr, "%d: { without }\n", line);
			return NULL;
		}
	} else if (ISALPHA(*s)) {
		for (t = s + 1; *t != '\0'; t++)
			if (!ISALPHA(*t) && !ISDIGIT(*t) && (*t != '_'))
				break;
	} else {
		fprintf(stderr, "%d: variables cannot start with '%c'\n",
			line, *s);
		return NULL;
	}

	if (after != NULL)
		*after = t;
	c = *t;
	*t = '\0';
	v = find_var(s);
	*t = c;
	if (v == NULL) {
		fprintf(stderr, "%d: unknown variable '%s'\n", line, s);
		return NULL;
	}

	s = strdup(v->v_value);
	value = expand_string(s, line);
	if (value != s)
		free(s);
	return value;
}


static char *expand_string(oldstring, line)
	char *oldstring;
	int line;
{
	char c, *s, *p1, *p2, *p3, *newstring, *value;
	int len;

	p3 = NULL;
	newstring = oldstring;

	for (s = oldstring; *s != '\0'; s++)
		if (*s == '$') {
			*s = '\0';
			s++;

			switch (*s)
			{
			case '$' :
				bcopy(s, s - 1, strlen(s));
				break;
			default :
				c = *s;
				if (c == '\0')
					return newstring;

				value = get_variable(s, &p3, line);
				if (value == NULL)
					return NULL;

				p2 = expand_string(value, line);
				if (p2 == NULL)
					return NULL;

				len = strlen(newstring) + strlen(p2);
				if (p3 != NULL) {
					if (c == '{' && *p3 == '}')
						p3++;
					len += strlen(p3);
				}
				p1 = malloc(len + 1);
				if (p1 == NULL)
					return NULL;

				*(s - 1) = '\0';
				strcpy(p1, newstring);
				strcat(p1, p2);
				if (p3 != NULL)
					strcat(p1, p3);

				s = p1 + len - strlen(p3) - 1;
				if (newstring != oldstring)
					free(newstring);
				newstring = p1;
				break;
			}
		}
	return newstring;
}


void set_variable(name, value)
	char *name;
	char *value;
{
	variable_t *v;
	int len;

	if (name == NULL || value == NULL || *name == '\0')
		return;

	v = find_var(name);
	if (v != NULL) {
		free(v->v_value);
		v->v_value = strdup(value);
		return;
	}

	len = strlen(value);

	if ((*value == '"' && value[len - 1] == '"') ||
	    (*value == '\'' && value[len - 1] == '\'')) {
		value[len - 1] = '\0';
		value++;
		len -=2;
	}

	v = (variable_t *)malloc(sizeof(*v));
	if (v == NULL)
		return;
	v->v_name = strdup(name);
	v->v_value = strdup(value);
	v->v_next = vtop;
	vtop = v;
}
