/*****************************************************************************/
/*
 *      names.c  --  USB name database manipulation routines
 *
 *      Copyright (C) 1999, 2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

/*
 * 	Copyright (C) 2005 Takahiro Hirofuchi
 * 		- names_deinit() is added.
 */

/*****************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>


#include "names.h"


/* ---------------------------------------------------------------------- */

struct vendor {
	struct vendor *next;
	u_int16_t vendorid;
	char name[1];
};

struct product {
	struct product *next;
	u_int16_t vendorid, productid;
	char name[1];
};

struct class {
	struct class *next;
	u_int8_t classid;
	char name[1];
};

struct subclass {
	struct subclass *next;
	u_int8_t classid, subclassid;
	char name[1];
};

struct protocol {
	struct protocol *next;
	u_int8_t classid, subclassid, protocolid;
	char name[1];
};

struct audioterminal {
	struct audioterminal *next;
	u_int16_t termt;
	char name[1];
};

struct genericstrtable {
        struct genericstrtable *next;
        unsigned int num;
        char name[1];
};

/* ---------------------------------------------------------------------- */

#define HASH1  0x10
#define HASH2  0x02
#define HASHSZ 16

static unsigned int hashnum(unsigned int num)
{
	unsigned int mask1 = HASH1 << 27, mask2 = HASH2 << 27;

	for (; mask1 >= HASH1; mask1 >>= 1, mask2 >>= 1)
		if (num & mask1)
			num ^= mask2;
	return num & (HASHSZ-1);
}

/* ---------------------------------------------------------------------- */

static struct vendor *vendors[HASHSZ] = { NULL, };
static struct product *products[HASHSZ] = { NULL, };
static struct class *classes[HASHSZ] = { NULL, };
static struct subclass *subclasses[HASHSZ] = { NULL, };
static struct protocol *protocols[HASHSZ] = { NULL, };
static struct audioterminal *audioterminals[HASHSZ] = { NULL, };
static struct genericstrtable *hiddescriptors[HASHSZ] = { NULL, };
static struct genericstrtable *reports[HASHSZ] = { NULL, };
static struct genericstrtable *huts[HASHSZ] = { NULL, };
static struct genericstrtable *biass[HASHSZ] = { NULL, };
static struct genericstrtable *physdess[HASHSZ] = { NULL, };
static struct genericstrtable *hutus[HASHSZ] = { NULL, };
static struct genericstrtable *langids[HASHSZ] = { NULL, };
static struct genericstrtable *countrycodes[HASHSZ] = { NULL, };

/* ---------------------------------------------------------------------- */

static const char *names_genericstrtable(struct genericstrtable *t[HASHSZ], unsigned int index)
{
        struct genericstrtable *h;

        for (h = t[hashnum(index)]; h; h = h->next)
                if (h->num == index)
                        return h->name;
        return NULL;
}

const char *names_hid(u_int8_t hidd)
{
	return names_genericstrtable(hiddescriptors, hidd);
}

const char *names_reporttag(u_int8_t rt)
{
	return names_genericstrtable(reports, rt);
}

const char *names_huts(unsigned int data)
{
	return names_genericstrtable(huts, data);
}

const char *names_hutus(unsigned int data)
{
	return names_genericstrtable(hutus, data);
}

const char *names_langid(u_int16_t langid)
{
	return names_genericstrtable(langids, langid);
}

const char *names_physdes(u_int8_t ph)
{
	return names_genericstrtable(physdess, ph);
}

const char *names_bias(u_int8_t b)
{
	return names_genericstrtable(biass, b);
}

const char *names_countrycode(unsigned int countrycode)
{
	return names_genericstrtable(countrycodes, countrycode);
}

const char *names_vendor(u_int16_t vendorid)
{
	struct vendor *v;

	v = vendors[hashnum(vendorid)];
	for (; v; v = v->next)
		if (v->vendorid == vendorid)
			return v->name;
	return NULL;
}

const char *names_product(u_int16_t vendorid, u_int16_t productid)
{
	struct product *p;

	p = products[hashnum((vendorid << 16) | productid)];
	for (; p; p = p->next)
		if (p->vendorid == vendorid && p->productid == productid)
			return p->name;
	return NULL;
}

const char *names_class(u_int8_t classid)
{
	struct class *c;

	c = classes[hashnum(classid)];
	for (; c; c = c->next)
		if (c->classid == classid)
			return c->name;
	return NULL;
}

const char *names_subclass(u_int8_t classid, u_int8_t subclassid)
{
	struct subclass *s;

	s = subclasses[hashnum((classid << 8) | subclassid)];
	for (; s; s = s->next)
		if (s->classid == classid && s->subclassid == subclassid)
			return s->name;
	return NULL;
}

const char *names_protocol(u_int8_t classid, u_int8_t subclassid, u_int8_t protocolid)
{
	struct protocol *p;

	p = protocols[hashnum((classid << 16) | (subclassid << 8) | protocolid)];
	for (; p; p = p->next)
		if (p->classid == classid && p->subclassid == subclassid && p->protocolid == protocolid)
			return p->name;
	return NULL;
}

const char *names_audioterminal(u_int16_t termt)
{
	struct audioterminal *at;

	at = audioterminals[hashnum(termt)];
	for (; at; at = at->next)
		if (at->termt == termt)
			return at->name;
	return NULL;
}

/* ---------------------------------------------------------------------- */
/* add a cleanup function by takahiro */

struct pool {
	struct pool *next;
	void *mem;
};

static struct pool *pool_head = NULL;

static void *my_malloc(size_t size)
{
	struct pool *p;

	p = calloc(1, sizeof(struct pool));
	if (!p) {
		free(p);
		return NULL;
	}

	p->mem = calloc(1, size);
	if (!p->mem)
		return NULL;

	p->next = pool_head;
	pool_head = p;

	return p->mem;
}

void names_free(void)
{
	struct pool *pool;

	if (!pool_head)
		return;

	for (pool = pool_head; pool != NULL; ) {
		struct pool *tmp;

		if (pool->mem)
			free(pool->mem);

		tmp = pool;
		pool = pool->next;
		free(tmp);
	}
}

/* ---------------------------------------------------------------------- */

static int new_vendor(const char *name, u_int16_t vendorid)
{
	struct vendor *v;
	unsigned int h = hashnum(vendorid);

	v = vendors[h];
	for (; v; v = v->next)
		if (v->vendorid == vendorid)
			return -1;
	v = my_malloc(sizeof(struct vendor) + strlen(name));
	if (!v)
		return -1;
	strcpy(v->name, name);
	v->vendorid = vendorid;
	v->next = vendors[h];
	vendors[h] = v;
	return 0;
}

static int new_product(const char *name, u_int16_t vendorid, u_int16_t productid)
{
	struct product *p;
	unsigned int h = hashnum((vendorid << 16) | productid);

	p = products[h];
	for (; p; p = p->next)
		if (p->vendorid == vendorid && p->productid == productid)
			return -1;
	p = my_malloc(sizeof(struct product) + strlen(name));
	if (!p)
		return -1;
	strcpy(p->name, name);
	p->vendorid = vendorid;
	p->productid = productid;
	p->next = products[h];
	products[h] = p;
	return 0;
}

static int new_class(const char *name, u_int8_t classid)
{
	struct class *c;
	unsigned int h = hashnum(classid);

	c = classes[h];
	for (; c; c = c->next)
		if (c->classid == classid)
			return -1;
	c = my_malloc(sizeof(struct class) + strlen(name));
	if (!c)
		return -1;
	strcpy(c->name, name);
	c->classid = classid;
	c->next = classes[h];
	classes[h] = c;
	return 0;
}

static int new_subclass(const char *name, u_int8_t classid, u_int8_t subclassid)
{
	struct subclass *s;
	unsigned int h = hashnum((classid << 8) | subclassid);

	s = subclasses[h];
	for (; s; s = s->next)
		if (s->classid == classid && s->subclassid == subclassid)
			return -1;
	s = my_malloc(sizeof(struct subclass) + strlen(name));
	if (!s)
		return -1;
	strcpy(s->name, name);
	s->classid = classid;
	s->subclassid = subclassid;
	s->next = subclasses[h];
	subclasses[h] = s;
	return 0;
}

static int new_protocol(const char *name, u_int8_t classid, u_int8_t subclassid, u_int8_t protocolid)
{
	struct protocol *p;
	unsigned int h = hashnum((classid << 16) | (subclassid << 8) | protocolid);

	p = protocols[h];
	for (; p; p = p->next)
		if (p->classid == classid && p->subclassid == subclassid && p->protocolid == protocolid)
			return -1;
	p = my_malloc(sizeof(struct protocol) + strlen(name));
	if (!p)
		return -1;
	strcpy(p->name, name);
	p->classid = classid;
	p->subclassid = subclassid;
	p->protocolid = protocolid;
	p->next = protocols[h];
	protocols[h] = p;
	return 0;
}

static int new_audioterminal(const char *name, u_int16_t termt)
{
	struct audioterminal *at;
	unsigned int h = hashnum(termt);

	at = audioterminals[h];
	for (; at; at = at->next)
		if (at->termt == termt)
			return -1;
	at = my_malloc(sizeof(struct audioterminal) + strlen(name));
	if (!at)
		return -1;
	strcpy(at->name, name);
	at->termt = termt;
	at->next = audioterminals[h];
	audioterminals[h] = at;
	return 0;
}

static int new_genericstrtable(struct genericstrtable *t[HASHSZ], const char *name, unsigned int index)
{
        struct genericstrtable *g;
	unsigned int h = hashnum(index);

        for (g = t[h]; g; g = g->next)
                if (g->num == index)
                        return -1;
        g = my_malloc(sizeof(struct genericstrtable) + strlen(name));
        if (!g)
                return -1;
        strcpy(g->name, name);
        g->num = index;
        g->next = t[h];
        t[h] = g;
        return 0;
}

static int new_hid(const char *name, u_int8_t hidd)
{
	return new_genericstrtable(hiddescriptors, name, hidd);
}

static int new_reporttag(const char *name, u_int8_t rt)
{
	return new_genericstrtable(reports, name, rt);
}

static int new_huts(const char *name, unsigned int data)
{
	return new_genericstrtable(huts, name, data);
}

static int new_hutus(const char *name, unsigned int data)
{
	return new_genericstrtable(hutus, name, data);
}

static int new_langid(const char *name, u_int16_t langid)
{
	return new_genericstrtable(langids, name, langid);
}

static int new_physdes(const char *name, u_int8_t ph)
{
	return new_genericstrtable(physdess, name, ph);
}
static int new_bias(const char *name, u_int8_t b)
{
	return new_genericstrtable(biass, name, b);
}

static int new_countrycode(const char *name, unsigned int countrycode)
{
	return new_genericstrtable(countrycodes, name, countrycode);
}

/* ---------------------------------------------------------------------- */

#define DBG(x)

static void parse(FILE *f)
{
	char buf[512], *cp;
	unsigned int linectr = 0;
	int lastvendor = -1, lastclass = -1, lastsubclass = -1, lasthut=-1, lastlang=-1;
	unsigned int u;

	while (fgets(buf, sizeof(buf), f)) {
		linectr++;
		/* remove line ends */
		if ((cp = strchr(buf, 13)))
			*cp = 0;
		if ((cp = strchr(buf, 10)))
			*cp = 0;
		if (buf[0] == '#' || !buf[0])
			continue;
		cp = buf;
                if (buf[0] == 'P' && buf[1] == 'H' && buf[2] == 'Y' && buf[3] == 'S' && buf[4] == 'D' &&
                    buf[5] == 'E' && buf[6] == 'S' && /*isspace(buf[7])*/ buf[7] == ' ') {
                        cp = buf + 8;
                        while (isspace(*cp))
                                cp++;
                        if (!isxdigit(*cp)) {
                                fprintf(stderr, "Invalid Physdes type at line %u\n", linectr);
                                continue;
                        }
                        u = strtoul(cp, &cp, 16);
                        while (isspace(*cp))
                                cp++;
                        if (!*cp) {
                                fprintf(stderr, "Invalid Physdes type at line %u\n", linectr);
                                continue;
                        }
                        if (new_physdes(cp, u))
                                fprintf(stderr, "Duplicate Physdes  type spec at line %u terminal type %04x %s\n", linectr, u, cp);
                        DBG(printf("line %5u physdes type %02x %s\n", linectr, u, cp));
                        continue;

                }
                if (buf[0] == 'P' && buf[1] == 'H' && buf[2] == 'Y' && /*isspace(buf[3])*/ buf[3] == ' ') {
                        cp = buf + 4;
                        while (isspace(*cp))
                                cp++;
                        if (!isxdigit(*cp)) {
                                fprintf(stderr, "Invalid PHY type at line %u\n", linectr);
                                continue;
                        }
                        u = strtoul(cp, &cp, 16);
                        while (isspace(*cp))
                                cp++;
                        if (!*cp) {
                                fprintf(stderr, "Invalid PHY type at line %u\n", linectr);
                                continue;
                        }
                        if (new_physdes(cp, u))
                                fprintf(stderr, "Duplicate PHY type spec at line %u terminal type %04x %s\n", linectr, u, cp);
                        DBG(printf("line %5u PHY type %02x %s\n", linectr, u, cp));
                        continue;

                }
                if (buf[0] == 'B' && buf[1] == 'I' && buf[2] == 'A' && buf[3] == 'S' && /*isspace(buf[4])*/ buf[4] == ' ') {
                        cp = buf + 5;
                        while (isspace(*cp))
                                cp++;
                        if (!isxdigit(*cp)) {
                                fprintf(stderr, "Invalid BIAS type at line %u\n", linectr);
                                continue;
                        }
                        u = strtoul(cp, &cp, 16);
                        while (isspace(*cp))
                                cp++;
                        if (!*cp) {
                                fprintf(stderr, "Invalid BIAS type at line %u\n", linectr);
                                continue;
                        }
                        if (new_bias(cp, u))
                                fprintf(stderr, "Duplicate BIAS  type spec at line %u terminal type %04x %s\n", linectr, u, cp);
                        DBG(printf("line %5u BIAS type %02x %s\n", linectr, u, cp));
                        continue;

                }
                if (buf[0] == 'L' && /*isspace(buf[1])*/ buf[1] == ' ') {
                        cp =  buf+2;
                        while (isspace(*cp))
                                cp++;
                        if (!isxdigit(*cp)) {
                                fprintf(stderr, "Invalid LANGID spec at line %u\n", linectr);
                                continue;
                        }
                        u = strtoul(cp, &cp, 16);
                        while (isspace(*cp))
                                cp++;
                        if (!*cp) {
                                fprintf(stderr, "Invalid LANGID spec at line %u\n", linectr);
                                continue;
                        }
                        if (new_langid(cp, u))
                                fprintf(stderr, "Duplicate LANGID spec at line %u language-id %04x %s\n", linectr, u, cp);
                        DBG(printf("line %5u LANGID %02x %s\n", linectr, u, cp));
                        lasthut = lastclass = lastvendor = lastsubclass = -1;
                        lastlang = u;
                        continue;
                }
		if (buf[0] == 'C' && /*isspace(buf[1])*/ buf[1] == ' ') {
			/* class spec */
			cp = buf+2;
			while (isspace(*cp))
				cp++;
			if (!isxdigit(*cp)) {
				fprintf(stderr, "Invalid class spec at line %u\n", linectr);
				continue;
			}
			u = strtoul(cp, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				fprintf(stderr, "Invalid class spec at line %u\n", linectr);
				continue;
			}
			if (new_class(cp, u))
				fprintf(stderr, "Duplicate class spec at line %u class %04x %s\n", linectr, u, cp);
			DBG(printf("line %5u class %02x %s\n", linectr, u, cp));
			lasthut = lastlang = lastvendor = lastsubclass = -1;
			lastclass = u;
			continue;
		}
		if (buf[0] == 'A' && buf[1] == 'T' && isspace(buf[2])) {
			/* audio terminal type spec */
			cp = buf+3;
			while (isspace(*cp))
				cp++;
			if (!isxdigit(*cp)) {
				fprintf(stderr, "Invalid audio terminal type at line %u\n", linectr);
				continue;
			}
			u = strtoul(cp, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				fprintf(stderr, "Invalid audio terminal type at line %u\n", linectr);
				continue;
			}
			if (new_audioterminal(cp, u))
				fprintf(stderr, "Duplicate audio terminal type spec at line %u terminal type %04x %s\n", linectr, u, cp);
			DBG(printf("line %5u audio terminal type %02x %s\n", linectr, u, cp));
			continue;
		}
		if (buf[0] == 'H' && buf[1] == 'C' && buf[2] == 'C' && isspace(buf[3])) {
			/* HID Descriptor bCountryCode */
                        cp =  buf+3;
                        while (isspace(*cp))
                                cp++;
                        if (!isxdigit(*cp)) {
                                fprintf(stderr, "Invalid HID country code at line %u\n", linectr);
                                continue;
                        }
                        u = strtoul(cp, &cp, 10);
                        while (isspace(*cp))
                                cp++;
                        if (!*cp) {
                                fprintf(stderr, "Invalid HID country code at line %u\n", linectr);
                                continue;
                        }
                        if (new_countrycode(cp, u))
                                fprintf(stderr, "Duplicate HID country code at line %u country %02u %s\n", linectr, u, cp);
                        DBG(printf("line %5u keyboard country code %02u %s\n", linectr, u, cp));
                        continue;
		}
		if (isxdigit(*cp)) {
			/* vendor */
			u = strtoul(cp, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				fprintf(stderr, "Invalid vendor spec at line %u\n", linectr);
				continue;
			}
			if (new_vendor(cp, u))
				fprintf(stderr, "Duplicate vendor spec at line %u vendor %04x %s\n", linectr, u, cp);
			DBG(printf("line %5u vendor %04x %s\n", linectr, u, cp));
			lastvendor = u;
			lasthut = lastlang = lastclass = lastsubclass = -1;
			continue;
		}
		if (buf[0] == '\t' && isxdigit(buf[1])) {
			/* product or subclass spec */
			u = strtoul(buf+1, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				fprintf(stderr, "Invalid product/subclass spec at line %u\n", linectr);
				continue;
			}
			if (lastvendor != -1) {
				if (new_product(cp, lastvendor, u))
					fprintf(stderr, "Duplicate product spec at line %u product %04x:%04x %s\n", linectr, lastvendor, u, cp);
				DBG(printf("line %5u product %04x:%04x %s\n", linectr, lastvendor, u, cp));
				continue;
			}
			if (lastclass != -1) {
				if (new_subclass(cp, lastclass, u))
					fprintf(stderr, "Duplicate subclass spec at line %u class %02x:%02x %s\n", linectr, lastclass, u, cp);
				DBG(printf("line %5u subclass %02x:%02x %s\n", linectr, lastclass, u, cp));
				lastsubclass = u;
				continue;
			}
			if (lasthut != -1) {
				if (new_hutus(cp, (lasthut << 16)+u))
					fprintf(stderr, "Duplicate HUT Usage Spec at line %u\n", linectr);
				continue;
			}
			if (lastlang != -1) {
                                if (new_langid(cp, lastlang+(u<<10)))
                                        fprintf(stderr, "Duplicate LANGID Usage Spec at line %u\n", linectr);
                                continue;
                        }
			fprintf(stderr, "Product/Subclass spec without prior Vendor/Class spec at line %u\n", linectr);
			continue;
		}
		if (buf[0] == '\t' && buf[1] == '\t' && isxdigit(buf[2])) {
			/* protocol spec */
			u = strtoul(buf+2, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				fprintf(stderr, "Invalid protocol spec at line %u\n", linectr);
				continue;
			}
			if (lastclass != -1 && lastsubclass != -1) {
				if (new_protocol(cp, lastclass, lastsubclass, u))
					fprintf(stderr, "Duplicate protocol spec at line %u class %02x:%02x:%02x %s\n", linectr, lastclass, lastsubclass, u, cp);
				DBG(printf("line %5u protocol %02x:%02x:%02x %s\n", linectr, lastclass, lastsubclass, u, cp));
				continue;
			}
			fprintf(stderr, "Protocol spec without prior Class and Subclass spec at line %u\n", linectr);
			continue;
		}
		if (buf[0] == 'H' && buf[1] == 'I' && buf[2] == 'D' && /*isspace(buf[3])*/ buf[3] == ' ') {
			cp = buf + 4;
                        while (isspace(*cp))
                                cp++;
                        if (!isxdigit(*cp)) {
                                fprintf(stderr, "Invalid HID type at line %u\n", linectr);
                                continue;
                        }
                        u = strtoul(cp, &cp, 16);
                        while (isspace(*cp))
                                cp++;
                        if (!*cp) {
                                fprintf(stderr, "Invalid HID type at line %u\n", linectr);
                                continue;
                        }
                        if (new_hid(cp, u))
                                fprintf(stderr, "Duplicate HID type spec at line %u terminal type %04x %s\n", linectr, u, cp);
                        DBG(printf("line %5u HID type %02x %s\n", linectr, u, cp));
                        continue;

		}
                if (buf[0] == 'H' && buf[1] == 'U' && buf[2] == 'T' && /*isspace(buf[3])*/ buf[3] == ' ') {
                        cp = buf + 4;
                        while (isspace(*cp))
                                cp++;
                        if (!isxdigit(*cp)) {
                                fprintf(stderr, "Invalid HUT type at line %u\n", linectr);
                                continue;
                        }
                        u = strtoul(cp, &cp, 16);
                        while (isspace(*cp))
                                cp++;
                        if (!*cp) {
                                fprintf(stderr, "Invalid HUT type at line %u\n", linectr);
                                continue;
                        }
                        if (new_huts(cp, u))
                                fprintf(stderr, "Duplicate HUT type spec at line %u terminal type %04x %s\n", linectr, u, cp);
			lastlang = lastclass = lastvendor = lastsubclass = -1;
			lasthut = u;
                        DBG(printf("line %5u HUT type %02x %s\n", linectr, u, cp));
                        continue;

                }
                if (buf[0] == 'R' && buf[1] == ' ') {
                        cp = buf + 2;
                        while (isspace(*cp))
                                cp++;
                        if (!isxdigit(*cp)) {
                                fprintf(stderr, "Invalid Report type at line %u\n", linectr);
                                continue;
                        }
                        u = strtoul(cp, &cp, 16);
                        while (isspace(*cp))
                                cp++;
                        if (!*cp) {
                                fprintf(stderr, "Invalid Report type at line %u\n", linectr);
                                continue;
                        }
                        if (new_reporttag(cp, u))
                                fprintf(stderr, "Duplicate Report type spec at line %u terminal type %04x %s\n", linectr, u, cp);
                        DBG(printf("line %5u Report type %02x %s\n", linectr, u, cp));
                        continue;

                }
                if (buf[0] == 'V' && buf[1] == 'T') {
			/* add here */
			continue;
		}
		fprintf(stderr, "Unknown line at line %u\n", linectr);
	}
}

/* ---------------------------------------------------------------------- */

int names_init(char *n)
{
	FILE *f;

	if (!(f = fopen(n, "r"))) {
		return errno;
	}
	parse(f);
	fclose(f);
	return 0;
}
